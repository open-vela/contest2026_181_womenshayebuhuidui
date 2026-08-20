# 20. 配对时 bluetoothd 崩在 unqlite pager 上：定位与修复

> 日期：2026-08-20
> 影响：产品镜像同样中招；手机侧已显示「已保存」，但手表侧 bond 没落库

## 现象

HyperOS 手机（A4:D1:FE:B3:CC:A4）第一次配对，握手本身是走通的：

```
[SAL] SSP passkey_confirm: 577218, auto-accepting (headless)
[pan] security level=3
[l2cap_br] conf_rsp SUCCESS
```

手机弹出并显示「已保存」。紧接着 bluetoothd 在把 bond 写进
`/data/misc/bt/bt_storage.db` 的时候挂掉，任务是 `bt_service_6`（pid 13,
group 6），线程是 libuv 线程池的 `worker`。

## 硬件异常帧的精确解码

pandbg 镜像开了 `DEBUG_BUSFAULT` + `ARCH_STACKDUMP`，栈里 0x60071548 处的异常
帧是：

| 寄存器 | 值 | 含义 |
|--------|-----|------|
| R0  | 0x2007ffcc | `pPager->pfd`，unqlite 的文件句柄 |
| R1  | 4          | `EXCLUSIVE_LOCK` |
| R3  | 0x00011906 | `pfd->pMethods`——**垃圾值** |
| LR  | 0x121fab45 | `pager_lock_db+0x31` |
| PC  | 0x121f831a | `unqliteOsLock` 里的 `ldr r3,[r3,#28]` |
| BFAR| 0x00011922 | = 0x11906 + 0x1C，与上面那条 `ldr` 完全对上 |

所以句柄指针本身是好的，被改坏的是句柄里的第一个字（虚函数表指针）。
SP 只用了 8112 字节里的约 1040 字节，不是爆栈。

## 被改写的那 48 个字节是谁

unqlite 分配文件句柄的语句是
`SyMemBackendAlloc(pAlloc, sizeof(unqlite_file) + szOsFile)`，即
4 + `sizeof(unixFile)`(44) = **48 字节**，且分配后清零。

`0x2007ffcc + 48 = 0x2007fffc`，而 SRAM 堆的上界正是 0x20080000
（`vendor/sifli/chips/sf32lb52/sifli_allocateheap.c`：SRAM 0x20000000..0x20080000，
`_ebss` = 0x200712c0，PSRAM 8 MB 由 `kumm_addregion` 追加）。也就是说这块句柄
**恰好是 SRAM 堆最顶上的 48 字节**——开机时第一个分配出来的块。

`CONFIG_DEBUG_MM=y` / `CONFIG_DEBUG_MM_ERROR=y` 本来就是开着的，整个过程没有
任何 mm 断言，说明 0x2007ffc4 处的 mm 节点头是完好的。头没坏、payload 偏移 0
的 4 字节被换掉 —— 这是**块被释放后又被别人当新块用**的特征，不是邻居写越界。

## 走过的一条死路：unqlite 自己没有加锁

一开始的假设是 unqlite 在这个 flat build 上没有串行化。逐段读完之后这个假设
不成立，记下来免得下次再走：

- `sUnqlMPGlobal` 是 **task-group TLS**：`#define sUnqlMPGlobal (*getUnqlMPGlobal())`
  （unqlite.c:3596-3600），实现在 60297-60336，用 `pthread_once` +
  `task_tls_alloc`/`task_tls_get_value`。NuttX flat build 下同一 task group 的
  所有 pthread 共享它，也就是 bluetoothd 的所有线程共享。
- `unqliteCoreInitialize` 装的是内建 mutex 子系统，把
  `nThreadingLevel` 置成 `UNQLITE_THREAD_LEVEL_MULTI`（unqlite.c:3770-3772）。
- `__UNIXES__` 有定义，取到的是 `sPthreadMutexMethods`，真正的递归 pthread 互斥。
- `unqlite_open` 建 `pDb->pMutex`（4364-4366），
  `unqlite_kv_store/kv_fetch/kv_delete/commit` 四个入口全都拿这把锁。崩掉那条
  线程自己的栈里就有 `UnixMutexLeave`/`pthread_mutex_unlock` 的帧。

游标 API（`unqlite_kv_cursor_*`）拿的是 `pCursor` 而不是 `pDb`，确实没保护，
但 storage.c 一次都没用过。构建里那个 `-DUNQLITE_LOCK_BY_SEM` 在 unqlite.c 里
从未被引用，是个死开关。

结论：**unqlite 这一侧是线程安全的**，问题在调用方。

## 真正的破坏源：异步 uv_db 路径

`apps/frameworks/system/utils/uv/src/uv_db.c`（不在授权改动范围内，只读诊断）
有三处凑在一起就会踩内存：

1. `uv__queue_insert_tail(&handle->queue, &req->node)` 在 `uv_queue_work`
   **之前**执行，而 `error:` 标签只 `free(req)`。一旦 `uv_queue_work` 失败，
   一个已经释放的 request 还挂在链表里，之后的 `uv__queue_remove()` 就往
   已释放内存里写指针。
2. uv_db 的 request 队列没有锁，却是「谁调用谁插入」。
3. `bt_storage_load_adapter_info()` / `bt_storage_save_adapter_info()` 是从
   `sal_adapter_le_interface.c:1466` 进来的，也就是 **zblue 协议栈线程**，不是
   loop 线程。`uv_queue_work` 在非 loop 线程上调用本身就不是线程安全的
   （`uv__req_register` 动的是 `loop->active_reqs`）。

再叠上旧 storage.c 的所有权约定：调用方的 buffer 要活到线程池那边用完，commit
又落在第三个线程上。开机时保存 AdapterInfo 恰好是从 loop 线程发起的，所以没
出事；bond 保存是从协议栈线程发起的，就炸了。

## 修法：BT 存储路径整条改成同步

`frameworks/connectivity/bluetooth/service/common/storage.c`
（`CONFIG_BLUETOOTH_STORAGE_PROPERTY_SUPPORT` 没开，CMakeLists.txt:220-224 编的
就是这一份；`service/common/bt_storage.c` 是一份几乎一样的**未编译**孪生文件，
故意没动）：

- 加一把 `static pthread_mutex_t storage_lock`，每个 set/get 在**调用线程上**
  同步执行，commit 就地做完，线程池完全不参与。
- 删掉 `key_set_callback()`。
- buffer 所有权改成「调用方永远负责 free」：`bt_storage_save_adapter_info()` 和
  `bt_storage_save_remote_device()` 无论成败都自己 `free`。
- get 拿不到 key 时返回负值且**不**调回调，由调用方自己报告 —— 三个调用点
  （`adapter_service.c:1413` / `:1459` / `:1464`）本来就写了
  `if (ret < 0) xxx_loaded(NULL, 0, 0);`，契约一致。
- 加载回调在锁**外**执行：它们会重入 adapter 状态机，而状态机又会写 key。
- 加两行 `syslog`（`[bt_storage] set/get …`），因为这个配置下 `BT_LOG*` 是空宏。

## 硬件验证

镜像 pandbg（md5 8e286300…）：

| 检查项 | 结果 |
|--------|------|
| 开机 `bt_storage.db ready (synchronous)` | t=9.4 s |
| `get AdapterInfo` | ret=0 len=104 |
| `get BleBonded` / `WhiteList` / `BtBonded` | ret=-6（键不存在，走调用方兜底） |
| `set AdapterInfo`（开机覆盖 scanmode 后落库） | ret=0，无故障 |
| 连续 13 次 `set scanmode` → 13 次同步 set+commit | 全部 ret=0，0 条异常行 |
| 堆 | 1,095,824 / 8,433,216，压测前后无增长 |
| `bt_storage.db` | 稳定 12288 字节，无残留 journal |
| `get bonded 1` / `get bonded 0` | cnt:0 |

`get BtBonded ret=-6` 直接证明了**上次那笔 bond 确实没写进去**——手机侧的
「已保存」是单方面的。所以重测前手机必须先「忘记该设备」，否则手机拿着
link key 而手表没有，重连只会失败。

顺手验掉一条一直没机会做的检查：库里存 `scan_mode=1`，重启后
`get scanmode` 回 `Scan Mode:2`，`CONFIG_BLUETOOTH_BOOT_SCAN_MODE=2` 的覆盖
按预期生效，`Adapter State: 4`。

另外拿一个不存在的地址试了 PANU 连接失败路径，干净收敛、没有崩：

```
[pan] worker: create_br... → state=connecting → [pan] ACL connect err 4
→ [pan] worker: ACL up timeout → state=disconnected
```

## 同一轮里顺手清掉的两件事

**1. HCI 逐帧 trace 从产品镜像里摘掉。** 这是产品级验收上的硬伤：

- `vendor/sifli/chips/sf32lb52/sf32lb52_bth4.c` 里 `SF32LB52_BT_TRACE` 原本被
  写死成 `1`，收发两个方向各有一条「40 字节十六进制」的 syslog，**每个 UART
  chunk 一条**。
- `service/stacks/zephyr/hci_h4.c` 里还有一条 `syslog(6, "[h4] frame …")`，
  **每个解出来的 HCI 帧一条**。

BNEP 跑数据时这等于每个 ACL 包一次控制台写，1 Mbaud 的串口会直接把吞吐压死，
还可能引发 ACL RX 积压。现在两者都挂到 Kconfig 上：

| 符号 | 位置 | 产品 | pandbg |
|------|------|------|--------|
| `SF32LB52_BT_TRACE` | vendor/sifli/chips/sf32lb52/Kconfig | n | y |
| `SF32LB52_BT_TRACE_ACL_FULL` | 同上 | n | y |
| `BLUETOOTH_HCI_FRAME_TRACE` | frameworks/connectivity/bluetooth/Kconfig | n | y |

两条丢帧日志（`no buf` / `tailroom`）不受开关控制，一直留着——它们代表协议栈
真的丢了东西，而这个配置下 `BT_LOG*` 看不见。
pandbg 的 defconfig 由 `docs_ble/tools/mk_pandbg_config.sh` 生成，新符号加在
脚本里，重新跑一次即可（不要用 `savedefconfig`，它会回写源 defconfig 并抹掉注释）。

产品镜像 flash 因此少了约 900 字节（6,216,492 → 6,215,592），SRAM 不变。

**2. `framework/common/bt_list.c` 的返回类型错误。** 之前把 `assert(list)` 换成
`if (!list) { syslog(...); return; }` 时，有六个函数不是 `void`：
`bt_list_is_empty` / `bt_list_length` / `bt_list_head` / `bt_list_tail` /
`bt_list_next` / `bt_list_find`。裸 `return;` 在这里返回的是垃圾值（靠
`-Wno-error` 才编过去）。改成分别返回 `true` / `0` / `NULL`。

## 本轮镜像

| 镜像 | flash | SRAM | md5 |
|------|-------|------|-----|
| ai_agent（产品） | 6,215,592 B / 37.05% | 463,456 B / 88.40% | b0240aef8c488601e147c67025f4d762 |
| ai_agent_pandbg  | 6,221,040 B / 37.08% | 463,584 B / 88.42% | 8e286300a8fe2b4d3efd297244e5d05c |

比上一轮各 +32 字节 SRAM，就是那把 `pthread_mutex_t`。

## 关于 BT 框架代码的版本管理

`frameworks/.gitignore` 第 15 行是 `/*/`，整个 `connectivity/` 树都被外层仓库
忽略，所以这些改动**不在 frameworks 仓的索引里**。实际的版本历史在
`frameworks/connectivity/bluetooth/` 自己的 git 仓（本轮之前最新一条是
`24d23bcd fix(pan): make the DHCP worker exit cleanly …`）。
改这棵树之后要在那个仓里按路径提交，不要指望 `git -C frameworks status` 看得见。

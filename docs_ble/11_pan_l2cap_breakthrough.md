# 11 PAN BREDR L2CAP 突破实录（2026-08-15 夜间会话）

## 目标回顾

在不破坏既有 BLE GATT 代理路径的前提下，打通「蓝牙代理上网」的第二条路：
**BREDR PAN（BNEP over L2CAP PSM 0x000F）**，由板子作为 PANU(2) 连接手机 NAP(1) 实现上网。

今日成果：**L2CAP 连接已被手机接受（CONN_RSP result=0x0000 SUCCESS）**，
阻塞点从「完全无法连接」推进到「配置协商（CONFIG）阶段被拒」。

## 完整测试链路状态机（今日打通的部分）

```
pan connect <phone> 1 2
  -> worker 线程 bt_conn_create_br()        OK 全新 ACL 建立
  -> [pan] ACL up                            OK
  -> bt_conn_set_security(L2) ret=0          OK 加密请求发出
  -> [pan] security level=2                  OK 加密完成
  -> bt_l2cap_chan_connect(PSM 0x000F) ret=0 OK L2CAP 请求发出
  -> 手机回 CONN_RSP: dcid=0x0072 scid=0x0040 result=0x0000 SUCCESS OK
  -> 手机发 CONF_REQ（配置请求）              OK 收到
  -> 我方 l2cap_br_conf() 发 CONF_REQ        OK 发出
  -> 手机回 CONF_RSP result=0x0001 UNACCEPTABLE_PARAMS 【当前阻塞】
  -> zblue 对非 SUCCESS 结果执行 disconnect -> ACL 断（reason 0x13）
```

## 今日修复清单（按时间顺序）

### 1. worker 复用 ACL 的 acl_ready 等待 bug
- 现象：4s 等待版测试中 [pan] worker: ACL up timeout，但 ACL 实际已 up。
- 根因：配对时 connected 回调已触发过 g_pan_acl_ready=true；worker 把标志重置为
  false 后再等，永远等不到第二次回调。
- 修复：复用 ACL 且 bt_conn_get_info 显示 CONNECTED 时直接跳过等待。
- 提交：af54c8db（bluetooth 仓库 bletest 分支）

### 2. 测试脚本回 12 秒
- 配对后等 12s，让 Android 断开配对 ACL（reason 19），再做全新连接。
- 修改：auto_pan_test.py 第 [6] 步 sleep 12。

### 3. 【关键】pan_conn_t.chan 类型错误 -> L2CAP EEXIST
- 现象：[pan] L2CAP connect ret=-17 (EEXIST)，第一次连接就失败。
- 根因：zblue 的 BR/EDR L2CAP 用宏
  BR_CHAN(_ch) = CONTAINER_OF(_ch, struct bt_l2cap_br_chan, chan)
  访问 psm/rx/tx/state 等字段。我们的 pan_conn_t 里声明的是
  struct bt_l2cap_chan chan（比 bt_l2cap_br_chan 小得多），
  BR_CHAN() 越界读取 calloc 堆内存 -> psm 读到垃圾值 -> 判为「已连接过」-> EEXIST。
  同时 zblue 写入 rx/tx/state 也会越界写坏内存。
- 修复：pan_conn_t.chan 改为 struct bt_l2cap_br_chan（与 rfcomm/avdtp/avctp 一致），
  所有 &conn->chan 改为 &conn->chan.chan，回调里 CONTAINER_OF 用
  CONTAINER_OF(BT_L2CAP_BR_CHAN(chan), pan_conn_t, chan)。
- 提交：7d7ae176
- 效果：L2CAP connect ret=0，请求成功发出。

### 4. worker ACL up 等待 3s -> 10s 轮询
- 现象：一次测试中 create_br 后 ACL up 回调超过 3s 才到，worker 提前放弃。
- 修复：轮询 g_pan_acl_ready + bt_conn_get_info 状态，最多 10s。
- 提交：0c6d033e

### 5. 【最新】br_chan rx.mtu 未初始化 -> CONFIG 阶段 MTU=0 被拒
- 现象：手机接受连接（CONN_RSP SUCCESS）后，我方发 CONF_REQ，
  手机回 CONF_RSP result=0x0001 (UNACCEPTABLE_PARAMS)，随后断开。
- 根因：pan_conn_t 是 calloc，br_chan->rx.mtu=0。zblue l2cap_br_conf() 里
  if (rx.mtu != L2CAP_BR_DEFAULT_MTU) 加 MTU option，
  于是发了一个 MTU=0 的配置 option -> 手机拒绝。
- 修复：bt_sal_pan_connect 里初始化
  conn->chan.rx.mtu = L2CAP_BR_DEFAULT_MTU (672) 和
  conn->chan.required_sec_level = BT_SECURITY_L2。
- 提交：317d80cb
- 状态：已编译通过，已烧录（最后一次烧录成功于 flash retry 第 1 次）。

### 6. LCPU bth4 打印扩展（调试辅助）
- vendor/sifli/chips/sf32lb52/sf32lb52_bth4.c：RX/TX 从 8 字节扩展到 40 字节，
  才能看到完整 L2CAP 信令（CONN_RSP/CONF_RSP 的 result 字段）。
- 提交：d5bd364（RX）、95811bc（TX）

## 关键证据（/tmp/pan_raw.log 实抓）

- 手机 CONN_RSP（len=42 的 recv 前半）：
  03 07 08 00 72 00 40 00 00 00 00 00
  = CONN_RSP ident=0x07 len=8 dcid=0x0072 scid=0x0040 result=0x0000(SUCCESS) status=0
- 手机 CONF_REQ（同包后半）：
  04 04 08 00 40 00 00 00 01 02
  = CONF_REQ ident=0x04 len=8 dcid=0x0040 flags=0 option: 01 02(MTU opt, 值被截断)
- 手机 CONF_RSP：
  05 08 06 00 40 00 00 00 01 00
  = CONF_RSP ident=0x08 len=6 scid=0x0040 flags=0 result=0x0001(UNACCEPTABLE_PARAMS)
- 随后 zblue 主动断开（reason 0x13 由手机侧确认）。

## 当前阻塞点分析（明天从这里继续）

手机拒绝了我们的 CONF_REQ。可能原因（按嫌疑排序）：
1. CONF_REQ 内容仍有问题：tx 40 字节打印已就绪，下次测试直接抓我方
   CONF_REQ 的完整字节（code/ident/len/dcid/flags/options），确认 dcid 与
   手机 CONN_RSP 给的 0x0072 一致、flags=0、无异常 option。
2. 手机要求先收到带合法 MTU 的配置：Android BNEP 有时要求对端 MTU >= 672；
   确认我方 rx.mtu 初始化生效后 l2cap_br_conf 不再带 MTU option（rx.mtu==672
   时它不加 option，理论上合法；但也可尝试显式带 MTU=672）。
3. zblue l2cap_br 与 Android 的配置交互细节：CONF_REQ 的 ident/顺序、
   或者手机要求双方都完成配置后才算 connected（zblue 在 CONF_RSP 非 SUCCESS
   时直接 disconnect，这是 zblue 的简化行为）。
4. 若 CONFIG 始终被拒，备选：直接在 zblue l2cap_br_conf 里强制发 MTU=672 option
   （改动 l2cap_br.c 的 conf 逻辑），或查 Android 是否要求先 SDP 查询 PAN 服务。

## 烧录经验（今天验证）

- 可靠路径：logs/flash_rts.py（后台 sftool --before no_reset + RTS 断电 500ms 重启）。
  失败率较高，建议 for 循环重试 2-3 次，成功标志 sftool returncode: 0。
- 备用：replug_flash.py（等用户拔插，端口出现瞬间 no_reset_no_sync 烧录）。
- 烧录 99% 处 timeout waiting for RAM command response 时固件往往已写入，
  板子重启后能正常进 NuttX（可用 reboot 命令验证）。
- 烧录失败后板子可能停在 bootloader msh>（RT-Thread shell），发 reboot 可进 NuttX。

## 明天待办

1. 拔插 USB -> 烧录最新固件（含 rx.mtu 修复 + tx 40 字节打印）。
2. 手机保持「蓝牙网络共享」开启（今天已确认必须开启，否则手机直接断开 ACL）。
3. 跑 auto_pan_test.py，抓我方 CONF_REQ 完整字节，分析被拒原因。
4. 按上面嫌疑 1-4 逐项实验，目标：CONFIG 通过 -> BNEP Setup -> state:2 CONNECTED。
5. 成功后继续：BNEP 数据面（FRAME_ETH）-> TAP/bt-pan -> 路由 -> 真正上网。

## 提交状态（全部在 bletest 分支）

- frameworks/connectivity/bluetooth：bletest 分支，本会话新增 4 个提交：
  af54c8db（worker acl wait）/ 7d7ae176（br_chan 类型）/ 0c6d033e（10s 轮询）/
  317d80cb（rx.mtu 初始化）
- vendor/sifli：d5bd364（RX 打印 40B）、95811bc（TX 打印 40B）
- 本文档：docs_ble/11_pan_l2cap_breakthrough.md
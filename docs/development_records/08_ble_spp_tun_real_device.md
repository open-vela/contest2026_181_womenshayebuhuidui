# SF32LB52 真机 BLE SPP+TUN 蓝牙代理链路验证记录

> 本文记录在 SF32LB52 开发板（sf32lb52_devkit_lcd）真机上打通「蓝牙 SPP + TUN 网络代理」设备端链路的完整过程：架构、配置、验证结果与六层根因排障。验证日期 2026-08-11，验证脚本 [`../../logs/hw_ble_test.py`](../../logs/hw_ble_test.py)，日志 [`../../logs/hw_ble_test.log`](../../logs/hw_ble_test.log)。

## 1. 背景与目标

- SF32LB52 为纯蓝牙芯片（BT/BLE 5.3），**无 WiFi**，联网只能走手机 App 蓝牙代理（路线 B）。
- QEMU 预演（路线 A）已完成（见 [`07_qemu_aiagent_llm_validation.md`](07_qemu_aiagent_llm_validation.md)）。
- 本次目标：真机设备端验证 `ai_agent → bluetoothd(SOCKET_IPC) → zblue 栈 → H4 控制器 → SPP(SCN 3) → 手机侧 TUN` 链路全部就绪。

## 2. 最终链路架构 [已验证]

```
手机 App(虚拟TUN 192.168.55.1)
   │ 蓝牙 SPP (SCN 3)
   ▼
SF32LB52 蓝牙控制器
   │ HCI (H4, /dev/ttyHCI0, sf32lb52_bth4.c)
   ▼
zblue 蓝牙栈 (BREDR, bt_sal_enable)
   │ SAL
   ▼
bluetoothd 守护进程 (service_loop 事件循环线程 + service_loop_join 保活)
   │ SOCKET_IPC Unix socket (/var/run/bt:bluetooth)
   ▼
ai_agent ble_net.c:
   TUN(bt-net, 192.168.55.2/24) ↔ uv_poll → euv_pipe → SPP(SCN 3) ↔ 手机
```

## 3. 验证环境

| 项 | 值 |
|---|---|
| 目标机 | SF32LB52-DevKit-LCD（16MB Flash / 8MB PSRAM） |
| 串口 | `/dev/ttyACM0`，1000000 baud（CH34x 桥接） |
| 固件 | `out/sf32lb52_aiagent_ble/nuttx.bin`（约 4.6MB，cmake + Unix Makefiles 构建） |
| 构建命令 | `cmake -B out/sf32lb52_aiagent_ble -S nuttx -DBOARD_CONFIG=../contest2026_181_womenshayebuhuidui/board/contest_board/configs/ai_agent`，需 PATH 含 `prebuilts/gcc/linux-x86_64/arm-none-eabi/bin` 与 `prebuilts/build-tools/linux-x86_64/bin`（genromfs） |
| 烧录命令 | `./flash_only.sh --port /dev/ttyACM0 -i out/sf32lb52_aiagent_ble/nuttx.bin`（**必须 `-i` 指定镜像**，默认值是 vendor nsh 旧固件） |

## 4. 关键配置（defconfig 增量）

```text
# TUN + RPMSG
CONFIG_NET_TUN=y
CONFIG_NET_TUN_PKTSIZE=1518
CONFIG_NET_RPMSG=y
CONFIG_NET_RPMSG_RXBUF_SIZE=4096
# Unix domain socket（蓝牙框架 SOCKET_IPC 基础）
CONFIG_NET_LOCAL=y
# BT H4 UART 伪设备（/dev/ttyHCI0，sf32lb52_bth4 HCI 传输）
CONFIG_UART_BTH4=y
# 蓝牙框架（SOCKET_IPC + bluetoothd 独立服务）
CONFIG_LIBUV=y
CONFIG_LIBUV_EXTENSION=y
CONFIG_BLUETOOTH=y
CONFIG_BLUETOOTH_FRAMEWORK=y
CONFIG_BLUETOOTH_FRAMEWORK_SOCKET_IPC=y
CONFIG_BLUETOOTH_SERVICE=y
CONFIG_BLUETOOTH_SERVER=y
CONFIG_BLUETOOTH_STACK_BREDR_ZBLUE=y
CONFIG_BLUETOOTH_BLE_SUPPORT=y
CONFIG_BLUETOOTH_STACK_LE_ZBLUE=y
# CONFIG_BLUETOOTH_GATT is not set
CONFIG_BLUETOOTH_SPP=y
CONFIG_BT_RFCOMM=y
# CONFIG_BLUETOOTH_LOG is not set
CONFIG_AI_AGENT_LVGL_UI=y
CONFIG_AI_AGENT_BLE_NET=y
CONFIG_AI_AGENT_NET_RPMSG=y
```

## 5. 验证结果 [已验证]

串口日志序列（`logs/hw_ble_test.log`）：

```text
nsh> bluetoothd &
bluetoothd [10:100]
bluetoothd main 35
/data/misc/bt folder create: 0
  (ps: bluetoothd 存活, Waiting Semaphore)

vela> ai_agent
[netmgr] O74I: Initializing RPMSG/TUN network
[ble_net] Initializing BLE network channel
[ble_net] TUN device bt-net created
[ble_net] Enabling BT adapter (state=0)
[ble_net] BT adapter ON
[ble_net] SPP server started on SCN 3
[ble_net] BLE network channel ready (MTU=1518)
[netmgr] BLE SPP+TUN proxy channel started

vela> net_status
Network connected: no
State: DISCONNECTED        ← 无手机 App 连接，符合预期
```

- 代码改动：`packages/ai_agent/src/infra/ble_net.c`（SPP 前 enable adapter 并等待 ON）、`network_manager.c`（BLE_NET 接线）、`heartbeat.c`（可中断等待）、`apps/frameworks/connectivity/bluetooth/service/src/main.c`（mkdir /var(/run) + 退出码诊断）、`service/ipc/bluetooth_ipc.c` + `common/service_loop.c/h`（service_loop_join）。

## 6. 六层根因排障记录

| # | 现象 | 根因 | 修复 |
|---|---|---|---|
| 1 | 串口命令无回显无执行 | NSH 行结束符只认 `\n`，脚本发 `\r` 导致命令积压 | 脚本统一发 `\r\n` |
| 2 | bluetoothd 启动后静默退出，ai_agent 报 `Failed to get BT instance`(-19) | `CONFIG_NET_LOCAL` 未开，`socket(PF_LOCAL)` 失败，init_queue 失败触发 set_stop 退出（BT_LOGE 被吞） | defconfig 加 `CONFIG_NET_LOCAL=y` |
| 3 | 同上 | `/var`、`/var/run` 目录不存在（NuttX local socket 用 `nx_mkfifo("/var/run/bt:bluetoothCS/SC")`，`mkdir` 不递归） | bluetoothd main 依次 `mkdir("/var")` + `mkdir("/var/run")` |
| 4 | bluetoothd 退出码 ret=0 但仍死亡 | socket IPC 模式 `bluetooth_ipc_join_thread_pool()` 为空函数，main 线程跑完即退，事件循环线程随进程被杀 | 新增 `service_loop_join()`（等待事件循环线程），socket 分支 join 时调用 |
| 5 | `Failed to register SPP app`(-12) | 未调用 `bt_adapter_enable`；SPP 是 BREDR profile，需栈 ON 后 `spp_startup` 才置 started=1 | `spp_server_start` 中 enable adapter 并轮询等待 `BT_ADAPTER_STATE_ON`（30s 超时） |
| 6 | `BT adapter enable timeout (state=0)` | `CONFIG_UART_BTH4` 未开，`sf32lb52_bt_initialize()`（注册 `/dev/ttyHCI0`）被 `#ifdef CONFIG_UART_BTH4` 排除，zblue `bt_enable` 找不到控制器 | defconfig 加 `CONFIG_UART_BTH4=y`（依赖 `DRIVERS_BLUETOOTH` 已满足） |

### 排障方法论

- **关闭 `CONFIG_BLUETOOTH_LOG` 的代价**：框架内 `BT_LOGE/BT_LOGW` 全部被编译掉，错误静默。替代方案：在 bluetoothd main 出口无条件 `syslog(LOG_ERR, "bluetoothd exit: ret=%d", ret)`，并在各关键阶段加 syslog，用退出码区分「正常返回（事件循环线程被创建后 main 直接 return）」与「init 失败」。
- **验证脚本 marker 检测**：按 `\n` split 出的行做匹配（split 会清空累积缓冲区，直接 `in buf` 永远失败）。
- **quit 崩溃坑**：`heartbeat_thread` 用 `sleep(1800)` 长睡，进程退出时被强制取消导致堆断言、系统挂死；已改为 `pthread_cond_timedwait` + stop 时 signal 唤醒，线程自然退出。

## 7. 验证工具

- `logs/hw_ble_test.py`：非交互串口验证脚本。流程：等 NSH → `bluetoothd &` → ps 查存活 → `ai_agent`（等待 `BLE SPP+TUN proxy channel started`）→ `net_status` → 保持系统运行（**不发 quit**）。
- 板子挂死恢复：**完全断电重插**（拔 USB 等 10 秒再插），立即烧录（boot ROM 窗口约 10-20 秒）；RTS/DTR 脉冲无效。

## 8. 待办（手机 App 联调）

1. App（om.agent.coapp）实现蓝牙 SPP 客户端，连接 SCN 3。
2. App 侧虚拟 TUN 网段对齐：`192.168.55.1/24`（设备端 `bt-net` 静态 IP `192.168.55.2`）。
3. 连接后设备端预期：`SPP connected` + `net_status` → CONNECTED，随后 LLM 请求经 SPP→TUN 转发。
4. LLM key 配置存于 `/data/ai_agent/config/config.json`（littlefs 持久），`set_llm <host> <model> <key>` 配置（阶跃星辰 `https://api.stepfun.com/v1`，模型 `step-3.7-flash`）。

## 9. 重大后续发现（2026-08-12）：SPP 在控制器固件层不可用

**⚠️ 第 8 节作废**：真机联调时手机永远扫描不到设备。HCI trace（`SF32LB52_BT_TRACE=1`）证明：

- 蓝牙控制器运行在 **LCPU 闭源固件**（sf32lb52_bt_adapter.c 经 IPC 转发 HCI）；
- `HCI_RESET` 等初始化命令有 Command Complete 响应；
- **`Write_Scan_Enable`（BREDR 命令）发出后控制器零响应**——LCPU 固件未实现 BREDR。

**结论**：SF32LB52 的经典蓝牙（SPP）在硬件固件层面不可用（非软件配置问题）。蓝牙数据通道已转向 **BLE GATT NUS 透传**（详见 [`09_app_spp_tun_integration.md`](09_app_spp_tun_integration.md) 第 1-2 节）。本节保留为排障证据，`bt_adapter_set_scan_mode`/`set_name`/`BT_DEVICE_NAME` 等配置本身正确（BLE 广播同样适用）。

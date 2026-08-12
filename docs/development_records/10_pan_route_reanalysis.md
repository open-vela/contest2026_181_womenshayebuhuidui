# 10. PAN 路线重新分析：BREDR 误判纠正与冗余代码清单

日期：2026-08-12
状态：路线决策中（PAN 自实现 vs 串口 SLIP）

## 1. 重大结论纠正：BREDR 并非不可用

### 错误结论（此前记录）
"SF32LB52 的 LCPU 固件只实现 BLE，BREDR 被砍"（基于 HCI trace 观察 Write_Scan_Enable 无响应）

### 纠正依据
1. **官方规格**：SF32LB52-MOD-1 模组明确支持双模蓝牙 5.3（BR/EDR + BLE），官方联网方式就是**蓝牙 PAN（网络共享）**
2. **HCI trace 局限性**（此前未意识到）：
   - 默认 trace 只打印 ACL 数据（`type == BT_ACL_OUT`），**命令不打印**
   - 实际 HCI 命令路径是 `sf32lb52_host_send_packet`（adapter IPC 直连），**bth4.c 的 send trace 不触发**
   - "无 recv 响应"可能是 trace 观察不完整，而非控制器忽略
   - 已修复：`sf32lb52_host_send_packet` 加全量命令 trace（`sf32lb52 host tx: ...`）

### 正确结论
- 芯片支持 BREDR + BLE（硬件能力）
- openvela 蓝牙框架分层：应用层 Profile API + SAL（协议栈适配层）
- **SF32LB52 官方协议栈 = ZBLUE**（vendor 板级 defconfig 显式 `CONFIG_BLUETOOTH_STACK_BREDR_ZBLUE=y`，SAL 仅 `stacks/zephyr` 一个实现）——非 BLUELET（external/bluelet 无 src 源码缺失，且官方不用）

## 2. PAN 官方实现状态（已核实官方 dev 分支）

| 层 | 本地（contest） | 官方 dev |
|---|---|---|
| 应用层 API（`bt_pan.h`）| ✅ | ✅ |
| Profile（`panu_service.c`）| ⚠️ 骨架（引用不存在的 `sal_pan_interface.h`）| ⚠️ 骨架 |
| SAL 接口（`sal_pan_interface.h`）| ❌ 不存在 | ❌ 不存在（接口清单无 PAN）|
| SAL 实现（`bt_sal_pan_*`）| ❌ 全工程无 | ❌ 无 |
| panu_service 编译 | ❌ 未启用 | ❌ 未启用 |

**结论：openvela 官方（dev + contest）均未实现 PAN SAL——需要自实现 BNEP（L2CAP BR PSM 0x000F + BNEP 控制帧 + 以太网封装 + TUN 桥接），约 600-1000 行。**

## 3. 冗余代码清单（基于 PAN 正确路线）

### 设备端（2100 行，约 1500 行冗余）
| 文件 | 行数 | PAN 路线处置 |
|---|---|---|
| `packages/ai_agent/src/infra/ble_net.c`（SPP+TUN）| 827 | **部分保留**：TUN 管理（tun_open/tun_set_up/tun_poll，~200 行）BNEP 桥接可复用；SPP server/pipe/帧协议废弃（`#if 0` 隔离）|
| `packages/ai_agent/src/infra/ble_gatt.c`（NUS GATT+广播）| 612 | **废弃**（`#if 0`）；BREDR 可见性设置逻辑可参考 |
| `packages/ai_agent/src/infra/ble_gatt_net.c`（GATT+TUN 桥接）| 477 | **废弃**（`#if 0`）|
| `packages/ai_agent/src/infra/ble_cmd_handler.c`（命令通道）| 184 | **废弃**（`#if 0`）|
| `network_manager.c` 中 8 处 ble_net/ble_gatt 接线 | - | **改写**为 PAN 接线 |

### App 端（1338 行，100% 冗余——PAN 不需要 App）
| 文件 | 行数 | 处置 |
|---|---|---|
| `ble/BleTunnelManager.kt`（GATT 隧道）| 240 | 废弃（保留备份）|
| `vpn/TunnelService.kt`（BLE 桥接）| 220 | 废弃（保留备份）|
| `vpn/TcpProxy.kt`（TCP/DNS 代理）| 413 | 废弃（保留备份）|
| `viewmodel/TunnelViewModel.kt` + `ui/tunnel/TunnelScreen.kt` | 465 | 废弃（保留备份）|
| `ble/SppManager.kt` | 已删 | - |

**合计：约 3400 行代码中约 2900 行冗余（85%）**。保留原则：`#if 0` 隔离而非删除（可回退），App 端代码整体保留（可能用于其他演示）。

## 4. 未记录的事件补录

1. **设备挂死模式**（多次）：运行正常 → 手机/NRF 连接尝试 → bluetoothd 的 bt_service_10 线程 `uv__io_poll` hardfault → 系统静默挂死（无崩溃输出）。证据：HCI ACL 数据交换后崩溃。**BLE 连接数据流触发，非单纯广播问题**
2. **5 分钟稳定性监控**（2026-08-12）：设备在无连接尝试时稳定运行（无挂死），`netmgr Found iface eth0` 日志风暴（poll 线程每 ~1s 刷屏）
3. **烧录窗口错过**：boot ROM 窗口 ~10-20 秒，多次 `Timeout("waiting for shell prompt")`，需断电重插 + sleep 重试
4. **App 编译多次修复**（未逐条记录）：KDoc 注释特殊字符解析陷阱（`[len_hi]` 等）、Material3 实验 API OptIn、StateFlow 与布尔同名冲突（用户多次改回 isScanning 命名）、BleManager MAC 过滤陷阱、`device.name` 为 null 漏扫（改用 `scanRecord?.deviceName`）
5. **官方 dev 分支核实**（2026-08-12）：fetch openvela/frameworks_bluetooth dev 分支，确认 SAL 接口清单无 PAN、panu_service 未编译

## 5. 路线决策（待定）

- **A. 自实现 PAN（BNEP）**：600-1000 行协议代码 + 反复烧录调试；手机蓝牙共享直连，无需 App
- **B. 串口 SLIP**：~300 行 + USB-TTL 硬件（~10 元）；最可靠
- **C. 接受现状**：真机展示设备端功能 + QEMU 演示 LLM

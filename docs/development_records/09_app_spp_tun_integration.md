# 手机 App 蓝牙代理联网联调规划（SPP → BLE GATT 转向）

> 本文记录设备端（SF32LB52，见 [`08_ble_spp_tun_real_device.md`](08_ble_spp_tun_real_device.md)）与 Android 配套 App（com.agent.coapp）的蓝牙代理联网联调方案。**2026-08-12 重大决策：经典蓝牙 SPP 方案被否决（控制器固件不支持 BREDR），转向 BLE GATT NUS 透传通道**。

## 1. 关键决策记录（2026-08-12）

### 1.1 SPP（经典蓝牙）方案否决

**现象**：手机系统蓝牙设置永远扫描不到设备（即使完成以下全部正确配置）：
- `bt_adapter_set_scan_mode(BT_SCAN_MODE_CONNECTABLE_DISCOVERABLE)` + `bt_adapter_set_name("Agent-Watch")`（ble_net.c，adapter ON 后调用）
- defconfig `CONFIG_BT_DEVICE_NAME="Agent-Watch"`、`CONFIG_BT_CLASSIC=y`、`CONFIG_BT_ORIGINAL_API=y`
- SAL 层 `bt_br_set_visibility(true, true)` 返回成功（HCI 命令提交无报错）

**根因（HCI trace 证据）**：SF32LB52 蓝牙控制器运行在 **LCPU 闭源固件**（sf32lb52_bt_adapter.c 经 IPC 转发 HCI）。打开 `SF32LB52_BT_TRACE=1` 后：
- `HCI_RESET` 等初始化命令：控制器有 Command Complete 响应（`bth4 recv: len=7/15/13 first=04`）
- **`Write_Scan_Enable`（BREDR 命令）发出后：控制器零响应**

**结论**：LCPU 蓝牙固件**只实现 BLE + 基础 HCI 初始化**，BREDR（经典蓝牙）命令被静默忽略——**SPP 通道在硬件固件层面不可用**，与软件配置无关。

### 1.2 转向 BLE GATT NUS 透传

| 对比 | SPP（已否决） | **BLE GATT NUS（新方案）** |
|---|---|---|
| 控制器支持 | ❌ LCPU 固件无响应 | ✅ 穿戴芯片强项 |
| 手机发现 | 永远扫不到 | ✅ BLE 广播一定可见 |
| App 端改动 | SppManager（作废） | **复用现有 BleManager（NUS 客户端，UUID 6e400001）**，改动更小 |
| 设备端改动 | ble_net SPP server（作废） | 实现 GATT NUS server + TUN↔GATT 桥接 |
| 吞吐 | ~100KB/s（理论） | ~10-30KB/s（MTU 247）——LLM 文本流足够 |

## 2. BLE GATT 目标架构

```
┌─ SF32LB52 设备 ──────────────────────┐   ┌─ Android 手机 ──────────────────────────────┐
│ ai_agent (LLM/TLS)                   │   │ com.agent.coapp                              │
│   ↓ 发往 0.0.0.0 的 IP 包             │   │  BleManager (NUS 客户端, 6e400001)           │
│ TUN bt-net (192.168.55.2)            │   │   ↑↓ notify / write（帧协议）                │
│   ↓↑ (整包)                          │GATT│   ↓↑                                        │
│ NUS GATT server (6e400001)           │◄──┤  TunnelService 桥接层                        │
│   ↓↑ 帧协议 [len_hi][len_lo][payload] │   │   ↓↑                                        │
│ ble_net.c（TUN ↔ GATT 桥接改造）     │   │  TcpProxy（TCP/DNS 用户态代理）              │
└───────────────────────────────────────┘   │   ↓ 本机真实网络 (socket)                   │
                                            └───────────────────────────────────────────┘
```

角色定位不变：**手机 = 网关 + TCP/UDP 代理**（透明代理，非 VPN，无需授权）。

## 3. 已核实前提

| 项 | 值 |
|---|---|
| App BLE UUID | NUS：Service `6e400001-b5a3-f393-e0a9-e50e24dcca9e`，RX(写) `6e400002`，TX(通知) `6e400003` |
| 设备 TUN | `bt-net` 192.168.55.2/24，MTU 1518 |
| 帧协议 | `[len_hi][len_lo][payload]`（大端 16 位，M1 已完成，SPP/GATT 通用） |
| App 技术栈 | Kotlin + Compose + MVVM，minSdk 26；现有 BleManager 即 NUS 客户端 |
| 设备端 GATT | 当前 `CONFIG_BLUETOOTH_GATT` 关闭（需开启 + 实现 NUS server） |

## 4. 里程碑

### M1 帧协议（已完成 ✅ 2026-08-11）
- `ble_net_send`/`ble_net_receive` 侧帧协议改造，真机验证无回归。
- ⚠️ 排障遗留：bluetoothd 偶发 hardfault（FLAT build 布局敏感内存越界，`CONFIG_DEBUG_MM` 布局下未触发，源头未定位）；保持 DEBUG_MM 构建 + libuv 诊断日志。

### M1.5 设备端 GATT NUS 通道（进行中）
- defconfig：开 `CONFIG_BLUETOOTH_GATT` + GATT server 相关
- 实现 NUS 服务（6e400001 系列，与 App 对齐）：手机 write → 帧重组 → TUN；TUN → GATT notify
- 复用 ble_net.c 的帧协议与 TUN 管理，替换 SPP pipe 为 GATT 数据通路
- 验收：手机 BLE 扫描看到 `Agent-Watch` → App 连接 → 设备串口出现连接日志

### M2' App 桥接改造（代码已完成，2026-08-12）
- `ble/BleTunnelManager.kt`（新建）：BLE GATT NUS 连接（6e400001）+ 帧协议收发（与设备端对齐）+ 分片写入（20 字节/包）
- `vpn/TunnelService.kt`：桥接从 SPP 改为 BLE（BleTunnelManager + TcpProxy）
- `viewmodel/TunnelViewModel.kt` + `ui/tunnel/TunnelScreen.kt`：设备列表改为 **BLE 扫描**（BleManager.startScan）+ 已配对合并
- `ble/SppManager.kt`：废弃保留（无人引用）

**联调步骤**（用户编译 APK 后）：
1. 编译安装 APK → 打开「联网」页 → 授权蓝牙权限
2. 「扫描设备」→ 列表出现 **Agent-Watch** → 选择 → 「启动隧道」
3. 设备侧串口预期：`GATT connected`
4. 设备侧 `set_llm https://api.stepfun.com/v1 step-3.7-flash <key>` + `ask 你好` → 首次真机 LLM 对话

**预期排障点**：
- 扫描不到 → 确认设备串口日志 `advertising started OK`（广播在发）
- 连接失败 → 确认设备 `NUS service registered` + `GATT connected` 日志
- 连接成功但无网络 → 设备侧 `net_status`；TCP 代理需设备默认路由指向 bt-net
- 已知问题：设备侧堆损坏（广播路径越界写，未定位）——mallinfo 已绕过（agent_mem_get_status 保守值）；zblue async pipe fd 失效（EBADF）——uv__async_send 不再断言

### M3' 端到端 LLM 对话
1. 手机 BLE 扫描 → 连接 `Agent-Watch` → 授权（现有流程）
2. App 隧道页启动 → 设备串口 `GATT connected`
3. 设备侧 `set_llm https://api.stepfun.com/v1 step-3.7-flash <key>` + `ask 你好` → **真机首次 LLM 对话**
4. 多轮对话 + 断连重连

## 5. 风险清单（更新）

| 风险 | 等级 | 缓解 |
|---|---|---|
| BLE 吞吐低（~10-30KB/s）| 中 | MTU 协商 247 + LLM 文本流够用；TLS 握手慢可接受 |
| 设备端 GATT server 实现工作量 | 中 | NUS 是标准服务，NuttX/zblue 有现成模式参考 |
| 后台限制杀前台服务 | 低 | 前台服务 + 电池白名单引导 |
| 设备端 TCP 超时（转发延迟）| 低 | netmgr read_timeout 默认 30s，可调 |

## 6. 联调命令速查（设备侧）

```text
set_dns 223.5.5.5 8.8.8.8      # 默认即可，DNS 由 App UDP 代理转发
set_llm https://api.stepfun.com/v1 step-3.7-flash <key>
ask 你好
net_status                    # CONNECTED = App 隧道已建立
```

## 7. 已废弃内容归档

- **SPP 方案**：`ble/SppManager.kt`（RFCOMM）、SPP server（SCN 3）、`BT_SCAN_MODE` 配置——保留代码但标记为不可用（控制器固件不支持 BREDR）。
- **USB RNDIS 方案**：芯片 USB device 的 D+/D- 未引出至 Type-C 口（见 08 文档），已排除。
- **电脑直连**：电脑无蓝牙适配器 + 板载 USB 不可用，仅保留「USB-TTL + SLIP」理论选项。

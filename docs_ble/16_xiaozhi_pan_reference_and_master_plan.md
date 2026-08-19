# 16. xiaozhi-sf32 蓝牙 PAN 技术报告 + vela_contest 蓝牙完全打通总方案

> 日期：2026-08-17 ｜ 参考工程：/home/aila/projects/xiaozhi-sf32（SiFli SDK / RT-Thread / BTS2 栈）
> 本工程：/home/aila/projects/vela_contest（openvela / NuttX / Bluetooth Framework / zblue 栈）
> 依据：小米 openvela 蓝牙移植说明（SAL API 为协议栈接入标准方式）

---

## 第一部分：xiaozhi-sf32 蓝牙 PAN 实现与管理技术报告

### 1.1 总体架构

xiaozhi-sf32 运行于 SF32LB52x（小智 AI 对话手表/桌面牌），**整机无 WiFi，唯一上行网络就是蓝牙 PAN**：

```
┌─────────────────────────────────────────────────────┐
│ app 层（main.c 状态机 + 邮箱）                        │
│   配对/连接管理、UI 提示、重连策略、睡眠策略            │
├─────────────────────────────────────────────────────┤
│ SDK 中间件：bt_connection_manager / ble_connection_   │
│ manager / bts2_app_pan（PAN Profile）/ dfu_pan(OTA)   │
├─────────────────────────────────────────────────────┤
│ BTS2 双模协议栈（host 侧 profile + 闭源 LCPU 控制器）  │
├─────────────────────────────────────────────────────┤
│ lwIP：PAN profile 注册网卡 → DHCP → webclient/WS/MQTT │
└─────────────────────────────────────────────────────┘
```

关键事实：
- **协议栈是 BTS2（SiFli 私有）**，BR 链路、SSP、link key 管理全部由 SDK+LCPU 封装，app 只见 profile 级事件——这是它与本项目（zblue 自移植、事事亲力亲为）的最大差异。
- **角色固定：板子=PANU（主动方），手机=NAP**（手机开"蓝牙网络共享"）。与本项目 `pan connect <addr> 1 2`（dst=NAP, src=PANU）一致。

### 1.2 SDK 侧组件与配置（proj.conf）

```
CONFIG_RT_USING_BLUETOOTH=y      # RT-Thread 蓝牙设备框架
CONFIG_BLUETOOTH=y
CONFIG_BT_FINSH=y               # 蓝牙 CLI
CONFIG_BT_PROFILE_CUSTOMIZE=y
CONFIG_CFG_PAN=y                # PAN (BNEP) profile
CONFIG_BT_AUTO_CONNECT_LAST_DEVICE=y   # SDK 级自动回连最后配对设备
CONFIG_BTS2_APP_MENU=y
CONFIG_USING_DFU_PAN=y          # PAN 网络 OTA 中间件
```

### 1.3 连接建立流程（app/src/main.c，核心状态机）

事件入口：`bt_interface_register_bt_event_notify_callback(bt_app_interface_event_handle)`，主循环用 RT-Thread 邮箱 `g_bt_app_mb` 串行化处理（与本项目 bluetoothd service-loop 同思路）。

```
开机 → sifli_ble_enable()
  └─ BT_NOTIFY_COMMON_BT_STACK_READY → 邮箱 BT_APP_READY
       └─ bt_interface_set_local_name("小智-XX:XX")（MAC 后缀防重名）

手机配对/回连（SDK AUTO_CONNECT_LAST_DEVICE 参与）
  ├─ BT_NOTIFY_COMMON_ACL_CONNECTED        （清主动断开标志）
  ├─ BT_NOTIFY_COMMON_ENCRYPTION 完成或 PAIR_IND 成功
  │    └─ 记下 bd_addr，bt_connected=TRUE
  │    └─ ★启动 3 秒一次性 pan_connect_timer —— 注释明确：
  │       "Trigger PAN connection after PAN_TIMER_MS to avoid SDP confliction"
  │       （避开配对后 SDP 查询窗口，这是官方踩坑经验）
  └─ 超时回调 → 邮箱 BT_APP_CONNECT_PAN
       └─ bt_interface_conn_ext(&bd_addr, BT_PROFILE_PAN)

BT_NOTIFY_PAN_PROFILE_CONNECTED
  └─ g_pan_connected=TRUE → 邮箱 BT_APP_CONNECT_PAN_SUCCESS
       └─ ★PAN 连好才开始一切联网业务（串行化启动链）：
          NTP+天气 → 设备注册(HTTPS) → OTA 版本查询(dfu_pan)
          → 启动小智会话（websocket 或 MQTT）→ 创建睡眠定时器
```

### 1.4 连接管理/重连策略（app 层最值得移植的部分）

| 场景 | 判定 | 策略 |
|------|------|------|
| PAN 建立后断开且从未成功连过 | `first_pan_connected==FALSE` | `pan_reconnect()`：3 秒定时器重试 ×3，失败提示"请确保手机开了网络共享" |
| ACL 断开-res==SCO_DISCONNECTED | 手机主动断开 | 置 `Initiate_disconnection_flag`，30 秒后进睡眠（省电，不纠缠） |
| ACL 异常断开 | 其它 res | 10 秒周期重连定时器，`bt_interface_conn_ext(addr, BT_PROFILE_HID)` ×30 次（约 5 分钟） |
| 重连以 HID 为载体 | `BT_NOTIFY_HID_PROFILE_CONNECTED` 事件 | 若 PAN 未连：停 PAN 定时器，立即 `conn_ext(addr, BT_PROFILE_PAN)`（HID 连接成功≈ACL/加密就绪，借它搭 PAN） |
| 重连超 30 次 | — | 放弃，进睡眠 |
| KEY_MISSING | 配对信息丢失 | `bt_cm_delete_bonded_devs_and_linkkey()` 清本侧密钥，等待重新配对 |

websocket/MQTT 模块协作（xiaozhi_websocket.c）：联网前检查 `g_pan_connected`；若 BT 已连 PAN 未连 → 主动调 `pan_reconnect()` 并等待 ≤5s——**网络层与蓝牙管理层解耦但互相可触发**。

CLI：`pan_cmd del_bond`（清全部配对）/ `pan_cmd conn_pan`（手动触发 PAN 连接）；`Write_MAC` 写 OTP 蓝牙 MAC。

### 1.5 网络层与设备身份

- lwIP 网卡由 PAN profile 注册（BNEP EtherType 帧 ⇔ netif），IP 由手机 NAP 侧 DHCP 分配。
- 设备身份全部派生自蓝牙 MAC：
  - `get_mac_address()` = `bt_pan_get_mac_address()` → HTTP `Device-Id` 头；
  - `get_client_id()` = SHA256(MAC)[0:16] 格式化成 UUID → `Client-Id`/chip_id；
  - MAC 首选 OTP（`rt_flash_config_read(FACTORY_CFG_ID_MAC)`），无则 UID 生成并清 bit0/bit1 防组播。
- OTA：`dfu_pan` 中间件走 PAN 网络 HTTPS（ota.sifli.com），支持多镜像（代码/图片/字体）+ CRC32，查询→弹窗→置标志→重启搬运。
- Class of Device = NETWORK | PERIPHERAL | REMCONTROL（手机侧识别为可共享网络的外设）。

### 1.6 对本项目最有价值的 5 条经验

1. **配对/加密完成 → 延时 3 秒再连 PAN**（避 SDP 冲突）——本项目 PAN worker 可直接借鉴。
2. **断链分类**（对端主动 vs 异常）决定"睡眠省电"还是"重连挣扎"。
3. **重连借 HID/任意 profile 通道恢复 ACL，PAN 随后附挂**——本项目可借 createbond/任意 BR 连接触发。
4. **PAN 连接成功是所有联网业务的前置条件**（启动链串行化），网络层可反向触发蓝牙重连。
5. **身份/OTA 全部架在 PAN 网络之上**，证明 PAN 数据面一旦打通，业务层无需任何特殊适配。

---

## 第二部分：vela_contest 蓝牙现状盘点（对照表）

| 能力 | xiaozhi-sf32 | vela_contest 现状 |
|------|--------------|-------------------|
| 协议栈 | BTS2（闭源，SDK 封装） | zblue（external/zblue 移植）+ LCPU 闭源控制器 |
| 接入方式 | SDK profile API | **Bluetooth Framework SAL API**（sal_pan_interface.c，符合小米标准路径） |
| 守护进程 | SDK 内建 | bluetoothd（frameworks/connectivity/bluetooth/service/src/main.c 的 main()，即说明中的 bluetoothd_main） |
| PAN/BNEP | bts2_app_pan 现成 | **自研 SAL 已完成**（737 行：L2CAP PSM 0x000F + Setup 握手 + FRAME_ETH + MTU 1691） |
| 网卡 | lwIP netif（SDK 注册） | panu_service.c TAP 桥 → bt-pan + DHCPC ✅ |
| 可发现性 | SDK 自动 | R62 已突破（强制 inquiry scan + 100% 占空比）✅ |
| 配对 SSP | SDK 自动 | 桥接已做到 IO_CAPABILITY_REPLY，**未完成**❌ |
| link key | SDK 持久化 | RAM only，**无持久化**❌（无 settings 子系统） |
| 加密 ACL | SDK 自动 | set_security 被跳过（PAN_SAL_SKIP_SECURITY=1）❌ |
| 上网 | ✅ 量产 | ❌ 未打通（卡在配对/加密） |
| 应用层管理 | 邮箱状态机 | 无（bttool 手动操作）❌ |
| 备用通道 | — | BLE GATT NUS+TUN 代理（协议级已闭环，真机待验）✅ |

已验证里程碑：BREDR 可用（闸门0）、inquiry 双向、手机可发现板子、L2CAP PSM 0x000F 被手机接受（MTU 1691 修复后 CONFIG 应可通过）、SSP 事件号桥接（标准号↔zblue 偏移号、handle↔bdaddr 寻址）。

**当前卡点链**：SSP 配对未完成 → 无 link key → 加密不建立 → 手机拒绝未加密 BNEP → bt-pan 无数据 → 不能上网。

---

## 第三部分：蓝牙能力完全打通总方案（P0→P5）

> 架构原则（依据小米说明）：一切经 Bluetooth Framework——协议栈适配走 SAL（已完成 PAN SAL），服务与 IPC 走 bluetoothd，应用只碰 framework API/bttool/网卡。**不再绕过框架直调 zblue。**

### P0 基线固化（0.5 天）

1. 把散落的 gitignored 补丁正式化：`vendor/sifli/.../sf32lb52_bth4.c`、`external/zblue`（rx_work 排空/SSP 日志）、`frameworks/.../hci_h4.c` 的修改提交入库（或至少打入 contest 仓补丁目录），防丢失。
2. rcS 增加 `rm -rf /data/misc/bt && bluetoothd &`（规避 bt_list 崩溃 + 免手动起服务）。
3. 保持"调试期不启动 ai_agent"（mm 锁破坏问题独立排期）。

### P1 攻克 SSP 配对（当前主卡点，1~2 天）

目标：手机点板子 → SSP 数字比较弹窗 → 确认 → Link_Key_Notification → RAM 有 key。

1. **核对 io capability**：zblue 当前 0x03（DisplayOnly）。手机 NAP 通常要求 Just Works 或 Numeric Comparison——把 zblue/auth 回调注册成 DisplayYesNo（`bt_conn_auth_cb` 带 `pairing_confirm` 自动接受），否则 SSP 方法协商可能停在 IO 交换（R58 现象：IO_CAPABILITY_REPLY 后 LCPU 卡住，高度怀疑此处）。
2. **核对 LCPU SSP 事件参数布局**：R57 桥接按"status+handle+payload"假设重写事件，若弹窗仍不出现，用 40 字节 trace 抓原始事件逐字段比对标准 HCI 格式（IO_CAPA_REQ 标准是 bdaddr+capability+oob+auth，无 handle——LCPU 可能用私有头）。
3. **Write_Local_Name/EIR**：LCPU 名字是 6 字节私有格式，显示成地址。尝试转发 Write_Extended_Inquiry_Response（0x0c52）与 248 字节 name 命令原样下发；若 LCPU 拒绝，接受"显示地址"作为 P1 非阻塞项。
4. 备选路径：板侧主动 `createbond <手机> 1`（bttool）发起 SSP，走 initiator 方向绕开被动机局的 LCPU 卡点。
5. 验收：`pan connect` 前 HCI trace 出现 Encryption Change(0x08) + zblue `link_key_reply` 发 16 字节 key。

### P2 link key 持久化（1 天，doc 12 方案 B）

- port 层新增文件式 BR key store：
  - 存：挂钩 `bt_keys_link_key_store()`（keys_br.c），无条件写 `/data/misc/bt/br_key.bin`；
  - 载：`bt_hci_link_key_req()` RAM 未命中时先从文件恢复进 `br_key_pool` 再应答；
  - 容量：CONFIG_BT_MAX_PAIRED 提到 ≥4（手机+耳机+测试机）。
- 效果：重启后免重新配对，跨启动回连成立（对齐 xiaozhi 的 BT_AUTO_CONNECT_LAST_DEVICE 体验）。
- 同步恢复 `PAN_SAL_SKIP_SECURITY=0`：P1 通了以后，加密链路才是手机 NAP 接受 BNEP 的正路。

### P3 BNEP 数据面 → 上网（1 天，大概率顺水推舟）

1. `pan connect <手机> 1 2` → 加密 ACL → L2CAP CONFIG（MTU 1691 已修）→ BNEP Setup SUCCESS → `PROFILE_STATE_CONNECTED`。
2. `ifconfig bt-pan` up → `ifconfig bt-pan dhcp`（CONFIG_NETUTILS_DHCPC=y 已备）→ 手机 NAP 分配 IP（Android 常见 192.168.44.x）。
3. 验证链：ping 网关 → ping 8.8.8.8 → DNS 解析 → `curl http://…`（镜像缺 curl 则用 ai_chat/webclient 任一 HTTP 客户端，或加 CONFIG_NETUTILS_PING/ntpclient）。
4. 稳定性回归：断 PAN/断 ACL/手机关共享 各 10 次；确认 chan 归属规则（ACL 断开不提前 free，已修）、reconnect cooldown 生效；监控 .bss（SRAM 90.35%，P1/P2 新增代码需抠门）。

### P4 应用层连接管理（移植 xiaozhi 状态机，2 天）

位置：framework service 层（panu_service.c 内）或 packages/ai_agent `network_manager.c`，推荐前者（服务化，应用零改动即可受益）：

1. **栈就绪**（adapter state ON）→ set local name（含 MAC 后缀）。
2. **bond/加密完成事件** → **延时 3 秒**（xiaozhi 的 SDP 避让）→ 自动 `pan connect` 最后连接过的 NAP 地址（地址持久化到 /data/misc/bt/last_nap）。
3. **PAN connected** → 自动 DHCP → 通知 network_manager 默认路由切到 bt-pan（`netlib_set_dripaddr`/route 配置）→ ai_agent 联网链路就绪。
4. **断链分类重连**（xiaozhi 1.4 表格直接翻译）：
   - PAN 断且从未连成：3×3s 重试后提示；
   - ACL 异常断：10s 周期 ×30 次 createbond+pan connect（借 P2 持久 key）；
   - 手机主动断：进入低功耗等待（手表场景；桌面牌可改为无限低频重试）。
5. **network_manager 双通道**：bt-pan（主，BR PAN）+ 现有 BLE GATT NUS+TUN 代理（备，协议已闭环）。策略：bt-pan up 时走 bt-pan；down 时回退 GATT 代理；状态机互斥切换。
6. 设备身份对齐：Device-Id/chip_id 改用 bt-pan 网卡 MAC（= 本机 BT 地址），算法照抄 xiaozhi（SHA256 前 16 字节 UUID 化）。
7. CLI 保留：bttool `pan connect/dump` + 新增 `pan auto on|off`、`pan status`。

### P5 收尾（1 天）

- `bt_pan_test.py` 扩展成全自动冒烟：复位→起服务→等手机配对（人工一次）→pan→dhcp→ping→HTTP→断链重连→ 通过/失败报告。
- 文档：更新 docs_ble/00_README 结论表（PAN：❌不可用 → ✅可用），LOG.md 记 Round 8。
- 交付物：defconfig 终版（含 PAN、DHCPC、BT_DEBUG 收敛）、rcS、补丁清单。

### 里程碑判定

| 里程碑 | 判据 |
|--------|------|
| M1 配对打通 | Encryption Change 0x08 出现，RAM/文件有 link key |
| M2 BNEP 打通 | bt-pan up + DHCP 获得 IP + ping 通手机网关 |
| M3 上网打通 | ping 8.8.8.8 + DNS + HTTP 200 经手机 NAT |
| M4 全自动 | 重启后免人工：自动回连→自动 PAN→自动 DHCP→ai_agent 可联网 |
| M5 双通道 | bt-pan 断→GATT 代理自动接管，恢复→切回 |

### 风险与备案

- **R1 LCPU SSP 卡死无法软件绕过**：改走板侧 initiator（createbond）；再不行评估 SiFli 官方 SDK 的 LCPU 固件版本差异（官方 PAN example 证明同一 LCPU 能跑 PAN，说明配对路径必然存在，坚持逐事件比对）。
- **R2 手机拒绝未加密 BNEP 且加密始终不通**：BLE GATT NUS+TUN 代理已是完整备案（协议级闭环），保证项目"蓝牙上网"能力兜底交付。
- **R3 SRAM 溢出**：P1/P2 代码量小；必要时关 CONFIG_BT_DEBUG_LOG_LEVEL=6 或裁 GATT_CLIENT。

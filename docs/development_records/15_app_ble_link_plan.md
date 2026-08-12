# 15. 官方 App（com.agent.coapp）BLE 联调方案

日期：2026-08-13
状态：设备端 BLE legacy 广播已空口验证通过，App 联调待执行

## 1. 目标

打通「手机 App ↔ SF32LB52 设备」的 BLE GATT NUS 数据通道，实现：
1. 手机扫描发现设备 "Agent-Watch" 并连接
2. NUS 服务发现 + RX/TX 特征读写
3. JSON 命令收发（ping/status，及后续扩展：LLM Key 推送、对话转发）

## 2. 协议与端点（双方已一致）

| 项 | 值 |
|---|---|
| 广播名 | `Agent-Watch`（App 扫描前缀 `Agent-`） |
| 广播类型 | Legacy ADV_IND（已修复，空口验证通过） |
| 设备 MAC | `CD:AB:78:56:34:12`（nRF Connect 实测） |
| NUS 服务 UUID | `6e400001-b5a3-f393-e0a9-e50e24dcca9e` |
| RX 特征（App 写入） | `6e400002-b5a3-f393-e0a9-e50e24dcca9e` |
| TX 特征（设备通知） | `6e400003-b5a3-f393-e0a9-e50e24dcca9e` |
| 命令格式 | JSON：`{"cmd":"ping"}` / `{"cmd":"status"}` / `{"cmd":"wifi_config","ssid":"...","password":"..."}` |

设备端响应（TX 通知）：`{"status":"ok","msg":"..."}` 等。

## 3. 双方代码现状

### 3.1 设备端（已就绪）

- `packages/ai_agent/src/infra/ble_gatt.c`：NUS GATT server + legacy 广播（本次已修复）
- `packages/ai_agent/src/infra/ble_cmd_handler.c`：处理 `wifi_config`/`ping`/`status` 三命令
- 启动方式：`bluetoothd &` → `ai_agent` → vela> `ble_gatt_test init`
- 注意：`wifi_config` 对无 WiFi 的 SF32LB52 无效（网络走 USB/BLE 代理）；`ping`/`status` 通道可用

### 3.2 App 端（需小改）

`com.agent.coapp-main/app/src/main/java/com/agent/coapp/ble/BleManager.kt`：

| 问题 | 现状 | 处理 |
|---|---|---|
| 目标 MAC 硬编码 | L39 `TARGET_DEVICE_ADDRESS = "D0:C1:BF:B0:DF:F4"` | 改为实测 `CD:AB:78:56:34:12`，或扫描列表选择（`startScan(null)` 已支持全扫描） |
| 配网流程 | 连接后自动发送 wifi_config JSON 并等 "wifi connected" | 无 WiFi 板会超时 15s 失败——改为手动模式：连接后只做服务发现，用 `sendCommand` 发 ping/status |
| GATT 缓存 | `refreshGattCache` 反射调用 | 换设备/重刷固件后若服务发现失败，先在系统蓝牙中"忘记设备" |

### 3.3 已确认的一致性

- NUS 三段 UUID 与设备端 `NUS_SVC_UUID_BYTES` 小端字节序一致 ✅
- 扫描前缀 `Agent-` 与广播名匹配 ✅
- MTU 协商：App `requestMtu(512)`；设备端已修复双重减 3 的 MTU bug ✅

## 4. 联调步骤

1. 设备端启动：`bluetoothd &` → `ai_agent` → vela> `ble_gatt_test init`（确认日志 `Advertising started`）
2. 手机确认系统蓝牙能扫到 "Agent-Watch"（已验证 ✅）
3. App 修改 `TARGET_DEVICE_ADDRESS` 后编译：`cd com.agent.coapp-main && ./gradlew assembleDebug`
4. App 配网页 → 扫描 → 连接 → 服务发现（日志应显示 6e400001 服务）
5. 验证 RX/TX：App 发 `{"cmd":"ping"}` → 设备回 `pong`；App 发 `{"cmd":"status"}` → 设备回连接/MTU 状态
6. （进阶）扩展 `ble_cmd_handler.c` 增加 `llm_key`/`chat` 命令 → App 推送 LLM Key 与对话转发

## 5. 验证清单

- [ ] App 扫描列表出现 Agent-Watch（-40dBm 量级）
- [ ] 连接成功，MTU 协商 ≥ 200
- [ ] NUS 服务 + 3 特征完整发现
- [ ] 写 6e400002 → 设备收到并处理（串口日志可见）
- [ ] 设备 TX 通知 → App `onCharacteristicChanged` 收到
- [ ] ping/status 往返正常，连接保持 5 分钟无断连

## 6. 风险与注意事项

1. **连接后广播自动停止**（legacy 特性）：设备端 `on_connected` 已清理广播状态，断开后自动重播（本轮已修复）
2. **残余堆损坏风险**：预存越界写根因未定位，连接后若 hardfault 需回到根因定位（redzone/二分）
3. **App 缓存**：换固件后若服务发现异常，系统蓝牙"忘记设备"再重连
4. **wifi_config 语义**：对无 WiFi 板保留命令但返回不支持提示，避免 App 端误判超时

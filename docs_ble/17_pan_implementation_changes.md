# 17. PAN 实现变更记录 (P0-P5)

> 日期：2026-08-20
> 目标：完全打通蓝牙 PAN 能力，实现手机蓝牙网络共享上网

## 变更总览

| 阶段 | 文件 | 变更内容 |
|------|------|----------|
| P0 | `vendor/sifli/.../rcS` | 改成三行 `bluetoothd & / sleep 2 / ai_agent -d &`（去掉 audio_setup 与 rm -rf /data/misc/bt） |
| P0 | `contest.../defconfig` | `BT_MAX_CONN=1→2`, 添加 `HCI_AUTO_REPLY_IN_JUST_WORK=y` |
| P1 | `frameworks/.../bluetooth_define.h` | `DEFAULT_IO_CAPABILITY` 改为 `BT_IO_CAPABILITY_DISPLAYYESNO` |
| P1 | `frameworks/.../sal_adapter_interface.c` | `zblue_on_pairing_confirm` 添加 auto-accept |
| P4 | `frameworks/.../panu_service.c` | 添加 auto-connect 状态机 + auto-DHCP + reconnect |
| P5 | `bt_pan_test.py` | 更新为完整 E2E 测试脚本 |

## P0: 基线固化

### rcS (vendor/sifli/boards/sf32lb52/sf32lb52_devkit_lcd/src/etc/init.d/rcS)

调试期一度写成这样：

```bash
audio_setup
rm -rf /data/misc/bt    # 清除旧 link key，防 bt_list 崩溃
bluetoothd &             # 自动启动蓝牙守护进程
sleep 2
ai_agent -d &
```

三处都是坑，最终版只剩三行（详见文件顶部注释）：

```bash
bluetoothd &
sleep 2
ai_agent -d &
```

- `audio_setup` 在本树任何配置里都不是 builtin，`nsh_initscript()` 不带
  IGNORE 标志，脚本第一条失败就把下面所有行丢掉——bluetoothd/ai_agent 从来
  没被启动过。
- `rm -rf /data/misc/bt` 每次开机都把 bond 数据库和 `last_nap` 删掉：手机每
  次复位后都要重新配对，PAN 自动回连也没有目标地址。目录由 bluetoothd 的
  `create_bt_folder()` 自己建（EEXIST 当成功），脚本不该碰。
- `mkdir -p /data/misc/bt` 同样违反「不能有失败命令」：`cmd_mkdir` 直接返回
  `mkdir()` 的原始结果，`-p` 只压掉错误信息，返回值仍是 -1。
- 另外 NSH 的 `CONFIG_NSH_LINELEN=64`，脚本每行必须短于 64 字符，否则超出部分
  会被当成下一条命令。

### defconfig 变更

```diff
-CONFIG_BT_MAX_CONN=1
+CONFIG_BT_MAX_CONN=2
+# SSP Just-Works auto-reply
+CONFIG_HCI_AUTO_REPLY_IN_JUST_WORK=y
```

`BT_MAX_CONN=2` 允许一个 slot 给 connectable-adv，一个给手机 ACL 连接。

## P1: 攻克 SSP 配对

### 根因分析

`DEFAULT_IO_CAPABILITY` 原为 `BT_IO_CAPABILITY_NOINPUTNOOUTPUT` (0x03)，导致：
1. `get_io_capa()` 返回 NoInputOutput
2. 与手机协商出 JUST_WORKS 配对方式
3. JUST_WORKS 不需要用户交互，但部分手机要求认证配对（MITM 保护）
4. 手机拒绝 JUST_WORKS → 配对失败 → 无 link key → 无加密 → BNEP 被拒

### 修复 1: bluetooth_define.h

```diff
-#define DEFAULT_IO_CAPABILITY BT_IO_CAPABILITY_NOINPUTNOOUTPUT
+#define DEFAULT_IO_CAPABILITY BT_IO_CAPABILITY_DISPLAYYESNO
```

效果：
- `get_io_capa()` 返回 `BT_IO_DISPLAY_YESNO` (0x01)
- 与手机协商出 PASSKEY_CONFIRM（数字比较）配对方式
- 配对是认证的（authenticated），抵抗 MITM 攻击

### 修复 2: sal_adapter_interface.c

```c
static void zblue_on_pairing_confirm(struct bt_conn* conn)
{
    bt_address_t addr;
    zblue_conn_get_addr(conn, &addr);
    /* Auto-accept Just-Works pairing */
    adapter_on_ssp_request(&addr, BT_TRANSPORT_BREDR, 0, PAIR_TYPE_CONSENT, 0, NULL);
    bt_conn_auth_pairing_confirm(conn);  // ← 新增：自动接受
}
```

效果：当手机 NoInputOutput 且协商出 JUST_WORKS 时，自动接受配对请求。

### 配对方式矩阵（修复后）

| 手机 IO Cap | 本机 IO Cap | 协商方式 | 处理 |
|-------------|-------------|----------|------|
| DisplayYesNo | DisplayYesNo | PASSKEY_CONFIRM | `passkey_confirm` auto-accept ✅ |
| DisplayOnly | DisplayYesNo | JUST_WORKS | `pairing_confirm` auto-accept ✅ |
| KeyboardOnly | DisplayYesNo | PASSKEY_INPUT | 手机输入，本机确认 ✅ |
| NoInputOutput | DisplayYesNo | JUST_WORKS | `pairing_confirm` auto-accept ✅ |

## P2: Link Key 持久化

已有实现（验证通过，无需修改）：
- `keys_br.c`: 文件式 BR key store → `/data/misc/bt/br_key.bin`
- `ssp.c`: `bt_keys_link_key_store()` 在 link key notification 后调用
- `ssp.c`: `bt_keys_link_key_load_file()` 在 RAM miss 时从文件恢复
- rcS：**不**清除 `/data/misc/bt`——保留 bond 数据库与 `last_nap` 才有免配对回连

## P3: BNEP 加密配置

已有实现（验证通过，无需修改）：
- `PAN_SAL_SKIP_SECURITY=0`：请求加密
- `bt_conn_set_security(acl, BT_SECURITY_L2)`：在 PAN worker 中调用
- `required_sec_level = BT_SECURITY_L2`：所有 L2CAP channel 要求加密
- bth4.c R69/R70：Set_Event_Mask 补丁确保 Encryption Change 事件到达

## P4: 应用层连接管理

### panu_service.c 新增内容

#### 自动连接状态机

```
IDLE → WAITING_BOND → CONNECT_PENDING → CONNECTING → CONNECTED
                                                    ↓
                                              RECONNECTING → CONNECT_PENDING
```

#### 核心功能

1. **栈就绪设置设备名**：`pan_on_adapter_state_changed()` 在 `BT_ADAPTER_STATE_ON` 时调用 `adapter_set_name("Agent-Watch-XX:XX:XX:XX:XX:XX")`（含MAC后缀，对齐xiaozhi）
2. **自动 PAN 连接**：BR/EDR 配对完成后 3 秒自动连接 PAN（避 SDP 冲突）
3. **自动 DHCP**：PAN 连接成功后自动在 bt-pan 上运行 DHCP
4. **三类断链分类重连**（翻译自xiaozhi策略表）：
   - PAN从未连成：3×3秒重试，失败提示"请确保手机开了网络共享"
   - 异常断开（链路丢失/超时）：10秒周期×30次重连（约5分钟）
   - 手机主动断开：进入低功耗等待（ACL断开原因待plumb）
5. **地址持久化**：最后连接的 NAP 地址保存到 `/data/misc/bt/last_nap`
6. **冷却期**：ACL 断开后 5 秒冷却（防 LCPU 状态残留）
7. **设备身份**：`pan_get_bt_mac_address()` 提供标准MAC地址，`pan_get_device_id()` 提供SHA256(MAC)[0:16] UUID格式（兼容xiaozhi的get_client_id()），可用于Device-Id/chip_id

### 双通道互斥切换（network_manager.c）

在 `iface_poll_thread` 中实现 bt-pan（主）与 BLE GATT NUS+TUN（备）的优先级切换：

```
每次轮询:
  1. check_btpan_has_ip() — 检查 bt-pan 是否有 IP
     ├─ 有 → active_channel = "bt-pan"（主通道）
     └─ 无 → 检查备份通道
               ├─ ble_gatt_net_is_connected() && check_backup_channel_has_ip()
               │   → active_channel = "ble-gatt"（备份通道）
               └─ 都无 → active_channel = "none"（断开）
```

新增函数：
- `check_btpan_has_ip()`: 专门检查 bt-pan 接口是否有有效 IPv4 地址
- `check_backup_channel_has_ip()`: 检查非 bt-pan 接口是否有 IP（用于备份通道检测）

#### 关键函数

- `pan_on_bond_state()`: 配对完成回调，触发自动连接
- `pan_auto_do_connect()`: 执行 PAN 连接（带冷却检查）
- `pan_start_dhcp()`: 启动 DHCP 线程
- `pan_dhcp_thread()`: DHCP 工作者线程，获取 IP + DNS

## P5: 测试

### bt_pan_test.py

更新为完整 E2E 测试脚本，测试项：
1. Enable Bluetooth
2. Check adapter state
3. Inquiry devices
4. Create bond (SSP)
5. Connect PAN
6. PAN state dump
7. Network verification (ifconfig + ping)

用法：`python3 bt_pan_test.py /dev/ttyACM0 A4:CC:B3:FE:D1:A4`

## 编译验证

```bash
cd /home/aila/projects/vela_contest
lunch openvela_contest2026_181_board_ai_agent
m
```

## 端到端验证流程

1. 手机开启"蓝牙网络共享"
2. 板子上电，bluetoothd 自动启动
3. bttool: `enable` → `createbond <手机> 1`
4. SSP 配对：手机弹窗 → 确认 → 配对成功
5. PAN 自动连接（3 秒延迟）
6. bt-pan 自动获取 IP（DHCP）
7. `ping 8.8.8.8` → 成功
8. ai_agent 可通过 bt-pan 上网

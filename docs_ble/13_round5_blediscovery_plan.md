# 13. Round 5 测试手册：BLE 发现 → 配对 → PAN（免烧录优先）

日期：2026-08-15 晚 ｜ 分支：bletest ｜ 板子：SF32LB52 DevKit

## 背景（Round 4-4 已查明）

1. **BR 可发现失效**：sf32lb52_bth4.c 的 emulate 吞掉 Write_Scan_Enable →
   控制器 inquiry/page scan 从未打开 → 手机永远看不到板子。
2. **BLE adv 失败**：bttool `adv start -t adv_ind` 是 ext 语义（convert 强制
   EXT_ADV option），控制器 LE 特性被 emulate 全 0 → 判为不支持 ext →
   STACK_ERR(3)。**必须加 `-m legacy`**。
3. **上次系统卡死**：误启动 ai_agent（内存不足 assert 崩溃破坏 mm 锁）→
   全系统无响应。**不要启动 ai_agent**。
4. **手机只响应已配对设备的 BR page**（MIUI 行为）→ 必须先建立 bond。
5. **link key 不持久化**（zblue 无 settings）→ 配对后**同启动内**必须立即
   pan connect，否则重启后密钥丢失。

## 测试流程（用户在场）

### 步骤 0：恢复板子
- 物理拔插 USB（RTS 复位无效，板子死锁）→ 板子重启到 nsh。
- 确认 bluetoothd 运行（ps）。

### 步骤 1：启动 bttool + BLE legacy 广播（免烧录）
```
bttool
adv start -t adv_ind -m legacy -n Agent-Watch
```
- 预期：`on_advertising_start_cb ... status:0`（SUCCESS）
- 失败时看 `[adv]` syslog（诊断已加）：
  - `ext create fail` / `legacy start fail` 带 errno

### 步骤 2：手机配对
- 手机蓝牙列表（下拉刷新）→ "Agent-Watch"（BLE）→ 点击配对
- 手机输 PIN 0000（若有输入框）或确认配对
- 板子侧如有 `[PIN] please reply:` → `pair pin A4:CC:B3:FE:D1:A4 1 0000`
- 确认 bond state BONDED

### 步骤 3：pan connect（同启动内，密钥在 RAM）
```
pan connect A4:CC:B3:FE:D1:A4 1 2
```
- 预期链：ACL up → set_security → Link_Key_Request → **RAM key 应答** →
  Encryption Change → `[pan] security level=2` → L2CAP(0x000F) MTU 1691 →
  BNEP Setup resp 0x0000 → TAP "bt-pan" 创建 → PROFILE_STATE_CONNECTED

### 步骤 4：上网验证
- `ifconfig bt-pan 192.168.44.2 netmask 255.255.255.0`（静态猜 Android 网段）
- `route add default gw 192.168.44.1`（或 ifconfig gw 参数）
- 包计数 / 手机端「蓝牙网络共享」客户端显示
- 注意：MAC OUI 若非法可能影响上网（官方 SDK 提示）

## 备选路径（若 BLE adv 失败，烧新固件）

新固件（out/openvela_contest2026_181_board_ai_agent/nuttx.bin 19:09）含：
1. **WRITE_SCAN_ENABLE 转发 LCPU**（不再 emulate）→ `set scanmode 2`
   真正下发 → BR 可发现（官方 SDK 证实 LCPU 支持）
2. SAL adv syslog 诊断

烧录（需用户确认）：`python3 logs/flash_rts.py`（重试 2-5 次）
烧录后：`set scanmode 2` → 手机直接发现（BR）→ 配对 0000 → pan connect

## 工具
- /tmp/pan_test_round5.py：boot_check / start_bttool / adv_legacy / watch /
  pair_pin / pan_connect / cmd
- 串口：/dev/ttyACM0 @ 1000000

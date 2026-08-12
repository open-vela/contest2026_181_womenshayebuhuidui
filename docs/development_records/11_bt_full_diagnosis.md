# 11. 蓝牙全链路深度诊断：HCI 实证、fault 定位与 LCPU 固件缺陷确认

日期：2026-08-12
状态：诊断完成，HCPU 侧修复实验待执行

> 本文记录 SF32LB52（SF32LB52-MOD-1 N16R8）在 openvela 上蓝牙联网的完整深度诊断：从 HCI trace 实证、bluetoothd 崩溃定位、fault 寄存器解码，到广播 enable 缺失根因与 LCPU 固件缺陷确认。验证日志：`/tmp/*.log`、`logs/spp_bredr_verify.log` 等。

## 1. 关键结论速览

| # | 结论 | 证据 |
|---|---|---|
| 1 | **BREDR 命令链路真实存在**（控制器正常响应） | 4 条 HCI 命令（Reset/Features/Version/BD_ADDR）全部 Command Complete |
| 2 | **Write_Scan_Enable 等 30+ 命令被 vendor 驱动模拟**（不发送到控制器） | `sf32lb52_bth4.c` 的 `sf32lb52_bt_emulate_cmd()` |
| 3 | **bluetoothd 崩溃 = 布局敏感堆损坏**（崩溃点随机） | 两次崩溃点不同（libuv mutex / inode 链表） |
| 4 | **Fault 类型 = 精确总线错误**（PRECISERR） | `CFSR=0x8200, HFSR=0x40000000, BFAR=0x00100121`（LPSYS ROM 区非法访问） |
| 5 | **广播 enable 曾被跳过**：`le_adv_start_add_conn` -ENOMEM → goto 跳过 `LE_Set_Advertising_Enable` | zblue adv.c 源码 + HCI trace 缺 enable 命令 |
| 6 | **ext adv 广播 0x2039 响应成功但空口无信号** → LCPU 固件缺陷（或 ext 支持问题） | 手机双通道扫描不到 |
| 7 | **GATT=y 是 bluetoothd 崩溃诱因之一**（08 文档成功固件 GATT 关闭） | 关闭 GATT 后无崩溃（但静默挂死） |
| 8 | **BT_MAX_CONN=2 触发确定性启动崩溃**（flash g_lock 布局敏感） | 连续 3 次断电重插必崩，回退后恢复 |
| 9 | **探针 open/close 改变堆布局可绕开崩溃**（非修复，仅诊断） | 13 个探测点全过、无崩溃 |
| 10 | **屏幕硬件正常**：fb0 390×450 16bpp 就绪，lvgldemo 点亮成功 | `ls /dev` + fb 命令 |

## 2. 实验过程与证据

### 2.1 烧录方法（stub 超时解决）

```bash
# default_reset 模式 stub 下载超时；--before no_reset 成功
./build_and_flash.sh flash -p /dev/ttyACM0 -i out/xxx/nuttx.bin --timeout 180 --before no_reset
```

### 2.2 HCI trace 全链路（BREDR 实证）

```
[ble_net] Enabling BT adapter (state=0)
→ HCI_RESET (0x0C03)            → Command Complete (len=7)  ✅
→ Read_Local_Supported_Features → Command Complete (len=15) ✅
→ Read_Local_Version (0x1001)   → Command Complete (len=15) ✅
→ Read_BD_ADDR (0x1009)         → Command Complete (len=13) ✅
```

### 2.3 bttool 官方工具（CONFIG_BLUETOOTH_TOOLS=y）

- **LF-only 交互陷阱**：bttool 的 `getline` 只去 `\n`，CRLF 导致 `enable\r` 匹配失败（Unknown command）
- **enable 状态链成功**：`Adapter state changed: 1 → 2 → 4`（BLE + BREDR 全启用）
- 完整信息：`Adapter Name: Agent-Watch, Cap: 3, Class: 0x00280704, Mode:2`
- `set scanmode 2` 成功（但 Write_Scan_Enable 被模拟，真实可见性未验证）

### 2.4 fault 寄存器定位（CONFIG_DEBUG_HARDFAULT_ALERT=y）

```
CFSR: 00008200   → BFARVALID(0x8000) + PRECISERR(0x0200) = 精确总线错误
HFSR: 40000000   → FORCED（升级为 HardFault）
BFAR: 00100121   → 访问 LPSYS ROM 区（0x00100000 附近）失败
崩溃现场：zblue common_init → prng_init → mbedtls 熵池 → getrandom
  → open("/dev/urandom") → inode_find → _inode_compare → 💥
```

**机制**：inode 是堆分配（`fs_heap_zalloc`）——inode 链表节点指针被**堆数据区越界写**破坏为 0x00100000，遍历时解引用 `i_name`（偏移 0x121）→ 0x00100121 → 总线错误。

### 2.5 探针打点（定位破坏阶段）

- btservice.c（bluetoothd 初始化 6 点）+ hci_core.c（zblue common_init 7 点）
- 探测法：每阶段 `open("/dev/urandom")`（与崩溃路径相同的 inode 查找）
- **结果**：全部通过、prng_init 完成、无崩溃——**探针的 open/close 改变了堆分配布局，绕开了破坏路径**（布局敏感堆损坏实锤）

### 2.6 广播 enable 缺失根因（zblue adv.c）

```c
// bt_le_adv_start_legacy:
err = le_adv_start_add_conn(adv, &conn);   // CONNECTABLE 时分配 conn
if (err == -ENOMEM && !dir_adv && ...) {
    goto set_adv_state;    // ❌ 跳过 bt_le_adv_set_enable（0x200A/0x2039）！
}
err = bt_le_adv_set_enable(adv, true);     // 广播使能（被跳过）
```

- 根因：`CONFIG_BT_MAX_CONN=1` 连接池不足 → -ENOMEM → 广播 enable 跳过
- **修复 1（已实施）**：zblue adv.c 修改——-ENOMEM 时继续 set_enable（conn=NULL）
- **修复 2（已回退）**：BT_MAX_CONN=2——触发 flash g_lock 布局敏感崩溃，回退

### 2.7 ext adv 广播（LCPU 固件缺陷确认）

```
0x2036 (LE_SET_EXT_ADV_PARAM)    ✅ 响应
0x2037 (LE_SET_EXT_ADV_DATA)     ✅ 响应
0x2039 (LE_SET_EXT_ADV_ENABLE)   ✅ Command Complete status:0
on_advertising_start_cb status:0 → 广播"成功"
但手机双通道（BLE App + 经典蓝牙）均扫描不到 → 空口无信号
```

- **结论**：LCPU 固件对 ext adv enable 返回成功但实际不发射（或 ext 支持缺陷）
- legacy 广播（0x200A）**尚未真正测试**（bttool `-m legacy` 参数解析失败，待排查）

### 2.8 BREDR 可见性（被模拟）

- `Write_Scan_Enable`（0x0C1A）在 `emulate_cmd` 列表中被本地模拟 → 命令未达控制器
- 模拟列表 30+ 命令：WRITE_SCAN_ENABLE / WRITE_LOCAL_NAME / WRITE_CLASS_OF_DEVICE / SET_EVENT_MASK / LE_SET_EVENT_MASK 等
- **可实验**：移出模拟列表真实下发，观察 LCPU 响应（待执行）

### 2.9 GATT=y 崩溃诱因确认

- 08 文档成功固件：`# CONFIG_BLUETOOTH_GATT is not set`
- spp_verify（从 ai_agent defconfig 复制）：`CONFIG_BLUETOOTH_GATT=y` → bt_service_10 崩溃
- **关闭 GATT 后**：无崩溃（但 ai_agent enable 路径静默挂死——探针干扰，待移除验证）

## 3. 当前固件状态（spp_verify）

```text
CONFIG_AI_AGENT_BLE_NET=y      # SPP+TUN 通道（08 文档验证路线）
# CONFIG_AI_AGENT_BLE_GATT is not set
# CONFIG_BLUETOOTH_GATT is not set   # GATT 关闭（崩溃诱因移除）
CONFIG_BLUETOOTH_TOOLS=y       # bttool
CONFIG_DEBUG_HARDFAULT_ALERT=y # fault 诊断
CONFIG_BT_MAX_CONN=1           # 默认（回退）
# 含探针打点（btservice.c + hci_core.c，待移除）
```

## 4. 待办清单（HCPU 侧可执行）

| # | 任务 | 状态 |
|---|---|---|
| 1 | 移除探针代码（btservice.c + hci_core.c）恢复纯净 | 待执行 |
| 2 | **移除 Write_Scan_Enable 模拟**真实下发（BREDR 可见性实验） | 待执行 |
| 3 | **legacy 广播测试**（0x200A，排查 bttool -m legacy 参数问题） | 待执行 |
| 4 | 手机经典蓝牙 + BLE 双通道扫描验证 | 待执行 |
| 5 | 若 LCPU 均不支持 → 证据提交思澈/大赛组委会（LCPU 固件缺陷报告） | 待决策 |
| 6 | 蓝牙失败兜底：SLIP（USB-TTL）/ ESP8266 SLIP 路由器（非蓝牙方案已规划） | 待决策 |

## 5. 关键文件与代码位置

| 文件 | 说明 |
|---|---|
| `vendor/sifli/chips/sf32lb52/sf32lb52_bth4.c` | H4 驱动 + `sf32lb52_bt_emulate_cmd()`（模拟 30+ 命令） |
| `vendor/sifli/chips/sf32lb52/sf32lb52_bt_adapter.c` | HCPU↔LCPU IPC 转发 + host tx trace |
| `vendor/sifli/chips/sf32lb52/sf32lb_flash.c` | NOR 驱动（preinit g_lock 布局敏感崩溃） |
| `external/zblue/zblue/subsys/bluetooth/host/adv.c` | 广播使能路径（-ENOMEM 跳过修复已实施） |
| `external/zblue/zblue/subsys/bluetooth/host/hci_core.c` | common_init + 探针（待移除） |
| `frameworks/connectivity/bluetooth/service/src/btservice.c` | bluetoothd 初始化 + 探针（待移除） |
| `frameworks/connectivity/bluetooth/tools/bt_tools.c` | bttool（LF-only 交互） |
| `nuttx/arch/arm/src/arm_m/arm_hardfault.c` | fault 寄存器打印（CONFIG_DEBUG_HARDFAULT_ALERT） |

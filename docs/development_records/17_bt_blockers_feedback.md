# 17. 蓝牙联网方案阻碍点汇总（反馈组委会）

日期：2026-08-13
队伍：Sen70s (#181)
硬件：SF32LB52-DevKit-LCD（思澈 SF32LB52-MOD-1，双模蓝牙 5.3，无 WiFi）
用途：向 openvela 大赛组委会反馈各蓝牙联网方案的无法执行点，请求技术支持

## 摘要

本队在 SF32LB52（无 WiFi）上实现 ai_agent 真机联网，依次验证了 5 条路径，每条路径均存在**框架/固件/内存层面**的阻断点。广播链路已自行修复（ext adv 假成功 → legacy 广播，空口验证通过），但**连接建立**阶段被一个预存内存破坏 bug 阻断。恳请组委会协助对接资源（思澈固件、PAN SAL、zblue 内存问题排查）。

## 方案 1：手机蓝牙网络共享（PAN/BNEP）——框架 SAL 缺失

| 项 | 内容 |
|---|---|
| 方案说明 | 手机开"蓝牙网络共享"，设备以 PANU 身份经 BNEP over L2CAP 接入手机 NAP |
| 验证结论 | **openvela 框架 PAN SAL 适配层全工程不存在** |
| 证据 | 框架有 API 头 `bt_pan.h` + profile 骨架 `panu_service.c`，但 `sal_pan_interface.h` 与 `bt_sal_pan_*` 实现全工程无引用；`panu_service` 未编译进固件；**官方 dev 分支核实同样缺失** |
| 影响 | 设备无法以 PANU 加入手机网络；自实现 BNEP 需 600-1000 行 + 框架层改动 |
| 诉求 | 官方提供 PAN SAL 实现，或 BNEP over L2CAP 的参考移植 |

## 方案 2：经典蓝牙 SPP（含官方 AI_AGENT_BLE_NET 的 SPP 代理路线）

| 项 | 内容 |
|---|---|
| 方案说明 | BREDR SPP 通道承载 IP（官方 `AI_AGENT_BLE_NET` 的"蓝牙 SPP 代理上网"路线） |
| 验证结论 | **vendor 驱动模拟 30+ 条 BREDR HCI 命令，命令从未到达 LCPU** |
| 证据 | `vendor/sifli/chips/sf32lb52/sf32lb52_bth4.c` 的 `sf32lb52_bt_emulate_cmd()` 对 Write_Scan_Enable、Write_Local_Name、Write_Class_of_Device、Set_Event_Mask 等 30+ 条命令本地合成 CMD_COMPLETE（status=0）直接返回，`sf32lb52_host_send_packet` 不执行 |
| 影响 | BREDR 链路的可发现性/连接参数从未真实下发；BREDR 端到端能力无法验证；SPP 路线无法推进 |
| 诉求 | 思澈厂商对 LCPU 固件 BREDR 支持情况与驱动模拟层的官方说明（哪些命令应真实下发） |

## 方案 3：BLE GATT NUS 数据通道（官方 App com.agent.coapp 路线）

### 3.1 已解决部分（本队自行修复，供参考）

| 环节 | 问题 | 修复 |
|---|---|---|
| 广播 | 扩展广播（0x2036/2037/2039）被 LCPU **假成功**（status=0 但空口无信号） | 对齐官方 Gemini-S1 配置走 legacy：`BT_EXT_ADV_LEGACY_SUPPORT=y` + `adv_type=BT_LE_LEGACY_ADV_IND`；nRF Connect 实测 **Advertising type: Legacy + Agent-Watch + NUS UUID 6e400001 + RSSI -43dBm**，空口验证通过 |
| App 闪退 | `startForeground` 未在时限内调用 / Manifest 缺 `foregroundServiceType` | 已修复 |

### 3.2 当前阻断点：连接建立被内存破坏阻断（请重点支持）

**现象**：手机 connectGatt 后连接永远无法建立，设备端无任何连接事件（45 秒 0 输出）。

**完整证据链**：

```
① LCPU 是标准控制器：初始化 9 条 HCI 命令全部 CMD_COMPLETE status=00
   （0x0C03/0x1003/0x1001/0x2001/0x1009/0x2006/0x2008/0x2009/0x200A）
② zblue host 计算 LE_SET_EVENT_MASK 掩码 = 0x020f（bit0=连接完成事件，正确）
③ sys_put_le64 写入 net_buf 后立即读回 = 0f 02 00...（正确）
④ 驱动层 bt_send 收到时 = 08 0f 02...（被破坏！0x020f → 0x0f08）
⑤ 错误的掩码下发 → LCPU 屏蔽 LE Connection Complete（0x3E/0x01）事件
⑥ 手机连接永远无法建立
```

**结论**：zblue 的命令 net_buf 在写入正确值后、发送前被**预存内存越界写**破坏。同源证据：
- 此前 inode 链表节点被破坏（BFAR=0x00100121 精确总线错误，arm_hardfault.c:186）
- 配置修改触发布局敏感崩溃（关闭 GATT_CLIENT 后 voice/LVGL 初始化崩溃，恢复后消失）
- 崩溃点随机、探针改变堆布局可绕开——均指向持续性越界写

**诉求**：请官方技术支持协助定位 zblue 在 NuttX 上的内存破坏源（net_buf 池管理 / 引用计数 / 与 LCPU IPC 的交互），或提供 SF32LB52 上 BLE peripheral 连接建立的可运行参考配置。

## 方案 4：USB RNDIS（非蓝牙备选，一并反馈）

| 项 | 内容 |
|---|---|
| 验证结论 | **芯片 USB 控制器被 CDC ACM 串口占用；composite 端点不足** |
| 证据 | 板子串口为 on-package USB CDC ACM（占唯一 USB 控制器）；USB 控制器仅 4 端点（EP0+3），而 RNDIS(3)+CDCACM(3) composite 需 6 端点，硬件不可行；两个 Type-C 口并联同一 USB |
| 影响 | USB 有线联网无法与烧录调试串口共存 |
| 诉求 | 无（硬件约束，仅记录） |

## 诉求汇总

| # | 诉求 | 优先级 |
|---|---|---|
| 1 | zblue/NuttX net_buf 内存破坏源定位支持（方案 3.2，阻断所有 BLE 数据通道） | 最高 |
| 2 | PAN SAL 实现 / BNEP 参考移植（方案 1） | 高 |
| 3 | 思澈 LCPU 固件 BREDR 命令支持说明与 bth4 模拟层澄清（方案 2） | 高 |
| 4 | SF32LB52 BLE peripheral 连接建立参考实现/配置（方案 3） | 高 |

## 附：本队已沉淀的可复用成果（git 检查点）

- legacy 广播修复（zblue `20ebb784582`、vendor `70777ae`/`b994695`、packages/ai_agent `d0e2d26`）
- App 端 BLE 隧道完整实现（BleTunnelManager/TunnelService/TcpProxy，含前台服务合规修复）
- 诊断文档 11-16 号（HCI trace 方法论、掩码证据链、net_buf 破坏实锤）

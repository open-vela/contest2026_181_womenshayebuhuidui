# 02 PAN / IPSP / 6LoWPAN 可行性论证（2026-08-14 代码级验证）

目标：确认"蓝牙代理上网"是否有标准协议路径可用，结论是 **没有**，必须走自定义 GATT 代理。

## 1. 经典蓝牙 PAN（BNEP）— ❌ 不可用

PAN（Personal Area Networking）基于 BNEP，**只存在于 BREDR（经典蓝牙）**，与 BLE 无关。
本板三条证据全部指向不可用：

| 检查项 | 结果 | 位置 |
|--------|------|------|
| LCPU 固件 BREDR 支持 | ❌ 不响应 BREDR 命令（历史诊断结论，docs/11 记录） | SF32LB52 LCPU 闭源固件 |
| zblue 栈 PAN/BNEP SAL | ❌ 无 sal_pan_interface.c，全栈无 bnep 字样 | frameworks/connectivity/bluetooth/service/stacks/zephyr/ |
| 框架 PAN 开关 | ❌ Kconfig 无 CONFIG_BLUETOOTH_PAN；panu_service.c 未编入任何构建 | frameworks/connectivity/bluetooth/ |

另外确认：框架里的 profiles/pan/panu_service.c 是 PAN **User** 角色的完整实现
（TAP 设备 bt-pan 桥接），代码可用但**缺 SAL 底层**（zblue 没实现 BNEP），
且需 BREDR 链路 — 对本板两层都缺。若未来 LCPU 固件支持 BREDR，可考虑接通该路径。

## 2. BLE IPSP / 6LoWPAN — ⚠️ 暂不可用（工作量 > 自研 GATT 代理）

- BLE 的标准 IP 承载是 IPSP（Internet Protocol Support Profile，基于 6LoWPAN 压缩 IPv6）。
- NuttX 侧：net/bluetooth（蓝牙网卡驱动）+ net/sixlowpan（6LoWPAN 压缩）均存在。
- 缺的一环：zblue 栈没有 6LoWPAN/IPSP 适配层（Zephyr 原生有 CONFIG_BT_6LOWPAN，
  但 openvela 的 zblue 裁剪栈未带出），且手机侧 Android 系统**不开放** IPSP 角色给普通 App。
- 结论：即使打通也要 IPv6 + 6LoWPAN 全栈改造，且手机 App 无法以标准 IPSP 接入 →
  **放弃，走自定义 GATT 隧道**。

## 3. 自定义 BLE GATT 隧道（NUS + TUN）— ✅ 唯一可行路径

- 设备：GATT 服务端（NUS）+ TUN 设备，IP 帧走 GATT notify/write。
- 手机：GATT 客户端 + 本地 VPN/TUN 桥 + NAT 出网（Android VpnService）。
- 优点：两端代码都在我们手里，LCPU 固件只要求 LE 能力（已实测可用）；
  手机侧用标准 BLE API + VpnService，无系统权限问题。
- 缺点：非标准协议，需自研帧协议与分片（已实现）；吞吐受 BLE 4.x 带宽限制（约 10-40 KB/s）。

## 4. 参考资料（离线备忘，web 检索不可用时按此索引）

- Bluetooth Core Spec Vol 3 Part B — BNEP 仅在 BREDR。
- Bluetooth LE IPSP spec（v1.0）— 基于 6LoWPAN 的 IPv6 承载，Android 不暴露给三方 App。
- Android VpnService — 应用级 VPN 隧道 + NAT，可作蓝牙代理出网桥。
- NuttX net/bluetooth 驱动 + net/sixlowpan — 仅当 zblue 提供 6LoWPAN 适配时可用。

## 决策

> **PAN 不可用；BLE GATT NUS + TUN 自研代理为唯一可行路径，继续投入。**

# docs_ble — BLE 蓝牙代理上网验证记录（bletest 分支）

> 分支：bletest ｜ 板卡：SF32LB52-DevKit-LCD ｜ 目标：**手机 App ↔ 手表 BLE 连接交互后，经蓝牙代理访问互联网**
> 本目录独立于旧 docs/，只记录 BLE 代理上网的验证过程、突破与发现。每次有突破即更新并 commit。

---

## 当前结论速览（2026-08-14/15，代码级验证）

| 问题 | 结论 | 依据 |
|------|------|------|
| PAN（BNEP，经典蓝牙）能否用？ | ❌ 不可用 | ① LCPU 闭源固件不支持 BREDR 命令；② zblue 栈无 sal_pan_interface（无 BNEP 实现）；③ 框架无 CONFIG_BLUETOOTH_PAN 选项；④ panu_service.c 从未被编入构建 |
| BLE IPSP / 6LoWPAN 能否用？ | ⚠️ 暂不可用 | NuttX 有 net/bluetooth + sixlowpan，但 zblue 栈未提供 6LoWPAN 适配层（无 bt6lowpan/IPSP SAL） |
| BLE GATT NUS + TUN 代理？ | ✅ 可行（当前主线） | 设备端代码已完整实现（ble_gatt.c + ble_gatt_net.c），本次已修复致命分片缺陷（见 03） |
| 本次代码审查发现的致命缺陷 | ✅ 已修复 | ble_gatt_send() 不切分超 MTU 报文 → 所有 IP 包被丢弃；App 端未协商 MTU、硬编码 20 字节分片（见 03） |

## 验证路径（本分支要做的）

1. ~~代码级验证~~（本次：架构可行性 + 分片缺陷定位与修复 + 编译通过）
2. 真机验证：固件烧录 → App 连接 → ping 通网关 → curl/LLM 请求穿透（见 04_verification_plan.md）
3. 性能验证：MTU 247 下吞吐量、稳定性、重连

## 文档索引

- [00_README.md](00_README.md) — 本页
- [01_ble_proxy_architecture.md](01_ble_proxy_architecture.md) — 当前 GATT+TUN 代理架构全貌
- [02_pan_feasibility.md](02_pan_feasibility.md) — PAN / IPSP / 6LoWPAN 可行性论证
- [03_fragmentation_fix.md](03_fragmentation_fix.md) — 关键突破：分片缺陷发现与修复
- [04_verification_plan.md](04_verification_plan.md) — 真机联调验证步骤
- [21_pan_breakthrough_authoritative.md](21_pan_breakthrough_authoritative.md) — **PAN 上网打通（权威版）**：三个根因的完整证据链 + Gate B/D/E/F 验证结果
- [22_pan_engineering_guide.md](22_pan_engineering_guide.md) — **工程指南**：四条硬规则、症状→先查什么、验证口径、大赛交付路径
- [LOG.md](LOG.md) — 逐轮调试日志（Round 1–12）

## 验证环境

- 工作区：/home/aila/projects/vela_contest
- 固件构建：out/openvela_contest2026_181_board_ai_agent（ninja，SRAM 88.31% 使用，512KB 内可容纳）
- 固件配置：CONFIG_AI_AGENT_BLE_GATT=y（GATT 服务端 + TUN + 广告）
- App：com.agent.coapp-main（Android，BLE GATT 客户端 + VPN 隧道）

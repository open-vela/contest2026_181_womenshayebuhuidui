# 08 PAN 实现路线图（Round 3，2026-08-15）

> 结论先行：**标准 PAN（BNEP over BREDR）目前 0% 实现**。Round 1 只做了可行性分析。
> 本文档给出实现所需缺口的精确清单与决策闸门。

## PAN 是什么、为什么难

- PAN = Personal Area Networking，基于 **BNEP**（Bluetooth Network Encapsulation Protocol），
  承载在 **BREDR（经典蓝牙）L2CAP PSM 0x000F** 之上，与 BLE 无关。
- 手机 Android 支持"蓝牙网络共享"（PAN-NAP/客户端），这正是"PAN 方式蓝牙代理上网"的标准形态。

## 现状核查（代码级，2026-08-15）

| 层 | 状态 | 证据 |
|----|------|------|
| 框架 API (bt_pan.h + framework/api/bt_pan.c) | ✅ 已存在 | CMakeLists 已备 CONFIG_BLUETOOTH_PAN 分支 |
| 框架 IPC (socket/binder) | ✅ 已存在 | bt_socket_pan.c / bt_pan.c(binder) |
| Profile 层 (service/profiles/pan/panu_service.c) | ✅ 已存在（PANU 角色，TAP 桥接） | 完整实现，从未被编译 |
| BREDR 栈 (zblue classic) | ✅ 已编入固件 | br.c/l2cap_br.c/rfcomm.c/sdp.c/sco.c/ssp.c 的 .o 均在构建产物中 |
| **BNEP 协议栈** | ❌ **不存在** | zblue 无 bnep.c；Zephyr 上游也从未实现 BNEP → 需自研 |
| **SAL 层 (sal_pan_interface.c/h)** | ❌ **不存在** | bt_sal_pan_init/connect/write 等接口无实现 |
| Kconfig CONFIG_BLUETOOTH_PAN | ❌ 不存在 | CMakeLists 引用但无选项定义 → panu 永远不编译 |
| TAP 设备支持 | ⚠️ 半就绪 | nuttx tun.c 支持 IFF_TAP(NET_LL_ETHERNET)，但当前构建 CONFIG_NET_ETHERNET 未开 |
| **LCPU 固件 BREDR 可用性** | ❓ **未真机验证（决定性闸门）** | 历史诊断（docs/11）称 BREDR 命令无响应；协议栈已编入不代表控制器可用 |

## 实现步骤（按依赖顺序）

1. **闸门 0：真机 BREDR 探测**（必须先做，0.5 天）
   - 烧录当前固件，串口发 HCI 命令：Read Local Supported Features、BR/EDR inquiry、
     BREDR 地址读取；或直接尝试与手机经典蓝牙配对。
   - 通过 → 继续；失败 → 标准 PAN 路线终止，回到 GATT 代理（已实现）。
2. 自研 BNEP（约 800-1200 行，基于 l2cap_br PSM 0x000F）：
   - BNEP 连接/断连（Set Up Connection Request）、控制通道（过滤/网关地址）、
     Ethernet 帧封装（协议类型压缩可选，先做非压缩）、多路广播支持可后置。
3. SAL 层 sal_pan_interface.c/h：对接 bt_sal_pan_* 六个接口 → zblue BNEP。
4. Kconfig：加 CONFIG_BLUETOOTH_PAN（依赖 BT_BREDR + BT_L2CAP_BR）。
5. 构建开 CONFIG_NET_ETHERNET + 验证 panu_service TAP 桥接（bt-pan 网卡 + 路由）。

## 风险提示

- 闸门 0 未过就写 BNEP = 白写风险（数百行无测试对象的协议代码）。
- BNEP 无现成参考实现可抄（Zephyr/Linux BlueZ 的 BNEP 是内核态，架构不同，仅可参考协议细节）。
- 即便 BREDR 可用，LCPU 固件怪癖（如 ext-adv 空发）可能同样出现在 BREDR 通道。

## 建议推进方式

- 方案 A（推荐）：先做闸门 0 探测准备（BREDR 探测脚本/NSH 命令，等板子插上即可跑），
  通过后再写 BNEP+SAL。
- 方案 B：现在就并行写 BNEP+SAL 代码（不依赖硬件，可在无板子时推进），
  闸门 0 通过后直接联调；若失败则代码搁置。

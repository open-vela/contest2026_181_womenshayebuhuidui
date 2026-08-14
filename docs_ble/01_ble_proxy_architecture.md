# 01 BLE 代理上网架构（GATT NUS + TUN）

## 总体链路

```
┌──────────────┐   BLE GATT NUS    ┌──────────────────┐   Android VPN   ┌─────────┐
│  SF32LB52 手表 │ ◄──────────────► │  手机 App         │ ◄─────────────► │ 互联网  │
│              │  (LE 广告/连接)   │ (GATT 客户端+隧道) │    (手机网络)    │         │
└──────────────┘                   └──────────────────┘                 └─────────┘
   TUN bt-gatt                          App 内 TUN/VPN 桥
   192.168.55.2                         192.168.55.1 (网关/NAT)
```

- 手表侧：NuttX 网络栈 → TUN 设备（bt-gatt, 192.168.55.2/24）→ ble_gatt_net.c 按
  [len_hi][len_lo][payload] 帧封装 → ble_gatt.c 经 GATT NUS TX 特征 notify 发给手机。
- 手机侧：GATT RX 特征收帧（按 2 字节长度前缀重组）→ App 内 VPN/TUN → 手机系统网络出网；
  回包反向：App TUN → 分片写 RX → 手表重组 → 写入 TUN → NuttX 网络栈。
- 手表所有 DNS/HTTP/HTTPS 流量都落在 192.168.55.0/24 网段，网关是手机。

## 设备端模块（packages/ai_agent/src/infra/）

| 文件 | 职责 |
|------|------|
| ble_gatt.c | GATT 服务端：NUS 服务（6e400001…），广告（legacy ADV_IND，200ms），连接/MTU/CCC 回调，**本次新增分片发送队列** |
| ble_gatt_net.c | TUN 设备 bt-gatt（192.168.55.2/24），帧封装/重组，GATT ↔ TUN 桥接，uv_poll 驱动 |
| ble_cmd_handler.c | JSON 命令通道（ping/status/wifi_config）— 预留 App 控制面 |

## 关键参数

- 服务 UUID：6e400001-b5a3-f393-e0a9-e50e24dcca9e（Nordic NUS 标准）
- RX 特征：6e400002（手机→手表，Write/WriteNR）；TX 特征：6e400003（手表→手机，Notify）
- 广告：LE Legacy ADV_IND（SF32LB52 LCPU 固件对 ext-adv 只回状态不空发，必须用 legacy 命令）
- TUN：bt-gatt 192.168.55.2/24，CONFIG_NET_TUN_PKTSIZE=1518
- 帧协议：大端 16 位长度前缀 + payload（与 SPP 时代 ble_net.c M1 帧一致）
- 默认 MTU 23 → 单包 20B；协商 247 → 单包 244B（分片后）

## 设备端配置开关（board/contest_board/configs/ai_agent/defconfig）

- CONFIG_AI_AGENT_BLE_GATT=y（编入 ble_gatt/ble_gatt_net/ble_cmd_handler）
- CONFIG_BLUETOOTH_GATT=y + CONFIG_BLUETOOTH_GATT_SERVER=y（GATT 服务端）
- CONFIG_BLUETOOTH_BLE_ADV=y、CONFIG_NET_TUN=y、CONFIG_LIBUV=y、CONFIG_BT_MAX_CONN=2
- 注意：CONFIG_BT_ATT_TX_COUNT=5（ATT 发送队列深度，分片队列必须按完成回调逐片发送）

## 数据方向约束（重要）

1. 手机→手表：App 按 (MTU-3) 分片写 RX（已修复：原硬编码 20B）。
2. 手表→手机：ble_gatt_send() 分片 + 完成回调逐片发送（已修复：原超 MTU 直接拒绝）。
3. 两端 MTU 必须一致：App requestMtu(247) 后，双方单包上限均为 244B。

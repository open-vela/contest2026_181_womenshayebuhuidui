# 03 关键突破：BLE 数据通道分片缺陷发现与修复（2026-08-14）

## 发现的缺陷（代码审查，bletest 分支建立后第一项产出）

### 缺陷 A：设备 → 手机方向，所有真实 IP 包被丢弃

- 位置：packages/ai_agent/src/infra/ble_gatt.c 原 ble_gatt_send()
- 原逻辑：len > (MTU-3) 时直接返回 **-EMSGSIZE**，不做切分。
- 后果：TUN 读到的 IP 包（20~1518 字节）经 2 字节帧头封装后 100% 超过
  默认 MTU 23 的 20 字节载荷上限 → **设备无法向手机发送任何 IP 流量**。
  ble_gatt_net.c 注释声称 "(chunked by ble_gatt)"，但实现从未切分。

### 缺陷 B：手机 → 设备方向，分片硬编码 20 字节且不协商 MTU

- 位置：com.agent.coapp-main/.../ble/BleTunnelManager.kt 原 sendFrame()
- 原逻辑：chunkLen = minOf(20, ...) 硬编码；从未调用 gatt.requestMtu()。
- 后果：1500 字节帧需约 75 次写入，吞吐极低；ATT 层没有发挥 247 MTU 能力。

## 修复方案

### 设备端（ble_gatt.c，已实现并编译通过）

- 新增 TX 分片队列：8 × 1600B 槽位（帧头 2B + 最大 IP 包 1518B），环型 FIFO。
- ble_gatt_send()：非阻塞入队（满则丢帧返回 -EBUSY，日志告警），
  由发送泵逐片发送，**单次只发一个 (MTU-3) 分片**，等框架 on_notify_complete
  回调到达后再发下一片（避免打爆 CONFIG_BT_ATT_TX_COUNT=5 的 ATT 队列）。
- 失败策略：notify 提交失败/完成回调带错误 → 丢弃当前槽位继续（TCP 重传兜底）。
- 断连/去初始化：清空队列，重连从干净状态开始。
- 增量编译验证：ninja 全链路通过，SRAM 占用 463016/524288 B（88.31%），
  新增队列约 13KB 在预算内。

### App 端（BleTunnelManager.kt，已实现，待有写权限环境编译）

- onServicesDiscovered 后调用 gatt.requestMtu(247)。
- 新增 onMtuChanged 记录协商 MTU；sendFrame 按 max(20, MTU-3) 分片。
- 协商失败自动回退 20B 分片（兼容不支持的从机）。

### 缺陷 C（同轮发现）：TUN 没有默认路由，公网流量不可达

- 位置：ble_gatt_net.c tun_set_up() 只设置 IP/掩码并 ifup，从未添加默认路由。
- 后果：设备只能到达 192.168.55.0/24（手机），ping 8.8.8.8 / DNS / HTTPS 全部
  "unroutable" — 代理链路形同虚设。
- 修复：tun_set_up(true) 时经 SIOCADDRT（libc addroute）添加
  0.0.0.0/0 → 192.168.55.1 默认路由；down 时 delroute 删除。
  TUN 无 L2/ARP，router 字段仅为占位，原始 IP 包直接进 TUN，由手机 NAT/代理出网。
- 已随本分支编译通过。

## 修复后预期

- 默认 MTU 23：设备→手机 20B/片（可用但慢）；手机→设备同 20B/片。
- MTU 247：设备→手机 244B/片；手机→设备 244B/片 → 吞吐提升 ~12 倍。
- 两端帧协议不变（[len_hi][len_lo][payload]），App 重组逻辑无需改动。

## 遗留风险

1. 手机侧 Android BLE 写入是串行队列，244B × N 次写入的时延未实测。
2. notify 完成回调在 zblue 只在成功时触发 — 失败丢槽策略已覆盖。
3. 队列满丢帧是主动降级策略，长传大文件时 TCP 会触发重传，需真机实测重传率。

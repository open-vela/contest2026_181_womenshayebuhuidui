# 05 App 端透明代理缺陷修复（Round 2，2026-08-15）

> 位置：com.agent.coapp-main/app/src/main/java/com/agent/coapp/vpn/TcpProxy.kt
> 性质：代码审查发现的 5 处协议级缺陷，已全部修复并编译通过（BUILD SUCCESSFUL）。

## 背景

App 的 TcpProxy 是 tun2socks 简化版：手机终止设备发起的 TCP 连接（回 SYN-ACK），
用本机 socket 连接真实目标，转发应用层字节流。设备（NuttX）对 TCP 段序号
**极其严格**（net/tcp/tcp_input.c：段 seq != rcv_nxt 时直接截断/丢弃并 return，
不处理 ACK 字段）——原实现对 NuttX 完全跑不通。

## 缺陷与修复

| # | 缺陷 | 后果 | 修复 |
|---|------|------|------|
| A | 纯 ACK 段 ack=0（handleTcp 两处） | 设备数据永远不被确认 → 无限重传 → 重复字节写入真实 socket，流损坏 | ack = session.clientAck（设备已发数据的下一期望字节） |
| B | 纯 ACK/FIN 段 seq=clientSeq（设备侧序号） | seq 与设备 rcv_nxt 错位 → NuttX 直接丢弃该段，连 ACK 字段都不处理 | seq = session.serverSeq（本代理发送流当前序号） |
| C | SYN-ACK 后 serverSeq 未 +1 | 首个数据段 seq 与 SYN-ACK 相同 → NuttX 按旧段截掉 1 字节，后续数据全部错位 | SYN-ACK 后 session.serverSeq = serverSeq + 1 |
| D | DNS 应答包 src/dst 写反 | 设备收到"源=自己"的包，DNS 解析永远失败 | sendUdp(dstIp, dstPort, srcIp, srcPort, ...) |
| E | 连接失败 RST ack=0 | NuttX SYN_SENT 状态忽略该 RST → 设备只能等 SYN 重传超时（分钟级） | RST ack = clientSeq + 1（确认设备 SYN） |
| F | ICMP 不处理 | 设备 ping 无回应，链路自检不可用 | 新增 ICMP echo reply（App 直接应答，用于链路自检；公网验证仍用 TCP） |

## 验证

- App 编译：GRADLE_USER_HOME 迁到可写目录后 assembleDebug 通过（--offline）。
- 设备侧核对：NuttX tcp_input.c 段接受逻辑、TUN 驱动软校验和（tcp_send.c 计算
  IP/TCP 校验和）、UDP 校验和为 0 时跳过校验 —— 与修复后代理行为一致。

## 遗留说明

- 代理不做 TCP 重传/乱序（依赖 BLE 链路稳定，小流量场景够用）。
- 首包 SYN 的 MSS 选项未回（设备默认 MSS 536，吞吐受限但可用）。
- 新连接建立前 readFromServer 可能先于 SYN-ACK 发出数据（executor 线程时序），
  实际由 clientAck 初始化修正后无碍。

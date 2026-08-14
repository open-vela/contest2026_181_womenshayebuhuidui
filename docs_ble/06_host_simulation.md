# 06 主机端全链路仿真验证（Round 3，2026-08-15）

> 工具：docs_ble/tools/tunnel_sim.py（python3 单文件，无第三方依赖）
> 意义：无真机条件下，对"设备↔手机"两端已修复代码的**协议逻辑**做端到端验证。

## 仿真镜像了什么

| 层 | 镜像对象 |
|----|----------|
| 帧协议 [len_hi][len_lo][payload] | ble_gatt_net.c send_packet / App handleRxStream |
| MTU 分片 (MTU-3) 与重组 | ble_gatt.c TX 队列分片 / BleTunnelManager.sendFrame |
| NuttX 严格段接受（seq != rcv_nxt 丢弃） | nuttx/net/tcp/tcp_input.c |
| 校验和（IP/TCP/ICMP，含伪头） | TcpProxy.kt checksum/tcpChecksum 同算法 |
| 代理 TCP 状态机（SYN 终止、ACK seq=serverSeq/ack=clientAck、SYN-ACK 后 serverSeq+1） | TcpProxy.kt 修复后逻辑 |
| DNS 转发（应答 src/dst 修正） | TcpProxy.kt handleUdp 修复后 |
| ICMP echo 应答 | TcpProxy.kt handleIcmp（Round2 新增） |

## 测试结果（全部 PASS，0 丢包）

```
[PASS] HTTP GET via tunnel (MTU 247)   recv 77B  drops=0
[PASS] 64KB transfer via tunnel        recv 65597B drops=0   ← 跨多帧+多分片+seq/ack 推进
[PASS] ICMP ping answered (MTU 247)    drops=0
[PASS] ICMP at MTU 23 (20B chunks)     drops=0               ← 未协商 MTU 的回退路径
[PASS] DNS query forwarded (direction fix)  udp=45B
```

- 64KB 大文件验证了：分片重组不丢字节、代理 ACK/seq 语义正确（否则 NuttX 严格
  接受规则会触发 drops>0）、双向数据流完整。
- MTU 23 用例验证 20B 分片回退路径（手机不协商 MTU 时也能工作，只是慢）。
- DNS 用例验证 Round2 方向修复：设备收到来自 DNS 服务器地址的应答（旧代码
  设备会收到"来自自己"的包）。

## 复现

```bash
python3 docs_ble/tools/tunnel_sim.py   # 退出码 0 = 全部通过
```

## 结论

两端协议设计（帧 + 分片 + NuttX 严格 TCP 语义 + 代理修复）在链路层可靠传输
假设下可端到端工作。剩余未验证项只剩真机 BLE 射频/协议栈行为
（GATT 连接、MTU 协商、notify 完成回调时序、LCPU 固件兼容性）。

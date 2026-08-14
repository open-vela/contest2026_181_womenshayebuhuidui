package com.agent.coapp.vpn

import android.util.Log
import java.io.IOException
import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.InetAddress
import java.net.InetSocketAddress
import java.net.Socket
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.Executors
import java.util.concurrent.atomic.AtomicInteger

/**
 * 用户态 TCP/UDP 代理（tun2socks 简化版）
 *
 * 设备是隧道中唯一的客户端，因此用「源地址+源端口」作为连接表主键。
 * 设备发起的每个 TCP 连接由本代理终止（回 SYN-ACK），再用本机 socket
 * 连接真实目标，转发应用层字节流。
 *
 * 简化边界：
 *  - 仅 IPv4
 *  - TCP 不做重传/乱序处理（蓝牙链路稳定 + LLM 小流量场景足够）
 *  - UDP 仅转发 DNS（53 端口）到公网 DNS
 */
class TcpProxy(
    private val writeToTun: (ByteArray) -> Unit,
    private val dnsServer: String = "8.8.8.8"
) {

    companion object {
        private const val TAG = "TcpProxy"

        private const val IP_HEADER_LEN = 20
        private const val TCP_HEADER_LEN = 20
        private const val PROTO_TCP = 6
        private const val PROTO_UDP = 17
        private const val PROTO_ICMP = 1
        private const val ICMP_ECHO_REQUEST = 8
        private const val ICMP_ECHO_REPLY = 0

        private const val TCP_SYN = 0x02
        private const val TCP_RST = 0x04
        private const val TCP_ACK = 0x10
        private const val TCP_PSH = 0x08
        private const val TCP_FIN = 0x01

        private const val DNS_PORT = 53
    }

    /** 连接表项：设备侧五元组（源地址:源端口）→ 会话 */
    private data class ConntrackKey(val srcIp: Int, val srcPort: Int)

    private class TcpSession(val socket: Socket, val dstIp: Int, val dstPort: Int) {
        // 设备侧（客户端）视角的 seq/ack
        var clientSeq = 0L
        var clientAck = 0L
        // 服务端（本代理）视角的 seq/ack
        var serverSeq = 0L
        var serverAck = 0L
        var synAcked = false
        var finSent = false
        val readThread = Thread()
    }

    private val sessions = ConcurrentHashMap<ConntrackKey, TcpSession>()
    private val executor = Executors.newCachedThreadPool()
    private val ipId = AtomicInteger(0x1000)
    private val dnsSocket = DatagramSocket()

    /** 处理来自 TUN 的完整 IP 包 */
    fun processPacket(packet: ByteArray) {
        if (packet.size < IP_HEADER_LEN) return
        val version = (packet[0].toInt() shr 4) and 0x0f
        if (version != 4) return

        val ihl = (packet[0].toInt() and 0x0f) * 4
        if (ihl < IP_HEADER_LEN || packet.size < ihl) return

        val protocol = packet[9].toInt() and 0xff
        when (protocol) {
            PROTO_TCP -> handleTcp(packet, ihl)
            PROTO_UDP -> handleUdp(packet, ihl)
            PROTO_ICMP -> handleIcmp(packet, ihl)
        }
    }

    // ── ICMP（仅 echo）───────────────────────────────────────────
    // 设备侧 ping 由本代理直接应答（链路自检用）。BLE 隧道不转发
    // 真实 ICMP（Android 应用层无法收发 ICMP），回复内容只证明
    // 链路与代理路径通畅；公网可达性请用 TCP（curl/LLM）验证。

    private fun handleIcmp(packet: ByteArray, ihl: Int) {
        if (packet.size < ihl + 8) return
        val type = packet[ihl].toInt() and 0xff
        if (type != ICMP_ECHO_REQUEST) return

        val srcIp = readInt(packet, 12)
        val dstIp = readInt(packet, 16)
        val icmpLen = packet.size - ihl
        val reply = ByteArray(icmpLen)

        // ICMP 头：type=0(echo reply), code=0, checksum 先置 0
        reply[0] = ICMP_ECHO_REPLY.toByte()
        reply[1] = 0
        reply[2] = 0; reply[3] = 0
        // id/seq/载荷原样带回
        System.arraycopy(packet, ihl + 4, reply, 4, icmpLen - 4)

        val csum = checksum(reply, 0, icmpLen)
        reply[2] = (csum shr 8).toByte(); reply[3] = (csum and 0xff).toByte()

        // IP 头：源/目的对调，重新计算 IP 校验和
        val totalLen = IP_HEADER_LEN + icmpLen
        val out = ByteArray(totalLen)
        System.arraycopy(packet, 0, out, 0, IP_HEADER_LEN)
        out[2] = (totalLen shr 8).toByte(); out[3] = (totalLen and 0xff).toByte()
        writeInt(out, 12, dstIp)
        writeInt(out, 16, srcIp)
        out[10] = 0; out[11] = 0
        val ipCsum = checksum(out, 0, IP_HEADER_LEN)
        out[10] = (ipCsum shr 8).toByte(); out[11] = (ipCsum and 0xff).toByte()
        System.arraycopy(reply, 0, out, IP_HEADER_LEN, icmpLen)

        writeToTun(out)
    }

    // ── TCP ─────────────────────────────────────────────────────

    private fun handleTcp(packet: ByteArray, ihl: Int) {
        if (packet.size < ihl + TCP_HEADER_LEN) return

        val srcIp = readInt(packet, 12)
        val dstIp = readInt(packet, 16)
        val srcPort = ((packet[ihl].toInt() and 0xff) shl 8) or (packet[ihl + 1].toInt() and 0xff)
        val dstPort = ((packet[ihl + 2].toInt() and 0xff) shl 8) or (packet[ihl + 3].toInt() and 0xff)
        val seq = readUInt(packet, ihl + 4)
        val ack = readUInt(packet, ihl + 8)
        val dataOffset = ((packet[ihl + 12].toInt() shr 4) and 0x0f) * 4
        val flags = packet[ihl + 13].toInt() and 0x3f
        val payloadLen = packet.size - ihl - dataOffset

        val key = ConntrackKey(srcIp, srcPort)

        if ((flags and TCP_SYN) != 0 && (flags and TCP_ACK) == 0) {
            // 新连接：发起真实连接，SYN-ACK 稍后回
            if (sessions.containsKey(key)) return
            Log.d(TAG, "SYN ${ipStr(srcIp)}:$srcPort -> ${ipStr(dstIp)}:$dstPort")
            openSession(key, srcIp, srcPort, dstIp, dstPort, seq)
            return
        }

        val session = sessions[key] ?: return

        if ((flags and TCP_RST) != 0) {
            closeSession(key, session)
            return
        }

        // 应用数据 → 真实 socket
        if (payloadLen > 0 && dataOffset >= TCP_HEADER_LEN) {
            session.clientAck = (session.clientAck.coerceAtLeast(seq + payloadLen))
            val data = packet.copyOfRange(ihl + dataOffset, packet.size)
            try {
                session.socket.getOutputStream().write(data)
                session.socket.getOutputStream().flush()
            } catch (e: IOException) {
                closeSession(key, session)
                return
            }
        }

        // FIN → 半关闭
        if ((flags and TCP_FIN) != 0) {
            try {
                session.socket.shutdownOutput()
            } catch (_: IOException) {
            }
            sendTcp(session, srcIp, srcPort, session.serverSeq, session.clientAck, TCP_FIN or TCP_ACK)
            session.finSent = true
        }

        // 回复 ACK（确认收到的数据）：
        //   seq 必须 = serverSeq（本代理发送流当前序号，NuttX 要求段 seq
        //   等于设备端 rcv_nxt 否则直接丢弃）；ack 必须 = clientAck
        //   （设备已发数据的下一个期望字节），不能为 0，否则设备永远
        //   收不到数据确认，会无限重传并把重复字节写入真实 socket。
        if (payloadLen > 0 || (flags and TCP_ACK) != 0) {
            sendTcp(session, srcIp, srcPort, session.serverSeq, session.clientAck, TCP_ACK)
        }
    }

    private fun openSession(
        key: ConntrackKey, srcIp: Int, srcPort: Int,
        dstIp: Int, dstPort: Int, clientSeq: Long
    ) {
        executor.execute {
            try {
                val socket = Socket()
                socket.tcpNoDelay = true
                socket.connect(InetSocketAddress(ipStr(dstIp), dstPort), 10_000)

                // 随机初始服务端序号
                val serverSeq = (System.nanoTime() and 0x7fffffff).toLong()
                val session = TcpSession(socket, dstIp, dstPort).apply {
                    this.clientSeq = clientSeq + 1
                    this.clientAck = clientSeq + 1
                    this.serverSeq = serverSeq
                    this.serverAck = clientSeq + 1
                }
                sessions[key] = session

                // 回 SYN-ACK（SYN 消耗一个序号，之后 serverSeq 必须 +1，
                // 否则首段数据的 seq 与设备端 rcv_nxt 错位会被 NuttX 丢弃/截断）
                sendTcp(session, srcIp, srcPort, serverSeq, clientSeq + 1, TCP_SYN or TCP_ACK)
                session.clientSeq = clientSeq + 1
                session.serverSeq = serverSeq + 1
                session.synAcked = true

                // 启动服务端读线程
                session.readThread.run { readFromServer(key, session) }
            } catch (e: IOException) {
                Log.w(TAG, "connect ${ipStr(dstIp)}:$dstPort failed: ${e.message}")
                // 回 RST 让设备端快速失败；ack 必须确认设备 SYN
                // (clientSeq+1)，否则 NuttX 在 SYN_SENT 状态会忽略 RST，
                // 设备只能等 SYN 重传超时。
                sendTcpRaw(srcIp, srcPort, dstIp, dstPort, 0, clientSeq + 1, TCP_RST or TCP_ACK)
            }
        }
    }

    private fun readFromServer(key: ConntrackKey, session: TcpSession) {
        val buf = ByteArray(4096)
        try {
            while (true) {
                val n = session.socket.getInputStream().read(buf)
                if (n < 0) break
                if (n > 0) {
                    val data = buf.copyOf(n)
                    // 数据封装为 TCP 段发回设备
                    sendTcpData(key, session, data)
                }
            }
        } catch (e: IOException) {
            Log.d(TAG, "server read ended: ${e.message}")
        } finally {
            closeSession(key, session)
        }
    }

    private fun sendTcpData(key: ConntrackKey, session: TcpSession, data: ByteArray) {
        val srcIp = key.srcIp
        val srcPort = key.srcPort
        val chunkSize = 1024
        var offset = 0
        while (offset < data.size) {
            val chunkLen = minOf(chunkSize, data.size - offset)
            val chunk = data.copyOfRange(offset, offset + chunkLen)
            sendTcp(session, srcIp, srcPort, session.serverSeq, session.clientAck, TCP_PSH or TCP_ACK, chunk)
            session.serverSeq += chunkLen
            offset += chunkLen
        }
    }

    private fun closeSession(key: ConntrackKey, session: TcpSession) {
        sessions.remove(key)
        try {
            session.socket.close()
        } catch (_: IOException) {
        }
        // 通知设备端连接关闭（若尚未 FIN）
        if (!session.finSent) {
            sendTcp(session, key.srcIp, key.srcPort, session.serverSeq, session.clientAck, TCP_FIN or TCP_ACK)
        }
    }

    /** 构造并发送 TCP 段（无载荷） */
    private fun sendTcp(
        session: TcpSession, srcIp: Int, srcPort: Int,
        seq: Long, ack: Long, flags: Int
    ) {
        sendTcp(session, srcIp, srcPort, seq, ack, flags, ByteArray(0))
    }

    /** 构造并发送 TCP 段（含载荷） */
    private fun sendTcp(
        session: TcpSession, srcIp: Int, srcPort: Int,
        seq: Long, ack: Long, flags: Int, payload: ByteArray
    ) {
        sendTcpRaw(srcIp, srcPort, session.dstIp, session.dstPort, seq, ack, flags, payload)
    }

    private fun sendTcpRaw(
        srcIp: Int, srcPort: Int, dstIp: Int, dstPort: Int,
        seq: Long, ack: Long, flags: Int, payload: ByteArray = ByteArray(0)
    ) {
        val totalLen = IP_HEADER_LEN + TCP_HEADER_LEN + payload.size
        val packet = ByteArray(totalLen)

        // IP 头
        packet[0] = 0x45
        packet[2] = (totalLen shr 8).toByte(); packet[3] = (totalLen and 0xff).toByte()
        val id = ipId.getAndIncrement() and 0xffff
        packet[4] = (id shr 8).toByte(); packet[5] = (id and 0xff).toByte()
        packet[8] = 64 // TTL
        packet[9] = PROTO_TCP.toByte()
        writeInt(packet, 12, srcIp)
        writeInt(packet, 16, dstIp)
        val ipChecksum = checksum(packet, 0, IP_HEADER_LEN)
        packet[10] = (ipChecksum shr 8).toByte(); packet[11] = (ipChecksum and 0xff).toByte()

        // TCP 头
        val tcpOff = IP_HEADER_LEN
        packet[tcpOff] = (srcPort shr 8).toByte(); packet[tcpOff + 1] = (srcPort and 0xff).toByte()
        packet[tcpOff + 2] = (dstPort shr 8).toByte(); packet[tcpOff + 3] = (dstPort and 0xff).toByte()
        writeUInt(packet, tcpOff + 4, seq)
        writeUInt(packet, tcpOff + 8, ack)
        packet[tcpOff + 12] = (5 shl 4).toByte() // data offset = 5
        packet[tcpOff + 13] = flags.toByte()
        // window size 65535
        packet[tcpOff + 14] = 0xff.toByte(); packet[tcpOff + 15] = 0xff.toByte()

        // 载荷
        if (payload.isNotEmpty()) {
            System.arraycopy(payload, 0, packet, tcpOff + TCP_HEADER_LEN, payload.size)
        }

        // TCP 校验和（伪头 + TCP 段）
        val tcpChecksum = tcpChecksum(srcIp, dstIp, packet, tcpOff, TCP_HEADER_LEN + payload.size)
        packet[tcpOff + 16] = (tcpChecksum shr 8).toByte(); packet[tcpOff + 17] = (tcpChecksum and 0xff).toByte()

        writeToTun(packet)
    }

    // ── UDP（仅 DNS）────────────────────────────────────────────

    private fun handleUdp(packet: ByteArray, ihl: Int) {
        if (packet.size < ihl + 8) return
        val srcIp = readInt(packet, 12)
        val dstIp = readInt(packet, 16)
        val srcPort = ((packet[ihl].toInt() and 0xff) shl 8) or (packet[ihl + 1].toInt() and 0xff)
        val dstPort = ((packet[ihl + 2].toInt() and 0xff) shl 8) or (packet[ihl + 3].toInt() and 0xff)
        val udpLen = ((packet[ihl + 4].toInt() and 0xff) shl 8) or (packet[ihl + 5].toInt() and 0xff)
        if (udpLen < 8 || packet.size < ihl + udpLen) return

        // 只转发 DNS 查询（设备侧 DNS 请求目的端口 53）
        if (dstPort != DNS_PORT) return

        val payload = packet.copyOfRange(ihl + 8, ihl + udpLen)
        executor.execute {
            try {
                val query = DatagramPacket(payload, payload.size, InetAddress.getByName(dnsServer), DNS_PORT)
                dnsSocket.soTimeout = 5000
                dnsSocket.send(query)
                val respBuf = ByteArray(512)
                val response = DatagramPacket(respBuf, respBuf.size)
                dnsSocket.receive(response)
                // 应答包方向：src=DNS 服务器(dstIp:53)，dst=设备(srcIp:srcPort)。
                // 原代码 src/dst 写反，设备会收到"来自自己"的包而被丢弃。
                sendUdp(dstIp, dstPort, srcIp, srcPort, response.data.copyOf(response.length))
            } catch (e: IOException) {
                Log.w(TAG, "DNS relay failed: ${e.message}")
            }
        }
    }

    private fun sendUdp(srcIp: Int, srcPort: Int, dstIp: Int, dstPort: Int, payload: ByteArray) {
        val udpLen = 8 + payload.size
        val totalLen = IP_HEADER_LEN + udpLen
        val packet = ByteArray(totalLen)

        packet[0] = 0x45
        packet[2] = (totalLen shr 8).toByte(); packet[3] = (totalLen and 0xff).toByte()
        val id = ipId.getAndIncrement() and 0xffff
        packet[4] = (id shr 8).toByte(); packet[5] = (id and 0xff).toByte()
        packet[8] = 64
        packet[9] = PROTO_UDP.toByte()
        writeInt(packet, 12, srcIp)
        writeInt(packet, 16, dstIp)
        val ipChecksum = checksum(packet, 0, IP_HEADER_LEN)
        packet[10] = (ipChecksum shr 8).toByte(); packet[11] = (ipChecksum and 0xff).toByte()

        val udpOff = IP_HEADER_LEN
        packet[udpOff] = (srcPort shr 8).toByte(); packet[udpOff + 1] = (srcPort and 0xff).toByte()
        packet[udpOff + 2] = (dstPort shr 8).toByte(); packet[udpOff + 3] = (dstPort and 0xff).toByte()
        packet[udpOff + 4] = (udpLen shr 8).toByte(); packet[udpOff + 5] = (udpLen and 0xff).toByte()
        if (payload.isNotEmpty()) {
            System.arraycopy(payload, 0, packet, udpOff + 8, payload.size)
        }
        // UDP 校验和可置 0（IPv4 允许）
        writeToTun(packet)
    }

    // ── 校验和与字节工具 ─────────────────────────────────────────

    private fun checksum(data: ByteArray, start: Int, len: Int): Int {
        var sum = 0
        var i = start
        val end = start + len
        while (i < end - 1) {
            sum += ((data[i].toInt() and 0xff) shl 8) or (data[i + 1].toInt() and 0xff)
            i += 2
        }
        if (i < end) {
            sum += (data[i].toInt() and 0xff) shl 8
        }
        while (sum > 0xffff) {
            sum = (sum and 0xffff) + (sum shr 16)
        }
        return sum.inv() and 0xffff
    }

    private fun tcpChecksum(srcIp: Int, dstIp: Int, segment: ByteArray, start: Int, len: Int): Int {
        var sum = 0L
        // 伪头
        sum += (srcIp ushr 16) and 0xffff
        sum += srcIp and 0xffff
        sum += (dstIp ushr 16) and 0xffff
        sum += dstIp and 0xffff
        sum += PROTO_TCP
        sum += len
        // 段
        var i = start
        val end = start + len
        while (i < end - 1) {
            sum += ((segment[i].toInt() and 0xff) shl 8) or (segment[i + 1].toInt() and 0xff)
            i += 2
        }
        if (i < end) {
            sum += (segment[i].toInt() and 0xff) shl 8
        }
        while ((sum shr 16) > 0) {
            sum = (sum and 0xffff) + (sum shr 16)
        }
        return (sum.inv() and 0xffff).toInt()
    }

    private fun readInt(data: ByteArray, offset: Int): Int {
        return ((data[offset].toInt() and 0xff) shl 24) or
            ((data[offset + 1].toInt() and 0xff) shl 16) or
            ((data[offset + 2].toInt() and 0xff) shl 8) or
            (data[offset + 3].toInt() and 0xff)
    }

    private fun writeInt(data: ByteArray, offset: Int, value: Int) {
        data[offset] = (value ushr 24).toByte()
        data[offset + 1] = (value ushr 16).toByte()
        data[offset + 2] = (value ushr 8).toByte()
        data[offset + 3] = value.toByte()
    }

    private fun readUInt(data: ByteArray, offset: Int): Long = readInt(data, offset).toLong() and 0xffffffffL

    private fun writeUInt(data: ByteArray, offset: Int, value: Long) {
        writeInt(data, offset, (value and 0xffffffffL).toInt())
    }

    private fun ipStr(ip: Int): String =
        "${(ip ushr 24) and 0xff}.${(ip ushr 16) and 0xff}.${(ip ushr 8) and 0xff}.${ip and 0xff}"

    /** 关闭代理（释放资源） */
    fun shutdown() {
        sessions.values.forEach { runCatching { it.socket.close() } }
        sessions.clear()
        runCatching { dnsSocket.close() }
        executor.shutdownNow()
    }
}

#!/usr/bin/env python3
"""
BLE 代理隧道端到端协议仿真 (docs_ble Round 3)
==============================================
在没有真机的情况下，用 Python 精确镜像两端已修复代码的协议逻辑：
  设备侧 (镜像 NuttX + ble_gatt_net.c + ble_gatt.c)：
    - 帧协议 [len_hi][len_lo][payload]，MTU 分片 (MTU-3)，重组
    - NuttX 严格段接受规则: seq != rcv_nxt 的段直接丢弃
    - 软件校验和 (IP/TCP)
  手机侧 (镜像 TcpProxy.kt 修复后逻辑):
    - SYN 终止 + 真实 socket 转发, SYN-ACK 后 serverSeq+1
    - 纯 ACK: seq=serverSeq, ack=clientAck
    - DNS 转发 (方向修正后), ICMP echo 应答
测试: HTTP GET / 大文件 / ICMP ping / DNS 查询 全部经隧道往返。
"""
import socket, struct, threading, time, queue, sys, os

# ---------- 校验和 (与 TcpProxy.kt checksum/tcpChecksum 相同算法) ----------
def ip_checksum(data: bytes) -> int:
    s = 0
    for i in range(0, len(data) - 1, 2):
        s += (data[i] << 8) | data[i+1]
    if len(data) & 1:
        s += data[-1] << 8
    while s > 0xffff:
        s = (s & 0xffff) + (s >> 16)
    return (~s) & 0xffff

def tcp_checksum(src, dst, tcp_seg: bytes) -> int:
    s = 0
    s += (src >> 16) & 0xffff; s += src & 0xffff
    s += (dst >> 16) & 0xffff; s += dst & 0xffff
    s += 6  # TCP
    s += len(tcp_seg)
    for i in range(0, len(tcp_seg) - 1, 2):
        s += (tcp_seg[i] << 8) | tcp_seg[i+1]
    if len(tcp_seg) & 1:
        s += tcp_seg[-1] << 8
    while (s >> 16) > 0:
        s = (s & 0xffff) + (s >> 16)
    return (~s) & 0xffff

def ipv4_pkt(src, dst, proto, payload: bytes, ident=0x1000) -> bytes:
    hdr = struct.pack("!BBHHHBBH4s4s", 0x45, 0, 20 + len(payload),
                      ident & 0xffff, 0, 64, proto, 0,
                      socket.inet_aton(src), socket.inet_aton(dst))
    csum = ip_checksum(hdr)
    hdr = hdr[:10] + struct.pack("!H", csum) + hdr[12:]
    return hdr + payload

def tcp_seg(src_ip, src_port, dst_ip, dst_port, seq, ack, flags, payload=b""):
    seg = struct.pack("!HHIIBBHHH", src_port, dst_port, seq & 0xffffffff,
                      ack & 0xffffffff, 5 << 4, flags, 65535, 0, 0)
    seg += payload
    csum = tcp_checksum(int.from_bytes(socket.inet_aton(src_ip), "big"),
                        int.from_bytes(socket.inet_aton(dst_ip), "big"), seg)
    seg = seg[:16] + struct.pack("!H", csum) + seg[18:]
    return seg

# ---------- BLE 链路仿真: 可靠有序, 每"包"≤ MTU-3 ----------
class BleLink:
    def __init__(self, mtu=247):
        self.mtu = mtu
        self.chunk = mtu - 3
        self.to_device = queue.Queue()  # phone -> device
        self.to_phone = queue.Queue()   # device -> phone
        self.stats = {"frames": 0, "chunks": 0}
    def phone_write(self, frame: bytes):
        for i in range(0, len(frame), self.chunk):
            self.to_device.put(frame[i:i+self.chunk])
            self.stats["chunks"] += 1
        self.stats["frames"] += 1
    def device_notify(self, frame: bytes):
        for i in range(0, len(frame), self.chunk):
            self.to_phone.put(frame[i:i+self.chunk])
            self.stats["chunks"] += 1
        self.stats["frames"] += 1

def frame(ip_pkt: bytes) -> bytes:
    return struct.pack("!H", len(ip_pkt)) + ip_pkt

class FrameAssembler:
    """镜像 ble_gatt_net_handle_rx_stream / handleRxStream"""
    def __init__(self):
        self.buf = b""
        self.packets = []
    def push(self, data: bytes):
        self.buf += data
        off = 0
        while len(self.buf) - off >= 2:
            plen = (self.buf[off] << 8) | self.buf[off+1]
            if plen == 0 or plen > 1518:
                self.buf = b""; return  # resync
            if len(self.buf) - off < 2 + plen:
                break
            self.packets.append(self.buf[off+2:off+2+plen])
            off += 2 + plen
        if off:
            self.buf = self.buf[off:]

# ---------- 手机侧代理 (镜像 TcpProxy.kt 修复后) ----------
PROTO_TCP, PROTO_UDP, PROTO_ICMP = 6, 17, 1
SYN, RST, ACK, PSH, FIN = 0x02, 0x04, 0x10, 0x08, 0x01

class PhoneProxy:
    def __init__(self, link: BleLink):
        self.link = link
        self.sessions = {}
        self.ip_id = 0x2000
        self.rx = FrameAssembler()
        self._stop = False
    def start_pump(self):
        """后台线程持续收取链路分片并处理 IP 包"""
        def loop():
            while not self._stop:
                self.pump(timeout=0.1)
        threading.Thread(target=loop, daemon=True).start()
    def pump(self, timeout=0.1):
        """从 BLE 链路收取分片并重组为 IP 包处理"""
        got = False
        while True:
            try:
                chunk = self.link.to_phone.get(timeout=timeout)
                got = True
            except queue.Empty:
                break
            self.rx.push(chunk)
            while self.rx.packets:
                self.process(self.rx.packets.pop(0))
        return got
    def process(self, ip_pkt: bytes):
        if len(ip_pkt) < 20 or (ip_pkt[0] >> 4) != 4:
            return
        ihl = (ip_pkt[0] & 0x0f) * 4
        proto = ip_pkt[9]
        if proto == PROTO_TCP:
            self.handle_tcp(ip_pkt, ihl)
        elif proto == PROTO_UDP:
            self.handle_udp(ip_pkt, ihl)
        elif proto == PROTO_ICMP:
            self.handle_icmp(ip_pkt, ihl)
    def send_to_device(self, ip_pkt: bytes):
        self.link.phone_write(frame(ip_pkt))
    def parse_tcp(self, pkt, ihl):
        src = socket.inet_ntoa(pkt[12:16]); dst = socket.inet_ntoa(pkt[16:20])
        sp, dp = struct.unpack("!HH", pkt[ihl:ihl+4])
        seq, ack = struct.unpack("!II", pkt[ihl+4:ihl+12])
        doff = (pkt[ihl+12] >> 4) * 4
        flags = pkt[ihl+13] & 0x3f
        return src, dst, sp, dp, seq, ack, doff, flags
    def handle_tcp(self, pkt, ihl):
        src, dst, sp, dp, seq, ack, doff, flags = self.parse_tcp(pkt, ihl)
        payload = pkt[ihl+doff:]
        key = (src, sp)
        if flags & SYN and not (flags & ACK):
            if key not in self.sessions:
                threading.Thread(target=self.open_session, args=(key, src, sp, dst, dp, seq), daemon=True).start()
            return
        s = self.sessions.get(key)
        if not s: return
        if flags & RST:
            self.close_session(key, s); return
        if payload and doff >= 20:
            s["client_ack"] = max(s["client_ack"], (seq + len(payload)) & 0xffffffff)
            try:
                s["sock"].sendall(payload)
            except OSError:
                self.close_session(key, s); return
        if flags & FIN:
            try: s["sock"].shutdown(socket.SHUT_WR)
            except OSError: pass
            self.send_tcp(key, s, s["server_seq"], s["client_ack"], FIN | ACK)
            s["fin_sent"] = True
        if payload or (flags & ACK):
            self.send_tcp(key, s, s["server_seq"], s["client_ack"], ACK)
    def open_session(self, key, src, sp, dst, dp, client_seq):
        try:
            sock = socket.create_connection((dst, dp), timeout=10)
            sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            server_seq = int.from_bytes(os.urandom(4), "big") & 0x7fffffff
            s = {"sock": sock, "dst": dst, "dp": dp,
                 "client_seq": (client_seq + 1) & 0xffffffff,
                 "client_ack": (client_seq + 1) & 0xffffffff,
                 "server_seq": server_seq,
                 "server_ack": (client_seq + 1) & 0xffffffff,
                 "fin_sent": False}
            self.sessions[key] = s
            self.send_tcp(key, s, server_seq, (client_seq + 1) & 0xffffffff, SYN | ACK)
            s["server_seq"] = (server_seq + 1) & 0xffffffff  # SYN 消耗一个序号
            threading.Thread(target=self.read_server, args=(key, s), daemon=True).start()
        except OSError:
            self.send_rst(src, sp, dst, dp, (client_seq + 1) & 0xffffffff)
    def read_server(self, key, s):
        try:
            while True:
                data = s["sock"].recv(4096)
                if not data: break
                off = 0
                while off < len(data):
                    chunk = data[off:off+1024]
                    self.send_tcp(key, s, s["server_seq"], s["client_ack"], PSH | ACK, chunk)
                    s["server_seq"] = (s["server_seq"] + len(chunk)) & 0xffffffff
                    off += len(chunk)
        except OSError:
            pass
        finally:
            self.close_session(key, s)
    def close_session(self, key, s):
        self.sessions.pop(key, None)
        try: s["sock"].close()
        except OSError: pass
        if not s["fin_sent"]:
            self.send_tcp(key, s, s["server_seq"], s["client_ack"], FIN | ACK)
    def send_tcp(self, key, s, seq, ack, flags, payload=b""):
        src, sp = key
        seg = tcp_seg(s["dst"], s["dp"], src, sp, seq, ack, flags, payload)
        self.send_to_device(ipv4_pkt(s["dst"], src, PROTO_TCP, seg, self.ip_id))
        self.ip_id += 1
    def send_rst(self, src, sp, dst, dp, ack):
        seg = tcp_seg(src, sp, dst, dp, 0, ack, RST | ACK)
        self.send_to_device(ipv4_pkt(dst, src, PROTO_TCP, seg, self.ip_id))
        self.ip_id += 1
    # ---- UDP (仅 DNS) ----
    def handle_udp(self, pkt, ihl):
        src = socket.inet_ntoa(pkt[12:16]); dst = socket.inet_ntoa(pkt[16:20])
        sp, dp, ulen = struct.unpack("!HHH", pkt[ihl:ihl+6])
        if dp != 53: return
        payload = pkt[ihl+8:ihl+ulen]
        def relay():
            try:
                u = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                u.settimeout(5)
                dns_port = getattr(self, "dns_port", 53)
                u.sendto(payload, (self.dns_server, dns_port))
                resp, _ = u.recvfrom(512)
                # 应答: src=DNS服务器(dstIp:53), dst=设备(srcIp:srcPort) — 修复D
                up = struct.pack("!HHHH", dp, sp, 8 + len(resp), 0) + resp
                self.send_to_device(ipv4_pkt(dst, src, PROTO_UDP, up, self.ip_id))
                self.ip_id += 1
            except OSError:
                pass
        threading.Thread(target=relay, daemon=True).start()
    # ---- ICMP echo (Round2 新增) ----
    def handle_icmp(self, pkt, ihl):
        if pkt[ihl] != 8: return
        src = socket.inet_ntoa(pkt[12:16]); dst = socket.inet_ntoa(pkt[16:20])
        icmp = pkt[ihl:]
        reply = bytes([0, 0, 0, 0]) + icmp[4:]
        csum = ip_checksum(reply)
        reply = reply[:2] + struct.pack("!H", csum) + reply[4:]
        self.send_to_device(ipv4_pkt(dst, src, PROTO_ICMP, reply, self.ip_id))
        self.ip_id += 1

# ---------- 设备侧 (镜像 NuttX 严格接受规则 + 帧封装) ----------
class DeviceStack:
    def __init__(self, link: BleLink):
        self.link = link
        self.rx = FrameAssembler()
        self.conns = {}
        self.drops = 0
        self.last_icmp = None
        self.last_udp = None
    def run_rx(self):
        while True:
            try:
                chunk = self.link.to_device.get(timeout=0.2)
            except queue.Empty:
                return
            self.rx.push(chunk)
            while self.rx.packets:
                self.process_ip(self.rx.packets.pop(0))
    def process_ip(self, pkt):
        if len(pkt) < 20: return
        ihl = (pkt[0] & 0x0f) * 4
        proto = pkt[9]
        if proto == PROTO_ICMP:
            self.on_icmp(pkt, ihl)
        elif proto == PROTO_TCP:
            self.on_tcp(pkt, ihl)
        elif proto == PROTO_UDP:
            self.last_udp = pkt[ihl+8:]
    def on_icmp(self, pkt, ihl):
        if ip_checksum(pkt[ihl:]) != 0:
            self.drops += 1; return
        self.last_icmp = pkt[ihl:]
    def on_tcp(self, pkt, ihl):
        src = socket.inet_ntoa(pkt[12:16]); dst = socket.inet_ntoa(pkt[16:20])
        sp, dp = struct.unpack("!HH", pkt[ihl:ihl+4])
        seq, ack = struct.unpack("!II", pkt[ihl+4:ihl+12])
        doff = (pkt[ihl+12] >> 4) * 4
        flags = pkt[ihl+13] & 0x3f
        payload = pkt[ihl+doff:]
        seg = pkt[ihl:]
        if tcp_checksum(int.from_bytes(pkt[12:16], "big"),
                        int.from_bytes(pkt[16:20], "big"), seg) != 0:
            self.drops += 1; return
        key = (src, sp)
        if flags & SYN and flags & ACK:
            s = self.conns.setdefault(key, {"snd_nxt": 0, "rcv_nxt": 0, "established": False, "recv": b""})
            s["rcv_nxt"] = (seq + 1) & 0xffffffff
            s["established"] = True
            return
        s = self.conns.get(key)
        if not s or not s["established"]: return
        if seq != s["rcv_nxt"]:
            self.drops += 1
            return
        if payload:
            s["recv"] += payload
            s["rcv_nxt"] = (s["rcv_nxt"] + len(payload)) & 0xffffffff
        self.send_tcp(src, sp, dst, dp, s["snd_nxt"], s["rcv_nxt"], ACK)
        if flags & FIN:
            s["rcv_nxt"] = (s["rcv_nxt"] + 1) & 0xffffffff
            self.send_tcp(src, sp, dst, dp, s["snd_nxt"], s["rcv_nxt"], ACK)
            s["established"] = False
    def send_tcp(self, src_ip, src_port, dst_ip, dst_port, seq, ack, flags, payload=b""):
        seg = tcp_seg(src_ip, src_port, dst_ip, dst_port, seq, ack, flags, payload)
        self.link.device_notify(frame(ipv4_pkt(src_ip, dst_ip, PROTO_TCP, seg)))
    def send_icmp_echo(self, dst_ip, ident=0x1234, seq=1):
        body = struct.pack("!HH", ident, seq) + b"pingpayload"
        icmp = bytes([8, 0, 0, 0]) + body
        csum = ip_checksum(icmp)
        icmp = icmp[:2] + struct.pack("!H", csum) + icmp[4:]
        self.link.device_notify(frame(ipv4_pkt("192.168.55.2", dst_ip, PROTO_ICMP, icmp)))
    def send_dns_query(self, dst_ip, qname=b"example.com"):
        txid = 0xabcd
        q = struct.pack("!HHHHHH", txid, 0x0100, 1, 0, 0, 0)
        for part in qname.split(b"."):
            q += bytes([len(part)]) + part
        q += b"\x00" + struct.pack("!HH", 1, 1)
        up = struct.pack("!HHHH", 12345, 53, 8 + len(q), 0) + q
        self.link.device_notify(frame(ipv4_pkt("192.168.55.2", dst_ip, PROTO_UDP, up)))

def tcp_flow(link, dev, src_port, dst_port, request, expect_substr, wait=1.0, body_len=None):
    """设备发起 TCP: SYN -> 数据 -> 收响应, 返回 (ok, recv_bytes, drops)"""
    key = ("127.0.0.1", dst_port)
    dev.conns[key] = {"snd_nxt": 1000 + src_port, "rcv_nxt": 0, "established": False, "recv": b""}
    s = dev.conns[key]
    dev.send_tcp("192.168.55.2", src_port, "127.0.0.1", dst_port, s["snd_nxt"], 0, SYN)
    dev.run_rx()
    if not s["established"]:
        return False, 0, dev.drops, "no SYN-ACK"
    s["snd_nxt"] = (s["snd_nxt"] + 1) & 0xffffffff
    dev.send_tcp("192.168.55.2", src_port, "127.0.0.1", dst_port,
                 s["snd_nxt"], s["rcv_nxt"], PSH | ACK, request)
    s["snd_nxt"] = (s["snd_nxt"] + len(request)) & 0xffffffff
    deadline = time.time() + wait
    while time.time() < deadline:
        dev.run_rx()
        if expect_substr in s["recv"]:
            break
        time.sleep(0.05)
    ok = expect_substr in s["recv"]
    if body_len is not None:
        ok = ok and s["recv"].count(b"X") >= body_len
    return ok, len(s["recv"]), dev.drops, ""

def run_tests():
    results = []
    # 1. HTTP GET 小请求 (MTU 247)
    httpd = socket.socket(); httpd.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    httpd.bind(("127.0.0.1", 0)); httpd.listen(1)
    port = httpd.getsockname()[1]
    def http_server():
        c, _ = httpd.accept()
        c.recv(4096)
        body = b"HELLO-FROM-INTERNET"
        c.sendall(b"HTTP/1.1 200 OK\r\nContent-Length: " + str(len(body)).encode() +
                  b"\r\nConnection: close\r\n\r\n" + body)
        c.close(); httpd.close()
    threading.Thread(target=http_server, daemon=True).start()
    link = BleLink(mtu=247)
    dev = DeviceStack(link); proxy = PhoneProxy(link); proxy.start_pump()
    ok, n, drops, note = tcp_flow(link, dev, 40000, port,
        b"GET / HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n",
        b"HELLO-FROM-INTERNET")
    results.append(("HTTP GET via tunnel (MTU 247)", ok, f"recv {n}B drops={drops} {note}"))

    # 2. 64KB 大文件 (多帧 + 多分片 + seq/ack 推进)
    big = b"X" * 65536
    httpd2 = socket.socket(); httpd2.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    httpd2.bind(("127.0.0.1", 0)); httpd2.listen(1)
    port2 = httpd2.getsockname()[1]
    def big_server():
        c, _ = httpd2.accept()
        c.recv(4096)
        c.sendall(b"HTTP/1.1 200 OK\r\nContent-Length: 65536\r\nConnection: close\r\n\r\n" + big)
        c.close(); httpd2.close()
    threading.Thread(target=big_server, daemon=True).start()
    link2 = BleLink(mtu=247)
    dev2 = DeviceStack(link2); proxy2 = PhoneProxy(link2); proxy2.start_pump()
    ok2, n2, d2, note2 = tcp_flow(link2, dev2, 40001, port2,
        b"GET /big HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n",
        b"", wait=2.0, body_len=65536)
    results.append(("64KB transfer via tunnel", ok2, f"recv {n2}B drops={d2} {note2}"))

    # 3. ICMP echo (MTU 247)
    link3 = BleLink(mtu=247)
    dev3 = DeviceStack(link3); proxy3 = PhoneProxy(link3); proxy3.start_pump()
    dev3.send_icmp_echo("8.8.8.8")
    time.sleep(0.3)
    dev3.run_rx()
    ok3 = dev3.last_icmp is not None and dev3.last_icmp[0] == 0 and dev3.drops == 0
    results.append(("ICMP ping answered (MTU 247)", ok3, f"drops={dev3.drops}"))

    # 4. ICMP at MTU 23 (20B 分片回退路径)
    link4 = BleLink(mtu=23)
    dev4 = DeviceStack(link4); proxy4 = PhoneProxy(link4); proxy4.start_pump()
    dev4.send_icmp_echo("1.1.1.1")
    time.sleep(0.3)
    dev4.run_rx()
    ok4 = dev4.last_icmp is not None and dev4.last_icmp[0] == 0 and dev4.drops == 0
    results.append(("ICMP at MTU 23 (20B chunks)", ok4, f"drops={dev4.drops}"))

    # 5. DNS 转发 (应答方向修复验证)
    link5 = BleLink(mtu=247)
    dev5 = DeviceStack(link5); proxy5 = PhoneProxy(link5); proxy5.start_pump()
    dns = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    dns.bind(("127.0.0.1", 0)); dns_port = dns.getsockname()[1]
    proxy5.dns_server = "127.0.0.1"; proxy5.dns_port = dns_port
    def dns_mock():
        data, addr = dns.recvfrom(512)
        resp = data[:2] + b"\x81\x80" + data[4:6] + b"\x00\x01\x00\x00\x00\x00" + data[12:]
        resp += b"\xc0\x0c" + struct.pack("!HHIH", 1, 1, 60, 4) + socket.inet_aton("1.2.3.4")
        dns.sendto(resp, addr)
    threading.Thread(target=dns_mock, daemon=True).start()
    dev5.send_dns_query("8.8.8.8")
    dev5.run_rx()
    time.sleep(0.5)
    dns.close()
    ok5 = dev5.last_udp is not None and b"\x81\x80" in dev5.last_udp[:4]
    results.append(("DNS query forwarded (direction fix)", ok5, f"udp={len(dev5.last_udp or b'')}B"))

    for name, ok, note in results:
        print(f"[{'PASS' if ok else 'FAIL'}] {name}  {note}")
    return all(r[1] for r in results)

if __name__ == "__main__":
    sys.exit(0 if run_tests() else 1)

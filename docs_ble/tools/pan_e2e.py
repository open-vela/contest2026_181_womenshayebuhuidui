#!/usr/bin/env python3
"""PAN 数据面端到端验证：pan connect → bt-pan up → DHCP → ping → (nslookup)"""
import serial, sys, time, os

PORT = "/dev/ttyACM0"
PHONE = "a4:cc:b3:fe:d1:a4"
LOG = open(f"/home/aila/projects/vela_contest/logs/pan_e2e_{time.strftime('%H%M%S')}.log", "wb")

def out(s):
    sys.stdout.write(s); sys.stdout.flush(); LOG.write(s.encode()); LOG.flush()

def pump(ser, sec, markers=()):
    buf = b""; end = time.time() + sec
    while time.time() < end:
        d = ser.read(4096)
        if d:
            buf += d; out(d.decode(errors="replace"))
            for m in markers:
                if m in buf: return buf
    return buf

def send(ser, t):
    out(f"\n>>> {t}\n"); ser.write((t + "\n").encode())

ser = serial.Serial(PORT, 1000000, timeout=0.3)
ser.reset_input_buffer()
send(ser, "\n"); pump(ser, 5, (b"bttool>", b"nsh>"))

# 上个探针的 bttool 会话还活着（配对已完成）
send(ser, f"device {PHONE}"); pump(ser, 3)
send(ser, f"pan connect {PHONE} 1 2")
pump(ser, 75, (b"CONNECTED", b"[pan]"))

send(ser, "pan dump"); pump(ser, 3)
send(ser, "q"); pump(ser, 4, (b"nsh>",))

# 网络面
send(ser, "ifconfig"); pump(ser, 4)
send(ser, "ifconfig bt-pan dhcp"); pump(ser, 40)
send(ser, "ifconfig"); pump(ser, 4)
send(ser, "ping -c 3 192.168.44.1"); pump(ser, 20)
send(ser, "ping -c 3 8.8.8.8"); pump(ser, 25)
send(ser, "nslookup baidu.com"); pump(ser, 20)

out("\n[E2E DONE]\n")
LOG.close(); ser.close()

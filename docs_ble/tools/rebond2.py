#!/usr/bin/env python3
"""key mismatch 修复 v2：退到 nsh → 清 /data/misc/bt → 重启服务 → 重新配对 → PAN"""
import serial, sys, time

PORT = "/dev/ttyACM0"
PHONE = "a4:cc:b3:fe:d1:a4"
LOG = open(f"/home/aila/projects/vela_contest/logs/rebond2_{time.strftime('%H%M%S')}.log", "wb")

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
send(ser, "\n"); pump(ser, 5)

# 1) 退出 bttool, 杀 bluetoothd
send(ser, "q"); pump(ser, 4, (b"nsh>",))
send(ser, "ps"); buf = pump(ser, 3)
for line in buf.decode(errors="replace").split("\n"):
    if "bluetoothd" in line:
        pid = line.split()[0]
        send(ser, f"kill {pid}"); pump(ser, 3)

# 2) 彻底清 key 存储
send(ser, "rm -rf /data/misc/bt"); pump(ser, 3)
send(ser, "ls /data/misc"); pump(ser, 3)

# 3) 重新起服务并配对（手机请点弹窗）
send(ser, "bluetoothd &"); pump(ser, 8)
send(ser, "bttool"); pump(ser, 8, (b"bttool>",))
send(ser, "enable"); pump(ser, 30, (b"Adapter Name:",))
send(ser, f"createbond {PHONE} 1")
pump(ser, 90, (b"BONDED",))
send(ser, f"device {PHONE}"); pump(ser, 3)

# 4) PAN 全链路
send(ser, f"pan connect {PHONE} 1 2")
pump(ser, 75, (b"CONNECTED",))
send(ser, "pan dump"); pump(ser, 3)
send(ser, "q"); pump(ser, 4, (b"nsh>",))
send(ser, "ifconfig"); pump(ser, 4)
send(ser, "ifconfig bt-pan dhcp"); pump(ser, 45)
send(ser, "ifconfig"); pump(ser, 4)

sys.stdout.write("\n[REBOND2 DONE]\n"); sys.stdout.flush()
LOG.write(b"\n[REBOND2 DONE]\n"); LOG.close()
ser.close()

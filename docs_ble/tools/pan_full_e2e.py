#!/usr/bin/env python3
"""PAN 全链路 e2e v2（含重启免配对验证）:
enable → createbond(应免配对直接连) → pan connect → bt-pan → dhcp → ping
"""
import serial, sys, time, os

PORT = "/dev/ttyACM0"
PHONE = "a4:cc:b3:fe:d1:a4"
LOG = open(f"/home/aila/projects/vela_contest/logs/pan_e2e2_{time.strftime('%H%M%S')}.log", "wb")

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

send(ser, "\n"); pump(ser, 5, (b"nsh>",))
send(ser, "bluetoothd &"); pump(ser, 7)
send(ser, "bttool"); pump(ser, 8, (b"bttool>",))
send(ser, "enable"); pump(ser, 30, (b"Adapter Name:",))
send(ser, "state"); pump(ser, 3)

# 关键验证1: 重启后 link key 持久化 → createbond 应跳过配对直接 BONDED/加密
send(ser, f"createbond {PHONE} 1")
buf = pump(ser, 60, (b"BOND_STATE_BONDED", b"BONDED",))
send(ser, f"device {PHONE}"); pump(ser, 3)

# 关键验证2: PAN 数据面
send(ser, f"pan connect {PHONE} 1 2")
pump(ser, 75, (b"CONNECTED",))
send(ser, "pan dump"); pump(ser, 3)

send(ser, "q"); pump(ser, 4, (b"nsh>",))
send(ser, "ifconfig"); pump(ser, 4)
send(ser, "ifconfig bt-pan dhcp"); pump(ser, 40)
send(ser, "ifconfig"); pump(ser, 4)
send(ser, "ping -c 3 8.8.8.8"); pump(ser, 25)
send(ser, "nslookup baidu.com"); pump(ser, 20)

out("\n[E2E2 DONE]\n")
LOG.close(); ser.close()

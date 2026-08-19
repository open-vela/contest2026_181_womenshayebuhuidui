#!/usr/bin/env python3
"""出站 PAN 验证：配对回连 → pan connect（板拨手机NAP）→ 不退出bttool → dhcp"""
import serial, sys, time

PORT = "/dev/ttyACM0"
PHONE = "a4:cc:b3:fe:d1:a4"
LOG = open(f"/home/aila/projects/vela_contest/logs/panout_{time.strftime('%H%M%S')}.log", "wb")

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

send(ser, "bluetoothd &"); pump(ser, 8)
send(ser, "bttool"); pump(ser, 8, (b"bttool>",))
send(ser, "enable"); pump(ser, 30, (b"Adapter Name:",))

# 出站: 板子主动拨手机 NAP（link key 已在, 应免配对加密直达）
send(ser, f"pan connect {PHONE} 1 2")
pump(ser, 90, (b"CONNECTED", b"[pan] setup",))
send(ser, "pan dump"); pump(ser, 4)
send(ser, f"device {PHONE}"); pump(ser, 3)

# 若未连上, 试试 createbond 先(ACL 可能未建立)
send(ser, f"createbond {PHONE} 1")
pump(ser, 45, (b"BONDED",))
send(ser, f"pan connect {PHONE} 1 2")
pump(ser, 90, (b"CONNECTED",))
send(ser, "pan dump"); pump(ser, 4)

out("\n[PANOUT DONE]\n")
LOG.close(); ser.close()

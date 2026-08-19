#!/usr/bin/env python3
"""修复"手机取消配对后无法通信"：板侧删绑定 → 重新 SSP 配对 → PAN 全链路"""
import serial, sys, time

PORT = "/dev/ttyACM0"
PHONE = "a4:cc:b3:fe:d1:a4"
LOG = open(f"/home/aila/projects/vela_contest/logs/rebond_{time.strftime('%H%M%S')}.log", "wb")

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

# 复用现有 bttool 会话；若无则起服务
send(ser, "state"); buf = pump(ser, 4)
if b"Adapter State" not in buf:
    send(ser, "bluetoothd &"); pump(ser, 7)
    send(ser, "bttool"); pump(ser, 8, (b"bttool>",))
    send(ser, "enable"); pump(ser, 30, (b"Adapter Name:",))

# 1) 删掉板侧旧绑定（含 br_key.bin 由 zblue keys 清空 + 存储层删除）
send(ser, f"removebond {PHONE} 1"); pump(ser, 10)
send(ser, f"device {PHONE}"); pump(ser, 3)

# 2) 重新配对（手机请点弹窗确认）
out("\n[STEP] 重新配对 — 请在手机上确认配对弹窗\n")
send(ser, f"createbond {PHONE} 1")
pump(ser, 90, (b"BONDED",))
send(ser, f"device {PHONE}"); pump(ser, 3)

# 3) PAN
send(ser, f"pan connect {PHONE} 1 2")
pump(ser, 75, (b"CONNECTED", b"state:2",))
send(ser, "pan dump"); pump(ser, 3)
send(ser, "q"); pump(ser, 4, (b"nsh>",))
send(ser, "ifconfig"); pump(ser, 4)
send(ser, "ifconfig bt-pan dhcp"); pump(ser, 40)
send(ser, "ifconfig"); pump(ser, 4)

out("\n[REBOND DONE]\n")
LOG.close(); ser.close()

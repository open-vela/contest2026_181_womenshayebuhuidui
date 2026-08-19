#!/usr/bin/env python3
"""R68 legacy PIN 配对验证: enable → createbond → 检测 [PIN] 提示 → 自动应答 0000
之后继续观察 link key / BONDED / Encryption，成功则尝试 pan connect。
"""
import serial, sys, time, os

PORT = "/dev/ttyACM0"
PHONE = "a4:cc:b3:fe:d1:a4"
LOG = open(f"/home/aila/projects/vela_contest/logs/legacy_pin_{time.strftime('%H%M%S')}.log", "wb")

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
send(ser, "\n"); pump(ser, 6, (b"nsh>",))
send(ser, "rm -rf /data/misc/bt"); pump(ser, 2)
send(ser, "bluetoothd &"); pump(ser, 7)
send(ser, "bttool"); pump(ser, 8, (b"bttool>",))
send(ser, "enable"); pump(ser, 30, (b"Adapter Name:",))
send(ser, "state"); pump(ser, 3)

# 板侧主动发起 legacy 配对
send(ser, f"createbond {PHONE} 1")
buf = b""
end = time.time() + 90
pin_asked = False
bonded = False
while time.time() < end:
    d = ser.read(4096)
    if d:
        buf += d; out(d.decode(errors="replace"))
        if not pin_asked and b"[PIN]" in buf:
            pin_asked = True
            out("\n[AUTO] PIN request detected, replying 0000\n")
            send(ser, f"pair pin {PHONE.upper()} 1 0000")
        if b"BOND_STATE_BONDED" in buf or b"BondState: BOND" in buf.replace(b"BOND_NONE", b"").replace(b"BONDING", b""):
            bonded = True
            break
        if b"bond state" in buf and b"BONDED" in buf:
            bonded = True
            break

out(f"\n[RESULT] pin_asked={pin_asked} bonded={bonded}\n")

if bonded:
    send(ser, f"device {PHONE}")
    pump(ser, 3)
    send(ser, f"pan connect {PHONE} 1 2")
    pump(ser, 60, (b"CONNECTED", b"pan",))
    send(ser, "pan dump"); pump(ser, 3)

out("\n[DONE]\n")
LOG.close(); ser.close()

#!/usr/bin/env python3
"""被动配对监听: 起服务→enable→监听 180s 等手机主动连接/配对
用法: python3 docs_ble/tools/pair_listen.py [秒]
"""
import serial, sys, time, os

PORT = "/dev/ttyACM0"
DUR = int(sys.argv[1]) if len(sys.argv) > 1 else 180
LOGDIR = "/home/aila/projects/vela_contest/logs"
os.makedirs(LOGDIR, exist_ok=True)
LOGPATH = os.path.join(LOGDIR, time.strftime("pair_listen_%Y%m%d_%H%M%S.log"))
LOG = open(LOGPATH, "wb")

def pump(ser, seconds, markers=()):
    buf = b""; deadline = time.time() + seconds
    while time.time() < deadline:
        d = ser.read(4096)
        if d:
            buf += d
            sys.stdout.write(d.decode("utf-8", errors="replace")); sys.stdout.flush()
            LOG.write(d); LOG.flush()
            for m in markers:
                if m in buf: return buf
    return buf

def send(ser, t):
    print(f"\n>>> {t}", flush=True)
    ser.write((t + "\n").encode())

ser = serial.Serial(PORT, 1000000, timeout=0.3)
ser.reset_input_buffer()
ser.write(b"\n")
out = pump(ser, 6, (b"nsh>",))
if b"bttool>" in out:
    send(ser, "q"); pump(ser, 3, (b"nsh>",))
if b"nsh>" not in out:
    print("[!] 需要 USB 拔插复位"); sys.exit(2)

send(ser, "rm -rf /data/misc/bt"); pump(ser, 2)
send(ser, "bluetoothd &"); pump(ser, 6)
send(ser, "bttool"); pump(ser, 8, (b"bttool>",))
send(ser, "enable"); pump(ser, 30, (b"Adapter Name:",))
send(ser, "set scanmode 2"); pump(ser, 5)

print(f"\n[*] 监听 {DUR}s — 请在手机蓝牙设置中点击板子(cd:ab:78:56:34:12)进行配对", flush=True)
pump(ser, DUR)
print(f"\n[log] {LOGPATH}")
LOG.close(); ser.close()

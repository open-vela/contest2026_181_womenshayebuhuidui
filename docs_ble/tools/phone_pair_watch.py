#!/usr/bin/env python3
"""手机主动配对监视器：起服务后监听 N 秒等手机点板子配对"""
import serial, sys, time

PORT = "/dev/ttyACM0"
DUR = int(sys.argv[1]) if len(sys.argv) > 1 else 420

ser = serial.Serial(PORT, 1000000, timeout=0.3)
ser.reset_input_buffer()

def cmd(c, wait=4):
    ser.write((c + "\n").encode()); time.sleep(0.2)
    end = time.time() + wait; buf = b""
    while time.time() < end:
        d = ser.read(4096)
        if d: buf += d
    print(f"\n>>> {c}\n" + buf.decode(errors="replace"), flush=True)
    return buf.decode(errors="replace")

cmd("\n", 6)
cmd("rm -rf /data/misc/bt", 2)
cmd("bluetoothd &", 7)
cmd("bttool", 8)
cmd("enable", 30)
cmd("set scanmode 2", 5)

print("\n[LISTEN] 请在手机上点击板子发起配对", flush=True)
end = time.time() + DUR
while time.time() < end:
    d = ser.read(4096)
    if d:
        sys.stdout.write(d.decode(errors="replace")); sys.stdout.flush()
ser.close()

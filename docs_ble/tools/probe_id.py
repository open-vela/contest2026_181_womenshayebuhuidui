#!/usr/bin/env python3
"""
快速识别 /dev/ttyACM0 上的固件类型（docs_ble Round 3）
用法: python3 probe_id.py [端口] [波特率]
输出: NSH(真机 NuttX) / VELA(模拟器 goldfish) / 无响应
"""
import serial, sys, time

PORT = sys.argv[1] if len(sys.argv) > 1 else "/dev/ttyACM0"
BAUD = int(sys.argv[2]) if len(sys.argv) > 2 else 1000000

try:
    ser = serial.Serial(PORT, BAUD, timeout=0.3)
except Exception as e:
    print(f"[id] 无法打开 {PORT}: {e}")
    print("[id] 提示: 若报 'Device or resource busy'，先停掉占用进程: pkill -f serial_relay")
    sys.exit(1)

ser.reset_input_buffer()
ser.write(b"\r\n")
buf = b""
deadline = time.time() + 8
while time.time() < deadline:
    data = ser.read(256)
    if data:
        buf += data
        sys.stdout.write(data.decode("utf-8", errors="replace"))
        sys.stdout.flush()
    elif buf:
        break

text = buf.decode("utf-8", errors="replace")
if "nsh>" in text or "NuttShell" in text or "NuttX" in text:
    print("\n=== 判定: 真机 NuttX (NSH) — 可以跑 BREDR 探测 ===")
elif "vela>" in text or "goldfish" in text or "eth0" in text:
    print("\n=== 判定: 模拟器 (goldfish/vela) — 无蓝牙，无法探测 ===")
elif buf:
    print("\n=== 判定: 有输出但无法识别（可能是其他固件） ===")
else:
    print("\n=== 判定: 无响应（设备可能未就绪/被占用/不是串口） ===")
ser.close()

#!/usr/bin/env python3
"""完整日志 v2: kill 旧 bluetoothd + 重启 + pan connect"""
import serial, time

PORT = "/dev/ttyACM0"
PHONE = "a4:cc:b3:fe:d1:a4"

def pump(ser, seconds, markers=()):
    buf = b""
    deadline = time.time() + seconds
    while time.time() < deadline:
        data = ser.read(256)
        if data:
            buf += data
            for m in markers:
                if m in buf:
                    return buf
    return buf

def send(ser, text):
    print(f">>> {text}", flush=True)
    ser.write((text + "\n").encode())

def main():
    ser = serial.Serial(PORT, 1000000, timeout=0.3)
    ser.reset_input_buffer()
    ser.write(b"\n")
    pump(ser, 8, (b"nsh>",))
    # 杀掉旧 bluetoothd（若有）: ps 找 PID 再 kill
    send(ser, "ps")
    out = pump(ser, 3)
    for line in out.decode(errors="replace").split("\n"):
        if "bluetoothd" in line and "grep" not in line:
            parts = line.split()
            if parts:
                pid = parts[0]
                print(f"kill bluetoothd pid={pid}", flush=True)
                send(ser, f"kill {pid}")
                pump(ser, 2)
    send(ser, "bluetoothd &")
    pump(ser, 5)
    send(ser, "bttool")
    pump(ser, 8, (b"bttool>",))
    send(ser, "enable")
    out = pump(ser, 25, (b"Adapter Name:",))
    if b"Adapter Name:" not in out:
        print("enable FAIL", flush=True)
        return
    print("adapter ON", flush=True)
    send(ser, "pair auto 1")
    pump(ser, 2)
    print("=== pan connect, 抓 25s ===", flush=True)
    send(ser, f"pan connect {PHONE} 1 2")
    out = pump(ser, 25)
    text = out.decode(errors="replace")
    with open("/tmp/pan_full3.log", "w") as fp:
        fp.write(text)
    print("saved", len(text), flush=True)
    print("\n=== [pan] ===", flush=True)
    for line in text.split("\n"):
        if "[pan]" in line:
            print("|", line.strip()[:160], flush=True)
    print("\n=== bttool ===", flush=True)
    for line in text.split("\n"):
        if any(k in line for k in ["pan_connection", "connection state", "name changed", "bond", "error"]):
            print("|", line.strip()[:140], flush=True)
    print("\n=== HCI 断链/连接事件 ===", flush=True)
    import re
    for m in re.finditer(r"bth4 recv: len=\d+ pending=\d+ ((?:[0-9a-f]{2} )+?)(?=\r|\n)", text):
        evt = m.group(1).strip()
        if evt[:2] in ("04 03", "04 05", "04 06", "04 12", "04 13", "04 1b"):
            print("|", evt[:70], flush=True)
    send(ser, "quit")
    pump(ser, 2)
    ser.close()

if __name__ == "__main__":
    main()

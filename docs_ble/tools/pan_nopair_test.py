#!/usr/bin/env python3
"""无配对版: enable + 直接 pan connect (依赖持久化 bond key)"""
import serial, time, re

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
    pump(ser, 12, (b"nsh>",))
    send(ser, "bluetoothd &")
    pump(ser, 5)
    send(ser, "bttool")
    pump(ser, 8, (b"bttool>",))
    send(ser, "enable")
    out = pump(ser, 25, (b"Adapter Name:",))
    if b"Adapter Name:" not in out:
        print("enable FAIL"); ser.close(); return 1
    print("adapter ON", flush=True)
    print("=== 直接 pan connect (无配对) ===", flush=True)
    send(ser, f"pan connect {PHONE} 1 2")
    out = pump(ser, 20, (b"state:2",))
    with open("/tmp/pan_nopair.log", "wb") as fp:
        fp.write(out)
    text = out.decode(errors="replace")
    print("saved", len(text), flush=True)
    st = re.findall(rb"state:(\d)", out)
    print("states:", st, flush=True)
    for line in text.split("\n"):
        if "[pan]" in line:
            print("PAN|", line.strip()[:150], flush=True)
    print("=== HCI 事件 ===", flush=True)
    for m in re.finditer(r"bth4 recv: len=\d+ pending=\d+ ((?:[0-9a-f]{2} )+?)(?=\r|\n)", text):
        evt = m.group(1).strip()
        code = evt[:2]
        names = {"04 03": "DisconnComp", "04 05": "Disc", "04 06": "ConnReq!",
                 "04 0e": "CmdComp", "04 0f": "CmdStatus", "04 12": "ConnComp",
                 "04 13": "Encrypt", "04 18": "PINreq", "04 19": "LinkKeyReq",
                 "04 1a": "LinkKeyNotif", "04 1b": "AuthReq?"}
        if code in names:
            print("|", names[code], evt[:64], flush=True)
    if b"state:2" in out:
        print("********** PAN CONNECTED! **********", flush=True)
        send(ser, "quit")
        pump(ser, 3, (b"nsh>",))
        send(ser, "ifconfig bt-pan")
        print("ifconfig:", pump(ser, 4).decode(errors="replace")[-300:], flush=True)
    send(ser, "quit")
    pump(ser, 2)
    ser.close()

if __name__ == "__main__":
    main()

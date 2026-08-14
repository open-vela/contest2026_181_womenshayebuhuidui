#!/usr/bin/env python3
"""抓 pan connect 完整日志 + 检测板子挂死时刻"""
import serial, sys, time, re

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
    ser.write((text + "\n").encode())

def main():
    log = open("/tmp/pan_full.log", "wb")
    ser = serial.Serial(PORT, 1000000, timeout=0.3)
    ser.reset_input_buffer()
    ser.write(b"\n")
    pump(ser, 15, (b"nsh>",))
    send(ser, "bluetoothd &"); pump(ser, 5)
    send(ser, "bttool"); pump(ser, 8, (b"bttool>",))
    send(ser, "enable"); pump(ser, 15, (b"Adapter Name:",))
    send(ser, "pair auto 1"); pump(ser, 2)
    print("=== pan connect ===", flush=True)
    send(ser, f"pan connect {PHONE} 1 2")
    # 抓 30 秒, 每 5 秒检查板子是否还活着(发 \n 看回显)
    t0 = time.time()
    last = b""
    while time.time() - t0 < 30:
        d = ser.read(512)
        if d:
            last += d
            log.write(d)
            if len(last) > 200000:
                last = last[-200000:]
        else:
            # 无数据: 探测板子存活
            ser.write(b"\n")
            time.sleep(0.3)
            d2 = ser.read(512)
            if d2:
                log.write(d2)
                last += d2
            else:
                print(f"[{time.time()-t0:.0f}s] 板子无响应(疑似挂死)", flush=True)
                break
    log.close()
    text = last.decode(errors="replace")
    print("captured", len(text), "bytes", flush=True)
    # 关键事件
    for ln in text.split("\r\r\n"):
        s = ln.strip()
        if any(k in s for k in ["pan_connection", "connection state", "name changed", "bond", "assert", "dump"]):
            print("|", s[:140], flush=True)
    # HCI 事件序列
    print("\n=== HCI 事件序列 ===", flush=True)
    for m in re.finditer(r"bth4 recv: len=\d+ pending=\d+ ((?:[0-9a-f]{2} )+?)(?=\r|\n)", text):
        evt = m.group(1).strip()
        code = evt[:2]
        names = {"04 02": "InquiryResult", "04 03": "DisconnComp", "04 05": "Disc2",
                 "04 07": "L2CAP-Cmd", "04 0e": "CmdComp", "04 0f": "CmdStatus",
                 "04 12": "ConnComp", "04 13": "Encrypt", "04 1b": "AuthReq", "04 18": "PINreq"}
        print("|", names.get(code, code), evt[:64], flush=True)
    for m in re.finditer(r"04 03 0b 00 [0-9a-f]{2} 00 ([0-9a-f]{2})", text):
        print("DISCONNECT REASON: 0x" + m.group(1), flush=True)
    ser.close()

if __name__ == "__main__":
    main()

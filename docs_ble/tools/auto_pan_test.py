#!/usr/bin/env python3
"""
PAN 全自动测试 (docs_ble Round 4) - 由 agent 直接驱动串口
流程: nsh> -> bluetoothd -> bttool -> enable -> pair auto -> createbond
      -> [PIN] -> pair pin 0000 -> 等 BONDED -> pan connect -> 等 CONNECTED
      -> ifconfig bt-pan
手机配合: 蓝牙网络共享开启; 配对弹窗输入 0000
"""
import serial, sys, time, re

PORT = sys.argv[1] if len(sys.argv) > 1 else "/dev/ttyACM0"
BAUD = int(sys.argv[2]) if len(sys.argv) > 2 else 1000000
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
    print(f">>> {text}")
    ser.write((text + "\n").encode())

def main():
    ser = serial.Serial(PORT, BAUD, timeout=0.3)
    ser.reset_input_buffer()
    print("=== [1] 等待 nsh> ===")
    ser.write(b"\n")
    out = pump(ser, 20, (b"nsh>",))
    if b"nsh>" not in out:
        print("FAIL: 无 nsh> 提示符")
        ser.close(); return 1
    print("OK nsh>")

    print("=== [2] 启动 bluetoothd ===")
    send(ser, "bluetoothd &")
    pump(ser, 4)
    out = pump(ser, 3, (b"btsvc-stackmgr",))
    print("bluetoothd started" if out else "warn: 无 btsvc 输出")
    time.sleep(2)

    print("=== [3] bttool ===")
    send(ser, "bttool")
    out = pump(ser, 8, (b"bttool>",))
    if b"create instance error" in out:
        print("FAIL: create instance error")
        ser.close(); return 1
    print("bttool OK")

    print("=== [4] enable (等 30s) ===")
    send(ser, "enable")
    out = pump(ser, 30, (b"Adapter Name:",))
    if b"Adapter Name:" not in out:
        print("FAIL: enable 未完成, 尾部:", out[-200:])
        ser.close(); return 1
    print("adapter ON")

    send(ser, "pair auto 1")
    pump(ser, 2)

    print("=== [5] createbond (配对) ===")
    print("*** 手机准备: 弹 PIN 时输入 0000 ***")
    send(ser, f"createbond {PHONE} 1")
    out = pump(ser, 12, (b"please reply:",))
    if b"please reply:" in out or b"PIN" in out:
        print("PIN 请求到达, 回复 0000 ...")
        send(ser, f"pair pin {PHONE} 1 0000")
    out = pump(ser, 20, (b"name changed", b"BONDED", b"bond state"))
    if b"name changed" in out or b"BONDED" in out:
        print("配对成功!")
    else:
        print("warn: 未确认配对完成, 输出尾部:", out[-200:])

    print("=== [6] 等 12s (Android 断开配对 ACL 后全新连接) ===")
    time.sleep(12)
    print("=== [6] pan connect ===")
    send(ser, f"pan connect {PHONE} 1 2")
    out = pump(ser, 25, (b"state:2",))
    # 保存完整原始日志
    with open("/tmp/pan_raw.log", "wb") as fp:
        fp.write(out)
    print(f"[saved /tmp/pan_raw.log {len(out)} bytes]", flush=True)
    if b"state:2" in out:
        print("****** PAN CONNECTED (state:2) ******")
        result = "CONNECTED"
    else:
        st = re.findall(rb"state:(\d)", out)
        print("pan connect 输出:", st)
        # 解析 HCI 事件序列
        text = out.decode(errors="replace")
        print("\n=== HCI 事件序列 ===")
        for m in re.finditer(r"bth4 recv: len=\d+ pending=\d+ ((?:[0-9a-f]{2} )+?)(?=\r|\n)", text):
            evt = m.group(1).strip()
            code = evt[:2]
            names = {"04 03": "DisconnComp", "04 05": "Disconn", "04 06": "ConnRequest!",
                     "04 0e": "CmdComp", "04 0f": "CmdStatus", "04 12": "ConnComp",
                     "04 13": "EncryptChange", "04 1b": "AuthReq"}
            print("|", names.get(code, code), evt[:64])
        for m in re.finditer(r"04 03 0b 00 [0-9a-f]{2} 00 ([0-9a-f]{2})", text):
            print("DISCONNECT REASON: 0x" + m.group(1))
        # [pan] syslog
        for line in text.split("\n"):
            if "[pan]" in line:
                print("PAN|", line.strip()[:140])
        result = "FAILED"
        pump(ser, 5)

    print("=== [7] 检查 bt-pan ===")
    send(ser, "quit")
    pump(ser, 3, (b"nsh>",))
    send(ser, "ifconfig bt-pan")
    out = pump(ser, 5)
    print("ifconfig:", out[-400:])
    if result == "CONNECTED":
        send(ser, "ping -c 3 8.8.8.8")
        out = pump(ser, 15)
        ok = b"bytes from" in out or b"icmp_seq" in out
        print("ping:", "OK!!" if ok else "无响应")
        print(out[-500:])
    ser.close()
    print(f"\n===== 结果: {result} =====")
    return 0 if result == "CONNECTED" else 1

if __name__ == "__main__":
    sys.exit(main())

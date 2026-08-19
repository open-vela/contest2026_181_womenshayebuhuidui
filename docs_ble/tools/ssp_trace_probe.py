#!/usr/bin/env python3
"""SSP 配对全事件抓包探针（P1：定标 LCPU SSP 事件真实布局）

流程: 清 /data/misc/bt → 重启 bluetoothd → bttool enable → inquiry(确认手机在场)
      → createbond(板侧主动 SSP) → 抓 90s 全部 HCI/桥接/zblue 日志
输出: 屏幕 + /home/aila/projects/vela_contest/logs/ssp_probe_<ts>.log
"""
import serial, sys, time, os

PORT = "/dev/ttyACM0"
BAUD = 1000000
PHONE = "a4:cc:b3:fe:d1:a4"
LOGDIR = "/home/aila/projects/vela_contest/logs"

os.makedirs(LOGDIR, exist_ok=True)
LOGPATH = os.path.join(LOGDIR, time.strftime("ssp_probe_%Y%m%d_%H%M%S.log"))
LOG = open(LOGPATH, "wb")

def note(s):
    line = f"\n[{time.strftime('%H:%M:%S')}] === {s} ===\n"
    LOG.write(line.encode()); LOG.flush()

def pump(ser, seconds, markers=()):
    buf = b""
    deadline = time.time() + seconds
    while time.time() < deadline:
        data = ser.read(4096)
        if data:
            buf += data
            sys.stdout.write(data.decode("utf-8", errors="replace"))
            sys.stdout.flush()
            LOG.write(data); LOG.flush()
            for m in markers:
                if m in buf:
                    return buf
    return buf

def send(ser, text):
    note(f">>> {text}")
    ser.write((text + "\n").encode())

def main():
    ser = serial.Serial(PORT, BAUD, timeout=0.3)
    ser.reset_input_buffer()
    note("probe start")
    ser.write(b"\n")
    out = pump(ser, 8, (b"nsh>", b"bttool>"))

    if b"bttool>" in out:
        send(ser, "q"); pump(ser, 3, (b"nsh>",))
    if b"nsh>" not in out:
        note("无 nsh 提示符，尝试 RTS 复位")
        try:
            ser.rts = True; time.sleep(0.5); ser.rts = False
        except Exception:
            pass
        time.sleep(5)
        ser.write(b"\n")
        out = pump(ser, 8, (b"nsh>",))
        if b"nsh>" not in out:
            note("失败: 需要 USB 拔插复位后重跑")
            sys.exit(2)

    send(ser, "rm -rf /data/misc/bt"); pump(ser, 2)
    send(ser, "ps"); out = pump(ser, 3)
    for line in out.decode(errors="replace").split("\n"):
        if "bluetoothd" in line:
            parts = line.split()
            if parts and parts[0].isdigit():
                send(ser, f"kill {parts[0]}"); pump(ser, 2)
    send(ser, "bluetoothd &"); pump(ser, 6)
    send(ser, "bttool"); pump(ser, 8, (b"bttool>",))

    send(ser, "enable"); pump(ser, 30, (b"Adapter Name:",))
    send(ser, "state"); pump(ser, 3)

    send(ser, "inquiry start 15")
    out = pump(ser, 25)
    phone_seen = b"a4:cc:b3:fe:d1:a4" in out or b"A4:CC:B3:FE:D1:A4" in out
    note(f"inquiry phone_seen={phone_seen}")

    note("createbond 板侧主动 SSP — 开始抓 LCPU 事件流")
    send(ser, f"createbond {PHONE} 1")
    pump(ser, 90)

    send(ser, f"device {PHONE}"); pump(ser, 3)
    send(ser, "state"); pump(ser, 3)
    note("probe end")
    print(f"\n\n[log saved] {LOGPATH}")

if __name__ == "__main__":
    try:
        main()
    finally:
        LOG.close()

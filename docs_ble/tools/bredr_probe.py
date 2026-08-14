#!/usr/bin/env python3
"""
BREDR 闸门 0 探测脚本（docs_ble Round 3）
=========================================
用途：真机插上后，自动验证 LCPU 固件是否支持 BREDR（经典蓝牙）。
这是 PAN(BNEP) 实现的决定性前提。

用法:
  python3 bredr_probe.py [串口, 默认 /dev/ttyACM0] [波特率, 默认 1000000]

判定:
  PASS - get addr 返回非零 BREDR 地址 + discovery(inquiry) 有结果
  FAIL - HCI 命令超时 / unknown command / 全零地址
  UNKNOWN - 需要人工看日志
"""
import serial, sys, time, re

PORT = sys.argv[1] if len(sys.argv) > 1 else "/dev/ttyACM0"
BAUD = int(sys.argv[2]) if len(sys.argv) > 2 else 1000000

def pump(ser, seconds, stop_markers=()):
    buf = b""
    deadline = time.time() + seconds
    while time.time() < deadline:
        data = ser.read(256)
        if data:
            buf += data
            sys.stdout.write(data.decode("utf-8", errors="replace"))
            sys.stdout.flush()
            for m in stop_markers:
                if m in buf:
                    return buf
    return buf

def cmd(ser, text, wait=3):
    sys.stdout.write(f"\n>>> {text}\n")
    ser.write((text + "\r\n").encode())

def main():
    ser = serial.Serial(PORT, BAUD, timeout=0.3)
    ser.reset_input_buffer()
    print(f"[probe] 等待 nsh> ...")
    ser.write(b"\r\n")
    pump(ser, 15, (b"nsh>", b"# "))
    results = []

    # 1. 使能适配器
    cmd(ser, "bttool enable", 5)
    pump(ser, 8)
    results.append(("enable", "观察日志"))

    # 2. 本地地址（BREDR 地址非零 = 控制器有 BREDR 地址空间）
    cmd(ser, "bttool get addr", 3)
    out = pump(ser, 6)
    addr = re.search(rb"([0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}", out)
    results.append(("get addr", addr.group(0).decode() if addr else "N/A"))

    # 3. 可发现+可连接（BREDR inquiry/page scan）
    cmd(ser, "bttool set scanmode 2", 3)
    pump(ser, 5)
    results.append(("set scanmode 2", "观察日志"))

    # 4. 经典发现（inquiry，10 秒）
    cmd(ser, "bttool discovery start 10", 3)
    out = pump(ser, 16)
    devs = re.findall(rb"([0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}", out)
    found = len(devs) > 0
    results.append(("discovery", f"发现 {len(devs)} 个地址" if found else "未发现(可能无设备或 BREDR 不可用)"))

    # 5. 日志抓取：HCI 错误/超时特征
    cmd(ser, "bttool dump", 3)
    out = pump(ser, 6)
    errs = re.findall(rb"(?:timeout|unknown|fail|error|not support)", out, re.I)
    results.append(("dump", f"错误关键词 {len(errs)} 个"))

    print("\n========== 探测结果 ==========")
    for k, v in results:
        print(f"  {k}: {v}")

    addr_ok = results[1][1] not in ("N/A",)
    verdict = "PASS(疑似)" if addr_ok and found else ("FAIL(疑似)" if addr_ok is False else "UNKNOWN")
    print(f"判定: {verdict}")
    print("提示: 完整日志请另存串口输出; 若 get addr 非零但 discovery 空,")
    print("      检查手机是否开启经典蓝牙可见性(可被发现), 以及日志中 inquiry 命令的响应。")
    ser.close()

if __name__ == "__main__":
    main()

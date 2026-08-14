#!/usr/bin/env python3
"""
BREDR 闸门 0 探测脚本 v2（docs_ble Round 3）
=========================================
v2 修复:
  1. 先启动 bluetoothd 服务（rcS 为空，不会自动启动）
  2. bttool 是交互式工具: 进入 bttool> 后命令不带 "bttool" 前缀

用法:
  python3 bredr_probe.py [串口, 默认 /dev/ttyACM0] [波特率, 默认 1000000]

判定:
  PASS - get addr 返回非零 BREDR 地址 + inquiry 有结果
  FAIL - HCI 命令超时 / unknown command / 全零地址 / bluetoothd 起不来
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
    return pump(ser, wait)

def main():
    ser = serial.Serial(PORT, BAUD, timeout=0.3)
    ser.reset_input_buffer()
    print(f"[probe] 等待 nsh> ...")
    ser.write(b"\r\n")
    pump(ser, 15, (b"nsh>",))
    results = []

    # 0. 启动 bluetoothd 服务（后台）
    cmd(ser, "bluetoothd &", 4)
    out = pump(ser, 5)
    results.append(("bluetoothd", "已发送启动命令"))

    # 1. 进入 bttool 交互模式
    cmd(ser, "bttool", 4)
    out = pump(ser, 6, (b"bttool>",))
    if b"create instance error" in out:
        print("[probe] !! bluetoothd 未就绪或连接失败，等待后重试 ...")
        time.sleep(3)
        cmd(ser, "bttool", 4)
        out = pump(ser, 6, (b"bttool>",))
    results.append(("bttool", "交互模式"))

    # 2. 适配器使能
    cmd(ser, "enable", 6)
    out = pump(ser, 8)
    results.append(("enable", "观察日志"))

    # 3. 适配器状态
    cmd(ser, "state", 3)
    out = pump(ser, 5)
    results.append(("state", out.decode(errors="replace")[-80:].replace("\n", " ")))

    # 4. 本地地址（BREDR 地址非零 = 控制器有 BREDR 地址空间）
    cmd(ser, "get addr", 3)
    out = pump(ser, 6)
    addr = re.search(rb"([0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}", out)
    addr_str = addr.group(0).decode() if addr else "N/A"
    results.append(("get addr", addr_str))

    # 5. 可发现+可连接（BREDR inquiry/page scan）—— bttool 命令是 set scanmode
    cmd(ser, "set scanmode 2", 3)
    pump(ser, 5)
    results.append(("set scanmode 2", "观察日志"))

    # 6. 经典发现（inquiry，10 秒）—— 注意 bttool 命令名是 inquiry
    cmd(ser, "inquiry start 10", 3)
    out = pump(ser, 16)
    devs = re.findall(rb"([0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}", out)
    found = len(devs) > 0
    results.append(("inquiry", f"发现 {len(devs)} 个地址" if found else "未发现设备"))

    # 7. 退出
    cmd(ser, "quit", 2)
    pump(ser, 2)

    print("\n========== 探测结果 ==========")
    for k, v in results:
        print(f"  {k}: {v}")

    addr_ok = addr_str not in ("N/A", "00:00:00:00:00:00", "")
    if addr_ok and found:
        verdict = "PASS (BREDR 可用)"
    elif addr_ok and not found:
        verdict = "UNKNOWN (地址有效但 inquiry 无结果 — 检查手机经典蓝牙可见性)"
    elif not addr_ok:
        verdict = "FAIL (BREDR 地址无效/不可用)"
    else:
        verdict = "UNKNOWN"
    print(f"判定: {verdict}")
    print("提示: 完整日志请另存串口输出。若 inquiry 空, 请确认手机开了经典蓝牙可见性,")
    print("      或在 NSH 先跑 ai_agent & 让蓝牙栈完整初始化后再试。")
    ser.close()

if __name__ == "__main__":
    main()

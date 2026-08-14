#!/usr/bin/env python3
"""
BREDR 闸门 0 探测脚本 v3（docs_ble Round 3）
=========================================
v3 修复:
  1. 先退出设备上残留的 bttool 交互会话（v1/v2 脚本遗留，会吃掉后续所有命令）
  2. 检查/启动 bluetoothd
  3. 重新进入 bttool 交互模式执行探测
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

def cmd(ser, text, wait=3, marker=None):
    # 注意: 只发 \n。bttool 的 getline 只剥 \n, 若发 \r\n 则 \r 残留
    # 在 token 尾部, 导致 "enable\r" != "enable" 匹配失败 (UnKnow command)
    sys.stdout.write(f"\n>>> {text}\n")
    ser.write((text + "\n").encode())
    return pump(ser, wait, (marker,) if marker else ())

def wait_prompt(ser, prompt, timeout=10):
    """等待指定提示符出现"""
    buf = b""
    deadline = time.time() + timeout
    while time.time() < deadline:
        data = ser.read(256)
        if data:
            buf += data
            if prompt in buf:
                return True
    return prompt in buf

def main():
    ser = serial.Serial(PORT, BAUD, timeout=0.3)
    ser.reset_input_buffer()
    print("[probe] 等待 nsh> ...")
    ser.write(b"\n")
    out0 = pump(ser, 8, (b"nsh>",))
    if b"nsh>" not in out0 and b"bttool>" not in out0:
        # 无任何提示符: 尝试 RTS 复位(部分板子有效), 再等一次
        print("[probe] 无提示符, 尝试 RTS 复位 ...")
        try:
            ser.rts = True; time.sleep(0.5); ser.rts = False
        except Exception:
            pass
        time.sleep(4)
        ser.write(b"\n")
        out0 = pump(ser, 8, (b"nsh>",))
    if b"nsh>" not in out0:
        print("[probe] !! 未检测到 nsh> 提示符。")
        if b"bttool>" in out0:
            print("[probe] !! 设备上存在 bttool 会话(可能已僵尸化)。")
        print("[probe] !! 请物理复位: 拔插 USB 或按板子复位键, 待 nsh> 出现后重新运行。")
        print("[probe] !! (脚本的 RTS 复位对部分板子无效, 不要依赖它)")
        ser.close()
        sys.exit(2)
    results = []

    # 1. 检测/清理残留的 bttool 会话
    #    僵尸会话(create instance error 后)命令表为空, 连 quit 都无法执行,
    #    只能重启设备。正常会话发 quit 会回到 nsh>。
    print("[probe] 检测残留 bttool 会话 ...")
    ser.write(b"\n")
    out = pump(ser, 2)
    if b"bttool>" in out:
        # 有 bttool 会话, 尝试正常退出 (quit 必须只发 \n)
        ser.write(b"quit\n")
        out = pump(ser, 3)
        if b"UnKnow command quit" in out or b"Unknow command quit" in out:
            print("\n[probe] !! 检测到僵尸 bttool 会话(命令表未初始化, 无法用命令退出)。")
            print("[probe] !! 请物理复位: 拔插 USB 或按板子复位键, 待 nsh> 出现后重新运行本脚本。")
            print("[probe] !! (bluetoothd 由脚本自动启动, 无需手动)")
            ser.close()
            sys.exit(2)
        pump(ser, 2, (b"nsh>",))
    if not wait_prompt(ser, b"nsh>", 5):
        ser.write(b"\r\n")
        pump(ser, 3, (b"nsh>",))

    # 2. 启动 bluetoothd（若已运行会报 already running 之类，无害）
    out = cmd(ser, "bluetoothd &", 4)
    time.sleep(2)
    out += pump(ser, 4)
    results.append(("bluetoothd", "已启动" if b"already" not in out else "已在运行"))

    # 3. 进入 bttool 交互模式（新会话）
    out = cmd(ser, "bttool", 5, marker=b"bttool>")
    if b"create instance error" in out:
        print("[probe] !! create instance error：bluetoothd 未就绪，等待重试 ...")
        time.sleep(3)
        out = cmd(ser, "bttool", 5, marker=b"bttool>")
    results.append(("bttool", "OK(无实例错误)" if b"create instance error" not in out else "实例创建失败!"))

    # 4. 适配器使能
    cmd(ser, "enable", 6)
    out = pump(ser, 8)
    results.append(("enable", "已发送"))

    # 5. 状态
    out = cmd(ser, "state", 3)
    results.append(("state", out.decode(errors="replace")[-100:].replace("\n", " ")))

    # 6. 本地 BREDR 地址
    out = cmd(ser, "get addr", 3)
    addr = re.search(rb"([0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}", out)
    addr_str = addr.group(0).decode() if addr else "N/A"
    results.append(("get addr", addr_str))

    # 7. 可发现+可连接
    cmd(ser, "set scanmode 2", 3)
    pump(ser, 5)

    # 8. BREDR inquiry（10 秒，手机需开经典蓝牙可见性）
    out = cmd(ser, "inquiry start 10", 3)
    out += pump(ser, 16)
    devs = re.findall(rb"([0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}", out)
    found = len(devs) > 0
    results.append(("inquiry", f"发现 {len(devs)} 个设备" if found else "未发现设备"))

    # 9. 干净退出，回到 NSH
    cmd(ser, "quit", 2)
    pump(ser, 2, (b"nsh>",))

    print("\n========== 探测结果 ==========")
    for k, v in results:
        print(f"  {k}: {v}")

    if "实例创建失败" in results[2][1]:
        verdict = "FAIL (bluetoothd 未就绪 — 需人工排查蓝牙服务)"
    elif addr_str not in ("N/A", "00:00:00:00:00:00"):
        verdict = "PASS (BREDR 地址有效" + ("，且发现设备" if found else "，inquiry 无结果需复查手机可见性") + ")"
    else:
        verdict = "FAIL (BREDR 地址无效/不可用)"
    print(f"判定: {verdict}")
    ser.close()

if __name__ == "__main__":
    main()

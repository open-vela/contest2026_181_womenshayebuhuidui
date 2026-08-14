# 09 BREDR 闸门 0 探测方案（Round 3，2026-08-15）

> 前置：docs_ble/08（PAN 实现路线图）。标准 PAN(BNEP) 的决定性前提是
> **LCPU 固件是否支持 BREDR**。本方案用现成 bttool（CONFIG_BLUETOOTH_TOOLS=y 已编入），
> **零固件改动**即可在真机上完成探测。

## 为什么 discovery 就能探测 BREDR

zblue SAL 的 start_discovery 同时执行 **BREDR inquiry**（bt_br_* HCI 命令：
write_inquiry_scan_type / inquiry）与 LE 扫描（sal_adapter_interface.c 1025-1093 行）。
若 LCPU 固件不支持 BREDR，inquiry 命令会超时/失败并体现在日志中。

## 探测步骤（自动化脚本 docs_ble/tools/bredr_probe.py）

| 步骤 | 命令 | 判定信号 |
|------|------|----------|
| 1 | bttool enable | 适配器启用成功与否 |
| 2 | bttool get addr | **BREDR 本地地址非零** = 控制器有 BREDR 地址空间 |
| 3 | bttool set scanmode 2 | 可发现+可连接（inquiry/page scan 打开） |
| 4 | bttool discovery start 10 | **inquiry 发现设备**（手机需开经典蓝牙可见） |
| 5 | bttool dump | 抓取 timeout/unknown/fail 等错误关键词 |

脚本判定：
- get addr 非零 + discovery 有结果 → **闸门 0 疑似通过**
- 日志出现 HCI command timeout / unknown opcode → **闸门 0 失败**
- 地址非零但 discovery 空 → 需人工复查（手机可见性、日志中 inquiry 响应）

## 使用

```bash
# 板子插上、固件(当前 ai_agent 构建)烧录后：
python3 docs_ble/tools/bredr_probe.py /dev/ttyACM0 1000000
```

## 备选人工步骤（脚本异常时）

1. 串口手动跑 bttool 上述命令，观察 bluetoothd 日志：
   - "HCI command timeout" / "unknown HCI command" → BREDR 不可用
   - inquiry 正常返回设备 → BREDR 可用
2. 手机开"允许被其他设备发现"，重复 discovery start 10。
3. bttool pair <手机地址> 1（transport=1 即 BREDR）→ 配对成功则铁证 BREDR 可用。

## 探测通过后的下一步（PAN 实现）

1. 自研 BNEP（l2cap_br PSM 0x000F）→ sal_pan_interface.c/h → Kconfig CONFIG_BLUETOOTH_PAN
   → panu_service.c 编入（bttool panu 命令即可用，tools/panu.c 已存在）。
2. 构建开 CONFIG_NET_ETHERNET（TAP 桥接需要）。
3. 手机开"蓝牙网络共享"→ 与手表配对 → bt-pan 网卡 up → 验证代理上网。

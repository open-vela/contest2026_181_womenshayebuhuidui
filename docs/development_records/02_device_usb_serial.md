# 设备连接、USB 与串口

## 1. 目标设备

- 开发板：`SF32LB52-DevKit-LCD`
- USB 芯片枚举信息：`QinHeng 1a86:55d3 USB Single Serial`
- USB CDC ACM 节点：本轮最终使用 `/dev/ttyACM0`
- 串口速率：`1000000`
- RTS：用于板卡断电/上电或复位时序

设备名可能因重新枚举变化。`/dev/ttyACM0` 是本轮成功路径，不保证下一次连接仍使用同名节点。

## 2. 推荐检测顺序

```bash
# USB 是否枚举
lsusb

# 内核日志；若 dmesg 被限制则使用 journalctl
journalctl -k -n 100 --no-pager | grep -Ei 'usb|acm|1a86|55d3|error'

# 当前串口节点
ls -l /dev/ttyACM* /dev/ttyUSB* 2>/dev/null

# 稳定别名（可能不存在或因重新枚举变化）
ls -l /dev/serial/by-id/ 2>/dev/null

# 权限与占用
id
ls -l /dev/ttyACM0
fuser -v /dev/ttyACM0 2>/dev/null
```

若插拔后设备没有出现，不要继续使用旧路径；重新插拔/重启开发板后，应从 `lsusb` 和当前设备节点重新开始确认。

## 3. 本轮 USB 故障过程

### 3.1 `error -71`

[已验证] 内核日志曾多次出现 USB 枚举错误 `-71`，设备无法稳定进入可用串口状态。

[推断] `-71` 表示 USB 协议/枚举交互异常，可能与线材、供电、信号质量、主机 USB 控制器或板端复位时序有关；现有记录不足以证明唯一根因。

处理顺序：

1. 断开并重新连接开发板 USB。
2. 重启或复位开发板。
3. 等待 USB 重新枚举。
4. 重新执行 `lsusb`、内核日志和 `/dev/ttyACM*` 检查。
5. 只使用当前仍存在且确认对应开发板的设备节点。

### 3.2 `dmesg` 无权限

现象：

```text
dmesg: 读取内核缓冲区失败: 不允许的操作
```

原因：系统启用了 `kernel.dmesg_restrict=1`，普通用户不能读取内核 ring buffer。

处理：改用：

```bash
journalctl -k -n 100 --no-pager
```

该方法无需修改系统安全配置，适合本次排查。

### 3.3 by-id 路径失效

旧的 `/dev/serial/by-id/usb-1a86_USB_Single_Serial_5ABA071435-if00` 曾不存在。原因是 USB 设备断开或重新枚举后，原路径失效；不能把它直接解释为固件构建错误。

处理：检查当前 `/dev/ttyACM*`，最终确认 `/dev/ttyACM0` 可用于烧录和串口。

## 4. 串口终端

用户已确认下面命令可以正常回车并执行 NSH 命令：

```bash
picocom \
  -b 1000000 \
  --noreset \
  --lower-rts \
  --lower-dtr \
  --omap crlf \
  /dev/ttyACM0
```

关键点：

- `--noreset`：避免终端打开时自动复位板子。
- `--lower-rts --lower-dtr`：避免串口工具改变板卡电源/复位线状态。
- `--omap crlf`：将输入换行映射为目标 NSH 能正确识别的 CRLF；缺少它时曾出现 Enter 和命令无法执行。

连接后可测试：

```text
help
ai_agent -h
lvgldemo
```

## 5. RTS 与串口占用

烧录和串口终端不能同时占用 `/dev/ttyACM0`。烧录前关闭 picocom、Python 串口脚本等程序；串口验证前等待烧录工具退出。

板卡 RTS 复位的基本逻辑：

```python
ser.rts = True    # 断电/复位
sleep(0.5)
ser.rts = False   # 上电
```

实际时序应以 `docs/setup_guide.md` 和当前 `build_and_flash.sh` 为准。若烧录工具提示无法连接，优先检查串口占用、设备节点是否已变化和 RTS 后的 bootloader 等待时间。

## 6. 成功判据

- `lsusb` 能看到对应 USB 设备。
- 当前 `/dev/ttyACM*` 节点存在且权限允许访问。
- `fuser` 显示没有其他程序占用，或占用者是当前预期程序。
- picocom 使用 `--omap crlf` 后出现 NuttX NSH，并可执行 `help`。

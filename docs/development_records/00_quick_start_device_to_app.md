# 从验证设备连接到运行项目

本文档只保留“确认开发板连接 → 使用现有脚本完成构建烧录 → 连接 NSH → 运行项目”的快速流程。

> 目标项目：`contest2026_181_womenshayebuhuidui`  
> 脚本位置：`/home/aila/projects/vela_contest/contest2026_181_womenshayebuhuidui/build_and_flash.sh`  
> 目标板：`SF32LB52-DevKit-LCD`  
> 目标串口：通常为 `/dev/ttyACM0`，每次连接后必须重新确认。

详细错误原因见 [`05_troubleshooting.md`](05_troubleshooting.md)，完整设备排查见 [`02_device_usb_serial.md`](02_device_usb_serial.md)。

## 一、验证开发板连接

连接并重启开发板后执行：

```bash
lsusb
journalctl -k -n 100 --no-pager | grep -Ei 'usb|acm|1a86|55d3|error'
ls -l /dev/ttyACM* 2>/dev/null
ls -l /dev/serial/by-id/ 2>/dev/null
```

正常情况下应看到 QinHeng USB Serial：

```text
1a86:55d3
```

并出现类似设备节点：

```text
/dev/ttyACM0
```

如果没有设备节点：

1. 重新插拔 USB 线。
2. 重新启动开发板。
3. 再次执行上述检查。
4. 如果日志出现 `error -71`，按 [`02_device_usb_serial.md`](02_device_usb_serial.md) 排查 USB 枚举和供电问题。

如果 `dmesg` 报无权限，使用：

```bash
journalctl -k -n 100 --no-pager
```

## 二、确认串口未被占用

```bash
fuser -v /dev/ttyACM0 2>/dev/null
ls -l /dev/ttyACM0
```

烧录前关闭 picocom、Python 串口脚本等程序。同一个串口不能同时被烧录工具和终端占用。

## 三、使用现有脚本构建并烧录

不需要手动执行单独的编译命令或直接调用 `sftool`，使用项目已有脚本：

```bash
cd /home/aila/projects/vela_contest/contest2026_181_womenshayebuhuidui

./build_and_flash.sh \
  --port /dev/ttyACM0
```

如果需要确认脚本支持的参数：

```bash
./build_and_flash.sh --help
```

注意：不要将底层 `sftool` 的 `--connect-attempts` 参数直接传给脚本；此前该脚本不接受该参数。

脚本成功完成后，应看到类似结果：

```text
Connected success!
Download stub success!
烧录完成，板子已重启运行
```

这表示固件构建和烧录阶段完成。脚本完成后等待开发板重启，再连接串口。

## 四、连接 NuttX 串口

```bash
picocom \
  -b 1000000 \
  --noreset \
  --lower-rts \
  --lower-dtr \
  --omap crlf \
  /dev/ttyACM0
```

`--omap crlf` 不能省略。本项目曾出现缺少该参数时 Enter 和 NSH 命令无法执行的问题。

看到类似下面的提示，说明 NuttX 已启动：

```text
nsh>
```

## 五、验证并运行项目

先确认设备节点：

```text
nsh> ls /dev
```

重点检查：

```text
/dev/lcd0
/dev/fb0
/dev/input0
```

确认应用命令存在：

```text
nsh> ai_agent -h
```

先运行基础 LVGL 示例：

```text
nsh> lvgldemo
```

如果屏幕显示 LVGL demo，说明基础显示链路正常。然后运行项目：

```text
nsh> ai_agent -q hello
```

或者进入交互模式：

```text
nsh> ai_agent
ai> hello
ai> nuttx
ai> vela
ai> lcd
ai> help
ai> quit
```

正常情况下，AI Agent 预期显示：

```text
你好HerSen
AI Agent Ready
Waiting for query...
```

输入关键词后，屏幕响应文本应更新，同时串口输出 AI Response。

## 六、运行失败或黑屏时快速判断

| 现象 | 下一步 |
|---|---|
| 找不到 `/dev/ttyACM0` | 重新检查 USB 枚举，不要继续使用旧路径 |
| `build_and_flash.sh` 烧录失败 | 检查串口占用、RTS 状态和当前设备路径 |
| `ai_agent: command not found` | 确认脚本烧录的是包含 `CONFIG_EXAMPLES_AI_AGENT=y` 的最新固件 |
| `/dev/lcd0` 或 `/dev/input0` 不存在 | 检查 LCD、framebuffer、触摸驱动和板级初始化 |
| `lvgldemo` 也不能显示 | 优先排查基础 LVGL/LCD 驱动 |
| `lvgldemo` 能显示但 `ai_agent` 不能显示 | 检查 AI Agent 的 LVGL 初始化和设备路径 |
| 串口有初始化输出但屏幕全黑 | 检查背光、CO5300 时序和实际 framebuffer 节点 |
| 只有 `ai_agent -n` 能运行 | 控制台逻辑正常，显示链路仍需排查 |

## 七、完成标准

一次完整运行应同时满足：

- `lsusb` 能识别开发板。
- 当前有效串口节点存在。
- `build_and_flash.sh` 构建和烧录成功。
- 串口出现 `nsh>`。
- `ai_agent -h` 能执行。
- `lvgldemo` 或 AI Agent 能初始化 LVGL。
- 屏幕显示预期内容。
- 输入 `hello` 后响应能够更新。
- 重启开发板后可以重复运行。

当前记录中，构建、烧录和 NuttX 启动已经确认；屏幕是否实际点亮，必须通过开发板现场观察确认，不能仅凭脚本成功判断。

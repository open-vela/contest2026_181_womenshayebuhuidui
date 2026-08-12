# 构建、烧录与 NuttX 启动

## 1. 构建目标与产物

目标配置：`vendor/sifli/boards/sf32lb52/sf32lb52_devkit_lcd/configs/nsh`。

主要产物：

```text
/home/aila/projects/vela_contest/out/sifli_sf32lb52_devkit_lcd_nsh/nuttx.bin
```

已有基础指南：[`../setup_guide.md`](../setup_guide.md)。

## 2. 标准构建流程

```bash
cd /home/aila/projects/vela_contest
export PATH="/home/aila/projects/vela_contest/prebuilts/gcc/linux-x86_64/arm-none-eabi/bin:/home/aila/projects/vela_contest/prebuilts/kconfig-frontends/bin:$PATH"
source build/envsetup.sh
export VELA_EXTRA_FLAGS="-Wno-cpp -Wno-deprecated-declarations -Wno-error -Wno-strict-prototypes -Wno-undef"
lunch vendor/sifli/boards/sf32lb52/sf32lb52_devkit_lcd/configs/nsh
m
```

`VELA_EXTRA_FLAGS` 应在 `source` 之后、`lunch` 之前设置；其作用是避免 vendor SDK 警告被 `-Werror` 当作错误。

## 3. AI Agent 集成证据

[已验证] 当前构建配置包含：

```text
CONFIG_EXAMPLES_AI_AGENT=y
CONFIG_EXAMPLES_AI_AGENT_PROGNAME="ai_agent"
CONFIG_GRAPHICS_LVGL=y
CONFIG_EXAMPLES_LVGLDEMO=y
```

[已验证] 构建产物中的 `apps/builtin/builtin_list.h` 包含 `ai_agent` 和 `lvgldemo`。

[已验证] `nuttx.map` 中包含 `ai_agent_main.c.o` 和 LVGL demo 对象。

这些证据只能说明应用已经被编译、链接并注册为 NuttX builtin，不能说明命令已在开发板上执行，更不能单独证明屏幕已点亮。

## 4. 烧录参数

目标芯片和地址：

| 参数 | 值 |
|---|---|
| 芯片 | `SF32LB52` |
| 设备 | 本轮为 `/dev/ttyACM0` |
| 波特率 | `1000000` |
| 烧录地址 | `0x12010000` |
| 镜像 | `out/sifli_sf32lb52_devkit_lcd_nsh/nuttx.bin` |
| 结束动作 | `soft_reset` |

`build_and_flash.sh` 的参数应先执行 `--help` 确认。此前将 `--connect-attempts 20` 直接传给脚本时，脚本报参数不支持；这是脚本 CLI 与底层 `sftool` 参数不一致，不是编译错误。不要未经检查把底层参数添加到脚本命令行。

## 5. 本轮烧录结果

初次失败原因：旧的 `/dev/serial/by-id/...` 路径已经不存在，且 USB 曾发生 `error -71` 重新枚举。

重新连接和检测后改用 `/dev/ttyACM0`，烧录输出出现：

```text
Connected success!
Download stub success!
No need to re-download, skip!
烧录完成，板子已重启运行
```

[已验证] 固件烧录成功。

## 6. NuttX 启动与 NSH

[已验证] 烧录后开发板成功启动 NuttX，并出现 NSH 提示符。

启动时曾出现：

```text
WARN: LSM6DS3 not found at 0x6a on I2C1: -5
ERROR: LSM6DS3 register failed on I2C1: -5
ERROR: sf32lb52_lsm6ds3_initialize failed: -5
WARN: skip HAL_FLASH_Init during XIP bringup, NOR write/erase disabled
```

当前证据表明这些警告没有阻止 NuttX 启动和 NSH 出现。它们应记录为板上未连接/未识别传感器和 XIP 期间 NOR 写擦限制，不要误判为 LCD 已损坏。

串口命令必须使用带 `--omap crlf` 的 picocom 配置，详见 [`02_device_usb_serial.md`](02_device_usb_serial.md)。

## 7. 运行阶段命令

在 NSH 中按以下顺序测试：

```text
help
ls /dev
lvgldemo
ai_agent -h
ai_agent -q hello
ai_agent
```

每条命令都要单独记录原始输出和屏幕现象。命令存在、命令无报错、LVGL 成功初始化、屏幕出现预期内容是四个不同的验证层级。

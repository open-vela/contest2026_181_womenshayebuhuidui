# SF32LB52-DevKit-LCD 开发指南

本文档记录了从零开始在 SF32LB52-DevKit-LCD 板子上开发和运行应用的完整流程。

---

## 一、环境准备

### 1.1 工具链位置

ARM 交叉编译器已预装在项目中：

```
/home/aila/projects/vela_contest/prebuilts/gcc/linux-x86_64/arm-none-eabi/bin/
```

Kconfig 工具：

```
/home/aila/projects/vela_contest/prebuilts/kconfig-frontends/bin/
```

### 1.2 设置 PATH

每次打开新终端都需要执行：

```bash
export PATH="/home/aila/projects/vela_contest/prebuilts/gcc/linux-x86_64/arm-none-eabi/bin:/home/aila/projects/vela_contest/prebuilts/kconfig-frontends/bin:$PATH"
```

---

## 二、编译固件

### 2.1 完整编译命令

```bash
# 进入项目根目录
cd /home/aila/projects/vela_contest

# 1. 设置 PATH
export PATH="/home/aila/projects/vela_contest/prebuilts/gcc/linux-x86_64/arm-none-eabi/bin:/home/aila/projects/vela_contest/prebuilts/kconfig-frontends/bin:$PATH"

# 2. 加载构建环境
source build/envsetup.sh

# 3. 设置编译选项（必须在 source 之后、lunch 之前）
#    关键：去掉 -Werror，否则 vendor SDK 的警告会变成错误
export VELA_EXTRA_FLAGS="-Wno-cpp -Wno-deprecated-declarations -Wno-error -Wno-strict-prototypes -Wno-undef"

# 4. 选择板子配置
lunch vendor/sifli/boards/sf32lb52/sf32lb52_devkit_lcd/configs/nsh

# 5. 编译
m
```

编译成功后，固件位于：

```
out/sifli_sf32lb52_devkit_lcd_nsh/nuttx.bin
```

### 2.2 常见编译问题

| 问题 | 原因 | 解决方案 |
|------|------|----------|
| `arm-none-eabi-gcc: not found` | PATH 未设置 | 执行 export PATH=... |
| `-Werror=undef` 错误 | 默认开启了 -Werror | 设置 VELA_EXTRA_FLAGS 去掉 -Werror |
| `add_library cannot create target` | 目标名冲突 | 检查是否有重复的应用目录 |
| 链接时 undefined reference | 缺少库或未启用网络 | 检查 defconfig 或简化代码 |

---

## 三、烧录固件

### 3.1 烧录工具

使用 `sftool`（已安装在 `/usr/local/bin/sftool`）。

### 3.2 烧录方法

由于板子使用 RTS 控制电源，需要特殊的烧录流程：

```python
#!/usr/bin/env python3
import serial
import time
import subprocess
import threading

def flash_board():
    """在后台执行烧录"""
    time.sleep(0.5)  # 等待 bootloader 启动
    result = subprocess.run([
        'sftool', '-c', 'SF32LB52', '-p', '/dev/ttyACM0', '-b', '1000000',
        '--before', 'no_reset', '--connect-attempts', '20',
        '--after', 'soft_reset',
        'write_flash',
        '/home/aila/projects/vela_contest/out/sifli_sf32lb52_devkit_lcd_nsh/nuttx.bin@0x12010000'
    ], capture_output=True, text=True, timeout=180)
    print("returncode:", result.returncode)
    if result.returncode != 0:
        print("stderr:", result.stderr)

# 启动烧录线程
flash_thread = threading.Thread(target=flash_board)
flash_thread.start()

# 通过 RTS 复位板子
ser = serial.Serial('/dev/ttyACM0', 1000000, timeout=0.1)
ser.rts = True   # 断电
time.sleep(0.5)  # 保持 500ms
ser.rts = False   # 上电
ser.close()

# 等待烧录完成
flash_thread.join(timeout=200)
```

### 3.3 烧录参数说明

| 参数 | 说明 |
|------|------|
| `-c SF32LB52` | 芯片型号 |
| `-p /dev/ttyACM0` | 串口设备（USB CDC） |
| `-b 1000000` | 波特率 |
| `--before no_reset` | 不自动复位（手动控制） |
| `--connect-attempts 20` | 连接尝试次数 |
| `--after soft_reset` | 烧录后软复位 |
| `@0x12010000` | 烧录偏移地址（XIP） |

### 3.4 烧录失败排查

| 现象 | 原因 | 解决方案 |
|------|------|----------|
| `Failed to download stub: Timeout` | bootloader 未进入 | 调整 RTS 时序，增加等待时间 |
| `Failed to connect to the chip` | 串口被占用 | 关闭其他串口终端 |
| 挂起不动 | 板子状态异常 | 拔插 USB 线，重新烧录 |

---

## 四、串口交互

### 4.1 连接串口

```bash
# 推荐使用 picocom（不会意外复位板子）
picocom -b 1000000 --noreset --lower-rts --lower-dtr /dev/ttyACM0
```

### 4.2 Python 串口脚本

```python
import serial, time

ser = serial.Serial('/dev/ttyACM0', 1000000, timeout=1)

# 发送命令
ser.write(b'help\r\n')
time.sleep(1)

# 读取响应
data = ser.read(2000)
print(data.decode('utf-8', errors='replace'))

ser.close()
```

### 4.3 复位板子

```python
import serial, time

ser = serial.Serial('/dev/ttyACM0', 1000000, timeout=0.1)
ser.rts = True   # 断电
time.sleep(0.1)
ser.rts = False   # 上电
ser.close()
time.sleep(2)     # 等待启动
```

---

## 五、创建自定义应用

### 5.1 目录结构

在比赛仓库中创建应用：

```
contest2026_181_womenshayebuhuidui/
└── app/
    └── my_app/
        ├── CMakeLists.txt    # CMake 构建文件
        ├── Kconfig           # 配置选项
        ├── Make.defs         # Make 构建注册
        ├── Makefile          # Make 构建文件
        ├── my_app_main.c     # 主程序
        └── README.md         # 说明文档
```

### 5.2 Kconfig 模板

```kconfig
config EXAMPLES_MY_APP
	bool "My Application"
	default n
	---help---
		Enable my application.

if EXAMPLES_MY_APP

config EXAMPLES_MY_APP_PROGNAME
	string "Program name"
	default "my_app"
	---help---
		The program name for NSH.

config EXAMPLES_MY_APP_STACKSIZE
	int "Stack size"
	default 8192

config EXAMPLES_MY_APP_PRIORITY
	int "Priority"
	default 100

endif # EXAMPLES_MY_APP
```

**重要：必须定义 `PROGNAME`，否则应用不会被编译！**

### 5.3 CMakeLists.txt 模板

```cmake
if(CONFIG_EXAMPLES_MY_APP)
  nuttx_add_application(
    NAME
    ${CONFIG_EXAMPLES_MY_APP_PROGNAME}
    PRIORITY
    ${CONFIG_EXAMPLES_MY_APP_PRIORITY}
    STACKSIZE
    ${CONFIG_EXAMPLES_MY_APP_STACKSIZE}
    MODULE
    ${CONFIG_EXAMPLES_MY_APP}
    SRCS
    my_app_main.c)
endif()
```

### 5.4 Make.defs 模板

```makefile
ifneq ($(CONFIG_EXAMPLES_MY_APP),)
CONFIGURED_APPS += $(APPDIR)/demos/contest2026_181_my_app
endif
```

**注意：路径必须指向应用的实际位置！**

### 5.5 注册应用

#### 方法一：通过 manifest（推荐）

编辑 `contest2026_181_womenshayebuhuidui.xml`：

```xml
<project path="contest2026_181_womenshayebuhuidui" name="contest2026_181_womenshayebuhuidui">
    <linkfile src="app/hello_app" dest="packages/demos/contest2026_181_hello_app"/>
    <linkfile src="app/my_app" dest="packages/demos/contest2026_181_my_app"/>
</project>
```

然后创建 symlink：

```bash
ln -sf ../../contest2026_181_womenshayebuhuidui/app/my_app \
    /home/aila/projects/vela_contest/packages/demos/contest2026_181_my_app
```

#### 方法二：直接编辑 Kconfig

编辑 `packages/demos/Kconfig`，添加：

```kconfig
source "/home/aila/projects/vela_contest/packages/demos/contest2026_181_my_app/Kconfig"
```

### 5.6 启用应用

在 defconfig 中添加：

```
CONFIG_EXAMPLES_MY_APP=y
CONFIG_EXAMPLES_MY_APP_PROGNAME="my_app"
CONFIG_EXAMPLES_MY_APP_STACKSIZE=8192
CONFIG_EXAMPLES_MY_APP_PRIORITY=100
```

defconfig 位置：

```
vendor/sifli/boards/sf32lb52/sf32lb52_devkit_lcd/configs/nsh/defconfig
```

---

## 六、AI Agent 应用说明

### 6.1 应用位置

```
contest2026_181_womenshayebuhuidui/app/ai_agent/
├── ai_agent_main.c
├── CMakeLists.txt
├── Kconfig
├── Make.defs
├── Makefile
└── README.md
```

### 6.2 使用方法

```bash
# 单次查询
nsh> ai_agent -q hello
nsh> ai_agent -q nuttx
nsh> ai_agent -q vela

# 交互模式（默认）
nsh> ai_agent
ai> hello
ai> nuttx
ai> quit

# 显示帮助
nsh> ai_agent -h
```

### 6.3 支持的关键词

| 关键词 | 响应 |
|--------|------|
| hello | 问候语 |
| nuttx | NuttX RTOS 介绍 |
| vela | OpenVela 平台介绍 |
| sifli | SiFli 芯片介绍 |
| lcd | LCD 屏幕信息 |
| help | 帮助信息 |

---

## 七、完整工作流程

从零开始的完整流程：

```bash
# ========== 第一步：环境准备 ==========
cd /home/aila/projects/vela_contest
export PATH="$(pwd)/prebuilts/gcc/linux-x86_64/arm-none-eabi/bin:$(pwd)/prebuilts/kconfig-frontends/bin:$PATH"

# ========== 第二步：创建应用 ==========
mkdir -p contest2026_181_womenshayebuhuidui/app/my_app
# ... 编写代码和配置文件 ...

# ========== 第三步：注册应用 ==========
# 创建 symlink
ln -sf ../../contest2026_181_womenshayebuhuidui/app/my_app \
    packages/demos/contest2026_181_my_app

# 编辑 Kconfig
echo 'source "/home/aila/projects/vela_contest/packages/demos/contest2026_181_my_app/Kconfig"' \
    >> packages/demos/Kconfig

# ========== 第四步：启用应用 ==========
echo -e "\nCONFIG_EXAMPLES_MY_APP=y" >> \
    vendor/sifli/boards/sf32lb52/sf32lb52_devkit_lcd/configs/nsh/defconfig

# ========== 第五步：编译 ==========
source build/envsetup.sh
export VELA_EXTRA_FLAGS="-Wno-cpp -Wno-deprecated-declarations -Wno-error -Wno-strict-prototypes -Wno-undef"
lunch vendor/sifli/boards/sf32lb52/sf32lb52_devkit_lcd/configs/nsh
m

# ========== 第六步：烧录 ==========
python3 -c "
import serial, time, subprocess, threading

def flash():
    time.sleep(0.5)
    subprocess.run([
        'sftool', '-c', 'SF32LB52', '-p', '/dev/ttyACM0', '-b', '1000000',
        '--before', 'no_reset', '--connect-attempts', '20',
        '--after', 'soft_reset',
        'write_flash',
        'out/sifli_sf32lb52_devkit_lcd_nsh/nuttx.bin@0x12010000'
    ], timeout=180)

t = threading.Thread(target=flash)
t.start()
ser = serial.Serial('/dev/ttyACM0', 1000000, timeout=0.1)
ser.rts = True; time.sleep(0.5); ser.rts = False
ser.close()
t.join(timeout=200)
"

# ========== 第七步：运行 ==========
python3 -c "
import serial, time
ser = serial.Serial('/dev/ttyACM0', 1000000, timeout=1)
ser.write(b'my_app\r\n')
time.sleep(2)
print(ser.read(2000).decode())
ser.close()
"
```

---

## 八、注意事项

1. **编译选项**：必须设置 `VELA_EXTRA_FLAGS` 去掉 `-Werror`，否则 vendor SDK 编译失败
2. **应用注册**：Kconfig 中必须定义 `PROGNAME`，否则应用不会被编译
3. **Make.defs 路径**：必须指向应用的实际位置，不是示例位置
4. **烧录时序**：RTS 复位后需要等待足够时间让 bootloader 启动
5. **串口工具**：使用 `picocom --noreset --lower-rts --lower-dtr` 避免意外复位
6. **defconfig 安全**：如果包含 API key 等敏感信息，添加到 `.gitignore`

---

## 九、文件位置速查

| 文件 | 路径 |
|------|------|
| 板子配置 | `vendor/sifli/boards/sf32lb52/sf32lb52_devkit_lcd/configs/nsh/defconfig` |
| 编译输出 | `out/sifli_sf32lb52_devkit_lcd_nsh/nuttx.bin` |
| 应用目录 | `contest2026_181_womenshayebuhuidui/app/` |
| Symlink 目标 | `packages/demos/contest2026_181_*` |
| demos Kconfig | `packages/demos/Kconfig` |
| ARM 工具链 | `prebuilts/gcc/linux-x86_64/arm-none-eabi/bin/` |

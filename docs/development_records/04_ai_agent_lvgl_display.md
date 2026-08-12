# AI Agent、LVGL 与开发板显示

## 1. 不能直接运行 `.c` 文件

`app/ai_agent/ai_agent_main.c` 是源码，不是在开发板上直接执行的脚本。它必须通过 Kconfig/CMake/Make.defs 编译进 NuttX，并以 builtin 命令运行：

```text
nsh> ai_agent
```

当前固件已经满足编译集成条件：`CONFIG_EXAMPLES_AI_AGENT=y`，并且 `builtin_list.h`、`nuttx.map` 均包含相关证据。具体构建和烧录过程见 [`03_build_flash_nuttx.md`](03_build_flash_nuttx.md)。

## 2. 当前实现能力边界

`ai_agent_main.c` 使用静态 `g_responses[]` 关键词表，根据输入返回本地字符串。已知关键词包括 `hello`、`nuttx`、`vela`、`sifli`、`weather`、`help`、`lcd`。

因此当前可确认的是：

| 能力 | 状态 |
|---|---|
| NuttX builtin 命令 `ai_agent` | [已验证] 已编入当前构建产物 |
| 本地关键词响应 | [已验证] 代码中存在 |
| LVGL UI 创建代码 | [已验证] 代码中存在 |
| 云端 LLM/WebSocket | [待验证/未实现证据] 当前源码未展示实际云端调用 |
| ASR/TTS | [待验证/未实现证据] 当前源码未展示硬件和云端链路 |
| 通用 Agent 规划、工具调用和对话记忆 | [待验证] 当前代码不足以证明 |

README 中关于 `-s`、`-i` 的描述与当前源码 getopt 字符串 `"hq:n"` 不完全一致。以实际源码和 `ai_agent -h` 输出为准；不要把文档描述当作已经实现的参数。

## 3. LVGL 初始化内容

源码中的显示初始化：

```c
info.fb_path = "/dev/lcd0";
info.input_path = "/dev/input0";
lv_nuttx_init(&info, &result);
```

UI 预期内容：

- 黑色背景。
- 顶部绿色标题：`你好HerSen`。
- 灰色状态：`AI Agent Ready`。
- 白色响应文本：`Waiting for query...`。
- 查询时状态变为 `Thinking...`，完成后恢复 `AI Agent Ready`。

当前构建还启用了 CO5300 390×450 LCD、FT6146 输入和 framebuffer/LVGL 相关配置。实际设备节点必须在板上通过 `ls /dev` 现场确认，不能只依据 `.config` 推断节点一定存在。

## 4. 从基础显示到 AI Agent 的验证顺序

### 4.1 先确认设备节点

```text
nsh> ls /dev
```

重点检查：

```text
/dev/lcd0
/dev/fb0
/dev/input0
```

设备节点缺失时，先不要判断是 AI Agent 代码问题，应回到 LCD、framebuffer、输入驱动和板级初始化排查。

### 4.2 验证基础 LVGL

```text
nsh> lvgldemo
```

记录：

- 命令是否存在。
- 是否报 LVGL/NuttX 驱动错误。
- 屏幕是否出现 demo。
- 屏幕是否仍全黑。

`lvgldemo` 编入固件只证明构建集成成功；只有在现场观察到 demo，才能证明基础显示链路工作。

### 4.3 验证 AI Agent 入口

```text
nsh> ai_agent -h
nsh> ai_agent -q hello
nsh> ai_agent
```

交互模式可输入：

```text
ai> hello
ai> nuttx
ai> vela
ai> lcd
ai> help
ai> quit
```

`-n` 可用于跳过 LVGL 初始化、单独验证控制台逻辑：

```text
nsh> ai_agent -n
```

如果 `-n` 模式能返回本地关键词响应而普通模式不能显示，问题范围会缩小到 LVGL/设备路径/显示驱动；如果连 `-n` 都不能运行，应先检查固件是否与当前构建产物一致。

## 5. 黑屏诊断分层

| 观察结果 | 优先判断 |
|---|---|
| `ai_agent` 不存在 | 开发板仍运行旧固件，重新确认烧录镜像 |
| `ai_agent -n` 能运行，普通模式报 LVGL 初始化失败 | 检查 `/dev/lcd0`、`/dev/input0`，再比较 `/dev/fb0` |
| `lvgldemo` 也无法初始化 | LCD/framebuffer/LVGL 基础链路问题 |
| `lvgldemo` 有画面但 `ai_agent` 无画面 | AI Agent 初始化参数或 UI 代码问题 |
| 串口显示初始化成功但屏幕全黑 | 检查背光、LCD 控制器、刷新时序和实际节点 |
| 画面出现但中文异常 | 检查 `lv_font_simsun_16_cjk` 和字体配置 |

在没有现场输出前，不应直接把 `info.fb_path` 从 `/dev/lcd0` 改成 `/dev/fb0`；这只能作为后续排障假设。

## 6. 最终显示验收

将显示标记为 `[已验证]` 前必须记录：

1. 开发板完成启动并出现 NSH。
2. `lvgldemo` 或 `ai_agent` 命令执行无关键初始化错误。
3. 屏幕不是全黑或随机噪点。
4. 能看到预期标题/状态/响应内容。
5. 输入 `hello` 等关键词后，响应文本能更新。
6. 断电/重启后至少重复一次仍能显示。
7. 最好保存现场照片、串口输出或测试日期。

截至当前记录：烧录和 NuttX 启动已确认；屏幕实际点亮仍为 **[待验证]**。

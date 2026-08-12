# 企业级交付手册 — contest2026_181_womenshayebuhuidui

> **项目**：基于 SF32LB52 的嵌入式 AI 智能体（AI Agent + LVGL 显示 + 蓝牙联网 + LLM 对话）
> **队伍**：181 | **仓库**：`contest2026_181_womenshayebuhuidui`
> **比赛**：2026 首届 openvela AI 硬件开发者大赛（AI 硬件产品创新赛道）
> **版本**：v1.0（草案） | **日期**：2026-08-12 | **状态**：交付基线草案（按里程碑滚动更新）
> **比赛截止**：2026-09-20

---

## 文档控制

| 项 | 内容 |
|---|---|
| 文档版本 | v1.0（2026-08-12） |
| 维护者 | Team 181（Sen70s） |
| 评审机制 | 每次里程碑完成后评审更新；CodeReview 子代理审校 |
| 配套文档 | [开发记录索引](development_records/README.md)、[TASK_STATUS.md](../TASK_STATUS.md)、[PROJECT_STATUS_REPORT.md](development_records/PROJECT_STATUS_REPORT.md) |
| 状态标记 | **[已验证]** 有命令输出/构建产物/现场反馈支持；**[部分验证]** 链路部分成功；**[待验证]** 需现场确认；**[推断]** 可能解释非唯一根因 |

---

# 1. 项目概述

## 1.1 项目背景与目标

本项目为 openvela AI Coding Contest 2026 参赛作品，目标是**在无 WiFi 模块的 SF32LB52 穿戴级硬件上，构建一个可通过蓝牙联网、具备 LLM 对话能力的 AI 智能体桌面宠物**，实现"端侧 AI + 手机网关"的产品形态：

- **端侧**：SF32LB52-DevKit-LCD 运行 NuttX/openvela，AI Agent 负责对话逻辑与 UI 展示；
- **通道**：手机 App 通过 **BLE GATT NUS 透传**为设备提供互联网网关（透明代理，非 VPN）；
- **云端**：LLM（OpenAI 兼容 API，阶跃星辰 step-3.7-flash）提供智能对话能力；
- **产品亮点**：低成本穿戴硬件 + 手机即网关，无需 WiFi 模组即可获得云端 AI 能力。

## 1.2 交付范围与边界

**本次交付包含：**

| 类别 | 内容 |
|---|---|
| 设备端固件 | NuttX 配置（board/contest_board）、ai_agent 应用、LVGL 显示、蓝牙/网络框架适配 |
| 手机 App | com.agent.coapp（Kotlin + Compose），含 BLE GATT NUS 隧道、配置/技能/日志管理 |
| 验证证据 | QEMU 全链路验证、真机 BLE 链路验证、故障排查记录（15 项，12 项已解决） |
| 工程资产 | 开发记录 11 份、AI Coding 日志 6 会话、构建烧录脚本 4 个 |

**明确不在本次范围：**

- ❌ WiFi 联网（SF32LB52 硬件无 WiFi 模组，[已验证] 硬件限制）
- ❌ 经典蓝牙 SPP（LCPU 闭源固件不支持 BREDR，[已验证] 已否决）
- ❌ ASR/TTS 语音交互（需音频硬件与 SDK，未实现，保留接口）
- ❌ USB RNDIS 直连（芯片 USB device 的 D+/D- 未引出至 Type-C，[已验证] 排除）

## 1.3 关键指标（KPI）

| 指标 | 当前值 | 目标值 | 状态 |
|---|---|---|---|
| 固件构建成功 | 100% | 100% | ✅ [已验证] |
| 烧录成功率 | 常规成功 | 100% | ✅ [已验证]（偶发需断电重插） |
| QEMU LLM 对话 | 通过（step-3.7-flash） | 通过 | ✅ [已验证] |
| 真机 LLM 对话 | 未验证 | 通过 | ❌ 阻塞于 BLE GATT NUS |
| BLE 吞吐 | 预期 10-30KB/s | ≥10KB/s | ⏸️ 待实测（MTU 247） |
| littlefs 持久化 | 重启后数据保留 | 保留 | ✅ [已验证] |
| 屏幕点亮 | 待现场验收 | 点亮 | 🔄 [待验证] |

## 1.4 术语表

| 术语 | 说明 |
|---|---|
| LCPU | SF32LB52 内置蓝牙控制器运行单元，执行闭源固件 |
| BREDR | 经典蓝牙（Basic Rate / Enhanced Data Rate） |
| GATT NUS | Nordic UART Service 透传协议（6e400001 系列 UUID） |
| TUN | 内核虚拟网卡设备（本项目为 bt-net，192.168.55.2/24） |
| littlefs | 掉电安全的嵌入式文件系统（NOR Flash 持久化） |
| bluetoothd | openvela 蓝牙框架守护进程（基于 zblue 协议栈） |
| SAL | zblue 的系统抽象层（仅 stacks/zephyr 有实现） |

---

# 2. 方案与架构

## 2.1 总体架构

```
┌─ SF32LB52 设备端 ───────────────────────────┐   ┌─ Android 手机端 ───────────────────────────┐
│                                              │   │                                              │
│  ai_agent 应用（对话逻辑 / 命令 CLI）         │   │  com.agent.coapp                              │
│    ├─ LVGL UI（宠物桌宠 + 对话气泡）          │   │    ├─ UI（Compose + MVVM）                   │
│    └─ netmgr（TCP/TLS/DNS/cron/REST）         │   │    │   ├─ ChatScreen / ConfigScreen          │
│         ↓ 发往 0.0.0.0 的 IP 包               │   │    │   ├─ SkillsScreen / LogsScreen          │
│  TUN bt-net (192.168.55.2/24, MTU 1518)       │   │    │   └─ TunnelScreen（BLE 扫描/连接）      │
│         ↓↑ 整包转发                           │   │    ├─ BleTunnelManager（NUS 客户端）         │
│  BLE GATT NUS Server (6e400001)               │◄──┤    │   ├─ TunnelService（桥接层）             │
│         ↓↑ 帧协议 [len_hi][len_lo][payload]   │   │    │   └─ TcpProxy（TCP/DNS 用户态代理）      │
│  zblue / bluetoothd（BLE 5.3 协议栈）         │   │    └─ 本机真实网络（socket）                 │
│                                              │   │                                              │
│  NuttX RTOS / LVGL 9.1 / littlefs / 驱动      │   │   角色：手机 = 网关 + 透明 TCP 代理           │
└──────────────────────────────────────────────┘   └──────────────────────────────────────────────┘
```

**角色定位**：手机 App 承担"网关 + TCP/DNS 透明代理"（用户态代理，非 VPN，无需授权）；设备端所有出网流量通过 BLE 隧道交给手机转发，设备侧代码保持"我本机有网"的语义，无需感知隧道细节。

## 2.2 硬件平台

| 组件 | 规格 |
|---|---|
| MCU | SF32LB52（ARM Cortex-M4F @ 120MHz，LCPU 运行闭源蓝牙固件） |
| 内存 | 16MB NOR Flash + 8MB PSRAM（N16R8 模组） |
| 屏幕 | CO5300 390×450，16bpp（RGB565，AMOLED 面板，开发记录中亦写作 LCD），设备节点 /dev/lcd0 |
| 触控 | FT6146 电容触摸，设备节点 /dev/input0 |
| 蓝牙 | BLE 5.3 + BREDR 双模（**[已验证]** 仅 BLE 可用，BREDR 被固件忽略） |
| 串口 | USB CDC ACM（QinHeng 1a86:55d3 → /dev/ttyACM0） |
| 烧录 | sftool，镜像写入 0x12010000（XIP） |

## 2.3 软件分层

| 层 | 组件 | 说明 |
|---|---|---|
| 应用层 | `ai_agent`（379 行 C） | 命令行 CLI + LVGL UI，关键词响应表（v2.0 基础版） |
| 框架层 | LVGL 9.1 / bluetoothd(zblue) / netmgr / littlefs | openvela 标准框架 |
| 系统层 | NuttX RTOS（NSH、文件系统、网络栈、设备驱动） | 含 vendor 板级适配（sf32lb52_* 驱动） |
| 配套 App | com.agent.coapp（Kotlin） | 29 个 Kotlin 文件，minSdk 26，Compose + MVVM |

## 2.4 联网技术路线演进（含决策记录）

| 时间 | 路线 | 结论 |
|---|---|---|
| 08-11 | BLE SPP + TUN | ✅ 设备端链路验证通过，后否决 |
| 08-12 | 深度诊断 | ✅ HCI trace 实证：`Write_Scan_Enable` 零响应 → **LCPU 固件仅实现 BLE** |
| 08-12 | **BLE GATT NUS（当前主线）** | 🔄 进行中：NUS 标准服务，手机广播可见，App 复用 BleManager |
| 待定 | PAN（自实现 BNEP）/ USB-TTL SLIP | ⏸️ 兜底方案评估中（决策点：1 周内） |

**ADR-001：SPP → BLE GATT NUS 转向（2026-08-12）**

- 背景：手机系统蓝牙永远扫描不到设备，配置全部正确仍失败；
- 根因：BREDR 命令被 LCPU 固件静默忽略（HCI trace 实证）；
- 决策：转向 BLE GATT NUS 透传——穿戴芯片 BLE 是强项，广播一定可见，吞吐 10-30KB/s 对 LLM 文本流足够；
- 影响：App 端复用现有 BleManager（改动小），设备端需实现 NUS server + TUN↔GATT 桥接（M1.5，2-3 天）。

**ADR-002：蓝牙协议栈 = zblue（非 BLUELET）**

- 官方 SAL 仅 `stacks/zephyr` 实现；PAN 官方未实现（dev 分支接口清单核实，[已验证]）。

## 2.5 模块设计

### 2.5.1 ai_agent 应用模块（设备端）

主线固件采用 **openvela 官方 ai_agent 框架**（`packages/ai_agent`，通过 `CONFIG_EXAMPLES_AI_AGENT_VELA=y` + `CONFIG_EXAMPLES_AI_AGENT_VELA_SHELL_FULL=y` 启用）：

- 命令 CLI：`vela>` 提供 `set_llm` / `ask` / `net_status` / `set_dns` / cron / skill / REST API（QEMU 全链路 [已验证]）；
- 扩展组件：`CONFIG_AI_AGENT_LVGL_UI=y`（LVGL 显示）、`CONFIG_AI_AGENT_BLE_GATT=y`（GATT NUS 通道，M1.5）、`CONFIG_AI_AGENT_NET_RPMSG=y`（网络接线）；
- 数据目录：`/data/ai_agent`（config.json / cron.json / skills，littlefs 持久化）。

**自研示例应用** `app/ai_agent/ai_agent_main.c`（379 行）同时保留，用于真机显示链路验证与示例参考（非主线 LLM 框架）：

- 启动画面 "你好HerSen" 标题 + "AI Agent Ready" 状态 + 白色响应文本（lv_font_simsun_16_cjk 中文字体）；
- 本地关键词应答表 `g_responses[]`（hello/nuttx/vela/sifli/lcd/help/weather/default），小写归一化 + `strstr` 匹配；
- 模式：`-q <query>` 单次查询、`-n` 跳过 LVGL（控制台模式）、默认交互模式。

### 2.5.2 显示模块（LVGL）

- `/dev/lcd0`（framebuffer）+ `/dev/input0`（触摸），`lv_nuttx_init` 挂载；
- 390×450 RGB565，标题/状态/响应三 Label + 长文本换行（LV_LABEL_LONG_WRAP）；
- 屏幕点亮状态：**[待验证]**（需现场验收，不能仅凭命令存在判定）。

### 2.5.3 网络模块

- `netmgr` 网络管理：状态机 DISCONNECTED→CONNECTED，IP 10.0.2.15（QEMU）/ 192.168.55.2（TUN bt-net，MTU 1518）；
- TUN 设备 + 路由：设备默认路由指向 bt-net，出网 IP 包整包经蓝牙隧道转发；
- 关键 defconfig：`CONFIG_NET_TUN=y`、`CONFIG_NET_LOCAL=y`（socket PF_LOCAL，bluetoothd IPC 必需）、`CONFIG_NET_TCP=y/UDP=y`；
- DNS：默认 223.5.5.5/8.8.8.8，可 `set_dns` 覆盖；真机场景由 App UDP 代理转发。

### 2.5.4 蓝牙模块（zblue / bluetoothd）

| 组件 | 状态 | 说明 |
|---|---|---|
| HCI 传输 | ✅ [已验证] | `CONFIG_UART_BTH4=y`（H4 控制器），bth4 recv 命令链路完整 |
| 初始化命令 | ✅ [已验证] | Reset/Features/Version/BD_ADDR 全部 Command Complete |
| BREDR | ❌ [已验证] | Write_Scan_Enable 零响应（LCPU 固件缺陷，已确证） |
| 广播 | ⚠️ [已验证] | ext adv（0x2039）假成功、空口无信号 → LCPU 缺陷；legacy 广播（0x200A）待测 |
| GATT | 🔄 开发中 | 主线 defconfig 已开 `CONFIG_BLUETOOTH_GATT=y` + `CONFIG_BLUETOOTH_GATT_SERVER=y`（08-12 提交），**待真机验证**；GATT=y 曾是崩溃诱因（R1），未根解 |
| 设备名 | ✅ | `Agent-Watch`（defconfig `CONFIG_BT_DEVICE_NAME` + 动态改名） |

**已定位的稳定性问题**：bluetoothd 偶发 hardfault（`CFSR=0x8200 PRECISERR`，`BFAR=0x00100121`，inode 链表被越界写破坏），根因为 FLAT build 布局敏感堆越界，13 个探针点绕过崩溃，**修复待执行（TASK-4）**。

### 2.5.5 存储模块（littlefs）

- `/data`（type littlefs，约 4MB），AI Agent 数据目录 `/data/ai_agent`（配置、cron.json、skills）；
- NOR 写入修复（[已验证]）：DMA 页写路径失效 + QMODE 下 QE 位未设置 → `sf32lb_flash_write_page_single()` 单线写页替代，wdog 硬复位后数据保留。

### 2.5.6 Android 配套 App（com.agent.coapp）

| 模块 | 文件 | 职责 |
|---|---|---|
| BLE 层 | `BleManager.kt` | 扫描/连接/通知监听（支持参数化过滤） |
| 隧道层 | `BleTunnelManager.kt` | **GATT NUS 客户端（6e400001）+ 帧协议 + 20B 分片写入**（新，08-12） |
| 桥接层 | `TunnelService.kt` | BLE ↔ TCP 代理桥接（由 SPP 改为 BLE） |
| 代理层 | `TcpProxy.kt` | 本机 socket 转发设备 TCP/DNS 流量 |
| UI/VM | Tunnel/Chat/Config/Skills/Logs Screen + ViewModel | Compose + MVVM |
| 已删除 | `SppManager.kt` | SPP 方案废弃后删除（见 ADR-001） |

架构约定（[已验证] 代码现状）：App 代理设备联网使用**普通前台 Service 而非 VpnService**（透明代理，无需 VPN 授权，兼容性更好）。

---

# 3. 构建与部署

## 3.1 环境要求

| 项 | 要求 |
|---|---|
| 主机 OS | Linux（本项目在 Ubuntu 22.04 验证） |
| 工具链 | 项目内置：`prebuilts/gcc/linux-x86_64/arm-none-eabi/bin`、`prebuilts/kconfig-frontends/bin`、`prebuilts/build-tools/linux-x86_64/bin`（genromfs，主线 cmake 构建必需） |
| 烧录工具 | `sftool`（/usr/local/bin/sftool） |
| 串口工具 | picocom（`--noreset --lower-rts --lower-dtr`）、Python pyserial |
| 其他 | repo 工具（已配置 .repo/ 同步 openvela 全量工程） |

## 3.2 源码获取与工程映射

```bash
repo init -u https://github.com/open-vela/contest2026_181_womenshayebuhuidui \
  -b dev-ai-contest-2026 -m contest2026_181_womenshayebuhuidui.xml
repo sync -c -j8
```

| 团队仓路径 | openvela 编译树映射 |
|---|---|
| `app/ai_agent` | `packages/demos/contest2026_181_ai_agent` |
| `app/hello_app` | `packages/demos/contest2026_181_hello_app` |
| `quickapp/hello_quickapp` | `packages/apps/contest2026_181_hello_quickapp` |
| `board/contest_board` | `vendor/openvela/boards/contest2026_181_board` |

> 映射经 manifest `<linkfile>` 软链完成，生产仓库零改动。新增应用需同步补 `<linkfile>` 条目并执行 `ln -sf` 建立符号链接。

## 3.3 固件构建

### 3.3.1 vendor 基线流程（手工，旧框架示例）

```bash
cd /home/aila/projects/vela_contest
export PATH="$(pwd)/prebuilts/gcc/linux-x86_64/arm-none-eabi/bin:$(pwd)/prebuilts/kconfig-frontends/bin:$PATH"
source build/envsetup.sh
export VELA_EXTRA_FLAGS="-Wno-cpp -Wno-deprecated-declarations -Wno-error -Wno-strict-prototypes -Wno-undef"
lunch vendor/sifli/boards/sf32lb52/sf32lb52_devkit_lcd/configs/nsh
m
```

产物：`out/sifli_sf32lb52_devkit_lcd_nsh/nuttx.bin`（vendor nsh 配置，启用旧框架 `CONFIG_EXAMPLES_AI_AGENT=y`，**不含 BLE/TUN 主线功能**；主线构建见 3.3.3）

### 3.3.2 项目自定义配置

本项目维护 3 份自定义 defconfig（`board/contest_board/configs/`）：

| 配置 | 用途 | 关键差异 |
|---|---|---|
| `ai_agent/defconfig`（282 行） | **主线配置** | `CONFIG_EXAMPLES_AI_AGENT_VELA=y`（官方框架）+ `_SHELL_FULL`、`CONFIG_AI_AGENT_LVGL_UI=y`、`CONFIG_AI_AGENT_BLE_GATT=y`、`CONFIG_BLUETOOTH_GATT=y`、TUN/littlefs/zblue BLE |
| `spp_verify/defconfig`（285 行） | SPP 验证（已否决，归档） | 含 BREDR/SPP 相关项 |
| `nsh/defconfig`（3 行） | 占位模板 | contest2026_000_board 样例，不可引导真机（仅结构示例） |

**⚠️ 构建缓存陷阱（必读）**：修改 defconfig 后若 CMake 缓存未失效，配置不生效。可靠做法：**删除 `out/` 目录后重新 configure**。同类陷阱：`.config.prev` 比较机制会导致 `include/nuttx/config.h` 不重新生成——删除 `.config/.config.prev/.../include/nuttx/config.h` 后重新 configure。`VELA_EXTRA_FLAGS` 必须在 source 之后、lunch 之前设置。

### 3.3.3 主线配置构建（cmake，推荐）

```bash
cd /home/aila/projects/vela_contest
export PATH="$(pwd)/prebuilts/gcc/linux-x86_64/arm-none-eabi/bin:$(pwd)/prebuilts/build-tools/linux-x86_64/bin:$PATH"
cmake -B out/sf32lb52_aiagent_ble -S nuttx \
  -DBOARD_CONFIG=../contest2026_181_womenshayebuhuidui/board/contest_board/configs/ai_agent
cmake --build out/sf32lb52_aiagent_ble -j$(nproc)
```

产物：`out/sf32lb52_aiagent_ble/nuttx.bin`（约 4.6MB，含 BLE/TUN/VELA 框架；08-11 真机验证同款构建方式）。

### 3.3.4 一键脚本（位于工作区根，仓库外）

```bash
cd /home/aila/projects/vela_contest
./build_and_flash.sh --port /dev/ttyACM0                                # 构建 + 烧录一体化
./flash_only.sh --port /dev/ttyACM0 -i out/sf32lb52_aiagent_ble/nuttx.bin  # 仅烧录（务必 -i 指定主线镜像）
```

> ⚠️ 脚本与 Android App 工程位于**工作区根目录（参赛仓库之外）**，交付前需随作品一并归档或迁入仓库（见 5.4 检查项 #9）。

## 3.4 烧录

```bash
# 前置：确认串口节点、关闭占用（fuser -v /dev/ttyACM0；关闭 picocom/串口脚本）
sftool -c SF32LB52 -p /dev/ttyACM0 -b 1000000 \
  --before no_reset --connect-attempts 20 --after soft_reset \
  write_flash out/sifli_sf32lb52_devkit_lcd_nsh/nuttx.bin@0x12010000
```

**关键时序**：板子由 RTS 控制电源，需"后台启动烧录线程 + RTS 断电 500ms 再上电"抢占 bootloader 窗口；成功标志 `Connected success!` + `Download stub success!`。失败时**断电重插 USB** 恢复下载模式（[已验证] 芯片需物理重启）。

## 3.5 串口交互（NSH）

```bash
picocom -b 1000000 --noreset --lower-rts --lower-dtr --omap crlf /dev/ttyACM0
```

> ⚠️ `--omap crlf` 不可省略（否则 Enter 无响应）；交互式工具发送命令用 **CRLF 行结束符**（Python 脚本 `ser.write(b'cmd\r\n')`）。

## 3.6 Android App 构建

```bash
cd /home/aila/projects/vela_contest/com.agent.coapp-main   # 工作区根（仓库外，交付时归档，见 5.4 #9）
./gradlew assembleDebug          # 产物 app/build/outputs/apk/debug/
```

技术栈：Kotlin + Jetpack Compose + MVVM + minSdk 26；依赖 Gradle 8.x + AGP。安装后：授权蓝牙权限 → 「联网」页扫描 → 选择 `Agent-Watch` → 启动隧道。

## 3.7 部署验证流程（端到端）

```text
1. lsusb 确认 1a86:55d3 → 2. 烧录 → 3. picocom 见 nsh>
4. ls /dev 确认 lcd0/fb0/input0 → 5. lvgldemo 验证显示链路
6. ai_agent -h / ai_agent -q hello → 7.（BLE 阶段）App 扫描连接 → 设备串口见连接日志
8.（LLM 阶段）set_llm <url> <model> <key> → ask 你好 → 真机首次 LLM 回复
```

完成标准：构建烧录成功、NSH 可达、AI Agent 可执行、屏幕点亮（现场）、重启可复现。

---

# 4. 测试与验收

## 4.1 测试策略

| 层级 | 环境 | 覆盖 |
|---|---|---|
| 单元/组件 | 真机串口 | 设备节点、CLI、LVGL 初始化、littlefs 读写 |
| 集成 | QEMU（aarch64 virt + virtio-net） | 网络、LLM、cron、REST、skill |
| 系统 | 真机 + App | BLE 隧道、端到端 LLM 对话（进行中） |
| 回归 | 真机 | 重启复现、断连重连 |

## 4.2 QEMU 全链路验证结果（[已验证]，2026-08-11）

**环境**：QEMU virt（cortex-a53），`qemu-aiagent` defconfig，virtio-net-pci + slirp user 网络（10.0.2.0/24）。注意：QEMU virt 无 MSI 中断控制器，必须 `CONFIG_DRIVERS_VIRTIO_PCI_POLLING_PERIOD=1000` 轮询模式。

| 用例 | 结果 | 证据要点 |
|---|---|---|
| 网络连通 | ✅ | eth0 10.0.2.15，ping 10.0.2.2 0% 丢包，netmgr CONNECTED |
| LLM 对话 `ask` | ✅ | step-3.7-flash 回复 458 字，TLS 握手 240ms（net_diag 404 属正常） |
| cron 定时触发 | ✅ | 每 20s 精确触发 4 次，消息推送成功；cron.json 持久化 |
| REST API | ✅ | /api/config、/api/skills（增删查）、/api/logs 全通过 |
| skill 在线安装 | ⚠️ [部分验证] | REST POST 通道可用；`install_skill` CLI 有 URL 解析缺陷（host 未拆分） |

**遗留问题**：NSH 单行输入 >80 字符被截断；QEMU 重启后 /data（tmpfs）丢失，真机 littlefs 无此问题。

**复现命令**（完整脚本见 `logs/qemu_net_test.sh` / `logs/qemu_cron_test.sh` / `logs/qemu_rest_test.sh`）：

```bash
# QEMU 关键启动参数：
#   -netdev user,id=net0,hostfwd=tcp::28789-:28789 -device virtio-net-pci,netdev=net0
#   （virt 无 MSI，必须 CONFIG_DRIVERS_VIRTIO_PCI_POLLING_PERIOD=1000 轮询模式）
# vela> CLI 验证命令：
#   set_llm https://api.stepfun.com/v1 step-3.7-flash <key>
#   ask 你好
```

## 4.3 真机验证结果（SF32LB52-DevKit-LCD）

| 模块 | 结果 | 说明 |
|---|---|---|
| USB/串口/NSH | ✅ [已验证] | ttyACM0，`--omap crlf` 正常 |
| 构建/烧录/启动 | ✅ [已验证] | nuttx.bin@0x12010000，nsh> 出现 |
| 设备节点 | ✅ [已验证] | /dev/lcd0、/dev/fb0、/dev/input0 存在 |
| 屏幕点亮 | 🔄 [待验证] | 需现场验收 |
| littlefs 持久化 | ✅ [已验证] | wdog 硬复位后数据保留 |
| BLE 适配器/TUN | ✅ [已验证] | adapter ON（state=4）、bt-net 192.168.55.2、SPP server 就绪（08-11） |
| bluetoothd 保活 | ✅ [已验证] | service_loop_join() 修复静默退出 |
| BLE GATT NUS | 🔄 开发中 | M1.5，验收 = 串口出现 `GATT connected` |
| App ↔ 设备联调 | ⏸️ 阻塞 | 依赖 M1.5 |
| 真机 LLM 对话 | ❌ 未验证 | 依赖 BLE 隧道（M3'） |

## 4.4 验收标准（Acceptance Criteria）

| 编号 | 验收项 | 标准 | 状态 |
|---|---|---|---|
| AC-1 | 构建可复现 | 按 3.3 步骤从零构建成功 | ✅ |
| AC-2 | 烧录可复现 | sftool 写入成功且重启运行 | ✅ |
| AC-3 | AI Agent 运行 | `ai_agent -h`/`-q hello` 正常响应 | ✅ |
| AC-4 | 屏幕点亮 | 现场非全黑、内容正确、重启可复现 | 🔄 |
| AC-5 | BLE 广播可见 | 手机 BLE 扫描到 `Agent-Watch` | 🔄 |
| AC-6 | GATT 连接 | 设备串口出现 `GATT connected` | 🔄 |
| AC-7 | 真机 LLM 对话 | `ask 你好` 收到 LLM 回复 | ❌ |
| AC-8 | 稳定性 | 多轮对话 + 断连重连无 hardfault | ❌ |
| AC-9 | 持久化 | 重启后配置/技能保留 | ✅ |
| AC-10 | 文档完整 | 本手册 + 开发记录 + AI 日志齐备 | 🔄 |

## 4.5 已知问题与风险清单

| # | 风险/问题 | 等级 | 状态 | 缓解措施 |
|---|---|---|---|---|
| R1 | 布局敏感堆损坏（bluetoothd hardfault） | 🔴 高 | 已定位未修复 | TASK-4：移除探针 + 排查越界写源头；DEBUG_MM 构建 + libuv 诊断日志 |
| R2 | LCPU 固件不支持 BREDR | 🟡 中 | 已确证 | BLE GATT 路线绕过；必要时向芯片厂商提交缺陷报告 |
| R3 | ext adv 空口无信号（固件缺陷） | 🟡 中 | 已确证 | 待测 legacy 广播（0x200A）；手机 BLE 扫描验证 |
| R4 | BLE 吞吐低（10-30KB/s） | 🟡 中 | 预期值 | MTU 协商 247，LLM 文本流够用 |
| R5 | 屏幕背光/CO5300 时序风险 | 🟡 中 | 待验证 | 现场优先验收（T-01，0.5 天） |
| R6 | zblue async pipe fd 失效（EBADF） | 🟡 中 | 已绕过 | uv__async_send 不再断言 |
| R7 | 烧录偶发失败 | 🟢 低 | 已绕过 | 断电重插 + RTS 时序 |
| R8 | 文档欠账：app/ai_agent/README.md 仍宣传未实现的 ASR/TTS | 🟢 低 | 待修 | 5.4 检查项 #8 覆盖 |

## 4.6 故障与解决历史（摘要）

15 项故障，12 项已解决、3 项进行中。关键条目：

| # | 故障 | 根因 | 方案 |
|---|---|---|---|
| 7 | NSH 回车无响应 | 行结束符 | picocom `--omap crlf` |
| 10 | NOR 写校验失败 | DMA 页写失效 + QE 位未设 | 单线写页 `sf32lb_flash_write_page_single()` |
| 12 | bluetoothd 静默退出 | 事件循环线程退出即进程死 | `CONFIG_NET_LOCAL` + `/var/run` mkdir + `service_loop_join()` |
| 13 | SPP register failed | 蓝牙栈未 enable | `bt_adapter_enable` 等待 ON |
| 14 | BT adapter timeout | H4 控制器未注册 | `CONFIG_UART_BTH4=y` |
| 15 | 布局敏感堆损坏 | inode 链表越界写 | 探针绕过（修复待执行） |

完整表见 [PROJECT_STATUS_REPORT.md](development_records/PROJECT_STATUS_REPORT.md) 附录十。

---

# 5. 项目管理与交付物清单

## 5.1 里程碑与进度

| 里程碑 | 内容 | 日期 | 状态 |
|---|---|---|---|
| M0 | 环境 + 工具链 | 08-08 | ✅ |
| M1 | 固件构建 + 烧录 + NSH | 08-09 | ✅ |
| M2 | AI Agent 基础 + LVGL 集成 | 08-09 | ✅ |
| M3 | QEMU 全链路（LLM/cron/REST） | 08-11 | ✅ |
| M4 | BLE SPP+TUN 设备端（后否决） | 08-11 | ✅ |
| M5 | littlefs 持久化 | 08-11 | ✅ |
| M6 | 蓝牙深度诊断 + 根因定位 | 08-12 | ✅ |
| M1.5 | BLE GATT NUS 通道 | 计划 2-3 天 | 🔄 |
| M2' | App 桥接改造 | 08-12 代码完成 | 🔄 |
| M3' | 真机端到端 LLM 对话 | 依赖前序 | ❌ |

**主线任务**：T-01 屏幕验收 → T-04 移除探针 → T-02 GATT NUS → T-03 App 联调 → T-07 真机 LLM（编号以 [TASK_STATUS.md](../TASK_STATUS.md) 为准）。

## 5.2 目录结构与代码导航

```
contest2026_181_womenshayebuhuidui/          # 参赛交付仓库
├── app/ai_agent/            # 自研 AI Agent 示例应用（379 行，显示验证用）
├── app/hello_app/           # 示例应用（模板）
├── quickapp/hello_quickapp/ # 快应用示例（未使用）
├── board/contest_board/     # 板级配置（configs/{ai_agent,spp_verify,nsh}）
├── docs/
│   ├── setup_guide.md       # 环境与构建指南
│   ├── DELIVERY_MANUAL.md   # 本手册
│   └── development_records/ # 11 份开发记录 + 全貌报告
├── logs/Sen70s/             # AI Coding 日志（6 会话，JSONL）
└── TASK_STATUS.md           # 任务进度速查

/home/aila/projects/vela_contest/            # 工作区根（仓库外，交付时归档）
├── build_and_flash.sh       # 一键构建烧录
├── flash_only.sh            # 仅烧录
├── nsh_console.sh/.py       # 串口终端
└── com.agent.coapp-main/    # Android 配套 App（独立工程）
    └── app/src/main/java/com/agent/coapp/
        ├── ble/  vpn/  network/  data/  ui/  viewmodel/  repository/
```

## 5.3 AI Coding 日志

- 位置：`logs/Sen70s/`，manifest.json 健康状态全部 `ok`；
- 规模：6 会话（08-08×3、08-09×1、08-10×1、08-12×1），共 549 事件；
- 归集方式：AI 工具自动记录 → 主动导出到仓内 `logs/` → 随 PR 提交；
- 注意：`logs/` 最终导出日志必须提交，不得 gitignore。

## 5.4 交付物检查清单（提交前逐项核验）

| # | 检查项 | 完成 |
|---|---|---|
| 1 | README.md 替换为作品说明（简介/选题/目录/运行/AI 说明） | 🔄 |
| 2 | 固件 defconfig 与代码一致，主线（ai_agent 配置）构建通过 | ✅ |
| 3 | 屏幕现场验收证据（照片/串口输出） | 🔄 |
| 4 | AI Coding 日志导出至 logs/ 并提交 | ✅（滚动更新） |
| 5 | 本手册与开发记录同步更新 | 🔄 |
| 6 | 敏感信息清理（API key 不入库，.gitignore 覆盖） | ✅ |
| 7 | 废弃代码标记/归档（SPP 相关 ~2900 行） | 🔄 |
| 8 | 更新 app/ai_agent/README.md，删除 ASR/TTS 等未实现功能描述 | 🔄 |
| 9 | Android App 源码（com.agent.coapp-main）归档入库或随作品提交 | 🔄 |
| 10 | 提交 PR + CLA 签署 + 自评审核 | 🔄 |

## 5.5 后续计划与运维

| 阶段 | 内容 | 时间 |
|---|---|---|
| 本周 | T-01 屏幕验收、T-04 移除探针、T-05 BREDR 实验 | 0.5-1 天 |
| 1-2 周 | T-02 GATT NUS（2-3 天）、T-06 legacy 广播、T-03 App 联调 | 滚动 |
| 2-3 周 | T-07 真机 LLM、T-08 文档、T-09 冗余清理 | 滚动 |
| 决策点 | T-10 PAN vs SLIP vs 现状（1 周内定） | - |

> **交付截止**：2026-09-20（比赛收卷）。按 5.4 检查清单倒排，最后 1 周仅做文档与验收收尾。

**交付节奏**：每个里程碑完成后更新本手册（版本 +1）、TASK_STATUS.md、开发记录，并执行一次文档审校。

---

# 6. 附录

## 6.1 命令速查表

| 场景 | 命令 |
|---|---|
| 环境 | `export PATH=...prebuilts/gcc/...:...kconfig-frontends/bin:$PATH` |
| 构建 | `source build/envsetup.sh && export VELA_EXTRA_FLAGS=... && lunch <config> && m` |
| 烧录 | `sftool -c SF32LB52 -p /dev/ttyACM0 -b 1000000 --before no_reset --after soft_reset write_flash nuttx.bin@0x12010000` |
| 串口 | `picocom -b 1000000 --noreset --lower-rts --lower-dtr --omap crlf /dev/ttyACM0` |
| 主线构建 | `cmake -B out/sf32lb52_aiagent_ble -S nuttx -DBOARD_CONFIG=.../configs/ai_agent && cmake --build out/sf32lb52_aiagent_ble` |
| 设备端 | `ai_agent -h` / `ai_agent -q hello` / `lvgldemo`（自研示例）；`bluetoothd &`（BLE 服务） |
| QEMU 验证 | `-netdev user,hostfwd=tcp::28789-:28789 -device virtio-net-pci`；`set_llm ...` / `ask 你好`（脚本 logs/qemu_*_test.sh） |
| BLE 联调 | `set_llm https://api.stepfun.com/v1 step-3.7-flash <key>` / `ask 你好` / `net_status` |
| App | `cd /home/aila/projects/vela_contest/com.agent.coapp-main && ./gradlew assembleDebug` |

## 6.2 参考文献索引

| 文档 | 内容 |
|---|---|
| [00_quick_start_device_to_app.md](development_records/00_quick_start_device_to_app.md) | 从连接到运行的快速流程 |
| [03_build_flash_nuttx.md](development_records/03_build_flash_nuttx.md) | 构建/烧录/启动 |
| [04_ai_agent_lvgl_display.md](development_records/04_ai_agent_lvgl_display.md) | AI Agent + LVGL 显示验收 |
| [05_troubleshooting.md](development_records/05_troubleshooting.md) | 故障排查索引 |
| [07_qemu_aiagent_llm_validation.md](development_records/07_qemu_aiagent_llm_validation.md) | QEMU 全链路验证 |
| [08_ble_spp_tun_real_device.md](development_records/08_ble_spp_tun_real_device.md) | 真机 BLE SPP+TUN 验证 |
| [09_app_spp_tun_integration.md](development_records/09_app_spp_tun_integration.md) | App 联调规划与 GATT 转向 |
| [10_pan_route_reanalysis.md](development_records/10_pan_route_reanalysis.md) | PAN 路线分析（BREDR 纠正） |
| [11_bt_full_diagnosis.md](development_records/11_bt_full_diagnosis.md) | 蓝牙深度诊断 |
| [PROJECT_STATUS_REPORT.md](development_records/PROJECT_STATUS_REPORT.md) | 项目全貌报告 |
| [TASK_STATUS.md](../TASK_STATUS.md) | 任务进度速查 |

---

**手册维护说明**：本手册由 Team 181 维护，采用多智能体协作方式编写（章节分工 + CodeReview 子代理审校）。任何状态变更（✅/🔄/❌）需附带证据并同步更新对应开发记录。

**最后更新**：2026-08-12

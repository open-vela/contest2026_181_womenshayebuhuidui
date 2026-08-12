# 启动指南 — 连接设备进入桌面

> **适用范围**：SF32LB52-DevKit-LCD（AI 智能体桌宠）
> **主线配置**：`vendor/sifli/boards/sf32lb52/sf32lb52_devkit_lcd/configs/nsh`
> **最后更新**：2026-08-12

---

## 1. 启动流程总览

```
USB 连接（设备上电）
  → NuttX 启动（启动日志 + NuttShell nsh>）
  → ai_agent            （AI 主程序，NSH 命令）
      └─ display_init()           ← 初始化 LVGL
          └─ launcher_create()     ← ★ 创建应用桌面
              ├── ☁️ 桌宠图标  → pet_page_create()
              │   └─ 云朵 + 气泡 + 动画
              ├── ⚙️ 设置图标  → settings_page_create()
              │   └─ LLM 配置展示
              └── ℹ️ 关于图标  → （待实现）
```

**桌面页组成**（`app/ai_agent/ui/`）：

| 元素 | 实现文件 | 说明 |
|------|---------|------|
| 应用桌面 | `launcher_page.c` | 图标网格 + 标题 + 底部版权 |
| 桌宠页面 | `pet_page.c` | 云朵角色 + 气泡对话 + 浮动/眨眼动画 |
| 设置页面 | `settings_page.c` | LLM 配置展示（Host/Model/Key） |
| 返回按钮 | 各页面独立实现 | ← 返回桌面 |
| 显示驱动 | `lv_nuttx_init()` | framebuffer `/dev/lcd0` + 触摸 `/dev/input0`（CO5300 屏）|

---

## 2. 连接与启动步骤

### 2.1 硬件连接

1. USB 线连接开发板（Type-C 调试口，CH34x 桥接）
2. 确认串口节点：`lsusb` 应看到 `1a86:55d3`（QinHeng）

### 2.2 打开串口终端

```bash
picocom -b 1000000 --noreset --lower-rts --lower-dtr --omap crlf /dev/ttyACM0
```

> ⚠️ `--omap crlf` 不可省略；NSH 需要 CRLF 行结束符。

### 2.3 启动 AI 主程序（进入桌面）

```text
nsh> ai_agent
```

**启动成功标志**（串口日志）：

```
========================================
  AI Agent v3.0 - SF32LB52-DevKit-LCD
  Contest 2026 Team 181
  With LVGL Desktop + Pet Display
========================================

Initializing LVGL display...
[Launcher] Creating desktop page...
[Launcher] Desktop created successfully
Display initialized successfully! Desktop is ready.
```

**同时屏幕显示**：
- ✅ **深蓝色桌面背景** (`0x1a1a2e`)
- ✅ **三个金色图标**：
  - ☁️ 桌宠（左侧）
  - ⚙️ 设置（中间）
  - ℹ️ 关于（右侧）
- ✅ **顶部标题**：`🖥️  AI Agent 桌面`
- ✅ **底部版权**：`Team 181 - Contest 2026`

### 2.4 验证桌面交互

| 操作 | 预期效果 |
|------|---------|
| **点击 ☁️ 桌宠图标** | 进入桌宠页 → 云朵居中 + 浮动动画 + 眨眼 + 气泡弹出 |
| **再次点击云朵** | 气泡切换显示/隐藏 |
| **点击 ← 返回按钮** | 返回桌面页 |
| **点击 ⚙️ 设置图标** | 显示 LLM 配置（Host/Model/Key） |
| **点击 ℹ️ 关于图标** | 提示"About page not implemented yet"（待实现）|
| **命令行 `ai_agent -q hello`** | 进入桌宠页 + 显示 AI 响应气泡 |

---

## 3. 一键启动脚本（可选）

自动完成"启动 ai_agent → 状态检查"：

```bash
cd /home/aila/projects/vela_contest

# 启动 AI Agent 并保持系统运行
python3 logs/hw_ble_test.py
```

脚本输出：启动日志落盘 `logs/hw_ble_test.log`，验证结束系统保持运行（桌面常驻）。

---

## 4. 启动方式对比

| 方式 | 命令 | 说明 |
|------|------|------|
| **手动启动（推荐调试）** | `nsh> ai_agent` | 可控，便于观察日志 |
| **自动启动（演示用）** | NSH 启动脚本 `/etc/rcS` | 上电自动进入桌面 |
| **后台启动** | `nsh> ai_agent &` | 不推荐（LVGL 需要前台）|

### 4.1 开机自动启动（可选）

当前 `ai_agent` 为 **NSH 手动命令启动**（调试可控）。若需上电自动进入桌面，二选一：

- **方案 A**：NSH 启动脚本（`/etc/rcS` 或 `CONFIG_NSH_ARCHINIT`）追加：
  ```text
  ai_agent
  ```

- **方案 B**：board 初始化代码 `board_app_initialize()` 直接 `nxtask_create("ai_agent", ...)`。

> 建议验收阶段保持手动启动（便于观察每步日志）；演示/交付阶段启用自动启动。

---

## 5. 常见问题

| 现象 | 处理 |
|------|------|
| 串口无输出（设备挂死） | **断电重插** USB（等 20-30 秒）恢复（芯片需物理重启，RTS 无法复位）|
| 烧录失败 `Timeout("waiting for shell prompt")` | boot ROM 窗口 ~10-20 秒：断电重插后立即烧录；失败则 `sleep 5-8` 重试 |
| 屏幕不亮/全黑 | 检查 `/dev/lcd0`、`/dev/fb0` 节点存在；现场验收背光（CO5300 时序）|
| `ai_agent: command not found` | 确认固件包含 `CONFIG_EXAMPLES_AI_AGENT=y`；重新构建烧录 |
| 桌面显示异常 | 确认 `display_init()` 成功（日志有 `Desktop created successfully`）|
| 点击图标无响应 | 检查 LVGL 渲染循环（`lv_timer_handler()` 是否被调用）|

---

## 6. 页面架构说明

### 6.1 页面层级

```
应用启动
  ├─ [桌面] launcher_page（顶层，默认显示）
  │   ├─ [桌宠] pet_page（点击 ☁️ 进入）
  │   │   └─ ← 返回按钮 → 回桌面
  │   ├─ [设置] settings_page（点击 ⚙️ 进入）
  │   │   └─ ← 返回按钮 → 回桌面
  │   └─ [关于] about_page（待实现）
  │       └─ ← 返回按钮 → 回桌面
  │
  └─ [命令行] NSH 交互层
      ├─ `ai_agent -q hello`  → 自动进入桌宠页显示响应
      ├─ `ai_agent`          → 进入交互模式
      └─ `ai_agent -h`       → 显示帮助
```

### 6.2 页面切换机制

- **进入页面**：`launcher_enter_page(PAGE_X)` → 创建目标页面 → `lv_obj_move_foreground()`
- **返回桌面**：`launcher_back_to_desktop()` → 删除当前页面 → 桌面置前
- **页面前景**：通过 `lv_obj_move_foreground()` 管理，旧页面仍在背景中存在

### 6.3 空闲动画

| 动画 | 效果 | 周期 |
|------|------|------|
| **Y 轴浮动** | `±6px` 垂直浮动 | `1500ms`（循环）|
| **眨眼** | 显示/隐藏云朵（模拟眨眼）| `2800ms` 间隔 + `100ms` 眨眼 |

---

## 7. 快速验证清单

**首次启动验收**（所有 ✅ 才算通过）：

- [ ] `lsusb` 能看到 `1a86:55d3`
- [ ] `/dev/ttyACM0` 存在
- [ ] `picocom` 连接成功，看到 `nsh>`
- [ ] `ai_agent` 启动成功
- [ ] **屏幕显示深蓝色桌面**（非全黑）
- [ ] **看到三个金色图标**（☁️/⚙️/ℹ️）
- [ ] **点击 ☁️ → 进入桌宠页**（云朵 + 气泡）
- [ ] **点击 ← → 返回桌面**
- [ ] **点击 ⚙️ → 显示 LLM 配置信息**
- [ ] 命令行 `ai_agent -q hello` → 进入桌宠页 + 显示响应

**当前记录**：构建已确认；屏幕实际点亮**[待验证]**。

---

## 8. 关联文档

| 文档 | 内容 |
|------|------|
| [`00_quick_start_device_to_app.md`](development_records/00_quick_start_device_to_app.md) | 连接→烧录→NSH 快速流程 |
| [`12_launcher_desktop_implementation.md`](development_records/12_launcher_desktop_implementation.md) | 桌面实施方案（设计 + 构建 + 验证）|
| [`LAUNCHER_QUICK_REF.md`](LAUNCHER_QUICK_REF.md) | 桌面快速参考卡 |
| [`04_ai_agent_lvgl_display.md`](development_records/04_ai_agent_lvgl_display.md) | AI Agent + LVGL 显示验收 |
| [`05_troubleshooting.md`](development_records/05_troubleshooting.md) | 故障排查索引 |
| [`setup_guide.md`](../setup_guide.md) | 环境配置与构建指南 |

# LVGL 应用桌面实施方案

> **方案**: A - LVGL 应用桌面
> **日期**: 2026-08-12
> **状态**: ✅ 代码完成，待构建验证

---

## 📋 实施概述

实现了完整的 LVGL 应用桌面系统，提供图标点击进入不同应用页面的交互体验。

### 核心特性

- ✅ **应用桌面页**：三个图标（桌宠/设置/关于）
- ✅ **桌宠页面**：云朵角色 + 对话气泡 + 空闲动画
- ✅ **设置页面**：LLM 配置展示（只读）
- ✅ **页面切换**：点击图标进入，返回键回桌面
- ✅ **空闲动画**：浮动 + 眨眼

---

## 📁 文件结构

```
app/ai_agent/
├── ai_agent_main.c              (已修改)
├── CMakeLists.txt               (已更新)
├── ui/
│   ├── ui.h/ui.c                (新增) UI 统一入口
│   ├── launcher_page.h/c        (新增) 桌面页（图标网格）
│   ├── pet_page.h/c             (新增) 桌宠页（对话界面）
│   └── settings_page.h/c        (新增) 设置页（LLM 配置）
└── ...
```

**新增代码**：~900 行（6 个文件）
**修改代码**：`ai_agent_main.c`（~30% 修改）

---

## 🎯 页面架构

```
开机 → LVGL 初始化 → 桌面页（launcher_page）
├── ☁️ 桌宠图标  → 点击进入 → 桌宠页（pet_page）
│   └── 显示：云朵 + 气泡 + 状态
│
├── ⚙️ 设置图标  → 点击进入 → 设置页（settings_page）
│   └── 显示：LLM Host/Model/Key
│
└── ℹ️ 关于图标  → 点击进入 → 关于页（待实现）
    └── 显示：版本/队伍信息

返回键 → 返回桌面
```

---

## 🔧 关键实现

### 1. 桌面页（launcher_page）

- **图标网格**：3 个图标，水平居中布局
- **图标按钮**：`lv_btn_create()` + 点击事件
- **视觉设计**：
  - 背景：深蓝色 `0x1a1a2e`
  - 图标：金色 `0xffd700` emoji
  - 按钮：圆角 + 边框

### 2. 页面前景管理

```c
// 进入页面时
lv_obj_move_foreground(target_page);

// 返回桌面时
lv_obj_move_foreground(desktop_page);
lv_obj_del(current_page);
```

### 3. 返回按钮 + 回调

```c
// 每个子页面有独立的"← 返回"按钮
// 点击触发回调：launcher_back_to_desktop()
```

### 4. 空闲动画（pet_page）

- **Y 轴浮动**：`±6px`，`1500ms`，循环
- **眨眼动画**：`100ms` 眨眼，`2800ms` 间隔，循环

### 5. 气泡对话（pet_page）

- **初始隐藏**：`LV_OBJ_FLAG_HIDDEN`
- **点击显示**：清除 hidden flag
- **再次点击**：隐藏 bubble toggle

---

## 🔨 构建与验证

### 步骤 1：构建

```bash
cd /home/aila/projects/vela_contest

# 设置 PATH
export PATH="/home/aila/projects/vela_contest/prebuilts/gcc/linux-x86_64/arm-none-eabi/bin:/home/aila/projects/vela_contest/prebuilts/kconfig-frontends/bin:$PATH"

# 加载环境
source build/envsetup.sh

# 设置编译选项
export VELA_EXTRA_FLAGS="-Wno-cpp -Wno-deprecated-declarations -Wno-error -Wno-strict-prototypes -Wno-undef"

# 配置
lunch vendor/sifli/boards/sf32lb52/sf32lb52_devkit_lcd/configs/nsh

# 编译
m
```

**成功标志**：
```
[ai_agent] Linking...
build successful
```

### 步骤 2：烧录

```bash
cd /home/aila/projects/vela_contest/contest2026_181_womenshayebuhuidui

# 一键构建 + 烧录
./build_and_flash.sh --port /dev/ttyACM0
```

**成功标志**：
```
Connected success!
Download stub success!
烧录完成，板子已重启运行
```

### 步骤 3：串口验证

```bash
# 连接 picocom（必须带 --omap crlf）
picocom -b 1000000 --noreset --lower-rts --lower-dtr --omap crlf /dev/ttyACM0
```

### 步骤 4：功能测试

```bash
# 启动 AI Agent
nsh> ai_agent

# 预期看到：
# 1. 深蓝色桌面页
# 2. 三个图标：☁️ 桌宠 / ⚙️ 设置 / ℹ️ 关于
# 3. 底部：Team 181 - Contest 2026

# 点击 ☁️ 桌宠图标
# 预期：
# - 进入桌宠页（深蓝色背景）
# - 云朵 emoji 居中（+ 浮动 + 眨眼动画）
# - 显示气泡："你好呀，我是小云 ☁️"
# - 状态："点击云朵开始对话"

# 点击 ← 返回按钮
# 预期：回到底色桌面

# 点击 ⚙️ 设置图标
# 预期：显示 LLM 配置（服务地址/模型/API Key）

# 命令行查询
ai_agent -q hello
# 预期：自动进入桌宠页 + 显示响应气泡
```

---

## 🎨 视觉设计参考

### 配色方案

| 元素 | 颜色 | 色值 |
|------|------|------|
| 桌面背景 | 深蓝 | `0x1a1a2e` |
| 按钮背景 | 蓝灰 | `0x2a2a4a` |
| 按钮边框 | 灰蓝 | `0x4a4a6a` |
| 图标/标题 | 金色 | `0xffd700` |
| 副标题 | 淡蓝 | `0x88ccff` |
| 文字 | 白色 | `0xffffff` |
| 灰色说明 | 灰色 | `0xaaaaaa` |

### 布局规格

| 页面 | 尺寸 | 图标位置 |
|------|------|---------|
| 桌面页 | 390×450 | 图标起始 `(60, 60)` |
| 桌宠页 | 390×450 | 云朵 `(135, 60)` |
| 设置页 | 390×450 | 配置项起始 `(20, 60)` |

---

## 📊 性能评估

| 指标 | 估算 | 说明 |
|------|------|------|
| **代码量** | 900 行 | 6 个文件 |
| **SRAM 开销** | +15KB | LVGL 页面对象（桌面+页面对齐） |
| **Flash 开销** | +20KB | 新增代码段 |
| **渲染延迟** | <16ms | LVGL 标准帧率 (60fps) |

**结论**：开销可控，板子 512KB SRAM 足以支持。

---

## 🔄 后续扩展建议

### 优先级 P0（立即）

- [ ] **验证构建与显示**：构建烧录 → 串口验证 → 确认桌面显示
- [ ] **优化图标图像**：将 emoji 替换为实际 `lv_img` 位图（需要 C 数组资源）
- [ ] **关于页面实现**：简单的版本/队伍信息展示

### 优先级 P1（近期）

- [ ] **设置页可编辑**：实现 NSH 命令 → 设置页实时更新
- [ ] **返回手势支持**：监听 `/dev/input0` 返回键事件
- [ ] **页面前进栈**：维护页面历史栈（实现多级页面返回）

### 优先级 P2（可选）

- [ ] **页面过渡动画**：`lv_anim_timeline` 实现滑动淡入淡出
- [ ] **桌面图标自定义**：允许拖动/排列图标
- [ ] **多桌面页**：左右滑动切换桌面

---

## ⚠️ 已知限制

1. **图标为 emoji 占位**：实际应替换为 `lv_img` + C 数组位图
2. **关于页未实现**：返回"not implemented"提示
3. **设置页只读**：LLM 配置需在 NSH 中修改，设置页未实时同步
4. **无页面过渡动画**：切换为即时跳转
5. **返回按钮仅子页面**：桌面页无返回按钮（应该是顶层）

---

## 📚 参考文档

- `docs/development_records/06_pet_ui_design.md` - 桌宠 UI 设计文档
- `docs/development_records/PROJECT_STATUS_REPORT.md` - 项目全貌分析
- `TASK_STATUS.md` - 任务与进度速查

---

**代码完成时间**：2026-08-12
**下一步**：构建烧录验证
**预计验证时间**：0.5-1 天（构建 10-15 分钟 + 现场验证 30 分钟）

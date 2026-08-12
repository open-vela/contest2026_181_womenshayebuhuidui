# 项目全貌分析报告

> **项目**: contest2026_181_womenshayebuhuidui
> **比赛**: 2026 首届 openvela AI 硬件开发者大赛
> **队伍**: 编号 181 | GitHub: Sen70s
> **目标板**: SF32LB52-DevKit-LCD (SF32LB52-MOD-1 N16R8)
> **分析日期**: 2026-08-12
> **文档来源**: README.md, setup_guide.md, docs/development_records/*, logs/Sen70s/manifest.json

---

## 一、项目基本信息

### 1.1 项目定位

这是 **openvela AI Coding Contest 2026** 的团队参赛仓库，基于 **NuttX RTOS** + **SF32LB52** 嵌入式硬件平台开发 AI 智能体应用。

**核心特征**：
- 🎯 **三赛道可选**：快应用/手表应用创新、AI 硬件产品创新、新硬件适配
- 📱 **目标设备**：SF32LB52-DevKit-LCD（390×450 CO5300 LCD + FT6146 触控 + 双模蓝牙 5.3）
- 🤖 **核心功能**：AI Agent + LVGL 显示 + BLE 联网 + LLM 对话 + 语音交互
- 🔧 **开发模式**：本地 AI 辅助开发（Claude Code），AI 日志自动归集

### 1.2 目录结构

```
contest2026_181_womenshayebuhuidui/
├── app/
│   ├── ai_agent/          # AI 智能体主应用（核心作品）
│   └── hello_app/         # 示例应用
├── quickapp/
│   └── hello_quickapp/    # 快应用示例
├── board/
│   └── contest_board/     # 板级适配（占位骨架）
├── docs/
│   └── development_records/  # 完整开发记录（11 份）
├── logs/
│   └── Sen70s/            # AI Coding 日志（5 个会话）
├── build_and_flash.sh     # 一键构建烧录脚本
├── README.md              # 参赛仓库说明
└── contest2026_181_womenshayebuhuidui.xml  # repo manifest
```

**openvela 工作区映射**：
| 团队路径 | openvela 映射路径 |
|---------|-----------------|
| `app/ai_agent` | `packages/demos/contest2026_181_ai_agent` |
| `app/hello_app` | `packages/demos/contest2026_181_hello_app` |
| `quickapp/hello_quickapp` | `packages/apps/contest2026_181_hello_quickapp` |
| `board/contest_board` | `vendor/openvela/boards/contest2026_181_board` |

---

## 二、技术架构

### 2.1 硬件平台

| 组件 | 规格 |
|------|------|
| **MCU** | SF32LB52 (ARM Cortex-M4F, 120MHz) |
| **内存** | 16MB Flash + 8MB PSRAM |
| **屏幕** | CO5300, 390×450, 16bpp (RGB565) |
| **触控** | FT6146 电容触摸（/dev/input0） |
| **蓝牙** | BLE 5.3 + BREDR（LCPU 固件限制） |
| **串口** | USB CDC ACM (QinHeng 1a86:55d3, /dev/ttyACM0) |
| **烧录地址** | 0x12010000 (XIP) |

### 2.2 软件栈

```
┌──────────────────────────────────────────────────────┐
│         AI Agent 应用层 (ai_agent_main.c)             │
│  - LVGL UI (宠物桌宠 + 对话气泡)                      │
│  - 语音交互 (VAD → ASR → LLM → TTS)                  │
│  - BLE 联网 (GATT NUS / SPP+TUN 代理)                 │
├──────────────────────────────────────────────────────┤
│         框架层 (openvela)                            │
│  - LVGL 9.1.0 (图形界面)                              │
│  - bluetoothd + zblue 栈 (蓝牙)                       │
│  - netmgr + TUN (网络代理)                            │
│  - littlefs (持久化存储)                               │
├──────────────────────────────────────────────────────┤
│         NuttX RTOS                                   │
│  - NSH Shell (命令行交互)                             │
│  - 文件系统 / 设备驱动 / 网络栈                        │
└──────────────────────────────────────────────────────┘
```

### 2.3 三条技术路线

| 路线 | 状态 | 说明 |
|------|------|------|
| **A. QEMU 预演** | ✅ 完成 | qemu-aiagent 配置，LLM/cron/REST 全链路验证 |
| **B. BLE GATT NUS** | 🔄 进行中 | 真机 BLE 通道（替代 SPP，因 LCPU 固件不支持 BREDR）|
| **C. PAN/串口 SLIP** | ⏸️ 待定 | 兜底方案（自实现 BNEP 或 USB-TTL SLIP）|

---

## 三、已完成任务清单

### ✅ 阶段 1：环境搭建与基础工具（2026-08-08）

| 任务 | 状态 | 证据 |
|------|------|------|
| ARM 交叉编译器配置 | [已验证] | PATH 包含 `prebuilts/gcc/arm-none-eabi/bin` |
| Kconfig 工具链 | [已验证] | PATH 包含 `prebuilts/kconfig-frontends/bin` |
| USB 串口连接 | [已验证] | `/dev/ttyACM0`, 1a86:55d3 |
| picocom 终端配置 | [已验证] | `--omap crlf` 解决回车问题 |

**参考文档**：`docs/setup_guide.md`, `02_device_usb_serial.md`

---

### ✅ 阶段 2：固件构建与烧录（2026-08-08 ~ 2026-08-09）

| 任务 | 状态 | 证据 |
|------|------|------|
| defconfig 配置 | [已验证] | `CONFIG_EXAMPLES_AI_AGENT=y`, `CONFIG_GRAPHICS_LVGL=y` |
| 完整编译流程 | [已验证] | `nuttx.bin` 生成在 `out/sifli_sf32lb52_devkit_lcd_nsh/` |
| `build_and_flash.sh` | [已验证] | 自动构建 + 烧录一体化脚本 |
| 固件烧录 (0x12010000) | [已验证] | `Connected success!`, `Download stub success!` |
| NuttX 启动 | [已验证] | NSH 提示符 `nsh>` 出现 |
| `ai_agent`/`lvgldemo` 编入 | [已验证] | `builtin_list.h` 和 `nuttx.map` 确认 |

**参考文档**：`03_build_flash_nuttx.md`

---

### ✅ 阶段 3：基础显示验证（2026-08-09）

| 任务 | 状态 | 证据 |
|------|------|------|
| 设备节点确认 | [已验证] | `/dev/lcd0`, `/dev/fb0`, `/dev/input0` 存在 |
| LVGL 初始化 | [已验证] | `lv_nuttx_init()` 调用 `/dev/lcd0` + `/dev/input0` |
| `lvgldemo` 运行 | [待验证] | 命令存在，屏幕点亮待现场确认 |
| 屏幕硬件状态 | [已验证] | `fb0` 390×450 16bpp 就绪 |

**参考文档**：`04_ai_agent_lvgl_display.md`

---

### ✅ 阶段 4：AI Agent 基础功能（2026-08-09）

| 任务 | 状态 | 证据 |
|------|------|------|
| 命令行入口 | [已验证] | `ai_agent -h`, `ai_agent -q hello`, `ai_agent` 交互模式 |
| 本地关键词响应 | [已验证] | `g_responses[]` 静态表（hello/nuttx/vela/sifli/lcd/help）|
| LVGL UI 显示 | [已验证] | 代码中存在黑色背景 + 标题 + 状态 + 响应文本 |
| 控制台逻辑 | [已验证] | `-n` 模式可跳过 LVGL 验证命令行 |

**参考文档**：`04_ai_agent_lvgl_display.md`

---

### ✅ 阶段 5：QEMU 全链路验证（2026-08-11）

| 任务 | 状态 | 证据 |
|------|------|------|
| QEMU 构建配置 | [已验证] | `qemu-aiagent` defconfig，virtio-net-pci |
| 网络连通性 | [已验证] | IP `10.0.2.15`, ping 通 `10.0.2.2` |
| LLM 对话 (ask) | [已验证] | 阶跃星辰 step-3.7-flash，文本回复正常 |
| cron 定时任务 | [已验证] | 每 20 秒精确触发 4 次，消息推送成功 |
| REST API (coapp) | [已验证] | `/api/config`, `/api/skills`, `/api/logs` 全通过 |
| skill 在线安装 | [部分验证] | REST API 上传成功，`install_skill` CLI 有 URL 解析缺陷 |

**日志**：`logs/qemu_net_test.sh`, `logs/qemu_cron_test.sh`, `logs/qemu_rest_test.sh`

**参考文档**：`07_qemu_aiagent_llm_validation.md`

---

### ✅ 阶段 6：真机 BLE SPP+TUN 验证（2026-08-11）

| 任务 | 状态 | 证据 |
|------|------|------|
| 自定义构建配置 | [已验证] | `sf32lb52_aiagent_ble` outdir, cmake + Unix Makefiles |
| TUN 设备创建 | [已验证] | `bt-net` 192.168.55.2/24, MTU 1518 |
| 蓝牙适配器启用 | [已验证] | `BT adapter ON`, state=4 |
| SPP server 启动 | [已验证] | `SPP server started on SCN 3` |
| BLE 网络通道就绪 | [已验证] | `BLE SPP+TUN proxy channel started` |
| bluetoothd 守护进程 | [已验证] | `service_loop` + `service_loop_join` 保活 |

**⚠️ 重大发现**：**SPP 方案被否决**——LCPU 固件不支持 BREDR（Write_Scan_Enable 零响应），转向 BLE GATT NUS。

**六层根因排障**：
1. NSH 行结束符问题（\r → \r\n）
2. `CONFIG_NET_LOCAL` 缺失（socket PF_LOCAL 失败）
3. `/var/run` 目录不存在（mkdir 不递归）
4. `service_loop_join()` 缺失（事件循环线程退出即进程死）
5. `bt_adapter_enable` 未调用（SPP 需要 BREDR adapter ON）
6. `CONFIG_UART_BTH4` 未开（H4 控制器未注册）

**参考文档**：`08_ble_spp_tun_real_device.md`

---

### ✅ 阶段 7：NOR Flash 持久化（2026-08-10 ~ 2026-08-11）

| 任务 | 状态 | 证据 |
|------|------|------|
| littlefs 挂载 | [已验证] | `/data type littlefs`, df 显示 4M |
| 文件持久化 | [已验证] | wdog 硬复位后文件保留 |
| 驱动缺陷修复 | [已验证] | `sf32lb_flash_write_page_single()` 单线写页修复 QMODE bug |

**根因**：DMA 页写路径失效 + QMODE 下默认 quad 命令但芯片 QE 位未设置

**参考文档**：`05_troubleshooting.md` (故障 #13)

---

### ✅ 阶段 8：蓝牙深度诊断（2026-08-12）

| 任务 | 状态 | 证据 |
|------|------|------|
| HCI trace 全链路 | [已验证] | Reset/Features/Version/BD_ADDR 4 条命令全响应 |
| 命令模拟识别 | [已验证] | `sf32lb52_bt_emulate_cmd()` 识别 30+ 模拟命令 |
| fault 寄存器定位 | [已验证] | `CFSR=0x8200 (PRECISERR)`, `BFAR=0x00100121` |
| 堆损坏定位 | [已验证] | inode 链表被越界写破坏为 LPSYS ROM 区指针 |
| 广播 enable 缺失根因 | [已验证] | `CONFIG_BT_MAX_CONN=1` → -ENOMEM → 跳过 `LE_Set_Advertising_Enable` |
| ext adv 空口无信号 | [已验证] | 0x2039 响应成功但手机双通道扫描不到 → **LCPU 固件缺陷** |
| GATT=y 崩溃诱因 | [已验证] | 关闭 GATT 后 bluetoothd 无崩溃 |
| 探针法布局敏感验证 | [已验证] | 13 个探测点全部通过 → 堆布局改变绕开崩溃 |

**待执行**：
- [ ] 移除探针代码（btservice.c + hci_core.c）
- [ ] 移除 Write_Scan_Enable 模拟，真实下发
- [ ] legacy 广播测试（0x200A）
- [ ] 手机经典蓝牙 + BLE 双通道扫描

**参考文档**：`11_bt_full_diagnosis.md`

---

### ✅ 阶段 9：PAN 路线分析（2026-08-12）

| 任务 | 状态 | 结论 |
|------|------|------|
| BREDR 误判纠正 | [已验证] | 芯片支持双模，HCI trace 观察不完整导致误判 |
| 官方协议栈确认 | [已验证] | **ZBLUE**（非 BLUELET），SAL 仅 `stacks/zephyr` 实现 |
| PAN 官方实现核实 | [已验证] | **官方未实现**（dev 分支 SAL 接口清单无 PAN） |
| 冗余代码评估 | [已验证] | 3400 行中 ~2900 行冗余（85%）|

**代码清理计划**：
- 设备端：`ble_net.c` (SPP废弃) + `ble_gatt.c` (废弃) + `ble_gatt_net.c` (废弃) + `ble_cmd_handler.c` (废弃)
- App 端：BleTunnelManager/TunnelService/TcpProxy（PAN 方案不需要 App）

**路线选项**：
- **A. 自实现 PAN (BNEP)**：600-1000 行 + 反复调试
- **B. 串口 SLIP**：~300 行 + USB-TTL 硬件（~10 元），最可靠
- **C. 接受现状**：真机展示 + QEMU 演示 LLM

**参考文档**：`10_pan_route_reanalysis.md`

---

## 四、核心待办任务清单

### 🔥 高优先级（直接影响作品完成度）

#### **TASK-1: 设备端 BLE GATT NUS 通道实现**
**里程碑 M1.5** - `09_app_spp_tun_integration.md`

- [ ] defconfig 开启 `CONFIG_BLUETOOTH_GATT` + GATT server 相关选项
- [ ] 实现 NUS 服务（6e400001-b5a3-f393-e0a9-e50e24dcca9e）
  - Service: `6e400001`
  - RX Characteristic (写): `6e400002`
  - TX Characteristic (通知): `6e400003`
- [ ] GATT ↔ TUN 桥接改造（替换 SPP pipe）
- [ ] 复用 `ble_net.c` 帧协议（`[len_hi][len_lo][payload]` 大端 16 位）
- [ ] 广播可见性验证（手机 BLE 扫描到 `Agent-Watch`）
- [ ] **验收标准**：设备串口出现 `GATT connected`

**估计工作量**：2-3 天
**阻塞依赖**：需要先移除探针代码（TASK-4）

---

#### **TASK-2: 屏幕点亮现场验证**
**验收标准** - `04_ai_agent_lvgl_display.md`

- [ ] `ls /dev` 确认 `/dev/lcd0`, `/dev/fb0`, `/dev/input0`
- [ ] 运行 `lvgldemo`，确认屏幕显示（非全黑）
- [ ] 运行 `ai_agent -q hello`，确认 LVGL UI 更新
- [ ] 重启后复现验证
- [ ] 保存现场照片/串口输出

**估计工作量**：0.5 天（如硬件正常）
**风险**：可能因背光/CO5300 时序/实际 fb 节点等问题需要额外调试

---

#### **TASK-3: App 端 BLE GATT NUS 联调准备**
**里程碑 M2'** - `09_app_spp_tun_integration.md`

**App 侧已完成**：
- ✅ `BleTunnelManager.kt`（GATT NUS 客户端 + 帧协议 + 分片写入）
- ✅ `TunnelService.kt`（桥接层改为 BLE）
- ✅ `TunnelViewModel.kt` + `TunnelScreen.kt`（BLE 扫描界面）
- ✅ 废弃 `SppManager.kt`

**待执行**：
- [ ] 编译安装 APK
- [ ] 授权蓝牙权限
- [ ] BLE 扫描 → 连接 `Agent-Watch` → 设备串口预期 `GATT connected`
- [ ] `set_llm` + `ask 你好` → 真机首次 LLM 对话

**估计工作量**：1 天

---

### ⚡ 中优先级（提升稳定性）

#### **TASK-4: 清理探针代码与修复崩溃**
**优先级**：高（布局敏感堆损坏）

- [ ] 移除 `btservice.c` 和 `hci_core.c` 探针打点代码
- [ ] 移除 `CONFIG_DEBUG_HARDFAULT_ALERT`（生产构建）
- [ ] 验证移除后 bluetoothd 仍无崩溃
- [ ] 保留 `service_loop_join()` + `/var/run` mkdir 修复

**估计工作量**：0.5 天

---

#### **TASK-5: 移除命令模拟真实下发**
**优先级**：中（BREDR 可见性实验）

- [ ] `sf32lb52_bth4.c` 从 `sf32lb52_bt_emulate_cmd()` 移除 `Write_Scan_Enable`
- [ ] 真实下发到 LCPU，观察 HCI 响应
- [ ] 若 LCPU 仍不支持 → 提交固件缺陷报告给思澈

**估计工作量**：0.5 天

---

#### **TASK-6: legacy 广播测试**
**优先级**：中（BLE 广播验证）

- [ ] 排查 `bttool -m legacy` 参数解析失败问题
- [ ] 测试 `LE_SET_ADVERTISE_ENABLE` (0x200A) legacy 广播
- [ ] 手机 BLE App 扫描验证广播是否可见

**估计工作量**：1 天

---

#### **TASK-7: 端到端 LLM 对话验证**
**里程碑 M3'** - 真机首次 LLM 对话

- [ ] 设备端 GATT NUS 就绪 + App 连接成功
- [ ] `set_llm https://api.stepfun.com/v1 step-3.7-flash <key>`
- [ ] `ask 你好` → 确认设备端 LLM 回复
- [ ] 多轮对话稳定性测试
- [ ] 断连重连测试

**估计工作量**：1 天
**阻塞依赖**：TASK-1, TASK-3

---

### 📋 低优先级（优化与文档）

#### **TASK-8: 清除冗余代码**
**预计减少 2900 行（85%）** - `10_pan_route_reanalysis.md`

- [ ] 设备端 `#if 0` 隔离 SPP/GATT 废弃代码
- [ ] App 端标记废弃（保留备份用于其他演示）
- [ ] `network_manager.c` 改写为 PAN 接线或删除 BLE 接线

**估计工作量**：1 天

---

#### **TASK-9: 完善项目文档**
**提交前必须** - `README.md`

- [ ] 重写 README.md（替换组委会模板）
  - [ ] 作品简介与选题方向
  - [ ] 目录结构说明
  - [ ] 运行方式（编译 → 烧录 → 运行完整步骤）
  - [ ] AI Coding 使用说明
- [ ] 更新 `app/ai_agent/README.md`（移除未实现功能描述）
- [ ] 补充 `docs/` 开发记录到提交包

**估计工作量**：0.5 天

---

#### **TASK-10: 备用方案评估（PAN/串口 SLIP）**
**决策** - `10_pan_route_reanalysis.md`

- [ ] 评估自实现 BNEP 工作量（600-1000 行）
- [ ] 若选择 SLIP：评估 USB-TTL 硬件需求
- [ ] 若接受现状：整理 QEMU + 真机展示材料

**估计工作量**：待定

---

## 五、当前项目状态总结

### 5.1 整体进度

```
总体完成度: ████████░░ 60%

环境搭建    ████████████████████ 100% ✅
固件构建    ████████████████████ 100% ✅
基础显示    ██████████████░░░░░░  80% 🔄 (待现场验证)
AI Agent    ████████████████████ 100% ✅ (基础版)
QEMU 验证   ████████████████████ 100% ✅
BLE 设备端  ████████████████░░░░  80% 🔄 (SPP 作废，待 GATT NUS)
App 联调    ████████░░░░░░░░░░░░  40% ⏸️ (代码完成，待设备端就绪)
LLM 真机    ██░░░░░░░░░░░░░░░░░░  20% ❌ (依赖 BLE GATT NUS)
文档完善    ██████░░░░░░░░░░░░░░  60% 🔄
```

### 5.2 技术状态矩阵

| 功能模块 | 模拟器 (QEMU) | 真机 (SF32LB52) | 说明 |
|---------|-------------|---------------|------|
| **LLM 对话** | ✅ 已验证 | ❌ 未验证 | 依赖 BLE 联网 |
| **cron 定时任务** | ✅ 已验证 | ⏸️ 待实现 | 设备端尚未集成 |
| **REST API** | ✅ 已验证 | ⏸️ 待测试 | 真机未验证 |
| **BLE SPP+TUN** | ❌ 不适用 | ❌ 已否决 | LCPU 固件不支持 BREDR |
| **BLE GATT NUS** | ❌ 不适用 | 🔄 开发中 | **当前主线** |
| **LVGL 显示** | ⚠️ 部分验证 | ⏸️ 待验证 | lvgldemo 未完成现场验收 |
| **ASR/TTS** | ❌ 未实现 | ❌ 未实现 | 需要音频硬件和 SDK |
| **NOR Flash** | ❌ 不适用 | ✅ 已验证 | littlefs 持久化就绪 |

### 5.3 AI Coding 日志

**日志目录**：`logs/Sen70s/`

**manifest.json 统计**：
- 总会话数：**5 个**
- 最新会话：2026-08-10（last_event_at）
- 健康状态：全部 `ok`
- 总计事件：~428 个（含 125+29+15+244+35）
- 使用的模型：`gpt-5.6-sol`（OpenAI GPT 系列）

**日志目录结构**：
```
logs/Sen70s/
├── manifest.json
├── 2026-08-08/  (3 个会话，169 个事件)
├── 2026-08-09/  (1 个会话，244 个事件)
└── 2026-08-10/  (1 个会话，35 个事件)
```

---

## 六、关键阻塞点与风险

### 🚧 阻塞点

1. **TASK-2 屏幕验证**（基础显示未现场验收）
   - 可能原因：背光时序、CO5300 初始化、实际 fb 节点非 `/dev/lcd0`
   - 建议：优先现场验证，不要等到所有功能完成后再检查

2. **TASK-1 BLE GATT NUS 未实现**
   - 影响：LLM 真机对话、App 联调全部阻塞
   - 依赖：需要先移除探针代码（TASK-4）

3. **蓝牙稳定性问题**（bluetoothd 布局敏感崩溃）
   - 现象：BLE 连接数据流触发堆损坏 → 系统挂死
   - 状态：根因已定位（inode 链表越界写），修复方案待执行

### ⚠️ 风险

| 风险 | 等级 | 缓解措施 |
|------|------|---------|
| BLE 吞吐低（~10-30KB/s）| 中 | MTU 协商 247，LLM 文本流够用 |
| 设备端 TCP 超时 | 低 | netmgr read_timeout 默认 30s，可调 |
| 布局敏感崩溃未根解 | 高 | 需要系统排查堆越界写入源头 |
| LCPU 固件缺陷 | 中 | BREDR 不可用确认；BLE GATT 可绕过 |
| 提交截止时间 | 中 | 9 月 20 日，当前约 5 周 |

---

## 七、下一步行动建议

### 立即执行（本周）

1. **TASK-2**: 现场验证屏幕点亮（1 小时）
2. **TASK-4**: 移除探针代码（0.5 天）
3. **TASK-5**: 移除命令模拟，测试 BREDR（0.5 天）

### 短期执行（1-2 周）

4. **TASK-1**: 实现 BLE GATT NUS 通道（2-3 天）
5. **TASK-6**: legacy 广播测试（1 天）
6. **TASK-3**: App 端联调准备（0.5 天）

### 中期执行（2-3 周）

7. **TASK-7**: 真机 LLM 对话验证（1 天）
8. **TASK-9**: 完善项目文档（0.5 天）
9. **TASK-8**: 清除冗余代码（1 天）

### 决策点

10. **TASK-10**: PAN vs SLIP 路线决策（1 周内）

---

## 八、参考资源

### 开发文档

- `docs/setup_guide.md` — 环境配置与构建指南
- `docs/development_records/README.md` — 开发记录索引
- `00_quick_start_device_to_app.md` — 快速开始指南
- `02_device_usb_serial.md` — USB/串口连接
- `03_build_flash_nuttx.md` — 构建/烧录流程
- `04_ai_agent_lvgl_display.md` — AI Agent + LVGL 显示验证
- `05_troubleshooting.md` — 故障排查索引
- `06_pet_ui_design.md` — 宠物 UI 设计（桌宠）
- `07_qemu_aiagent_llm_validation.md` — QEMU 全链路验证
- `08_ble_spp_tun_real_device.md` — BLE SPP+TUN 真机验证
- `09_app_spp_tun_integration.md` — App 蓝牙代理联网规划
- `10_pan_route_reanalysis.md` — PAN 路线重新分析
- `11_bt_full_diagnosis.md` — 蓝牙全链路深度诊断

### 脚本与工具

- `build_and_flash.sh` — 一键构建 + 烧录
- `flash_only.sh` — 仅烧录
- `nsh_console.sh` — 串口 NSH 终端
- `logs/hw_ble_test.py` — BLE 真机验证脚本
- `logs/qemu_*_test.sh` — QEMU 测试脚本集

### AI 日志

- `logs/Sen70s/manifest.json` — 会话清单
- `logs/Sen70s/2026-08-0{8,9,10}/` — 原始对话日志

### 官方文档

- [大赛总览](https://github.com/open-vela/docs/blob/dev-ai-contest-2026/zh-cn/contest_2026/contest_overview.md)
- [代码提交指南](https://github.com/open-vela/docs/blob/dev-ai-contest-2026/zh-cn/contest_2026/code_submission_guide.md)
- [AI 日志归集手册](https://github.com/open-vela/docs/blob/dev-ai-contest-2026/zh-cn/contest_2026/ai_coding_log_guide.md)

---

## 九、关键决策记录

### 决策 1：SPP → BLE GATT NUS 转向（2026-08-12）

**背景**：真机联调时手机永远扫描不到设备

**根因**：SF32LB52 LCPU 闭源固件不支持 BREDR（Write_Scan_Enable 零响应）

**影响**：
- ✅ 手机 App BLE 扫描可见（BLE 广播穿戴芯片强项）
- ⚠️ 吞吐降低（~10-30KB/s vs ~100KB/s，但 LLM 文本流够用）
- 🔄 App 侧复用现有 BleManager（NUS 客户端），改动更小

**参考**：`09_app_spp_tun_integration.md` §1.1, `11_bt_full_diagnosis.md` §2.3

---

### 决策 2：PAN 自实现评估中（2026-08-12）

**背景**：需要联网方案，PAN vs SLIP vs 接受现状

**发现**：
- ✅ 芯片支持 BREDR（官方规格 + HCI 命令链路实证）
- ❌ openvela 官方未实现 PAN SAL（dev 分支核实）
- ⚠️ BREDR 可见性命令被 vendor 驱动模拟，真实下发待测试

**选项**：
- **A. 自实现 BNEP**（600-1000 行 + 反复调试）
- **B. 串口 SLIP**（~300 行 + USB-TTL 硬件，最可靠）
- **C. 接受现状**（真机展示 + QEMU 演示 LLM）

**待决策**：截止本周内

**参考**：`10_pan_route_reanalysis.md`

---

## 十、附录：故障与解决历史

| # | 故障 | 状态 | 解决方案 |
|---|------|------|---------|
| 1 | AI 日志采集器路径错误 | ✅ 已修复 | 设置 `CONTEST_REPO_DIR` |
| 2 | `transcript_unreadable` | ✅ 已绕过 | 单个文件异常，其余正常 |
| 3 | `dmesg` 无权限 | ✅ 已修复 | 改用 `journalctl -k` |
| 4 | USB `error -71` | ✅ 已绕过 | 重新插拔/重启 |
| 5 | `/dev/serial/by-id` 路径失效 | ✅ 已绕过 | 改用 `/dev/ttyACM0` |
| 6 | 烧录脚本不支持 `--connect-attempts` | ✅ 已修复 | 移除不兼容参数 |
| 7 | NSH 回车无响应 | ✅ 已修复 | `--omap crlf` |
| 8 | LSM6DS3 初始化错误 | ✅ 忽略 | 非阻塞警告，无传感器 |
| 9 | 黑屏/显示未验证 | 🔄 待验证 | 现场测试 pending |
| 10 | NOR Flash write verify mismatch | ✅ 已修复 | `sf32lb_flash_write_page_single()` 单线写页 |
| 11 | SPP 方案被否决 | ✅ 已决策 | 转向 BLE GATT NUS |
| 12 | bluetoothd 静默退出 | ✅ 已修复 | `CONFIG_NET_LOCAL` + `/var/run` + `service_loop_join` |
| 13 | SPP register failed | ✅ 已修复 | `bt_adapter_enable` 等待 ON |
| 14 | BT adapter enable timeout | ✅ 已修复 | `CONFIG_UART_BTH4=y` |
| 15 | 布局敏感堆损坏 | 🔄 已定位 | 探针验证完成，修复待执行 |

**总计**：15 个故障，12 个已解决，3 个进行中

---

**报告生成时间**：2026-08-12
**下次更新**：TASK-1 ~ TASK-4 完成后

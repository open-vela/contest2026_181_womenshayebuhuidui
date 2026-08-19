# 项目任务与进度速查

> **项目**: contest2026_181_womenshayebuhuidui | **队伍**: Sen70s (#181)
> **目标板**: SF32LB52-DevKit-LCD | **截止**: 2026-09-20

---

## 🎯 核心主线任务

| ID | 任务 | 优先级 | 状态 | 截止 |
|----|------|--------|------|------|
| **T-01** | 屏幕点亮现场验证 | 🔥 高 | 🔄 待验证 | 本周 |
| **T-02** | BLE GATT NUS 通道实现 | 🔥 高 | ⏸️ 未开始 | 1周内 |
| **T-03** | App 端联调 | 🔥 高 | ⏸️ 阻塞 | T-02 后 |
| **T-04** | 移除探针代码 | 🔥 高 | ⏸️ 未开始 | 本周 |
| **T-05** | 真机 LLM 对话验证 | 🔥 高 | ❌ 未验证 | T-02/03 后 |

---

## 📊 功能完成度

```
环境搭建    [███████████████████] 100% ✅
固件构建    [███████████████████] 100% ✅
基础显示    [█████████████░░░░░░]  80% 🔄 (待现场验收)
AI Agent    [███████████████████] 100% ✅ (基础版)
开机自启    [███████████████████] 100% ✅ (rcS: audio_setup + ai_agent -d, 上电即桌面, 08-16)
端侧 TFLM  [███████████████████] 100% ✅ (velaAI 本地模型, 08-16)
语音输入   [███████████████████] 100% ✅ (特征提取 bug 修复, 主机 81.9% 准确率, 08-16)
语音输入-真机 [████████████████░░] 80% 🔄 (累计 12 修: hard fault/时钟表/O_RDWR/apb 垃圾/瞬态/mq 阻塞/多 worker/信号量残留/看门狗/VAD 直流+自适应+封顶; 全链路稳定跑通+UI 25s 自愈; ASR 真实语音准确率待模型迭代, 见 docs/development_records/13、18)
QEMU 验证   [███████████████████] 100% ✅
BLE SPP     [███████████████████] 100% ❌ (已否决)
BLE GATT    [░░░░░░░░░░░░░░░░░░░░]   0% ⏸️ (当前主线)
App 联调    [███████████░░░░░░░░]  40% ⏸️ (代码完成)
真机 LLM   [████░░░░░░░░░░░░░░░░]  20% ❌ (依赖 BLE)
文档完善    [████████░░░░░░░░░░░░]  60% 🔄
```

---

## ✅ 已完成里程碑

- ✅ **M0**：环境搭建 + 工具链配置（08-08）
- ✅ **M1**：固件构建 + 烧录 + NSH 启动（08-09）
- ✅ **M2**：AI Agent 基础功能 + LVGL 集成（08-09）
- ✅ **M3**：QEMU 全链路验证（LLM/cron/REST）（08-11）
- ✅ **M4**：BLE SPP+TUN 设备端验证（08-11，后否决）
- ✅ **M5**：NOR Flash littlefs 持久化（08-11）
- ✅ **M6**：蓝牙深度诊断 + 根因定位（08-12）
- ✅ **M7**：velaAI 端侧 TFLite Micro 模型部署 + 桌宠接入（08-16，真机验证通过；增补: BPE 分词器逐位对齐 + agent 两段式模式 100/100/100, **真机 10 项查询全对 ~9s**; UI 常驻+宠物页卡死修复+动画删除+推理可打断, 见 docs/development_records/13、18）
- ✅ **M8**：语音提问模块（mic 采集 + VAD + 命令词 ASR + 本地模型回答）主机验证 81.9%（08-16；修复 `fft_radix2` twiddle 步长 `int` 截断 bug，C 特征与 Python 逐位一致）

---

## 🔄 进行中/待执行

| 任务 | 负责人 | 预计时间 | 阻塞 |
|------|--------|---------|------|
| T-01 屏幕验证 | 用户 | 0.5天 | - |
| T-04 移除探针 | - | 0.5天 | - |
| T-05 BREDR 实验 | - | 0.5天 | T-04 |
| T-02 BLE GATT NUS | - | 2-3天 | T-04 |
| T-06 legacy 广播 | - | 1天 | T-05 |
| T-03 App 联调 | 用户 | 1天 | T-02 |
| T-07 真机 LLM | - | 1天 | T-02/03 |
| T-08 文档完善 | - | 0.5天 | - |
| T-09 清除冗余代码 | - | 1天 | - |
| T-10 PAN/SLIP 决策 | - | 1周内 | - |

---

## 🚨 关键阻塞点

1. **屏幕点亮未验收** → 需现场验证（1 小时）
2. **BLE GATT NUS 未实现** → 阻塞真机 LLM 对话
3. **蓝牙稳定性** → 布局敏感堆损坏待根解
4. **BREDR 固件支持** → LCPU 闭源固件不响应 BREDR 命令
5. **USB 烧录不稳定** → sftool stub 下载超时（1M 与 460800+compat 均失败），需物理重插 USB 后按 replug_fast 模式烧录（08-16 语音固件已就绪: `out/sifli_sf32lb52_devkit_lcd_nsh/nuttx.bin` 7.8MB）

---

## 📁 关键文档

- **全貌分析**：[`docs/development_records/PROJECT_STATUS_REPORT.md`](PROJECT_STATUS_REPORT.md)
- **快速开始**：[`00_quick_start_device_to_app.md`](00_quick_start_device_to_app.md)
- **构建指南**：[`03_build_flash_nuttx.md`](03_build_flash_nuttx.md)
- **蓝牙诊断**：[`11_bt_full_diagnosis.md`](11_bt_full_diagnosis.md)
- **PAN 分析**：[`10_pan_route_reanalysis.md`](10_pan_route_reanalysis.md)
- **QEMU 验证**：[`07_qemu_aiagent_llm_validation.md`](07_qemu_aiagent_llm_validation.md)
- **AI 日志**：`logs/Sen70s/manifest.json`（5 个会话）

---

## 🎤 关键决策

| 决策 | 日期 | 结论 |
|------|------|------|
| SPP → BLE GATT NUS | 08-12 | LCPU 固件不支持 BREDR，转向 BLE GATT NUS |
| PAN vs SLIP | 08-12 | 待定（官方未实现 PAN，SLIP 最可靠） |
| littlefs 修复 | 08-11 | 单线写页替代 QMODE（DMA 路径失效）|

---

**最后更新**：2026-08-12
**下次更新**：T-01 ~ T-05 完成后

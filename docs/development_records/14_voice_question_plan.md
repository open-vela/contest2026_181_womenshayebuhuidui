# 真机语音提问、文字回答 — 麦克风探索报告与实施计划

> 日期: 2026-08-16 | 状态: 探索完成, **P1 代码完成+编译通过**, 真机验证受 USB 链路不稳定阻塞
> 目标: SF32LB52-DevKit-LCD 真机上"语音提问 → 文字回答"(本地 velaAI 模型)

---

## 一、硬件探索结论(已验证)

| 项 | 结论 | 证据 |
|----|------|------|
| 板载麦克风 | ✅ **集成 MEMS MIC**, 模拟音频输入 | 官方产品页/oshwhub 设计页: "SF32LB52-DevKit-LCD开发板集成MEMS MIC和音频功放芯片。支持板上mic的音频信号输入。支持外接喇叭(3W/4Ω)" |
| 音频输出 | ✅ 模拟音频输出 + 板载 Class-D 音频 PA | AUDIO_PA_CTRL = PA10 (bsp_pinmux.c:157) |
| 芯片音频外设 | ✅ 内置 audcodec: ADC×2(麦克风)/DAC×2 + MICBIAS | `vendor/sifli/chips/drivers/cmsis/sf32lb52x/audcodec.h`(907 寄存器定义) |
| Codec HAL | ✅ 完整 DMA 收发 API, **已编入当前固件** | `bf0_hal_audcodec.c`(vendor CMakeLists:76), `HAL_AUDCODEC_Init/Receive_DMA/Transmit_DMA` |
| 当前固件音频 | ❌ CONFIG_AUDIO 未启用, 无 /dev/audio0 | `.config: CONFIG_AUDIO is not set` |
| 参考 SDK | 芯片 Kconfig 含 I2S MIC/CODEC 选项(RT-Thread 侧), 模拟 MIC 走 audcodec ADC | `chips/sf32lb52/Kconfig:1278-1293` |

**结论: 板子自带 MEMS 麦克风, 芯片自带编解码器与 HAL, 无需外接硬件即可采集语音。**

## 二、openvela 侧结论(查阅资料)

1. **官方语音链路** ([ai_agent_quickstart.md](../../docs/zh-cn/contest_2026/ai_hardware/ai_agent_quickstart.md) §3):
   麦克风采集 → **火山引擎流式 ASR**(WebSocket) → LLM → 火山 TTS;
   命令 `set_volc_key` / `set_volc_asr` / `voice_start`。实现位于
   `packages/ai_agent/src/voice/` (voice_channel.c + volc_asr.c + volc_tts.c)。

2. **音频驱动规范** ([Audio_Driver_Guide](../../docs/zh-cn/device_dev_guide/media/audio/Audio_Driver_Guide.md)):
   实现 `audio_lowerhalf_s`(`audio_ops_s`) → `audio_register("/dev/audio0")`;
   官方测试工具: `nxplayer`(播放) / `nxrecorder`(录音, apps/system/nxrecorder,
   AUDIOIOC_* 标准接口); 参考实现 `arch/sim/src/sim/sim_alsa.c`。

3. **网络约束(关键)**: 设备**无 WiFi 模组**; BLE GATT NUS 网络隧道未完成
   (docs_ble/15: 仅到"手机可发现板子", 配对/SSP/PAN/BNEP 未通)。
   → **火山云 ASR 短期不可行, 必须端侧 ASR**。

## 三、ASR 方案对比与选择

| 方案 | 依赖 | 状态 | 结论 |
|------|------|------|------|
| A. 火山云 ASR | 网络; BLE 隧道 | 隧道未完成 | ❌ 长期 |
| B. 手机 ASR 转发 | BLE GATT NUS 文本通道 + App 改造 | 通道未建 | ⏸️ 中期备选 |
| **C. 端侧 TFLM 命令词识别** | 训练小 CNN(mel 谱→命令), 16x8 量化 | TFLM 已就绪 | ✅ **推荐** |

**方案 C 详情**: 主机侧 TTS 合成中文命令语音(与真机麦克风采集结合) →
mel 频谱特征 → 小 CNN(≈10-20 个智能家居命令)→ 16x8 量化 TFLite →
TFLM 部署(复用上轮 velaAI 推理栈)→ 识别命令映射为本地 LM prompt
(`<usr> 打开客厅的灯 <bot>`)→ 本地模型生成文字回答 → 桌宠气泡显示。
**全离线, 契合大赛"TFLite Micro 端侧 AI"主题。**

## 四、实施计划

| 阶段 | 内容 | 验证 |
|------|------|------|
| **P1 音频驱动** | NuttX audio lower-half: audcodec HAL + DMA 采集(16k/16bit/mono) + MIC 增益 + 软件 VAD; 启用 CONFIG_AUDIO + nxrecorder | 真机录音 → 串口/文件回传 → 波形/频谱确认 MIC 有效 |
| **P2 命令词模型** | 主机: 中文 TTS 合成命令集语音 → mel 特征 → CNN 训练 → 16x8 量化导出; 设备: TFLM 推理 | 真机命令识别率 ≥ 90% |
| **P3 端到端** | ai_agent 集成: 触发录音 → VAD → ASR → 本地 LM → 桌宠气泡; 保留关键词兜底 | 真机"语音提问 → 文字回答"全链路 |

## 五、P1 进展 (2026-08-16)

**已交付 (代码 + 编译验证):**

| 项 | 位置 | 说明 |
|----|------|------|
| NuttX audio lower-half 驱动 | `app/ai_agent/audio/sf32lb52_audcodec.c/h` | audcodec HAL + DMA 采集 (16k/16bit/mono), 注册 /dev/audio/audio0 |
| 注册应用 | `app/ai_agent/audio/audio_setup_main.c` | NSH 应用 `audio_setup`, 启动即注册音频设备 |
| 构建配置 | defconfig: CONFIG_AUDIO + NXRECORDER + AUDIO_SETUP | 完整固件 7.66MB / 精简测试固件 966KB 均编译通过 |
| 参考实现 | SiFli SDK `drv_audcodec_m.c` (gitee) | PLL/DMA/通道配置逐项对照 (DMAC1_CH4, REQ 39, 16k 时钟表) |

**真机验证状态: 阻塞**
- 完整固件(含音频驱动)已 99% 写入并成功启动 (设备串口确认 ai_agent/audio_setup/nxrecorder/hexdump 均在)
- 板载驱动注册成功 (`/dev/audio/dev/audio0` 存在), 但旧版 getcaps 缺 ac_format.hw,
  nxrecorder 设备匹配失败; 修复已完成, **尚未能烧录** (新镜像 966KB)
- 烧录连续 6 次失败 (stub 下载/RAM 命令超时/Broken pipe), 判断为 **USB 链路硬件级不稳定**
  (早期 7.6MB 烧录成功过, 现连 2KB stub 都断; 建议换线/换口/外接供电后再试)

**烧录恢复方法 (USB 稳定后):**
```bash
# 精简测试固件 (验证麦克风)
./build_and_flash.sh flash -p /dev/ttyACM0 -i out/audio_test/nuttx.bin
# 或: sftool -c SF32LB52 -p /dev/ttyACM0 -b 1000000 --before no_reset \
#   --connect-attempts 40 --after soft_reset \
#   write_flash out/audio_test/nuttx.bin@0x12010000
# 真机验证: audio_setup -> nxrecorder -> device /dev/audio/audio0
#           -> recordraw /data/mic.pcm 1 16 16000 3 -> hexdump /data/mic.pcm
```

## 六、风险与依赖

- audcodec 时钟/PLL 需自实现 `bf0_enable_pll`(__weak 空实现, 需查 48M xtal 分频链)
- DMA 通道选择需参考 bf0 HAL 其他外设(DMA_HandleTypeDef 初始化模式)
- 命令词模型训练数据以 TTS 合成为主, 真机录音微调可提升鲁棒性
- 若 PA 需使能才能在调试中听到输出, 不影响录音(仅影响播放验证)

## 六、P2 进展 (命令词识别模型, 2026-08-16)

**架构**: 16kHz PCM → 1s 窗口 → 40 mel × 48 帧 (log-mel, 全局归一化) →
小 CNN (3×Conv+Pool, ~68K 参数, int8 量化) → 12 类 (unknown + 11 命令)

**代码 (app/ai_agent/speech/)**:
| 文件 | 说明 |
|------|------|
| `cmds.txt` | 11 条命令 + 文本变体 + 对应本地 LM prompt |
| `gen_speech_data.py` | edge-tts 中文合成 (8 音色 × 5 语速 × 变体) + 增强 (音量/噪声/时移) + unknown 拒绝样本, 可断点续跑 |
| `train_cmd_model.py` | mel 特征 + CNN 训练 + int8 TFLite 导出 + 一致性校验, 生成 C 表 (model_data.h/mel_table.h/feat_stats.h/cmd_table.h) |
| `speech_feat.c/h` | 设备端 log-mel 特征 (512 FFT + mel 矩阵 + 归一化) |
| `cmd_asr.cxx/h` | TFLM 命令识别推理 (arena 64KB, 置信度阈值 0.55) |
| `mic_capture.c/h` | 经 /dev/audio/audio0 录音 (AUDIOIOC 流程, 8×4096B 缓冲) |
| `voice_question.c/h` | 端到端: VAD(能量) → 录音 → 识别 → 本地 LM → 回复 |
| `host_speech_main.cxx` | 主机侧验证 (设备同款代码, val 集准确率) |

**接入**: `ai_agent -v` (语音提问模式) + 桌宠点击云朵 → "我在听…" → 语音回答气泡。

**状态**: 训练数据生成中 (~1300 样本), 训练后主机侧 TFLM 验证 → 固件编译。

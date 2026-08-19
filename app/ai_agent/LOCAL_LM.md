# velaAI 端侧语言模型接入 (TFLite Micro)

本目录将 [`velaAI`](https://github.com/open-vela/contest2026_181_womenshayebuhuidui)
项目训练的**端侧 LSTM 语言模型** (TFLite Micro 16x8 量化) 部署进 ai_agent,
作为桌宠 (pet page) 的本地回复引擎 —— 不依赖云端, 完全在 SF32LB52 MCU 上运行。

## 模型与数据

| 文件 | 说明 |
|------|------|
| `model/velaai_lm_v3_16x8.tflite` | V3 模型 (vocab 5000, LSTM 256, 231 万参数, val acc 0.8705) |
| `model/model_data.h` | 模型字节数组 (RODATA, XIP 从 Flash 直接执行) |
| `model/tokenizer_v3_data.h` | SentencePiece V3 词表 (5000 pieces, blob + FNV-1a 哈希) |

重新生成 (需 sentencepiece):

```bash
python3 tools/gen_local_lm_tables.py \
  --tflite /path/to/velaAI/mcu_language_model_v3_16x8.tflite \
  --sp-model /path/to/velaAI/tokenizer_v3/tokenizer.model \
  --outdir model
```

## Agent 模式 (2026-08-16 增强)

`ai_lm_agent_reply()` 按 velaAI 训练格式走**两段式智能体流程**:

```
"<usr> 把卧室空调调到26度"
  → [模型] 生成 "<call> set_ac <arg> 卧室 <arg> 26 </tool>"
  → [端侧执行器] 设备状态表 / RTC / 模拟传感器 → "<obs> success"
  → [模型] 生成 "<bot> 好的，卧室空调已设为26度 <eos>"
  → [校验] 回复须包含调用的房间与数值; 不通过 → 训练模板兜底
```

设计动机 (主机侧 100 条留出集实测):

| 配置 | 房间正确 | 数值正确 | 结构正确 |
|------|---------|---------|---------|
| 全精度 h5 直答 | 81% | 100% | 82% (整句) |
| 16x8 量化直答 (`ai_lm_reply`) | 38% | 67% | 18% (整句) |
| **16x8 + agent 模式 (本实现)** | **100%** | **100%** | **100%** |

量化损伤主要在槽位复制 (int8 权重), 而工具调用生成仍 100% 准确, 故:
工具选择由模型+规则意图层共同确认, 动作真值由执行器掌握,
自然语言措辞优先用模型生成 (通过校验时), 失败回退与训练模板一致的确定性回复。

意图层 (`ag_detect_intent`): 规则识别 灯/窗帘/空调(开关/调温/模式)/新设备/
定时/门锁/天气/时间/温度/音乐/电视, 修正量化模型的偶发工具错选
(如裸"今天天气怎么样"在训练数据中总带城市前缀, 量化模型会误调 set_ac)。

执行器 (`ag_execute`): 设备状态表 (16 项, 掉电丢失) + 真实 RTC 报时;
温度/天气为演示值 (板上无对应传感器)。

## 代码结构

| 文件 | 说明 |
|------|------|
| `ai_lm.cxx` / `ai_lm.h` | TFLM 推理封装: 简单模式 + agent 模式 (两段式/执行器/校验/模板) |
| `tokenizer.c` / `tokenizer.h` | SentencePiece BPE 分词 (与 SP 逐位一致, 见下) |
| `ai_agent_main.c` | `ai_agent -q "..."`: agent 模式生成, 关键词表兜底 |
| `ui/pet_page.c` | 点击 ☁️ 桌宠 → 语音提问 → agent 模式回复; 麦克风失败时闲聊演示 |
| `speech/voice_question.c` | mic 采集 + VAD + 命令词 ASR → agent 模式回复 |
| `tools/host_agent_eval.cxx` | agent 模式批量评估 (x86 主机, 设备同款代码) |

## 分词器 (与 SentencePiece 逐位一致)

`tokenizer.c` 按训练侧 sentencepiece BPE 算法实现:

1. 归一化 (identity): 去首尾空白, 连续空白压缩
2. 空格转义为 `▁` (U+2581)
3. `<usr>/<bot>/<call>/<arg></tool>/<obs>/<eos>` 用户定义符号作原子
4. 段内反复合并"拼接结果 id 最小"的相邻符号对 (SP BPE 词表
   score = -(id-9), id 序即合并优先级)
5. 连续未知字符合并为单个 `<unk>`

已在 velaAI 全部数据逐位验证: corpus_v3 20009 + train_v3 18008 条全一致。
(早前"贪心最长匹配"版本与 BPE 合并序不等价, 会产生分布外 prompt, 已废弃。)

## 模型接口 (与 velaAI 训练/导出侧一一对应)

```
输入: token(1,1) int32, h_in(1,256) f32, c_in(1,256) f32
输出: c_out(1,256) f32, logits(1,5000) f32, h_out(1,256) f32
生成: 自回归 argmax, 遇 <eos>(id=2) 早停
简单模式 Prompt: "<usr> {用户输入} <bot>"
Agent 模式:       "<usr> {用户输入}" → <call>.. → 执行器 → "<obs> v" → <bot>
```

注意: LSTM 被导出为单步 unroll 的基础算子 (GATHER / FULLY_CONNECTED /
SOFTMAX / SPLIT / UNPACK / TANH / LOGISTIC / MUL / ADD / QUANTIZE /
DEQUANTIZE), 全部在 TFLM 支持列表内, 无需 LSTM 专用内核。

## 构建配置

需要启用 (defconfig):

```text
CONFIG_HAVE_CXX=y
CONFIG_LIBCXXTOOLCHAIN=y        # arm-none-eabi libstdc++
CONFIG_MATH_GEMMLOWP=y
CONFIG_MATH_KISSFFT=y
CONFIG_MATH_RUY=y
CONFIG_SYSTEM_FLATBUFFERS=y
CONFIG_TFLITEMICRO=y            # apps/mlearning/tflite-micro
```

`CMakeLists.txt` 在 `CONFIG_TFLITEMICRO` 开启时自动加入 `ai_lm.cxx` +
`tokenizer.c` 并链接 `tflite_micro` 库。

## Tensor Arena

`ai_lm.cxx` 中 `AI_LM_ARENA_SIZE` 默认 64KB (主机侧 RecordingMicroInterpreter
实测仅需 37840 字节, 静态 BSS)。
若 SRAM 紧张, 可改为从 PSRAM (0x60000000, 8MB) heap 分配。

## TFLM 补丁 (apps/mlearning/tflite-micro 工作树)

openvela 自带的 TFLM 提交 (cfa4c91d) 有两个问题, 需在构建树打补丁
(补丁文件见 `tools/`, 已在当前工作树应用):

| 补丁 | 原因 |
|------|------|
| `0004-gcc13-placement-new-delete.patch` | GCC 13 要求 placement new 的 operator delete 可访问 (compatibility.h 宏改为 public) |
| `0005-gather-unpack-int16.patch` | 16x8 模型的 Embedding GATHER 与 LSTM gate UNPACK 输出 int16, TFLM 内核原本不支持 |
| `0006-nuttx-stdlib-cxx-compat.patch` | NuttX `stdlib.h` 的 div_t/ldiv_t/lldiv_t/putenv 与 libstdc++ `<cstdlib>` 冲突, C++ 模式跳过 (nuttx/include/stdlib.h) |

defconfig 需启用 C++ (CONFIG_LIBCXXTOOLCHAIN) + 上述库。

## 主机侧验证

- `tools/build_host_test.sh`: x86 TFLM 编译**设备同款** `tokenizer.c` /
  `ai_lm.cxx`, 跑端到端生成 (简单 + agent 两种模式)。
- 分词器逐位校验: 对 corpus_v3 全部 20009 行输出与 sentencepiece 一致。
- agent 模式评估: 从 val_v3 抽 100 条, `host_agent_eval` 输出按
  房间/数值/结构三项评分 → 100% / 100% / 100% (见上方表格)。

## 已知边界

- 16x8 量化模型的槽位复制弱于全精度 (直答模式房间 38%), agent 模式通过
  执行器校验 + 模板兜底修正; 简单模式 `ai_lm_reply` 保留原行为用于对比。
- 意图层为关键词规则, 覆盖训练数据的设备/工具词表; 未覆盖的说法
  (AG_INT_NONE) 交由模型自行决定是否调用工具。
- 温度/天气为演示值 (板上无传感器); 设备状态表掉电丢失。
- 未登录字符编码为 `<unk>` (vocab 无 byte-fallback piece)。

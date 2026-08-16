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

## 代码结构

| 文件 | 说明 |
|------|------|
| `ai_lm.cxx` / `ai_lm.h` | TFLM 推理封装: 初始化 + 自回归生成 + `<bot>` 回复提取 |
| `tokenizer.c` / `tokenizer.h` | 简化 SentencePiece 分词: 最长匹配编码 + 解码 |
| `ai_agent_main.c` | `ai_agent -q "..."`: 本地模型生成, 关键词表兜底 |
| `ui/pet_page.c` | 点击 ☁️ 桌宠 → 后台线程跑模型 → 气泡显示回复 |

## 模型接口 (与 velaAI 训练/导出侧一一对应)

```
输入: token(1,1) int32, h_in(1,256) f32, c_in(1,256) f32
输出: c_out(1,256) f32, logits(1,5000) f32, h_out(1,256) f32
生成: 自回归 argmax, 遇 <eos>(id=2) 早停
Prompt: "<usr> {用户输入} <bot>"  (与训练数据格式一致)
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

`tools/build_host_test.sh` 用 x86 TFLM 编译**设备同款** `tokenizer.c` /
`ai_lm.cxx`, 跑端到端生成, 与 velaAI 的 Python 参考输出对比
(单步 logits 逐位一致, 全部 demo 话术输出一致)。

## 已知边界

- 模型为小参数 LSTM, 部分问句会生成工具调用占位 (`<call> ... </tool>
  <obs> ... <bot> 回复`), `ai_lm_reply` 自动提取 `<bot>` 后文本;
  无 `<bot>` 的工具残渣视为生成失败并回退关键词表。
- 未登录字符编码为 `<unk>` (vocab 无 byte-fallback piece)。

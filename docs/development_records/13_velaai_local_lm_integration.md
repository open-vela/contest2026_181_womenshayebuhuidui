# velaAI 端侧模型接入 ai_agent (TFLite Micro) — 开发记录

> 日期: 2026-08-16 | 状态: 代码完成, 固件编译通过, 真机待验证
> 关联: `app/ai_agent/LOCAL_LM.md` (技术细节), `TASK_STATUS.md`

## 一、目标

按 openvela 大赛「TFLite Micro 端侧 AI」要求, 将
[`velaAI`](../../../../../velaAI) 项目训练的 LSTM 语言模型
(V3, vocab 5000, LSTM 256, 16x8 量化, val acc 0.8705) 部署进
`app/ai_agent`, 并让桌宠 (pet page) 直接使用该模型生成回复 —
不依赖云端, 完全在 SF32LB52 MCU 上运行。

## 二、改动清单

### 新增 (contest 仓 app/ai_agent/)

| 文件 | 说明 |
|------|------|
| `model/velaai_lm_v3_16x8.tflite` | V3 模型二进制 (3.0 MB) |
| `model/model_data.h` | 模型 C 数组 (RODATA, 16 字节对齐, XIP) |
| `model/tokenizer_v3_data.h` | SP 词表表 (5000 pieces, FNV-1a 哈希) |
| `ai_lm.cxx` / `ai_lm.h` | TFLM 推理: 初始化 + 自回归生成 + `<bot>` 回复提取 |
| `tokenizer.c` / `tokenizer.h` | 简化 SentencePiece 分词 (最长匹配, 与 SP 逐位一致) |
| `tools/gen_local_lm_tables.py` | 表生成脚本 (tflite→model_data.h, sp model→词表) |
| `tools/build_host_test.sh` | 主机侧 x86 验证 (设备同款代码) |
| `tools/0004~0006-*.patch` | TFLM/NuttX 工作树补丁 (见 LOCAL_LM.md) |
| `ui/assets/` | 桌宠 UI 资源 (字体/云朵位图/壁纸, 从 packages/ai_agent 同步) |
| `LOCAL_LM.md` | 技术文档 |

### 修改

| 文件 | 说明 |
|------|------|
| `ai_agent_main.c` | `ai_agent -q "..."` 优先走本地模型, 关键词表兜底 |
| `ui/pet_page.c` | 点击 ☁️ 桌宠 → 后台线程跑模型 → 气泡显示 (lv_async_call) |
| `CMakeLists.txt` / `Makefile` | 加入 ai_lm/tokenizer/assets; CONFIG_TFLITEMICRO 时链接 tflite_micro |
| `Kconfig` | 栈默认值 49152; 文档说明 TFLM 行为 |

### 构建配置 (defconfig: vendor/sifli/.../nsh)

```text
CONFIG_HAVE_CXX=y
CONFIG_LIBCXXTOOLCHAIN=y
CONFIG_MATH_GEMMLOWP=y
CONFIG_MATH_KISSFFT=y
CONFIG_SYSTEM_FLATBUFFERS=y
CONFIG_TFLITEMICRO=y
CONFIG_EXAMPLES_AI_AGENT_STACKSIZE=49152
```

### 工作树补丁 (生产仓, 已记录在 tools/, 不可进 contest 仓)

| 位置 | 内容 |
|------|------|
| `apps/mlearning/tflite-micro/.../compatibility.h` | GCC13 placement new delete 可访问 |
| `apps/mlearning/tflite-micro/.../kernels/gather.cc` | GATHER 支持 int16 (Embedding 16x8) |
| `apps/mlearning/tflite-micro/.../kernels/unpack.cc` | UNPACK 支持 int16 (LSTM gate) |
| `apps/mlearning/tflite-micro/Kconfig` | TFLITEMICRO 去掉 MATH_RUY 依赖 (ruy 需 std::thread, TFLM 不用) |
| `nuttx/include/stdlib.h` | div_t/ldiv_t/lldiv_t/putenv/div C++ 模式跳过 (libstdc++ 冲突) |

## 三、验证

1. **主机侧 (x86 TFLM)**: 设备同款 tokenizer.c + ai_lm.cxx 编译运行,
   单步 logits 与 Python TFLite 解释器逐位一致; 全部 demo 话术输出一致;
   Tensor Arena 实测 37.8 KB (配置 64 KB)。
2. **固件编译**: `./build_and_flash.sh build` 通过,
   nuttx.bin 4.40 MB (含 3 MB 模型), flash 占用 23.6%, SRAM 72 KB/512 KB。
3. **真机**: 待烧录 (需物理拔插 USB 抓 bootloader 窗口)。

## 四、已知边界

- 量化模型部分问句回复质量有限 (如 "现在几点了" 只生成工具占位),
  `ai_lm_reply` 提取 `<bot>` 后文本, 失败时回退关键词表。
- 模型为小参数量 LSTM, 复杂语义 (多轮/推理) 不在能力范围。

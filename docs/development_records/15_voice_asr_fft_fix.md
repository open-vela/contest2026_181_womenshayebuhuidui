# 15: 语音命令词 ASR — C 特征提取 bug 根因与修复 (08-16)

## 现象
- 主机端 TFLM 端到端准确率仅 17.6%，Python 训练侧 ~85%。
- 同一 PCM 文件：C `speech_feats_from_pcm()` 输出 mel0=-3.3165，Python 为 -6.1986。

## 排查过程
1. 独立拷贝 FFT（`/tmp/test_fft.c`）对同一文件计算正确（-6.1986）→ 怀疑非 FFT 本身。
2. 在 `speech_feat.c` 内逐级插桩：FFT 输入样本 w[0..399] 与 Python 完全一致；
   Hanning 窗 h[i] 与 Python 一致；FFT 输入 re[] 一致。
3. 对比两处 FFT 实现文本 → **发现差异在声明**：

   - `speech_feat.c`: `int i, j, k, len, step;`  ← step 是 int
   - `test_fft.c`:   `float step = cwr * wr - cwi * wi;`

   蝶形内 `step = cwr*wr - cwi*wi; cwr = step;` 中 `step` 为 `int`，
   旋转因子被截断为 0/±1 → 每级蝶形 twiddle 全部错误 → FFT 输出面目全非。
   -O0/-O2 结果一致，进一步佐证与编译优化无关。

## 修复
```c
int i, j, k, len;
float t, step;
```

## 验证
- C 特征与 Python（float32 训练管线）最大绝对差 ~4e-6（float32 舍入级）。
- 主机 TFLM 端到端 val 准确率：17.6% → **81.9%**（354/432，val 为整嗓音留出 6/7 号）。
  剩余错误集中在留出嗓音 7 的少数命令（类 2/5/8/10 被拒为 unknown），属泛化问题，
  可通过增加 TTS 嗓音/增强重训改善，不影响流水线正确性。
- 全量固件重新构建通过：`out/sifli_sf32lb52_devkit_lcd_nsh/nuttx.bin`（7,809,628 B）。

## 顺带修复
- `voice_question.c` `build_window()`：环形缓冲（2s）按线性索引拷贝，
  越过 wrap 点会越界读；改为取模索引。
- `cmd_asr.cxx` 注释 12→11 类。

## 待办
- 真机烧录（USB stub 下载超时，需物理重插后烧录）→ 验证 16kHz 采集 + VAD + ASR。

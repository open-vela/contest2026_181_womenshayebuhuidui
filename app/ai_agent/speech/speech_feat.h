/****************************************************************************
 * speech_feat.h - 端侧语音特征提取 (log-mel 频谱)
 *
 * 与训练侧 train_cmd_model.py 完全一致:
 *   16kHz int16 PCM -> 1.0s -> 25ms/10ms 帧 -> 512FFT -> 40 mel -> log
 *   -> 取偶帧 (96->48) -> 全局归一化 (mean/std 烘焙常量)
 *
 * Team 181 - Contest 2026
 ****************************************************************************/

#ifndef SPEECH_FEAT_H
#define SPEECH_FEAT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SPEECH_SR        16000
#define SPEECH_WIN_LEN   400      /* 25ms */
#define SPEECH_HOP       160      /* 10ms */
#define SPEECH_FFT_N     512
#define SPEECH_N_MEL     40
#define SPEECH_N_FRAMES  48       /* 96 帧取偶帧 */
#define SPEECH_FRAME_STRIDE 2     /* 96 -> 48 */
#define SPEECH_BUF_LEN   SPEECH_SR /* 1s 窗口 */

/**
 * 从 1 秒 PCM 提取归一化 log-mel 特征。
 *
 * @param pcm     int16 PCM (>= SPEECH_BUF_LEN 样本)
 * @param n       pcm 样本数 (不足补零)
 * @param feats   输出 (SPEECH_N_FRAMES * SPEECH_N_MEL float, 行优先)
 * @return 0 成功
 */
int speech_feats_from_pcm(const int16_t *pcm, int n, float *feats);

#ifdef __cplusplus
}
#endif

#endif /* SPEECH_FEAT_H */

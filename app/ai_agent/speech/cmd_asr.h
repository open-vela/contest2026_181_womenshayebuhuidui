/****************************************************************************
 * cmd_asr.h - 端侧命令词识别 (TFLite Micro)
 *
 * 输入: 1 秒 16kHz PCM (int16) -> log-mel (48x40) -> 小 CNN -> 命令类别
 * 类别表: cmd_table.h (类 0 = unknown, 1..11 为命令)
 *
 * Team 181 - Contest 2026
 ****************************************************************************/

#ifndef SPEECH_CMD_ASR_H
#define SPEECH_CMD_ASR_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 初始化命令识别解释器 (加载模型 + arena)。
 * @return 0 成功, -1 失败
 */
int cmd_asr_init(void);

/**
 * 识别 1 秒语音。
 *
 * @param pcm     int16 PCM (>= 16000 样本)
 * @param n       pcm 样本数
 * @param score   输出: 识别置信度 (0..1, 可 NULL)
 * @return 命令类别 id (0 = unknown/无命令), -1 失败
 */
int cmd_asr_recognize(const int16_t *pcm, int n, float *score);

/**
 * 类别 id -> 用户文本 (用于本地 LM prompt)。
 */
const char *cmd_asr_text(int class_id);

#ifdef __cplusplus
}
#endif

#endif /* SPEECH_CMD_ASR_H */

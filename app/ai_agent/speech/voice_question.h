/****************************************************************************
 * voice_question.h - 语音提问 -> 本地模型文字回答
 *
 * 流程: 录音(自动 VAD 检测语音起止) -> 命令词识别 -> velaAI 本地 LM
 *       -> 回复文本
 *
 * Team 181 - Contest 2026
 ****************************************************************************/

#ifndef SPEECH_VOICE_QUESTION_H
#define SPEECH_VOICE_QUESTION_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 录一段语音并生成文字回答 (阻塞, 最长 MAX_SEC 秒)。
 *
 * @param reply       输出回复文本
 * @param reply_size  reply 容量
 * @param max_sec     最长录音秒数
 * @return 0 成功, 1 未识别到命令 (reply 为提示语), 负值失败
 */
int voice_question_ask(char *reply, int reply_size, int max_sec);

#ifdef __cplusplus
}
#endif

#endif /* SPEECH_VOICE_QUESTION_H */

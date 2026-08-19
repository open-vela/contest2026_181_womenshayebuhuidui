/****************************************************************************
 * chat_log.h - 对话日志 (RAM 环形缓冲 + /data/ai_chat.log 持久化)
 *
 * 记录每次问答 (来源: 语音 / 控制台 / 闲聊演示), 宠物页"日志"按钮查看。
 * 重启后从文件恢复最近 CHAT_LOG_MAX 条。
 *
 * Team 181 - Contest 2026
 ****************************************************************************/

#ifndef AI_AGENT_CHAT_LOG_H
#define AI_AGENT_CHAT_LOG_H

#include <stddef.h>

#define CHAT_LOG_ASK     0       /* 问 */
#define CHAT_LOG_ANSWER  1       /* 答 */

/* 启动时调用: 从 /data/ai_chat.log 恢复最近记录 (文件缺失/未挂载则忽略) */
void chat_log_init(void);

/* 追加一条: is_answer=CHAT_LOG_ASK/ANSWER, text 为 UTF-8 文本 */
void chat_log_add(int is_answer, const char *text);

/* 当前条数 (<= CHAT_LOG_MAX) */
int chat_log_count(void);

/* 取第 idx 条 (0 = 最新), 返回 0 成功, -1 越界 */
int chat_log_get(int idx, char *buf, int bufsize);

#endif /* AI_AGENT_CHAT_LOG_H */

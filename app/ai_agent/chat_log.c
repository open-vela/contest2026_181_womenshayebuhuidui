/****************************************************************************
 * chat_log.c - 对话日志实现 (RAM 环 + /data/ai_chat.log)
 *
 * 线程安全: 互斥锁保护; 条目为整行文本 "HH:MM 问|答 文本"。
 *
 * Team 181 - Contest 2026
 ****************************************************************************/

#include <nuttx/config.h>

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "chat_log.h"

#define CHAT_LOG_MAX    24                    /* 内存保留条数 */
#define CHAT_LOG_LINE   176                   /* 单行缓冲 (含时间与问/答前缀) */
#define CHAT_LOG_PATH   "/data/ai_chat.log"   /* littlefs 持久化 */

typedef struct
{
  char line[CHAT_LOG_LINE];
} chat_line_t;

static chat_line_t s_ring[CHAT_LOG_MAX];      /* [0] = 最新 */
static int s_count;
static pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER;

/* 压入环形缓冲 (最新在最前, memmove 滚动 — 低频事件, 开销可忽略) */
static void ring_push(const char *line)
{
  memmove(&s_ring[1], &s_ring[0],
          sizeof(chat_line_t) * (CHAT_LOG_MAX - 1));
  snprintf(s_ring[0].line, CHAT_LOG_LINE, "%s", line);

  if (s_count < CHAT_LOG_MAX)
    {
      s_count++;
    }
}

static void file_append(const char *line)
{
  FILE *f = fopen(CHAT_LOG_PATH, "a");

  if (f == NULL)
    {
      return;   /* /data 未挂载等情况: 静默, 仅内存记录 */
    }

  fprintf(f, "%s\n", line);
  fclose(f);
}

void chat_log_init(void)
{
  FILE *f = fopen(CHAT_LOG_PATH, "r");
  char buf[CHAT_LOG_LINE];

  if (f == NULL)
    {
      return;
    }

  pthread_mutex_lock(&s_lock);

  while (fgets(buf, sizeof(buf), f) != NULL)
    {
      buf[strcspn(buf, "\n")] = '\0';

      if (buf[0] != '\0')
        {
          ring_push(buf);
        }
    }

  pthread_mutex_unlock(&s_lock);
  fclose(f);
}

void chat_log_add(int is_answer, const char *text)
{
  char line[CHAT_LOG_LINE];
  time_t now = time(NULL);
  struct tm tmv;

  if (text == NULL || text[0] == '\0')
    {
      return;
    }

  localtime_r(&now, &tmv);
  snprintf(line, sizeof(line), "%02d:%02d %s %.*s",
           tmv.tm_hour, tmv.tm_min,
           is_answer == CHAT_LOG_ANSWER ? "答" : "问",
           CHAT_LOG_LINE - 16, text);

  pthread_mutex_lock(&s_lock);
  ring_push(line);
  pthread_mutex_unlock(&s_lock);

  file_append(line);
}

int chat_log_count(void)
{
  int n;

  pthread_mutex_lock(&s_lock);
  n = s_count;
  pthread_mutex_unlock(&s_lock);

  return n;
}

int chat_log_get(int idx, char *buf, int bufsize)
{
  int ret = -1;

  if (buf == NULL || bufsize <= 0)
    {
      return -1;
    }

  pthread_mutex_lock(&s_lock);

  if (idx >= 0 && idx < s_count)
    {
      snprintf(buf, bufsize, "%s", s_ring[idx].line);
      ret = 0;
    }

  pthread_mutex_unlock(&s_lock);

  return ret;
}

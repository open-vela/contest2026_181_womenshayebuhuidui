/****************************************************************************
 * voice_question.c - 语音提问 -> 本地模型文字回答
 *
 * 录音流程:
 *   1. mic_capture_start()
 *   2. 以 20ms 帧能量做 VAD: 检测到语音起点后, 收集到 1s 窗口;
 *      语音结束后 400ms 静音或达到 max_sec 则停止
 *   3. cmd_asr_recognize() -> 命令类别
 *   4. 类别 -> 用户文本 -> ai_lm_reply() -> 回复
 *   5. mic_capture_stop()
 *
 * Team 181 - Contest 2026
 ****************************************************************************/

#include <stdio.h>
#include <string.h>

#include "voice_question.h"
#include "mic_capture.h"
#include "cmd_asr.h"
#include "../ai_lm.h"

#define VAD_FRAME        320        /* 20ms @16k */
#define VAD_THRESHOLD    900.0f     /* 帧能量阈值 (int16 平方和) */
#define VAD_TAIL_FRAMES  20         /* 语音结束后 400ms */
#define PCM_WINDOW       16000      /* 1s 识别窗口 */

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static float frame_energy(const int16_t *pcm, int n)
{
  float e = 0.0f;
  int i;

  for (i = 0; i < n; i++)
    {
      e += (float)pcm[i] * (float)pcm[i];
    }

  return e / (float)n;
}

/* 从环形缓冲中取一段语音: 返回语音起点前 0.2s 起的 1s 窗口 */
static int build_window(const int16_t *ring, int ring_len,
                        int onset, int speech_len, int16_t *out)
{
  int start = onset - 3200;             /* 起点前 0.2s */
  int i;

  if (start < 0)
    {
      start = 0;
    }

  /* 环形缓冲: 数据可能跨越 wrap 点, 用取模索引线性拷贝 */
  for (i = 0; i < PCM_WINDOW; i++)
    {
      out[i] = ring[(start + i) % ring_len];
    }

  (void)speech_len;
  return PCM_WINDOW;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int voice_question_ask(char *reply, int reply_size, int max_sec)
{
  static int16_t ring[PCM_WINDOW * 2];  /* 环形缓冲 (最多 2s) */
  int ring_pos = 0;
  int onset = -1;
  int tail = 0;
  int speech_len = 0;
  int total_frames = max_sec * MIC_CAPTURE_RATE / VAD_FRAME;
  int16_t pcm[VAD_FRAME];
  int16_t window[PCM_WINDOW];
  float score = 0.0f;
  int class_id;
  const char *cmd_text;
  int ret;
  int i;

  if (reply == NULL || reply_size <= 0)
    {
      return -1;
    }

  reply[0] = '\0';

  if (cmd_asr_init() != 0)
    {
      snprintf(reply, reply_size, "语音识别初始化失败");
      return -1;
    }

  if (ai_lm_init() != 0)
    {
      snprintf(reply, reply_size, "本地模型初始化失败");
      return -1;
    }

  if (mic_capture_start() != 0)
    {
      snprintf(reply, reply_size, "麦克风不可用");
      return -1;
    }

  printf("[voice] listening... (max %ds)\n", max_sec);

  for (i = 0; i < total_frames; i++)
    {
      int n = mic_capture_read(pcm, sizeof(pcm), 500);

      if (n <= 0)
        {
          continue;   /* 超时: 继续等 */
        }

      /* 写入环形缓冲 */
      for (int k = 0; k < n / 2; k++)
        {
          ring[ring_pos] = pcm[k];
          ring_pos = (ring_pos + 1) % (PCM_WINDOW * 2);
        }

      float e = frame_energy(pcm, n / 2);

      if (onset < 0)
        {
          if (e > VAD_THRESHOLD)
            {
              onset = (ring_pos - n / 2 + PCM_WINDOW * 2) % (PCM_WINDOW * 2);
              speech_len = n / 2;
              printf("[voice] speech onset (e=%.0f)\n", e);
            }
        }
      else
        {
          speech_len += n / 2;

          if (e > VAD_THRESHOLD)
            {
              tail = 0;
            }
          else if (++tail >= VAD_TAIL_FRAMES)
            {
              break;   /* 语音结束 */
            }
        }
    }

  if (onset < 0)
    {
      printf("[voice] no speech detected\n");
      snprintf(reply, reply_size, "没有听到声音，请再说一遍");
      mic_capture_stop();
      return 1;
    }

  build_window(ring, PCM_WINDOW * 2, onset, speech_len, window);

  class_id = cmd_asr_recognize(window, PCM_WINDOW, &score);
  if (class_id <= 0)
    {
      printf("[voice] no command recognized (score=%.2f)\n", score);
      snprintf(reply, reply_size, "没听清，请再说一遍");
      mic_capture_stop();
      return 1;
    }

  cmd_text = cmd_asr_text(class_id);
  printf("[voice] command %d: %s (score=%.2f)\n", class_id,
         cmd_text ? cmd_text : "?", score);

  if (cmd_text != NULL && cmd_text[0] != '\0')
    {
      ret = ai_lm_reply(cmd_text, reply, reply_size);
      if (ret == 0)
        {
          mic_capture_stop();
          return 0;
        }

      snprintf(reply, reply_size, "%s", cmd_text);
    }
  else
    {
      snprintf(reply, reply_size, "没听清，请再说一遍");
    }

  mic_capture_stop();
  return 1;
}

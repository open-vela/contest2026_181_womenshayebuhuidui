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
#include "../chat_log.h"

#define VAD_FRAME        320        /* 20ms @16k */
#define VAD_TAIL_FRAMES  20         /* 语音结束后 400ms */
#define PCM_WINDOW       16000      /* 1s 识别窗口 */

/* VAD 自适应阈值: 前 0.4s 估环境底噪 (去直流能量), 阈值 = 底噪 x 16,
 * 下限 3000。固定 900 的旧阈值会被 ~±300 直流偏置的环境音恒超
 * (310^2≈96100), 造成假 onset + 监听跑满 4 秒 (真机实测, 08-16 修) */
#define VAD_ADAPT_FRAMES 20         /* 前 0.4s 估底噪 */
#define VAD_NOISE_MULT   16.0f
#define VAD_THRESHOLD_MIN 3000.0f

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/* 帧能量: 先去直流 (减帧均值) 再求方差 — 麦克风信号带 ~±300 直流
 * 偏置, 直接求平方和会把偏置算成恒定"语音能量" */
static float frame_energy(const int16_t *pcm, int n)
{
  float mean = 0.0f;
  float e = 0.0f;
  int i;

  for (i = 0; i < n; i++)
    {
      mean += (float)pcm[i];
    }

  mean /= (float)n;

  for (i = 0; i < n; i++)
    {
      float d = (float)pcm[i] - mean;

      e += d * d;
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
  /* 大缓冲一律 static: 本函数会被 16KB 栈的桌宠 worker 线程调用,
   * window 若放栈上 (32KB) 必然溢出 (真机踩踏致 UI 卡死, 08-16 修) */
  static int16_t ring[PCM_WINDOW * 2];  /* 环形缓冲 (最多 2s) */
  static int16_t window[PCM_WINDOW];    /* ASR 识别窗口 (32KB) */
  int ring_pos = 0;
  int onset = -1;
  int tail = 0;
  int speech_len = 0;
  int total_frames = max_sec * MIC_CAPTURE_RATE / VAD_FRAME;
  int16_t pcm[VAD_FRAME];
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

  printf("[voice] asr init ok\n");

  if (ai_lm_init() != 0)
    {
      snprintf(reply, reply_size, "本地模型初始化失败");
      return -1;
    }

  printf("[voice] lm init ok\n");

  if (mic_capture_start() != 0)
    {
      snprintf(reply, reply_size, "麦克风不可用");
      return -1;
    }

  /* 冲刷上一会话残留 (停止瞬态/未消费缓冲), 丢弃 0.3s 后再开始判定,
   * 否则残留满幅样本会令底噪=0 且瞬间假触发 onset (真机 08-16) */
  mic_capture_flush(300);

  printf("[voice] mic ok\n");

  printf("[voice] listening... (max %ds)\n", max_sec);

  int dbg_reads = 0;
  int dead_reads = 0;   /* 开场连续完全超时计数 (DMA 未重启检测) */
  float noise_sum = 0.0f;
  int noise_n = 0;
  float vad_thr = VAD_THRESHOLD_MIN;

  for (i = 0; i < total_frames; i++)
    {
      /* UI 打断 (离开页面/再次点击): 立即结束监听释放 CPU */
      if (ai_lm_cancel_check())
        {
          printf("[voice] cancelled\n");
          mic_capture_stop();
          return -1;
        }

      int n = mic_capture_read(pcm, sizeof(pcm), 500);

      if (n <= 0)
        {
          /* 无数据保护 (全程): 连续 4 次完全超时 (~2s) 说明采集管线
           * 已死。onset 前直接放弃走降级; onset 后视为语音结束
           * (窗口已有语音, 截断保护), 都不会挂满 200x500ms */
          if (++dead_reads >= 4)
            {
              if (onset < 0)
                {
                  printf("[voice] no mic data, bail out\n");
                  mic_capture_stop();
                  return -1;
                }

              printf("[voice] data lost after onset, treat as end\n");
              break;
            }

          continue;   /* 超时: 继续等 */
        }

      dead_reads = 0;

      /* 诊断: 前几次读取的用户侧数据 */
      if (dbg_reads < 3)
        {
          int mx = 0;

          for (int k = 0; k < n / 2; k++)
            {
              int a = pcm[k] < 0 ? -pcm[k] : pcm[k];
              if (a > mx)
                {
                  mx = a;
                }
            }

          printf("[voice-dbg] read#%d n=%d max=%d: %04x %04x %04x %04x\n",
                 i, n, mx,
                 (uint16_t)pcm[0], (uint16_t)pcm[1],
                 (uint16_t)pcm[2], (uint16_t)pcm[3]);
          dbg_reads++;
        }

      /* 写入环形缓冲 */
      for (int k = 0; k < n / 2; k++)
        {
          ring[ring_pos] = pcm[k];
          ring_pos = (ring_pos + 1) % (PCM_WINDOW * 2);
        }

      float e = frame_energy(pcm, n / 2);

      /* 适应期: 前 0.4s 估环境底噪 (不判定 onset) */
      if (noise_n < VAD_ADAPT_FRAMES)
        {
          noise_sum += e;
          noise_n++;

          if (noise_n == VAD_ADAPT_FRAMES)
            {
              float avg = noise_sum / (float)noise_n;

              vad_thr = avg * VAD_NOISE_MULT;

              if (vad_thr < VAD_THRESHOLD_MIN)
                {
                  vad_thr = VAD_THRESHOLD_MIN;
                }

              /* 上限封顶: 用户在适应期就开口会把底噪均值抬高几个
               * 数量级 (真机实测 noise=4.5e7 -> 阈值 7.1e8, 比真实
               * 语音能量还高, onset 永远无法触发) */
              if (vad_thr > 5.0e7f)
                {
                  vad_thr = 5.0e7f;
                }

              printf("[voice] vad threshold=%.0f (noise=%.0f)\n",
                     vad_thr, avg);
            }

          continue;
        }

      if (onset < 0)
        {
          if (e > vad_thr)
            {
              onset = (ring_pos - n / 2 + PCM_WINDOW * 2) % (PCM_WINDOW * 2);
              speech_len = n / 2;
              printf("[voice] speech onset (e=%.0f)\n", e);
            }
        }
      else
        {
          speech_len += n / 2;

          if (e > vad_thr)
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

  /* 记对话日志: 识别到的问句 */
  if (cmd_text != NULL && cmd_text[0] != '\0')
    {
      chat_log_add(CHAT_LOG_ASK, cmd_text);
    }

  if (cmd_text != NULL && cmd_text[0] != '\0')
    {
      /* agent 模式: <call> 生成 -> 端侧执行器 -> <obs> -> <bot> 回复 */
      ret = ai_lm_agent_reply(cmd_text, reply, reply_size);
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

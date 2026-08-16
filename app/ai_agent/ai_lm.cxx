/****************************************************************************
 * ai_lm.cxx - velaAI V3 端侧语言模型推理 (TFLite Micro, 16x8 量化)
 *
 * 模型: model/velaai_lm_v3_16x8.tflite
 *   - 训练: /home/aila/projects/velaAI (train_gpu_v3.py, val acc 0.8705)
 *   - 导出: export_tflite_v3.py (LSTM 单步 unroll, 多IO显式状态)
 *
 * Tensor 布局 (host 侧与 Keras 参考逐项校验):
 *   输入: [0]=token int32(1,1)  [1]=h_in f32(1,256)  [2]=c_in f32(1,256)
 *   输出: [0]=c_out f32(1,256)  [1]=logits f32(1,5000)  [2]=h_out f32(1,256)
 *
 * 生成: 逐 token 自回归 argmax, 遇 <eos>(2) 早停; 回复取最后一个 <bot> 之后文本。
 *
 * Team 181 - Contest 2026
 ****************************************************************************/

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>

#include "ai_lm.h"
#include "tokenizer.h"
#include "model/model_data.h"

#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Tensor Arena: 主机侧 RecordingMicroInterpreter 实测 37840 字节,
 * 留足余量取 64KB (静态 BSS; 若 SRAM 紧张可改为 PSRAM malloc) */
#ifndef AI_LM_ARENA_SIZE
#define AI_LM_ARENA_SIZE (64 * 1024)
#endif

#define AI_LM_MAX_PROMPT_CHARS  96    /* 用户输入上限 (UTF-8 字节) */
#define AI_LM_PROMPT_MAX_TOKENS 96    /* prompt token 上限 */

/* 输入张量索引 (与导出模型一致, host 校验过) */
#define IN_TOKEN 0
#define IN_H     1
#define IN_C     2

/* 输出张量索引: [0]=c_out [1]=logits [2]=h_out */
#define OUT_C     0
#define OUT_LOGIT 1
#define OUT_H     2

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* 模型字节 (RODATA, XIP 直接从 Flash 执行, 16 字节对齐; 定义见 model_data.h) */

/* Tensor Arena (BSS; 若 SRAM 紧张可改为 PSRAM malloc) */
static uint8_t s_arena[AI_LM_ARENA_SIZE] __attribute__((aligned(16)));

static tflite::MicroInterpreter *s_interp = NULL;
static tflite::MicroMutableOpResolver<12> s_resolver;
static pthread_mutex_t s_lm_mutex = PTHREAD_MUTEX_INITIALIZER;

/* 自回归状态 (跨 step 保持, 由锁保护) */
static float s_c[AI_LM_LSTM_UNITS];
static float s_h[AI_LM_LSTM_UNITS];

/* 回复文本暂存 (避免大数组在栈上) */
static char s_reply_scratch[AI_LM_REPLY_MAX + 1];

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/* 在输出 logits 张量上做 argmax (数据留在 arena 内, 不拷贝) */
static int32_t lm_argmax(const TfLiteTensor *logits)
{
  int32_t best = 0;
  float bestv = logits->data.f[0];

  for (int i = 1; i < logits->dims->data[1]; i++)
    {
      if (logits->data.f[i] > bestv)
        {
          bestv = logits->data.f[i];
          best = i;
        }
    }

  return best;
}

/* 单步推理: 喂 token + 当前状态, 更新 s_c/s_h, 返回 logits 张量 */
static const TfLiteTensor *lm_step(int32_t token)
{
  TfLiteTensor *in_tok = s_interp->input(IN_TOKEN);
  TfLiteTensor *in_h = s_interp->input(IN_H);
  TfLiteTensor *in_c = s_interp->input(IN_C);

  in_tok->data.i32[0] = token;
  memcpy(in_h->data.f, s_h, sizeof(s_h));
  memcpy(in_c->data.f, s_c, sizeof(s_c));

  if (s_interp->Invoke() != kTfLiteOk)
    {
      return NULL;
    }

  memcpy(s_c, s_interp->output(OUT_C)->data.f, sizeof(s_c));
  memcpy(s_h, s_interp->output(OUT_H)->data.f, sizeof(s_h));

  return s_interp->output(OUT_LOGIT);
}

/* 去掉字符串首尾空白 */
static void reply_trim(char *s)
{
  char *start = s;
  char *end;

  while (*start == ' ' || *start == '\t' || *start == '\n' || *start == '\r')
    {
      start++;
    }

  end = start + strlen(start);
  while (end > start &&
         (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\n' ||
          end[-1] == '\r'))
    {
      *--end = '\0';
    }

  if (start != s)
    {
      memmove(s, start, strlen(start) + 1);
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int ai_lm_init(void)
{
  const tflite::Model *model;

  pthread_mutex_lock(&s_lm_mutex);

  if (s_interp != NULL)
    {
      pthread_mutex_unlock(&s_lm_mutex);
      return 0;
    }

  model = tflite::GetModel(g_velaai_model);
  if (model == NULL || model->version() != TFLITE_SCHEMA_VERSION)
    {
      pthread_mutex_unlock(&s_lm_mutex);
      return -1;
    }

  /* 注册模型所需的全部算子 (与 host 侧 op 列表一致) */
  s_resolver.AddGather();
  s_resolver.AddFullyConnected();
  s_resolver.AddSoftmax();
  s_resolver.AddSplit();
  s_resolver.AddUnpack();
  s_resolver.AddTanh();
  s_resolver.AddLogistic();
  s_resolver.AddMul();
  s_resolver.AddAdd();
  s_resolver.AddQuantize();
  s_resolver.AddDequantize();

  static tflite::MicroInterpreter interpreter(model, s_resolver, s_arena,
                                              AI_LM_ARENA_SIZE);
  if (interpreter.AllocateTensors() != kTfLiteOk)
    {
      pthread_mutex_unlock(&s_lm_mutex);
      return -1;
    }

  s_interp = &interpreter;
  memset(s_c, 0, sizeof(s_c));
  memset(s_h, 0, sizeof(s_h));

  pthread_mutex_unlock(&s_lm_mutex);
  return 0;
}

int ai_lm_generate(const int32_t *prompt, int n_prompt,
                   int32_t *out_tokens, int max_new, int *out_len)
{
  const TfLiteTensor *logits;
  int32_t next;
  int produced = 0;

  if (out_len != NULL)
    {
      *out_len = 0;
    }

  if (s_interp == NULL || prompt == NULL || out_tokens == NULL ||
      n_prompt < 0 || max_new <= 0)
    {
      return -1;
    }

  pthread_mutex_lock(&s_lm_mutex);

  /* 每次生成从零状态开始 (状态只在单次生成内部传递) */
  memset(s_c, 0, sizeof(s_c));
  memset(s_h, 0, sizeof(s_h));

  /* 1) 编码阶段: 逐 token 走 prompt, 只累积状态 */
  for (int i = 0; i < n_prompt; i++)
    {
      logits = lm_step(prompt[i]);
      if (logits == NULL)
        {
          pthread_mutex_unlock(&s_lm_mutex);
          return -1;
        }
    }

  /* 2) 解码阶段: argmax 自回归 */
  for (int step = 0; step < max_new; step++)
    {
      next = lm_argmax(logits);

      if (next == AI_LM_EOS_ID)
        {
          break;
        }

      out_tokens[produced++] = next;

      logits = lm_step(next);
      if (logits == NULL)
        {
          break;
        }
    }

  pthread_mutex_unlock(&s_lm_mutex);

  if (out_len != NULL)
    {
      *out_len = produced;
    }

  return 0;
}

int ai_lm_reply(const char *user_text, char *reply, int reply_size)
{
  char prompt[AI_LM_MAX_PROMPT_CHARS + 32];
  int32_t prompt_ids[AI_LM_PROMPT_MAX_TOKENS];
  int32_t gen_ids[AI_LM_MAX_NEW];
  int n_prompt;
  int n_gen;
  int n_dec;
  char *bot;

  if (reply == NULL || reply_size <= 0)
    {
      return -1;
    }

  reply[0] = '\0';

  if (user_text == NULL || user_text[0] == '\0')
    {
      return -1;
    }

  /* 1) 构造 prompt: "<usr> {text} <bot>" (与训练数据格式一致) */
  snprintf(prompt, sizeof(prompt), "<usr> %.*s <bot>",
           AI_LM_MAX_PROMPT_CHARS, user_text);

  /* 2) 分词 */
  n_prompt = sp_tokenize(prompt, prompt_ids, AI_LM_PROMPT_MAX_TOKENS);
  if (n_prompt <= 0)
    {
      return -1;
    }

  /* 3) 自回归生成 */
  if (ai_lm_generate(prompt_ids, n_prompt, gen_ids, AI_LM_MAX_NEW, &n_gen) != 0)
    {
      return -1;
    }

  if (n_gen <= 0)
    {
      return -1;
    }

  /* 4) 解码 */
  n_dec = sp_detokenize(gen_ids, n_gen, s_reply_scratch,
                        sizeof(s_reply_scratch));
  if (n_dec <= 0)
    {
      return -1;
    }

  /* 5) 提取最后一个 <bot> 之后的文本 */
  bot = strstr(s_reply_scratch, "<bot>");
  if (bot != NULL)
    {
      bot += 5;
      /* 跳过 bot 前缀空白 */
      while (*bot == ' ')
        {
          bot++;
        }

      if (*bot == '\0')
        {
          return -1;
        }

      strncpy(reply, bot, reply_size - 1);
      reply[reply_size - 1] = '\0';
    }
  else
    {
      /* 无 <bot>: 若是工具调用残渣则视为失败, 否则直接展示 */
      if (strstr(s_reply_scratch, "<call>") != NULL ||
          strstr(s_reply_scratch, "<obs>") != NULL)
        {
          return -1;
        }

      strncpy(reply, s_reply_scratch, reply_size - 1);
      reply[reply_size - 1] = '\0';
    }

  reply_trim(reply);

  return (reply[0] != '\0') ? 0 : -1;
}

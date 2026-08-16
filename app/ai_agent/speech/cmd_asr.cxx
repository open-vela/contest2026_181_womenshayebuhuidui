/****************************************************************************
 * cmd_asr.c - 端侧命令词识别 (TFLite Micro, int8 CNN)
 *
 * 模型: cmd_asr_model.tflite (train_cmd_model.py 训练导出)
 * 输入: (1, 48, 40, 1) float32 log-mel
 * 输出: (1, CMD_ASR_N_CLASSES) float32 softmax 概率
 *
 * Team 181 - Contest 2026
 ****************************************************************************/

#include <string.h>
#include <pthread.h>

#include "cmd_asr.h"
#include "speech_feat.h"
#include "cmd_table.h"
#include "model_data.h"

#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#ifndef CMD_ASR_ARENA_SIZE
#define CMD_ASR_ARENA_SIZE (64 * 1024)
#endif

/* 置信度阈值: 低于此值视为 unknown (拒绝) */
#define CMD_ASR_CONF_THRESHOLD 0.55f

/****************************************************************************
 * Private Data
 ****************************************************************************/

static uint8_t s_arena[CMD_ASR_ARENA_SIZE] __attribute__((aligned(16)));
static tflite::MicroInterpreter *s_interp = NULL;
static tflite::MicroMutableOpResolver<12> s_resolver;
static pthread_mutex_t s_asr_mutex = PTHREAD_MUTEX_INITIALIZER;

static float s_feats[SPEECH_N_FRAMES * SPEECH_N_MEL];

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int cmd_asr_init(void)
{
  const tflite::Model *model;

  pthread_mutex_lock(&s_asr_mutex);

  if (s_interp != NULL)
    {
      pthread_mutex_unlock(&s_asr_mutex);
      return 0;
    }

  model = tflite::GetModel(g_cmd_asr_model);
  if (model == NULL || model->version() != TFLITE_SCHEMA_VERSION)
    {
      pthread_mutex_unlock(&s_asr_mutex);
      return -1;
    }

  s_resolver.AddConv2D();
  s_resolver.AddMaxPool2D();
  s_resolver.AddRelu();
  s_resolver.AddFullyConnected();
  s_resolver.AddSoftmax();
  s_resolver.AddQuantize();
  s_resolver.AddDequantize();
  s_resolver.AddReshape();
  s_resolver.AddPad();
  s_resolver.AddMul();
  s_resolver.AddAdd();

  static tflite::MicroInterpreter interpreter(model, s_resolver, s_arena,
                                              CMD_ASR_ARENA_SIZE);
  if (interpreter.AllocateTensors() != kTfLiteOk)
    {
      pthread_mutex_unlock(&s_asr_mutex);
      return -1;
    }

  s_interp = &interpreter;
  pthread_mutex_unlock(&s_asr_mutex);
  return 0;
}

int cmd_asr_recognize(const int16_t *pcm, int n, float *score)
{
  int best = 0;
  float bestv = -1.0f;

  if (score != NULL)
    {
      *score = 0.0f;
    }

  if (s_interp == NULL || pcm == NULL || n <= 0)
    {
      return -1;
    }

  pthread_mutex_lock(&s_asr_mutex);

  /* 特征提取 (48x40 归一化 log-mel) */
  if (speech_feats_from_pcm(pcm, n, s_feats) != 0)
    {
      pthread_mutex_unlock(&s_asr_mutex);
      return -1;
    }

  /* 填输入 */
  TfLiteTensor *in = s_interp->input(0);
  memcpy(in->data.f, s_feats, sizeof(s_feats));

  if (s_interp->Invoke() != kTfLiteOk)
    {
      pthread_mutex_unlock(&s_asr_mutex);
      return -1;
    }

  /* argmax (类 0 是 unknown, 允许选中; 置信度过滤在调用方/此处) */
  const TfLiteTensor *out = s_interp->output(0);
  for (int i = 0; i < CMD_ASR_N_CLASSES; i++)
    {
      if (out->data.f[i] > bestv)
        {
          bestv = out->data.f[i];
          best = i;
        }
    }

  pthread_mutex_unlock(&s_asr_mutex);

  if (score != NULL)
    {
      *score = bestv;
    }

  /* 置信度不足或 unknown -> 拒绝 */
  if (best == 0 || bestv < CMD_ASR_CONF_THRESHOLD)
    {
      return 0;
    }

  return best;
}

const char *cmd_asr_text(int class_id)
{
  if (class_id < 0 || class_id >= CMD_ASR_N_CLASSES)
    {
      return NULL;
    }

  return g_cmd_texts[class_id];
}

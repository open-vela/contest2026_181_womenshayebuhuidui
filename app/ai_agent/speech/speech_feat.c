/****************************************************************************
 * speech_feat.c - 端侧语音特征提取 (log-mel 频谱)
 *
 * 实现要点:
 *   - 512 点基 2 FFT (就地, 浮点)
 *   - mel 滤波矩阵来自 mel_table.h (训练脚本生成)
 *   - 全局归一化均值/标准差来自 feat_stats.h (训练脚本生成)
 *   所有参数与 train_cmd_model.py 一致。
 *
 * Team 181 - Contest 2026
 ****************************************************************************/

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "speech_feat.h"
#include "mel_table.h"
#include "feat_stats.h"

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/* 512 点基 2 FFT (Cooley-Tukey, 就地, re/im 分离) */
static void fft_radix2(float *re, float *im, int n)
{
  int i, j, k, len;
  float t, step;

  /* 位反转置换 */
  for (i = 1, j = 0; i < n; i++)
    {
      int bit = n >> 1;

      for (; j & bit; bit >>= 1)
        {
          j ^= bit;
        }

      j ^= bit;
      if (i < j)
        {
          t = re[i]; re[i] = re[j]; re[j] = t;
          t = im[i]; im[i] = im[j]; im[j] = t;
        }
    }

  /* 蝶形运算 */
  for (len = 2; len <= n; len <<= 1)
    {
      float wang = -2.0f * (float)M_PI / (float)len;
      float wr = cosf(wang);
      float wi = sinf(wang);

      for (i = 0; i < n; i += len)
        {
          float cwr = 1.0f;
          float cwi = 0.0f;

          for (k = 0; k < len / 2; k++)
            {
              int a = i + k;
              int b = i + k + len / 2;
              float tr = cwr * re[b] - cwi * im[b];
              float ti = cwr * im[b] + cwi * re[b];

              re[b] = re[a] - tr;
              im[b] = im[a] - ti;
              re[a] = re[a] + tr;
              im[a] = im[a] + ti;

              step = cwr * wr - cwi * wi;
              cwi = cwr * wi + cwi * wr;
              cwr = step;
            }
        }
    }
}

/* 一个 25ms 帧 -> log-mel 40 维 (归一化前) */
static void frame_logmel(const float *w, float *out)
{
  float re[SPEECH_FFT_N];
  float im[SPEECH_FFT_N];
  int i, m;

  for (i = 0; i < SPEECH_FFT_N; i++)
    {
      if (i < SPEECH_WIN_LEN)
        {
          /* Hanning 窗 */
          float h = 0.5f - 0.5f * cosf(2.0f * (float)M_PI * i /
                                       (SPEECH_WIN_LEN - 1));
          re[i] = w[i] * h;
        }
      else
        {
          re[i] = 0.0f;
        }

      im[i] = 0.0f;
    }

  fft_radix2(re, im, SPEECH_FFT_N);

  for (m = 0; m < SPEECH_N_MEL; m++)
    {
      float acc = 0.0f;

      for (i = 0; i < MEL_FFT_BINS; i++)
        {
          float power = re[i] * re[i] + im[i] * im[i];
          acc += g_mel_fbank[m][i] * power;
        }

      out[m] = logf(acc + 1e-10f);
    }

}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int speech_feats_from_pcm(const int16_t *pcm, int n, float *feats)
{
  float frame[SPEECH_WIN_LEN];
  float mel[SPEECH_N_MEL];
  int i, f;

  if (pcm == NULL || feats == NULL)
    {
      return -1;
    }

  for (f = 0; f < SPEECH_N_FRAMES; f++)
    {
      int raw = f * SPEECH_FRAME_STRIDE;   /* 96 -> 48 取偶帧 */
      int s = raw * SPEECH_HOP;

      for (i = 0; i < SPEECH_WIN_LEN; i++)
        {
          int idx = s + i;

          if (idx < n)
            {
              frame[i] = (float)pcm[idx] / 32768.0f;
            }
          else
            {
              frame[i] = 0.0f;
            }
        }

      frame_logmel(frame, mel);

      for (i = 0; i < SPEECH_N_MEL; i++)
        {
          feats[f * SPEECH_N_MEL + i] =
              (mel[i] - SPEECH_FEAT_MEAN) * SPEECH_FEAT_INV_STD;
        }
    }

  return 0;
}

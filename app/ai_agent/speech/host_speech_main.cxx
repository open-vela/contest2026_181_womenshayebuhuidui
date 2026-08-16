// host_speech_main.cxx - 主机侧验证: cmd_asr 端到端 (特征+推理) 准确率
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <vector>
#include <string>
#include "cmd_asr.h"
#include "cmd_table.h"

static int g_total = 0;
static int g_correct = 0;

static void eval_dir(const char *path, int expect_class)
{
  DIR *d = opendir(path);
  struct dirent *ent;

  if (!d) return;
  while ((ent = readdir(d)) != NULL)
    {
      char fpath[512];
      std::vector<int16_t> pcm;

      if (strstr(ent->d_name, ".pcm") == NULL) continue;
      snprintf(fpath, sizeof(fpath), "%s/%s", path, ent->d_name);

      FILE *f = fopen(fpath, "rb");
      if (!f) continue;
      fseek(f, 0, SEEK_END);
      long sz = ftell(f);
      fseek(f, 0, SEEK_SET);
      pcm.resize(sz / 2);
      fread(pcm.data(), 2, pcm.size(), f);
      fclose(f);

      float score = 0.0f;
      int cls = cmd_asr_recognize(pcm.data(), (int)pcm.size(), &score);
      g_total++;
      if (cls == expect_class) g_correct++;

      if (cls != expect_class)
        {
          printf("  MIS %s -> class %d (expect %d) score %.2f\n",
                 fpath, cls, expect_class, score);
        }
    }
  closedir(d);
}

int main(int argc, char **argv)
{
  const char *base = argc > 1 ? argv[1] : "data/val";

  printf("=== host speech test: cmd_asr (TFLM) ===\n");
  if (cmd_asr_init() != 0)
    {
      printf("FATAL: cmd_asr_init failed\n");
      return 1;
    }
  printf("init OK\n");

  char path[512];
  for (int c = 0; c < CMD_ASR_N_CLASSES; c++)
    {
      snprintf(path, sizeof(path), "%s/c%d", base, c);
      eval_dir(path, c);
    }

  printf("\n=== accuracy: %d/%d = %.1f%% ===\n",
         g_correct, g_total, g_total ? 100.0 * g_correct / g_total : 0.0);
  return (g_total && g_correct * 100 / g_total >= 90) ? 0 : 2;
}

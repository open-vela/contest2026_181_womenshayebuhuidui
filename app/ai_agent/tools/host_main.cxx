// host_main.cxx - host 侧验证: ai_lm_init / ai_lm_reply 全链路
// 与 velaAI 的 Python 参考 (test_obs_v3.py generate) 输出对比。
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "ai_lm.h"

static int g_fail = 0;

static void check(const char *text)
{
  char reply[AI_LM_REPLY_MAX];

  int ret = ai_lm_reply(text, reply, sizeof(reply));
  printf("Q: %s\n  -> [%d] %s\n", text, ret, reply);

  if (ret != 0)
    {
      g_fail++;
    }
}

static void check_agent(const char *text)
{
  char reply[AI_LM_REPLY_MAX];

  int ret = ai_lm_agent_reply(text, reply, sizeof(reply));
  printf("Q: %s\n  -> [%d] %s\n", text, ret, reply);

  if (ret != 0)
    {
      g_fail++;
    }
}

int main(void)
{
  printf("=== host test: ai_lm (velaAI V3 16x8 TFLM) ===\n");

  if (ai_lm_init() != 0)
    {
      printf("FATAL: ai_lm_init failed\n");
      return 1;
    }

  printf("init OK\n");

  printf("\n--- simple mode (ai_lm_reply) ---\n");
  check("你好");
  check("你是谁");
  check("打开客厅的灯");
  check("现在几点了");
  check("把卧室空调调到26度");
  check("帮我发射火箭");
  check("今天天气怎么样");
  check("播放一首周杰伦的歌");
  check("你叫什么名字");
  check("再见");

  /* 边界: 空输入 */
  check("");
  check("   ");

  printf("\n--- agent mode (ai_lm_agent_reply) ---\n");
  check_agent("你好");
  check_agent("你是谁");
  check_agent("你能做什么");
  check_agent("打开客厅的灯");
  check_agent("关闭客厅的灯");
  check_agent("打开主卧的窗帘");
  check_agent("把卧室空调调到26度");
  check_agent("现在几点了");
  check_agent("今天天气怎么样");
  check_agent("把厨房的加湿器打开");
  check_agent("厨房加湿器什么状态");
  check_agent("10分钟后提醒我");
  check_agent("帮我发射火箭");
  check_agent("播放一首周杰伦的歌");
  check_agent("再见");

  printf("\n=== done (fail=%d) ===\n", g_fail);
  return g_fail ? 1 : 0;
}

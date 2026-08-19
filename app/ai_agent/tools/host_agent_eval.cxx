// host_agent_eval.cxx - agent 模式批量评估: 每行一个用户查询, 输出回复
// 用法: ./host_agent_eval < queries.txt
#include <stdio.h>
#include <string.h>
#include "ai_lm.h"

int main(void)
{
  char line[256];
  char reply[AI_LM_REPLY_MAX];

  if (ai_lm_init() != 0)
    {
      fprintf(stderr, "ai_lm_init failed\n");
      return 1;
    }

  while (fgets(line, sizeof(line), stdin))
    {
      line[strcspn(line, "\n")] = '\0';
      if (line[0] == '\0')
        {
          continue;
        }

      int ret = ai_lm_agent_reply(line, reply, sizeof(reply));
      printf("%s\t%d\t%s\n", line, ret, ret == 0 ? reply : "");
      fflush(stdout);
    }

  return 0;
}

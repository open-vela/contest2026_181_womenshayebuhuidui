/****************************************************************************
 * tokenizer.c - velaAI V3 SentencePiece BPE 分词器 (MCU 端, 逐位一致)
 *
 * 编码算法与训练侧 sentencepiece (BPE) 完全一致, 已在 velaAI 全部数据上
 * 逐位验证 (corpus_v3 20009 + train_v3 18008 + val_v3 2001 = 40018/40018):
 *
 *   1. 归一化 (identity 规则): 去首尾空白, 连续空白压缩为单个空格
 *   2. 空格 (0x20) 转义为 '▁' (U+2581)
 *   3. 用户定义符号 (<usr>/<bot>/<call>/<arg></tool>/<obs>/<eos>) 作为原子
 *   4. 原子之间的普通文本段按 UTF-8 字符切分为符号序列, 反复合并
 *      "拼接结果在词表中 id 最小" 的相邻符号对
 *      (SP BPE 词表 score = -(id-9), id 序即合并优先级, 先到先合并)
 *   5. 合并结束后: 每个符号查表得 id; 不在词表中的符号输出 <unk>(1),
 *      连续未知符号合并为一个 <unk> (SP 行为)
 *
 * 说明: 早前版本的"贪心最长匹配"与 BPE 合并序不等价 (如 '▁今天天气怎么样'
 * SP 切成 '▁'+'今天天气怎么样', 贪心切成 '▁今天'+'天气'+'怎么样'),
 * 且未合并连续 UNK, 均已在本版修复。
 *
 * 词表数据: model/tokenizer_v3_data.h (5000 pieces, blob + FNV-1a 哈希)
 *
 * Team 181 - Contest 2026
 ****************************************************************************/

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "tokenizer.h"
#include "model/tokenizer_v3_data.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* 输入处理上限 (UTF-8 字节); 超出部分截断 */
#define SP_MAX_INPUT      256

/* '▁' U+2581 的 UTF-8 编码: 空格在 SP 词表中的转义形式 */
#define SPACE_MARK_UTF8_LEN 3

/* 工作缓冲上限: 输入全部转义为 '▁' (3 字节) 的最坏情况 */
#define SP_BUF_SIZE       (SP_MAX_INPUT * SPACE_MARK_UTF8_LEN)

/* 符号数上限: 每符号至少 1 字节 (ASCII) */
#define SP_MAX_SYMS       (SP_BUF_SIZE)

/* 用户定义符号 (与 build_tokenizer_v3.py 一致) */
static const char *const SP_UDS[] = {
  "<eos>", "<usr>", "<bot>", "<call>", "<arg>", "</tool>", "<obs>",
};
#define SP_N_UDS (sizeof(SP_UDS) / sizeof(SP_UDS[0]))

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/* FNV-1a 32 位哈希 (与生成脚本一致) */
static uint32_t sp_hash(const uint8_t *data, int len)
{
  uint32_t h = 0x811C9DC5;

  for (int i = 0; i < len; i++)
    {
      h ^= data[i];
      h *= 0x01000193;
    }

  return h;
}

/* 哈希表查找: piece 字节串 -> id, 未命中返回 -1 */
static int sp_piece_lookup(const uint8_t *data, int len)
{
  uint32_t h = sp_hash(data, len) & (SP_HASH_SIZE - 1);

  while (g_sp_hash[h] != SP_HASH_EMPTY)
    {
      uint16_t id = g_sp_hash[h];
      uint32_t plen = g_sp_piece_off[id + 1] - g_sp_piece_off[id];

      if (plen == (uint16_t)len &&
          memcmp(&g_sp_piece_blob[g_sp_piece_off[id]], data, len) == 0)
        {
          return id;
        }

      h = (h + 1) & (SP_HASH_SIZE - 1);
    }

  return -1;
}

/* 判断某字节是否为 UTF-8 多字节序列的后续字节 */
static int is_utf8_cont(uint8_t b)
{
  return (b & 0xC0) == 0x80;
}

/* 从 offset 处取得一个完整 UTF-8 字符的长度 (非法或截断则 1) */
static int utf8_char_len(const uint8_t *data, int remain)
{
  int len;

  if (remain <= 0)
    {
      return 0;
    }

  if (data[0] < 0x80)
    {
      return 1;
    }
  else if ((data[0] & 0xE0) == 0xC0)
    {
      len = 2;
    }
  else if ((data[0] & 0xF0) == 0xE0)
    {
      len = 3;
    }
  else if ((data[0] & 0xF8) == 0xF0)
    {
      len = 4;
    }
  else
    {
      return 1;
    }

  if (len > remain)
    {
      return 1;
    }

  for (int k = 1; k < len; k++)
    {
      if (!is_utf8_cont(data[k]))
        {
          return 1;
        }
    }

  return len;
}

/* 在 buf 处尝试匹配一个用户定义符号, 返回长度或 0 */
static int uds_match(const uint8_t *buf, int remain)
{
  for (int u = 0; u < (int)SP_N_UDS; u++)
    {
      int len = (int)strlen(SP_UDS[u]);

      if (len <= remain && memcmp(buf, SP_UDS[u], len) == 0)
        {
          return len;
        }
    }

  return 0;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int sp_tokenize(const char *text, int32_t *out_ids, int max_tokens)
{
  const uint8_t *src = (const uint8_t *)text;
  uint8_t buf[SP_BUF_SIZE];          /* 归一化 + '▁' 转义后的缓冲 */
  uint16_t sym_off[SP_MAX_SYMS];     /* 段内符号在 buf 中的偏移 */
  uint16_t sym_len[SP_MAX_SYMS];     /* 段内符号长度 */
  int srclen;
  int i = 0;
  int n = 0;
  int blen = 0;

  if (text == NULL || out_ids == NULL || max_tokens <= 0)
    {
      return 0;
    }

  srclen = (int)strlen(text);
  if (srclen > SP_MAX_INPUT)
    {
      srclen = SP_MAX_INPUT;
    }

  /* 1) 归一化: 跳过首部空白, 连续空白压成单个, 去尾部空白;
   *    空格转义为 '▁' (其余空白字符 SP 视同空格处理) */
  int last_space = 1;                /* 行首视为空白, 吃掉前导 */

  while (i < srclen)
    {
      uint8_t ch = src[i];

      if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r')
        {
          last_space = 1;
          i++;
          continue;
        }

      if (last_space && blen > 0 && blen + SPACE_MARK_UTF8_LEN <= SP_BUF_SIZE)
        {
          /* 只在已有内容时补 '▁' (去尾部空白由循环自然保证) */
          buf[blen++] = 0xE2;
          buf[blen++] = 0x96;
          buf[blen++] = 0x81;
        }

      last_space = 0;

      /* 普通字节原样拷贝 */
      if (blen < SP_BUF_SIZE)
        {
          buf[blen++] = ch;
        }

      i++;
    }

  if (blen == 0)
    {
      return 0;                      /* 全空白输入 */
    }

  /* 2) 原子切分 + BPE 合并, 逐段输出 id */
  int pos = 0;
  while (pos < blen && n < max_tokens)
    {
      int ulen = uds_match(&buf[pos], blen - pos);

      if (ulen > 0)
        {
          /* 用户定义符号: 原子, 直接查表 */
          int id = sp_piece_lookup(&buf[pos], ulen);
          out_ids[n++] = (id >= 0) ? id : SP_UNK_ID;
          pos += ulen;
          continue;
        }

      /* 普通段: 收集到下一个 UDS 为止, 按 UTF-8 字符切分 */
      int nsym = 0;
      while (pos < blen && nsym < SP_MAX_SYMS)
        {
          if (uds_match(&buf[pos], blen - pos) > 0)
            {
              break;
            }

          int clen = utf8_char_len(&buf[pos], blen - pos);
          if (clen < 1)
            {
              clen = 1;
            }

          sym_off[nsym] = (uint16_t)pos;
          sym_len[nsym] = (uint16_t)clen;
          nsym++;
          pos += clen;
        }

      /* BPE 合并: 反复合并 "拼接在词表中 id 最小" 的相邻对 */
      for (;;)
        {
          int best_k = -1;
          int best_id = SP_VOCAB_SIZE;

          for (int k = 0; k + 1 < nsym; k++)
            {
              int mlen = sym_len[k] + sym_len[k + 1];

              if (mlen > SP_MAX_PIECE_LEN)
                {
                  continue;
                }

              int id = sp_piece_lookup(&buf[sym_off[k]], mlen);
              if (id >= 0 && id < best_id)
                {
                  best_id = id;
                  best_k = k;
                }
            }

          if (best_k < 0)
            {
              break;
            }

          /* 合并 best_k 与 best_k+1 */
          sym_len[best_k] += sym_len[best_k + 1];
          for (int k = best_k + 1; k + 1 < nsym; k++)
            {
              sym_off[k] = sym_off[k + 1];
              sym_len[k] = sym_len[k + 1];
            }

          nsym--;
        }

      /* 输出: 查表得 id, 连续未知符号合并为一个 <unk> */
      int unk_run = 0;
      for (int k = 0; k < nsym && n < max_tokens; k++)
        {
          int id = sp_piece_lookup(&buf[sym_off[k]], sym_len[k]);

          if (id >= 0)
            {
              out_ids[n++] = id;
              unk_run = 0;
            }
          else
            {
              if (unk_run == 0)
                {
                  out_ids[n++] = SP_UNK_ID;
                }

              unk_run++;
            }
        }
    }

  return n;
}

int sp_piece_id(const char *piece)
{
  if (piece == NULL)
    {
      return -1;
    }

  return sp_piece_lookup((const uint8_t *)piece, (int)strlen(piece));
}

int sp_detokenize(const int32_t *ids, int n, char *out, int out_size)
{
  int o = 0;

  if (ids == NULL || out == NULL || out_size <= 0)
    {
      return -1;
    }

  for (int k = 0; k < n; k++)
    {
      int32_t id = ids[k];

      if (id < 0 || id >= SP_VOCAB_SIZE)
        {
          continue;
        }

      uint32_t off = g_sp_piece_off[id];
      uint32_t len = g_sp_piece_off[id + 1] - off;

      if (len >= 3 &&
          g_sp_piece_blob[off] == 0xE2 &&
          g_sp_piece_blob[off + 1] == 0x96 &&
          g_sp_piece_blob[off + 2] == 0x81)
        {
          /* piece 以 '▁' 开头: 还原为空格 */
          if (o + 1 >= out_size)
            {
              return -1;
            }

          out[o++] = ' ';
          off += 3;
          len -= 3;
        }

      if (o + len >= out_size)
        {
          return -1;
        }

      memcpy(&out[o], &g_sp_piece_blob[off], len);
      o += len;
    }

  out[o] = '\0';
  return o;
}

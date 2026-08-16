/****************************************************************************
 * tokenizer.c - velaAI V3 SentencePiece 风格分词器 (MCU 端)
 *
 * 编码算法 (与训练侧 tokenizer_v3 等价度验证见 tools/README):
 *   1. 输入 UTF-8 字节流, 空格 (0x20) 替换为 '▁' (0xE2 0x96 0x81)
 *   2. 从左到右贪心最长匹配: 在每个位置尝试最长的词表 piece
 *   3. 无匹配 -> 消耗一个完整 UTF-8 字符, 输出 <unk> (id=1)
 *
 * 词表数据: model/tokenizer_v3_data.h (5000 pieces, 67KB blob + FNV-1a 哈希)
 *
 * Team 181 - Contest 2026
 ****************************************************************************/

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "tokenizer.h"
#include "model/tokenizer_v3_data.h"

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* '▁' U+2581 的 UTF-8 编码: 空格在 SP 词表中的转义形式 */
static const uint8_t SPACE_MARK_UTF8[3] = { 0xE2, 0x96, 0x81 };

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

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int sp_tokenize(const char *text, int32_t *out_ids, int max_tokens)
{
  const uint8_t *src = (const uint8_t *)text;
  int srclen = (int)strlen(text);
  int i = 0;
  int n = 0;

  if (text == NULL || out_ids == NULL || max_tokens <= 0)
    {
      return 0;
    }

  while (i < srclen && n < max_tokens)
    {
      int remain = srclen - i;
      int match_len = 0;
      int match_id = -1;
      int l;

      /* 空格 -> '▁' (3 字节), 单独匹配词条 4295 或作为更长 piece 的前缀 */
      if (src[i] == 0x20)
        {
          uint8_t tmp[SP_MAX_PIECE_LEN];
          int tmp_len = 0;

          tmp[tmp_len++] = SPACE_MARK_UTF8[0];
          tmp[tmp_len++] = SPACE_MARK_UTF8[1];
          tmp[tmp_len++] = SPACE_MARK_UTF8[2];

          /* '▁' + 后续字符的最长匹配 (如 '▁你好' 是一个 piece) */
          l = SP_MAX_PIECE_LEN - tmp_len;
          if (l > remain - 1)
            {
              l = remain - 1;
            }

          for (; l >= 0; l--)
            {
              int id;

              if (l > 0)
                {
                  memcpy(&tmp[tmp_len], &src[i + 1], l);
                }

              id = sp_piece_lookup(tmp, tmp_len + l);
              if (id >= 0)
                {
                  match_id = id;
                  match_len = 1 + l; /* 空格 + l 字节 */
                  break;
                }
            }

          if (match_id >= 0)
            {
              out_ids[n++] = match_id;
              i += match_len;
              continue;
            }
        }

      /* 普通最长匹配 */
      l = remain;
      if (l > SP_MAX_PIECE_LEN)
        {
          l = SP_MAX_PIECE_LEN;
        }

      for (; l > 0; l--)
        {
          int id = sp_piece_lookup(&src[i], l);

          if (id >= 0)
            {
              match_id = id;
              match_len = l;
              break;
            }
        }

      if (match_id >= 0)
        {
          out_ids[n++] = match_id;
          i += match_len;
        }
      else
        {
          /* 未命中: 输出 <unk>, 消耗一个 UTF-8 字符 */
          int clen = utf8_char_len(&src[i], remain);

          out_ids[n++] = SP_UNK_ID;
          i += clen;
        }
    }

  return n;
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
          g_sp_piece_blob[off] == SPACE_MARK_UTF8[0] &&
          g_sp_piece_blob[off + 1] == SPACE_MARK_UTF8[1] &&
          g_sp_piece_blob[off + 2] == SPACE_MARK_UTF8[2])
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

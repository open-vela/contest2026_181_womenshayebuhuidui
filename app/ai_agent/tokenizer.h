/****************************************************************************
 * tokenizer.h - velaAI V3 SentencePiece 风格分词器 (MCU 端)
 *
 * 与训练侧 tokenizer_v3 (vocab=5000, BPE, identity 归一化) 对应的简化实现:
 *   - 编码: 空格 -> '▁' (U+2581), 前缀最长匹配分段, 未命中 -> <unk>
 *   - 解码: '▁' -> 空格, 其余字节原样拼接
 *   - 词表数据来自 model/tokenizer_v3_data.h (由
 *     tools/gen_local_lm_tables.py 从 tokenizer.model 生成)
 *
 * Team 181 - Contest 2026
 ****************************************************************************/

#ifndef VELA_AI_TOKENIZER_H
#define VELA_AI_TOKENIZER_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 将 UTF-8 文本编码为 token id 序列 (最长匹配, 未命中 -> SP_UNK_ID)。
 *
 * @param text        UTF-8 输入文本 (可为中文/英文/标点, 空格按 SP 规则转 '▁')
 * @param out_ids     输出 token id 数组
 * @param max_tokens  out_ids 容量
 * @return 实际 token 数; 0 表示输入为空或全为非法字节
 */
int sp_tokenize(const char *text, int32_t *out_ids, int max_tokens);

/**
 * 将 token id 序列解码为 UTF-8 文本。
 *
 * @param ids        token id 数组
 * @param n          token 数
 * @param out        输出文本缓冲
 * @param out_size   out 容量 (含 '\0')
 * @return 写出的字符数 (不含 '\0'); 负数为缓冲不足
 */
int sp_detokenize(const int32_t *ids, int n, char *out, int out_size);

#ifdef __cplusplus
}
#endif

#endif /* VELA_AI_TOKENIZER_H */

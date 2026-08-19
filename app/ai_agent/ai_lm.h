/****************************************************************************
 * ai_lm.h - velaAI V3 端侧语言模型 (TFLite Micro) 推理接口
 *
 * 模型: mcu_language_model_v3_16x8.tflite (LSTM 256 / vocab 5000, 16x8 量化)
 * 架构: Embedding -> LSTM(return_state) -> Dense(softmax), 多IO显式状态:
 *   输入: token(1,1) int32, h_in(1,256) f32, c_in(1,256) f32
 *   输出: c_out(1,256) f32, logits(1,5000) f32, h_out(1,256) f32
 * 生成: 自回归逐 token argmax, 遇 <eos>(id=2) 早停
 *
 * Team 181 - Contest 2026
 ****************************************************************************/

#ifndef VELA_AI_LM_H
#define VELA_AI_LM_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 模型超参数 (与训练/导出侧一致) */
#define AI_LM_VOCAB_SIZE   5000
#define AI_LM_LSTM_UNITS   256
#define AI_LM_EOS_ID       2

/* 生成配置 */
#define AI_LM_MAX_NEW      32      /* 最多生成 token 数 */
#define AI_LM_REPLY_MAX    384     /* 回复文本缓冲上限 (UTF-8 字节) */

/**
 * 初始化 TFLM 解释器 (加载模型 + 分配 Tensor Arena)。
 * 线程安全: 调用方需保证初始化完成后再调用生成接口。
 *
 * @return 0 成功, -1 失败
 */
int ai_lm_init(void);

/**
 * 自回归生成: 以 prompt token 序列为条件, 生成 max_new 个 token 或遇 EOS 停止。
 *
 * @param prompt      prompt token id 数组
 * @param n_prompt    prompt 长度
 * @param out_tokens  输出 token 数组 (容量 >= max_new)
 * @param max_new     最大生成数
 * @param out_len     实际生成的 token 数
 * @return 0 成功, -1 失败
 */
int ai_lm_generate(const int32_t *prompt, int n_prompt,
                   int32_t *out_tokens, int max_new, int *out_len);

/**
 * 端到端回复: "<usr> {text} <bot>" -> 分词 -> 生成 -> 提取 <bot> 回复文本。
 *
 * @param user_text   用户输入 (UTF-8)
 * @param reply       输出回复文本缓冲 (>= AI_LM_REPLY_MAX)
 * @param reply_size  reply 容量
 * @return 0 成功 (reply 非空), -1 失败 (模型不可用或生成空文本, 调用方应兜底)
 */
int ai_lm_reply(const char *user_text, char *reply, int reply_size);

/**
 * Agent 模式 (两段式, 与 velaAI 训练数据格式一致):
 *   1. "<usr> {text}" -> 模型生成 "<call> tool <arg>.. </tool>" (或直接闲聊回复)
 *   2. 端侧执行器 (设备状态表 / RTC / 模拟传感器) 产出 <obs> 值
 *   3. "<usr> {text} <call>.. </tool> <obs> {obs}" -> 模型生成 "<bot>" 回复
 *   4. 槽位校验: 回复须包含调用的房间/数值参数; 不通过则用与训练模板
 *      一致的确定性回复兜底 (16x8 量化模型的槽位复制易错, 应用层保证正确性)
 *
 * @param user_text   用户输入 (UTF-8)
 * @param reply       输出回复文本缓冲 (>= AI_LM_REPLY_MAX)
 * @param reply_size  reply 容量
 * @return 0 成功, -1 失败 (调用方回退关键词表)
 */
int ai_lm_agent_reply(const char *user_text, char *reply, int reply_size);

/**
 * 中途打断: 请求取消正在进行的推理 (逐 token 检查, 立即释放 CPU)。
 * 下一次 ai_lm_reply / ai_lm_agent_reply 开始时会自动清标志。
 */
void ai_lm_cancel(void);

/**
 * 清除取消标志 (新请求开始时调用, 避免上一次的取消毒化新会话)。
 */
void ai_lm_cancel_clear(void);

/**
 * 查询是否有取消请求 (语音监听循环等长循环可据此提前退出)。
 */
int ai_lm_cancel_check(void);

#ifdef __cplusplus
}
#endif

#endif /* VELA_AI_LM_H */

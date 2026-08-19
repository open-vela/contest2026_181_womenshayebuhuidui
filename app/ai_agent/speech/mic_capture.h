/****************************************************************************
 * mic_capture.h - 板载麦克风采集 (经 NuttX 音频框架 /dev/audio/audio0)
 *
 * Team 181 - Contest 2026
 ****************************************************************************/

#ifndef SPEECH_MIC_CAPTURE_H
#define SPEECH_MIC_CAPTURE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MIC_CAPTURE_RATE    16000
#define MIC_CAPTURE_CHANNELS 1
#define MIC_CAPTURE_BITS    16

/**
 * 打开并启动采集 (16kHz/16bit/mono)。
 * @return 0 成功, 负值失败
 */
int mic_capture_start(void);

/**
 * 读取一段 PCM (阻塞, 内部自动补队列)。
 *
 * @param buf       输出 int16 PCM
 * @param max_bytes buf 容量 (字节)
 * @param timeout_ms 超时 (ms), <=0 无限等待
 * @return 读取字节数 (0=超时), 负值失败
 */
int mic_capture_read(int16_t *buf, int max_bytes, int timeout_ms);

/**
 * 新会话开始时冲刷陈旧数据: 取空消息队列 + 丢弃 discard_ms 毫秒采集。
 * 消除上一次会话停止瞬态/残留缓冲造成的假 VAD 触发。
 */
void mic_capture_flush(int discard_ms);

/**
 * 停止并释放采集。
 */
void mic_capture_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* SPEECH_MIC_CAPTURE_H */

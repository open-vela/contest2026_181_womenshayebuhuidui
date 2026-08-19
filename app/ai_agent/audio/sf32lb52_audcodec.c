/****************************************************************************
 * sf32lb52_audcodec.c - SF32LB52 内置音频编解码器 NuttX Audio Lower-Half
 *
 * 麦克风采集 (MEMS MIC -> audcodec ADC CH0, DMA 循环采集):
 *   - 默认 16kHz / 16bit / 单声道 (PCM S16_LE)
 *   - DMA: DMAC1_CH4, 请求 39 (AUDCODEC_ADC0_DMA_REQUEST)
 *   - ISR: HAL_DMA_IRQHandler -> HAL_AUDCODEC_Rx( Half )CpltCallback -> 信号量
 *   - 工作线程: 半缓冲就绪 -> 填充 pending apb -> upper 回调 DEQUEUE
 *
 * 参考: SiFli SDK rtos/rtthread/bsp/sifli/drivers/drv_audcodec_m.c
 *       (openvela Audio Driver Guide: audio_lowerhalf_s + audio_register)
 *
 * Team 181 - Contest 2026
 ****************************************************************************/

#include <nuttx/config.h>

#include <debug.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>

#include <nuttx/audio/audio.h>
#include <nuttx/fs/fs.h>
#include <nuttx/irq.h>
#include <nuttx/kmalloc.h>
#include <nuttx/mutex.h>
#include <nuttx/semaphore.h>
#include <nuttx/kthread.h>
#include <nuttx/queue.h>

/* SiFli HAL (需与 arch 构建一致的宏与头文件路径, 见 CMakeLists) */
#define SOC_BF0_HCPU
#define SF32LB52X
#include "bf0_hal.h"
#include "dma_config.h"

#include "sf32lb52_audcodec.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define SF32LB52_AUDIO_DEVNAME      "audio0"

/* DMA 循环缓冲: 半缓冲 = 2048 B = 64ms @ 16kHz/16bit/mono */
#define SF32LB52_RX_BUF_SIZE        4096
#define SF32LB52_RX_HALF            (SF32LB52_RX_BUF_SIZE / 2)
#define SF32LB52_AUDIO_BUF_SIZE     4096   /* apb 大小 (与 mic_capture 一致) */
#define SF32LB52_AUDIO_NUM_BUFS     8      /* apb 数量 (与 mic_capture 一致) */

#define SF32LB52_AUDIO_DEFAULT_RATE 16000
#define SF32LB52_AUDIO_DEFAULT_CHS  1
#define SF32LB52_AUDIO_DEFAULT_BITS 16

/* 采集工作线程 */
#define SF32LB52_AUDIO_WORKER_PRIO  120
#define SF32LB52_AUDIO_WORKER_STACK 2048

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct sf32lb52_audio_s
{
  struct audio_lowerhalf_s dev;         /* 必须是第一个成员 */

  AUDCODEC_HandleTypeDef codec;         /* audcodec HAL 句柄 */
  DMA_HandleTypeDef dma_rx;             /* ADC CH0 DMA (DMAC1_CH4, REQ 39) */

  uint8_t rx_buf[SF32LB52_RX_BUF_SIZE] __attribute__((aligned(4)));
  volatile int rx_half;                 /* 下一个就绪的半缓冲: 0/1 */

  uint16_t samprate;
  uint8_t  nchannels;
  uint8_t  bpsamp;
  volatile bool recording;

  sem_t rx_sem;                         /* ISR -> 工作线程 */
  pid_t worker;                         /* 采集工作线程 */

  /* 诊断计数器 (采集无数据排查, 见开发记录 13) */
  volatile uint32_t dbg_irq;            /* DMA ISR 进入次数 */
  volatile uint32_t dbg_cplt;           /* 半/全传输回调次数 */
  volatile uint32_t dbg_wake;           /* 工作线程唤醒次数 */

  volatile int skip_events;             /* 启动后丢弃的前 N 个半缓冲事件
                                            (ADC 使能瞬态满幅毛刺) */

  mutex_t pendlock;
  dq_queue_t pendq;                     /* 等待填充的 apb */
  struct ap_buffer_s *aux;              /* 正在填充的 apb */

  volatile bool drain;                  /* 请求工作线程清空 pending 队列 */
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int sf32lb52_audio_getcaps(FAR struct audio_lowerhalf_s *dev,
                                  int type, FAR struct audio_caps_s *caps);
static int sf32lb52_audio_configure(FAR struct audio_lowerhalf_s *dev,
                                    FAR const struct audio_caps_s *caps);
static int sf32lb52_audio_start(FAR struct audio_lowerhalf_s *dev);
static int sf32lb52_audio_stop(FAR struct audio_lowerhalf_s *dev);
static int sf32lb52_audio_enqueuebuffer(FAR struct audio_lowerhalf_s *dev,
                                        FAR struct ap_buffer_s *apb);
static int sf32lb52_audio_cancelbuffer(FAR struct audio_lowerhalf_s *dev,
                                       FAR struct ap_buffer_s *apb);
static int sf32lb52_audio_reserve(FAR struct audio_lowerhalf_s *dev);
static int sf32lb52_audio_release(FAR struct audio_lowerhalf_s *dev);
static int sf32lb52_audio_shutdown(FAR struct audio_lowerhalf_s *dev);
static int sf32lb52_audio_ioctl(FAR struct audio_lowerhalf_s *dev,
                                int cmd, unsigned long arg);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct audio_ops_s g_sf32lb52_audio_ops =
{
  .getcaps       = sf32lb52_audio_getcaps,
  .configure     = sf32lb52_audio_configure,
  .shutdown      = sf32lb52_audio_shutdown,
  .start         = sf32lb52_audio_start,
  .stop          = sf32lb52_audio_stop,
  .enqueuebuffer = sf32lb52_audio_enqueuebuffer,
  .cancelbuffer  = sf32lb52_audio_cancelbuffer,
  .reserve       = sf32lb52_audio_reserve,
  .release       = sf32lb52_audio_release,
  .ioctl         = sf32lb52_audio_ioctl,
};

static struct sf32lb52_audio_s g_audio;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/* ── HAL 回调 (由 HAL DMA ISR 调用, 弱符号由驱动接管) ── */

void HAL_AUDCODEC_RxCpltCallback(AUDCODEC_HandleTypeDef *hacodec, int cid)
{
  struct sf32lb52_audio_s *priv = &g_audio;

  (void)hacodec;
  (void)cid;
  priv->rx_half = 1;
  priv->dbg_cplt++;
  nxsem_post(&priv->rx_sem);
}

void HAL_AUDCODEC_RxHalfCpltCallback(AUDCODEC_HandleTypeDef *hacodec, int cid)
{
  struct sf32lb52_audio_s *priv = &g_audio;

  (void)hacodec;
  (void)cid;
  priv->rx_half = 0;
  priv->dbg_cplt++;
  nxsem_post(&priv->rx_sem);
}

/* ── DMA 中断 ── */

static int sf32lb52_audio_isr(int irq, FAR void *context, FAR void *arg)
{
  struct sf32lb52_audio_s *priv = (struct sf32lb52_audio_s *)arg;

  priv->dbg_irq++;
  HAL_DMA_IRQHandler(&priv->dma_rx);
  return OK;
}

/* ── 采集工作线程: 半缓冲就绪 -> 填充 apb ── */

static void sf32lb52_audio_deliver(FAR struct sf32lb52_audio_s *priv,
                                   FAR const uint8_t *data, size_t len)
{
  FAR struct ap_buffer_s *apb = priv->aux;

  if (apb == NULL)
    {
      return;
    }

  /* 填满当前 apb 为止 (容量 = nmaxbytes, apb_alloc 后 nbytes=0) */
  while (len > 0 && apb != NULL)
    {
      size_t space = apb->nmaxbytes - apb->curbyte;
      size_t chunk = len < space ? len : space;

      memcpy(apb->samp + apb->curbyte, data, chunk);
      apb->curbyte += chunk;
      data += chunk;
      len -= chunk;

      if (apb->curbyte >= apb->nmaxbytes)
        {
          /* 一个 apb 填满: 交给 upper (INPUT 路径按 nbytes 写文件) */
          apb->nbytes = apb->curbyte;
          priv->aux = NULL;
          priv->dev.upper(priv->dev.priv, AUDIO_CALLBACK_DEQUEUE, apb, OK);

          /* 取下一条 */
          nxmutex_lock(&priv->pendlock);
          apb = (FAR struct ap_buffer_s *)dq_remfirst(&priv->pendq);
          nxmutex_unlock(&priv->pendlock);
          priv->aux = apb;
        }
    }
}

static void sf32lb52_audio_worker_impl(FAR struct sf32lb52_audio_s *priv)
{
  const uint8_t *half;

  if (priv->rx_half == 0)
    {
      half = priv->rx_buf;
    }
  else
    {
      half = priv->rx_buf + SF32LB52_RX_HALF;
    }

  sf32lb52_audio_deliver(priv, half, SF32LB52_RX_HALF);
}

/* 清空 pending 队列并归还缓冲 — 只在 audcodec 工作线程上下文执行
 * (与 deliver 同线程, 不会并发进入 audio 核心; 曾因在语音线程里
 * drain 与交付路径撞锁导致 worker 永久卡死, 真机 wake=0, 08-16 修) */
static void sf32lb52_audio_drain_pending(FAR struct sf32lb52_audio_s *priv)
{
  nxmutex_lock(&priv->pendlock);

  while (!dq_empty(&priv->pendq))
    {
      FAR struct ap_buffer_s *apb;

      apb = (FAR struct ap_buffer_s *)dq_remfirst(&priv->pendq);
      priv->dev.upper(priv->dev.priv, AUDIO_CALLBACK_DEQUEUE, apb, OK);
    }

  nxmutex_unlock(&priv->pendlock);
  priv->aux = NULL;
}

static int sf32lb52_audio_worker(int argc, FAR char *argv[])
{
  FAR struct sf32lb52_audio_s *priv = &g_audio;

  while (1)
    {
      nxsem_wait(&priv->rx_sem);
      priv->dbg_wake++;

      /* 停止采集的 drain 请求 (工作线程上下文执行) */
      if (priv->drain)
        {
          priv->drain = false;
          sf32lb52_audio_drain_pending(priv);
          continue;
        }

      /* ADC 使能瞬态: 通路建立期间输出满幅毛刺 (真机实测首个半缓冲
       * max=32750, 假能量触发 VAD), 丢弃前几个事件 */
      if (priv->skip_events > 0)
        {
          priv->skip_events--;
          continue;
        }

      if (priv->recording)
        {
          sf32lb52_audio_worker_impl(priv);
        }
    }

  return 0;
}

/* ── 采集启动/停止 (HAL 序列参考 SiFli SDK drv_audcodec_m.c) ── */

static int sf32lb52_audio_start_capture(FAR struct sf32lb52_audio_s *priv)
{
  HAL_StatusTypeDef res;

  if (priv->recording)
    {
      return OK;
    }

  /* PLL: 16kHz 1000 系列 (type=1 -> freq_type=2) */
  bf0_enable_pll(priv->samprate, 1);

  /* ADC 通道配置 (16bit, MIC 音量) */
  res = HAL_AUDCODEC_Config_RChanel(&priv->codec, 0,
                                    &priv->codec.Init.adc_cfg);
  if (res != HAL_OK)
    {
      return -EIO;
    }

  HAL_AUDCODEC_Config_ADCPath_Volume(&priv->codec, 0, 12);

  /* DMA 循环采集 */
  priv->rx_half = 0;
  res = HAL_AUDCODEC_Receive_DMA(&priv->codec, priv->rx_buf,
                                 SF32LB52_RX_BUF_SIZE,
                                 HAL_AUDCODEC_ADC_CH0);
  if (res != HAL_OK)
    {
      return -EIO;
    }

  up_enable_irq(AUDCODEC_ADC0_DMA_IRQ + NVIC_IRQ_FIRST);

  /* 模拟 MIC 通路 (MICBIAS 等) */
  HAL_AUDCODEC_Config_Analog_ADCPath(priv->codec.Init.adc_cfg.adc_clk);

  /* 最后使能 ADC */
  __HAL_AUDCODEC_ADC_ENABLE(&priv->codec);

  /* 丢弃使能瞬态 (前 4 个半缓冲事件 ~0.25s) */
  priv->skip_events = 4;

  /* 工作线程存活探测: post 一次, 20ms 内 dbg_wake 应递增;
   * 无响应说明 worker 已卡死 (wake 停滞)。必须先杀旧线程再重建 —
   * 若旧线程日后复活, 两个 worker 抢同一 pendq/aux 会互相偷缓冲,
   * 数据黑洞 (真机 wake<cplt 与 wake>cplt 并存, 08-16) */
  {
    uint32_t w0 = priv->dbg_wake;

    nxsem_post(&priv->rx_sem);
    usleep(20000);

    if (priv->dbg_wake == w0)
      {
        pid_t w;

        printf("[mic] worker stalled, recreate (kill old first)\n");

        if (priv->worker > 0)
          {
            nxtask_delete(priv->worker);
            priv->worker = -1;
          }

        /* 清掉死线程留下的信号量残留计数 */
        while (nxsem_trywait(&priv->rx_sem) >= 0)
          {
          }

        w = kthread_create("audcodec_rx2", SF32LB52_AUDIO_WORKER_PRIO,
                           SF32LB52_AUDIO_WORKER_STACK,
                           sf32lb52_audio_worker, NULL);

        if (w > 0)
          {
            priv->worker = w;
          }

        usleep(20000);
      }
  }

  priv->recording = true;
  return OK;
}

static void sf32lb52_audio_stop_capture(FAR struct sf32lb52_audio_s *priv)
{
  if (!priv->recording)
    {
      return;
    }

  priv->recording = false;
  up_disable_irq(AUDCODEC_ADC0_DMA_IRQ + NVIC_IRQ_FIRST);
  HAL_AUDCODEC_DMAStop(&priv->codec, HAL_AUDCODEC_ADC_CH0);

  /* SDK 的 DMAStop 不清 State (源码中被注释), Receive_DMA 会一直
   * HAL_BUSY -> 二次启动 EIO (真机 2026-08-16 实测), 此处补清 */
  priv->codec.State[HAL_AUDCODEC_ADC_CH0] = HAL_AUDCODEC_STATE_READY;

  HAL_AUDCODEC_Close_Analog_ADCPath();
  __HAL_AUDCODEC_ADC_DISABLE(&priv->codec);

  priv->skip_events = 0;

  /* 诊断: 采集计数 + 原始字流 dump */
  {
    const int16_t *s16 = (const int16_t *)priv->rx_buf;
    int big = 0;
    int mx = 0;

    for (int i = 0; i < 512; i++)
      {
        int a = s16[i] < 0 ? -s16[i] : s16[i];

        if (a > 20000)
          {
            big++;
          }

        if (a > mx)
          {
            mx = a;
          }
      }

    printf("[mic-dbg] irq=%u cplt=%u wake=%u max16=%d big=%d\n",
           (unsigned)priv->dbg_irq, (unsigned)priv->dbg_cplt,
           (unsigned)priv->dbg_wake, mx, big);
    printf("[mic-raw]");

    for (int i = 0; i < 8; i++)
      {
        printf(" %04x", (uint16_t)s16[i]);
      }

    printf("\n");
  }

  priv->dbg_irq = priv->dbg_cplt = priv->dbg_wake = 0;

  /* 清空 pending apb: 交给 audcodec 工作线程执行 (drain 标志 + 唤醒),
   * 与交付路径同上下文, 避免并发进入 audio 核心 (曾致 worker 卡死) */
  priv->drain = true;
  nxsem_post(&priv->rx_sem);

  for (int i = 0; i < 200 && priv->drain; i++)
    {
      usleep(1000);   /* 最多等 200ms */
    }

  if (priv->drain)
    {
      /* 工作线程无响应 (已卡死): 直接重置队列引用 (不再回调,
       * 缓冲归用户进程释放); 清残留信号量计数; 下次 start 的
       * 存活探测会 杀旧+重建 worker */
      printf("[mic] drain timeout, worker dead\n");
      dq_init(&priv->pendq);
      priv->aux = NULL;
      priv->drain = false;
    }

  /* 清信号量残留计数: 卡死/缓慢的 worker 未消费的 post 若不清,
   * 下一会话会被陈旧唤醒轰炸 (wake 远大于 cplt), 且陈旧唤醒会
   * 白白消耗 skip_events 与 drain 判定 */
  while (nxsem_trywait(&priv->rx_sem) >= 0)
    {
    }
}

/* ── audio_ops_s 实现 ── */

static int sf32lb52_audio_getcaps(FAR struct audio_lowerhalf_s *dev,
                                  int type, FAR struct audio_caps_s *caps)
{
  FAR struct sf32lb52_audio_s *priv = (FAR struct sf32lb52_audio_s *)dev;

  (void)priv;
  caps->ac_format.hw = 0;
  caps->ac_controls.w = 0;

  switch (caps->ac_type)
    {
      /* 总体能力查询 (subtype=QUERY 或具体格式) */

      case AUDIO_TYPE_QUERY:
        caps->ac_channels = 1;

        switch (caps->ac_subtype)
          {
            case AUDIO_TYPE_QUERY:
              /* 支持 PCM + 输入/输出单元 */
              caps->ac_controls.b[0] = AUDIO_TYPE_INPUT | AUDIO_TYPE_OUTPUT;
              caps->ac_format.hw = 1 << (AUDIO_FMT_PCM - 1);
              break;

            case AUDIO_FMT_PCM:
              /* PCM 子格式列表 (按 ac_format.b[0] 分页) */
              switch (caps->ac_format.b[0])
                {
                  case 0:
                    caps->ac_controls.b[0] = AUDIO_SUBFMT_PCM_S16_LE;
                    caps->ac_controls.b[1] = AUDIO_SUBFMT_END;
                    break;

                  default:
                    caps->ac_controls.b[0] = AUDIO_SUBFMT_END;
                    break;
                }

              break;

            default:
              caps->ac_controls.b[0] = AUDIO_SUBFMT_END;
              break;
          }

        break;

      /* 采集能力 */

      case AUDIO_TYPE_INPUT:
        caps->ac_channels = 1;

        switch (caps->ac_subtype)
          {
            case AUDIO_TYPE_QUERY:
              /* 支持的格式 */
              caps->ac_controls.b[0] = AUDIO_SUBFMT_PCM_S16_LE;
              caps->ac_controls.b[1] = AUDIO_SUBFMT_END;
              break;

            case AUDIO_FMT_PCM:
              /* 采样率范围 (当前固定 16k) */
              caps->ac_controls.b[0] = SF32LB52_AUDIO_DEFAULT_RATE;
              caps->ac_controls.b[1] = 48000;
              break;

            default:
              caps->ac_controls.b[0] = AUDIO_SUBFMT_END;
              break;
          }

        break;

      /* 播放能力: P1 未实现 */

      case AUDIO_TYPE_OUTPUT:
        caps->ac_channels = 1;

        if (caps->ac_subtype == AUDIO_TYPE_QUERY)
          {
            caps->ac_controls.b[0] = AUDIO_SUBFMT_END;
          }
        else
          {
            caps->ac_controls.b[0] = AUDIO_SUBFMT_END;
          }

        break;

      default:
        return -ENOTTY;
    }

  return OK;
}

static int sf32lb52_audio_configure(FAR struct audio_lowerhalf_s *dev,
                                    FAR const struct audio_caps_s *caps)
{
  FAR struct sf32lb52_audio_s *priv = (FAR struct sf32lb52_audio_s *)dev;

  if (caps->ac_type != AUDIO_TYPE_INPUT)
    {
      return -ENOTTY;
    }

  /* nxrecorder 约定: 采样率在 hw[0] (b[3] 为高位), 位深在 b[2] */
  priv->samprate = caps->ac_controls.hw[0];
  if (priv->samprate == 0)
    {
      priv->samprate = SF32LB52_AUDIO_DEFAULT_RATE;
    }

  priv->nchannels = caps->ac_channels;
  if (priv->nchannels == 0)
    {
      priv->nchannels = SF32LB52_AUDIO_DEFAULT_CHS;
    }

  priv->bpsamp = caps->ac_controls.b[2];
  if (priv->bpsamp == 0)
    {
      priv->bpsamp = SF32LB52_AUDIO_DEFAULT_BITS;
    }

  /* 当前仅支持 16kHz/16bit/单声道, 其它请求收敛到该配置 */
  priv->samprate = SF32LB52_AUDIO_DEFAULT_RATE;
  priv->nchannels = SF32LB52_AUDIO_DEFAULT_CHS;
  priv->bpsamp = SF32LB52_AUDIO_DEFAULT_BITS;

  /* 16kHz 采样率时钟表 (PLL): {rate, clk_src_sel, clk_div, osr_sel,
   *  sel_clk_adc_source, sel_clk_adc, diva_clk_adc, fsp}
   * 参考 SiFli SDK codec_adc_clk_config_pll[3] (16k 项)
   * 注意: 结构体首字段是 samplerate, 早前版本漏写 16000 导致全体左移
   * (clk_src_sel=10 非法, clk_div=1), ADC 时钟快约一个量级 (真机实测
   * 半传输中断 ~1kHz, 应为 ~16Hz), 2026-08-16 修复 */
  static AUDCODE_ADC_CLK_CONFIG_TYPE adc_clk_16k =
    { 16000, 1, 10, 1, 1, 0, 5, 2 };

  priv->codec.Init.adc_cfg.opmode = 1;
  priv->codec.Init.adc_cfg.adc_clk = &adc_clk_16k;

  return OK;
}

static int sf32lb52_audio_start(FAR struct audio_lowerhalf_s *dev)
{
  return sf32lb52_audio_start_capture((FAR struct sf32lb52_audio_s *)dev);
}

static int sf32lb52_audio_stop(FAR struct audio_lowerhalf_s *dev)
{
  sf32lb52_audio_stop_capture((FAR struct sf32lb52_audio_s *)dev);
  return OK;
}

static int sf32lb52_audio_enqueuebuffer(FAR struct audio_lowerhalf_s *dev,
                                        FAR struct ap_buffer_s *apb)
{
  FAR struct sf32lb52_audio_s *priv = (FAR struct sf32lb52_audio_s *)dev;

  apb->curbyte = 0;

  nxmutex_lock(&priv->pendlock);
  if (priv->aux == NULL)
    {
      priv->aux = apb;
    }
  else
    {
      dq_addlast(&apb->dq_entry, &priv->pendq);
    }

  nxmutex_unlock(&priv->pendlock);
  return OK;
}

static int sf32lb52_audio_cancelbuffer(FAR struct audio_lowerhalf_s *dev,
                                       FAR struct ap_buffer_s *apb)
{
  FAR struct sf32lb52_audio_s *priv = (FAR struct sf32lb52_audio_s *)dev;

  nxmutex_lock(&priv->pendlock);
  dq_rem(&apb->dq_entry, &priv->pendq);
  nxmutex_unlock(&priv->pendlock);

  priv->dev.upper(priv->dev.priv, AUDIO_CALLBACK_DEQUEUE, apb, OK);
  return OK;
}

static int sf32lb52_audio_reserve(FAR struct audio_lowerhalf_s *dev)
{
  FAR struct sf32lb52_audio_s *priv = (FAR struct sf32lb52_audio_s *)dev;

  (void)priv;
  return OK;
}

/* NuttX audio 核心 AUDIOIOC_GETBUFFERINFO / SETBUFFERINFO / 未知命令
 * 直接转发到 lower->ops->ioctl (audio.c:1257/1271/1294, 无 NULL 保护),
 * 缺失该成员时 mic_capture 的 GETBUFFERINFO 会经 NULL 函数指针调用
 * 触发 hard fault (真机 2026-08-16 实测崩溃点, 见开发记录 13 第九节)。 */
static int sf32lb52_audio_ioctl(FAR struct audio_lowerhalf_s *dev,
                                int cmd, unsigned long arg)
{
  FAR struct ap_buffer_info_s *binfo;

  (void)dev;

  switch (cmd)
    {
      case AUDIOIOC_GETBUFFERINFO:
        binfo = (FAR struct ap_buffer_info_s *)arg;
        binfo->buffer_size = SF32LB52_AUDIO_BUF_SIZE;
        binfo->nbuffers    = SF32LB52_AUDIO_NUM_BUFS;
        return OK;

      case AUDIOIOC_SETBUFFERINFO:
        return OK;   /* 接受, 但维持驱动默认缓冲配置 */

      default:
        return -ENOTTY;
    }
}

static int sf32lb52_audio_release(FAR struct audio_lowerhalf_s *dev)
{
  FAR struct sf32lb52_audio_s *priv = (FAR struct sf32lb52_audio_s *)dev;

  sf32lb52_audio_stop_capture(priv);
  return OK;
}

static int sf32lb52_audio_shutdown(FAR struct audio_lowerhalf_s *dev)
{
  FAR struct sf32lb52_audio_s *priv = (FAR struct sf32lb52_audio_s *)dev;

  sf32lb52_audio_stop_capture(priv);
  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int sf32lb52_audcodec_register(void)
{
  FAR struct sf32lb52_audio_s *priv = &g_audio;
  AUDCODEC_HandleTypeDef *codec = &priv->codec;
  int ret;

  memset(priv, 0, sizeof(*priv));

  /* ── 时钟/电源 (参考 SiFli SDK bf0_audio_init) ── */
  HAL_PMU_EnableAudio(1);
  HAL_RCC_EnableModule(RCC_MOD_AUDCODEC);

  /* ── DMA 句柄: ADC CH0 = DMAC1_CH4, 请求 39 ── */
  priv->dma_rx.Instance = AUDCODEC_ADC0_DMA_INSTANCE;
  priv->dma_rx.Init.Request = AUDCODEC_ADC0_DMA_REQUEST;

  /* ── audcodec 句柄 ── */
  codec->Instance = hwp_audcodec;
  codec->hdma[HAL_AUDCODEC_ADC_CH0] = &priv->dma_rx;
  codec->Init.en_dly_sel = 0;
  codec->Init.adc_cfg.opmode = 1;
  codec->Init.adc_cfg.adc_clk = NULL;   /* configure() 时按采样率设置 */

  /* 默认 16kHz 时钟表 (同样补上 samplerate 首字段) */
  static AUDCODE_ADC_CLK_CONFIG_TYPE adc_clk_default =
    { 16000, 1, 10, 1, 1, 0, 5, 2 };
  codec->Init.adc_cfg.adc_clk = &adc_clk_default;
  priv->samprate = SF32LB52_AUDIO_DEFAULT_RATE;
  priv->nchannels = SF32LB52_AUDIO_DEFAULT_CHS;
  priv->bpsamp = SF32LB52_AUDIO_DEFAULT_BITS;

  if (HAL_AUDCODEC_Init(codec) != HAL_OK)
    {
      auderr("ERROR: HAL_AUDCODEC_Init failed\n");
      return -EIO;
    }

  /* ── 同步原语 ── */
  nxsem_init(&priv->rx_sem, 0, 0);
  nxmutex_init(&priv->pendlock);
  dq_init(&priv->pendq);

  /* ── DMA 中断 ── */
  ret = irq_attach(AUDCODEC_ADC0_DMA_IRQ + NVIC_IRQ_FIRST,
                   sf32lb52_audio_isr, priv);
  if (ret != OK)
    {
      auderr("ERROR: irq_attach failed: %d\n", ret);
      return ret;
    }

  /* ── 采集工作线程 ── */
  priv->worker = kthread_create("audcodec_rx", SF32LB52_AUDIO_WORKER_PRIO,
                                SF32LB52_AUDIO_WORKER_STACK,
                                sf32lb52_audio_worker, NULL);
  if (priv->worker < 0)
    {
      auderr("ERROR: kthread_create failed\n");
      return (int)priv->worker;
    }

  /* ── 注册到 NuttX 音频框架 ── */
  priv->dev.ops = &g_sf32lb52_audio_ops;
  ret = audio_register(SF32LB52_AUDIO_DEVNAME, &priv->dev);
  if (ret < 0)
    {
      auderr("ERROR: audio_register failed: %d\n", ret);
      return ret;
    }

  ainfo("SF32LB52 audcodec registered: %s (16kHz/16bit/mono)\n",
        SF32LB52_AUDIO_DEVNAME);
  return OK;
}

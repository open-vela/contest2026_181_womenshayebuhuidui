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

  mutex_t pendlock;
  dq_queue_t pendq;                     /* 等待填充的 apb */
  struct ap_buffer_s *aux;              /* 正在填充的 apb */
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
  nxsem_post(&priv->rx_sem);
}

void HAL_AUDCODEC_RxHalfCpltCallback(AUDCODEC_HandleTypeDef *hacodec, int cid)
{
  struct sf32lb52_audio_s *priv = &g_audio;

  (void)hacodec;
  (void)cid;
  priv->rx_half = 0;
  nxsem_post(&priv->rx_sem);
}

/* ── DMA 中断 ── */

static int sf32lb52_audio_isr(int irq, FAR void *context, FAR void *arg)
{
  struct sf32lb52_audio_s *priv = (struct sf32lb52_audio_s *)arg;

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

static int sf32lb52_audio_worker(int argc, FAR char *argv[])
{
  FAR struct sf32lb52_audio_s *priv = &g_audio;

  while (1)
    {
      nxsem_wait(&priv->rx_sem);

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
  HAL_AUDCODEC_Close_Analog_ADCPath();
  __HAL_AUDCODEC_ADC_DISABLE(&priv->codec);

  /* 清空 pending apb */
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
   * 参考 SiFli SDK codec_adc_clk_config_pll[3] (16k 项) */
  static AUDCODE_ADC_CLK_CONFIG_TYPE adc_clk_16k =
    { 1, 10, 1, 1, 0, 5, 2 };

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

  /* 默认 16kHz 时钟表 */
  static AUDCODE_ADC_CLK_CONFIG_TYPE adc_clk_default =
    { 1, 10, 1, 1, 0, 5, 2 };
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

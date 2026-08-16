/****************************************************************************
 * mic_capture.c - 板载麦克风采集 (NuttX 音频框架 /dev/audio/audio0)
 *
 * 流程 (与 nxrecorder 一致):
 *   open -> RESERVE -> CONFIGURE(16k/16bit/1ch) -> REGISTERMQ
 *        -> START -> ALLOCBUFFER xN -> ENQUEUEBUFFER xN
 *   read: mq_receive(AUDIO_MSG_DEQUEUE) -> 拷贝 apb->samp -> 重新入队
 *
 * Team 181 - Contest 2026
 ****************************************************************************/

#include <errno.h>
#include <fcntl.h>
#include <mqueue.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <nuttx/audio/audio.h>

#include "mic_capture.h"

#define MIC_DEV_PATH       "/dev/audio/audio0"
#define MIC_NUM_BUFFERS    8
#define MIC_BUFFER_BYTES   4096
#define MIC_MQ_NAME        "/mic_capture_q"
#define MIC_MQ_MSG_SIZE    sizeof(struct audio_msg_s)

/****************************************************************************
 * Private Data
 ****************************************************************************/

static int s_fd = -1;
static mqd_t s_mq = (mqd_t)-1;
static struct ap_buffer_s **s_bufs = NULL;
static int s_nbufs = 0;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int mic_enqueue(struct ap_buffer_s *apb)
{
  struct audio_buf_desc_s desc;

  apb->nbytes = apb->nmaxbytes;
  desc.numbytes = apb->nbytes;
  desc.u.buffer = apb;

  return ioctl(s_fd, AUDIOIOC_ENQUEUEBUFFER, (uintptr_t)&desc);
}

static int mic_alloc_buffers(int nbufs, int bytes)
{
  struct audio_buf_desc_s desc;
  int i;

  s_bufs = calloc(nbufs, sizeof(*s_bufs));
  if (s_bufs == NULL)
    {
      return -ENOMEM;
    }

  for (i = 0; i < nbufs; i++)
    {
      desc.numbytes = bytes;
      desc.u.pbuffer = &s_bufs[i];

      if (ioctl(s_fd, AUDIOIOC_ALLOCBUFFER, (uintptr_t)&desc) !=
          sizeof(desc))
        {
          return -EIO;
        }
    }

  s_nbufs = nbufs;
  return 0;
}

static void mic_free_buffers(void)
{
  struct audio_buf_desc_s desc;
  int i;

  if (s_bufs == NULL)
    {
      return;
    }

  for (i = 0; i < s_nbufs; i++)
    {
      if (s_bufs[i] != NULL)
        {
          desc.u.buffer = s_bufs[i];
          ioctl(s_fd, AUDIOIOC_FREEBUFFER, (uintptr_t)&desc);
          s_bufs[i] = NULL;
        }
    }

  free(s_bufs);
  s_bufs = NULL;
  s_nbufs = 0;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int mic_capture_start(void)
{
  struct audio_caps_desc_s cap_desc;
  struct mq_attr attr;
  struct ap_buffer_info_s binfo;
  int ret;
  int i;

  if (s_fd >= 0)
    {
      return 0;   /* 已启动 */
    }

  s_fd = open(MIC_DEV_PATH, O_RDONLY);
  if (s_fd < 0)
    {
      printf("[mic] open %s failed: %d\n", MIC_DEV_PATH, errno);
      return -errno;
    }

  if (ioctl(s_fd, AUDIOIOC_RESERVE) < 0)
    {
      ret = -errno;
      goto err_close;
    }

  /* CONFIGURE: 16kHz/16bit/单声道 */
  memset(&cap_desc, 0, sizeof(cap_desc));
  cap_desc.caps.ac_len = sizeof(cap_desc.caps);
  cap_desc.caps.ac_type = AUDIO_TYPE_INPUT;
  cap_desc.caps.ac_channels = MIC_CAPTURE_CHANNELS;
  cap_desc.caps.ac_controls.hw[0] = MIC_CAPTURE_RATE;
  cap_desc.caps.ac_controls.b[2] = MIC_CAPTURE_BITS;
  cap_desc.caps.ac_subtype = AUDIO_FMT_PCM;

  if (ioctl(s_fd, AUDIOIOC_CONFIGURE, (uintptr_t)&cap_desc) < 0)
    {
      ret = -errno;
      printf("[mic] CONFIGURE failed: %d\n", errno);
      goto err_reserve;
    }

  /* 消息队列 (收 apb 完成通知) */
  attr.mq_maxmsg = 16;
  attr.mq_msgsize = MIC_MQ_MSG_SIZE;
  attr.mq_flags = 0;
  s_mq = mq_open(MIC_MQ_NAME, O_CREAT | O_RDONLY, 0644, &attr);
  if (s_mq == (mqd_t)-1)
    {
      ret = -errno;
      goto err_reserve;
    }

  if (ioctl(s_fd, AUDIOIOC_REGISTERMQ, (uintptr_t)s_mq) < 0)
    {
      ret = -errno;
      goto err_mq;
    }

  /* 缓冲区 */
  memset(&binfo, 0, sizeof(binfo));
  if (ioctl(s_fd, AUDIOIOC_GETBUFFERINFO, (uintptr_t)&binfo) != OK)
    {
      binfo.buffer_size = MIC_BUFFER_BYTES;
      binfo.nbuffers = MIC_NUM_BUFFERS;
    }

  ret = mic_alloc_buffers(binfo.nbuffers, binfo.buffer_size);
  if (ret < 0)
    {
      goto err_mq;
    }

  for (i = 0; i < s_nbufs; i++)
    {
      if (mic_enqueue(s_bufs[i]) < 0)
        {
          ret = -errno;
          goto err_bufs;
        }
    }

  if (ioctl(s_fd, AUDIOIOC_START) < 0)
    {
      ret = -errno;
      printf("[mic] START failed: %d\n", errno);
      goto err_bufs;
    }

  printf("[mic] capture started (%s)\n", MIC_DEV_PATH);
  return 0;

err_bufs:
  mic_free_buffers();
err_mq:
  ioctl(s_fd, AUDIOIOC_UNREGISTERMQ);
  mq_close(s_mq);
  s_mq = (mqd_t)-1;
err_reserve:
  ioctl(s_fd, AUDIOIOC_RELEASE);
err_close:
  close(s_fd);
  s_fd = -1;
  return ret;
}

int mic_capture_read(int16_t *buf, int max_bytes, int timeout_ms)
{
  struct audio_msg_s msg;
  struct timespec ts;
  struct ap_buffer_s *apb;
  ssize_t n;
  int copied = 0;

  if (s_fd < 0 || buf == NULL || max_bytes <= 0)
    {
      return -EINVAL;
    }

  while (copied < max_bytes)
    {
      if (timeout_ms > 0)
        {
          clock_gettime(CLOCK_REALTIME, &ts);
          ts.tv_sec += timeout_ms / 1000;
          ts.tv_nsec += (timeout_ms % 1000) * 1000000L;
          if (ts.tv_nsec >= 1000000000L)
            {
              ts.tv_sec++;
              ts.tv_nsec -= 1000000000L;
            }

          n = mq_timedreceive(s_mq, (char *)&msg, MIC_MQ_MSG_SIZE,
                              NULL, &ts);
        }
      else
        {
          n = mq_receive(s_mq, (char *)&msg, MIC_MQ_MSG_SIZE, NULL);
        }

      if (n < 0)
        {
          if (errno == ETIMEDOUT)
            {
              break;
            }

          return -errno;
        }

      if (msg.msg_id != AUDIO_MSG_DEQUEUE)
        {
          continue;
        }

      apb = msg.u.ptr;
      if (apb == NULL)
        {
          continue;
        }

      if (copied + (int)apb->nbytes <= max_bytes)
        {
          memcpy((uint8_t *)buf + copied, apb->samp, apb->nbytes);
          copied += apb->nbytes;
        }
      else
        {
          int take = max_bytes - copied;

          memcpy((uint8_t *)buf + copied, apb->samp, take);
          copied += take;
        }

      mic_enqueue(apb);   /* 归还缓冲 */
    }

  return copied;
}

void mic_capture_stop(void)
{
  if (s_fd < 0)
    {
      return;
    }

  ioctl(s_fd, AUDIOIOC_STOP);
  ioctl(s_fd, AUDIOIOC_UNREGISTERMQ);
  mic_free_buffers();
  ioctl(s_fd, AUDIOIOC_RELEASE);
  close(s_fd);
  s_fd = -1;

  if (s_mq != (mqd_t)-1)
    {
      mq_close(s_mq);
      mq_unlink(MIC_MQ_NAME);
      s_mq = (mqd_t)-1;
    }

  printf("[mic] capture stopped\n");
}

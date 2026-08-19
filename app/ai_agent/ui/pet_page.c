/****************************************************************************
 * Pet Display Page
 *
 * Cloud pet page shown when the "桌宠" icon is tapped on the launcher.
 *
 * Layout (reuses the launcher wallpaper, overall padding 20):
 *   ┌──────────────────────────────┐
 *   │ (20)                        │
 *   │  [ 返回 ]                    │   layer 1: back button 60x60, circle
 *   │                              │
 *   │    中午好！今天怎么样？        │   layer 2: AI text (time greeting)
 *   │                              │
 *   │          ( ☁️ 云朵 )          │   layer 3: cloud image, 70% of full
 *   │                              │            width, aspect preserved
 *   │ (20)                        │
 *   └──────────────────────────────┘
 *
 * Cloud behaviors (2026-08-16 精简):
 *  - 动画已移除 (眨眼/弹跳在无 GPU 的 MCU 上重绘开销过大, 是卡顿来源)。
 *  - Touch: 点击云朵 -> 语音提问 -> 本地模型回答显示在文字层;
 *    生成中再点一次 = 打断推理立即释放 CPU。
 *
 * Team 181 - Contest 2026
 ****************************************************************************/

#include <lvgl/lvgl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "pet_page.h"

/* velaAI 端侧 TFLite Micro 语言模型 + 语音提问 (CONFIG_TFLITEMICRO 启用时) */
#ifdef CONFIG_TFLITEMICRO
#  include <pthread.h>
#  include "../ai_lm.h"
#  include "../speech/voice_question.h"
#endif

#include "../chat_log.h"

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* Launcher wallpaper (390x450 ARGB8888, shared with launcher_page.c) */
extern const uint32_t img_background_watch_px[];

/* Cloud pet bitmaps (300x300 ARGB8888, shared with pet_display.c) */
extern const uint32_t pet_cloud_open_px[];
extern const uint32_t pet_cloud_blink_px[];

/* Fonts */
extern const lv_font_t ui_font_sans_24; /* back button glyph */
extern const lv_font_t ui_font_cjk_18;  /* greeting text (CJK subset) */

#define PET_CLOUD_SIZE   300
#define PET_CLOUD_STRIDE (PET_CLOUD_SIZE * 4)
#define PET_CLOUD_WIDTH_PCT 70   /* cloud width = 70% of full page width */

/* (动画已移除: 眨眼/弹跳在低端 MCU 上重绘开销过大, 且是卡顿来源) */

/* Wallpaper image descriptor (same bitmap as the launcher desktop) */
static const lv_image_dsc_t s_bg_dsc = {
    .header = { .magic = LV_IMAGE_HEADER_MAGIC,
                .cf = LV_COLOR_FORMAT_ARGB8888,
                .flags = 0,
                .w = 390,
                .h = 450,
                .stride = 390 * 4,
                .reserved_2 = 0 },
    .data_size = 390 * 450 * 4,
    .data = (const uint8_t *)img_background_watch_px,
    .reserved = NULL,
    .reserved_2 = NULL,
};

/* Cloud image descriptors: open / closed / computed middle frames */
static const lv_image_dsc_t s_cloud_open_dsc = {
    .header = { .magic = LV_IMAGE_HEADER_MAGIC,
                .cf = LV_COLOR_FORMAT_ARGB8888,
                .flags = 0,
                .w = PET_CLOUD_SIZE,
                .h = PET_CLOUD_SIZE,
                .stride = PET_CLOUD_STRIDE,
                .reserved_2 = 0 },
    .data_size = PET_CLOUD_SIZE * PET_CLOUD_STRIDE,
    .data = (const uint8_t *)pet_cloud_open_px,
    .reserved = NULL,
    .reserved_2 = NULL,
};

static const lv_image_dsc_t s_cloud_blink_dsc = {
    .header = { .magic = LV_IMAGE_HEADER_MAGIC,
                .cf = LV_COLOR_FORMAT_ARGB8888,
                .flags = 0,
                .w = PET_CLOUD_SIZE,
                .h = PET_CLOUD_SIZE,
                .stride = PET_CLOUD_STRIDE,
                .reserved_2 = 0 },
    .data_size = PET_CLOUD_SIZE * PET_CLOUD_STRIDE,
    .data = (const uint8_t *)pet_cloud_blink_px,
    .reserved = NULL,
    .reserved_2 = NULL,
};

static lv_obj_t *pet_page_container;
static lv_obj_t *cloud_img;      /* layer 3: the cloud */
static lv_obj_t *ai_text_label;  /* layer 2: AI returned text */
static pet_back_callback_t back_callback = NULL;

static int s_cloud_scale;          /* current scale (256 = 100%) */

/* Touch reply auto-restore */
static lv_timer_t *s_greet_timer;  /* restores the greeting after tap */

/* 对话日志浮层 */
static lv_obj_t *s_log_overlay;

#ifdef CONFIG_TFLITEMICRO
/* ── 语音提问 (点击云朵 -> 录音 -> 识别 -> 本地模型回复) ──
 * 线程模型: worker 线程只写静态结果槽 + 置 ready 标志;
 * UI 线程用 lv_timer 轮询消费。绝不从 worker 直接调任何 lv_* /
 * lv_async_call (跨线程操作 LVGL 会破坏定时器链表 -> 卡死)。 */
static volatile int s_lm_busy;     /* 1 = 录音/生成中 */
static time_t s_busy_since;        /* busy 起始时刻 (看门狗) */
static volatile int s_result_ready;/* worker 写完结果 */
static char s_result_text[AI_LM_REPLY_MAX];
static volatile int s_result_ok;
static lv_timer_t *s_result_timer; /* UI 线程: 轮询 worker 结果 */
#endif

/* Forward declarations */
static void greeting_build(char *buf, size_t len);

/****************************************************************************
 * Touch interaction
 ****************************************************************************/

/**
 * Restore the time greeting after a tap reply
 */
static void greet_restore_timer_cb(lv_timer_t *timer)
{
    char greeting[64];

    lv_timer_delete(timer);
    s_greet_timer = NULL;
    if (ai_text_label != NULL)
    {
        greeting_build(greeting, sizeof(greeting));
        lv_label_set_text(ai_text_label, greeting);
    }
}

#ifdef CONFIG_TFLITEMICRO
/**
 * UI 线程 (lv_timer): 轮询 worker 结果槽, 应用到气泡, 3 秒后恢复问候。
 * 页面被删除时此 timer 一并删除, 结果自然作废 — 无悬挂引用。
 */
static void lm_result_timer_cb(lv_timer_t *timer)
{
  (void)timer;

  /* 看门狗: worker 卡死 (任何未预期路径) 25s 后强制恢复可点击。
   * 正常会话全程可达 ~20s (反应 2s + 说话 2s + 尾判定 0.4s +
   * ASR 1s + LM 推理 9s + flush/适应期 1s + 余量), 15s 会与自然
   * 完成赛跑造成误杀 (真机实测) */
  if (s_lm_busy && !s_result_ready &&
      time(NULL) - s_busy_since > 25)
    {
      printf("[pet] worker watchdog: force unbusy\n");
      s_lm_busy = 0;
      ai_lm_cancel();

      if (ai_text_label != NULL)
        {
          lv_label_set_text(ai_text_label,
                            "\xe5\x88\x9a\xe6\x89\x8d\xe5\x88\xb0\xe4\xba\x86"
                            "\xe4\xb8\x80\xe7\x82\xb9\xe5\xb0\x8f\xe9\x97\xae"
                            "\xe9\xa2\x98\xef\xbc\x8c\xe5\x86\x8d\xe8\xaf\x95"
                            "\xe4\xb8\x80\xe6\xac\xa1\xef\xbc\x9f");
          /* 刚遇到了一点小问题，再试一次？ */
        }

      return;
    }

  if (!s_result_ready)
    {
      return;
    }

  s_result_ready = 0;

  if (ai_text_label != NULL)
    {
      if (s_result_ok)
        {
          lv_label_set_text(ai_text_label, s_result_text);
        }
      else
        {
          /* 模型不可用/生成失败/被打断: 默认话术 */
          lv_label_set_text(ai_text_label,
                            "\xe5\x98\xbf\xef\xbc\x8c\xe6\x88\xb3\xe5\x88\xb0"
                            "\xe6\x88\x91\xe4\xba\x86\xef\xbd\x9e\xe6\x98\xaf"
                            "\xe5\xb0\x8f\xe4\xba\x91\xe5\x91\xa6\xef\xbc\x81");
          /* 嘿，戳到我了～是小云哦！ */
        }

      if (s_greet_timer != NULL)
        {
          lv_timer_delete(s_greet_timer);
        }

      s_greet_timer = lv_timer_create(greet_restore_timer_cb, 3000, NULL);
    }
}

/**
 * 后台线程: 语音提问 -> 本地 TFLM 模型回答;
 * 全程不触碰 LVGL, 只写静态结果槽 + ready 标志。
 * 中途被 UI 打断 (ai_lm_cancel) 时提前退出释放 CPU。
 */
static void *lm_tap_worker(void *arg)
{
  char reply[AI_LM_REPLY_MAX];
  static const char *const chitchat[] = {
    "\xe4\xbd\xa0\xe5\xa5\xbd",                     /* 你好 */
    "\xe4\xbd\xa0\xe8\x83\xbd\xe5\x81\x9a\xe4\xbb\x80\xe4\xb9\x88", /* 你能做什么 */
    "\xe4\xbd\xa0\xe6\x98\xaf\xe8\xb0\x81",         /* 你是谁 */
  };

  (void)arg;

  printf("[pet] worker enter\n");

  /* 清上一次会话残留的取消标志, 否则新会话一进来就判"已取消",
   * 秒回兜底文案 (真机: 无法复现"你好"对话的根因) */
  ai_lm_cancel_clear();

  s_result_ok = 0;
  if (voice_question_ask(reply, sizeof(reply), 4) >= 0)
    {
      strncpy(s_result_text, reply, AI_LM_REPLY_MAX - 1);
      s_result_text[AI_LM_REPLY_MAX - 1] = '\0';
      s_result_ok = 1;

      /* 语音问句已在 voice_question_ask 内记录, 此处记回复 */
      chat_log_add(CHAT_LOG_ANSWER, s_result_text);
    }
  else if (!ai_lm_cancel_check() && ai_lm_init() == 0)
    {
      /* 语音链路失败: 用闲聊话术演示本地模型 */
      int pick = (int)(time(NULL) & 0x7fffffff) %
                 (int)(sizeof(chitchat) / sizeof(chitchat[0]));

      chat_log_add(CHAT_LOG_ASK, chitchat[pick]);

      if (ai_lm_agent_reply(chitchat[pick], reply, sizeof(reply)) == 0)
        {
          strncpy(s_result_text, reply, AI_LM_REPLY_MAX - 1);
          s_result_text[AI_LM_REPLY_MAX - 1] = '\0';
          s_result_ok = 1;

          chat_log_add(CHAT_LOG_ANSWER, s_result_text);
        }
    }

  /* 被打断的会话不写结果: UI 已显示"已打断"提示, 若在此再写结果
   * 会被兜底文案覆盖 */
  if (!ai_lm_cancel_check())
    {
      s_result_ready = 1;   /* 交给 UI 线程消费 */
    }

  s_lm_busy = 0;
  printf("[pet] worker exit\n");
  return NULL;
}
#endif /* CONFIG_TFLITEMICRO */

/**
 * Cloud tapped: 语音提问 (本地模型回答);
 * 生成中再点一次 = 打断当前推理立即释放 CPU
 */
static void on_cloud_clicked(lv_event_t *e)
{
    (void)e;
    printf("[PetPage] Cloud tapped\n");

#ifdef CONFIG_TFLITEMICRO
    if (s_lm_busy)
    {
        /* 打断: 逐 token 检查的推理立即中止, CPU 交还 UI */
        ai_lm_cancel();

        if (ai_text_label != NULL)
        {
            lv_label_set_text(ai_text_label,
                              "\xe5\xb7\xb2\xe6\x89\x93\xe6\x96\xad\xef\xbc\x8c"
                              "\xe5\x86\x8d\xe7\x82\xb9\xe4\xb8\x80\xe6\xac\xa1"
                              "\xe9\x87\x8d\xe6\x96\xb0\xe6\x8f\x90\xe9\x97\xae");
            /* 已打断，再点一次重新提问 */
        }

        return;
    }

    {
        pthread_attr_t attr;
        struct sched_param sp;

        s_lm_busy = 1;
        s_busy_since = time(NULL);
        s_result_ready = 0;

        /* 提示用户开始说话 (worker 完成后替换为回答) */
        if (ai_text_label != NULL)
        {
            lv_label_set_text(ai_text_label,
                              "\xe6\x88\x91\xe5\x9c\xa8\xe5\x90\xac\xef\xbc\x8c"
                              "\xe8\xaf\xb7\xe8\xaf\xb4\xe5\x90\xa7\xef\xbd\x9e");
            /* 我在听，请说吧～ */
        }

        pthread_attr_init(&attr);

        /* 栈要容得下 ASR+LM 推理 (TFLM 工作集); 16KB 会溢出 */
        pthread_attr_setstacksize(&attr, 49152);

        /* 优先级低于 UI 线程 (100): 推理期间 UI 可抢占保持流畅 */
        sp.sched_priority = 150;
        pthread_attr_setschedparam(&attr, &sp);

        /* detach: 无需 join, 结束自动回收 (避免线程资源泄漏) */
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

        {
            pthread_t tid;

            if (pthread_create(&tid, &attr, lm_tap_worker, NULL) != 0)
            {
                s_lm_busy = 0;
            }
        }

        pthread_attr_destroy(&attr);
    }
#else
    /* Playful reply in the AI text layer, restore greeting after 3s */
    if (ai_text_label != NULL)
    {
        lv_label_set_text(ai_text_label,
                          "\xe5\x98\xbf\xef\xbc\x8c\xe6\x88\xb3\xe5\x88\xb0"
                          "\xe6\x88\x91\xe4\xba\x86\xef\xbd\x9e\xe6\x98\xaf"
                          "\xe5\xb0\x8f\xe4\xba\x91\xe5\x91\xa6\xef\xbc\x81");
        /* 嘿，戳到我了～是小云哦！ */
        if (s_greet_timer != NULL)
        {
            lv_timer_delete(s_greet_timer);
        }
        s_greet_timer = lv_timer_create(greet_restore_timer_cb, 3000, NULL);
    }
#endif
}

/****************************************************************************
 * 对话日志浮层
 ****************************************************************************/

/**
 * Close the log overlay
 */
static void on_log_close_clicked(lv_event_t *e)
{
    (void)e;

    if (s_log_overlay != NULL)
    {
        lv_obj_del(s_log_overlay);
        s_log_overlay = NULL;
    }
}

/**
 * Open the conversation log overlay (full screen, newest entry first)
 */
static void on_log_clicked(lv_event_t *e)
{
    lv_obj_t *head;
    lv_obj_t *title;
    lv_obj_t *close_btn;
    lv_obj_t *close_label;
    lv_obj_t *list;
    char line[192];
    int n;
    int i;

    (void)e;

    if (s_log_overlay != NULL)
    {
        return;   /* 已打开 */
    }

    /* 挂在页面容器下 (随页面删除, 不会残留到桌面); 忽略 flex 布局 */
    s_log_overlay = lv_obj_create(pet_page_container);
    lv_obj_add_flag(s_log_overlay, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_size(s_log_overlay, 390, 450);
    lv_obj_set_pos(s_log_overlay, 0, 0);
    lv_obj_set_style_bg_color(s_log_overlay, lv_color_hex(0xf7f7f4), 0);
    lv_obj_set_style_bg_opa(s_log_overlay, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_log_overlay, 0, 0);
    lv_obj_set_style_pad_all(s_log_overlay, 16, 0);
    lv_obj_set_layout(s_log_overlay, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(s_log_overlay, LV_FLEX_FLOW_COLUMN);

    /* ── 标题行: "对话日志" + 关闭按钮 ── */
    head = lv_obj_create(s_log_overlay);
    lv_obj_set_width(head, lv_pct(100));
    lv_obj_set_height(head, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(head, LV_OPA_0, 0);
    lv_obj_set_style_border_width(head, 0, 0);
    lv_obj_set_style_pad_all(head, 0, 0);
    lv_obj_set_layout(head, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(head, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(head, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_SPACE_BETWEEN);

    title = lv_label_create(head);
    lv_label_set_text(title, "\xe5\xaf\xb9\xe8\xaf\x9d\xe6\x97\xa5\xe5\xbf\x97");
    /* 对话日志 */
    lv_obj_set_style_text_font(title, &ui_font_cjk_18, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x2b2b2b), 0);

    close_btn = lv_btn_create(head);
    lv_obj_set_size(close_btn, 72, 44);
    lv_obj_set_style_bg_color(close_btn, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_bg_opa(close_btn, LV_OPA_70, 0);
    lv_obj_set_style_border_width(close_btn, 0, 0);
    lv_obj_set_style_radius(close_btn, 22, 0);

    close_label = lv_label_create(close_btn);
    lv_label_set_text(close_label, "\xe8\xbf\x94\xe5\x9b\x9e");
    /* 返回 */
    lv_obj_set_style_text_font(close_label, &ui_font_cjk_18, 0);
    lv_obj_set_style_text_color(close_label, lv_color_hex(0x2b2b2b), 0);
    lv_obj_center(close_label);

    lv_obj_add_event_cb(close_btn, on_log_close_clicked,
                        LV_EVENT_CLICKED, NULL);

    /* ── 滚动列表: 最新在最上 ── */
    list = lv_obj_create(s_log_overlay);
    lv_obj_set_width(list, lv_pct(100));
    lv_obj_set_flex_grow(list, 1);
    lv_obj_set_style_bg_opa(list, LV_OPA_0, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_pad_all(list, 0, 0);
    lv_obj_set_layout(list, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    n = chat_log_count();

    for (i = 0; i < n; i++)
    {
        lv_obj_t *lbl;

        if (chat_log_get(i, line, sizeof(line)) != 0)
        {
            break;
        }

        lbl = lv_label_create(list);
        lv_label_set_text(lbl, line);
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(lbl, lv_pct(100));
        lv_obj_set_style_text_font(lbl, &ui_font_cjk_18, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0x3a3a3a), 0);
        lv_obj_set_style_margin_top(lbl, 8, 0);
    }

    if (n == 0)
    {
        lv_obj_t *empty = lv_label_create(list);

        lv_label_set_text(empty,
                          "\xe6\x9a\x82\xe6\x97\xa0\xe5\xaf\xb9\xe8\xaf\x9d"
                          "\xe8\xae\xb0\xe5\xbd\x95");
        /* 暂无对话记录 */
        lv_obj_set_style_text_font(empty, &ui_font_cjk_18, 0);
        lv_obj_set_style_text_color(empty, lv_color_hex(0x999999), 0);
        lv_obj_set_style_margin_top(empty, 24, 0);
    }

    printf("[PetPage] Log overlay opened (%d entries)\n", n);
}

/****************************************************************************
 * Private helpers
 ****************************************************************************/

/**
 * Handle back button click
 */
static void on_back_clicked(lv_event_t *e)
{
    (void)e;
    printf("[PetPage] Back button clicked\n");
    if (back_callback != NULL)
    {
        back_callback();
    }
}

/**
 * Release all pet page resources (timers, computed frames)
 */
static void pet_cleanup(void)
{
#ifdef CONFIG_TFLITEMICRO
    /* 离开页面: 打断仍在跑的推理, 删除结果轮询定时器
     * (worker 之后写的结果槽无人消费, 自然作废, 无悬挂引用) */
    ai_lm_cancel();

    if (s_result_timer != NULL)
    {
        lv_timer_delete(s_result_timer);
        s_result_timer = NULL;
    }
#endif

    if (s_greet_timer != NULL)
    {
        lv_timer_delete(s_greet_timer);
        s_greet_timer = NULL;
    }

    /* 日志浮层随页面容器一起删除, 仅清指针 */
    s_log_overlay = NULL;

    cloud_img = NULL;
    ai_text_label = NULL;
}

/**
 * Container delete event: LVGL calls this when the page object is deleted
 * (launcher deletes the page via lv_obj_del, not pet_page_delete()).
 */
static void on_container_delete(lv_event_t *e)
{
    (void)e;
    pet_cleanup();
}

/**
 * Pick the greeting prefix from the current hour:
 *   05:00-10:59 -> 早上好   11:00-12:59 -> 中午好
 *   13:00-17:59 -> 下午好   18:00-04:59 -> 晚上好
 */
static const char *greeting_prefix(int hour)
{
    if (hour >= 5 && hour < 11)
    {
        return "\xe6\x97\xa9\xe4\xb8\x8a\xe5\xa5\xbd"; /* 早上好 */
    }
    if (hour >= 11 && hour < 13)
    {
        return "\xe4\xb8\xad\xe5\x8d\x88\xe5\xa5\xbd"; /* 中午好 */
    }
    if (hour >= 13 && hour < 18)
    {
        return "\xe4\xb8\x8b\xe5\x8d\x88\xe5\xa5\xbd"; /* 下午好 */
    }
    return "\xe6\x99\x9a\xe4\xb8\x8a\xe5\xa5\xbd";     /* 晚上好 */
}

/**
 * Build the default greeting: "{早上|中午|下午|晚上}好！今天怎么样？"
 *
 * @param buf  Output buffer
 * @param len  Buffer size
 */
static void greeting_build(char *buf, size_t len)
{
    time_t now;
    struct tm tmv;

    now = time(NULL);
    if (localtime_r(&now, &tmv) == NULL)
    {
        tmv.tm_hour = 12; /* fall back to 中午好 */
    }
    snprintf(buf, len, "%s\xef\xbc\x81\xe4\xbb\x8a\xe5\xa4\xa9\xe6\x80"
                        "\x8e\xe4\xb9\x88\xe6\xa0\xb7\xef\xbc\x9f",
             greeting_prefix(tmv.tm_hour)); /* ！今天怎么样？ */
}

/****************************************************************************
 * Public API
 ****************************************************************************/

/**
 * Create the pet display page
 *
 * @return Pet page container object, or NULL on failure
 */
lv_obj_t *pet_page_create(void)
{
    lv_obj_t *row1;      /* layer 1: back button */
    lv_obj_t *back_btn;
    lv_obj_t *back_label;
    lv_obj_t *row3;      /* layer 3: cloud image */
    char greeting[64];
    int full_w;
    lv_coord_t cloud_w;

    printf("[PetPage] Creating pet display page\n");

    /* Create pet page container: full-screen, launcher wallpaper */
    pet_page_container = lv_obj_create(lv_scr_act());
    lv_obj_set_size(pet_page_container, 390, 450);
    lv_obj_set_pos(pet_page_container, 0, 0);
    lv_obj_set_style_bg_image_src(pet_page_container, &s_bg_dsc, 0);
    lv_obj_set_style_bg_image_opa(pet_page_container, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(pet_page_container, 0, 0);
    lv_obj_set_style_pad_all(pet_page_container, 20, 0); /* 整体内边距 20 */
    lv_obj_add_event_cb(pet_page_container, on_container_delete,
                        LV_EVENT_DELETE, NULL);

    /* 垂直布局，三层 */
    lv_obj_set_layout(pet_page_container, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(pet_page_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(pet_page_container, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    /* ── 第一层：返回按钮，全圆角 60x60 ── */
    row1 = lv_obj_create(pet_page_container);
    lv_obj_set_width(row1, lv_pct(100));
    lv_obj_set_height(row1, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row1, LV_OPA_0, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(row1, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(row1, 0, 0);
    lv_obj_set_layout(row1, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(row1, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row1, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    back_btn = lv_btn_create(row1);
    lv_obj_set_size(back_btn, 60, 60); /* 60x60 圆形按钮 */
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_bg_opa(back_btn, LV_OPA_50, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(back_btn, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(back_btn, 0, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(back_btn, 0, LV_STATE_FOCUSED);
    lv_obj_set_style_border_width(back_btn, 0, LV_STATE_FOCUS_KEY);
    lv_obj_set_style_radius(back_btn, LV_RADIUS_CIRCLE, 0); /* 全圆角 */

    back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, "<");
    lv_obj_set_style_text_font(back_label, &ui_font_sans_24, 0);
    lv_obj_set_style_text_color(back_label, lv_color_hex(0x2b2b2b), 0);
    lv_obj_center(back_label);

    lv_obj_add_event_cb(back_btn, on_back_clicked, LV_EVENT_CLICKED, NULL);

    /* ── 日志按钮 (与返回按钮同行) ── */
    {
        lv_obj_t *log_btn = lv_btn_create(row1);
        lv_obj_t *log_label;

        lv_obj_set_size(log_btn, 88, 60);
        lv_obj_set_style_bg_color(log_btn, lv_color_hex(0xffffff), 0);
        lv_obj_set_style_bg_opa(log_btn, LV_OPA_50, LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(log_btn, 0, LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(log_btn, 0, LV_STATE_PRESSED);
        lv_obj_set_style_border_width(log_btn, 0, LV_STATE_FOCUSED);
        lv_obj_set_style_border_width(log_btn, 0, LV_STATE_FOCUS_KEY);
        lv_obj_set_style_radius(log_btn, 30, 0);
        lv_obj_set_style_margin_left(log_btn, 12, 0);

        log_label = lv_label_create(log_btn);
        lv_label_set_text(log_label,
                          "\xe6\x97\xa5\xe5\xbf\x97");   /* 日志 */
        lv_obj_set_style_text_font(log_label, &ui_font_cjk_18, 0);
        lv_obj_set_style_text_color(log_label, lv_color_hex(0x2b2b2b), 0);
        lv_obj_center(log_label);

        lv_obj_add_event_cb(log_btn, on_log_clicked,
                            LV_EVENT_CLICKED, NULL);
    }

    /* ── 第二层：AI 返回的文字（默认按时段问候） ── */
    greeting_build(greeting, sizeof(greeting));
    ai_text_label = lv_label_create(pet_page_container);
    lv_label_set_text(ai_text_label, greeting);
    lv_label_set_long_mode(ai_text_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(ai_text_label, lv_pct(100));
    lv_obj_set_style_text_font(ai_text_label, &ui_font_cjk_18, 0);
    lv_obj_set_style_text_color(ai_text_label, lv_color_hex(0x2b2b2b), 0);
    lv_obj_set_style_text_align(ai_text_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_margin_top(ai_text_label, 28, 0);

    /* ── 第三层：云朵图片，默认宽度为全宽的 70%，保持比例 ── */
    row3 = lv_obj_create(pet_page_container);
    lv_obj_set_width(row3, lv_pct(100));
    lv_obj_set_style_bg_opa(row3, LV_OPA_0, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(row3, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(row3, 0, 0);
    lv_obj_set_flex_grow(row3, 1); /* 占满剩余高度，图片在层内居中 */
    lv_obj_set_layout(row3, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(row3, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row3, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    full_w = lv_display_get_horizontal_resolution(NULL);
    cloud_w = full_w * PET_CLOUD_WIDTH_PCT / 100;   /* 70% of full width */
    s_cloud_scale = (int)cloud_w * 256 / PET_CLOUD_SIZE; /* keep ratio */

    cloud_img = lv_image_create(row3);
    lv_image_set_src(cloud_img, &s_cloud_open_dsc);
    lv_image_set_scale(cloud_img, s_cloud_scale);
    lv_obj_set_size(cloud_img, cloud_w, cloud_w); /* square bitmap: h = w */

    /* 触摸交互：点击云朵 -> 语音提问 (本地模型回答) */
    lv_obj_add_flag(cloud_img, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(cloud_img, on_cloud_clicked, LV_EVENT_CLICKED, NULL);

#ifdef CONFIG_TFLITEMICRO
    /* UI 线程结果轮询 (worker 只写结果槽, 不触碰 LVGL) */
    s_result_timer = lv_timer_create(lm_result_timer_cb, 100, NULL);
#endif

    printf("[PetPage] Pet display page created successfully\n");
    return pet_page_container;
}

/**
 * Set the back button callback
 *
 * @param callback Function to call when back button is pressed.
 *                 Set to NULL to disable.
 */
void pet_page_set_back_callback(pet_back_callback_t callback)
{
    back_callback = callback;
}

/**
 * Update the AI returned text in the text layer
 *
 * @param text Response text to display (UTF-8)
 */
void pet_page_update_response(const char *text)
{
    if (ai_text_label != NULL && text != NULL)
    {
        lv_label_set_text(ai_text_label, text);
        printf("[PetPage] Response updated: %s\n", text);
    }
}

/**
 * Update the pet status text
 *
 * The three-layer design has no dedicated status line; kept as a no-op
 * for API compatibility.
 *
 * @param status Status text (ignored)
 */
void pet_page_update_status(const char *status)
{
    (void)status;
}

/**
 * Clean up pet page resources
 */
void pet_page_delete(void)
{
    printf("[PetPage] Deleting pet page\n");
    if (pet_page_container != NULL)
    {
        lv_obj_del(pet_page_container); /* triggers on_container_delete */
        pet_page_container = NULL;
    }
    back_callback = NULL;
}

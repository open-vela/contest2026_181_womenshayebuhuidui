/****************************************************************************
 * Contest 2026 team 181 - AI Agent App with LVGL Display
 *
 * AI agent with AMOLED display support for SF32LB52-DevKit-LCD.
 * Displays "你好HerSen" on startup and shows AI responses on screen.
 ****************************************************************************/

#include <nuttx/config.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* LVGL headers */

#include <lvgl/lvgl.h>
#include <lvgl/src/drivers/nuttx/lv_nuttx_entry.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define AI_AGENT_MAX_INPUT_LEN    256
#define AI_AGENT_PROMPT           "ai> "
#define AI_AGENT_TITLE            "你好HerSen"

/****************************************************************************
 * Private Data
 ****************************************************************************/

static lv_obj_t *g_title_label = NULL;    /* Title label */
static lv_obj_t *g_response_label = NULL; /* Response label */
static lv_obj_t *g_status_label = NULL;   /* Status label */

/****************************************************************************
 * Simulated AI Responses
 ****************************************************************************/

typedef struct
{
  const char *keyword;
  const char *response;
} ai_response_t;

static const ai_response_t g_responses[] =
{
  {"hello",    "你好! I'm your AI assistant on SF32LB52. 有什么可以邦到您？"},
  {"nuttx",    "NuttX is a real-time operating system (RTOS) with a small footprint. It's POSIX-compatible and great for IoT devices!"},
  {"vela",     "OpenVela is Xiaomi's open-source IoT platform based on NuttX. It supports many hardware platforms including the SF32LB52!"},
  {"sifli",    "SiFli makes the SF32LB52 chip - a powerful ARM Cortex-M33 MCU with BLE, WiFi, and display support."},
  {"weather",  "I'm running on a development board without sensors, so I can't check the weather. But you could connect a sensor!"},
  {"help",     "Available commands:\n  - Ask any question\n  - Type 'quit' to exit\n  - Try keywords: nuttx, vela, sifli, hello"},
  {"lcd",      "The SF32LB52-DevKit-LCD has a 1.85 inch 390x450 CO5300 AMOLED display with capacitive touch!"},
  {"default",  "That's an interesting question! I'm a simple AI agent running on embedded hardware. For full AI capabilities, connect to the cloud."},
};

#define NUM_RESPONSES (sizeof(g_responses) / sizeof(g_responses[0]))

/****************************************************************************
 * LVGL Display Functions
 ****************************************************************************/

/****************************************************************************
 * Name: display_init
 *
 * Description:
 *   Initialize LVGL and create the display layout.
 *
 ****************************************************************************/

static int display_init(void)
{
  lv_nuttx_result_t result;
  lv_nuttx_dsc_t info;

  printf("Initializing LVGL display...\n");

  /* Initialize LVGL */

  lv_init();

  /* Initialize NuttX display driver */

  lv_nuttx_dsc_init(&info);
  info.fb_path = "/dev/lcd0";
  info.input_path = "/dev/input0";

  lv_nuttx_init(&info, &result);

  if (result.disp == NULL)
    {
      printf("ERROR: Failed to initialize LVGL NuttX driver\n");
      return -1;
    }

  /* Create black background */

  lv_obj_t *scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, lv_color_black(), 0);

  /* Create title label - "你好HerSen" */

  g_title_label = lv_label_create(scr);
  lv_label_set_text(g_title_label, AI_AGENT_TITLE);
  lv_obj_set_style_text_color(g_title_label, lv_color_make(0, 255, 0), 0);
  lv_obj_set_style_text_font(g_title_label, &lv_font_simsun_16_cjk, 0);
  lv_obj_align(g_title_label, LV_ALIGN_TOP_MID, 0, 20);

  /* Create status label */

  g_status_label = lv_label_create(scr);
  lv_label_set_text(g_status_label, "AI Agent Ready");
  lv_obj_set_style_text_color(g_status_label, lv_color_make(200, 200, 200), 0);
  lv_obj_set_style_text_font(g_status_label, &lv_font_simsun_16_cjk, 0);
  lv_obj_align(g_status_label, LV_ALIGN_TOP_MID, 0, 50);

  /* Create response label - for AI responses */

  g_response_label = lv_label_create(scr);
  lv_label_set_text(g_response_label, "Waiting for query...");
  lv_obj_set_style_text_color(g_response_label, lv_color_white(), 0);
  lv_obj_set_style_text_font(g_response_label, &lv_font_simsun_16_cjk, 0);
  lv_obj_set_width(g_response_label, 370);
  lv_label_set_long_mode(g_response_label, LV_LABEL_LONG_WRAP);
  lv_obj_align(g_response_label, LV_ALIGN_TOP_LEFT, 10, 90);

  /* Run one round of lv_timer_handler to refresh display */

  lv_timer_handler();

  printf("Display initialized successfully!\n");
  return 0;
}

/****************************************************************************
 * Name: display_update_response
 *
 * Description:
 *   Update the response label with new text.
 *
 ****************************************************************************/

static void display_update_response(const char *text)
{
  if (g_response_label != NULL)
    {
      lv_label_set_text(g_response_label, text);
      lv_timer_handler();
    }
}

/****************************************************************************
 * Name: display_update_status
 *
 * Description:
 *   Update the status label.
 *
 ****************************************************************************/

static void display_update_status(const char *status)
{
  if (g_status_label != NULL)
    {
      lv_label_set_text(g_status_label, status);
      lv_timer_handler();
    }
}

/****************************************************************************
 * AI Agent Functions
 ****************************************************************************/

static void ai_agent_show_usage(void)
{
  printf("Usage: ai_agent [options]\n"
         "  -h          : show this help\n"
         "  -q <query>  : send a query\n"
         "  -n          : no display (skip LVGL init)\n"
         "  -i          : interactive mode (default)\n");
}

static const char *ai_agent_find_response(const char *query)
{
  int i;
  char lower_query[AI_AGENT_MAX_INPUT_LEN];

  /* Convert query to lowercase for matching */

  strncpy(lower_query, query, sizeof(lower_query) - 1);
  lower_query[sizeof(lower_query) - 1] = '\0';

  for (i = 0; lower_query[i]; i++)
    {
      if (lower_query[i] >= 'A' && lower_query[i] <= 'Z')
        {
          lower_query[i] = lower_query[i] + 32;
        }
    }

  /* Search for keyword match */

  for (i = 0; i < NUM_RESPONSES - 1; i++)
    {
      if (strstr(lower_query, g_responses[i].keyword) != NULL)
        {
          return g_responses[i].response;
        }
    }

  /* Return default response */

  return g_responses[NUM_RESPONSES - 1].response;
}

static int ai_agent_query(const char *query)
{
  const char *response;

  printf("Thinking...\n");
  display_update_status("Thinking...");

  /* Simulate processing time */

  usleep(500000);  /* 0.5 second */

  /* Find response */

  response = ai_agent_find_response(query);

  /* Display on screen */

  display_update_response(response);
  display_update_status("AI Agent Ready");

  /* Also print to console */

  printf("\n--- AI Response ---\n");
  printf("%s\n", response);
  printf("--- End ---\n\n");

  return 0;
}

static int ai_agent_interactive(void)
{
  char input[AI_AGENT_MAX_INPUT_LEN];

  printf("\n=== AI Agent Interactive Mode ===\n");
  printf("Type your questions or 'quit' to exit.\n");
  printf("Try: hello, nuttx, vela, sifli, lcd, help\n\n");

  display_update_status("Interactive Mode - Type on console");

  while (1)
    {
      printf("%s", AI_AGENT_PROMPT);
      fflush(stdout);

      /* Run LVGL while waiting for input */

      lv_timer_handler();

      /* Read input */

      if (fgets(input, sizeof(input), stdin) == NULL)
        {
          break;
        }

      /* Remove newline */

      input[strcspn(input, "\n")] = '\0';

      /* Check for exit command */

      if (strcmp(input, "quit") == 0 || strcmp(input, "exit") == 0)
        {
          printf("Goodbye!\n");
          display_update_status("Goodbye!");
          break;
        }

      /* Skip empty input */

      if (strlen(input) == 0)
        {
          continue;
        }

      /* Process the query */

      ai_agent_query(input);
    }

  return 0;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: ai_agent_main
 *
 * Description:
 *   AI Agent application entry point.
 *
 ****************************************************************************/

int main(int argc, char *argv[])
{
  int ret = 0;
  int opt;
  const char *query = NULL;
  bool no_display = false;

  /* Parse command line arguments */

  while ((opt = getopt(argc, argv, "hq:n")) != -1)
    {
      switch (opt)
        {
          case 'h':
            ai_agent_show_usage();
            return EXIT_SUCCESS;

          case 'q':
            query = optarg;
            break;

          case 'n':
            no_display = true;
            break;

          default:
            ai_agent_show_usage();
            return EXIT_FAILURE;
        }
    }

  printf("========================================\n");
  printf("  AI Agent v2.0 - SF32LB52-DevKit-LCD\n");
  printf("  Contest 2026 Team 181\n");
  printf("  With AMOLED Display Support\n");
  printf("========================================\n\n");

  /* Initialize display (unless -n is specified) */

  if (!no_display)
    {
      ret = display_init();
      if (ret != 0)
        {
          printf("WARNING: Display init failed, continuing with console only\n");
        }
    }
  else
    {
      printf("Display skipped (console only mode)\n");
    }

  /* Process based on mode */

  if (query != NULL)
    {
      /* Single query mode */

      ret = ai_agent_query(query);
      usleep(3000000);  /* Wait 3 seconds to show result */
    }
  else
    {
      /* Default: enter interactive mode */

      ret = ai_agent_interactive();
    }

  return (ret == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

/****************************************************************************
 * Launcher Page - Application Desktop
 *
 * Provides a desktop-like UI with app icons that can be clicked to enter
 * different application pages (pet display, settings, about, etc.).
 *
 * Team 181 - Contest 2026
 ****************************************************************************/

#include <lvgl/lvgl.h>
#include "launcher_page.h"
#include "pet_page.h"
#include "settings_page.h"

/****************************************************************************
 * Private Data
 ****************************************************************************/

static lv_obj_t *desktop_page;
static lv_obj_t *pet_icon_btn;
static lv_obj_t *settings_icon_btn;
static lv_obj_t *about_icon_btn;

/****************************************************************************
 * Icon Click Callbacks
 ****************************************************************************/

/**
 * Pet icon clicked - enter pet display page
 */
static void on_pet_icon_clicked(lv_event_t *e)
{
    printf("[Launcher] Pet icon clicked - entering pet page\n");
    launcher_enter_page(PAGE_PET);
}

/**
 * Settings icon clicked - enter settings page
 */
static void on_settings_icon_clicked(lv_event_t *e)
{
    printf("[Launcher] Settings icon clicked - entering settings page\n");
    launcher_enter_page(PAGE_SETTINGS);
}

/**
 * About icon clicked - enter about page
 */
static void on_about_icon_clicked(lv_event_t *e)
{
    printf("[Launcher] About icon clicked - entering about page\n");
    launcher_enter_page(PAGE_ABOUT);
}

/****************************************************************************
 * Icon Creation Helper
 ****************************************************************************/

/**
 * Create an app icon button with emoji label
 *
 * @param parent Parent container
 * @param emoji Icon emoji/unicode character
 * @param label Button label text
 * @param x X position
 * @param y Y position
 * @return Icon button object
 */
static lv_obj_t *create_icon_button(lv_obj_t *parent, const char *emoji,
                                     const char *label, lv_coord_t x, lv_coord_t y)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 80, 90);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x2a2a4a), 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(0x4a4a6a), 0);
    lv_obj_set_style_border_width(btn, 2, 0);
    lv_obj_set_style_radius(btn, 12, 0);

    /* Emoji icon (using large font) */
    lv_obj_t *icon_label = lv_label_create(btn);
    lv_label_set_text(icon_label, emoji);
    lv_obj_set_style_text_font(icon_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(icon_label, lv_color_hex(0xffd700), 0); /* Gold */
    lv_obj_center(icon_label);

    /* Button label */
    lv_obj_t *text_label = lv_label_create(btn);
    lv_label_set_text(text_label, label);
    lv_obj_set_style_text_font(text_label, &lv_font_simsun_12_cjk, 0);
    lv_obj_set_style_text_color(text_label, lv_color_hex(0xffffff), 0);
    lv_obj_align(text_label, LV_ALIGN_BOTTOM_MID, 0, -5);

    return btn;
}

/****************************************************************************
 * Public API
 ****************************************************************************/

/**
 * Create the launcher desktop page
 *
 * This creates the main desktop with app icons. Call this first in ui_build().
 */
void launcher_create(void)
{
    printf("[Launcher] Creating desktop page...\n");

    /* Create desktop container on active screen */

    desktop_page = lv_obj_create(lv_scr_act());
    lv_obj_set_size(desktop_page, 390, 450);
    lv_obj_set_pos(desktop_page, 0, 0);
    lv_obj_set_style_bg_color(desktop_page, lv_color_hex(0x1a1a2e), 0); /* Dark blue */
    lv_obj_set_style_border_width(desktop_page, 0, 0);
    lv_obj_set_style_pad_all(desktop_page, 0, 0);

    /* Desktop title */

    lv_obj_t *title = lv_label_create(desktop_page);
    lv_label_set_text(title, "🖥️  AI Agent 桌面");
    lv_obj_set_style_text_font(title, &lv_font_simsun_16_cjk, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x88ccff), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 15);

    /* App icons - Grid layout (3 columns centered) */

    const int icon_width = 80;
    const int icon_height = 90;
    const int spacing_x = 15;
    const int spacing_y = 20;
    const int start_x = (390 - (3 * icon_width + 2 * spacing_x)) / 2;
    const int start_y = 60;

    /* Row 1: Pet (left), Settings (center), About (right) */

    pet_icon_btn = create_icon_button(desktop_page, "☁️", "桌宠",
                                       start_x, start_y);
    lv_obj_add_event_cb(pet_icon_btn, on_pet_icon_clicked,
                        LV_EVENT_CLICKED, NULL);

    settings_icon_btn = create_icon_button(desktop_page, "⚙️", "设置",
                                            start_x + icon_width + spacing_x, start_y);
    lv_obj_add_event_cb(settings_icon_btn, on_settings_icon_clicked,
                        LV_EVENT_CLICKED, NULL);

    about_icon_btn = create_icon_button(desktop_page, "ℹ️", "关于",
                                         start_x + 2 * (icon_width + spacing_x), start_y);
    lv_obj_add_event_cb(about_icon_btn, on_about_icon_clicked,
                        LV_EVENT_CLICKED, NULL);

    /* Footer info */

    lv_obj_t *footer = lv_label_create(desktop_page);
    lv_label_set_text(footer, "Team 181 - Contest 2026");
    lv_obj_set_style_text_font(footer, &lv_font_simsun_12_cjk, 0);
    lv_obj_set_style_text_color(footer, lv_color_hex(0x666688), 0);
    lv_obj_align(footer, LV_ALIGN_BOTTOM_MID, 0, -15);

    printf("[Launcher] Desktop created successfully\n");
}

/**
 * Enter a specific application page
 *
 * @param page Page ID (PAGE_PET, PAGE_SETTINGS, PAGE_ABOUT)
 */
void launcher_enter_page(launcher_page_id_t page)
{
    lv_obj_t *target_page = NULL;

    switch (page)
    {
        case PAGE_PET:
            printf("[Launcher] Entering pet page\n");
            target_page = pet_page_create();
            if (target_page == NULL)
            {
                printf("[Launcher] ERROR: Failed to create pet page\n");
                return;
            }
            pet_page_set_back_callback(launcher_back_to_desktop);
            break;

        case PAGE_SETTINGS:
            printf("[Launcher] Entering settings page\n");
            target_page = settings_page_create();
            if (target_page == NULL)
            {
                printf("[Launcher] ERROR: Failed to create settings page\n");
                return;
            }
            settings_page_set_back_callback(launcher_back_to_desktop);
            break;

        case PAGE_ABOUT:
            printf("[Launcher] Entering about page\n");
            /* TODO: Implement about_page_create() */
            printf("[Launcher] About page not implemented yet\n");
            return;

        default:
            printf("[Launcher] Unknown page ID: %d\n", page);
            return;
    }

    if (target_page != NULL)
    {
        lv_obj_move_foreground(target_page);
    }
}

/**
 * Return to desktop from current page
 */
void launcher_back_to_desktop(void)
{
    printf("[Launcher] Returning to desktop\n");

    /* Destroy current page (except desktop) */

    lv_obj_t *current = lv_screen_active();
    if (current != desktop_page)
    {
        /* Destroy the current page object */

        lv_obj_del(current);
    }

    /* Ensure desktop is in foreground */

    lv_obj_move_foreground(desktop_page);

    /* Refresh display */

    lv_timer_handler();
}

/**
 * Check if currently on desktop
 *
 * @return true if on desktop page, false otherwise
 */
bool launcher_is_on_desktop(void)
{
    lv_obj_t *current = lv_screen_active();
    return (current == desktop_page);
}

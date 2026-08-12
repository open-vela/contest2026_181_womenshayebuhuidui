/****************************************************************************
 * UI Module Header
 *
 * Unified include file for all UI pages.
 *
 * Team 181 - Contest 2026
 ****************************************************************************/

#ifndef __UI_H
#define __UI_H

/****************************************************************************
 * Included Headers
 ****************************************************************************/

#include "launcher_page.h"
#include "pet_page.h"
#include "settings_page.h"

/****************************************************************************
 * Public API
 ****************************************************************************/

/**
 * Initialize all UI pages
 *
 * This function creates the launcher desktop and initializes all pages.
 * Call this once at application startup.
 */
void ui_init(void);

/**
 * Enter the pet page from desktop
 *
 * Convenience wrapper for launcher_enter_page(PAGE_PET)
 */
void ui_enter_pet_page(void);

/**
 * Enter the settings page from desktop
 *
 * Convenience wrapper for launcher_enter_page(PAGE_SETTINGS)
 */
void ui_enter_settings_page(void);

/**
 * Return to desktop from current page
 *
 * Convenience wrapper for launcher_back_to_desktop()
 */
void ui_back_to_desktop(void);

/**
 * Check if currently on desktop
 *
 * @return true if on desktop, false otherwise
 */
bool ui_is_on_desktop(void);

#endif /* __UI_H */

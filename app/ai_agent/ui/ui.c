/****************************************************************************
 * UI Module Implementation
 *
 * Team 181 - Contest 2026
 ****************************************************************************/

#include "ui.h"
#include <stdio.h>

/****************************************************************************
 * Public API Implementation
 ****************************************************************************/

/**
 * Initialize all UI pages
 */
void ui_init(void)
{
    printf("[UI] Initializing UI module...\n");
    launcher_create();
    printf("[UI] UI module initialized\n");
}

/**
 * Enter the pet page
 */
void ui_enter_pet_page(void)
{
    launcher_enter_page(PAGE_PET);
}

/**
 * Enter the settings page
 */
void ui_enter_settings_page(void)
{
    launcher_enter_page(PAGE_SETTINGS);
}

/**
 * Return to desktop
 */
void ui_back_to_desktop(void)
{
    launcher_back_to_desktop();
}

/**
 * Check if on desktop
 */
bool ui_is_on_desktop(void)
{
    return launcher_is_on_desktop();
}

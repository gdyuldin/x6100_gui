/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "settings_types.h"

#ifdef __cplusplus
extern "C" {
#endif

void display_init();
void display_tick();

void display_set_brightness(int16_t value);
void display_set_buttons_backlight(buttons_light_t value);

void display_power_toggle();
bool display_is_on();

void display_invert(bool on);

#ifdef __cplusplus
}
#endif

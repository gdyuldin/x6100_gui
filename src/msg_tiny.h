/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

#pragma once

#include <unistd.h>
#include <stdint.h>

#include "lvgl/lvgl.h"

lv_obj_t * msg_tiny_init(lv_obj_t *align_base);

/* Re-align the tiny message label to its base object (after the base moved/resized). */
void msg_tiny_align(void);

void msg_tiny_set_text_fmt(const char * fmt, ...);
void msg_tiny_set_timeout(uint16_t x);

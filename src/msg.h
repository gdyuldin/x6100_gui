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

lv_obj_t * msg_init(lv_obj_t *align_base);

/* Re-align the message container to its base object (after the base moved/resized). */
void msg_align(void);

/// @brief Show or update message text
/// @param fmt
/// @param
void msg_update_text_fmt(const char * fmt, ...);


/// @brief Schedule message with text
/// @param fmt
/// @param
void msg_schedule_text_fmt(const char * fmt, ...);

/// @brief Schedule message with text for a longer time
/// @param fmt
/// @param
void msg_schedule_long_text_fmt(const char * fmt, ...);

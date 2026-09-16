/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

#pragma once

#include "settings_types.h"

#ifdef __cplusplus

#include <cstdarg>

extern "C" {
#include <stdint.h>
#endif

typedef struct {
    const char *name;
    const char *label;
    const char *welcome;
} voice_item_t;

extern voice_item_t voice_item[VOICES_NUM];

void voice_sure();
void voice_change_mode();

void voice_say_text_fmt(const char * fmt, ...);
void voice_delay_say_text_fmt(const char * fmt, ...);
void voice_say_freq(uint64_t freq);

void voice_say_bool(const char *prompt, bool x);
void voice_say_int(const char *prompt, int32_t x);
void voice_say_float(const char *prompt, float x);
void voice_say_float2(const char *prompt, float x);
void voice_say_text(const char *prompt, const char *x);
void voice_say_lang();

#ifdef __cplusplus
}
#endif

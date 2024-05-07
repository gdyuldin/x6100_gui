/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#include <cstdarg>
#endif

#define VOICES_NUM 4

typedef enum {
    VOICE_OFF = 0,
    VOICE_LCD,
    VOICE_ALWAYS
} voice_mode_t;

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

const char * voice_change(int16_t diff);

#ifdef __cplusplus
}
#endif

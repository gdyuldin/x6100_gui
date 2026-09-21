/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

#pragma once

#include "helpers.h"
#include "dsp.h"

#ifdef __cplusplus

#include <array>

extern "C" {
#endif

#include "audio.h"

#include <stdint.h>
#include <stdbool.h>
#include <liquid/liquid.h>

void cw_init();

void cw_put_audio_samples(size_t n, float *samples);

float cw_get_tone_freq(void);

#ifdef __cplusplus
}
#endif

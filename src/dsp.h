/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

#pragma once

#include "helpers.h"
#include "globals.h"

#ifdef __cplusplus

#include <liquid/liquid.h>
#include <map>

extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#ifdef __cplusplus
}
#endif

#define AUDIO_DECIM 4
#define WATERFALL_NFFT (RADIO_SAMPLES * 2)
#define SPECTRUM_NFFT SCREEN_WIDTH

#ifdef __cplusplus
extern "C" {
#endif

void dsp_init();
void dsp_samples(cfloat *buf_samples, uint16_t size, bool tx, uint32_t base_freq, bool vary_freq, uint8_t fft_dec);
void dsp_reset();

float dsp_get_spectrum_beta();
void dsp_set_waterfall_enabled(bool enabled);
void dsp_set_spectrum_enabled(bool enabled);
void dsp_set_spectrum_beta(float x);

void dsp_put_audio_samples(size_t nsamples, int16_t *samples);
#ifdef __cplusplus
}
#endif

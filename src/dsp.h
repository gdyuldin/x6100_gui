/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

#pragma once

#include <stdint.h>
#include <stdlib.h>
#include <liquid/liquid.h>

#define WATERFALL_NFFT  1024
#define SPECTRUM_NFFT   800

void dsp_init(uint8_t factor);
void dsp_samples(float complex *buf_samples, uint16_t size, bool tx);
void dsp_reset();

void dsp_set_spectrum_factor(uint8_t x);

float dsp_get_spectrum_beta();
void dsp_set_spectrum_beta(float x);

void dsp_put_audio_samples(size_t nsamples, int16_t *samples);

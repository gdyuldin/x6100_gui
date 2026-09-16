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

#define WATERFALL_NFFT (RADIO_SAMPLES * 2)
#define SPECTRUM_NFFT SCREEN_WIDTH

#define AUDIO_SUB_INVALID  (0)

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*audio_raw_cb_t)(size_t n, int16_t *samples);
typedef void (*audio_float_cb_t)(size_t n, float *samples);

void dsp_init();
void dsp_samples(cfloat *buf_samples, uint16_t size, bool tx, uint32_t base_freq, bool vary_freq, uint8_t fft_dec);
void dsp_reset();

float dsp_get_spectrum_beta();
float dsp_get_s_meter_db();
void dsp_set_waterfall_enabled(bool enabled);
void dsp_set_spectrum_enabled(bool enabled);
void dsp_set_spectrum_beta(float x);

void dsp_put_audio_samples(size_t nsamples, int16_t *samples);

/*
 * Audio subscriptions for audio from BASE.
 *
 * IDs are monotonic and never reused: dsp_audio_set_active() and
 * dsp_audio_unsubscribe() on a stale or already-removed id are safe no-ops.
 *
 * Audio callbacks run while an internal mutex is held, so they must never call
 * dsp_audio_subscribe_raw(), dsp_audio_subscribe_resampled(),
 * dsp_audio_set_active() or dsp_audio_unsubscribe() (non-recursive mutex).
 */
uint32_t dsp_audio_subscribe_raw(audio_raw_cb_t cb, bool exclusive);
uint32_t dsp_audio_subscribe_resampled(audio_float_cb_t cb, uint32_t target_rate_hz);
void dsp_audio_set_active(uint32_t id, bool active);
void dsp_audio_unsubscribe(uint32_t id);

/*
 * PSD frame subscribers.
 *
 * The DSP thread produces PSD frames (spectrum / waterfall / scope) and
 * delivers them to the subscribers of the matching kind, so dsp.cpp never
 * includes the UI or CAT modules. Callbacks run on the DSP thread; they must
 * not block, allocate, or call dsp_frame_subscribe()/dsp_frame_unsubscribe()/
 * dsp_frame_set_active() (non-recursive mutex -> deadlock).
 */
typedef enum {
    DSP_FRAME_SPECTRUM = 0,
    DSP_FRAME_WATERFALL,
    DSP_FRAME_SCOPE,
    DSP_FRAME_KIND_COUNT
} dsp_frame_kind_t;

typedef struct {
    const float *psd_db;   /* dB; DB_OFFSET and zoom offset already applied */
    uint16_t     size;     /* number of bins */
    bool         tx;
    uint32_t     base_freq;
    uint32_t     width_hz;
    uint8_t      fft_dec;
    float        min;
    float        max;
} dsp_frame_t;

typedef void (*dsp_frame_cb_t)(const dsp_frame_t *frame, void *user_data);

#define DSP_FRAME_SUB_INVALID (0u)

uint32_t dsp_frame_subscribe(dsp_frame_kind_t kind, dsp_frame_cb_t cb, void *user_data);
void     dsp_frame_set_active(uint32_t id, bool active);
void     dsp_frame_unsubscribe(uint32_t id);

#ifdef __cplusplus
}
#endif

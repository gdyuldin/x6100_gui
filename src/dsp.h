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

/* Single FFT size for the whole spectrum/waterfall/scope pipeline. Subscribers
 * request their own nfft (<= DSP_MAX_NFFT); the DSP decimates the shared
 * transform result down to it. WATERFALL_NFFT/SPECTRUM_NFFT live in
 * waterfall.h/spectrum.h respectively. */
#define DSP_MAX_NFFT (RADIO_SAMPLES * 2)

/* Full span of the PSD pipeline in Hz, i.e. the width at spectrum_factor == 1.
 * Single source of truth shared by dsp.cpp (frame width, S-meter and noise
 * level) and spectrum.c (frequency pan shift). */
#define FULL_BW_HZ (100000)

#define AUDIO_SUB_INVALID  (0)

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*audio_raw_cb_t)(size_t n, int16_t *samples);
typedef void (*audio_float_cb_t)(size_t n, float *samples);

void dsp_init();
void dsp_samples(cfloat *buf_samples, uint16_t size, bool tx, uint32_t base_freq, bool vary_freq, uint8_t fft_dec);
void dsp_reset();

float dsp_get_s_meter_db();

void dsp_put_audio_samples(size_t nsamples, int16_t *samples);

/*
 * Audio subscriptions for audio from BASE.
 *
 * IDs are monotonic and never reused: dsp_audio_set_active() and
 * dsp_audio_unsubscribe() on a stale or already-removed id are safe no-ops.
 *
 * Audio callbacks run while an internal mutex is held, so they must never call
 * dsp_audio_subscribe_raw(), dsp_audio_subscribe_float(),
 * dsp_audio_set_active() or dsp_audio_unsubscribe() (non-recursive mutex).
 */
uint32_t dsp_audio_subscribe_raw(audio_raw_cb_t cb, bool exclusive);
uint32_t dsp_audio_subscribe_float(audio_float_cb_t cb, uint32_t target_rate_hz);
void dsp_audio_set_active(uint32_t id, bool active);
void dsp_audio_unsubscribe(uint32_t id);

/*
 * PSD frame subscribers.
 *
 * The DSP thread produces a single PSD pipeline and delivers frames to every
 * subscriber, so dsp.cpp never knows which consumer it feeds. Callbacks run on
 * the DSP thread; they must not block, allocate, or call
 * dsp_frame_subscribe()/dsp_frame_unsubscribe()/dsp_frame_set_active()/
 * dsp_frame_set_chunks_per_frame() (non-recursive mutex -> deadlock).
 *
 * nfft is the number of bins the subscriber wants (no default): the DSP runs
 * one FFT of DSP_MAX_NFFT per direction and decimates the accumulated power
 * down to nfft. nfft == 0 or nfft > DSP_MAX_NFFT is rejected.
 *
 * chunks_per_frame is the delivery interval in BASE chunks. A frame is
 * delivered once the interval is reached and at least one chunk has been
 * accumulated, so the window may be partial (averaged over the chunks actually
 * accumulated, not over the interval). Cadence is counted in chunks, not
 * milliseconds, because the radio flow is quantized to chunks (~27/s) and a
 * millisecond timer would drift; chunks_per_frame == 0 is rejected.
 *
 * The cadence counter is not reset by a base-frequency / rx-tx / spectrum-factor
 * change and saturates at chunks_per_frame, so after a gap the first usable
 * chunk is delivered immediately. The cadence of a live subscription can be
 * changed with dsp_frame_set_chunks_per_frame() (also in BASE chunks); it takes
 * the same non-recursive mutex as the subscribe/active calls, so it must not be
 * called from a DSP callback either.
 *
 * allow_vary_freq: when false, transforms collected while the base frequency
 * is changing are dropped instead of accumulated, but they still advance the
 * cadence.
 */
typedef struct {
    uint16_t nfft;             /* number of output bins */
    uint16_t chunks_per_frame; /* chunks to accumulate before a frame */
    bool     allow_vary_freq;  /* deliver frames collected while retuning */
} dsp_frame_cfg_t;

/* Chunks accumulated per frame when a consumer has no cadence of its own
 * (~13.5 frames/s at the BASE flow rate). */
#define DSP_FRAME_DEFAULT_CHUNKS (2)

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

uint32_t dsp_frame_subscribe(const dsp_frame_cfg_t *cfg, dsp_frame_cb_t cb, void *user_data);
void     dsp_frame_set_active(uint32_t id, bool active);
void     dsp_frame_set_chunks_per_frame(uint32_t id, uint16_t chunks_per_frame);
void     dsp_frame_unsubscribe(uint32_t id);

#ifdef __cplusplus
}
#endif

/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

#pragma once

#include "helpers.h"

#ifdef __cplusplus

#include "cfg/subjects.h"
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
#define SPECTRUM_NFFT 800

#ifdef __cplusplus

class ChunkedSpgram {
    static std::map<int32_t, cfloat*> w_cache;

    size_t   nfft;
    size_t   chunk_size=0;
    size_t   window_size=0;
    size_t   buffer_size=0;
    windowcf buffer=NULL;
    fftplan  fft;
    cfloat  *buf_time;
    cfloat  *buf_freq;
    cfloat  *w=NULL;
    float   *psd;
    bool     accumulate     = true;
    float    alpha          = 1.0f;
    float    gamma          = 1.0f;
    size_t   num_transforms = 0;
    size_t   num_samples    = 0;

    void setup_buffer();
    void setup_window();
    cfloat* get_cached_window(size_t window_size);

  public:
    ChunkedSpgram(size_t nfft);
    ~ChunkedSpgram();
    void set_alpha(float val);
    void clear();
    void reset();
    bool ready();
    void execute_block(cfloat *block, size_t n_samples);
    void get_psd_mag(float *psd);
    void get_psd(float *psd, bool linear=false);
};

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

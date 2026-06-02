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

#define CW_DETECTOR_DECIM 2


#ifdef __cplusplus

#include <array>

extern "C" {
#endif

#include "audio.h"

#include <stdint.h>
#include <stdbool.h>
#include <liquid/liquid.h>

void cw_init();

void cw_put_audio_samples(unsigned int n, float *samples);

#ifdef __cplusplus
}

/**
 * Simple moving average class for smoothing freq, levels, etc.
 */
template <std::size_t N>
class MovingAverage {
    float sum=0.0f;
    std::array<float, N> data {0.0f};
    size_t w=0;

public:
    void put(float val);
    float get(void);
};

/**
 * Chunked average class
 */
template <std::size_t N>
class ChunkedAverage {
    std::array<float, N> data {0.0f};
    size_t w=0;

public:
    void put(float val);
    bool ready(void);
    float get(void);
};


/**
 * CW detector class for detecting frequency of tone, also noise and signal levels
 */
class CWDetector {
    float fs;
    float f0;
    float mu;
    float r;

    float w_min;
    float w_max;

    // Filter state
    cfloat x1=0;
    cfloat y_notch1=0;
    cfloat y_peak1=0;
    cfloat a;

    // Average instances
    // freq averaging with 50 ms window
    ChunkedAverage<50 * AUDIO_CAPTURE_RATE / AUDIO_DECIM / CW_DETECTOR_DECIM / 1000 > avg_freq;
    // signal averaging with 5ms window
    ChunkedAverage<5 * AUDIO_CAPTURE_RATE / AUDIO_DECIM / CW_DETECTOR_DECIM / 1000> avg_signal;
    // noise averaging with 50ms window
    MovingAverage<50 * AUDIO_CAPTURE_RATE / AUDIO_DECIM / CW_DETECTOR_DECIM / 1000> sma_noise;

public:
    CWDetector(float fs, float mu, float r):
        fs(fs), mu(mu), r(r)
        {};
    void set_f0(int16_t tone);
    void set_f_min(float freq);
    void set_f_max(float freq);
    void put(cfloat sample);
    bool get_freq(float *freq);
    bool get_signal_noise(float *sig_db, float *noise_db);
};

#endif

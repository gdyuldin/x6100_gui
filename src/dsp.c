/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

#include <stdlib.h>
#include <pthread.h>
#include <math.h>

#include "dsp.h"
#include "spectrum.h"
#include "waterfall.h"
#include "util.h"
#include "radio.h"
#include "params.h"
#include "meter.h"
#include "audio.h"
#include "cw.h"
#include "rtty.h"
#include "dialog_ft8.h"
#include "dialog_msg_voice.h"
#include "recorder.h"

static int32_t          nfft = 400;
static iirfilt_cccf     dc_block;

static pthread_mutex_t  spectrum_mux;

static uint8_t          spectrum_factor = 1;
static firdecim_crcf    spectrum_decim;

static spgramcf         spectrum_sg;
static float            *spectrum_psd;
static float            *spectrum_psd_filtered;
static float            spectrum_beta = 0.7f;
static uint8_t          spectrum_fps_ms = (1000 / 15);
static uint64_t         spectrum_time;
static float complex    *spectrum_dec_buf;

static spgramcf         waterfall_sg;
static float            *waterfall_psd;
static uint8_t          waterfall_fps_ms = (1000 / 25);
static uint64_t         waterfall_time;

static float complex    *buf;
static float complex    *buf_filtered;

static uint8_t          delay;

static firhilbf         audio_hilb;
static float complex    *audio;

static bool             ready = false;
static bool             auto_clear = true;

static void dsp_calc_auto(float *data_buf, uint16_t size);

/* * */

void dsp_init() {
    pthread_mutex_init(&spectrum_mux, NULL);

    dc_block = iirfilt_cccf_create_dc_blocker(0.005f);

    spectrum_sg = spgramcf_create(nfft, LIQUID_WINDOW_HANN, nfft, nfft / 4);
    spectrum_psd = (float *) malloc(nfft * sizeof(float));
    spectrum_psd_filtered = (float *) malloc(nfft * sizeof(float));

    dsp_set_spectrum_factor(params_mode.spectrum_factor);

    for (uint16_t i = 0; i < nfft; i++)
        spectrum_psd_filtered[i] = S_MIN;

    waterfall_sg = spgramcf_create(nfft, LIQUID_WINDOW_HANN, nfft, nfft / 4);
    waterfall_psd = (float *) malloc(nfft * sizeof(float));

    buf = (float complex*) malloc(RADIO_SAMPLES * sizeof(float complex));
    buf_filtered = (float complex*) malloc(RADIO_SAMPLES * sizeof(float complex));

    spectrum_time = get_time();
    waterfall_time = get_time();
    
    delay = 4;
    
    audio = (float complex *) malloc(AUDIO_CAPTURE_RATE * sizeof(float complex));
    audio_hilb = firhilbf_create(7, 60.0f);
    
    ready = true;
}

void dsp_reset() {
    delay = 4;

    iirfilt_cccf_reset(dc_block);
    spgramcf_reset(spectrum_sg);
    spgramcf_reset(waterfall_sg);
}

void dsp_samples(float complex *buf_samples, uint16_t size) {
    int res;

    if (delay)
        delay--;

    uint64_t now = get_time();

    /* Spectrum */

    iirfilt_cccf_execute_block(dc_block, buf_samples, size, buf_filtered);

    pthread_mutex_lock(&spectrum_mux);

    if (spectrum_factor > 1) {
        firdecim_crcf_execute_block(spectrum_decim, buf_filtered, size / spectrum_factor, spectrum_dec_buf);
        spgramcf_write(spectrum_sg, spectrum_dec_buf, size / spectrum_factor);
        
        memset(spectrum_dec_buf, 0, sizeof(float complex) * size / spectrum_factor);

        for (uint8_t i = 0; i < spectrum_factor - 1; i++) {
            spgramcf_write(spectrum_sg, spectrum_dec_buf, size / spectrum_factor);
        }
    } else {
        spgramcf_write(spectrum_sg, buf_filtered, size);
    }

    spgramcf_get_psd(spectrum_sg, spectrum_psd);

    liquid_vectorf_addscalar(spectrum_psd, nfft, -30.0f, spectrum_psd);

    pthread_mutex_unlock(&spectrum_mux);
    
    if (now - spectrum_time > spectrum_fps_ms) {
        if (!delay) {
            for (uint16_t i = 0; i < nfft; i++) {
                float psd = spectrum_psd[i];
                
                lpf(&spectrum_psd_filtered[i], psd, spectrum_beta);
            }
        
            spectrum_data(spectrum_psd_filtered, nfft);
        }

        spgramcf_reset(spectrum_sg);
        spectrum_time = now;
    }

    /* Waterfall */

    spgramcf_write(waterfall_sg, buf_filtered, size);
    spgramcf_get_psd(waterfall_sg, waterfall_psd);

    liquid_vectorf_addscalar(waterfall_psd, nfft, -30.0f, waterfall_psd);
    
    if (now - waterfall_time > waterfall_fps_ms) {
        if (!delay) {
            waterfall_data(waterfall_psd, nfft);
        }

        spgramcf_reset(waterfall_sg);
        waterfall_time = now;
    }

    /* S-Meter */

    if (dialog_msg_voice_get_state() != MSG_VOICE_RECORD) {
        int32_t filter_from, filter_to;
        int32_t from, to;
    
        radio_filter_get(&filter_from, &filter_to);
    
        from = nfft / 2;
        from -= filter_to * nfft / 100000;
    
        to = nfft / 2;
        to -= filter_from * nfft / 100000;
    
        int16_t peak_db = -121;
    
        for (int32_t i = from; i <= to; i++)
            if (waterfall_psd[i] > peak_db)
                peak_db = waterfall_psd[i];

        meter_update(peak_db, 0.8f);
    }

    /* Auto min, max */

    if (!delay) {
        dsp_calc_auto(waterfall_psd, nfft);
    }
}

void dsp_set_spectrum_factor(uint8_t x) {
    if (x == spectrum_factor)
        return;

    pthread_mutex_lock(&spectrum_mux);

    spectrum_factor = x;

    if (spectrum_decim) {
        firdecim_crcf_destroy(spectrum_decim);
        spectrum_decim = NULL;
    }

    if (spectrum_dec_buf) {
        free(spectrum_dec_buf);
        spectrum_dec_buf = NULL;
    }

    if (spectrum_factor > 1) {
        spectrum_decim = firdecim_crcf_create_kaiser(spectrum_factor, 16, 40.0f);
        spectrum_dec_buf = (float complex *) malloc(RADIO_SAMPLES * sizeof(float complex) / spectrum_factor);
    }

    spgramcf_reset(spectrum_sg);

    for (uint16_t i = 0; i < nfft; i++)
        spectrum_psd_filtered[i] = S_MIN;

    pthread_mutex_unlock(&spectrum_mux);
    spectrum_clear();
}

float dsp_get_spectrum_beta() {
    return spectrum_beta;
}

void dsp_set_spectrum_beta(float x) {
    spectrum_beta = x;
}

void dsp_put_audio_samples(size_t nsamples, int16_t *samples) {
    if (!ready) {
        return;
    }

    if (dialog_msg_voice_get_state() == MSG_VOICE_RECORD) {
        dialog_msg_voice_put_audio_samples(nsamples, samples);
        return;
    }

    if (recorder_is_on()) {
        recorder_put_audio_samples(nsamples, samples);
    }

    for (uint16_t i = 0; i < nsamples; i++)
        firhilbf_r2c_execute(audio_hilb, samples[i] / 32768.0f, &audio[i]);

    x6100_mode_t    mode = radio_current_mode();
    
    if (rtty_get_state() == RTTY_RX) {
        rtty_put_audio_samples(nsamples, audio);
    } else if (mode == x6100_mode_cw || mode == x6100_mode_cwr) {
        cw_put_audio_samples(nsamples, audio);
    } else {
        dialog_audio_samples(nsamples, audio);
    }
}

void dsp_auto_clear() {
    auto_clear = true;
}

static int compare_fft(const void *p1, const void *p2) {
    float *i1 = (float *) p1;
    float *i2 = (float *) p2;

    return (*i1 < *i2) ? -1 : 1;
}

static void dsp_calc_auto(float *data_buf, uint16_t size) {
    float       min = 0;
    float       max = 0;
    uint16_t    window = 30;

    qsort(data_buf, size, sizeof(float), compare_fft);
    
    for (uint16_t i = 0; i < window; i++) {
        min += data_buf[i];
        max += data_buf[size - i - 1];
    }

    min /= window;
    max /= window;

    if (max > S9_40) {
        max = S9_40;
    } else if (max < S8) {
        max = S8;
    }

    if (min > S7) {
        min = S7;
    } else if (min < S_MIN) {
        min = S_MIN;
    }

    if (auto_clear) {
        spectrum_auto_min = min;
        spectrum_auto_max = max;

        waterfall_auto_min = min;
        waterfall_auto_max = max;

        auto_clear = false;
    } else {
        lpf(&spectrum_auto_min, min, 0.75f);
        lpf(&spectrum_auto_max, max, 0.55f);

        lpf(&waterfall_auto_min, min, 0.95f);
        lpf(&waterfall_auto_max, max, 0.85f);
    }
}

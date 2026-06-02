/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */
#include "cw.h"


#include <math.h>
#include <complex.h>

// DEBUG
#include <iostream>
#include <fstream>

#include "util.h"
#include "cfg/cfg.h"
#include "cfg/subjects.h"

extern "C" {
    #include "lvgl/lvgl.h"
    #include "params/params.h"
    #include "cw_decoder.h"
    #include "panel.h"
    #include "meter.h"
    #include "cw_tune_ui.h"
    #include "pubsub_ids.h"
}

typedef struct {
    uint16_t    n;
    float       val;
} fft_item_t;

#define M_I std::complex<float>(0.0f, 1.0f)

#define NUM_STAGES 6
#define DECIM_FACTOR (1LL << NUM_STAGES)
#define FFT 128
#define MAX_CW_BW 500

static bool ready = false;

static cbuffercf input_cbuf;

static firdecim_crcf decim;
static CWDetector *cw_detector;

static float threshold_pulse;
static float threshold_silence;
static bool  peak_on = false;
static size_t samples_counter = 0;

static int32_t key_tone = 0;
static float   cw_decoder_peak_beta;
static float   cw_decoder_noise_beta;
static float   cw_decoder_snr;
static float   cw_decoder_snr_gist;
static bool    cw_decoder;
static bool    cw_tune;

static void on_key_tone_change(Subject *subj, void *user_data);
static void on_val_float_change(Subject *subj, void *user_data);
static void on_val_bool_change(Subject *subj, void *user_data);
static void on_low_filter_change(Subject *subj, void *user_data);
static void on_high_filter_change(Subject *subj, void *user_data);


// DEBUG
// Open file in binary mode
std::ofstream srcFile("/mnt/src.bin", std::ios::out | std::ios::binary);
std::ofstream decimFile("/mnt/decim.bin", std::ios::out | std::ios::binary);
std::ofstream peakFile("/mnt/peak.bin", std::ios::out | std::ios::binary);
std::ofstream notchFile("/mnt/notch.bin", std::ios::out | std::ios::binary);


void cw_init() {
    cfg.key_tone.val->subscribe(on_key_tone_change)->notify();
    // TODO: remove peak and noise betas
    cfg.cw_decoder_peak_beta.val->subscribe(on_val_float_change, (void*)&cw_decoder_peak_beta)->notify();
    cfg.cw_decoder_noise_beta.val->subscribe(on_val_float_change, (void*)&cw_decoder_noise_beta)->notify();
    cfg.cw_decoder_snr.val->subscribe(on_val_float_change, (void*)&cw_decoder_snr)->notify();
    cfg.cw_decoder_snr_gist.val->subscribe(on_val_float_change, (void*)&cw_decoder_snr_gist)->notify();
    cfg.cw_decoder.val->subscribe(on_val_bool_change, (void*)&cw_decoder)->notify();
    cfg.cw_tune.val->subscribe(on_val_bool_change, (void*)&cw_tune)->notify();

    input_cbuf = cbuffercf_create(10000);

    decim = firdecim_crcf_create_kaiser(CW_DETECTOR_DECIM, 31, 60.0f);

    cw_detector = new CWDetector((float)AUDIO_CAPTURE_RATE / AUDIO_DECIM / CW_DETECTOR_DECIM, 0.01f, 0.95f);
    cw_detector->set_f0(subject_get_int(cfg.key_tone.val));
    cfg_cur.filter.low->subscribe(on_low_filter_change)->notify();
    cfg_cur.filter.high->subscribe(on_high_filter_change)->notify();

    ready = true;
}

static int compare_fft_items(const void *p1, const void *p2) {
    fft_item_t *i1 = (fft_item_t *) p1;
    fft_item_t *i2 = (fft_item_t *) p2;

    return (i1->val > i2->val) ? -1 : 1;
}

static void update_peak_freq(float freq) {
    // if (peak_on) {
    if (true) {
        cw_tune_set_freq(freq);
    }
}

void cw_put_audio_samples(unsigned int n, float *samples) {
    if (!ready) {
        return;
    }
    if ((!cw_decoder) && (!cw_tune)) {
        return;
    }
    // cfloat sample;
    // size_t max_pos;

    // cfloat *decim_input;

    // // fill input buffer
    // cbuffercf_write(input_cbuf, samples, n);

    // // DEBUG
    // srcFile.write(reinterpret_cast<const char*>(samples), sizeof(cfloat) * n);

    // // Downsample
    // while (cbuffercf_size(input_cbuf) >= CW_DETECTOR_DECIM) {
    //     unsigned int _n;
    //     cfloat val;
    //     float avg_freq;
    //     float sig_db;
    //     float noise_db;
    //     cbuffercf_read(input_cbuf, CW_DETECTOR_DECIM, &decim_input, &_n);
    //     cbuffercf_release(input_cbuf, CW_DETECTOR_DECIM);
    //     firdecim_crcf_execute(decim, decim_input, &val);

    //     // DEBUG
    //     decimFile.write(reinterpret_cast<const char*>(&val), sizeof(val));

    //     cw_detector->put(val);

    //     if (cw_detector->get_freq(&avg_freq)) {
    //         update_peak_freq(key_tone - avg_freq);
    //     }

    //     if (cw_detector->get_signal_noise(&sig_db, &noise_db)) {
    //         // printf("%f, %f\n", sig_db, noise_db);

    //         // Update thresholds
    //         // lpf(&threshold_pulse, noise_db + cw_decoder_snr, cw_decoder_noise_beta, 0.0f);
    //         threshold_pulse = noise_db + cw_decoder_snr;
    //         threshold_silence = threshold_pulse - cw_decoder_snr_gist;

    //         // printf("%d, %f\n", peak_on, sig_db, threshold_pulse, threshold_silence);

    //         // Detect tones/silence
    //         if (peak_on) {
    //             if (sig_db < threshold_silence) {
    //                 peak_on = false;
    //             }
    //         } else {
    //             if (sig_db > threshold_pulse) {
    //                 peak_on = true;
    //             }
    //         }
    //         // printf("%d, %f, %f, %f\n", peak_on, sig_db, samples_counter * 1000.0f / AUDIO_CAPTURE_RATE * CW_DETECTOR_DECIM);
    //         cw_decoder_signal(peak_on, samples_counter * 1000.0f / AUDIO_CAPTURE_RATE * CW_DETECTOR_DECIM);
    //         samples_counter = 0;
    //     }
    //     samples_counter++;
    // }
}

static void on_key_tone_change(Subject *subj, void *user_data) {
    key_tone = static_cast<SubjectT<int32_t>*>(subj)->get();
}

static void on_val_float_change(Subject *subj, void *user_data) {
    *(float*)user_data = static_cast<SubjectT<float>*>(subj)->get();
}

static void on_val_bool_change(Subject *subj, void *user_data) {
    *(bool*)user_data = static_cast<SubjectT<int32_t>*>(subj)->get();
}

static void on_low_filter_change(Subject *subj, void *user_data) {
    cw_detector->set_f_min(subject_get_int(subj));
}

static void on_high_filter_change(Subject *subj, void *user_data) {
    cw_detector->set_f_max(subject_get_int(subj));
}

// Prev CW detector:
// * dds for key tone with 6 stages (689 Hz sampling)
// 128 FFT (5.38 Hz for bin) (period - 0.186 sec)
// get max freq pos, update
// get pulse and silence thresholds with lpf and SNR
//
// rms - push and average (with not correct log)
//

/**
 * MovingAverage
 */

template <std::size_t N> void MovingAverage<N>::put(float val) {
    auto prev = data[w];
    sum = sum - prev + val;
    data[w] = val;
    w = (w + 1) % data.size();
}

template <std::size_t N> float MovingAverage<N>::get(void) {
    return sum / data.size();
}

/**
 * ChunkedAverage
 */

template <std::size_t N> void ChunkedAverage<N>::put(float val) {
    data[w] = val;
    w = (w + 1) % data.size();
}

template <std::size_t N> bool ChunkedAverage<N>::ready(void) {
    return w == 0;
}

template <std::size_t N> float ChunkedAverage<N>::get(void) {
    float sum = 0.0f;
    for (auto &i : data) {
        sum += i;
    }
    return sum / data.size();
}

/**
 * CWDetector
 */

void CWDetector::set_f0(int16_t tone) {
    f0 = tone;
    // perhaps, we should not change it
    a = std::exp(M_I * 2.0f * M_PIf32 * f0 / fs);
}

void CWDetector::set_f_min(float freq) {
    w_min = freq * 2.0f * M_PIf32 / fs;
}

void CWDetector::set_f_max(float freq) {
    w_max = freq * 2.0f * M_PIf32 / fs;
}

void CWDetector::put(cfloat sample) {
    // Get notch output
    cfloat y_notch = sample - a * x1 + r * a * y_notch1;
    cfloat y_peak = (1 - r) * a * x1 + r * a * y_peak1;

    // DEBUG
    peakFile.write(reinterpret_cast<const char*>(&y_peak), sizeof(y_peak));
    notchFile.write(reinterpret_cast<const char*>(&y_notch), sizeof(y_notch));

    // Update theta (Gradient descent to minimize y[n]^2)
    auto gradient = y_notch * std::conj(x1 - r * y_notch1);
    cfloat new_a;
    new_a = a + mu * gradient;
    float w = std::arg(new_a);

    // Check for boundaries
    if ((w > w_min) && (w < w_max)) {
        a = new_a;
    }
    // a = new_a;
    // Update states
    x1 = sample;
    y_notch1 = y_notch;
    y_peak1 = y_peak;

    // Store freq
    float freq = w * fs / (2 * M_PIf32);
    avg_freq.put(freq);

    // Store signal and noise
    sma_noise.put(std::norm(y_notch));
    avg_signal.put(std::norm(y_peak));
}


bool CWDetector::get_freq(float *freq) {
    if (avg_freq.ready()) {
        *freq = avg_freq.get();
        return true;
    } else {
        return false;
    }
}

bool CWDetector::get_signal_noise(float *sig_db, float *noise_db) {
    if (avg_signal.ready()) {
        float sig = avg_signal.get();
        float noise = sma_noise.get();
        *sig_db = 10.0f * log10f(LV_MAX(sig, 1e-12));
        *noise_db = 10.0f * log10f(LV_MAX(noise, 1e-12));
        return true;
    }
    return false;
}

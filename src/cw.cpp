/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */
#include "cw.h"

#include "cfg/settings_manager.h"

#include <math.h>
#include <complex.h>

extern "C" {
    #include "lvgl/lvgl.h"
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

static bool ready = false;

static CWDetector *cw_detector;

static float min_db = 50.0f;
static float max_db = 50.0f;
static float ema_sig_db = S1;
static float ema_noise_db = S9;
static float threshold_pulse;
static float threshold_silence;
static bool  peak_on = false;
static size_t samples_counter = 0;
static float tone_freq;

static int32_t key_tone = 0;
static float   cw_decoder_snr;
static float   cw_decoder_snr_gist;
static bool    cw_decoder;
static bool    cw_tune;
static int32_t filter_low;
static int32_t filter_high;

static void on_key_tone_change(Subject *subj, void *user_data);
static void on_val_float_change(Subject *subj, void *user_data);
static void on_val_bool_change(Subject *subj, void *user_data);
static void on_low_filter_change(Subject *subj, void *user_data);
static void on_high_filter_change(Subject *subj, void *user_data);


void cw_init() {
    cfg_sm.p_key_tone.subscribe_and_notify(on_key_tone_change);
    tone_freq = key_tone;
    cfg_sm.p_cw_decoder_snr.subscribe_and_notify(on_val_float_change, (void*)&cw_decoder_snr);
    cfg_sm.p_cw_decoder_snr_gist.subscribe_and_notify(on_val_float_change, (void*)&cw_decoder_snr_gist);
    cfg_sm.p_cw_decoder.subscribe_and_notify(on_val_bool_change, (void*)&cw_decoder);
    cfg_sm.p_cw_tune.subscribe_and_notify(on_val_bool_change, (void*)&cw_tune);

    cw_detector = new CWDetector((float)SAMPLE_RATE, 0.01f, 0.8f);
    cw_detector->set_f0(cfg_sm.p_key_tone.get());

    cfg_sm.cp_cur_filter_low.subscribe_and_notify(on_low_filter_change);
    cfg_sm.cp_cur_filter_high.subscribe_and_notify(on_high_filter_change);

    ready = true;
}

static void update_peak_freq(float freq) {
    if (peak_on) {
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

    for (size_t i = 0; i < n; i++)
    {
        float avg_freq, sig_db, noise_db, sig_to_noise;
        cw_detector->put(samples[i]);
        if (cw_detector->get_signal_noise(&sig_db, &noise_db)) {

            ema_sig_db += (sig_db - ema_sig_db) * 0.5f;

            // track average noise level
            ema_noise_db += (noise_db - ema_noise_db) * 0.05f;

            float aligned_sig = ema_sig_db - ema_noise_db;

            // track lower bound of the aligned signal
            float k;
            if (min_db > aligned_sig) {
                k = 0.05f;
            } else {
                k = 0.001f;
            }
            min_db += (aligned_sig - min_db)*k;

            // track upper bound of the aligned signal
            k;
            if (max_db < aligned_sig) {
                k = 0.05f;
            } else {
                k = 0.001f;
            }
            max_db += (aligned_sig - max_db)*k;

            // threshold_pulse = min_db + cw_decoder_snr;
            threshold_pulse = LV_MAX(min_db + cw_decoder_snr, (min_db + max_db) * 0.5f);
            threshold_silence = threshold_pulse - cw_decoder_snr_gist;

            // Detect tones/silence
            if (peak_on) {
                if (aligned_sig < threshold_silence) {
                    peak_on = false;
                }
            } else {
                if (aligned_sig > threshold_pulse) {
                    peak_on = true;
                }
            }
            cw_decoder_signal(peak_on, samples_counter * 1000.0f / SAMPLE_RATE);
            samples_counter = 0;
        }
        samples_counter++;

        if (cw_detector->get_freq(&avg_freq)) {
            tone_freq = avg_freq;
            avg_freq = LV_CLAMP(filter_low, avg_freq, filter_high);
            update_peak_freq(key_tone - avg_freq);
        }
    }
}

float cw_get_tone_freq(void) {
    return tone_freq;
}

static void on_key_tone_change(Subject *subj, void *user_data) {
    key_tone = cfg_sm.p_key_tone.get();
}

static void on_val_float_change(Subject *subj, void *user_data) {
    *(float*)user_data = static_cast<SubjectT<float>*>(subj)->get();
}

static void on_val_bool_change(Subject *subj, void *user_data) {
    *(bool*)user_data = static_cast<SubjectT<int32_t>*>(subj)->get();
}


static void on_low_filter_change(Subject *subj, void *user_data) {
    filter_low = cfg_sm.cp_cur_filter_low.get();
}

static void on_high_filter_change(Subject *subj, void *user_data) {
    filter_high = cfg_sm.cp_cur_filter_high.get();
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

CWDetector::CWDetector(float fs, float mu, float r): fs(fs), mu(mu), r(r) {
    r2 = r * r;
}

void CWDetector::set_f0(int16_t tone) {
    f0 = tone;
    // perhaps, we should not change it
    a = -2.0f * cosf(2.0 * M_PIf * f0 / fs);
}

void CWDetector::set_r(float r) {
    this->r = r;
    this->r2 = r * r;
}

void CWDetector::put(float sample) {
    // Get notch output
    float y_notch = sample + a * x1 + x2 - r * a * y_notch1 - r2 * y_notch2;
    float y_peak = (r - 1) * a * x1 + (r2 - 1) * x2 - r * a * y_peak1 - r2 * y_peak2;

    auto psi = x1 - r * y_notch1;

    // Update k using LMS rule
    a = a - mu * y_notch * psi;
    a = LV_CLAMP(-1.999f, a, 1.999f); // Stability boundary
    // Exact analytical frequency extraction
    float w = acosf(-a / 2.0f);

    // Update states
    x2 = x1;
    x1 = sample;
    y_notch2 = y_notch1;
    y_notch1 = y_notch;
    y_peak2 = y_peak1;
    y_peak1 = y_peak;

    // Store freq
    float freq = w * fs / (2 * M_PIf);
    avg_freq.put(freq);

    // Store signal
    avg_signal.put(y_peak * y_peak);
    avg_noise.put(y_notch * y_notch);
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
        *sig_db = 10.0f * log10f(LV_MAX(sig, 1e-12));
        float noise = avg_noise.get();
        *noise_db = 10.0f * log10f(LV_MAX(noise, 1e-12));
        return true;
    }
    return false;
}

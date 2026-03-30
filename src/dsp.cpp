/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

#include "dsp.h"

#include "cw.h"
#include "util.h"
#include "buttons.h"
#include "cfg/subjects.h"

#include <algorithm>
#include <numeric>

extern "C" {
    #include "audio.h"
    #include "cfg/cfg.h"
    #include "dialog_msg_voice.h"
    #include "meter.h"
    #include "radio.h"
    #include "recorder.h"
    #include "rtty.h"
    #include "spectrum.h"
    #include "waterfall.h"

    #include <math.h>
    #include <pthread.h>
    #include <stdlib.h>
}

#define DB_OFFSET -30.0f
#define OEM_PSD_DELAY 4
#define R8_PSD_DELAY 0

#define SG_ALPHA_WF 0.8f
#define SG_ALPHA_SP 0.4f
#define SG_ALPHA_FACTOR 0.1f

static iirfilt_cccf dc_block;

static pthread_mutex_t spectrum_mux = PTHREAD_MUTEX_INITIALIZER;

static x6100_base_ver_t base_ver;
static bool fw_decim = false;  // BASE firmware performs a decimation
static bool fw_dc_blocker = false;  // BASE firmware performs a DC blocker on IQ

static uint8_t       spectrum_factor = 1;
static firdecim_crcf spectrum_decim_rx;
static firdecim_crcf spectrum_decim_tx;
static bool          waterfall_fft_decim = false;
static float         zoom_level_offset = 0.0f;

static ChunkedSpgram *spectrum_sg_rx;
static ChunkedSpgram *spectrum_sg_tx;
static float          spectrum_psd[SPECTRUM_NFFT];
static float          spectrum_psd_filtered[SPECTRUM_NFFT];
static float          spectrum_beta   = 0.7f;
static uint8_t        spectrum_fps_ms = (1000 / 15);
static uint64_t       spectrum_time;
static cfloat         spectrum_dec_buf[RADIO_SAMPLES];
static uint32_t       spectrum_prev_freq;

static ChunkedSpgram *waterfall_sg_rx;
static ChunkedSpgram *waterfall_sg_tx;
static float          waterfall_psd_lin[WATERFALL_NFFT];
static float          waterfall_psd[WATERFALL_NFFT];
static uint8_t        waterfall_fps_ms = (1000 / 15);
static uint64_t       waterfall_time;

static cfloat buf_filtered[RADIO_SAMPLES * 2];

static uint32_t cur_freq;
static uint8_t  psd_delay;
static uint8_t  min_max_delay;

static firhilbf audio_hilb;
static cfloat  *audio;

static bool ready = false;

static int32_t filter_from = 0;
static int32_t filter_to   = 3000;
static x6100_mode_t cur_mode;
static float noise_level = S_MIN;

static void dsp_update_min_max(float *data_buf, uint16_t size);
static void update_zoom(int32_t new_zoom);
static void on_zoom_change(Subject *subj, void *user_data);
static void update_filter_from(Subject *subj, void *user_data);
static void update_filter_to(Subject *subj, void *user_data);
static void update_cur_mode(Subject *subj, void *user_data);
static void on_cur_freq_change(Subject *subj, void *user_data);


std::map<int32_t, cfloat*> ChunkedSpgram::w_cache;

ChunkedSpgram::ChunkedSpgram(size_t nfft) {
    this->nfft = nfft;
    this->buf_time = (cfloat *) calloc(sizeof(cfloat), nfft);
    this->buf_freq = (cfloat *) calloc(sizeof(cfloat), nfft);
    this->psd = (float *) calloc(sizeof(float), nfft);
    this->fft = fft_create_plan(nfft, buf_time, buf_freq, LIQUID_FFT_FORWARD, 0);
}

ChunkedSpgram::~ChunkedSpgram() {
    free(this->buf_time);
    free(this->buf_freq);
    free(this->psd);

    if (buffer) {
        windowcf_destroy(buffer);
    }
    fft_destroy_plan(fft);
}

void ChunkedSpgram::setup_buffer() {
    if (buffer) {
        windowcf_destroy(buffer);
    }
    buffer = windowcf_create(this->buffer_size);

}

void ChunkedSpgram::setup_window() {
    w = get_cached_window(window_size);
}

cfloat *ChunkedSpgram::get_cached_window(size_t window_size) {
    if (auto search = w_cache.find(window_size); search != w_cache.end()) {
        return search->second;
    } else {
        cfloat *window = (cfloat *) calloc(sizeof(cfloat), window_size);
        size_t i;
        for (i = 0; i < window_size; i++) {
            window[i] = liquid_kaiser(i, window_size, 10.0f);
            // window[i] = liquid_hann(i, window_size);
        }
        // scale by window magnitude
        float g = 0.0f;
        for (i=0; i<window_size; i++)
            g += std::norm(window[i]);
        g = 1.0f / sqrtf(g * nfft / window_size);
        // printf("nfft: %d, window_size: %d, buffer_size: %d, scale: %f\n", nfft, window_size, buffer_size, g);

        // scale window and copy
        for (i=0; i<window_size; i++)
            window[i] *= g;
        w_cache[window_size] = window;
        return window;
    }
}

void ChunkedSpgram::set_alpha(float val) {
    // validate input
    if (val != -1 && (val < 0.0f || val > 1.0f)) {
        printf("set_alpha(), alpha must be in {-1,[0,1]}");
        return;
    }

    // set accumulation flag appropriately
    accumulate = (val == -1.0f) ? true : false;

    if (accumulate) {
        this->alpha = 1.0f;
        this->gamma = 1.0f;
    } else {
        this->alpha = val;
        this->gamma = 1.0f - val;
    }
}

void ChunkedSpgram::clear() {
    num_transforms = 0;
    for (size_t i = 0; i < nfft; i++) {
        psd[i] = 0.0f;
        buf_time[i] = 0.0f;
    }
}

void ChunkedSpgram::reset() {
    clear();
    if (buffer) {
        windowcf_reset(buffer);
    }
}

bool ChunkedSpgram::ready() {
    return num_transforms > 0;
}

void ChunkedSpgram::execute_block(cfloat *chunk, size_t n_samples) {
    if (n_samples != chunk_size) {
        chunk_size = n_samples;
        window_size = LV_MIN(nfft, chunk_size);
        buffer_size = nfft - nfft % window_size;
        setup_window();
        setup_buffer();
        clear();
    }

    cfloat val;
    for (size_t i=0; i<window_size; i++) {
        val = chunk[i] * w[i];
        windowcf_push(buffer, val);
    }

    cfloat *rc;
    if (windowcf_read(buffer, &rc) != LIQUID_OK) {
        return;
    }
    memcpy(buf_time, rc, sizeof(cfloat) * buffer_size);
    fft_execute(fft);

    // accumulate output
    // TODO: vectorize this operation
    for (size_t i=0; i<nfft; i++) {
        float v = std::norm(buf_freq[i]);
        if (num_transforms == 0)
            psd[i] = v;
        else
            psd[i] = gamma*psd[i] + alpha*v;
    }
    num_transforms++;
}

void ChunkedSpgram::get_psd_mag(float *psd) {
    // compute magnitude (linear) and run FFT shift
    unsigned int i;
    unsigned int nfft_2 = nfft / 2;
    float scale = accumulate ? 1.0f / LV_MAX((size_t)1, num_transforms) : 1.0f;
    for (i=0; i<nfft; i++) {
        unsigned int k = (i + nfft_2) % nfft;
        psd[i] = LV_MAX((float)LIQUID_SPGRAM_PSD_MIN, this->psd[k]) * scale;
    }
    if (accumulate) {
        clear();
    }
}

void ChunkedSpgram::get_psd(float *psd, bool linear) {
    // compute magnitude, linear
    get_psd_mag(psd);
    if (!linear) {
        // convert to dB
        unsigned int i;
        for (i=0; i<nfft; i++) {
            // 10.0 because psd is squared magnitude (power)
            psd[i] = 10.0f * log10f(psd[i]);
        }
    }
}

/* * */

void dsp_init() {
    base_ver = x6100_control_get_base_ver();

    if ((util_compare_version(base_ver, (x6100_base_ver_t){1, 1, 9, 0}) >= 0) || (base_ver.rev >= 8)) {
        fw_decim = true;
        waterfall_fft_decim = true;
    } else {
        fw_decim = false;
    }
    if (base_ver.rev >= 8) {
        fw_dc_blocker = true;
    }

    waterfall_sg_rx = new ChunkedSpgram(WATERFALL_NFFT);
    waterfall_sg_rx->set_alpha(-1.0f);
    waterfall_sg_tx = new ChunkedSpgram(WATERFALL_NFFT);
    waterfall_sg_tx->set_alpha(-1.0f);

    spectrum_sg_rx = new ChunkedSpgram(SPECTRUM_NFFT);
    spectrum_sg_rx->set_alpha(-1.0f);
    spectrum_sg_tx = new ChunkedSpgram(SPECTRUM_NFFT);
    spectrum_sg_tx->set_alpha(-1.0f);

    dc_block = iirfilt_cccf_create_dc_blocker(0.005f);

    spectrum_time  = get_time();
    waterfall_time = get_time();

    if (base_ver.rev < 8) {
        psd_delay = OEM_PSD_DELAY;
    } else {
        psd_delay = R8_PSD_DELAY;
    }

    audio      = (cfloat *)malloc(AUDIO_CAPTURE_RATE * sizeof(cfloat));
    audio_hilb = firhilbf_create(7, 60.0f);

    subject_add_observer_and_call(cfg_cur.zoom, on_zoom_change, NULL);
    subject_add_observer(cfg_cur.band->if_shift.val, update_filter_to, NULL);
    subject_add_observer(cfg_cur.band->if_shift.val, update_filter_from, NULL);
    subject_add_observer_and_call(cfg_cur.filter.real.from, update_filter_from, NULL);
    subject_add_observer_and_call(cfg_cur.filter.real.to, update_filter_to, NULL);
    cfg_cur.mode->subscribe(update_cur_mode)->notify();

    cfg_cur.fg_freq->subscribe(on_cur_freq_change);
    ready = true;
}

void dsp_reset() {
    if (base_ver.rev < 8) {
        psd_delay = OEM_PSD_DELAY;
    } else {
        psd_delay = R8_PSD_DELAY;
    }

    iirfilt_cccf_reset(dc_block);
    spectrum_sg_rx->reset();
    spectrum_sg_tx->reset();
    waterfall_sg_rx->reset();
    waterfall_sg_tx->reset();
}

static void process_samples(cfloat *buf_samples, uint16_t size, firdecim_crcf sp_decim, ChunkedSpgram *sp_sg,
                            ChunkedSpgram *wf_sg, bool tx) {
    // Swap I and Q
    for (size_t i = 0; i < size; i++) {
        buf_filtered[i] = {buf_samples[i].imag(), buf_samples[i].real()};
    }

    if (!fw_dc_blocker) {
        iirfilt_cccf_execute_block(dc_block, buf_filtered, size, buf_filtered);
    }

    cfloat *samples_for_wf = buf_filtered;
    size_t wf_n_samples = size;
    size_t sp_n_samples = size;

    if ((spectrum_factor > 1) && !fw_decim) {
        sp_n_samples = size / spectrum_factor;
        firdecim_crcf_execute_block(sp_decim, buf_filtered, sp_n_samples, spectrum_dec_buf);
        sp_sg->execute_block(spectrum_dec_buf, sp_n_samples);
        if (waterfall_fft_decim) {
            samples_for_wf = spectrum_dec_buf;
            wf_n_samples = sp_n_samples;
        }
    } else {
        sp_sg->execute_block(buf_filtered, sp_n_samples);
    }
    if (wf_sg) {
        wf_sg->execute_block(samples_for_wf, wf_n_samples);
    }
}

static bool update_spectrum(ChunkedSpgram *sp_sg, uint64_t now, bool tx, uint32_t base_freq) {
    if ((now - spectrum_time > spectrum_fps_ms) && sp_sg->ready()) {
        sp_sg->get_psd(spectrum_psd);
        liquid_vectorf_addscalar(spectrum_psd, SPECTRUM_NFFT, DB_OFFSET + zoom_level_offset, spectrum_psd);
        // Shift filtered
        if (base_freq != spectrum_prev_freq) {
            int32_t shift = ((int64_t)base_freq - spectrum_prev_freq) * (spectrum_factor * SPECTRUM_NFFT) / 100000;
            float *src = spectrum_psd_filtered;
            float *dst = spectrum_psd_filtered;
            float *to_clear_p;
            int32_t size;
            if (shift > 0) {
                src = spectrum_psd_filtered + shift;
                size = SPECTRUM_NFFT - shift;
                to_clear_p = spectrum_psd_filtered + size;
            } else if (shift < 0) {
                dst = spectrum_psd_filtered - shift;
                size = SPECTRUM_NFFT + shift;
                to_clear_p = spectrum_psd_filtered;
            }
            if (size > 0) {
                memmove(dst, src, size * sizeof(*src));
                float *stop = to_clear_p + LV_ABS(shift);
                do
                {
                    *to_clear_p++ = S_MIN;
                } while (to_clear_p < stop);

            }
            spectrum_prev_freq = base_freq;
        }
        lpf_block(spectrum_psd_filtered, spectrum_psd, spectrum_beta, SPECTRUM_NFFT);
        spectrum_data(spectrum_psd_filtered, SPECTRUM_NFFT, tx, base_freq);
        spectrum_time = now;
        return true;
    }
    return false;
}

static bool update_waterfall(ChunkedSpgram *wf_sg, uint64_t now, bool tx, uint32_t base_freq) {
    if ((now - waterfall_time > waterfall_fps_ms) && (!psd_delay) & wf_sg->ready()) {
        wf_sg->get_psd(waterfall_psd_lin, true);
        for (size_t i = 0; i < WATERFALL_NFFT; i++) {
            waterfall_psd[i] = 10.0f * log10f(waterfall_psd_lin[i]);
        }
        liquid_vectorf_addscalar(waterfall_psd, WATERFALL_NFFT, DB_OFFSET + zoom_level_offset, waterfall_psd);
        uint32_t width_hz = 100000;
        if (waterfall_fft_decim) {
            width_hz /= spectrum_factor;
        }
        waterfall_data(waterfall_psd, WATERFALL_NFFT, tx, base_freq, width_hz);
        waterfall_time = now;
        return true;
    }
    return false;
}

static void update_s_meter() {
    if (dialog_msg_voice_get_state() != MSG_VOICE_RECORD) {
        int32_t from, to, center;
        int32_t bw = 100000;
        if (fw_decim) {
            bw /= spectrum_factor;
        }
        center = WATERFALL_NFFT / 2;
        from = center + filter_from * WATERFALL_NFFT / bw;
        to = center + filter_to * WATERFALL_NFFT / bw;
        from = LV_MAX(from, 0);
        to = LV_MIN(to, WATERFALL_NFFT - 1);

        float sum_db, sum;
        sum = 0.0f;

        for (int32_t i = from; i <= to; i++) {
            sum += waterfall_psd_lin[i];
        }

        sum_db = 10.0f * log10f(sum) + DB_OFFSET;

        meter_update(sum_db, params.spectrum_beta.x * 0.01f);
    }
}

void dsp_samples(cfloat *buf_samples, uint16_t size, bool tx, uint32_t base_freq, bool vary_freq, uint8_t fft_dec) {
    if (!ready) {
        return;
    }

    if (base_freq != 0) {
        if (cur_freq != base_freq) {
            cur_freq = base_freq;
            waterfall_sg_rx->reset();
            spectrum_sg_rx->reset();
        }
    }

    if (fft_dec && (fft_dec != spectrum_factor)) {
        update_zoom(fft_dec);
    }

    firdecim_crcf sp_decim;
    ChunkedSpgram *sp_sg, *wf_sg;
    uint64_t      now = get_time();

    if (psd_delay) {
        psd_delay--;
    }

    pthread_mutex_lock(&spectrum_mux);
    if (tx) {
        sp_decim = spectrum_decim_tx;
        sp_sg    = spectrum_sg_tx;
        wf_sg    = waterfall_sg_tx;
    } else {
        sp_decim = spectrum_decim_rx;
        sp_sg    = spectrum_sg_rx;
        wf_sg    = waterfall_sg_rx;
    }
    if (vary_freq) {
        wf_sg = NULL;
    }
    process_samples(buf_samples, size, sp_decim, sp_sg, wf_sg, tx);
    update_spectrum(sp_sg, now, tx, base_freq);
    pthread_mutex_unlock(&spectrum_mux);
    if (wf_sg) {
        if (update_waterfall(wf_sg, now, tx, base_freq)) {
            update_s_meter();
            // TODO: skip on disabled auto min/max
            if (!tx) {
                dsp_update_min_max(waterfall_psd_lin, WATERFALL_NFFT);
            } else {
                min_max_delay = 2;
            }
        }
    }
}

static void update_zoom(int32_t new_zoom) {
    if (new_zoom == spectrum_factor)
        return;

    pthread_mutex_lock(&spectrum_mux);

    spectrum_factor = new_zoom;

    if (spectrum_decim_rx) {
        firdecim_crcf_destroy(spectrum_decim_rx);
        spectrum_decim_rx = NULL;
    }

    if (spectrum_decim_tx) {
        firdecim_crcf_destroy(spectrum_decim_tx);
        spectrum_decim_tx = NULL;
    }

    if ((spectrum_factor > 1) && !fw_decim) {
        spectrum_decim_rx = firdecim_crcf_create_kaiser(spectrum_factor, 8, 60.0f);
        firdecim_crcf_set_scale(spectrum_decim_rx, sqrt(1.0f / (float)spectrum_factor));
        spectrum_decim_tx = firdecim_crcf_create_kaiser(spectrum_factor, 8, 60.0f);
        firdecim_crcf_set_scale(spectrum_decim_tx, sqrt(1.0f / (float)spectrum_factor));
    }

    for (uint16_t i = 0; i < SPECTRUM_NFFT; i++)
        spectrum_psd_filtered[i] = S_MIN;

    spectrum_sg_rx->reset();
    waterfall_sg_rx->reset();

    pthread_mutex_unlock(&spectrum_mux);
}

static void on_zoom_change(Subject *subj, void *user_data) {
    int32_t new_zoom = subject_get_int(subj);
    if ((base_ver.rev < 8) && (util_compare_version(base_ver, (x6100_base_ver_t){1, 1, 9, 0}) < 0)) {
        update_zoom(new_zoom);
    } else {
        zoom_level_offset = log2f(new_zoom) * 3.0f;
    }
}

static void update_filter_from(Subject *subj, void *user_data) {
    filter_from = subject_get_int(cfg_cur.filter.real.from) + subject_get_int(cfg_cur.band->if_shift.val);
}

static void update_filter_to(Subject *subj, void *user_data) {
    filter_to = subject_get_int(cfg_cur.filter.real.to) + subject_get_int(cfg_cur.band->if_shift.val);
}

static void update_cur_mode(Subject *subj, void *user_data) {
    cur_mode = (x6100_mode_t)subject_get_int(cfg_cur.mode);
}

static void on_cur_freq_change(Subject *subj, void *user_data) {
    int32_t new_freq = static_cast<SubjectT<int32_t> *>(subj)->get();
    if (base_ver.rev < 8) {
        psd_delay = OEM_PSD_DELAY;
        cur_freq = new_freq;
    } else {
        psd_delay = R8_PSD_DELAY;
    }
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

    if (rtty_get_state() == RTTY_RX) {
        rtty_put_audio_samples(nsamples, audio);
    } else if (cur_mode == x6100_mode_cw || cur_mode == x6100_mode_cwr) {
        cw_put_audio_samples(nsamples, audio);
    } else {
        dialog_audio_samples(nsamples, audio);
    }
}

static void dsp_update_min_max(float *data_buf, uint16_t size) {
    if (min_max_delay) {
        min_max_delay--;
        return;
    }
    int window_size = (WATERFALL_NFFT * 2500) / 100000;
    if (fw_decim) {
        window_size *= spectrum_factor;
    }
    float power_sum[size - window_size];

    // Sum with window
    for (size_t i = 0; i < size - window_size; i++) {
        power_sum[i] = 0.0f;
        for (size_t j = 0; j < window_size; j++) {
            power_sum[i] += data_buf[i + j];
        }
    }

    // Search minimum
    float min = MAXFLOAT;
    for (size_t i = 0; i < size - window_size; i++) {
        if (min > power_sum[i]) {
            min = power_sum[i];
        }
    }

    // Convert to db
    min = 10.0f * log10f(min) + DB_OFFSET;

    lpf(&noise_level, min, 0.8f, S_MIN);

    min = noise_level;
    meter_set_noise(min);

    min -= 15.0f;

    if (min < S_MIN) {
        min = S_MIN;
    } else if (min > S8) {
        min = S8;
    }
    float max = min + 48.0f;

    spectrum_update_min(min);
    waterfall_update_min(min);

    spectrum_update_max(max);
    waterfall_update_max(max);
}

/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

#include "dsp.h"

#include "cfg/cfg_api.h"
#include "cat/scope_streamer.h"

#include "common/resampler.h"

#include "helpers.h"
#include "util.h"
#include "common/vector.h"

#include <algorithm>
#include <atomic>
#include <map>
#include <mutex>
#include <numeric>
#include <vector>

extern "C" {
    #include "audio.h"
    #include "meter.h"
    #include "radio.h"
    #include "spectrum.h"
    #include "waterfall.h"
    #include "params/params.h"

    #include <math.h>
    #include <pthread.h>
    #include <stdlib.h>
}

#define DB_OFFSET -34.0f
#define OEM_PSD_DELAY 4
#define R8_PSD_DELAY 0

#define SG_ALPHA_WF 0.8f
#define SG_ALPHA_SP 0.4f
#define SG_ALPHA_FACTOR 0.1f

#define FULL_BW_HZ 100000

/* Levels used for the TX spectrum/waterfall/scope, matching the existing
 * S-meter scale (S4 .. S9+20). */
#define DSP_TX_LEVEL_MIN S4
#define DSP_TX_LEVEL_MAX S9_20

// Forward declaration
class ChunkedSpgram;

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

static float auto_min = S_MIN;
static float auto_max = S9_40;

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

/* CPU savers used by the FT8 dialog while it owns the audio pipe. When
 * the corresponding flag is false, the heavy spectrum/waterfall FFT and
 * UI paint pass is skipped entirely. */
static std::atomic<bool> waterfall_enabled{true};
static std::atomic<bool> spectrum_enabled{true};

static cfloat buf_filtered[RADIO_SAMPLES * 2];

static uint32_t cur_freq;
static uint8_t  psd_delay;
static uint8_t  min_max_delay;

static iirfilt_rrrf audio_dc_blocker;

static bool ready = false;

static int32_t filter_from = 0;
static int32_t filter_to   = 3000;
static x6100_mode_t cur_mode;
static float noise_level = S_MIN;

/* Audio subscriptions for audio from BASE */

#define MAX_RAW_SUBS        4
#define MAX_RESAMPLED_SUBS  8
#define MAX_AUDIO_SUBS      (MAX_RAW_SUBS + MAX_RESAMPLED_SUBS)

enum AudioSubKind {
    AUDIO_SUB_FREE = 0,
    AUDIO_SUB_RAW,
    AUDIO_SUB_RESAMPLED,
};

struct AudioSub {
    uint32_t          id        = 0;
    AudioSubKind      kind      = AUDIO_SUB_FREE;
    audio_raw_cb_t    raw_cb    = nullptr;
    audio_float_cb_t  float_cb  = nullptr;
    uint32_t          rate_hz   = 0;
    std::atomic<bool> active{false};
    bool              exclusive = false;
    Resampler        *resampler = nullptr;
};

/*
 * Flat list of subscriptions, guarded by subs_mutex. Free slots have
 * kind == AUDIO_SUB_FREE. IDs are monotonic and never reused, so a stale id
 * matches nothing and dsp_audio_set_active()/dsp_audio_unsubscribe() become
 * harmless no-ops for it.
 *
 * subs_mutex is held for the whole dsp_put_audio_samples() dispatch, so
 * callbacks must never call dsp_audio_subscribe_*(), dsp_audio_unsubscribe()
 * or dsp_audio_set_active() (non-recursive mutex -> deadlock).
 */
static AudioSub           subs[MAX_AUDIO_SUBS];
static std::mutex         subs_mutex;
static uint32_t           next_sub_id = 1;

static void dsp_update_auto_levels(float *psd_lin, uint16_t size);
static void dsp_resolve_levels(bool tx, float *out_min, float *out_max);
static void update_zoom(int32_t new_zoom);
static void on_zoom_change(Subject *subj, void *user_data);
static void update_filters(Subject *subj, void *user_data);
static void update_cur_mode(Subject *subj, void *user_data);
static void on_cur_freq_change(Subject *subj, void *user_data);


class ChunkedSpgram {

    // Window cache keyed by window size. The vectors own the window samples, so
    // the cache is released cleanly when the static map is destroyed at exit.
    static inline std::map<int32_t, std::vector<cfloat>> w_cache;

    size_t   nfft_;
    size_t   chunk_size_  = 0;
    size_t   window_size_ = 0;
    size_t   buffer_size_ = 0;
    windowcf buffer_      = NULL;
    fftplan  fft_;
    cfloat  *buf_time_;
    cfloat  *buf_freq_;
    cfloat  *w_ = NULL;
    float   *psd_;
    bool     accumulate_     = true;
    float    alpha_          = 1.0f;
    float    gamma_          = 1.0f;
    size_t   num_transforms_ = 0;

    void setup_buffer() {
        if (buffer_) {
            windowcf_destroy(buffer_);
        }
        buffer_ = windowcf_create(buffer_size_);
    };

    void setup_window() { w_ = get_cached_window(window_size_); };

    cfloat *get_cached_window(size_t window_size) {
        auto search = w_cache.find((int32_t)window_size);
        if (search != w_cache.end()) {
            return search->second.data();
        }

        std::vector<cfloat> window(window_size);
        for (size_t i = 0; i < window_size; i++) {
            window[i] = liquid_kaiser(i, window_size, 10.0f);
            // window[i] = liquid_hann(i, window_size);
        }
        // scale by window magnitude
        float g = 0.0f;
        for (size_t i = 0; i < window_size; i++)
            g += std::norm(window[i]);
        g = 1.0f / sqrtf(g * nfft_ / window_size);

        // scale window and copy
        for (size_t i = 0; i < window_size; i++)
            window[i] *= g;

        auto it = w_cache.emplace((int32_t)window_size, std::move(window)).first;
        return it->second.data();
    };

  public:
    ChunkedSpgram(size_t nfft) : nfft_(nfft) {
        buf_time_ = (cfloat *)calloc(sizeof(cfloat), nfft);
        buf_freq_ = (cfloat *)calloc(sizeof(cfloat), nfft);
        psd_      = (float *)calloc(sizeof(float), nfft);
        fft_      = fft_create_plan(nfft, buf_time_, buf_freq_, LIQUID_FFT_FORWARD, 0);
    };

    ~ChunkedSpgram() {
        free(buf_time_);
        free(buf_freq_);
        free(psd_);

        if (buffer_) {
            windowcf_destroy(buffer_);
        }
        fft_destroy_plan(fft_);
    };

    void set_alpha(float val) {
        // validate input
        if (val != -1 && (val < 0.0f || val > 1.0f)) {
            printf("set_alpha(), alpha must be in {-1,[0,1]}");
            return;
        }

        // set accumulation flag appropriately
        accumulate_ = (val == -1.0f) ? true : false;

        if (accumulate_) {
            alpha_ = 1.0f;
            gamma_ = 1.0f;
        } else {
            alpha_ = val;
            gamma_ = 1.0f - val;
        }
    };

    void clear() {
        num_transforms_ = 0;
        for (size_t i = 0; i < nfft_; i++) {
            psd_[i]      = 0.0f;
            buf_time_[i] = 0.0f;
        }
    };

    void reset() {
        clear();
        if (buffer_) {
            windowcf_reset(buffer_);
        }
    };

    bool ready() { return num_transforms_ > 0; };

    void execute_block(cfloat *chunk, size_t n_samples, float *last_mag=nullptr) {
        if (n_samples != chunk_size_) {
            chunk_size_  = n_samples;
            window_size_ = LV_MIN(nfft_, chunk_size_);
            buffer_size_ = nfft_ - nfft_ % window_size_;
            setup_window();
            setup_buffer();
            clear();
        }

        cfloat val;
        for (size_t i = 0; i < window_size_; i++) {
            val = chunk[i] * w_[i];
            windowcf_push(buffer_, val);
        }

        cfloat *rc;
        if (windowcf_read(buffer_, &rc) != LIQUID_OK) {
            return;
        }
        memcpy(buf_time_, rc, sizeof(cfloat) * buffer_size_);
        fft_execute(fft_);

        // accumulate output
        // TODO: vectorize this operation
        for (size_t i = 0; i < nfft_; i++) {
            float v = std::norm(buf_freq_[i]);
            if (last_mag) {
                last_mag[i] = v;
            }
            if (num_transforms_ == 0)
                psd_[i] = v;
            else
                psd_[i] = gamma_ * psd_[i] + alpha_ * v;
        }
        num_transforms_++;
    };

    void get_psd_mag(float *psd) {
        // compute magnitude (linear) and run FFT shift
        uint32_t nfft_2 = nfft_ / 2;
        float    scale  = accumulate_ ? 1.0f / LV_MAX(1, num_transforms_) : 1.0f;
        for (size_t i = 0; i < nfft_; i++) {
            uint32_t k = (i + nfft_2) % nfft_;
            psd[i]     = LV_MAX(LIQUID_SPGRAM_PSD_MIN, psd_[k]) * scale;
        }
        if (accumulate_) {
            clear();
        }
    };

    void get_psd(float *psd) {
        // compute magnitude, linear
        get_psd_mag(psd);

        // convert to dB
        for (size_t i = 0; i < nfft_; i++) {
            // 10.0 because psd is squared magnitude (power)
            psd[i] = 10.0f * log10f(psd[i]);
        }
    };
};

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

    audio_dc_blocker = iirfilt_rrrf_create_dc_blocker(2.0f * M_PI_2f32 * 50.0f / AUDIO_CAPTURE_RATE);

    cfg.mode.zoom()->subscribe_and_notify(on_zoom_change);

    cfg.band.if_shift()->subscribe(update_filters);
    cfg.filter.low()->subscribe(update_filters);
    cfg.filter.high()->subscribe_and_notify(update_filters);
    cfg.computed.mode()->subscribe_and_notify(update_filters);

    cfg.computed.mode()->subscribe_and_notify(update_cur_mode);

    cfg.computed.fg_freq()->subscribe(on_cur_freq_change);
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
    // Swap I and Q, add offset
    for (size_t i = 0; i < size; i++) {
        buf_filtered[i] = {buf_samples[i].imag(), buf_samples[i].real()};
    }

    if (!fw_dc_blocker) {
        iirfilt_cccf_execute_block(dc_block, buf_filtered, size, buf_filtered);
    }

    cfloat *samples_for_wf = buf_filtered;
    size_t wf_n_samples = size;
    size_t sp_n_samples = size;

    if (spectrum_enabled.load(std::memory_order_relaxed)) {
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
    }
    if (wf_sg) {
        wf_sg->execute_block(samples_for_wf, wf_n_samples);  // always run FFT for S-meter
    }
}

static bool update_spectrum(ChunkedSpgram *sp_sg, uint64_t now, bool tx, uint32_t base_freq, uint8_t fft_dec, float min,
                            float max) {
    if ((now - spectrum_time > spectrum_fps_ms) && sp_sg->ready()) {
        sp_sg->get_psd(spectrum_psd);
        liquid_vectorf_addscalar(spectrum_psd, SPECTRUM_NFFT, DB_OFFSET + zoom_level_offset, spectrum_psd);
        // Shift filtered
        if (base_freq != spectrum_prev_freq) {
            int32_t shift = ((int64_t)base_freq - spectrum_prev_freq) * (spectrum_factor * SPECTRUM_NFFT) / FULL_BW_HZ;
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
        spectrum_data(spectrum_psd_filtered, SPECTRUM_NFFT, tx, base_freq, fft_dec, min, max);
        spectrum_time = now;
        return true;
    }
    return false;
}

/**
 * Refresh waterfall PSD buffers (common to both S-meter and waterfall UI).
 * Returns true when fresh linear / dB PSD data is ready.
 */
static bool update_waterfall_psd(ChunkedSpgram *wf_sg, uint64_t now) {
    if ((now - waterfall_time > waterfall_fps_ms) && (!psd_delay) & wf_sg->ready()) {
        wf_sg->get_psd_mag(waterfall_psd_lin);
        for (size_t i = 0; i < WATERFALL_NFFT; i++) {
            waterfall_psd[i] = 10.0f * log10f(waterfall_psd_lin[i]);
        }
        liquid_vectorf_addscalar(waterfall_psd, WATERFALL_NFFT, DB_OFFSET + zoom_level_offset, waterfall_psd);
        waterfall_time = now;
        return true;
    }
    return false;
}

static void update_s_meter() {
    int32_t from, to, center;
    int32_t bw = FULL_BW_HZ;
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
    uint64_t      now      = get_time();
    bool          wf_ready = false;
    float         level_min, level_max;

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
    if (wf_sg) {
        wf_ready = update_waterfall_psd(wf_sg, now);
    }
    if (wf_ready) {
        if (tx) {
            min_max_delay = 2;
        } else {
            dsp_update_auto_levels(waterfall_psd_lin, WATERFALL_NFFT);
        }
    }
    dsp_resolve_levels(tx, &level_min, &level_max);
    if (spectrum_enabled.load(std::memory_order_relaxed)) {
        update_spectrum(sp_sg, now, tx, base_freq, fft_dec, level_min, level_max);
    }
    pthread_mutex_unlock(&spectrum_mux);

    if (wf_ready) {
        /* S-meter runs every PSD refresh regardless of waterfall UI state. */
        update_s_meter();

        bool waterfall_on = waterfall_enabled.load(std::memory_order_relaxed);
        if (waterfall_on) {
            uint32_t width_hz = FULL_BW_HZ;
            if (waterfall_fft_decim) {
                width_hz /= spectrum_factor;
            }
            waterfall_data(waterfall_psd, WATERFALL_NFFT, tx, base_freq, width_hz, level_min, level_max);
        }

        // CI-V scope streaming (uses same PSD data, independent of waterfall_on)
        {
            uint32_t width_hz = FULL_BW_HZ;
            if (waterfall_fft_decim) {
                width_hz /= spectrum_factor;
            }
            scope_streamer_push_data(waterfall_psd, WATERFALL_NFFT, base_freq, width_hz, level_min, level_max);
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
    int32_t new_zoom = cfg.mode.zoom()->get();
    if ((base_ver.rev < 8) && (util_compare_version(base_ver, (x6100_base_ver_t){1, 1, 9, 0}) < 0)) {
        update_zoom(new_zoom);
    } else {
        zoom_level_offset = log2f(new_zoom) * 3.0f;
        if (base_ver.rev < 8) {
            // OEM BASE >= 1.1.9 decimates in firmware but does not report fft_dec
            // back via flow_info, so the feedback path in dsp_samples() never fires
            // and spectrum_factor stays at 1. Sync it locally instead.
            update_zoom(new_zoom);
        }
    }
}

static void update_filters(Subject *subj, void *user_data) {
    auto low = cfg.filter.low()->get();
    auto high = cfg.filter.high()->get();
    auto if_shift = cfg.band.if_shift()->get();
    auto mode = cfg.computed.mode()->get();
    switch (mode) {
        case x6100_mode_lsb:
        case x6100_mode_lsb_dig:
        case x6100_mode_cwr:
            filter_from = -high + if_shift;
            filter_to = -low + if_shift;
            break;

        case x6100_mode_am:
        case x6100_mode_nfm:
            filter_from = -high + if_shift;
            filter_to = high + if_shift;
            break;
        default:
            filter_from = low + if_shift;
            filter_to = high + if_shift;
            break;
    }
}

static void update_cur_mode(Subject *subj, void *user_data) {
    cur_mode = (x6100_mode_t)cfg.computed.mode()->get();
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

static uint32_t alloc_sub_id() {
    if (next_sub_id == AUDIO_SUB_INVALID) {
        next_sub_id = 1;
    }
    return next_sub_id++;
}

uint32_t dsp_audio_subscribe_raw(audio_raw_cb_t cb, bool exclusive) {
    if (!cb) {
        return AUDIO_SUB_INVALID;
    }

    std::lock_guard<std::mutex> lock(subs_mutex);

    for (size_t i = 0; i < MAX_AUDIO_SUBS; i++) {
        if (subs[i].kind != AUDIO_SUB_FREE) {
            continue;
        }
        subs[i].id        = alloc_sub_id();
        subs[i].kind      = AUDIO_SUB_RAW;
        subs[i].raw_cb    = cb;
        subs[i].exclusive = exclusive;
        subs[i].active.store(false, std::memory_order_relaxed);
        return subs[i].id;
    }

    return AUDIO_SUB_INVALID;
}

uint32_t dsp_audio_subscribe_resampled(audio_float_cb_t cb, uint32_t target_rate_hz) {
    if (!cb) {
        return AUDIO_SUB_INVALID;
    }

    Resampler *resampler = new Resampler(AUDIO_CAPTURE_RATE / target_rate_hz);

    std::lock_guard<std::mutex> lock(subs_mutex);

    for (size_t i = 0; i < MAX_AUDIO_SUBS; i++) {
        if (subs[i].kind != AUDIO_SUB_FREE) {
            continue;
        }
        subs[i].id        = alloc_sub_id();
        subs[i].kind      = AUDIO_SUB_RESAMPLED;
        subs[i].float_cb  = cb;
        subs[i].rate_hz   = target_rate_hz;
        subs[i].resampler = resampler;
        subs[i].active.store(false, std::memory_order_relaxed);
        return subs[i].id;
    }

    delete resampler;
    return AUDIO_SUB_INVALID;
}

void dsp_audio_set_active(uint32_t id, bool active) {
    if (id == AUDIO_SUB_INVALID) {
        return;
    }

    std::lock_guard<std::mutex> lock(subs_mutex);

    for (size_t i = 0; i < MAX_AUDIO_SUBS; i++) {
        if (subs[i].kind != AUDIO_SUB_FREE && subs[i].id == id) {
            subs[i].active.store(active, std::memory_order_release);
            return;
        }
    }
}

void dsp_audio_unsubscribe(uint32_t id) {
    if (id == AUDIO_SUB_INVALID) {
        return;
    }

    std::lock_guard<std::mutex> lock(subs_mutex);

    for (size_t i = 0; i < MAX_AUDIO_SUBS; i++) {
        if (subs[i].kind == AUDIO_SUB_FREE || subs[i].id != id) {
            continue;
        }

        subs[i].active.store(false, std::memory_order_relaxed);

        if (subs[i].kind == AUDIO_SUB_RESAMPLED) {
            delete subs[i].resampler;
            subs[i].resampler = nullptr;
            subs[i].float_cb  = nullptr;
        } else {
            subs[i].raw_cb = nullptr;
        }

        subs[i].kind = AUDIO_SUB_FREE;
        subs[i].id   = 0;
        return;
    }
}

void dsp_put_audio_samples(size_t nsamples, int16_t *samples) {
    if (!ready) {
        return;
    }

    std::lock_guard<std::mutex> lock(subs_mutex);

    for (size_t i = 0; i < MAX_AUDIO_SUBS; i++) {
        if (subs[i].kind == AUDIO_SUB_RAW && subs[i].raw_cb != nullptr && subs[i].exclusive &&
            subs[i].active.load(std::memory_order_acquire)) {
            subs[i].raw_cb(nsamples, samples);
            return;
        }
    }

    for (size_t i = 0; i < MAX_AUDIO_SUBS; i++) {
        if (subs[i].kind == AUDIO_SUB_RAW && subs[i].raw_cb != nullptr && !subs[i].exclusive &&
            subs[i].active.load(std::memory_order_acquire)) {
            subs[i].raw_cb(nsamples, samples);
        }
    }

    float float_samples[nsamples];
    vector_s16_to_f(samples, float_samples, nsamples);
    for (size_t i = 0; i < nsamples; i++) {
        iirfilt_rrrf_execute(audio_dc_blocker, float_samples[i], &float_samples[i]);
    }

    for (size_t si = 0; si < MAX_AUDIO_SUBS; si++) {
        if (subs[si].kind != AUDIO_SUB_RESAMPLED || subs[si].resampler == nullptr) {
            continue;
        }
        if (!subs[si].active.load(std::memory_order_acquire)) {
            continue;
        }

        Resampler *s = subs[si].resampler;
        size_t decim = s->decim_factor();
        size_t out_n = 0;
        float scratch[nsamples / decim + 1];

        for (size_t i = 0; i < nsamples; i++) {
            if (s->feed(float_samples[i])) {
                scratch[out_n++] = s->execute();
            }
        }
        if (out_n > 0) {
            subs[si].float_cb(out_n, scratch);
        }
    }
}

static void dsp_update_auto_levels(float *psd_lin, uint16_t size) {
    if (min_max_delay) {
        min_max_delay--;
        return;
    }
    int32_t bw_hz = FULL_BW_HZ;
    if (fw_decim) {
        bw_hz /= spectrum_factor;
    }

    // 2.5 kHz
    uint32_t win_size_hz = 2500;

    int window_size = (size * win_size_hz) / bw_hz;

    // Skip borders
    size_t start = size * 0.04f;
    size_t stop = size * (1 - 0.04f);

    float power_sum[stop - start - window_size];

    float running = 0.0f;
    for (size_t j = 0; j < window_size; j++)
        running += psd_lin[start + j];
    power_sum[0] = running;
    float min = running;

    for (size_t i = 1; i < stop - start - window_size; i++) {
        running += psd_lin[start + i + window_size - 1] - psd_lin[start + i - 1];
        power_sum[i] = running;
        if (running < min) min = running;
    }
    min = LV_MAX(1e-12f, min);

    // Get Minimum Statistics offset for the noise level
    float offset;
    switch (bw_hz)
    {
    case FULL_BW_HZ:
        offset = 3.98f;
        break;
    case FULL_BW_HZ / 2:
        offset = 2.45f;
        break;
    case FULL_BW_HZ / 4:
        offset = 1.46f;
        break;
    default:
        offset = 0.82f;
        break;
    }

    // Convert to db
    min = 10.0f * log10f(min) + DB_OFFSET + offset;

    lpf(&noise_level, min, 0.8f, S_MIN);

    min = noise_level;
    // Use win size for min/max and bandwidth for noise level on S-meter
    float noise_bw_offset = 10.0f * log10f(((float)filter_to - filter_from) / win_size_hz);
    meter_set_noise(min + noise_bw_offset);

    min -= 19.0f;

    if (min < S_MIN) {
        min = S_MIN;
    } else if (min > S8) {
        min = S8;
    }
    auto_min = min;
    auto_max = auto_min + 48.0f;
}

/* Resolve the min/max pair passed to the spectrum/waterfall/scope consumers.
 * Single point of level policy: TX defaults, auto levels + offset, or the
 * manual grid levels. Called once per frame from dsp_samples(). */
static void dsp_resolve_levels(bool tx, float *out_min, float *out_max) {
    if (tx) {
        *out_min = DSP_TX_LEVEL_MIN;
        *out_max = DSP_TX_LEVEL_MAX;
    } else if (cfg.spectrum.auto_level_enabled()->get()) {
        float offset = cfg.spectrum.auto_level_offset()->get();
        *out_min = auto_min - offset;
        *out_max = auto_max - offset;
    } else {
        *out_min = cfg.band.grid_min()->get();
        *out_max = cfg.band.grid_max()->get();
    }
}

void dsp_set_waterfall_enabled(bool enabled) {
    waterfall_enabled.store(enabled, std::memory_order_relaxed);
    if (enabled) {
        psd_delay = 4;
        waterfall_time = get_time();
    }
}
void dsp_set_spectrum_enabled(bool enabled) {
    spectrum_enabled.store(enabled, std::memory_order_relaxed);
    if (enabled) {
        psd_delay = 4;
        spectrum_time = get_time();
    }
}

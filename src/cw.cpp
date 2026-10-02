/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */
#include "cw.h"

#include <array>
#include <cmath>
#include <optional>

#include "cfg/cfg_api.h"
#include "cw_decoder/cw_receiver.h"


extern "C" {
    #include "lvgl/lvgl.h"
    #include "panel.h"
    #include "meter.h"
    #include "cw_tune_ui.h"
    #include "pubsub_ids.h"
}

template <std::size_t N>
class FilterQueue {
    std::array<float, N> values;

    size_t id = 0;
    size_t count = 0;

public:
    void put(float val) {
        values[id] = val;
        id = (id + 1) % N;
        if (count < N) {
            count++;
        }
    }

    std::optional<float> get() {
        if (count == N) {
            size_t mid_id = (id + N / 2) % N;
            return {values[mid_id]};
        }
        return {};
    }

    void reset() {
        count = 0;
    }
};

static bool ready = false;

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

static uint32_t     dsp_audio_sub_id = AUDIO_SUB_INVALID;
static x6100_mode_t mode;
static uint8_t update_counter = 0;

static void on_key_tone_change(Subject *subj, void *user_data);
static void on_threshold_change(Subject *subj, void *user_data);
static void on_val_bool_change(Subject *subj, void *user_data);
static void on_low_filter_change(Subject *subj, void *user_data);
static void on_high_filter_change(Subject *subj, void *user_data);
static void on_cw_mode_change(Subject *subj, void *user_data);
static void on_cw_peak_change(Subject *subj, void *user_data);
static void update_cw_active();

static void cw_on_off_cb(bool val);
static void on_cw_frame();

namespace {
    cw::CwReceiver cw_receiver{panel_add_text, cw_on_off_cb, on_cw_frame};
    FilterQueue<3> freq_queue{};
} // end namespace

void cw_init() {
    cfg.cw.key_tone()->subscribe_and_notify(on_key_tone_change);
    tone_freq = key_tone;
    cfg.cw.decoder()->subscribe_and_notify(on_val_bool_change, (void*)&cw_decoder);
    cfg.cw.tune()->subscribe_and_notify(on_val_bool_change, (void*)&cw_tune);

    cfg.cw.decoder_snr()->subscribe_and_notify(on_threshold_change);
    cfg.cur.mode()->subscribe_and_notify(on_cw_mode_change);

    if (dsp_audio_sub_id == AUDIO_SUB_INVALID) {
        dsp_audio_sub_id = dsp_audio_subscribe_float(cw_put_audio_samples, static_cast<uint32_t>(cw::CwReceiver::SAMPLE_RATE));
        update_cw_active();
    }

    // 0.95 - 0.97
    // cw_detector = new CWDetector((float)CW_CAPTURE_RATE, 0.95f);
    // cfg.cw.key_tone()->subscribe_and_notify([](Subject *, void *) {
    //     cw_detector->set_f0(cfg.cw.key_tone()->get());
    // });

    cfg.filter.low()->subscribe_and_notify(on_low_filter_change);
    cfg.filter.high()->subscribe_and_notify(on_high_filter_change);

    // CW peak support
    cfg.cw.key_tone()->subscribe(on_cw_peak_change);
    cfg.cw.peak_on()->subscribe(on_cw_peak_change);
    cfg.cw.peak_q()->subscribe_and_notify(on_cw_peak_change);

    ready = true;
}

static void update_peak_freq(float freq) {
    if (peak_on) {
        cw_tune_set_freq(freq);
    }
}

void cw_put_audio_samples(size_t n, float *samples) {
    if (!ready) {
        return;
    }
    if ((!cw_decoder) && (!cw_tune)) {
        return;
    }

    cw_receiver.process_audio_frame(n, samples);
}

static void on_cw_frame() {
    if (peak_on) {
        float new_freq = cw_receiver.get_tone_freq();
        if ((new_freq >= filter_low) && (new_freq <= filter_high)) {
            freq_queue.put(new_freq);
            auto filtered_freq = freq_queue.get();
            if (filtered_freq) {
                new_freq = *filtered_freq;
                tone_freq += 0.5f * (new_freq - tone_freq);
            }
        }
    } else {
        freq_queue.reset();
    }

    update_counter++;
    if (update_counter >= 12) {
        update_counter = 0;
        // printf("peak_on: %d, cw_receiver.get_tone_freq(): %f\n", peak_on, cw_receiver.get_tone_freq());
        update_peak_freq(tone_freq - key_tone);
        char buf[8];
        snprintf(buf, 8, "WPM: %.0f", cw_receiver.get_measured_wpm());
        panel_set_info(buf);
    }
}

float cw_get_tone_freq(void) {
    return tone_freq;
}

static void on_key_tone_change(Subject *subj, void *user_data) {
    key_tone = cfg.cw.key_tone()->get();
}

static void on_val_bool_change(Subject *subj, void *user_data) {
    *(bool*)user_data = static_cast<SubjectT<int32_t>*>(subj)->get();
    update_cw_active();
}

static void on_threshold_change(Subject *subj, void *user_data) {
    cw_receiver.change_threshold(cfg.cw.decoder_snr()->get());
}

static void on_low_filter_change(Subject *subj, void *user_data) {
    filter_low = cfg.filter.low()->get();
    cw_receiver.change_hpf_hz(filter_low);

}

static void on_high_filter_change(Subject *subj, void *user_data) {
    filter_high = cfg.filter.high()->get();
    cw_receiver.change_lpf_hz(filter_high);
}

static void on_cw_mode_change(Subject *subj, void *user_data) {
    mode = static_cast<x6100_mode_t>(static_cast<SubjectT<int32_t>*>(subj)->get());
    update_cw_active();
}

void on_cw_peak_change(Subject *subj, void *user_data) {
    float key_tone = static_cast<float>(cfg.cw.key_tone()->get());
    float q = static_cast<float>(cfg.cw.peak_q()->get());
    bool on = cfg.cw.peak_on()->get();
    cw_receiver.change_peak_filter(on, key_tone, q);
}

static void update_cw_active() {
    bool on = (mode == x6100_mode_cw || mode == x6100_mode_cwr)
           && (cw_decoder || cw_tune);
    dsp_audio_set_active(dsp_audio_sub_id, on);
}

void cw_on_off_cb(bool val) {
    peak_on = val;
}


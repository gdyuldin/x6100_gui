/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI - FT8 TX player
 *
 *  Copyright (c) 2026
 */

#include "tx_worker.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "lvgl/lvgl.h"

#include "../audio.h"
#include "../cfg/cfg.h"
#include "../cfg/subjects.h"
#include "../params/params.h"
#include "../radio.h"
#include "../tx_info.h"
#include "worker.h"

/* FT8 audio tone offset inside the radio passband. */
#define SIGNAL_FREQ_HZ   1325
#define MAX_PWR_W        5.0f
#define GAIN_MIN_DB     (-30.0f)
#define GAIN_MAX_DB      0.0f

/* ALC-driven gain correction (legacy formula from dialog_ft8.c). */
static float get_correction(void) {
    static uint8_t msg_id = 0;
    float correction = 0.0f;
    float pwr        = 0.0f;
    float alc        = 0.0f;

    if (tx_info_refresh(&msg_id, &alc, &pwr, NULL)) {
        float target_pwr = LV_MIN(subject_get_float(cfg.pwr.val), MAX_PWR_W);
        if (alc > 0.5f) {
            correction = log10f(log10f(11.1f - alc)) * 20.0f - 0.38f;
        } else if (target_pwr - pwr > 0.5f) {
            correction = log10f(target_pwr / (pwr + 0.01f)) * 10.0f;
        }
    }
    return correction;
}

bool tx_worker_run_with_config(const ft8_tx_config_t *cfg) {
    const char    *tx_text             = cfg->tx_text;
    int32_t        audio_sample_rate   = cfg->audio_sample_rate;
    float          base_gain_offset    = cfg->base_gain_offset;
    bool           force_free_text     = cfg->force_free_text;
    float          sec_since_slot_start = cfg->sec_since_slot_start;
    tx_abort_fn_t  abort_check         = cfg->abort_check;
    void          *abort_check_ctx     = cfg->abort_check_ctx;
    int16_t *samples   = NULL;
    uint32_t n_samples = 0;

    if (!ftx_worker_generate_tx_samples(tx_text, SIGNAL_FREQ_HZ,
                                        (uint32_t)audio_sample_rate,
                                        &samples, &n_samples)) {
        return true; /* nothing to send; not an abort */
    }

    if (subject_get_float(cfg.pwr.val) > MAX_PWR_W) {
        radio_set_pwr(MAX_PWR_W);
    }

    float gain_offset      = base_gain_offset + params.ft8_output_gain_offset.x;
    float play_gain_offset = audio_set_play_vol(gain_offset + 6.0f);
    gain_offset           -= play_gain_offset;

    uint64_t radio_freq = subject_get_int(cfg_cur.fg_freq);
    radio_set_freq((int32_t)radio_freq + (int32_t)params.ft8_tx_freq.x - SIGNAL_FREQ_HZ);
    radio_set_modem(true);

    float    prev_gain_offset = gain_offset;
    size_t   counter          = 0;
    int16_t *ptr              = samples;
    size_t   part;

    bool aborted = false;
    while (true) {
        if (counter > 30) {
            gain_offset += get_correction() * 0.4f;
            if (gain_offset > GAIN_MAX_DB) {
                gain_offset = GAIN_MAX_DB;
            } else if (gain_offset < GAIN_MIN_DB) {
                gain_offset = GAIN_MIN_DB;
            }
        }
        if (n_samples <= 0) {
            break;
        }
        if (abort_check && abort_check(abort_check_ctx)) {
            aborted = true;
            break;
        }
        part = LV_MIN(1024 * 2, n_samples);
        if (gain_offset == prev_gain_offset) {
            if (gain_offset != 0.0f) {
                audio_gain_db(ptr, part, gain_offset, ptr);
            }
        } else {
            audio_gain_db_transition(ptr, part, prev_gain_offset, gain_offset, ptr);
            prev_gain_offset = gain_offset;
        }
        audio_play(ptr, part);
        n_samples -= part;
        ptr       += part;
        counter++;
    }

    params_float_set(&params.ft8_output_gain_offset,
                     gain_offset - base_gain_offset + play_gain_offset);
    audio_play_wait();
    radio_set_modem(false);
    radio_set_freq((int32_t)radio_freq);
    free(samples);
    audio_set_play_vol(params.play_gain_db_f.x);

    return !aborted;
}

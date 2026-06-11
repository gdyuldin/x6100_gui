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
#include <ft8lib/constants.h>

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

/* FT8 TX must end by 14.5 s into the 15 s slot so the receiver can
 * resync for the next slot. Allows tail-align to truncate the front
 * of a late-started burst rather than skipping the TX entirely. */
#define FT8_TX_END_SEC   14.5f

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

bool tx_worker_run_with_config(const ft8_tx_config_t *tx_cfg) {
    const char    *tx_text             = tx_cfg->tx_text;
    float          base_gain_offset    = tx_cfg->base_gain_offset;
    float          sec_since_slot_start = tx_cfg->sec_since_slot_start;
    tx_abort_fn_t  abort_check         = tx_cfg->abort_check;
    void          *abort_check_ctx     = tx_cfg->abort_check_ctx;
    int16_t *samples   = NULL;
    uint32_t n_samples = 0;

    if (!ftx_worker_generate_tx_samples(tx_text, SIGNAL_FREQ_HZ,
                                        (uint32_t)AUDIO_PLAY_RATE,
                                        &samples, &n_samples)) {
        return true; /* nothing to send; not an abort */
    }

    /* Tail-align (FT8 only): if we started late in the slot, drop a
     * matching prefix from the synthesised audio so the burst still
     * ends by FT8_TX_END_SEC. The receiver demodulates the tail of
     * the signal, so a clean tail matters more than a clean head.
     * FT4 slot is short enough that overrun is not an issue. */
    uint32_t skip_samples = 0;
    if (subject_get_int(cfg.ft8_protocol.val) == FTX_PROTOCOL_FT8) {
        float remain_sec = FT8_TX_END_SEC - sec_since_slot_start;
        if (remain_sec <= 0.0f) {
            free(samples);
            return false;
        }
        float burst_sec = (float)n_samples / (float)AUDIO_PLAY_RATE;
        float skip_sec  = burst_sec - remain_sec;
        if (skip_sec > 0.0f) {
            skip_samples = (uint32_t)(0.5f + skip_sec * (float)AUDIO_PLAY_RATE);
            if (skip_samples >= n_samples) {
                free(samples);
                return false;
            }
        }
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
    int16_t *ptr              = samples + skip_samples;
    size_t   part;
    n_samples                -= skip_samples;

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

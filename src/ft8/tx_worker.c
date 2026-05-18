/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI - FT8 TX player
 *
 *  Copyright (c) 2026
 */

#include "tx_worker.h"

#include <math.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "../audio.h"
#include "../cfg/cfg.h"
#include "../cfg/subjects.h"
#include "../params/params.h"
#include "../radio.h"
#include "../tx_info.h"
#include "worker.h"

/* FT8 signal tone relative to USB Dig audio center.
 *
 * Two TX modes are supported, switched by cfg.ft8_xit:
 *   - XIT ON  (default, legacy X6100 behaviour): synth at SIGNAL_FREQ_DEFAULT_HZ
 *     and shift the radio center frequency by (ft8_tx_freq - SIGNAL_FREQ_DEFAULT_HZ)
 *     before TX, then restore. Keeps the on-air signal at the user's tuned
 *     QRG regardless of where in the passband the cursor sits.
 *   - XIT OFF (WSJT-X style): keep the radio center fixed and synth the tone
 *     directly at the cursor offset (clamped to the active filter bandwidth).
 *     Avoids two PLL retunes per slot but the on-air QRG follows the cursor.
 */
#define SIGNAL_FREQ_DEFAULT_HZ 1325
#define MAX_PWR_W         10.0f
#define GAIN_MIN_DB       (-30.0f)
#define GAIN_MAX_DB       0.0f

/* FT8 TX must end by 14.5 s into the 15 s slot so the receiver can
 * resync for the next slot. */
#define FT8_TX_END_SEC 14.5f

static inline int32_t clamp_i32(int32_t v, int32_t lo, int32_t hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static atomic_bool s_abort = false;

void tx_worker_request_abort(void) { atomic_store(&s_abort, true); }
void tx_worker_clear_abort(void)   { atomic_store(&s_abort, false); }
bool tx_worker_abort_requested(void) { return atomic_load(&s_abort); }

/* ALC-driven gain correction. Bounded so we never feed +>0 dBFS into the
 * mixer. */
static float compute_correction(void) {
    static uint8_t msg_id = 0;
    float correction = 0.0f;
    float pwr        = 0.0f;
    float alc        = 0.0f;

    if (tx_info_refresh(&msg_id, &alc, &pwr, NULL)) {
        float target_pwr = subject_get_float(cfg.pwr.val);
        if (target_pwr > MAX_PWR_W) target_pwr = MAX_PWR_W;
        if (alc > 0.5f) {
            correction = log10f(log10f(11.1f - alc)) * 20.0f - 0.38f;
        } else if (target_pwr - pwr > 0.5f) {
            correction = log10f(target_pwr / (pwr + 0.01f)) * 10.0f;
        }
    }
    return correction;
}

static inline float clamp_gain(float g) {
    if (g > GAIN_MAX_DB) return GAIN_MAX_DB;
    if (g < GAIN_MIN_DB) return GAIN_MIN_DB;
    return g;
}

bool tx_worker_run(const char *tx_text, bool force_free_text,
                   int32_t audio_sample_rate,
                   float   base_gain_offset,
                   float   sec_since_slot_start) {
    if (!tx_text) return false;

    /* Resolve TX mode + tone frequency before generating samples. The XIT
     * decision is latched once per call so a mid-TX toggle cannot leave us
     * with a tone at one freq and the radio retuned for another. */
    bool    xit_mode    = subject_get_int(cfg.ft8_xit.val);
    int32_t signal_freq;
    if (xit_mode) {
        signal_freq = SIGNAL_FREQ_DEFAULT_HZ;
    } else {
        int32_t lo = (int32_t)subject_get_int(cfg_cur.filter.low);
        int32_t hi = (int32_t)subject_get_int(cfg_cur.filter.high);
        if (hi < lo) { int32_t t = lo; lo = hi; hi = t; }
        signal_freq = clamp_i32((int32_t)params.ft8_tx_freq.x, lo, hi);
    }

    int16_t *samples   = NULL;
    uint32_t n_samples = 0;

    if (!ftx_worker_generate_tx_samples(tx_text, force_free_text,
                                        (uint32_t)signal_freq,
                                        (uint32_t)audio_sample_rate,
                                        &samples, &n_samples)) {
        return false;
    }

    /* Tail-align (FT8 only): crop the beginning so TX ends by 14.5 s. */
    int protocol = subject_get_int(cfg.ft8_protocol.val);
    uint32_t skip_samples = 0;
    if (protocol == FTX_PROTOCOL_FT8) {
        float remain_sec = FT8_TX_END_SEC - sec_since_slot_start;
        if (remain_sec <= 0.0f) {
            free(samples);
            return false;
        }
        float burst_sec = (float)n_samples / (float)audio_sample_rate;
        float skip_sec = burst_sec - remain_sec;
        if (skip_sec > 0.0f) {
            skip_samples = (uint32_t)(0.5f + skip_sec * (float)audio_sample_rate);
            if (skip_samples >= n_samples) {
                free(samples);
                return false;
            }
        }
    }

    /* Enforce the 10 W ceiling even if the user dialed higher. */
    if (subject_get_float(cfg.pwr.val) > MAX_PWR_W) {
        radio_set_pwr(MAX_PWR_W);
    }

    /* Playback gain offset: session-persistent learned offset minus whatever
     * gain the audio mixer happens to be at right now. */
    float gain_offset        = base_gain_offset + params.ft8_output_gain_offset.x;
    float play_gain_offset   = audio_set_play_vol(gain_offset + 6.0f);
    gain_offset             -= play_gain_offset;

    /* Capture radio freq up-front so we can restore it even if XIT is toggled
     * during TX. In XIT mode the center moves to put the tone at the cursor;
     * in fixed-VFO mode the radio is left alone. */
    uint64_t radio_freq = subject_get_int(cfg_cur.fg_freq);
    if (xit_mode) {
        radio_set_freq((int32_t)radio_freq + (int32_t)params.ft8_tx_freq.x - SIGNAL_FREQ_DEFAULT_HZ);
    }
    radio_set_modem(true);

    float    prev_gain_offset = gain_offset;
    uint32_t counter          = 0;
    int16_t *ptr              = samples + skip_samples;
    n_samples                -= skip_samples;

    bool aborted = false;
    while (n_samples > 0) {
        if (atomic_load(&s_abort)) { aborted = true; break; }

        if (counter > 30) {
            gain_offset += compute_correction() * 0.4f;
            gain_offset  = clamp_gain(gain_offset);
        }
        uint32_t part = (n_samples > 2048u) ? 2048u : n_samples;

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
    if (xit_mode) {
        radio_set_freq((int32_t)radio_freq);
    }
    free(samples);
    audio_set_play_vol(params.play_gain_db_f.x);

    return !aborted;
}

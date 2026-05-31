/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI - FT8 TX player
 *
 *  Copyright (c) 2026
 */

/*
 *  Synthesises an FT8/FT4 TX waveform from tx_text and pushes it to the
 *  audio output with per-block ALC-based gain correction. Blocks for the
 *  duration of one slot, so it is invoked from the decode thread between
 *  RX frames.
 *
 *  The caller supplies an abort-check callback that the worker polls each
 *  block, so dialog state (e.g. dialog_ft8.c's ft8_state_t) can preempt
 *  TX without this module owning that state itself.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Abort-check callback. Return true to stop TX after the current block. */
typedef bool (*tx_abort_fn_t)(void *ctx);

/* ---- TX configuration struct ------------------------------------------
 *
 *  Replaces the long positional-parameter list of tx_worker_run() so
 *  future feature PRs (free-msg, tail-align, etc.) can set their own
 *  fields without changing the signature and causing merge conflicts.
 *
 *  Fields:
 *    tx_text             - FT8/FT4 message text (e.g. "CQ BG7NZL OL63")
 *    audio_sample_rate   - audio output sample rate (e.g. AUDIO_PLAY_RATE)
 *    base_gain_offset    - per-band/per-firmware gain offset (dB)
 *    force_free_text     - when true, encode as FT8 free text (free-msg PR)
 *    sec_since_slot_start- seconds since slot boundary, for tail-align (tail-align PR)
 *    abort_check         - polled each block; return true to stop TX
 *    abort_check_ctx     - opaque context passed to abort_check
 */
typedef struct {
    const char    *tx_text;
    int32_t        audio_sample_rate;
    float          base_gain_offset;
    bool           force_free_text;
    float          sec_since_slot_start;
    tx_abort_fn_t  abort_check;
    void          *abort_check_ctx;
} ft8_tx_config_t;

/* Transmit using a config struct. Returns true on normal completion;
 * false if the abort callback fired mid-transmit. */
bool tx_worker_run_with_config(const ft8_tx_config_t *cfg);

/* Legacy positional-parameter wrapper. Kept for callers that haven't
 * migrated to ft8_tx_config_t yet. */
static inline bool tx_worker_run(const char    *tx_text,
                                 int32_t        audio_sample_rate,
                                 float          base_gain_offset,
                                 tx_abort_fn_t  abort_check,
                                 void          *abort_check_ctx) {
    ft8_tx_config_t cfg = {
        .tx_text             = tx_text,
        .audio_sample_rate   = audio_sample_rate,
        .base_gain_offset    = base_gain_offset,
        .force_free_text     = false,
        .sec_since_slot_start = 0.0f,
        .abort_check         = abort_check,
        .abort_check_ctx     = abort_check_ctx,
    };
    return tx_worker_run_with_config(&cfg);
}

#ifdef __cplusplus
}
#endif

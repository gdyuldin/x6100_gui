/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI - FT8 TX player
 *
 *  Copyright (c) 2026
 */

/*
 *  Synthesises the TX waveform from tx_msg text and pushes it to the audio
 *  output, with per-slot ALC-based gain correction. Blocks for the duration
 *  of one FT8/FT4 slot (~12.6s / ~5.0s) so it is invoked from the decode
 *  thread between RX frames, the same way the old decode_thread did.
 *
 *  State query lets the caller early-abort from outside the thread.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Transmit tx_text at base_freq_hz + 1325 Hz (FT8 signal centre). Returns
 * true on normal completion, false on immediate failure (generate_tx_samples
 * failed). The function only returns when TX has finished or an abort flag
 * was set via tx_worker_request_abort(). */
bool tx_worker_run(const char *tx_text,
                   bool force_free_text,
                   int32_t audio_sample_rate,
                   float   base_gain_offset,
                   float   sec_since_slot_start);

/* External early-termination flag. Polled by tx_worker inside its send loop.
 * Thread-safe via atomic. */
void tx_worker_request_abort(void);
void tx_worker_clear_abort(void);
bool tx_worker_abort_requested(void);

#ifdef __cplusplus
}
#endif

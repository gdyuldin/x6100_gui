/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI - FT8 file-log module
 *
 *  Copyright (c) 2026
 */

/*
 *  Per-session plain-text file log at /mnt/ft8_logs/x6100_log_<timestamp>.txt.
 *  Distinct from src/qso_log.* which is a sqlite QSO database.
 *
 *  File format (CSV-ish, one record per line):
 *      RX,<slot_ts>,<rf_hz>,<snr>,<text>
 *      TX,<slot_ts>,<rf_hz>,<text>
 *      QSO,<slot_ts>,<remote_call>
 *      DNF,<slot_ts>,<center_hz>,<delta_db>
 *  slot_ts is UTC-local "%Y-%m-%d_%H-%M-%S".
 *
 *  ft8_log_rx_collect() dedups and queues RX lines until the slot ends;
 *  ft8_log_rx_flush_slot() writes them out at slot end so the file stays
 *  chronologically ordered regardless of worker decode timing.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

/* Per-RX-slot metadata shared across the ft8 modules. */
typedef struct {
    bool     odd;
    bool     answer_generated;
    time_t   slot_start;
    uint64_t base_freq_hz;
} slot_info_t;

#ifdef __cplusplus
extern "C" {
#endif

void ft8_log_init(void);
void ft8_log_close(void);

void ft8_log_rx_collect(const slot_info_t *info, float freq_hz, int snr, const char *text);
void ft8_log_rx_flush_slot(const slot_info_t *info);

void ft8_log_tx(time_t slot_start, uint64_t base_freq_hz, uint32_t hz_offset, const char *text);
void ft8_log_qso(time_t slot_start, const char *remote_call);
void ft8_log_dnf(time_t slot_start, uint16_t center_hz, float delta_db);

#ifdef __cplusplus
}
#endif

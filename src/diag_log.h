/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Xiegu X6100 - diagnostic log (1KO125, Hany El Imam 2026)
 *
 * /mnt/ptt.log on the DATA partition, one line per event, UTC time: every
 * TX command of the APP with its origin (front PTT key, CAT USB/Bluetooth,
 * CAT LAN, FT8, voice messages, ATU, SWR scan...), and every RX/TX change
 * reported by the BASE, marked "NOT COMMANDED" when the APP had not asked
 * for TX (microphone PTT, rear PTT jack, or the BASE itself). CW is left
 * out: the CW key and the TX of the CW modes.
 *
 * The file is limited to DIAG_MAX_BYTES; the previous part is kept as
 * ptt.log.old. At most DIAG_MAX_PER_S lines a second are written; the next
 * line says how many were skipped.
 */
#pragma once
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Writes the start line. Call first thing in main(). */
void diag_init(void);
/* Name of the origin of the TX commands of the calling thread
 * ("CAT LAN", "FT8"...); a static string. */
void diag_set_source(const char *name);
const char *diag_source(void);
void diag_ptt_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
#ifdef __cplusplus
}
#endif

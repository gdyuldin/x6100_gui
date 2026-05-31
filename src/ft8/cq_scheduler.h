/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI - FT8 CQ scheduler
 *
 *  Copyright (c) 2026
 */

/*
 *  Composes outgoing CQ TX messages. Currently exposes just the message
 *  builder; higher-level scheduling (when to actually queue a CQ for the
 *  next slot, pausing for an active QSO, etc.) lives in dialog_ft8.c for
 *  now. This header is a placeholder boundary for future moves.
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Build a canonical FT8 CQ message of the form
 *   "CQ <callsign> <grid>"               (no modifier)
 *   "CQ_<mod> <callsign> <grid>"         (with modifier)
 * The result is written to 'out' (caller-allocated). Behaviour matches
 * the historical make_cq_msg() in dialog_ft8.c: the message is round-
 * tripped through ftx_message_{encode,decode} for validation, and any
 * codec error is logged via LV_LOG_USER but does NOT clear 'out'. */
void cq_make_message(const char *callsign,
                     const char *qth,
                     const char *cq_mod,
                     char       *out);

#ifdef __cplusplus
}
#endif

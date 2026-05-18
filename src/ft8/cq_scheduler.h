/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI - FT8 CQ scheduler
 *
 *  Copyright (c) 2026
 */

/*
 *  Composes the next CQ TX message and decides whether/when to queue it
 *  for the next slot. Mirrors the legacy schedule_cq_tx_if_needed() flow
 *  but uses qso_state instead of global variables.
 *
 *  State here is only the "cq_paused_for_qso" flag, which lives on the UI
 *  thread and is toggled by the session loop.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "../cfg/subjects.h"
#include "qso_state.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool paused_for_qso;
} cq_state_t;

void cq_state_reset(cq_state_t *cq);

/* Build a canonical CQ message "CQ [_mod] <callsign> [<grid>]" into out. */
void cq_make_message(const char *callsign,
                     const char *qth,
                     const char *cq_mod,
                     char *out, size_t out_sz,
                     bool omit_qth);

/* Try to schedule a CQ TX into qso_state. Returns true if the CQ was queued
 * (tx_msg populated, tx_time_slot set). Does nothing if any precondition
 * is false (tx_enabled off, cq_enabled off, tx_msg non-empty, call in
 * progress, or empty callsign). */
bool cq_schedule_if_needed(cq_state_t   *cq,
                           qso_state_t  *qso,
                           Subject      *cq_enabled_subj,
                           Subject      *tx_enabled_subj,
                           bool          ftx_qso_has_current);

#ifdef __cplusplus
}
#endif

/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI - FT8 QSO state
 *
 *  Copyright (c) 2026
 */

/*
 *  Thread-safe container for the scattered per-QSO state that used to live
 *  as free-floating static variables in dialog_ft8.c:
 *
 *      tx_msg                      queued TX message for the next TX slot
 *      tx_time_slot                which slot (odd/even) to TX in
 *      qso_active_call             other station's callsign for in-progress QSO
 *      qso_tx_exhausted            TX repeats exhausted but QSO still alive
 *      autosel_recover_mode        allow AutoSel to replace the current QSO
 *      autosel_qso_active          current QSO was started by AutoSel
 *      autosel_grid_tx_count       how many times we have sent our grid
 *      pending_to_me_*             unanswered call-to-us while TX was busy
 *      pending_grid_to_me_*        grid-stage priority switch target
 *
 *  All access via this API is mutex-protected. Business logic in the
 *  session loop reads/writes through these accessors - values are never
 *  exposed as globals.
 *
 *  Higher-level helpers (reset_all, clear_qso_pending) make batch updates
 *  atomic so the session loop cannot observe half-updated state.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qso.h"  /* ftx_tx_msg_t, ftx_msg_meta_t */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct qso_state_s qso_state_t;

qso_state_t *qso_state_create(void);
void         qso_state_destroy(qso_state_t *s);

/* Bulk reset: clear every field back to "no QSO, no pending, no TX". */
void qso_state_reset_all(qso_state_t *s);

/* tx_msg accessors. set() takes ownership of the contents - text is copied. */
void qso_state_get_tx_msg(const qso_state_t *s, ftx_tx_msg_t *out);
void qso_state_set_tx_msg(qso_state_t *s, const ftx_tx_msg_t *in);
void qso_state_clear_tx_msg(qso_state_t *s);
bool qso_state_tx_msg_empty(const qso_state_t *s);
int  qso_state_tx_msg_repeats(const qso_state_t *s);
void qso_state_tx_msg_dec_repeats(qso_state_t *s);

/* Mutable borrow: returns a pointer to the internal tx_msg. Caller must hold
 * the state's lock via qso_state_lock()/unlock() around the critical region
 * if it reads/writes multiple fields atomically with ftx_qso_processor_*. */
void qso_state_lock(qso_state_t *s);
void qso_state_unlock(qso_state_t *s);
ftx_tx_msg_t *qso_state_tx_msg_locked(qso_state_t *s);

bool         qso_state_tx_time_slot(const qso_state_t *s);
void         qso_state_set_tx_time_slot(qso_state_t *s, bool odd);

void         qso_state_get_active_call(const qso_state_t *s, char *out, size_t out_sz);
void         qso_state_set_active_call(qso_state_t *s, const char *call);
void         qso_state_clear_active_call(qso_state_t *s);

bool         qso_state_tx_exhausted(const qso_state_t *s);
void         qso_state_set_tx_exhausted(qso_state_t *s, bool v);
bool         qso_state_autosel_recover(const qso_state_t *s);
void         qso_state_set_autosel_recover(qso_state_t *s, bool v);
bool         qso_state_autosel_qso_active(const qso_state_t *s);
void         qso_state_set_autosel_qso_active(qso_state_t *s, bool v);
uint8_t      qso_state_autosel_grid_tx_count(const qso_state_t *s);
void         qso_state_set_autosel_grid_tx_count(qso_state_t *s, uint8_t v);
void         qso_state_inc_autosel_grid_tx_count(qso_state_t *s);

/* Pending replies. */
bool qso_state_has_pending_to_me(const qso_state_t *s);
void qso_state_set_pending_to_me(qso_state_t *s, const ftx_msg_meta_t *meta, bool odd);
void qso_state_get_pending_to_me(const qso_state_t *s, ftx_msg_meta_t *out, bool *out_odd);
void qso_state_clear_pending_to_me(qso_state_t *s);

bool qso_state_has_pending_grid_to_me(const qso_state_t *s);
void qso_state_set_pending_grid_to_me(qso_state_t *s, const ftx_msg_meta_t *meta, bool odd);
void qso_state_get_pending_grid_to_me(const qso_state_t *s, ftx_msg_meta_t *out, bool *out_odd);
void qso_state_clear_pending_grid_to_me(qso_state_t *s);

/* Convenience: clear all "no current QSO" state at once. */
void qso_state_clear_qso_related(qso_state_t *s);

/* Is msg a three-token "DE GRID" exchange? */
bool qso_state_is_grid_exchange_msg(const char *msg);

#ifdef __cplusplus
}
#endif

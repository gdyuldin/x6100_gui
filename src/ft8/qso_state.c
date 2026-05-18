/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI - FT8 QSO state
 *
 *  Copyright (c) 2026
 */

#include "qso_state.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../qth/qth.h"

struct qso_state_s {
    pthread_mutex_t mux;

    ftx_tx_msg_t    tx_msg;
    bool            tx_time_slot;
    char            active_call[16];

    bool            qso_tx_exhausted;
    bool            autosel_recover_mode;
    bool            autosel_qso_active;
    uint8_t         autosel_grid_tx_count;

    bool            pending_to_me_valid;
    bool            pending_to_me_odd;
    ftx_msg_meta_t  pending_to_me_meta;

    bool            pending_grid_to_me_valid;
    bool            pending_grid_to_me_odd;
    ftx_msg_meta_t  pending_grid_to_me_meta;
};

qso_state_t *qso_state_create(void) {
    qso_state_t *s = (qso_state_t *)calloc(1, sizeof(*s));
    if (!s) return NULL;
    pthread_mutex_init(&s->mux, NULL);
    return s;
}

void qso_state_destroy(qso_state_t *s) {
    if (!s) return;
    pthread_mutex_destroy(&s->mux);
    free(s);
}

static inline void lock(qso_state_t *s)   { pthread_mutex_lock(&s->mux); }
static inline void unlock(qso_state_t *s) { pthread_mutex_unlock(&s->mux); }

void qso_state_lock(qso_state_t *s)   { lock(s); }
void qso_state_unlock(qso_state_t *s) { unlock(s); }

ftx_tx_msg_t *qso_state_tx_msg_locked(qso_state_t *s) {
    return s ? &s->tx_msg : NULL;
}

void qso_state_reset_all(qso_state_t *s) {
    if (!s) return;
    lock(s);
    memset(&s->tx_msg, 0, sizeof(s->tx_msg));
    s->tx_time_slot             = false;
    s->active_call[0]           = '\0';
    s->qso_tx_exhausted         = false;
    s->autosel_recover_mode     = false;
    s->autosel_qso_active       = false;
    s->autosel_grid_tx_count    = 0;
    s->pending_to_me_valid      = false;
    s->pending_to_me_odd        = false;
    memset(&s->pending_to_me_meta, 0, sizeof(s->pending_to_me_meta));
    s->pending_grid_to_me_valid = false;
    s->pending_grid_to_me_odd   = false;
    memset(&s->pending_grid_to_me_meta, 0, sizeof(s->pending_grid_to_me_meta));
    unlock(s);
}

void qso_state_get_tx_msg(const qso_state_t *s, ftx_tx_msg_t *out) {
    if (!s || !out) return;
    pthread_mutex_lock((pthread_mutex_t *)&s->mux);
    *out = s->tx_msg;
    pthread_mutex_unlock((pthread_mutex_t *)&s->mux);
}

void qso_state_set_tx_msg(qso_state_t *s, const ftx_tx_msg_t *in) {
    if (!s || !in) return;
    lock(s);
    s->tx_msg = *in;
    unlock(s);
}

void qso_state_clear_tx_msg(qso_state_t *s) {
    if (!s) return;
    lock(s);
    s->tx_msg.msg[0]          = '\0';
    s->tx_msg.repeats         = 0;
    s->tx_msg.force_free_text = false;
    unlock(s);
}

bool qso_state_tx_msg_empty(const qso_state_t *s) {
    if (!s) return true;
    pthread_mutex_lock((pthread_mutex_t *)&s->mux);
    bool empty = (s->tx_msg.msg[0] == '\0');
    pthread_mutex_unlock((pthread_mutex_t *)&s->mux);
    return empty;
}

int qso_state_tx_msg_repeats(const qso_state_t *s) {
    if (!s) return 0;
    pthread_mutex_lock((pthread_mutex_t *)&s->mux);
    int r = s->tx_msg.repeats;
    pthread_mutex_unlock((pthread_mutex_t *)&s->mux);
    return r;
}

void qso_state_tx_msg_dec_repeats(qso_state_t *s) {
    if (!s) return;
    lock(s);
    if (s->tx_msg.repeats > 0) s->tx_msg.repeats--;
    unlock(s);
}

bool qso_state_tx_time_slot(const qso_state_t *s) {
    if (!s) return false;
    pthread_mutex_lock((pthread_mutex_t *)&s->mux);
    bool v = s->tx_time_slot;
    pthread_mutex_unlock((pthread_mutex_t *)&s->mux);
    return v;
}

void qso_state_set_tx_time_slot(qso_state_t *s, bool odd) {
    if (!s) return;
    lock(s);
    s->tx_time_slot = odd;
    unlock(s);
}

void qso_state_get_active_call(const qso_state_t *s, char *out, size_t out_sz) {
    if (!s || !out || out_sz == 0) { if (out && out_sz) out[0] = '\0'; return; }
    pthread_mutex_lock((pthread_mutex_t *)&s->mux);
    snprintf(out, out_sz, "%s", s->active_call);
    pthread_mutex_unlock((pthread_mutex_t *)&s->mux);
}

void qso_state_set_active_call(qso_state_t *s, const char *call) {
    if (!s) return;
    lock(s);
    if (call) snprintf(s->active_call, sizeof(s->active_call), "%s", call);
    else      s->active_call[0] = '\0';
    unlock(s);
}

void qso_state_clear_active_call(qso_state_t *s) {
    qso_state_set_active_call(s, NULL);
}

/* Generic bool accessors - avoid code duplication for the many bool fields. */
static inline bool getb(const qso_state_t *s, const bool *p) {
    pthread_mutex_lock((pthread_mutex_t *)&s->mux);
    bool v = *p;
    pthread_mutex_unlock((pthread_mutex_t *)&s->mux);
    return v;
}
static inline void setb(qso_state_t *s, bool *p, bool v) {
    lock(s); *p = v; unlock(s);
}

bool qso_state_tx_exhausted(const qso_state_t *s)          { return s ? getb(s, &s->qso_tx_exhausted) : false; }
void qso_state_set_tx_exhausted(qso_state_t *s, bool v)    { if (s) setb(s, &s->qso_tx_exhausted, v); }
bool qso_state_autosel_recover(const qso_state_t *s)       { return s ? getb(s, &s->autosel_recover_mode) : false; }
void qso_state_set_autosel_recover(qso_state_t *s, bool v) { if (s) setb(s, &s->autosel_recover_mode, v); }
bool qso_state_autosel_qso_active(const qso_state_t *s)    { return s ? getb(s, &s->autosel_qso_active) : false; }
void qso_state_set_autosel_qso_active(qso_state_t *s, bool v) { if (s) setb(s, &s->autosel_qso_active, v); }

uint8_t qso_state_autosel_grid_tx_count(const qso_state_t *s) {
    if (!s) return 0;
    pthread_mutex_lock((pthread_mutex_t *)&s->mux);
    uint8_t v = s->autosel_grid_tx_count;
    pthread_mutex_unlock((pthread_mutex_t *)&s->mux);
    return v;
}
void qso_state_set_autosel_grid_tx_count(qso_state_t *s, uint8_t v) {
    if (!s) return;
    lock(s); s->autosel_grid_tx_count = v; unlock(s);
}
void qso_state_inc_autosel_grid_tx_count(qso_state_t *s) {
    if (!s) return;
    lock(s);
    if (s->autosel_grid_tx_count < 255) s->autosel_grid_tx_count++;
    unlock(s);
}

/* Pending to-me. */

bool qso_state_has_pending_to_me(const qso_state_t *s) { return s ? getb(s, &s->pending_to_me_valid) : false; }

void qso_state_set_pending_to_me(qso_state_t *s, const ftx_msg_meta_t *meta, bool odd) {
    if (!s || !meta) return;
    lock(s);
    s->pending_to_me_meta  = *meta;
    s->pending_to_me_odd   = odd;
    s->pending_to_me_valid = true;
    unlock(s);
}

void qso_state_get_pending_to_me(const qso_state_t *s, ftx_msg_meta_t *out, bool *out_odd) {
    if (!s) return;
    pthread_mutex_lock((pthread_mutex_t *)&s->mux);
    if (out)     *out     = s->pending_to_me_meta;
    if (out_odd) *out_odd = s->pending_to_me_odd;
    pthread_mutex_unlock((pthread_mutex_t *)&s->mux);
}

void qso_state_clear_pending_to_me(qso_state_t *s) {
    if (!s) return;
    lock(s);
    s->pending_to_me_valid = false;
    s->pending_to_me_odd   = false;
    memset(&s->pending_to_me_meta, 0, sizeof(s->pending_to_me_meta));
    unlock(s);
}

bool qso_state_has_pending_grid_to_me(const qso_state_t *s) { return s ? getb(s, &s->pending_grid_to_me_valid) : false; }

void qso_state_set_pending_grid_to_me(qso_state_t *s, const ftx_msg_meta_t *meta, bool odd) {
    if (!s || !meta) return;
    lock(s);
    s->pending_grid_to_me_meta  = *meta;
    s->pending_grid_to_me_odd   = odd;
    s->pending_grid_to_me_valid = true;
    unlock(s);
}

void qso_state_get_pending_grid_to_me(const qso_state_t *s, ftx_msg_meta_t *out, bool *out_odd) {
    if (!s) return;
    pthread_mutex_lock((pthread_mutex_t *)&s->mux);
    if (out)     *out     = s->pending_grid_to_me_meta;
    if (out_odd) *out_odd = s->pending_grid_to_me_odd;
    pthread_mutex_unlock((pthread_mutex_t *)&s->mux);
}

void qso_state_clear_pending_grid_to_me(qso_state_t *s) {
    if (!s) return;
    lock(s);
    s->pending_grid_to_me_valid = false;
    s->pending_grid_to_me_odd   = false;
    memset(&s->pending_grid_to_me_meta, 0, sizeof(s->pending_grid_to_me_meta));
    unlock(s);
}

void qso_state_clear_qso_related(qso_state_t *s) {
    if (!s) return;
    lock(s);
    s->qso_tx_exhausted         = false;
    s->autosel_recover_mode     = false;
    s->pending_to_me_valid      = false;
    s->active_call[0]           = '\0';
    s->autosel_qso_active       = false;
    s->autosel_grid_tx_count    = 0;
    s->pending_grid_to_me_valid = false;
    unlock(s);
}

bool qso_state_is_grid_exchange_msg(const char *msg) {
    if (!msg || msg[0] == '\0') return false;
    char t1[16] = {0};
    char t2[16] = {0};
    char t3[16] = {0};
    if (sscanf(msg, "%15s %15s %15s", t1, t2, t3) != 3) return false;
    return qth_grid_check(t3);
}

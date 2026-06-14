/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI - FT8 auto-select (unseen-CQ picker)
 *
 *  Copyright (c) 2026
 */

/*
 *  Tracks CQ callers observed during the current slot window, filters them
 *  by the system cycle blacklist and the user's persistent blacklist, and
 *  picks one for auto-QSO according to auto_sel_mode_t.
 *
 *  Backed by std::deque<UnseenEntry> (per-slot scratch), std::array<> for
 *  the short cycle blacklist, and std::vector<std::string> for the user
 *  blacklist. All accesses are mutex-protected so worker-thread
 *  autosel_add_candidate() and UI-thread autosel_pick() are safe.
 *
 *  C-compatible API so C callers (qso_state, dialog) can use it directly.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "audio_worker.h"   /* slot_info_t */
#include "qso.h"            /* ftx_msg_meta_t, FTxQsoProcessor, ftx_tx_msg_t */
#include "lvgl/lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AUTO_SEL_OFF         = 0,
    AUTO_SEL_FIRST       = 1,
    AUTO_SEL_FARTHEST    = 2,
    AUTO_SEL_HIGHEST_SNR = 3,
    AUTO_SEL_NEW_GRID    = 4,
} auto_sel_mode_t;

typedef struct {
    ftx_msg_meta_t meta;
    int            dist;
    int            snr;
} autosel_candidate_t;

void autosel_init(void);
void autosel_deinit(void);

/* Load / persist /mnt/autosel_blacklist.txt. */
void autosel_user_blacklist_load(void);

/* Worker thread: register a CQ caller observed in this slot. */
void autosel_add_candidate(const ftx_msg_meta_t *meta, int dist, int snr);

/* UI thread: pick a candidate per mode. Returns false if nothing suitable. */
bool autosel_pick(auto_sel_mode_t mode, autosel_candidate_t *out);

/* Drop all unseen candidates (end-of-slot hygiene). */
void autosel_clear_unseen(void);

/* System cycle blacklist. Calls normalise callsigns internally. */
void autosel_blacklist_add(const char *call);
void autosel_blacklist_mark_seen(const char *call);
void autosel_blacklist_advance_cycle(void);
void autosel_blacklist_clear_all(void);

/* Blacklist checks. user_blacklisted() reads only the persistent list;
 * blacklisted() reads either list. */
bool autosel_is_user_blacklisted(const char *call);
bool autosel_is_blacklisted(const char *call);

/* ---- Dialog extension points (called from dialog_ft8.c) -------------- */

void autosel_init_state(void);
void autosel_cleanup_state(void);

void autosel_rx_hook(const char *text, int snr,
                     float freq_hz, float time_sec,
                     ftx_msg_meta_t *meta,
                     const slot_info_t *info);

void autosel_slot_end_hook(const slot_info_t *info);

/** Cycle through AUTO_SEL_OFF → FIRST → FARTHEST → HIGHEST_SNR → NEW_GRID.
 *  OFF clears unseen candidates and the cycle blacklist. */
void autosel_cycle_mode(void);

/** Return current mode as int (for label rendering). */
int  autosel_get_mode(void);

/** Return human-readable mode name. */
const char *autosel_get_mode_text(void);

/** Pre-TX grid pileup preemption: if a pending grid caller is stashed
 *  and we are mid-QSO, swap tx_msg to work the grid caller.
 *  Does not defer the tick — odd/even parity is already aligned. */
void autosel_grid_swap_on_tick(const slot_info_t *info);

/** Post-TX housekeeping: track grid TX count, detect QSO exhaustion.
 *  Call from on_tick_cb after repeats-- and before msg[0]='\0'. */
void autosel_post_tx(void);

/** Called from add_rx_text when the QSO processor updates tx_msg.
 *  Tracks qso_active_call, cq_paused flag, clears exhaustion state. */
void autosel_on_tx_msg_updated(const ftx_msg_meta_t *meta, bool odd_slot);

/** Called from save_qso: resume CQ immediately if it was paused for
 *  this QSO. Needed because slot_end can't do it (has_current() is
 *  still true at save time). */
void autosel_on_qso_saved(void);

/** Called from mode_ft4_ft8_cb: reset autosel session state on
 *  FT8/FT4 protocol switch. */
void autosel_on_mode_switch(void);

/** Called from tx_cq_en_dis_cb when the user toggles TX CQ. */
void autosel_on_tx_cq_toggle(bool cq_enabling);

/** Called from on_table_press when the user manually starts a QSO. */
void autosel_on_manual_qso_start(const ftx_msg_meta_t *meta);

/* ---- Deferred dialog getters (implemented in dialog_ft8.c) ------------- */

FTxQsoProcessor *ft8_get_qso_processor(void);
ftx_tx_msg_t    *ft8_get_tx_msg(void);
bool            *ft8_get_tx_time_slot(void);
lv_obj_t        *ft8_get_finder(void);
lv_obj_t        *ft8_get_waterfall(void);
bool             ft8_is_tx_enabled(void);
bool             ft8_is_cq_enabled(void);
void             ft8_set_cq_enabled(bool on);
void             ft8_set_dial_freq(uint32_t freq);
void             ft8_finder_set_cursor_async(int16_t freq_hz);
void             ft8_set_dial_freq_async(uint32_t freq);
void             ft8_schedule_cq_tx(void);
void             ft8_schedule_cq_tx_async(void);
void             ft8_get_qth(double *lat, double *lon);

#ifdef __cplusplus
}
#endif

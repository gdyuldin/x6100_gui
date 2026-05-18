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

#include "qso.h"   /* ftx_msg_meta_t */

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

#ifdef __cplusplus
}
#endif

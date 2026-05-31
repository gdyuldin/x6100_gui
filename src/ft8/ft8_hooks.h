/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI - FT8 Hook Chain API
 *
 *  Copyright (c) 2026
 *
 *  Internal header: included by dialog_ft8.c and by future feature PRs
 *  that register hooks. Do NOT include from generic UI files — use
 *  dialog_ft8.h for the dialog pointer only.
 */

#pragma once

#include "ft8/audio_worker.h"   /* slot_info_t */
#include "ft8/qso.h"            /* FTxQsoProcessor, ftx_tx_msg_t, ftx_msg_meta_t */
#include "ft8/tx_worker.h"      /* ft8_tx_config_t, tx_abort_fn_t */
#include "lvgl/lvgl.h"          /* lv_obj_t */

/* ---- Hook chain limits ----------------------------------------------- */
#define FT8_MAX_HOOKS        4
#define FT8_BUTTON_MAX_PAGES  4
#define FT8_BUTTON_SLOTS      5

/* ======================================================================
 *  Hook Types
 * ====================================================================== */

/** Lifecycle hook: called on dialog open (init) and close (cleanup).
 *  Cleanup hooks are iterated in reverse registration order. */
typedef void (*ft8_lifecycle_fn_t)(void);

/** RX message hook: called after add_rx_text() for every decoded
 *  FT8/FT4 message. meta may be NULL if the decoding context did not
 *  produce a full meta (edge case — callers should guard). */
typedef void (*ft8_rx_msg_fn_t)(const char *text, int snr,
                                float freq_hz, float time_sec,
                                ftx_msg_meta_t *meta);

/** PSD frame hook: called after the waterfall data has been pushed.
 *  Runs on the worker thread — LVGL mutations must go through
 *  scheduler_put(). */
typedef void (*ft8_psd_fn_t)(const float *psd, uint16_t nfft,
                             float sec_since_slot_start,
                             const slot_info_t *info);

/** Slot-end hook: called after ftx_qso_processor_start_new_slot().
 *  The slot_info_t describes the slot that just ended. */
typedef void (*ft8_slot_end_fn_t)(const slot_info_t *info);

/** Pre-TX hook: called immediately before state transitions to
 *  TX_PROCESS within on_tick_cb. The caller may log, clear DNF, etc.
 *  This hook CANNOT affect control flow (no return value). */
typedef void (*ft8_pre_tx_fn_t)(const slot_info_t *info);

/* ======================================================================
 *  Registration API
 * ====================================================================== */

void ft8_register_init_hook(ft8_lifecycle_fn_t fn);
void ft8_register_cleanup_hook(ft8_lifecycle_fn_t fn);
void ft8_register_rx_msg_hook(ft8_rx_msg_fn_t fn);
void ft8_register_psd_hook(ft8_psd_fn_t fn);
void ft8_register_slot_end_hook(ft8_slot_end_fn_t fn);
void ft8_register_pre_tx_hook(ft8_pre_tx_fn_t fn);

/* ======================================================================
 *  Button Registration
 * ====================================================================== */

/** Button descriptor for external feature PRs. Each PR registers
 *  its button(s) through ft8_register_button() and the dialog builds
 *  a 4-page × 5-slot button grid from the registry.
 *
 *  - page: 0..3 (0=page1 … 3=page4)
 *  - slot: 0..4  (slot 0 is reserved for page-flip buttons;
 *                  feature PRs should use slots 1..4)
 *  - label / label_fn: mutually exclusive; label_fn wins when non-NULL
 *  - press: called on button press
 *  - subject: optional Subject pointer for auto-refresh (BTN_TEXT_FN style) */
struct button_item_t;  /* fwd — defined in buttons.h */

typedef struct {
    const char *label;
    const char *(*label_fn)(void);
    void (*press)(struct button_item_t *);
    int  *subject;
    int   page;
    int   slot;
} ft8_button_def_t;

void ft8_register_button(const ft8_button_def_t *btn);

/* ======================================================================
 *  AutoSel Module Interface (getters for cross-module access)
 *
 *  auto-sel's state machine needs read/write access to several
 *  dialog_ft8.c static variables (qso_processor, tx_msg, finder,
 *  tx_time_slot, etc.). These getters and setters expose a stable
 *  API so auto-sel can be implemented in a separate source file and
 *  linked in without touching dialog_ft8.c internals.
 * ====================================================================== */

FTxQsoProcessor *ft8_get_qso_processor(void);
ftx_tx_msg_t    *ft8_get_tx_msg(void);
bool            *ft8_get_tx_time_slot(void);
lv_obj_t        *ft8_get_finder(void);
lv_obj_t        *ft8_get_waterfall(void);
bool             ft8_is_tx_enabled(void);

/** Schedule a CQ TX using the current callsign/grid/modifier.
 *  Equivalent to the user pressing the TX CQ button. */
void             ft8_schedule_cq_tx(void);

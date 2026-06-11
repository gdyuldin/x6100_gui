/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI - FT8 auto DNF (slot-begin peak notch)
 *
 *  Copyright (c) 2026
 */

/*
 *  Auto DNF (Do-Not-Force) watches the waterfall PSD during the first 0.25..0.75s
 *  of each slot, detects a dominant interferer via time-dimension max-pool + a
 *  25th-percentile floor estimate, and if the peak stands out by more than
 *  min_delta_db, engages the radio's manual DNF notch at the peak center.
 *
 *  All state is owned by auto_dnf_ctx_t. create/destroy own the entry
 *  snapshot and the band/att/pre observers; restore_entry() is called on
 *  dialog close even if we crash during teardown.
 *
 *  Threading:
 *    - auto_dnf_on_psd() runs on the worker thread (no LVGL calls).
 *    - UI apply / clear actually happen on the scheduler (UI thread) via
 *      scheduler_put() trampolines owned by this module.
 *    - auto_dnf_snapshot_entry / restore_entry / build_overlay / destroy run
 *      on the UI thread from the dialog lifecycle.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "lvgl/lvgl.h"

#include "audio_worker.h"   /* slot_info_t */
#include "params.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct auto_dnf_ctx_s auto_dnf_ctx_t;

auto_dnf_ctx_t *auto_dnf_create(const ft8_tuning_t *tuning);
void            auto_dnf_destroy(auto_dnf_ctx_t *ctx);

/* Overlay UI (blue peak lines + delta_db label over the waterfall,
 * below the decode table in z-order). */
void auto_dnf_build_overlay(auto_dnf_ctx_t *ctx,
                            lv_obj_t *parent,
                            lv_coord_t overlay_x,
                            lv_coord_t overlay_y,
                            lv_coord_t overlay_h,
                            lv_coord_t overlay_w_px,
                            int32_t filter_low_hz,
                            int32_t filter_high_hz);

/* User opened the FT8 dialog - remember current DNF settings so we can
 * restore them on close. */
void auto_dnf_snapshot_entry(auto_dnf_ctx_t *ctx);

/* Restore the DNF subjects to the entry snapshot. Safe to call multiple
 * times; second+ calls are no-ops. */
void auto_dnf_restore_entry(auto_dnf_ctx_t *ctx);

/* Worker-thread hook per waterfall PSD frame.
 * frame_ts is the wall-clock timestamp of the audio this PSD represents
 * (NOT the current wall clock), used for slot-aligned scan/clear timing. */
void auto_dnf_on_psd(auto_dnf_ctx_t *ctx,
                     const float *psd, uint16_t nfft,
                     int32_t sample_rate,
                     int32_t filter_low_hz,
                     int32_t filter_high_hz,
                     struct timespec frame_ts,
                     bool is_our_tx_slot);

/* TX just started - clear any active notch so it does not affect our TX. */
void auto_dnf_clear_for_tx(auto_dnf_ctx_t *ctx);

/* dialog_ft8.c deferred API (PR #242). */
void ft8_get_filter_range(int *low_hz, int *high_hz);
bool ft8_is_our_tx_slot(const slot_info_t *info);

/* Module extension-point entry points (direct call from dialog_ft8.c). */
void ft8_autodnf_on_init(lv_obj_t *overlay_parent);
void ft8_autodnf_set_waterfall(lv_obj_t *waterfall_obj);
void ft8_autodnf_on_cleanup(void);
void ft8_autodnf_on_psd(const float *psd, uint16_t nfft,
                        float sec_since_slot_start, const slot_info_t *info);
void ft8_autodnf_on_pre_tx(const slot_info_t *info);
void ft8_autodnf_restore_entry(void);

#ifdef __cplusplus
}
#endif

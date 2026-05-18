/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI - FT8 auto DNF (slot-begin peak notch)
 *
 *  Copyright (c) 2026
 */

#include "auto_dnf.h"
#include "ft8_log.h"

#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <ft8lib/constants.h>

#include "../cfg/cfg.h"
#include "../cfg/subjects.h"
#include "../scheduler.h"

#define FLOOR_SAMPLES_MAX   4096
#define FLOOR_PERCENTILE    25          /* 25th percentile of band max-pool */
#define HALF_WIDTH_HZ       35          /* 70 Hz notch width */

/* Single-instance module: the FT8 dialog can only have one auto_dnf active
 * at a time, and scheduler_put trampolines need to address "the" instance
 * unambiguously. */
static auto_dnf_ctx_t *g_self = NULL;

typedef struct {
    int32_t dnf;
    int32_t center;
    int32_t width;
    bool    valid;
} dnf_state_t;

struct auto_dnf_ctx_s {
    ft8_tuning_t tuning;

    /* Geometry (pixel extents of the waterfall overlay area). */
    lv_obj_t  *line_l;
    lv_obj_t  *line_r;
    lv_obj_t  *label;
    lv_coord_t overlay_x;
    lv_coord_t overlay_y;
    lv_coord_t overlay_h;
    lv_coord_t overlay_w_px;
    int32_t    filter_low_hz;
    int32_t    filter_high_hz;

    /* Entry snapshot - restored on dialog close. */
    dnf_state_t entry;

    /* Override state. */
    bool        override_active;
    uint64_t    active_slot_start;
    uint64_t    clear_scheduled_slot_start;

    /* Per-slot scan state. */
    uint64_t    slot_seen;                 /* last slot we reset max-pool for */
    bool        applied_this_slot;
    float      *maxpool;                   /* [nfft] */
    uint16_t    maxpool_nfft;

    /* Floor history (low-percentile rolling mean). */
    double      floor_sum;
    uint32_t    floor_count;
    Observer   *obs_band;
    Observer   *obs_att;
    Observer   *obs_pre;

    /* Scratch for percentile sort, sized once in create(). */
    float       floor_samples[FLOOR_SAMPLES_MAX];
};

/* ---------- overlay ---------------------------------------------------- */

static int32_t clip_i32(int32_t v, int32_t lo, int32_t hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static void overlay_hide(auto_dnf_ctx_t *ctx) {
    if (ctx->line_l) lv_obj_add_flag(ctx->line_l, LV_OBJ_FLAG_HIDDEN);
    if (ctx->line_r) lv_obj_add_flag(ctx->line_r, LV_OBJ_FLAG_HIDDEN);
    if (ctx->label)  lv_obj_add_flag(ctx->label,  LV_OBJ_FLAG_HIDDEN);
}

static void overlay_show(auto_dnf_ctx_t *ctx,
                         uint16_t center_hz,
                         uint16_t half_width_hz,
                         float delta_db,
                         bool dnf_applied) {
    if (!ctx->line_l || !ctx->line_r || !ctx->label) return;

    int32_t span = ctx->filter_high_hz - ctx->filter_low_hz;
    if (span <= 0) return;

    int32_t left_hz  = (int32_t)center_hz - (int32_t)half_width_hz;
    int32_t right_hz = (int32_t)center_hz + (int32_t)half_width_hz;
    left_hz  = clip_i32(left_hz,  ctx->filter_low_hz, ctx->filter_high_hz);
    right_hz = clip_i32(right_hz, ctx->filter_low_hz, ctx->filter_high_hz);

    int32_t x_l = (int32_t)((int64_t)(left_hz  - ctx->filter_low_hz) * ctx->overlay_w_px / span);
    int32_t x_r = (int32_t)((int64_t)(right_hz - ctx->filter_low_hz) * ctx->overlay_w_px / span);
    x_l = clip_i32(x_l, 0, ctx->overlay_w_px - 1);
    x_r = clip_i32(x_r, 0, ctx->overlay_w_px - 1);

    lv_obj_set_pos(ctx->line_l, (lv_coord_t)(ctx->overlay_x + x_l), ctx->overlay_y);
    lv_obj_set_pos(ctx->line_r, (lv_coord_t)(ctx->overlay_x + x_r), ctx->overlay_y);

    char buf[32];
    snprintf(buf, sizeof(buf), "+ %.0f dB", (double)delta_db);
    lv_label_set_text(ctx->label, buf);
    lv_obj_set_pos(ctx->label,
                   (lv_coord_t)(ctx->overlay_x + (x_l < x_r ? x_l : x_r)),
                   ctx->overlay_y);
    lv_obj_set_style_text_color(
        ctx->label,
        dnf_applied ? lv_color_hex(0xFF0000) : lv_color_hex(0x0066FF),
        LV_PART_MAIN);

    lv_obj_clear_flag(ctx->line_l, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(ctx->line_r, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(ctx->label,  LV_OBJ_FLAG_HIDDEN);
}

/* ---------- scheduler trampolines (UI thread) ------------------------- */

typedef struct {
    uint16_t center_hz;
    uint16_t half_width_hz;
    float    delta_db;
    uint64_t slot_start;
    bool     apply_dnf;
} apply_msg_t;

typedef struct {
    uint64_t slot_start;
} clear_msg_t;

static void do_restore_now(auto_dnf_ctx_t *ctx);

static void apply_cb(void *arg) {
    auto_dnf_ctx_t *ctx = g_self;
    if (!ctx) return;
    const apply_msg_t *m = (const apply_msg_t *)arg;

    /* Protect against stale scheduler deliveries crossing slot boundaries. */
    struct timespec ts_now;
    clock_gettime(CLOCK_REALTIME, &ts_now);
    int proto = subject_get_int(cfg.ft8_protocol.val);
    uint64_t slot_ns = (proto == FTX_PROTOCOL_FT4) ? 7500000000ULL : 15000000000ULL;
    uint64_t epoch_ns = (uint64_t)ts_now.tv_sec * 1000000000ULL + (uint64_t)ts_now.tv_nsec;
    uint64_t cur_slot_start = epoch_ns / slot_ns;
    if (m->slot_start != cur_slot_start) return;

    /* Always update overlay so the user can see the detection even when we
     * don't apply the notch. */
    overlay_show(ctx, m->center_hz, m->half_width_hz, m->delta_db, false);

    if (!subject_get_int(cfg.ft8_auto_dnf.val)) return;
    if (subject_get_int(cfg.dnf_auto.val))      return;
    if (!m->apply_dnf)                          return;

    if (!ctx->override_active && !ctx->entry.valid) {
        /* entry snapshot not taken (create() without snapshot) - fall back
         * to current state so we can still restore later. */
        ctx->entry.dnf    = subject_get_int(cfg.dnf.val);
        ctx->entry.center = subject_get_int(cfg.dnf_center.val);
        ctx->entry.width  = subject_get_int(cfg.dnf_width.val);
        ctx->entry.valid  = true;
    }
    ctx->override_active = true;
    ctx->active_slot_start = m->slot_start;

    subject_set_int(cfg.dnf.val,        true);
    subject_set_int(cfg.dnf_center.val, m->center_hz);
    subject_set_int(cfg.dnf_width.val,  m->half_width_hz);

    /* Log only actual applies. */
    time_t slot_start_wall = (time_t)(ts_now.tv_sec - (int)(ts_now.tv_sec % (slot_ns / 1000000000ULL)));
    ft8_log_dnf(slot_start_wall, m->center_hz, m->delta_db);

    overlay_show(ctx, m->center_hz, m->half_width_hz, m->delta_db, true);
}

static void clear_cb(void *arg) {
    auto_dnf_ctx_t *ctx = g_self;
    if (!ctx || !ctx->override_active) return;
    const clear_msg_t *m = (const clear_msg_t *)arg;
    if (m && (m->slot_start != 0) && (ctx->active_slot_start != m->slot_start)) {
        /* Old-slot clear ran after new-slot apply - ignore. */
        return;
    }
    do_restore_now(ctx);
}

static void do_restore_now(auto_dnf_ctx_t *ctx) {
    if (ctx->override_active && ctx->entry.valid) {
        int cur_dnf    = subject_get_int(cfg.dnf.val);
        int cur_center = subject_get_int(cfg.dnf_center.val);
        int cur_width  = subject_get_int(cfg.dnf_width.val);
        if (cur_dnf    != ctx->entry.dnf)    subject_set_int(cfg.dnf.val,        ctx->entry.dnf);
        if (cur_center != ctx->entry.center) subject_set_int(cfg.dnf_center.val, ctx->entry.center);
        if (cur_width  != ctx->entry.width)  subject_set_int(cfg.dnf_width.val,  ctx->entry.width);
    }
    ctx->override_active = false;
    ctx->active_slot_start = 0;
    overlay_hide(ctx);
}

/* ---------- floor observer callback ----------------------------------- */

static void floor_history_clear_cb(Subject *subj, void *user) {
    (void)subj;
    auto_dnf_ctx_t *ctx = (auto_dnf_ctx_t *)user;
    if (!ctx) return;
    ctx->floor_sum   = 0.0;
    ctx->floor_count = 0;
}

/* ---------- peak detector --------------------------------------------- */

static int float_asc(const void *a, const void *b) {
    float x = *(const float *)a;
    float y = *(const float *)b;
    return (x < y) ? -1 : (x > y);
}

static float local_weighted(const float *maxpool, uint32_t i,
                            uint32_t low_bin, uint32_t high_bin) {
    /* Center-weighted kernel [1,2,3,2,1]/9 over bins (i-2..i+2), skipping
     * out-of-range bins. */
    static const float kw[5] = { 1.0f/9.0f, 2.0f/9.0f, 3.0f/9.0f, 2.0f/9.0f, 1.0f/9.0f };
    float sum = 0.0f;
    float wsum = 0.0f;
    for (int k = -2; k <= 2; k++) {
        int32_t bin = (int32_t)i + k;
        if (bin < (int32_t)low_bin || bin >= (int32_t)high_bin) continue;
        float v = maxpool[bin];
        if (!isfinite(v) || v < -300.0f || v > 300.0f) continue;
        sum  += v * kw[k + 2];
        wsum += kw[k + 2];
    }
    return (wsum > 0.0f) ? (sum / wsum) : -1e9f;
}

static void run_peak_detection(auto_dnf_ctx_t *ctx,
                               uint32_t low_bin, uint32_t high_bin,
                               uint16_t nfft, int32_t sample_rate,
                               uint64_t slot_start) {
    /* Build percentile sample from maxpool; skip sentinel/bogus values. */
    int cnt = 0;
    for (uint32_t i = low_bin + 2; (i + 2) < high_bin && cnt < FLOOR_SAMPLES_MAX; i++) {
        float v = ctx->maxpool[i];
        if (!isfinite(v) || v < -300.0f || v > 300.0f) continue;
        ctx->floor_samples[cnt++] = v;
    }

    float floor_db = (ctx->floor_count > 0)
                     ? (float)(ctx->floor_sum / (double)ctx->floor_count)
                     : -1.0e9f;
    if (cnt > 0) {
        qsort(ctx->floor_samples, (size_t)cnt, sizeof(float), float_asc);
        int idx = (cnt * FLOOR_PERCENTILE) / 100;
        if (idx >= cnt) idx = cnt - 1;
        float p25 = ctx->floor_samples[idx];
        if (isfinite(p25) && p25 >= -300.0f && p25 <= 300.0f) {
            ctx->floor_sum += (double)p25;
            ctx->floor_count++;
            floor_db = (float)(ctx->floor_sum / (double)ctx->floor_count);
        }
    }

    float peak_db = -1.0e9f;
    int   peak_bin = -1;
    for (uint32_t i = low_bin + 2; (i + 2) < high_bin; i++) {
        float v = local_weighted(ctx->maxpool, i, low_bin, high_bin);
        if (!isfinite(v) || v < -300.0f || v > 300.0f) continue;
        if (v > peak_db) { peak_db = v; peak_bin = (int)i; }
    }

    if (peak_bin < 0) return;

    float delta_db = (floor_db > -300.0f && peak_db > -300.0f) ? (peak_db - floor_db) : 0.0f;
    if (!isfinite(delta_db) || delta_db < 0.0f) delta_db = 0.0f;
    if (delta_db > 200.0f) delta_db = 200.0f;

    const float hz_per_bin = (float)sample_rate / (float)nfft;
    float   bin_hz = ((float)(peak_bin - (int)(nfft / 2))) * hz_per_bin;
    /* +7Hz: Costas sync tone offset to actual transmit frequency. */
    uint16_t center_hz = (uint16_t)lroundf(fabsf(bin_hz)) + 7;
    bool apply_dnf = (delta_db >= ctx->tuning.min_delta_db);

    apply_msg_t m = {
        .center_hz     = center_hz,
        .half_width_hz = (uint16_t)HALF_WIDTH_HZ,
        .delta_db      = delta_db,
        .slot_start    = slot_start,
        .apply_dnf     = apply_dnf,
    };
    scheduler_put(apply_cb, &m, sizeof(m));
    if (apply_dnf) ctx->applied_this_slot = true;
}

/* ---------- public: worker PSD hook ----------------------------------- */

void auto_dnf_on_psd(auto_dnf_ctx_t *ctx,
                     const float *psd, uint16_t nfft,
                     int32_t sample_rate,
                     int32_t filter_low_hz,
                     int32_t filter_high_hz,
                     struct timespec frame_ts,
                     bool is_our_tx_slot) {
    if (!ctx || !psd || nfft == 0) return;
    if (!subject_get_int(cfg.ft8_auto_dnf.val)) return;

    /* Lazy-size the max-pool buffer when nfft first becomes known. */
    if (ctx->maxpool_nfft != nfft) {
        free(ctx->maxpool);
        ctx->maxpool = (float *)malloc((size_t)nfft * sizeof(float));
        ctx->maxpool_nfft = nfft;
        if (!ctx->maxpool) { ctx->maxpool_nfft = 0; return; }
        for (uint32_t i = 0; i < nfft; i++) ctx->maxpool[i] = -1e9f;
    }

    int32_t span = filter_high_hz - filter_low_hz;
    if (span <= 0) return;

    uint32_t low_bin  = nfft / 2 + (uint32_t)((int32_t)nfft * filter_low_hz  / sample_rate);
    uint32_t high_bin = nfft / 2 + (uint32_t)((int32_t)nfft * filter_high_hz / sample_rate);
    if (high_bin > nfft) high_bin = nfft;
    if (high_bin <= low_bin + 8) return;

    /* Slot id from PSD frame timestamp (not current wall clock).
     * This aligns DNF scan windows with the actual audio, removing
     * the FFT pipeline latency mismatch.
     *
     * The clear path below uses wall clock separately — the notch must
     * be removed BEFORE the real slot boundary, not delayed by the
     * pipeline latency that the PSD timestamp carries. */
    int proto = subject_get_int(cfg.ft8_protocol.val);
    uint64_t slot_ns = (proto == FTX_PROTOCOL_FT4) ? 7500000000ULL : 15000000000ULL;
    uint64_t epoch_ns = (uint64_t)frame_ts.tv_sec * 1000000000ULL + (uint64_t)frame_ts.tv_nsec;
    uint64_t slot_start = epoch_ns / slot_ns;
    float sec_since_slot_start = (float)(epoch_ns - slot_start * slot_ns) / 1.0e9f;

    float slot_period = (proto == FTX_PROTOCOL_FT4) ? FT4_SLOT_TIME : FT8_SLOT_TIME;

    if (slot_start != ctx->slot_seen) {
        ctx->slot_seen                    = slot_start;
        ctx->applied_this_slot            = false;
        ctx->clear_scheduled_slot_start   = 0;
        for (uint32_t i = low_bin; i < high_bin && i < nfft; i++) {
            ctx->maxpool[i] = -1e9f;
        }
    }

    /* Pre-expire the active notch near slot end so the next scan sees
     * un-notched PSD. Uses wall clock (not PSD frame_ts) so the clear
     * fires before the real slot boundary, not delayed by pipeline latency. */
    {
        struct timespec ts_wall;
        clock_gettime(CLOCK_REALTIME, &ts_wall);
        uint64_t wall_ns = (uint64_t)ts_wall.tv_sec * 1000000000ULL + (uint64_t)ts_wall.tv_nsec;
        uint64_t wall_slot_start = wall_ns / slot_ns;
        float wall_sec = (float)(wall_ns - wall_slot_start * slot_ns) / 1.0e9f;

        if (ctx->override_active && ctx->active_slot_start != 0 &&
            ctx->clear_scheduled_slot_start != wall_slot_start &&
            wall_sec > (slot_period - ctx->tuning.clear_time_sec)) {
            clear_msg_t m = { .slot_start = ctx->active_slot_start };
            scheduler_put(clear_cb, &m, sizeof(m));
            ctx->clear_scheduled_slot_start = wall_slot_start;
        }
    }

    if (is_our_tx_slot || ctx->applied_this_slot) return;

    if (sec_since_slot_start >= ctx->tuning.scan_start_sec &&
        sec_since_slot_start <  ctx->tuning.scan_end_sec) {
        /* Accumulate max-pool across the scan window. */
        for (uint32_t i = low_bin + 2; (i + 2) < high_bin; i++) {
            if (psd[i] > ctx->maxpool[i]) ctx->maxpool[i] = psd[i];
        }
        return;
    }

    if (sec_since_slot_start >= ctx->tuning.scan_end_sec) {
        run_peak_detection(ctx, low_bin, high_bin, nfft, sample_rate, slot_start);
    }
}

void auto_dnf_clear_for_tx(auto_dnf_ctx_t *ctx) {
    if (!ctx || !ctx->override_active) return;
    clear_msg_t m = { .slot_start = ctx->active_slot_start };
    scheduler_put(clear_cb, &m, sizeof(m));
}

/* ---------- entry snapshot / restore ---------------------------------- */

void auto_dnf_snapshot_entry(auto_dnf_ctx_t *ctx) {
    if (!ctx) return;
    ctx->entry.dnf    = subject_get_int(cfg.dnf.val);
    ctx->entry.center = subject_get_int(cfg.dnf_center.val);
    ctx->entry.width  = subject_get_int(cfg.dnf_width.val);
    ctx->entry.valid  = true;
}

void auto_dnf_restore_entry(auto_dnf_ctx_t *ctx) {
    if (!ctx || !ctx->entry.valid) return;

    int cur_dnf    = subject_get_int(cfg.dnf.val);
    int cur_center = subject_get_int(cfg.dnf_center.val);
    int cur_width  = subject_get_int(cfg.dnf_width.val);
    if (cur_dnf    != ctx->entry.dnf)    subject_set_int(cfg.dnf.val,        ctx->entry.dnf);
    if (cur_center != ctx->entry.center) subject_set_int(cfg.dnf_center.val, ctx->entry.center);
    if (cur_width  != ctx->entry.width)  subject_set_int(cfg.dnf_width.val,  ctx->entry.width);

    ctx->override_active   = false;
    ctx->active_slot_start = 0;
    overlay_hide(ctx);
}

/* ---------- overlay build --------------------------------------------- */

void auto_dnf_build_overlay(auto_dnf_ctx_t *ctx,
                            lv_obj_t *parent,
                            lv_coord_t overlay_x,
                            lv_coord_t overlay_y,
                            lv_coord_t overlay_h,
                            lv_coord_t overlay_w_px,
                            int32_t filter_low_hz,
                            int32_t filter_high_hz) {
    if (!ctx) return;
    ctx->overlay_x      = overlay_x;
    ctx->overlay_y      = overlay_y;
    ctx->overlay_h      = overlay_h;
    ctx->overlay_w_px   = overlay_w_px;
    ctx->filter_low_hz  = filter_low_hz;
    ctx->filter_high_hz = filter_high_hz;

    ctx->line_l = lv_obj_create(parent);
    lv_obj_clear_flag(ctx->line_l, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(ctx->line_l, 2, overlay_h);
    lv_obj_set_style_border_width(ctx->line_l, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(ctx->line_l, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(ctx->line_l, lv_color_hex(0x0066FF), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ctx->line_l, LV_OPA_80, LV_PART_MAIN);
    lv_obj_add_flag(ctx->line_l, LV_OBJ_FLAG_HIDDEN);

    ctx->line_r = lv_obj_create(parent);
    lv_obj_clear_flag(ctx->line_r, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(ctx->line_r, 2, overlay_h);
    lv_obj_set_style_border_width(ctx->line_r, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(ctx->line_r, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(ctx->line_r, lv_color_hex(0x0066FF), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ctx->line_r, LV_OPA_80, LV_PART_MAIN);
    lv_obj_add_flag(ctx->line_r, LV_OBJ_FLAG_HIDDEN);

    ctx->label = lv_label_create(parent);
    lv_obj_set_style_text_color(ctx->label, lv_color_hex(0x0066FF), LV_PART_MAIN);
    lv_obj_set_style_text_opa(ctx->label,   LV_OPA_COVER,           LV_PART_MAIN);
    lv_label_set_text(ctx->label, "");
    lv_obj_add_flag(ctx->label, LV_OBJ_FLAG_HIDDEN);
}

/* ---------- create / destroy ------------------------------------------ */

auto_dnf_ctx_t *auto_dnf_create(const ft8_tuning_t *tuning) {
    auto_dnf_ctx_t *ctx = (auto_dnf_ctx_t *)calloc(1, sizeof(*ctx));
    if (!ctx) return NULL;
    if (tuning) {
        ctx->tuning = *tuning;
    } else {
        ctx->tuning.scan_start_sec = FT8_AUTO_DNF_SCAN_START_SEC_DEFAULT;
        ctx->tuning.scan_end_sec   = FT8_AUTO_DNF_SCAN_END_SEC_DEFAULT;
        ctx->tuning.clear_time_sec = FT8_AUTO_DNF_CLEAR_TIME_SEC_DEFAULT;
        ctx->tuning.min_delta_db   = FT8_AUTO_DNF_MIN_DELTA_DB_DEFAULT;
    }

    ctx->obs_band = subject_add_observer(cfg.band_id.val,  floor_history_clear_cb, ctx);
    ctx->obs_att  = subject_add_observer(cfg_cur.att,      floor_history_clear_cb, ctx);
    ctx->obs_pre  = subject_add_observer(cfg_cur.pre,      floor_history_clear_cb, ctx);

    g_self = ctx;
    return ctx;
}

void auto_dnf_destroy(auto_dnf_ctx_t *ctx) {
    if (!ctx) return;

    /* Deleting observers in reverse order of creation. */
    if (ctx->obs_pre)  { observer_del(ctx->obs_pre);  ctx->obs_pre  = NULL; }
    if (ctx->obs_att)  { observer_del(ctx->obs_att);  ctx->obs_att  = NULL; }
    if (ctx->obs_band) { observer_del(ctx->obs_band); ctx->obs_band = NULL; }

    /* Always restore in case destroy is called without an explicit
     * restore_entry() call. */
    auto_dnf_restore_entry(ctx);

    if (ctx->maxpool) { free(ctx->maxpool); ctx->maxpool = NULL; }

    /* LVGL overlay objects are parented to the dialog and will be deleted
     * with it. Just forget the pointers. */
    ctx->line_l = NULL;
    ctx->line_r = NULL;
    ctx->label  = NULL;

    if (g_self == ctx) g_self = NULL;
    free(ctx);
}

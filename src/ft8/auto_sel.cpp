/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI - FT8 auto-select (unseen-CQ picker + state machine)
 *
 *  Copyright (c) 2026
 */

#include "auto_sel.h"

/* subjects.h and cfg.h already gate their C++/C contents internally; do
 * NOT wrap them in extern "C" or their own STL includes get C linkage. */
#include "../cfg/subjects.h"
#include "../cfg/cfg.h"
#include "../params/params.h"

#include <ft8lib/constants.h>

extern "C" {
#include "../qth/qth.h"
#include "../msg.h"
#include "../qso_log.h"
}

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace {

constexpr size_t   MAX_UNSEEN                   = 100;
constexpr size_t   CYCLE_BL_SIZE                = 8;
constexpr uint8_t  CYCLE_BL_CLEAR_MISS          = 2;
constexpr size_t   USER_BL_CALL_MAX_LEN         = 15;
constexpr const char *USER_BL_PATH              = "/mnt/autosel_blacklist.txt";

struct UnseenEntry {
    ftx_msg_meta_t meta;
    int            dist;
    int            snr;
};

struct CycleEntry {
    char    call[16];
    uint8_t miss_cycles;
    bool    seen_this_cycle;
};

std::mutex                                  g_mutex;
std::deque<UnseenEntry>                     g_unseen;
std::array<CycleEntry, CYCLE_BL_SIZE>       g_cycle_bl{};
std::vector<std::string>                    g_user_bl;

/* ---- AutoSel state machine ------------------------------------------- */

auto_sel_mode_t  s_mode                 = AUTO_SEL_OFF;
char             s_qso_active_call[16]  = {0};
bool             s_cq_paused_for_qso    = false;
bool             s_recover_mode         = false;
bool             s_qso_active           = false;
uint8_t          s_grid_tx_count        = 0;
bool             s_qso_tx_exhausted     = false;
bool             s_pending_to_me_valid  = false;
bool             s_pending_to_me_odd    = false;
ftx_msg_meta_t   s_pending_to_me_meta;
bool             s_pending_grid_valid   = false;
bool             s_pending_grid_odd     = false;
ftx_msg_meta_t   s_pending_grid_meta;
bool             s_local_qth_valid      = false;

/* ---- helpers (hold g_mutex while calling these) ----------------------- */

void trim_in_place(std::string &s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
        s.erase(s.begin());
    }
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
        s.pop_back();
    }
}

void normalize_call(const char *in, std::string &out) {
    out.clear();
    if (!in) return;
    out.assign(in);

    if (out.size() >= 3 &&
        static_cast<unsigned char>(out[0]) == 0xEF &&
        static_cast<unsigned char>(out[1]) == 0xBB &&
        static_cast<unsigned char>(out[2]) == 0xBF) {
        out.erase(0, 3);
    }
    trim_in_place(out);

    for (auto &c : out) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    if (out.size() > USER_BL_CALL_MAX_LEN) out.resize(USER_BL_CALL_MAX_LEN);
}

bool user_blacklisted_norm(const std::string &norm) {
    if (norm.empty()) return false;
    for (const auto &s : g_user_bl) {
        if (s == norm) return true;
    }
    return false;
}

bool cycle_blacklisted_norm(const std::string &norm) {
    if (norm.empty()) return false;
    for (const auto &e : g_cycle_bl) {
        if (e.call[0] != '\0' && (norm == e.call)) return true;
    }
    return false;
}

bool any_blacklisted_norm(const std::string &norm) {
    return user_blacklisted_norm(norm) || cycle_blacklisted_norm(norm);
}

void save_user_bl_locked(bool use_crlf) {
    const char *eol      = use_crlf ? "\r\n" : "\n";
    std::string tmp_path = std::string(USER_BL_PATH) + ".tmp";
    FILE *fp = std::fopen(tmp_path.c_str(), "wb");
    if (!fp) return;
    for (const auto &s : g_user_bl) {
        if (s.empty()) continue;
        std::fputs(s.c_str(), fp);
        std::fputs(eol, fp);
    }
    std::fflush(fp);
    std::fclose(fp);
    if (std::rename(tmp_path.c_str(), USER_BL_PATH) != 0) {
        std::remove(tmp_path.c_str());
    }
}

bool is_grid_exchange_msg(const char *msg) {
    if (!msg || msg[0] == '\0') return false;
    char t1[16] = {0}, t2[16] = {0}, t3[16] = {0};
    if (sscanf(msg, "%15s %15s %15s", t1, t2, t3) != 3) return false;
    return qth_grid_check(t3);
}

const char *mode_text(auto_sel_mode_t m) {
    switch (m) {
        case AUTO_SEL_OFF:         return "Off";
        case AUTO_SEL_FIRST:       return "First";
        case AUTO_SEL_FARTHEST:    return "Farthest";
        case AUTO_SEL_HIGHEST_SNR: return "Best SNR";
        case AUTO_SEL_NEW_GRID:    return "New Grid";
        default:                   return "?";
    }
}

void reset_qso_state(void) {
    s_recover_mode        = false;
    s_pending_to_me_valid = false;
    s_pending_grid_valid  = false;
    s_qso_active          = false;
    s_grid_tx_count       = 0;
    s_qso_active_call[0]  = '\0';
    s_qso_tx_exhausted    = false;
}

void start_qso_from_meta(ftx_msg_meta_t *meta, bool target_odd_slot) {
    FTxQsoProcessor *qso = ft8_get_qso_processor();
    ftx_tx_msg_t    *txm = ft8_get_tx_msg();
    if (!qso || !txm || !meta) return;

    ftx_qso_processor_start_qso(qso, meta, txm);
    if (txm->msg[0] == '\0') return;

    bool *tx_slot = ft8_get_tx_time_slot();
    *tx_slot = target_odd_slot;

    ft8_finder_set_cursor_async((int16_t)meta->freq_hz);
    if (!subject_get_int(cfg.ft8_hold_freq.val)) {
        ft8_set_dial_freq_async((uint32_t)meta->freq_hz);
    }
    strncpy(s_qso_active_call, meta->call_de, sizeof(s_qso_active_call) - 1);
    s_qso_active_call[sizeof(s_qso_active_call) - 1] = '\0';

    msg_schedule_text_fmt("Next TX: %s", txm->msg);
}

void pause_cq_for_qso(void) {
    if (ft8_is_cq_enabled()) {
        s_cq_paused_for_qso = true;
        ft8_set_cq_enabled(false);
    }
}

void resume_cq_if_qso_gone(void) {
    FTxQsoProcessor *qso = ft8_get_qso_processor();
    ftx_tx_msg_t    *txm = ft8_get_tx_msg();
    bool qso_gone = (!qso || !ftx_qso_processor_has_current(qso));

    if (s_cq_paused_for_qso && qso_gone && (!txm || txm->msg[0] == '\0')) {
        ft8_set_cq_enabled(true);
        s_cq_paused_for_qso = false;
        ft8_schedule_cq_tx();
    }
}

int compute_dist(const ftx_msg_meta_t *meta) {
    if (!meta || meta->grid[0] == '\0' || !s_local_qth_valid) return 0;
    if (!qth_grid_check(meta->grid)) return 0;
    double qth_lat = 0.0, qth_lon = 0.0;
    ft8_get_qth(&qth_lat, &qth_lon);
    double grid_lat = 0.0, grid_lon = 0.0;
    qth_str_to_pos(meta->grid, &grid_lat, &grid_lon);
    return (int)qth_pos_dist(grid_lat, grid_lon, qth_lat, qth_lon);
}

} /* namespace */

/* -------------------- candidate queue C API --------------------------- */

extern "C" void autosel_init(void) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_unseen.clear();
    for (auto &e : g_cycle_bl) {
        e.call[0]          = '\0';
        e.miss_cycles      = 0;
        e.seen_this_cycle  = false;
    }
    g_user_bl.clear();
}

extern "C" void autosel_deinit(void) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_unseen.clear();
    for (auto &e : g_cycle_bl) {
        e.call[0]          = '\0';
        e.miss_cycles      = 0;
        e.seen_this_cycle  = false;
    }
    g_user_bl.clear();
}

extern "C" void autosel_user_blacklist_load(void) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_user_bl.clear();

    bool dirty    = false;
    bool use_crlf = false;

    FILE *fp = std::fopen(USER_BL_PATH, "rb");
    if (!fp) {
        if (errno == ENOENT) {
            FILE *nfp = std::fopen(USER_BL_PATH, "wb");
            if (nfp) std::fclose(nfp);
        }
        return;
    }

    char line[128];
    while (std::fgets(line, sizeof(line), fp)) {
        if (std::strchr(line, '\r')) use_crlf = true;

        std::string orig(line);
        trim_in_place(orig);

        std::string call;
        normalize_call(line, call);
        if (call.empty())            continue;
        if (call[0] == '#' || call[0] == ';') continue;

        if (!(orig.empty() || orig[0] == '#' || orig[0] == ';')) {
            if (orig != call) dirty = true;
        }

        bool exists = false;
        for (const auto &s : g_user_bl) {
            if (s == call) { exists = true; break; }
        }
        if (exists) {
            dirty = true;
            continue;
        }
        g_user_bl.push_back(call);
    }
    std::fclose(fp);

    if (dirty) save_user_bl_locked(use_crlf);
}

extern "C" void autosel_add_candidate(const ftx_msg_meta_t *meta, int dist, int snr) {
    if (!meta) return;

    qso_log_search_worked_t worked = qso_log_search_worked(
        meta->call_de,
        subject_get_int(cfg.ft8_protocol.val) == FTX_PROTOCOL_FT8 ? MODE_FT8 : MODE_FT4,
        qso_log_freq_to_band(subject_get_int(cfg_cur.fg_freq)));
    if (worked != SEARCH_WORKED_NO) return;

    std::string norm;
    normalize_call(meta->call_de, norm);

    std::lock_guard<std::mutex> lock(g_mutex);
    if (norm.empty() || any_blacklisted_norm(norm)) return;
    for (const auto &u : g_unseen) {
        if (std::strcmp(u.meta.call_de, meta->call_de) == 0) return;
    }
    if (g_unseen.size() >= MAX_UNSEEN) return;

    UnseenEntry e;
    e.meta = *meta;
    e.dist = dist;
    e.snr  = snr;
    g_unseen.push_back(e);
}

extern "C" void autosel_clear_unseen(void) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_unseen.clear();
}

extern "C" bool autosel_pick(auto_sel_mode_t mode, autosel_candidate_t *out) {
    if (!out || mode == AUTO_SEL_OFF) return false;

    std::lock_guard<std::mutex> lock(g_mutex);

    g_unseen.erase(
        std::remove_if(g_unseen.begin(), g_unseen.end(), [](const UnseenEntry &u) {
            std::string norm;
            normalize_call(u.meta.call_de, norm);
            return any_blacklisted_norm(norm);
        }),
        g_unseen.end());

    if (g_unseen.empty()) return false;

    auto it_best = g_unseen.end();

    if (mode == AUTO_SEL_FIRST) {
        it_best = g_unseen.begin();
    } else if (mode == AUTO_SEL_NEW_GRID) {
        qso_log_mode_t qlog_mode = subject_get_int(cfg.ft8_protocol.val) == FTX_PROTOCOL_FT8 ? MODE_FT8 : MODE_FT4;
        qso_log_band_t qlog_band = qso_log_freq_to_band(subject_get_int(cfg_cur.fg_freq));
        for (auto it = g_unseen.begin(); it != g_unseen.end(); ++it) {
            if (it->meta.grid[0] == '\0') continue;
            if (!qso_log_search_worked_grid(it->meta.grid, qlog_mode, qlog_band)) {
                it_best = it;
                break;
            }
        }
        if (it_best == g_unseen.end()) it_best = g_unseen.begin();
    } else if (mode == AUTO_SEL_FARTHEST) {
        int best_dist = 0;
        for (auto it = g_unseen.begin(); it != g_unseen.end(); ++it) {
            if (it->dist > best_dist) {
                best_dist = it->dist;
                it_best   = it;
            }
        }
        if (it_best == g_unseen.end()) it_best = g_unseen.begin();
    } else {
        it_best = g_unseen.begin();
        for (auto it = std::next(g_unseen.begin()); it != g_unseen.end(); ++it) {
            if (it->snr > it_best->snr) it_best = it;
        }
    }

    if (it_best == g_unseen.end()) return false;

    out->meta = it_best->meta;
    out->dist = it_best->dist;
    out->snr  = it_best->snr;
    g_unseen.erase(it_best);
    return true;
}

extern "C" void autosel_blacklist_add(const char *call) {
    if (!call || call[0] == '\0') return;
    std::string norm;
    normalize_call(call, norm);
    if (norm.empty()) return;

    std::lock_guard<std::mutex> lock(g_mutex);

    for (auto &e : g_cycle_bl) {
        if (e.call[0] != '\0' && norm == e.call) {
            e.miss_cycles     = 0;
            e.seen_this_cycle = true;
            return;
        }
    }
    for (auto &e : g_cycle_bl) {
        if (e.call[0] == '\0') {
            std::snprintf(e.call, sizeof(e.call), "%s", norm.c_str());
            e.miss_cycles     = 0;
            e.seen_this_cycle = true;
            return;
        }
    }
    size_t worst = 0;
    for (size_t i = 1; i < g_cycle_bl.size(); i++) {
        if (g_cycle_bl[i].miss_cycles > g_cycle_bl[worst].miss_cycles) worst = i;
    }
    std::snprintf(g_cycle_bl[worst].call, sizeof(g_cycle_bl[worst].call), "%s", norm.c_str());
    g_cycle_bl[worst].miss_cycles     = 0;
    g_cycle_bl[worst].seen_this_cycle = true;
}

extern "C" void autosel_blacklist_mark_seen(const char *call) {
    if (!call || call[0] == '\0') return;
    std::string norm;
    normalize_call(call, norm);
    if (norm.empty()) return;

    std::lock_guard<std::mutex> lock(g_mutex);
    for (auto &e : g_cycle_bl) {
        if (e.call[0] != '\0' && norm == e.call) {
            e.seen_this_cycle = true;
            return;
        }
    }
}

extern "C" void autosel_blacklist_advance_cycle(void) {
    std::lock_guard<std::mutex> lock(g_mutex);
    for (auto &e : g_cycle_bl) {
        if (e.call[0] == '\0') continue;
        if (e.seen_this_cycle) {
            e.miss_cycles = 0;
        } else {
            if (e.miss_cycles < 255) e.miss_cycles++;
        }
        e.seen_this_cycle = false;
        if (e.miss_cycles >= CYCLE_BL_CLEAR_MISS) {
            e.call[0]         = '\0';
            e.miss_cycles     = 0;
            e.seen_this_cycle = false;
        }
    }
}

extern "C" void autosel_blacklist_clear_all(void) {
    std::lock_guard<std::mutex> lock(g_mutex);
    for (auto &e : g_cycle_bl) {
        e.call[0]         = '\0';
        e.miss_cycles     = 0;
        e.seen_this_cycle = false;
    }
}

extern "C" bool autosel_is_user_blacklisted(const char *call) {
    if (!call || call[0] == '\0') return false;
    std::string norm;
    normalize_call(call, norm);
    std::lock_guard<std::mutex> lock(g_mutex);
    return user_blacklisted_norm(norm);
}

extern "C" bool autosel_is_blacklisted(const char *call) {
    if (!call || call[0] == '\0') return false;
    std::string norm;
    normalize_call(call, norm);
    std::lock_guard<std::mutex> lock(g_mutex);
    return any_blacklisted_norm(norm);
}

/* -------------------- state machine extension points ------------------ */

extern "C" void autosel_rx_hook(const char *text, int snr,
                                float freq_hz, float time_sec,
                                ftx_msg_meta_t *meta,
                                const slot_info_t *info) {
    (void)text;
    (void)freq_hz;
    (void)time_sec;

    if (!meta) return;

    if (s_qso_tx_exhausted && meta->to_me && meta->call_de[0] != '\0') {
        if ((s_qso_active_call[0] == '\0') ||
            (strcmp(s_qso_active_call, meta->call_de) != 0)) {
            if (!s_pending_to_me_valid) {
                s_pending_to_me_meta  = *meta;
                s_pending_to_me_odd   = info->odd;
                s_pending_to_me_valid = true;
            }
        }
    }

    FTxQsoProcessor *qso = ft8_get_qso_processor();
    ftx_tx_msg_t    *txm = ft8_get_tx_msg();
    if (s_qso_active && txm && is_grid_exchange_msg(txm->msg) &&
        meta->to_me && (meta->type == FTX_MSG_TYPE_GRID) && meta->call_de[0] != '\0') {
        if ((s_qso_active_call[0] == '\0') ||
            (strcmp(s_qso_active_call, meta->call_de) != 0)) {
            if (!s_pending_grid_valid) {
                s_pending_grid_meta  = *meta;
                s_pending_grid_odd   = info->odd;
                s_pending_grid_valid = true;
            }
        }
    }

    if (meta->type == FTX_MSG_TYPE_CQ && !meta->to_me) {
        autosel_blacklist_mark_seen(meta->call_de);
        bool no_current  = !qso || !ftx_qso_processor_has_current(qso);
        bool may_collect = (s_mode != AUTO_SEL_OFF) &&
                           (no_current || s_recover_mode);
        if (may_collect) {
            int dist = compute_dist(meta);
            autosel_add_candidate(meta, dist, meta->local_snr);
        }
    }
}

extern "C" void autosel_slot_end_hook(const slot_info_t *info) {
    FTxQsoProcessor *qso = ft8_get_qso_processor();
    ftx_tx_msg_t    *txm = ft8_get_tx_msg();

    autosel_blacklist_advance_cycle();

    if (!qso || !ftx_qso_processor_has_current(qso)) {
        reset_qso_state();
    }

    resume_cq_if_qso_gone();

    if (s_qso_tx_exhausted && (!txm || txm->msg[0] == '\0') && s_pending_to_me_valid) {
        if (qso && ftx_qso_processor_has_current(qso)) {
            ftx_qso_processor_reset(qso);
        }
        start_qso_from_meta(&s_pending_to_me_meta, !s_pending_to_me_odd);
        pause_cq_for_qso();
        s_pending_to_me_valid  = false;
        s_qso_tx_exhausted     = false;
        s_recover_mode         = false;
        s_qso_active           = false;
        s_grid_tx_count        = 0;
        s_pending_grid_valid   = false;
    }

    if ((!txm || txm->msg[0] == '\0') && (s_mode != AUTO_SEL_OFF) &&
        (!qso || !ftx_qso_processor_has_current(qso) || s_recover_mode)) {
        autosel_candidate_t pick;
        if (autosel_pick(s_mode, &pick)) {
            if (s_recover_mode && qso && ftx_qso_processor_has_current(qso)) {
                ftx_qso_processor_reset(qso);
            }
            start_qso_from_meta(&pick.meta, !info->odd);
            s_qso_active          = true;
            s_grid_tx_count       = 0;
            s_pending_grid_valid  = false;
            pause_cq_for_qso();
            s_recover_mode        = false;
            s_qso_tx_exhausted    = false;
            s_pending_to_me_valid = false;
        }
    }

    autosel_clear_unseen();
}

/** Grid pileup preemption: swap tx_msg to the stashed grid caller.
 *  Does not defer the tick — odd/even parity is already aligned. */
extern "C" void autosel_grid_swap_on_tick(const slot_info_t *info) {
    FTxQsoProcessor *qso = ft8_get_qso_processor();
    ftx_tx_msg_t    *txm = ft8_get_tx_msg();
    if (!qso || !txm || !info) return;
    if (!is_grid_exchange_msg(txm->msg)) return;
    if (!s_qso_active) return;
    if (s_grid_tx_count < 2) return;
    if (!s_pending_grid_valid) return;

    if (ftx_qso_processor_has_current(qso)) {
        ftx_qso_processor_reset(qso);
    }
    start_qso_from_meta(&s_pending_grid_meta, !s_pending_grid_odd);

    s_pending_grid_valid = false;
    s_qso_active         = false;
    s_grid_tx_count      = 0;
}

extern "C" void autosel_post_tx(void) {
    FTxQsoProcessor *qso = ft8_get_qso_processor();
    ftx_tx_msg_t    *txm = ft8_get_tx_msg();
    if (!qso || !txm) return;

    bool sent_grid = is_grid_exchange_msg(txm->msg);

    if (sent_grid && s_qso_active && (s_grid_tx_count < 255)) {
        s_grid_tx_count++;
    }

    bool repeats_exhausted = (txm->repeats == 0);

    if (repeats_exhausted) {
        bool was_cq = (strncmp(txm->msg, "CQ", 2) == 0);

        if (!was_cq && ftx_qso_processor_has_current(qso)) {
            s_qso_tx_exhausted = true;

            if (sent_grid && s_qso_active_call[0] != '\0') {
                autosel_blacklist_add(s_qso_active_call);
            }
            if ((s_mode != AUTO_SEL_OFF) && !s_cq_paused_for_qso) {
                s_recover_mode = true;
            }
        }
    }

    if (!ftx_qso_processor_has_current(qso) && txm->msg[0] == '\0') {
        resume_cq_if_qso_gone();
    }
}

extern "C" void autosel_on_qso_saved(void) {
    if (s_cq_paused_for_qso) {
        ft8_set_cq_enabled(true);
        s_cq_paused_for_qso = false;
        ft8_schedule_cq_tx();
    }
}

extern "C" void autosel_on_tx_msg_updated(const ftx_msg_meta_t *meta, bool odd_slot) {
    (void)odd_slot;
    if (!meta) return;

    if (meta->to_me && (meta->call_de[0] != '\0')) {
        strncpy(s_qso_active_call, meta->call_de, sizeof(s_qso_active_call) - 1);
        s_qso_active_call[sizeof(s_qso_active_call) - 1] = '\0';
    }

    if (ft8_is_cq_enabled()) {
        s_cq_paused_for_qso = true;
        ft8_set_cq_enabled(false);
    }

    s_qso_tx_exhausted   = false;
    s_recover_mode       = false;
    s_grid_tx_count      = 0;
    s_pending_grid_valid = false;
}

extern "C" void autosel_on_mode_switch(void) {
    s_cq_paused_for_qso = false;
    autosel_clear_unseen();
    reset_qso_state();
}

extern "C" void autosel_init_state(void) {
    s_mode              = AUTO_SEL_OFF;
    s_local_qth_valid   = false;
    s_cq_paused_for_qso = false;
    reset_qso_state();

    autosel_init();
    autosel_user_blacklist_load();

    if (params.qth.x[0] != '\0' && qth_grid_check(params.qth.x)) {
        s_local_qth_valid = true;
    }
}

extern "C" void autosel_cleanup_state(void) {
    autosel_deinit();
    reset_qso_state();
    s_cq_paused_for_qso = false;
    s_mode = AUTO_SEL_OFF;
}

extern "C" int autosel_get_mode(void) {
    return (int)s_mode;
}

extern "C" const char *autosel_get_mode_text(void) {
    return mode_text(s_mode);
}

extern "C" void autosel_cycle_mode(void) {
    s_mode = (auto_sel_mode_t)((s_mode + 1) % 5);
    if (s_mode == AUTO_SEL_OFF) {
        autosel_clear_unseen();
        autosel_blacklist_clear_all();
        reset_qso_state();
    }
}

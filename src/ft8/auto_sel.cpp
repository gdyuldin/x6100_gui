/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI - FT8 auto-select (unseen-CQ picker)
 *
 *  Copyright (c) 2026
 */

#include "auto_sel.h"

/* subjects.h and cfg.h already gate their C++/C contents internally; do
 * NOT wrap them in extern "C" or their own STL includes get C linkage. */
#include "../cfg/subjects.h"
#include "../cfg/cfg.h"

#include <ft8lib/constants.h>

extern "C" {
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
constexpr size_t   USER_BL_CALL_MAX_LEN         = 15;   /* matches 16-1 in old code */
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

    /* Strip UTF-8 BOM if present. */
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

} /* namespace */

/* -------------------- public C API ------------------------------------- */

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

    /* Already worked (per qso_log)? skip. These calls take their own
     * qso_log mutex internally so we run them outside our lock. */
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

    /* Purge blacklisted entries first; they may have been added before the
     * blacklist was loaded/updated. */
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
    } else { /* AUTO_SEL_HIGHEST_SNR (and any other non-OFF value) */
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

    /* Refresh existing */
    for (auto &e : g_cycle_bl) {
        if (e.call[0] != '\0' && norm == e.call) {
            e.miss_cycles     = 0;
            e.seen_this_cycle = true;
            return;
        }
    }
    /* Free slot */
    for (auto &e : g_cycle_bl) {
        if (e.call[0] == '\0') {
            std::snprintf(e.call, sizeof(e.call), "%s", norm.c_str());
            e.miss_cycles     = 0;
            e.seen_this_cycle = true;
            return;
        }
    }
    /* Evict entry with the largest miss count. */
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

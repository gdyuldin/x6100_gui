#include "qso.h"

extern "C" {
#include "../qth/qth.h"
#include "utils.h"
}

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <pthread.h>

/*
 * Stateless-core QSO engine (ft8d model).
 *
 * compute_one() is a pure function: one decoded message in, at most one
 * reply out. All memory lives in a small module-private state block and
 * is touched only by the two public entry points:
 *
 *   ftx_qso_on_user_message    - arms the manual target (callsign + parity
 *                                lock + sticky seed), then evaluates the
 *                                clicked message as a batch of one.
 *   ftx_qso_on_decoded_messages- per-slot decision. MANUAL: filter the
 *                                batch down to "from target, addressed to
 *                                me", fall back to the sticky message when
 *                                the peer is silent. AUTO: ft8d pipeline
 *                                (candidates -> blacklist -> selector).
 */

namespace {

constexpr int STICKY_RETRY_MAX  = 5;  /* manual: retries while the peer is silent */
constexpr int AUTO_TX_TEXT_MAX  = 3;  /* auto: same TX text sent at most 3 times */
constexpr int SNR_REPORT_MIN    = -30;
constexpr int SNR_REPORT_MAX    = 30;

pthread_mutex_t engine_mutex = PTHREAD_MUTEX_INITIALIZER;

struct BlacklistEntry {
    char text[35];
    int  count;
};

/* Per-callsign QSO bookkeeping (shared by manual and auto modes).
 * grid survives a save; the rest is cleared once the QSO is logged.
 * rst_sent can only be (re)armed by us actually transmitting a report,
 * which makes duplicate logging structurally impossible: after a save the
 * stateless replies to re-sent R+nn are bare RR73s. */
struct PeerEntry {
    char   call[13];
    char   grid[9];
    bool   has_rst_sent;
    int    rst_sent;
    bool   has_rst_rcvd;
    int    rst_rcvd;
    time_t qso_start;
};

struct EngineState {
    /* Manual mode (armed by a user click). */
    bool  has_target;
    bool  target_odd;
    char  target_call[13];
    bool  has_sticky;
    int   sticky_count;
    char  sticky_text[64];
    int   sticky_snr;
    float sticky_freq;

    /* Auto modes. */
    char           last_call[13];
    BlacklistEntry blacklist[64];
    int            blacklist_len;
    int            blacklist_next;

    /* QSO logging. LRU: front = most recently touched, the tail entry is
     * evicted when full (a busy band can bring 60+ decodes per slot, so an
     * active QSO must not be flushed out by a flood of one-shot CQs). */
    PeerEntry peers[256];
    int       peers_len;
};

EngineState g_state;

/* One reply candidate; `order` is the ft8d QSO progress (bigger = deeper). */
struct Candidate {
    int            order;
    ftx_msg_type_t type;
    char           call[13];
    char           grid[9];
    char           text[35];
    int            snr;
    float          freq_hz;
    bool           grid_worked;
};

/* ---------- small helpers ---------------------------------------------- */

char upch(char c) {
    return (char)std::toupper((unsigned char)c);
}

bool ieq(const char *a, const char *b) {
    if (!a || !b) return false;
    while (*a && *b) {
        if (upch(*a) != upch(*b)) return false;
        a++;
        b++;
    }
    return *a == *b;
}

bool ieq_str(const std::string &a, const char *b) {
    return ieq(a.c_str(), b);
}

void copy_str(char *dst, size_t dst_len, const char *src) {
    if (!dst || dst_len == 0) return;
    if (!src) src = "";
    std::snprintf(dst, dst_len, "%s", src);
}

void init_meta(ftx_msg_meta_t *meta, int snr, float freq_hz, float time_sec) {
    if (!meta) return;
    std::memset(meta, 0, sizeof(*meta));
    meta->local_snr  = snr;
    meta->remote_snr = 0;
    meta->freq_hz    = freq_hz;
    meta->time_sec   = time_sec;
    meta->type       = FXT_MSG_TYPE_OTHER;
}

void init_response(bool rx_odd, ftx_qso_response_t *response) {
    if (!response) return;
    std::memset(response, 0, sizeof(*response));
    response->tx_odd = !rx_odd;
}

std::string local_qth4(const ftx_qso_context_t *ctx) {
    if (!ctx || !ctx->local_qth) return "";
    std::string qth(ctx->local_qth);
    if (qth.size() > 4) qth.resize(4);
    return qth;
}

int clamp_snr(int snr) {
    if (snr < SNR_REPORT_MIN) return SNR_REPORT_MIN;
    if (snr > SNR_REPORT_MAX) return SNR_REPORT_MAX;
    return snr;
}

void normalize_dxp_tokens(const ftx_qso_context_t *ctx, std::vector<std::string> &tokens, const std::string &text) {
    if ((tokens.size() < 5) || (text.find(';') == text.npos)) return;

    std::vector<std::string> new_tokens;
    const char *local_callsign = (ctx && ctx->local_callsign) ? ctx->local_callsign : "";
    if (ieq_str(tokens[0], local_callsign)) {
        new_tokens.push_back(tokens[0]);
        new_tokens.push_back(tokens[3]);
        new_tokens.push_back(tokens[1].substr(0, tokens[1].size() - 1));
    } else {
        new_tokens.push_back(tokens[2]);
        new_tokens.push_back(tokens[3]);
        new_tokens.push_back(tokens[4]);
    }
    tokens = new_tokens;
}

std::vector<std::string> parse_tokens(const ftx_qso_context_t *ctx, const char *text) {
    std::string src = text ? text : "";
    std::vector<std::string> tokens = split_text(src);
    normalize_dxp_tokens(ctx, tokens, src);

    for (auto &token : tokens) {
        if (!token.empty() && token[0] == '<') {
            token = token.substr(1, token.length() - 2);
        }
    }
    return tokens;
}

bool callsign_is_local(const ftx_qso_context_t *ctx, const std::string &callsign) {
    return ctx && ctx->local_callsign && ieq_str(callsign, ctx->local_callsign);
}

/* Exception-free SNR token parser: "+05" / "-12". std::stoi would throw
 * across the extern "C" boundary on malformed decodes like a bare "+". */
bool parse_snr(const std::string &s, int *out) {
    char *end = nullptr;
    long  v   = std::strtol(s.c_str(), &end, 10);
    if (end == s.c_str() || *end != '\0') return false;
    *out = (int)v;
    return true;
}

void fill_tx(ftx_qso_response_t *response, const char *msg, float freq_hz) {
    if (!response || !msg || msg[0] == '\0') return;
    response->action  = FTX_QSO_ACTION_TX;
    response->freq_hz = freq_hz;
    copy_str(response->tx_msg, sizeof(response->tx_msg), msg);
}

/* ---------- QSO bookkeeping (peers dict) --------------------------------- */

/* Look up (or create) the peer's entry and move it to the front, so the
 * table evicts the least recently touched callsign. */
PeerEntry *peer_get(const char *call) {
    PeerEntry *peers = g_state.peers;
    int cap = (int)(sizeof(g_state.peers) / sizeof(g_state.peers[0]));

    for (int i = 0; i < g_state.peers_len; i++) {
        if (!ieq(peers[i].call, call)) continue;
        if (i > 0) {
            PeerEntry found = peers[i];
            std::memmove(&peers[1], &peers[0], i * sizeof(PeerEntry));
            peers[0] = found;
        }
        return &peers[0];
    }

    if (g_state.peers_len < cap) {
        g_state.peers_len++;
    }
    /* Shift everything down one slot; when full this drops the tail. */
    std::memmove(&peers[1], &peers[0], (g_state.peers_len - 1) * sizeof(PeerEntry));
    std::memset(&peers[0], 0, sizeof(peers[0]));
    copy_str(peers[0].call, sizeof(peers[0].call), call);
    return &peers[0];
}

/* Clear everything except the grid, which stays useful for later QSOs. */
void peer_close_qso(PeerEntry *peer) {
    peer->has_rst_sent = false;
    peer->has_rst_rcvd = false;
    peer->rst_sent     = 0;
    peer->rst_rcvd     = 0;
    peer->qso_start    = 0;
}

bool peer_qso_complete(const PeerEntry *peer) {
    return peer->has_rst_sent && peer->has_rst_rcvd;
}

void peer_fill_record(const PeerEntry *peer, time_t end_time, ftx_qso_record_t *rec) {
    std::memset(rec, 0, sizeof(*rec));
    copy_str(rec->call, sizeof(rec->call), peer->call);
    copy_str(rec->grid, sizeof(rec->grid), peer->grid);
    rec->rst_sent   = peer->rst_sent;
    rec->rst_rcvd   = peer->rst_rcvd;
    rec->start_time = peer->qso_start ? peer->qso_start : end_time;
    rec->end_time   = end_time;
}

/* Try to log the QSO with `peer`; on success fills response->qso and
 * closes the peer's bookkeeping. */
void peer_try_save(PeerEntry *peer, time_t end_time, ftx_qso_response_t *response) {
    if (!peer_qso_complete(peer)) return;
    peer_fill_record(peer, end_time, &response->qso);
    response->save = true;
    peer_close_qso(peer);
}

/* Every decoded message feeds the peers dict: grids from any message that
 * carries one, reports from messages addressed to us, and the received
 * final 73 (the peer may skip RR73) which can complete a QSO with no
 * reply scheduled. */
void analyze_rx(const ftx_qso_context_t *ctx,
                const ftx_decoded_msg_t *msgs,
                size_t msg_count,
                ftx_qso_response_t *response) {
    for (size_t i = 0; i < msg_count; i++) {
        if (!msgs[i].text) continue;

        ftx_msg_meta_t meta;
        ftx_qso_parse_rx_text(ctx, msgs[i].text, msgs[i].snr,
                              msgs[i].freq_hz, msgs[i].time_sec, &meta);
        if (meta.call_de[0] == '\0') continue;
        if (ieq(meta.call_de, ctx->local_callsign)) continue;

        PeerEntry *peer = peer_get(meta.call_de);
        if (meta.grid[0] != '\0') {
            copy_str(peer->grid, sizeof(peer->grid), meta.grid);
        }
        if (!meta.to_me) continue;

        if ((meta.type == FTX_MSG_TYPE_REPORT) || (meta.type == FTX_MSG_TYPE_R_REPORT)) {
            peer->has_rst_rcvd = true;
            peer->rst_rcvd     = meta.remote_snr;
        } else if (meta.type == FTX_MSG_TYPE_73) {
            peer_try_save(peer, ctx->now, response);
        }
    }
}

/* Bookkeeping attached to every transmitted reply.
 * - Replying to a CQ (tx1) or grid (tx2) starts a fresh QSO;
 * - order 3/4 arms rst_sent with the exact value we put on the air;
 * - our RR73 / 73 completes the QSO when both reports were exchanged.
 * Sticky retransmits never restart the QSO clock. */
void emit_candidate(const ftx_qso_context_t *ctx,
                    const Candidate *cand,
                    bool sticky,
                    ftx_qso_response_t *response) {
    PeerEntry *peer = peer_get(cand->call);

    /* The click entry point bypasses analyze_rx: keep the grid fresh here too. */
    if (cand->grid[0] != '\0') {
        copy_str(peer->grid, sizeof(peer->grid), cand->grid);
    }

    if (!sticky && ((cand->type == FTX_MSG_TYPE_CQ) || (cand->type == FTX_MSG_TYPE_GRID))) {
        peer->qso_start = ctx->now;
    } else if (peer->qso_start == 0) {
        peer->qso_start = ctx->now;
    }

    if ((cand->order == 3) || (cand->order == 4)) {
        peer->has_rst_sent = true;
        peer->rst_sent     = clamp_snr(cand->snr);
    }
    if ((cand->order == 5) || (cand->order == 6)) {
        peer_try_save(peer, ctx->now, response);
    }

    fill_tx(response, cand->text, cand->freq_hz);
}

/* ---------- pure reply generator (ft8d compute_tx, one message) --------- */

/* Map one decoded message to at most one reply. Returns false when the
 * message deserves no reply (not addressed to us and not an opportunity,
 * a final 73, our own echo, or unparseable). */
bool compute_one(const ftx_qso_context_t *ctx,
                 const char *text, int snr, float freq_hz,
                 Candidate *cand) {
    if (!ctx || !ctx->local_callsign || ctx->local_callsign[0] == '\0' ||
        !text || text[0] == '\0' || !cand) {
        return false;
    }
    /* Hash / incomplete decodes ("...") are unusable. */
    if (std::strchr(text, '.')) return false;

    ftx_msg_meta_t meta;
    ftx_qso_parse_rx_text(ctx, text, snr, freq_hz, 0.0f, &meta);
    if (meta.call_de[0] == '\0') return false;
    if (ieq(meta.call_de, ctx->local_callsign)) return false; /* own echo */

    std::memset(cand, 0, sizeof(*cand));
    cand->type    = meta.type;
    cand->snr     = snr;
    cand->freq_hz = meta.freq_hz;
    copy_str(cand->call, sizeof(cand->call), meta.call_de);
    copy_str(cand->grid, sizeof(cand->grid), meta.grid);

    const char *me = ctx->local_callsign;

    if (meta.type == FTX_MSG_TYPE_CQ) {
        cand->order = 2;
        std::snprintf(cand->text, sizeof(cand->text), "%s %s %s",
                      meta.call_de, me, local_qth4(ctx).c_str());
        return true;
    }

    if (meta.to_me) {
        switch (meta.type) {
            case FTX_MSG_TYPE_GRID:
                cand->order = 3;
                std::snprintf(cand->text, sizeof(cand->text), "%s %s %+03d",
                              meta.call_de, me, clamp_snr(snr));
                return true;
            case FTX_MSG_TYPE_REPORT:
                cand->order = 4;
                std::snprintf(cand->text, sizeof(cand->text), "%s %s R%+03d",
                              meta.call_de, me, clamp_snr(snr));
                return true;
            case FTX_MSG_TYPE_R_REPORT:
                cand->order = 5;
                std::snprintf(cand->text, sizeof(cand->text), "%s %s RR73",
                              meta.call_de, me);
                return true;
            case FTX_MSG_TYPE_RR73:
                cand->order = 6;
                std::snprintf(cand->text, sizeof(cand->text), "%s %s 73",
                              meta.call_de, me);
                return true;
            default:
                return false; /* 73 / OTHER: nothing to send */
        }
    }

    /* Someone else's QSO just ended: tail-end the freed station. */
    if ((meta.type == FTX_MSG_TYPE_RR73) || (meta.type == FTX_MSG_TYPE_73)) {
        cand->order = 0;
        std::snprintf(cand->text, sizeof(cand->text), "%s %s %s",
                      meta.call_de, me, local_qth4(ctx).c_str());
        return true;
    }

    return false;
}

/* ---------- manual mode -------------------------------------------------- */

void sticky_clear(void) {
    g_state.has_sticky     = false;
    g_state.sticky_count   = 0;
    g_state.sticky_text[0] = '\0';
}

void sticky_store(const char *text, int snr, float freq_hz) {
    g_state.has_sticky   = true;
    g_state.sticky_count = 0;
    g_state.sticky_snr   = snr;
    g_state.sticky_freq  = freq_hz;
    copy_str(g_state.sticky_text, sizeof(g_state.sticky_text), text);
}

/* Reply to one real message from the target and refresh the sticky slot.
 * The peer's RR73 answers with 73 but is never stored: 73 must stay purely
 * reactive, one per actually received RR73. A real 73 closes the QSO. */
void manual_reply_real(const ftx_qso_context_t *ctx,
                       const ftx_decoded_msg_t *msg,
                       ftx_msg_type_t type,
                       ftx_qso_response_t *response) {
    if (type == FTX_MSG_TYPE_73) {
        sticky_clear();
        return;
    }

    Candidate cand;
    if (!compute_one(ctx, msg->text, msg->snr, msg->freq_hz, &cand)) {
        return;
    }
    if (type == FTX_MSG_TYPE_RR73) {
        sticky_clear();
    } else {
        sticky_store(msg->text, msg->snr, msg->freq_hz);
    }
    emit_candidate(ctx, &cand, false, response);
}

void manual_decide(const ftx_qso_context_t *ctx,
                   const ftx_decoded_msg_t *msgs,
                   size_t msg_count,
                   bool rx_odd,
                   ftx_qso_response_t *response) {
    if (!g_state.has_target) return;
    /* A QSO keeps one parity for its whole life; slot ends of my own TX
     * parity carry no information about the peer. */
    if (rx_odd != g_state.target_odd) return;

    /* Pick the most advanced real message from the target addressed to me.
     * FXT_MSG_TYPE_OTHER sorts after 73 in the enum and carries no protocol
     * step, so it is skipped outright. */
    const ftx_decoded_msg_t *best = NULL;
    ftx_msg_type_t best_type = FXT_MSG_TYPE_OTHER;
    for (size_t i = 0; i < msg_count; i++) {
        if (!msgs[i].text) continue;
        ftx_msg_meta_t meta;
        ftx_qso_parse_rx_text(ctx, msgs[i].text, msgs[i].snr,
                              msgs[i].freq_hz, msgs[i].time_sec, &meta);
        if (meta.type == FXT_MSG_TYPE_OTHER) continue;
        if (!meta.to_me || !ieq(meta.call_de, g_state.target_call)) continue;
        if (!best || meta.type >= best_type) {
            best      = &msgs[i];
            best_type = meta.type;
        }
    }

    if (best) {
        manual_reply_real(ctx, best, best_type, response);
        return;
    }

    /* Peer silent (or busy with someone else): sticky retry. */
    if (g_state.has_sticky && (g_state.sticky_count < STICKY_RETRY_MAX)) {
        Candidate cand;
        if (compute_one(ctx, g_state.sticky_text, g_state.sticky_snr,
                        g_state.sticky_freq, &cand)) {
            g_state.sticky_count++;
            emit_candidate(ctx, &cand, true, response);
        }
    }
}

/* ---------- auto modes (ft8d pipeline) ----------------------------------- */

BlacklistEntry *blacklist_find(const char *text) {
    for (int i = 0; i < g_state.blacklist_len; i++) {
        if (ieq(g_state.blacklist[i].text, text)) {
            return &g_state.blacklist[i];
        }
    }
    return NULL;
}

void blacklist_bump(const char *text) {
    BlacklistEntry *e = blacklist_find(text);
    if (!e) {
        int cap = (int)(sizeof(g_state.blacklist) / sizeof(g_state.blacklist[0]));
        if (g_state.blacklist_len < cap) {
            e = &g_state.blacklist[g_state.blacklist_len++];
        } else {
            e = &g_state.blacklist[g_state.blacklist_next];
            g_state.blacklist_next = (g_state.blacklist_next + 1) % cap;
        }
        copy_str(e->text, sizeof(e->text), text);
        e->count = 0;
    }
    e->count++;
}

bool candidate_distance(const Candidate *cand,
                        double local_lat, double local_lon, double *dist) {
    if (cand->grid[0] == '\0') return false;
    if (!qth_grid_check(cand->grid)) return false;
    double lat, lon;
    qth_str_to_pos(cand->grid, &lat, &lon);
    *dist = qth_pos_dist(lat, lon, local_lat, local_lon);
    return true;
}

void auto_decide(const ftx_qso_context_t *ctx,
                 const ftx_decoded_msg_t *msgs,
                 size_t msg_count,
                 ftx_qso_response_t *response) {
    Candidate cands[64];
    size_t    n = 0;

    for (size_t i = 0; i < msg_count && n < 64; i++) {
        if (!msgs[i].text) continue;
        Candidate cand;
        if (!compute_one(ctx, msgs[i].text, msgs[i].snr, msgs[i].freq_hz, &cand)) {
            continue;
        }
        /* Behaviour level: RES never initiates (no answering other CQs,
         * no tail-ending — order >= 3 replies to stations calling me are
         * its whole point); FULL initiates but never tail-ends. */
        if ((ctx->auto_level == FTX_QSO_AUTO_RES) && (cand.order <= 2)) continue;
        if ((ctx->auto_level == FTX_QSO_AUTO_FULL) && (cand.order == 0)) continue;
        /* remove_worked (always on in auto modes): never initiate a QSO
         * with an already worked tuple; replies (order >= 3) keep flowing
         * once the peer calls us. */
        if (msgs[i].worked && (cand.order <= 2)) continue;
        /* ft8d tx_blacklist: same TX text at most AUTO_TX_TEXT_MAX times. */
        BlacklistEntry *e = blacklist_find(cand.text);
        if (e && e->count >= AUTO_TX_TEXT_MAX) continue;
        cand.grid_worked = msgs[i].grid_worked;
        cands[n++] = cand;
    }
    if (n == 0) return;

    /* Level 1: deepest QSO progress wins. */
    int max_order = cands[0].order;
    for (size_t i = 1; i < n; i++) {
        if (cands[i].order > max_order) max_order = cands[i].order;
    }
    size_t m = 0;
    for (size_t i = 0; i < n; i++) {
        if (cands[i].order == max_order) cands[m++] = cands[i];
    }
    n = m;

    /* Level 2: stick to the station we transmitted to last. */
    if (g_state.last_call[0] != '\0') {
        m = 0;
        for (size_t i = 0; i < n; i++) {
            if (ieq(cands[i].call, g_state.last_call)) cands[m++] = cands[i];
        }
        if (m > 0) n = m;
    }

    /* Level 3: mode tie-break. */
    size_t pick = 0;
    switch (ctx->sel) {
        case FTX_QSO_SEL_NEW_GRID:
            /* Prefer grids never worked from here; a preference, not a
             * filter — when everything is worked, pick among all. Ties
             * within the pool break by SNR either way. */
            m = 0;
            for (size_t i = 0; i < n; i++) {
                if (!cands[i].grid_worked) cands[m++] = cands[i];
            }
            if (m > 0) n = m;
            for (size_t i = 1; i < n; i++) {
                if (cands[i].snr > cands[pick].snr) pick = i;
            }
            break;
        case FTX_QSO_SEL_DISTANCE: {
            bool have_local = ctx->local_qth && ctx->local_qth[0] != '\0';
            double local_lat = 0, local_lon = 0;
            if (have_local) {
                qth_str_to_pos(ctx->local_qth, &local_lat, &local_lon);
            }
            bool   found = false;
            double best  = -1.0;
            if (have_local) {
                for (size_t i = 0; i < n; i++) {
                    double d;
                    if (candidate_distance(&cands[i], local_lat, local_lon, &d) &&
                        (!found || d > best)) {
                        found = true;
                        best  = d;
                        pick  = i;
                    }
                }
            }
            if (found) break;
        } /* no distance data: fall through to SNR */
        /* fallthrough */
        case FTX_QSO_SEL_SNR:
        default:
            for (size_t i = 1; i < n; i++) {
                if (cands[i].snr > cands[pick].snr) pick = i;
            }
            break;
        case FTX_QSO_SEL_RANDOM:
            pick = (size_t)(std::rand() % (int)n);
            break;
    }

    blacklist_bump(cands[pick].text);
    copy_str(g_state.last_call, sizeof(g_state.last_call), cands[pick].call);
    emit_candidate(ctx, &cands[pick], false, response);
}

void decide(const ftx_qso_context_t *ctx,
            const ftx_decoded_msg_t *msgs,
            size_t msg_count,
            bool rx_odd,
            ftx_qso_response_t *response) {
    init_response(rx_odd, response);
    if (!ctx || !ctx->local_callsign || ctx->local_callsign[0] == '\0' || !response) {
        return;
    }
    /* Feed the peers dict from every decoded message regardless of mode
     * (grids, received reports, received final 73). */
    analyze_rx(ctx, msgs, msg_count, response);

    if (ctx->auto_level == FTX_QSO_AUTO_OFF) {
        manual_decide(ctx, msgs, msg_count, rx_odd, response);
    } else {
        auto_decide(ctx, msgs, msg_count, response);
    }
}

} /* namespace */

/********************** GLOBAL FUNCTIONS **********************/

std::vector<std::string> split_text(std::string text) {
    std::vector<std::string> tokens;

    const char  delim = ' ';
    size_t      initialPos = 0;
    size_t      pos;
    std::string token;
    do {
        pos = text.find(delim, initialPos);
        token = text.substr(initialPos, pos - initialPos);
        if (!token.empty())
            tokens.push_back(token);
        initialPos = pos + 1;
    } while (pos != std::string::npos);
    return tokens;
}

void ftx_qso_parse_rx_text(const ftx_qso_context_t *ctx,
                           const char *text, int snr,
                           float freq_hz, float time_sec,
                           ftx_msg_meta_t *meta) {
    init_meta(meta, snr, freq_hz, time_sec);
    if (!meta || !text) return;

    std::vector<std::string> tokens = parse_tokens(ctx, text);
    if (tokens.empty()) return;

    if (ieq_str(tokens[0], "CQ")) {
        meta->type = FTX_MSG_TYPE_CQ;
        size_t call_de_pos;
        if ((tokens.size() > 2) && is_cq_modifier(tokens[1].c_str())) {
            call_de_pos = 2;
        } else {
            call_de_pos = 1;
        }
        if (tokens.size() <= call_de_pos) return;
        copy_str(meta->call_de, sizeof(meta->call_de), tokens[call_de_pos].c_str());
        if (tokens.size() > call_de_pos + 1) {
            copy_str(meta->grid, sizeof(meta->grid), tokens[call_de_pos + 1].c_str());
        }
        return;
    }

    if (tokens.size() < 3) return;

    const std::string &call_to = tokens[0];
    const std::string &call_de = tokens[1];
    const std::string &payload = tokens[2];

    copy_str(meta->call_de, sizeof(meta->call_de), call_de.c_str());
    meta->to_me = callsign_is_local(ctx, call_to);

    int snr_val;
    if (ieq_str(payload, "73")) {
        meta->type = FTX_MSG_TYPE_73;
    } else if (ieq_str(payload, "RRR") || ieq_str(payload, "RR73")) {
        meta->type = FTX_MSG_TYPE_RR73;
    } else if ((payload.size() >= 2) && upch(payload[0]) == 'R' &&
               ((payload[1] == '+') || (payload[1] == '-')) &&
               parse_snr(payload.substr(1), &snr_val)) {
        meta->type = FTX_MSG_TYPE_R_REPORT;
        meta->remote_snr = snr_val;
    } else if (!payload.empty() && ((payload[0] == '+') || (payload[0] == '-')) &&
               parse_snr(payload, &snr_val)) {
        meta->type = FTX_MSG_TYPE_REPORT;
        meta->remote_snr = snr_val;
    } else if (qth_grid_check(payload.c_str())) {
        meta->type = FTX_MSG_TYPE_GRID;
        copy_str(meta->grid, sizeof(meta->grid), payload.c_str());
    }
}

void ftx_qso_on_decoded_messages(const ftx_qso_context_t *ctx,
                                 const ftx_decoded_msg_t *msgs,
                                 size_t msg_count,
                                 bool rx_odd,
                                 ftx_qso_response_t *response) {
    pthread_mutex_lock(&engine_mutex);
    decide(ctx, msgs, msg_count, rx_odd, response);
    pthread_mutex_unlock(&engine_mutex);
}

void ftx_qso_on_user_message(const ftx_qso_context_t *ctx,
                             const char *text,
                             int snr,
                             float freq_hz,
                             bool rx_odd,
                             ftx_qso_response_t *response) {
    init_response(rx_odd, response);
    if (!ctx || !ctx->local_callsign || ctx->local_callsign[0] == '\0' ||
        !text || text[0] == '\0' || !response) {
        return;
    }

    pthread_mutex_lock(&engine_mutex);

    ftx_msg_meta_t meta;
    ftx_qso_parse_rx_text(ctx, text, snr, freq_hz, 0.0f, &meta);
    if (meta.call_de[0] == '\0' || ieq(meta.call_de, ctx->local_callsign)) {
        pthread_mutex_unlock(&engine_mutex);
        return;
    }

    /* Arm the manual target: callsign + parity lock, fresh sticky. The
     * clicked message seeds the sticky slot so a missed TX window is
     * regenerated at the next target-parity slot end. */
    g_state.has_target = true;
    g_state.target_odd = rx_odd;
    copy_str(g_state.target_call, sizeof(g_state.target_call), meta.call_de);
    sticky_clear();

    Candidate cand;
    if (compute_one(ctx, text, snr, freq_hz, &cand)) {
        if ((meta.type != FTX_MSG_TYPE_RR73) && (meta.type != FTX_MSG_TYPE_73)) {
            sticky_store(text, snr, freq_hz);
        }
        copy_str(g_state.last_call, sizeof(g_state.last_call), cand.call);
        emit_candidate(ctx, &cand, false, response);
    }

    pthread_mutex_unlock(&engine_mutex);
}

size_t ftx_qso_flush_complete(ftx_qso_record_t *records, size_t max) {
    if (!records || (max == 0)) return 0;

    pthread_mutex_lock(&engine_mutex);

    size_t n = 0;
    for (int i = 0; (i < g_state.peers_len) && (n < max); i++) {
        PeerEntry *peer = &g_state.peers[i];
        if (!peer_qso_complete(peer)) continue;
        /* No final RR73/73 was ever seen, so there is no real end moment:
         * approximate it as start + 5 minutes (a typical FT8 QSO span).
         * qso_start is always set here — rst_sent is only armed by a real
         * transmission, which fills the start time. */
        peer_fill_record(peer, peer->qso_start + 300, &records[n++]);
        peer_close_qso(peer);
    }

    pthread_mutex_unlock(&engine_mutex);
    return n;
}

void ftx_qso_reset(void) {
    pthread_mutex_lock(&engine_mutex);
    std::memset(&g_state, 0, sizeof(g_state));
    pthread_mutex_unlock(&engine_mutex);
}

void ftx_qso_clear_decision_state(void) {
    pthread_mutex_lock(&engine_mutex);

    g_state.has_target     = false;
    g_state.target_call[0] = '\0';
    sticky_clear();

    g_state.last_call[0]   = '\0';
    g_state.blacklist_len  = 0;
    g_state.blacklist_next = 0;

    pthread_mutex_unlock(&engine_mutex);
}

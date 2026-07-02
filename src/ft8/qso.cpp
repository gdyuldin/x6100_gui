#include "qso.h"

extern "C" {
#include "../qth/qth.h"
#include "utils.h"
}

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

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

void normalize_dxp_tokens(const ftx_qso_context_t *ctx, std::vector<std::string> &tokens, const std::string &text) {
    if ((tokens.size() < 5) || (text.find(';') == text.npos)) return;

    std::vector<std::string> new_tokens;
    const char *local_callsign = (ctx && ctx->local_callsign) ? ctx->local_callsign : "";
    if (tokens[0] == local_callsign) {
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
    return ctx && ctx->local_callsign && callsign == ctx->local_callsign;
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

void fill_tx(ftx_qso_response_t *response, const char *msg) {
    if (!response || !msg || msg[0] == '\0') return;
    response->action = FTX_QSO_ACTION_TX;
    copy_str(response->tx_msg, sizeof(response->tx_msg), msg);
}

void make_grid_reply(const ftx_qso_context_t *ctx, const char *call, ftx_qso_response_t *response) {
    char msg[35];
    std::snprintf(msg, sizeof(msg), "%s %s %s",
                  call,
                  ctx && ctx->local_callsign ? ctx->local_callsign : "",
                  local_qth4(ctx).c_str());
    fill_tx(response, msg);
}

} /* namespace */

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

    if (tokens[0] == "CQ") {
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
    if (payload == "73") {
        meta->type = FTX_MSG_TYPE_73;
    } else if ((payload == "RRR") || (payload == "RR73")) {
        meta->type = FTX_MSG_TYPE_RR73;
    } else if ((payload.size() >= 2) && payload[0] == 'R' &&
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
    (void)ctx;
    (void)msgs;
    (void)msg_count;
    init_response(rx_odd, response);

    /* Phase 1 insertion point: once the stateless/manual path is stable, the
     * next QSO engine can inspect the complete decoded slot here. */
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

    ftx_msg_meta_t meta;
    ftx_qso_parse_rx_text(ctx, text, snr, freq_hz, 0.0f, &meta);
    if (meta.call_de[0] == '\0') return;
    response->freq_hz = meta.freq_hz;

    if (!meta.to_me || meta.type == FTX_MSG_TYPE_CQ) {
        make_grid_reply(ctx, meta.call_de, response);
        return;
    }

    char msg[35];
    switch (meta.type) {
        case FTX_MSG_TYPE_GRID:
            std::snprintf(msg, sizeof(msg), "%s %s %+03d",
                          meta.call_de, ctx->local_callsign, snr);
            fill_tx(response, msg);
            break;
        case FTX_MSG_TYPE_REPORT:
            std::snprintf(msg, sizeof(msg), "%s %s R%+03d",
                          meta.call_de, ctx->local_callsign, snr);
            fill_tx(response, msg);
            break;
        case FTX_MSG_TYPE_R_REPORT:
            std::snprintf(msg, sizeof(msg), "%s %s RR73",
                          meta.call_de, ctx->local_callsign);
            fill_tx(response, msg);
            break;
        case FTX_MSG_TYPE_RR73:
            std::snprintf(msg, sizeof(msg), "%s %s 73",
                          meta.call_de, ctx->local_callsign);
            fill_tx(response, msg);
            break;
        case FTX_MSG_TYPE_73:
        default:
            break;
    }
}

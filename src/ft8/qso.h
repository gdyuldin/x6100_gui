#pragma once

#include <stdbool.h>
#include <stddef.h>

typedef enum {
    // "CQ CALL ..." or "CQ DX CALL ..." or "CQ EU CALL ..."
    FTX_MSG_TYPE_CQ,
    // "CALL1 CALL2 GRID"
    FTX_MSG_TYPE_GRID,
    // "CALL1 CALL2 +1"
    FTX_MSG_TYPE_REPORT,
    // "CALL1 CALL2 R+1"
    FTX_MSG_TYPE_R_REPORT,
    // "CALL1 CALL2 RR73"
    FTX_MSG_TYPE_RR73,
    // "CALL1 CALL2 73"
    FTX_MSG_TYPE_73,

    FXT_MSG_TYPE_OTHER,
} ftx_msg_type_t;

typedef struct {
    char           grid[9];
    char           call_de[13];
    int            local_snr;
    int            remote_snr;
    bool           to_me;
    float          freq_hz;
    float          time_sec;
    ftx_msg_type_t type;
} ftx_msg_meta_t;

typedef struct {
    char msg[35];
    int  repeats;
} ftx_tx_msg_t;

typedef struct {
    const char *local_callsign;
    const char *local_qth;
} ftx_qso_context_t;

typedef struct {
    const char *text;
    int         snr;
    float       freq_hz;
    float       time_sec;
    bool        odd;
} ftx_decoded_msg_t;

typedef enum {
    FTX_QSO_ACTION_RX = 0,
    FTX_QSO_ACTION_TX,
} ftx_qso_action_t;

/* Engine output for exactly one upcoming slot. The caller transmits tx_msg
 * once in the next slot of tx_odd parity and then waits for the next engine
 * response; any retry/repeat policy lives inside the engine. */
typedef struct {
    ftx_qso_action_t action;
    bool  tx_odd;
    char  tx_msg[35];
    /* Peer frequency for finder cursor / QSY; 0 = keep current. */
    float freq_hz;
} ftx_qso_response_t;

#ifdef __cplusplus
#include <string>
#include <vector>

std::vector<std::string> split_text(std::string text);

extern "C" {
#endif

void ftx_qso_parse_rx_text(const ftx_qso_context_t *ctx,
                           const char *text, int snr,
                           float freq_hz, float time_sec,
                           ftx_msg_meta_t *meta);

void ftx_qso_on_decoded_messages(const ftx_qso_context_t *ctx,
                                 const ftx_decoded_msg_t *msgs,
                                 size_t msg_count,
                                 bool rx_odd,
                                 ftx_qso_response_t *response);

void ftx_qso_on_user_message(const ftx_qso_context_t *ctx,
                             const char *text,
                             int snr,
                             float freq_hz,
                             bool rx_odd,
                             ftx_qso_response_t *response);

#ifdef __cplusplus
}
#endif

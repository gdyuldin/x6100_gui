/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI - FT8 CQ scheduler
 *
 *  Copyright (c) 2026
 */

#include "cq_scheduler.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <ft8lib/constants.h>
#include <ft8lib/message.h>

#include "../cfg/cfg.h"
#include "../msg.h"
#include "../params/params.h"

#define MAX_TX_START_DELAY_SEC  1.5f

void cq_state_reset(cq_state_t *cq) {
    if (!cq) return;
    cq->paused_for_qso = false;
}

void cq_make_message(const char *callsign,
                     const char *qth,
                     const char *cq_mod,
                     char *out, size_t out_sz,
                     bool omit_qth) {
    if (!out || out_sz == 0) return;
    if (!callsign) callsign = "";
    if (!qth)      qth      = "";
    if (!cq_mod)   cq_mod   = "";

    if (cq_mod[0] != '\0') {
        snprintf(out, out_sz, "CQ_%s %s", cq_mod, callsign);
    } else {
        snprintf(out, out_sz, "CQ %s", callsign);
    }

    if (!omit_qth) {
        size_t len = strlen(out);
        if (len + 6 < out_sz) {
            snprintf(out + len, out_sz - len, " %.4s", qth);
        }
    }

    /* Validate the message fits in a 77-bit FT8 frame.
     * If callsign+grid is too long, ftx_message_encode will fail. */
    ftx_message_t msg;
    ftx_message_rc_t rc = ftx_message_encode(&msg, NULL, out);
    if (rc != FTX_MESSAGE_RC_OK) {
        LV_LOG_USER("CQ message too long for FT8: '%s' (rc=%d)", out, rc);
        out[0] = '\0';
    }
}

static bool get_time_slot_odd(ftx_protocol_t proto, float *sec_since_start) {
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    float sec       = (float)(now.tv_sec % 60) + (float)now.tv_nsec / 1.0e9f;
    float slot_time = (proto == FTX_PROTOCOL_FT4) ? FT4_SLOT_TIME : FT8_SLOT_TIME;
    *sec_since_start = fmodf(sec, slot_time);
    return ((int)(sec / slot_time) % 2) != 0;
}

bool cq_schedule_if_needed(cq_state_t   *cq,
                           qso_state_t  *qso,
                           Subject      *cq_enabled_subj,
                           Subject      *tx_enabled_subj,
                           bool          ftx_qso_has_current) {
    (void)cq;
    if (!qso) return false;
    if (!cq_enabled_subj || !subject_get_int(cq_enabled_subj)) return false;
    if (!tx_enabled_subj || !subject_get_int(tx_enabled_subj)) return false;

    if (!qso_state_tx_msg_empty(qso)) return false;
    if (ftx_qso_has_current)          return false;
    if (strlen(params.callsign.x) == 0) return false;

    ftx_tx_msg_t m = {0};
    cq_make_message(params.callsign.x,
                    params.qth.x,
                    params.ft8_cq_modifier.x,
                    m.msg, sizeof(m.msg),
                    subject_get_int(cfg.ft8_omit_cq_qth.val));
    m.repeats         = subject_get_int(cfg.ft8_max_repeats.val);
    m.force_free_text = false;

    qso_state_set_tx_msg(qso, &m);

    ftx_protocol_t proto = subject_get_int(cfg.ft8_protocol.val);
    float sec_since_start = 0.0f;
    bool  cur_odd = get_time_slot_odd(proto, &sec_since_start);
    bool  tx_time_slot = !cur_odd;
    if (sec_since_start < MAX_TX_START_DELAY_SEC) {
        tx_time_slot = !tx_time_slot;
    }
    qso_state_set_tx_time_slot(qso, tx_time_slot);

    if (m.msg[2] == '_') {
        msg_schedule_text_fmt("Next TX: CQ %s", m.msg + 3);
    } else {
        msg_schedule_text_fmt("Next TX: %s", m.msg);
    }
    return true;
}

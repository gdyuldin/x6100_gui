/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI - FT8 CQ scheduler
 *
 *  Copyright (c) 2026
 */

#include "cq_scheduler.h"

#include <stdio.h>
#include <string.h>

#include <ft8lib/message.h>

#include "lvgl/lvgl.h"

void cq_make_message(const char *callsign,
                     const char *qth,
                     const char *cq_mod,
                     char       *out) {
    char buf[128];
    if (strlen(cq_mod)) {
        snprintf(buf, FTX_MAX_MESSAGE_LENGTH, "CQ_%s %s %.4s", cq_mod, callsign, qth);
    } else {
        snprintf(buf, FTX_MAX_MESSAGE_LENGTH, "CQ %s %.4s", callsign, qth);
    }

    ftx_message_rc_t rc;
    ftx_message_t    msg;
    rc = ftx_message_encode(&msg, NULL, buf);
    if (rc != FTX_MESSAGE_RC_OK) {
        LV_LOG_USER("Error: %d while encoding message: '%s'", rc, buf);
    }
    rc = ftx_message_decode(&msg, NULL, buf);
    if (rc != FTX_MESSAGE_RC_OK) {
        LV_LOG_USER("Error: %d while decoding message: '%s'", rc, buf);
    }
    strcpy(out, buf);
}

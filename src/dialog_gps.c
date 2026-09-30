/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */
#include "dialog_gps.h"

#include "gps.h"
#include "dialog.h"
#include "styles.h"
#include "events.h"
#include "pubsub_ids.h"
#include "radio.h"
#include "keyboard.h"
#include "qth/qth.h"
#include "cfg/cfg_api.h"
#include "msg.h"
#include "lvgl/lvgl.h"

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <math.h>

#define HEIGHT 42
#define LABEL_WIDTH 300

static void construct_cb(lv_obj_t *parent);
static void destruct_cb();
static void key_cb(lv_event_t * e);

typedef enum {
    deg_dd,
    deg_ddmm,
    deg_ddmmss
} deg_str_type;

static deg_str_type         deg_type = deg_ddmm;

static lv_obj_t             *satellites_cnt;
static lv_obj_t             *fix;
static lv_obj_t             *date;
static lv_obj_t             *lat;
static lv_obj_t             *lon;
static lv_obj_t             *qth;
static lv_obj_t             *status;

static lv_timer_t           *status_update_timer;

static void                 *gps_sub;

static dialog_t             dialog = {
    .run = false,
    .construct_cb = construct_cb,
    .destruct_cb = destruct_cb,
    .key_cb = key_cb
};

dialog_t                    *dialog_gps = &dialog;

char *deg_to_str2(deg_str_type type, double f, char *buf, unsigned int buf_size, const char *suffix_pos, const char *suffix_neg) {
    int dsec, sec, deg, min;
    double fdsec, fsec, fdeg, fmin;
    const char *suffix = "";

    if (20 > buf_size) {
        strncpy(buf, "Err", buf_size);
        return buf;
    }

    if (!isfinite(f) || 360.0 < fabs(f)) {
        strncpy(buf, "N/A", buf_size);
        return buf;
    }

    /* suffix? */
    if (0.0 > f) {
        f = -f;
        if (NULL != suffix_neg) {
            suffix = suffix_neg;
        }
    } else if (NULL != suffix_pos) {
        suffix = suffix_pos;
    }

    /* add rounding quanta */
    /* IEEE 754 wants round to nearest even.
     * We cheat and just round to nearest.
     * Intel trying to kill off round to nearest even. */
    switch (type) {
        default:
            /* huh? */
            type = deg_dd;
            /* It's not worth battling fallthrough warnings just for two lines */
            f += 0.5 * 1e-8;              /* round up */
            break;

        case deg_dd:
            /* DD.dddddddd */
            f += 0.5 * 1e-8;              /* round up */
            break;

        case deg_ddmm:
            /* DD MM.mmmmmm */
            f += (0.5 * 1e-6) / 60;       /* round up */
            break;

        case deg_ddmmss:
            f += (0.5 * 1e-5) / 3600;     /* round up */
            break;
    }

    fmin = modf(f, &fdeg);
    deg = (int)fdeg;

    if (360 == deg) {
        /* fix round-up roll-over */
        deg = 0;
        fmin = 0.0;
    }

    if (deg_dd == type) {
        /* DD.dddddddd */
        long frac_deg = (long)(fmin * 100000000.0);
        /* cm level accuracy requires the %08ld */
        snprintf(buf, buf_size, "%d.%08ld%s", deg, frac_deg, suffix);
        return buf;
    }

    fsec = modf(fmin * 60, &fmin);
    min = (int)fmin;

    if (deg_ddmm == type) {
        /* DD MM.mmmmmm */
        sec = (int)(fsec * 1000000.0);
        snprintf(buf, buf_size, "%d %s %02d.%06d'", deg, suffix, min, sec);
        return buf;
    }

    /* else DD MM SS.sss */
    fdsec = modf(fsec * 60.0, &fsec);
    sec = (int)fsec;
    dsec = (int)(fdsec * 100000.0);
    snprintf(buf, buf_size, "%d %s %02d' %02d.%05d\"", deg, suffix, min, sec, dsec);

    return buf;
}

static void gps_cb(void *s, lv_msg_t *m) {
    struct gps_data_t   data;
    char                str[64];

    gps_get_snapshot(&data);

    if (data.set & SATELLITE_SET) {
        lv_label_set_text_fmt(satellites_cnt, "%i/%i", data.satellites_visible, data.satellites_used);
    }

    switch (data.fix.mode) {
        case MODE_3D:
            lv_label_set_text(fix, "3D");
            break;

        case MODE_2D:
            lv_label_set_text(fix, "2D");
            break;

        case MODE_NO_FIX:
            lv_label_set_text(fix, "None");
            break;

        default:
            break;
    }

    if (data.set & TIME_SET) {
        timespec_to_iso8601(data.fix.time, str, sizeof(str));
        lv_label_set_text(date, str);
    } else {
        lv_label_set_text(date, "N/A");
    }

    if (data.fix.mode >= MODE_2D) {
        deg_to_str2(deg_type, data.fix.latitude, str, sizeof(str), "N", "S");
        lv_label_set_text(lat, str);

        deg_to_str2(deg_type, data.fix.longitude, str, sizeof(str), "E", "W");
        lv_label_set_text(lon, str);

        char qth_val[9];
        qth_pos_to_str(data.fix.latitude, data.fix.longitude, qth_val);
        lv_label_set_text(qth, qth_val);

        const char *saved_qth = PARAM_T_GET(cfg.qth());
        if ((strlen(saved_qth) == 0) || (strncmp(qth_val, saved_qth, strlen(saved_qth)) != 0)) {
            param_t_set(cfg.qth(), qth_val);
            msg_schedule_text_fmt("QTH updated: %s", PARAM_T_GET(cfg.qth()));
        }
    } else {
        lv_label_set_text(lat, "N/A");
        lv_label_set_text(lon, "N/A");
        lv_label_set_text(qth, "N/A");
    }
}

static void gps_status_update_timer(lv_timer_t * timer)
{
    static char*  gps_status_str[] = {
        "waiting",
        "working",
        "restarting",
        "exited",
    };
    lv_label_set_text_fmt(status, "%s", gps_status_str[gps_status()]);
}

static void construct_cb(lv_obj_t *parent) {
    lv_obj_t    *label;
    lv_coord_t  y = 32;

    dialog.obj = dialog_init(parent);

    lv_group_add_obj(keyboard_group, dialog.obj);
    lv_obj_add_event_cb(dialog.obj, key_cb, LV_EVENT_KEY, NULL);
    gps_sub = lv_msg_subscribe(MSG_GPS, gps_cb, NULL);

    /* Working, satellites count */

    label = lv_label_create(dialog.obj);

    lv_label_set_text(label, "Sat in view/in use:");
    lv_obj_set_size(label, LABEL_WIDTH, HEIGHT);
    lv_obj_set_pos(label, 30, y);

    satellites_cnt = lv_label_create(dialog.obj);

    lv_label_set_text(satellites_cnt, "N/A");
    lv_obj_set_size(satellites_cnt, 450, HEIGHT);
    lv_obj_set_pos(satellites_cnt, LABEL_WIDTH + 50, y);

    y += HEIGHT;

    /* Fix */

    label = lv_label_create(dialog.obj);

    lv_label_set_text(label, "Fix:");
    lv_obj_set_size(label, LABEL_WIDTH, HEIGHT);
    lv_obj_set_pos(label, 30, y);

    fix = lv_label_create(dialog.obj);

    lv_label_set_text(fix, "N/A");
    lv_obj_set_size(fix, 450, HEIGHT);
    lv_obj_set_pos(fix, LABEL_WIDTH + 50, y);

    y += HEIGHT;

    /* Date, time */

    label = lv_label_create(dialog.obj);

    lv_label_set_text(label, "Date, time:");
    lv_obj_set_size(label, LABEL_WIDTH, HEIGHT);
    lv_obj_set_pos(label, 30, y);

    date = lv_label_create(dialog.obj);

    lv_label_set_text(date, "N/A");
    lv_obj_set_size(date, 450, HEIGHT);
    lv_obj_set_pos(date, LABEL_WIDTH + 50, y);

    y += HEIGHT;

    /* Lat */

    label = lv_label_create(dialog.obj);

    lv_label_set_text(label, "Latitude:");
    lv_obj_set_size(label, LABEL_WIDTH, HEIGHT);
    lv_obj_set_pos(label, 30, y);

    lat = lv_label_create(dialog.obj);

    lv_label_set_text(lat, "N/A");
    lv_obj_set_size(lat, 450, HEIGHT);
    lv_obj_set_pos(lat, LABEL_WIDTH + 50, y);

    y += HEIGHT;

    /* Lon */

    label = lv_label_create(dialog.obj);

    lv_label_set_text(label, "Longitude:");
    lv_obj_set_size(label, LABEL_WIDTH, HEIGHT);
    lv_obj_set_pos(label, 30, y);

    lon = lv_label_create(dialog.obj);

    lv_label_set_text(lon, "N/A");
    lv_obj_set_size(lon, LABEL_WIDTH, HEIGHT);
    lv_obj_set_pos(lon, LABEL_WIDTH + 50, y);

    y += HEIGHT;

    /* QTH Grid */

    label = lv_label_create(dialog.obj);

    lv_label_set_text(label, "QTH Grid:");
    lv_obj_set_size(label, LABEL_WIDTH, HEIGHT);
    lv_obj_set_pos(label, 30, y);

    qth = lv_label_create(dialog.obj);

    lv_label_set_text(qth, "N/A");
    lv_obj_set_size(qth, 450, HEIGHT);
    lv_obj_set_pos(qth, LABEL_WIDTH + 50, y);

    y += HEIGHT;

    /* GPS status */

    label = lv_label_create(dialog.obj);

    lv_label_set_text(label, "GPS status");
    lv_obj_set_size(label, LABEL_WIDTH, HEIGHT);
    lv_obj_set_pos(label, 30, y);

    status = lv_label_create(dialog.obj);

    lv_label_set_text_fmt(status, "%s", "");
    lv_obj_set_size(status, 450, HEIGHT);
    lv_obj_set_pos(status, LABEL_WIDTH + 50, y);

    status_update_timer = lv_timer_create(gps_status_update_timer, 500,  NULL);
    lv_timer_ready(status_update_timer);
}

static void destruct_cb() {
    if (gps_sub) {
        lv_msg_unsubscribe(gps_sub);
        gps_sub = NULL;
    }
    lv_timer_del(status_update_timer);
}

static void key_cb(lv_event_t * e) {
    uint32_t key = *((uint32_t *)lv_event_get_param(e));

    switch (key) {
        case LV_KEY_ESC:
            dialog_destruct(&dialog);
            break;

        case KEY_VOL_LEFT_EDIT:
        case KEY_VOL_LEFT_SELECT:
            radio_change_vol(-1);
            break;

        case KEY_VOL_RIGHT_EDIT:
        case KEY_VOL_RIGHT_SELECT:
            radio_change_vol(1);
            break;
    }
}

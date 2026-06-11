/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI - FT8 persistent tuning parameters
 *
 *  Copyright (c) 2026
 */

#include "params.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PATH_SCAN_START     "/mnt/auto_dnf_scan_start_sec.txt"
#define PATH_SCAN_END       "/mnt/auto_dnf_scan_end_sec.txt"
#define PATH_CLEAR_TIME     "/mnt/auto_dnf_clear_time_sec.txt"
#define PATH_MIN_DELTA_DB   "/mnt/auto_notch_threshold_db.txt"

/* Slot-relative seconds are bounded by the 15s slot period with generous slack. */
#define RANGE_SEC_MIN       0.0f
#define RANGE_SEC_MAX       2.0f
#define RANGE_DB_MIN        0.0f
#define RANGE_DB_MAX        120.0f

static void write_default(const char *path, float default_val, const char *fmt) {
    FILE *wf = fopen(path, "w");
    if (!wf) return;
    fprintf(wf, fmt, (double)default_val);
    fclose(wf);
}

static bool parse_trimmed_float(const char *buf, float *out) {
    char *end = NULL;
    errno = 0;
    float v = strtof(buf, &end);
    if ((errno != 0) || (end == buf) || !isfinite(v)) {
        return false;
    }
    while (end && *end && isspace((unsigned char)*end)) {
        end++;
    }
    if ((end == NULL) || (*end != '\0')) {
        return false;
    }
    *out = v;
    return true;
}

static void load_float_txt(const char *path,
                           float default_val,
                           float min_val,
                           float max_val,
                           const char *write_fmt,
                           float *out) {
    FILE *f = fopen(path, "r");
    if (!f) {
        if (errno == ENOENT) {
            write_default(path, default_val, write_fmt);
        }
        *out = default_val;
        return;
    }

    char buf[64] = {0};
    bool ok = false;
    if (fgets(buf, sizeof(buf), f)) {
        float v;
        if (parse_trimmed_float(buf, &v) && (v >= min_val) && (v <= max_val)) {
            *out = v;
            ok = true;
        }
    }
    fclose(f);

    if (!ok) {
        *out = default_val;
        write_default(path, default_val, write_fmt);
    }
}

void ft8_params_load(ft8_tuning_t *out) {
    if (!out) return;

    load_float_txt(PATH_SCAN_START,
                   FT8_AUTO_DNF_SCAN_START_SEC_DEFAULT,
                   RANGE_SEC_MIN, RANGE_SEC_MAX, "%.3f\n",
                   &out->scan_start_sec);
    load_float_txt(PATH_SCAN_END,
                   FT8_AUTO_DNF_SCAN_END_SEC_DEFAULT,
                   RANGE_SEC_MIN, RANGE_SEC_MAX, "%.3f\n",
                   &out->scan_end_sec);
    load_float_txt(PATH_CLEAR_TIME,
                   FT8_AUTO_DNF_CLEAR_TIME_SEC_DEFAULT,
                   RANGE_SEC_MIN, RANGE_SEC_MAX, "%.3f\n",
                   &out->clear_time_sec);
    load_float_txt(PATH_MIN_DELTA_DB,
                   FT8_AUTO_DNF_MIN_DELTA_DB_DEFAULT,
                   RANGE_DB_MIN, RANGE_DB_MAX, "%.1f\n",
                   &out->min_delta_db);
}

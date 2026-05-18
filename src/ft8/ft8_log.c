/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI - FT8 file-log module
 *
 *  Copyright (c) 2026
 */

#include "ft8_log.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#define FT8_LOG_DIR        "/mnt/ft8_logs"
#define FT8_LOG_LINE_MAX   512
#define FT8_LOG_QUEUE_MAX  512

typedef struct rx_line_s {
    char              *line;
    struct rx_line_s  *next;
} rx_line_t;

static FILE      *s_fp             = NULL;
static rx_line_t *s_rx_lines       = NULL;
static size_t     s_rx_lines_count = 0;
static time_t     s_rx_slot_start  = 0;

static void format_slot_ts(time_t slot_start, char *out, size_t out_size) {
    struct tm tm_slot;
    if ((out_size == 0) || (localtime_r(&slot_start, &tm_slot) == NULL)) {
        if (out_size) out[0] = '\0';
        return;
    }
    strftime(out, out_size, "%Y-%m-%d_%H-%M-%S", &tm_slot);
}

static void rx_lines_clear(void) {
    rx_line_t *cur = s_rx_lines;
    while (cur) {
        rx_line_t *next = cur->next;
        free(cur->line);
        free(cur);
        cur = next;
    }
    s_rx_lines       = NULL;
    s_rx_lines_count = 0;
    s_rx_slot_start  = 0;
}

static void ensure_open(void) {
    if (s_fp) return;

    if ((mkdir(FT8_LOG_DIR, 0755) < 0) && (errno != EEXIST)) {
        return;
    }

    time_t    now = time(NULL);
    struct tm tm_now;
    if (localtime_r(&now, &tm_now) == NULL) {
        return;
    }

    char ts[32];
    if (strftime(ts, sizeof(ts), "%Y-%m-%d_%H-%M-%S", &tm_now) == 0) {
        return;
    }

    char path[256];
    snprintf(path, sizeof(path), "%s/x6100_log_%s.txt", FT8_LOG_DIR, ts);
    s_fp = fopen(path, "a");
}

void ft8_log_init(void) {
    /* Lazy-open on first write; init here just guarantees a clean slate. */
    ft8_log_close();
}

void ft8_log_close(void) {
    if (s_fp) {
        if (s_rx_lines) {
            ft8_log_rx_flush_slot(NULL);
        }
        fflush(s_fp);
        fclose(s_fp);
        s_fp = NULL;
    }
    rx_lines_clear();
}

void ft8_log_rx_collect(const slot_info_t *info, float freq_hz, int snr, const char *text) {
    if (!info || !text) return;
    ensure_open();
    if (!s_fp) return;

    if (s_rx_slot_start != info->slot_start) {
        rx_lines_clear();
        s_rx_slot_start = info->slot_start;
    }

    char slot_ts[32];
    format_slot_ts(info->slot_start, slot_ts, sizeof(slot_ts));

    uint64_t rf_hz = info->base_freq_hz + (uint64_t)llroundf(freq_hz);

    char line_buf[FT8_LOG_LINE_MAX];
    snprintf(line_buf, sizeof(line_buf),
             "RX,%s,%llu,%d,%s",
             slot_ts,
             (unsigned long long)rf_hz,
             snr,
             text);

    for (rx_line_t *cur = s_rx_lines; cur; cur = cur->next) {
        if (strcmp(cur->line, line_buf) == 0) {
            return; /* dedup */
        }
    }

    if (s_rx_lines_count >= FT8_LOG_QUEUE_MAX) {
        return;
    }

    char *dup = strdup(line_buf);
    if (!dup) return;

    rx_line_t *node = (rx_line_t *)malloc(sizeof(rx_line_t));
    if (!node) {
        free(dup);
        return;
    }
    node->line = dup;
    node->next = s_rx_lines;
    s_rx_lines = node;
    s_rx_lines_count++;
}

void ft8_log_rx_flush_slot(const slot_info_t *info) {
    (void)info;
    if (!s_fp || !s_rx_lines) {
        rx_lines_clear();
        return;
    }

    /* In-place reverse so oldest-first. The collect queue is prepended, so
     * s_rx_lines is currently newest-first.
     */
    rx_line_t *rev = NULL;
    rx_line_t *cur = s_rx_lines;
    while (cur) {
        rx_line_t *next = cur->next;
        cur->next = rev;
        rev = cur;
        cur = next;
    }
    s_rx_lines = rev;

    for (cur = s_rx_lines; cur; cur = cur->next) {
        fprintf(s_fp, "%s\n", cur->line);
    }
    fflush(s_fp);
    rx_lines_clear();
}

void ft8_log_tx(time_t slot_start, uint64_t base_freq_hz, uint32_t hz_offset, const char *text) {
    if (!text) return;
    ensure_open();
    if (!s_fp) return;

    char slot_ts[32];
    format_slot_ts(slot_start, slot_ts, sizeof(slot_ts));

    uint64_t rf_hz = base_freq_hz + (uint64_t)hz_offset;

    char msg_buf[96];
    strncpy(msg_buf, text, sizeof(msg_buf) - 1);
    msg_buf[sizeof(msg_buf) - 1] = '\0';
    if (strncmp(msg_buf, "CQ_", 3) == 0) {
        msg_buf[2] = ' ';
    }

    fprintf(s_fp, "TX,%s,%llu,%s\n", slot_ts, (unsigned long long)rf_hz, msg_buf);
    fflush(s_fp);
}

void ft8_log_qso(time_t slot_start, const char *remote_call) {
    if (!remote_call || (remote_call[0] == '\0')) return;
    ensure_open();
    if (!s_fp) return;

    char slot_ts[32];
    format_slot_ts(slot_start, slot_ts, sizeof(slot_ts));
    fprintf(s_fp, "QSO,%s,%s\n", slot_ts, remote_call);
    fflush(s_fp);
}

void ft8_log_dnf(time_t slot_start, uint16_t center_hz, float delta_db) {
    ensure_open();
    if (!s_fp) return;

    char slot_ts[32];
    format_slot_ts(slot_start, slot_ts, sizeof(slot_ts));
    fprintf(s_fp, "DNF,%s,%u,%.1f\n", slot_ts, (unsigned)center_hz, (double)delta_db);
    fflush(s_fp);
}

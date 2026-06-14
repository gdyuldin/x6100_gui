/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI - FT8 File-Log Module (RX/TX only)
 *
 *  Copyright (c) 2026
 *
 *  Writes RX and TX lines to /mnt/ft8_logs/x6100_log_<ts>.txt
 *  during FT8 sessions. No QSO/DNF/AutoSel integration.
 */

#include "ft8_log.h"

#include "cfg/cfg.h"
#include "cfg/subjects.h"
#include "params/params.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

#define LOG_DIR           "/mnt/ft8_logs"
#define RX_QUEUE_MAX      512

typedef struct rx_node_s {
    char             text[35];
    int              snr;
    float            freq_hz;
    float            time_sec;
    struct rx_node_s *next;
} rx_node_t;

static FILE       *s_file = NULL;
static time_t      s_slot_start;
static uint64_t    s_slot_base_freq;
static rx_node_t  *s_rx_head = NULL;
static uint16_t    s_rx_count = 0;

static const char *slot_ts_str(time_t slot_start, char *buf, size_t bufsz) {
    if (slot_start == 0 || bufsz < 20) return NULL;
    struct tm tm;
    localtime_r(&slot_start, &tm);
    snprintf(buf, bufsz, "%04d-%02d-%02d_%02d-%02d-%02d",
             tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
             tm.tm_hour, tm.tm_min, tm.tm_sec);
    return buf;
}

static void ensure_file_open(void) {
    if (s_file) return;

    FILE *test = fopen(LOG_DIR "/.test_write", "w");
    if (!test) return;
    fclose(test);
    remove(LOG_DIR "/.test_write");

    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    char path[128];
    snprintf(path, sizeof(path),
             LOG_DIR "/x6100_log_%04d%02d%02d_%02d%02d%02d.txt",
             tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
             tm.tm_hour, tm.tm_min, tm.tm_sec);
    s_file = fopen(path, "a");
}

static void sanitize_tx_text(char *text) {
    if (strncmp(text, "CQ_", 3) == 0)
        text[2] = ' ';
}

static void rx_queue_clear(void) {
    rx_node_t *cur = s_rx_head;
    while (cur) {
        rx_node_t *next = cur->next;
        free(cur);
        cur = next;
    }
    s_rx_head = NULL;
    s_rx_count = 0;
}

static bool rx_queue_has(const char *text) {
    for (rx_node_t *cur = s_rx_head; cur; cur = cur->next) {
        if (strcmp(cur->text, text) == 0)
            return true;
    }
    return false;
}

static void ft8_log_rx_collect(const slot_info_t *info,
                               uint64_t base_freq,
                               float freq_hz, int snr,
                               const char *text) {
    if (!info || !text || !text[0]) return;

    ensure_file_open();

    if (info->slot_start != s_slot_start) {
        rx_queue_clear();
        s_slot_start = info->slot_start;
        s_slot_base_freq = base_freq;
    }

    if (rx_queue_has(text)) return;

    if (s_rx_count >= RX_QUEUE_MAX) {
        rx_node_t *old = s_rx_head;
        s_rx_head = old->next;
        free(old);
        s_rx_count--;
    }

    rx_node_t *node = (rx_node_t *)calloc(1, sizeof(rx_node_t));
    if (!node) return;
    strncpy(node->text, text, sizeof(node->text) - 1);
    node->snr      = snr;
    node->freq_hz  = freq_hz;
    node->time_sec = 0.0f;
    node->next     = s_rx_head;
    s_rx_head      = node;
    s_rx_count++;
}

static void ft8_log_rx_flush_slot(const slot_info_t *info) {
    if (!s_rx_head) return;

    ensure_file_open();
    if (!s_file) return;

    char ts_buf[24];
    const char *ts = slot_ts_str(info ? info->slot_start : s_slot_start,
                                 ts_buf, sizeof(ts_buf));
    if (!ts) return;

    rx_node_t *reversed = NULL;
    {
        rx_node_t *cur = s_rx_head;
        while (cur) {
            rx_node_t *n = (rx_node_t *)calloc(1, sizeof(rx_node_t));
            if (!n) break;
            memcpy(n->text, cur->text, sizeof(n->text));
            n->snr     = cur->snr;
            n->freq_hz = cur->freq_hz;
            n->next    = reversed;
            reversed   = n;
            cur = cur->next;
        }
    }

    uint64_t base = s_slot_base_freq;
    for (rx_node_t *n = reversed; n; n = n->next) {
        uint64_t rf_hz = base + (uint64_t)llroundf(n->freq_hz);
        fprintf(s_file, "RX,%s,%llu,%d,%s\n",
                ts, (unsigned long long)rf_hz, n->snr, n->text);
    }

    {
        rx_node_t *cur = reversed;
        while (cur) {
            rx_node_t *next = cur->next;
            free(cur);
            cur = next;
        }
    }

    fflush(s_file);
    rx_queue_clear();
}

static void ft8_log_tx(time_t slot_start, uint64_t base_freq,
                       int hz_offset, const char *text) {
    if (!text || !text[0]) return;
    ensure_file_open();
    if (!s_file) return;

    char ts_buf[24];
    const char *ts = slot_ts_str(slot_start, ts_buf, sizeof(ts_buf));
    if (!ts) return;

    uint64_t rf_hz = base_freq + (uint64_t)hz_offset;

    char tx_text[35];
    strncpy(tx_text, text, sizeof(tx_text) - 1);
    tx_text[sizeof(tx_text) - 1] = '\0';
    sanitize_tx_text(tx_text);

    fprintf(s_file, "TX,%s,%llu,%s\n",
            ts, (unsigned long long)rf_hz, tx_text);
    fflush(s_file);
}

void ft8_log_on_init(void) {
    rx_queue_clear();
    s_slot_start = 0;
    s_slot_base_freq = 0;
    ensure_file_open();
}

void ft8_log_on_cleanup(void) {
    if (s_rx_head) {
        static slot_info_t dummy;
        memset(&dummy, 0, sizeof(dummy));
        dummy.slot_start = s_slot_start;
        ft8_log_rx_flush_slot(&dummy);
    }

    if (s_file) {
        fclose(s_file);
        s_file = NULL;
    }
}

void ft8_log_on_rx_msg(const char *text, int snr, float freq_hz, float time_sec,
                       const ftx_msg_meta_t *meta, const slot_info_t *info) {
    (void)time_sec;
    (void)meta;
    uint64_t base = (uint64_t)subject_get_int(cfg_cur.fg_freq);
    ft8_log_rx_collect(info, base, freq_hz, snr, text);
}

void ft8_log_on_slot_end(const slot_info_t *info) {
    ft8_log_rx_flush_slot(info);
}

void ft8_log_on_pre_tx(const slot_info_t *info, const char *tx_text) {
    if (!info) return;
    uint64_t base = (uint64_t)subject_get_int(cfg_cur.fg_freq);
    int hz_offset = (int)params.ft8_tx_freq.x;
    ft8_log_tx(info->slot_start, base, hz_offset, tx_text);
}

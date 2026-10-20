/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Xiegu X6100 - diagnostic logs (1KO125, Hany El Imam 2026), see diag_log.h
 */

#include "diag_log.h"

#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#define PTT_LOG         "/mnt/ptt.log"
#define DIAG_MAX_BYTES  (512 * 1024)
#define DIAG_MAX_PER_S  10          /* lines per second and file, the rest only counted */

static __thread const char *thread_source = NULL;
static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

/* lines written in the current second, per file */
typedef struct {
    time_t   sec;
    int      n;
    unsigned skipped;
} rate_t;
static rate_t rate_ptt;

/* ---- writing ------------------------------------------------------------------- */

static void vlog_to(const char *path, rate_t *rate, int max_per_s, const char *fmt, va_list ap)
{
    char line[512];
    struct timespec ts;
    struct tm tm;

    clock_gettime(CLOCK_REALTIME, &ts);
    gmtime_r(&ts.tv_sec, &tm);
    int n = (int)strftime(line, sizeof(line), "%Y-%m-%d %H:%M:%S", &tm);
    n += snprintf(line + n, sizeof(line) - (size_t)n, ".%03ld UTC  ", ts.tv_nsec / 1000000);
    vsnprintf(line + n, sizeof(line) - (size_t)n, fmt, ap);
    size_t len = strlen(line);
    while (len && (line[len - 1] == '\n' || line[len - 1] == '\r')) line[--len] = '\0';

    pthread_mutex_lock(&log_mutex);
    /* a burst must not wear the SD card: DIAG_MAX_PER_S lines a second */
    if (rate->sec != ts.tv_sec) {
        rate->sec = ts.tv_sec;
        rate->n   = 0;
    }
    if (++rate->n > max_per_s) {
        rate->skipped++;
        pthread_mutex_unlock(&log_mutex);
        return;
    }
    if (rate->skipped) {
        snprintf(line + len, sizeof(line) - len, "  (%u lines skipped before this one)", rate->skipped);
        len = strlen(line);
        rate->skipped = 0;
    }
    struct stat st;
    if (stat(path, &st) == 0 && st.st_size > DIAG_MAX_BYTES) {
        char old[64];
        snprintf(old, sizeof(old), "%s.old", path);
        rename(path, old);
    }
    int fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd >= 0) {
        if (len > sizeof(line) - 2) len = sizeof(line) - 2;
        line[len++] = '\n';
        ssize_t w = write(fd, line, len);
        (void)w;
        fsync(fd);              /* the next event may be a crash or a power cut */
        close(fd);
    }
    pthread_mutex_unlock(&log_mutex);
}

void diag_ptt_log(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vlog_to(PTT_LOG, &rate_ptt, DIAG_MAX_PER_S, fmt, ap);
    va_end(ap);
}

void diag_set_source(const char *name)
{
    thread_source = name;
}

const char *diag_source(void)
{
    static __thread char buf[32];
    if (thread_source) return thread_source;
    snprintf(buf, sizeof(buf), "thread %ld", (long)syscall(SYS_gettid));
    return buf;
}

void diag_init(void)
{
    diag_set_source("UI");              /* the thread of main(): LVGL, keys */
    diag_ptt_log("---- start (firmware running) ----");
}

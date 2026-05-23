#include "tx_log.h"

#include <errno.h>
#include <execinfo.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#define TX_LOG_DIR "/mnt/tx_logs"
#define TX_LOG_STACK_DEPTH 16

typedef struct tx_log_item_s {
    struct timespec ts;
    int32_t freq_hz;
    int32_t mode;
    float pwr;
    char event[32];
    char detail[128];
    int stack_size;
    void *stack[TX_LOG_STACK_DEPTH];
    struct tx_log_item_s *next;
} tx_log_item_t;

static pthread_mutex_t tx_log_queue_mux = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t tx_log_queue_cond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t tx_log_file_mux = PTHREAD_MUTEX_INITIALIZER;

static tx_log_item_t *tx_log_head = NULL;
static tx_log_item_t *tx_log_tail = NULL;

static FILE *tx_log_fp = NULL;
static bool tx_log_ready = false;

static void tx_log_open_file(void) {
    if (tx_log_fp) {
        return;
    }

    if (mkdir(TX_LOG_DIR, 0755) < 0) {
        if (errno != EEXIST) {
            return;
        }
    }

    time_t now = time(NULL);
    struct tm tm_now;
    if (localtime_r(&now, &tm_now) == NULL) {
        return;
    }

    char ts[32];
    if (strftime(ts, sizeof(ts), "%Y-%m-%d_%H-%M-%S", &tm_now) == 0) {
        return;
    }

    char path[256];
    snprintf(path, sizeof(path), "%s/tx_log_%s.txt", TX_LOG_DIR, ts);

    tx_log_fp = fopen(path, "a");
}

static void tx_log_write_item(tx_log_item_t *item) {
    if (!item) {
        return;
    }

    pthread_mutex_lock(&tx_log_file_mux);
    if (!tx_log_fp) {
        tx_log_open_file();
    }

    if (tx_log_fp) {
        struct tm tm_now;
        char ts[64] = {0};
        if (localtime_r(&item->ts.tv_sec, &tm_now)) {
            char base_ts[32];
            if (strftime(base_ts, sizeof(base_ts), "%Y-%m-%d %H:%M:%S", &tm_now) > 0) {
                snprintf(ts, sizeof(ts), "%s.%03ld", base_ts, item->ts.tv_nsec / 1000000L);
            }
        }

        fprintf(tx_log_fp,
            "ts=%s event=%s freq=%d mode=%d pwr=%.2f detail=%s\n",
            ts[0] ? ts : "unknown",
            item->event,
            item->freq_hz,
            item->mode,
            item->pwr,
            item->detail[0] ? item->detail : "-"
        );

        if (item->stack_size > 0) {
            char **symbols = backtrace_symbols(item->stack, item->stack_size);
            if (symbols) {
                for (int i = 0; i < item->stack_size; i++) {
                    fprintf(tx_log_fp, "  %s\n", symbols[i]);
                }
                free(symbols);
            }
        }

        fprintf(tx_log_fp, "\n");
        fflush(tx_log_fp);
    }
    pthread_mutex_unlock(&tx_log_file_mux);
}

static void *tx_log_thread(void *arg) {
    (void)arg;
    while (true) {
        pthread_mutex_lock(&tx_log_queue_mux);
        while (!tx_log_head) {
            pthread_cond_wait(&tx_log_queue_cond, &tx_log_queue_mux);
        }
        tx_log_item_t *item = tx_log_head;
        tx_log_head = item->next;
        if (!tx_log_head) {
            tx_log_tail = NULL;
        }
        pthread_mutex_unlock(&tx_log_queue_mux);

        tx_log_write_item(item);
        free(item);
    }
    return NULL;
}

void tx_log_init(void) {
    if (tx_log_ready) {
        return;
    }

    tx_log_open_file();
    pthread_t thread;
    if (pthread_create(&thread, NULL, tx_log_thread, NULL) == 0) {
        pthread_detach(thread);
        tx_log_ready = true;
    }
}

void tx_log_event(const char *event, int32_t freq_hz, int32_t mode, float pwr, const char *detail) {
    if (!tx_log_ready || !event) {
        return;
    }

    tx_log_item_t *item = calloc(1, sizeof(tx_log_item_t));
    if (!item) {
        return;
    }

    clock_gettime(CLOCK_REALTIME, &item->ts);
    item->freq_hz = freq_hz;
    item->mode = mode;
    item->pwr = pwr;
    strncpy(item->event, event, sizeof(item->event) - 1);
    if (detail) {
        strncpy(item->detail, detail, sizeof(item->detail) - 1);
    }

    item->stack_size = backtrace(item->stack, TX_LOG_STACK_DEPTH);

    pthread_mutex_lock(&tx_log_queue_mux);
    if (tx_log_tail) {
        tx_log_tail->next = item;
        tx_log_tail = item;
    } else {
        tx_log_head = item;
        tx_log_tail = item;
    }
    pthread_cond_signal(&tx_log_queue_cond);
    pthread_mutex_unlock(&tx_log_queue_mux);
}

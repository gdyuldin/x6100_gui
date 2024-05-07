/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <math.h>
#include <time.h>
#include <sys/time.h>
#include <pthread.h>

#include "lvgl/lvgl.h"
#include "dialog.h"
#include "dialog_ft8.h"
#include "styles.h"
#include "params.h"
#include "radio.h"
#include "audio.h"
#include "keyboard.h"
#include "events.h"
#include "radio.h"
#include "buttons.h"
#include "main_screen.h"
#include "qth.h"
#include "msg.h"
#include "util.h"
#include "recorder.h"

#include "widgets/lv_waterfall.h"
#include "widgets/lv_finder.h"

#include "ft8/unpack.h"
#include "ft8/pack.h"
#include "ft8/ldpc.h"
#include "ft8/decode.h"
#include "ft8/constants.h"
#include "ft8/encode.h"
#include "ft8/crc.h"
#include "gfsk.h"

#define DECIM           4
#define SAMPLE_RATE     (AUDIO_CAPTURE_RATE / DECIM)

#define MIN_SCORE       10
#define MAX_CANDIDATES  120
#define LDPC_ITER       20
#define MAX_DECODED     50
#define FREQ_OSR        2
#define TIME_OSR        4

#define FT8_BANDS       11
#define FT4_BANDS       9

#define WIDTH           775

typedef enum {
    NOT_READY = 0,
    IDLE,
    RX_PROCESS,
    TX_PROCESS,
    TX_STOP,
} ft8_state_t;

typedef enum {
    QSO_IDLE = 0,
    QSO_NEXT,
    QSO_ODD,
    QSO_EVEN,
} ft8_qso_t;

typedef enum {
    MSG_RX_INFO = 0,
    MSG_RX_MSG,
    MSG_RX_CQ,
    MSG_RX_TO_ME,
    MSG_TX_MSG
} ft8_msg_type_t;

typedef enum {
    MSG_TX_INVALID = 0,
    MSG_TX_CQ,
    MSG_TX_CALLING,
    MSG_TX_R_REPORT,

    MSG_TX_REPORT,
    MSG_TX_RR73,

    MSG_TX_DONE
} ft8_tx_msg_t;

typedef struct {
    ft8_msg_type_t  type;
    int16_t         snr;
    int16_t         dist;
    bool            odd;
} ft8_cell_t;

typedef struct {
    char            *msg;
    ft8_cell_t      *cell;
} ft8_msg_t;

typedef struct {
    char        remote_callsign[32];
    char        remote_qth[32];
    int16_t     remote_snr;
    
    char        local_callsign[32];
    char        local_qth[32];
    int16_t     local_snr;
} ft8_qso_item_t;

static ft8_state_t          state = NOT_READY;
static ft8_qso_t            qso = QSO_IDLE;
static bool                 odd;

static char                 tx_msg[64] = "";
static ft8_qso_item_t       qso_item;

static lv_obj_t             *table;
static int16_t              table_rows;

static lv_timer_t           *timer = NULL;
static lv_anim_t            fade;
static bool                 fade_run = false;

static lv_obj_t             *finder;
static lv_obj_t             *waterfall;
static uint16_t             waterfall_nfft;
static spgramcf             waterfall_sg;
static float                *waterfall_psd;
static uint8_t              waterfall_fps_ms = (1000 / 5);
static uint64_t             waterfall_time;

static pthread_cond_t       audio_cond;
static pthread_mutex_t      audio_mutex;
static cbuffercf            audio_buf;
static pthread_t            thread;

static firdecim_crcf        decim;
static float complex        *decim_buf;
static complex float        *rx_window = NULL;
static complex float        *time_buf;
static complex float        *freq_buf;
static windowcf             frame_window;
static fftplan              fft;

static float                symbol_period;
static uint32_t             block_size;
static uint32_t             subblock_size;
static uint16_t             nfft;
static float                fft_norm;
static waterfall_t          wf;

static candidate_t          candidate_list[MAX_CANDIDATES];
static message_t            decoded[MAX_DECODED];
static message_t*           decoded_hashtable[MAX_DECODED];

static struct tm            timestamp;

static void construct_cb(lv_obj_t *parent);
static void key_cb(lv_event_t * e);
static void destruct_cb();
static void audio_cb(unsigned int n, float complex *samples);
static void rotary_cb(int32_t diff);
static void * decode_thread(void *arg);

static void show_all_cb(lv_event_t * e);
static void show_cq_cb(lv_event_t * e);

static void mode_ft8_cb(lv_event_t * e);
static void mode_ft4_cb(lv_event_t * e);

static void tx_cq_dis_cb(lv_event_t * e);
static void tx_cq_en_cb(lv_event_t * e);

static void tx_call_dis_cb(lv_event_t * e);
static void tx_call_en_cb(lv_event_t * e);

static void mode_auto_cb(lv_event_t * e);

static void make_tx_msg(ft8_tx_msg_t msg, int16_t snr);
static bool do_rx_msg(ft8_cell_t *cell, const char * msg, bool pressed);

static button_item_t button_show_all = { .label = "Show\nAll", .press = show_all_cb };
static button_item_t button_show_cq = { .label = "Show\nCQ", .press = show_cq_cb };

static button_item_t button_mode_ft8 = { .label = "Mode\nFT8", .press = mode_ft8_cb };
static button_item_t button_mode_ft4 = { .label = "Mode\nFT4", .press = mode_ft4_cb };

static button_item_t button_tx_cq_dis = { .label = "TX CQ\nDisabled", .press = tx_cq_dis_cb };
static button_item_t button_tx_cq_en = { .label = "TX CQ\nEnabled", .press = tx_cq_en_cb };

static button_item_t button_tx_call_dis = { .label = "TX Call\nDisabled", .press = tx_call_dis_cb, .hold = tx_cq_en_cb };
static button_item_t button_tx_call_en = { .label = "TX Call\nEnabled", .press = tx_call_en_cb, .hold = tx_cq_en_cb };

static button_item_t button_auto_dis = { .label = "Auto\nDisabled", .press = mode_auto_cb };
static button_item_t button_auto_en = { .label = "Auto\nEnabled", .press = mode_auto_cb };

static dialog_t             dialog = {
    .run = false,
    .construct_cb = construct_cb,
    .destruct_cb = destruct_cb,
    .audio_cb = audio_cb,
    .rotary_cb = rotary_cb,
    .key_cb = key_cb
};

dialog_t                    *dialog_ft8 = &dialog;

static void reset() {
    wf.num_blocks = 0;
    state = IDLE;
}

static void init() {
    /* FT8 decoder */

    float   slot_time;
    
    switch (params.ft8_protocol) {
        case PROTO_FT4:
            slot_time = FT4_SLOT_TIME;
            symbol_period = FT4_SYMBOL_PERIOD;
            break;
            
        case PROTO_FT8:
            slot_time = FT8_SLOT_TIME;
            symbol_period = FT8_SYMBOL_PERIOD;
            break;
    }
    
    block_size = SAMPLE_RATE * symbol_period;
    subblock_size = block_size / TIME_OSR;
    nfft = block_size * FREQ_OSR;
    fft_norm = 2.0f / nfft;
    
    const uint32_t max_blocks = slot_time / symbol_period;
    const uint32_t num_bins = SAMPLE_RATE * symbol_period / 2;

    size_t mag_size = max_blocks * TIME_OSR * FREQ_OSR * num_bins * sizeof(uint8_t);
    
    wf.max_blocks = max_blocks;
    wf.num_bins = num_bins;
    wf.time_osr = TIME_OSR;
    wf.freq_osr = FREQ_OSR;
    wf.block_stride = TIME_OSR * FREQ_OSR * num_bins;
    wf.mag = (uint8_t *) malloc(mag_size);
    wf.protocol = params.ft8_protocol;

    /* FT8 DSP */
    
    decim_buf = (float complex *) malloc(block_size * sizeof(float complex));
    time_buf = (float complex*) malloc(nfft * sizeof(float complex));
    freq_buf = (float complex*) malloc(nfft * sizeof(float complex));
    fft = fft_create_plan(nfft, time_buf, freq_buf, LIQUID_FFT_FORWARD, 0);
    frame_window = windowcf_create(nfft);

    rx_window = malloc(nfft * sizeof(complex float));

    for (uint16_t i = 0; i < nfft; i++)
        rx_window[i] = liquid_hann(i, nfft);

    float gain = 0.0f;

    for (uint16_t i = 0; i < nfft; i++)
        gain += rx_window[i] * rx_window[i];
        
    gain = 1.0f / sqrtf(gain);

    for (uint16_t i = 0; i < nfft; i++)
        rx_window[i] *= gain;

    qso = QSO_IDLE;

    reset();

    /* Waterfall */

    waterfall_nfft = block_size * 2;

    waterfall_sg = spgramcf_create(waterfall_nfft, LIQUID_WINDOW_HANN, waterfall_nfft, waterfall_nfft / 4);
    waterfall_psd = (float *) malloc(waterfall_nfft * sizeof(float));
    waterfall_time = get_time();
        
    /* Worker */
        
    pthread_mutex_init(&audio_mutex, NULL);
    pthread_cond_init(&audio_cond, NULL);
    pthread_create(&thread, NULL, decode_thread, NULL);
}

static void done() {
    state = IDLE;

    pthread_cancel(thread);
    pthread_join(thread, NULL);

    free(wf.mag);
    windowcf_destroy(frame_window);

    free(decim_buf);
    free(time_buf);
    free(freq_buf);
    fft_destroy_plan(fft);

    spgramcf_destroy(waterfall_sg);
    free(waterfall_psd);

    free(rx_window);
}

static void send_info(const char * fmt, ...) {
    va_list     args;
    char        buf[128];
    ft8_msg_t   *msg = malloc(sizeof(ft8_msg_t));

    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    msg->msg = strdup(buf);

    msg->cell = lv_mem_alloc(sizeof(ft8_cell_t));
    msg->cell->type = MSG_RX_INFO;

    event_send(table, EVENT_FT8_MSG, msg);
}

static const char * find_qth(const char *str) {
    char *ptr = rindex(str, ' ');
    
    if (ptr) {
        ptr++;
        
        if (strcmp(ptr, "RR73") != 0 && grid_check(ptr)) {
            return ptr;
        }
    }
    
    return NULL;
}

static bool to_me(const char * text) {
    int16_t         callsign_len = strlen(params.callsign.x);

    return (callsign_len > 0) && (strncasecmp(text, params.callsign.x, callsign_len) == 0);
}

static void send_rx_text(int16_t snr, const char * text) {
    ft8_msg_type_t  type;
    int16_t         callsign_len = strlen(params.callsign.x);

    if (to_me(text)) {
        type = MSG_RX_TO_ME;
    } else if (strncmp(text, "CQ ", 3) == 0) {
        type = MSG_RX_CQ;
    } else {
        if (params.ft8_show_all) {
            type = MSG_RX_MSG;
        } else {
            return;
        }
    }

    ft8_msg_t *msg = malloc(sizeof(ft8_msg_t));

    msg->msg = strdup(text);

    msg->cell = lv_mem_alloc(sizeof(ft8_cell_t));
    msg->cell->snr = snr;
    msg->cell->type = type;
    msg->cell->odd = odd;

    if (params.qth.x[0] != 0) {
        const char *qth = find_qth(text);
            
        msg->cell->dist = qth ? grid_dist(qth) : 0;
    } else {
        msg->cell->dist = 0;
    }

    event_send(table, EVENT_FT8_MSG, msg);
}

static void send_tx_text(const char * text) {
    ft8_msg_t   *msg = malloc(sizeof(ft8_msg_t));

    msg->msg = strdup(text);

    msg->cell = lv_mem_alloc(sizeof(ft8_cell_t));
    msg->cell->type = MSG_TX_MSG;

    event_send(table, EVENT_FT8_MSG, msg);
}

static void decode() {
    uint16_t    num_candidates = ft8_find_sync(&wf, MAX_CANDIDATES, candidate_list, MIN_SCORE);

    memset(decoded_hashtable, 0, sizeof(decoded_hashtable));
    memset(decoded, 0, sizeof(decoded));
    
    for (uint16_t idx = 0; idx < num_candidates; idx++) {
        const candidate_t *cand = &candidate_list[idx];
        
        if (cand->score < MIN_SCORE)
            continue;
            
        float freq_hz = (cand->freq_offset + (float) cand->freq_sub / wf.freq_osr) / symbol_period;
        float time_sec = (cand->time_offset + (float) cand->time_sub / wf.time_osr) * symbol_period;
        
        message_t       message;
        decode_status_t status;
        
        if (!ft8_decode(&wf, cand, &message, LDPC_ITER, &status)) {
            continue;
        }
        
        uint16_t    idx_hash = message.hash % MAX_DECODED;
        bool        found_empty_slot = false;
        bool        found_duplicate = false;
        
        do {
            if (decoded_hashtable[idx_hash] == NULL) {
                found_empty_slot = true;
            } else if (decoded_hashtable[idx_hash]->hash == message.hash && strcmp(decoded_hashtable[idx_hash]->text, message.text) == 0) {
                found_duplicate = true;
            } else {
                idx_hash = (idx_hash + 1) % MAX_DECODED;
            }
        } while (!found_empty_slot && !found_duplicate);

        if (found_empty_slot) {
            memcpy(&decoded[idx_hash], &message, sizeof(message));
            decoded_hashtable[idx_hash] = &decoded[idx_hash];

            send_rx_text(cand->snr, message.text);
        }
    }
}

void static waterfall_process(float complex *frame, const size_t size) {
    uint64_t now = get_time();

    spgramcf_write(waterfall_sg, frame, size);

    if (now - waterfall_time > waterfall_fps_ms) {
        uint32_t low_bin = waterfall_nfft / 2 + waterfall_nfft * params_mode.filter_low / SAMPLE_RATE;
        uint32_t high_bin = waterfall_nfft / 2 + waterfall_nfft * params_mode.filter_high / SAMPLE_RATE;
    
        spgramcf_get_psd(waterfall_sg, waterfall_psd);

        lv_waterfall_add_data(waterfall, &waterfall_psd[low_bin], high_bin - low_bin);
        event_send(waterfall, LV_EVENT_REFRESH, NULL);

        waterfall_time = now;
        spgramcf_reset(waterfall_sg);
    }
}

void static process(float complex *frame) {
    complex float   *frame_ptr;
    int             offset = wf.num_blocks * wf.block_stride;
    int             frame_pos = 0;
    
    for (int time_sub = 0; time_sub < wf.time_osr; time_sub++) {
        windowcf_write(frame_window, &frame[frame_pos], subblock_size);
        frame_pos += subblock_size;

        windowcf_read(frame_window, &frame_ptr);
        
        for (uint32_t pos = 0; pos < nfft; pos++)
            time_buf[pos] = rx_window[pos] * frame_ptr[pos];

        fft_execute(fft);
                
        for (int freq_sub = 0; freq_sub < wf.freq_osr; freq_sub++)
            for (int bin = 0; bin < wf.num_bins; bin++) {
                int             src_bin = (bin * wf.freq_osr) + freq_sub;
                complex float   freq = freq_buf[src_bin];
                float           v = crealf(freq * conjf(freq));
                float           db = 10.0f * log10f(v);
                int             scaled = (int16_t) (db * 2.0f + 240.0f);
                
                if (scaled < 0) {
                    scaled = 0;
                } else if (scaled > 255) {
                    scaled = 255;
                }

                wf.mag[offset] = scaled;
                offset++;
            }
    }
    
    wf.num_blocks++;
}

static bool do_start(bool *odd) {
    struct tm       *tm;
    time_t          now;
    bool            start = false;

    now = time(NULL);
    tm = localtime(&now);
    
    switch (params.ft8_protocol) {
        case PROTO_FT4:
            switch (tm->tm_sec) {
                case 0 ... 1:
                case 15 ... 16:
                case 30 ... 31:
                case 45 ... 46:
                    *odd = true;
                    start = true;
                    break;

                case 7 ... 8:
                case 22 ... 23:
                case 37 ... 38:
                case 52 ... 53:
                    *odd = false;
                    start = true;
                    break;
                    
                default:
                    start = false;
            }
            break;
            
        case PROTO_FT8:
            switch (tm->tm_sec) {
                case 0 ... 1:
                case 30 ... 31:
                    *odd = true;
                    start = true;
                    break;

                case 15 ... 16:
                case 45 ... 46:
                    *odd = false;
                    start = true;
                    break;
                    
                default:
                    start = false;
            }
            break;
    }
    
    if (start) {
        timestamp = *tm;
    }
    
    return start;
}

static void rx_worker(bool sync) {
    unsigned int    n;
    float complex   *buf;
    const size_t    size = block_size * DECIM;

    pthread_mutex_lock(&audio_mutex);

    while (cbuffercf_size(audio_buf) < size) {
        pthread_cond_wait(&audio_cond, &audio_mutex);
    }
    
    pthread_mutex_unlock(&audio_mutex);
        
    while (cbuffercf_size(audio_buf) > size) {
        cbuffercf_read(audio_buf, size, &buf, &n);

        firdecim_crcf_execute_block(decim, buf, block_size, decim_buf);
        cbuffercf_release(audio_buf, size);

        waterfall_process(decim_buf, block_size);

        if (sync) {
            process(decim_buf);
    
            if (wf.num_blocks >= wf.max_blocks) {
                decode();
                reset();
            }
        }
    }
}

static void tx_worker() {
    uint8_t tones[FT8_NN];
    uint8_t packed[FTX_LDPC_K_BYTES];
    int     rc = pack77(tx_msg, packed);

    if (rc < 0) {
        LV_LOG_ERROR("Cannot parse message %i", rc);
        state = IDLE;
        return;
    }

    ft8_encode(packed, tones);

    int32_t     n_samples = 0;
    float       symbol_bt = (params.ft8_protocol == PROTO_FT4) ? FT4_SYMBOL_BT : FT8_SYMBOL_BT;
    int16_t     *samples = gfsk_synth(tones, FT8_NN, params.ft8_tx_freq.x, symbol_bt, symbol_period, &n_samples);
    int16_t     *ptr = samples;
    size_t      part = 1024 * 2;

    radio_set_ptt(true);

    while (true) {
        if (n_samples <= 0 || state != TX_PROCESS) {
            state = IDLE;
            break;
        }

        audio_play(ptr, part);

        n_samples -= part;
        ptr += part;
    }

    audio_play_wait();
    radio_set_ptt(false);
    free(samples);
}

static void * decode_thread(void *arg) {
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
    
    while (true) {
        switch (state) {
            case IDLE:
                if (do_start(&odd)) {
                    switch (qso) {
                        case QSO_IDLE:
                            state = RX_PROCESS;
                            break;
                            
                        case QSO_NEXT:
                            state = TX_PROCESS;
                            qso = odd ? QSO_ODD : QSO_EVEN;
                            break;
                            
                        case QSO_ODD:
                            state = odd ? TX_PROCESS : RX_PROCESS;
                            break;
                            
                        case QSO_EVEN:
                            state = !odd ? TX_PROCESS : RX_PROCESS;
                            break;
                    }
                    
                    switch (state) {
                        case RX_PROCESS:
                            if (qso == QSO_IDLE) {
                                send_info("RX %s %02i:%02i:%02i", params_band.label, timestamp.tm_hour, timestamp.tm_min, timestamp.tm_sec);
                            }
                            rx_worker(true);
                            break;
                            
                        case TX_PROCESS:
                            send_tx_text(tx_msg);
                            break;
                    }
                } else {
                    rx_worker(false);
                }
                break;
 
            case RX_PROCESS:
                rx_worker(true);
                break;
                
            case TX_PROCESS:
            case TX_STOP:
                tx_worker();
                break;
                
            default:
                break;
        }
    }

    return NULL;
}

static void add_msg_cb(lv_event_t * e) {
    ft8_msg_t   *msg = (ft8_msg_t *) lv_event_get_param(e);
    int16_t     row = 0;
    int16_t     col = 0;
    bool        scroll;

    lv_table_get_selected_cell(table, &row, &col);
    scroll = table_rows == (row + 1);

#ifdef MAX_TABLE_MSG
    if (table_rows > MAX_TABLE_MSG) {
        for (uint16_t i = 1; i < table_rows; i++)
            lv_table_set_cell_value(table, i-1, 0, lv_table_get_cell_value(table, i, 0));
            
        table_rows--;
    }
#endif

    lv_table_set_cell_value(table, table_rows, 0, msg->msg);
    
    if (msg->cell) {
        lv_table_set_cell_user_data(table, table_rows, 0, msg->cell);
        
        if (params.ft8_auto.x && (msg->cell->type == MSG_RX_TO_ME)) {
            do_rx_msg(msg->cell, msg->msg, false);
        }
    }
    
    if (scroll) {
        int32_t *c = malloc(sizeof(int32_t));
        *c = LV_KEY_DOWN;
        
        lv_event_send(table, LV_EVENT_KEY, c);
    }
    
    table_rows++;
}

static void table_draw_part_begin_cb(lv_event_t * e) {
    lv_obj_t                *obj = lv_event_get_target(e);
    lv_obj_draw_part_dsc_t  *dsc = lv_event_get_draw_part_dsc(e);

    if (dsc->part == LV_PART_ITEMS) {
        uint32_t    row = dsc->id / lv_table_get_col_cnt(obj);
        uint32_t    col = dsc->id - row * lv_table_get_col_cnt(obj);
        ft8_cell_t  *cell = lv_table_get_cell_user_data(obj, row, col);
        
        if (cell == NULL) {
            dsc->label_dsc->align = LV_TEXT_ALIGN_CENTER;
            dsc->rect_dsc->bg_color = lv_color_white();
            dsc->rect_dsc->bg_opa = 128;

            return;
        }
        
        switch (cell->type) {
            case MSG_RX_INFO:
                dsc->label_dsc->align = LV_TEXT_ALIGN_CENTER;
                dsc->rect_dsc->bg_color = lv_color_white();
                dsc->rect_dsc->bg_opa = 128;
                break;
                
            case MSG_RX_CQ:
                dsc->rect_dsc->bg_color = lv_color_hex(0x00DD00);
                dsc->rect_dsc->bg_opa = 128;
                break;

            case MSG_RX_TO_ME:
                dsc->rect_dsc->bg_color = lv_color_hex(0xFF0000);
                dsc->rect_dsc->bg_opa = 128;
                break;

            case MSG_TX_MSG:
                dsc->rect_dsc->bg_color = lv_color_hex(0x0000DD);
                dsc->rect_dsc->bg_opa = 128;
                break;
        }
    }
}

static void table_draw_part_end_cb(lv_event_t * e) {
    lv_obj_t                *obj = lv_event_get_target(e);
    lv_obj_draw_part_dsc_t  *dsc = lv_event_get_draw_part_dsc(e);

    if (dsc->part == LV_PART_ITEMS) {
        uint32_t    row = dsc->id / lv_table_get_col_cnt(obj);
        uint32_t    col = dsc->id - row * lv_table_get_col_cnt(obj);
        ft8_cell_t  *cell = lv_table_get_cell_user_data(obj, row, col);

        if (cell == NULL) {
            return;
        }

        if (cell->type == MSG_RX_MSG || cell->type == MSG_RX_CQ || cell->type == MSG_RX_TO_ME) {
            char                buf[64];
            const lv_coord_t    cell_top = lv_obj_get_style_pad_top(obj, LV_PART_ITEMS);
            const lv_coord_t    cell_bottom = lv_obj_get_style_pad_bottom(obj, LV_PART_ITEMS);
            lv_area_t           area;

            dsc->label_dsc->align = LV_TEXT_ALIGN_RIGHT;

            area.y1 = dsc->draw_area->y1 + cell_top;
            area.y2 = dsc->draw_area->y2 - cell_bottom;

            area.x2 = dsc->draw_area->x2 - 15;
            area.x1 = area.x2 - 120;
            
            snprintf(buf, sizeof(buf), "%i dB", cell->snr);
            lv_draw_label(dsc->draw_ctx, dsc->label_dsc, &area, buf, NULL);

            if (cell->dist > 0) {
                area.x2 = area.x1 - 10;
                area.x1 = area.x2 - 200;
                
                snprintf(buf, sizeof(buf), "%i km", cell->dist);
                lv_draw_label(dsc->draw_ctx, dsc->label_dsc, &area, buf, NULL);
            }
        }
    }
}

static void selected_msg_cb(lv_event_t * e) {
    int16_t     row;
    int16_t     col;

    lv_table_get_selected_cell(table, &row, &col);
}

static void key_cb(lv_event_t * e) {
    uint32_t key = *((uint32_t *) lv_event_get_param(e));

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

static void destruct_cb() {
    done();
    
    firdecim_crcf_destroy(decim);
    free(audio_buf);

    mem_load(MEM_BACKUP_ID);

    main_screen_lock_mode(false);
    main_screen_lock_freq(false);
    main_screen_lock_band(false);

    radio_set_pwr(params.pwr);
}

static void load_band() {
    uint16_t mem_id = 0;
    
    switch (params.ft8_protocol) {
        case PROTO_FT8:
            mem_id = MEM_FT8_ID;
            
            if (params.ft8_band > FT8_BANDS - 1) {
                params.ft8_band = FT8_BANDS - 1;
            }
            break;
            
        case PROTO_FT4:
            mem_id = MEM_FT4_ID;

            if (params.ft8_band > FT4_BANDS - 1) {
                params.ft8_band = FT4_BANDS - 1;
            }
            break;
    }
    
    mem_load(mem_id + params.ft8_band);
}

static void clean() {
    reset();

    lv_table_set_row_cnt(table, 1);
    lv_table_set_cell_value(table, 0, 0, "Wait sync");

    lv_waterfall_clear_data(waterfall);

    table_rows = 0;

    int32_t *c = malloc(sizeof(int32_t));
    *c = LV_KEY_UP;
        
    lv_event_send(table, LV_EVENT_KEY, c);
}

static void make_tx_msg(ft8_tx_msg_t msg, int16_t snr) {
    char qth[5] = "";

    if (strlen(params.qth.x) >= 4) {
        strncpy(qth, params.qth.x, sizeof(qth) - 1);
    }

    switch (msg) {
        case MSG_TX_CQ:
            snprintf(tx_msg, sizeof(tx_msg) - 1, "CQ %s %s", params.callsign.x, qth);
            break;
            
        case MSG_TX_CALLING:
            qso_item.local_snr = snr;
            snprintf(tx_msg, sizeof(tx_msg) - 1, "%s %s %s", qso_item.remote_callsign, params.callsign.x, qth);
            break;

        case MSG_TX_REPORT:
            qso_item.local_snr = snr;
            snprintf(tx_msg, sizeof(tx_msg) - 1, "%s %s %+02i", qso_item.remote_callsign, params.callsign.x, qso_item.local_snr);
            break;

        case MSG_TX_R_REPORT:
            snprintf(tx_msg, sizeof(tx_msg) - 1, "%s %s R%+02i", qso_item.remote_callsign, params.callsign.x, qso_item.local_snr);
            break;

        case MSG_TX_RR73:
            snprintf(tx_msg, sizeof(tx_msg) - 1, "%s %s RR73", qso_item.remote_callsign, params.callsign.x);
            break;
            
        default:
            return;
    }
    
    msg_set_text_fmt("Next TX: %s", tx_msg);
}

static ft8_tx_msg_t parse_rx_msg(const char * str) {
    char            *s = strdup(str);
    char            *call_to = NULL;
    char            *call_de = NULL;
    char            *extra = NULL;

    /* Split */
    
    call_to = strtok(s, " ");
    
    if (call_to) {
        call_de = strtok(NULL, " ");
        
        if (call_de) {
            extra = strtok(NULL, " ");
        }
    }

    /* Analysis */

    if (call_to && strcmp(call_to, "CQ") == 0) {
        strcpy(qso_item.remote_callsign, call_de ? call_de : "");
        strcpy(qso_item.remote_qth, extra ? extra : "");
        
        free(s);
        return MSG_TX_CALLING;
    }
    
    if (call_to && to_me(call_to)) {
        if (extra && strcmp(extra, "RR73") == 0 || strcmp(extra, "73") == 0) {
            buttons_load(2, &button_tx_cq_en);
            
            free(s);
            return MSG_TX_DONE;
        }
    
        if (grid_check(extra)) {
            strcpy(qso_item.remote_callsign, call_de);
            strcpy(qso_item.remote_qth, extra);
            
            free(s);
            return MSG_TX_REPORT;
        }
        
        if (extra[0] == 'R' && (extra[1] == '-' || extra[1] == '+')) {
            qso_item.remote_snr = atoi(extra + 1);
            strcpy(qso_item.remote_callsign, call_de);
            
            free(s);
            return MSG_TX_RR73;
        }
        
        if (extra[0] == '-' || extra[0] == '+') {
            qso_item.remote_snr = atoi(extra);
            strcpy(qso_item.remote_callsign, call_de);
            
            free(s);
            return MSG_TX_R_REPORT;
        }
    }

    free(s);
    return MSG_TX_INVALID;
}

static bool do_rx_msg(ft8_cell_t *cell, const char * msg, bool pressed) {
    ft8_tx_msg_t next_msg = parse_rx_msg(msg);

    switch (next_msg) {
        case MSG_TX_CALLING:
            if (pressed) {
                send_info("Start QSO");
            }
            break;
    
        case MSG_TX_INVALID:
            return false;

        case MSG_TX_DONE:
            qso = QSO_IDLE;
            buttons_load(2, &button_tx_call_dis);
            return true;
    }
    
    qso = cell->odd ? QSO_EVEN : QSO_ODD;   /* Must be reversed */
    make_tx_msg(next_msg, cell->snr);

    buttons_load(2, &button_tx_call_en);
    return true;
}

static void band_cb(lv_event_t * e) {
    int band = params.ft8_band;
    int max_band = 0;
    
    switch (params.ft8_protocol) {
        case PROTO_FT8:
            max_band = FT8_BANDS - 1;
            break;
            
        case PROTO_FT4:
            max_band = FT4_BANDS - 1;
            break;
    }

    if (lv_event_get_code(e) == EVENT_BAND_UP) {
        band++;
        
        if (band > max_band) {
            band = 0;
        }
    } else {
        band--;
        
        if (band < 0) {
            band = max_band;
        }
    }
    
    params_lock();
    params.ft8_band = band;
    params_unlock(&params.durty.ft8_band);
    load_band();

    done();
    init();
    clean();
}

static void msg_timer(lv_timer_t *t) {
    lv_anim_set_values(&fade, lv_obj_get_style_opa_layered(table, 0), LV_OPA_COVER);
    lv_anim_start(&fade);
    timer = NULL;
}

static void fade_anim(void * obj, int32_t v) {
    lv_obj_set_style_opa_layered(obj, v, 0);
}

static void fade_ready(lv_anim_t * a) {
    fade_run = false;
}

static void rotary_cb(int32_t diff) {
    uint32_t f = params.ft8_tx_freq.x + diff;
    
    if (f > params_mode.filter_high) {
        f = params_mode.filter_high;
    }
    
    if (f < params_mode.filter_low) {
        f = params_mode.filter_low;
    }

    params_uint16_set(&params.ft8_tx_freq, f);

    lv_finder_set_value(finder, f);
    lv_obj_invalidate(finder);

    if (!fade_run) {
        fade_run = true;
        lv_anim_set_values(&fade, lv_obj_get_style_opa_layered(table, 0), LV_OPA_TRANSP);
        lv_anim_start(&fade);
    }

    if (timer) {
        lv_timer_reset(timer);
    } else {
        timer = lv_timer_create(msg_timer, 1000, NULL);
        lv_timer_set_repeat_count(timer, 1);
    }
}

static void construct_cb(lv_obj_t *parent) {
    dialog.obj = dialog_init(parent);

    lv_obj_add_event_cb(dialog.obj, band_cb, EVENT_BAND_UP, NULL);
    lv_obj_add_event_cb(dialog.obj, band_cb, EVENT_BAND_DOWN, NULL);

    decim = firdecim_crcf_create_kaiser(DECIM, 16, 40.0f);
    audio_buf = cbuffercf_create(AUDIO_CAPTURE_RATE);

    /* Waterfall */

    waterfall = lv_waterfall_create(dialog.obj);

    lv_obj_add_style(waterfall, &waterfall_style, 0);
    lv_obj_clear_flag(waterfall, LV_OBJ_FLAG_SCROLLABLE);

    lv_color_t palette[256];
    
    styles_waterfall_palette(palette, 256);
    lv_waterfall_set_palette(waterfall, palette, 256);
    lv_waterfall_set_size(waterfall, WIDTH, 325);

    lv_obj_set_pos(waterfall, 13, 13);

    /* Freq finder */

    finder = lv_finder_create(waterfall);

    lv_finder_set_width(finder, 50);
    lv_finder_set_value(finder, params.ft8_tx_freq.x);

    lv_obj_set_size(finder, WIDTH, 325);
    lv_obj_set_pos(finder, 0, 0);

    lv_obj_set_style_radius(finder, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(finder, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(finder, LV_OPA_0, LV_PART_MAIN);

    lv_obj_set_style_bg_color(finder, bg_color, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(finder, LV_OPA_50, LV_PART_INDICATOR);

    lv_obj_set_style_border_width(finder, 1, LV_PART_INDICATOR);
    lv_obj_set_style_border_color(finder, lv_color_white(), LV_PART_INDICATOR);
    lv_obj_set_style_border_opa(finder, LV_OPA_50, LV_PART_INDICATOR);

    /* Table */

    table = lv_table_create(dialog.obj);
    
    lv_obj_remove_style(table, NULL, LV_STATE_ANY | LV_PART_MAIN);
    lv_obj_add_event_cb(table, add_msg_cb, EVENT_FT8_MSG, NULL);
    lv_obj_add_event_cb(table, selected_msg_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(table, tx_call_dis_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(table, key_cb, LV_EVENT_KEY, NULL);
    lv_obj_add_event_cb(table, table_draw_part_begin_cb, LV_EVENT_DRAW_PART_BEGIN, NULL);
    lv_obj_add_event_cb(table, table_draw_part_end_cb, LV_EVENT_DRAW_PART_END, NULL);

    lv_obj_set_size(table, WIDTH, 325 - 55);
    lv_obj_set_pos(table, 13, 13 + 55);
    
    lv_table_set_col_cnt(table, 1);
    lv_table_set_col_width(table, 0, WIDTH - 5);

    lv_obj_set_style_border_width(table, 0, LV_PART_ITEMS);
    
    lv_obj_set_style_bg_opa(table, 192, LV_PART_MAIN);
    lv_obj_set_style_bg_color(table, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_border_width(table, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(table, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_border_opa(table, 128, LV_PART_MAIN);
    
    lv_obj_set_style_bg_opa(table, LV_OPA_TRANSP, LV_PART_ITEMS);
    lv_obj_set_style_text_color(table, lv_color_white(), LV_PART_ITEMS);
    lv_obj_set_style_pad_top(table, 3, LV_PART_ITEMS);
    lv_obj_set_style_pad_bottom(table, 3, LV_PART_ITEMS);
    lv_obj_set_style_pad_left(table, 5, LV_PART_ITEMS);
    lv_obj_set_style_pad_right(table, 0, LV_PART_ITEMS);

    lv_obj_set_style_text_color(table, lv_color_black(), LV_PART_ITEMS | LV_STATE_EDITED);
    lv_obj_set_style_bg_color(table, lv_color_white(), LV_PART_ITEMS | LV_STATE_EDITED);
    lv_obj_set_style_bg_opa(table, 128, LV_PART_ITEMS | LV_STATE_EDITED);

    lv_table_set_cell_value(table, 0, 0, "Wait sync");

    /* Fade */

    lv_anim_init(&fade);
    lv_anim_set_var(&fade, table);
    lv_anim_set_time(&fade, 250);
    lv_anim_set_exec_cb(&fade, fade_anim);
    lv_anim_set_ready_cb(&fade, fade_ready);

    /* * */

    lv_group_add_obj(keyboard_group, table);
    lv_group_set_editing(keyboard_group, true);

    table_rows = 0;

    if (params.ft8_show_all) {
        buttons_load(0, &button_show_all);
    } else {
        buttons_load(0, &button_show_cq);
    }

    switch (params.ft8_protocol) {
        case PROTO_FT8:
            buttons_load(1, &button_mode_ft8);
            break;

        case PROTO_FT4:
            buttons_load(1, &button_mode_ft4);
            break;
    }

    buttons_load(2, &button_tx_cq_dis);
    buttons_load(3, params.ft8_auto.x ? &button_auto_en : &button_auto_dis);
    
    mem_save(MEM_BACKUP_ID);
    load_band();

    lv_finder_set_range(finder, params_mode.filter_low, params_mode.filter_high);

    main_screen_lock_mode(true);
    main_screen_lock_freq(true);
    main_screen_lock_band(true);

    init();
    
    if (params.pwr > 5.0f) {
        radio_set_pwr(5.0f);
        msg_set_text_fmt("Power was limited to 5W");
    }
}

static void show_all_cb(lv_event_t * e) {
    params_lock();
    params.ft8_show_all = false;
    params_unlock(&params.durty.ft8_show_all);

    buttons_load(0, &button_show_cq);
}

static void show_cq_cb(lv_event_t * e) {
    params_lock();
    params.ft8_show_all = true;
    params_unlock(&params.durty.ft8_show_all);

    buttons_load(0, &button_show_all);
}

static void mode_ft8_cb(lv_event_t * e) {
    params_lock();
    params.ft8_protocol = PROTO_FT4;
    params_unlock(&params.durty.ft8_protocol);

    buttons_load(1, &button_mode_ft4);

    done();
    init();
    clean();
    load_band();
}

static void mode_ft4_cb(lv_event_t * e) {
    params_lock();
    params.ft8_protocol = PROTO_FT8;
    params_unlock(&params.durty.ft8_protocol);

    buttons_load(1, &button_mode_ft8);

    done();
    init();
    clean();
    load_band();
}

static void mode_auto_cb(lv_event_t * e) {
    params_bool_set(&params.ft8_auto, !params.ft8_auto.x);

    buttons_load(3, params.ft8_auto.x ? &button_auto_en : &button_auto_dis);
}

static void tx_cq_dis_cb(lv_event_t * e) {
    if (strlen(params.callsign.x) == 0) {
        msg_set_text_fmt("Call sign required");

        return;
    }
    
    buttons_load(2, &button_tx_cq_en);
    make_tx_msg(MSG_TX_CQ, 0);
    qso = QSO_NEXT;
}

static void tx_cq_en_cb(lv_event_t * e) {
    buttons_load(2, &button_tx_cq_dis);
    
    if (state == TX_PROCESS) {
        state = TX_STOP;
    }
    qso = QSO_IDLE;
}

static void tx_call_off() {
    buttons_load(2, &button_tx_call_dis);
    state = TX_STOP;
    qso = QSO_IDLE;
}

static void tx_call_dis_cb(lv_event_t * e) {
    if (strlen(params.callsign.x) == 0) {
        msg_set_text_fmt("Call sign required");

        return;
    }

    if (state == TX_PROCESS) {
        tx_call_off();
    } else {
        int16_t     row;
        int16_t     col;

        lv_table_get_selected_cell(table, &row, &col);

        ft8_cell_t  *cell = lv_table_get_cell_user_data(table, row, col);

        if (cell == NULL || cell->type == MSG_TX_MSG || cell->type == MSG_RX_INFO) {
            msg_set_text_fmt("What should I do about it?");
        } else {
            if (!do_rx_msg(cell, lv_table_get_cell_value(table, row, col), true)) {
                msg_set_text_fmt("Invalid message");
                tx_call_off();
            }
        }
    }
}

static void tx_call_en_cb(lv_event_t * e) {
    buttons_load(2, &button_tx_call_dis);

    if (state == TX_PROCESS) {
        state = TX_STOP;
    }
    qso = QSO_IDLE;
}

static void audio_cb(unsigned int n, float complex *samples) {
    if (state == IDLE || state == RX_PROCESS) {
        pthread_mutex_lock(&audio_mutex);
        cbuffercf_write(audio_buf, samples, n);
        pthread_cond_broadcast(&audio_cond);
        pthread_mutex_unlock(&audio_mutex);
    }
}

/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

#include "dialog_ft8.h"

#include "ft8/worker.h"
#include "ft8/qso.h"
#include "ft8/utils.h"
#include "lvgl/lvgl.h"
#include "dialog.h"
#include "styles.h"
#include "params/params.h"
#include "cfg/digital_modes.h"
#include "radio.h"
#include "audio.h"
#include "keyboard.h"
#include "events.h"
#include "buttons.h"
#include "main_screen.h"
#include "qth/qth.h"
#include "msg.h"
#include "util.h"
#include "recorder.h"
#include "tx_info.h"
#include "textarea_window.h"

#include "widgets/lv_waterfall.h"
#include "widgets/lv_finder.h"

#include <ft8lib/message.h>
#include "ft8/worker.h"
#include "adif.h"
#include "qso_log.h"
#include "scheduler.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <math.h>
#include <time.h>
#include <sys/time.h>
#include <pthread.h>
#include <errno.h>
#include <ctype.h>

#define DECIM           6
#define SAMPLE_RATE     (AUDIO_CAPTURE_RATE / DECIM)

#define WIDTH           771

#define UNKNOWN_SNR     99

#define MAX_PWR         5.0f

#define FT8_WIDTH_HZ    50
#define FT4_WIDTH_HZ    83

#define MAX_TABLE_MSG   512
#define CLEAN_N_ROWS    64

#define MAX_TX_START_DELAY 1.5f

#define WAIT_SYNC_TEXT "Wait sync"

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof(arr[0]))

typedef enum {
    RX_PROCESS,
    TX_PROCESS,
} ft8_state_t;

typedef enum {
    CELL_RX_INFO = 0,
    CELL_RX_MSG,
    CELL_RX_CQ,
    CELL_RX_TO_ME,
    CELL_TX_MSG
} ft8_cell_type_t;


/**
 * LVGL cell user data
 */
typedef struct {
    ft8_cell_type_t cell_type;
    int16_t         dist;
    bool            odd;
    ftx_msg_meta_t  meta;
    char            text[64];

    qso_log_search_worked_t       worked_type;
} cell_data_t;


typedef struct {
    bool odd;
    bool answer_generated;
} slot_info_t;

static ft8_state_t state = RX_PROCESS;
static Subject    *tx_enabled;
static Subject    *cq_enabled;
static bool        tx_time_slot;

static ftx_tx_msg_t         tx_msg;

static lv_obj_t             *table;

static lv_timer_t           *timer = NULL;
static lv_anim_t            fade;
static bool                 fade_run = false;
static bool                 disable_buttons = false;

static lv_obj_t             *finder;
static lv_obj_t             *waterfall;
static uint16_t             waterfall_nfft;
static spgramcf             waterfall_sg;
static float                *waterfall_psd;
static uint8_t              waterfall_fps_ms = (1000 / 5);
static uint64_t             waterfall_time;

static pthread_mutex_t      audio_mutex = PTHREAD_MUTEX_INITIALIZER;
static cbuffercf            audio_buf;
static pthread_t            thread;

static firdecim_crcf        decim;
static float complex        *decim_buf;

static adif_log             ft8_log;
static FTxQsoProcessor         *qso_processor;

static double               cur_lat, cur_lon;

static int32_t  filter_low, filter_high;

static float base_gain_offset;

static void construct_cb(lv_obj_t *parent);
static void key_cb(lv_event_t * e);
static void destruct_cb();
static void audio_cb(unsigned int n, float complex *samples);
static void rotary_cb(int32_t diff);
static void * decode_thread(void *arg);

static const char * cq_all_label_getter();
static const char * protocol_label_getter();
static const char * tx_cq_label_getter();
static const char * tx_call_label_getter();
static const char * hold_freq_label_getter();
static const char * auto_label_getter();

static void show_cq_all_cb(struct button_item_t *btn);
static void mode_ft4_ft8_cb(struct button_item_t *btn);
static void tx_cq_en_dis_cb(struct button_item_t *btn);
static void tx_call_en_dis_cb(struct button_item_t *btn);

static void hold_tx_freq_cb(struct button_item_t *btn);
static void mode_auto_cb(struct button_item_t *btn);
static void cq_modifier_cb(struct button_item_t *btn);
static void time_sync(struct button_item_t *btn);

static void cell_press_cb(lv_event_t * e);

static void keyboard_open();
static bool keyboard_cancel_cb();
static bool keyboard_ok_cb();
static void keyboard_close();

static void add_info(const char * fmt, ...);
static void add_tx_text(const char * text);
static void make_cq_msg(const char *callsign, const char *qth, const char *cq_mod, char *text);
static bool get_time_slot(struct timespec now, float *time_since_start);


// button label is current state, press action and name - next state

static buttons_page_t btn_page_1;
static buttons_page_t btn_page_2;

static button_item_t button_page_1 = { .type=BTN_TEXT, .label = "(Page: 1:2)", .press = button_next_page_cb, .next=&btn_page_2};
static button_item_t button_show_cq_all = { .type=BTN_TEXT_FN, .label_fn = cq_all_label_getter, .press = show_cq_all_cb, .subj=&cfg.ft8_show_all.val};
static button_item_t button_mode_ft4_ft8 = { .type=BTN_TEXT_FN, .label_fn = protocol_label_getter, .press = mode_ft4_ft8_cb, .subj=&cfg.ft8_protocol.val };
static button_item_t button_tx_cq_en_dis = { .type=BTN_TEXT_FN, .label_fn = tx_cq_label_getter, .press = tx_cq_en_dis_cb };
static button_item_t button_tx_call_en_dis = { .type=BTN_TEXT_FN, .label_fn = tx_call_label_getter, .press = tx_call_en_dis_cb};

static button_item_t button_page_2 = { .type=BTN_TEXT, .label = "(Page: 2:2)", .press = button_next_page_cb, .next=&btn_page_1};
static button_item_t button_hold_freq = { .type=BTN_TEXT_FN, .label_fn = hold_freq_label_getter, .press = hold_tx_freq_cb, .subj=&cfg.ft8_hold_freq.val };
static button_item_t button_auto_en_dis = { .type=BTN_TEXT_FN, .label_fn = auto_label_getter, .press = mode_auto_cb, .subj=&cfg.ft8_auto.val };
static button_item_t button_cq_mod = { .type=BTN_TEXT, .label = "CQ\nModifier", .press = cq_modifier_cb };
static button_item_t button_time_sync = { .type=BTN_TEXT, .label = "Time\nSync", .press = time_sync };

static buttons_page_t btn_page_1 = {
    {&button_page_1, &button_show_cq_all, &button_mode_ft4_ft8, &button_tx_cq_en_dis, &button_tx_call_en_dis}
};

static buttons_page_t btn_page_2 = {
    {&button_page_2, &button_hold_freq, &button_auto_en_dis, &button_cq_mod, &button_time_sync}
};

static dialog_t dialog = {
    .run = false,
    .construct_cb = construct_cb,
    .destruct_cb = destruct_cb,
    .audio_cb = audio_cb,
    .rotary_cb = rotary_cb,
    .key_cb = key_cb,
};

dialog_t *dialog_ft8 = &dialog;

static void save_qso(const char *remote_callsign, const char *remote_grid, const int r_snr, const int s_snr) {
    time_t now = time(NULL);

    char * canonized_call = util_canonize_callsign(remote_callsign, false);
    qso_log_record_t qso = qso_log_record_create(
        params.callsign.x,
        canonized_call,
        now, subject_get_int(cfg.ft8_protocol.val) == FTX_PROTOCOL_FT8 ? MODE_FT8 : MODE_FT4,
        s_snr, r_snr, subject_get_int(cfg_cur.fg_freq), NULL, NULL,
        params.qth.x, remote_grid
    );
    free(canonized_call);

    adif_add_qso(ft8_log, qso);

    // Save QSO to sqlite log
    qso_log_record_save(qso);

    msg_schedule_text_fmt("QSO saved");
    lv_finder_clear_cursor(finder);
}

static void worker_init() {

    /* ftx worker */
    qso_processor = ftx_qso_processor_init(params.callsign.x, params.qth.x, save_qso, subject_get_int(cfg.ft8_max_repeats.val));

    ftx_worker_init(SAMPLE_RATE, subject_get_int(cfg.ft8_protocol.val));
    int block_size = ftx_worker_get_block_size();

    decim_buf = (float complex *) malloc(block_size * sizeof(float complex));

    /* Waterfall */
    waterfall_nfft = (uint16_t)(WIDTH * SAMPLE_RATE / (filter_high - filter_low));

    waterfall_sg = spgramcf_create(waterfall_nfft, LIQUID_WINDOW_HANN, waterfall_nfft, waterfall_nfft / 2);
    waterfall_psd = (float *) malloc(waterfall_nfft * sizeof(float));
    waterfall_time = get_time();

    /* Worker */
    pthread_create(&thread, NULL, decode_thread, NULL);
}

static void worker_done() {
    state = RX_PROCESS;

    pthread_cancel(thread);
    pthread_join(thread, NULL);
    radio_set_modem(false);
    pthread_mutex_unlock(&audio_mutex);
    ftx_worker_free();
    free(decim_buf);

    spgramcf_destroy(waterfall_sg);
    free(waterfall_psd);

    ftx_qso_processor_delete(qso_processor);
    lv_finder_clear_cursor(finder);
    tx_msg.msg[0] = '\0';
}

void static waterfall_process(float complex *frame, const size_t size) {
    uint64_t now = get_time();

    spgramcf_write(waterfall_sg, frame, size);

    if (now - waterfall_time > waterfall_fps_ms) {
        uint32_t low_bin = waterfall_nfft / 2 + waterfall_nfft * filter_low / SAMPLE_RATE;
        uint32_t high_bin = waterfall_nfft / 2 + waterfall_nfft * filter_high / SAMPLE_RATE;

        high_bin = LV_MIN(high_bin, waterfall_nfft);

        spgramcf_get_psd(waterfall_sg, waterfall_psd);
        // Normalize FFT
        liquid_vectorf_addscalar(
            &waterfall_psd[low_bin],
            high_bin - low_bin,
            -10.f * log10f(sqrtf(waterfall_nfft)),
            &waterfall_psd[low_bin]);

        lv_waterfall_add_data(waterfall, &waterfall_psd[low_bin], high_bin - low_bin);

        waterfall_time = now;
        spgramcf_reset(waterfall_sg);
    }
}

static void truncate_table() {
    lv_coord_t     removed_rows_height = 0;
    uint16_t       table_rows = lv_table_get_row_cnt(table);
    if (table_rows > MAX_TABLE_MSG) {
        LV_LOG_USER("Start");

        lv_table_t *table_obj = (lv_table_t*) table;

        for (size_t i = 0; i < CLEAN_N_ROWS; i++) {
            removed_rows_height += table_obj->row_h[i];
        }

        if (table_obj->row_act > CLEAN_N_ROWS) {
            table_obj->row_act -= CLEAN_N_ROWS;
        } else {
            table_obj->row_act = 0;
        }

        for (uint16_t i = CLEAN_N_ROWS; i < table_rows; i++) {
            cell_data_t *cell_data_copy = malloc(sizeof(cell_data_t));
            lv_table_set_cell_value(table, i-CLEAN_N_ROWS, 0, lv_table_get_cell_value(table, i, 0));
            *cell_data_copy = *(cell_data_t *) lv_table_get_cell_user_data(table, i, 0);
            lv_table_set_cell_user_data(table, i-CLEAN_N_ROWS, 0, cell_data_copy);
        }
        table_rows -= CLEAN_N_ROWS;

        lv_table_set_row_cnt(table, table_rows);
        lv_obj_scroll_by_bounded(table, 0, removed_rows_height, LV_ANIM_OFF);
    }
}

static void add_msg_cb(void *data) {
    truncate_table();
    uint16_t    row = 0;
    uint16_t    col = 0;
    bool        scroll;

    lv_table_get_selected_cell(table, &row, &col);
    uint16_t table_rows = lv_table_get_row_cnt(table);
    if ((table_rows == 1) && strcmp(lv_table_get_cell_value(table, 0, 0), WAIT_SYNC_TEXT) == 0) {
        table_rows--;
    }
    scroll = table_rows == (row + 1);

    // Copy data, because original event data will be deleted
    cell_data_t *cell_data = (cell_data_t*)data;
    cell_data_t *cell_data_copy = malloc(sizeof(cell_data_t));
    *cell_data_copy = *cell_data;

    lv_table_set_cell_value(table, table_rows, 0, cell_data_copy->text);
    lv_table_set_cell_user_data(table, table_rows, 0, cell_data_copy);

    if (scroll) {
        static int32_t c = LV_KEY_DOWN;

        lv_event_send(table, LV_EVENT_KEY, &c);
    }
}

static void table_draw_part_begin_cb(lv_event_t * e) {
    lv_obj_t                *obj = lv_event_get_target(e);
    lv_obj_draw_part_dsc_t  *dsc = lv_event_get_draw_part_dsc(e);

    if (dsc->part == LV_PART_ITEMS) {
        uint32_t    row = dsc->id / lv_table_get_col_cnt(obj);
        uint32_t    col = dsc->id - row * lv_table_get_col_cnt(obj);
        cell_data_t *cell_data = lv_table_get_cell_user_data(obj, row, col);

        dsc->rect_dsc->bg_opa = LV_OPA_50;

        if (cell_data == NULL) {
            dsc->label_dsc->align = LV_TEXT_ALIGN_CENTER;
            dsc->rect_dsc->bg_color = lv_color_hex(0x303030);
        } else {
            switch (cell_data->cell_type) {
                case CELL_RX_INFO:
                    dsc->label_dsc->align = LV_TEXT_ALIGN_CENTER;
                    dsc->rect_dsc->bg_color = lv_color_hex(0x303030);
                    break;

                case CELL_RX_CQ:
                    switch (cell_data->worked_type) {
                        case SEARCH_WORKED_NO:
                            // green
                            dsc->rect_dsc->bg_color = lv_color_hex(0x00DD00);
                            break;
                        case SEARCH_WORKED_YES:
                            // dark green
                            dsc->label_dsc->opa = LV_OPA_90;
                            dsc->rect_dsc->bg_color = lv_color_hex(0x2e5a00);
                            break;
                        case SEARCH_WORKED_SAME_MODE:
                            // darker green
                            dsc->label_dsc->decor = LV_TEXT_DECOR_STRIKETHROUGH;
                            dsc->label_dsc->opa = LV_OPA_80;
                            dsc->rect_dsc->bg_color = lv_color_hex(0x224400);
                            break;
                    }
                    break;

                case CELL_RX_TO_ME:
                    dsc->rect_dsc->bg_color = lv_color_hex(0xFF0000);
                    break;

                case CELL_TX_MSG:
                    dsc->rect_dsc->bg_color = lv_color_hex(0x0000FF);
                    break;
                default:
                    dsc->rect_dsc->bg_color = lv_color_black();
                    break;
            }
        }

        uint16_t    selected_row, selected_col;
        lv_table_get_selected_cell(obj, &selected_row, &selected_col);

        if (selected_row == row) {
            dsc->rect_dsc->bg_color = lv_color_lighten(dsc->rect_dsc->bg_color, 20);
        }
    }
}

static void table_draw_part_end_cb(lv_event_t * e) {
    lv_obj_t                *obj = lv_event_get_target(e);
    lv_obj_draw_part_dsc_t  *dsc = lv_event_get_draw_part_dsc(e);

    if (dsc->part == LV_PART_ITEMS) {
        uint32_t    row = dsc->id / lv_table_get_col_cnt(obj);
        uint32_t    col = dsc->id - row * lv_table_get_col_cnt(obj);
        cell_data_t *cell_data = lv_table_get_cell_user_data(obj, row, col);

        if (cell_data == NULL) {
            return;
        }

        if ((cell_data->cell_type == CELL_RX_MSG) ||
            (cell_data->cell_type == CELL_RX_CQ) ||
            (cell_data->cell_type == CELL_RX_TO_ME)
        ) {
            char                buf[64];
            const lv_coord_t    cell_top = lv_obj_get_style_pad_top(obj, LV_PART_ITEMS);
            const lv_coord_t    cell_bottom = lv_obj_get_style_pad_bottom(obj, LV_PART_ITEMS);
            lv_area_t           area;

            dsc->label_dsc->align = LV_TEXT_ALIGN_RIGHT;

            area.y1 = dsc->draw_area->y1 + cell_top;
            area.y2 = dsc->draw_area->y2 - cell_bottom;

            area.x2 = dsc->draw_area->x2 - 15;
            area.x1 = area.x2 - 120;

            snprintf(buf, sizeof(buf), "%i dB", cell_data->meta.local_snr);
            lv_draw_label(dsc->draw_ctx, dsc->label_dsc, &area, buf, NULL);

            if (cell_data->dist > 0) {
                area.x2 = area.x1 - 10;
                area.x1 = area.x2 - 200;

                snprintf(buf, sizeof(buf), "%i km", cell_data->dist);
                lv_draw_label(dsc->draw_ctx, dsc->label_dsc, &area, buf, NULL);
            }
        }
    }
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
    // TODO: check free mem
    keyboard_close();
    worker_done();

    firdecim_crcf_destroy(decim);
    free(audio_buf);

    mem_load(MEM_BACKUP_ID);

    main_screen_lock_mode(false);
    main_screen_lock_ab(false);
    main_screen_lock_freq(false);
    main_screen_lock_band(false);

    radio_set_pwr(subject_get_float(cfg.pwr.val));
    adif_log_close(ft8_log);
}

static void load_band(int8_t dir) {
    cfg_digital_type_t type;
    switch (subject_get_int(cfg.ft8_protocol.val)) {
        case FTX_PROTOCOL_FT8:
            type = CFG_DIG_TYPE_FT8;
            lv_finder_set_width(finder, FT8_WIDTH_HZ);
            break;

        case FTX_PROTOCOL_FT4:
            type = CFG_DIG_TYPE_FT4;
            lv_finder_set_width(finder, FT4_WIDTH_HZ);
            break;
    }
    bool res = cfg_digital_load(dir, type);
    if (res) {
        msg_update_text_fmt("%s", cfg_digital_label_get());
    }
}

/// @brief Clean waterfall and table
static void clean_screen() {
    lv_table_set_row_cnt(table, 0);
    lv_table_set_row_cnt(table, 1);
    lv_table_set_cell_value(table, 0, 0, WAIT_SYNC_TEXT);

    lv_waterfall_clear_data(waterfall);

    int32_t *c = malloc(sizeof(int32_t));
    *c = LV_KEY_UP;

    lv_event_send(table, LV_EVENT_KEY, c);
}

static void band_cb(lv_event_t * e) {
    int8_t dir;

    if (lv_event_get_code(e) == EVENT_BAND_UP) {
        dir = 1;
    } else {
        dir = -1;
    }

    load_band(dir);

    worker_done();
    worker_init();
    clean_screen();
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

static void set_freq(uint32_t freq) {
    if (freq > filter_high) {
        freq = filter_high;
    }

    if (freq < filter_low) {
        freq = filter_low;
    }

    params_uint16_set(&params.ft8_tx_freq, freq);

    lv_finder_set_value(finder, freq);
    lv_obj_invalidate(finder);
}

static void rotary_cb(int32_t diff) {
    uint32_t abs_diff = abs(diff);
    if (abs_diff > 3) {
        diff *= (abs_diff < 6) ? 5 : 10;
    }
    uint32_t f = params.ft8_tx_freq.x + diff;
    f = limit(f, filter_low, filter_high - (subject_get_int(cfg.ft8_protocol.val) == FTX_PROTOCOL_FT8 ? FT8_WIDTH_HZ : FT4_WIDTH_HZ));

    set_freq(f);

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

    if (!cq_enabled) {
        cq_enabled = subject_create_int(false);
        button_tx_cq_en_dis.subj = &cq_enabled;
    } else {
        subject_set_int(cq_enabled, false);
    }
    if (!tx_enabled) {
        tx_enabled = subject_create_int(true);
        button_tx_call_en_dis.subj = &tx_enabled;
    }

    buttons_load_page(&btn_page_1);

    decim = firdecim_crcf_create_kaiser(DECIM, 8, 40.0f);
    firdecim_crcf_set_scale(decim, 1.0f / DECIM);
    audio_buf = cbuffercf_create(AUDIO_CAPTURE_RATE * 3);

    /* Waterfall */

    waterfall = lv_waterfall_create(dialog.obj);

    lv_obj_add_style(waterfall, &waterfall_style, 0);
    lv_obj_clear_flag(waterfall, LV_OBJ_FLAG_SCROLLABLE);

    lv_waterfall_set_palette(waterfall, (lv_color_t*)wf_palette, 256);
    lv_waterfall_set_size(waterfall, WIDTH, 325);
    lv_waterfall_set_min(waterfall, -60);

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
    lv_obj_add_event_cb(table, cell_press_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(table, key_cb, LV_EVENT_KEY, NULL);
    lv_obj_add_event_cb(table, table_draw_part_begin_cb, LV_EVENT_DRAW_PART_BEGIN, NULL);
    lv_obj_add_event_cb(table, table_draw_part_end_cb, LV_EVENT_DRAW_PART_END, NULL);

    lv_obj_set_size(table, WIDTH, 325 - 55);
    lv_obj_set_pos(table, 13, 13 + 55);

    lv_table_set_col_cnt(table, 1);
    lv_table_set_col_width(table, 0, WIDTH - 2);

    lv_obj_set_style_border_width(table, 0, LV_PART_ITEMS);

    lv_obj_set_style_bg_opa(table, 192, LV_PART_MAIN);
    lv_obj_set_style_bg_color(table, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_border_width(table, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(table, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_border_opa(table, 128, LV_PART_MAIN);

    lv_obj_set_style_text_color(table, lv_color_hex(0xC0C0C0), LV_PART_ITEMS);
    lv_obj_set_style_pad_top(table, 3, LV_PART_ITEMS);
    lv_obj_set_style_pad_bottom(table, 3, LV_PART_ITEMS);
    lv_obj_set_style_pad_left(table, 5, LV_PART_ITEMS);
    lv_obj_set_style_pad_right(table, 0, LV_PART_ITEMS);

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

    mem_save(MEM_BACKUP_ID);
    load_band(0);

    filter_low = subject_get_int(cfg_cur.filter.low);
    filter_high = subject_get_int(cfg_cur.filter.high);

    lv_finder_set_range(finder, filter_low, filter_high);

    qth_str_to_pos(params.qth.x, &cur_lat, &cur_lon);

    main_screen_lock_ab(true);
    main_screen_lock_mode(true);
    main_screen_lock_freq(true);
    main_screen_lock_band(true);

    worker_init();

    /* Logger */
    ft8_log = adif_log_init("/mnt/ft_log.adi");

    if (subject_get_float(cfg.pwr.val) > MAX_PWR) {
        radio_set_pwr(MAX_PWR);
        msg_schedule_text_fmt("Power was limited to %0.0fW", MAX_PWR);
    }

    // setup gain offset
    float target_pwr = LV_MIN(subject_get_float(cfg.pwr.val), MAX_PWR);
    if (x6100_control_get_patched_revision() >= 3) {
        // patched firmware has a true power control
        base_gain_offset = -9.4f;
    } else {
        base_gain_offset = -16.4f + log10f(target_pwr) * 10.0f;
    }
}

/* Buttons */

const char *cq_all_label_getter() {
    static char buf[32];
    sprintf(buf, "Show:\n%s", subject_get_int(cfg.ft8_show_all.val) ? "All": "CQ");
    return buf;
}

const char *protocol_label_getter() {
    static char buf[32];
    sprintf(buf, "Mode:\n%s", subject_get_int(cfg.ft8_protocol.val) == FTX_PROTOCOL_FT8 ? "FT8": "FT4");
    return buf;
}

const char *tx_cq_label_getter() {
    static char buf[32];
    sprintf(buf, "TX CQ:\n%s", subject_get_int(cq_enabled) ? "Enabled": "Disabled");
    return buf;
}

const char *tx_call_label_getter() {
    static char buf[32];
    sprintf(buf, "TX Call:\n%s", subject_get_int(tx_enabled) ? "Enabled": "Disabled");
    return buf;
}

const char *hold_freq_label_getter() {
    static char buf[32];
    sprintf(buf, "Hold Freq:\n%s", subject_get_int(cfg.ft8_hold_freq.val) ? "Enabled": "Disabled");
    return buf;
}

const char *auto_label_getter() {
    static char buf[32];
    sprintf(buf, "Auto:\n%s", subject_get_int(cfg.ft8_auto.val) ? "Enabled": "Disabled");
    return buf;
}

static void show_cq_all_cb(struct button_item_t *btn) {
    if (disable_buttons) return;
    subject_set_int(cfg.ft8_show_all.val, !subject_get_int(cfg.ft8_show_all.val));
}

static void mode_ft4_ft8_cb(struct button_item_t *btn) {
    if (disable_buttons) return;

    ftx_protocol_t proto = subject_get_int(cfg.ft8_protocol.val);
    if (proto == FTX_PROTOCOL_FT8)
        proto = FTX_PROTOCOL_FT4;
    else {
        proto = FTX_PROTOCOL_FT8;
    }
    subject_set_int(cfg.ft8_protocol.val, proto);
    subject_set_int(cq_enabled, false);

    worker_done();
    worker_init();
    clean_screen();
    load_band(0);
}

static void mode_auto_cb(struct button_item_t *btn) {
    if (disable_buttons) return;
    bool new_val = !subject_get_int(cfg.ft8_auto.val);
    subject_set_int(cfg.ft8_auto.val, new_val);
    ftx_qso_processor_set_auto(qso_processor, new_val);
}

static void hold_tx_freq_cb(struct button_item_t *btn) {
    if (disable_buttons) return;
    subject_set_int(cfg.ft8_hold_freq.val, !subject_get_int(cfg.ft8_hold_freq.val));
}

static void tx_cq_en_dis_cb(struct button_item_t *btn) {
    if (disable_buttons) return;

    if (!subject_get_int(cq_enabled)){
        if (strlen(params.callsign.x) == 0) {
            msg_schedule_text_fmt("Call sign required");
            return;
        }
        subject_set_int(cq_enabled, true);
        subject_set_int(tx_enabled, true);

        make_cq_msg(params.callsign.x, params.qth.x, params.ft8_cq_modifier.x, tx_msg.msg);

        struct timespec now;
        clock_gettime(CLOCK_REALTIME, &now);
        float time_since_slot_start;
        tx_time_slot = !get_time_slot(now, &time_since_slot_start);
        if (time_since_slot_start < MAX_TX_START_DELAY) {
            tx_time_slot = !tx_time_slot;
        }

        if (tx_msg.msg[2] == '_') {
            msg_schedule_text_fmt("Next TX: CQ %s", tx_msg.msg + 3);
        } else {
            msg_schedule_text_fmt("Next TX: %s", tx_msg.msg);
        }
        tx_msg.repeats = subject_get_int(cfg.ft8_max_repeats.val);
        ftx_qso_processor_reset(qso_processor);
        lv_finder_clear_cursor(finder);
    } else {
        if (state == TX_PROCESS) {
            state = RX_PROCESS;
        }
        subject_set_int(cq_enabled, false);
        tx_msg.msg[0] = '\0';
    }
}

static void tx_call_en_dis_cb(struct button_item_t *btn) {
    if (disable_buttons)
        return;

    if (!subject_get_int(tx_enabled)) {
        if (strlen(params.callsign.x) == 0) {
            msg_schedule_text_fmt("Call sign required");
            return;
        }
        subject_set_int(tx_enabled, true);
    } else {
        if (state == TX_PROCESS) {
            state = RX_PROCESS;
        }
        subject_set_int(tx_enabled, false);
    }
}

static void tx_call_off() {
    state = RX_PROCESS;
    subject_set_int(tx_enabled, false);
}

static void cq_modifier_cb(struct button_item_t *btn) {
    if (disable_buttons) return;
    keyboard_open();
}

static void time_sync(struct button_item_t *btn) {
    time_t now = time(NULL);
    uint8_t sec = now % 60;
    float drift, slot_time;
    switch (subject_get_int(cfg.ft8_protocol.val)) {
        case FTX_PROTOCOL_FT4:
            slot_time = FT4_SLOT_TIME;
            break;

        case FTX_PROTOCOL_FT8:
            slot_time = FT8_SLOT_TIME;
            break;
    }
    drift = fmodf(sec + slot_time / 2, slot_time) - slot_time / 2;
    struct timespec tp;

    now -= (int) drift;
    tp.tv_sec = now;
    tp.tv_nsec = 0;

    int res = clock_settime(CLOCK_REALTIME, &tp);
    if (res != 0)
    {
        LV_LOG_ERROR("Can't set system time: %s\n", strerror(errno));
        return;
    }
}

static void cell_press_cb(lv_event_t * e) {
    if (state == TX_PROCESS) {
        tx_call_off();
    } else {
        uint16_t     row;
        uint16_t     col;

        lv_table_get_selected_cell(table, &row, &col);

        cell_data_t  *cell_data = lv_table_get_cell_user_data(table, row, col);

        if ((cell_data == NULL) ||
            (cell_data->cell_type == CELL_TX_MSG) ||
            (cell_data->cell_type == CELL_RX_INFO)
        ) {
            msg_schedule_text_fmt("What should I do about it?");
        } else {
            ftx_qso_processor_start_qso(qso_processor, &cell_data->meta, &tx_msg);
            if (strlen(tx_msg.msg) > 0) {
                lv_finder_set_cursor(finder, cell_data->meta.freq_hz);
                if (!subject_get_int(cfg.ft8_hold_freq.val)) {
                    set_freq(cell_data->meta.freq_hz);
                }
                tx_time_slot = !cell_data->odd;
                subject_set_int(tx_enabled, true);
                add_info("Start QSO with %s", cell_data->meta.call_de);
                msg_schedule_text_fmt("Next TX: %s", tx_msg.msg);
            } else {
                msg_schedule_text_fmt("Invalid message");
                tx_call_off();
            }
        }
    }
}

static void keyboard_open() {
    lv_group_remove_obj(table);
    textarea_window_open(keyboard_ok_cb, keyboard_cancel_cb);
    lv_obj_t *text = textarea_window_text();

    lv_textarea_set_accepted_chars(text,
        "0123456789"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    );

    if (strlen(params.ft8_cq_modifier.x) > 0) {
        textarea_window_set(params.ft8_cq_modifier.x);
    } else {
        lv_obj_t *text = textarea_window_text();
        lv_textarea_set_placeholder_text(text, " CQ modifier");
    }
    disable_buttons = true;
}

static void keyboard_close() {
    textarea_window_close();
    lv_group_add_obj(keyboard_group, table);
    lv_group_set_editing(keyboard_group, true);
    disable_buttons = false;
}

static bool keyboard_cancel_cb() {
    keyboard_close();
    return true;
}

static bool keyboard_ok_cb() {
    char *cq_mod = (char *)textarea_window_get();
    if ((strlen(cq_mod) > 0) && !is_cq_modifier(cq_mod)) {
        msg_schedule_text_fmt("Unsupported CQ modifier");
        return false;
    }
    params_str_set(&params.ft8_cq_modifier, cq_mod);
    keyboard_close();
    return true;
}

static void audio_cb(unsigned int n, float complex *samples) {
    if (state == RX_PROCESS) {
        pthread_mutex_lock(&audio_mutex);
        cbuffercf_write(audio_buf, samples, n);
        pthread_mutex_unlock(&audio_mutex);
    }
}

static bool get_time_slot(struct timespec now, float *sec_since_start) {
    bool cur_odd;
    float sec = (now.tv_sec % 60) + now.tv_nsec / 1.0e9f;

    switch (subject_get_int(cfg.ft8_protocol.val)) {
    case FTX_PROTOCOL_FT4:
        cur_odd = (int)(sec / FT4_SLOT_TIME) % 2;
        *sec_since_start = fmodf(sec, FT4_SLOT_TIME);
        break;

        case FTX_PROTOCOL_FT8:
        cur_odd = (int)(sec / FT8_SLOT_TIME) % 2;
        *sec_since_start = fmodf(sec, FT8_SLOT_TIME);
        break;
    }
    return cur_odd;
}


/**
 * Create CQ TX message
 */
static void make_cq_msg(const char *callsign, const char *qth, const char *cq_mod, char *text) {
    size_t text_len;
    if (strlen(cq_mod)) {
        snprintf(text, FTX_MAX_MESSAGE_LENGTH, "CQ_%s %s", cq_mod, callsign);
    } else {
        snprintf(text, FTX_MAX_MESSAGE_LENGTH, "CQ %s", callsign);
    }
    if (!subject_get_int(cfg.ft8_omit_cq_qth.val)){
        text_len = strlen(text);
        snprintf(text + text_len, FTX_MAX_MESSAGE_LENGTH - text_len, " %.4s", qth);
    }
}

/**
 * Get output level correction, based on output power and ALC
 */
static float get_correction() {
    static uint8_t msg_id = 0;
    float correction = 0.0f;
    float pwr, alc;

    if (tx_info_refresh(&msg_id, &alc, &pwr, NULL)) {
        float target_pwr = LV_MIN(subject_get_float(cfg.pwr.val), MAX_PWR);
        if (alc > 0.5f) {
            correction = log10f(log10f(11.1f - alc)) * 20.0f - 0.38f;
        } else if (target_pwr - pwr > 0.5f) {
            // TODO: check battery level
            correction = log10f(target_pwr / (pwr + 0.01f)) * 10.0f;
        }
    }
    return correction;
}

static void tx_worker() {
    const uint16_t signal_freq = 1325;
    int16_t       *samples;
    uint32_t       n_samples;

    if (!ftx_worker_generate_tx_samples(tx_msg.msg, signal_freq, AUDIO_PLAY_RATE, &samples, &n_samples)) {
        state = RX_PROCESS;
        return;
    }
    if (subject_get_float(cfg.pwr.val) > MAX_PWR) {
        radio_set_pwr(MAX_PWR);
    }

    float gain_offset = base_gain_offset + params.ft8_output_gain_offset.x;
    float play_gain_offset = audio_set_play_vol(gain_offset + 6.0f);
    gain_offset -= play_gain_offset;

    // Change freq before tx
    uint64_t radio_freq = subject_get_int(cfg_cur.fg_freq);
    radio_set_freq(radio_freq + params.ft8_tx_freq.x - signal_freq);
    radio_set_modem(true);

    float    prev_gain_offset = gain_offset;
    size_t   counter = 0;
    int16_t *ptr = samples;
    size_t   part;

    while (true) {
        if (counter > 30) {
            gain_offset += get_correction() * 0.4f;

            if (gain_offset > 0.0)
                gain_offset = 0.0f;
            else if (gain_offset < -30.0f)
                gain_offset = -30.0f;
        }
        if (n_samples <= 0 || state != TX_PROCESS) {
            state = RX_PROCESS;
            break;
        }
        part = LV_MIN(1024 * 2, n_samples);
        if (gain_offset == prev_gain_offset) {
            if (gain_offset != 0.0f) {
                audio_gain_db(ptr, part, gain_offset, ptr);
            }
        } else {
            // Smooth change gain
            audio_gain_db_transition(ptr, part, prev_gain_offset, gain_offset, ptr);
            prev_gain_offset = gain_offset;
        }
        audio_play(ptr, part);

        n_samples -= part;
        ptr += part;
        counter++;
    }
    params_float_set(&params.ft8_output_gain_offset, gain_offset - base_gain_offset + play_gain_offset);
    audio_play_wait();
    radio_set_modem(false);
    // Restore freq
    radio_set_freq(radio_freq);
    free(samples);
    audio_set_play_vol(params.play_gain_db_f.x);
}

/**
 * Add INFO message to the table
 */
static void add_info(const char * fmt, ...) {
    va_list     args;
    cell_data_t  cell_data;
    cell_data.cell_type = CELL_RX_INFO;

    va_start(args, fmt);
    vsnprintf(cell_data.text, sizeof(cell_data.text), fmt, args);
    va_end(args);

    scheduler_put(add_msg_cb, &cell_data, sizeof(cell_data_t));
}

/**
 * Add TX message to the table
 */
static void add_tx_text(const char * text) {
    cell_data_t  cell_data;
    cell_data.cell_type = CELL_TX_MSG;

    strncpy(cell_data.text, text, sizeof(cell_data.text) - 1);
    if (strncmp(cell_data.text, "CQ_", 3) == 0) {
        cell_data.text[2] = ' ';
    }
    scheduler_put(add_msg_cb, &cell_data, sizeof(cell_data_t));
}

/**
 * Parse and add RX messages to the table
 */
static void add_rx_text(int16_t snr, const char * text, slot_info_t *s_info, float freq_hz, float time_sec) {

    ftx_msg_meta_t meta;
    meta.freq_hz = freq_hz;
    meta.time_sec = time_sec;
    char * old_msg = strdup(tx_msg.msg);
    ftx_qso_processor_add_rx_text(qso_processor, text, snr, &meta, &tx_msg);

    if ((strlen(tx_msg.msg) > 0) && (strcmp(old_msg, tx_msg.msg) != 0)) {
        lv_finder_set_cursor(finder, meta.freq_hz);
        if (!subject_get_int(cfg.ft8_hold_freq.val)) {
            set_freq(freq_hz);
        }
        tx_time_slot = !s_info->odd;
        msg_schedule_text_fmt("Next TX: %s", tx_msg.msg);
        if (subject_get_int(cq_enabled)) {
            subject_set_int(cq_enabled, false);
        }
    }
    free(old_msg);

    ft8_cell_type_t cell_type;
    if (meta.to_me) {
        cell_type = CELL_RX_TO_ME;
    } else if (meta.type == FTX_MSG_TYPE_CQ) {
        cell_type = CELL_RX_CQ;
    } else if (!subject_get_int(cfg.ft8_show_all.val)) {
        return;
    } else {
        cell_type = CELL_RX_MSG;
    }

    cell_data_t  cell_data;
    if (meta.type == FTX_MSG_TYPE_CQ) {
        cell_data.worked_type = qso_log_search_worked(
            meta.call_de,
            subject_get_int(cfg.ft8_protocol.val) == FTX_PROTOCOL_FT8 ? MODE_FT8 : MODE_FT4,
            qso_log_freq_to_band(subject_get_int(cfg_cur.fg_freq))
        );
    }

    cell_data.cell_type = cell_type;
    strncpy(cell_data.text, text, sizeof(cell_data.text) - 1);
    cell_data.meta = meta;
    cell_data.odd = s_info->odd;
    if (params.qth.x[0] != 0) {
        if (strlen(meta.grid) > 0) {
            double lat, lon;
            qth_str_to_pos(meta.grid, &lat, &lon);
            cell_data.dist = qth_pos_dist(lat, lon, cur_lat, cur_lon);
        } else {
            cell_data.dist = 0;
        }
    } else {
        cell_data.dist = 0;
    }
    scheduler_put(add_msg_cb, (void*)&cell_data, sizeof(cell_data_t));
}

static void received_message_cb(const char *text, int snr, float freq_hz, float time_sec, void *user_data) {
    slot_info_t *s_info = (slot_info_t *)user_data;
    add_rx_text(snr, text, s_info, freq_hz, time_sec);
}

static void rx_worker(bool new_slot, slot_info_t *s_info) {
    unsigned int   n;
    float complex *buf;
    const int block_size = ftx_worker_get_block_size();
    const size_t   size = block_size * DECIM;

    pthread_mutex_lock(&audio_mutex);

    while (cbuffercf_size(audio_buf) > size) {
        cbuffercf_read(audio_buf, size, &buf, &n);

        firdecim_crcf_execute_block(decim, buf, block_size, decim_buf);
        cbuffercf_release(audio_buf, size);

        waterfall_process(decim_buf, block_size);

        ftx_worker_put_rx_samples(decim_buf, block_size);

        if (ftx_worker_is_full()) {
            ftx_worker_decode(received_message_cb, true, (void *)s_info);
            ftx_worker_reset();
        } else {
            ftx_worker_decode(received_message_cb, false, (void *)s_info);
        }
    }
    pthread_mutex_unlock(&audio_mutex);

    if (new_slot) {
        ftx_worker_decode(received_message_cb, true, (void *)s_info);
        ftx_worker_reset();
        ftx_qso_processor_start_new_slot(qso_processor);
    }
}

static void * decode_thread(void *arg) {
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

    struct timespec now;
    struct timespec slot_start_ts;
    bool            new_odd;
    struct tm      *ts;
    float           sec_since_slot_start;
    bool            new_slot    = false;
    bool            have_tx_msg = false;

    slot_info_t s_info = {.odd=false, .answer_generated=false};

    while (true) {
        clock_gettime(CLOCK_REALTIME, &now);
        new_odd = get_time_slot(now, &sec_since_slot_start);
        new_slot = new_odd != s_info.odd;
        rx_worker(new_slot, &s_info);
        s_info.odd = new_odd;

        have_tx_msg = tx_msg.msg[0] != '\0';

        if ((sec_since_slot_start < MAX_TX_START_DELAY) && have_tx_msg) {
            // Start TX and continue after done
            if ((tx_time_slot == new_odd) && subject_get_int(tx_enabled)) {
                state = TX_PROCESS;
                add_tx_text(tx_msg.msg);
                tx_worker();
                if (tx_msg.repeats > 0) {
                    tx_msg.repeats--;
                }
                if (tx_msg.repeats == 0){
                    if (strncmp(tx_msg.msg, "CQ", 2) == 0) {
                        subject_set_int(cq_enabled, false);
                    }
                    tx_msg.msg[0] = '\0';
                }
                continue;
            }
        }

        if (new_slot) {
            // Add message about new slot;
            state = RX_PROCESS;
            if ((!have_tx_msg || !subject_get_int(tx_enabled))) {
                ts = localtime(&now.tv_sec);
                add_info("RX %s %02i:%02i:%02i", cfg_digital_label_get(),
                    ts->tm_hour, ts->tm_min, ts->tm_sec);
            }
        } else {
            usleep(100000);
        }
    }

    return NULL;
}

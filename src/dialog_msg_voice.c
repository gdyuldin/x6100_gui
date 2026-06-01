/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

#include <time.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <math.h>
#include <sndfile.h>
#include <dirent.h>
#include <pthread.h>

#include <aether_radio/x6100_control/control.h>

#include "audio.h"
#include "dialog.h"
#include "dialog_msg_voice.h"
#include "styles.h"
#include "params/params.h"
#include "events.h"
#include "util.h"
#include "panel.h"
#include "keyboard.h"
#include "textarea_window.h"
#include "msg.h"
#include "meter.h"

#define BUF_SIZE 1024

typedef enum {
    VOICE_BEACON_OFF = 0,
    VOICE_BEACON_PLAY,
    VOICE_BEACON_IDLE,
} voice_beacon_t;

static msg_voice_state_t    state = MSG_VOICE_OFF;
static voice_beacon_t       beacon = VOICE_BEACON_OFF;
static char                 *path = "/mnt/msg";

static lv_obj_t             *table;
static int16_t              table_rows = 0;
static SNDFILE              *file = NULL;

static char                 *prev_filename;
static pthread_t            thread;
static int16_t              samples_buf[BUF_SIZE];

static void construct_cb(lv_obj_t *parent);
static void destruct_cb();
static void key_cb(lv_event_t * e);
static void rec_stop_cb(button_item_t *item);
static void play_stop_cb(button_item_t *item);
static void send_stop_cb(button_item_t *item);
static void beacon_stop_cb(button_item_t *item);

static void dialog_msg_voice_send_cb(button_item_t *item);
static void dialog_msg_voice_beacon_cb(button_item_t *item);
static void dialog_msg_voice_period_cb(button_item_t *item);

static void dialog_msg_voice_rec_cb(button_item_t *item);
static void dialog_msg_voice_play_cb(button_item_t *item);
static void dialog_msg_voice_rename_cb(button_item_t *item);
static void dialog_msg_voice_delete_cb(button_item_t *item);

static button_item_t btn_rec_stop = {
    .type  = BTN_TEXT,
    .label = "Rec\nStop",
    .press = rec_stop_cb,
};
static button_item_t btn_play_stop = {
    .type  = BTN_TEXT,
    .label = "Play\nStop",
    .press = play_stop_cb,
};
static button_item_t btn_send_stop = {
    .type  = BTN_TEXT,
    .label = "Send\nStop",
    .press = send_stop_cb,
};
static button_item_t btn_beacon_stop = {
    .type  = BTN_TEXT,
    .label = "Beacon\nStop",
    .press = beacon_stop_cb,
};

/* Msg Voice */

static button_item_t btn_msg_p1 = {
    .type  = BTN_TEXT,
    .label = "(MSG 1:2)",
    .press = button_next_page_cb,
};
static button_item_t btn_send = {
    .type  = BTN_TEXT,
    .label = "Send",
    .press = dialog_msg_voice_send_cb,
};
static button_item_t btn_beacon = {
    .type  = BTN_TEXT,
    .label = "Beacon",
    .press = dialog_msg_voice_beacon_cb,
};
static button_item_t btn_beacon_period = {
    .type  = BTN_TEXT,
    .label = "Beacon\nPeriod",
    .press = dialog_msg_voice_period_cb,
};

static buttons_page_t page_msg_voice_1 = {
    {
     &btn_msg_p1,
     &btn_send,
     &btn_beacon,
     &btn_beacon_period,
     }
};
static button_item_t btn_msg_p2 = {
    .type  = BTN_TEXT,
    .label = "(MSG 2:2)",
    .press = button_next_page_cb,
};
static button_item_t btn_rec = {
    .type  = BTN_TEXT,
    .label = "Rec",
    .press = dialog_msg_voice_rec_cb,
};
static button_item_t btn_rename = {
    .type  = BTN_TEXT,
    .label = "Rename",
    .press = dialog_msg_voice_rename_cb,
};
static button_item_t btn_delete = {
    .type  = BTN_TEXT,
    .label = "Delete",
    .press = dialog_msg_voice_delete_cb,
};
static button_item_t btn_play = {
    .type  = BTN_TEXT,
    .label = "Play",
    .press = dialog_msg_voice_play_cb,
};

static buttons_page_t page_msg_voice_2 = {
    {
     &btn_msg_p2,
     &btn_rec,
     &btn_rename,
     &btn_delete,
     &btn_play,
     }
};

static buttons_group_t group_msg_voice = {
    &page_msg_voice_1,
    &page_msg_voice_2,
};

static dialog_t             dialog = {
    .run = false,
    .construct_cb = construct_cb,
    .destruct_cb = destruct_cb,
    .btn_page = &page_msg_voice_1,
    .audio_cb = NULL,
    .key_cb = NULL
};

dialog_t                    *dialog_msg_voice = &dialog;

static void load_table() {
    table_rows = 0;

    DIR             *dp;
    struct dirent   *ep;

    dp = opendir(path);

    if (dp != NULL) {
        while ((ep = readdir(dp)) != NULL) {
            if (strcmp(ep->d_name, ".") == 0 || strcmp(ep->d_name, "..") == 0) {
                continue;
            }

            lv_table_set_cell_value(table, table_rows++, 0, ep->d_name);
        }

        closedir(dp);
    }
    if (table_rows > 0) {
        lv_table_set_row_cnt(table, table_rows);
    } else {
        lv_table_set_cell_value(table, table_rows++, 0, "");
        lv_table_set_row_cnt(table, 1);
    }
}

static bool create_file() {
    SF_INFO sfinfo;

    memset(&sfinfo, 0, sizeof(sfinfo));

    sfinfo.samplerate = AUDIO_CAPTURE_RATE;
    sfinfo.channels = 1;
    sfinfo.format = SF_FORMAT_MPEG | SF_FORMAT_MPEG_LAYER_III;

    char        filename[64];
    time_t      now = time(NULL);
    struct tm   *t = localtime(&now);

    snprintf(filename, sizeof(filename),
        "%s/MSG_%04i%02i%02i_%02i%02i%02i.mp3",
        path, t->tm_year + 1900, t->tm_mon + 1, t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec
    );

    file = sf_open(filename, SFM_WRITE, &sfinfo);

    if (file == NULL) {
        const char* err = sf_strerror(NULL);
        LV_LOG_ERROR("Problem with create file: %s", err);
        return false;
    }

    return true;
}

static void close_file() {
    sf_close(file);
}

static const char* get_item() {
    if (table_rows == 0) {
        return NULL;
    }

    int16_t     row = 0;
    int16_t     col = 0;

    lv_table_get_selected_cell(table, &row, &col);

    if (row == LV_TABLE_CELL_NONE) {
        return NULL;
    }

    return lv_table_get_cell_value(table, row, col);
}

static void play_item() {
    const char *item = get_item();

    if (!item) {
        return;
    }

    char filename[64];

    strcpy(filename, path);
    strcat(filename, "/");
    strcat(filename, item);

    SF_INFO sfinfo;

    memset(&sfinfo, 0, sizeof(sfinfo));

    SNDFILE *file = sf_open(filename, SFM_READ, &sfinfo);

    if (!file) {
        return;
    }

    state = MSG_VOICE_PLAY;
    while (state == MSG_VOICE_PLAY) {
        int res = sf_read_short(file, samples_buf, BUF_SIZE);

        if (res > 0) {
            audio_play(samples_buf, res);
        } else {
            state = MSG_VOICE_OFF;
        }
    }

    sf_close(file);
    audio_play_wait();
}

static void * play_thread(void *arg) {
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

    audio_set_play_mode(AUDIO_PLAY_ON);
    play_item();
    audio_set_play_mode(AUDIO_PLAY_OFF);

    if (dialog.run) {
        buttons_unload_page();
        buttons_load_page(&page_msg_voice_2);
    }
}

static void * send_thread(void *arg) {
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

    msg_update_text_fmt("Sending message");

    radio_set_ptt(true);
    play_item();
    radio_set_ptt(false);

    if (dialog.run) {
        buttons_unload_page();
        buttons_load_page(&page_msg_voice_1);
    }
}

static void * beacon_thread(void *arg) {
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

    while (true) {
        switch (beacon) {
            case VOICE_BEACON_OFF:
                buttons_unload_page();
                buttons_load_page(&page_msg_voice_1);
                return NULL;

            case VOICE_BEACON_PLAY:
                msg_update_text_fmt("Sending message");
                radio_set_ptt(true);
                play_item();
                radio_set_ptt(false);
                break;

            case VOICE_BEACON_IDLE:
                msg_update_text_fmt("Beacon pause: %i s", params.voice_msg_period);
                sleep(params.voice_msg_period);
                break;
        }

        switch (beacon) {
            case VOICE_BEACON_PLAY:
                beacon = VOICE_BEACON_IDLE;
                break;

            case VOICE_BEACON_IDLE:
                beacon = VOICE_BEACON_PLAY;
                break;
        }
    }
    return NULL;
}

static bool textarea_window_close_cb() {
    lv_group_add_obj(keyboard_group, table);
    lv_group_set_editing(keyboard_group, true);

    free(prev_filename);
    prev_filename = NULL;
    return true;
}

static bool textarea_window_edit_ok_cb() {
    const char *new_filename = textarea_window_get();

    if (strcmp(prev_filename, new_filename) != 0) {
        char prev[64];
        char new[64];

        snprintf(prev, sizeof(prev), "%s/%s", path, prev_filename);
        snprintf(new, sizeof(new), "%s/%s", path, new_filename);

        if (rename(prev, new) == 0) {
            load_table();
            textarea_window_close_cb();
        }
    } else {
        free(prev_filename);
        prev_filename = NULL;
    }
    return true;
}

static void tx_cb(lv_event_t * e) {
    if (beacon == VOICE_BEACON_IDLE) {
        pthread_cancel(thread);
        pthread_join(thread, NULL);
        beacon = VOICE_BEACON_OFF;

        buttons_unload_page();
        buttons_load_page(&page_msg_voice_1);
    }
}

static void construct_cb(lv_obj_t *parent) {
    dialog.obj = dialog_init(parent);

    page_msg_voice_1.items[0]->next = &page_msg_voice_2;
    page_msg_voice_1.items[0]->prev = &page_msg_voice_2;
    page_msg_voice_2.items[0]->next = &page_msg_voice_1;
    page_msg_voice_2.items[0]->prev = &page_msg_voice_1;

    lv_obj_add_event_cb(dialog.obj, tx_cb, EVENT_RADIO_TX, NULL);

    table = lv_table_create(dialog.obj);

    lv_obj_remove_style(table, NULL, LV_STATE_ANY | LV_PART_MAIN);

    lv_obj_set_size(table, 775, 325);

    lv_table_set_col_cnt(table, 1);
    lv_table_set_col_width(table, 0, 770);

    lv_obj_set_style_border_width(table, 0, LV_PART_ITEMS);

    lv_obj_set_style_bg_opa(table, LV_OPA_TRANSP, LV_PART_ITEMS);
    lv_obj_set_style_text_color(table, lv_color_white(), LV_PART_ITEMS);
    lv_obj_set_style_pad_top(table, 5, LV_PART_ITEMS);
    lv_obj_set_style_pad_bottom(table, 5, LV_PART_ITEMS);
    lv_obj_set_style_pad_left(table, 0, LV_PART_ITEMS);
    lv_obj_set_style_pad_right(table, 0, LV_PART_ITEMS);

    lv_obj_set_style_text_color(table, lv_color_black(), LV_PART_ITEMS | LV_STATE_EDITED);
    lv_obj_set_style_bg_color(table, lv_color_white(), LV_PART_ITEMS | LV_STATE_EDITED);
    lv_obj_set_style_bg_opa(table, 128, LV_PART_ITEMS | LV_STATE_EDITED);

    lv_obj_add_event_cb(table, key_cb, LV_EVENT_KEY, NULL);
    lv_group_add_obj(keyboard_group, table);
    lv_group_set_editing(keyboard_group, true);

    lv_obj_center(table);

    mkdir(path, 0755);
    load_table();
}

static void destruct_cb() {
    audio_set_play_mode(AUDIO_PLAY_OFF);

    if (beacon == VOICE_BEACON_IDLE) {
        pthread_cancel(thread);
        pthread_join(thread, NULL);
    }

    beacon = VOICE_BEACON_OFF;
    state = MSG_VOICE_OFF;
    textarea_window_close();
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

void dialog_msg_voice_send_cb(button_item_t *item) {
    if (state == MSG_VOICE_OFF) {
        pthread_create(&thread, NULL, send_thread, NULL);

        buttons_unload_page();
        buttons_load(1, &btn_send_stop);
    }
}

static void send_stop_cb(button_item_t *iteme) {
    state = MSG_VOICE_OFF;
}

void dialog_msg_voice_beacon_cb(button_item_t *item) {
    if (state == MSG_VOICE_OFF) {
        if (get_item()) {
            beacon = VOICE_BEACON_PLAY;
            pthread_create(&thread, NULL, beacon_thread, NULL);

            buttons_unload_page();
            buttons_load(2, &btn_beacon_stop);
        }
    }
}

static void beacon_stop_cb(button_item_t *item) {
    switch (state) {
        case MSG_VOICE_OFF:
            pthread_cancel(thread);
            pthread_join(thread, NULL);
            beacon = VOICE_BEACON_OFF;

            buttons_unload_page();
            buttons_load_page(&page_msg_voice_1);
            break;

        case MSG_VOICE_PLAY:
            beacon = VOICE_BEACON_OFF;
            state = MSG_VOICE_OFF;
            break;

        default:
            break;
    }
}

void dialog_msg_voice_period_cb(button_item_t *item) {
    params_lock();

    switch (params.voice_msg_period) {
        case 10:
            params.voice_msg_period = 30;
            break;

        case 30:
            params.voice_msg_period = 60;
            break;

        case 60:
            params.voice_msg_period = 120;
            break;

        case 120:
            params.voice_msg_period = 10;
            break;
    }

    params_unlock(&params.dirty.voice_msg_period);
    msg_update_text_fmt("Beacon period: %i s", params.voice_msg_period);
}

void dialog_msg_voice_rec_cb(button_item_t *item) {
    if (state == MSG_VOICE_OFF) {
        if (create_file()) {
            audio_set_play_mode(AUDIO_PLAY_VOICE_REC);
            state = MSG_VOICE_RECORD;

            buttons_unload_page();
            buttons_load(1, &btn_rec_stop);
        }
    }
}

static void rec_stop_cb(button_item_t *item) {
    buttons_unload_page();
    buttons_load_page(&page_msg_voice_2);

    audio_set_play_mode(AUDIO_PLAY_OFF);
    state = MSG_VOICE_OFF;
    close_file();
    load_table();
}

void dialog_msg_voice_play_cb(button_item_t *item) {
    if (state == MSG_VOICE_OFF) {
        pthread_create(&thread, NULL, play_thread, NULL);

        buttons_unload_page();
        buttons_load(4, &btn_play_stop);
    }
}

void play_stop_cb(button_item_t *item) {
    state = MSG_VOICE_OFF;
}

void dialog_msg_voice_rename_cb(button_item_t *item) {
    prev_filename = strdup(get_item());

    if (prev_filename) {
        lv_group_remove_obj(table);
        textarea_window_open(textarea_window_edit_ok_cb, textarea_window_close_cb);
        textarea_window_set(prev_filename);
    }
}

void dialog_msg_voice_delete_cb(button_item_t *item) {
    const char *name = get_item();

    if (name) {
        char filename[64];

        strcpy(filename, path);
        strcat(filename, "/");
        strcat(filename, name);

        unlink(filename);
        load_table();
    }
}

msg_voice_state_t dialog_msg_voice_get_state() {
    return state;
}

void dialog_msg_voice_put_audio_samples(size_t nsamples, int16_t *samples) {
    int16_t peak = 0;

    for (uint16_t i = 0; i < nsamples; i++) {
        int16_t x = abs(samples[i]);

        if (x > peak) {
            peak = x;
        }
    }

    peak = S1 + (peak / 32768.0) * (S9_40 - S1);
    meter_update(peak, 0.25f);
    sf_write_short(file, samples, nsamples);
}

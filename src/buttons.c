/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

#include "styles.h"
#include "main_screen.h"
#include "buttons.h"
#include "mfk.h"
#include "vol.h"
#include "main.h"
#include "msg.h"
#include "rtty.h"
#include "pannel.h"
#include "params.h"
#include "dialog.h"
#include "dialog_settings.h"
#include "dialog_swrscan.h"
#include "dialog_ft8.h"
#include "dialog_freq.h"
#include "dialog_gps.h"
#include "dialog_msg_cw.h"
#include "dialog_msg_voice.h"
#include "dialog_recorder.h"
#include "voice.h"

#define BUTTONS     5

typedef struct {
    lv_obj_t        *obj;
    button_item_t   *item;
} button_t;

static uint8_t      btn_height = 62;
static button_t     btn[BUTTONS];
static lv_obj_t     *parent_obj = NULL;

static void button_next_page_cb(lv_event_t * e);
static void button_app_page_cb(lv_event_t * e);
static void button_vol_update_cb(lv_event_t * e);
static void button_mfk_update_cb(lv_event_t * e);
static void button_mem_load_cb(lv_event_t * e);

static void button_prev_page_cb(void * ptr);
static void button_vol_hold_cb(void * ptr);
static void button_mfk_hold_cb(void * ptr);
static void button_mem_save_cb(void * ptr);

static void button_action_cb(lv_event_t * e);

static button_page_t    buttons_page = PAGE_VOL_1;

static button_item_t    buttons[] = {
    { .label = "(VOL 1:4)",         .press = button_next_page_cb,   .hold = button_prev_page_cb,    .next = PAGE_VOL_2, .prev = PAGE_MEM_2, .voice = "Volume|page 1" },
    { .label = "Audio\nVol",        .press = button_vol_update_cb,                                  .data = VOL_VOL },
    { .label = "SQL",               .press = button_vol_update_cb,  .hold = button_vol_hold_cb,     .data = VOL_SQL },
    { .label = "RFG",               .press = button_vol_update_cb,  .hold = button_vol_hold_cb,     .data = VOL_RFG },
    { .label = "TX\nPower",         .press = button_vol_update_cb,  .hold = button_vol_hold_cb,     .data = VOL_PWR },

    { .label = "(VOL 2:4)",         .press = button_next_page_cb,   .hold = button_prev_page_cb,    .next = PAGE_VOL_3, .prev = PAGE_VOL_1, .voice = "Volume|page 2" },
    { .label = "Filter\nLow",       .press = button_vol_update_cb,  .hold = button_vol_hold_cb,     .data = VOL_FILTER_LOW },
    { .label = "Filter\nHigh",      .press = button_vol_update_cb,  .hold = button_vol_hold_cb,     .data = VOL_FILTER_HIGH },
    { .label = "Speaker\nMode",     .press = button_vol_update_cb,  .hold = button_vol_hold_cb,     .data = VOL_SPMODE },
    { .label = "",                  .press = NULL },

    { .label = "(VOL 3:4)",         .press = button_next_page_cb,   .hold = button_prev_page_cb,    .next = PAGE_VOL_4, .prev = PAGE_VOL_2, .voice = "Volume|page 3" },
    { .label = "MIC\nSelect",       .press = button_vol_update_cb,  .hold = button_vol_hold_cb,     .data = VOL_MIC },
    { .label = "H-MIC\nGain",       .press = button_vol_update_cb,  .hold = button_vol_hold_cb,     .data = VOL_HMIC },
    { .label = "I-MIC\nGain",       .press = button_vol_update_cb,  .hold = button_vol_hold_cb,     .data = VOL_IMIC },
    { .label = "Moni\nLevel",       .press = button_vol_update_cb,  .hold = button_vol_hold_cb,     .data = VOL_MONI },

    { .label = "(VOL 4:4)",         .press = button_next_page_cb,   .hold = button_prev_page_cb,    .next = PAGE_MFK_1, .prev = PAGE_VOL_3, .voice = "Volume|page 4" },
    { .label = "Voice",             .press = button_vol_update_cb,  .hold = button_vol_hold_cb,     .data = VOL_VOICE_LANG },
    { .label = "Voice\nRate",       .press = button_vol_update_cb,  .hold = button_vol_hold_cb,     .data = VOL_VOICE_RATE },
    { .label = "Voice\nPitch",      .press = button_vol_update_cb,  .hold = button_vol_hold_cb,     .data = VOL_VOICE_PITCH },
    { .label = "Voice\nVolume",     .press = button_vol_update_cb,  .hold = button_vol_hold_cb,     .data = VOL_VOICE_VOLUME },
    
    { .label = "(MFK 1:4)",         .press = button_next_page_cb,   .hold = button_prev_page_cb,    .next = PAGE_MFK_2, .prev = PAGE_VOL_4, .voice = "MFK|page 1" },
    { .label = "Min\nLevel",        .press = button_mfk_update_cb,  .hold = button_mfk_hold_cb,     .data = MFK_MIN_LEVEL },
    { .label = "Max\nLevel",        .press = button_mfk_update_cb,  .hold = button_mfk_hold_cb,     .data = MFK_MAX_LEVEL },
    { .label = "Spectrum\nZoom",    .press = button_mfk_update_cb,  .hold = button_mfk_hold_cb,     .data = MFK_SPECTRUM_FACTOR },
    { .label = "Spectrum\nBeta",    .press = button_mfk_update_cb,  .hold = button_mfk_hold_cb,     .data = MFK_SPECTRUM_BETA },

    { .label = "(MFK 2:4)",         .press = button_next_page_cb,   .hold = button_prev_page_cb,    .next = PAGE_MFK_3, .prev = PAGE_MFK_1, .voice = "MFK|page 2" },
    { .label = "Spectrum\nFill",    .press = button_mfk_update_cb,  .hold = button_mfk_hold_cb,     .data = MFK_SPECTRUM_FILL },
    { .label = "Spectrum\nPeak",    .press = button_mfk_update_cb,  .hold = button_mfk_hold_cb,     .data = MFK_SPECTRUM_PEAK },
    { .label = "Peaks\nHold",       .press = button_mfk_update_cb,  .hold = button_mfk_hold_cb,     .data = MFK_PEAK_HOLD },
    { .label = "Peaks\nSpeed",      .press = button_mfk_update_cb,  .hold = button_mfk_hold_cb,     .data = MFK_PEAK_SPEED },

    { .label = "(MFK 3:4)",         .press = button_next_page_cb,   .hold = button_prev_page_cb,    .next = PAGE_MFK_4, .prev = PAGE_MFK_2, .voice = "MFK|page 3" },
    { .label = "Charger",           .press = button_mfk_update_cb,  .hold = button_mfk_hold_cb,     .data = MFK_CHARGER },
    { .label = "Antenna",           .press = button_mfk_update_cb,  .hold = button_mfk_hold_cb,     .data = MFK_ANT },
    { .label = "RIT",               .press = button_mfk_update_cb,  .hold = button_mfk_hold_cb,     .data = MFK_RIT },
    { .label = "XIT",               .press = button_mfk_update_cb,  .hold = button_mfk_hold_cb,     .data = MFK_XIT },

    { .label = "(MFK 4:4)",         .press = button_next_page_cb,   .hold = button_prev_page_cb,    .next = PAGE_MEM_1, .prev = PAGE_MFK_3, .voice = "MFK|page 4" },
    { .label = "AGC\nHang",         .press = button_mfk_update_cb,  .hold = button_mfk_hold_cb,     .data = MFK_AGC_HANG },
    { .label = "AGC\nKnee",         .press = button_mfk_update_cb,  .hold = button_mfk_hold_cb,     .data = MFK_AGC_KNEE },
    { .label = "AGC\nSlope",        .press = button_mfk_update_cb,  .hold = button_mfk_hold_cb,     .data = MFK_AGC_SLOPE },
    { .label = "",                  .press = NULL },

    { .label = "(MEM 1:2)",         .press = button_next_page_cb,   .hold = button_prev_page_cb,    .next = PAGE_MEM_2, .prev = PAGE_MFK_4, .voice = "Memory|page 1" },
    { .label = "Set 1",             .press = button_mem_load_cb,    .hold = button_mem_save_cb,     .data = 1 },
    { .label = "Set 2",             .press = button_mem_load_cb,    .hold = button_mem_save_cb,     .data = 2 },
    { .label = "Set 3",             .press = button_mem_load_cb,    .hold = button_mem_save_cb,     .data = 3 },
    { .label = "Set 4",             .press = button_mem_load_cb,    .hold = button_mem_save_cb,     .data = 4 },

    { .label = "(MEM 2:2)",         .press = button_next_page_cb,   .hold = button_prev_page_cb,    .next = PAGE_VOL_1, .prev = PAGE_MEM_1, .voice = "Memory|page 2" },
    { .label = "Set 5",             .press = button_mem_load_cb,    .hold = button_mem_save_cb,     .data = 5 },
    { .label = "Set 6",             .press = button_mem_load_cb,    .hold = button_mem_save_cb,     .data = 6 },
    { .label = "Set 7",             .press = button_mem_load_cb,    .hold = button_mem_save_cb,     .data = 7 },
    { .label = "Set 8",             .press = button_mem_load_cb,    .hold = button_mem_save_cb,     .data = 8 },

    /* CW */
    
    { .label = "(KEY 1:2)",         .press = button_next_page_cb,   .hold = button_prev_page_cb,    .next = PAGE_KEY_2, .prev = PAGE_CW_DECODER_1, .voice = "Key|page 1" },
    { .label = "Speed",             .press = button_mfk_update_cb,  .hold = button_mfk_hold_cb,     .data = MFK_KEY_SPEED },
    { .label = "Volume",            .press = button_mfk_update_cb,  .hold = button_mfk_hold_cb,     .data = MFK_KEY_VOL },
    { .label = "Train",             .press = button_mfk_update_cb,  .hold = button_mfk_hold_cb,     .data = MFK_KEY_TRAIN },
    { .label = "Tone",              .press = button_mfk_update_cb,  .hold = button_mfk_hold_cb,     .data = MFK_KEY_TONE },
    
    { .label = "(KEY 2:2)",         .press = button_next_page_cb,   .hold = button_prev_page_cb,    .next = PAGE_CW_DECODER_1, .prev = PAGE_KEY_1, .voice = "Key|page 2" },
    { .label = "Key\nMode",         .press = button_mfk_update_cb,  .hold = button_mfk_hold_cb,     .data = MFK_KEY_MODE },
    { .label = "Iambic\nMode",      .press = button_mfk_update_cb,  .hold = button_mfk_hold_cb,     .data = MFK_IAMBIC_MODE },
    { .label = "QSK\nTime",         .press = button_mfk_update_cb,  .hold = button_mfk_hold_cb,     .data = MFK_QSK_TIME },
    { .label = "Ratio",             .press = button_mfk_update_cb,  .hold = button_mfk_hold_cb,     .data = MFK_KEY_RATIO },

    { .label = "(CW 1:1)",          .press = button_next_page_cb,   .hold = button_prev_page_cb,    .next = PAGE_KEY_1, .prev = PAGE_KEY_2, .voice = "CW page" },
    { .label = "CW\nDecoder",       .press = button_mfk_update_cb,  .hold = button_mfk_hold_cb,     .data = MFK_CW_DECODER },
    { .label = "CW\nSNR",           .press = button_mfk_update_cb,  .hold = button_mfk_hold_cb,     .data = MFK_CW_DECODER_SNR },
    { .label = "CW Peak\nBeta",     .press = button_mfk_update_cb,  .hold = button_mfk_hold_cb,     .data = MFK_CW_DECODER_PEAK_BETA },
    { .label = "CW Noise\nBeta",    .press = button_mfk_update_cb,  .hold = button_mfk_hold_cb,     .data = MFK_CW_DECODER_NOISE_BETA },
    
    /* DSP */

    { .label = "(DFN 1:3)",         .press = button_next_page_cb,   .hold = button_prev_page_cb,    .next = PAGE_DFN_2, .prev = PAGE_DFN_3, .voice = "DNF page" },
    { .label = "DNF",               .press = button_mfk_update_cb,  .hold = button_mfk_hold_cb,     .data = MFK_DNF },
    { .label = "DNF\nCenter",       .press = button_mfk_update_cb,  .hold = button_mfk_hold_cb,     .data = MFK_DNF_CENTER },
    { .label = "DNF\nWidth",        .press = button_mfk_update_cb,  .hold = button_mfk_hold_cb,     .data = MFK_DNF_WIDTH },
    { .label = "",                  .press = NULL },

    { .label = "(DFN 2:3)",         .press = button_next_page_cb,   .hold = button_prev_page_cb,    .next = PAGE_DFN_3, .prev = PAGE_DFN_1, .voice = "NB page" },
    { .label = "NB",                .press = button_mfk_update_cb,  .hold = button_mfk_hold_cb,     .data = MFK_NB },
    { .label = "NB\nLevel",         .press = button_mfk_update_cb,  .hold = button_mfk_hold_cb,     .data = MFK_NB_LEVEL },
    { .label = "NB\nWidth",         .press = button_mfk_update_cb,  .hold = button_mfk_hold_cb,     .data = MFK_NB_WIDTH },
    { .label = "",                  .press = NULL },

    { .label = "(DFN 3:3)",         .press = button_next_page_cb,   .hold = button_prev_page_cb,    .next = PAGE_DFN_1, .prev = PAGE_DFN_2, .voice = "NR page" },
    { .label = "NR",                .press = button_mfk_update_cb,  .hold = button_mfk_hold_cb,     .data = MFK_NR },
    { .label = "NR\nLevel",         .press = button_mfk_update_cb,  .hold = button_mfk_hold_cb,     .data = MFK_NR_LEVEL },
    { .label = "",                  .press = NULL },
    { .label = "",                  .press = NULL },

    /* APP */

    { .label = "(APP 1:2)",         .press = button_next_page_cb,   .next = PAGE_APP_2, .voice = "Application|page 1" },
    { .label = "RTTY",              .press = button_app_page_cb,    .data = PAGE_RTTY },
    { .label = "FT8",               .press = button_app_page_cb,    .data = PAGE_FT8 },
    { .label = "SWR\nScan",         .press = button_app_page_cb,    .data = PAGE_SWRSCAN },
    { .label = "GPS",               .press = button_app_page_cb,    .data = PAGE_GPS },

    { .label = "(APP 2:2)",         .press = button_next_page_cb,   .next = PAGE_APP_1, .voice = "Application|page 2" },
    { .label = "Recorder",          .press = button_app_page_cb,    .data = PAGE_RECORDER },
    { .label = "QTH",               .press = button_action_cb,      .data = ACTION_APP_QTH },
    { .label = "Callsign",          .press = button_action_cb,      .data = ACTION_APP_CALLSIGN },
    { .label = "Settings",          .press = button_app_page_cb,    .data = PAGE_SETTINGS },

    /* RTTY */

    { .label = "(RTTY 1:1)",        .press = NULL },
    { .label = "Rate",              .press = button_mfk_update_cb,  .data = MFK_RTTY_RATE },
    { .label = "Shift",             .press = button_mfk_update_cb,  .data = MFK_RTTY_SHIFT },
    { .label = "Center",            .press = button_mfk_update_cb,  .data = MFK_RTTY_CENTER },
    { .label = "Reverse",           .press = button_mfk_update_cb,  .data = MFK_RTTY_REVERSE },

    /* Settings */

    { .label = "",                  .press = NULL },
    { .label = "",                  .press = NULL },
    { .label = "",                  .press = NULL },
    { .label = "",                  .press = NULL },
    { .label = "",                  .press = NULL },

    /* SWR Scan */

    { .label = "Run",               .press = dialog_swrscan_run_cb },
    { .label = "Scale",             .press = dialog_swrscan_scale_cb },
    { .label = "Span",              .press = dialog_swrscan_span_cb },
    { .label = "",                  .press = NULL },
    { .label = "",                  .press = NULL },

    /* FT8 */

    { .label = "",                  .press = NULL },
    { .label = "",                  .press = NULL },
    { .label = "",                  .press = NULL },
    { .label = "",                  .press = NULL },
    { .label = "",                  .press = NULL },

    /* GPS */

    { .label = "",                  .press = NULL },
    { .label = "",                  .press = NULL },
    { .label = "",                  .press = NULL },
    { .label = "",                  .press = NULL },
    { .label = "",                  .press = NULL },

    /* Msg CW */

    { .label = "(MSG 1:2)",         .press = button_next_page_cb,   .next = PAGE_MSG_CW_2 },
    { .label = "Send",              .press = dialog_msg_cw_send_cb },
    { .label = "Beacon",            .press = dialog_msg_cw_beacon_cb },
    { .label = "Beacon\nPeriod",    .press = dialog_msg_cw_period_cb },
    { .label = "",                  .press = NULL },

    { .label = "(MSG 2:2)",         .press = button_next_page_cb,   .next = PAGE_MSG_CW_1 },
    { .label = "New",               .press = dialog_msg_cw_new_cb },
    { .label = "Edit",              .press = dialog_msg_cw_edit_cb },
    { .label = "Delete",            .press = dialog_msg_cw_delete_cb },
    { .label = "",                  .press = NULL },

    /* Msg Voice */

    { .label = "(MSG 1:2)",         .press = button_next_page_cb,   .next = PAGE_MSG_VOICE_2 },
    { .label = "Send",              .press = dialog_msg_voice_send_cb },
    { .label = "Beacon",            .press = dialog_msg_voice_beacon_cb },
    { .label = "Beacon\nPeriod",    .press = dialog_msg_voice_period_cb },
    { .label = "",                  .press = NULL },

    { .label = "(MSG 2:2)",         .press = button_next_page_cb,   .next = PAGE_MSG_VOICE_1 },
    { .label = "Rec",               .press = dialog_msg_voice_rec_cb },
    { .label = "Rename",            .press = dialog_msg_voice_rename_cb },
    { .label = "Delete",            .press = dialog_msg_voice_delete_cb },
    { .label = "Play",              .press = dialog_msg_voice_play_cb },
    
    /* Recorder */

    { .label = "(REC 1:1)",         .press = NULL },
    { .label = "Rec",               .press = dialog_recorder_rec_cb },
    { .label = "Rename",            .press = dialog_recorder_rename_cb },
    { .label = "Delete",            .press = dialog_recorder_delete_cb },
    { .label = "Play",              .press = dialog_recorder_play_cb },
};

void buttons_init(lv_obj_t *parent) {
    uint16_t y = 480 - btn_height;
    uint16_t x = 0;
    uint16_t width = 152;

    for (uint8_t i = 0; i < 5; i++) {
        lv_obj_t *f = lv_btn_create(parent);
        
        lv_obj_remove_style_all(f); 
        lv_obj_add_style(f, &btn_style, 0);

        lv_obj_set_pos(f, x, y);
        lv_obj_set_size(f, width, btn_height);

        x += width + 10;
        
        lv_obj_t *label = lv_label_create(f);
        
        lv_obj_center(label);
        lv_obj_set_user_data(f, label);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);

        btn[i].obj = f;
    }

    parent_obj = parent;
}

void buttons_load(uint8_t n, button_item_t *item) {
    lv_obj_t        *label = lv_obj_get_user_data(btn[n].obj);

    lv_obj_remove_event_cb(btn[n].obj, NULL);
    lv_obj_add_event_cb(btn[n].obj, item->press, LV_EVENT_PRESSED, item);
    lv_label_set_text(label, item->label);

    btn[n].item = item;
}

void buttons_load_page(button_page_t page) {
    buttons_page = page;

    for (uint8_t i = 0; i < BUTTONS; i++) {
        buttons_load(i, &buttons[buttons_page * BUTTONS + i]);
    }
}

void buttons_unload_page() {
    for (uint8_t i = 0; i < BUTTONS; i++) {
        lv_obj_t        *label = lv_obj_get_user_data(btn[i].obj);

        lv_obj_remove_event_cb(btn[i].obj, NULL);
        lv_label_set_text(label, "");
        btn[i].item = NULL;
    }
}

static void button_next_page_cb(lv_event_t * e) {
    button_item_t *item = lv_event_get_user_data(e);

    buttons_unload_page();
    buttons_load_page(item->next);
    
    char *voice = buttons[item->next * BUTTONS].voice;
    
    if (voice) {
        voice_say_text_fmt("%s", voice);
    }
}

static void button_app_page_cb(lv_event_t * e) {
    button_item_t *item = lv_event_get_user_data(e);

    main_screen_app(item->data);
}

static void button_action_cb(lv_event_t * e) {
    button_item_t *item = lv_event_get_user_data(e);

    main_screen_action(item->data);
}

static void button_vol_update_cb(lv_event_t * e) {
    button_item_t *item = lv_event_get_user_data(e);
    encoder_set_mode(vol, (void *) &item->data);
}

static void button_mfk_update_cb(lv_event_t * e) {
    button_item_t *item = lv_event_get_user_data(e);
    encoder_set_mode(mfk, (void *) &item->data);
}

static void button_prev_page_cb(void * ptr) {
    button_item_t *item = (button_item_t*) ptr;

    buttons_unload_page();
    buttons_load_page(item->prev);

    char *voice = buttons[item->prev * BUTTONS].voice;
    
    if (voice) {
        voice_say_text_fmt("%s", voice);
    }
}

static void button_vol_hold_cb(void * ptr) {
    button_item_t   *item = (button_item_t*) ptr;
    uint64_t        mask = (uint64_t) 1L << item->data;

    params_lock();
    params.vol_modes ^= mask;
    params_unlock(&params.durty.vol_modes);
    
    if (params.vol_modes & mask) {
        msg_set_text_fmt("Added to VOL encoder");
        voice_say_text_fmt("Added to volume encoder");
    } else {
        msg_set_text_fmt("Removed from VOL encoder");
        voice_say_text_fmt("Romoved to volume encoder");
    }
}

static void button_mfk_hold_cb(void * ptr) {
    button_item_t   *item = (button_item_t*) ptr;
    uint64_t        mask = (uint64_t) 1L << item->data;

    params_lock();
    params.mfk_modes ^= mask;
    params_unlock(&params.durty.mfk_modes);
    
    if (params.mfk_modes & mask) {
        msg_set_text_fmt("Added to MFK encoder");
        voice_say_text_fmt("Added to MFK encoder");
    } else {
        msg_set_text_fmt("Removed from MFK encoder");
        voice_say_text_fmt("Removed from MFK encoder");
    }
}

static void button_mem_load_cb(lv_event_t * e) {
    button_item_t *item = lv_event_get_user_data(e);

    mem_load(item->data);
    voice_say_text_fmt("Memory %i loaded", item->data);
}

static void button_mem_save_cb(void * ptr) {
    button_item_t   *item = (button_item_t*) ptr;
 
    mem_save(item->data);
    voice_say_text_fmt("Memory %i stored", item->data);
}

void buttons_press(uint8_t n, bool hold) {
    if (hold) {
        button_item_t *item = btn[n].item;
        
        if (item != NULL && item->hold) {
            item->hold((void *)item);
        }
    } else {
        lv_event_send(btn[n].obj, LV_EVENT_PRESSED, NULL);
        lv_event_send(btn[n].obj, LV_EVENT_RELEASED, NULL);
    }
}

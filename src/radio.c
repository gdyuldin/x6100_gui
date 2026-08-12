/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

/*********************
 *      INCLUDES
 *********************/
#include "radio.h"

#include <unistd.h>
#include <stdio.h>
#include <pthread.h>
#include <string.h>

#include <aether_radio/x6100_control/low/flow.h>
#include <aether_radio/x6100_control/low/gpio.h>

#include "cfg/cfg_api.h"
#include "cfg/db.h"
#include "util.h"
#include "dsp.h"
#include "params/params.h"
#include "hkey.h"
#include "tx_info.h"
#include "info.h"
#include "dialog_swrscan.h"
#include "cw.h"
#include "pubsub_ids.h"

/*********************
 *      DEFINES
 *********************/

#define FLOW_RESTART_TIMEOUT 300
#define IDLE_TIMEOUT        (3 * 1000)

#define FILTER_2_OFFSET 60
#define FILTER_2_OFFSET_IN 20
#define FILTER_2_OFFSET_OUT (FILTER_2_OFFSET - FILTER_2_OFFSET_IN)

/**********************
 *      MACROS
 **********************/

#define WITH_RADIO_LOCK(fn) radio_lock(); fn; radio_unlock();

#define CHANGE_PARAM(new_val, val, dirty, radio_fn) \
    if (new_val != val) { \
        params_lock(); \
        val = new_val; \
        params_unlock(&dirty); \
        radio_lock(); \
        radio_fn(val); \
        radio_unlock(); \
    }

/**********************
 *      TYPEDEFS
 **********************/

typedef enum {
    x6100_flow_fp32 = 0,
    x6100_flow_bf16,
} x6100_flow_fmt_t;

/**********************
 *  STATIC PROTOTYPES
 **********************/

static void on_change_bool(Subject *subj, void *user_data);
static void radio_lock();
static void radio_unlock();

static void on_vfo_freq_change(Subject *subj, void *user_data);
static void on_vfo_mode_change(Subject *subj, void *user_data);
static void on_vfo_agc_change(Subject *subj, void *user_data);
static void on_vfo_att_change(Subject *subj, void *user_data);
static void on_vfo_pre_change(Subject *subj, void *user_data);
static void on_low_filter_change(Subject *subj, void *user_data);
static void on_high_filter_change(Subject *subj, void *user_data);

static void on_if_shift_change(Subject *subj, void *user_data);
static void on_band_change(Subject *subj, void *user_data);
static void update_agc_time(Subject *subj, void *user_data);
static void on_atu_network_change(Subject *subj, void *user_data);
static void on_change_comp_ratio(Subject *subj, void *user_data);
static void base_control_command(Subject *subj, void *user_data);
static void on_fw_zoom_change(Subject *subj, void *user_data);

static void on_change_float(Subject *subj, void *user_data);
static void on_change_uint32(Subject *subj, void *user_data);
static void on_change_int32(Subject *subj, void *user_data);
static void on_change_uint16(Subject *subj, void *user_data);
static void on_change_uint8(Subject *subj, void *user_data);
static void on_change_int8(Subject *subj, void *user_data);

static void recover_processing_audio_inputs();
static bool radio_tick();
static void * radio_thread(void *arg);

/**********************
 *  STATIC VARIABLES
 **********************/

static radio_rx_tx_change_t notify_rx_tx;
static void(*low_power_cb)(bool) = NULL;

static pthread_mutex_t  control_mux;

static x6100_flow_t    *pack;
static x6100_base_ver_t base_ver;
static int8_t           audio_in_lvl;

static radio_state_t    state = RADIO_RX;
static uint64_t         now_time;
static uint64_t         prev_time;
static uint64_t         idle_time;
static bool             mute = false;

static cfloat           samples_buf[RADIO_SAMPLES*2];

static uint32_t min_tx;
static uint32_t max_tx;

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void radio_bb_reset() {
    x6100_gpio_set(x6100_pin_bb_reset, 1);
    usleep(100000);
    x6100_gpio_set(x6100_pin_bb_reset, 0);
}

void radio_init() {
    if (!x6100_gpio_init())
        return;

    x6100_gpio_set(x6100_pin_morse_key, 1);     /* Morse key off */

    if (!x6100_flow_init())
        return;

    while (!x6100_control_init()) {
        usleep(100000);
    }
}

void radio_start() {

    base_ver = x6100_control_get_base_ver();

    // Enable center mode by default (old behavior)
    if ((util_compare_version(base_ver, (x6100_base_ver_t){1, 1, 9, 0}) >= 0) || (base_ver.rev >= 8)) {
        x6100_control_if_shift_set(false);
    }

    pack = malloc(sizeof(x6100_flow_t));

    subject_subscribe_and_notify((Subject*)cfg_band_vfoa_freq, on_vfo_freq_change, (void*)X6100_VFO_A);
    subject_subscribe_and_notify((Subject*)cfg_band_vfob_freq, on_vfo_freq_change, (void*)X6100_VFO_B);

    subject_subscribe_and_notify((Subject*)cfg_band_vfoa_mode, on_vfo_mode_change, (void*)X6100_VFO_A);
    subject_subscribe_and_notify((Subject*)cfg_band_vfob_mode, on_vfo_mode_change, (void*)X6100_VFO_B);

    subject_subscribe_and_notify((Subject*)cfg_band_vfoa_agc, on_vfo_agc_change, (void*)X6100_VFO_A);
    subject_subscribe_and_notify((Subject*)cfg_band_vfob_agc, on_vfo_agc_change, (void*)X6100_VFO_B);

    subject_subscribe_and_notify((Subject*)cfg_band_vfoa_att, on_vfo_att_change, (void*)X6100_VFO_A);
    subject_subscribe_and_notify((Subject*)cfg_band_vfob_att, on_vfo_att_change, (void*)X6100_VFO_B);

    subject_subscribe_and_notify((Subject*)cfg_band_vfoa_pre, on_vfo_pre_change, (void*)X6100_VFO_A);
    subject_subscribe_and_notify((Subject*)cfg_band_vfob_pre, on_vfo_pre_change, (void*)X6100_VFO_B);

    subject_subscribe_and_notify((Subject*)cfg_band_current_vfo, on_change_uint32, x6100_control_vfo_set);
    subject_subscribe_and_notify((Subject*)cfg_band_split, on_change_uint8, x6100_control_split_set);
    subject_subscribe_and_notify((Subject*)cfg_rfgain, on_change_uint8, x6100_control_rfg_set);

    subject_subscribe_and_notify((Subject*)cfg_band_if_shift, on_if_shift_change, NULL);

    subject_subscribe_and_notify((Subject*)cfg_band_tx_i_offset, on_change_int32, x6100_control_tx_i_offset_set);
    subject_subscribe_and_notify((Subject*)cfg_band_tx_q_offset, on_change_int32, x6100_control_tx_q_offset_set);

    subject_subscribe((Subject*)cfg_band_id, on_band_change, NULL);

    subject_subscribe((Subject*)cfg_cur_agc, update_agc_time, NULL);
    subject_subscribe_and_notify((Subject*)cfg_cur_mode, update_agc_time, NULL);

    subject_subscribe((Subject*)cfg_cur_filter_low, on_low_filter_change, NULL);
    subject_subscribe_and_notify((Subject*)cfg_cur_mode, on_low_filter_change, NULL);
    subject_subscribe((Subject*)cfg_cur_filter_high, on_high_filter_change, NULL);
    subject_subscribe_and_notify((Subject*)cfg_cur_mode, on_high_filter_change, NULL);

    subject_subscribe_and_notify((Subject*)cfg_volume, on_change_uint8, x6100_control_rxvol_set);
    subject_subscribe_and_notify((Subject*)cfg_squelch, on_change_uint8, x6100_control_sql_set);
    subject_subscribe_and_notify((Subject*)cfg_pwr, on_change_float, x6100_control_txpwr_set);
    subject_subscribe_and_notify((Subject*)cfg_output_gain, on_change_float, x6100_control_adc_dac_gain_set);
    subject_subscribe_and_notify((Subject*)cfg_band_dac_offset, on_change_float, x6100_control_dac_gain_set);
    subject_subscribe_and_notify((Subject*)cfg_atu_enabled, on_change_uint8, x6100_control_atu_set);
    cfg_atu_network_subscribe(on_atu_network_change, NULL);
    on_atu_network_change(NULL, NULL);

    /* Compressor */
    subject_subscribe_and_notify((Subject*)cfg_comp, on_change_comp_ratio, NULL);
    subject_subscribe_and_notify((Subject*)cfg_comp_threshold_offset, on_change_float, x6100_control_comp_threshold_set);
    subject_subscribe_and_notify((Subject*)cfg_comp_makeup_offset, on_change_float, x6100_control_comp_makeup_set);

    /* VOX */
    subject_subscribe_and_notify((Subject*)cfg_vox_on, on_change_uint8, x6100_control_vox_set);
    subject_subscribe_and_notify((Subject*)cfg_vox_gain, on_change_uint8, x6100_control_vox_gain_set);
    subject_subscribe_and_notify((Subject*)cfg_vox_ag, on_change_uint8, x6100_control_vox_ag_set);
    subject_subscribe_and_notify((Subject*)cfg_vox_delay, on_change_uint16, x6100_control_vox_delay_set);

    subject_subscribe_and_notify((Subject*)cfg_mic, on_change_uint8, x6100_control_mic_set);
    subject_subscribe_and_notify((Subject*)cfg_hmic, on_change_uint8, x6100_control_hmic_set);
    subject_subscribe_and_notify((Subject*)cfg_imic, on_change_uint8, x6100_control_imic_set);
    subject_subscribe_and_notify((Subject*)cfg_moni, on_change_uint8, x6100_control_moni_set);

    subject_subscribe_and_notify((Subject*)cfg_rit, base_control_command, (void*)x6100_rit);
    subject_subscribe_and_notify((Subject*)cfg_xit, base_control_command, (void*)x6100_xit);
    subject_subscribe_and_notify((Subject*)cfg_fm_emphasis, on_change_bool, x6100_control_fm_emp);

    subject_subscribe_and_notify((Subject*)cfg_tx_filter_low, on_change_uint16, x6100_control_tx_filter_low_set);
    subject_subscribe_and_notify((Subject*)cfg_tx_filter_high, on_change_uint16, x6100_control_tx_filter_high_set);

    subject_subscribe_and_notify((Subject*)cfg_cessb_on, on_change_bool, x6100_control_cessb_set);
    subject_subscribe_and_notify((Subject*)cfg_cessb_power_up, on_change_float, x6100_control_cessb_power_up_set);

    subject_subscribe_and_notify((Subject*)cfg_key_tone, on_change_uint16, x6100_control_key_tone_set);
    subject_subscribe_and_notify((Subject*)cfg_key_speed, on_change_uint8, x6100_control_key_speed_set);
    subject_subscribe_and_notify((Subject*)cfg_key_mode, on_change_uint8, x6100_control_key_mode_set);
    subject_subscribe_and_notify((Subject*)cfg_iambic_mode, on_change_uint8, x6100_control_iambic_mode_set);
    subject_subscribe_and_notify((Subject*)cfg_key_vol, on_change_uint16, x6100_control_key_vol_set);
    subject_subscribe_and_notify((Subject*)cfg_key_train, on_change_uint8, x6100_control_key_train_set);
    subject_subscribe_and_notify((Subject*)cfg_qsk_time, on_change_uint16, x6100_control_qsk_time_set);
    subject_subscribe_and_notify((Subject*)cfg_key_ratio, on_change_float, x6100_control_key_ratio_set);
    subject_subscribe_and_notify((Subject*)cfg_cw_peak_on, on_change_bool, x6100_control_cw_peak_set);
    subject_subscribe_and_notify((Subject*)cfg_cw_peak_q, on_change_uint8, x6100_control_cw_peak_q_set);

    subject_subscribe_and_notify((Subject*)cfg_agc_hang, on_change_uint8, x6100_control_agc_hang_set);
    subject_subscribe_and_notify((Subject*)cfg_agc_knee, on_change_int8, x6100_control_agc_knee_set);
    subject_subscribe_and_notify((Subject*)cfg_agc_slope, on_change_uint8, x6100_control_agc_slope_set);

    subject_subscribe_and_notify((Subject*)cfg_dnf, on_change_uint8, x6100_control_dnf_set);
    subject_subscribe_and_notify((Subject*)cfg_dnf_center, on_change_uint16, x6100_control_dnf_center_set);
    subject_subscribe_and_notify((Subject*)cfg_dnf_width, on_change_uint16, x6100_control_dnf_width_set);
    subject_subscribe_and_notify((Subject*)cfg_dnf_auto, on_change_uint16, x6100_control_dnf_update_set);
    subject_subscribe_and_notify((Subject*)cfg_nb, on_change_uint8, x6100_control_nb_set);
    subject_subscribe_and_notify((Subject*)cfg_nb_level, on_change_uint8, x6100_control_nb_level_set);
    subject_subscribe_and_notify((Subject*)cfg_nb_width, on_change_uint8, x6100_control_nb_width_set);
    subject_subscribe_and_notify((Subject*)cfg_nr, on_change_uint8, x6100_control_nr_set);
    subject_subscribe_and_notify((Subject*)cfg_nr_level, on_change_uint8, x6100_control_nr_level_set);

    if ((util_compare_version(base_ver, (x6100_base_ver_t){1, 1, 9, 0}) >= 0) || (base_ver.rev >= 8)) {
        subject_subscribe_and_notify((Subject*)cfg_mode_zoom, on_fw_zoom_change, NULL);
    }

    x6100_control_charger_set(params.charger.x == RADIO_CHARGER_ON);
    x6100_control_bias_drive_set(params.bias_drive);
    x6100_control_bias_final_set(params.bias_final);

    x6100_control_spmode_set(params.spmode.x);

    x6100_control_linein_set(params.line_in);
    x6100_control_lineout_set(params.line_out);

    if (base_ver.rev >= 8) {
        x6100_control_bf16_flow_set(true);
    }

    prev_time = get_time();
    idle_time = prev_time;

    pthread_mutex_init(&control_mux, NULL);

    pthread_t thread;

    pthread_create(&thread, NULL, radio_thread, NULL);
    pthread_detach(thread);
}

void radio_set_rx_tx_notify_fn(radio_rx_tx_change_t cb) {
    notify_rx_tx = cb;
}

void radio_set_low_power_cb(void (*cb)(bool)) {
    low_power_cb = cb;
}

radio_state_t radio_get_state() {
    return state;
}

void radio_set_freq(int32_t freq) {
    if (!radio_check_freq(freq)) {
        LV_LOG_ERROR("Freq %i incorrect", freq);
        return;
    }
    x6100_vfo_t vfo = param_i_get(cfg_band_current_vfo);
    int32_t shift = cfg_transverter_shift_for(freq);
    WITH_RADIO_LOCK(x6100_control_vfo_freq_set(vfo, freq - shift));
}

bool radio_check_freq(int32_t freq) {
    return cfg_is_valid_hw_freq(freq);
}

uint16_t radio_change_vol(int16_t df) {
    int32_t vol = param_i_get(cfg_volume);
    if (df == 0) {
        return vol;
    }

    mute = false;

    uint16_t new_val = limit(vol + df, 0, 55);

    if (new_val != vol) {
        param_i_set(cfg_volume, new_val);
    };

    return new_val;
}

void radio_change_mute() {
    mute = !mute;
    x6100_control_rxvol_set(mute ? 0 : param_i_get(cfg_volume));
}

bool radio_change_spmode(int16_t df) {
    if (df == 0) {
        return params.spmode.x;
    }

    params_bool_set(&params.spmode, df > 0);

    WITH_RADIO_LOCK(x6100_control_spmode_set(params.spmode.x));

    return params.spmode.x;
}

void radio_start_atu() {
    if (state == RADIO_RX) {
        state = RADIO_ATU_START;
    }
}

bool radio_start_swrscan() {
    if (state != RADIO_RX) {
        return false;
    }

    cparam_i_set(cfg_cur_mode, x6100_mode_am);
    radio_lock();
    x6100_control_txpwr_set(5.0f);
    x6100_control_swrscan_set(true);
    radio_unlock();
    state = RADIO_SWRSCAN;

    return true;
}

void radio_stop_swrscan() {
    if (state == RADIO_SWRSCAN) {
        state = RADIO_RX;
        radio_lock();
        x6100_control_swrscan_set(false);
        x6100_control_txpwr_set(param_f_get(cfg_pwr));
        radio_unlock();
    }
}

void radio_set_pwr(float d) {
    WITH_RADIO_LOCK(x6100_control_txpwr_set(d));
}

x6100_vfo_t radio_toggle_vfo() {
    x6100_vfo_t new_vfo = (param_i_get(cfg_band_current_vfo) == X6100_VFO_A) ? X6100_VFO_B : X6100_VFO_A;

    param_i_set(cfg_band_current_vfo, new_vfo);
    // TODO: move to another file
    voice_say_text_fmt("V F O %s", (new_vfo == X6100_VFO_A) ? "A" : "B");

    return new_vfo;
}

void radio_poweroff() {
    if (params.charger.x == RADIO_CHARGER_SHADOW) {
        WITH_RADIO_LOCK(x6100_control_charger_set(true));
    }
    cfg_api_flush_all();
    cfg_db_shutdown();
    state = RADIO_POWEROFF;
}

void radio_set_charger(bool on) {
    WITH_RADIO_LOCK(x6100_control_charger_set(on));
}

void radio_set_ptt(bool tx) {
    WITH_RADIO_LOCK(x6100_control_ptt_set(tx));
}

void radio_set_modem(bool tx) {
    WITH_RADIO_LOCK(x6100_control_modem_set(tx));
}

void radio_set_line_in(uint8_t d) {
    CHANGE_PARAM(d, params.line_in, params.dirty.line_in, x6100_control_linein_set);
}

void radio_set_line_out(uint8_t d) {
    CHANGE_PARAM(d, params.line_out, params.dirty.line_out, x6100_control_lineout_set);
}

void radio_set_morse_key(bool on) {
    x6100_gpio_set(x6100_pin_morse_key, on ? 0 : 1);
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

 static void radio_lock() {
    pthread_mutex_lock(&control_mux);
}

static void radio_unlock() {
    idle_time = get_time();
    pthread_mutex_unlock(&control_mux);
}

static void on_change_bool(Subject *subj, void *user_data) {
    int32_t new_val = subject_i_get((SubjectInt*)subj);
    void (*fn)(bool) = (void (*)(bool))user_data;
    WITH_RADIO_LOCK(fn(new_val));
}

static void on_change_int8(Subject *subj, void *user_data) {
    int32_t new_val = subject_i_get((SubjectInt*)subj);
    void (*fn)(int8_t) = (void (*)(int8_t))user_data;
    WITH_RADIO_LOCK(fn(new_val));
}

static void on_change_uint8(Subject *subj, void *user_data) {
    int32_t new_val = subject_i_get((SubjectInt*)subj);
    void (*fn)(uint8_t) = (void (*)(uint8_t))user_data;
    WITH_RADIO_LOCK(fn(new_val));
}


static void on_change_uint16(Subject *subj, void *user_data) {
    int32_t new_val = subject_i_get((SubjectInt*)subj);
    void (*fn)(uint16_t) = (void (*)(uint16_t))user_data;
    WITH_RADIO_LOCK(fn(new_val));
}

static void on_change_uint32(Subject *subj, void *user_data) {
    int32_t new_val = subject_i_get((SubjectInt*)subj);
    void (*fn)(uint32_t) = (void (*)(uint32_t))user_data;
    WITH_RADIO_LOCK(fn(new_val));
}

static void on_change_int32(Subject *subj, void *user_data) {
    int32_t new_val = subject_i_get((SubjectInt*)subj);
    void (*fn)(int32_t) = (void (*)(int32_t))user_data;
    WITH_RADIO_LOCK(fn(new_val));
}

static void on_change_float(Subject *subj, void *user_data) {
    float new_val = subject_f_get((SubjectFloat*)subj);
    void (*fn)(float) = (void (*)(float))user_data;
    WITH_RADIO_LOCK(fn(new_val));
}

static void on_vfo_freq_change(Subject *subj, void *user_data) {
    x6100_vfo_t vfo = (x6100_vfo_t )user_data;
    int32_t new_val;
    if (vfo == X6100_VFO_A) {
        new_val = param_i_get(cfg_band_vfoa_freq);
    } else {
        new_val = param_i_get(cfg_band_vfob_freq);
    }
    int32_t shift = cfg_transverter_shift_for(new_val);
    shift += param_i_get(cfg_band_if_shift);
    WITH_RADIO_LOCK(x6100_control_vfo_freq_set(vfo, new_val - shift));
    LV_LOG_USER("Radio set vfo %i freq=%i (%i)", vfo, new_val, new_val - shift);
}

static void on_vfo_mode_change(Subject *subj, void *user_data) {
    x6100_vfo_t vfo = (x6100_vfo_t )user_data;
    int32_t new_val = subject_i_get((SubjectInt*)subj);
    WITH_RADIO_LOCK(x6100_control_vfo_mode_set(vfo, new_val));
    LV_LOG_USER("Radio set vfo %i mode=%i", vfo, new_val);;
}

static void on_vfo_agc_change(Subject *subj, void *user_data) {
    x6100_vfo_t vfo = (x6100_vfo_t )user_data;
    int32_t new_val = subject_i_get((SubjectInt*)subj);
    WITH_RADIO_LOCK(x6100_control_vfo_agc_set(vfo, new_val));
    LV_LOG_USER("Radio set vfo %i agc=%i", vfo, new_val);
}

static void update_agc_time(Subject *subj, void *user_data) {
    x6100_agc_t     agc = cparam_i_get(cfg_cur_agc);
    x6100_mode_t    mode = cparam_i_get(cfg_cur_mode);
    uint16_t        agc_time = 500;

    switch (agc) {
        case x6100_agc_off:
            agc_time = 1000;
            break;

        case x6100_agc_slow:
            agc_time = 1000;
            break;

        case x6100_agc_fast:
            agc_time = 100;
            break;

        case x6100_agc_auto:
            switch (mode) {
                case x6100_mode_lsb:
                case x6100_mode_lsb_dig:
                case x6100_mode_usb:
                case x6100_mode_usb_dig:
                    agc_time = 500;
                    break;

                case x6100_mode_cw:
                case x6100_mode_cwr:
                    agc_time = 100;
                    break;

                case x6100_mode_am:
                case x6100_mode_nfm:
                    agc_time = 1000;
                    break;
            }
            break;
    }
    WITH_RADIO_LOCK(x6100_control_agc_time_set(agc_time));
    LV_LOG_USER("Radio set agc time=%u for agc: %i\n", agc_time, agc);
}

static void on_vfo_att_change(Subject *subj, void *user_data) {
    x6100_vfo_t vfo = (x6100_vfo_t )user_data;
    int32_t new_val = subject_i_get((SubjectInt*)subj);
    WITH_RADIO_LOCK(x6100_control_vfo_att_set(vfo, new_val));
    LV_LOG_USER("Radio set vfo %i att=%i", vfo, new_val);
}

static void on_vfo_pre_change(Subject *subj, void *user_data) {
    x6100_vfo_t vfo = (x6100_vfo_t )user_data;
    int32_t new_val = subject_i_get((SubjectInt*)subj);
    WITH_RADIO_LOCK(x6100_control_vfo_pre_set(vfo, new_val));
    LV_LOG_USER("Radio set vfo %i pre=%i", vfo, new_val);
}

static void on_atu_network_change(Subject *subj, void *user_data) {
    (void)subj;
    (void)user_data;
    uint32_t new_val = cfg_atu_get_network();
    WITH_RADIO_LOCK(x6100_control_cmd(x6100_atu_network, new_val));
    LV_LOG_USER("Radio set atu network=%u", new_val);
}

static void on_low_filter_change(Subject *subj, void *user_data) {
    x6100_mode_t mode = cparam_i_get(cfg_cur_mode);
    if ((mode == x6100_mode_am) || (mode == x6100_mode_nfm)) {
        // No update low on AM/FM, low should be -high
        return;
    }
    int32_t low = cparam_i_get(cfg_cur_filter_low);
    int32_t low2 = LV_MAX(0, low - FILTER_2_OFFSET_OUT);
    radio_lock();
    LV_LOG_USER("Radio set filters: low=%i, low2=%i", low, low2);
    x6100_control_cmd(x6100_filter1_low, low);
    x6100_control_cmd(x6100_filter2_low, low2);
    radio_unlock();
}

static void on_high_filter_change(Subject *subj, void *user_data) {
    int32_t high = cparam_i_get(cfg_cur_filter_high);
    int32_t high2 = high + FILTER_2_OFFSET_OUT;
    switch (cparam_i_get(cfg_cur_mode)) {
        case x6100_mode_am:
        case x6100_mode_nfm:
            // For AM BASE uses absolute low and high values to choose LPF filters for I and Q
            radio_lock();
            LV_LOG_USER("Radio set filters: low=%i, low2=%i", -high, -high2);
            x6100_control_cmd(x6100_filter1_low, -high);
            x6100_control_cmd(x6100_filter2_low, -high2);
            radio_unlock();
            break;
        default:
            break;
    }
    radio_lock();
    LV_LOG_USER("Radio set filters: high=%i, high2=%i", high, high2);
    x6100_control_cmd(x6100_filter1_high, high);
    x6100_control_cmd(x6100_filter2_high, high2);
    radio_unlock();
}

static void on_change_comp_ratio(Subject *subj, void *user_data) {
    uint8_t ratio = subject_i_get((SubjectInt*)subj);
    if (ratio < 1) {
        ratio = 1;
    }
    if (ratio == 1) {
        // invert
        WITH_RADIO_LOCK(x6100_control_comp_set(true));
    } else {
        radio_lock();
        x6100_control_comp_set(false);
        x6100_control_comp_level_set((x6100_comp_level_t)(ratio - 2));
        radio_unlock();
    }
}

static void on_fw_zoom_change(Subject *subj, void *user_data) {
    uint8_t zoom = subject_i_get((SubjectInt*)subj);
    uint8_t val = 0;
    while (zoom > 1) {
        val++;
        zoom >>= 1;
    }
    WITH_RADIO_LOCK(x6100_control_fftdec_set(val));
}

static void on_if_shift_change(Subject *subj, void *user_data) {
    int32_t shift = subject_i_get((SubjectInt*)subj);
    int32_t cur_freq = cparam_i_get(cfg_fg_freq);

    LV_LOG_USER("Shift: %d, cur_freq: %d\n", shift, cur_freq);
    if (shift != 0) {
        radio_lock();
        x6100_control_if_shift_freq_set(shift);
        x6100_control_if_shift_set(true);
        radio_unlock();
    } else {
        WITH_RADIO_LOCK(x6100_control_if_shift_set(false));
    }
    x6100_vfo_t vfo = param_i_get(cfg_band_current_vfo);
    on_vfo_freq_change(NULL, (void*)vfo);
}

static void on_band_change(Subject *subj, void *user_data) {
    // Workaround for bug with ignored IQ offset on band change from BASE
    int32_t i_val = param_i_get(cfg_band_tx_i_offset);
    int32_t q_val = param_i_get(cfg_band_tx_q_offset);
    if (i_val == (int32_t)x6100_control_get(x6100_txiofs)) {
        i_val++;
    }
    radio_lock();
    x6100_control_tx_i_offset_set(i_val);
    radio_unlock();
    if (q_val == (int32_t)x6100_control_get(x6100_txqofs)) {
        q_val ++;
    }
    radio_lock();
    x6100_control_tx_q_offset_set(q_val);
    radio_unlock();
}

static void base_control_command(Subject *subj, void *user_data) {
    uint32_t val = subject_i_get((SubjectInt*)subj);
    x6100_cmd_enum_t cmd = (x6100_cmd_enum_t)user_data;
    WITH_RADIO_LOCK(x6100_control_cmd(cmd, val));
}

/**
 * Restore "listening" of main board and USB soundcard after ATU
 */
static void recover_processing_audio_inputs() {
    usleep(10000);
    x6100_vfo_t vfo = param_i_get(cfg_band_current_vfo);
    radio_lock();
    x6100_control_vfo_mode_set(vfo, x6100_mode_usb_dig);
    x6100_control_txpwr_set(0.1f);
    x6100_control_modem_set(true);
    usleep(50000);
    x6100_control_modem_set(false);
    x6100_control_txpwr_set(param_f_get(cfg_pwr));
    x6100_control_vfo_mode_set(vfo, cparam_i_get(cfg_cur_mode));
    radio_unlock();
}

static bool radio_tick() {
    if (now_time < prev_time) {
        prev_time = now_time;
    }

    int32_t d = now_time - prev_time;

    if (x6100_flow_read(pack)) {
        prev_time = now_time;

        static uint8_t delay;

        if (delay++ > 10) {
            delay = 0;
            clock_update_power(pack->vext * 0.1f, pack->vbat*0.1f, pack->batcap, pack->flag.charging);
            if (low_power_cb) {
                low_power_cb(!pack->flag.vext && (pack->vbat <= 60));
            }
        }
        flow_info_t flow_info = pack->flow_info;

        uint32_t base_freq = 0;
        uint8_t fft_dec = 0;
        if (base_ver.rev >= 8) {
            base_freq = flow_info.lo_freq;
            fft_dec = 1U << flow_info.fft_dec;
        }

        cfloat *flow_samples = (cfloat*)((char *)pack + offsetof(x6100_flow_t, samples));
        cfloat *samples;
        size_t n_samples;

        if (base_ver.rev >= 8) {
            if (flow_info.flow_fmt == x6100_flow_fp32) {
                samples = flow_samples;
                n_samples = RADIO_SAMPLES;
            } else if (flow_info.flow_fmt == x6100_flow_bf16) {
                n_samples = RADIO_SAMPLES * 2;
                uint16_t *u16_flow_samples = (uint16_t*)flow_samples;
                uint32_t *u32_samples_buf = (uint32_t*)samples_buf;
                for (size_t i = 0; i < n_samples * 2; i+=4) {
                    u32_samples_buf[i] = (uint32_t)u16_flow_samples[i] << 16;
                    u32_samples_buf[i+1] = (uint32_t)u16_flow_samples[i+1] << 16;
                    u32_samples_buf[i+2] = (uint32_t)u16_flow_samples[i+2] << 16;
                    u32_samples_buf[i+3] = (uint32_t)u16_flow_samples[i+3] << 16;
                }
                samples = samples_buf;
            }
        } else {
            samples = flow_samples;
            n_samples = RADIO_SAMPLES;
        }
        // printf("%d from %d, freq: %d, vary_freq: %d\n", flow_info->flow_seq_n, seq_total, base_freq, vary_freq);
        // union {
        //     uint32_t i;
        //     float f;
        // } fuint = {flow_info->_pad2};
        // printf("max signal: %g\n", fuint.f);
        // printf("audio mul: %.*g\n", fuint.f);
        dsp_samples(samples, n_samples, pack->flag.tx, base_freq, flow_info.vary_freq, fft_dec);

        switch (state) {
            case RADIO_RX:
                if (pack->flag.tx) {
                    state = RADIO_TX;
                    if (notify_rx_tx) {
                        notify_rx_tx(true);
                    }
                }
                break;

            case RADIO_TX:
                if (!pack->flag.tx) {
                    state = RADIO_RX;
                    if (notify_rx_tx) {
                        notify_rx_tx(false);
                    }
                } else {
                    // printf("%d, %d\n", pack->tx_power, pack->alc_level);
                    tx_info_update(pack->tx_power * 0.1f, pack->vswr * 0.1f, pack->alc_level * 0.1f);
                }
                break;

            case RADIO_ATU_START:
                WITH_RADIO_LOCK(x6100_control_atu_tune(true));
                state = RADIO_ATU_WAIT;
                break;

            case RADIO_ATU_WAIT:
                if (pack->flag.tx) {
                    if (notify_rx_tx) {
                        notify_rx_tx(true);
                    }
                    state = RADIO_ATU_RUN;
                }
                break;

            case RADIO_ATU_RUN:
                if (pack->flag.atu_status && !pack->flag.tx) {
                    cfg_atu_save_network(pack->atu_params);
                    WITH_RADIO_LOCK(x6100_control_atu_tune(false));
                    param_i_set(cfg_atu_enabled, true);
                    recover_processing_audio_inputs();
                    if (notify_rx_tx) {
                        notify_rx_tx(false);
                    }

                    // TODO: change with observer on atu->loaded change
                    WITH_RADIO_LOCK(x6100_control_cmd(x6100_atu_network, pack->atu_params));
                    state = RADIO_RX;
                } else if (pack->flag.tx) {
                    tx_info_update(pack->tx_power * 0.1f, pack->vswr * 0.1f, pack->alc_level * 0.1f);
                }
                break;

            case RADIO_SWRSCAN:
                dialog_swrscan_update(pack->vswr * 0.1f);
                break;

            case RADIO_POWEROFF:
                x6100_control_poweroff();
                state = RADIO_OFF;
                break;

            case RADIO_OFF:
                break;
        }

        hkey_put(pack->hkey);
    } else {
        if (d > FLOW_RESTART_TIMEOUT) {
            LV_LOG_WARN("Flow reset");
            prev_time = now_time;
            x6100_flow_restart();
            dsp_reset();
        }
        return true;
    }
    return false;
}

static void * radio_thread(void *arg) {
    while (true) {
        now_time = get_time();

        if (radio_tick()) {
            usleep(2000);
        }

        int32_t idle = now_time - idle_time;

        if (idle > IDLE_TIMEOUT && state == RADIO_RX) {
            WITH_RADIO_LOCK(x6100_control_idle());

            idle_time = now_time;
        }
    }
}

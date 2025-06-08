/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */
#include "radio.h"

#include "cfg/atu.h"
#include "cfg/transverter.h"
#include "util.h"
#include "dsp.h"
#include "params/params.h"
#include "hkey.h"
#include "tx_info.h"
#include "info.h"
#include "dialog_swrscan.h"
#include "cw.h"
#include "pubsub_ids.h"

#include <aether_radio/x6100_control/low/flow.h>
#include <aether_radio/x6100_control/low/gpio.h>

#include <unistd.h>
#include <stdio.h>
#include <pthread.h>
#include <string.h>



#define FLOW_RESTART_TIMEOUT 300
#define IDLE_TIMEOUT        (3 * 1000)

static radio_rx_tx_change_t notify_rx_tx;

static pthread_mutex_t  control_mux;

static x6100_flow_t     *pack;

static radio_state_t    state = RADIO_RX;
static uint64_t         now_time;
static uint64_t         prev_time;
static uint64_t         idle_time;
static bool             mute = false;

#define WITH_RADIO_LOCK(fn) radio_lock(); fn; radio_unlock();

#define CHANGE_PARAM(new_val, val, dirty, radio_fn) \
    if (new_val != val) { \
        params_lock(); \
        val = new_val; \
        params_unlock(&dirty); \
        radio_lock(); \
        radio_fn(val); \
        radio_unlock(); \
        lv_msg_send(MSG_PARAM_CHANGED, NULL); \
    }

static void radio_lock() {
    pthread_mutex_lock(&control_mux);
}

static void radio_unlock() {
    idle_time = get_time();
    pthread_mutex_unlock(&control_mux);
}

/**
 * Restore "listening" of main board and USB soundcard after ATU
 */
static void recover_processing_audio_inputs() {
    usleep(10000);
    x6100_vfo_t vfo = subject_get_int(cfg_cur.band->vfo.val);
    radio_lock();
    x6100_control_vfo_mode_set(vfo, x6100_mode_usb_dig);
    x6100_control_txpwr_set(0.1f);
    x6100_control_modem_set(true);
    usleep(50000);
    x6100_control_modem_set(false);
    x6100_control_txpwr_set(subject_get_float(cfg.pwr.val));
    x6100_control_vfo_mode_set(vfo, subject_get_int(cfg_cur.mode));
    radio_unlock();
}

bool radio_tick() {
    if (now_time < prev_time) {
        prev_time = now_time;
    }

    int32_t d = now_time - prev_time;

    if (x6100_flow_read(pack)) {
        prev_time = now_time;

        static uint8_t delay = 0;

        if (delay++ > 10) {
            delay = 0;
            clock_update_power(pack->vext * 0.1f, pack->vbat*0.1f, pack->batcap, pack->flag.charging);
        }
        cfloat *samples = (cfloat*)((char *)pack + offsetof(x6100_flow_t, samples));
        dsp_samples(samples, RADIO_SAMPLES, pack->flag.tx);

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
                    subject_set_int(cfg.atu_enabled.val, true);
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
            usleep(15000);
        }

        int32_t idle = now_time - idle_time;

        if (idle > IDLE_TIMEOUT && state == RADIO_RX) {
            WITH_RADIO_LOCK(x6100_control_idle());

            idle_time = now_time;
        }
    }
}

static void on_change_int8(Subject *subj, void *user_data) {
    int32_t new_val = subject_get_int(subj);
    void (*fn)(int8_t) = (void (*)(int8_t))user_data;
    WITH_RADIO_LOCK(fn(new_val));
}

static void on_change_uint8(Subject *subj, void *user_data) {
    int32_t new_val = subject_get_int(subj);
    void (*fn)(uint8_t) = (void (*)(uint8_t))user_data;
    WITH_RADIO_LOCK(fn(new_val));
}

static void on_change_uint16(Subject *subj, void *user_data) {
    int32_t new_val = subject_get_int(subj);
    void (*fn)(uint16_t) = (void (*)(uint16_t))user_data;
    WITH_RADIO_LOCK(fn(new_val));
}

static void on_change_uint32(Subject *subj, void *user_data) {
    int32_t new_val = subject_get_int(subj);
    void (*fn)(uint32_t) = (void (*)(uint32_t))user_data;
    WITH_RADIO_LOCK(fn(new_val));
}

static void on_change_int32(Subject *subj, void *user_data) {
    int32_t new_val = subject_get_int(subj);
    void (*fn)(int32_t) = (void (*)(int32_t))user_data;
    WITH_RADIO_LOCK(fn(new_val));
}

static void on_change_float(Subject *subj, void *user_data) {
    float new_val = subject_get_float(subj);
    void (*fn)(float) = (void (*)(float))user_data;
    WITH_RADIO_LOCK(fn(new_val));
}

static void on_vfo_freq_change(Subject *subj, void *user_data) {
    x6100_vfo_t vfo = (x6100_vfo_t )user_data;
    int32_t new_val = subject_get_int(subj);
    int32_t shift = cfg_transverter_get_shift(new_val);
    WITH_RADIO_LOCK(x6100_control_vfo_freq_set(vfo, new_val - shift));
    LV_LOG_USER("Radio set vfo %i freq=%i (%i)", vfo, new_val, new_val - shift);
}

static void on_vfo_mode_change(Subject *subj, void *user_data) {
    x6100_vfo_t vfo = (x6100_vfo_t )user_data;
    int32_t new_val = subject_get_int(subj);
    WITH_RADIO_LOCK(x6100_control_vfo_mode_set(vfo, new_val));
    LV_LOG_USER("Radio set vfo %i mode=%i", vfo, new_val);;
}

static void on_vfo_agc_change(Subject *subj, void *user_data) {
    x6100_vfo_t vfo = (x6100_vfo_t )user_data;
    int32_t new_val = subject_get_int(subj);
    WITH_RADIO_LOCK(x6100_control_vfo_agc_set(vfo, new_val));
    LV_LOG_USER("Radio set vfo %i agc=%i", vfo, new_val);
}

static void update_agc_time(Subject *subj, void *user_data) {
    x6100_agc_t     agc = subject_get_int(cfg_cur.agc);
    x6100_mode_t    mode = subject_get_int(cfg_cur.mode);
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
    int32_t new_val = subject_get_int(subj);
    WITH_RADIO_LOCK(x6100_control_vfo_att_set(vfo, new_val));
    LV_LOG_USER("Radio set vfo %i att=%i", vfo, new_val);
}

static void on_vfo_pre_change(Subject *subj, void *user_data) {
    x6100_vfo_t vfo = (x6100_vfo_t )user_data;
    int32_t new_val = subject_get_int(subj);
    WITH_RADIO_LOCK(x6100_control_vfo_pre_set(vfo, new_val));
    LV_LOG_USER("Radio set vfo %i pre=%i", vfo, new_val);
}

static void on_atu_network_change(Subject *subj, void *user_data) {
    uint32_t new_val = subject_get_int(subj);
    WITH_RADIO_LOCK(x6100_control_cmd(x6100_atu_network, new_val));
    LV_LOG_USER("Radio set atu network=%u", new_val);
}

static void on_low_filter_change(Subject *subj, void *user_data) {
    int32_t low = subject_get_int(subj);
    switch (subject_get_int(cfg_cur.mode)) {
        case x6100_mode_am:
        case x6100_mode_nfm:
            break;

        default:
            radio_lock();
            LV_LOG_USER("Radio set filter_low=%i", low);
            x6100_control_cmd(x6100_filter1_low, low);
            x6100_control_cmd(x6100_filter2_low, low);
            radio_unlock();
            break;
    }
}

static void on_high_filter_change(Subject *subj, void *user_data) {
    int32_t high = subject_get_int(subj);
    radio_lock();
    switch (subject_get_int(cfg_cur.mode)) {
        case x6100_mode_am:
        case x6100_mode_nfm:
            LV_LOG_USER("Radio set filter_low=%i", -high);
            LV_LOG_USER("Radio set filter_high=%i", high);
            x6100_control_cmd(x6100_filter1_low, -high);
            x6100_control_cmd(x6100_filter2_low, -high);
            x6100_control_cmd(x6100_filter1_high, high);
            x6100_control_cmd(x6100_filter2_high, high);
            break;

        default:
            LV_LOG_USER("Radio set filter_high=%i", high);
            x6100_control_cmd(x6100_filter1_high, high);
            x6100_control_cmd(x6100_filter2_high, high);
            break;
    }
    radio_unlock();
}

void on_change_comp_ratio(Subject *subj, void *user_data) {
    uint8_t ratio = subject_get_int(subj);
    if (ratio < 1) {
        ratio = 1;
    }
    if (ratio == 1) {
        // invert
        x6100_control_comp_set(true);
    } else {
        x6100_control_comp_set(false);
        x6100_control_comp_level_set((x6100_comp_level_t)(ratio - 2));
    }

}

void radio_bb_reset() {
    x6100_gpio_set(x6100_pin_bb_reset, 1);
    usleep(100000);
    x6100_gpio_set(x6100_pin_bb_reset, 0);
}

void radio_init() {
    if (!x6100_gpio_init())
        return;

    while (!x6100_control_init()) {
        usleep(100000);
    }

    if (!x6100_flow_init())
        return;

    x6100_gpio_set(x6100_pin_morse_key, 1);     /* Morse key off */

    pack = malloc(sizeof(x6100_flow_t));

    subject_add_observer_and_call(cfg_cur.band->vfo_a.freq.val, on_vfo_freq_change, (void*)X6100_VFO_A);
    subject_add_observer_and_call(cfg_cur.band->vfo_b.freq.val, on_vfo_freq_change, (void*)X6100_VFO_B);

    subject_add_observer_and_call(cfg_cur.band->vfo_a.mode.val, on_vfo_mode_change, (void*)X6100_VFO_A);
    subject_add_observer_and_call(cfg_cur.band->vfo_b.mode.val, on_vfo_mode_change, (void*)X6100_VFO_B);

    subject_add_observer_and_call(cfg_cur.band->vfo_a.agc.val, on_vfo_agc_change, (void*)X6100_VFO_A);
    subject_add_observer_and_call(cfg_cur.band->vfo_b.agc.val, on_vfo_agc_change, (void*)X6100_VFO_B);

    subject_add_observer_and_call(cfg_cur.band->vfo_a.att.val, on_vfo_att_change, (void*)X6100_VFO_A);
    subject_add_observer_and_call(cfg_cur.band->vfo_b.att.val, on_vfo_att_change, (void*)X6100_VFO_B);

    subject_add_observer_and_call(cfg_cur.band->vfo_a.pre.val, on_vfo_pre_change, (void*)X6100_VFO_A);
    subject_add_observer_and_call(cfg_cur.band->vfo_b.pre.val, on_vfo_pre_change, (void*)X6100_VFO_B);

    subject_add_observer_and_call(cfg_cur.band->vfo.val, on_change_uint32, x6100_control_vfo_set);
    subject_add_observer_and_call(cfg_cur.band->split.val, on_change_uint8, x6100_control_split_set);
    subject_add_observer_and_call(cfg_cur.band->rfg.val, on_change_uint8, x6100_control_rfg_set);

    subject_add_observer(cfg_cur.agc, update_agc_time, NULL);
    subject_add_observer_and_call(cfg_cur.mode, update_agc_time, NULL);

    subject_add_observer_and_call(cfg_cur.filter.low, on_low_filter_change, NULL);
    subject_add_observer_and_call(cfg_cur.filter.high, on_high_filter_change, NULL);

    subject_add_observer_and_call(cfg.vol.val, on_change_uint8, x6100_control_rxvol_set);
    subject_add_observer_and_call(cfg.sql.val, on_change_uint8, x6100_control_sql_set);
    subject_add_observer_and_call(cfg.pwr.val, on_change_float, x6100_control_txpwr_set);
    subject_add_observer_and_call(cfg_cur.band->output_gain.val, on_change_float, x6100_control_output_gain_set);
    subject_add_observer_and_call(cfg.atu_enabled.val, on_change_uint8, x6100_control_atu_set);
    subject_add_observer_and_call(cfg_cur.atu->network, on_atu_network_change, NULL);
    subject_add_observer_and_call(cfg.comp.val, on_change_comp_ratio, NULL);
    subject_add_observer_and_call(cfg.comp_threshold_offset.val, on_change_float, x6100_control_comp_threshold_set);
    subject_add_observer_and_call(cfg.comp_makeup_offset.val, on_change_float, x6100_control_comp_makeup_set);

    subject_add_observer_and_call(cfg.tx_i_offset.val, on_change_int32, x6100_control_tx_i_offset_set);
    subject_add_observer_and_call(cfg.tx_q_offset.val, on_change_int32, x6100_control_tx_q_offset_set);

    subject_add_observer_and_call(cfg.key_tone.val, on_change_uint16, x6100_control_key_tone_set);
    subject_add_observer_and_call(cfg.key_speed.val, on_change_uint8, x6100_control_key_speed_set);
    subject_add_observer_and_call(cfg.key_mode.val, on_change_uint8, x6100_control_key_mode_set);
    subject_add_observer_and_call(cfg.iambic_mode.val, on_change_uint8, x6100_control_iambic_mode_set);
    subject_add_observer_and_call(cfg.key_vol.val, on_change_uint16, x6100_control_key_vol_set);
    subject_add_observer_and_call(cfg.key_train.val, on_change_uint8, x6100_control_key_train_set);
    subject_add_observer_and_call(cfg.qsk_time.val, on_change_uint16, x6100_control_qsk_time_set);
    subject_add_observer_and_call(cfg.key_ratio.val, on_change_float, x6100_control_key_ratio_set);

    subject_add_observer_and_call(cfg.agc_hang.val, on_change_uint8, x6100_control_agc_hang_set);
    subject_add_observer_and_call(cfg.agc_knee.val, on_change_int8, x6100_control_agc_knee_set);
    subject_add_observer_and_call(cfg.agc_slope.val, on_change_uint8, x6100_control_agc_slope_set);

    subject_add_observer_and_call(cfg.dnf.val, on_change_uint8, x6100_control_dnf_set);
    subject_add_observer_and_call(cfg.dnf_center.val, on_change_uint16, x6100_control_dnf_center_set);
    subject_add_observer_and_call(cfg.dnf_width.val, on_change_uint16, x6100_control_dnf_width_set);
    subject_add_observer_and_call(cfg.dnf_auto.val, on_change_uint16, x6100_control_dnf_update_set);
    subject_add_observer_and_call(cfg.nb.val, on_change_uint8, x6100_control_nb_set);
    subject_add_observer_and_call(cfg.nb_level.val, on_change_uint8, x6100_control_nb_level_set);
    subject_add_observer_and_call(cfg.nb_width.val, on_change_uint8, x6100_control_nb_width_set);
    subject_add_observer_and_call(cfg.nr.val, on_change_uint8, x6100_control_nr_set);
    subject_add_observer_and_call(cfg.nr_level.val, on_change_uint8, x6100_control_nr_level_set);

    x6100_control_charger_set(params.charger == RADIO_CHARGER_ON);
    x6100_control_bias_drive_set(params.bias_drive);
    x6100_control_bias_final_set(params.bias_final);

    x6100_control_mic_set(params.mic);
    x6100_control_hmic_set(params.hmic);
    x6100_control_imic_set(params.imic);
    x6100_control_spmode_set(params.spmode.x);

    x6100_control_vox_set(params.vox);
    x6100_control_vox_ag_set(params.vox_ag);
    x6100_control_vox_delay_set(params.vox_delay);
    x6100_control_vox_gain_set(params.vox_gain);

    x6100_control_cmd(x6100_rit, params.rit);
    x6100_control_cmd(x6100_xit, params.xit);
    x6100_control_linein_set(params.line_in);
    x6100_control_lineout_set(params.line_out);
    x6100_control_cmd(x6100_monilevel, params.moni);

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

radio_state_t radio_get_state() {
    return state;
}

void radio_set_freq(int32_t freq) {
    if (!radio_check_freq(freq)) {
        LV_LOG_ERROR("Freq %i incorrect", freq);
        return;
    }
    x6100_vfo_t vfo = subject_get_int(cfg_cur.band->vfo.val);
    int32_t shift = cfg_transverter_get_shift(freq);
    WITH_RADIO_LOCK(x6100_control_vfo_freq_set(vfo, freq - shift));
}

bool radio_check_freq(int32_t freq) {
    if (freq >= 500000 && freq <= 55000000) {
        return true;
    }
    return cfg_transverter_get_shift(freq) != 0;
}

uint16_t radio_change_vol(int16_t df) {
    int32_t vol = subject_get_int(cfg.vol.val);
    if (df == 0) {
        return vol;
    }

    mute = false;

    uint16_t new_val = limit(vol + df, 0, 55);

    if (new_val != vol) {
        subject_set_int(cfg.vol.val, new_val);
    };

    return new_val;
}

void radio_change_mute() {
    mute = !mute;
    x6100_control_rxvol_set(mute ? 0 : subject_get_int(cfg.vol.val));
}

uint16_t radio_change_moni(int16_t df) {
    if (df == 0) {
        return params.moni;
    }

    int16_t new_val = limit(params.moni + df, 0, 100);
    if (new_val != params.moni) {
        params_lock();
        params.moni = new_val;
        params_unlock(&params.dirty.moni);
        WITH_RADIO_LOCK(x6100_control_cmd(x6100_monilevel, params.moni));
        lv_msg_send(MSG_PARAM_CHANGED, NULL);
    }

    return params.moni;
}

bool radio_change_spmode(int16_t df) {
    if (df == 0) {
        return params.spmode.x;
    }

    params_bool_set(&params.spmode, df > 0);
    lv_msg_send(MSG_PARAM_CHANGED, NULL);

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

    subject_set_int(cfg_cur.mode, x6100_mode_am);
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
        x6100_control_txpwr_set(subject_get_float(cfg.pwr.val));
        radio_unlock();
    }
}

void radio_set_pwr(float d) {
    WITH_RADIO_LOCK(x6100_control_txpwr_set(d));
}

x6100_mic_sel_t radio_change_mic(int16_t d) {
    if (d == 0) {
        return params.mic;
    }

    params_lock();

    switch (params.mic) {
        case x6100_mic_builtin:
            params.mic = d > 0 ? x6100_mic_handle : x6100_mic_auto;
            break;

        case x6100_mic_handle:
            params.mic = d > 0 ? x6100_mic_auto : x6100_mic_builtin;
            break;

        case x6100_mic_auto:
            params.mic = d > 0 ? x6100_mic_builtin : x6100_mic_handle;
            break;
    }

    params_unlock(&params.dirty.mic);
    lv_msg_send(MSG_PARAM_CHANGED, NULL);

    WITH_RADIO_LOCK(x6100_control_mic_set(params.mic));

    return params.mic;
}

uint8_t radio_change_hmic(int16_t d) {
    if (d == 0) {
        return params.hmic;
    }
    int32_t new_val = limit(params.hmic + d, 0, 50);
    CHANGE_PARAM(new_val, params.hmic, params.dirty.hmic, x6100_control_hmic_set);

    return params.hmic;
}

uint8_t radio_change_imic(int16_t d) {
    if (d == 0) {
        return params.imic;
    }

    int32_t new_val = limit(params.imic + d, 0, 35);
    CHANGE_PARAM(new_val, params.imic, params.dirty.imic, x6100_control_imic_set);

    return params.imic;
}

x6100_vfo_t radio_toggle_vfo() {
    x6100_vfo_t new_vfo = (subject_get_int(cfg_cur.band->vfo.val) == X6100_VFO_A) ? X6100_VFO_B : X6100_VFO_A;

    subject_set_int(cfg_cur.band->vfo.val, new_vfo);
    // TODO: move to another file
    voice_say_text_fmt("V F O %s", (new_vfo == X6100_VFO_A) ? "A" : "B");

    return new_vfo;
}

void radio_poweroff() {
    if (params.charger == RADIO_CHARGER_SHADOW) {
        WITH_RADIO_LOCK(x6100_control_charger_set(true));
    }

    state = RADIO_POWEROFF;
}

radio_charger_t radio_change_charger(int16_t d) {
    if (d == 0) {
        return params.charger;
    }

    params_lock();

    switch (params.charger) {
        case RADIO_CHARGER_OFF:
            params.charger = d > 0 ? RADIO_CHARGER_ON : RADIO_CHARGER_SHADOW;
            break;

        case RADIO_CHARGER_ON:
            params.charger = d > 0 ? RADIO_CHARGER_SHADOW : RADIO_CHARGER_OFF;
            break;

        case RADIO_CHARGER_SHADOW:
            params.charger = d > 0 ? RADIO_CHARGER_OFF : RADIO_CHARGER_ON;
            break;
    }

    params_unlock(&params.dirty.charger);
    lv_msg_send(MSG_PARAM_CHANGED, NULL);

    WITH_RADIO_LOCK(x6100_control_charger_set(params.charger == RADIO_CHARGER_ON));

    return params.charger;
}

void radio_set_ptt(bool tx) {
    WITH_RADIO_LOCK(x6100_control_ptt_set(tx));
}

void radio_set_modem(bool tx) {
    WITH_RADIO_LOCK(x6100_control_modem_set(tx));
}

int16_t radio_change_rit(int16_t d) {
    if (d == 0) {
        return params.rit;
    }

    int16_t new_val = limit(align_int(params.rit + d * 10, 10), -1500, +1500);
    if (new_val != params.rit) {
        params_lock();
        params.rit = new_val;
        params_unlock(&params.dirty.rit);
        lv_msg_send(MSG_PARAM_CHANGED, NULL);
        WITH_RADIO_LOCK(x6100_control_cmd(x6100_rit, params.rit));
    }

    return params.rit;
}

int16_t radio_change_xit(int16_t d) {
    if (d == 0) {
        return params.xit;
    }

    int16_t new_val = limit(align_int(params.xit + d * 10, 10), -1500, +1500);
    if (new_val != params.xit) {
        params_lock();
        params.xit = new_val;
        params_unlock(&params.dirty.xit);
        lv_msg_send(MSG_PARAM_CHANGED, NULL);
        WITH_RADIO_LOCK(x6100_control_cmd(x6100_xit, params.xit));
    }

    return params.xit;
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

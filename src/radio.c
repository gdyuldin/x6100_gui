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
static void(*low_power_cb)(bool) = NULL;

static pthread_mutex_t  control_mux;

static x6100_flow_t     *pack;
static x6100_base_ver_t base_ver;

static radio_state_t    state = RADIO_RX;
static uint64_t         now_time;
static uint64_t         prev_time;
static uint64_t         idle_time;
static bool             mute = false;

static cfloat           samples_buf[RADIO_SAMPLES*2];

typedef struct __attribute__((__packed__)) {
    uint32_t lo_freq;
    uint8_t flow_fmt;
    uint8_t flow_seq_n: 4;
    uint8_t flow_seq_total: 4;
    uint8_t vary_freq: 1;
    uint8_t fft_dec: 3;
    uint32_t _pad1: 12;
    uint32_t _pad2;
} flow_info_t;

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


typedef enum {
    x6100_flow_fp32 = 0,
    x6100_flow_bf16,
} x6100_flow_fmt_t;

static void radio_lock() {
    pthread_mutex_lock(&control_mux);
}

static void radio_unlock() {
    idle_time = get_time();
    pthread_mutex_unlock(&control_mux);
}

#include <math.h>
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

static uint32_t min_tx;
static uint32_t max_tx;

bool radio_tick() {
    if (now_time < prev_time) {
        prev_time = now_time;
    }

    int32_t d = now_time - prev_time;

    if (x6100_flow_read(pack)) {
        prev_time = now_time;

        static uint8_t delay;
        bool vary_freq = false;

        if (delay++ > 10) {
            delay = 0;
            clock_update_power(pack->vext * 0.1f, pack->vbat*0.1f, pack->batcap, pack->flag.charging);
            if (low_power_cb) {
                low_power_cb(!pack->flag.vext && (pack->vbat <= 60));
            }
        }
        flow_info_t *flow_info = (flow_info_t*)pack->reserved_3;

        uint32_t base_freq = 0;
        uint8_t fft_dec = 0;
        if (base_ver.rev >= 8) {
            base_freq = flow_info->lo_freq;
            fft_dec = 1U << flow_info->fft_dec;
        }

        cfloat *flow_samples = (cfloat*)((char *)pack + offsetof(x6100_flow_t, samples));
        cfloat *samples;
        size_t n_samples;

        if (base_ver.rev >= 8) {
            if (flow_info->flow_fmt == x6100_flow_fp32) {
                samples = flow_samples;
                n_samples = RADIO_SAMPLES;
            } else if (flow_info->flow_fmt == x6100_flow_bf16) {
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
            vary_freq = flow_info->vary_freq;
        } else {
            samples = flow_samples;
            n_samples = RADIO_SAMPLES;
        }
        // printf("%d from %d, freq: %d, vary_freq: %d\n", flow_info->flow_seq_n, seq_total, base_freq, vary_freq);
        // union {
        //     uint32_t i;
        //     float f;
        // } fuint = {flow_info->_pad2};
        // printf("audio rms: %f\n", 20.0f * log10f(fuint.f));
        // printf("audio mul: %.*g\n", fuint.f);
        dsp_samples(samples, n_samples, pack->flag.tx, base_freq, flow_info->vary_freq, fft_dec);

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
            usleep(2000);
        }

        int32_t idle = now_time - idle_time;

        if (idle > IDLE_TIMEOUT && state == RADIO_RX) {
            WITH_RADIO_LOCK(x6100_control_idle());

            idle_time = now_time;
        }
    }
}

static void on_change_bool(Subject *subj, void *user_data) {
    int32_t new_val = subject_get_int(subj);
    void (*fn)(bool) = (void (*)(bool))user_data;
    WITH_RADIO_LOCK(fn(new_val));
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
    int32_t new_val;
    if (vfo == X6100_VFO_A) {
        new_val = subject_get_int(cfg_cur.band->vfo_a.freq.val);
    } else {
        new_val = subject_get_int(cfg_cur.band->vfo_b.freq.val);
    }
    int32_t shift = cfg_transverter_get_shift(new_val);
    shift += subject_get_int(cfg_cur.band->if_shift.val);
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
        WITH_RADIO_LOCK(x6100_control_comp_set(true));
    } else {
        radio_lock();
        x6100_control_comp_set(false);
        x6100_control_comp_level_set((x6100_comp_level_t)(ratio - 2));
        radio_unlock();
    }
}

void on_fw_zoom_change(Subject *subj, void *user_data) {
    uint8_t zoom = subject_get_int(subj);
    uint8_t val = 0;
    while (zoom > 1) {
        val++;
        zoom >>= 1;
    }
    WITH_RADIO_LOCK(x6100_control_fftdec_set(val));
}

void on_if_shift_change(Subject *subj, void *user_data) {
    int32_t shift = subject_get_int(subj);
    int32_t cur_freq = subject_get_int(cfg_cur.fg_freq);

    LV_LOG_USER("Shift: %d, cur_freq: %d\n", shift, cur_freq);
    if (shift != 0) {
        radio_lock();
        x6100_control_if_shift_freq_set(shift);
        x6100_control_if_shift_set(true);
        radio_unlock();
    } else {
        WITH_RADIO_LOCK(x6100_control_if_shift_set(false));
    }
    x6100_vfo_t vfo = subject_get_int(cfg_cur.band->vfo.val);
    on_vfo_freq_change(NULL, (void*)vfo);
}

void on_band_change(Subject *subj, void *user_data) {
    // Workaround for bug with ignored IQ offset on band change from BASE
    int32_t i_val = subject_get_int(cfg_cur.band->tx_i_offset.val);
    int32_t q_val = subject_get_int(cfg_cur.band->tx_q_offset.val);
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

void base_control_command(Subject *subj, void *user_data) {
    uint32_t val = subject_get_int(subj);
    x6100_cmd_enum_t cmd = (x6100_cmd_enum_t)user_data;
    WITH_RADIO_LOCK(x6100_control_cmd(cmd, val));
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

    base_ver = x6100_control_get_base_ver();

    // Enable center mode by default (old behavior)
    if ((util_compare_version(base_ver, (x6100_base_ver_t){1, 1, 9, 0}) >= 0) || (base_ver.rev >= 8)) {
        x6100_control_if_shift_set(false);
    }

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

    subject_add_observer_and_call(cfg_cur.band->if_shift.val, on_if_shift_change, NULL);

    subject_add_observer_and_call(cfg_cur.band->tx_i_offset.val, on_change_int32, x6100_control_tx_i_offset_set);
    subject_add_observer_and_call(cfg_cur.band->tx_q_offset.val, on_change_int32, x6100_control_tx_q_offset_set);

    subject_add_observer(cfg.band_id.val, on_band_change, NULL);

    subject_add_observer(cfg_cur.agc, update_agc_time, NULL);
    subject_add_observer_and_call(cfg_cur.mode, update_agc_time, NULL);

    subject_add_observer_and_call(cfg_cur.filter.low, on_low_filter_change, NULL);
    subject_add_observer_and_call(cfg_cur.filter.high, on_high_filter_change, NULL);

    subject_add_observer_and_call(cfg.vol.val, on_change_uint8, x6100_control_rxvol_set);
    subject_add_observer_and_call(cfg.sql.val, on_change_uint8, x6100_control_sql_set);
    subject_add_observer_and_call(cfg.pwr.val, on_change_float, x6100_control_txpwr_set);
    subject_add_observer_and_call(cfg.output_gain.val, on_change_float, x6100_control_adc_dac_gain_set);
    subject_add_observer_and_call(cfg_cur.band->dac_offset.val, on_change_float, x6100_control_dac_gain_set);
    subject_add_observer_and_call(cfg.atu_enabled.val, on_change_uint8, x6100_control_atu_set);
    subject_add_observer_and_call(cfg_cur.atu->network, on_atu_network_change, NULL);
    subject_add_observer_and_call(cfg.comp.val, on_change_comp_ratio, NULL);
    subject_add_observer_and_call(cfg.comp_threshold_offset.val, on_change_float, x6100_control_comp_threshold_set);
    subject_add_observer_and_call(cfg.comp_makeup_offset.val, on_change_float, x6100_control_comp_makeup_set);

    subject_add_observer_and_call(cfg.mic.val, on_change_uint8, x6100_control_mic_set);
    subject_add_observer_and_call(cfg.hmic.val, on_change_uint8, x6100_control_hmic_set);
    subject_add_observer_and_call(cfg.imic.val, on_change_uint8, x6100_control_imic_set);
    subject_add_observer_and_call(cfg.moni.val, on_change_uint8, x6100_control_moni_set);

    subject_add_observer_and_call(cfg.rit.val, base_control_command, (void*)x6100_rit);
    subject_add_observer_and_call(cfg.xit.val, base_control_command, (void*)x6100_xit);
    subject_add_observer_and_call(cfg.fm_emphasis.val, on_change_bool, x6100_control_fm_emp);

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

    if ((util_compare_version(base_ver, (x6100_base_ver_t){1, 1, 9, 0}) >= 0) || (base_ver.rev >= 8)) {
        subject_add_observer_and_call(cfg_cur.zoom, on_fw_zoom_change, NULL);
    }

    x6100_control_charger_set(params.charger.x == RADIO_CHARGER_ON);
    x6100_control_bias_drive_set(params.bias_drive);
    x6100_control_bias_final_set(params.bias_final);

    x6100_control_spmode_set(params.spmode.x);

    x6100_control_vox_set(params.vox);
    x6100_control_vox_ag_set(params.vox_ag);
    x6100_control_vox_delay_set(params.vox_delay);
    x6100_control_vox_gain_set(params.vox_gain);

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

x6100_vfo_t radio_toggle_vfo() {
    x6100_vfo_t new_vfo = (subject_get_int(cfg_cur.band->vfo.val) == X6100_VFO_A) ? X6100_VFO_B : X6100_VFO_A;

    subject_set_int(cfg_cur.band->vfo.val, new_vfo);
    // TODO: move to another file
    voice_say_text_fmt("V F O %s", (new_vfo == X6100_VFO_A) ? "A" : "B");

    return new_vfo;
}

void radio_poweroff() {
    if (params.charger.x == RADIO_CHARGER_SHADOW) {
        WITH_RADIO_LOCK(x6100_control_charger_set(true));
    }

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

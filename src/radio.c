/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

#include <unistd.h>
#include <stdio.h>
#include <pthread.h>
#include <string.h>

#include <aether_radio/x6100_control/low/flow.h>
#include <aether_radio/x6100_control/low/gpio.h>

#include "util.h"
#include "radio.h"
#include "dsp.h"
#include "main_screen.h"
#include "waterfall.h"
#include "params.h"
#include "hkey.h"
#include "tx_info.h"
#include "meter.h"
#include "events.h"
#include "clock.h"
#include "info.h"
#include "dialog_swrscan.h"
#include "voice.h"

#define FLOW_RESTART_TIMOUT 300
#define IDLE_TIMEOUT        (3 * 1000)

static lv_obj_t         *main_obj;

static pthread_mutex_t  control_mux;

static x6100_flow_t     *pack;

static radio_state_t    state = RADIO_RX;
static uint64_t         now_time;
static uint64_t         prev_time;
static uint64_t         idle_time;
static bool             mute = false;

static void update_agc_time();

static void radio_lock() {
    pthread_mutex_lock(&control_mux);
}

static void radio_unlock() {
    idle_time = get_time();
    pthread_mutex_unlock(&control_mux);
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
            clock_update_power(pack->vext * 0.1f, pack->vbat*0.1f, pack->batcap);
        }

        dsp_samples(pack->samples, RADIO_SAMPLES);

        switch (state) {
            case RADIO_RX:
                if (pack->flag.tx) {
                    state = RADIO_TX;
                    event_send(main_obj, EVENT_RADIO_TX, NULL);
                }
                break;

            case RADIO_TX:
                if (!pack->flag.tx) {
                    state = RADIO_RX;
                    event_send(main_obj, EVENT_RADIO_RX, NULL);
                } else {
                    tx_info_update(pack->tx_power * 0.1f, pack->vswr * 0.1f, pack->alc_level * 0.1f);
                }
                break;

            case RADIO_ATU_START:
                radio_lock();
                x6100_control_atu_tune(true);
                radio_unlock();
                state = RADIO_ATU_WAIT;
                break;
                
            case RADIO_ATU_WAIT:
                if (pack->flag.tx) {
                    event_send(main_obj, EVENT_RADIO_TX, NULL);
                    state = RADIO_ATU_RUN;
                }
                break;
                
            case RADIO_ATU_RUN:
                if (!pack->flag.tx) {
                    params_atu_save(pack->atu_params);
                    radio_lock();
                    x6100_control_atu_tune(false);
                    radio_unlock();
                    event_send(main_obj, EVENT_RADIO_RX, NULL);
                    
                    if (params.atu) {
                        radio_lock();
                        x6100_control_cmd(x6100_atu_network, pack->atu_params);
                        radio_unlock();
                        params.atu_loaded = true;
                        event_send(main_obj, EVENT_ATU_UPDATE, NULL);
                    }
                    state = RADIO_RX;
                } else {
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
        if (d > FLOW_RESTART_TIMOUT) {
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
            usleep(5000);
        }
        
        int32_t idle = now_time - idle_time;
        
        if (idle > IDLE_TIMEOUT && state == RADIO_RX) {
            pthread_mutex_lock(&control_mux);
            x6100_control_idle();
            pthread_mutex_unlock(&control_mux);
            
            idle_time = now_time;
        }
    }
}

void radio_vfo_set() {
    uint64_t shift;

    radio_lock();

    for (int i = 0; i < 2; i++) {
        x6100_control_vfo_mode_set(i, params_band.vfo_x[i].mode);
        x6100_control_vfo_agc_set(i, params_band.vfo_x[i].agc);
        x6100_control_vfo_pre_set(i, params_band.vfo_x[i].pre);
        x6100_control_vfo_att_set(i, params_band.vfo_x[i].att);
        
        radio_check_freq(params_band.vfo_x[i].freq, &shift);
        x6100_control_vfo_freq_set(i, params_band.vfo_x[i].freq - shift);
        params_band.vfo_x[i].shift = (shift != 0);
    }

    x6100_control_vfo_set(params_band.vfo);
    x6100_control_split_set(params_band.split);
    x6100_control_rfg_set(params_band.rfg);
    radio_unlock();

    params_bands_find(params_band.vfo_x[params_band.vfo].freq, &params.freq_band);
}

void radio_mode_set() {
    x6100_mode_t    mode = radio_current_mode();

    radio_lock();
    
    if (mode == x6100_mode_am || mode == x6100_mode_nfm) {
        x6100_control_cmd(x6100_filter1_low, -params_mode.filter_high);
        x6100_control_cmd(x6100_filter2_low, -params_mode.filter_high);
    } else {
        x6100_control_cmd(x6100_filter1_low, params_mode.filter_low);
        x6100_control_cmd(x6100_filter2_low, params_mode.filter_low);
    }

    x6100_control_cmd(x6100_filter1_high, params_mode.filter_high);
    x6100_control_cmd(x6100_filter2_high, params_mode.filter_high);

    radio_unlock();
    update_agc_time();
}

void radio_bb_reset() {
    x6100_gpio_set(x6100_pin_bb_reset, 1);
    usleep(100000);
    x6100_gpio_set(x6100_pin_bb_reset, 0);
}

void radio_init(lv_obj_t *obj) {
    if (!x6100_gpio_init())
        return;

    while (!x6100_control_init()) {
        usleep(100000);
    }

    if (!x6100_flow_init())
        return;

    x6100_gpio_set(x6100_pin_wifi, 1);          /* WiFi off */
    x6100_gpio_set(x6100_pin_morse_key, 1);     /* Morse key off */
    
    main_obj = obj;

    pack = malloc(sizeof(x6100_flow_t));

    radio_vfo_set();
    radio_mode_set();
    radio_load_atu();

    x6100_control_rxvol_set(params.vol);
    x6100_control_rfg_set(params_band.rfg);
    x6100_control_sql_set(params.sql);
    x6100_control_atu_set(params.atu);
    x6100_control_txpwr_set(params.pwr);
    x6100_control_charger_set(params.charger == RADIO_CHARGER_ON);
    x6100_control_bias_drive_set(params.bias_drive);
    x6100_control_bias_final_set(params.bias_final);

    x6100_control_key_speed_set(params.key_speed);
    x6100_control_key_mode_set(params.key_mode);
    x6100_control_iambic_mode_set(params.iambic_mode);
    x6100_control_key_tone_set(params.key_tone);
    x6100_control_key_vol_set(params.key_vol);
    x6100_control_key_train_set(params.key_train);
    x6100_control_qsk_time_set(params.qsk_time);
    x6100_control_key_ratio_set(params.key_ratio * 0.1f);

    x6100_control_mic_set(params.mic);
    x6100_control_hmic_set(params.hmic);
    x6100_control_imic_set(params.imic);
    x6100_control_spmode_set(params.spmode.x);

    x6100_control_dnf_set(params.dnf);
    x6100_control_dnf_center_set(params.dnf_center);
    x6100_control_dnf_width_set(params.dnf_width);
    x6100_control_nb_set(params.nb);
    x6100_control_nb_level_set(params.nb_level);
    x6100_control_nb_width_set(params.nb_width);
    x6100_control_nr_set(params.nr);
    x6100_control_nr_level_set(params.nr_level);

    x6100_control_agc_hang_set(params.agc_hang);
    x6100_control_agc_knee_set(params.agc_knee);
    x6100_control_agc_slope_set(params.agc_slope);

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

radio_state_t radio_get_state() {
    return state;
}

void radio_set_freq(uint64_t freq) {
    uint64_t shift = 0;
    
    if (!radio_check_freq(freq, &shift)) {
        LV_LOG_ERROR("Freq %llu incorrect", freq);
        return;
    }

    params_lock();
    params_band.vfo_x[params_band.vfo].freq = freq;
    params_band.vfo_x[params_band.vfo].shift = (shift != 0);
    params_unlock(&params_band.vfo_x[params_band.vfo].durty.freq);

    radio_lock();
    x6100_control_vfo_freq_set(params_band.vfo, freq - shift);
    radio_unlock();

    radio_load_atu();
}

bool radio_check_freq(uint64_t freq, uint64_t *shift) {
    if (freq >= 500000 && freq <= 55000000) {
        if (shift != NULL) {
            *shift = 0;
        }
        return true;
    }
    
    for (uint8_t i = 0; i < TRANSVERTER_NUM; i++)
        if (freq >= params_transverter[i].from && freq <= params_transverter[i].to) {
            if (shift != NULL) {
                *shift = params_transverter[i].shift;
            }
            return true;
        }

    return false;
}

uint64_t radio_change_freq(int32_t df, uint64_t *prev_freq) {
    *prev_freq = params_band.vfo_x[params_band.vfo].freq;

    radio_set_freq(align_long(params_band.vfo_x[params_band.vfo].freq + df, abs(df)));

    return params_band.vfo_x[params_band.vfo].freq;
}

uint16_t radio_change_vol(int16_t df) {
    if (df == 0) {
        return params.vol;
    }
    
    mute = false;
    
    params_lock();
    params.vol = limit(params.vol + df, 0, 55);
    params_unlock(&params.durty.vol);

    radio_lock();
    x6100_control_rxvol_set(params.vol);
    radio_unlock();
    
    return params.vol;
}

void radio_change_mute() {
    mute = !mute;
    x6100_control_rxvol_set(mute ? 0 : params.vol);
}

uint16_t radio_change_moni(int16_t df) {
    if (df == 0) {
        return params.moni;
    }
    
    params_lock();
    params.moni = limit(params.moni + df, 0, 100);
    params_unlock(&params.durty.moni);

    radio_lock();
    x6100_control_cmd(x6100_monilevel, params.moni);
    radio_unlock();
    
    return params.moni;
}

bool radio_change_spmode(int16_t df) {
    if (df == 0) {
        return params.spmode.x;
    }
    
    params_bool_set(&params.spmode, df > 0);

    radio_lock();
    x6100_control_spmode_set(params.spmode.x);
    radio_unlock();
    
    return params.spmode.x;
}

uint16_t radio_change_rfg(int16_t df) {
    if (df == 0) {
        return params_band.rfg;
    }
    
    params_lock();
    params_band.rfg = limit(params_band.rfg + df, 0, 100);
    params_unlock(&params_band.durty.rfg);

    radio_lock();
    x6100_control_rfg_set(params_band.rfg);
    radio_unlock();

    return params_band.rfg;
}

uint16_t radio_change_sql(int16_t df) {
    if (df == 0) {
        return params.sql;
    }
    
    params_lock();
    params.sql = limit(params.sql + df, 0, 100);
    params_unlock(&params.durty.sql);

    radio_lock();
    x6100_control_sql_set(params.sql);
    radio_unlock();

    return params.sql;
}

bool radio_change_pre() {
    bool vfoa = (params_band.vfo == X6100_VFO_A);

    params_lock();
    
    params_band.vfo_x[params_band.vfo].pre = !params_band.vfo_x[params_band.vfo].pre;
    params_unlock(&params_band.vfo_x[params_band.vfo].durty.pre);

    radio_lock();
    x6100_control_vfo_pre_set(params_band.vfo, params_band.vfo_x[params_band.vfo].pre);
    radio_unlock();

    voice_say_text_fmt("Preamplifier %s", params_band.vfo_x[params_band.vfo].pre ? "On" : "Off");
    return params_band.vfo_x[params_band.vfo].pre;
}

bool radio_change_att() {
    params_lock();
    
    params_band.vfo_x[params_band.vfo].att = !params_band.vfo_x[params_band.vfo].att;
    params_unlock(&params_band.vfo_x[params_band.vfo].durty.att);

    radio_lock();
    x6100_control_vfo_att_set(params_band.vfo, params_band.vfo_x[params_band.vfo].att);
    radio_unlock();

    voice_say_text_fmt("Attenuator %s", params_band.vfo_x[params_band.vfo].att ? "On" : "Off");
    return params_band.vfo_x[params_band.vfo].att;
}

void radio_filter_get(int32_t *from_freq, int32_t *to_freq) {
    x6100_mode_t    mode = radio_current_mode();

    switch (mode) {
        case x6100_mode_lsb:
        case x6100_mode_lsb_dig:
            *from_freq = -params_mode.filter_high;
            *to_freq = -params_mode.filter_low;
            break;
            
        case x6100_mode_usb:
        case x6100_mode_usb_dig:
            *from_freq = params_mode.filter_low;
            *to_freq = params_mode.filter_high;
            break;

        case x6100_mode_cw:
            *from_freq = params_mode.filter_low;
            *to_freq = params_mode.filter_high;
            break;
            
        case x6100_mode_cwr:
            *from_freq = -params_mode.filter_high;
            *to_freq = -params_mode.filter_low;
            break;

        case x6100_mode_am:
        case x6100_mode_nfm:
            *from_freq = -params_mode.filter_high;
            *to_freq = params_mode.filter_high;
            break;
            
        default:
            *from_freq = 0;
            *to_freq = 0;
    }
}

void radio_set_mode(x6100_vfo_t vfo,  x6100_mode_t mode) {
    params_lock();
    params_band.vfo_x[vfo].mode = mode;
    params_unlock(&params_band.vfo_x[vfo].durty.mode);

    radio_lock();
    x6100_control_vfo_mode_set(vfo, mode);
    radio_unlock();
}

void radio_restore_mode(x6100_mode_t mode) {
    x6100_vfo_t vfo = params_band.vfo;

    params_lock();
    params_band.vfo_x[vfo].mode = mode;
    params_unlock(&params_band.vfo_x[vfo].durty.mode);

    radio_lock();
    x6100_control_vfo_mode_set(vfo, mode);
    radio_unlock();
}

void radio_change_mode(radio_mode_t select) {

    x6100_mode_t    mode = radio_current_mode();

    switch (select) {
        case RADIO_MODE_AM:
            switch (mode) {
                case x6100_mode_am:
                    mode = x6100_mode_nfm;
                    voice_say_text_fmt("N F M modulation");
                    break;
                    
                case x6100_mode_nfm:
                    mode = x6100_mode_am;
                    voice_say_text_fmt("A M modulation");
                    break;
                    
                default:
                    mode = x6100_mode_am;
                    voice_say_text_fmt("A M modulation");
                    break;
            }
            break;
            
        case RADIO_MODE_CW:
            switch (mode) {
                case x6100_mode_cw:
                    mode = x6100_mode_cwr;
                    voice_say_text_fmt("CWR modulation");
                    break;
                    
                case x6100_mode_cwr:
                    mode = x6100_mode_cw;
                    voice_say_text_fmt("CW modulation");
                    break;
                    
                default:
                    mode = x6100_mode_cw;
                    voice_say_text_fmt("CW modulation");
                    break;
            }
            break;
            
        case RADIO_MODE_SSB:
            switch (mode) {
                case x6100_mode_lsb:
                    mode = x6100_mode_lsb_dig;
                    voice_say_text_fmt("LSB digital modulation");
                    break;
                    
                case x6100_mode_lsb_dig:
                    mode = x6100_mode_usb;
                    voice_say_text_fmt("USB modulation");
                    break;
                    
                case x6100_mode_usb:
                    mode = x6100_mode_usb_dig;
                    voice_say_text_fmt("USB digital modulation");
                    break;
                    
                case x6100_mode_usb_dig:
                    mode = x6100_mode_lsb;
                    voice_say_text_fmt("LSB modulation");
                    break;
                    
                default: 
                    mode = x6100_mode_lsb;
                    voice_say_text_fmt("LSB modulation");
                    break;
            }
            break;
            
        case RADIO_MODE_USB:
            mode = x6100_mode_usb;
            break;

        case RADIO_MODE_LSB:
            mode = x6100_mode_lsb;
            break;
        
        default:
            break;
    }

    params_mode_save();
    radio_set_mode(params_band.vfo, mode);
}

x6100_mode_t radio_current_mode() {
    return params_band.vfo_x[params_band.vfo].mode;
}

uint32_t radio_change_filter_low(int32_t df) {
    if (df == 0) {
        return params_mode.filter_low;
    }

    x6100_mode_t    mode = radio_current_mode();

    if (mode == x6100_mode_am || mode == x6100_mode_nfm) {
        return 0;
    }

    params_lock();

    params_mode.filter_low = align_int(params_mode.filter_low + df * 10, 10);

    if (params_mode.filter_low < 0) {
        params_mode.filter_low = 0;
    } else if (params_mode.filter_low > 6000) {
        params_mode.filter_low = 6000;
    } else if (params_mode.filter_low > params_mode.filter_high) {
        params_mode.filter_low = params_mode.filter_high;
    }
    params_unlock(&params_mode.durty.filter_low);

    radio_lock();
    x6100_control_cmd(x6100_filter1_low, params_mode.filter_low);
    x6100_control_cmd(x6100_filter2_low, params_mode.filter_low);
    radio_unlock();
    
    return params_mode.filter_low;
}

uint32_t radio_change_filter_high(int32_t df) {
    if (df == 0) {
        return params_mode.filter_high;
    }

    x6100_mode_t    mode = radio_current_mode();

    params_lock();
    params_mode.filter_high = align_int(params_mode.filter_high + df * 50, 50);

    if (params_mode.filter_high < 0) {
        params_mode.filter_high = 0;
    } else if (params_mode.filter_high > 6000) {
        params_mode.filter_high = 6000;
    } else if (params_mode.filter_high < params_mode.filter_low) {
        params_mode.filter_high = params_mode.filter_low;
    }
    params_unlock(&params_mode.durty.filter_high);

    radio_lock();
    
    if (mode == x6100_mode_am || mode == x6100_mode_nfm) {
        x6100_control_cmd(x6100_filter1_low, -params_mode.filter_high);
        x6100_control_cmd(x6100_filter2_low, -params_mode.filter_high);
    }

    x6100_control_cmd(x6100_filter1_high, params_mode.filter_high);
    x6100_control_cmd(x6100_filter2_high, params_mode.filter_high);
    
    radio_unlock();
    return params_mode.filter_high;
}

static void update_agc_time() {
    x6100_agc_t     agc = params_band.vfo_x[params_band.vfo].agc;
    x6100_mode_t    mode = radio_current_mode();
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

    radio_lock();
    x6100_control_agc_time_set(agc_time);
    radio_unlock();
}

void radio_change_agc() {
    params_lock();

    x6100_agc_t     agc = params_band.vfo_x[params_band.vfo].agc;
    
    switch (agc) {
        case x6100_agc_off:
            agc = x6100_agc_slow;
            voice_say_text_fmt("Auto gain slow mode");
            break;
            
        case x6100_agc_slow:
            agc = x6100_agc_fast;
            voice_say_text_fmt("Auto gain fast mode");
            break;
            
        case x6100_agc_fast:
            agc = x6100_agc_auto;
            voice_say_text_fmt("Auto gain auto mode");
            break;
            
        case x6100_agc_auto:
            agc = x6100_agc_off;
            voice_say_text_fmt("Auto gain off");
            break;
    }

    update_agc_time();

    params_band.vfo_x[params_band.vfo].agc = agc;
    params_unlock(&params_band.vfo_x[params_band.vfo].durty.agc);

    radio_lock();
    x6100_control_vfo_agc_set(params_band.vfo, agc);
    radio_unlock();
}

void radio_change_atu() {
    params_lock();
    params.atu = !params.atu;
    params_unlock(&params.durty.atu);

    radio_lock();
    x6100_control_atu_set(params.atu);
    radio_unlock();
    
    radio_load_atu();
    voice_say_text_fmt("Auto tuner %s", params.atu ? "On" : "Off");
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
    
    state = RADIO_SWRSCAN;

    x6100_control_vfo_mode_set(params_band.vfo, x6100_mode_am);
    x6100_control_txpwr_set(5.0f);
    x6100_control_swrscan_set(true);
        
    return true;
}

void radio_stop_swrscan() {
    if (state == RADIO_SWRSCAN) {
        x6100_control_swrscan_set(false);
        x6100_control_txpwr_set(params.pwr);
        state = RADIO_RX;
    }
}

void radio_load_atu() {
    if (params.atu) {
        if (params_band.vfo_x[params_band.vfo].shift) {
            info_atu_update();

            radio_lock();
            x6100_control_atu_set(false);
            radio_unlock();

            return;
        }
    
        uint32_t atu = params_atu_load(&params.atu_loaded);

        radio_lock();
        x6100_control_atu_set(true);
        x6100_control_cmd(x6100_atu_network, atu);
        radio_unlock();
    
        if (state != RADIO_SWRSCAN) {
            info_atu_update();
        }
    }
}

float radio_change_pwr(int16_t d) {
    if (d == 0) {
        return params.pwr;
    }
    
    params_lock();
    params.pwr += d * 0.1f;
    
    if (params.pwr > 10.0f) {
        params.pwr = 10.0f;
    } else if (params.pwr < 0.1f) {
        params.pwr = 0.1f;
    }
    
    params_unlock(&params.durty.pwr);

    radio_lock();
    x6100_control_txpwr_set(params.pwr);
    radio_unlock();
    
    return params.pwr;
}

void radio_set_pwr(float d) {
    radio_lock();
    x6100_control_txpwr_set(d);
    radio_unlock();
}

uint16_t radio_change_key_speed(int16_t d) {
    if (d == 0) {
        return params.key_speed;
    }
    
    params_lock();
    params.key_speed = limit(params.key_speed + d, 5, 50);
    params_unlock(&params.durty.key_speed);

    radio_lock();
    x6100_control_key_speed_set(params.key_speed);
    radio_unlock();

    return params.key_speed;
}

x6100_key_mode_t radio_change_key_mode(int16_t d) {
    if (d == 0) {
        return params.key_mode;
    }

    params_lock();

    switch (params.key_mode) {
        case x6100_key_manual:
            params.key_mode = d > 0 ? x6100_key_auto_left : x6100_key_auto_right;
            break;
            
        case x6100_key_auto_left:
            params.key_mode = d > 0 ? x6100_key_auto_right : x6100_key_manual;
            break;
            
        case x6100_key_auto_right:
            params.key_mode = d > 0 ? x6100_key_manual : x6100_key_auto_left;
            break;
    }

    params_unlock(&params.durty.key_mode);

    radio_lock();
    x6100_control_key_mode_set(params.key_mode);
    radio_unlock();

    return params.key_mode;
}

x6100_iambic_mode_t radio_change_iambic_mode(int16_t d) {
    if (d == 0) {
        return params.iambic_mode;
    }

    params_lock();

    params.iambic_mode = (params.iambic_mode == x6100_iambic_a) ? x6100_iambic_b : x6100_iambic_a;

    params_unlock(&params.durty.iambic_mode);

    radio_lock();
    x6100_control_iambic_mode_set(params.iambic_mode);
    radio_unlock();

    return params.iambic_mode;
}

uint16_t radio_change_key_tone(int16_t d) {
    if (d == 0) {
        return params.key_tone;
    }

    params_lock();

    params.key_tone += (d > 0) ? 10 : -10;

    if (params.key_tone < 400) {
        params.key_tone = 400;
    } else if (params.key_tone > 1200) {
        params.key_tone = 1200;
    }

    params_unlock(&params.durty.key_tone);

    radio_lock();
    x6100_control_key_tone_set(params.key_tone);
    radio_unlock();
    
    return params.key_tone;
}

uint16_t radio_change_key_vol(int16_t d) {
    if (d == 0) {
        return params.key_vol;
    }

    params_lock();

    params.key_vol = limit(params.key_vol + d, 0, 32);
    params_unlock(&params.durty.key_vol);

    radio_lock();
    x6100_control_key_vol_set(params.key_vol);
    radio_unlock();

    return params.key_vol;
}

bool radio_change_key_train(int16_t d) {
    if (d == 0) {
        return params.key_train;
    }

    params_lock();
    params.key_train = !params.key_train;
    params_unlock(&params.durty.key_train);

    radio_lock();
    x6100_control_key_train_set(params.key_train);
    radio_unlock();
    
    return params.key_train;
}

uint16_t radio_change_qsk_time(int16_t d) {
    if (d == 0) {
        return params.qsk_time;
    }

    params_lock();

    int16_t x = params.qsk_time;
    
    if (d > 0) {
        x += 10;
    } else {
        x -= 10;
    }
    
    params.qsk_time = limit(x, 0, 1000);
    params_unlock(&params.durty.qsk_time);

    radio_lock();
    x6100_control_qsk_time_set(params.qsk_time);
    radio_unlock();
    
    return params.qsk_time;
}

uint8_t radio_change_key_ratio(int16_t d) {
    if (d == 0) {
        return params.key_ratio;
    }

    params_lock();

    int16_t x = params.key_ratio;

    if (d > 0) {
        x += 5;
    } else {
        x -= 5;
    }

    params.key_ratio = limit(x, 25, 45);
    params_unlock(&params.durty.key_ratio);

    radio_lock();
    x6100_control_key_ratio_set(params.key_ratio * 0.1f);
    radio_unlock();

    return params.key_ratio;
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
    
    params_unlock(&params.durty.mic);
    
    radio_lock();
    x6100_control_mic_set(params.mic);
    radio_unlock();
    
    return params.mic;
}

uint8_t radio_change_hmic(int16_t d) {
    if (d == 0) {
        return params.hmic;
    }

    params_lock();
    params.hmic = limit(params.hmic + d, 0, 50);
    params_unlock(&params.durty.hmic);
    
    radio_lock();
    x6100_control_hmic_set(params.hmic);
    radio_unlock();
    
    return params.hmic;
}

uint8_t radio_change_imic(int16_t d) {
    if (d == 0) {
        return params.imic;
    }

    params_lock();
    params.imic = limit(params.imic + d, 0, 35);
    params_unlock(&params.durty.imic);
    
    radio_lock();
    x6100_control_imic_set(params.imic);
    radio_unlock();
    
    return params.imic;
}

x6100_vfo_t radio_set_vfo(x6100_vfo_t vfo) {
    params_lock();
    params_band.vfo = vfo;
    params_unlock(&params_band.durty.vfo);

    radio_lock();
    x6100_control_vfo_set(vfo);
    radio_unlock();
}

x6100_vfo_t radio_change_vfo() {
    x6100_vfo_t vfo = (params_band.vfo == X6100_VFO_A) ? X6100_VFO_B : X6100_VFO_A;

    radio_set_vfo(vfo);
    voice_say_text_fmt("V F O %s", (params_band.vfo == X6100_VFO_A) ? "A" : "B");
    
    return params_band.vfo;
}

void radio_change_split() {
    params_lock();
    params_band.split = !params_band.split;
    params_unlock(&params_band.durty.split);
    
    radio_lock();
    x6100_control_split_set(params_band.split);
    radio_unlock();
    voice_say_text_fmt("Split %s", params_band.split ? "On" : "Off");
}

void radio_poweroff() {
    if (params.charger == RADIO_CHARGER_SHADOW) {
        radio_lock();
        x6100_control_charger_set(true);
        radio_unlock();
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
    
    params_unlock(&params.durty.charger);
    
    radio_lock();
    x6100_control_charger_set(params.charger == RADIO_CHARGER_ON);
    radio_unlock();
    
    return params.charger;
}

bool radio_change_dnf(int16_t d) {
    if (d == 0) {
        return params.dnf;
    }

    params_lock();
    params.dnf = !params.dnf;
    params_unlock(&params.durty.dnf);
    
    radio_lock();
    x6100_control_dnf_set(params.dnf);
    radio_unlock();

    return params.dnf;
}

uint16_t radio_change_dnf_center(int16_t d) {
    if (d == 0) {
        return params.dnf_center;
    }

    params_lock();
    params.dnf_center = limit(params.dnf_center + d * 50, 100, 3000);
    params_unlock(&params.durty.dnf_center);
    
    radio_lock();
    x6100_control_dnf_center_set(params.dnf_center);
    radio_unlock();

    return params.dnf_center;
}

uint16_t radio_change_dnf_width(int16_t d) {
    if (d == 0) {
        return params.dnf_width;
    }

    params_lock();
    params.dnf_width = limit(params.dnf_width + d * 5, 10, 100);
    params_unlock(&params.durty.dnf_width);
    
    radio_lock();
    x6100_control_dnf_width_set(params.dnf_width);
    radio_unlock();

    return params.dnf_width;
}

bool radio_change_nb(int16_t d) {
    if (d == 0) {
        return params.nb;
    }

    params_lock();
    params.nb = !params.nb;
    params_unlock(&params.durty.nb);
    
    radio_lock();
    x6100_control_nb_set(params.nb);
    radio_unlock();

    return params.nb;
}

uint8_t radio_change_nb_level(int16_t d) {
    if (d == 0) {
        return params.nb_level;
    }

    params_lock();
    params.nb_level = limit(params.nb_level + d * 5, 0, 100);
    params_unlock(&params.durty.nb_level);
    
    radio_lock();
    x6100_control_nb_level_set(params.nb_level);
    radio_unlock();

    return params.nb_level;
}

uint8_t radio_change_nb_width(int16_t d) {
    if (d == 0) {
        return params.nb_width;
    }

    params_lock();
    params.nb_width = limit(params.nb_width + d * 5, 0, 100);
    params_unlock(&params.durty.nb_width);
    
    radio_lock();
    x6100_control_nb_width_set(params.nb_width);
    radio_unlock();

    return params.nb_width;
}

bool radio_change_nr(int16_t d) {
    if (d == 0) {
        return params.nr;
    }

    params_lock();
    params.nr = !params.nr;
    params_unlock(&params.durty.nr);
    
    radio_lock();
    x6100_control_nr_set(params.nr);
    radio_unlock();

    return params.nr;
}

uint8_t radio_change_nr_level(int16_t d) {
    if (d == 0) {
        return params.nr_level;
    }

    params_lock();
    params.nr_level = limit(params.nr_level + d * 5, 0, 60);
    params_unlock(&params.durty.nr_level);
    
    radio_lock();
    x6100_control_nr_level_set(params.nr_level);
    radio_unlock();

    return params.nr_level;
}

bool radio_change_agc_hang(int16_t d) {
    if (d == 0) {
        return params.agc_hang;
    }

    params_lock();
    params.agc_hang = !params.agc_hang;
    params_unlock(&params.durty.agc_hang);

    radio_lock();
    x6100_control_agc_hang_set(params.agc_hang);
    radio_unlock();

    return params.agc_hang;
}

int8_t radio_change_agc_knee(int16_t d) {
    if (d == 0) {
        return params.agc_knee;
    }

    params_lock();
    params.agc_knee = limit(params.agc_knee + d, -100, 0);
    params_unlock(&params.durty.agc_knee);

    radio_lock();
    x6100_control_agc_knee_set(params.agc_knee);
    radio_unlock();

    return params.agc_knee;
}

uint8_t radio_change_agc_slope(int16_t d) {
    if (d == 0) {
        return params.agc_slope;
    }

    params_lock();
    params.agc_slope = limit(params.agc_slope + d, 0, 10);
    params_unlock(&params.durty.agc_slope);

    radio_lock();
    x6100_control_agc_slope_set(params.agc_slope);
    radio_unlock();

    return params.agc_slope;
}

void radio_set_ptt(bool tx) {
    radio_lock();
    x6100_control_ptt_set(tx);
    radio_unlock();
}

int16_t radio_change_rit(int16_t d) {
    if (d == 0) {
        return params.rit;
    }
    
    params_lock();
    params.rit = limit(align_int(params.rit + d * 10, 10), -1500, +1500);
    params_unlock(&params.durty.rit);

    radio_lock();
    x6100_control_cmd(x6100_rit, params.rit);
    radio_unlock();
    
    return params.rit;
}

int16_t radio_change_xit(int16_t d) {
    if (d == 0) {
        return params.xit;
    }
    
    params_lock();
    params.xit = limit(align_int(params.xit + d * 10, 10), -1500, +1500);
    params_unlock(&params.durty.xit);

    radio_lock();
    x6100_control_cmd(x6100_xit, params.xit);
    radio_unlock();
    
    return params.xit;
}

void radio_set_line_in(uint8_t d) {
    params_lock();
    params.line_in = d;
    params_unlock(&params.durty.line_in);
    
    radio_lock();
    x6100_control_linein_set(d);
    radio_unlock();
}

void radio_set_line_out(uint8_t d) {
    params_lock();
    params.line_out = d;
    params_unlock(&params.durty.line_out);
    
    radio_lock();
    x6100_control_lineout_set(d);
    radio_unlock();
}

void radio_set_morse_key(bool on) {
    x6100_gpio_set(x6100_pin_morse_key, on ? 0 : 1);
}

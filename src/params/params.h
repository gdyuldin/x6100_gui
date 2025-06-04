/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#include <aether_radio/x6100_control/control.h>
#include <ft8lib/constants.h>
#include "../radio.h"
#include "../clock.h"
#include "../voice.h"
#include "common.h"
#include "types.h"
#include "../cfg/cfg.h"

typedef enum {
    BUTTONS_DARK = 0,
    BUTTONS_LIGHT,
    BUTTONS_TEMPORARILY
} buttons_light_t;

typedef enum {
    ACTION_NONE = 0,
    ACTION_SCREENSHOT,
    ACTION_RECORDER,
    ACTION_MUTE,
    ACTION_STEP_UP,
    ACTION_STEP_DOWN,
    ACTION_VOICE_MODE,
    ACTION_BAT_INFO,
    ACTION_NR_TOGGLE,
    ACTION_NB_TOGGLE,

    ACTION_APP_RTTY = 100,
    ACTION_APP_FT8,
    ACTION_APP_SWRSCAN,
    ACTION_APP_GPS,
    ACTION_APP_SETTINGS,
    ACTION_APP_RECORDER,
    ACTION_APP_QTH,
    ACTION_APP_CALLSIGN,
    ACTION_APP_WIFI,
} press_action_t;

typedef enum {
    FREQ_ACCEL_NONE = 0,
    FREQ_ACCEL_LITE,
    FREQ_ACCEL_STRONG,
} freq_accel_t;

/* Themes */
typedef enum {
    THEME_SIMPLE,
    THEME_LEGACY,
    THEME_NAKED,
} themes_t;


/* Params */

typedef struct {
    uint64_t            vol_modes;
    uint64_t            mfk_modes;

    /* LCD */

    int16_t             brightness_normal;
    int16_t             brightness_idle;
    uint16_t            brightness_timeout; /* seconds */
    buttons_light_t     brightness_buttons;

    /* radio */

    x6100_mic_sel_t     mic;
    uint8_t             hmic;
    uint8_t             imic;
    radio_charger_t     charger;
    uint16_t            bias_drive;
    uint16_t            bias_final;
    int16_t             rit;
    int16_t             xit;
    uint8_t             line_in;
    uint8_t             line_out;
    int16_t             moni;
    params_bool_t       spmode;
    params_uint8_t      freq_accel;

    /* VOX */

    bool                vox;
    uint8_t             vox_ag;
    uint16_t            vox_delay;
    uint8_t             vox_gain;

    /* main screen */

    int16_t             spectrum_beta;
    bool                spectrum_peak;
    uint16_t            spectrum_peak_hold;
    float               spectrum_peak_speed;
    bool                spectrum_filled;
    params_bool_t       waterfall_smooth_scroll;
    params_bool_t       waterfall_center_line;
    params_bool_t       waterfall_zoom;
    params_bool_t       mag_freq;
    params_bool_t       mag_info;
    params_bool_t       mag_alc;
    clock_view_t        clock_view;
    uint8_t             clock_time_timeout;     /* seconds */
    uint8_t             clock_power_timeout;    /* seconds */
    uint8_t             clock_tx_timeout;       /* seconds */

    /* Msg */

    uint16_t            cw_encoder_period;  /* seconds */
    uint16_t            voice_msg_period;   /* seconds */

    /* RTTY */

    uint16_t            rtty_center;
    uint16_t            rtty_shift;
    uint32_t            rtty_rate;
    bool                rtty_reverse;
    uint8_t             rtty_bits;
    float               rtty_snr;

    /* FT8 */

    params_uint16_t     ft8_tx_freq;
    params_str_t        ft8_cq_modifier;

    // FT8 gain offset for different radios/bands/modes
    params_float_t      ft8_output_gain_offset;

    /* Long press actions */

    uint8_t             long_gen;
    uint8_t             long_app;
    uint8_t             long_key;
    uint8_t             long_msg;
    uint8_t             long_dfn;
    uint8_t             long_dfl;

    /* HMic F1, F2 actions */

    uint8_t             press_f1;
    uint8_t             press_f2;
    uint8_t             long_f1;
    uint8_t             long_f2;

    /* Audio play/rec */

    params_float_t      play_gain_db_f;
    params_float_t      rec_gain_db_f;

    /* Voice */

    params_uint8_t      voice_mode;
    params_uint8_t      voice_lang;
    params_uint8_t      voice_rate;
    params_uint8_t      voice_pitch;
    params_uint8_t      voice_volume;

    params_str_t        qth;
    params_str_t        callsign;

    /* WiFi / BT */

    params_bool_t       wifi_enabled;

    /* Theme */

    params_uint8_t       theme;

    /* durty flags */

    struct {
        bool    vol_modes;
        bool    mfk_modes;

        bool    brightness_normal;
        bool    brightness_idle;
        bool    brightness_timeout;
        bool    brightness_buttons;

        bool    band;
        bool    mic;
        bool    hmic;
        bool    imic;
        bool    charger;
        bool    rit;
        bool    xit;
        bool    line_in;
        bool    line_out;
        bool    moni;

        bool    vox;
        bool    vox_ag;
        bool    vox_delay;
        bool    vox_gain;

        bool    spectrum_beta;
        bool    spectrum_peak;
        bool    spectrum_peak_hold;
        bool    spectrum_peak_speed;
        bool    spectrum_filled;
        bool    clock_view;
        bool    clock_time_timeout;
        bool    clock_power_timeout;
        bool    clock_tx_timeout;

        bool    cw_encoder_period;
        bool    voice_msg_period;

        bool    rtty_center;
        bool    rtty_shift;
        bool    rtty_rate;
        bool    rtty_reverse;

        bool    ft8_show_all;
        bool    ft8_protocol;
        bool    ft8_band;

        bool    long_gen;
        bool    long_app;
        bool    long_key;
        bool    long_msg;
        bool    long_dfn;
        bool    long_dfl;

        bool    press_f1;
        bool    press_f2;
        bool    long_f1;
        bool    long_f2;

    } dirty;
} params_t;


extern params_t params;

void params_init();

void params_bool_set(params_bool_t *var, bool x);
void params_uint8_set(params_uint8_t *var, uint8_t x);
void params_uint16_set(params_uint16_t *var, uint16_t x);
void params_str_set(params_str_t *var, const char *x);
void params_float_set(params_float_t *var, float x);

uint8_t params_uint8_change(params_uint8_t *var, int16_t df);

void params_msg_cw_load();
void params_msg_cw_new(const char *val);
void params_msg_cw_edit(uint32_t id, const char *val);
void params_msg_cw_delete(uint32_t id);

char *params_charger_str_get(radio_charger_t val);

char *params_mic_str_get(x6100_mic_sel_t val);

char *params_key_mode_str_get(x6100_key_mode_t val);

char *params_iambic_mode_str_ger(x6100_iambic_mode_t val);
char *params_comp_str_get(uint8_t comp);

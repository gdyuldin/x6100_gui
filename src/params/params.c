/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */


#include "params.h"

#include "db.h"

#include "../util.h"
#include "../mfk.h"
#include "../vol.h"
#include "../dialog_msg_cw.h"
#include "../qth/qth.h"

#include "lvgl/lvgl.h"

#include <unistd.h>
#include <stdio.h>
#include <pthread.h>
#include <sqlite3.h>
#include <string.h>

#define BAND_NOT_LOADED -10

params_t params = {
    .vol_modes              = (1 << VOL_VOL) | (1 << VOL_RFG) | (1 << VOL_FILTER_LOW) | (1 << VOL_FILTER_HIGH) | (1 << VOL_PWR) | (1 << VOL_HMIC),
    .mfk_modes              = (1 << MFK_SPECTRUM_FACTOR) | (1 << MFK_SPECTRUM_BETA) | (1 << MFK_PEAK_HOLD) | (1<< MFK_PEAK_SPEED),

    .brightness_normal      = 9,
    .brightness_idle        = 1,
    .brightness_timeout     = 10,
    .brightness_buttons     = BUTTONS_TEMPORARILY,

    .spectrum_beta          = 70,
    .spectrum_filled        = true,
    .spectrum_peak          = true,
    .spectrum_peak_hold     = 5000,
    .spectrum_peak_speed    = 0.5f,
    .spectrum_auto_min      = { .x = true,  .name = "spectrum_auto_min",        .voice = "Auto minimum of spectrum" },
    .spectrum_auto_max      = { .x = true,  .name = "spectrum_auto_max",        .voice = "Auto maximum of spectrum" },
    .waterfall_auto_min     = { .x = true,  .name = "waterfall_auto_min",       .voice = "Auto minimum of waterfall" },
    .waterfall_auto_max     = { .x = true,  .name = "waterfall_auto_max",       .voice = "Auto maximum of waterfall" },
    .waterfall_smooth_scroll= { .x = true,  .name = "waterfall_smooth_scroll",  .voice = "Waterfall smooth scroll"},
    .waterfall_center_line  = { .x = true,  .name = "waterfall_center_line",    .voice = "Waterfall center line"},
    .waterfall_zoom         = { .x = true,  .name = "waterfall_zoom",           .voice = "Waterfall zoom"},
    .mag_freq               = { .x = true,  .name = "mag_freq",                 .voice = "Magnification of frequency" },
    .mag_info               = { .x = true,  .name = "mag_info",                 .voice = "Magnification of info" },
    .mag_alc                = { .x = true,  .name = "mag_alc",                  .voice = "Magnification of A L C" },
    .clock_view             = CLOCK_TIME_POWER,
    .clock_time_timeout     = 5,
    .clock_power_timeout    = 3,
    .clock_tx_timeout       = 1,

    .ant                    = 1,
    .pwr                    = 5.0f,
    .mic                    = x6100_mic_auto,
    .hmic                   = 20,
    .imic                   = 30,
    .charger                = true,
    .bias_drive             = 450,
    .bias_final             = 650,
    .rit                    = 0,
    .xit                    = 0,
    .line_in                = 10,
    .line_out               = 10,
    .moni                   = 59,
    .spmode                 = { .x = false,             .name = "spmode",           .voice = "Speaker mode" },
    .freq_accel             = { .x = FREQ_ACCEL_LITE,   .name = "freq_accel",       .voice = "Frequency acceleration" },

    .dnf                    = false,
    .dnf_center             = 1000,
    .dnf_width              = 50,
    .nb                     = false,
    .nb_level               = 10,
    .nb_width               = 10,
    .nr                     = false,
    .nr_level               = 0,

    .agc_hang               = false,
    .agc_knee               = -60,
    .agc_slope              = 6,

    .vox                    = false,
    .vox_ag                 = 0,
    .vox_delay              = 100,
    .vox_gain               = 50,

    .key_speed              = 15,
    .key_mode               = x6100_key_manual,
    .iambic_mode            = x6100_iambic_a,
    .key_vol                = 10,
    .key_train              = false,
    .qsk_time               = 100,
    .key_ratio              = 30,

    .cw_decoder             = true,
    .cw_tune                = false,
    .cw_decoder_snr         = 5.0f,
    .cw_decoder_snr_gist    = 1.0f,
    .cw_decoder_peak_beta   = 0.10f,
    .cw_decoder_noise_beta  = 0.80f,

    .cw_encoder_period      = 10,
    .voice_msg_period       = 10,

    .rtty_center            = 800,
    .rtty_shift             = 170,
    .rtty_rate              = 4545,
    .rtty_reverse           = false,
    .rtty_bits              = 5,
    .rtty_snr               = 3.0f,

    .swrscan_linear         = true,
    .swrscan_span           = 200000,

    .ft8_show_all           = true,
    .ft8_protocol           = FTX_PROTOCOL_FT8,
    .ft8_tx_freq            = { .x = 1325,      .name = "ft8_tx_freq" },
    .ft8_auto               = { .x = true,      .name = "ft8_auto" },
    .ft8_output_gain_offset = { .x = 0.0f,      .name = "ft8_output_gain_offset" },
    .ft8_cq_modifier        = { .x = "",        .name = "ft8_cq_modifier"},

    .long_gen               = ACTION_SCREENSHOT,
    .long_app               = ACTION_APP_RECORDER,
    .long_key               = ACTION_NONE,
    .long_msg               = ACTION_RECORDER,
    .long_dfn               = ACTION_VOICE_MODE,
    .long_dfl               = ACTION_BAT_INFO,

    .press_f1               = ACTION_STEP_UP,
    .press_f2               = ACTION_NONE,
    .long_f1                = ACTION_STEP_DOWN,
    .long_f2                = ACTION_NONE,

    .play_gain_db_f         = { .x = 0.0f, .name = "play_gain_db_f"},
    .rec_gain_db_f          = { .x = 0.0f, .name = "rec_gain_db_f"},

    .voice_mode             = { .x = VOICE_LCD,                                 .name = "voice_mode" },
    .voice_lang             = { .x = 0,   .min = 0,  .max = (VOICES_NUM - 1),   .name = "voice_lang" },
    .voice_rate             = { .x = 100, .min = 50, .max = 150,                .name = "voice_rate",     .voice = "Voice rate" },
    .voice_pitch            = { .x = 100, .min = 50, .max = 150,                .name = "voice_pitch",    .voice = "Voice pitch" },
    .voice_volume           = { .x = 100, .min = 50, .max = 150,                .name = "voice_volume",   .voice = "Voice volume" },

    .qth                    = { .x = "",  .max_len = 6, .name = "qth" },
    .callsign               = { .x = "",  .max_len = 12, .name = "callsign" },

    .wifi_enabled           = { .x = false, .name="wifi_enabled" },

    .theme                  = { .x = THEME_SIMPLE, .name="theme"},

    .atu                    = { .x = false, .name="atu" },
    .current_band           = { .id = BAND_NOT_LOADED },
};

transverter_t params_transverter[TRANSVERTER_NUM] = {
    { .from = 144000000,    .to = 150000000,    .shift = 116000000 },
    { .from = 432000000,    .to = 438000000,    .shift = 404000000 }
};

static sqlite3_stmt     *write_mode_stmt;
static sqlite3_stmt     *save_atu_stmt;
static sqlite3_stmt     *load_atu_stmt;
static sqlite3_stmt     *bands_find_all_stmt;
static sqlite3_stmt     *bands_find_stmt;


/* System params */

static bool params_load_bool(params_bool_t *var, const char *name, const int32_t x) {
    if (strcmp(name, var->name) == 0) {
        var->x = x;
        return true;
    }

    return false;
}

static bool params_load_uint8(params_uint8_t *var, const char *name, const int32_t x) {
    if (strcmp(name, var->name) == 0) {
        var->x = x;
        return true;
    }

    return false;
}

static bool params_load_uint16(params_uint16_t *var, const char *name, const int32_t x) {
    if (strcmp(name, var->name) == 0) {
        var->x = x;
        return true;
    }

    return false;
}

static bool params_load_float(params_float_t *var, const char *name, float x) {
    if (strcmp(name, var->name) == 0) {
        var->x = x;
        return true;
    }

    return false;
}

static bool params_load_str(params_str_t *var, const char *name, const char *x) {
    if (strcmp(name, var->name) == 0) {
        strncpy(var->x, x, sizeof(var->x) - 1);
        return true;
    }

    return false;
}

static bool params_load() {
    sqlite3_stmt    *stmt;
    int             rc;

    rc = sqlite3_prepare_v2(db, "SELECT * FROM params", -1, &stmt, 0);

    if (rc != SQLITE_OK) {
        return false;
    }

    while (sqlite3_step(stmt) != SQLITE_DONE) {
        const char      *name = sqlite3_column_text(stmt, 0);
        const float     f = sqlite3_column_double(stmt, 1);
        const int32_t   i = sqlite3_column_int(stmt, 1);
        const int64_t   l = sqlite3_column_int64(stmt, 1);
        const char      *t = sqlite3_column_text(stmt, 1);

        if (strcmp(name, "band") == 0) {
            params.band_id = i;
            params_band_load(i);
        } else if (strcmp(name, "sql") == 0) {
            params.sql = i;
        } else if (strcmp(name, "pwr") == 0) {
            params.pwr = i * 0.1f;
        } else if (strcmp(name, "spectrum_beta") == 0) {
            params.spectrum_beta = i;
        } else if (strcmp(name, "spectrum_filled") == 0) {
            params.spectrum_filled = i;
        } else if (strcmp(name, "spectrum_peak") == 0) {
            params.spectrum_peak = i;
        } else if (strcmp(name, "spectrum_peak_hold") == 0) {
            params.spectrum_peak_hold = i;
        } else if (strcmp(name, "spectrum_peak_speed") == 0) {
            params.spectrum_peak_speed = i * 0.1f;
        } else if (strcmp(name, "key_speed") == 0) {
            params.key_speed = i;
        } else if (strcmp(name, "key_mode") == 0) {
            params.key_mode = i;
        } else if (strcmp(name, "iambic_mode") == 0) {
            params.iambic_mode = i;
        } else if (strcmp(name, "key_vol") == 0) {
            params.key_vol = i;
        } else if (strcmp(name, "key_train") == 0) {
            params.key_train = i;
        } else if (strcmp(name, "qsk_time") == 0) {
            params.qsk_time = i;
        } else if (strcmp(name, "key_ratio") == 0) {
            params.key_ratio = i;
        } else if (strcmp(name, "mic") == 0) {
            params.mic = i;
        } else if (strcmp(name, "hmic") == 0) {
            params.hmic = i;
        } else if (strcmp(name, "imic") == 0) {
            params.imic = i;
        } else if (strcmp(name, "charger") == 0) {
            params.charger = i;
        } else if (strcmp(name, "dnf") == 0) {
            params.dnf = i;
        } else if (strcmp(name, "dnf_center") == 0) {
            params.dnf_center = i;
        } else if (strcmp(name, "dnf_width") == 0) {
            params.dnf_width = i;
        } else if (strcmp(name, "nb") == 0) {
            params.nb = i;
        } else if (strcmp(name, "nb_level") == 0) {
            params.nb_level = i;
        } else if (strcmp(name, "nb_width") == 0) {
            params.nb_width = i;
        } else if (strcmp(name, "nr") == 0) {
            params.nr = i;
        } else if (strcmp(name, "nr_level") == 0) {
            params.nr_level = i;
        } else if (strcmp(name, "agc_hang") == 0) {
            params.agc_hang = i;
        } else if (strcmp(name, "agc_knee") == 0) {
            params.agc_knee = i;
        } else if (strcmp(name, "agc_slope") == 0) {
            params.agc_slope = i;
        } else if (strcmp(name, "cw_decoder") == 0) {
            params.cw_decoder = i;
        } else if (strcmp(name, "cw_tune") == 0) {
            params.cw_tune = i;
        } else if (strcmp(name, "cw_decoder_snr_2") == 0) {
            params.cw_decoder_snr = i * 0.1f;
        } else if (strcmp(name, "cw_decoder_peak_beta") == 0) {
            params.cw_decoder_peak_beta = i * 0.01f;
        } else if (strcmp(name, "cw_decoder_noise_beta") == 0) {
            params.cw_decoder_noise_beta = i * 0.01f;
        } else if (strcmp(name, "cw_encoder_period") == 0) {
            params.cw_encoder_period = i;
        } else if (strcmp(name, "voice_msg_period") == 0) {
            params.voice_msg_period = i;
        } else if (strcmp(name, "vol_modes") == 0) {
            params.vol_modes = l;
        } else if (strcmp(name, "mfk_modes") == 0) {
            params.mfk_modes = l;
        } else if (strcmp(name, "rtty_rate") == 0) {
            params.rtty_rate = i;
        } else if (strcmp(name, "rtty_shift") == 0) {
            params.rtty_shift = i;
        } else if (strcmp(name, "rtty_center") == 0) {
            params.rtty_center = i;
        } else if (strcmp(name, "rtty_reverse") == 0) {
            params.rtty_reverse = i;
        } else if (strcmp(name, "ant") == 0) {
            params.ant = i;
        } else if (strcmp(name, "rit") == 0) {
            params.rit = i;
        } else if (strcmp(name, "xit") == 0) {
            params.xit = i;
        } else if (strcmp(name, "brightness_normal") == 0) {
            params.brightness_normal = i;
        } else if (strcmp(name, "brightness_idle") == 0) {
            params.brightness_idle = i;
        } else if (strcmp(name, "brightness_timeout") == 0) {
            params.brightness_timeout = i;
        } else if (strcmp(name, "brightness_buttons") == 0) {
            params.brightness_buttons = i;
        } else if (strcmp(name, "line_in") == 0) {
            params.line_in = i;
        } else if (strcmp(name, "line_out") == 0) {
            params.line_out = i;
        } else if (strcmp(name, "moni") == 0) {
            params.moni = i;
        } else if (strcmp(name, "clock_view") == 0) {
            params.clock_view = i;
        } else if (strcmp(name, "clock_time_timeout") == 0) {
            params.clock_time_timeout = i;
        } else if (strcmp(name, "clock_power_timeout") == 0) {
            params.clock_power_timeout = i;
        } else if (strcmp(name, "clock_tx_timeout") == 0) {
            params.clock_tx_timeout = i;
        } else if (strcmp(name, "swrscan_linear") == 0) {
            params.swrscan_linear = i;
        } else if (strcmp(name, "swrscan_span") == 0) {
            params.swrscan_span = i;
        } else if (strcmp(name, "ft8_show_all") == 0) {
            params.ft8_show_all = i;
        } else if (strcmp(name, "ft8_protocol") == 0) {
            params.ft8_protocol = i;
        } else if (strcmp(name, "long_gen") == 0) {
            params.long_gen = i;
        } else if (strcmp(name, "long_app") == 0) {
            params.long_app = i;
        } else if (strcmp(name, "long_key") == 0) {
            params.long_key = i;
        } else if (strcmp(name, "long_msg") == 0) {
            params.long_msg = i;
        } else if (strcmp(name, "long_dfn") == 0) {
            params.long_dfn = i;
        } else if (strcmp(name, "long_dfl") == 0) {
            params.long_dfl = i;
        } else if (strcmp(name, "press_f1") == 0) {
            params.press_f1 = i;
        } else if (strcmp(name, "press_f2") == 0) {
            params.press_f2 = i;
        } else if (strcmp(name, "long_f1") == 0) {
            params.long_f1 = i;
        } else if (strcmp(name, "long_f2") == 0) {
            params.long_f2 = i;
        }

        if (params_load_bool(&params.atu, name, i)) continue;

        if (params_load_float(&params.play_gain_db_f, name, f)) continue;
        if (params_load_float(&params.rec_gain_db_f, name, f)) continue;

        if (params_load_bool(&params.mag_freq, name, i)) continue;
        if (params_load_bool(&params.mag_info, name, i)) continue;
        if (params_load_bool(&params.mag_alc, name, i)) continue;
        if (params_load_bool(&params.spectrum_auto_min, name, i)) continue;
        if (params_load_bool(&params.spectrum_auto_max, name, i)) continue;
        if (params_load_bool(&params.waterfall_auto_min, name, i)) continue;
        if (params_load_bool(&params.waterfall_auto_max, name, i)) continue;
        if (params_load_bool(&params.waterfall_smooth_scroll, name, i)) continue;
        if (params_load_bool(&params.waterfall_center_line, name, i)) continue;
        if (params_load_bool(&params.waterfall_zoom, name, i)) continue;
        if (params_load_bool(&params.spmode, name, i)) continue;
        if (params_load_bool(&params.ft8_auto, name, i)) continue;
        if (params_load_float(&params.ft8_output_gain_offset, name, f)) continue;
        if (params_load_str(&params.ft8_cq_modifier, name, t)) continue;

        if (params_load_uint8(&params.voice_mode, name, i)) continue;
        if (params_load_uint8(&params.voice_lang, name, i)) continue;
        if (params_load_uint8(&params.voice_rate, name, i)) continue;
        if (params_load_uint8(&params.voice_pitch, name, i)) continue;
        if (params_load_uint8(&params.voice_volume, name, i)) continue;
        if (params_load_uint8(&params.freq_accel, name, i)) continue;

        if (params_load_uint16(&params.ft8_tx_freq, name, i)) continue;

        if (params_load_str(&params.qth, name, t)) continue;

        if (params_load_str(&params.callsign, name, t)) continue;
        if (params_load_bool(&params.wifi_enabled, name, i)) continue;
        if (params_load_uint8(&params.theme, name, i)) continue;
    }

    sqlite3_finalize(stmt);
    return true;
}

static void params_save_bool(params_bool_t *var) {
    if (var->dirty) {
        params_write_int(var->name, var->x, &var->dirty);
    }
}

static void params_save_uint8(params_uint8_t *var) {
    if (var->dirty) {
        params_write_int(var->name, var->x, &var->dirty);
    }
}

static void params_save_uint16(params_uint16_t *var) {
    if (var->dirty) {
        params_write_int(var->name, var->x, &var->dirty);
    }
}

static void params_save_float(params_float_t *var) {
    if (var->dirty) {
        params_write_float(var->name, var->x, &var->dirty);
    }
}

static void params_save_str(params_str_t *var) {
    if (var->dirty) {
        params_write_text(var->name, var->x, &var->dirty);
    }
}

static void params_save() {
    if (!sql_query_exec("BEGIN")) {
        return;
    }

    if (params.dirty.band)                  params_write_int("band", params.band_id, &params.dirty.band);
    if (params.dirty.sql)                   params_write_int("sql", params.sql, &params.dirty.sql);
    if (params.dirty.pwr)                   params_write_int("pwr", params.pwr * 10, &params.dirty.pwr);

    if (params.dirty.spectrum_beta)         params_write_int("spectrum_beta", params.spectrum_beta, &params.dirty.spectrum_beta);
    if (params.dirty.spectrum_filled)       params_write_int("spectrum_filled", params.spectrum_filled, &params.dirty.spectrum_filled);
    if (params.dirty.spectrum_peak)         params_write_int("spectrum_peak", params.spectrum_peak, &params.dirty.spectrum_peak);
    if (params.dirty.spectrum_peak_hold)    params_write_int("spectrum_peak_hold", params.spectrum_peak_hold, &params.dirty.spectrum_peak_hold);
    if (params.dirty.spectrum_peak_speed)   params_write_int("spectrum_peak_speed", params.spectrum_peak_speed * 10, &params.dirty.spectrum_peak_speed);

    if (params.dirty.key_speed)             params_write_int("key_speed", params.key_speed, &params.dirty.key_speed);
    if (params.dirty.key_mode)              params_write_int("key_mode", params.key_mode, &params.dirty.key_mode);
    if (params.dirty.iambic_mode)           params_write_int("iambic_mode", params.iambic_mode, &params.dirty.iambic_mode);
    if (params.dirty.key_vol)               params_write_int("key_vol", params.key_vol, &params.dirty.key_vol);
    if (params.dirty.key_train)             params_write_int("key_train", params.key_train, &params.dirty.key_train);
    if (params.dirty.qsk_time)              params_write_int("qsk_time", params.qsk_time, &params.dirty.qsk_time);
    if (params.dirty.key_ratio)             params_write_int("key_ratio", params.key_ratio, &params.dirty.key_ratio);

    if (params.dirty.mic)                   params_write_int("mic", params.mic, &params.dirty.mic);
    if (params.dirty.hmic)                  params_write_int("hmic", params.hmic, &params.dirty.hmic);
    if (params.dirty.imic)                  params_write_int("imic", params.imic, &params.dirty.imic);

    if (params.dirty.charger)               params_write_int("charger", params.charger, &params.dirty.charger);

    if (params.dirty.dnf)                   params_write_int("dnf", params.dnf, &params.dirty.dnf);
    if (params.dirty.dnf_center)            params_write_int("dnf_center", params.dnf_center, &params.dirty.dnf_center);
    if (params.dirty.dnf_width)             params_write_int("dnf_width", params.dnf_width, &params.dirty.dnf_width);
    if (params.dirty.nb)                    params_write_int("nb", params.nb, &params.dirty.nb);
    if (params.dirty.nb_level)              params_write_int("nb_level", params.nb_level, &params.dirty.nb_level);
    if (params.dirty.nb_width)              params_write_int("nb_width", params.nb_width, &params.dirty.nb_width);
    if (params.dirty.nr)                    params_write_int("nr", params.nr, &params.dirty.nr);
    if (params.dirty.nr_level)              params_write_int("nr_level", params.nr_level, &params.dirty.nr_level);

    if (params.dirty.agc_hang)              params_write_int("agc_hang", params.agc_hang, &params.dirty.agc_hang);
    if (params.dirty.agc_knee)              params_write_int("agc_knee", params.agc_knee, &params.dirty.agc_knee);
    if (params.dirty.agc_slope)             params_write_int("agc_slope", params.agc_slope, &params.dirty.agc_slope);

    if (params.dirty.cw_decoder)            params_write_int("cw_decoder", params.cw_decoder, &params.dirty.cw_decoder);
    if (params.dirty.cw_tune)               params_write_int("cw_tune", params.cw_tune, &params.dirty.cw_tune);
    if (params.dirty.cw_decoder_snr)        params_write_int("cw_decoder_snr_2", params.cw_decoder_snr * 10, &params.dirty.cw_decoder_snr);
    if (params.dirty.cw_decoder_peak_beta)  params_write_int("cw_decoder_peak_beta", params.cw_decoder_peak_beta * 100, &params.dirty.cw_decoder_peak_beta);
    if (params.dirty.cw_decoder_noise_beta) params_write_int("cw_decoder_noise_beta", params.cw_decoder_noise_beta * 100, &params.dirty.cw_decoder_noise_beta);

    if (params.dirty.cw_encoder_period)     params_write_int("cw_encoder_period", params.cw_encoder_period, &params.dirty.cw_encoder_period);
    if (params.dirty.voice_msg_period)      params_write_int("voice_msg_period", params.voice_msg_period, &params.dirty.voice_msg_period);

    if (params.dirty.vol_modes)             params_write_int64("vol_modes", params.vol_modes, &params.dirty.vol_modes);
    if (params.dirty.mfk_modes)             params_write_int64("mfk_modes", params.mfk_modes, &params.dirty.mfk_modes);

    if (params.dirty.rtty_rate)             params_write_int("rtty_rate", params.rtty_rate, &params.dirty.rtty_rate);
    if (params.dirty.rtty_shift)            params_write_int("rtty_shift", params.rtty_shift, &params.dirty.rtty_shift);
    if (params.dirty.rtty_center)           params_write_int("rtty_center", params.rtty_center, &params.dirty.rtty_center);
    if (params.dirty.rtty_reverse)          params_write_int("rtty_reverse", params.rtty_reverse, &params.dirty.rtty_reverse);

    if (params.dirty.ant)                   params_write_int("ant", params.ant, &params.dirty.ant);
    if (params.dirty.rit)                   params_write_int("rit", params.rit, &params.dirty.rit);
    if (params.dirty.xit)                   params_write_int("xit", params.xit, &params.dirty.xit);

    if (params.dirty.line_in)               params_write_int("line_in", params.line_in, &params.dirty.line_in);
    if (params.dirty.line_out)              params_write_int("line_out", params.line_out, &params.dirty.line_out);

    if (params.dirty.moni)                  params_write_int("moni", params.moni, &params.dirty.moni);

    if (params.dirty.brightness_normal)     params_write_int("brightness_normal", params.brightness_normal, &params.dirty.brightness_normal);
    if (params.dirty.brightness_idle)       params_write_int("brightness_idle", params.brightness_idle, &params.dirty.brightness_idle);
    if (params.dirty.brightness_timeout)    params_write_int("brightness_timeout", params.brightness_timeout, &params.dirty.brightness_timeout);
    if (params.dirty.brightness_buttons)    params_write_int("brightness_buttons", params.brightness_buttons, &params.dirty.brightness_buttons);

    if (params.dirty.clock_view)            params_write_int("clock_view", params.clock_view, &params.dirty.clock_view);
    if (params.dirty.clock_time_timeout)    params_write_int("clock_time_timeout", params.clock_time_timeout, &params.dirty.clock_time_timeout);
    if (params.dirty.clock_power_timeout)   params_write_int("clock_power_timeout", params.clock_power_timeout, &params.dirty.clock_power_timeout);
    if (params.dirty.clock_tx_timeout)      params_write_int("clock_tx_timeout", params.clock_tx_timeout, &params.dirty.clock_tx_timeout);

    if (params.dirty.swrscan_linear)        params_write_int("swrscan_linear", params.swrscan_linear, &params.dirty.swrscan_linear);
    if (params.dirty.swrscan_span)          params_write_int("swrscan_span", params.swrscan_span, &params.dirty.swrscan_span);

    if (params.dirty.ft8_show_all)          params_write_int("ft8_show_all", params.ft8_show_all, &params.dirty.ft8_show_all);
    if (params.dirty.ft8_protocol)          params_write_int("ft8_protocol", params.ft8_protocol, &params.dirty.ft8_protocol);

    if (params.dirty.long_gen)              params_write_int("long_gen", params.long_gen, &params.dirty.long_gen);
    if (params.dirty.long_app)              params_write_int("long_app", params.long_app, &params.dirty.long_app);
    if (params.dirty.long_key)              params_write_int("long_key", params.long_key, &params.dirty.long_key);
    if (params.dirty.long_msg)              params_write_int("long_msg", params.long_msg, &params.dirty.long_msg);
    if (params.dirty.long_dfn)              params_write_int("long_dfn", params.long_dfn, &params.dirty.long_dfn);
    if (params.dirty.long_dfl)              params_write_int("long_dfl", params.long_dfl, &params.dirty.long_dfl);

    if (params.dirty.press_f1)              params_write_int("press_f1", params.press_f1, &params.dirty.press_f1);
    if (params.dirty.press_f2)              params_write_int("press_f2", params.press_f2, &params.dirty.press_f2);
    if (params.dirty.long_f1)               params_write_int("long_f1", params.long_f1, &params.dirty.long_f1);
    if (params.dirty.long_f2)               params_write_int("long_f2", params.long_f2, &params.dirty.long_f2);

    params_save_float(&params.play_gain_db_f);
    params_save_float(&params.rec_gain_db_f);

    params_save_uint8(&params.voice_mode);
    params_save_uint8(&params.voice_lang);
    params_save_uint8(&params.voice_rate);
    params_save_uint8(&params.voice_pitch);
    params_save_uint8(&params.voice_volume);
    params_save_uint8(&params.freq_accel);

    params_save_uint16(&params.ft8_tx_freq);

    params_save_bool(&params.atu);

    params_save_bool(&params.mag_freq);
    params_save_bool(&params.mag_info);
    params_save_bool(&params.mag_alc);
    params_save_bool(&params.spectrum_auto_min);
    params_save_bool(&params.spectrum_auto_max);
    params_save_bool(&params.waterfall_auto_min);
    params_save_bool(&params.waterfall_auto_max);
    params_save_bool(&params.waterfall_smooth_scroll);
    params_save_bool(&params.waterfall_center_line);
    params_save_bool(&params.waterfall_zoom);
    params_save_bool(&params.spmode);
    params_save_bool(&params.ft8_auto);
    params_save_float(&params.ft8_output_gain_offset);
    params_save_str(&params.ft8_cq_modifier);

    params_save_str(&params.qth);
    params_save_str(&params.callsign);
    params_save_bool(&params.wifi_enabled);
    params_save_uint8(&params.theme);

    sql_query_exec("COMMIT");
}

/* Transverter */

bool transverter_load() {
    sqlite3_stmt    *stmt;
    int             rc;

    rc = sqlite3_prepare_v2(db, "SELECT * FROM transverter", -1, &stmt, 0);

    if (rc != SQLITE_OK) {
        return false;
    }

    while (sqlite3_step(stmt) != SQLITE_DONE) {
        const int       id = sqlite3_column_int(stmt, 0);
        const char      *name = sqlite3_column_text(stmt, 1);
        const uint64_t  val = sqlite3_column_int64(stmt, 2);

        if (strcmp(name, "from") == 0) {
            params_transverter[id].from = val;
        } else if (strcmp(name, "to") == 0) {
            params_transverter[id].to = val;
        } else if (strcmp(name, "shift") == 0) {
            params_transverter[id].shift = val;
        }
    }

    sqlite3_finalize(stmt);
    return true;
}

static void transverter_write(sqlite3_stmt *stmt, uint8_t id, const char *name, uint64_t data, bool *dirty) {
    sqlite3_bind_int64(stmt, 1, id);
    sqlite3_bind_text(stmt, 2, name, strlen(name), 0);
    sqlite3_bind_int64(stmt, 3, data);
    sqlite3_step(stmt);
    sqlite3_reset(stmt);
    sqlite3_clear_bindings(stmt);

    *dirty = false;
}

void transverter_save() {
    sqlite3_stmt    *stmt;
    int             rc;

    rc = sqlite3_prepare_v2(db, "INSERT INTO transverter(id, name, val) VALUES(?, ?, ?)", -1, &stmt, 0);

    if (rc != SQLITE_OK) {
        return;
    }

    if (!sql_query_exec("BEGIN")) {
        return;
    }

    for (uint8_t i = 0; i < TRANSVERTER_NUM; i++) {
        transverter_t *transverter = &params_transverter[i];

        if (transverter->dirty.from)    transverter_write(stmt, i, "from", transverter->from, &transverter->dirty.from);
        if (transverter->dirty.to)      transverter_write(stmt, i, "to", transverter->to, &transverter->dirty.to);
        if (transverter->dirty.shift)   transverter_write(stmt, i, "shift", transverter->shift, &transverter->dirty.shift);
    }

    sql_query_exec("COMMIT");
}

/* * */

static void * params_thread(void *arg) {
    while (true) {
        pthread_mutex_lock(&params_mux);
        if (params_ready_to_save()){
            params_save();
            params_band_save(params.band_id);
            params_mode_save();
            transverter_save();
        }
        pthread_mutex_unlock(&params_mux);
        usleep(100000);
    }
}

void params_init() {
    int rc;
    if (database_init()) {
        cfg_init(db);
        if (!params_load()) {
            LV_LOG_ERROR("Load params");
            sqlite3_close(db);
            db = NULL;
        }

        if (rc != SQLITE_OK) {
            LV_LOG_ERROR("Prepare write");
        }

        rc = sqlite3_prepare_v2(db, "INSERT INTO mode_params(mode, name, val) VALUES(?, ?, ?)", -1, &write_mode_stmt, 0);

        if (rc != SQLITE_OK) {
            LV_LOG_ERROR("Prepare mode write");
        }

        rc = sqlite3_prepare_v2(db, "INSERT INTO atu(ant, freq, val) VALUES(?, ?, ?)", -1, &save_atu_stmt, 0);

        if (rc != SQLITE_OK) {
            LV_LOG_ERROR("Prepare atu save");
        }

        rc = sqlite3_prepare_v2(db, "SELECT val FROM atu WHERE ant = ? AND freq = ?", -1, &load_atu_stmt, 0);

        if (rc != SQLITE_OK) {
            LV_LOG_ERROR("Prepare atu load");
        }

        rc = sqlite3_prepare_v2(db,
            "SELECT id,name,start_freq,stop_freq,type FROM bands "
                "WHERE (stop_freq BETWEEN ? AND ?) OR (start_freq BETWEEN ? AND ?) OR (start_freq <= ? AND stop_freq >= ?) "
                "ORDER BY start_freq ASC",
                -1, &bands_find_all_stmt, 0
        );

        if (rc != SQLITE_OK) {
            LV_LOG_ERROR("Prepare bands all find");
        }

        rc = sqlite3_prepare_v2(db,  "SELECT id,name,start_freq,stop_freq,type FROM bands WHERE (? BETWEEN start_freq AND stop_freq)", -1, &bands_find_stmt, 0);

        if (rc != SQLITE_OK) {
            LV_LOG_ERROR("Prepare bands find");
        }

        if (!transverter_load()) {
            LV_LOG_ERROR("Load transverter");
        }
    } else {
        LV_LOG_ERROR("Open params.db");
    }

    pthread_t thread;

    pthread_create(&thread, NULL, params_thread, NULL);
    pthread_detach(thread);
    params_modulation_setup(&params_lo_offset_get);
}

int32_t params_lo_offset_get() {
    x6100_mode_t mode = radio_current_mode();
    int32_t key_tone = subject_get_int(cfg.key_tone.val);
    switch (mode) {
        case x6100_mode_cw:
            return -key_tone;
        case x6100_mode_cwr:
            return key_tone;
        default:
            return 0;
    }
}

void params_atu_save(uint32_t val) {
    uint64_t freq = params_band_cur_freq_get();

    params_lock();

    sqlite3_bind_int(save_atu_stmt, 1, params.ant);
    sqlite3_bind_int(save_atu_stmt, 2, freq / 50000);
    sqlite3_bind_int(save_atu_stmt, 3, val);

    sqlite3_step(save_atu_stmt);
    sqlite3_reset(save_atu_stmt);
    sqlite3_clear_bindings(save_atu_stmt);

    params_unlock(NULL);
}

uint32_t params_atu_load(bool *loaded) {
    uint32_t    res = 0;
    uint64_t    freq = params_band_cur_freq_get();

    *loaded = false;

    params_lock();

    sqlite3_bind_int(load_atu_stmt, 1, params.ant);
    sqlite3_bind_int(load_atu_stmt, 2, freq / 50000);

    if (sqlite3_step(load_atu_stmt) != SQLITE_DONE) {
        res = sqlite3_column_int64(load_atu_stmt, 0);
        *loaded = true;
    }

    sqlite3_reset(load_atu_stmt);
    sqlite3_clear_bindings(load_atu_stmt);

    params_unlock(NULL);

    return res;
}

void params_msg_cw_load() {
    sqlite3_stmt    *stmt;
    int             rc;

    rc = sqlite3_prepare_v2(db, "SELECT id,val FROM msg_cw", -1, &stmt, 0);

    if (rc != SQLITE_OK) {
        return;
    }

    while (sqlite3_step(stmt) != SQLITE_DONE) {
        int         id = sqlite3_column_int(stmt, 0);
        const char  *val = sqlite3_column_text(stmt, 1);

        dialog_msg_cw_append(id, val);
    }

    sqlite3_finalize(stmt);
}

void params_msg_cw_new(const char *val) {
    sqlite3_stmt    *stmt;
    int             rc;

    rc = sqlite3_prepare_v2(db, "INSERT INTO msg_cw (val) VALUES(?)", -1, &stmt, 0);

    if (rc != SQLITE_OK) {
        return;
    }

    sqlite3_bind_text(stmt, 1, val, strlen(val), 0);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    dialog_msg_cw_append(sqlite3_last_insert_rowid(db), val);
}

void params_msg_cw_edit(uint32_t id, const char *val) {
    sqlite3_stmt    *stmt;
    int             rc;

    rc = sqlite3_prepare_v2(db, "UPDATE msg_cw SET val = ? WHERE id = ?", -1, &stmt, 0);

    if (rc != SQLITE_OK) {
        return;
    }

    sqlite3_bind_text(stmt, 1, val, strlen(val), 0);
    sqlite3_bind_int(stmt, 2, id);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

void params_msg_cw_delete(uint32_t id) {
    sqlite3_stmt    *stmt;
    int             rc;

    rc = sqlite3_prepare_v2(db, "DELETE FROM msg_cw WHERE id = ?", -1, &stmt, 0);

    if (rc != SQLITE_OK) {
        return;
    }

    sqlite3_bind_int(stmt, 1, id);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

band_t * params_bands_find_all(uint64_t freq, int32_t half_width, uint16_t *count) {
    uint64_t    left = freq - half_width;
    uint64_t    right = freq + half_width;
    band_t      *res = NULL;
    uint16_t    n = 0;

    sqlite3_bind_int64(bands_find_all_stmt, 1, left);
    sqlite3_bind_int64(bands_find_all_stmt, 2, right);
    sqlite3_bind_int64(bands_find_all_stmt, 3, left);
    sqlite3_bind_int64(bands_find_all_stmt, 4, right);
    sqlite3_bind_int64(bands_find_all_stmt, 5, left);
    sqlite3_bind_int64(bands_find_all_stmt, 6, right);

    while (sqlite3_step(bands_find_all_stmt) != SQLITE_DONE) {
        n++;
        res = realloc(res, sizeof(band_t) * n);

        band_t *current = &res[n - 1];

        current->id = sqlite3_column_int(bands_find_all_stmt, 0);
        current->name = strdup(sqlite3_column_text(bands_find_all_stmt, 1));
        current->start_freq = sqlite3_column_int64(bands_find_all_stmt, 2);
        current->stop_freq = sqlite3_column_int64(bands_find_all_stmt, 3);
        current->type = sqlite3_column_int(bands_find_all_stmt, 4);
    }

    sqlite3_reset(bands_find_all_stmt);
    sqlite3_clear_bindings(bands_find_all_stmt);

    *count = n;
    return res;
}

bool params_bands_find(uint64_t freq) {
    if ((params.current_band.id != BAND_NOT_LOADED) && (params.current_band.start_freq <= freq) && (params.current_band.stop_freq >= freq)) {
        return false;
    }
    bool res = false;

    sqlite3_bind_int64(bands_find_stmt, 1, freq);

    if (sqlite3_step(bands_find_stmt) == SQLITE_ROW) {
        if (params.current_band.name)
            free(params.current_band.name);

        params.current_band.id = sqlite3_column_int(bands_find_stmt, 0);
        params.current_band.name = strdup(sqlite3_column_text(bands_find_stmt, 1));
        params.current_band.start_freq = sqlite3_column_int64(bands_find_stmt, 2);
        params.current_band.stop_freq = sqlite3_column_int64(bands_find_stmt, 3);
        params.current_band.type = sqlite3_column_int(bands_find_stmt, 4);

        res = true;
    }

    sqlite3_reset(bands_find_stmt);
    sqlite3_clear_bindings(bands_find_stmt);

    return res;
}

bool params_bands_find_next(uint64_t freq, bool up) {
    bool            res = false;
    sqlite3_stmt    *stmt;
    int             rc;

    if (up) {
        rc = sqlite3_prepare_v2(db, "SELECT id,name,start_freq,stop_freq,type FROM bands WHERE (? < start_freq AND type != 0) ORDER BY start_freq ASC", -1, &stmt, 0);
    } else {
        rc = sqlite3_prepare_v2(db, "SELECT id,name,start_freq,stop_freq,type FROM bands WHERE (? > stop_freq AND type != 0) ORDER BY start_freq DESC", -1, &stmt, 0);
    }

    if (rc != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_int64(stmt, 1, freq);

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        if (params.current_band.name)
            free(params.current_band.name);

        params.current_band.id = sqlite3_column_int(stmt, 0);
        params.current_band.name = strdup(sqlite3_column_text(stmt, 1));
        params.current_band.start_freq = sqlite3_column_int64(stmt, 2);
        params.current_band.stop_freq = sqlite3_column_int64(stmt, 3);
        params.current_band.type = sqlite3_column_int(stmt, 4);

        res = true;
    }

    sqlite3_finalize(stmt);

    return res;
}

void params_bool_set(params_bool_t *var, bool x) {
    params_lock();
    var->x = x;
    params_unlock(&var->dirty);

    if (var->voice) {
        voice_say_bool(var->voice, var->x);
    }
}

void params_uint8_set(params_uint8_t *var, uint8_t x) {
    params_lock();
    var->x = x;
    params_unlock(&var->dirty);

    if (var->voice) {
        voice_say_int(var->voice, var->x);
    }
}

void params_uint16_set(params_uint16_t *var, uint16_t x) {
    params_lock();
    var->x = x;
    params_unlock(&var->dirty);

    if (var->voice) {
        voice_say_int(var->voice, var->x);
    }
}

void params_float_set(params_float_t *var, float x) {
    params_lock();
    var->x = x;
    params_unlock(&var->dirty);

    if (var->voice) {
        voice_say_int(var->voice, var->x);
    }
}

void params_str_set(params_str_t *var, const char *x) {
    params_lock();
    strncpy(var->x, x, sizeof(var->x) - 1);
    params_unlock(&var->dirty);
}

uint8_t params_uint8_change(params_uint8_t *var, int16_t df) {
    if (df == 0) {
        return var->x;
    }

    int32_t x = var->x + df;

    if (x > var->max) x = var->max;
    if (x < var->min) x = var->min;

    params_uint8_set(var, x);

    return var->x;
}

inline char * params_charger_str_get(radio_charger_t val) {
    switch (val) {
        case RADIO_CHARGER_OFF:
            return "Off";
        case RADIO_CHARGER_ON:
            return "On";
        case RADIO_CHARGER_SHADOW:
            return "Shadow";
    }
}

inline char * params_mic_str_get(x6100_mic_sel_t val) {
    switch (val) {
        case x6100_mic_builtin:
            return "Built-In";
        case x6100_mic_handle:
            return "Handle";
        case x6100_mic_auto:
            return "Auto";
        default:
            return "";
    }
}

inline char * params_key_mode_str_get(x6100_key_mode_t val) {
    switch (val) {
        case x6100_key_manual:
            return "Manual";
        case x6100_key_auto_left:
            return "Auto-L";
        case x6100_key_auto_right:
            return "Auto-R";
    }
}

inline char * params_iambic_mode_str_ger(x6100_iambic_mode_t val) {
    switch (val) {
        case x6100_iambic_a:
            return "A";
        case x6100_iambic_b:
            return "B";
    }
}

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
    .brightness_normal      = 9,
    .brightness_idle        = 1,
    .brightness_timeout     = 10,
    .brightness_buttons     = BUTTONS_TEMPORARILY,

    .spectrum_beta          = { .x = 70, .min = 0, .max = 90, .name = "spectrum_beta"},
    .spectrum_filled        = { .x = true,  .name = "spectrum_filled"},
    .spectrum_peak          = { .x = true,  .name = "spectrum_peak"},
    .spectrum_peak_hold     = { .x = 5,  .min=1, .max=10, .name = "spectrum_peak_hold"},
    .spectrum_peak_speed    = { .x = 5,  .min=1, .max=30, .name = "spectrum_peak_speed"},
    .waterfall_smooth_scroll= { .x = false,  .name = "waterfall_smooth_scroll",  .voice = "Waterfall smooth scroll"},
    .waterfall_center_line  = { .x = true,  .name = "waterfall_center_line",    .voice = "Waterfall center line"},
    .waterfall_zoom         = { .x = true,  .name = "waterfall_zoom",           .voice = "Waterfall zoom"},
    .mag_freq               = { .x = true,  .name = "mag_freq",                 .voice = "Magnification of frequency" },
    .mag_info               = { .x = true,  .name = "mag_info",                 .voice = "Magnification of info" },
    .mag_alc                = { .x = true,  .name = "mag_alc",                  .voice = "Magnification of A L C" },
    .clock_view             = CLOCK_TIME_POWER,
    .clock_time_timeout     = 5,
    .clock_power_timeout    = 3,
    .clock_tx_timeout       = 1,

    .charger                = { .x = 1, .min = 0, .max = 2, .name = "charger"},
    .bias_drive             = 450,
    .bias_final             = 650,
    .line_in                = 10,
    .line_out               = 10,
    .spmode                 = { .x = false,             .name = "spmode",           .voice = "Speaker mode" },
    .freq_accel             = { .x = FREQ_ACCEL_LITE,   .name = "freq_accel",       .voice = "Frequency acceleration" },

    .vox                    = false,
    .vox_ag                 = 0,
    .vox_delay              = 100,
    .vox_gain               = 50,

    .cw_encoder_period      = 10,
    .voice_msg_period       = 10,

    .rtty_center            = 800,
    .rtty_shift             = 170,
    .rtty_rate              = 4545,
    .rtty_reverse           = false,
    .rtty_bits              = 5,
    .rtty_snr               = 3.0f,

    .ft8_tx_freq            = { .x = 1325,      .name = "ft8_tx_freq" },
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

    .spectrum_r             = { .x = 170, .min = 0, .max = 255, .name = "Spectrum R" },
    .spectrum_g             = { .x = 170, .min = 0, .max = 255, .name = "Spectrum G" },
    .spectrum_b             = { .x = 170, .min = 0, .max = 255, .name = "Spectrum B" },
    .meter_color            = { .x = METER_GRAY, .name="meter_color"},
    .swr_color              = { .x = SWR_GRAY,   .name="swr_color"},
};

static sqlite3_stmt     *write_mode_stmt;
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

        if (strcmp(name, "cw_encoder_period") == 0) {
            params.cw_encoder_period = i;
        } else if (strcmp(name, "voice_msg_period") == 0) {
            params.voice_msg_period = i;
        } else if (strcmp(name, "rtty_rate") == 0) {
            params.rtty_rate = i;
        } else if (strcmp(name, "rtty_shift") == 0) {
            params.rtty_shift = i;
        } else if (strcmp(name, "rtty_center") == 0) {
            params.rtty_center = i;
        } else if (strcmp(name, "rtty_reverse") == 0) {
            params.rtty_reverse = i;
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
        } else if (strcmp(name, "clock_view") == 0) {
            params.clock_view = i;
        } else if (strcmp(name, "clock_time_timeout") == 0) {
            params.clock_time_timeout = i;
        } else if (strcmp(name, "clock_power_timeout") == 0) {
            params.clock_power_timeout = i;
        } else if (strcmp(name, "clock_tx_timeout") == 0) {
            params.clock_tx_timeout = i;
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

        if (params_load_float(&params.play_gain_db_f, name, f)) continue;
        if (params_load_float(&params.rec_gain_db_f, name, f)) continue;

        if (params_load_uint8(&params.charger, name, i)) continue;
        if (params_load_bool(&params.mag_freq, name, i)) continue;
        if (params_load_bool(&params.mag_info, name, i)) continue;
        if (params_load_bool(&params.mag_alc, name, i)) continue;
        if (params_load_uint8(&params.spectrum_beta, name, i)) continue;
        if (params_load_uint8(&params.spectrum_peak_hold, name, i)) continue;
        if (params_load_uint8(&params.spectrum_peak_speed, name, i)) continue;
        if (params_load_bool(&params.spectrum_filled, name, i)) continue;
        if (params_load_bool(&params.spectrum_peak, name, i)) continue;
        if (params_load_bool(&params.waterfall_smooth_scroll, name, i)) continue;
        if (params_load_bool(&params.waterfall_center_line, name, i)) continue;
        if (params_load_bool(&params.waterfall_zoom, name, i)) continue;
        if (params_load_bool(&params.spmode, name, i)) continue;
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

        if (params_load_uint8(&params.spectrum_r, name, i)) continue;
        if (params_load_uint8(&params.spectrum_g, name, i)) continue;
        if (params_load_uint8(&params.spectrum_b, name, i)) continue;
        if (params_load_uint8(&params.meter_color, name, i)) continue;
        if (params_load_uint8(&params.swr_color, name, i)) continue;
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

    if (params.dirty.cw_encoder_period)     params_write_int("cw_encoder_period", params.cw_encoder_period, &params.dirty.cw_encoder_period);
    if (params.dirty.voice_msg_period)      params_write_int("voice_msg_period", params.voice_msg_period, &params.dirty.voice_msg_period);

    if (params.dirty.rtty_rate)             params_write_int("rtty_rate", params.rtty_rate, &params.dirty.rtty_rate);
    if (params.dirty.rtty_shift)            params_write_int("rtty_shift", params.rtty_shift, &params.dirty.rtty_shift);
    if (params.dirty.rtty_center)           params_write_int("rtty_center", params.rtty_center, &params.dirty.rtty_center);
    if (params.dirty.rtty_reverse)          params_write_int("rtty_reverse", params.rtty_reverse, &params.dirty.rtty_reverse);

    if (params.dirty.line_in)               params_write_int("line_in", params.line_in, &params.dirty.line_in);
    if (params.dirty.line_out)              params_write_int("line_out", params.line_out, &params.dirty.line_out);

    if (params.dirty.brightness_normal)     params_write_int("brightness_normal", params.brightness_normal, &params.dirty.brightness_normal);
    if (params.dirty.brightness_idle)       params_write_int("brightness_idle", params.brightness_idle, &params.dirty.brightness_idle);
    if (params.dirty.brightness_timeout)    params_write_int("brightness_timeout", params.brightness_timeout, &params.dirty.brightness_timeout);
    if (params.dirty.brightness_buttons)    params_write_int("brightness_buttons", params.brightness_buttons, &params.dirty.brightness_buttons);

    if (params.dirty.clock_view)            params_write_int("clock_view", params.clock_view, &params.dirty.clock_view);
    if (params.dirty.clock_time_timeout)    params_write_int("clock_time_timeout", params.clock_time_timeout, &params.dirty.clock_time_timeout);
    if (params.dirty.clock_power_timeout)   params_write_int("clock_power_timeout", params.clock_power_timeout, &params.dirty.clock_power_timeout);
    if (params.dirty.clock_tx_timeout)      params_write_int("clock_tx_timeout", params.clock_tx_timeout, &params.dirty.clock_tx_timeout);

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

    params_save_uint8(&params.charger);
    params_save_bool(&params.mag_freq);
    params_save_bool(&params.mag_info);
    params_save_bool(&params.mag_alc);
    params_save_uint8(&params.spectrum_beta);
    params_save_uint8(&params.spectrum_peak_hold);
    params_save_uint8(&params.spectrum_peak_speed);
    params_save_bool(&params.spectrum_filled);
    params_save_bool(&params.spectrum_peak);
    params_save_bool(&params.waterfall_smooth_scroll);
    params_save_bool(&params.waterfall_center_line);
    params_save_bool(&params.waterfall_zoom);
    params_save_bool(&params.spmode);
    params_save_float(&params.ft8_output_gain_offset);
    params_save_str(&params.ft8_cq_modifier);

    params_save_str(&params.qth);
    params_save_str(&params.callsign);
    params_save_bool(&params.wifi_enabled);
    params_save_uint8(&params.theme);

    params_save_uint8(&params.spectrum_r);
    params_save_uint8(&params.spectrum_g);
    params_save_uint8(&params.spectrum_b);
    params_save_uint8(&params.meter_color);
    params_save_uint8(&params.swr_color);

    sql_query_exec("COMMIT");
}

/* * */

static void * params_thread(void *arg) {
    while (true) {
        pthread_mutex_lock(&params_mux);
        if (params_ready_to_save()){
            params_save();
            // params_mode_save();
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

        rc = sqlite3_prepare_v2(db, "INSERT INTO mode_params(mode, name, val) VALUES(?, ?, ?)", -1, &write_mode_stmt, 0);

        if (rc != SQLITE_OK) {
            LV_LOG_ERROR("Prepare mode write");
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

    } else {
        LV_LOG_ERROR("Open params.db");
    }

    pthread_t thread;

    pthread_create(&thread, NULL, params_thread, NULL);
    pthread_detach(thread);
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

inline char *params_comp_str_get(uint8_t comp) {
    static char buf[8];
    if (comp == 1) {
        return "1:1 (Off)";
    }
    sprintf(buf, "%d:1", comp);
    return buf;
}

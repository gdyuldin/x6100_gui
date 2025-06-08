/**
 * Work with bands and band_params table on DB
 */
#include "band.private.h"
#include "params.private.h"

#include "cfg.private.h"

#include "transverter.h"

#include "../lvgl/lvgl.h"
#include "band.h"
#include <aether_radio/x6100_control/control.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static sqlite3      *db;
static sqlite3_stmt *insert_stmt;
static sqlite3_stmt *read_stmt;
static sqlite3_stmt *read_band_by_pk_stmt;
static sqlite3_stmt *read_band_by_freq_stmt;
static sqlite3_stmt *find_up_band_stmt;
static sqlite3_stmt *find_down_band_stmt;
static sqlite3_stmt *read_all_bands_stmt;

static pthread_mutex_t write_mutex             = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t read_mutex              = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t read_band_by_pk_mutex   = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t read_band_by_freq_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t find_up_down_band_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t read_all_bands_mutex    = PTHREAD_MUTEX_INITIALIZER;

static band_info_t _band_info_cache = {.id = BAND_UNDEFINED, .start_freq = 0, .stop_freq = 0};

// Gain for all bands, configured using previous version. Default value
static float common_gain;

cfg_band_t cfg_band;

static void init_db(sqlite3 *database);

static void on_fg_freq_change(Subject *subj, void *user_data);
static void on_bg_freq_change(Subject *subj, void *user_data);

static void on_ab_freq_change(Subject *subj, void *user_data);
static void on_ab_mode_change(Subject *subj, void *user_data);
static void on_ab_agc_change(Subject *subj, void *user_data);
static void on_ab_att_change(Subject *subj, void *user_data);
static void on_ab_pre_change(Subject *subj, void *user_data);

static void on_band_id_change(Subject *subj, void *user_data);
static void on_vfo_change(Subject *subj, void *user_data);
static void on_cur_mode_change(Subject *subj, void *user_data);
static void on_cur_agc_change(Subject *subj, void *user_data);
static void on_cur_att_change(Subject *subj, void *user_data);
static void on_cur_pre_change(Subject *subj, void *user_data);

static void fill_band_cfg_item(cfg_item_t *item, Subject * val, const char * db_name, int pk);
static void fill_band_cfg_item_float(cfg_item_t *item, Subject * val, float db_scale, const char * db_name, int pk);

void cfg_band_params_init(sqlite3 *database) {
    init_db(database);

    common_gain = subject_get_float(cfg.output_gain.val);

    x6100_mode_t default_mode;
    int32_t      band_id      = subject_get_int(cfg.band_id.val);
    band_info_t *band_info    = get_band_info_by_pk(band_id);
    uint32_t     default_freq = 14000000;
    if (band_info && (band_info->id != BAND_UNDEFINED)) {
        default_freq = (band_info->start_freq + band_info->stop_freq) / 2;
    } else if (default_freq > 10000000) {
        default_mode = x6100_mode_usb;
    } else {
        default_mode = x6100_mode_lsb;
    }

    cfg_cur.freq_shift = subject_create_int(0);

    cfg_cur.fg_freq = subject_create_int(default_freq);
    subject_add_observer(cfg_cur.fg_freq, on_fg_freq_change, NULL);
    cfg_cur.bg_freq = subject_create_int(default_freq);
    subject_add_observer(cfg_cur.bg_freq, on_bg_freq_change, NULL);

    cfg_cur.mode = subject_create_int(default_mode);
    subject_add_observer(cfg_cur.mode, on_cur_mode_change, NULL);
    cfg_cur.agc = subject_create_int(x6100_agc_auto);
    subject_add_observer(cfg_cur.agc, on_cur_agc_change, NULL);
    cfg_cur.att = subject_create_int(x6100_att_off);
    subject_add_observer(cfg_cur.att, on_cur_att_change, NULL);
    cfg_cur.pre = subject_create_int(x6100_pre_off);
    subject_add_observer(cfg_cur.pre, on_cur_pre_change, NULL);

    /* Fill band configuration */

    fill_band_cfg_item(&cfg_band.vfo_a.freq, subject_create_int(default_freq), "vfoa_freq", band_id);
    fill_band_cfg_item(&cfg_band.vfo_a.mode, subject_create_int(default_mode), "vfoa_mode", band_id);
    fill_band_cfg_item(&cfg_band.vfo_a.agc, subject_create_int(x6100_agc_auto), "vfoa_agc", band_id);
    fill_band_cfg_item(&cfg_band.vfo_a.att, subject_create_int(x6100_att_off), "vfoa_att", band_id);
    fill_band_cfg_item(&cfg_band.vfo_a.pre, subject_create_int(x6100_pre_off), "vfoa_pre", band_id);

    fill_band_cfg_item(&cfg_band.vfo_b.freq, subject_create_int(default_freq), "vfob_freq", band_id);
    fill_band_cfg_item(&cfg_band.vfo_b.mode, subject_create_int(default_mode), "vfob_mode", band_id);
    fill_band_cfg_item(&cfg_band.vfo_b.agc, subject_create_int(x6100_agc_auto), "vfob_agc", band_id);
    fill_band_cfg_item(&cfg_band.vfo_b.att, subject_create_int(x6100_att_off), "vfob_att", band_id);
    fill_band_cfg_item(&cfg_band.vfo_b.pre, subject_create_int(x6100_pre_off), "vfob_pre", band_id);

    fill_band_cfg_item(&cfg_band.grid.min, subject_create_int(-121), "grid_min", band_id);
    fill_band_cfg_item(&cfg_band.grid.max, subject_create_int(-73), "grid_max", band_id);

    fill_band_cfg_item(&cfg_band.vfo, subject_create_int(X6100_VFO_A), "vfo", band_id);
    fill_band_cfg_item(&cfg_band.split, subject_create_int(false), "split", band_id);
    fill_band_cfg_item(&cfg_band.rfg, subject_create_int(100), "rfg", band_id);
    fill_band_cfg_item_float(&cfg_band.output_gain, subject_create_float(common_gain), 0.2f, "output_gain", band_id);

    subject_add_observer(cfg_band.vfo_a.freq.val, on_ab_freq_change, &cfg_band.vfo_a.freq);
    subject_add_observer(cfg_band.vfo_b.freq.val, on_ab_freq_change, &cfg_band.vfo_b.freq);

    subject_add_observer(cfg_band.vfo_a.mode.val, on_ab_mode_change, &cfg_band.vfo_a.mode);
    subject_add_observer(cfg_band.vfo_b.mode.val, on_ab_mode_change, &cfg_band.vfo_b.mode);

    subject_add_observer(cfg_band.vfo_a.agc.val, on_ab_agc_change, &cfg_band.vfo_a.agc);
    subject_add_observer(cfg_band.vfo_b.agc.val, on_ab_agc_change, &cfg_band.vfo_b.agc);

    subject_add_observer(cfg_band.vfo_a.att.val, on_ab_att_change, &cfg_band.vfo_a.att);
    subject_add_observer(cfg_band.vfo_b.att.val, on_ab_att_change, &cfg_band.vfo_b.att);

    subject_add_observer(cfg_band.vfo_a.pre.val, on_ab_pre_change, &cfg_band.vfo_a.pre);
    subject_add_observer(cfg_band.vfo_b.pre.val, on_ab_pre_change, &cfg_band.vfo_b.pre);

    subject_add_observer(cfg_band.vfo.val, on_vfo_change, NULL);
    subject_add_observer(cfg.band_id.val, on_band_id_change, NULL);

    /* Load values from table */
    cfg_item_t *cfg_arr  = (cfg_item_t *)&cfg_band;
    uint32_t    cfg_size = sizeof(cfg_band) / sizeof(*cfg_arr);
    init_items(cfg_arr, cfg_size, cfg_band_params_load_item, cfg_band_params_save_item);
    load_items_from_db(cfg_arr, cfg_size);
}


void cfg_band_set_freq_for_vfo(x6100_vfo_t vfo, int32_t freq) {
    band_info_t *band_info = get_band_info_by_freq(freq);
    if (band_info == NULL) {
        LV_LOG_ERROR("Unknown band, can't set freq %i for vfo %u", freq, vfo);
        return;
    }
    int32_t            new_band_id = band_info->id;
    struct vfo_params *target;
    if (vfo == X6100_VFO_A) {
        target = &cfg_band.vfo_a;
    } else {
        target = &cfg_band.vfo_b;
    }
    if (new_band_id != target->freq.pk) {
        cfg_band.vfo.pk = new_band_id;
        save_item_to_db(&cfg_band.vfo, true);
        // save old freq and update band_id
        cfg_band_params_save_all();
        cfg_band_params_change_pk(new_band_id);
        if (new_band_id != BAND_UNDEFINED) {
            cfg_band_params_load_all();
        }
        subject_set_int(cfg.band_id.val, new_band_id);
    }
    subject_set_int(target->freq.val, freq);
}

void cfg_band_vfo_copy() {
    struct vfo_params *src, *dst;
    if (subject_get_int(cfg_band.vfo.val) == X6100_VFO_A) {
        src = &cfg_band.vfo_a;
        dst = &cfg_band.vfo_b;
    } else {
        src = &cfg_band.vfo_b;
        dst = &cfg_band.vfo_a;
    }
    subject_set_int(dst->freq.val, subject_get_int(src->freq.val));
    subject_set_int(dst->mode.val, subject_get_int(src->mode.val));
    subject_set_int(dst->agc.val, subject_get_int(src->agc.val));
    subject_set_int(dst->att.val, subject_get_int(src->att.val));
    subject_set_int(dst->pre.val, subject_get_int(src->pre.val));
}

void cfg_band_load_next(bool up) {
    int32_t      cur_freq  = subject_get_int(cfg_cur.fg_freq);
    int32_t      cur_id    = cfg_band.vfo.pk;
    band_info_t *band_info = get_band_info_next(cur_freq, up, cur_id);
    if (band_info != NULL) {
        subject_set_int(cfg.band_id.val, band_info->id);
    }
}

const char *cfg_band_label_get() {
    if (_band_info_cache.id != BAND_UNDEFINED) {
        return _band_info_cache.name;
    } else {
        return "";
    }
}

band_info_t *get_band_info_by_pk(int32_t band_id) {
    int rc;
    if (_band_info_cache.id == band_id) {
        return &_band_info_cache;
    }
    LV_LOG_USER("Loading band info for id: %i", band_id);
    sqlite3_stmt *stmt = read_band_by_pk_stmt;
    pthread_mutex_lock(&read_band_by_pk_mutex);
    rc = sqlite3_bind_int(stmt, sqlite3_bind_parameter_index(stmt, ":id"), band_id);
    if (rc != SQLITE_OK) {
        LV_LOG_ERROR("Failed to bind bands_id %i: %s", band_id, sqlite3_errmsg(db));
        pthread_mutex_unlock(&read_band_by_pk_mutex);
        return NULL;
    }
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        if (_band_info_cache.name) {
            free(_band_info_cache.name);
            _band_info_cache.name = NULL;
        }
        _band_info_cache.id         = band_id;
        _band_info_cache.name       = strdup(sqlite3_column_text(stmt, 0));
        _band_info_cache.start_freq = sqlite3_column_int(stmt, 1);
        _band_info_cache.stop_freq  = sqlite3_column_int(stmt, 2);
        _band_info_cache.active     = sqlite3_column_int(stmt, 3);
        rc                          = 0;
    } else {
        LV_LOG_USER("No info for band with id: %i", band_id);
        rc = -1;
    }
    sqlite3_reset(stmt);
    sqlite3_clear_bindings(stmt);
    pthread_mutex_unlock(&read_band_by_pk_mutex);
    if (rc == 0)
        return &_band_info_cache;
    else
        return NULL;
}

band_info_t *get_band_info_by_freq(uint32_t freq) {
    if ((freq > _band_info_cache.start_freq) && (freq <= _band_info_cache.stop_freq))
        return &_band_info_cache;
    LV_LOG_USER("Loading band info for freq: %u", freq);
    int           rc;
    sqlite3_stmt *stmt = read_band_by_freq_stmt;
    pthread_mutex_lock(&read_band_by_freq_mutex);
    rc = sqlite3_bind_int(stmt, sqlite3_bind_parameter_index(stmt, ":freq"), freq);
    if (rc != SQLITE_OK) {
        LV_LOG_ERROR("Failed to bind freq %lu to read_band_by_freq_stmt: %s", freq, sqlite3_errmsg(db));
        pthread_mutex_unlock(&read_band_by_freq_mutex);
        return NULL;
    }
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        if (_band_info_cache.name) {
            free(_band_info_cache.name);
            _band_info_cache.name = NULL;
        }
        if (sqlite3_column_type(stmt, 0) != SQLITE_NULL) {
            _band_info_cache.id     = sqlite3_column_int(stmt, 0);
            _band_info_cache.name   = strdup(sqlite3_column_text(stmt, 1));
            _band_info_cache.active = true;
        } else {
            _band_info_cache.id     = BAND_UNDEFINED;
            _band_info_cache.active = false;
        }
        _band_info_cache.start_freq = sqlite3_column_int(stmt, 2);
        if (sqlite3_column_type(stmt, 3) == SQLITE_NULL) {
            _band_info_cache.stop_freq = 0LU - 1;
        } else {
            _band_info_cache.stop_freq = sqlite3_column_int(stmt, 3);
        }
        rc = 0;
    } else {
        LV_LOG_WARN("No band info for freq: %lu", freq);
        rc = -1;
    }
    sqlite3_reset(stmt);
    sqlite3_clear_bindings(stmt);
    pthread_mutex_unlock(&read_band_by_freq_mutex);
    if (rc == 0) {
        return &_band_info_cache;
    } else {
        return NULL;
    }
}

band_info_t *get_band_info_next(uint32_t freq, bool up, int32_t cur_id) {
    int           rc;
    sqlite3_stmt *stmt;
    pthread_mutex_lock(&find_up_down_band_mutex);
    if (up) {
        stmt = find_up_band_stmt;
    } else {
        stmt = find_down_band_stmt;
    }
    rc = sqlite3_bind_int(stmt, sqlite3_bind_parameter_index(stmt, ":freq"), freq);
    if (rc != SQLITE_OK) {
        LV_LOG_ERROR("Failed to bind freq %lu to find up/down stmt: %s", freq, sqlite3_errmsg(db));
        pthread_mutex_unlock(&find_up_down_band_mutex);
        return NULL;
    }
    rc = sqlite3_bind_int(stmt, sqlite3_bind_parameter_index(stmt, ":id"), cur_id);
    if (rc != SQLITE_OK) {
        LV_LOG_ERROR("Failed to current band id %i to find up/down stmt: %s", cur_id, sqlite3_errmsg(db));
        pthread_mutex_unlock(&find_up_down_band_mutex);
        return NULL;
    }
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        if (_band_info_cache.name) {
            free(_band_info_cache.name);
            _band_info_cache.name = NULL;
        }
        _band_info_cache.id         = sqlite3_column_int(stmt, 0);
        _band_info_cache.name       = strdup(sqlite3_column_text(stmt, 1));
        _band_info_cache.active     = true;
        _band_info_cache.start_freq = sqlite3_column_int(stmt, 2);
        _band_info_cache.stop_freq  = sqlite3_column_int(stmt, 3);
        rc                          = 0;
    } else {
        LV_LOG_INFO("No next band info for freq: %lu, cur_id: %i and direction: %u", freq, cur_id, up);
        rc = -1;
    }
    sqlite3_reset(stmt);
    sqlite3_clear_bindings(stmt);
    pthread_mutex_unlock(&find_up_down_band_mutex);
    if (rc == 0) {
        return &_band_info_cache;
    } else {
        return NULL;
    }
}

uint32_t cfg_band_read_all_bands(band_info_t **results, int32_t *cap) {
    int           rc;
    sqlite3_stmt *stmt = read_all_bands_stmt;
    uint32_t      i    = 0;
    while (1) {
        rc = sqlite3_step(stmt);

        if (rc == SQLITE_ROW) {
            if (i >= *cap) {
                *cap *= 2;
                *results = realloc(*results, sizeof(**results) * *cap);
            }
            (*results)[i].id         = sqlite3_column_int(stmt, 0);
            (*results)[i].name       = strdup(sqlite3_column_text(stmt, 1));
            (*results)[i].start_freq = sqlite3_column_int(stmt, 2);
            (*results)[i].stop_freq  = sqlite3_column_int(stmt, 3);
            (*results)[i].active     = sqlite3_column_int(stmt, 4);
            i++;
        } else if (rc == SQLITE_DONE) {
            break;
        } else {
            LV_LOG_ERROR("Error while reading bands rows: %s", sqlite3_errmsg(db));
            break;
        }
    }
    sqlite3_reset(stmt);
    return i;
}

void cfg_band_params_save_all() {
    cfg_item_t *cfg_arr      = (cfg_item_t *)&cfg_band;
    int32_t     cfg_arr_size = sizeof(cfg_band) / sizeof(*cfg_arr);

    LV_LOG_USER("Save band params for pk=%i", cfg_arr[0].pk);
    for (size_t i = 0; i < cfg_arr_size; i++) {
        save_item_to_db(&cfg_arr[i], false);
    }
}

void cfg_band_params_change_pk(int32_t pk) {
    cfg_item_t *cfg_arr      = (cfg_item_t *)&cfg_band;
    int32_t     cfg_arr_size = sizeof(cfg_band) / sizeof(*cfg_arr);

    LV_LOG_USER("Set pk=%i for band params", pk);
    for (size_t i = 0; i < cfg_arr_size; i++) {
        cfg_arr[i].pk = pk;
    }
}

void cfg_band_params_load_all() {
    cfg_item_t *cfg_arr      = (cfg_item_t *)&cfg_band;
    int32_t     cfg_arr_size = sizeof(cfg_band) / sizeof(*cfg_arr);
    LV_LOG_USER("Load band params for pk=%i", cfg_arr[0].pk);
    load_items_from_db(cfg_arr, cfg_arr_size);
}

int cfg_band_params_load_item(cfg_item_t *item) {
    enum data_type dtype = subject_get_dtype(item->val);
    if ((dtype != DTYPE_INT) && (dtype != DTYPE_FLOAT)) {
        LV_LOG_WARN("Unknown item %s dtype: %u, can't load", item->db_name, dtype);
        return -1;
    }

    band_info_t *band_info = get_band_info_by_pk(item->pk);
    if (!band_info) {
        LV_LOG_ERROR("Can't load band info for pk: %i", item->pk);
        return -1;
    }
    sqlite3_stmt *stmt = read_stmt;
    pthread_mutex_lock(&read_mutex);
    int     rc;
    int32_t int_val;
    float float_val;
    rc = sqlite3_bind_text(stmt, sqlite3_bind_parameter_index(stmt, ":name"), item->db_name, strlen(item->db_name), 0);
    if (rc != SQLITE_OK) {
        LV_LOG_ERROR("Failed to bind name %s: %s", item->db_name, sqlite3_errmsg(db));
        pthread_mutex_unlock(&read_mutex);
        return rc;
    }
    rc = sqlite3_bind_int(stmt, sqlite3_bind_parameter_index(stmt, ":id"), item->pk);
    if (rc != SQLITE_OK) {
        LV_LOG_ERROR("Failed to bind bands_id %i: %s", item->pk, sqlite3_errmsg(db));
        pthread_mutex_unlock(&read_mutex);
        return rc;
    }

    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        if (dtype == DTYPE_INT) {
            int_val = sqlite3_column_int(stmt, 0);
            LV_LOG_USER("Loaded %s=%i (pk=%i)", item->db_name, int_val, item->pk);
        } else if (dtype == DTYPE_FLOAT) {
            float_val = cfg_float_val_from_db(item, read_stmt, 0);
        }
    }
    sqlite3_reset(stmt);
    sqlite3_clear_bindings(stmt);
    pthread_mutex_unlock(&read_mutex);

    if (rc == SQLITE_ROW) {
        rc = 0;
    } else {
        // Not found, use default values
        if (strcmp(item->db_name, "vfob_freq") == 0) {
            LV_LOG_USER("Copy vfoa freq to vfob");
            int_val = subject_get_int(cfg_band.vfo_a.freq.val);
            rc = 0;
        } else if (strcmp(item->db_name, "output_gain") == 0) {
            float_val = common_gain;
            LV_LOG_USER("Use general output gain: %f for band", common_gain);
            rc = 0;
        } else {
            LV_LOG_WARN("No results for load from band_params with name: %s and bands_id: %i", item->db_name, item->pk);
            // Save with default value
            cfg_band_params_save_item(item);
            rc = -1;
        }
    }
    // Update item subject
    if (rc == 0) {
        // Check for loaded frequency
        if ((strcmp(item->db_name, "vfoa_freq") == 0) || (strcmp(item->db_name, "vfob_freq") == 0)) {
            if ((int_val >= band_info->start_freq) && (int_val <= band_info->stop_freq) ||
                (band_info->id == BAND_UNDEFINED)) {
                subject_set_int(item->val, int_val);
            } else {
                LV_LOG_USER("Freq %lu for %s (band_id: %i) outside boundaries, db value ignored", int_val,
                            item->db_name, item->pk);
                subject_set_int(item->val, band_info->start_freq);
            }
        } else if (dtype == DTYPE_FLOAT) {
            subject_set_float(item->val, float_val);
        } else {
            subject_set_int(item->val, int_val);
        }
    }
    return rc;
}

int cfg_band_params_save_item(cfg_item_t *item) {
    int32_t      start_freq, stop_freq, band_id;
    band_info_t *band_info = get_band_info_by_pk(item->pk);
    if (!band_info) {
        band_id = BAND_UNDEFINED;
    } else {
        start_freq = band_info->start_freq;
        stop_freq  = band_info->stop_freq;
        band_id    = band_info->id;
    }
    sqlite3_stmt *stmt = insert_stmt;
    pthread_mutex_lock(&write_mutex);
    int     rc;
    int32_t int_val;
    float   float_val;

    rc = sqlite3_bind_text(stmt, sqlite3_bind_parameter_index(stmt, ":name"), item->db_name, strlen(item->db_name), 0);
    if (rc != SQLITE_OK) {
        LV_LOG_ERROR("Failed to bind name %s: %s", item->db_name, sqlite3_errmsg(db));
        pthread_mutex_unlock(&write_mutex);
        return rc;
    }
    rc = sqlite3_bind_int(stmt, sqlite3_bind_parameter_index(stmt, ":id"), item->pk);
    if (rc != SQLITE_OK) {
        LV_LOG_ERROR("Failed to bind bands_id %i: %s", item->pk, sqlite3_errmsg(db));
        pthread_mutex_unlock(&write_mutex);
        return rc;
    }
    int val_index = sqlite3_bind_parameter_index(stmt, ":val");
    enum data_type dtype = subject_get_dtype(item->val);
    switch (dtype) {
        case DTYPE_INT:
            int_val = subject_get_int(item->val);
            // Check that freq match band
            if ((strcmp(item->db_name, "vfoa_freq") == 0) || (strcmp(item->db_name, "vfob_freq") == 0)) {
                if ((band_id == BAND_UNDEFINED) || ((int_val >= start_freq) && (int_val <= stop_freq))) {
                    rc = sqlite3_bind_int(stmt, val_index, int_val);
                } else {
                    LV_LOG_USER("Freq %lu for %s (band_id: %u) outside boundaries, will not save", int_val,
                                item->db_name, item->pk);
                    rc = -1;
                }
            } else {
                rc = sqlite3_bind_int(stmt, val_index, int_val);
            }
            break;
        case DTYPE_FLOAT:
            rc = cfg_float_val_to_db(item, insert_stmt, val_index, &float_val);
            break;
        default:
            LV_LOG_WARN("Unknown item %s dtype: %u, will not save", item->db_name, dtype);
            sqlite3_reset(stmt);
            sqlite3_clear_bindings(stmt);
            pthread_mutex_unlock(&write_mutex);
            return -1;
            break;
    }
    if (rc != SQLITE_OK) {
        LV_LOG_ERROR("Failed to bind val for name %s: %s", item->db_name, sqlite3_errmsg(db));
        pthread_mutex_unlock(&write_mutex);
        return rc;
    }
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        LV_LOG_ERROR("Failed save item %s: %s", item->db_name, sqlite3_errmsg(db));
    } else {
        switch (dtype) {
            case DTYPE_INT:
                LV_LOG_USER("Saved %s=%i (pk=%i)", item->db_name, int_val, item->pk);
                break;
            case DTYPE_FLOAT:
                LV_LOG_USER("Saved %s=%f (pk=%i)", item->db_name, float_val, item->pk);
                break;
            default:
                break;
        }
        rc = 0;
    }
    sqlite3_reset(stmt);
    sqlite3_clear_bindings(stmt);
    pthread_mutex_unlock(&write_mutex);
    return rc;
}

static void init_db(sqlite3 *database) {
    db = database;
    int rc;
    rc = sqlite3_prepare_v2(db, "SELECT val FROM band_params WHERE bands_id = :id AND name = :name", -1, &read_stmt, 0);
    if (rc != SQLITE_OK) {
        LV_LOG_ERROR("Failed prepare read statement: %s", sqlite3_errmsg(db));
        exit(1);
    }
    rc = sqlite3_prepare_v2(db, "INSERT OR REPLACE INTO band_params(bands_id, name, val) VALUES(:id, :name, :val)", -1,
                            &insert_stmt, 0);
    if (rc != SQLITE_OK) {
        LV_LOG_ERROR("Failed prepare write statement: %s", sqlite3_errmsg(db));
        exit(1);
    }
    rc = sqlite3_prepare_v2(db, "SELECT name, start_freq, stop_freq, type FROM bands WHERE id = :id", -1,
                            &read_band_by_pk_stmt, 0);
    if (rc != SQLITE_OK) {
        LV_LOG_ERROR("Failed prepare read_band_by_pk_stmt statement: %s", sqlite3_errmsg(db));
        exit(1);
    }
    rc = sqlite3_prepare_v2(
        db,
        "SELECT id, name, start_freq, stop_freq FROM bands WHERE "
        "   (:freq >= start_freq) AND (:freq <= stop_freq) AND (type = 1) "
        "UNION SELECT * FROM ("
        "   SELECT NULL, NULL, a.stop_freq, b.start_freq FROM ("
        "       SELECT stop_freq FROM bands WHERE :freq > stop_freq AND type = 1 ORDER BY stop_freq DESC LIMIT 1"
        "   ) AS a FULL OUTER JOIN ("
        "       SELECT start_freq FROM bands WHERE :freq < start_freq AND type = 1 ORDER BY start_freq LIMIT 1"
        "   ) AS b"
        ") ORDER BY id DESC NULLS LAST LIMIT 1",
        -1, &read_band_by_freq_stmt, 0);
    if (rc != SQLITE_OK) {
        LV_LOG_ERROR("Failed prepare read_band_by_freq_stmt statement: %s", sqlite3_errmsg(db));
        exit(1);
    }
    rc = sqlite3_prepare_v2(db,
                            "SELECT id, name, start_freq, stop_freq, type FROM bands "
                            "WHERE :freq <= start_freq AND id != :id AND type = 1 ORDER BY start_freq LIMIT 1",
                            -1, &find_up_band_stmt, 0);
    if (rc != SQLITE_OK) {
        LV_LOG_ERROR("Failed prepare find_up_band_stmt statement: %s", sqlite3_errmsg(db));
        exit(1);
    }
    rc = sqlite3_prepare_v2(db,
                            "SELECT id, name, start_freq, stop_freq, type FROM bands "
                            "WHERE :freq >= stop_freq AND id != :id AND type = 1 ORDER BY start_freq DESC LIMIT 1",
                            -1, &find_down_band_stmt, 0);
    if (rc != SQLITE_OK) {
        LV_LOG_ERROR("Failed prepare find_down_band_stmt statement: %s", sqlite3_errmsg(db));
        exit(1);
    }
    rc = sqlite3_prepare_v2(db, "SELECT id, name, start_freq, stop_freq, type FROM bands", -1, &read_all_bands_stmt, 0);
    if (rc != SQLITE_OK) {
        LV_LOG_ERROR("Failed prepare read_all_bands statement: %s", sqlite3_errmsg(db));
        exit(1);
    }
}

static void on_fg_freq_change(Subject *subj, void *user_data) {
    // Copy freq to foreground vfo
    int32_t     freq = subject_get_int(subj);
    x6100_vfo_t vfo  = subject_get_int(cfg_band.vfo.val);
    cfg_band_set_freq_for_vfo(vfo, freq);
    // Update freq shift
    subject_set_int(cfg_cur.freq_shift, cfg_transverter_get_shift(freq));
}

static void on_bg_freq_change(Subject *subj, void *user_data) {
    // Copy freq to background vfo
    int32_t     freq = subject_get_int(subj);
    x6100_vfo_t vfo  = subject_get_int(cfg_band.vfo.val);
    if (vfo == X6100_VFO_A)
        vfo = X6100_VFO_B;
    else
        vfo = X6100_VFO_A;
    cfg_band_set_freq_for_vfo(vfo, freq);
}

/**
 * On changing freq
 */
static void on_ab_freq_change(Subject *subj, void *user_data) {
    cfg_item_t *cfg_band_arr;
    cfg_band_arr           = (cfg_item_t *)&cfg_band;
    uint32_t cfg_band_size = sizeof(cfg_band) / sizeof(cfg_item_t);

    cfg_item_t *item  = (cfg_item_t *)user_data;
    uint32_t    freq  = subject_get_int(subj);
    x6100_vfo_t vfo   = subject_get_int(cfg_band.vfo.val);
    bool        is_fg = ((vfo == X6100_VFO_A) && (strcmp(item->db_name, "vfoa_freq") == 0)) ||
                 ((vfo == X6100_VFO_B) && (strcmp(item->db_name, "vfob_freq")) == 0);

    if (is_fg) {
        // Set freq to current
        subject_set_int(cfg_cur.fg_freq, freq);
    } else {
        subject_set_int(cfg_cur.bg_freq, freq);
    }
}

/**
 * On changing mode
 */
static void on_ab_mode_change(Subject *subj, void *user_data) {
    cfg_item_t  *item      = (cfg_item_t *)user_data;
    x6100_mode_t mode      = subject_get_int(subj);
    x6100_vfo_t  vfo       = subject_get_int(cfg_band.vfo.val);
    bool         is_active = ((vfo == X6100_VFO_A) && (strcmp(item->db_name, "vfoa_mode") == 0)) ||
                     ((vfo == X6100_VFO_B) && (strcmp(item->db_name, "vfob_mode")) == 0);

    if (is_active) {
        // Set mode to current
        subject_set_int(cfg_cur.mode, mode);
    }
}

static void on_ab_agc_change(Subject *subj, void *user_data) {
    cfg_item_t *item      = (cfg_item_t *)user_data;
    x6100_agc_t agc       = subject_get_int(subj);
    x6100_vfo_t vfo       = subject_get_int(cfg_band.vfo.val);
    bool        is_active = ((vfo == X6100_VFO_A) && (strcmp(item->db_name, "vfoa_agc") == 0)) ||
                     ((vfo == X6100_VFO_B) && (strcmp(item->db_name, "vfob_agc")) == 0);

    if (is_active) {
        // Set agc to current
        subject_set_int(cfg_cur.agc, agc);
    }
}

static void on_ab_att_change(Subject *subj, void *user_data) {
    cfg_item_t *item      = (cfg_item_t *)user_data;
    x6100_att_t att       = subject_get_int(subj);
    x6100_vfo_t vfo       = subject_get_int(cfg_band.vfo.val);
    bool        is_active = ((vfo == X6100_VFO_A) && (strcmp(item->db_name, "vfoa_att") == 0)) ||
                     ((vfo == X6100_VFO_B) && (strcmp(item->db_name, "vfob_att")) == 0);

    if (is_active) {
        // Set att to current
        subject_set_int(cfg_cur.att, att);
    }
}

static void on_ab_pre_change(Subject *subj, void *user_data) {
    cfg_item_t *item      = (cfg_item_t *)user_data;
    x6100_pre_t pre       = subject_get_int(subj);
    x6100_vfo_t vfo       = subject_get_int(cfg_band.vfo.val);
    bool        is_active = ((vfo == X6100_VFO_A) && (strcmp(item->db_name, "vfoa_pre") == 0)) ||
                     ((vfo == X6100_VFO_B) && (strcmp(item->db_name, "vfob_pre")) == 0);

    if (is_active) {
        // Set pre to current
        subject_set_int(cfg_cur.pre, pre);
    }
}

/**
 * Change freq/mode on changing band
 */
static void on_band_id_change(Subject *subj, void *user_data) {
    int32_t new_band_id = subject_get_int(subj);
    if (new_band_id != cfg_band.vfo.pk) {
        cfg_band_params_save_all();
        cfg_band_params_change_pk(new_band_id);
        cfg_band_params_load_all();
    }
}

static void on_vfo_change(Subject *subj, void *user_data) {
    Subject *fg_freq_src, *bg_freq_src, *mode_src, *agc_src, *pre_src, *att_src;
    if (subject_get_int(subj) == X6100_VFO_A) {
        fg_freq_src = cfg_band.vfo_a.freq.val;
        bg_freq_src = cfg_band.vfo_b.freq.val;
        mode_src    = cfg_band.vfo_a.mode.val;
        agc_src    = cfg_band.vfo_a.agc.val;
        pre_src    = cfg_band.vfo_a.pre.val;
        att_src    = cfg_band.vfo_a.att.val;
    } else {
        fg_freq_src = cfg_band.vfo_b.freq.val;
        bg_freq_src = cfg_band.vfo_a.freq.val;
        mode_src    = cfg_band.vfo_b.mode.val;
        agc_src    = cfg_band.vfo_b.agc.val;
        pre_src    = cfg_band.vfo_b.pre.val;
        att_src    = cfg_band.vfo_b.att.val;
    }
    subject_set_int(cfg_cur.fg_freq, subject_get_int(fg_freq_src));
    subject_set_int(cfg_cur.bg_freq, subject_get_int(bg_freq_src));
    subject_set_int(cfg_cur.mode, subject_get_int(mode_src));
    subject_set_int(cfg_cur.agc, subject_get_int(agc_src));
    subject_set_int(cfg_cur.pre, subject_get_int(pre_src));
    subject_set_int(cfg_cur.att, subject_get_int(att_src));
}

static void on_cur_mode_change(Subject *subj, void *user_data) {
    // Copy mode to active vfo
    x6100_mode_t new_mode = subject_get_int(subj);
    Subject *    target_subj;
    if (subject_get_int(cfg_band.vfo.val) == X6100_VFO_A) {
        target_subj = cfg_band.vfo_a.mode.val;
    } else {
        target_subj = cfg_band.vfo_b.mode.val;
    }
    subject_set_int(target_subj, new_mode);
}

static void on_cur_agc_change(Subject *subj, void *user_data) {
    // Copy agc to active vfo
    x6100_agc_t new_agc = subject_get_int(subj);
    Subject *   target_subj;
    if (subject_get_int(cfg_band.vfo.val) == X6100_VFO_A) {
        target_subj = cfg_band.vfo_a.agc.val;
    } else {
        target_subj = cfg_band.vfo_b.agc.val;
    }
    subject_set_int(target_subj, new_agc);
}

static void on_cur_att_change(Subject *subj, void *user_data) {
    // Copy att to active vfo
    x6100_att_t new_att = subject_get_int(subj);
    Subject *   target_subj;
    if (subject_get_int(cfg_band.vfo.val) == X6100_VFO_A) {
        target_subj = cfg_band.vfo_a.att.val;
    } else {
        target_subj = cfg_band.vfo_b.att.val;
    }
    subject_set_int(target_subj, new_att);
}

static void on_cur_pre_change(Subject *subj, void *user_data) {
    // Copy pre to active vfo
    x6100_pre_t new_pre = subject_get_int(subj);
    Subject *   target_subj;
    if (subject_get_int(cfg_band.vfo.val) == X6100_VFO_A) {
        target_subj = cfg_band.vfo_a.pre.val;
    } else {
        target_subj = cfg_band.vfo_b.pre.val;
    }
    subject_set_int(target_subj, new_pre);
}

static void fill_band_cfg_item(cfg_item_t *item, Subject * val, const char * db_name, int pk) {
    fill_cfg_item(item, val, db_name);
    item->pk = pk;
}

static void fill_band_cfg_item_float(cfg_item_t *item, Subject * val, float db_scale, const char * db_name, int pk) {
    fill_cfg_item_float(item, val, db_scale, db_name);
    item->pk = pk;
}

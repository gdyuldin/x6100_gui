#include "mode.private.h"

#include "cfg.private.h"
#include "band.private.h"

#include "../lvgl/lvgl.h"
#include "mode.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static sqlite3        *db;
static sqlite3_stmt   *insert_stmt;
static sqlite3_stmt   *read_stmt;
static pthread_mutex_t write_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t read_mutex  = PTHREAD_MUTEX_INITIALIZER;

cfg_mode_t cfg_mode;

static const uint16_t freq_steps[] = {10, 100, 500, 1000, 5000};
static const size_t   freq_steps_n = sizeof(freq_steps) / sizeof(freq_steps[0]);

static void init_db(sqlite3 *db);

static void on_cur_mode_change(Subject *subj, void *user_data);

static void on_cur_filter_low_change(Subject *subj, void *user_data);
static void on_cur_filter_high_change(Subject *subj, void *user_data);
static void on_cur_filter_bw_change(Subject *subj, void *user_data);
static void on_cur_freq_step_change(Subject *subj, void *user_data);
static void on_cur_zoom_change(Subject *subj, void *user_data);

static void update_cur_low_filter(Subject *subj, void *user_data);
static void update_cur_high_filter(Subject *subj, void *user_data);
static void on_freq_step_change(Subject *subj, void *user_data);
static void on_zoom_change(Subject *subj, void *user_data);

static void update_real_filters(Subject *subj, void *user_data);
static void update_lo_offset(Subject *subj, void *user_data);

static void fill_mode_cfg_item_int(cfg_item_t *item, Subject * val, const char * db_name, int pk);

void cfg_mode_params_init(sqlite3 *database) {
    init_db(database);

    /* Fill mode configuration */
    x6100_mode_t mode    = subject_get_int(cfg_cur.mode);
    db_mode_t    db_mode = xmode_2_db(mode);
    uint32_t     low, high, step, zoom, cur_low, cur_high;
    mode_default_values(db_mode, &low, &high, &step, &zoom);
    if (db_mode == x6100_mode_cw) {
        cur_low  = subject_get_int(cfg.key_tone.val) - high / 2;
        cur_high = subject_get_int(cfg.key_tone.val) + high / 2;
    } else {
        cur_low  = low;
        cur_high = high;
    }

    cfg_cur.filter.low  = subject_create_int(cur_low);
    cfg_cur.filter.high = subject_create_int(cur_high);
    cfg_cur.filter.bw   = subject_create_int(high - low);

    cfg_cur.filter.real.from = subject_create_int(cur_low);
    cfg_cur.filter.real.to   = subject_create_int(cur_high);

    cfg_cur.freq_step = subject_create_int(step);
    cfg_cur.zoom      = subject_create_int(zoom);

    cfg_cur.lo_offset      = subject_create_int(0);

    subject_add_observer(cfg_cur.mode, on_cur_mode_change, NULL);

    subject_add_observer(cfg_cur.filter.low, on_cur_filter_low_change, NULL);
    subject_add_observer(cfg_cur.filter.high, on_cur_filter_high_change, NULL);
    subject_add_observer(cfg_cur.filter.bw, on_cur_filter_bw_change, NULL);
    subject_add_observer(cfg_cur.freq_step, on_cur_freq_step_change, NULL);
    subject_add_observer(cfg_cur.zoom, on_cur_zoom_change, NULL);

    subject_add_observer(cfg_cur.filter.low, update_real_filters, NULL);
    subject_add_observer(cfg_cur.filter.high, update_real_filters, NULL);
    subject_add_observer_and_call(cfg_cur.mode, update_real_filters, NULL);

    subject_add_observer(cfg_cur.mode, update_lo_offset, NULL);
    subject_add_observer_and_call(cfg.key_tone.val, update_lo_offset, NULL);

    fill_mode_cfg_item_int(&cfg_mode.filter_low, subject_create_int(low), "filter_low", db_mode);
    fill_mode_cfg_item_int(&cfg_mode.filter_high, subject_create_int(high), "filter_high", db_mode);
    fill_mode_cfg_item_int(&cfg_mode.freq_step, subject_create_int(step), "freq_step", db_mode);
    fill_mode_cfg_item_int(&cfg_mode.zoom, subject_create_int(zoom), "spectrum_factor", db_mode);

    subject_add_observer(cfg_mode.filter_low.val, update_cur_low_filter, &cfg_mode.filter_low);
    subject_add_observer(cfg_cur.mode, update_cur_low_filter, &cfg_mode.filter_low);

    subject_add_observer(cfg_mode.filter_high.val, update_cur_high_filter, &cfg_mode.filter_high);
    subject_add_observer(cfg_cur.mode, update_cur_high_filter, &cfg_mode.filter_high);

    subject_add_observer(cfg_mode.freq_step.val, on_freq_step_change, NULL);


    subject_add_observer(cfg_mode.zoom.val, on_zoom_change, NULL);

    /* Load values from table */
    cfg_item_t *cfg_arr  = (cfg_item_t *)&cfg_mode;
    uint32_t    cfg_size = sizeof(cfg_mode) / sizeof(*cfg_arr);
    init_items(cfg_arr, cfg_size, cfg_mode_params_load_item, cfg_mode_params_save_item);
    load_items_from_db(cfg_arr, cfg_size);
}

int cfg_mode_params_load_item(cfg_item_t *item) {
    if (item->pk == MODE_UNDEFINED) {
        LV_LOG_USER("Can't load %s for undefined mode", item->db_name);
        return 0;
    }
    int           rc;
    int32_t       val;
    sqlite3_stmt *stmt = read_stmt;
    pthread_mutex_lock(&read_mutex);
    rc = sqlite3_bind_text(stmt, sqlite3_bind_parameter_index(stmt, ":name"), item->db_name, strlen(item->db_name), 0);
    if (rc != SQLITE_OK) {
        LV_LOG_ERROR("Failed to bind name %s: %s", item->db_name, sqlite3_errmsg(db));
        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);
        pthread_mutex_unlock(&read_mutex);
        return rc;
    }
    rc = sqlite3_bind_int(stmt, sqlite3_bind_parameter_index(stmt, ":id"), item->pk);
    if (rc != SQLITE_OK) {
        LV_LOG_ERROR("Failed to bind mode %i: %s", item->pk, sqlite3_errmsg(db));
        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);
        pthread_mutex_unlock(&read_mutex);
        return rc;
    }

    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        switch (subject_get_dtype(item->val)) {
            case DTYPE_INT:
                val = sqlite3_column_int(stmt, 0);
                if (val < 0) {
                    LV_LOG_WARN("%s can't be negative (%i), ignore DB value", item->db_name, val);
                } else {
                    LV_LOG_USER("Loaded %s=%i (pk=%u)", item->db_name, val, item->pk);
                    subject_set_int(item->val, val);
                }
                break;
            default:
                LV_LOG_WARN("Unknown item %s dtype: %u, can't load", item->db_name, subject_get_dtype(item->val));
                sqlite3_reset(stmt);
                sqlite3_clear_bindings(stmt);
                pthread_mutex_unlock(&read_mutex);
                return -1;
        }
        rc = 0;
    } else {
        LV_LOG_WARN("No results for load from mode_params with name: %s and mode: %i", item->db_name, item->pk);
        // Save with default value
        cfg_mode_params_save_item(item);
        rc = -1;
    }
    sqlite3_reset(stmt);
    sqlite3_clear_bindings(stmt);
    pthread_mutex_unlock(&read_mutex);
    return rc;
}

int cfg_mode_params_save_item(cfg_item_t *item) {
    if (item->pk == MODE_UNDEFINED) {
        LV_LOG_USER("Can't save %s for undefined mode", item->db_name);
        return 0;
    }
    int rc;

    sqlite3_stmt *stmt = insert_stmt;
    pthread_mutex_lock(&write_mutex);
    rc = sqlite3_bind_text(stmt, sqlite3_bind_parameter_index(stmt, ":name"), item->db_name, strlen(item->db_name), 0);
    if (rc != SQLITE_OK) {
        LV_LOG_ERROR("Failed to bind name %s: %s", item->db_name, sqlite3_errmsg(db));
        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);
        pthread_mutex_unlock(&write_mutex);
        return rc;
    }
    rc = sqlite3_bind_int(stmt, sqlite3_bind_parameter_index(stmt, ":id"), item->pk);
    if (rc != SQLITE_OK) {
        LV_LOG_ERROR("Failed to bind mode %i: %s", item->pk, sqlite3_errmsg(db));
        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);
        pthread_mutex_unlock(&write_mutex);
        return rc;
    }
    int val_index = sqlite3_bind_parameter_index(stmt, ":val");
    int32_t int_val;

    switch (subject_get_dtype(item->val)) {
        case DTYPE_INT:
        int_val = subject_get_int(item->val);
            if (int_val < 0) {
                LV_LOG_ERROR("%s can't be negative (%i), will not save", item->db_name, int_val);
                sqlite3_reset(stmt);
                sqlite3_clear_bindings(stmt);
                pthread_mutex_unlock(&write_mutex);
                return -1;
            } else {
                LV_LOG_USER("Saved %s=%i (pk=%u)", item->db_name, int_val, item->pk);
                rc = sqlite3_bind_int(stmt, val_index, int_val);
            }
            break;
        default:
            LV_LOG_WARN("Unknown item %s dtype: %u, will not save", item->db_name, subject_get_dtype(item->val));
            rc = -1;
            break;
    }
    if (rc != SQLITE_OK) {
        LV_LOG_ERROR("Failed to bind val for name %s: %s", item->db_name, sqlite3_errmsg(db));
        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);
        pthread_mutex_unlock(&write_mutex);
        return rc;
    }
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        LV_LOG_ERROR("Failed save item %s: %s", item->db_name, sqlite3_errmsg(db));
    } else {
        rc = 0;
    }
    sqlite3_reset(stmt);
    sqlite3_clear_bindings(stmt);
    pthread_mutex_unlock(&write_mutex);
    return rc;
}

db_mode_t xmode_2_db(x6100_mode_t mode) {
    switch (mode) {
        case x6100_mode_lsb:
        case x6100_mode_usb:
            return x6100_mode_lsb;
            break;

        case x6100_mode_lsb_dig:
        case x6100_mode_usb_dig:
            return x6100_mode_lsb_dig;
            break;

        case x6100_mode_cw:
        case x6100_mode_cwr:
            return x6100_mode_cw;
            break;

        case x6100_mode_am:
            return x6100_mode_am;
            break;
        case x6100_mode_nfm:
            return x6100_mode_nfm;
            break;

        default:
            return MODE_UNDEFINED;
            break;
    }
}

bool mode_default_values(db_mode_t mode, uint32_t *low, uint32_t *high, uint32_t *step, uint32_t *zoom) {
    bool result = true;
    switch (mode) {
        case x6100_mode_lsb:
        case x6100_mode_lsb_dig:
            *low  = 50;
            *high = 2950;
            *step = 500;
            *zoom = 1;
            break;
        case x6100_mode_cw:
            *low  = 0;
            *high = 250;
            *step = 100;
            *zoom = 4;
            break;
        case x6100_mode_am:
        case x6100_mode_nfm:
            *low  = 0;
            *high = 4000;
            *step = 1000;
            *zoom = 1;
            break;
        default:
            result = false;
            break;
    }
    return result;
}

int32_t cfg_mode_change_freq_step(bool up) {
    // find current
    size_t  i;
    int32_t freq_step = subject_get_int(cfg_cur.freq_step);
    for (i = 0; i < freq_steps_n; i++) {
        if (freq_step == freq_steps[i]) {
            break;
        }
    }
    i         = (i + (up ? 1 : -1) + freq_steps_n) % freq_steps_n;
    freq_step = freq_steps[i];
    subject_set_int(cfg_cur.freq_step, freq_step);
    return freq_step;
}

int32_t cfg_mode_set_low_filter(int32_t val) {
    x6100_mode_t mode = subject_get_int(cfg_cur.mode);
    int32_t      high = subject_get_int(cfg_cur.filter.high);
    switch (mode) {
        case x6100_mode_am:
        case x6100_mode_nfm:
            return 0;
        default:
            if ((val >= 0) & (val < high)) {
                subject_set_int(cfg_cur.filter.low, val);
            }
            return subject_get_int(cfg_cur.filter.low);
    }
}

int32_t cfg_mode_set_high_filter(int32_t val) {
    x6100_mode_t mode = subject_get_int(cfg_cur.mode);
    int32_t      low = subject_get_int(cfg_cur.filter.low);
    if ((val <= MAX_FILTER_FREQ) & (val > low)) {
        subject_set_int(cfg_cur.filter.high, val);
    }
    return subject_get_int(cfg_cur.filter.high);
}

static void init_db(sqlite3 *database) {
    db = database;
    int rc;
    rc = sqlite3_prepare_v2(db, "SELECT val FROM mode_params WHERE mode = :id AND name = :name", -1, &read_stmt, 0);
    if (rc != SQLITE_OK) {
        LV_LOG_ERROR("Failed prepare read statement: %s", sqlite3_errmsg(db));
        exit(1);
    }
    rc = sqlite3_prepare_v2(db, "INSERT OR REPLACE INTO mode_params(mode, name, val) VALUES(:id, :name, :val)", -1,
                            &insert_stmt, 0);
    if (rc != SQLITE_OK) {
        LV_LOG_ERROR("Failed prepare write statement: %s", sqlite3_errmsg(db));
        exit(1);
    }
}

static void on_cur_mode_change(Subject *subj, void *user_data) {
    x6100_mode_t new_mode = subject_get_int(subj);

    if (cfg_mode.filter_high.dirty == NULL) {
        LV_LOG_USER("Skip updating mode values, not initialized");
        return;
    }

    // Update mode params
    db_mode_t   db_mode = xmode_2_db(new_mode);
    cfg_item_t *cfg_mode_arr;
    cfg_mode_arr           = (cfg_item_t *)&cfg_mode;
    uint32_t cfg_mode_size = sizeof(cfg_mode) / sizeof(cfg_item_t);
    // Save
    for (size_t i = 0; i < cfg_mode_size; i++) {
        if (cfg_mode_arr[i].pk != db_mode) {
            save_item_to_db(&cfg_mode_arr[i], false);
        }
    }
    // Load
    for (size_t i = 0; i < cfg_mode_size; i++) {
        if (cfg_mode_arr[i].pk != db_mode) {
            cfg_mode_arr[i].dirty->val = ITEM_STATE_LOADING;
            cfg_mode_arr[i].pk = db_mode;
            cfg_mode_arr[i].load(&cfg_mode_arr[i]);
            cfg_mode_arr[i].dirty->val = ITEM_STATE_CLEAN;
        }
    }
}

static void on_cur_filter_low_change(Subject *subj, void *user_data) {
    int32_t new_low = subject_get_int(subj);
    LV_LOG_INFO("New current low=%i", new_low);
    subject_set_int(cfg_cur.filter.bw, subject_get_int(cfg_cur.filter.high) - new_low);
    int32_t new_high;
    switch (cfg_mode.filter_low.pk) {
        case x6100_mode_cw:
        case x6100_mode_cwr:
            new_high = (subject_get_int(cfg.key_tone.val) - new_low) * 2;
            subject_set_int(cfg_mode.filter_high.val, new_high);
            break;

        default:
            subject_set_int(cfg_mode.filter_low.val, new_low);
            break;
    }
}

static void on_cur_filter_high_change(Subject *subj, void *user_data) {
    int32_t new_high = subject_get_int(subj);
    LV_LOG_INFO("New current high=%i", new_high);
    subject_set_int(cfg_cur.filter.bw, new_high - subject_get_int(cfg_cur.filter.low));
    switch (cfg_mode.filter_high.pk) {
        case x6100_mode_cw:
        case x6100_mode_cwr:
            new_high = (new_high - subject_get_int(cfg.key_tone.val)) * 2;
            subject_set_int(cfg_mode.filter_high.val, new_high);
            break;

        default:
            subject_set_int(cfg_mode.filter_high.val, new_high);
            break;
    }
}

static void on_cur_filter_bw_change(Subject *subj, void *user_data) {
    if (cfg_mode.filter_low.pk != cfg_mode.filter_high.pk) {
        LV_LOG_INFO("Skip update bw, different modes");
        return;
    }
    int32_t new_bw = subject_get_int(subj);
    int32_t new_low, new_high;
    switch (cfg_mode.filter_high.pk) {
        case x6100_mode_am:
        case x6100_mode_nfm:
            new_low  = subject_get_int(cfg_cur.filter.low);
            new_high = new_low + new_bw;
            break;
        default:
            new_low  = (subject_get_int(cfg_cur.filter.high) + subject_get_int(cfg_cur.filter.low) - new_bw) / 2;
            new_high = new_low + new_bw;
            break;
    }
    LV_LOG_INFO("New bw=%i, set cur filters: low=%i high=%i", new_bw, new_low, new_high);

    subject_set_int(cfg_cur.filter.low, new_low);
    subject_set_int(cfg_cur.filter.high, new_high);
}

static void on_cur_freq_step_change(Subject *subj, void *user_data) {
    if (cfg_mode.freq_step.dirty == NULL) {
        LV_LOG_USER("Freq step is not initialized, skip updating");
    }
    subject_set_int(cfg_mode.freq_step.val, subject_get_int(subj));
    ;
}

static void on_cur_zoom_change(Subject *subj, void *user_data) {
    subject_set_int(cfg_mode.zoom.val, subject_get_int(subj));
    ;
}

/**
 * On changing mode params
 */
static void update_cur_low_filter(Subject *subj, void *user_data) {
    cfg_item_t *item    = (cfg_item_t *)user_data;
    int32_t     cur_low = subject_get_int(cfg_mode.filter_low.val);
    switch (item->pk) {
        case x6100_mode_cw:
        case x6100_mode_cwr:
        case x6100_mode_am:
        case x6100_mode_nfm:
            // cur_low = subject_get_int(cfg.key_tone.val) - subject_get_int(cfg_mode.filter_high.val) / 2;
            // cur_low = 0;
            return;
            break;

        default:
            break;
    }
    LV_LOG_USER("Set current low filter: %i", cur_low);
    subject_set_int(cfg_cur.filter.low, cur_low);
}

static void update_cur_high_filter(Subject *subj, void *user_data) {
    cfg_item_t *item     = (cfg_item_t *)user_data;
    int32_t     cur_high = subject_get_int(cfg_mode.filter_high.val);
    int32_t     bw;
    switch (item->pk) {
        case x6100_mode_cw:
        case x6100_mode_cwr:
            bw       = cur_high;
            cur_high = subject_get_int(cfg.key_tone.val) + bw / 2;
            subject_set_int(cfg_cur.filter.low, cur_high - bw);
            break;

        case x6100_mode_am:
        case x6100_mode_nfm:
            subject_set_int(cfg_cur.filter.low, 0);
            break;

        default:
            break;
    }
    LV_LOG_USER("Set current high filter: %i", cur_high);
    subject_set_int(cfg_cur.filter.high, cur_high);
}

static void on_freq_step_change(Subject *subj, void *user_data) {
    subject_set_int(cfg_cur.freq_step, subject_get_int(subj));
}

static void on_zoom_change(Subject *subj, void *user_data) {
    subject_set_int(cfg_cur.zoom, subject_get_int(subj));
}

/**
 * get frequencies for display and dsp (with negative numbers)
 */
static void update_real_filters(Subject *subj, void *user_data) {
    x6100_mode_t mode = subject_get_int(cfg_cur.mode);
    int32_t      low  = subject_get_int(cfg_cur.filter.low);
    int32_t      high = subject_get_int(cfg_cur.filter.high);
    int32_t      from, to;

    switch (mode) {
        case x6100_mode_lsb:
        case x6100_mode_lsb_dig:
        case x6100_mode_cwr:
            from = -high;
            to   = -low;
            break;

        case x6100_mode_usb:
        case x6100_mode_usb_dig:
        case x6100_mode_cw:
            from = low;
            to   = high;
            break;

        case x6100_mode_am:
        case x6100_mode_nfm:
            from = -high;
            to   = high;
            break;

        default:
            LV_LOG_WARN("Unknown modulation: %u, filters will no be updated", mode);
            return;
    }
    LV_LOG_INFO("Set real filters: (%i, %i)", from, to);
    subject_set_int(cfg_cur.filter.real.from, from);
    subject_set_int(cfg_cur.filter.real.to, to);
}


static void update_lo_offset(Subject *subj, void *user_data) {
    x6100_mode_t mode = subject_get_int(cfg_cur.mode);
    int32_t key_tone = subject_get_int(cfg.key_tone.val);
    int32_t lo_offset;
    switch (mode) {
        case x6100_mode_cw:
            lo_offset = -key_tone;
            break;
        case x6100_mode_cwr:
            lo_offset = key_tone;
            break;
        default:
            lo_offset = 0;
    }
    subject_set_int(cfg_cur.lo_offset, lo_offset);
}

static void fill_mode_cfg_item_int(cfg_item_t *item, Subject * val, const char * db_name, int pk) {
    fill_cfg_item_int(item, val, db_name);
    item->pk = pk;
    item->load = NULL;
    item->save = NULL;
}

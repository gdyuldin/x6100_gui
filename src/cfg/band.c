/**
 * Work with band_params table on DB
 */
#include "band.private.h"

#include "subjects.private.h"

#include "../lvgl/lvgl.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static sqlite3      *db;
static sqlite3_stmt *write_stmt;
static sqlite3_stmt *read_stmt;
static sqlite3_stmt *read_band_by_pk_stmt;
static sqlite3_stmt *read_band_by_freq_stmt;
static sqlite3_stmt *read_all_bands_stmt;

static band_info_t _band_info_cache = {.id = BAND_UNDEFINED, .start_freq=0, .stop_freq=0};

void cfg_band_params_init(sqlite3 *database) {
    db = database;
    int rc;
    rc = sqlite3_prepare_v2(db, "SELECT val FROM band_params WHERE bands_id = :id AND name = :name", -1, &read_stmt, 0);
    if (rc != SQLITE_OK) {
        LV_LOG_ERROR("Failed prepare read statement: %s", sqlite3_errmsg(db));
        exit(1);
    }
    rc = sqlite3_prepare_v2(db, "INSERT OR REPLACE INTO band_params(bands_id, name, val) VALUES(:id, :name, :val)", -1,
                            &write_stmt, 0);
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
        "   (:freq > start_freq) AND (:freq <= stop_freq) AND (type = 1) "
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
    rc = sqlite3_prepare_v2(db, "SELECT id, name, start_freq, stop_freq, type FROM bands", -1, &read_all_bands_stmt, 0);
    if (rc != SQLITE_OK) {
        LV_LOG_ERROR("Failed prepare read_all_bands statement: %s", sqlite3_errmsg(db));
        exit(1);
    }
}

band_info_t *get_band_info_by_pk(int32_t band_id) {
    int rc;
    sqlite3_stmt *stmt = read_band_by_pk_stmt;
    if (_band_info_cache.id == band_id) {
        return &_band_info_cache;
    }
    rc = sqlite3_bind_int(stmt, sqlite3_bind_parameter_index(stmt, ":id"), band_id);
    if (rc != SQLITE_OK) {
        LV_LOG_ERROR("Failed to bind bands_id %s: %s", band_id, sqlite3_errmsg(db));
        return NULL;
    }
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        if (_band_info_cache.name) {
            free(_band_info_cache.name);
            _band_info_cache.name = NULL;
        }
        _band_info_cache.id = band_id;
        _band_info_cache.name = strdup(sqlite3_column_text(stmt, 0));
        _band_info_cache.start_freq = sqlite3_column_int(stmt, 1);
        _band_info_cache.stop_freq = sqlite3_column_int(stmt, 2);
        _band_info_cache.active = sqlite3_column_int(stmt, 3);
        rc = 0;
    } else {
        LV_LOG_WARN("No info for band with id: %i", band_id);
        rc = -1;
    }
    sqlite3_reset(stmt);
    sqlite3_clear_bindings(stmt);
    if (rc == 0)
        return &_band_info_cache;
    else
        return NULL;
}

band_info_t *get_band_info_by_freq(uint32_t freq) {
    if ((freq > _band_info_cache.start_freq) && (freq <= _band_info_cache.stop_freq))
        return &_band_info_cache;
    int rc;
    sqlite3_stmt *stmt = read_band_by_freq_stmt;
    rc = sqlite3_bind_int(stmt, sqlite3_bind_parameter_index(stmt, ":freq"), freq);
    if (rc != SQLITE_OK) {
        LV_LOG_ERROR("Failed to bind freq %lu to read_band_by_freq_stmt: %s", freq, sqlite3_errmsg(db));
        return NULL;
    }
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        if (_band_info_cache.name) {
            free(_band_info_cache.name);
            _band_info_cache.name = NULL;
        }
        if (sqlite3_column_type(stmt, 0) != SQLITE_NULL) {
            _band_info_cache.id = sqlite3_column_int(stmt, 0);
            _band_info_cache.name = strdup(sqlite3_column_text(stmt, 1));
            _band_info_cache.active = true;
        } else {
            _band_info_cache.id = BAND_UNDEFINED;
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
    if (rc == 0) {
        return &_band_info_cache;
    } else {
        return NULL;
    }

}

int cfg_band_params_load_item(cfg_item_t *item) {
    if (item->pk == BAND_UNDEFINED) {
        LV_LOG_USER("Can't load %s for undefined band", item->db_name);
        return 0;
    }
    band_info_t *band_info = get_band_info_by_pk(item->pk);
    if (!band_info) {
        LV_LOG_ERROR("Can't load band info for pk: %i", item->pk);
        return -1;
    }
    sqlite3_stmt *stmt = read_stmt;
    int rc;
    int32_t int_val;
    rc = sqlite3_bind_text(stmt, sqlite3_bind_parameter_index(stmt, ":name"), item->db_name,
                           strlen(item->db_name), 0);
    if (rc != SQLITE_OK) {
        LV_LOG_ERROR("Failed to bind name %s: %s", item->db_name, sqlite3_errmsg(db));
        return rc;
    }
    rc = sqlite3_bind_int(stmt, sqlite3_bind_parameter_index(stmt, ":id"), item->pk);
    if (rc != SQLITE_OK) {
        LV_LOG_ERROR("Failed to bind bands_id %s: %s", item->pk, sqlite3_errmsg(db));
        return rc;
    }

    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        switch (item->val->dtype) {
        case DTYPE_INT:
            int_val = sqlite3_column_int(stmt, 0);
            // Check that freq match band
            if ((strcmp(item->db_name, "vfoa_freq") == 0) || (strcmp(item->db_name, "vfob_freq") == 0)) {
                if ((int_val >= band_info->start_freq) && (int_val <= band_info->stop_freq)) {
                    subject_set_int(item->val, int_val);
                } else {
                    LV_LOG_USER("Freq %lu for %s (band_id: %u) outside boundaries, db value ignored", int_val, item->db_name, item->pk);
                }
            } else {
                subject_set_int(item->val, int_val);
            }
            break;
        default:
            LV_LOG_WARN("Unknown item %s dtype: %u, can't load", item->db_name, item->val->dtype);
            return -1;
        }
        rc = 0;
    } else {
        if (strcmp(item->db_name, "vfob_freq") == 0) {
            LV_LOG_INFO("Copy vfoa freq to vfob");
            subject_set_int(item->val, subject_get_int(cfg_band.vfo_a.freq.val));
            rc = 0;
        } else {
            LV_LOG_WARN("No results for load from band_params with name: %s and bands_id: %i", item->db_name, item->pk);
            // Save with default value
            cfg_band_params_save_item(item);
            rc = -1;
        }
    }
    sqlite3_reset(stmt);
    sqlite3_clear_bindings(stmt);
    return rc;
}

int cfg_band_params_save_item(cfg_item_t *item) {
    if (item->pk == BAND_UNDEFINED) {
        LV_LOG_USER("Can't save %s for undefined band", item->db_name);
        return 0;
    }

    band_info_t *band_info = get_band_info_by_pk(item->pk);
    if (!band_info) {
        LV_LOG_ERROR("Can't load band info for pk: %i", item->pk);
        return -1;
    }
    sqlite3_stmt *stmt = write_stmt;

    int      rc;
    int32_t int_val;

    rc = sqlite3_bind_text(stmt, sqlite3_bind_parameter_index(stmt, ":name"), item->db_name,
                           strlen(item->db_name), 0);
    if (rc != SQLITE_OK) {
        LV_LOG_ERROR("Failed to bind name %s: %s", item->db_name, sqlite3_errmsg(db));
        return rc;
    }
    rc = sqlite3_bind_int(stmt, sqlite3_bind_parameter_index(stmt, ":id"), item->pk);
    if (rc != SQLITE_OK) {
        LV_LOG_ERROR("Failed to bind bands_id %s: %s", item->pk, sqlite3_errmsg(db));
        return rc;
    }
    int val_index = sqlite3_bind_parameter_index(stmt, ":val");

    switch (item->val->dtype) {
    case DTYPE_INT:
        int_val = item->val->int_val;
        // Check that freq match band
        if ((strcmp(item->db_name, "vfoa_freq") == 0) || (strcmp(item->db_name, "vfob_freq") == 0)) {
            if ((int_val >= band_info->start_freq) && (int_val <= band_info->stop_freq)) {
                rc = sqlite3_bind_int(stmt, val_index, int_val);
            } else {
                LV_LOG_USER("Freq %lu for %s (band_id: %u) outside boundaries, will not save", int_val, item->db_name, item->pk);
            }
        } else {
            rc = sqlite3_bind_int(stmt, val_index, int_val);
        }
        break;
    default:
        LV_LOG_WARN("Unknown item %s dtype: %u, will not save", item->db_name, item->val->dtype);
        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);
        return -1;
        break;
    }
    if (rc != SQLITE_OK) {
        LV_LOG_ERROR("Failed to bind val for name %s: %s", item->db_name, sqlite3_errmsg(db));
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
    return rc;
}

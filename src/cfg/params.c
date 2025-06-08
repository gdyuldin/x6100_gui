/**
 * Work with params table on DB
 */
#include "params.private.h"

#include "../lvgl/lvgl.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

static sqlite3      *db;
static sqlite3_stmt *insert_stmt;
static sqlite3_stmt *read_stmt;
static pthread_mutex_t write_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t read_mutex = PTHREAD_MUTEX_INITIALIZER;


void cfg_params_init(sqlite3 *database) {
    db = database;
    int rc;
    rc = sqlite3_prepare_v2(db, "SELECT val FROM params WHERE name = :name", -1, &read_stmt, 0);
    if (rc != SQLITE_OK) {
        LV_LOG_ERROR("Failed prepare read statement: %s", sqlite3_errmsg(db));
        exit(1);
    }
    rc = sqlite3_prepare_v2(db, "INSERT OR REPLACE INTO params(name, val) VALUES(:name, :val)", -1, &insert_stmt, 0);
    if (rc != SQLITE_OK) {
        LV_LOG_ERROR("Failed prepare write statement: %s", sqlite3_errmsg(db));
        exit(1);
    }
}


int cfg_params_load_item(cfg_item_t *item) {
    int rc;
    pthread_mutex_lock(&read_mutex);
    rc = sqlite3_bind_text(read_stmt, sqlite3_bind_parameter_index(read_stmt, ":name"), item->db_name, strlen(item->db_name), 0);
    if (rc != SQLITE_OK) {
        LV_LOG_ERROR("Failed to bind name %s: %s", item->db_name, sqlite3_errmsg(db));
        pthread_mutex_unlock(&read_mutex);
        return rc;
    }
    int32_t int_val;
    uint64_t uint64_val;
    rc = sqlite3_step(read_stmt);
    if (rc == SQLITE_ROW) {
        switch (subject_get_dtype(item->val)) {
            case DTYPE_INT:
                int_val = sqlite3_column_int(read_stmt, 0);
                LV_LOG_USER("Loaded %s=%i (pk=%i)", item->db_name, int_val, item->pk);
                subject_set_int(item->val, int_val);
                break;
            case DTYPE_UINT64:
                uint64_val = sqlite3_column_int64(read_stmt, 0);
                LV_LOG_USER("Loaded %s=%llu (pk=%i)", item->db_name, uint64_val, item->pk);
                subject_set_uint64(item->val, uint64_val);
                break;
            case DTYPE_FLOAT: ;
                float val = cfg_float_val_from_db(item, read_stmt, 0);
                subject_set_float(item->val, val);
                break;
            default:
                LV_LOG_WARN("Unknown item %s dtype: %u, can't load", item->db_name, subject_get_dtype(item->val));
                pthread_mutex_unlock(&read_mutex);
                return -1;
        }
        rc = 0;
    } else {
        LV_LOG_WARN("No results for load %s", item->db_name);
        rc = -1;
    }
    sqlite3_reset(read_stmt);
    sqlite3_clear_bindings(read_stmt);
    pthread_mutex_unlock(&read_mutex);
    return rc;
}

int cfg_params_save_item(cfg_item_t *item) {
    int rc;
    pthread_mutex_lock(&write_mutex);
    rc = sqlite3_bind_text(insert_stmt, sqlite3_bind_parameter_index(insert_stmt, ":name"), item->db_name,
                           strlen(item->db_name), 0);
    if (rc != SQLITE_OK) {
        LV_LOG_WARN("Can't bind name %s to save params query", item->db_name);
        pthread_mutex_unlock(&write_mutex);
        return rc;
    }
    int            val_index = sqlite3_bind_parameter_index(insert_stmt, ":val");
    enum data_type dtype     = subject_get_dtype(item->val);
    int32_t        int_val;
    uint64_t       uint64_val;
    float          float_val;
    switch (dtype) {
        case DTYPE_INT:
            int_val = subject_get_int(item->val);
            rc      = sqlite3_bind_int(insert_stmt, val_index, int_val);
            if (rc != SQLITE_OK) {
                LV_LOG_WARN("Can't bind val %i to save params query", int_val);
            }
            break;
        case DTYPE_UINT64:
            uint64_val = subject_get_uint64(item->val);
            rc         = sqlite3_bind_int64(insert_stmt, val_index, uint64_val);
            if (rc != SQLITE_OK) {
                LV_LOG_WARN("Can't bind val %llu to save params query", uint64_val);
            }
            break;
        case DTYPE_FLOAT:
            rc = cfg_float_val_to_db(item, insert_stmt, val_index, &float_val);
            break;
        default:
            LV_LOG_WARN("Unknown item %s dtype: %u, will not save", item->db_name, dtype);
            sqlite3_reset(insert_stmt);
            sqlite3_clear_bindings(insert_stmt);
            pthread_mutex_unlock(&write_mutex);
            return -1;
            break;
    }
    if (rc == SQLITE_OK) {
        rc = sqlite3_step(insert_stmt);
        if (rc != SQLITE_DONE) {
            LV_LOG_ERROR("Failed save item %s: %s", item->db_name, sqlite3_errmsg(db));
        } else {
            switch (dtype) {
                case DTYPE_INT:
                    LV_LOG_USER("Saved %s=%i (pk=%i)", item->db_name, int_val, item->pk);
                    break;
                case DTYPE_UINT64:
                    LV_LOG_USER("Saved %s=%llu (pk=%i)", item->db_name, uint64_val, item->pk);
                    break;
                case DTYPE_FLOAT:
                    LV_LOG_USER("Saved %s=%f (pk=%i)", item->db_name, float_val, item->pk);
                    break;
                default:
                    break;
            }
            rc = 0;
        }
    }
    sqlite3_reset(insert_stmt);
    sqlite3_clear_bindings(insert_stmt);
    pthread_mutex_unlock(&write_mutex);
    return rc;
}


float cfg_float_val_from_db(cfg_item_t *item, sqlite3_stmt *stmt, int iCol) {
    float val;
    if (item->db_scale != 0) {
        val = sqlite3_column_int(stmt, iCol) * item->db_scale;
    } else {
        val = sqlite3_column_double(stmt, iCol);
    }
    LV_LOG_USER("Loaded %s=%f (pk=%i)", item->db_name, val, item->pk);
    return val;
}

int cfg_float_val_to_db(cfg_item_t *item, sqlite3_stmt *stmt, int iCol, float* val) {
    int rc;
    *val = subject_get_float(item->val);
    if (item->db_scale != 0) {
        rc = sqlite3_bind_int(stmt, iCol, roundf(*val / item->db_scale));
    } else {
        rc = sqlite3_bind_double(stmt, iCol, *val);
    }
    if (rc != SQLITE_OK) {
        LV_LOG_WARN("Can't bind val %f to save params query", *val);
    }
    return rc;
}

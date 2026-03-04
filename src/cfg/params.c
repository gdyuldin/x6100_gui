/**
 * Work with params table on DB
 */
#include "params.private.h"

#include "../lvgl/lvgl.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

static inline int prepare_read_stmt(cfg_item_t *item);
static inline int prepare_write_stmt(cfg_item_t *item);

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


int cfg_params_load_item_int(cfg_item_t *item) {
    if (subject_get_dtype(item->val) != DTYPE_INT) {
        LV_LOG_WARN("Wrong item %s dtype: %u, can't load", item->db_name, subject_get_dtype(item->val));
        return -1;
    }
    int rc;
    pthread_mutex_lock(&read_mutex);
    rc = prepare_read_stmt(item);
    if (rc != SQLITE_OK) {
        pthread_mutex_unlock(&read_mutex);
        return rc;
    }

    int32_t int_val;
    rc = sqlite3_step(read_stmt);
    if (rc == SQLITE_ROW) {
        int_val = sqlite3_column_int(read_stmt, 0);
        LV_LOG_USER("Loaded %s=%i (pk=%i)", item->db_name, int_val, item->pk);
        subject_set_int(item->val, int_val);
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

int cfg_params_load_item_uint64(cfg_item_t *item) {
    if (subject_get_dtype(item->val) != DTYPE_UINT64) {
        LV_LOG_WARN("Wrong item %s dtype: %u, can't load", item->db_name, subject_get_dtype(item->val));
        return -1;
    }
    int rc;
    pthread_mutex_lock(&read_mutex);
    rc = prepare_read_stmt(item);
    if (rc != SQLITE_OK) {
        pthread_mutex_unlock(&read_mutex);
        return rc;
    }

    uint64_t uint64_val;
    rc = sqlite3_step(read_stmt);
    if (rc == SQLITE_ROW) {
        uint64_val = sqlite3_column_int64(read_stmt, 0);
        LV_LOG_USER("Loaded %s=%llu (pk=%i)", item->db_name, uint64_val, item->pk);
        subject_set_uint64(item->val, uint64_val);
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

int cfg_params_load_item_float(cfg_item_t *item) {
    if (subject_get_dtype(item->val) != DTYPE_FLOAT) {
        LV_LOG_WARN("Wrong item %s dtype: %u, can't load", item->db_name, subject_get_dtype(item->val));
        return -1;
    }
    int rc;
    pthread_mutex_lock(&read_mutex);
    rc = prepare_read_stmt(item);
    if (rc != SQLITE_OK) {
        pthread_mutex_unlock(&read_mutex);
        return rc;
    }

    float float_val;
    rc = sqlite3_step(read_stmt);
    if (rc == SQLITE_ROW) {
        if (item->db_scale != 0) {
            float_val = sqlite3_column_int(read_stmt, 0) * item->db_scale;
        } else {
            float_val = sqlite3_column_double(read_stmt, 0);
        }
        LV_LOG_USER("Loaded %s=%f (pk=%i)", item->db_name, float_val, item->pk);
        subject_set_float(item->val, float_val);
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

int cfg_params_load_item_str(cfg_item_t *item) {
    if (subject_get_dtype(item->val) != DTYPE_STR) {
        LV_LOG_WARN("Wrong item %s dtype: %u, can't load", item->db_name, subject_get_dtype(item->val));
        return -1;
    }
    int rc;
    pthread_mutex_lock(&read_mutex);
    rc = prepare_read_stmt(item);
    if (rc != SQLITE_OK) {
        pthread_mutex_unlock(&read_mutex);
        return rc;
    }

    const char *text_val;
    rc = sqlite3_step(read_stmt);
    if (rc == SQLITE_ROW) {
        text_val = sqlite3_column_text(read_stmt, 0);
        LV_LOG_USER("Loaded %s=%s (pk=%i)", item->db_name, text_val, item->pk);
        if (item->db_to_val) {
            char *processed_val = (char *)item->db_to_val((void*)text_val, item->val);
            subject_set_text(item->val, processed_val);
            free(processed_val);
        } else {
            subject_set_text(item->val, text_val);
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


int cfg_params_save_item_int(cfg_item_t *item) {
    enum data_type dtype = subject_get_dtype(item->val);
    if (dtype != DTYPE_INT) {
        LV_LOG_WARN("Wrong item %s dtype: %u, will not save", item->db_name, dtype);
        return -1;
    }

    int rc;

    pthread_mutex_lock(&write_mutex);
    rc = prepare_write_stmt(item);
    if (rc != SQLITE_OK) {
        pthread_mutex_unlock(&write_mutex);
        return rc;
    }

    int     val_index = sqlite3_bind_parameter_index(insert_stmt, ":val");
    int32_t int_val;

    int_val = subject_get_int(item->val);
    rc      = sqlite3_bind_int(insert_stmt, val_index, int_val);
    if (rc != SQLITE_OK) {
        LV_LOG_WARN("Can't bind val %i to save params query", int_val);
    } else {
        rc = sqlite3_step(insert_stmt);
        if (rc != SQLITE_DONE) {
            LV_LOG_ERROR("Failed save item %s: %s", item->db_name, sqlite3_errmsg(db));
        } else {
            LV_LOG_USER("Saved %s=%i (pk=%i)", item->db_name, int_val, item->pk);
            rc = SQLITE_OK;
        }
    }
    sqlite3_reset(insert_stmt);
    sqlite3_clear_bindings(insert_stmt);
    pthread_mutex_unlock(&write_mutex);
    return rc;
}

int cfg_params_save_item_uint64(cfg_item_t *item) {
    enum data_type dtype = subject_get_dtype(item->val);
    if (dtype != DTYPE_UINT64) {
        LV_LOG_WARN("Wrong item %s dtype: %u, will not save", item->db_name, dtype);
        return -1;
    }

    int rc;

    pthread_mutex_lock(&write_mutex);
    rc = prepare_write_stmt(item);
    if (rc != SQLITE_OK) {
        pthread_mutex_unlock(&write_mutex);
        return rc;
    }

    int      val_index = sqlite3_bind_parameter_index(insert_stmt, ":val");
    uint64_t uint64_val;
    uint64_val = subject_get_uint64(item->val);
    rc         = sqlite3_bind_int64(insert_stmt, val_index, uint64_val);
    if (rc != SQLITE_OK) {
        LV_LOG_WARN("Can't bind val %llu to save params query", uint64_val);
    } else {
        rc = sqlite3_step(insert_stmt);
        if (rc != SQLITE_DONE) {
            LV_LOG_ERROR("Failed save item %s: %s", item->db_name, sqlite3_errmsg(db));
        } else {
            LV_LOG_USER("Saved %s=%llu (pk=%i)", item->db_name, uint64_val, item->pk);
            rc = 0;
        }
    }
    sqlite3_reset(insert_stmt);
    sqlite3_clear_bindings(insert_stmt);
    pthread_mutex_unlock(&write_mutex);
    return rc;
}

int cfg_params_save_item_float(cfg_item_t *item) {
    enum data_type dtype = subject_get_dtype(item->val);
    if (dtype != DTYPE_FLOAT) {
        LV_LOG_WARN("Wrong item %s dtype: %u, will not save", item->db_name, dtype);
        return -1;
    }

    int rc;

    pthread_mutex_lock(&write_mutex);
    rc = prepare_write_stmt(item);
    if (rc != SQLITE_OK) {
        pthread_mutex_unlock(&write_mutex);
        return rc;
    }

    int   val_index = sqlite3_bind_parameter_index(insert_stmt, ":val");
    float float_val;

    float_val = subject_get_float(item->val);
    if (item->db_scale != 0) {
        rc = sqlite3_bind_int(insert_stmt, val_index, roundf(float_val / item->db_scale));
    } else {
        rc = sqlite3_bind_double(insert_stmt, val_index, float_val);
    }
    if (rc != SQLITE_OK) {
        LV_LOG_WARN("Can't bind val %f to save params query", float_val);
    } else {
        rc = sqlite3_step(insert_stmt);
        if (rc != SQLITE_DONE) {
            LV_LOG_ERROR("Failed save item %s: %s", item->db_name, sqlite3_errmsg(db));
        } else {
            LV_LOG_USER("Saved %s=%f (pk=%i)", item->db_name, float_val, item->pk);
            rc = 0;
        }
    }
    sqlite3_reset(insert_stmt);
    sqlite3_clear_bindings(insert_stmt);
    pthread_mutex_unlock(&write_mutex);
    return rc;
}

int cfg_params_save_item_str(cfg_item_t *item) {
    enum data_type dtype = subject_get_dtype(item->val);
    if (dtype != DTYPE_STR) {
        LV_LOG_WARN("Wrong item %s dtype: %u, will not save", item->db_name, dtype);
        return -1;
    }

    int rc;

    pthread_mutex_lock(&write_mutex);
    rc = prepare_write_stmt(item);
    if (rc != SQLITE_OK) {
        pthread_mutex_unlock(&write_mutex);
        return rc;
    }

    int         val_index = sqlite3_bind_parameter_index(insert_stmt, ":val");
    const char *text_val;

    text_val = subject_get_text(item->val);
    rc       = sqlite3_bind_text(insert_stmt, val_index, text_val, -1, 0);
    if (rc != SQLITE_OK) {
        LV_LOG_WARN("Can't bind val '%s' to save params query", text_val);
    } else {
        rc = sqlite3_step(insert_stmt);
        if (rc != SQLITE_DONE) {
            LV_LOG_ERROR("Failed save item %s: %s", item->db_name, sqlite3_errmsg(db));
        } else {
            LV_LOG_USER("Saved %s=%s (pk=%i)", item->db_name, text_val, item->pk);
            rc = 0;
        }
    }
    sqlite3_reset(insert_stmt);
    sqlite3_clear_bindings(insert_stmt);
    pthread_mutex_unlock(&write_mutex);
    return rc;
}

static inline int prepare_read_stmt(cfg_item_t *item) {
    int rc;
    rc = sqlite3_bind_text(read_stmt, sqlite3_bind_parameter_index(read_stmt, ":name"), item->db_name, strlen(item->db_name), 0);
    if (rc != SQLITE_OK) {
        LV_LOG_ERROR("Failed to bind name %s: %s", item->db_name, sqlite3_errmsg(db));
    }
    return rc;
}


static inline int prepare_write_stmt(cfg_item_t *item) {
    int rc;
    rc = sqlite3_bind_text(insert_stmt, sqlite3_bind_parameter_index(insert_stmt, ":name"), item->db_name,
                           strlen(item->db_name), 0);
    if (rc != SQLITE_OK) {
        LV_LOG_WARN("Can't bind name %s to save params query", item->db_name);
    }
    return rc;
}

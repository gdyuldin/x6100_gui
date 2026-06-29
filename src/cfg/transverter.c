#include "cfg.private.h"
#include "transverter.private.h"

#include "../lvgl/lvgl.h"

#include <stdlib.h>

cfg_transverter_t cfg_transverters[TRANSVERTER_NUM];

static cfg_item_t** cfg_arr;
static size_t cfg_arr_size = sizeof(cfg_transverters) / sizeof(cfg_item_t);

static sqlite3        *db;
static sqlite3_stmt   *insert_stmt;
static sqlite3_stmt   *read_stmt;
static sqlite3_stmt   *get_by_freq_stmt;
static pthread_mutex_t write_mutex       = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t read_mutex        = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t get_by_freq_mutex = PTHREAD_MUTEX_INITIALIZER;

static int cfg_transverter_load_item(cfg_item_t *item);
static int cfg_transverter_save_item(cfg_item_t *item);

void cfg_transverter_init(sqlite3 *database) {
    db = database;
    int rc;
    rc = sqlite3_prepare_v2(db, "SELECT val FROM transverter WHERE name = :name AND id = :id", -1, &read_stmt, 0);
    if (rc != SQLITE_OK) {
        LV_LOG_ERROR("Failed prepare read statement: %s", sqlite3_errmsg(db));
        exit(1);
    }
    rc = sqlite3_prepare_v2(db, "INSERT OR REPLACE INTO transverter(id, name, val) VALUES(:id, :name, :val)", -1,
                            &insert_stmt, 0);
    if (rc != SQLITE_OK) {
        LV_LOG_ERROR("Failed prepare write statement: %s", sqlite3_errmsg(db));
        exit(1);
    }

    cfg_transverters[0] = (cfg_transverter_t){
        .from  = {.val = subject_create_int(144000000), .db_name = "from",  .pk = 0},
        .to    = {.val = subject_create_int(150000000), .db_name = "to",    .pk = 0},
        .shift = {.val = subject_create_int(116000000), .db_name = "shift", .pk = 0},
    };
    cfg_transverters[1] = (cfg_transverter_t){
        .from  = {.val = subject_create_int(432000000), .db_name = "from",  .pk = 1},
        .to    = {.val = subject_create_int(438000000), .db_name = "to",    .pk = 1},
        .shift = {.val = subject_create_int(404000000), .db_name = "shift", .pk = 1},
    };
    /* Load values from table */
    cfg_arr = make_params_pointers_array((cfg_item_t *)&cfg_transverters, cfg_arr_size);
    init_items(cfg_arr, cfg_arr_size, cfg_transverter_load_item, cfg_transverter_save_item);
    load_items_from_db(cfg_arr, cfg_arr_size);
}

int32_t cfg_transverter_get_shift(int32_t freq) {
    if (cfg_transverters[0].from.dirty == NULL) {
        LV_LOG_USER("Can't check transverter freq, not loaded yet");
        return 0;
    }
    for (size_t i = 0; i < TRANSVERTER_NUM; i++) {
        if ((subject_get_int(cfg_transverters[i].from.val) <= freq) &&
            (subject_get_int(cfg_transverters[i].to.val) >= freq)) {
            return subject_get_int(cfg_transverters[i].shift.val);
        }
    }
    return 0;
}

static int cfg_transverter_load_item(cfg_item_t *item) {
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
        subject_set_int(item->val, sqlite3_column_int(stmt, 0));
        rc = 0;
    } else {
        LV_LOG_WARN("No results for load from transverter with name: %s and id: %i", item->db_name, item->pk);
        // Save with default value
        cfg_transverter_save_item(item);
        rc = -1;
    }
    sqlite3_reset(stmt);
    sqlite3_clear_bindings(stmt);
    pthread_mutex_unlock(&read_mutex);
    return rc;
}

static int cfg_transverter_save_item(cfg_item_t *item) {
    int           rc;
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
        LV_LOG_ERROR("Failed to bind transverter id %i: %s", item->pk, sqlite3_errmsg(db));
        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);
        pthread_mutex_unlock(&write_mutex);
        return rc;
    }
    rc = sqlite3_bind_int(stmt, sqlite3_bind_parameter_index(stmt, ":val"), subject_get_int(item->val));
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

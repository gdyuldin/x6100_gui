/**
 * Work with params table on DB
 */
#include "db.private.h"

#include "../lvgl/lvgl.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <charconv>

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

// Common sqlite3 code
class StmtResetGuard {
    sqlite3_stmt* stmt_;
    std::unique_lock<std::mutex> lock;
public:
    explicit StmtResetGuard(std::mutex &mux, sqlite3_stmt* stmt) : stmt_(stmt), lock(mux) {}

    ~StmtResetGuard() {
        if (stmt_) {
            sqlite3_reset(stmt_);
            sqlite3_clear_bindings(stmt_);
        }
    }
    // Delete copy constructor and assignment operator to prevent copying
    StmtResetGuard(const StmtResetGuard&) = delete;
    StmtResetGuard& operator=(const StmtResetGuard&) = delete;
};

// Helper trait that is always false, but depends on T
template <typename>
inline constexpr bool always_false_v = false;

// value to std::string converter for logging
template <typename T>
std::string value_to_string(const T& value) {
    if constexpr (std::is_same_v<T, int32_t>) {
        std::array<char, 12> buf{};  // enough for 32‑bit int
        auto [ptr, ec] = std::to_chars(buf.data(), buf.data() + buf.size(), value);
        return std::string(buf.data(), ptr);
    } else if constexpr (std::is_same_v<T, float>) {
        // std::to_chars for float is available in C++17, but you may need to handle precision
        std::array<char, 32> buf{};
        auto [ptr, ec] = std::to_chars(buf.data(), buf.data() + buf.size(), value);
        return std::string(buf.data(), ptr);
    } else if constexpr (std::is_same_v<T, std::string>) {
        return value;
    } else {
        static_assert(always_false_v<T>, "Unsupported type for logging");
    }
}

template <typename T>
struct ParamLoadResult {
    T value;
    int rc;  // load_save_error_codes_t (negative) or sqlite3 rc (positive)
};


template <typename T> ParamLoadResult<T> cfg_param_load(const char *name) {
    int            rc;
    StmtResetGuard guard(read_mutex, read_stmt);

    rc = sqlite3_bind_text(read_stmt, sqlite3_bind_parameter_index(read_stmt, ":name"),
                           name, strlen(name), SQLITE_STATIC);
    if (rc != SQLITE_OK) {
        LV_LOG_ERROR("Failed to bind name %s: %s", name, sqlite3_errmsg(db));
        return {T{}, rc};
    }

    rc = sqlite3_step(read_stmt);
    if (rc == SQLITE_ROW) {
        T value;
        if constexpr (std::is_same_v<T, int32_t>) {
            value = sqlite3_column_int(read_stmt, 0);
            LV_LOG_USER("Loaded %s=%i", name, value);
        }
        else if constexpr (std::is_same_v<T, float>) {
            value = sqlite3_column_double(read_stmt, 0);
            LV_LOG_USER("Loaded %s=%f", name, value);
        }
        else if constexpr (std::is_same_v<T, std::string>) {
            value = reinterpret_cast<const char *>(sqlite3_column_text(read_stmt, 0));
            LV_LOG_USER("Loaded %s=%s", name, value);
        }
        else {
            static_assert(always_false_v<T>, "Unsupported type passed to cfg_param_load().");
        }
        return {value, SUCCESS};
    }
    else {
        LV_LOG_WARN("No results for load %s", name);
        return {T{}, NOT_FOUND};
    }
}

template <typename T> int cfg_param_save(const char *name, const T &value) {
    int rc;
    StmtResetGuard guard(write_mutex, insert_stmt);
    pthread_mutex_lock(&write_mutex);

    rc = sqlite3_bind_text(insert_stmt, sqlite3_bind_parameter_index(insert_stmt, ":name"), name,
                           strlen(name), 0);
    if (rc != SQLITE_OK) {
        LV_LOG_WARN("Can't bind name %s to save params query", name);
        return rc;
    }

    int     val_index = sqlite3_bind_parameter_index(insert_stmt, ":val");

    if constexpr (std::is_same_v<T, int32_t>) {
        rc      = sqlite3_bind_int(insert_stmt, val_index, value);
    }
    else if constexpr (std::is_same_v<T, float>) {
        rc      = sqlite3_bind_double(insert_stmt, val_index, value);
    }
    else if constexpr (std::is_same_v<T, std::string>) {
        rc = sqlite3_bind_text(insert_stmt, val_index, value.c_str(), -1, 0);
    }
    else {
        static_assert(always_false_v<T>, "Unsupported type passed to cfg_param_save().");
    }

    if (rc != SQLITE_OK) {
        LV_LOG_WARN("Can't bind val %s to save params query", value_to_string(value).c_str());
    } else {
        rc = sqlite3_step(insert_stmt);
        if (rc != SQLITE_DONE) {
            LV_LOG_ERROR("Failed save item %s: %s", name, sqlite3_errmsg(db));
        } else {
            LV_LOG_USER("Saved %s=%s", name, value_to_string(value).c_str());
            rc = SUCCESS;
        }
    }
    return rc;
}


int cfg_params_load_item_int(cfg_item_t *item) {
    if (subject_get_dtype(item->val) != DTYPE_INT) {
        LV_LOG_WARN("Wrong item %s dtype: %u, can't load", item->db_name, subject_get_dtype(item->val));
        return WRONG_TYPE;
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
        rc = SUCCESS;
    } else {
        LV_LOG_WARN("No results for load %s", item->db_name);
        rc = NOT_FOUND;
    }
    sqlite3_reset(read_stmt);
    sqlite3_clear_bindings(read_stmt);
    pthread_mutex_unlock(&read_mutex);
    return rc;
}

int cfg_params_load_item_uint64(cfg_item_t *item) {
    if (subject_get_dtype(item->val) != DTYPE_UINT64) {
        LV_LOG_WARN("Wrong item %s dtype: %u, can't load", item->db_name, subject_get_dtype(item->val));
        return WRONG_TYPE;
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
        rc = SUCCESS;
    } else {
        LV_LOG_WARN("No results for load %s", item->db_name);
        rc = NOT_FOUND;
    }
    sqlite3_reset(read_stmt);
    sqlite3_clear_bindings(read_stmt);
    pthread_mutex_unlock(&read_mutex);
    return rc;
}

int cfg_params_load_item_float(cfg_item_t *item) {
    if (subject_get_dtype(item->val) != DTYPE_FLOAT) {
        LV_LOG_WARN("Wrong item %s dtype: %u, can't load", item->db_name, subject_get_dtype(item->val));
        return WRONG_TYPE;
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
        rc = SUCCESS;
    } else {
        LV_LOG_WARN("No results for load %s", item->db_name);
        rc = NOT_FOUND;
    }
    sqlite3_reset(read_stmt);
    sqlite3_clear_bindings(read_stmt);
    pthread_mutex_unlock(&read_mutex);
    return rc;
}

int cfg_params_load_item_str(cfg_item_t *item) {
    if (subject_get_dtype(item->val) != DTYPE_STR) {
        LV_LOG_WARN("Wrong item %s dtype: %u, can't load", item->db_name, subject_get_dtype(item->val));
        return WRONG_TYPE;
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
        text_val = (const char *)sqlite3_column_text(read_stmt, 0);
        LV_LOG_USER("Loaded %s=%s (pk=%i)", item->db_name, text_val, item->pk);
        if (item->db_to_val) {
            char *processed_val = (char *)item->db_to_val((void*)text_val, item->val);
            subject_set_text(item->val, processed_val);
            free(processed_val);
        } else {
            subject_set_text(item->val, text_val);
        }
        rc = SUCCESS;
    } else {
        LV_LOG_WARN("No results for load %s", item->db_name);
        rc = NOT_FOUND;
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
        return WRONG_TYPE;
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
            rc = SUCCESS;
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
        return WRONG_TYPE;
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
            rc = SUCCESS;
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
        return WRONG_TYPE;
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
            rc = SUCCESS;
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
        return WRONG_TYPE;
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
            rc = SUCCESS;
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

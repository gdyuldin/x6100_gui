#include "cfg.private.h"

#include "band.private.h"
#include "params.private.h"
#include "subjects.private.h"

#include "../util.h"
#include "../lvgl/lvgl.h"
#include <aether_radio/x6100_control/control.h>

#include <stdio.h>
#include <stdlib.h>

cfg_t      cfg;
cfg_band_t cfg_band;

static band_info_t cur_band_info;

static int init_params_cfg(sqlite3 *db);
static int init_band_cfg(sqlite3 *db);

static void  init_items(cfg_item_t *cfg_arr, uint32_t count);
static int   load_items_from_db(cfg_item_t *cfg_arr, uint32_t count);
static void  save_items_to_db(cfg_item_t *cfg_arr, uint32_t cfg_size);
static void *params_save_thread(void *arg);

static void on_item_change(subject_t subj, void *user_data);
static void on_band_id_change(subject_t subj, void *user_data);
static void on_freq_change(subject_t subj, void *user_data);

/*

select band (on cfg->band_id change)
* save cur band params using pk (check that freq is within range)
* update pk from band_id
* load band params (if band_id != -1)

 */

int cfg_init(sqlite3 *db) {
    int rc;

    rc = init_params_cfg(db);
    if (rc != 0) {
        LV_LOG_ERROR("Error during loading params");
        return rc;
    }
    rc = init_band_cfg(db);
    if (rc != 0) {
        LV_LOG_ERROR("Error during loading band params");
        return rc;
    }

    pthread_t thread;
    pthread_create(&thread, NULL, params_save_thread, NULL);
    pthread_detach(thread);

// #define TEST_CFG
#ifdef TEST_CFG
    #include "test_cfg.c"
    test();
#endif
    return rc;
}

/**
 * Delayed save of item
 */
static void on_item_change(subject_t subj, void *user_data) {
    cfg_item_t *item = (cfg_item_t *)user_data;
    pthread_mutex_lock(&item->dirty->mux);
    item->dirty->val = true;
    pthread_mutex_unlock(&item->dirty->mux);
}

/**
 * Change freq/mode on changing band
 */
static void on_band_id_change(subject_t subj, void *user_data) {
    if (cfg_band.vfo_a.freq.dirty == NULL) {
        LV_LOG_USER("Skip updating cfg_band, not initialized");
        return;
    }
    cfg_item_t *cfg_band_arr;
    cfg_band_arr = (cfg_item_t *)&cfg_band;
    uint32_t cfg_band_size = sizeof(cfg_band) / sizeof(cfg_item_t);

    LV_LOG_INFO("Save band params for %u", cfg_band_arr[0].pk);
    save_items_to_db(cfg_band_arr, cfg_band_size);
    int32_t new_band_id = subject_get_int(subj);
    if (new_band_id == BAND_UNDEFINED)
        return;
    for (size_t i = 0; i < cfg_band_size; i++) {
        cfg_band_arr[i].pk = new_band_id;
    }
    LV_LOG_INFO("Load band params for %u", cfg_band_arr[0].pk);
    load_items_from_db(cfg_band_arr, cfg_band_size);
}

/**
 * On changing freq
 */
static void on_freq_change(subject_t subj, void *user_data) {
    /**
     * change freq (on cfg_band->freq change):
        * check for cross band
        * find band
        * save new band vfo (a/b) and freq from current values
        * select band (update cfg->band_id)
     */
    cfg_item_t *cfg_band_arr;
    cfg_band_arr = (cfg_item_t *)&cfg_band;
    uint32_t cfg_band_size = sizeof(cfg_band) / sizeof(cfg_item_t);

    cfg_item_t  *item = (cfg_item_t *)user_data;
    uint32_t     freq = subject_get_int(subj);
    band_info_t *band_info = get_band_info_by_freq(freq);
    LV_LOG_INFO("Loaded band info: %i for freq: %lu (pk = %i, name: %s)", band_info->id, freq, item->pk, item->db_name);

    if ((band_info->id != item->pk) && (band_info->id != BAND_UNDEFINED)) {
        x6100_vfo_t vfo = subject_get_int(cfg_band.vfo.val);
        if (((vfo == X6100_VFO_A) && (item->db_name == "vfoa_freq")) || (vfo == X6100_VFO_B) && (item->db_name == "vfob_freq")) {
            LV_LOG_INFO("Save new freq and vfo with target band_id");
            item->pk = band_info->id;
            item->save(item);
            cfg_band.vfo.pk = band_info->id;
            cfg_band.vfo.save(&cfg_band.vfo);
            subject_set_int(cfg.band_id.val, band_info->id);
        }
    }
}

/**
 * Init cfg items
 */
static void init_items(cfg_item_t *cfg_arr, uint32_t count) {
    for (size_t i = 0; i < count; i++) {
        cfg_arr[i].dirty = malloc(sizeof(*cfg_arr[i].dirty));
        cfg_arr[i].dirty->val = false;
        pthread_mutex_init(&cfg_arr[i].dirty->mux, NULL);
        observer_t o = subject_add_observer(cfg_arr[i].val, on_item_change, &cfg_arr[i]);
    }
}
/**
 * Load items from db
 */
static int load_items_from_db(cfg_item_t *cfg_arr, uint32_t count) {
    int rc;
    for (size_t i = 0; i < count; i++) {
        LV_LOG_INFO("Loading %s from db", cfg_arr[i].db_name);
        rc = cfg_arr[i].load(&cfg_arr[i]);
        if (rc != 0) {
            LV_LOG_USER("Can't load %s", cfg_arr[i].db_name);
        }
        cfg_arr[i].dirty->val = false;
    }
    return rc;
}

/**
 * Save items to db
 */
static void save_items_to_db(cfg_item_t *cfg_arr, uint32_t cfg_size) {
    int rc;
    for (size_t i = 0; i < cfg_size; i++) {
        pthread_mutex_lock(&cfg_arr[i].dirty->mux);
        if (cfg_arr[i].dirty->val) {
            rc = cfg_arr[i].save(&cfg_arr[i]);
            if (rc != 0) {
                LV_LOG_USER("Can't save %s", cfg_arr[i].db_name);
            }
            cfg_arr[i].dirty->val = false;
        }
        pthread_mutex_unlock(&cfg_arr[i].dirty->mux);
    }
}

/**
 * Save thread
 */
static void *params_save_thread(void *arg) {
    cfg_item_t *cfg_arr;
    cfg_arr = (cfg_item_t *)&cfg;
    uint32_t cfg_size = sizeof(cfg) / sizeof(cfg_item_t);

    cfg_item_t *cfg_band_arr;
    cfg_band_arr = (cfg_item_t *)&cfg_band;
    uint32_t cfg_band_size = sizeof(cfg_band) / sizeof(cfg_item_t);

    while (true) {
        save_items_to_db(cfg_arr, cfg_size);
        save_items_to_db(cfg_band_arr, cfg_band_size);
        sleep_usec(500000);
    }
}

/**
 * Initialization functions
 */
static int init_params_cfg(sqlite3 *db) {
    /* Init db modules */
    cfg_params_init(db);

    /* Fill configuration */
    cfg.key_tone = (cfg_item_t){
        .val = subject_create_int(700),
        .db_name = "key_tone",
        .load = cfg_params_load_item,
        .save = cfg_params_save_item,
    };
    cfg.vol = (cfg_item_t){
        .val = subject_create_int(20),
        .db_name = "vol",
        .load = cfg_params_load_item,
        .save = cfg_params_save_item,
    };
    cfg.band_id = (cfg_item_t){
        .val = subject_create_int(5),
        .db_name = "band",
        .load = cfg_params_load_item,
        .save = cfg_params_save_item,
    };

    /* Bind callbacks */
    subject_add_observer(cfg.band_id.val, on_band_id_change, NULL);

    /* Load values from table */
    cfg_item_t *cfg_arr = (cfg_item_t *)&cfg;
    uint32_t    cfg_size = sizeof(cfg) / sizeof(*cfg_arr);
    init_items(cfg_arr, cfg_size);
    return load_items_from_db(cfg_arr, cfg_size);
}

static int init_band_cfg(sqlite3 *db) {
    int rc;

    cfg_band_params_init(db);

    band_info_t *band_info = get_band_info_by_pk(subject_get_int(cfg.band_id.val));
    uint32_t     default_freq = 14000000;
    if (band_info) {
        default_freq = (band_info->start_freq + band_info->stop_freq) / 2;
    }

    /* Fill band configuration */
    int32_t band_id = subject_get_int(cfg.band_id.val);

    cfg_band.vfo_a.freq = (cfg_item_t){
        .val = subject_create_int(default_freq),
        .db_name = "vfoa_freq",
        .load = cfg_band_params_load_item,
        .save = cfg_band_params_save_item,
        .pk = band_id,
    };
    cfg_band.vfo_b.freq = (cfg_item_t){
        .val = subject_create_int(default_freq),
        .db_name = "vfob_freq",
        .load = cfg_band_params_load_item,
        .save = cfg_band_params_save_item,
        .pk = band_id,
    };
    cfg_band.vfo = (cfg_item_t){
        .val = subject_create_int(X6100_VFO_A),
        .db_name = "vfo",
        .load = cfg_band_params_load_item,
        .save = cfg_band_params_save_item,
        .pk = band_id,
    };

    subject_add_observer(cfg_band.vfo_a.freq.val, on_freq_change, &cfg_band.vfo_a.freq);
    subject_add_observer(cfg_band.vfo_b.freq.val, on_freq_change, &cfg_band.vfo_b.freq);

    /* Load values from table */
    cfg_item_t *cfg_arr = (cfg_item_t *)&cfg_band;
    uint32_t    cfg_size = sizeof(cfg_band) / sizeof(*cfg_arr);
    init_items(cfg_arr, cfg_size);
    return load_items_from_db(cfg_arr, cfg_size);
}

#include "cfg.h"

#include "params.private.h"
#include "subjects.private.h"

#include "../util.h"

cfg_t cfg;

static void * params_save_thread(void *arg);
static void on_item_change(subject_t subj, void *user_data);

int cfg_init(sqlite3 *db) {
    int rc;

    /* Init db modules */
    rc = cfg_params_init(db);
    if (rc != 0) {
        return rc;
    }

    /* Fill configuration */
    cfg.key_tone    = (cfg_item_t){.val = subject_create_int(700), .db_name = "key_tone"};
    cfg.vol         = (cfg_item_t){.val = subject_create_int(20), .db_name = "vol"};


    /* Load values from table */
    cfg_item_t *cfg_arr;
    cfg_arr = (cfg_item_t *)&cfg;
    uint32_t cfg_size = sizeof(cfg_t) / sizeof(cfg_item_t);
    for (size_t i = 0; i < cfg_size; i++) {
        rc = cfg_params_load_item(&cfg_arr[i]);
        if (rc != 0) {
            break;
        }
        pthread_mutex_init(&cfg_arr[i].ditry_mux, NULL);
        observer_t o = subject_add_observer(cfg_arr[i].val, on_item_change, &cfg_arr[i]);
    }

    pthread_t thread;
    pthread_create(&thread, NULL, params_save_thread, NULL);
    pthread_detach(thread);
    return rc;
}

#include <stdio.h>
static void * params_save_thread(void *arg) {
    cfg_item_t *cfg_arr;
    cfg_arr = (cfg_item_t *)&cfg;
    uint32_t cfg_size = sizeof(cfg_t) / sizeof(cfg_item_t);

    while (true) {
        for (size_t i = 0; i < cfg_size; i++) {
            pthread_mutex_lock(&cfg_arr[i].ditry_mux);
            if (cfg_arr[i].dirty) {
                cfg_params_save_item(&cfg_arr[i]);
                cfg_arr[i].dirty = false;
            }
            pthread_mutex_unlock(&cfg_arr[i].ditry_mux);
        }
        sleep_usec(100000);
    }
}

static void on_item_change(subject_t subj, void *user_data) {
    cfg_item_t *item = (cfg_item_t *)user_data;
    pthread_mutex_lock(&item->ditry_mux);
    item->dirty = true;
    pthread_mutex_unlock(&item->ditry_mux);
}

#pragma once

#include "cfg.h"

#ifdef __cplusplus
extern "C" {
#endif

#include <pthread.h>
#include <sqlite3.h>

enum item_state_t {
    ITEM_STATE_CLEAN,
    ITEM_STATE_CHANGED,
    ITEM_STATE_LOADING,
};

struct dirty_t {
    enum item_state_t val;
    pthread_mutex_t   mux;
};

void init_items(cfg_item_t *cfg_arr, uint32_t count, int (*load)(struct cfg_item_t *item),
                int (*save)(struct cfg_item_t *item));
int  load_items_from_db(cfg_item_t *cfg_arr, uint32_t count);
void save_item_to_db(cfg_item_t *item, bool force);
void save_items_to_db(cfg_item_t *cfg_arr, uint32_t cfg_size);

void fill_cfg_item_int(cfg_item_t *item, Subject * val, const char * db_name);
void fill_cfg_item_uint64(cfg_item_t *item, Subject * val, const char * db_name);
void fill_cfg_item_float(cfg_item_t *item, Subject * val, float db_scale, const char * db_name);
void fill_cfg_item_text(cfg_item_t *item, Subject * val, const char * db_name);


#ifdef __cplusplus
}
#endif

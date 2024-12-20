#pragma once

#include "cfg.h"

#define BAND_UNDEFINED -1

#include <sqlite3.h>

typedef struct {
    int32_t  id;
    char    *name;
    uint32_t start_freq;
    uint32_t stop_freq;
    int32_t  active;
} band_info_t;

void cfg_band_params_init(sqlite3 *db);

band_info_t *get_band_info_by_pk(int32_t band_id);
band_info_t *get_band_info_by_freq(uint32_t freq);

int cfg_band_params_load_item(cfg_item_t *item);

int cfg_band_params_save_item(cfg_item_t *item);

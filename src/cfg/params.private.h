#pragma once

#include "cfg.h"

#include <sqlite3.h>

typedef enum {
    SUCCESS = 0,
    WRONG_TYPE = -1,
    NOT_FOUND = -2,
} load_save_error_codes_t;

void cfg_params_init(sqlite3 *db);

int cfg_params_load_item_int(cfg_item_t *item);
int cfg_params_load_item_uint64(cfg_item_t *item);
int cfg_params_load_item_float(cfg_item_t *item);
int cfg_params_load_item_str(cfg_item_t *item);

int cfg_params_save_item_int(cfg_item_t *item);
int cfg_params_save_item_uint64(cfg_item_t *item);
int cfg_params_save_item_float(cfg_item_t *item);
int cfg_params_save_item_str(cfg_item_t *item);

#pragma once

#include "cfg.h"

#include <sqlite3.h>

void cfg_params_init(sqlite3 *db);

int cfg_params_load_item(cfg_item_t *item);

int cfg_params_save_item(cfg_item_t *item);

float cfg_float_val_from_db(cfg_item_t *item, sqlite3_stmt *stmt, int iCol);
int cfg_float_val_to_db(cfg_item_t *item, sqlite3_stmt *stmt, int iCol, float* val);

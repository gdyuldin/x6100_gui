#pragma once

#include "cfg.h"

#include <sqlite3.h>

void cfg_params_init(sqlite3 *db);

int cfg_params_load_item(cfg_item_t *item);

int cfg_params_save_item(cfg_item_t *item);

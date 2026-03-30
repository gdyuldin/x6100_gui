#pragma once

#include "mode.h"

#include "cfg.h"
#include <sqlite3.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MODE_UNDEFINED -1

#include <aether_radio/x6100_control/control.h>

typedef int32_t db_mode_t;

typedef struct {
    cfg_item_t filter_low;
    cfg_item_t filter_high;
    cfg_item_t freq_step;
    cfg_item_t zoom;
} cfg_mode_t;

extern cfg_mode_t cfg_mode;

void cfg_mode_params_init(sqlite3 *database);
int cfg_mode_params_load_item(cfg_item_t *item);
int cfg_mode_params_save_item(cfg_item_t *item);
db_mode_t xmode_2_db(x6100_mode_t mode);
bool mode_default_values(db_mode_t mode, uint32_t *low, uint32_t *high, uint32_t *step, uint32_t *zoom);

#ifdef __cplusplus
}
#endif

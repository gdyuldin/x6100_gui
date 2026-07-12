#pragma once

#include "band.h"

#include <sqlite3.h>

#define BAND_UNDEFINED -1

extern cfg_band_t cfg_band;

void cfg_band_params_init(sqlite3 *db);

band_info_t *get_band_info_by_pk(int32_t band_id);
band_info_t *get_band_info_by_freq(uint32_t freq);
band_info_t *get_band_info_next(uint32_t freq, bool up, int32_t cur_id);

void cfg_band_params_save_all();
void cfg_band_params_change_pk(int32_t pk);
void cfg_band_params_load_all();
void cfg_band_params_load_all_except_freq(x6100_vfo_t vfo);
int cfg_band_params_load_item(cfg_item_t *item);
int cfg_band_params_save_item(cfg_item_t *item);

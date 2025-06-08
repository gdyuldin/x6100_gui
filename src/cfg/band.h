#pragma once

#include "common.h"

#include <aether_radio/x6100_control/control.h>

typedef struct {
    int32_t  id;
    char    *name;
    uint32_t start_freq;
    uint32_t stop_freq;
    int32_t  active;
} band_info_t;

struct vfo_params {
    cfg_item_t freq;
    cfg_item_t mode;
    cfg_item_t agc;
    cfg_item_t pre;
    cfg_item_t att;
};

typedef struct {
    struct vfo_params vfo_a;
    struct vfo_params vfo_b;
    cfg_item_t        vfo;
    cfg_item_t        split;
    cfg_item_t        rfg;
    cfg_item_t        output_gain;
    struct {
        cfg_item_t min;
        cfg_item_t max;
    } grid;
} cfg_band_t;

void cfg_band_set_freq_for_vfo(x6100_vfo_t vfo, int32_t freq);

void        cfg_band_vfo_copy();
void        cfg_band_load_next(bool up);
const char *cfg_band_label_get();
uint32_t    cfg_band_read_all_bands(band_info_t **results, int32_t *cap);

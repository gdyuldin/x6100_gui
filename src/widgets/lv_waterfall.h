/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

#ifndef LV_WATERFALL_H
#define LV_WATERFALL_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/

#include "lvgl/lvgl.h"

#include <time.h>

/**********************
 *      TYPEDEFS
 **********************/

typedef struct {
    lv_img_t        obj;
    lv_img_dsc_t    *dsc;

    uint32_t        line_len;
    uint8_t         *line_buf;

    lv_color_t      *palette;
    uint16_t        palette_cnt;

    int16_t         min;
    int16_t         max;

    struct timespec frame_ts;   /* wall-clock timestamp of most recent PSD frame */
} lv_waterfall_t;

extern const lv_obj_class_t lv_waterfall_class;

/**********************
 * GLOBAL PROTOTYPES
 **********************/

lv_obj_t * lv_waterfall_create(lv_obj_t * parent);

/*=====================
 * Setter functions
 *====================*/

void lv_waterfall_set_palette(lv_obj_t * obj, lv_color_t * palette, uint16_t cnt);
void lv_waterfall_set_size(lv_obj_t * obj, lv_coord_t w, lv_coord_t h);

void lv_waterfall_clear_data(lv_obj_t * obj);
void lv_waterfall_add_data(lv_obj_t * obj, float * data, uint16_t cnt);

/* Add data with wall-clock timestamp for time-aligned processing (e.g. DNF). */
void lv_waterfall_add_data_with_ts(lv_obj_t * obj, float * data, uint16_t cnt, struct timespec ts);

/* Return the timestamp of the most recent PSD frame, or {0,0} if none yet. */
struct timespec lv_waterfall_get_frame_ts(lv_obj_t * obj);

/* Insert a full-width marker line (scrolls like normal data). */
void lv_waterfall_add_marker_line(lv_obj_t * obj, lv_color_t color);

void lv_waterfall_set_min(lv_obj_t *obj, int16_t val);
void lv_waterfall_set_max(lv_obj_t *obj, int16_t val);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif

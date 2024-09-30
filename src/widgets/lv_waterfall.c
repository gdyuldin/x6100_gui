/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

/*********************
 *      INCLUDES
 *********************/

#include "lv_waterfall.h"

/*********************
 *      DEFINES
 *********************/
#define MY_CLASS &lv_waterfall_class

/**********************
 *  STATIC PROTOTYPES
 **********************/

static void lv_waterfall_constructor(const lv_obj_class_t * class_p, lv_obj_t * obj);
static void lv_waterfall_destructor(const lv_obj_class_t * class_p, lv_obj_t * obj);

/**********************
 *  STATIC VARIABLES
 **********************/

const lv_obj_class_t lv_waterfall_class  = {
    .constructor_cb = lv_waterfall_constructor,
    .destructor_cb = lv_waterfall_destructor,
    .base_class = &lv_img_class,
    .instance_size = sizeof(lv_waterfall_t),
};

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

lv_obj_t * lv_waterfall_create(lv_obj_t * parent) {
    LV_LOG_USER("begin");
    lv_obj_t * obj = lv_obj_class_create_obj(MY_CLASS, parent);
    lv_obj_class_init_obj(obj);

    return obj;
}

/*=====================
 * Setter functions
 *====================*/

void lv_waterfall_set_palette(lv_obj_t * obj, lv_color_t * palette, uint16_t cnt) {
    LV_ASSERT_OBJ(obj, MY_CLASS);
    LV_ASSERT_NULL(palette);

    lv_waterfall_t * waterfall = (lv_waterfall_t *)obj;

    waterfall->palette = lv_mem_realloc(waterfall->palette, cnt * sizeof(waterfall->palette[0]));
    waterfall->palette_cnt = cnt;

    memcpy(waterfall->palette, palette, cnt * sizeof(waterfall->palette[0]));
}

void lv_waterfall_set_size(lv_obj_t * obj, lv_coord_t w, lv_coord_t h) {
    LV_ASSERT_OBJ(obj, MY_CLASS);

    lv_obj_set_size(obj, w, h);

    lv_waterfall_t * waterfall = (lv_waterfall_t *)obj;

    waterfall->dsc = lv_img_buf_alloc(w, h, LV_IMG_CF_TRUE_COLOR);
    memset(waterfall->dsc->data, 0, waterfall->dsc->data_size);

    waterfall->line_len = waterfall->dsc->data_size / waterfall->dsc->header.h;
    waterfall->line_buf = lv_mem_realloc(waterfall->line_buf, waterfall->line_len);

    lv_img_set_src(obj, waterfall->dsc);
    lv_img_cache_invalidate_src(waterfall->dsc);
}

void lv_waterfall_clear_data(lv_obj_t * obj) {
    LV_ASSERT_OBJ(obj, MY_CLASS);

    lv_waterfall_t * waterfall = (lv_waterfall_t *)obj;

    memset(waterfall->dsc->data, 0, waterfall->dsc->data_size);
    lv_img_cache_invalidate_src(waterfall->dsc);
}

void lv_waterfall_add_data(lv_obj_t * obj, float * data, uint16_t cnt) {
    LV_ASSERT_OBJ(obj, MY_CLASS);

    lv_waterfall_t  *waterfall = (lv_waterfall_t *)obj;
    lv_img_dsc_t    *dsc = waterfall->dsc;

    if (!dsc || !waterfall->palette) {
        return;
    }

    uint32_t        line_len = waterfall->line_len;

    /* Scroll down */

    memmove(dsc->data + line_len, dsc->data, dsc->data_size - line_len);

    /* Paint */

    for (uint32_t x = 0; x < dsc->header.w; x++) {
        uint32_t    index = x * cnt / dsc->header.w;
        float       d = data[index];
        float       v = (d - waterfall->min) / (waterfall->max - waterfall->min);

        if (v < 0.0f) {
            v = 0.0f;
        } else if (v > 1.0f) {
            v = 1.0f;
        }

        uint8_t id = v * 255;

        lv_img_buf_set_px_color(dsc, x, 0, waterfall->palette[id]);
    }
}

void lv_waterfall_set_min(lv_obj_t * obj, int16_t val) {
    LV_ASSERT_OBJ(obj, MY_CLASS);
    lv_waterfall_t * waterfall = (lv_waterfall_t *)obj;
    waterfall->min = val;
}
void lv_waterfall_set_max(lv_obj_t * obj, int16_t val) {
    LV_ASSERT_OBJ(obj, MY_CLASS);
    lv_waterfall_t * waterfall = (lv_waterfall_t *)obj;
    waterfall->max = val;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static void lv_waterfall_constructor(const lv_obj_class_t * class_p, lv_obj_t * obj) {
    LV_UNUSED(class_p);
    LV_TRACE_OBJ_CREATE("begin");

    lv_waterfall_t * waterfall = (lv_waterfall_t *)obj;

    waterfall->palette = NULL;
    waterfall->palette_cnt = 0;
    waterfall->line_len = 0;
    waterfall->line_buf = NULL;
    waterfall->min = -40;
    waterfall->max = 0;

    LV_TRACE_OBJ_CREATE("finished");
}

static void lv_waterfall_destructor(const lv_obj_class_t * class_p, lv_obj_t * obj) {
    LV_UNUSED(class_p);
    lv_waterfall_t * waterfall = (lv_waterfall_t *)obj;

    if (waterfall->palette) lv_mem_free(waterfall->palette);
    if (waterfall->line_buf) lv_mem_free(waterfall->line_buf);
}

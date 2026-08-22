#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/

#include "lvgl/lvgl.h"

/**********************
 *      TYPEDEFS
 **********************/

typedef struct {
    char *label;
    float val;
} bar_tick_t;

typedef struct {
    float       threshold;
    lv_color_t  color;
} bar_color_threshold_t;

typedef lv_color_t (*bar_color_cb_t)(float);

typedef struct {
    lv_obj_t    obj;

    float       range_min;
    float       range_max;
    float       slice_step;

    float       value;
    float       peak_value;
    bool        peak_enabled;
    lv_color_t  peak_color;

    bar_color_threshold_t *thresholds;
    uint8_t     threshold_count;

    uint8_t     tick_count;
    float      *tick_vals;
    lv_point_t *tick_sizes;
    char      **tick_labels;

    // Color callback
    bar_color_cb_t color_cb;
    lv_color_t     default_color;

    lv_draw_rect_dsc_t  rect_dsc_tmpl;
    lv_draw_label_dsc_t label_dsc_tmpl;

    uint8_t     slice_spacing_px;
} lv_bar_indicator_t;

extern const lv_obj_class_t lv_bar_indicator_class;

/**********************
 * GLOBAL PROTOTYPES
 **********************/

lv_obj_t *lv_bar_indicator_create(lv_obj_t *parent);

void lv_bar_indicator_set_range(lv_obj_t *obj, float min, float max, float slice_step);
void lv_bar_indicator_set_slice_spacing(lv_obj_t *obj, uint8_t px);
// Set ticks (values and labels)
void lv_bar_indicator_set_ticks(lv_obj_t *obj, const bar_tick_t *ticks, uint8_t cnt);

void lv_bar_indicator_set_color_cb(lv_obj_t *obj, bar_color_cb_t cb);
void lv_bar_indicator_set_default_color(lv_obj_t *obj, lv_color_t color);

void lv_bar_indicator_set_peak_enable(lv_obj_t *obj, bool enable);
void lv_bar_indicator_set_peak_color(lv_obj_t *obj, lv_color_t color);
void lv_bar_indicator_set_font(lv_obj_t *obj, const lv_font_t *font);

void lv_bar_indicator_set_value(lv_obj_t *obj, float value);
void lv_bar_indicator_set_peak_value(lv_obj_t *obj, float peak);


#ifdef __cplusplus
}
#endif


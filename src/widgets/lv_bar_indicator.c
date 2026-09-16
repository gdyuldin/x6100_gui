/*********************
 *      INCLUDES
 *********************/

#include "lv_bar_indicator.h"
#include <stdlib.h>
#include <string.h>

/*********************
 *      DEFINES
 *********************/

#define MY_CLASS &lv_bar_indicator_class

/**********************
 *  STATIC PROTOTYPES
 **********************/

static void lv_bar_indicator_constructor(const lv_obj_class_t *class_p, lv_obj_t *obj);
static void lv_bar_indicator_destructor(const lv_obj_class_t *class_p, lv_obj_t *obj);
static void lv_bar_indicator_event(const lv_obj_class_t *class_p, lv_event_t *e);

static void cache_tick_sizes(lv_bar_indicator_t *bar);

/**********************
 *  STATIC VARIABLES
 **********************/

const lv_obj_class_t lv_bar_indicator_class = {
    .constructor_cb = lv_bar_indicator_constructor,
    .destructor_cb = lv_bar_indicator_destructor,
    .base_class = &lv_obj_class,
    .event_cb = lv_bar_indicator_event,
    .instance_size = sizeof(lv_bar_indicator_t),
    .width_def = LV_DPI_DEF * 2,
    .height_def = LV_DPI_DEF / 5,
};

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

lv_obj_t *lv_bar_indicator_create(lv_obj_t *parent) {
    lv_obj_t *obj = lv_obj_class_create_obj(MY_CLASS, parent);
    lv_obj_class_init_obj(obj);

    return obj;
}

void lv_bar_indicator_set_range(lv_obj_t *obj, float min, float max, float slice_step) {
    LV_ASSERT_OBJ(obj, MY_CLASS);

    lv_bar_indicator_t *bar = (lv_bar_indicator_t *)obj;

    bar->range_min = min;
    bar->range_max = max;
    bar->slice_step = slice_step;
}

void lv_bar_indicator_set_ticks(lv_obj_t *obj, const bar_tick_t *ticks, uint8_t cnt) {
    LV_ASSERT_OBJ(obj, MY_CLASS);

    lv_bar_indicator_t *bar = (lv_bar_indicator_t *)obj;

    free(bar->tick_sizes);
    bar->tick_sizes = NULL;
    free(bar->tick_labels);
    bar->tick_labels = NULL;
    free(bar->tick_vals);
    bar->tick_vals = NULL;
    bar->tick_count = 0;

    if (ticks == NULL || cnt == 0) {
        return;
    }

    bar->tick_sizes = malloc(cnt * sizeof(lv_point_t));
    bar->tick_labels = malloc(cnt * sizeof(char *));
    bar->tick_vals = malloc(cnt * sizeof(float));
    if (bar->tick_sizes == NULL || bar->tick_labels == NULL || bar->tick_vals == NULL) {
        free(bar->tick_sizes);
        free(bar->tick_labels);
        free(bar->tick_vals);
        bar->tick_sizes = NULL;
        bar->tick_labels = NULL;
        bar->tick_vals = NULL;
        return;
    }

    for (uint8_t i = 0; i < cnt; i++) {
        bar->tick_labels[i] = ticks[i].label;
        bar->tick_vals[i] = ticks[i].val;
    }
    bar->tick_count = cnt;

    cache_tick_sizes(bar);
}

void lv_bar_indicator_set_color_cb(lv_obj_t *obj, bar_color_cb_t cb) {
    LV_ASSERT_OBJ(obj, MY_CLASS);
    lv_bar_indicator_t *bar = (lv_bar_indicator_t *)obj;
    bar->color_cb = cb;
}

void lv_bar_indicator_set_default_color(lv_obj_t *obj, lv_color_t color) {
    LV_ASSERT_OBJ(obj, MY_CLASS);

    lv_bar_indicator_t *bar = (lv_bar_indicator_t *)obj;

    if (bar->default_color.full == color.full) {
        return;
    }

    bar->default_color = color;
    lv_obj_invalidate(obj);
}

void lv_bar_indicator_set_peak_enable(lv_obj_t *obj, bool enable) {
    LV_ASSERT_OBJ(obj, MY_CLASS);

    lv_bar_indicator_t *bar = (lv_bar_indicator_t *)obj;

    bar->peak_enabled = enable;
}

void lv_bar_indicator_set_peak_color(lv_obj_t *obj, lv_color_t color) {
    LV_ASSERT_OBJ(obj, MY_CLASS);

    lv_bar_indicator_t *bar = (lv_bar_indicator_t *)obj;

    bar->peak_color = color;
}

void lv_bar_indicator_set_font(lv_obj_t *obj, const lv_font_t *font) {
    LV_ASSERT_OBJ(obj, MY_CLASS);

    lv_bar_indicator_t *bar = (lv_bar_indicator_t *)obj;

    bar->label_dsc_tmpl.font = font;

    cache_tick_sizes(bar);
}

void lv_bar_indicator_set_slice_spacing(lv_obj_t *obj, uint8_t px) {
    LV_ASSERT_OBJ(obj, MY_CLASS);

    lv_bar_indicator_t *bar = (lv_bar_indicator_t *)obj;
    bar->slice_spacing_px = px;
}

void lv_bar_indicator_set_value(lv_obj_t *obj, float value) {
    LV_ASSERT_OBJ(obj, MY_CLASS);

    lv_bar_indicator_t *bar = (lv_bar_indicator_t *)obj;

    bar->value = value;
    lv_obj_invalidate(obj);
}

void lv_bar_indicator_set_peak_value(lv_obj_t *obj, float peak) {
    LV_ASSERT_OBJ(obj, MY_CLASS);

    lv_bar_indicator_t *bar = (lv_bar_indicator_t *)obj;

    bar->peak_value = peak;

    if (bar->peak_enabled) {
        lv_obj_invalidate(obj);
    }
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static void lv_bar_indicator_constructor(const lv_obj_class_t *class_p, lv_obj_t *obj) {
    LV_UNUSED(class_p);
    LV_TRACE_OBJ_CREATE("begin");

    lv_bar_indicator_t *bar = (lv_bar_indicator_t *)obj;

    bar->range_min = 0.0f;
    bar->range_max = 10.0f;
    bar->slice_step = 1.0f;

    bar->value = 0.0f;
    bar->peak_value = 0.0f;
    bar->peak_enabled = false;
    bar->peak_color = lv_color_hex(0xAAAAAA);

    bar->default_color = lv_color_hex(0xAAAAAA);
    bar->color_cb = NULL;

    bar->thresholds = NULL;
    bar->threshold_count = 0;

    bar->tick_count = 0;
    bar->tick_vals = NULL;
    bar->tick_sizes = NULL;
    bar->tick_labels = NULL;

    lv_draw_rect_dsc_init(&bar->rect_dsc_tmpl);
    bar->rect_dsc_tmpl.bg_opa = LV_OPA_80;
    bar->rect_dsc_tmpl.border_width = 0;
    bar->rect_dsc_tmpl.radius = 0;

    lv_draw_label_dsc_init(&bar->label_dsc_tmpl);
    bar->label_dsc_tmpl.color = lv_color_white();
    bar->label_dsc_tmpl.font = NULL;

    bar->slice_spacing_px = 1;

    LV_TRACE_OBJ_CREATE("finished");
}

static void lv_bar_indicator_destructor(const lv_obj_class_t *class_p, lv_obj_t *obj) {
    LV_UNUSED(class_p);

    lv_bar_indicator_t *bar = (lv_bar_indicator_t *)obj;

    free(bar->thresholds);
    bar->thresholds = NULL;

    free(bar->tick_sizes);
    bar->tick_sizes = NULL;

    free(bar->tick_labels);
    bar->tick_labels = NULL;

    free(bar->tick_vals);
    bar->tick_vals = NULL;
}

static inline void set_slice_draw_area(lv_area_t *area, lv_coord_t x1, lv_coord_t i, lv_coord_t slice_w, lv_coord_t slice_spacing) {
        area->x1 = x1 + i * slice_w - (slice_w - slice_spacing) / 2;
        area->x2 = area->x1 + slice_w - slice_spacing - 1;
}

static void lv_bar_indicator_event(const lv_obj_class_t *class_p, lv_event_t *e) {
    LV_UNUSED(class_p);

    lv_res_t res = lv_obj_event_base(MY_CLASS, e);

    if (res != LV_RES_OK) {
        return;
    }

    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *obj = lv_event_get_target(e);

    if (code == LV_EVENT_DRAW_MAIN) {
        lv_bar_indicator_t *bar = (lv_bar_indicator_t *)obj;
        lv_draw_ctx_t *draw_ctx = lv_event_get_draw_ctx(e);

        lv_area_t bar_coords;
        lv_obj_get_coords(obj, &bar_coords);

        lv_coord_t transf_w = lv_obj_get_style_transform_width(obj, LV_PART_MAIN);
        lv_coord_t transf_h = lv_obj_get_style_transform_height(obj, LV_PART_MAIN);
        bar_coords.x1 -= transf_w;
        bar_coords.x2 += transf_w;
        bar_coords.y1 -= transf_h;
        bar_coords.y2 += transf_h;
        lv_coord_t barw = lv_area_get_width(&bar_coords);
        lv_coord_t barh = lv_area_get_height(&bar_coords);

        lv_coord_t x1 = bar_coords.x1;
        lv_coord_t y1 = bar_coords.y1;

        if (bar->tick_count > 0 && bar->tick_sizes && bar->tick_labels) {
            x1 += bar->tick_sizes[0].x / 2;
            barw -= bar->tick_sizes[0].x / 2;
        }

        if (barw <= 0) {
            return;
        }

        uint32_t slice_w = barw * bar->slice_step / (bar->range_max - bar->range_min);
        uint32_t slice_spacing = LV_MIN(bar->slice_spacing_px, slice_w - 1);

        lv_area_t area;
        area.y1 = y1;
        area.y2 = area.y1 + barh;

        float v = bar->range_min;
        uint32_t i = 0;
        while ((v <= bar->value) && (v <= bar->range_max)) {
            lv_draw_rect_dsc_t dsc = bar->rect_dsc_tmpl;
            if (bar->color_cb) {
                dsc.bg_color = bar->color_cb(v);
            } else {
                dsc.bg_color = bar->default_color;
            }
            set_slice_draw_area(&area, x1, i, slice_w, slice_spacing);
            if (area.x2 > bar_coords.x2) {
                break;
            }
            lv_draw_rect(draw_ctx, &dsc, &area);
            i++;
            v += bar->slice_step;
        }

        if (bar->peak_enabled && bar->peak_value > bar->value + 1.5f * bar->slice_step) {
            lv_draw_rect_dsc_t dsc = bar->rect_dsc_tmpl;
            dsc.bg_color = bar->peak_color;
            i = (bar->peak_value - bar->range_min) / bar->slice_step;
            set_slice_draw_area(&area, x1, i, slice_w, slice_spacing);
            if (area.x2 < bar_coords.x2) {
                lv_draw_rect(draw_ctx, &dsc, &area);
            }
        }

        if (bar->tick_count > 0 && bar->tick_sizes && bar->tick_labels) {
            // calculate tick position
            area.y1 = y1 + (barh - bar->tick_sizes[0].y) / 2;
            area.y2 = area.y1 + bar->tick_sizes[0].y;
            for (uint8_t i = 0; i < bar->tick_count; i++) {
                lv_coord_t offset = (bar->tick_vals[i] - bar->range_min) * slice_w / bar->slice_step + 0.5f;
                area.x1 = x1 + offset - bar->tick_sizes[i].x / 2 + 1;
                area.x2 = area.x1 + bar->tick_sizes[i].x;
                lv_draw_label(draw_ctx, &bar->label_dsc_tmpl, &area, bar->tick_labels[i], NULL);
            }
        }
    }
}

static void cache_tick_sizes(lv_bar_indicator_t *bar) {
    if (bar->tick_count == 0 || bar->tick_sizes == NULL) {
        return;
    }

    lv_point_t label_size;

    for (uint8_t i = 0; i < bar->tick_count; i++) {
        lv_txt_get_size(&label_size, bar->tick_labels[i], bar->label_dsc_tmpl.font, 0, 0, LV_COORD_MAX, 0);

        bar->tick_sizes[i].x = label_size.x;
        bar->tick_sizes[i].y = label_size.y;
    }
}

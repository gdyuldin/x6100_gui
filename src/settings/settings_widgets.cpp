#include "settings_widgets.h"

#include <algorithm>

#include "../keyboard.h"
#include "../voice.h"

/* Default column template: one label column and six SMALL_1 control columns. */
static const lv_coord_t col_dsc_init[COL_DSC_COUNT] = {
    740 - (SMALL_1 + SMALL_PAD) * 6, SMALL_1, SMALL_1, SMALL_1, SMALL_1, SMALL_1, SMALL_1, LV_GRID_TEMPLATE_LAST};

/***** SETTINGS PAGE *****/

void SettingsPage::reset() {
    destroy();

    std::fill_n(row_dsc, ROW_DSC_COUNT, DEFAULT_HEIGHT);
    std::copy(col_dsc_init, col_dsc_init + COL_DSC_COUNT, col_dsc);
    row = 0;

    grid = lv_obj_create(dialog->obj);
    lv_obj_set_layout(grid, LV_LAYOUT_GRID);
    lv_obj_set_size(grid, 780, 330);
    lv_obj_add_style(grid, &style.text_base_color, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(grid, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_column(grid, SMALL_PAD, 0);
    lv_obj_set_style_pad_row(grid, 5, 0);

    lv_obj_center(grid);
}

void SettingsPage::destroy() {
    if (grid) {
        lv_group_set_editing(keyboard_group, false);
        lv_obj_del(grid);
        grid = nullptr;
    }

    // The bound widgets are gone; drop their records so the deques do not grow
    // on every page switch. Observers are widget-bound too, so they go as well.
    binds.clear();
    slider_int_binds.clear();
    slider_float_binds.clear();
    observers.clear();
}

void SettingsPage::finish() {
    row_dsc[row] = LV_GRID_TEMPLATE_LAST;
    lv_obj_set_grid_dsc_array(grid, col_dsc, row_dsc);
}

uint8_t SettingsPage::delimiter() {
    row_dsc[row] = 10;

    return ++row;
}

lv_obj_t *SettingsPage::label(const char *text) {
    lv_obj_t *obj = lv_label_create(grid);

    lv_label_set_text(obj, text);
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, 0, 1, LV_GRID_ALIGN_CENTER, row, 1);

    return obj;
}

lv_obj_t *SettingsPage::cell(uint8_t col, uint8_t span, lv_coord_t width) {
    lv_obj_t *obj = lv_obj_create(grid);

    lv_obj_set_size(obj, width, 56);
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, col, span, LV_GRID_ALIGN_CENTER, row, 1);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(obj);

    return obj;
}

void SettingsPage::show_row(const row_items_t &items, bool show) {
    if (show) {
        row_dsc[items.row] = items.default_height;
        for (size_t i = 0; i < items.cnt; i++) {
            if (items.items[i])
                lv_obj_clear_flag(items.items[i], LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_set_style_grid_row_dsc_array(grid, row_dsc, 0);
    } else {
        row_dsc[items.row] = 0;
        for (size_t i = 0; i < items.cnt; i++) {
            if (items.items[i])
                lv_obj_add_flag(items.items[i], LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_set_style_grid_row_dsc_array(grid, row_dsc, 0);
    }
}

/***** SHARED UPDATE CALLBACKS *****/

SettingBind *SettingsPage::setting_bind(ParamInt *param, const char *voice, void (*extra)(void)) {
    binds.push_back(SettingBind{param, voice, extra});
    return &binds.back();
}

static void switch_update_cb(lv_event_t *e) {
    lv_obj_t    *obj = lv_event_get_target(e);
    SettingBind *b   = (SettingBind *)lv_event_get_user_data(e);
    bool         val = lv_obj_has_state(obj, LV_STATE_CHECKED);

    b->param->set(val);

    if (b->voice) {
        voice_say_bool(b->voice, val);
    }
    if (b->extra) {
        b->extra();
    }
}

static void spinbox_update_cb(lv_event_t *e) {
    lv_obj_t    *obj = lv_event_get_target(e);
    SettingBind *b   = (SettingBind *)lv_event_get_user_data(e);
    int32_t      val = (int32_t)lv_spinbox_get_value(obj);

    b->param->set(val);

    if (b->voice) {
        voice_say_int(b->voice, val);
    }
}

static void dropdown_update_cb(lv_event_t *e) {
    lv_obj_t    *obj = lv_event_get_target(e);
    SettingBind *b   = (SettingBind *)lv_event_get_user_data(e);
    int32_t      val = (int32_t)lv_dropdown_get_selected(obj);

    b->param->set(val);

    if (b->voice) {
        voice_say_int(b->voice, val);
    }
    if (b->extra) {
        b->extra();
    }
}

/* Integer params keep integer multiply to avoid rounding drift. The label uses
 * label_scale so a page can display a different unit than the stored value. */
static void slider_int_update_cb(lv_event_t *e) {
    lv_obj_t      *obj          = lv_event_get_target(e);
    SliderIntBind *b            = (SliderIntBind *)lv_event_get_user_data(e);
    int32_t        raw          = (int32_t)lv_slider_get_value(obj);
    lv_obj_t      *slider_label = (lv_obj_t *)lv_obj_get_user_data(obj);
    char          *fmt          = (char *)lv_obj_get_user_data(slider_label);

    lv_label_set_text_fmt(slider_label, fmt, raw * b->label_scale);
    b->param->set(raw * b->scale);

    if (b->extra) {
        b->extra();
    }
}

static void slider_float_update_cb(lv_event_t *e) {
    lv_obj_t        *obj          = lv_event_get_target(e);
    SliderFloatBind *b            = (SliderFloatBind *)lv_event_get_user_data(e);
    int32_t          raw          = (int32_t)lv_slider_get_value(obj);
    float            val          = b->transform ? b->transform(raw) : raw * b->scale;
    lv_obj_t        *slider_label = (lv_obj_t *)lv_obj_get_user_data(obj);
    char            *fmt          = (char *)lv_obj_get_user_data(slider_label);

    lv_label_set_text_fmt(slider_label, fmt, val);
    b->param->set(val);

    if (b->extra) {
        b->extra();
    }
}

/***** SHARED FACTORIES *****/

static lv_obj_t *create_switch(SettingsPage &page, lv_obj_t *parent) {
    lv_obj_t *obj = lv_switch_create(parent);

    dialog_item(page.dialog, obj);
    lv_obj_center(obj);

    return obj;
}

lv_obj_t *SettingsPage::switch_bool(lv_obj_t *parent, ParamInt &param, const char *voice) {
    lv_obj_t *obj = create_switch(*this, parent);

    lv_obj_add_event_cb(obj, switch_update_cb, LV_EVENT_VALUE_CHANGED, setting_bind(&param, voice));

    if (param.get()) {
        lv_obj_add_state(obj, LV_STATE_CHECKED);
    }
    return obj;
}

lv_obj_t *SettingsPage::spinbox_int(lv_obj_t *parent, ParamInt &param, int32_t min, int32_t max, const char *voice) {
    lv_obj_t *obj = lv_spinbox_create(parent);

    dialog_item(dialog, obj);

    lv_spinbox_set_value(obj, param.get());
    lv_spinbox_set_range(obj, min, max);

    lv_obj_add_event_cb(obj, spinbox_update_cb, LV_EVENT_VALUE_CHANGED, setting_bind(&param, voice));

    return obj;
}

lv_obj_t *SettingsPage::dropdown_int(lv_obj_t *parent, ParamInt &param, const char *options, const char *voice,
                                     void (*extra)(void)) {
    lv_obj_t *obj = lv_dropdown_create(parent);

    dialog_item(dialog, obj);

    lv_obj_add_event_cb(obj, dropdown_update_cb, LV_EVENT_VALUE_CHANGED, setting_bind(&param, voice, extra));

    lv_obj_t *list = lv_dropdown_get_list(obj);
    lv_obj_add_style(list, &style.dialog.dropdown, 0);

    lv_dropdown_set_options(obj, options);
    lv_dropdown_set_symbol(obj, NULL);

    lv_dropdown_set_selected(obj, param.get());

    return obj;
}

lv_obj_t *SettingsPage::slider_int(lv_obj_t *cell, ParamInt &param, int32_t min, int32_t max, int32_t step,
                                   size_t width, const char *fmt, void (*extra)(void), int32_t label_scale) {
    int32_t label_step = label_scale ? label_scale : step;

    slider_int_binds.push_back(SliderIntBind{&param, step, label_step, extra});
    SliderIntBind *b = &slider_int_binds.back();

    lv_obj_t *slider =
        slider_with_text<int32_t>(cell, (int32_t)param.get(), min, max, step, width, fmt, slider_int_update_cb, b);

    // slider_with_text() formats the initial label with the parameter value;
    // when the display scale differs from the slider step, rewrite it so the
    // initial label matches the value slider_int_update_cb will later produce.
    if (label_step != step) {
        lv_obj_t *slider_label = (lv_obj_t *)lv_obj_get_user_data(slider);
        lv_label_set_text_fmt(slider_label, fmt, ((int32_t)param.get() / step) * label_step);
    }
    return slider;
}

lv_obj_t *SettingsPage::slider_float(lv_obj_t *cell, ParamFloat &param, float min, float max, float step, size_t width,
                                     const char *fmt, float (*transform)(int32_t raw), void (*extra)(void)) {
    slider_float_binds.push_back(SliderFloatBind{&param, step, transform, extra});
    SliderFloatBind *b = &slider_float_binds.back();

    return slider_with_text<float>(cell, param.get(), min, max, step, width, fmt, slider_float_update_cb, b);
}

/***** BACKGROUND PREVIEW *****/

void settings_change_bg_opa_cb(lv_event_t *e) {
    lv_obj_t       *obj        = lv_event_get_target(e);
    lv_event_code_t code       = lv_event_get_code(e);
    SettingsPage   *page       = (SettingsPage *)lv_event_get_user_data(e);
    lv_obj_t       *dialog_obj = page->dialog->obj;

    lv_anim_t bg_fade_anim;
    lv_anim_init(&bg_fade_anim);
    lv_anim_set_exec_cb(&bg_fade_anim,
                        [](void *var, int32_t value) { lv_obj_set_style_bg_img_opa((lv_obj_t *)var, value, 0); });
    lv_anim_set_var(&bg_fade_anim, dialog_obj);
    lv_anim_set_time(&bg_fade_anim, 500);
    lv_opa_t cur_opa = lv_obj_get_style_bg_img_opa(dialog_obj, 0);

    if (((lv_obj_check_type(obj, &lv_slider_class) || lv_obj_check_type(obj, &lv_spinbox_class)) &&
         lv_obj_has_state(obj, LV_STATE_EDITED)) ||
        (lv_obj_check_type(obj, &lv_switch_class) && (code == LV_EVENT_FOCUSED))) {
        if (cur_opa != LV_OPA_60) {
            lv_anim_set_values(&bg_fade_anim, cur_opa, LV_OPA_60);
            lv_anim_start(&bg_fade_anim);
        }
    } else {
        if (cur_opa != LV_OPA_COVER) {
            lv_anim_set_values(&bg_fade_anim, cur_opa, LV_OPA_COVER);
            lv_anim_start(&bg_fade_anim);
        }
    }
}

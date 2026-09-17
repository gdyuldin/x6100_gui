#pragma once

#include <deque>
#include <vector>

extern "C" {
#include "../styles.h"
}

#include "../cfg/cfg_api.h"
#include "../dialog.h"

/* Shared grid metrics */

#define SMALL_PAD 5

#define SMALL_1 57
#define SMALL_2 (SMALL_1 * 2 + SMALL_PAD * 1)
#define SMALL_3 (SMALL_1 * 3 + SMALL_PAD * 2)
#define SMALL_4 (SMALL_1 * 4 + SMALL_PAD * 3)
#define SMALL_5 (SMALL_1 * 5 + SMALL_PAD * 4)
#define SMALL_6 (SMALL_1 * 6 + SMALL_PAD * 5)

#define SMALL_WIDTH 57
#define DEFAULT_HEIGHT 55

/* 7 layout columns plus the LV_GRID_TEMPLATE_LAST terminator. */
#define COL_DSC_COUNT 8
#define ROW_DSC_COUNT 64

typedef struct {
    uint8_t    row;
    lv_obj_t **items;
    uint8_t    cnt;
    lv_coord_t default_height = DEFAULT_HEIGHT;
} row_items_t;

// Binds an LVGL widget event to a cfg ParamInt. `voice` (optional) is spoken on
// user action only — never on DB load / initial state, so a prompt is heard
// only when the user actually changes the setting. `extra` (optional) runs
// after the parameter write (e.g. applying the value to hardware/styles).
struct SettingBind {
    ParamInt   *param;
    const char *voice;
    void (*extra)(void);
};

// Binds a slider to an integer parameter. `scale` converts the raw slider
// position to the parameter value; `label_scale` converts it to the displayed
// value (equal to `scale` unless a page shows a different unit).
struct SliderIntBind {
    ParamInt *param;
    int32_t   scale;
    int32_t   label_scale;
    void (*extra)(void);
};

// Binds a slider to a float parameter. `transform` (optional) maps the raw
// slider position to the parameter value (e.g. audio_set_play_vol); when it is
// null the value is `raw * scale`.
struct SliderFloatBind {
    ParamFloat *param;
    float       scale;
    float (*transform)(int32_t raw);
    void (*extra)(void);
};

// Explicit per-page context. Owns the grid, the row template and every bind /
// observer created while the page is being built, so nothing leaks across page
// switches. Records live in std::deque because widgets keep stable pointers to
// them for the lifetime of the page.
struct SettingsPage {
    lv_obj_t  *grid   = nullptr;
    dialog_t  *dialog = nullptr;
    lv_coord_t col_dsc[COL_DSC_COUNT];
    lv_coord_t row_dsc[ROW_DSC_COUNT];
    uint8_t    row = 0;

    std::deque<SettingBind>     binds;
    std::deque<SliderIntBind>   slider_int_binds;
    std::deque<SliderFloatBind> slider_float_binds;
    std::vector<Subscription>   observers;

    void    reset();     // destroy() + fresh grid, row_dsc filled with DEFAULT_HEIGHT
    void    destroy();   // delete grid, clear binds/sliders/observers
    void    finish();    // terminate row_dsc and push it to the grid
    uint8_t delimiter(); // row_dsc[row] = 10; return ++row

    lv_obj_t *label(const char *text);
    lv_obj_t *cell(uint8_t col, uint8_t span, lv_coord_t width);
    void      show_row(const row_items_t &items, bool show);

    SettingBind *setting_bind(ParamInt *param, const char *voice = nullptr, void (*extra)(void) = nullptr);

    lv_obj_t *switch_bool(lv_obj_t *parent, ParamInt &param, const char *voice = nullptr);
    lv_obj_t *spinbox_int(lv_obj_t *parent, ParamInt &param, int32_t min, int32_t max, const char *voice = nullptr);
    lv_obj_t *dropdown_int(lv_obj_t *parent, ParamInt &param, const char *options, const char *voice = nullptr,
                           void (*extra)(void) = nullptr);
    lv_obj_t *slider_int(lv_obj_t *cell, ParamInt &param, int32_t min, int32_t max, int32_t step, size_t width,
                         const char *fmt, void (*extra)(void) = nullptr, int32_t label_scale = 0);
    lv_obj_t *slider_float(lv_obj_t *cell, ParamFloat &param, float min, float max, float step, size_t width,
                           const char *fmt, float (*transform)(int32_t raw) = nullptr, void (*extra)(void) = nullptr);

    // Generic slider + value label pair. Kept in the header because it is a
    // template; param sliders go through slider_int/slider_float above.
    template <typename T>
    lv_obj_t *slider_with_text(lv_obj_t *cell, T val, T min, T max, T step, size_t width, const char *fmt,
                               lv_event_cb_t cb, void *cb_user_data = nullptr);
};

template <typename T>
lv_obj_t *SettingsPage::slider_with_text(lv_obj_t *cell, T val, T min, T max, T step, size_t width, const char *fmt,
                                         lv_event_cb_t cb, void *cb_user_data) {
    lv_obj_t *obj = lv_slider_create(cell);

    dialog_item(dialog, obj);

    lv_slider_set_mode(obj, LV_SLIDER_MODE_NORMAL);
    lv_slider_set_range(obj, min / step, max / step);
    lv_slider_set_value(obj, val / step, LV_ANIM_OFF);
    lv_obj_set_width(obj, width);

    /* Create a label below the slider */
    lv_obj_t *slider_label = lv_label_create(cell);
    lv_obj_add_style(slider_label, &style.text_base_color, LV_PART_MAIN);
    lv_obj_set_user_data(slider_label, (void *)fmt);
    lv_label_set_text_fmt(slider_label, fmt, val);
    lv_obj_align(slider_label, LV_ALIGN_RIGHT_MID, 12, 0);

    lv_obj_set_user_data(obj, slider_label);

    lv_obj_add_event_cb(obj, cb, LV_EVENT_VALUE_CHANGED, cb_user_data);
    return obj;
}

void make_general_page(SettingsPage &page);
void make_ui_page(SettingsPage &page);
void make_voice_page(SettingsPage &page);
void make_info_page(SettingsPage &page);

/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

#include "styles.h"

#include <stdlib.h>
#include "globals.h"

#include "styles_wf_palette.c"

#define PATH "A:/dev/shm/"

/* Skin API */
typedef struct {
    lv_color_t base_text_color;
    struct {
        lv_color_t fill_color;
        lv_color_t line_color;
    } spectrum;
} skin_t;

const uint32_t *wf_palette;

static uint32_t rng_state=1;

styles_t style;
colors_t colors;

/* Meter colors */
lv_color_t meter_color_noise;
lv_color_t meter_color_s9;
lv_color_t meter_color_s9plus;
lv_color_t meter_color_over;
lv_color_t meter_color_peak;

lv_color_t bg_color;

static lv_img_dsc_t clock_bg_dsc  = {0};
static lv_img_dsc_t info_bg_dsc   = {0};
static lv_img_dsc_t meter_bg_dsc  = {0};
static lv_img_dsc_t tx_info_bg_dsc = {0};
static lv_img_dsc_t button_bg_dsc = {0};
static lv_img_dsc_t dialog_bg_dsc = {0};

static skin_t skin_default;

static void setup_theme_legacy();
static void setup_theme_simple();
static void setup_theme_black();
static void setup_theme_flat();

static lv_color_t color_adjust_hsv_value(lv_color_t base, float scale);

static void setup_skin_default(skin_t *skin);

static void set_skin(skin_t *skin);

void styles_init(themes_t theme) {

    setup_skin_default(&skin_default);
    /* * */
    lv_style_t *s;

    lv_style_init(&style.text_base_color);
    lv_style_init(&style.text_muted_color);

    lv_style_init(&style.background);

    lv_style_init(&style.spectrum);
    lv_style_set_bg_color(&style.spectrum, lv_color_hex(0x000000));
    lv_style_set_bg_opa(&style.spectrum, LV_OPA_COVER);
    lv_style_set_border_width(&style.spectrum, 0);
    lv_style_set_radius(&style.spectrum, 0);
    lv_style_set_width(&style.spectrum, SCREEN_WIDTH);
    lv_style_set_x(&style.spectrum, 0);

    lv_style_init(&style.freq_bounds);
    lv_style_set_text_font(&style.freq_bounds, &mono_30);
    lv_style_set_pad_all(&style.freq_bounds, 3);
    lv_style_set_text_align(&style.freq_bounds, LV_TEXT_ALIGN_CENTER);
    lv_style_set_bg_color(&style.freq_bounds, lv_color_black());
    lv_style_set_bg_opa(&style.freq_bounds, LV_OPA_30);
    lv_style_set_radius(&style.freq_bounds, 5);

    lv_style_init(&style.waterfall);
    lv_style_set_bg_color(&style.waterfall, lv_color_hex(0x000000));
    // lv_style_set_border_color(&style.waterfall, lv_color_hex(0xAAAAAA));
    lv_style_set_border_width(&style.waterfall, 0);
    lv_style_set_radius(&style.waterfall, 0);
    lv_style_set_clip_corner(&style.waterfall, true);
    lv_style_set_width(&style.waterfall, SCREEN_WIDTH);
    lv_style_set_x(&style.waterfall, 0);
    lv_style_set_pad_all(&style.waterfall, 0);

    /* Buttons */
    lv_style_init(&style.btn.base);
    lv_style_set_text_font(&style.btn.base, &sony_30);
    lv_style_set_bg_img_opa(&style.btn.base, LV_OPA_COVER);
    lv_style_set_border_width(&style.btn.base, 0);
    lv_style_set_radius(&style.btn.base, 0);
    // lv_style_set_bg_opa(&style.btn.base, LV_OPA_0);
    lv_style_set_width(&style.btn.base, BTN_WIDTH);
    lv_style_set_height(&style.btn.base, BTN_HEIGHT);

    lv_style_init(&style.btn.active);
    lv_style_set_bg_img_recolor(&style.btn.active, lv_color_hex(0x00FF00));
    lv_style_set_bg_img_recolor_opa(&style.btn.active, LV_OPA_20);

    lv_style_init(&style.btn.disabled);
    lv_style_set_bg_img_recolor(&style.btn.disabled, lv_color_hex(0x000000));
    lv_style_set_bg_img_recolor_opa(&style.btn.disabled, LV_OPA_20);
    lv_style_set_text_color(&style.btn.disabled, lv_color_hex(0x101010));

    s = &style.btn.mark;
    lv_style_set_width(s, 24),
    lv_style_set_height(s, 24),
    lv_style_set_radius(s, 12);
    lv_style_set_bg_color(s, lv_color_hex(0x808080));
    lv_style_set_blend_mode(s, LV_BLEND_MODE_ADDITIVE);
    lv_style_set_outline_width(s, 0);
    lv_style_set_border_width(s, 2);
    lv_style_set_border_color(s, lv_color_hex(0x909090));
    lv_style_set_opa(s, LV_OPA_30);

    s = &style.btn.mark_assigned;
    lv_style_init(s);
    lv_style_set_opa(s, LV_OPA_40);
    lv_style_set_bg_color(s, lv_color_hex(0x80ff80));

    /* Message style */
    lv_style_init(&style.msg);
    lv_style_set_pad_hor(&style.msg, 10);
    lv_style_set_text_font(&style.msg, &sony_38);
    lv_style_set_width(&style.msg, 603);
    // lv_style_set_height(&style.msg, 66);
    lv_style_set_x(&style.msg, SCREEN_WIDTH / 2 - (603 / 2));
    lv_style_set_y(&style.msg, 270);
    lv_style_set_radius(&style.msg, 0);
    lv_style_set_bg_img_opa(&style.msg, LV_OPA_COVER);
    lv_style_set_pad_ver(&style.msg, 20);

    lv_style_init(&style.msg_tiny);
    lv_style_set_text_font(&style.msg_tiny, &sony_60);
    lv_style_set_width(&style.msg_tiny, 324);
    lv_style_set_height(&style.msg_tiny, 66);
    lv_style_set_x(&style.msg_tiny, SCREEN_WIDTH / 2 - (324 / 2));
    lv_style_set_y(&style.msg_tiny, 160 - 66/2 + 36/2);
    lv_style_set_radius(&style.msg_tiny, 0);
    lv_style_set_pad_ver(&style.msg_tiny, 12);

    /* Panel */
    lv_style_init(&style.panels.base);
    lv_style_set_text_font(&style.panels.base, &sony_38);
    lv_style_set_width(&style.panels.base, 795);
    lv_style_set_height(&style.panels.base, 182);
    lv_style_set_x(&style.panels.base, SCREEN_WIDTH / 2 - (795 / 2));
    lv_style_set_y(&style.panels.base, 230);
    lv_style_set_pad_ver(&style.panels.base, 10);
    lv_style_set_pad_hor(&style.panels.base, 10);
    lv_style_set_radius(&style.panels.base, 0);
    lv_style_set_bg_img_opa(&style.panels.base, LV_OPA_COVER);

    lv_style_init(&style.panels.info);
    lv_style_set_align(&style.panels.info, LV_ALIGN_OUT_TOP_LEFT);
    lv_style_set_y(&style.panels.info, -38);
    lv_style_set_text_font(&style.panels.info, &sony_30);
    lv_style_set_text_color(&style.panels.info, lv_color_hex(0x808080));
    lv_style_set_blend_mode(&style.panels.info, LV_BLEND_MODE_ADDITIVE);

    lv_style_init(&style.dialog.base);
    lv_style_set_text_font(&style.dialog.base, &sony_36);
    lv_style_set_width(&style.dialog.base, DIALOG_WIDTH);
    lv_style_set_height(&style.dialog.base, DIALOG_HEIGHT);
    lv_style_set_x(&style.dialog.base, (SCREEN_WIDTH - DIALOG_WIDTH) / 2);
    lv_style_set_y(&style.dialog.base, TOP_BLOCK_SMALL_HEIGHT + DIALOG_SPACING);
    lv_style_set_radius(&style.dialog.base, 0);
    lv_style_set_bg_img_opa(&style.dialog.base, LV_OPA_COVER);
    lv_style_set_pad_ver(&style.dialog.base, 0);
    lv_style_set_pad_hor(&style.dialog.base, 0);

    lv_style_init(&style.dialog.item);
    lv_style_set_bg_opa(&style.dialog.item, LV_OPA_TRANSP);

    lv_style_init(&style.dialog.item_focus);
    lv_style_set_bg_opa(&style.dialog.item_focus, 128);
    lv_style_set_text_color(&style.dialog.item_focus, lv_color_black());
    lv_style_set_border_color(&style.dialog.item_focus, lv_color_white());
    lv_style_set_border_width(&style.dialog.item_focus, 2);

    lv_style_init(&style.dialog.item_edited);
    lv_style_set_bg_opa(&style.dialog.item_edited, LV_OPA_COVER);
    lv_style_set_text_color(&style.dialog.item_edited, lv_color_black());

    lv_style_init(&style.dialog.dropdown);
    lv_style_set_text_font(&style.dialog.dropdown, &sony_30);

    /* Waterfall elements */
    lv_style_init(&style.waterfall_middle_line);
    lv_style_set_line_opa(&style.waterfall_middle_line, LV_OPA_60);
    // DRM overlay can't blend in additive mode. Maybe will move it back to primary
    // lv_style_set_blend_mode(&style.waterfall_middle_line, LV_BLEND_MODE_ADDITIVE);
    lv_style_set_pad_all(&style.waterfall_middle_line, 0);
    lv_style_set_line_width(&style.waterfall_middle_line, 2);

    /* Clock */
    lv_style_init(&style.clock);
    // lv_style_set_align(&style.clock, LV_ALIGN_CENTER);
    // lv_style_set_text_align(&style.clock, LV_TEXT_ALIGN_CENTER);
    lv_style_set_radius(&style.clock, 0);
    lv_style_set_bg_img_opa(&style.clock, LV_OPA_COVER);
    lv_style_set_width(&style.clock, CLOCK_WIDTH);
    lv_style_set_height(&style.clock, TOP_BLOCK_SMALL_HEIGHT);

    /* Knobs */
    lv_style_init(&style.knobs);
    lv_style_set_text_font(&style.knobs, &sony_24);
    lv_style_set_radius(&style.knobs, 8);
    lv_style_set_bg_opa(&style.knobs, LV_OPA_60);
    lv_style_set_bg_color(&style.knobs, lv_color_black());
    lv_style_set_border_width(&style.knobs, 0);
    lv_style_set_pad_hor(&style.knobs, 5);
    lv_style_set_pad_ver(&style.knobs, 3);

    /* Left info */
    lv_style_init(&style.info);
    // lv_style_set_align(&style.info, LV_ALIGN_TOP_LEFT);
    lv_style_set_pad_all(&style.info, 0);
    // lv_style_set_pad_hor(&style.info, 8);
    // lv_style_set_pad_ver(&style.info, 6);
    // lv_style_set_pad_row(&style.info, 0);
    lv_style_set_radius(&style.info, 0);
    lv_style_set_bg_img_opa(&style.info, LV_OPA_COVER);
    lv_style_set_border_width(&style.info, 0);
    lv_style_set_bg_opa(&style.info, LV_OPA_0);
    lv_style_set_width(&style.info, FREQ_INFO_WIDTH);
    lv_style_set_height(&style.info, TOP_BLOCK_SMALL_HEIGHT);

    lv_style_init(&style.info_row);
    lv_style_set_align(&style.info_row, LV_ALIGN_CENTER);
    lv_style_set_radius(&style.info_row, 0);
    lv_style_set_border_width(&style.info_row, 0);
    lv_style_set_bg_opa(&style.info_row, LV_OPA_0);
    lv_style_set_pad_hor(&style.info_row, 2);
    lv_style_set_pad_ver(&style.info_row, 0);
    lv_style_set_pad_column(&style.info_row, 2);

    lv_style_init(&style.info_item);
    lv_style_set_text_font(&style.info_item, &sony_20);
    lv_style_set_pad_ver(&style.info_item, 3);
    lv_style_set_radius(&style.info_item, 0);

    /* Meter */
    lv_style_init(&style.meter);
    lv_style_set_radius(&style.meter, 0);
    lv_style_set_align(&style.meter, LV_ALIGN_TOP_MID);
    lv_style_set_border_width(&style.meter, 0);
    lv_style_set_bg_img_opa(&style.meter, LV_OPA_COVER);
    lv_style_set_bg_opa(&style.meter, LV_OPA_0);
    lv_style_set_width(&style.meter, METER_WIDTH);
    lv_style_set_height(&style.meter, TOP_BLOCK_SMALL_HEIGHT);
    lv_style_set_pad_all(&style.meter, 10);

    /* TX info */
    lv_style_init(&style.tx_info);
    lv_style_set_radius(&style.tx_info, 0);
    lv_style_set_align(&style.tx_info, LV_ALIGN_TOP_MID);
    lv_style_set_border_width(&style.tx_info, 0);
    lv_style_set_bg_img_opa(&style.tx_info, LV_OPA_COVER);
    lv_style_set_bg_opa(&style.tx_info, LV_OPA_0);
    lv_style_set_width(&style.tx_info, METER_WIDTH);
    lv_style_set_height(&style.tx_info, TOP_BLOCK_BIG_HEIGHT);
    lv_style_set_pad_all(&style.tx_info, 10);

    /* CW tune */
    lv_style_init(&style.cw_tune);
    lv_style_set_radius(&style.cw_tune, 5);
    lv_style_set_bg_color(&style.cw_tune, lv_color_black());
    lv_style_set_border_width(&style.cw_tune, 0);
    lv_style_set_opa(&style.cw_tune, LV_OPA_50);
    lv_style_set_x(&style.cw_tune, 30);
    lv_style_set_y(&style.cw_tune, 70);

    /* RGB Picker Styles */
    lv_style_init(&style.rgb.preview_cont);
    lv_style_set_bg_opa(&style.rgb.preview_cont, LV_OPA_TRANSP);
    lv_style_set_pad_top(&style.rgb.preview_cont, 10);

    lv_style_init(&style.rgb.preview_rect);
    lv_style_set_radius(&style.rgb.preview_rect, 12);
    lv_style_set_border_color(&style.rgb.preview_rect, lv_color_white());
    lv_style_set_border_width(&style.rgb.preview_rect, 1);
    lv_style_set_bg_color(&style.rgb.preview_rect, lv_color_hex(0xAAAAAA));

    lv_style_init(&style.rgb.preview_hex);
    lv_style_set_text_font(&style.rgb.preview_hex, &sony_26);

    lv_style_init(&style.rgb.slider_panel);
    lv_style_set_bg_opa(&style.rgb.slider_panel, LV_OPA_TRANSP);
    lv_style_set_pad_row(&style.rgb.slider_panel, 12);

    lv_style_init(&style.rgb.slider_row);
    lv_style_set_pad_column(&style.rgb.slider_row, 10);
    lv_style_set_pad_top(&style.rgb.slider_row, 5);

    lv_style_init(&style.rgb.letter);
    lv_style_set_text_font(&style.rgb.letter, &sony_26);
    lv_style_set_pad_top(&style.rgb.letter, -2);

    lv_style_init(&style.rgb.slider);
    lv_style_set_border_width(&style.rgb.slider, 2);
    lv_style_set_border_color(&style.rgb.slider, lv_color_white());
    lv_style_set_border_opa(&style.rgb.slider, LV_OPA_COVER);
    lv_style_set_radius(&style.rgb.slider, 10);

    lv_style_init(&style.rgb.slider_focused);
    lv_style_set_border_width(&style.rgb.slider_focused, 3);
    lv_style_set_border_color(&style.rgb.slider_focused, lv_palette_main(LV_PALETTE_BLUE));

    lv_style_init(&style.rgb.val_label);
    lv_style_set_text_font(&style.rgb.val_label, &sony_26);
    lv_style_set_pad_top(&style.rgb.val_label, -2);

    styles_set_theme(theme);

    styles_update_meter_colors();
}

void styles_set_spectrum_color(lv_color_t fill_color, lv_color_t *line_color) {
    style.colors.spectrum.fill_up = color_adjust_hsv_value(fill_color, 1.4f);
    style.colors.spectrum.fill_down = color_adjust_hsv_value(fill_color, 0.3f);
    lv_color_t line_color_base;
    if (line_color) {
        line_color_base = *line_color;
    } else {
        line_color_base = fill_color;
    }
    style.colors.spectrum.line = line_color_base;
    style.colors.spectrum.peak = color_adjust_hsv_value(line_color_base, 0.5f);
}

void styles_update_meter_colors(void)
{
    if (params.meter_color.x == METER_GRAY) {
        meter_color_noise   = lv_color_hex(0x777777);
        meter_color_s9      = lv_color_hex(0xAAAAAA);
        meter_color_s9plus  = lv_color_hex(0xAAAA00);
        meter_color_over    = lv_color_hex(0xAA0000);
        meter_color_peak    = lv_color_hex(0xAAAAAA);
    } else {
        // Colored
        meter_color_noise   = lv_color_hex(0x228B22);
        meter_color_s9      = lv_color_hex(0x00CC00);
        meter_color_s9plus  = lv_color_hex(0xFFFF00);
        meter_color_over    = lv_color_hex(0xAA0000);
        meter_color_peak    = lv_color_hex(0xFFFF00);
    }
}

void styles_set_theme(themes_t theme) {
    switch (theme) {
        // case THEME_LEGACY:
        //     setup_theme_legacy();
        //     break;
        // case THEME_BLACK:
        //     setup_theme_black();
        //     break;
        // case THEME_FLAT:
        //     setup_theme_flat();
        //     break;
        case THEME_SIMPLE:
        default:
            set_skin(&skin_default);
            setup_theme_simple();
            break;
    }
}

static inline uint32_t xorshift32(uint32_t *state) {
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static inline int get_gaussian_noise_approx(uint32_t *rng_state) {
    int r1 = ((int)(xorshift32(rng_state) & 7)) - 4;
    int r2 = ((int)(xorshift32(rng_state) & 7)) - 4;
    int r3 = ((int)(xorshift32(rng_state) & 7)) - 4;

    return (r1 + r2 + r3) / 3;
}

static inline void add_noise(uint8_t *val) {
    // 1. Генерируем случайный шум (loc=0, scale ~ 1.0)
    int noise = get_gaussian_noise_approx(&rng_state);
    // 2. Применяем к пикселю (аналог img += noise)
    int pixel_val = *val + noise;

    // 3. Быстрый аналог np.clip(..., 0, 255)
    if (pixel_val < 0) {
        *val = 0;
    } else if (pixel_val > 255) {
        *val = 255;
    } else {
        *val = (uint8_t)pixel_val;
    }
}

static void render_grad_bg_with_border(lv_style_t *style, lv_img_dsc_t *bg_dsc, lv_opa_t opa,
    lv_coord_t border_width, lv_coord_t radius,
    const lv_grad_dsc_t *border_grad, const lv_grad_dsc_t *bg_grad) {
    lv_coord_t outer_radius = radius;
    lv_coord_t inner_radius = outer_radius - border_width;

    if(inner_radius < 0) inner_radius = 0;

    // Get size
    lv_style_value_t prop;
    int32_t w, h;
    lv_style_res_t res;
    res = lv_style_get_prop(style, LV_STYLE_WIDTH, &prop);
    if (res != LV_STYLE_RES_FOUND) {
        // TODO: add usual styles for draw
        LV_LOG_ERROR("Width is not defined, background will be be rendered");
        return;
    }
    w = prop.num;
    res = lv_style_get_prop(style, LV_STYLE_HEIGHT, &prop);
    if (res != LV_STYLE_RES_FOUND) {
        LV_LOG_ERROR("Height is not defined, background will be be rendered");
        return;
    }
    h = prop.num;

    if (bg_dsc->data) {
        free((void*)bg_dsc->data);
        bg_dsc->data = NULL;
        bg_dsc->data_size = 0;
    }

    uint32_t cf = LV_IMG_CF_TRUE_COLOR_ALPHA;

    bg_dsc->header.always_zero = 0;
    bg_dsc->header.w = w;
    bg_dsc->header.h = h;
    bg_dsc->header.cf = cf;
    bg_dsc->data_size = w * h * 4;
    bg_dsc->data = malloc(w * h * 4);

    uint8_t *canvas_buffer = (uint8_t*)bg_dsc->data;
    lv_obj_t * canvas = lv_canvas_create(lv_scr_act());
    lv_canvas_set_buffer(canvas, canvas_buffer, w, h, cf);

    // 2. Clear canvas with a base color
    lv_canvas_fill_bg(canvas, lv_color_white(), LV_OPA_TRANSP);

    lv_draw_rect_dsc_t draw_dsc;

    // Draw border
    if (border_width) {
        lv_draw_rect_dsc_init(&draw_dsc);
        draw_dsc.bg_grad = *border_grad;
        draw_dsc.radius = outer_radius;

        lv_canvas_draw_rect(canvas, 0, 0, w, h, &draw_dsc);
    }

    // Draw inner
    lv_draw_rect_dsc_init(&draw_dsc);
    draw_dsc.bg_grad = *bg_grad;
    draw_dsc.radius = inner_radius;

    lv_canvas_draw_rect(canvas, border_width, border_width, w - 2 * border_width, h - 2 * border_width, &draw_dsc);

    lv_obj_del(canvas);

    // dithering + alpha
    for (int i = 0; i < w * h; i++) {
        int j = i * 4;
        // Add noise for all channels
        add_noise(&canvas_buffer[j]);
        add_noise(&canvas_buffer[j + 1]);
        add_noise(&canvas_buffer[j + 2]);
        canvas_buffer[j + 3] = ((uint16_t)canvas_buffer[j + 3] * opa + 128) >> 8;
    }
}

static void setup_theme_legacy() {
    wf_palette = wf_palette_legacy;

    lv_style_set_line_color(&style.waterfall_middle_line, lv_color_hex(0xAAAAAA));

    bg_color = lv_color_hex(0x0040A0);
    lv_style_set_bg_color(&style.background, bg_color);

    /* Buttons */
    lv_style_set_bg_img_src(&style.btn.base, PATH "images/btn.bin");
    lv_style_remove_prop(&style.btn.base, LV_STYLE_WIDTH);
    lv_style_remove_prop(&style.btn.base, LV_STYLE_HEIGHT);

    lv_style_set_bg_img_src(&style.msg, PATH "images/msg.bin");
    /* Clock */
    lv_style_set_bg_img_src(&style.clock, PATH "images/top_short.bin");
    lv_style_set_width(&style.clock, 206);
    lv_style_set_height(&style.clock, 61);
    /* Info */
    lv_style_set_bg_img_src(&style.info, PATH "images/top_short.bin");
    lv_style_set_width(&style.info, 206);
    lv_style_set_height(&style.info, 61);
    /* Meter */
    lv_style_set_bg_img_src(&style.meter, PATH "images/top_long.bin");
    lv_style_set_width(&style.meter, 377);
    lv_style_set_height(&style.meter, 61);

    lv_style_set_bg_img_src(&style.panels.base, PATH "images/panel.bin");
    lv_style_set_bg_img_src(&style.msg_tiny, PATH "images/msg_tiny.bin");
    lv_style_set_bg_img_src(&style.dialog.base, PATH "images/dialog.bin");
    /* TX info */
    lv_style_set_bg_img_src(&style.tx_info, PATH "images/top_big.bin");
    lv_style_set_width(&style.tx_info, 377);
    lv_style_set_height(&style.tx_info, 123);

    lv_obj_invalidate(lv_scr_act());
}

static void setup_theme_black() {
    wf_palette = wf_palette_gauss;

    lv_style_set_line_color(&style.waterfall_middle_line, lv_color_hex(0xFF0000));

    bg_color = lv_color_hex(0x000000);
    lv_style_set_bg_color(&style.background, bg_color);

    /* Buttons */
    lv_style_set_bg_img_src(&style.btn.base, PATH "images/btn_black.bin");
    lv_style_remove_prop(&style.btn.base, LV_STYLE_WIDTH);
    lv_style_remove_prop(&style.btn.base, LV_STYLE_HEIGHT);

    lv_style_set_bg_img_src(&style.msg, PATH "images/msg_black.bin");
    /* Clock */
    lv_style_set_bg_img_src(&style.clock, PATH "images/top_short_black.bin");
    /* Info */
    lv_style_set_bg_img_src(&style.info, PATH "images/top_short_black.bin");
    /* Meter */
    lv_style_set_bg_img_src(&style.meter, PATH "images/top_long_black.bin");

    lv_style_set_bg_img_src(&style.panels.base, PATH "images/panel_black.bin");
    lv_style_set_bg_img_src(&style.msg_tiny, PATH "images/msg_tiny_black.bin");
    lv_style_set_bg_img_src(&style.dialog.base, PATH "images/dialog_black.bin");
    /* TX info */
    lv_style_set_bg_img_src(&style.tx_info, PATH "images/top_big_black.bin");

    lv_obj_invalidate(lv_scr_act());
}

static void setup_theme_flat() {
    wf_palette = wf_palette_gauss;

    lv_style_set_line_color(&style.waterfall_middle_line, lv_color_hex(0xFF0000));

    bg_color = lv_color_hex(0x36454F);
    lv_style_set_bg_color(&style.background, bg_color);

    lv_style_set_bg_img_src(&style.btn.base, PATH "images/dialog_dark.bin");
    lv_style_set_bg_img_src(&style.msg, PATH "images/msg_dark.bin");
    lv_style_set_width(&style.btn.base, 795);
    lv_style_set_height(&style.btn.base, 61);
    /* Clock */
    lv_style_set_bg_img_src(&style.clock, PATH "images/dialog_dark.bin");
    /* Info */
    lv_style_set_bg_img_src(&style.info, PATH "images/dialog_dark.bin");
    /* Meter */
    lv_style_set_bg_img_src(&style.meter, PATH "images/dialog_dark.bin");

    lv_style_set_bg_img_src(&style.panels.base, PATH "images/panel_dark.bin");
    lv_style_set_bg_img_src(&style.msg_tiny, PATH "images/msg_tiny_dark.bin");
    lv_style_set_bg_img_src(&style.dialog.base, PATH "images/dialog_dark.bin");
    /* TX info */
    lv_style_set_bg_img_src(&style.tx_info, PATH "images/dialog_dark.bin");

    lv_obj_invalidate(lv_scr_act());
}

static void setup_theme_simple() {
    wf_palette = wf_palette_gauss;

    lv_style_set_line_color(&style.waterfall_middle_line, lv_color_hex(0xAAAAAA));

    bg_color = lv_color_hex(0x27313a);
    // bg_color = lv_color_black();
    lv_style_set_bg_color(&style.background, bg_color);

    lv_color_t top_block_bg1_color     = lv_color_hex(0x5f7e97);
    lv_color_t top_block_bg2_color     = lv_color_hex(0x333333);
    lv_color_t top_block_border1_color = lv_color_hex(0xffffff);
    lv_color_t top_block_border2_color = lv_color_hex(0x3e3d3d);
    uint8_t    top_block_opa           = LV_OPA_60;

    /* Text styles */
    // lv_style_set_text_color(&style.text_base_color, lv_color_white());

    /* Top blocks */
    lv_coord_t border_width = 1;
    lv_coord_t radius       = 8;
    lv_grad_dsc_t border_grad = {
        .dir         = LV_GRAD_DIR_VER,
        .stops_count = 2,
        .stops       = {
                        [0] = {.color = top_block_border1_color, .frac = 0},
                        [1] = {.color = top_block_border2_color, .frac = 255},
                        }
    };

    lv_grad_dsc_t bg_grad = {
        .dir         = LV_GRAD_DIR_VER,
        .stops_count = 3,
        .stops       = {
                        [0] = {.color = top_block_bg1_color, .frac = 0},
                        [1] = {.color = lv_color_mix(top_block_bg1_color, top_block_bg2_color, 127), .frac = 160},
                        [2] = {.color = top_block_bg2_color, .frac = 255},
                        }
    };

    // Clock
    render_grad_bg_with_border(&style.clock, &clock_bg_dsc, top_block_opa, border_width, radius, &border_grad,
                               &bg_grad);
    lv_style_set_bg_img_src(&style.clock, &clock_bg_dsc);

    // Info
    render_grad_bg_with_border(&style.info, &info_bg_dsc, top_block_opa, border_width, radius, &border_grad,
                               &bg_grad);
    lv_style_set_bg_img_src(&style.info, &info_bg_dsc);

    // Meter
    render_grad_bg_with_border(&style.meter, &meter_bg_dsc, top_block_opa, border_width, radius, &border_grad,
                               &bg_grad);
    lv_style_set_bg_img_src(&style.meter, &meter_bg_dsc);

    // TX info
    render_grad_bg_with_border(&style.tx_info, &tx_info_bg_dsc, top_block_opa, border_width, radius, &border_grad,
                               &bg_grad);
    lv_style_set_bg_img_src(&style.tx_info, &tx_info_bg_dsc);


    /* Button */
    lv_color_t button_bg1_color     = lv_color_hex(0x4f4742);
    lv_color_t button_bg2_color     = lv_color_hex(0x1c150d);
    lv_color_t button_border1_color = lv_color_hex(0xa7a7a7);
    lv_color_t button_border2_color = lv_color_hex(0x1d1d1d);
    uint8_t    button_opa           = LV_OPA_90;

    border_grad = (lv_grad_dsc_t){
        .dir         = LV_GRAD_DIR_VER,
        .stops_count = 2,
        .stops       = {
                        [0] = {.color = button_border1_color, .frac = 0},
                        [1] = {.color = button_border2_color, .frac = 255},
                        }
    };

    bg_grad = (lv_grad_dsc_t){
        .dir         = LV_GRAD_DIR_VER,
        .stops_count = 3,
        .stops       = {
                        [0] = {.color = button_bg1_color, .frac = 0},
                        [1] = {.color = lv_color_mix(button_bg1_color, button_bg2_color, 127), .frac = 160},
                        [2] = {.color = button_bg2_color, .frac = 255},
                        }
    };

    render_grad_bg_with_border(&style.btn.base, &button_bg_dsc, button_opa, border_width, radius, &border_grad,
                               &bg_grad);
    lv_style_set_bg_img_src(&style.btn.base, &button_bg_dsc);

    /* Dialog */
    lv_color_t dialog_bg1_color     = lv_color_hex(0x4c6676);
    lv_color_t dialog_bg2_color     = lv_color_hex(0x2e3b47);
    lv_color_t dialog_border1_color = lv_color_hex(0x5f5f5f);
    lv_color_t dialog_border2_color = lv_color_hex(0xffffff);
    uint8_t    dialog_opa           = 255 * 95 / 100;

    border_grad = (lv_grad_dsc_t){
        .dir         = LV_GRAD_DIR_VER,
        .stops_count = 4,
        .stops       = {
                        [0] = {.color = dialog_border1_color, .frac = 0},
                        [1] = {.color = dialog_border2_color, .frac = 40},
                        [2] = {.color = dialog_border2_color, .frac = 215},
                        [3] = {.color = dialog_border1_color, .frac = 255},
                        }
    };

    bg_grad = (lv_grad_dsc_t){
        .dir         = LV_GRAD_DIR_VER,
        .stops_count = 4,
        .stops       = {
                        [0] = {.color = dialog_bg2_color, .frac = 0},
                        [1] = {.color = dialog_bg1_color, .frac = 40},
                        [2] = {.color = dialog_bg1_color, .frac = 215},
                        [3] = {.color = dialog_bg2_color, .frac = 255},
                        }
    };

    render_grad_bg_with_border(&style.dialog.base, &dialog_bg_dsc, dialog_opa, border_width, radius, &border_grad,
                               &bg_grad);
    lv_style_set_bg_img_src(&style.dialog.base, &dialog_bg_dsc);




    lv_style_set_bg_img_src(&style.msg, PATH "images/msg_dark.bin");



    lv_style_set_bg_img_src(&style.panels.base, PATH "images/panel_dark.bin");
    lv_style_set_bg_img_src(&style.msg_tiny, PATH "images/msg_tiny_dark.bin");


    lv_obj_invalidate(lv_scr_act());
}

static lv_color_t color_adjust_hsv_value(lv_color_t base, float scale) {
    lv_color_hsv_t hsv = lv_color_to_hsv(base);
    hsv.v = LV_CLAMP(0, hsv.v * scale, 255);
    return lv_color_hsv_to_rgb(hsv.h, hsv.s, hsv.v);
}

static void setup_skin_default(skin_t *skin) {
    skin->base_text_color = lv_color_white();
    // skin->base_text_color = lv_color_hex(0x00ffaa);

    skin->spectrum.fill_color = lv_color_hex(0x7db4be);
    skin->spectrum.line_color = lv_color_hex(0x7db4be);
}

static void set_skin(skin_t *skin) {
    lv_color_t muted_text_color;
    if (lv_color_brightness(skin->base_text_color) > 64) {
        // Bright color, muted should be darker
        muted_text_color = lv_color_darken(skin->base_text_color, LV_OPA_40);
    } else {
        muted_text_color = lv_color_lighten(skin->base_text_color, LV_OPA_40);
    }
    colors.base_text_color = skin->base_text_color;

    lv_style_set_text_color(&style.text_base_color, skin->base_text_color);
    lv_style_set_text_color(&style.btn.base, skin->base_text_color);
    lv_style_set_text_color(&style.freq_bounds, skin->base_text_color);
    lv_style_set_text_color(&style.msg, skin->base_text_color);
    lv_style_set_text_color(&style.msg_tiny, skin->base_text_color);
    lv_style_set_text_color(&style.panels.base, skin->base_text_color);
    lv_style_set_text_color(&style.dialog.base, skin->base_text_color);
    lv_style_set_text_color(&style.dialog.item, skin->base_text_color);
    lv_style_set_text_color(&style.clock, skin->base_text_color);
    lv_style_set_text_color(&style.knobs, skin->base_text_color);
    lv_style_set_text_color(&style.rgb.letter, skin->base_text_color);
    lv_style_set_text_color(&style.rgb.val_label, skin->base_text_color);

    lv_style_set_text_color(&style.text_muted_color, muted_text_color);

    /* Spectrum */
    // TODO: add spectrum custom color toggle
    if (params.spectrum_r.x == 0 && params.spectrum_g.x == 0 && params.spectrum_b.x == 0) {
        styles_set_spectrum_color(skin->spectrum.fill_color, &skin->spectrum.line_color);
    } else {
        styles_set_spectrum_color(lv_color_make(params.spectrum_r.x, params.spectrum_g.x, params.spectrum_b.x), NULL);
    }
}

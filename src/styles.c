/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

#include "styles.h"
#include "styles_custom.h"

const uint32_t wf_palette_legacy[256] = {
    0x000000, 0x000004, 0x000008, 0x00000c, 0x00000e, 0x000012, 0x000016, 0x000018,
    0x00001c, 0x00001e, 0x000022, 0x000024, 0x000026, 0x00002a, 0x00002c, 0x000030,
    0x000032, 0x000034, 0x000038, 0x00003a, 0x00003c, 0x00003e, 0x000041, 0x000044,
    0x000046, 0x000048, 0x00004c, 0x00004e, 0x000050, 0x000051, 0x000054, 0x000058,
    0x000059, 0x00005c, 0x00005e, 0x000060, 0x000061, 0x000066, 0x000068, 0x000069,
    0x00006c, 0x00006e, 0x000070, 0x000071, 0x000076, 0x000078, 0x000079, 0x00007c,
    0x00007e, 0x00007f, 0x03007d, 0x06007b, 0x090079, 0x0b0077, 0x0e0075, 0x140071,
    0x17006f, 0x19006d, 0x1c006b, 0x1f0069, 0x220067, 0x250065, 0x270063, 0x2a0061,
    0x2d005f, 0x30005d, 0x33005b, 0x350059, 0x380057, 0x3b0055, 0x3e0053, 0x410051,
    0x43004f, 0x46004d, 0x4c0049, 0x4f0047, 0x510045, 0x540043, 0x570041, 0x5a003f,
    0x5d003c, 0x5f003b, 0x620038, 0x650037, 0x680034, 0x6b0033, 0x6d0030, 0x70002f,
    0x73002c, 0x76002b, 0x790028, 0x7b0027, 0x7e0024, 0x810023, 0x840020, 0x87001f,
    0x89001c, 0x8c001b, 0x8f0018, 0x8f0018, 0x920017, 0x950014, 0x970013, 0x9a0010,
    0x9d000f, 0xa0000c, 0xa3000b, 0xa50008, 0xa80007, 0xab0004, 0xae0003, 0xb10000,
    0xb20100, 0xb30500, 0xb40800, 0xb50c00, 0xb61000, 0xb61300, 0xb71700, 0xb81a00,
    0xb91e00, 0xba2200, 0xba2500, 0xba2500, 0xbb2900, 0xbc2c00, 0xbd3000, 0xbe3400,
    0xbe3700, 0xbf3b00, 0xc03e00, 0xc14200, 0xc24600, 0xc24900, 0xc34d00, 0xc45000,
    0xc55400, 0xc65800, 0xc65800, 0xc65b00, 0xc75f00, 0xc86200, 0xc96600, 0xca6a00,
    0xca6d00, 0xcb7100, 0xcc7400, 0xcd7800, 0xce7c00, 0xce7f00, 0xce7f00, 0xcf8300,
    0xd08600, 0xd18a00, 0xd28e00, 0xd29100, 0xd39500, 0xd49800, 0xd59c00, 0xd6a000,
    0xd6a300, 0xd6a300, 0xd7a700, 0xd8aa00, 0xd9ae00, 0xdab200, 0xdab500, 0xdbb900,
    0xdcbc00, 0xddc000, 0xdec400, 0xdec400, 0xdec700, 0xdfcb00, 0xe0ce00, 0xe1d200,
    0xe2d600, 0xe2d900, 0xe3dd00, 0xe3dd00, 0xe4e000, 0xe5e400, 0xe5e503, 0xe6e607,
    0xe6e60b, 0xe6e60f, 0xe7e712, 0xe7e717, 0xe7e717, 0xe8e81b, 0xe8e81f, 0xe8e823,
    0xe9e927, 0xe9e92b, 0xeaea2f, 0xeaea33, 0xeaea33, 0xeaea37, 0xebeb3b, 0xebeb3f,
    0xecec43, 0xecec47, 0xecec4b, 0xeded4f, 0xeded4f, 0xeded52, 0xeeee57, 0xeeee5b,
    0xeeee5f, 0xefef63, 0xefef67, 0xefef67, 0xf0f06b, 0xf0f06f, 0xf0f073, 0xf1f177,
    0xf1f17b, 0xf2f27f, 0xf2f27f, 0xf2f283, 0xf2f287, 0xf3f38b, 0xf3f38f, 0xf4f492,
    0xf4f497, 0xf4f497, 0xf4f49b, 0xf5f59f, 0xf5f5a3, 0xf6f6a7, 0xf6f6ab, 0xf6f6ab,
    0xf6f6af, 0xf7f7b3, 0xf7f7b7, 0xf8f8bb, 0xf8f8bf, 0xf8f8c3, 0xf8f8c3, 0xf9f9c7,
    0xf9f9cb, 0xfafacf, 0xfafad2, 0xfafad7, 0xfafad7, 0xfbfbdb, 0xfbfbdf, 0xfcfce3,
    0xfcfce7, 0xfcfceb, 0xfcfceb, 0xfdfdef, 0xfdfdf3, 0xfefef7, 0xfefefb, 0xffffff,
};

const uint32_t wf_palette_gauss[256] = {
    0x000021, 0x000024, 0x000028, 0x00012b, 0x00012d, 0x000130, 0x000134, 0x000136,
    0x00023a, 0x00023c, 0x000240, 0x000242, 0x000344, 0x000348, 0x00034a, 0x00044f,
    0x000451, 0x000553, 0x000557, 0x000659, 0x00065b, 0x00075e, 0x000862, 0x000864,
    0x000966, 0x000968, 0x000a6c, 0x000b6e, 0x000c6f, 0x000d71, 0x000d73, 0x000f76,
    0x001078, 0x001179, 0x00127b, 0x00137c, 0x00147d, 0x001680, 0x001881, 0x001982,
    0x001a83, 0x001c84, 0x001d85, 0x001f85, 0x002286, 0x002387, 0x002587, 0x002787,
    0x002987, 0x002a87, 0x012c87, 0x012e87, 0x013087, 0x013287, 0x013486, 0x013985,
    0x013b84, 0x013d84, 0x024083, 0x024282, 0x024481, 0x024780, 0x02497f, 0x034c7e,
    0x034e7c, 0x03517b, 0x035479, 0x045678, 0x045976, 0x045c75, 0x055e73, 0x056171,
    0x056470, 0x06676e, 0x076c6a, 0x076f68, 0x087266, 0x087464, 0x097762, 0x097a60,
    0x0a7d5e, 0x0b7f5c, 0x0b825a, 0x0c8458, 0x0d8756, 0x0e8a54, 0x0f8c52, 0x108e50,
    0x11914e, 0x12934c, 0x13954a, 0x149748, 0x159a47, 0x169c45, 0x179e43, 0x199f41,
    0x1aa13f, 0x1ba33d, 0x1da53c, 0x1da53c, 0x1ea63a, 0x20a838, 0x22a937, 0x23aa35,
    0x25ab33, 0x27ac32, 0x29ad30, 0x2bae2f, 0x2daf2e, 0x2faf2c, 0x31b02b, 0x33b02a,
    0x35b129, 0x38b128, 0x3ab127, 0x3db126, 0x3fb125, 0x42b124, 0x44b023, 0x47b022,
    0x4aaf21, 0x4daf21, 0x4fae20, 0x4fae20, 0x52ad1f, 0x55ad1f, 0x58ac1e, 0x5bab1e,
    0x5fa91d, 0x62a81d, 0x65a71d, 0x68a61d, 0x6ca41c, 0x6fa31c, 0x72a11c, 0x76a01c,
    0x799e1c, 0x7d9c1c, 0x7d9c1c, 0x809b1c, 0x84991c, 0x87971d, 0x8a951d, 0x8e941d,
    0x91921d, 0x95901e, 0x988e1e, 0x9c8c1e, 0x9f8a1f, 0xa3891f, 0xa3891f, 0xa68720,
    0xa98521, 0xad8321, 0xb08122, 0xb38022, 0xb77e23, 0xba7c24, 0xbd7b25, 0xc07926,
    0xc37726, 0xc37726, 0xc67627, 0xc87428, 0xcb7329, 0xce722a, 0xd0702b, 0xd36f2c,
    0xd56e2e, 0xd86d2f, 0xda6c30, 0xda6c30, 0xdc6b31, 0xde6a32, 0xe06934, 0xe26935,
    0xe46836, 0xe56738, 0xe76739, 0xe76739, 0xe9663b, 0xea663c, 0xeb663e, 0xec663f,
    0xed6641, 0xee6643, 0xef6644, 0xf06646, 0xf06646, 0xf16648, 0xf1674a, 0xf2674b,
    0xf2684d, 0xf3684f, 0xf36951, 0xf36a53, 0xf36a53, 0xf36b55, 0xf36c57, 0xf36d5a,
    0xf36e5c, 0xf36f5e, 0xf37060, 0xf37262, 0xf37262, 0xf27365, 0xf27567, 0xf2766a,
    0xf1786c, 0xf17a6f, 0xf07c71, 0xf07c71, 0xf07e74, 0xef8077, 0xef8279, 0xee847c,
    0xee877f, 0xed8982, 0xed8982, 0xed8c85, 0xed8e88, 0xec918b, 0xec938e, 0xec9691,
    0xeb9994, 0xeb9994, 0xeb9c98, 0xeb9f9b, 0xeba29e, 0xeba5a2, 0xeba9a5, 0xeba9a5,
    0xebaca9, 0xebafad, 0xebb3b0, 0xebb6b4, 0xebbab8, 0xecbebc, 0xecbebc, 0xecc1c0,
    0xedc5c4, 0xeec9c8, 0xefcdcc, 0xefd1d0, 0xefd1d0, 0xf0d5d5, 0xf2dad9, 0xf3dedd,
    0xf4e2e2, 0xf6e7e6, 0xf6e7e6, 0xf7ebeb, 0xf9f0f0, 0xfbf5f5, 0xfcfafa, 0xffffff,
};

const uint32_t *wf_palette;

lv_style_t  background_style;
lv_style_t  spectrum_style;
lv_style_t  freq_style;
lv_style_t  freq_main_style;
lv_style_t  waterfall_style;
lv_style_t  btn_style;
lv_style_t  btn_active_style;
lv_style_t  btn_disabled_style;
lv_style_t  msg_style;
lv_style_t  msg_tiny_style;
lv_style_t  clock_style;
lv_style_t  knobs_style;
lv_style_t  info_style;
lv_style_t  info_row_style;
lv_style_t  info_item_style;
lv_style_t  meter_style;
lv_style_t  tx_info_style;

lv_style_t  panel_top_style;
lv_style_t  panel_mid_style;
lv_style_t  panel_bottom_style;
lv_style_t  pannel_style;

lv_style_t  dialog_style;
lv_style_t  dialog_item_style;
lv_style_t  dialog_item_focus_style;
lv_style_t  dialog_item_edited_style;
lv_style_t  dialog_dropdown_list_style;

lv_style_t  cw_tune_style;

lv_color_t  bg_color;

static void setup_theme_legacy();
static void setup_theme_simple();
static void setup_theme_flat();

void styles_init(themes_t theme) {
    /* * */

    lv_style_init(&background_style);

    lv_style_init(&spectrum_style);
    lv_style_set_bg_color(&spectrum_style, lv_color_hex(0x000000));
    lv_style_set_bg_opa(&spectrum_style, LV_OPA_COVER);
    lv_style_set_border_color(&spectrum_style, lv_color_hex(0xAAAAAA));
    lv_style_set_border_width(&spectrum_style, 0);
    lv_style_set_radius(&spectrum_style, 0);
    lv_style_set_width(&spectrum_style, 800);
    lv_style_set_x(&spectrum_style, 0);

    lv_style_init(&freq_style);
    lv_style_set_text_color(&freq_style, lv_color_white());
    lv_style_set_text_font(&freq_style, &sony_30);
    lv_style_set_pad_ver(&freq_style, 7);
    lv_style_set_width(&freq_style, 150);
    lv_style_set_height(&freq_style, 36);
    lv_style_set_text_align(&freq_style, LV_TEXT_ALIGN_CENTER);

    lv_style_init(&freq_main_style);
    lv_style_set_text_color(&freq_main_style, lv_color_white());
    lv_style_set_text_font(&freq_main_style, &sony_38);
    lv_style_set_pad_ver(&freq_main_style, 5);
    lv_style_set_width(&freq_main_style, 500);
    lv_style_set_height(&freq_main_style, 36);
    lv_style_set_text_align(&freq_main_style, LV_TEXT_ALIGN_CENTER);

    lv_style_init(&waterfall_style);
    lv_style_set_bg_color(&waterfall_style, lv_color_hex(0x000000));
    lv_style_set_border_color(&waterfall_style, lv_color_hex(0xAAAAAA));
    lv_style_set_border_width(&waterfall_style, 0);
    lv_style_set_radius(&waterfall_style, 0);
    lv_style_set_clip_corner(&waterfall_style, true);
    lv_style_set_width(&waterfall_style, 800);
    lv_style_set_x(&waterfall_style, 0);

    /* Buttons */
    lv_style_init(&btn_style);
    lv_style_set_text_font(&btn_style, &sony_30);
    lv_style_set_text_color(&btn_style, lv_color_white());
    lv_style_set_bg_img_opa(&btn_style, LV_OPA_COVER);
    lv_style_set_border_width(&btn_style, 0);
    lv_style_set_radius(&btn_style, 0);
    lv_style_set_bg_opa(&btn_style, LV_OPA_0);

    lv_style_init(&btn_active_style);
    lv_style_set_bg_img_recolor(&btn_active_style, lv_color_hex(0x00FF00));
    lv_style_set_bg_img_recolor_opa(&btn_active_style, LV_OPA_20);

    lv_style_init(&btn_disabled_style);
    lv_style_set_bg_img_recolor(&btn_disabled_style, lv_color_hex(0x000000));
    lv_style_set_bg_img_recolor_opa(&btn_disabled_style, LV_OPA_20);
    lv_style_set_text_color(&btn_disabled_style, lv_color_hex(0x101010));

    /* Message style */
    lv_style_init(&msg_style);
    lv_style_set_text_color(&msg_style, lv_color_white());
    lv_style_set_text_font(&msg_style, &sony_30);
    lv_style_set_width(&msg_style, 603);
    lv_style_set_height(&msg_style, 66);
    lv_style_set_x(&msg_style, 800 / 2 - (603 / 2));
    lv_style_set_y(&msg_style, 300);
    lv_style_set_radius(&msg_style, 0);
    lv_style_set_bg_img_opa(&msg_style, LV_OPA_COVER);
    lv_style_set_pad_ver(&msg_style, 20);

    lv_style_init(&msg_tiny_style);
    lv_style_set_text_color(&msg_tiny_style, lv_color_white());
    lv_style_set_text_font(&msg_tiny_style, &sony_60);
    lv_style_set_width(&msg_tiny_style, 324);
    lv_style_set_height(&msg_tiny_style, 66);
    lv_style_set_x(&msg_tiny_style, 800 / 2 - (324 / 2));
    lv_style_set_y(&msg_tiny_style, 160 - 66/2 + 36/2);
    lv_style_set_radius(&msg_tiny_style, 0);
    lv_style_set_pad_ver(&msg_tiny_style, 12);

    /* Panel */
    lv_style_init(&pannel_style);
    lv_style_set_text_color(&pannel_style, lv_color_white());
    lv_style_set_text_font(&pannel_style, &sony_38);
    lv_style_set_width(&pannel_style, 795);
    lv_style_set_height(&pannel_style, 182);
    lv_style_set_x(&pannel_style, 800 / 2 - (795 / 2));
    lv_style_set_y(&pannel_style, 230);
    lv_style_set_pad_ver(&pannel_style, 10);
    lv_style_set_pad_hor(&pannel_style, 10);
    lv_style_set_radius(&pannel_style, 0);
    lv_style_set_bg_img_opa(&pannel_style, LV_OPA_COVER);

    lv_style_init(&dialog_style);
    lv_style_set_text_color(&dialog_style, lv_color_white());
    lv_style_set_text_font(&dialog_style, &sony_36);
    lv_style_set_width(&dialog_style, 796);
    lv_style_set_height(&dialog_style, 348);
    lv_style_set_x(&dialog_style, 800 / 2 - (796 / 2));
    lv_style_set_y(&dialog_style, 66);
    lv_style_set_radius(&dialog_style, 0);
    lv_style_set_bg_img_opa(&dialog_style, LV_OPA_COVER);
    lv_style_set_pad_ver(&dialog_style, 0);
    lv_style_set_pad_hor(&dialog_style, 0);

    lv_style_init(&dialog_item_style);
    lv_style_set_bg_opa(&dialog_item_style, LV_OPA_TRANSP);
    lv_style_set_text_color(&dialog_item_style, lv_color_white());

    lv_style_init(&dialog_item_focus_style);
    lv_style_set_bg_opa(&dialog_item_focus_style, 128);
    lv_style_set_text_color(&dialog_item_focus_style, lv_color_black());
    lv_style_set_border_color(&dialog_item_focus_style, lv_color_white());
    lv_style_set_border_width(&dialog_item_focus_style, 2);

    lv_style_init(&dialog_item_edited_style);
    lv_style_set_bg_opa(&dialog_item_edited_style, LV_OPA_COVER);
    lv_style_set_text_color(&dialog_item_edited_style, lv_color_black());

    lv_style_init(&dialog_dropdown_list_style);
    lv_style_set_text_font(&dialog_dropdown_list_style, &sony_30);

    /* Clock */
    lv_style_init(&clock_style);
    lv_style_set_text_color(&clock_style, lv_color_white());
    lv_style_set_align(&clock_style, LV_ALIGN_TOP_RIGHT);
    lv_style_set_radius(&clock_style, 0);
    lv_style_set_bg_img_opa(&clock_style, LV_OPA_COVER);

    /* Knobs */
    lv_style_init(&knobs_style);
    lv_style_set_text_color(&knobs_style, lv_color_white());
    lv_style_set_radius(&knobs_style, 0);
    lv_style_set_bg_img_opa(&knobs_style, LV_OPA_COVER);

    /* Left info */
    lv_style_init(&info_style);
    lv_style_set_align(&info_style, LV_ALIGN_TOP_LEFT);
    lv_style_set_pad_hor(&info_style, 8);
    lv_style_set_pad_ver(&info_style, 6);
    lv_style_set_pad_row(&info_style, 0);
    lv_style_set_radius(&info_style, 0);
    lv_style_set_bg_img_opa(&info_style, LV_OPA_COVER);
    lv_style_set_border_width(&info_style, 0);
    lv_style_set_bg_opa(&info_style, LV_OPA_0);

    lv_style_init(&info_row_style);
    lv_style_set_align(&info_row_style, LV_ALIGN_CENTER);
    lv_style_set_radius(&info_row_style, 0);
    lv_style_set_border_width(&info_row_style, 0);
    lv_style_set_bg_opa(&info_row_style, LV_OPA_0);
    lv_style_set_pad_hor(&info_row_style, 2);
    lv_style_set_pad_ver(&info_row_style, 0);
    lv_style_set_pad_column(&info_row_style, 2);

    lv_style_init(&info_item_style);
    lv_style_set_text_font(&info_item_style, &sony_20);
    lv_style_set_pad_ver(&info_item_style, 3);
    lv_style_set_radius(&info_item_style, 0);

    /* Meter */
    lv_style_init(&meter_style);
    lv_style_set_radius(&meter_style, 0);
    lv_style_set_align(&meter_style, LV_ALIGN_TOP_MID);
    lv_style_set_border_width(&meter_style, 0);
    lv_style_set_bg_img_opa(&meter_style, LV_OPA_COVER);
    lv_style_set_bg_opa(&meter_style, LV_OPA_0);

    /* TX info */
    lv_style_init(&tx_info_style);
    lv_style_set_radius(&tx_info_style, 0);
    lv_style_set_align(&tx_info_style, LV_ALIGN_TOP_MID);
    lv_style_set_border_width(&tx_info_style, 0);
    lv_style_set_bg_img_opa(&tx_info_style, LV_OPA_COVER);
    lv_style_set_bg_opa(&tx_info_style, LV_OPA_0);

    lv_style_init(&cw_tune_style);
    lv_style_set_radius(&cw_tune_style, 5);
    lv_style_set_bg_color(&cw_tune_style, lv_color_black());
    lv_style_set_border_width(&cw_tune_style, 0);
    lv_style_set_opa(&cw_tune_style, LV_OPA_50);
    lv_style_set_x(&cw_tune_style, 30);
    lv_style_set_y(&cw_tune_style, 70);

    styles_set_theme(theme);

    styles_customize_theme();
}

void styles_set_theme(themes_t theme) {
    switch (theme) {
        case THEME_LEGACY:
            setup_theme_legacy();
            break;
        case THEME_FLAT:
            setup_theme_flat();
            break;
        case THEME_SIMPLE:
        default:
            setup_theme_simple();
            break;
    }
}

static void setup_theme_legacy() {
    wf_palette = wf_palette_legacy;
    wf_palette = wf_palette_gauss;

    bg_color = lv_color_hex(0x0040A0);
    lv_style_set_bg_color(&background_style, bg_color);

    lv_style_set_bg_img_src(&btn_style, PATH "images/btn.bin");
    lv_style_set_bg_img_src(&msg_style, PATH "images/msg.bin");
    /* Clock */
    lv_style_set_bg_img_src(&clock_style, PATH "images/top_short.bin");
    lv_style_set_width(&clock_style, 206);
    lv_style_set_height(&clock_style, 61);
    /* Knobs */
    // lv_style_set_bg_img_src(&knobs_style, PATH "images/top_short.bin");
    /* Info */
    lv_style_set_bg_img_src(&info_style, PATH "images/top_short.bin");
    lv_style_set_width(&info_style, 206);
    lv_style_set_height(&info_style, 61);
    /* Meter */
    lv_style_set_bg_img_src(&meter_style, PATH "images/top_long.bin");
    lv_style_set_width(&meter_style, 377);
    lv_style_set_height(&meter_style, 61);

    lv_style_set_bg_img_src(&pannel_style, PATH "images/panel.bin");
    lv_style_set_bg_img_src(&msg_tiny_style, PATH "images/msg_tiny.bin");
    lv_style_set_bg_img_src(&dialog_style, PATH "images/dialog.bin");
    /* TX info */
    lv_style_set_bg_img_src(&tx_info_style, PATH "images/top_big.bin");
    lv_style_set_width(&tx_info_style, 377);
    lv_style_set_height(&tx_info_style, 123);

    lv_obj_invalidate(lv_scr_act());
}

static void setup_theme_flat() {
    wf_palette = wf_palette_gauss;

    bg_color = lv_color_hex(0x36454F);
    lv_style_set_bg_color(&background_style, bg_color);

    lv_style_set_bg_img_src(&btn_style, PATH "images/dialog_dark.bin");
    lv_style_set_bg_img_src(&msg_style, PATH "images/msg_dark.bin");
    lv_style_set_width(&btn_style, 795);
    lv_style_set_height(&btn_style, 61);
    /* Clock */
    lv_style_set_bg_img_src(&clock_style, PATH "images/dialog_dark.bin");
    lv_style_set_width(&clock_style, 209);
    lv_style_set_height(&clock_style, 61);
    /* Info */
    lv_style_set_bg_img_src(&info_style, PATH "images/dialog_dark.bin");
    lv_style_set_width(&info_style, 209);
    lv_style_set_height(&info_style, 61);
    /* Meter */
    lv_style_set_bg_img_src(&meter_style, PATH "images/dialog_dark.bin");
    lv_style_set_width(&meter_style, 380);
    lv_style_set_height(&meter_style, 61);

    lv_style_set_bg_img_src(&pannel_style, PATH "images/panel_dark.bin");
    lv_style_set_bg_img_src(&msg_tiny_style, PATH "images/msg_tiny_dark.bin");
    lv_style_set_bg_img_src(&dialog_style, PATH "images/dialog_dark.bin");
    /* TX info */
    lv_style_set_bg_img_src(&tx_info_style, PATH "images/dialog_dark.bin");
    lv_style_set_width(&tx_info_style, 380);
    lv_style_set_height(&tx_info_style, 123);

    lv_obj_invalidate(lv_scr_act());
}

static void setup_theme_simple() {
    wf_palette = wf_palette_gauss;

    bg_color = lv_color_hex(0x27313a);
    lv_style_set_bg_color(&background_style, bg_color);

    lv_style_set_bg_img_src(&btn_style, PATH "images/btn_dark.bin");
    lv_style_set_bg_img_src(&msg_style, PATH "images/msg_dark.bin");
    /* Clock */
    lv_style_set_bg_img_src(&clock_style, PATH "images/top_short_dark.bin");
    lv_style_set_width(&clock_style, 209);
    lv_style_set_height(&clock_style, 61);
    /* Knobs */
    // lv_style_set_bg_img_src(&knobs_style, PATH "images/top_short_dark.bin");
    /* Info */
    lv_style_set_bg_img_src(&info_style, PATH "images/top_short_dark.bin");
    lv_style_set_width(&info_style, 209);
    lv_style_set_height(&info_style, 61);
    /* Meter */
    lv_style_set_bg_img_src(&meter_style, PATH "images/top_long_dark.bin");
    lv_style_set_width(&meter_style, 380);
    lv_style_set_height(&meter_style, 61);

    lv_style_set_bg_img_src(&pannel_style, PATH "images/panel_dark.bin");
    lv_style_set_bg_img_src(&msg_tiny_style, PATH "images/msg_tiny_dark.bin");
    lv_style_set_bg_img_src(&dialog_style, PATH "images/dialog_dark.bin");
    /* TX info */
    lv_style_set_bg_img_src(&tx_info_style, PATH "images/top_big_dark.bin");
    lv_style_set_width(&tx_info_style, 380);
    lv_style_set_height(&tx_info_style, 123);

    lv_obj_invalidate(lv_scr_act());
}

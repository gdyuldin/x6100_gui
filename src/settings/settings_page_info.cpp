#include "settings_widgets.h"

extern "C" {
#include "../main.h"
#include "../radio.h"
}

/***** PAGE *****/

void make_info_page(SettingsPage &page) {
    page.destroy();

    lv_obj_t *grid = lv_obj_create(page.dialog->obj);

    page.grid = grid;

    lv_obj_set_size(grid, lv_pct(100), lv_pct(100));
    lv_obj_set_style_border_width(grid, 0, LV_PART_MAIN);
    lv_obj_add_style(grid, &style.text_base_color, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(grid, 30, 0);
    lv_obj_set_style_pad_ver(grid, 40, 0);

    lv_obj_t *label = lv_label_create(grid);
    lv_obj_set_style_text_line_space(label, 20, LV_PART_MAIN);
    lv_label_set_text_fmt(label, "App version: %s\nBASE version: %s", VERSION, x6100_control_get_fw_version_str());
}

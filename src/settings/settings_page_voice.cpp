#include "settings_widgets.h"

#include <string>

#include "../voice.h"

/***** VOICE *****/

static void make_voice(SettingsPage &page) {
    uint8_t   col = 1;
    lv_obj_t *obj;

    page.label("Voice mode");

    obj = page.dropdown_int(page.grid, *cfg.voice.mode(), " Off \n When LCD off \n Always");

    lv_obj_set_size(obj, SMALL_6, 56);
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, 1, 6, LV_GRID_ALIGN_CENTER, page.row, 1);
    lv_obj_center(obj);

    /* * */

    page.row++;
    page.label("Voice rate, pitch, volume");

    obj = page.spinbox_int(page.grid, *cfg.voice.rate(), 50, 150, "Voice rate");

    lv_spinbox_set_digit_format(obj, 3, 0);
    lv_spinbox_set_digit_step_direction(obj, LV_DIR_LEFT);
    lv_obj_set_size(obj, SMALL_2, 56);
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, col, 2, LV_GRID_ALIGN_CENTER, page.row, 1);
    col += 2;

    obj = page.spinbox_int(page.grid, *cfg.voice.pitch(), 50, 150, "Voice pitch");

    lv_spinbox_set_digit_format(obj, 3, 0);
    lv_spinbox_set_digit_step_direction(obj, LV_DIR_LEFT);
    lv_obj_set_size(obj, SMALL_2, 56);
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, col, 2, LV_GRID_ALIGN_CENTER, page.row, 1);
    col += 2;

    obj = page.spinbox_int(page.grid, *cfg.voice.volume(), 50, 150, "Voice volume");

    lv_spinbox_set_digit_format(obj, 3, 0);
    lv_spinbox_set_digit_step_direction(obj, LV_DIR_LEFT);
    lv_obj_set_size(obj, SMALL_2, 56);
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, col, 2, LV_GRID_ALIGN_CENTER, page.row, 1);
    col += 2;

    page.row++;
}

static void make_voice_lang(SettingsPage &page) {
    page.label("Voice type");

    std::string options = "";
    for (auto &&item : voice_item) {
        options += " " + std::string(item.label) + " \n";
    }
    options[options.size() - 1] = '\0';

    lv_obj_t *obj = page.dropdown_int(page.grid, *cfg.voice.lang(), options.c_str());

    lv_obj_set_size(obj, SMALL_6, 56);
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, 1, 6, LV_GRID_ALIGN_CENTER, page.row, 1);
    lv_obj_center(obj);

    page.row++;
}

/***** PAGE *****/

void make_voice_page(SettingsPage &page) {
    page.reset();

    make_voice(page);
    make_voice_lang(page);

    page.finish();
}

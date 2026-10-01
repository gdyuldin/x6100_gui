#include "settings_widgets.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/rtc.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "../display.h"
#include "../settings_types.h"
#include "../voice.h"

extern "C" {
#include "../audio.h"
#include "../radio.h"
}

/***** DATETIME *****/

static bool      datetime_changed;
static time_t    now;
static struct tm ts;

static lv_obj_t *day;
static lv_obj_t *month;
static lv_obj_t *year;
static lv_obj_t *hour;
static lv_obj_t *min;
static lv_obj_t *sec;

static void datetime_update_cb(lv_event_t *e) {
    ts.tm_mday = lv_spinbox_get_value(day);
    ts.tm_mon  = lv_spinbox_get_value(month) - 1;
    ts.tm_year = lv_spinbox_get_value(year) - 1900;
    ts.tm_hour = lv_spinbox_get_value(hour);
    ts.tm_min  = lv_spinbox_get_value(min);
    ts.tm_sec  = lv_spinbox_get_value(sec);

    datetime_changed = true;

    /* Set system */

    struct timespec tp;

    tp.tv_sec  = mktime(&ts);
    tp.tv_nsec = 0;

    int res = clock_settime(CLOCK_REALTIME, &tp);
    if (res != 0) {
        LV_LOG_ERROR("Can't set system time: %s\n", strerror(errno));
        return;
    }
}

static void datetime_set_rtc_cb(lv_event_t *e) {
    lv_obj_t *obj = lv_event_get_target(e);
    if (!lv_obj_has_state(obj, LV_STATE_EDITED) && datetime_changed) {
        /* Set RTC */

        int rtc = open("/dev/rtc1", O_WRONLY);

        if (rtc > 0) {
            int res = ioctl(rtc, RTC_SET_TIME, &ts);
            if (res != 0) {
                LV_LOG_ERROR("Can't set RTC time: %s\n", strerror(errno));
                return;
            }
            close(rtc);
        } else {
            LV_LOG_ERROR("Can't open /dev/rtc1: %s\n", strerror(errno));
        }
        datetime_changed = false;
    }
}

static void make_date(SettingsPage &page) {
    lv_obj_t *obj;
    uint8_t   col = 1; // page.label() occupies column 0

    /* Label */

    page.label("Day, Month, Year");

    /* Day */

    obj = lv_spinbox_create(page.grid);
    day = obj;

    dialog_item(page.dialog, obj);

    lv_spinbox_set_value(obj, ts.tm_mday);
    lv_spinbox_set_range(obj, 1, 31);
    lv_spinbox_set_digit_format(obj, 2, 0);
    lv_spinbox_set_digit_step_direction(obj, LV_DIR_LEFT);
    lv_obj_set_size(obj, SMALL_2, 56);

    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, col, 2, LV_GRID_ALIGN_CENTER, page.row, 1);
    col += 2;
    lv_obj_add_event_cb(obj, datetime_update_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(obj, datetime_set_rtc_cb, LV_EVENT_FOCUSED, NULL);

    /* Month */

    obj   = lv_spinbox_create(page.grid);
    month = obj;

    dialog_item(page.dialog, obj);

    lv_spinbox_set_value(obj, ts.tm_mon + 1);
    lv_spinbox_set_range(obj, 1, 12);
    lv_spinbox_set_digit_format(obj, 2, 0);
    lv_spinbox_set_digit_step_direction(obj, LV_DIR_LEFT);
    lv_obj_set_size(obj, SMALL_2, 56);

    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, col, 2, LV_GRID_ALIGN_CENTER, page.row, 1);
    col += 2;
    lv_obj_add_event_cb(obj, datetime_update_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(obj, datetime_set_rtc_cb, LV_EVENT_FOCUSED, NULL);

    /* Year */

    obj  = lv_spinbox_create(page.grid);
    year = obj;

    dialog_item(page.dialog, obj);

    lv_spinbox_set_value(obj, ts.tm_year + 1900);
    lv_spinbox_set_range(obj, 2020, 2038);
    lv_spinbox_set_digit_format(obj, 4, 0);
    lv_spinbox_set_digit_step_direction(obj, LV_DIR_LEFT);
    lv_obj_set_size(obj, SMALL_2, 56);

    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, col, 2, LV_GRID_ALIGN_CENTER, page.row, 1);
    col += 2;
    lv_obj_add_event_cb(obj, datetime_update_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(obj, datetime_set_rtc_cb, LV_EVENT_FOCUSED, NULL);

    page.row++;
}

static void make_time(SettingsPage &page) {
    lv_obj_t *obj;
    uint8_t   col = 1; // page.label() occupies column 0

    /* Label */

    page.label("Hour, Min, Sec");

    /* Hour */

    obj  = lv_spinbox_create(page.grid);
    hour = obj;

    dialog_item(page.dialog, obj);

    lv_spinbox_set_value(obj, ts.tm_hour);
    lv_spinbox_set_range(obj, 0, 23);
    lv_spinbox_set_digit_format(obj, 2, 0);
    lv_spinbox_set_digit_step_direction(obj, LV_DIR_LEFT);
    lv_obj_set_size(obj, SMALL_2, 56);

    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, col, 2, LV_GRID_ALIGN_CENTER, page.row, 1);
    col += 2;
    lv_obj_add_event_cb(obj, datetime_update_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(obj, datetime_set_rtc_cb, LV_EVENT_FOCUSED, NULL);

    /* Min */

    obj = lv_spinbox_create(page.grid);
    min = obj;

    dialog_item(page.dialog, obj);

    lv_spinbox_set_value(obj, ts.tm_min);
    lv_spinbox_set_range(obj, 0, 59);
    lv_spinbox_set_digit_format(obj, 2, 0);
    lv_spinbox_set_digit_step_direction(obj, LV_DIR_LEFT);
    lv_obj_set_size(obj, SMALL_2, 56);

    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, col, 2, LV_GRID_ALIGN_CENTER, page.row, 1);
    col += 2;
    lv_obj_add_event_cb(obj, datetime_update_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(obj, datetime_set_rtc_cb, LV_EVENT_FOCUSED, NULL);

    /* Sec */

    obj = lv_spinbox_create(page.grid);
    sec = obj;

    dialog_item(page.dialog, obj);

    lv_spinbox_set_value(obj, ts.tm_sec);
    lv_spinbox_set_range(obj, 0, 59);
    lv_spinbox_set_digit_format(obj, 2, 0);
    lv_spinbox_set_digit_step_direction(obj, LV_DIR_LEFT);
    lv_obj_set_size(obj, SMALL_2, 56);

    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, col, 2, LV_GRID_ALIGN_CENTER, page.row, 1);
    col += 2;
    lv_obj_add_event_cb(obj, datetime_update_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(obj, datetime_set_rtc_cb, LV_EVENT_FOCUSED, NULL);

    page.row++;
}

/***** BACKLIGHT *****/

static void display_backlight_timeout_update_cb(lv_event_t *e) {
    lv_obj_t *obj = lv_event_get_target(e);

    cfg.display.brightness_timeout()->set((int32_t)lv_spinbox_get_value(obj));

    display_tick();
}

static void display_brightness_update_cb(lv_event_t *e) {
    lv_obj_t *obj = lv_event_get_target(e);

    cfg.display.brightness_normal()->set((int32_t)lv_slider_get_value(obj));
    cfg.display.brightness_idle()->set((int32_t)lv_slider_get_left_value(obj));

    display_set_brightness(cfg.display.brightness_normal()->get());
}

static void display_buttons_update_cb(lv_event_t *e) {
    lv_obj_t *obj = lv_event_get_target(e);

    display_set_buttons_backlight((buttons_light_t)lv_dropdown_get_selected(obj));
}

static void make_display(SettingsPage &page) {
    lv_obj_t *obj;
    uint8_t   col = 1; // page.label() occupies column 0

    /* Label */

    page.label("Timeout, Brightness");

    /* Timeout */

    obj = lv_spinbox_create(page.grid);

    dialog_item(page.dialog, obj);

    lv_spinbox_set_value(obj, cfg.display.brightness_timeout()->get());
    lv_spinbox_set_range(obj, 5, 120);
    lv_spinbox_set_digit_format(obj, 3, 0);
    lv_spinbox_set_digit_step_direction(obj, LV_DIR_LEFT);
    lv_obj_set_size(obj, SMALL_2, 56);

    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, col, 2, LV_GRID_ALIGN_CENTER, page.row, 1);
    col += 2;
    lv_obj_add_event_cb(obj, display_backlight_timeout_update_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* Brightness */

    obj = page.cell(col, 4, SMALL_4);
    col += 4;

    lv_obj_t *brightness_slider = lv_slider_create(obj);

    dialog_item(page.dialog, brightness_slider);

    lv_slider_set_mode(brightness_slider, LV_SLIDER_MODE_RANGE);
    lv_slider_set_value(brightness_slider, cfg.display.brightness_normal()->get(), LV_ANIM_OFF);
    lv_slider_set_left_value(brightness_slider, cfg.display.brightness_idle()->get(), LV_ANIM_OFF);
    lv_slider_set_range(brightness_slider, -1, 9);
    lv_obj_set_width(brightness_slider, SMALL_4 - 30);
    lv_obj_center(brightness_slider);

    lv_obj_add_event_cb(brightness_slider, display_brightness_update_cb, LV_EVENT_VALUE_CHANGED, NULL);

    page.row++;
    page.label("Buttons brightness");

    obj = lv_dropdown_create(page.grid);

    dialog_item(page.dialog, obj);

    lv_obj_set_size(obj, SMALL_6, 56);
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, 1, 6, LV_GRID_ALIGN_CENTER, page.row, 1);
    lv_obj_center(obj);

    lv_obj_t *list = lv_dropdown_get_list(obj);
    lv_obj_add_style(list, &style.dialog.dropdown, 0);

    lv_dropdown_set_options(obj, " Always Off \n Always On \n Temporarily On ");
    lv_dropdown_set_symbol(obj, NULL);
    lv_dropdown_set_selected(obj, cfg.display.brightness_buttons()->get());
    lv_obj_add_event_cb(obj, display_buttons_update_cb, LV_EVENT_VALUE_CHANGED, NULL);

    page.row++;
}

/***** LINE-IN, LINE-OUT *****/

static void make_line_gain(SettingsPage &page) {
    page.label("Line-in, Line-out");

    lv_obj_t *cell = page.cell(1, 3, SMALL_3);
    page.slider_int(cell, *cfg.radio.line_in(), 0, 36, 1, SMALL_3 - 30 - 60, "%d");

    cell = page.cell(4, 3, SMALL_3);
    page.slider_int(cell, *cfg.radio.line_out(), 0, 36, 1, SMALL_3 - 30 - 60, "%d");

    page.row++;
}

/***** PLAY, REC GAIN *****/

static float play_gain_transform(int32_t raw) {
    return audio_set_play_vol((float)raw);
}

static float rec_gain_transform(int32_t raw) {
    return audio_set_rec_vol((float)raw);
}

static void make_audio_gain(SettingsPage &page) {
    page.label("Play,Rec gain");

    lv_obj_t *cell = page.cell(1, 3, SMALL_3);
    page.slider_float(cell, *cfg.audio.play_gain_db(), -10.0f, 10.0f, 1.0f, SMALL_3 - 30 - 65, "%.1f",
                      play_gain_transform);

    cell = page.cell(4, 3, SMALL_3);
    page.slider_float(cell, *cfg.audio.rec_gain_db(), -10.0f, 10.0f, 1.0f, SMALL_3 - 30 - 65, "%.1f",
                      rec_gain_transform);

    page.row++;
}

/***** SPEAKER MODE *****/

static void sp_mode_update_cb(lv_event_t *e) {
    lv_obj_t *obj = lv_event_get_target(e);

    bool on = lv_obj_has_state(obj, LV_STATE_CHECKED);

    voice_say_bool("Speaker mode", on);
    cfg.radio.spmode()->set(on);
}

static void make_sp_mode(SettingsPage &page) {
    page.label("Speaker mode");

    lv_obj_t *obj = page.cell(4, 3, SMALL_3);

    obj = lv_switch_create(obj);

    dialog_item(page.dialog, obj);

    lv_obj_center(obj);
    lv_obj_add_event_cb(obj, sp_mode_update_cb, LV_EVENT_VALUE_CHANGED, NULL);

    if (cfg.radio.spmode()->get()) {
        lv_obj_add_state(obj, LV_STATE_CHECKED);
    }

    lv_obj_set_width(obj, SMALL_3 - 30);

    page.row++;
}

/***** COMP THRESHOLD, MAKEUP *****/

#define COMP_TH_MAKEUP_STEP 0.5f

static void make_comp_th_makeup(SettingsPage &page) {
    page.label("Comp threshold, makeup");

    lv_obj_t *cell = page.cell(1, 3, SMALL_3);
    page.slider_float(cell, *cfg.dsp.comp_threshold_offset(), -15.0f, 15.0f, COMP_TH_MAKEUP_STEP, SMALL_3 - 120,
                      "%0.1f");

    cell = page.cell(4, 3, SMALL_3);
    page.slider_float(cell, *cfg.dsp.comp_makeup_offset(), -15.0f, 15.0f, COMP_TH_MAKEUP_STEP, SMALL_3 - 120, "%0.1f");

    page.row++;
}

/***** TX IQ OFFSETS *****/

#define TX_OFFSET_SCALE 50

static void on_tx_offset_change(Subject *subj, void *user_data) {
    lv_obj_t *slider = (lv_obj_t *)user_data;
    auto     *subj_t = static_cast<SubjectInt *>(subj);
    lv_slider_set_value(slider, subj_t->get() / TX_OFFSET_SCALE, LV_ANIM_OFF);
    lv_event_send(slider, LV_EVENT_VALUE_CHANGED, NULL);
}

static void make_tx_offset(SettingsPage &page) {
    lv_obj_t *cell;

    page.label("TX IQ offsets");

    cell = page.cell(1, 3, SMALL_3);

    lv_obj_t *slider =
        page.slider_int(cell, *cfg.band.tx_i_offset(), -10000, 10000, TX_OFFSET_SCALE, SMALL_3 - 110, "%d", nullptr, 1);

    Observer *observer = cfg.band.tx_i_offset()->subscribe(on_tx_offset_change, slider);
    page.observers.emplace_back(observer);

    cell = page.cell(4, 3, SMALL_3);

    slider =
        page.slider_int(cell, *cfg.band.tx_q_offset(), -10000, 10000, TX_OFFSET_SCALE, SMALL_3 - 110, "%d", nullptr, 1);

    observer = cfg.band.tx_q_offset()->subscribe(on_tx_offset_change, slider);
    page.observers.emplace_back(observer);

    page.row++;
}

/***** TX CODEC GAIN *****/

#define OUTPUT_GAIN_STEP 0.2f

static void make_codec_gain(SettingsPage &page) {
    page.label("TX codec gain");

    lv_obj_t *cell = page.cell(1, 3, SMALL_6);
    page.slider_float(cell, *cfg.dsp.output_gain(), -25.0f, 25.0f, OUTPUT_GAIN_STEP, SMALL_6 - 120, "%0.1f");

    page.row++;
}

/***** TX CODEC DAC GAIN *****/

static void on_dac_gain_change(Subject *subj, void *user_data) {
    lv_obj_t *slider = (lv_obj_t *)user_data;
    lv_slider_set_value(slider, cfg.band.dac_offset()->get() / OUTPUT_GAIN_STEP, LV_ANIM_OFF);
    lv_event_send(slider, LV_EVENT_VALUE_CHANGED, NULL);
}

static void band_out_gain_correction(SettingsPage &page) {
    page.label("Band output gain corr");

    lv_obj_t *cell = page.cell(1, 3, SMALL_6);

    lv_obj_t *slider =
        page.slider_float(cell, *cfg.band.dac_offset(), -6.0f, 6.0f, OUTPUT_GAIN_STEP, SMALL_6 - 120, "%0.1f");

    Observer *observer = cfg.band.dac_offset()->subscribe(on_dac_gain_change, slider);
    page.observers.emplace_back(observer);

    page.row++;
}

/***** FM PRE/DE-EMPHASIS *****/

static void make_fm_emphasis(SettingsPage &page) {
    page.label("FM pre/de-emphasis");

    lv_obj_t *obj = page.cell(4, 3, SMALL_3);

    obj = page.switch_bool(obj, *cfg.dsp.fm_emphasis());
    lv_obj_set_width(obj, SMALL_3 - 30);

    page.row++;
}

/***** TX FILTER *****/

#define TX_FILTER_STEP 10

static void make_tx_filter(SettingsPage &page) {
    page.label("TX filter low");

    lv_obj_t *cell = page.cell(1, 3, SMALL_6);
    page.slider_int(cell, *cfg.dsp.tx_filter_low(), 50, 400, TX_FILTER_STEP, SMALL_6 - 150, "%d");
    page.row++;

    page.label("TX filter high");

    cell = page.cell(1, 3, SMALL_6);
    page.slider_int(cell, *cfg.dsp.tx_filter_high(), 2000, 4000, TX_FILTER_STEP, SMALL_6 - 150, "%d");
    page.row++;
}

/***** CESSB *****/

#define CESSB_POWER_UP_STEP 0.1f

static void make_cessb(SettingsPage &page) {
    page.label("CESSB on, level");

    /* on */
    lv_obj_t *cell = page.cell(1, 2, SMALL_2);

    lv_obj_t *obj = page.switch_bool(cell, *cfg.dsp.cessb_on());
    lv_obj_set_width(obj, SMALL_2 - 30);

    /* level */
    cell = page.cell(3, 4, SMALL_4);

    page.slider_float(cell, *cfg.dsp.cessb_power_up(), 0.0f, 5.0f, CESSB_POWER_UP_STEP, SMALL_3 - 40, "%0.1f");

    page.row++;
}

/***** CHARGER *****/

static void make_charger(SettingsPage &page) {
    page.label("Charger");

    lv_obj_t *obj = page.dropdown_int(page.grid, *cfg.radio.charger(), " Off \n On \n Shadow", nullptr,
                                      []() { radio_set_charger(cfg.radio.charger()->get() == RADIO_CHARGER_ON); });

    lv_obj_set_size(obj, SMALL_6, 56);
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, 1, 6, LV_GRID_ALIGN_CENTER, page.row, 1);
    lv_obj_center(obj);

    page.row++;
}

/***** TRANSVERTER *****/

static void transverter_update_cb(lv_event_t *e) {
    lv_obj_t *obj   = lv_event_get_target(e);
    ParamInt *param = (ParamInt *)lv_event_get_user_data(e);

    param->set(lv_spinbox_get_value(obj) * 1000000L);
}

static void make_transverter(SettingsPage &page, ParamInt &from, ParamInt &to, ParamInt &shift) {
    lv_obj_t *obj;
    uint8_t   col = 0;

    /* Label */

    obj = lv_label_create(page.grid);

    lv_label_set_text_fmt(obj, "Transverter %i", from.context_id() + 1);
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, col++, 1, LV_GRID_ALIGN_CENTER, page.row, 1);

    /* From */

    obj = lv_spinbox_create(page.grid);

    dialog_item(page.dialog, obj);

    lv_spinbox_set_value(obj, from.get() / 1000000L);
    lv_spinbox_set_range(obj, 70, 500);
    lv_spinbox_set_digit_format(obj, 3, 0);
    lv_spinbox_set_digit_step_direction(obj, LV_DIR_LEFT);
    lv_obj_set_size(obj, SMALL_2, 56);

    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, col, 2, LV_GRID_ALIGN_CENTER, page.row, 1);
    col += 2;
    lv_obj_add_event_cb(obj, transverter_update_cb, LV_EVENT_VALUE_CHANGED, &from);

    /* To */

    obj = lv_spinbox_create(page.grid);

    dialog_item(page.dialog, obj);

    lv_spinbox_set_value(obj, to.get() / 1000000L);
    lv_spinbox_set_range(obj, 70, 500);
    lv_spinbox_set_digit_format(obj, 3, 0);
    lv_spinbox_set_digit_step_direction(obj, LV_DIR_LEFT);
    lv_obj_set_size(obj, SMALL_2, 56);

    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, col, 2, LV_GRID_ALIGN_CENTER, page.row, 1);
    col += 2;
    lv_obj_add_event_cb(obj, transverter_update_cb, LV_EVENT_VALUE_CHANGED, &to);

    /* Shift */

    obj = lv_spinbox_create(page.grid);

    dialog_item(page.dialog, obj);

    lv_spinbox_set_value(obj, shift.get() / 1000000L);
    lv_spinbox_set_range(obj, 42, 500);
    lv_spinbox_set_digit_format(obj, 3, 0);
    lv_spinbox_set_digit_step_direction(obj, LV_DIR_LEFT);
    lv_obj_set_size(obj, SMALL_2, 56);

    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, col, 2, LV_GRID_ALIGN_CENTER, page.row, 1);
    col += 2;
    lv_obj_add_event_cb(obj, transverter_update_cb, LV_EVENT_VALUE_CHANGED, &shift);

    page.row++;
}

/***** PAGE *****/

void make_general_page(SettingsPage &page) {
    page.reset();

    x6100_base_ver_t base_ver = x6100_control_get_base_ver();

    now          = time(NULL);
    struct tm *t = localtime(&now);

    memcpy(&ts, t, sizeof(ts));

    make_date(page);
    make_time(page);
    page.delimiter();

    make_display(page);
    page.delimiter();

    make_line_gain(page);
    page.delimiter();

    make_audio_gain(page);
    page.delimiter();

    make_sp_mode(page);
    page.delimiter();

    if (base_ver.rev >= 3) {
        make_comp_th_makeup(page);
        page.delimiter();
    }

    make_tx_offset(page);
    page.delimiter();

    if (base_ver.rev >= 3) {
        make_codec_gain(page);
        page.delimiter();
    }

    if (base_ver.rev >= 8) {
        band_out_gain_correction(page);
        page.delimiter();
        make_fm_emphasis(page);
        page.delimiter();
    }

    if (base_ver.rev >= 9) {
        make_tx_filter(page);
        page.delimiter();
    }

    if (base_ver.rev >= 12) {
        make_cessb(page);
        page.delimiter();
    }

    make_charger(page);
    page.delimiter();

    make_transverter(page, *cfg.transverter.t0_from(), *cfg.transverter.t0_to(), *cfg.transverter.t0_shift());
    make_transverter(page, *cfg.transverter.t1_from(), *cfg.transverter.t1_to(), *cfg.transverter.t1_shift());

    page.finish();
}

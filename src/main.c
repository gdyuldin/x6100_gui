/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

#include "lvgl/lvgl.h"
#include "lv_drivers/display/fbdev.h"
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <sys/time.h>

#include "main.h"
#include "main_screen.h"
#include "styles.h"
#include "radio.h"
#include "dsp.h"
#include "util.h"
#include "keyboard.h"
#include "spectrum.h"
#include "waterfall.h"
#include "keypad.h"
#include "params/params.h"
#include "bands.h"
#include "audio.h"
#include "cw.h"
#include "pannel.h"
#include "cat.h"
#include "rtty.h"
#include "backlight.h"
#include "events.h"
#include "gps.h"
#include "mfk.h"
#include "vol.h"
#include "qso_log.h"
#include "scheduler.h"

#define DISP_BUF_SIZE (800 * 480 * 4)

rotary_t                    *vol;
encoder_t                   *mfk;

static lv_color_t           buf[DISP_BUF_SIZE];
static lv_disp_draw_buf_t   disp_buf;
static lv_disp_drv_t        disp_drv;

void * tick_thread (void *args);

int main(void) {
    lv_init();
    // lv_png_init();

    fbdev_init();
    audio_init();
    event_init();

    lv_disp_draw_buf_init(&disp_buf, buf, NULL, DISP_BUF_SIZE);
    lv_disp_drv_init(&disp_drv);

    disp_drv.draw_buf   = &disp_buf;
    disp_drv.flush_cb   = fbdev_flush;
    disp_drv.hor_res    = 480;
    disp_drv.ver_res    = 800;
    disp_drv.sw_rotate  = 1;
    disp_drv.rotated    = LV_DISP_ROT_90;

    lv_disp_drv_register(&disp_drv);

    lv_disp_set_bg_color(lv_disp_get_default(), lv_color_black());
    lv_disp_set_bg_opa(lv_disp_get_default(), LV_OPA_COVER);

    keyboard_init();

    keypad_t *keypad = keypad_init("/dev/input/event0");
    keypad_t *power = keypad_init("/dev/input/event4");

    rotary_t *main = rotary_init("/dev/input/event1");

    vol = rotary_init("/dev/input/event2");
    mfk = encoder_init("/dev/input/event3");

    vol->left[VOL_EDIT] = KEY_VOL_LEFT_EDIT;
    vol->right[VOL_EDIT] = KEY_VOL_RIGHT_EDIT;

    vol->left[VOL_SELECT] = KEY_VOL_LEFT_SELECT;
    vol->right[VOL_SELECT] = KEY_VOL_RIGHT_SELECT;

    params_init();
    mfk_change_mode(0);
    vol_change_mode(0);
    styles_init();

    dsp_init(params_current_mode_spectrum_factor_get());
    lv_obj_t *main_obj = main_screen();

    cw_init();
    rtty_init();
    radio_init(
        &main_screen_notify_tx,
        &main_screen_notify_rx,
        &main_screen_notify_atu_update
    );
    backlight_init();
    cat_init();
    pannel_visible();
    gps_init();
    if (!qso_log_init()) {
        LV_LOG_ERROR("Can't init QSO log");
    }
    qso_log_import_adif("/mnt/incoming_log.adi");

    pthread_t thread;
    pthread_create(&thread, NULL, tick_thread, NULL);
    pthread_detach(thread);

#if 0
    lv_obj_set_style_bg_opa(lv_scr_act(), LV_OPA_0, 0);
    lv_scr_load_anim(main_obj, LV_SCR_LOAD_ANIM_FADE_IN, 250, 0, false);
#else
    lv_scr_load(main_obj);
#endif

    int64_t next_loop_time, sleep_time;
    while (1) {
        next_loop_time = get_time() + lv_timer_handler();
        event_obj_check();
        scheduler_work();
        sleep_time = next_loop_time - get_time();
        if (sleep_time > 0) {
            usleep(sleep_time * 1000);
        }
    }
    return 0;
}

void * tick_thread (void *args)
{
      while(1) {
        usleep(5 * 1000);    /*Sleep for 5 millisecond*/
        lv_tick_inc(5);      /*Tell LVGL that 5 milliseconds were elapsed*/
    }
}

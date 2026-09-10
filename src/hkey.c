/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

#include <stdlib.h>
#include <aether_radio/x6100_control/low/flow.h>
#include "lvgl/lvgl.h"

#include "hkey.h"
#include "events.h"
#include "display.h"
#include "keyboard.h"

#define HKEY_LONG_TIME 1000

static event_hkey_t     event = { .state = HKEY_RELEASE };
static lv_timer_t       *timer = NULL;

static void hkey_event() {
    event_hkey_t    *e = malloc(sizeof(event_hkey_t));
    
    *e = event;
    event_send(lv_scr_act(), EVENT_HKEY, (void*) e);
}

static void hkey_key(int32_t key) {
    if (event.state == HKEY_RELEASE || event.state == HKEY_LONG_RELEASE) {
        event_send_key(key);
        event.state = HKEY_PRESS;
        event.key = HKEY_UNKNOWN;
    }
}

static void hkey_timer(lv_timer_t *t) {
    event.state = HKEY_LONG;
    hkey_event();
    timer = NULL;
}

void hkey_put(uint32_t key) {
    switch (key) {
        case 0:
            switch (event.state) {
                case HKEY_PRESS:
                    event.state = HKEY_RELEASE;
                    
                    if (event.key != HKEY_UNKNOWN) {
                        hkey_event();
                    }
                    break;

                case HKEY_LONG:
                    event.state = HKEY_LONG_RELEASE;

                    if (event.key != HKEY_UNKNOWN) {
                        hkey_event();
                    }
                    break;
                    
                default:
                    break;
            }
            
            if (timer) {
                lv_timer_del(timer);
                timer = NULL;
            }
            return;

        /* * */

        case X6100_HKEY_1:
            if (lv_group_get_editing(keyboard_group)) {
                event.key = HKEY_1;
            } else {
                hkey_key('1');
            }
            break;

        case X6100_HKEY_2:
            if (lv_group_get_editing(keyboard_group)) {
                event.key = HKEY_2;
            } else {
                hkey_key('2');
            }
            break;

        case X6100_HKEY_3:
            if (lv_group_get_editing(keyboard_group)) {
                event.key = HKEY_3;
            } else {
                hkey_key('3');
            }
            break;

        case X6100_HKEY_4:
            if (lv_group_get_editing(keyboard_group)) {
                event.key = HKEY_4;
            } else {
                hkey_key('4');
            }
            break;

        case X6100_HKEY_5:
            if (lv_group_get_editing(keyboard_group)) {
                event.key = HKEY_5;
            } else {
                hkey_key('5');
            }
            break;

        case X6100_HKEY_6:
            if (lv_group_get_editing(keyboard_group)) {
                event.key = HKEY_6;
            } else {
                hkey_key('6');
            }
            break;

        case X6100_HKEY_7:
            if (lv_group_get_editing(keyboard_group)) {
                event.key = HKEY_7;
            } else {
                hkey_key('7');
            }
            break;

        case X6100_HKEY_8:
            if (lv_group_get_editing(keyboard_group)) {
                event.key = HKEY_8;
            } else {
                hkey_key('8');
            }
            break;

        case X6100_HKEY_9:
            if (lv_group_get_editing(keyboard_group)) {
                event.key = HKEY_9;
            } else {
                hkey_key('9');
            }
            break;

        case X6100_HKEY_DOT:
            if (lv_group_get_editing(keyboard_group)) {
                event.key = HKEY_DOT;
            } else {
                hkey_key('.');
            }
            break;

        case X6100_HKEY_0:
            if (lv_group_get_editing(keyboard_group)) {
                event.key = HKEY_0;
            } else {
                hkey_key('0');
            }
            break;

        case X6100_HKEY_CE:
            hkey_key(LV_KEY_BACKSPACE);
            break;

        case X6100_HKEY_FINP:
            hkey_key(HKEY_FINP);
            break;

        /* * */

        case X6100_HKEY_SPCH:
            event.key = HKEY_SPCH;
            break;
                
        case X6100_HKEY_TUNER:
            event.key = HKEY_TUNER;
            break;

        case X6100_HKEY_XFC:
            event.key = HKEY_XFC;
            break;
    
        case X6100_HKEY_UP:
            event.key = HKEY_UP;
            break;

        case X6100_HKEY_DOWN:
            event.key = HKEY_DOWN;
            break;

        case X6100_HKEY_VM:
            event.key = HKEY_VM;
            break;

        case X6100_HKEY_NW:
            event.key = HKEY_NW;
            break;

        case X6100_HKEY_F1:
            event.key = HKEY_F1;
            break;

        case X6100_HKEY_F2:
            event.key = HKEY_F2;
            break;

        case X6100_HKEY_MODE:
            event.key = HKEY_MODE;
            break;

        case X6100_HKEY_FIL:
            event.key = HKEY_FIL;
            break;

        case X6100_HKEY_GENE:
            event.key = HKEY_GENE;
            break;

       default:
            event.key = HKEY_UNKNOWN;
            break;
    }
    
    switch (event.state) {
        case HKEY_RELEASE:
        case HKEY_LONG_RELEASE:
            event.state = HKEY_PRESS;
            display_tick();
            
            if (event.key != HKEY_UNKNOWN) {
                hkey_event();

                if (timer) {
                    lv_timer_del(timer);
                    timer = NULL;
                }

                timer = lv_timer_create(hkey_timer, HKEY_LONG_TIME, NULL);
                lv_timer_set_repeat_count(timer, 1);
            }
            break;

        default:
            break;
    }
}

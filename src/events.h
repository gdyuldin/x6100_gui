/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

#pragma once

#include "lvgl/lvgl.h"

#include <unistd.h>
#include <stdint.h>

typedef enum {
    KEYPAD_UNKNOWN = 0,
    
    KEYPAD_POWER,    
    KEYPAD_GEN,
    KEYPAD_APP,
    KEYPAD_KEY,
    KEYPAD_MSG,
    KEYPAD_DFN,
    KEYPAD_DFL,
    
    KEYPAD_F1,
    KEYPAD_F2,
    KEYPAD_F3,
    KEYPAD_F4,
    KEYPAD_F5,
    KEYPAD_LOCK,
    
    KEYPAD_PTT,
    KEYPAD_BAND_DOWN,
    KEYPAD_BAND_UP,
    KEYPAD_MODE_AM,
    KEYPAD_MODE_CW,
    KEYPAD_MODE_SSB,
    
    KEYPAD_AB,
    KEYPAD_PRE,
    KEYPAD_ATU,
    KEYPAD_VM,
    KEYPAD_AGC,
    KEYPAD_FST
} keypad_key_t;

typedef enum {
    KEYPAD_PRESS = 0,
    KEYPAD_RELEASE,
    KEYPAD_LONG,
    KEYPAD_LONG_RELEASE
} keypad_state_t;

typedef struct {
    keypad_key_t    key;
    keypad_state_t  state;
} event_keypad_t;

typedef enum {
    HKEY_UNKNOWN = 0,

    HKEY_SPCH,
    HKEY_TUNER,
    HKEY_XFC,
    HKEY_UP,
    HKEY_DOWN,
    HKEY_VM,
    HKEY_NW,
    HKEY_F1,
    HKEY_F2,
    HKEY_1,
    HKEY_2,
    HKEY_3,
    HKEY_4,
    HKEY_5,
    HKEY_6,
    HKEY_7,
    HKEY_8,
    HKEY_9,
    HKEY_DOT,
    HKEY_0, 
    HKEY_CE,
    HKEY_MODE,
    HKEY_FIL,
    HKEY_GENE
} hkey_t;

typedef enum {
    HKEY_PRESS = 0,
    HKEY_RELEASE,
    HKEY_LONG,
    HKEY_LONG_RELEASE
} hkey_state_t;

typedef struct {
    hkey_t          key;
    hkey_state_t    state;
} event_hkey_t;

typedef enum {
//  LV_KEY_HOME             = 2,
//  LV_KEY_END              = 3,
    KEY_VOL_LEFT_EDIT       = 4,
    KEY_VOL_RIGHT_EDIT      = 5,
    KEY_VOL_LEFT_SELECT     = 6,
    KEY_VOL_RIGHT_SELECT    = 7,
//  LV_KEY_BACKSPACE        = 8,
//  LV_KEY_NEXT             = 9,
//  LV_KEY_ENTER            = 10,
//  LV_KEY_PREV             = 11,
    HKEY_FINP               = 12
//  LV_KEY_UP               = 17,
//  LV_KEY_DOWN             = 18,
//  LV_KEY_RIGHT            = 19,
//  LV_KEY_LEFT             = 20,
//  LV_KEY_ESC              = 27,
//  LV_KEY_DEL              = 127,
} keys_t;

extern uint32_t EVENT_MAIN_ROTARY;
extern uint32_t EVENT_VOL_ROTARY;
extern uint32_t EVENT_MFK_ROTARY;
extern uint32_t EVENT_KEYPAD;
extern uint32_t EVENT_HKEY;
extern uint32_t EVENT_RADIO_TX;
extern uint32_t EVENT_RADIO_RX;
extern uint32_t EVENT_PANNEL_UPDATE;
extern uint32_t EVENT_SCREEN_UPDATE;
extern uint32_t EVENT_ATU_UPDATE;
extern uint32_t EVENT_MSG_UPDATE;
extern uint32_t EVENT_FREQ_UPDATE;
extern uint32_t EVENT_FT8_MSG;
extern uint32_t EVENT_GPS;
extern uint32_t EVENT_BAND_UP;
extern uint32_t EVENT_BAND_DOWN;

void event_init();

void event_obj_check();
void event_send(lv_obj_t *obj, lv_event_code_t event_code, void *param);
void event_send_key(int32_t key);

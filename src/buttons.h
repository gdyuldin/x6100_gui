/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

#pragma once

#define BUTTONS 5

#ifdef __cplusplus
#include "cfg/subjects.h"
#endif


#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl/lvgl.h"
#include "params/params.h"
#include "main_screen.h"
#include "mfk.h"
#include "vol.h"

#define BTN_HEIGHT 62

typedef enum {
    BTN_EMPTY,
    BTN_TEXT,
    BTN_TEXT_FN,
} btn_type_t;

typedef struct button_item_t {
    btn_type_t type;
    union {
        const char *label;
        const char *(*label_fn)();
    };
    void (*press)(struct button_item_t *);
    void (*hold)(struct button_item_t *);
    const char *voice;
    // next/prev page
    struct buttons_page_t *next;
    struct buttons_page_t *prev;
    int32_t                data;
    lv_obj_t              *label_obj;
    Subject              **subj;
    Observer              *observer;
    bool                   mark;
    bool                   disabled;
} button_item_t;

typedef struct buttons_page_t {
    button_item_t *items[BUTTONS];
} buttons_page_t;

typedef buttons_page_t *buttons_group_t[];

extern buttons_page_t buttons_page_vol_1;

extern buttons_page_t buttons_page_msg_cw_1;
extern buttons_page_t buttons_page_msg_cw_2;

extern buttons_page_t buttons_page_rtty;

extern buttons_group_t buttons_group_gen;
extern buttons_group_t buttons_group_app;
extern buttons_group_t buttons_group_key;
extern buttons_group_t buttons_group_dfn;
extern buttons_group_t buttons_group_dfl;
extern buttons_group_t buttons_group_vm;

// TODO: move to applications
extern buttons_group_t buttons_group_msg_cw;
extern buttons_group_t buttons_group_msg_voice;

void            buttons_init(lv_obj_t *parent);
void            buttons_refresh(button_item_t *item);
void            buttons_mark(button_item_t *item, bool val);
void            buttons_disabled(button_item_t *item, bool val);
void            buttons_load(uint8_t n, button_item_t *item);
void            buttons_load_page(buttons_page_t *page);
void            buttons_unload_page();
void            button_next_page_cb(button_item_t *item);
void            button_prev_page_cb(button_item_t *item);
void            buttons_press(uint8_t n, bool hold);
void            buttons_load_page_group(buttons_group_t group);
buttons_page_t *buttons_get_cur_page();

#ifdef __cplusplus
}
#endif

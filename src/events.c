/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

#include <stdlib.h>
#include <pthread.h>
#include "events.h"
#include "backlight.h"
#include "keyboard.h"

#define QUEUE_SIZE  32

uint32_t        EVENT_MAIN_ROTARY;
uint32_t        EVENT_VOL_ROTARY;
uint32_t        EVENT_MFK_ROTARY;
uint32_t        EVENT_KEYPAD;
uint32_t        EVENT_HKEY;
uint32_t        EVENT_RADIO_TX;
uint32_t        EVENT_RADIO_RX;
uint32_t        EVENT_PANNEL_UPDATE;
uint32_t        EVENT_SCREEN_UPDATE;
uint32_t        EVENT_ATU_UPDATE;
uint32_t        EVENT_MSG_UPDATE;
uint32_t        EVENT_FREQ_UPDATE;
uint32_t        EVENT_FT8_MSG;
uint32_t        EVENT_GPS;
uint32_t        EVENT_BAND_UP;
uint32_t        EVENT_BAND_DOWN;

typedef struct {
    lv_obj_t        *obj;
    lv_event_code_t event_code;
    void            *param;
} item_t;

static item_t           *queue[QUEUE_SIZE];
static uint8_t          queue_write = 0;
static uint8_t          queue_read = 0;
static pthread_mutex_t  queue_mux;

void event_init() {
    EVENT_MAIN_ROTARY = lv_event_register_id();
    EVENT_VOL_ROTARY = lv_event_register_id();
    EVENT_MFK_ROTARY = lv_event_register_id();
    EVENT_KEYPAD = lv_event_register_id();
    EVENT_HKEY = lv_event_register_id();
    EVENT_RADIO_TX = lv_event_register_id();
    EVENT_RADIO_RX = lv_event_register_id();
    EVENT_PANNEL_UPDATE = lv_event_register_id();
    EVENT_SCREEN_UPDATE = lv_event_register_id();
    EVENT_ATU_UPDATE = lv_event_register_id();
    EVENT_MSG_UPDATE = lv_event_register_id();
    EVENT_FREQ_UPDATE = lv_event_register_id();
    EVENT_FT8_MSG = lv_event_register_id();
    EVENT_GPS = lv_event_register_id();
    EVENT_BAND_UP = lv_event_register_id();
    EVENT_BAND_DOWN = lv_event_register_id();

    for (uint8_t i = 0; i < QUEUE_SIZE; i++)
        queue[i] = NULL;

    pthread_mutex_init(&queue_mux, NULL);
}

void event_obj_check() {
    while (queue_read != queue_write) {
        pthread_mutex_lock(&queue_mux);
        queue_read = (queue_read + 1) % QUEUE_SIZE;
        
        item_t *item = queue[queue_read];
        queue[queue_read] = NULL;
        pthread_mutex_unlock(&queue_mux);
        
        if (item) {
            if (item->event_code == LV_EVENT_REFRESH) {
                if (backlight_is_on()) {
                    lv_obj_invalidate(item->obj);
                }
            } else {
                lv_event_send(item->obj, item->event_code, item->param);
            }

            if (item->param != NULL) {
                free(item->param);
            }

            free(item);
        }
    }
}

void event_send(lv_obj_t *obj, lv_event_code_t event_code, void *param) {
    pthread_mutex_lock(&queue_mux);

    uint8_t next = (queue_write + 1) % QUEUE_SIZE;

    if (queue[next]) {
        pthread_mutex_unlock(&queue_mux);
        LV_LOG_ERROR("Overflow");
        return;
    }

    item_t *item = malloc(sizeof(item_t));
    
    item->obj = obj;
    item->event_code = event_code;
    item->param = param;

    queue[next] = item;
    queue_write = next;

    pthread_mutex_unlock(&queue_mux);
}

void event_send_key(int32_t key) {
    int32_t *c = malloc(sizeof(int32_t));
    *c = key;
        
    event_send(lv_group_get_focused(keyboard_group), LV_EVENT_KEY, c);
}

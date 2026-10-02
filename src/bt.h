/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2024 Georgy Dyuldin aka R2RFE
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BT_OFF,
    BT_ON,
    BT_CONNECTING,
    BT_CONNECTED,
} bt_status_t;

typedef enum {
    BT_DEV_UNKNOWN = 0,
    BT_DEV_AUDIO,
    BT_DEV_KEYBOARD,
} bt_dev_class_t;

typedef struct {
    char           bdaddr[18];
    char           name[248];
    bool           paired;
    bool           connected;
    bool           known;
    uint8_t        rssi;
    bt_dev_class_t dev_class;
    uint32_t       cod;
    bool           has_battery;
    uint8_t        battery;
} bt_dev_info_t;

typedef struct {
    bt_dev_info_t *dev_arr;
    uint8_t        count;
} bt_dev_arr_t;

typedef enum {
    BT_PASSKEY_NONE,
    BT_PASSKEY_CONFIRM, /* RequestConfirmation - accept/reject a shown passkey */
    BT_PASSKEY_PIN,     /* RequestPinCode - user types a PIN (string) */
    BT_PASSKEY_ENTRY,   /* RequestPasskey - user types a passkey (uint) */
} bt_passkey_kind_t;

typedef void (*bt_passkey_cb_t)(const char *bdaddr, const char *name, bt_passkey_kind_t kind,
                                const char *pin_hint);

void bt_power_setup();
void bt_cleanup();

bt_status_t bt_get_status();

void bt_power_on();
void bt_power_off();

void bt_start_discovery();
void bt_stop_discovery();
bool bt_is_scanning();
bool bt_is_busy();

bt_dev_arr_t bt_get_devices();
void bt_devices_info_delete(bt_dev_arr_t arr);

void bt_device_class_text(const bt_dev_info_t *info, char *out, size_t sz);

void bt_pair(const char *bdaddr);
void bt_connect(const char *bdaddr);
void bt_disconnect(const char *bdaddr);
void bt_remove(const char *bdaddr);

void bt_set_passkey_cb(bt_passkey_cb_t cb);
void bt_agent_respond(const char *bdaddr, bool accept, const char *pin);

#ifdef __cplusplus
}
#endif

/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2024 Georgy Dyuldin aka R2RFE
 */

#pragma once

#include <NetworkManager.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus

extern "C" {

#endif

typedef enum {
    WIFI_OFF,
    WIFI_STARTING,
    WIFI_DISCONNECTED,
    WIFI_CONNECTING,
    WIFI_CONNECTED,
} wifi_status_t;

typedef gboolean (*password_validator_fn)(const char *);

typedef struct {
    char                  ssid[33];
    char                  bssid[18];
    uint8_t               strength;
    NM80211Mode           mode;
    bool                  is_connected;
    bool                  known;
    password_validator_fn password_validator;
} wifi_ap_info_t;

typedef struct {
    wifi_ap_info_t *ap_arr;
    uint8_t         count;
    bool            is_connected;
} wifi_ap_arr_t;

// typedef void (*wifi_ap_change_cb)(wifi_ap_info_t *ap_info);

void wifi_power_setup();

// Cleanup on app exit
void wifi_cleanup();

/**
 * Turn WiFi/BT chip on
 */
void wifi_power_on();

/**
 * Turn WiFi/BT chip off
 */
void wifi_power_off();

wifi_ap_arr_t wifi_get_available_access_points();

void wifi_aps_info_delete(wifi_ap_arr_t aps_info);

wifi_status_t wifi_get_status();

void wifi_start_scan();

bool wifi_scanning();

void wifi_add_connection(const char *ssid, const char *password);

void wifi_update_connection(const char *id, const char *password);

void wifi_delete_connection(const char *id);

void wifi_connect(const char *id);

void wifi_disconnect();

bool wifi_get_ipaddr(char **ip_addr, char **gateway);

#ifdef __cplusplus
}
#endif

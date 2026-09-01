/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2024 Georgy Dyuldin aka R2RFE
 */

#pragma once

// Messages IDs for UI part messaging (publishing/subscribing)
enum msg_t {
    MSG_WIFI_STATE_CHANGED,
    MSG_USB_DEVICE_CHANGED,
    MSG_RADIO_RX,
    MSG_RADIO_TX,
    MSG_LOW_POWER,
};

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
    // Radio messages
    MSG_RADIO_RX,
    MSG_RADIO_TX,
    MSG_LOW_POWER,

    // Service state messages
    MSG_GPS,
    MSG_RECORDER_START,
    MSG_RECORDER_STOP,

    // UI locks messages
    MSG_LOCK_AB,
    MSG_LOCK_MODE,
    MSG_LOCK_FREQ,
    MSG_LOCK_BAND,

    // Other UI messages
    MSG_DIALOG_START,
    MSG_DIALOG_STOP,

    MSG_PANEL_SHOW,
    MSG_PANEL_HIDE,
};

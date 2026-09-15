/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

#pragma once

#include <gps.h>

typedef enum {
    GPS_STATUS_WAITING=0,
    GPS_STATUS_WORKING,
    GPS_STATUS_RESTARTING,
    GPS_STATUS_EXITED,
} gps_status_t;

void gps_init();

gps_status_t gps_status();

/**
 * Copy the latest GPS data into out. The internal snapshot is guarded by a
 * mutex, so this is safe to call from the main thread while the GPS thread
 * updates it. Intended to be called from a MSG_GPS subscriber.
 */
void gps_get_snapshot(struct gps_data_t *out);

/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

#include "gps.h"

#include "lvgl/lvgl.h"
#include "scheduler.h"
#include "usb_devices.h"
#include "pubsub_ids.h"

#include <errno.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>

static struct gps_data_t    gpsdata;
static struct gps_data_t    snapshot;
static uint64_t             prev_time = 0;
static gps_status_t         status=GPS_STATUS_WAITING;

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  cond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t snapshot_mux = PTHREAD_MUTEX_INITIALIZER;

static bool connect() {
    if (gps_open("localhost", "2947", &gpsdata) == -1) {
        LV_LOG_ERROR("GPSD open: %s", gps_errstr(errno));
        return false;
    }
    gps_stream(&gpsdata, WATCH_ENABLE | WATCH_JSON, NULL);

    return true;
}

static void data_receive() {
    while (gps_waiting(&gpsdata, 5000000)) {
        usleep(100000);
        if (gps_read(&gpsdata, NULL, 0) != -1) {
            if (MODE_SET != (MODE_SET & gpsdata.set)) {
                // did not even get mode, nothing to see here
                continue;
            }
            status = GPS_STATUS_WORKING;

            pthread_mutex_lock(&snapshot_mux);
            memcpy(&snapshot, &gpsdata, sizeof(snapshot));
            pthread_mutex_unlock(&snapshot_mux);

            scheduler_msg_send(MSG_GPS, NULL);
        }
    }
}

static void disconnect() {
    gps_stream(&gpsdata, WATCH_DISABLE, NULL);
    gps_close(&gpsdata);

}

static void * gps_thread(void *arg) {
    while (true) {
        status = GPS_STATUS_WAITING;
        if (connect()) {
            data_receive();
            status = GPS_STATUS_RESTARTING;
            disconnect();
        } else {
            status = GPS_STATUS_WAITING;
            pthread_mutex_lock(&lock);
            pthread_cond_wait(&cond, &lock);
            pthread_mutex_unlock(&lock);
        }
    }
}

static void on_usb_device_change(void * s, lv_msg_t * msg) {
    enum usb_devices_event_t event = (enum usb_devices_event_t)msg->payload;
    if (event == USB_DEV_ADDED) {
        pthread_cond_signal(&cond);
    }
}

void gps_init() {
    pthread_t thread;

    pthread_create(&thread, NULL, gps_thread, NULL);
    pthread_detach(thread);
    lv_msg_subscribe(MSG_USB_DEVICE_CHANGED, on_usb_device_change, NULL);
}


gps_status_t gps_status() {
    return status;
}

void gps_get_snapshot(struct gps_data_t *out) {
    pthread_mutex_lock(&snapshot_mux);
    memcpy(out, &snapshot, sizeof(*out));
    pthread_mutex_unlock(&snapshot_mux);
}

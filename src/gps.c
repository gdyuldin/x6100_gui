/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

#include "gps.h"

#include "lvgl/lvgl.h"
#include "events.h"
#include "dialog_gps.h"

#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>
#include <errno.h>
#include <pthread.h>
#include <math.h>
#include <libudev.h>

static struct gps_data_t    gpsdata;
static uint64_t             prev_time = 0;
static gps_status_t         status=GPS_STATUS_WAITING;

static struct udev *udev;
static struct udev_device *dev;
static struct udev_monitor *mon;
static int fd;


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
            if (dialog_gps->run) {
                struct gps_data_t *msg = malloc(sizeof(struct gps_data_t));

                memcpy(msg, &gpsdata, sizeof(*msg));
                event_send(dialog_gps->obj, EVENT_GPS, msg);
            }
        }
    }
}

static void disconnect() {
    gps_stream(&gpsdata, WATCH_DISABLE, NULL);
    gps_close(&gpsdata);

}

static void wait_new_device() {
	while (1) {
		fd_set fds;
		struct timeval tv;
		int ret;

		FD_ZERO(&fds);
		FD_SET(fd, &fds);
		tv.tv_sec = 0;
		tv.tv_usec = 0;

		ret = select(fd+1, &fds, NULL, NULL, &tv);
		if (ret > 0 && FD_ISSET(fd, &fds)) {
			dev = udev_monitor_receive_device(mon);
			if (dev && (strcmp(udev_device_get_action(dev), "add") == 0)) {
				/* free dev */
				udev_device_unref(dev);
                return;
			}
		}
		/* 500 milliseconds */
		usleep(500*1000);
	}
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
            wait_new_device();
        }
    }
}

void gps_init() {
    /* create udev object */
	udev = udev_new();
	if (!udev) {
		LV_LOG_ERROR("Cannot create udev context.");
	}
    mon = udev_monitor_new_from_netlink(udev, "udev");
	udev_monitor_filter_add_match_subsystem_devtype(mon, "usb", NULL);
	udev_monitor_enable_receiving(mon);
	fd = udev_monitor_get_fd(mon);

    pthread_t thread;

    pthread_create(&thread, NULL, gps_thread, NULL);
    pthread_detach(thread);
}


gps_status_t gps_status() {
    return status;
}

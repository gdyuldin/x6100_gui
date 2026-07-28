/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <linux/input.h>
#include <threads.h>
#include <sys/epoll.h>

#include "backlight.h"
#include "util.h"

extern "C" {
    #include "rotary.h"
    #include "keyboard.h"
}


#define MAX_EVENTS 8


static int32_t remain_diff = 0;
static lv_indev_state_t prev_state;

template <int Size> class MainReadings {
    rotary_data_t   data[Size];
    int8_t          w;
    int8_t          r;
    pthread_mutex_t mux = PTHREAD_MUTEX_INITIALIZER;
    struct timespec prev_time = {0, 0};

  public:
    lv_indev_t    *indev;
    lv_indev_drv_t indev_drv;
    int            dev_fd;
    int            epoll_fd;

    void put(int16_t val) {
        // TODO: check for full

        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);

        float delta_ms = (now.tv_sec - prev_time.tv_sec) * 1000.0f;
        delta_ms += (now.tv_nsec - prev_time.tv_nsec) * 1e-6f;
        delta_ms = LV_MIN(delta_ms, 500.0f);
        prev_time = now;

        pthread_mutex_lock(&mux);
        data[w].diff = val;
        data[w].dt = delta_ms / LV_ABS(val);
        // data[w].speed = (now - prev_time);
        w        = (w + 1) % Size;
        pthread_mutex_unlock(&mux);
    }

    bool get(rotary_data_t *val) {
        int ret = true;
        pthread_mutex_lock(&mux);
        if (r == w) {
            // Empty
            ret = false;
        } else {
            *val = data[r];
            r    = (r + 1) % Size;
        }
        pthread_mutex_unlock(&mux);
        return ret;
    }

    bool empty() {
        bool val;
        pthread_mutex_lock(&mux);
        val = r == w;
        pthread_mutex_unlock(&mux);
        return val;
    }
};

static MainReadings<16> main_readings;


static void * main_knob_read_thread(void *args) {
    struct input_event  in;
    struct epoll_event events[MAX_EVENTS];

    while (true) {
        int16_t             diff = 0;

        // Wait indefinitely for input events
        int nfds = epoll_wait(main_readings.epoll_fd, events, MAX_EVENTS, -1);

        for (int i = 0; i < nfds; i++) {
            if (events[i].events & EPOLLIN) {
                while (read(events[i].data.fd, &in, sizeof(in)) > 0) {
                    if (in.type == EV_REL) {
                        diff += in.value;
                    }
                }
            }
        }

        if (diff) {
            main_readings.put(diff);
        }
    }
    return NULL;
}

static void rotary_main_input_read(lv_indev_drv_t *drv, lv_indev_data_t *data) {
    // Read events from main_readings
    rotary_data_t diff;
    if (main_readings.get(&diff)) {
        backlight_tick();
        rotary_data_t *diff_copy = (rotary_data_t *) malloc(sizeof(rotary_data_t));
        *diff_copy = diff;
        lv_event_send(lv_scr_act(), EVENT_ROTARY, (void *) diff_copy);
        if (!main_readings.empty()) {
            data->continue_reading = 1;
        }
    }
}


static void rotary_input_read(lv_indev_drv_t *drv, lv_indev_data_t *data) {
    struct input_event  in;
    rotary_t            *rotary = (rotary_t*) drv->user_data;
    int32_t             diff = 0;
    bool                send = false;

    if (remain_diff == 0) {
        while (read(rotary->fd, &in, sizeof(struct input_event)) > 0) {
            if (in.type == EV_REL) {
                diff += in.value;
                send = true;
            }
        }

        if (send) {
            backlight_tick();

            data->continue_reading = 1;
            remain_diff = diff;
        }
    } else if (prev_state == LV_INDEV_STATE_PRESSED) {
        data->state = LV_INDEV_STATE_RELEASED;
        if (remain_diff != 0) {
            data->continue_reading = 1;
        }
    } else {
        if (remain_diff > 0) {
            diff = 1;
        } else {
            diff = -1;
        }
        remain_diff -= diff;
        data->state = LV_INDEV_STATE_PRESSED;
        data->key = diff > 0 ? rotary->left[rotary->state] : rotary->right[rotary->state];
        data->continue_reading = 1;

    }
    prev_state = data->state;
}

static int open_dev(char *dev_name) {
    int fd = open(dev_name, O_RDWR | O_NOCTTY | O_NDELAY);

    if (fd == -1) {
        LV_LOG_ERROR("unable to open rotary interface:");

        return fd;
    }

    int ret = fcntl(fd, F_SETFL, O_ASYNC | O_NONBLOCK);
    if (ret == -1) {
        perror("Can't set descriptor properties");
        return ret;
    }
    return fd;
}


rotary_t * rotary_init(char *dev_name) {
    int fd = open_dev(dev_name);
    if (fd == -1) {
        return NULL;
    }

    rotary_t *rotary = (rotary_t*)malloc(sizeof(rotary_t));

    memset(rotary, 0, sizeof(rotary_t));
    rotary->fd = fd;

    lv_indev_drv_init(&rotary->indev_drv);

    rotary->indev_drv.type = LV_INDEV_TYPE_KEYPAD;
    rotary->indev_drv.read_cb = rotary_input_read;
    rotary->indev_drv.user_data = rotary;

    rotary->indev = lv_indev_drv_register(&rotary->indev_drv);

    lv_indev_set_group(rotary->indev, keyboard_group);

    return rotary;
}


void rotary_main_init(char *dev_name) {
    int epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) {
        LV_LOG_ERROR("Can't create epoll for main rotary fd");
        return;
    }

    int dev_fd = open_dev(dev_name);

    if (dev_fd == -1) {
        close(epoll_fd);
        return;
    }

    // Register device with epoll
    struct epoll_event ev = {
        .events = EPOLLIN,
    };
    ev.data.fd = dev_fd;

    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, dev_fd, &ev);


    main_readings.dev_fd = dev_fd;
    main_readings.epoll_fd = epoll_fd;

    pthread_t thread;
    pthread_create(&thread, NULL, main_knob_read_thread, NULL);
    pthread_detach(thread);

    lv_indev_drv_init(&main_readings.indev_drv);

    main_readings.indev_drv.type = LV_INDEV_TYPE_KEYPAD;
    main_readings.indev_drv.read_cb = rotary_main_input_read;

    main_readings.indev = lv_indev_drv_register(&main_readings.indev_drv);

    lv_indev_set_group(main_readings.indev, keyboard_group);
}

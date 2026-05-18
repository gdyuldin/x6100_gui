/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2026
 */

#include "remote_screen.h"

#include <sys/stat.h>
#include <time.h>

#include "screenshot.h"
#include "util.h"

/* Use tmpfs; JPG via stb_image_write (faster, smaller, no extra lib) */
#define REMOTE_SCREEN_PATH "/dev/shm/remote_screen.jpg"
#define REMOTE_SCREEN_REQ_PATH "/tmp/remote_screen.req"
#define REMOTE_SCREEN_POLL_MS 200

static time_t last_req_mtime = 0;
static uint64_t last_stat_check = 0;

void remote_screen_tick() {
    uint64_t now = get_time();
    if (now - last_stat_check < REMOTE_SCREEN_POLL_MS) {
        return;
    }
    last_stat_check = now;

    struct stat st;
    if (stat(REMOTE_SCREEN_REQ_PATH, &st) != 0) {
        return;
    }

    if (st.st_mtime == last_req_mtime) {
        return;
    }

    if (screenshot_take_to_path(REMOTE_SCREEN_PATH, false)) {
        last_req_mtime = st.st_mtime;
    }
}

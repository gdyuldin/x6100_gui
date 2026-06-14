/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <png.h>
#include <pthread.h>

#include "lvgl/lvgl.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include "screenshot.h"
#include "util.h"
#include "msg.h"

static char         file_str_final[128];
static char         file_str_tmp[128];
static lv_img_dsc_t snapshot;
static uint8_t      *rows[480];
static uint8_t      *buf;
static bool         screenshot_notify = true;
static bool         screenshot_busy = false;
static pthread_mutex_t screenshot_mux = PTHREAD_MUTEX_INITIALIZER;

static int path_is_jpeg(const char *path) {
    size_t len = path ? strlen(path) : 0;
    if (len >= 4 && strcmp(path + len - 4, ".jpg") == 0)
        return 1;
    if (len >= 5 && strcmp(path + len - 5, ".jpeg") == 0)
        return 1;
    return 0;
}

static void * screenshot_thread(void *arg) {
    if (path_is_jpeg(file_str_final)) {
        /* JPG path: use stb_image_write (no extra lib), write to .tmp then rename */
        uint8_t *rgb = (uint8_t *) malloc(800 * 480 * 3);
        if (!rgb) {
            msg_update_text_fmt("Error write file");
            goto done;
        }
        for (uint16_t y = 0; y < 480; y++) {
            for (uint16_t x = 0; x < 800; x++) {
                uint32_t from = (y * 800 + x) * 4 + 2;
                uint32_t to = (y * 800 + x) * 3;
                rgb[to++] = buf[from--];
                rgb[to++] = buf[from--];
                rgb[to++] = buf[from--];
            }
        }
        /* Do not flip: LVGL snapshot is already top-to-bottom, same as PNG path */
        if (stbi_write_jpg(file_str_tmp, 800, 480, 3, rgb, 85)) {
            if (rename(file_str_tmp, file_str_final) != 0)
                remove(file_str_tmp);
            if (screenshot_notify)
                msg_update_text_fmt("Saved %s", file_str_final);
        } else {
            remove(file_str_tmp);
            msg_update_text_fmt("Error write file");
        }
        free(rgb);
        goto done;
    }

    FILE *fp = fopen(file_str_tmp, "wb");

    if (!fp) {
        msg_update_text_fmt("Error write file");
        goto done;
    }

    png_structp png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);

    if (!png_ptr) {
        LV_LOG_ERROR("Create write struct");
        goto close_file;
    }

    png_infop png_info = png_create_info_struct(png_ptr);

    if (!png_info) {
        LV_LOG_ERROR("Create info struct");
        goto destroy_write;
    }

    if (setjmp(png_jmpbuf(png_ptr))) {
        LV_LOG_ERROR("Set jump");
        goto destroy_write;
    }

    png_init_io(png_ptr, fp);

    png_set_IHDR(
        png_ptr, png_info,
        800, 480,
        8, PNG_COLOR_TYPE_RGB,
        PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT,
        PNG_FILTER_TYPE_DEFAULT);

    for (uint16_t y = 0; y < 480; y++) {
        rows[y] = (uint8_t *) malloc(800 * 3);

        for (uint16_t x = 0; x < 800; x++) {
            uint32_t    to = x * 3;
            uint32_t    from = (y * 800 + x) * 4 + 2;

            rows[y][to++] = buf[from--];
            rows[y][to++] = buf[from--];
            rows[y][to++] = buf[from--];
        }
    }

    png_set_rows(png_ptr, png_info, rows);
    png_write_png(png_ptr, png_info, PNG_TRANSFORM_IDENTITY, NULL);
    png_write_end(png_ptr, png_info);

    for (uint16_t y = 0; y < 480; y++) {
        free(rows[y]);
    }

    if (screenshot_notify) {
        msg_update_text_fmt("Saved %s", file_str_final);
    }

destroy_write:
    png_destroy_write_struct(&png_ptr, NULL);
close_file:
    fclose(fp);

    // Atomically replace the final file only after the PNG is fully written.
    // This prevents clients from reading a partially-written file.
    if (rename(file_str_tmp, file_str_final) != 0) {
        remove(file_str_tmp);
    }
done:
    free(buf);
    pthread_mutex_lock(&screenshot_mux);
    screenshot_busy = false;
    pthread_mutex_unlock(&screenshot_mux);
}

bool screenshot_take_to_path(const char *path, bool notify) {
    pthread_mutex_lock(&screenshot_mux);
    if (screenshot_busy) {
        pthread_mutex_unlock(&screenshot_mux);
        return false;
    }
    screenshot_busy = true;
    pthread_mutex_unlock(&screenshot_mux);

    snprintf(file_str_final, sizeof(file_str_final), "%s", path);
    snprintf(file_str_tmp, sizeof(file_str_tmp), "%s.tmp", path);
    screenshot_notify = notify;

    uint32_t buf_size = lv_snapshot_buf_size_needed(lv_scr_act(), LV_IMG_CF_TRUE_COLOR_ALPHA);
    buf = (uint8_t *) malloc(buf_size);
    if (!buf) {
        pthread_mutex_lock(&screenshot_mux);
        screenshot_busy = false;
        pthread_mutex_unlock(&screenshot_mux);
        return false;
    }

    lv_snapshot_take_to_buf(lv_scr_act(), LV_IMG_CF_TRUE_COLOR_ALPHA, &snapshot, buf, buf_size);

    pthread_t thread;
    pthread_create(&thread, NULL, screenshot_thread, NULL);
    pthread_detach(thread);
    return true;
}

bool screenshot_take() {
    char time_str[64];
    get_time_str(time_str, sizeof(time_str));

    char path[128];
    snprintf(path, sizeof(path), "/mnt/%s.png", time_str);

    return screenshot_take_to_path(path, true);
}
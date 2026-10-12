/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 */

#pragma once

#include "lvgl/lvgl.h"

#include <stdbool.h>
#include <stdint.h>

/* USB keyboard typing for LVGL's keypad input.
 *
 * LVGL's keypad only takes a key when the previous one was released, but
 * fast typing overlaps keys ("a" still down when "s" goes down), and
 * those keys were lost. This releases the held key first, then presses
 * the new one, and ignores the late release of a key already let go.
 * Keys are tracked by scancode, since a key's character can change while
 * it's held (Shift released first). */

/* One event from the keyboard: false when there's none left. `key` is the
 * LVGL key or character (0 for Shift and other keys with no character),
 * `value` is evdev's: 0 up, 1 down, 2 auto-repeat. */
typedef bool (*kbd_source_t)(void *ctx, uint16_t *scancode, uint32_t *key, int *value);

typedef struct {
    bool     held;
    uint16_t held_code;
    uint32_t held_key;
    bool     pending; /* a press to report on the next read */
    uint16_t pending_code;
    uint32_t pending_key;
} kbd_rollover_t;

/* An LVGL keypad read_cb body. */
void kbd_rollover_read(kbd_rollover_t *k, kbd_source_t src, void *ctx, lv_indev_data_t *data);

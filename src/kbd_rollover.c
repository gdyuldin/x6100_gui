/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 */

#include "kbd_rollover.h"

static void report(lv_indev_data_t *data, uint32_t key, bool pressed) {
    data->key              = key;
    data->state            = pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    data->continue_reading = true; /* there may be more events queued */
}

void kbd_rollover_read(kbd_rollover_t *k, kbd_source_t src, void *ctx, lv_indev_data_t *data) {
    /* The press that arrived while another key was down: its turn now. */
    if (k->pending) {
        k->pending   = false;
        k->held      = true;
        k->held_code = k->pending_code;
        k->held_key  = k->pending_key;
        report(data, k->held_key, true);
        return;
    }

    uint16_t code;
    uint32_t key;
    int      value;
    while (src(ctx, &code, &key, &value)) {
        if (value == 0) {
            /* Only the held key's release counts; others were released
             * already, when the next key went down. */
            if (!k->held || code != k->held_code) continue;
            k->held = false;
            report(data, k->held_key, false);
            return;
        }
        if (key == 0) continue;                       /* Shift, Ctrl...: no character */
        if (k->held && code == k->held_code) continue; /* auto-repeat: LVGL repeats by itself */
        if (k->held) {
            /* Rollover: let go of the old key now, press the new one next read. */
            k->pending      = true;
            k->pending_code = code;
            k->pending_key  = key;
            k->held         = false;
            report(data, k->held_key, false);
            return;
        }
        k->held      = true;
        k->held_code = code;
        k->held_key  = key;
        report(data, key, true);
        return;
    }

    /* Nothing new: the same state as last time. */
    data->key              = k->held_key;
    data->state            = k->held ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    data->continue_reading = false;
}

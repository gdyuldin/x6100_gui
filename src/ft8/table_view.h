/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI - FT8 message table view
 *
 *  Copyright (c) 2026
 */

/*
 *  Wraps the LVGL lv_table that displays FT8 RX/TX messages. Owns the cell
 *  backing data in a fixed-size ring pool, so truncate/scroll/close never
 *  call malloc or free and there is no way to leak or double-free a cell.
 *
 *  Usage (UI thread only unless noted):
 *      table_view_build(parent, x, y, w, h);
 *      table_view_set_press_cb(on_press);
 *      table_view_set_actions(&actions);
 *
 *      // from worker thread:
 *      cell_data_t cd = ...;
 *      scheduler_put(table_view_add_msg_cb, &cd, sizeof(cd));
 *
 *      // on dialog close:
 *      table_view_destroy();
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "lvgl/lvgl.h"

#include "../qso_log.h"
#include "qso.h"   /* ftx_msg_meta_t */

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CELL_RX_INFO = 0,
    CELL_START_QSO,
    CELL_RX_MSG,
    CELL_RX_CQ,
    CELL_RX_TO_ME,
    CELL_TX_MSG,
} ft8_cell_type_t;

typedef struct {
    ft8_cell_type_t         cell_type;
    int16_t                 dist;
    bool                    odd;
    ftx_msg_meta_t          meta;
    char                    text[64];
    qso_log_search_worked_t worked_type;
} cell_data_t;

/* Optional dialog-level actions invoked from the table's key handler. */
typedef struct {
    void (*on_close)(void);           /* LV_KEY_ESC */
    void (*on_vol_change)(int32_t d); /* KEY_VOL_*_EDIT/SELECT */
} table_view_actions_t;

/* Fired when a data-bearing row (RX_MSG / RX_CQ / RX_TO_ME) is pressed. */
typedef void (*table_view_press_cb_t)(const cell_data_t *cell);

void      table_view_build(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h);
void      table_view_destroy(void);
lv_obj_t *table_view_obj(void);

void table_view_set_press_cb(table_view_press_cb_t cb);
void table_view_set_actions(const table_view_actions_t *actions);

/* Clear table + ring pool and show a single "Wait sync" placeholder row. */
void table_view_reset(void);

/* Insert on the UI thread. Prefer the scheduler trampoline below from
 * worker threads. The text field is used as the visible row content. */
void table_view_push_ui(const cell_data_t *src);

/* Trampoline that matches scheduler_fn_t signature. Expects 'data' to point
 * to a cell_data_t value copy that the scheduler will free after this
 * returns; table_view_push_ui() takes its own snapshot into the ring pool. */
void table_view_add_msg_cb(void *data);

#ifdef __cplusplus
}
#endif

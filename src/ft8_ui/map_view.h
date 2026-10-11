/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI - FT8 world map view
 */

/*
 *  World map with Maidenhead fields; decoded stations are plotted by their
 *  grid. MFK press cycles what MFK rotation does: scroll the longitude,
 *  scroll the latitude (zoomed in only) or zoom (1x..6x). Zoomed in, the
 *  Maidenhead squares and the calls of all stations are shown.
 *
 *  Usage (UI thread only unless noted):
 *      map_view_build(parent, x, y, w, h);
 *      map_view_set_home(grid);
 *      map_view_set_actions(&actions);
 *
 *      // from worker thread:
 *      map_station_t st = ...;
 *      scheduler_put(map_view_add_station_cb, &st, sizeof(st));
 *
 *      // on dialog close:
 *      map_view_destroy();
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "lvgl/lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MAP_STATION_OTHER = 0,
    MAP_STATION_CQ,
    MAP_STATION_WORKED,
    MAP_STATION_TO_ME,
} map_station_kind_t;

typedef struct {
    char               call[13];
    char               grid[9];
    map_station_kind_t kind;
} map_station_t;

typedef struct {
    void (*on_close)(void);           /* LV_KEY_ESC */
    void (*on_vol_change)(int32_t d); /* KEY_VOL_*_EDIT/SELECT */
} map_view_actions_t;

void      map_view_build(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h);
void      map_view_destroy(void);
lv_obj_t *map_view_obj(void);

void map_view_set_actions(const map_view_actions_t *actions);

/* Own position, empty or invalid grid hides the home marker. */
void map_view_set_home(const char *grid);

/* Center the view on the own position, the zoom stays. */
void map_view_go_home(void);

/* Forget all plotted stations (band change). */
void map_view_clear(void);

void map_view_add_station(const map_station_t *st);

/* Trampoline that matches scheduler_fn_t signature. */
void map_view_add_station_cb(void *data);

#ifdef __cplusplus
}
#endif

/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI - FT8 world map view
 */

#include "map_view.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "world_land.h"
#include "../events.h"
#include "../styles.h"
#include "../qth/qth.h"

/*
 *  Equirectangular projection. The visible latitude band is fixed, so the
 *  whole map height is used and only the longitude scrolls (with wrap).
 *  Land, sea, borders and the Maidenhead field lines are rendered into a
 *  canvas on every scroll; field labels, stations and the legend are drawn on top
 *  in LV_EVENT_DRAW_POST.
 */

#define LAT_TOP         80.0f
#define LAT_BOTTOM      (-60.0f)
#define PAN_STEP        10.0f       /* degrees per MFK step */
#define MAX_STATIONS    128
#define STATION_TTL     (30 * 60)   /* seconds */
#define MAX_CROSS       64          /* polygon edge crossings per row */

#define COLOR_SEA       0x0B1E33
#define COLOR_LAND      0x2B4A2B
#define COLOR_GRID      0x2F5070
#define COLOR_BORDER    0x7A9A7A
#define COLOR_ADMIN1    0x4A6A4E
#define COLOR_LABEL     0x4A6A8A

extern lv_font_t sony_12;

typedef struct {
    char               call[13];
    float              lat;
    float              lon;
    map_station_kind_t kind;
    time_t             heard;
} station_t;

static lv_obj_t           *s_obj    = NULL;
static lv_obj_t           *s_canvas = NULL;
static lv_color_t         *s_buf    = NULL;
static int16_t            *s_cross  = NULL;
static uint8_t            *s_cross_n = NULL;
static lv_coord_t          s_w, s_h;
static float               s_ppd;           /* pixels per degree */
static float               s_center_lon;

static bool                s_home_valid = false;
static float               s_home_lat, s_home_lon;

static station_t           s_stations[MAX_STATIONS];
static map_view_actions_t  s_actions = {0};

static void event_cb(lv_event_t *e);

static const lv_obj_class_t map_class = {
    .base_class    = &lv_obj_class,
    .editable      = LV_OBJ_CLASS_EDITABLE_TRUE,     /* MFK rotation sends LEFT/RIGHT keys */
    .group_def     = LV_OBJ_CLASS_GROUP_DEF_FALSE,
    .instance_size = sizeof(lv_obj_t),
};

static float wrap_lon(float lon) {
    lon = fmodf(lon + 180.0f, 360.0f);
    if (lon < 0.0f) lon += 360.0f;
    return lon - 180.0f;
}

static float lon_to_x(float lon) {
    return s_w / 2.0f + wrap_lon(lon - s_center_lon) * s_ppd;
}

static float lat_to_y(float lat) {
    return (LAT_TOP - lat) * s_ppd;
}

/* -------- Canvas rendering --------------------------------------------- */

static void add_edge(float x0, float y0, float x1, float y1) {
    if (y0 == y1) return;
    if (y0 > y1) {
        float t;
        t = x0; x0 = x1; x1 = t;
        t = y0; y0 = y1; y1 = t;
    }

    /* Rows whose center lies in [y0, y1) */
    int ys = (int)ceilf(y0 - 0.5f);
    int ye = (int)ceilf(y1 - 0.5f);
    if (ys < 0) ys = 0;
    if (ye > s_h) ye = s_h;

    for (int y = ys; y < ye; y++) {
        if (s_cross_n[y] >= MAX_CROSS) continue;
        float x = x0 + ((y + 0.5f) - y0) * (x1 - x0) / (y1 - y0);
        s_cross[y * MAX_CROSS + s_cross_n[y]++] = (int16_t)lroundf(x);
    }
}

static void render_land(void) {
    memset(s_cross_n, 0, s_h);

    const int16_t (*p)[2] = world_land_pts;
    for (uint16_t r = 0; r < world_land_count; r++) {
        uint16_t n = world_land_len[r];

        float min_x = 1e9f, max_x = -1e9f;
        for (uint16_t i = 0; i < n; i++) {
            float x = s_w / 2.0f + (p[i][0] / 10.0f - s_center_lon) * s_ppd;
            if (x < min_x) min_x = x;
            if (x > max_x) max_x = x;
        }

        /* The ring may be visible at its own position or shifted by +-360 */
        for (int k = -1; k <= 1; k++) {
            float shift = k * 360.0f * s_ppd;
            if ((max_x + shift < 0) || (min_x + shift >= s_w)) continue;

            for (uint16_t i = 0; i < n; i++) {
                uint16_t j = (i + 1) % n;
                add_edge(s_w / 2.0f + (p[i][0] / 10.0f - s_center_lon) * s_ppd + shift, lat_to_y(p[i][1] / 10.0f),
                         s_w / 2.0f + (p[j][0] / 10.0f - s_center_lon) * s_ppd + shift, lat_to_y(p[j][1] / 10.0f));
            }
        }
        p += n;
    }

    lv_color_t sea  = lv_color_hex(COLOR_SEA);
    lv_color_t land = lv_color_hex(COLOR_LAND);

    for (lv_coord_t y = 0; y < s_h; y++) {
        lv_color_t *row = &s_buf[y * s_w];
        int16_t    *xs  = &s_cross[y * MAX_CROSS];
        uint8_t     cnt = s_cross_n[y];

        for (lv_coord_t x = 0; x < s_w; x++) row[x] = sea;

        /* Insertion sort, there are only a few crossings per row */
        for (uint8_t i = 1; i < cnt; i++) {
            int16_t v = xs[i];
            int8_t  j = i - 1;
            while (j >= 0 && xs[j] > v) {
                xs[j + 1] = xs[j];
                j--;
            }
            xs[j + 1] = v;
        }

        /* Even-odd fill */
        for (uint8_t i = 0; i + 1 < cnt; i += 2) {
            int16_t x0 = LV_MAX(xs[i], 0);
            int16_t x1 = LV_MIN(xs[i + 1], s_w);
            for (int16_t x = x0; x < x1; x++) row[x] = land;
        }
    }
}

static void render_grid(void) {
    lv_color_t grid = lv_color_hex(COLOR_GRID);

    for (int lon = -180; lon < 180; lon += 20) {
        int x = (int)lroundf(lon_to_x((float)lon));
        if (x < 0 || x >= s_w) continue;
        for (lv_coord_t y = 0; y < s_h; y++) s_buf[y * s_w + x] = grid;
    }

    for (int lat = (int)LAT_BOTTOM; lat <= (int)LAT_TOP; lat += 10) {
        int y = (int)lroundf(lat_to_y((float)lat));
        if (y < 0 || y >= s_h) continue;
        for (lv_coord_t x = 0; x < s_w; x++) s_buf[y * s_w + x] = grid;
    }
}

static void draw_line(int x0, int y0, int x1, int y1, lv_color_t color) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;

    while (true) {
        if (x0 >= 0 && x0 < s_w && y0 >= 0 && y0 < s_h) {
            s_buf[y0 * s_w + x0] = color;
        }
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

static void render_lines(const int16_t (*p)[2], const uint16_t *len, uint16_t count, uint32_t color) {
    lv_color_t c = lv_color_hex(color);

    for (uint16_t r = 0; r < count; r++) {
        uint16_t n = len[r];

        for (int k = -1; k <= 1; k++) {
            float shift = s_w / 2.0f + k * 360.0f * s_ppd;

            int x0 = (int)lroundf((p[0][0] / 10.0f - s_center_lon) * s_ppd + shift);
            int y0 = (int)lroundf(lat_to_y(p[0][1] / 10.0f));
            for (uint16_t i = 1; i < n; i++) {
                int x1 = (int)lroundf((p[i][0] / 10.0f - s_center_lon) * s_ppd + shift);
                int y1 = (int)lroundf(lat_to_y(p[i][1] / 10.0f));
                if (!((x0 < 0 && x1 < 0) || (x0 >= s_w && x1 >= s_w))) {
                    draw_line(x0, y0, x1, y1, c);
                }
                x0 = x1;
                y0 = y1;
            }
        }
        p += n;
    }
}

static void render(void) {
    render_land();
    render_grid();
    render_lines(world_admin1_pts, world_admin1_len, world_admin1_count, COLOR_ADMIN1);
    render_lines(world_border_pts, world_border_len, world_border_count, COLOR_BORDER);
    lv_obj_invalidate(s_obj);
}

/* -------- Overlay ------------------------------------------------------ */

static void draw_text(lv_draw_ctx_t *draw_ctx, lv_draw_label_dsc_t *dsc, lv_coord_t x, lv_coord_t y,
                      lv_coord_t w, const char *txt) {
    lv_area_t area = {
        .x1 = s_obj->coords.x1 + x,
        .y1 = s_obj->coords.y1 + y,
        .x2 = s_obj->coords.x1 + x + w - 1,
        .y2 = s_obj->coords.y1 + y + dsc->font->line_height - 1,
    };
    lv_draw_label(draw_ctx, dsc, &area, txt, NULL);
}

static void draw_dot(lv_draw_ctx_t *draw_ctx, float x, float y, lv_coord_t r, uint32_t color) {
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.radius       = LV_RADIUS_CIRCLE;
    dsc.bg_color     = lv_color_hex(color);
    dsc.border_color = lv_color_black();
    dsc.border_width = 1;

    lv_coord_t cx = s_obj->coords.x1 + (lv_coord_t)lroundf(x);
    lv_coord_t cy = s_obj->coords.y1 + (lv_coord_t)lroundf(y);
    lv_area_t  area = { cx - r, cy - r, cx + r, cy + r };
    lv_draw_rect(draw_ctx, &dsc, &area);
}

static uint32_t kind_color(map_station_kind_t kind) {
    switch (kind) {
        case MAP_STATION_CQ:     return 0x00DD00;
        case MAP_STATION_WORKED: return 0x6A8A40;
        case MAP_STATION_TO_ME:  return 0xFF4040;
        default:                 return 0xE0C000;
    }
}

static void draw_overlay(lv_event_t *e) {
    lv_draw_ctx_t       *draw_ctx = lv_event_get_draw_ctx(e);
    lv_draw_label_dsc_t  label;
    char                 buf[96];

    lv_draw_label_dsc_init(&label);
    label.font  = &sony_12;
    label.align = LV_TEXT_ALIGN_CENTER;
    label.color = lv_color_hex(COLOR_LABEL);

    /* Maidenhead field names */
    for (int i = 0; i < 18; i++) {
        float x = lon_to_x(-180.0f + i * 20.0f + 10.0f);
        if (x < -20 || x > s_w + 20) continue;

        for (int j = 0; j < 18; j++) {
            float lat = -90.0f + j * 10.0f + 5.0f;
            if (lat < LAT_BOTTOM || lat > LAT_TOP) continue;

            buf[0] = 'A' + i;
            buf[1] = 'A' + j;
            buf[2] = '\0';
            draw_text(draw_ctx, &label, (lv_coord_t)x - 20, (lv_coord_t)lat_to_y(lat) - label.font->line_height / 2,
                      40, buf);
        }
    }

    /* Stations, most important kinds on top */
    time_t now   = time(NULL);
    int    count = 0;

    label.font  = &sony_16;
    label.align = LV_TEXT_ALIGN_LEFT;
    label.color = lv_color_white();

    for (map_station_kind_t kind = MAP_STATION_OTHER; kind <= MAP_STATION_TO_ME; kind++) {
        for (int i = 0; i < MAX_STATIONS; i++) {
            station_t *st = &s_stations[i];
            if (st->call[0] == '\0' || st->kind != kind || now - st->heard > STATION_TTL) continue;

            count++;
            float x = lon_to_x(st->lon);
            float y = lat_to_y(st->lat);
            if (x < 0 || x >= s_w || y < 0 || y >= s_h) continue;

            draw_dot(draw_ctx, x, y, 4, kind_color(st->kind));
            if (st->kind == MAP_STATION_TO_ME) {
                draw_text(draw_ctx, &label, (lv_coord_t)x + 7, (lv_coord_t)y - label.font->line_height / 2, 120,
                          st->call);
            }
        }
    }

    if (s_home_valid) {
        draw_dot(draw_ctx, lon_to_x(s_home_lon), lat_to_y(s_home_lat), 6, 0xFFFFFF);
        draw_dot(draw_ctx, lon_to_x(s_home_lon), lat_to_y(s_home_lat), 2, 0x000000);
    }

    /* Legend */
    lv_draw_rect_dsc_t bg;
    lv_draw_rect_dsc_init(&bg);
    bg.bg_color = lv_color_black();
    bg.bg_opa   = LV_OPA_70;

    lv_area_t area = {
        .x1 = s_obj->coords.x1,
        .y1 = s_obj->coords.y2 - label.font->line_height - 6,
        .x2 = s_obj->coords.x2,
        .y2 = s_obj->coords.y2,
    };
    lv_draw_rect(draw_ctx, &bg, &area);

    label.flag = LV_TEXT_FLAG_RECOLOR;
    snprintf(buf, sizeof(buf), "#00DD00 CQ#   #6A8A40 Worked#   #E0C000 Other#   #FF4040 To me#      Stations: %i", count);
    draw_text(draw_ctx, &label, 8, s_h - label.font->line_height - 3, s_w - 16, buf);
}

/* -------- LVGL event plumbing ------------------------------------------ */

static void pan(float deg) {
    s_center_lon = wrap_lon(s_center_lon + deg);
    render();
}

static void event_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_DRAW_POST) {
        draw_overlay(e);
    } else if (code == LV_EVENT_CLICKED) {
        if (s_home_valid) {
            s_center_lon = s_home_lon;
            render();
        }
    } else if (code == LV_EVENT_FOCUSED) {
        /* Long MFK press toggles edit mode, keep editing so rotation scrolls */
        lv_group_t *g = lv_obj_get_group(s_obj);
        if (g && !lv_group_get_editing(g)) {
            lv_group_set_editing(g, true);
        }
    } else if (code == LV_EVENT_KEY) {
        uint32_t key = *((uint32_t *)lv_event_get_param(e));

        switch (key) {
            case LV_KEY_LEFT:
                pan(-PAN_STEP);
                break;
            case LV_KEY_RIGHT:
                pan(PAN_STEP);
                break;
            case LV_KEY_ESC:
                if (s_actions.on_close) s_actions.on_close();
                break;
            case KEY_VOL_LEFT_EDIT:
            case KEY_VOL_LEFT_SELECT:
                if (s_actions.on_vol_change) s_actions.on_vol_change(-1);
                break;
            case KEY_VOL_RIGHT_EDIT:
            case KEY_VOL_RIGHT_SELECT:
                if (s_actions.on_vol_change) s_actions.on_vol_change(1);
                break;
            default:
                break;
        }
    }
}

/* -------- Public API --------------------------------------------------- */

void map_view_build(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h) {
    s_w   = w;
    s_h   = h;
    s_ppd = h / (LAT_TOP - LAT_BOTTOM);
    memset(&s_actions, 0, sizeof(s_actions));

    s_buf     = malloc(sizeof(lv_color_t) * w * h);
    s_cross   = malloc(sizeof(int16_t) * MAX_CROSS * h);
    s_cross_n = malloc(h);

    s_obj = lv_obj_class_create_obj(&map_class, parent);
    lv_obj_class_init_obj(s_obj);
    lv_obj_remove_style_all(s_obj);
    lv_obj_clear_flag(s_obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(s_obj, w, h);
    lv_obj_set_pos(s_obj, x, y);
    lv_obj_add_event_cb(s_obj, event_cb, LV_EVENT_ALL, NULL);

    s_canvas = lv_canvas_create(s_obj);
    lv_canvas_set_buffer(s_canvas, s_buf, w, h, LV_IMG_CF_TRUE_COLOR);
    lv_obj_clear_flag(s_canvas, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(s_canvas, 0, 0);

    s_center_lon = s_home_valid ? s_home_lon : 0.0f;
    render();
}

void map_view_destroy(void) {
    if (s_obj) {
        lv_obj_del(s_obj);
        s_obj    = NULL;
        s_canvas = NULL;
    }
    free(s_buf);
    free(s_cross);
    free(s_cross_n);
    s_buf     = NULL;
    s_cross   = NULL;
    s_cross_n = NULL;
    memset(&s_actions, 0, sizeof(s_actions));
}

lv_obj_t *map_view_obj(void) {
    return s_obj;
}

void map_view_set_actions(const map_view_actions_t *actions) {
    if (actions) {
        s_actions = *actions;
    } else {
        memset(&s_actions, 0, sizeof(s_actions));
    }
}

void map_view_set_home(const char *grid) {
    s_home_valid = grid && strlen(grid) >= 4 && qth_grid_check(grid);

    if (s_home_valid) {
        double lat, lon;
        qth_str_to_pos(grid, &lat, &lon);
        s_home_lat = (float)lat;
        s_home_lon = (float)lon;
    }
    if (s_obj) {
        s_center_lon = s_home_valid ? s_home_lon : 0.0f;
        render();
    }
}

void map_view_clear(void) {
    memset(s_stations, 0, sizeof(s_stations));
    if (s_obj) lv_obj_invalidate(s_obj);
}

void map_view_add_station(const map_station_t *st) {
    if (!st || strlen(st->grid) < 4 || !qth_grid_check(st->grid)) return;

    /* Same call updates its slot, otherwise take a free or the oldest one */
    station_t *slot = &s_stations[0];
    for (int i = 0; i < MAX_STATIONS; i++) {
        station_t *cur = &s_stations[i];
        if (strcmp(cur->call, st->call) == 0) {
            slot = cur;
            break;
        }
        if (cur->heard < slot->heard) {
            slot = cur;
        }
    }

    double lat, lon;
    qth_str_to_pos(st->grid, &lat, &lon);

    strncpy(slot->call, st->call, sizeof(slot->call) - 1);
    slot->call[sizeof(slot->call) - 1] = '\0';
    slot->lat   = (float)lat;
    slot->lon   = (float)lon;
    slot->kind  = st->kind;
    slot->heard = time(NULL);

    if (s_obj) lv_obj_invalidate(s_obj);
}

void map_view_add_station_cb(void *data) {
    map_view_add_station((const map_station_t *)data);
}

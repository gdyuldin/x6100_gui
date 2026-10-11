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
 *  Equirectangular projection. In the world view (1x) the fixed latitude band
 *  LAT_TOP..LAT_BOTTOM fills the map height and only the longitude scrolls
 *  (with wrap). Zoomed in, the view also scrolls in latitude, kept inside
 *  LAT_LIMIT_N..LAT_LIMIT_S.
 *
 *  MFK press cycles what MFK rotation does: longitude, latitude (zoomed in
 *  only) and zoom. The current mode is shown in the top right corner.
 *
 *  Land, sea, borders and the Maidenhead field and square lines are rendered
 *  into a canvas on every change; grid labels, stations, the mode indicator
 *  and the legend are drawn on top in LV_EVENT_DRAW_POST. Zoomed in far enough
 *  for the squares, their digits are shown as rulers along the top and left
 *  edge (like on a paper map) instead of in every square.
 */

#define LAT_TOP         80.0f
#define LAT_BOTTOM      (-60.0f)
#define LAT_LIMIT_N     85.0f       /* zoomed views stay inside this band */
#define LAT_LIMIT_S     (-70.0f)
#define PAN_FRACTION    (1.0f / 16) /* part of the view moved per MFK step */
#define CALLS_ZOOM      3           /* from this zoom on all calls are labeled */
#define CALLS_PER_SPOT  3           /* calls listed per position, the rest as "+N" */
#define SQUARE_LINE_PX  9.0f        /* min height of 1 degree to draw the squares */
#define RULER_W         14          /* width of the left square ruler */
#define MAX_STATIONS    128
#define STATION_TTL     (30 * 60)   /* seconds */
#define MAX_CROSS       128         /* polygon edge crossings per row, at most 255 */

#define COLOR_SEA       0x0B1E33
#define COLOR_LAND      0x2B4A2B
#define COLOR_GRID      0x2F5070
#define COLOR_SQUARE    0x2A4866
#define COLOR_RULER     0x9FBFDF
#define COLOR_BORDER    0x7A9A7A
#define COLOR_ADMIN1    0x4A6A4E
#define COLOR_LABEL     0x4A6A8A

extern lv_font_t sony_10;
extern lv_font_t sony_12;

typedef enum {
    MODE_LON = 0,
    MODE_LAT,
    MODE_ZOOM,
} map_mode_t;

typedef struct {
    char               call[13];
    float              lat;
    float              lon;
    map_station_kind_t kind;
    time_t             heard;
} station_t;

/* Bounding box of a land ring, 0.1 degree */
typedef struct {
    int16_t lon0, lon1;
    int16_t lat0, lat1;
} box_t;

/* Stations sharing one position (grid) on screen */
typedef struct {
    float x, y;
    int   n;
    int   idx[CALLS_PER_SPOT];
} spot_t;

static const uint8_t zoom_levels[] = {1, 2, 3, 4, 6};
#define ZOOM_COUNT ((int)(sizeof(zoom_levels) / sizeof(zoom_levels[0])))

static lv_obj_t           *s_obj    = NULL;
static lv_obj_t           *s_canvas = NULL;
static lv_color_t         *s_buf    = NULL;
static int16_t            *s_cross  = NULL;
static uint8_t            *s_cross_n = NULL;
static box_t              *s_land_box = NULL;
static lv_coord_t          s_w, s_h;
static float               s_base_ppd;      /* pixels per degree at 1x */
static float               s_ppd;           /* pixels per degree */
static float               s_center_lon;
static float               s_center_lat;
static int                 s_zoom;          /* index into zoom_levels */
static map_mode_t          s_mode;

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
    return s_h / 2.0f + (s_center_lat - lat) * s_ppd;
}

/* x of a 0.1 degree data longitude without wrap, callers add the +-360 shift */
static float data_x(int16_t lon) {
    return s_w / 2.0f + (lon / 10.0f - s_center_lon) * s_ppd;
}

static float data_y(int16_t lat) {
    return lat_to_y(lat / 10.0f);
}

static float view_w_deg(void) {
    return s_w / s_ppd;
}

static float view_h_deg(void) {
    return s_h / s_ppd;
}

/* -------- View state --------------------------------------------------- */

static void clamp_lat(void) {
    if (s_zoom == 0) {
        s_center_lat = (LAT_TOP + LAT_BOTTOM) / 2.0f;
        return;
    }

    float half = view_h_deg() / 2.0f;
    s_center_lat = LV_CLAMP(LAT_LIMIT_S + half, s_center_lat, LAT_LIMIT_N - half);
}

static void set_zoom(int zoom) {
    if (zoom < 0 || zoom >= ZOOM_COUNT || zoom == s_zoom) return;

    /* Leaving the world view: look at the own latitude, the longitude stays */
    if (s_zoom == 0 && s_home_valid) {
        s_center_lat = s_home_lat;
    }

    s_zoom = zoom;
    s_ppd  = s_base_ppd * zoom_levels[zoom];
    clamp_lat();

    /* The world view shows all latitudes, nothing to scroll */
    if (s_zoom == 0 && s_mode == MODE_LAT) {
        s_mode = MODE_LON;
    }
}

static void next_mode(void) {
    switch (s_mode) {
        case MODE_LON:
            s_mode = (s_zoom > 0) ? MODE_LAT : MODE_ZOOM;
            break;
        case MODE_LAT:
            s_mode = MODE_ZOOM;
            break;
        default:
            s_mode = MODE_LON;
            break;
    }
}

static void go_home(void) {
    if (!s_home_valid) return;

    s_center_lon = s_home_lon;
    s_center_lat = s_home_lat;
    clamp_lat();
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
        uint16_t     n   = world_land_len[r];
        const box_t *box = &s_land_box[r];

        /* Rings above or below the view add no crossings to visible rows */
        if (data_y(box->lat0) >= 0 && data_y(box->lat1) < s_h) {
            /* The ring may be visible at its own position or shifted by +-360 */
            for (int k = -1; k <= 1; k++) {
                float shift = k * 360.0f * s_ppd;
                if ((data_x(box->lon1) + shift < 0) || (data_x(box->lon0) + shift >= s_w)) continue;

                for (uint16_t i = 0; i < n; i++) {
                    uint16_t j = (i + 1) % n;
                    add_edge(data_x(p[i][0]) + shift, data_y(p[i][1]), data_x(p[j][0]) + shift, data_y(p[j][1]));
                }
            }
        }
        p += n;
    }

    lv_color_t sea  = lv_color_hex(COLOR_SEA);
    lv_color_t land = lv_color_hex(COLOR_LAND);

    for (lv_coord_t y = 0; y < s_h; y++) {
        lv_color_t *row = &s_buf[y * s_w];
        int16_t    *xs  = &s_cross[y * MAX_CROSS];
        int         cnt = s_cross_n[y];

        for (lv_coord_t x = 0; x < s_w; x++) row[x] = sea;

        /* Insertion sort, there are only a few crossings per row */
        for (int i = 1; i < cnt; i++) {
            int16_t v = xs[i];
            int     j = i - 1;
            while (j >= 0 && xs[j] > v) {
                xs[j + 1] = xs[j];
                j--;
            }
            xs[j + 1] = v;
        }

        /* Even-odd fill */
        for (int i = 0; i + 1 < cnt; i += 2) {
            int16_t x0 = LV_MAX(xs[i], 0);
            int16_t x1 = LV_MIN(xs[i + 1], s_w);
            for (int16_t x = x0; x < x1; x++) row[x] = land;
        }
    }
}

/* step 1 draws a solid line, 2 a dotted one */
static void vline(float fx, lv_color_t color, int step) {
    int x = (int)lroundf(fx);
    if (x < 0 || x >= s_w) return;
    for (lv_coord_t y = 0; y < s_h; y += step) s_buf[y * s_w + x] = color;
}

static void hline(float fy, lv_color_t color, int step) {
    int y = (int)lroundf(fy);
    if (y < 0 || y >= s_h) return;
    for (lv_coord_t x = 0; x < s_w; x += step) s_buf[y * s_w + x] = color;
}

static void render_grid(void) {
    /* Maidenhead squares (2 x 1 degree) once they are big enough */
    if (s_ppd >= SQUARE_LINE_PX) {
        lv_color_t square = lv_color_hex(COLOR_SQUARE);
        float      half_w = view_w_deg() / 2.0f;

        for (int lon = (int)floorf((s_center_lon - half_w) / 2.0f) * 2; lon <= s_center_lon + half_w; lon += 2) {
            vline(lon_to_x((float)lon), square, 2);
        }
        for (int lat = -90; lat <= 90; lat++) {
            hline(lat_to_y((float)lat), square, 2);
        }
    }

    /* Maidenhead fields (20 x 10 degree) */
    lv_color_t grid = lv_color_hex(COLOR_GRID);

    for (int lon = -180; lon < 180; lon += 20) {
        vline(lon_to_x((float)lon), grid, 1);
    }
    for (int lat = -90; lat <= 90; lat += 10) {
        hline(lat_to_y((float)lat), grid, 1);
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
            float shift = k * 360.0f * s_ppd;

            int x0 = (int)lroundf(data_x(p[0][0]) + shift);
            int y0 = (int)lroundf(data_y(p[0][1]));
            for (uint16_t i = 1; i < n; i++) {
                int x1 = (int)lroundf(data_x(p[i][0]) + shift);
                int y1 = (int)lroundf(data_y(p[i][1]));
                if (!((x0 < 0 && x1 < 0) || (x0 >= s_w && x1 >= s_w) ||
                      (y0 < 0 && y1 < 0) || (y0 >= s_h && y1 >= s_h))) {
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

/* Dark translucent box behind text, area relative to the map */
static void draw_box(lv_draw_ctx_t *draw_ctx, const lv_area_t *area, lv_opa_t opa) {
    lv_draw_rect_dsc_t bg;
    lv_draw_rect_dsc_init(&bg);
    bg.bg_color = lv_color_black();
    bg.bg_opa   = opa;

    lv_area_t abs = *area;
    lv_area_move(&abs, s_obj->coords.x1, s_obj->coords.y1);
    lv_draw_rect(draw_ctx, &bg, &abs);
}

static lv_coord_t text_width(const lv_font_t *font, const char *txt) {
    lv_point_t size;
    lv_txt_get_size(&size, txt, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    return size.x;
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

/* Label priority, lower first */
static int kind_rank(map_station_kind_t kind) {
    switch (kind) {
        case MAP_STATION_TO_ME:  return 0;
        case MAP_STATION_CQ:     return 1;
        case MAP_STATION_OTHER:  return 2;
        default:                 return 3;
    }
}

static bool squares_shown(void) {
    return s_ppd >= SQUARE_LINE_PX;
}

/* Strips holding the square rulers, relative to the map */
static void ruler_areas(lv_area_t *top, lv_area_t *left) {
    lv_coord_t lh = sony_10.line_height;

    *top  = (lv_area_t){ 0, 0, s_w - 1, lh + 1 };
    *left = (lv_area_t){ 0, lh + 2, RULER_W - 1, s_h - 1 };
}

/*
 * Square digits as rulers: the longitude digit of each column along the top
 * edge, the latitude digit of each row along the left edge. Together with the
 * field name in the middle they give the full square, e.g. "PM" + 9 + 5.
 */
static void draw_square_rulers(lv_draw_ctx_t *draw_ctx) {
    lv_draw_label_dsc_t label;
    lv_area_t           top, left;
    char                buf[2] = {0};

    lv_draw_label_dsc_init(&label);
    label.font  = &sony_10;
    label.align = LV_TEXT_ALIGN_CENTER;
    label.color = lv_color_hex(COLOR_RULER);

    lv_coord_t lh = label.font->line_height;

    ruler_areas(&top, &left);
    draw_box(draw_ctx, &top, LV_OPA_60);
    draw_box(draw_ctx, &left, LV_OPA_60);

    float half_w = view_w_deg() / 2.0f;
    for (int lon = (int)floorf((s_center_lon - half_w) / 2.0f) * 2; lon <= s_center_lon + half_w; lon += 2) {
        float x = lon_to_x(lon + 1.0f);
        if (x < RULER_W + 4 || x > s_w - 4) continue;

        int l = (int)lroundf(wrap_lon((float)lon)) + 180;  /* 0..358 */
        buf[0] = '0' + (l % 20) / 2;
        draw_text(draw_ctx, &label, (lv_coord_t)x - 8, 1, 16, buf);
    }

    /* Rows may be lower than the font, then label every n-th */
    int   every  = (int)ceilf(lh / s_ppd);
    float half_h = view_h_deg() / 2.0f;
    for (int lat = (int)floorf(s_center_lat - half_h); lat <= s_center_lat + half_h; lat++) {
        if (lat < -90 || lat >= 90 || (lat + 90) % every) continue;

        float y = lat_to_y(lat + 0.5f) - lh / 2.0f;
        if (y < left.y1 || y + lh > s_h) continue;

        buf[0] = '0' + (lat + 90) % 10;
        draw_text(draw_ctx, &label, 0, (lv_coord_t)y, RULER_W, buf);
    }
}

static void draw_grid_labels(lv_draw_ctx_t *draw_ctx) {
    lv_draw_label_dsc_t label;
    char                buf[8];
    bool                squares = squares_shown();

    if (squares) {
        draw_square_rulers(draw_ctx);
    }

    lv_draw_label_dsc_init(&label);
    label.align = LV_TEXT_ALIGN_CENTER;
    label.color = lv_color_hex(COLOR_LABEL);

    /*
     * Field names ("PM") in the middle of the visible part of each field, so a
     * zoomed view still shows which field it is in. With the squares shown
     * they get a bigger font and a box, as the rulers only give the digits.
     */
    label.font = squares ? &sony_16 : &sony_12;

    /* Keep clear of the rulers */
    lv_area_t top, left;
    ruler_areas(&top, &left);
    float min_x = squares ? left.x2 + 1 : 0;
    float min_y = squares ? top.y2 + 1 : 0;

    for (int i = 0; i < 18; i++) {
        float xc = lon_to_x(-180.0f + i * 20.0f + 10.0f);
        float x0 = LV_MAX(xc - 10.0f * s_ppd, min_x);
        float x1 = LV_MIN(xc + 10.0f * s_ppd, s_w);
        if (x1 - x0 < 20) continue;

        for (int j = 0; j < 18; j++) {
            float yc = lat_to_y(-90.0f + j * 10.0f + 5.0f);
            float y0 = LV_MAX(yc - 5.0f * s_ppd, min_y);
            float y1 = LV_MIN(yc + 5.0f * s_ppd, s_h);
            if (y1 - y0 < label.font->line_height) continue;

            buf[0] = 'A' + i;
            buf[1] = 'A' + j;
            buf[2] = '\0';

            lv_coord_t x = (lv_coord_t)((x0 + x1) / 2);
            lv_coord_t y = (lv_coord_t)((y0 + y1) / 2) - label.font->line_height / 2;

            if (squares) {
                lv_coord_t w = text_width(label.font, buf) + 8;
                lv_area_t  box = { x - w / 2, y, x + w / 2, y + label.font->line_height };
                draw_box(draw_ctx, &box, LV_OPA_70);
            }
            draw_text(draw_ctx, &label, x - 20, y, 40, buf);
        }
    }
}

/* Taken label areas, relative to the map */
static lv_area_t s_taken[2 * MAX_STATIONS + 8];
static int       s_taken_n;

static bool area_free(const lv_area_t *a) {
    if (a->x1 < 0 || a->y1 < 0 || a->x2 >= s_w || a->y2 >= s_h) return false;

    for (int i = 0; i < s_taken_n; i++) {
        if (_lv_area_is_on(a, &s_taken[i])) return false;
    }
    return true;
}

static void take_area(const lv_area_t *a) {
    if (s_taken_n < (int)(sizeof(s_taken) / sizeof(s_taken[0]))) {
        s_taken[s_taken_n++] = *a;
    }
}

/* Calls of the stations at one position, to the right of the dot or else to the left */
static void draw_spot_calls(lv_draw_ctx_t *draw_ctx, const spot_t *spot, int total) {
    lv_draw_label_dsc_t label;
    char                more[8];

    lv_draw_label_dsc_init(&label);
    label.font  = &sony_12;
    label.align = LV_TEXT_ALIGN_LEFT;

    lv_coord_t lh    = label.font->line_height;
    int        lines = spot->n + (total > spot->n ? 1 : 0);
    lv_coord_t w     = 0;

    snprintf(more, sizeof(more), "+%i", total - spot->n);
    for (int i = 0; i < spot->n; i++) {
        w = LV_MAX(w, text_width(label.font, s_stations[spot->idx[i]].call));
    }
    if (total > spot->n) {
        w = LV_MAX(w, text_width(label.font, more));
    }
    w += 4;

    lv_coord_t h = lines * lh + 2;
    lv_coord_t y = (lv_coord_t)spot->y - lh / 2 - 1;
    lv_area_t  area = { (lv_coord_t)spot->x + 7, y, (lv_coord_t)spot->x + 7 + w - 1, y + h - 1 };

    if (!area_free(&area)) {
        lv_area_move(&area, -(w + 14), 0);
        if (!area_free(&area)) return;
    }
    take_area(&area);
    draw_box(draw_ctx, &area, LV_OPA_50);

    for (int i = 0; i < lines; i++) {
        bool        is_more = i == spot->n;
        const char *txt     = is_more ? more : s_stations[spot->idx[i]].call;

        label.color = is_more ? lv_color_white() : lv_color_hex(kind_color(s_stations[spot->idx[i]].kind));
        draw_text(draw_ctx, &label, area.x1 + 2, area.y1 + 1 + i * lh, w, txt);
    }
}

static void draw_calls(lv_draw_ctx_t *draw_ctx, time_t now) {
    static spot_t spots[MAX_STATIONS];
    static int    totals[MAX_STATIONS];
    int           n_spots = 0;

    /* Group visible stations by position, the most important ones first */
    for (int rank = 0; rank < 4; rank++) {
        for (int i = 0; i < MAX_STATIONS; i++) {
            station_t *st = &s_stations[i];
            if (st->call[0] == '\0' || kind_rank(st->kind) != rank || now - st->heard > STATION_TTL) continue;

            float x = lon_to_x(st->lon);
            float y = lat_to_y(st->lat);
            if (x < 0 || x >= s_w || y < 0 || y >= s_h) continue;

            spot_t *spot = NULL;
            for (int s = 0; s < n_spots; s++) {
                if (fabsf(spots[s].x - x) < 4 && fabsf(spots[s].y - y) < 4) {
                    spot = &spots[s];
                    break;
                }
            }
            if (!spot) {
                spot      = &spots[n_spots];
                spot->x   = x;
                spot->y   = y;
                spot->n   = 0;
                totals[n_spots++] = 0;
            }
            if (spot->n < CALLS_PER_SPOT) {
                spot->idx[spot->n++] = i;
            }
            totals[spot - spots]++;
        }
    }

    /* Labels must not hide the dots of other stations or the home marker */
    for (int s = 0; s < n_spots; s++) {
        lv_area_t dot = { (lv_coord_t)spots[s].x - 5, (lv_coord_t)spots[s].y - 5,
                          (lv_coord_t)spots[s].x + 5, (lv_coord_t)spots[s].y + 5 };
        take_area(&dot);
    }
    if (s_home_valid) {
        lv_coord_t x = (lv_coord_t)lon_to_x(s_home_lon);
        lv_coord_t y = (lv_coord_t)lat_to_y(s_home_lat);
        lv_area_t  home = { x - 7, y - 7, x + 7, y + 7 };
        take_area(&home);
    }

    /* Spots are ordered by their most important station, so they get the room first */
    for (int s = 0; s < n_spots; s++) {
        draw_spot_calls(draw_ctx, &spots[s], totals[s]);
    }
}

static void draw_mode(lv_draw_ctx_t *draw_ctx, lv_area_t *area) {
    static const char *names[] = {
        [MODE_LON]  = "< Lon >",
        [MODE_LAT]  = "^ Lat v",
        [MODE_ZOOM] = "- Zoom +",
    };

    lv_draw_label_dsc_t label;
    char                buf[32];

    lv_draw_label_dsc_init(&label);
    label.font  = &sony_16;
    label.align = LV_TEXT_ALIGN_LEFT;
    label.color = lv_color_white();

    snprintf(buf, sizeof(buf), "MFK: %s   %ix", names[s_mode], zoom_levels[s_zoom]);
    lv_coord_t w = text_width(label.font, buf) + 12;

    area->x1 = s_w - w;
    area->y1 = 0;
    area->x2 = s_w - 1;
    area->y2 = label.font->line_height + 5;

    draw_box(draw_ctx, area, LV_OPA_70);
    draw_text(draw_ctx, &label, area->x1 + 6, 3, w, buf);
}

static void draw_overlay(lv_event_t *e) {
    lv_draw_ctx_t       *draw_ctx = lv_event_get_draw_ctx(e);
    lv_draw_label_dsc_t  label;
    char                 buf[96];

    draw_grid_labels(draw_ctx);

    /* Stations, most important kinds on top */
    time_t now   = time(NULL);
    int    count = 0;
    bool   calls = zoom_levels[s_zoom] >= CALLS_ZOOM;

    lv_draw_label_dsc_init(&label);
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
            if (!calls && st->kind == MAP_STATION_TO_ME) {
                draw_text(draw_ctx, &label, (lv_coord_t)x + 7, (lv_coord_t)y - label.font->line_height / 2, 120,
                          st->call);
            }
        }
    }

    if (s_home_valid) {
        draw_dot(draw_ctx, lon_to_x(s_home_lon), lat_to_y(s_home_lat), 6, 0xFFFFFF);
        draw_dot(draw_ctx, lon_to_x(s_home_lon), lat_to_y(s_home_lat), 2, 0x000000);
    }

    /* Mode indicator and legend, call labels keep clear of them */
    lv_area_t mode_area;
    draw_mode(draw_ctx, &mode_area);

    lv_area_t legend = {
        .x1 = 0,
        .y1 = s_h - label.font->line_height - 6,
        .x2 = s_w - 1,
        .y2 = s_h - 1,
    };

    if (calls) {
        s_taken_n = 0;
        take_area(&mode_area);
        take_area(&legend);
        if (squares_shown()) {
            lv_area_t top, left;
            ruler_areas(&top, &left);
            take_area(&top);
            take_area(&left);
        }
        draw_calls(draw_ctx, now);
    }

    draw_box(draw_ctx, &legend, LV_OPA_70);
    label.flag = LV_TEXT_FLAG_RECOLOR;
    snprintf(buf, sizeof(buf), "#00DD00 CQ#   #6A8A40 Worked#   #E0C000 Other#   #FF4040 To me#      Stations: %i", count);
    draw_text(draw_ctx, &label, 8, s_h - label.font->line_height - 3, s_w - 16, buf);
}

/* -------- LVGL event plumbing ------------------------------------------ */

/* One MFK step in the current mode, dir is +1 (clockwise) or -1 */
static void step(int dir) {
    switch (s_mode) {
        case MODE_LON:
            s_center_lon = wrap_lon(s_center_lon + dir * view_w_deg() * PAN_FRACTION);
            break;
        case MODE_LAT:
            s_center_lat += dir * view_h_deg() * PAN_FRACTION;
            clamp_lat();
            break;
        case MODE_ZOOM:
            set_zoom(s_zoom + dir);
            break;
    }
    render();
}

static void event_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_DRAW_POST) {
        draw_overlay(e);
    } else if (code == LV_EVENT_SHORT_CLICKED) {
        next_mode();
        lv_obj_invalidate(s_obj);
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
                step(-1);
                break;
            case LV_KEY_RIGHT:
                step(1);
                break;
            /* Keyboard: arrows up/down and +/- work in any mode */
            case LV_KEY_UP:
            case LV_KEY_DOWN:
                if (s_zoom > 0) {
                    s_center_lat += (key == LV_KEY_UP ? 1 : -1) * view_h_deg() * PAN_FRACTION;
                    clamp_lat();
                    render();
                }
                break;
            case '+':
            case '-':
                set_zoom(s_zoom + (key == '+' ? 1 : -1));
                render();
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
    s_w        = w;
    s_h        = h;
    s_base_ppd = h / (LAT_TOP - LAT_BOTTOM);
    s_ppd      = s_base_ppd;
    s_zoom     = 0;
    s_mode     = MODE_LON;
    memset(&s_actions, 0, sizeof(s_actions));

    s_buf      = malloc(sizeof(lv_color_t) * w * h);
    s_cross    = malloc(sizeof(int16_t) * MAX_CROSS * h);
    s_cross_n  = malloc(h);
    s_land_box = malloc(sizeof(box_t) * world_land_count);

    const int16_t (*p)[2] = world_land_pts;
    for (uint16_t r = 0; r < world_land_count; r++) {
        box_t *box = &s_land_box[r];
        box->lon0 = box->lon1 = p[0][0];
        box->lat0 = box->lat1 = p[0][1];
        for (uint16_t i = 1; i < world_land_len[r]; i++) {
            box->lon0 = LV_MIN(box->lon0, p[i][0]);
            box->lon1 = LV_MAX(box->lon1, p[i][0]);
            box->lat0 = LV_MIN(box->lat0, p[i][1]);
            box->lat1 = LV_MAX(box->lat1, p[i][1]);
        }
        p += world_land_len[r];
    }

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
    clamp_lat();
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
    free(s_land_box);
    s_buf      = NULL;
    s_cross    = NULL;
    s_cross_n  = NULL;
    s_land_box = NULL;
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
        if (s_home_valid) s_center_lat = s_home_lat;
        clamp_lat();
        render();
    }
}

void map_view_go_home(void) {
    if (!s_obj || !s_home_valid) return;

    go_home();
    render();
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

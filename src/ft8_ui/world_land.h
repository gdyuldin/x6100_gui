/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI - coarse world land outlines and country borders
 *  for the FT8 map
 */

#pragma once

#include <stdint.h>

/* Points are {lon, lat} in 0.1 degree units, stored back to back;
 * *_len[i] is the number of points of line i. Land lines are closed rings,
 * borders are open polylines. */
extern const int16_t  world_land_pts[][2];
extern const uint16_t world_land_len[];
extern const uint16_t world_land_count;

extern const int16_t  world_border_pts[][2];
extern const uint16_t world_border_len[];
extern const uint16_t world_border_count;

/* State/province lines of large countries only */
extern const int16_t  world_admin1_pts[][2];
extern const uint16_t world_admin1_len[];
extern const uint16_t world_admin1_count;

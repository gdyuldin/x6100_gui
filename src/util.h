/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

#pragma once

#include "helpers.h"

#ifdef __cplusplus

#include "vector"


extern "C" {
#endif
#include <aether_radio/x6100_control/control.h>
#include <stdint.h>
#include <stdbool.h>
#include <sys/types.h>
#include <liquid/liquid.h>

/* Macros */

#define ARRAY_SIZE(arr) (sizeof((arr)) / sizeof((arr)[0]))

uint64_t get_time();
void get_time_str(char *str, size_t str_size);

void split_freq(int32_t freq, uint16_t *mhz, uint16_t *khz, uint16_t *hz);
int32_t align_int(int32_t x, uint16_t step);
int32_t limit(int32_t x, int32_t min, int32_t max);
float sqr(float x);
void lpf(float *x, float current, float beta, float initial);
void lpf_block(float *x, float *current, float beta, unsigned int count);

char *util_canonize_callsign(const char *callsign, bool strip_slashes);

void sleep_usec(uint32_t msec);

int32_t util_compare_version(x6100_base_ver_t a, x6100_base_ver_t b);

#ifdef __cplusplus
}
#endif

/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <sys/types.h>
#include <liquid/liquid.h>

uint64_t get_time();
void get_time_str(char *str, size_t str_size);

void split_freq(uint64_t freq, uint16_t *mhz, uint16_t *khz, uint16_t *hz);
int32_t align_int(int32_t x, uint16_t step);
uint64_t align_long(uint64_t x, uint16_t step);
int32_t limit(int32_t x, int32_t min, int32_t max);
float sqr(float x);
void lpf(float *x, float current, float beta, float initial);
void lpf_block(float *x, float *current, float beta, unsigned int count);

void to_bcd(uint8_t bcd_data[], uint64_t data, uint8_t len);
uint64_t from_bcd(const uint8_t bcd_data[], uint8_t len);
int loop_modes(int16_t dir, int mode, uint64_t modes, const int max_val);
int sign(int x);

typedef struct wrms_s * wrms_t;

wrms_t wrms_create(size_t n, size_t delay);
void wrms_destroy(wrms_t wr);
size_t wrms_size(wrms_t wr);
size_t wrms_delay(wrms_t wr);

void wrms_pushcf(wrms_t wr, liquid_float_complex x);
bool wrms_ready(wrms_t wr);
float wrms_get_val(wrms_t wr);

size_t argmax(float *x, size_t n);

char *util_canonize_callsign(const char *callsign, bool strip_slashes);

void sleep_usec(uint32_t msec);

/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */
#include "util.h"

#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <sys/time.h>
#include <time.h>


/**
 * Return time in ms from unix epoch
 */
uint64_t get_time() {
    struct timespec now;

    clock_gettime(CLOCK_MONOTONIC, &now);

    uint64_t usec = (uint64_t) now.tv_sec * 1000L + now.tv_nsec / 1000000L;

    return usec;
}

void get_time_str(char *str, size_t str_size) {
    time_t      now = time(NULL);
    struct tm   *t = localtime(&now);

    snprintf(str, str_size, "%04i-%02i-%02i %02i-%02i-%02i", t->tm_year + 1900, t->tm_mon + 1, t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec);
}

void split_freq(uint64_t freq, uint16_t *mhz, uint16_t *khz, uint16_t *hz) {
    *mhz = freq / 1000000;
    *khz = (freq / 1000) % 1000;
    *hz = freq % 1000;
}

int32_t align_int(int32_t x, uint16_t step) {
    if (step == 0) {
        return x;
    }

    return x - (x % step);
}

uint64_t align_long(uint64_t x, uint16_t step) {
    if (step == 0) {
        return x;
    }

    return x - (x % step);
}

int32_t limit(int32_t x, int32_t min, int32_t max) {
    if (x < min) {
        return min;
    } else if (x > max) {
        return max;
    }

    return x;
}

float sqr(float x) {
    return x * x;
}

void lpf(float *x, float current, float beta, float initial) {
    if (*x == initial){
        *x = current;
    } else {
        *x = *x * beta + current * (1.0f - beta);
    }

}

void lpf_block(float *x, float *current, float beta, unsigned int count) {
    liquid_vectorf_mulscalar(current, count, (1.0f - beta), current);
    liquid_vectorf_mulscalar(x, count, beta, x);
    liquid_vectorf_add(x, current, count, x);
}

void to_bcd(uint8_t bcd_data[], uint64_t data, uint8_t len) {
    int16_t i;

    for (i = 0; i < len / 2; i++) {
        uint8_t a = data % 10;

        data /= 10;
        a |= (data % 10) << 4;
        data /= 10;
        bcd_data[i] = a;
    }

    if (len & 1) {
        bcd_data[i] &= 0x0f;
        bcd_data[i] |= data % 10;
    }
}

uint64_t from_bcd(const uint8_t bcd_data[], uint8_t len) {
    int16_t     i;
    uint64_t    data = 0;

    if (len & 1) {
        data = bcd_data[len / 2] & 0x0F;
    }

    for (i = (len / 2) - 1; i >= 0; i--) {
        data *= 10;
        data += bcd_data[i] >> 4;
        data *= 10;
        data += bcd_data[i] & 0x0F;
    }

    return data;
}


int loop_modes(int16_t dir, int mode, uint64_t modes, int max_val) {
    while (1) {
        if (dir > 0) {
            if (mode == max_val) {
                mode = 0;
            } else {
                mode++;
            }
        } else {
            if (dir < 0)
            {
                if (mode == 0) {
                    mode = max_val;
                } else {
                    mode--;
                }
            }
        }
        if (modes & (1LL << mode)) {
            break;
        }
        if (dir == 0)
        {
            if (mode == max_val) {
                mode = 0;
            } else {
                mode++;
            }
        }
    }
    return mode;
}

int sign(int x) {
    return (x > 0) - (x < 0);
}

// Window rms

struct wrms_s {
    windowcf window;
    size_t size;
    size_t delay;
    int16_t remain;
};

wrms_t wrms_create(size_t n, size_t delay) {
    wrms_t wr = (wrms_t) malloc(sizeof(struct wrms_s));
    wr->size = n;
    wr->delay = delay;
    wr->remain = wr->delay;
    wr->window = windowcf_create(n);
    return wr;
}

void wrms_destroy(wrms_t wr) {
    windowcf_destroy(wr->window);
    free(wr);
}

size_t wrms_size(wrms_t wr) {
    return wr->size;
}

size_t wrms_delay(wrms_t wr) {
    return wr->delay;
}

void wrms_push(wrms_t wr, liquid_float_complex x) {
    if (wr->remain == 0) {
        wr->remain = wr->delay;
    }
    wr->remain--;
    windowcf_push(wr->window, x);
}

bool wrms_ready(wrms_t wr) {
    return wr->remain == 0;
}

float wrms_get_val(wrms_t wr) {
    float complex * r;
    windowcf_read(wr->window, &r);
    float rms = 0.0;
    for (uint8_t i = 0; i < wr->size; i++) {
        rms += crealf(r[i] * conjf(r[i]));
    }
    rms = sqrtf(rms / wr->size);
    return rms;
}

size_t argmax(float * x, size_t n) {
    float max = -INFINITY;
    size_t pos;
    for (size_t i = 0; i < n; i++)
    {
        if (x[i] > max) {
            max = x[i];
            pos = i;
        }
    }
    return pos;
}

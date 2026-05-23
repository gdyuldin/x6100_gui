/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */
#include "rtty.h"

#include "audio.h"
#include "panel.h"
#include "params/params.h"
#include "util.h"
#include "cfg/subjects.h"
#include "tx_log.h"

#include "lvgl/lvgl.h"

#include <math.h>
#include <stdlib.h>
#include <pthread.h>

#define SYMBOL_OVER 8
#define SYMBOL_FACTOR 2
#define SYMBOL_LEN (SYMBOL_OVER * SYMBOL_FACTOR)

#define RTTY_SYMBOL_CODE (0b11011)
#define RTTY_LETTER_CODE (0b11111)

typedef enum {
    RX_STATE_IDLE,
    RX_STATE_START,
    RX_STATE_DATA,
    RX_STATE_STOP
} rx_state_t;

static pthread_mutex_t rtty_mux;

static fskdem demod = NULL;

static nco_crcf       nco     = NULL;
static float complex *nco_buf = NULL;

static uint16_t symbol_samples;
static uint16_t symbol_over;

static cbuffercf      rx_buf;
static complex float *rx_window = NULL;
static uint8_t        rx_symbol[SYMBOL_LEN];
static float          rx_symbol_pwr[SYMBOL_LEN];
static uint8_t        rx_symbol_cur = 0;
static rx_state_t     rx_state      = RX_STATE_IDLE;
static uint8_t        rx_counter    = 0;
static uint8_t        rx_bitcntr    = 0;
static uint8_t        rx_data       = 0;
static bool           rx_letter     = true;

static bool         ready = false;
static rtty_state_t state = RTTY_OFF;

static x6100_mode_t cur_mode;

static const char rtty_letters[32] = {'\0', 'E', '\n', 'A', ' ', 'S', 'I', 'U', '\0', 'D', 'R',
                                      'J',  'N', 'F',  'C', 'K', 'T', 'Z', 'L', 'W',  'H', 'Y',
                                      'P',  'Q', 'O',  'B', 'G', ' ', 'M', 'X', 'V',  ' '};

static const char rtty_symbols[32] = {'\0', '3', '\n', '-', ' ', '\0', '8', '7', '\0', '$', '4',
                                      '\'', ',', '!',  ':', '(', '5',  '"', ')', '2',  '#', '6',
                                      '0',  '1', '9',  '?', '&', ' ',  '.', '/', ';',  ' '};

static void on_cur_mode_change(Subject *subj, void *user_data);

static void update_nco() {
    float radians = 2.0f * (float)M_PI * (float)params.rtty_center / (float)AUDIO_CAPTURE_RATE;

    nco_crcf_set_phase(nco, 0.0f);
    nco_crcf_set_frequency(nco, radians);
}

static void init() {
    symbol_samples = (float)AUDIO_CAPTURE_RATE / (float)(params.rtty_rate / 100.0f) / (float)SYMBOL_FACTOR + 0.5f;
    symbol_over    = symbol_samples / SYMBOL_OVER;

    nco     = nco_crcf_create(LIQUID_NCO);
    nco_buf = (float complex *)malloc(symbol_samples * sizeof(float complex));
    update_nco();

    /* RX */

    demod  = fskdem_create(1, symbol_samples, (float)params.rtty_shift / (float)AUDIO_CAPTURE_RATE / 2.0f);
    rx_buf = cbuffercf_create(symbol_samples * 50);

    rx_window = malloc(symbol_samples * sizeof(complex float));

    for (uint16_t i = 0; i < symbol_samples; i++)
        rx_window[i] = liquid_hann(i, symbol_samples);

    ready = true;
}

static void done() {
    ready = false;

    nco_crcf_destroy(nco);
    free(nco_buf);

    fskdem_destroy(demod);
    cbuffercf_destroy(rx_buf);
    free(rx_window);
}

static void update() {
    pthread_mutex_lock(&rtty_mux);
    done();
    init();
    pthread_mutex_unlock(&rtty_mux);
}

void rtty_init() {
    pthread_mutex_init(&rtty_mux, NULL);
    subject_add_observer_and_call(cfg_cur.mode, on_cur_mode_change, NULL);
    init();
}

static char baudot_decoder(uint8_t c) {
    if (c == RTTY_SYMBOL_CODE) {
        rx_letter = false;
        return 0;
    }

    if (c == RTTY_LETTER_CODE) {
        rx_letter = true;
        return 0;
    }

    return rx_letter ? rtty_letters[c] : rtty_symbols[c];
}

static bool is_mark_space(uint8_t *correction) {
    uint16_t res = 0;

    if (rx_symbol[0] && !rx_symbol[SYMBOL_LEN - 1]) {
        for (int i = 0; i < SYMBOL_LEN; i++)
            res += rx_symbol[i];

        if (abs(SYMBOL_LEN / 2 - res) < 1) {
            *correction = res;
            return true;
        }
    }
    return false;
}

static bool is_mark() {
    return rx_symbol[SYMBOL_LEN / 2];
}

static void add_symbol(float pwr) {
    for (uint8_t i = 1; i < SYMBOL_LEN; i++) {
        rx_symbol[i - 1]     = rx_symbol[i];
        rx_symbol_pwr[i - 1] = rx_symbol_pwr[i];
    }

    rx_symbol_pwr[SYMBOL_LEN - 1] = pwr;

    float   p_avr = 0.0f;
    uint8_t p_num = SYMBOL_LEN / 2;

    for (uint8_t i = SYMBOL_LEN - p_num; i < SYMBOL_LEN; i++)
        p_avr += rx_symbol_pwr[i];

    p_avr /= (float)p_num;

    if (rx_symbol_cur == 0) {
        if (p_avr > params.rtty_snr) {
            rx_symbol_cur = 1;
        }
    } else {
        if (p_avr < -params.rtty_snr) {
            rx_symbol_cur = 0;
        }
    }

    /* LV_LOG_USER("%5.1f %i", p_avr, rx_symbol_cur); */

    rx_symbol[SYMBOL_LEN - 1] = rx_symbol_cur;

    uint8_t correction;

    switch (rx_state) {
        case RX_STATE_IDLE:
            if (is_mark_space(&correction)) {
                rx_state   = RX_STATE_START;
                rx_counter = correction;
            }
            break;

        case RX_STATE_START:
            if (--rx_counter == 0) {
                if (!is_mark()) {
                    rx_state   = RX_STATE_DATA;
                    rx_counter = SYMBOL_LEN;
                    rx_bitcntr = 0;
                    rx_data    = 0;
                } else {
                    rx_state = RX_STATE_IDLE;
                }
            }
            break;

        case RX_STATE_DATA:
            if (--rx_counter == 0) {
                rx_data |= is_mark() << rx_bitcntr++;
                rx_counter = SYMBOL_LEN;
            }

            if (rx_bitcntr == params.rtty_bits)
                rx_state = RX_STATE_STOP;
            break;

        case RX_STATE_STOP:
            if (--rx_counter == 0) {
                if (is_mark()) {
                    char c = baudot_decoder(rx_data);

                    if (c) {
                        char str[2] = {c, 0};

                        panel_add_text(str);
                    }
                }
                rx_state = RX_STATE_IDLE;
            }
            break;
    }
}

void rtty_put_audio_samples(unsigned int n, cfloat *samples) {
    pthread_mutex_lock(&rtty_mux);

    if (!ready) {
        pthread_mutex_unlock(&rtty_mux);
        return;
    }

    cbuffercf_write(rx_buf, samples, n);

    while (cbuffercf_size(rx_buf) > symbol_samples) {
        unsigned int   symbol;
        unsigned int   n;
        cfloat *buf;

        cbuffercf_read(rx_buf, symbol_samples, &buf, &n);
        nco_crcf_mix_block_down(nco, buf, nco_buf, n);

        for (uint16_t i = 0; i < n; i++)
            nco_buf[i] *= rx_window[i];

        fskdem_demodulate(demod, nco_buf);

        float pwr0 = 10.0f * log10f(fskdem_get_symbol_energy(demod, 0, 1));
        float pwr1 = 10.0f * log10f(fskdem_get_symbol_energy(demod, 1, 1));
        float pwr  = pwr0 - pwr1;

        if (((cur_mode == x6100_mode_usb || cur_mode == x6100_mode_usb_dig) && !params.rtty_reverse) ||
            ((cur_mode == x6100_mode_lsb || cur_mode == x6100_mode_lsb_dig) && params.rtty_reverse)) {
            pwr = -pwr;
        }

        add_symbol(pwr);

        cbuffercf_release(rx_buf, symbol_over);
    }

    pthread_mutex_unlock(&rtty_mux);
}

void rtty_set_state(rtty_state_t x) {
    if (state != x) {
        state = x;
        if (x == RTTY_TX) {
            tx_log_event(
                "TX_ATTEMPT_RTTY_ON",
                subject_get_int(cfg_cur.fg_freq),
                subject_get_int(cfg_cur.mode),
                subject_get_float(cfg.pwr.val),
                NULL
            );
        } else {
            tx_log_event(
                "TX_ATTEMPT_RTTY_OFF",
                subject_get_int(cfg_cur.fg_freq),
                subject_get_int(cfg_cur.mode),
                subject_get_float(cfg.pwr.val),
                NULL
            );
        }
    }
}

rtty_state_t rtty_get_state() {
    return state;
}

float rtty_change_rate(int16_t df) {
    if (df == 0) {
        return (float)params.rtty_rate / 100.0f;
    }

    params_lock();

    switch (params.rtty_rate) {
        case 4500:
            params.rtty_rate = df > 0 ? 4545 : 15000;
            break;

        case 4545:
            params.rtty_rate = df > 0 ? 5000 : 4500;
            break;

        case 5000:
            params.rtty_rate = df > 0 ? 5600 : 4545;
            break;

        case 5600:
            params.rtty_rate = df > 0 ? 7500 : 5000;
            break;

        case 7500:
            params.rtty_rate = df > 0 ? 10000 : 5600;
            break;

        case 10000:
            params.rtty_rate = df > 0 ? 11000 : 7500;
            break;

        case 11000:
            params.rtty_rate = df > 0 ? 15000 : 10000;
            break;

        case 15000:
            params.rtty_rate = df > 0 ? 4500 : 11000;
            break;

        default:
            params.rtty_rate = 4500;
            break;
    }

    params_unlock(&params.dirty.rtty_rate);
    update();

    return (float)params.rtty_rate / 100.0f;
}

uint16_t rtty_change_shift(int16_t df) {
    if (df == 0) {
        return params.rtty_shift;
    }

    params_lock();

    switch (params.rtty_shift) {
        case 170:
            params.rtty_shift = df > 0 ? 425 : 850;
            break;

        case 425:
            params.rtty_shift = df > 0 ? 450 : 170;
            break;

        case 450:
            params.rtty_shift = df > 0 ? 850 : 425;
            break;

        case 850:
            params.rtty_shift = df > 0 ? 170 : 450;
            break;

        default:
            params.rtty_shift = 170;
            break;
    }

    params_unlock(&params.dirty.rtty_shift);
    update();

    return params.rtty_shift;
}

uint16_t rtty_change_center(int16_t df) {
    if (df == 0) {
        return params.rtty_center;
    }

    params_lock();
    params.rtty_center = limit(align_int(params.rtty_center + df * 10, 10), 800, 1600);
    params_unlock(&params.dirty.rtty_center);

    pthread_mutex_lock(&rtty_mux);
    update_nco();
    pthread_mutex_unlock(&rtty_mux);

    return params.rtty_center;
}

bool rtty_change_reverse(int16_t df) {
    if (df == 0) {
        return params.rtty_reverse;
    }

    params_lock();
    params.rtty_reverse = !params.rtty_reverse;
    params_unlock(&params.dirty.rtty_reverse);

    return params.rtty_reverse;
}

static void on_cur_mode_change(Subject *subj, void *user_data) {
    cur_mode = subject_get_int(subj);
}

/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */
#include "cw_encoder.h"

#include "cw_decoder.h"
#include "params/params.h"
#include "cfg/cfg.h"
#include "cfg/subjects.h"
#include "radio.h"
#include "msg.h"
#include "buttons.h"
#include "tx_log.h"

#include "lvgl/lvgl.h"
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sched.h>


static cw_encoder_state_t   state = CW_ENCODER_IDLE;
static pthread_t            thread;

static char                 *current_msg = NULL;
static char                 *current_char = NULL;

static uint8_t get_morse(char *str, char **morse) {
    cw_characters_t *character = &cw_characters[0];

    while (character->morse) {
        uint8_t char_len = strlen(character->character);

        if (strncasecmp(character->character, str, char_len) == 0) {
            *morse = character->morse;

            return char_len;
        }

        character++;
    }

    return 0;
}

static void send_morse(char *str, time_t dit_nsec, time_t dah_nsec) {
    struct timespec t;
    t.tv_sec = 0;
    while (*str) {
        switch (*str) {
            case '.':
                t.tv_nsec = dit_nsec;
                radio_set_morse_key(true);
                nanosleep(&t, NULL);
                radio_set_morse_key(false);
                break;

            case '-':
                t.tv_nsec = dah_nsec;
                radio_set_morse_key(true);
                nanosleep(&t, NULL);
                radio_set_morse_key(false);
                break;

            default:
                break;
        }
        str++;
        t.tv_nsec = dit_nsec;
        nanosleep(&t, NULL);
    }
    t.tv_nsec = dah_nsec - dit_nsec;
    nanosleep(&t, NULL);
}

static void * endecode_thread(void *arg) {
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);


    time_t dit_nsec = 20000000L / (subject_get_int(cfg.key_speed.val)) * 60;
    time_t dah_nsec = dit_nsec * subject_get_float(cfg.key_ratio.val);
    time_t world_space_nsec = dit_nsec * 7;

    struct timespec t;

    t.tv_sec = 0;

    while (true) {
        char    *morse;
        uint8_t len;

        if (*current_char == ' ') {
            current_char++;
            t.tv_nsec = world_space_nsec - dah_nsec;
            nanosleep(&t, NULL);
        } else {
            len = get_morse(current_char, &morse);
            if (len) {
                send_morse(morse, dit_nsec, dah_nsec);
                current_char += len;
            } else {
                current_char++;
                t.tv_nsec = world_space_nsec - dah_nsec;
                nanosleep(&t, NULL);
            }
        }
        if (*current_char == 0) {
            if (state == CW_ENCODER_SEND) {
                state = CW_ENCODER_IDLE;

                buttons_unload_page();
                buttons_load_page(&buttons_page_msg_cw_1);
                break;
            } else {
                state = CW_ENCODER_BEACON_IDLE;
                msg_update_text_fmt("Beacon pause: %i s", params.cw_encoder_period);
                sleep(params.cw_encoder_period);

                state = CW_ENCODER_BEACON;
                current_char = current_msg;
            }
        }
    }
}

void cw_encoder_stop() {
    if (state != CW_ENCODER_IDLE) {
        tx_log_event(
            "TX_ATTEMPT_CW_STOP",
            subject_get_int(cfg_cur.fg_freq),
            subject_get_int(cfg_cur.mode),
            subject_get_float(cfg.pwr.val),
            NULL
        );
        pthread_cancel(thread);
        pthread_join(thread, NULL);

        radio_set_morse_key(false);
        state = CW_ENCODER_IDLE;
    }
}

void cw_encoder_send(const char *text, bool beacon) {
    cw_encoder_stop();

    tx_log_event(
        beacon ? "TX_ATTEMPT_CW_BEACON" : "TX_ATTEMPT_CW_SEND",
        subject_get_int(cfg_cur.fg_freq),
        subject_get_int(cfg_cur.mode),
        subject_get_float(cfg.pwr.val),
        text
    );

    if (current_msg) {
        free(current_msg);
    }

    current_msg = strdup(text);
    current_char = current_msg;
    state = beacon ? CW_ENCODER_BEACON : CW_ENCODER_SEND;

    int rc;
    pthread_attr_t attr;
    struct sched_param param;
    rc = pthread_attr_init (&attr);
    rc = pthread_attr_getschedparam (&attr, &param);
    param.sched_priority += 5;
    rc = pthread_attr_setschedparam (&attr, &param);

    pthread_create(&thread, &attr, endecode_thread, NULL);
}

cw_encoder_state_t cw_encoder_state() {
    return state;
}

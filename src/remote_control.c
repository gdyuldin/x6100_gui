/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2026
 */

#include "remote_control.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "events.h"
#include "main.h"
#include "vol.h"
#include "backlight.h"
#include "keyboard.h"

#define REMOTE_CTRL_FIFO "/tmp/x6100_remote_ctrl"
#define REMOTE_LINE_MAX 256

static int remote_fd = -1;
static char line_buf[REMOTE_LINE_MAX];
static size_t line_len = 0;

typedef struct {
    const char *name;
    keypad_key_t key;
} key_map_t;

static const key_map_t keypad_keys[] = {
    {"POWER", KEYPAD_POWER},
    {"GEN", KEYPAD_GEN},
    {"APP", KEYPAD_APP},
    {"KEY", KEYPAD_KEY},
    {"MSG", KEYPAD_MSG},
    {"DFN", KEYPAD_DFN},
    {"DFL", KEYPAD_DFL},
    {"F1", KEYPAD_F1},
    {"F2", KEYPAD_F2},
    {"F3", KEYPAD_F3},
    {"F4", KEYPAD_F4},
    {"F5", KEYPAD_F5},
    {"LOCK", KEYPAD_LOCK},
    {"PTT", KEYPAD_PTT},
    {"BAND_DOWN", KEYPAD_BAND_DOWN},
    {"BAND_UP", KEYPAD_BAND_UP},
    {"MODE_AM", KEYPAD_MODE_AM},
    {"MODE_CW", KEYPAD_MODE_CW},
    {"MODE_SSB", KEYPAD_MODE_SSB},
    {"AB", KEYPAD_AB},
    {"PRE", KEYPAD_PRE},
    {"ATU", KEYPAD_ATU},
    {"VM", KEYPAD_VM},
    {"AGC", KEYPAD_AGC},
    {"FST", KEYPAD_FST},
};

static void to_upper_str(char *s) {
    for (; *s; ++s) {
        *s = (char)toupper((unsigned char)*s);
    }
}

static keypad_key_t find_keypad_key(const char *name) {
    for (size_t i = 0; i < sizeof(keypad_keys) / sizeof(keypad_keys[0]); i++) {
        if (strcmp(name, keypad_keys[i].name) == 0) {
            return keypad_keys[i].key;
        }
    }
    return KEYPAD_UNKNOWN;
}

static keypad_state_t parse_key_state(const char *action) {
    if (strcmp(action, "PRESS") == 0) {
        return KEYPAD_PRESS;
    }
    if (strcmp(action, "RELEASE") == 0) {
        return KEYPAD_RELEASE;
    }
    if (strcmp(action, "LONG") == 0) {
        return KEYPAD_LONG;
    }
    if (strcmp(action, "LONG_RELEASE") == 0) {
        return KEYPAD_LONG_RELEASE;
    }
    return KEYPAD_PRESS;
}

static void send_keypad_event(keypad_key_t key, keypad_state_t state) {
    event_keypad_t *evt = malloc(sizeof(event_keypad_t));
    if (!evt) {
        return;
    }
    evt->key = key;
    evt->state = state;
    event_send(lv_scr_act(), EVENT_KEYPAD, (void *)evt);
}

static void send_encoder_keys(int32_t key_left, int32_t key_right, int32_t diff) {
    int32_t steps = diff < 0 ? -diff : diff;
    if (steps == 0) {
        return;
    }

    int32_t key = diff < 0 ? key_left : key_right;
    for (int32_t i = 0; i < steps; i++) {
        event_send_key(key);
    }
}

static void send_vfo_rotary(int32_t diff) {
    if (diff == 0) {
        return;
    }
    int32_t *delta = malloc(sizeof(int32_t));
    if (!delta) {
        return;
    }
    *delta = diff;
    /* Match rotary.c semantics: the receiver (e.g. main_screen_rotary_cb)
     * owns and frees the allocated delta.
     * Using event_send() would also free it in event_obj_check(), causing a
     * double-free and UI reset/crash.
     */
    lv_event_send(lv_scr_act(), EVENT_ROTARY, (void *)delta);
}

static void handle_key_command(const char *name, const char *action) {
    char key_name[REMOTE_LINE_MAX];
    char action_name[REMOTE_LINE_MAX];

    snprintf(key_name, sizeof(key_name), "%s", name);
    snprintf(action_name, sizeof(action_name), "%s", action);
    to_upper_str(key_name);
    to_upper_str(action_name);

    keypad_key_t key = find_keypad_key(key_name);
    if (key == KEYPAD_UNKNOWN) {
        return;
    }

    if (strcmp(action_name, "CLICK") == 0) {
        send_keypad_event(key, KEYPAD_PRESS);
        send_keypad_event(key, KEYPAD_RELEASE);
    } else {
        keypad_state_t state = parse_key_state(action_name);
        send_keypad_event(key, state);
    }
}

static void handle_knob_command(const char *name, const char *delta_str) {
    char knob_name[REMOTE_LINE_MAX];

    snprintf(knob_name, sizeof(knob_name), "%s", name);
    to_upper_str(knob_name);

    int32_t diff = (int32_t)strtol(delta_str, NULL, 10);
    if (diff == 0) {
        return;
    }

    if (strcmp(knob_name, "VFO") == 0) {
        send_vfo_rotary(diff);
    } else if (strcmp(knob_name, "MFK") == 0) {
        send_encoder_keys(LV_KEY_LEFT, LV_KEY_RIGHT, diff);
    } else if (strcmp(knob_name, "VOL") == 0) {
        bool is_select = (vol_get_state() == VOL_STATE_SELECT);
        int32_t key_left  = is_select ? KEY_VOL_LEFT_SELECT  : KEY_VOL_LEFT_EDIT;
        int32_t key_right = is_select ? KEY_VOL_RIGHT_SELECT : KEY_VOL_RIGHT_EDIT;
        send_encoder_keys(key_left, key_right, diff);
    }
}

static void handle_knob_press(const char *name, const char *action) {
    char knob_name[REMOTE_LINE_MAX];
    char action_name[REMOTE_LINE_MAX];

    snprintf(knob_name, sizeof(knob_name), "%s", name);
    snprintf(action_name, sizeof(action_name), "%s", action);
    to_upper_str(knob_name);
    to_upper_str(action_name);

    if (strcmp(knob_name, "MFK") == 0) {
        if (strcmp(action_name, "PRESS") == 0 || strcmp(action_name, "CLICK") == 0) {
            lv_obj_t *focused = lv_group_get_focused(keyboard_group);
            if (focused) {
                event_send(focused, LV_EVENT_PRESSED, NULL);
            }
        }
    } else if (strcmp(knob_name, "VOL") == 0) {
        if (strcmp(action_name, "PRESS") == 0 || strcmp(action_name, "CLICK") == 0) {
            event_send_key(LV_KEY_ESC);
        }
    }
}

static void handle_line(const char *line) {
    char cmd[REMOTE_LINE_MAX] = {0};
    char arg1[REMOTE_LINE_MAX] = {0};
    char arg2[REMOTE_LINE_MAX] = {0};

    if (sscanf(line, "%255s %255s %255s", cmd, arg1, arg2) < 2) {
        return;
    }

    to_upper_str(cmd);
    backlight_tick();

    if (strcmp(cmd, "KEY") == 0) {
        if (arg2[0] == '\0') {
            return;
        }
        handle_key_command(arg1, arg2);
    } else if (strcmp(cmd, "KNOB") == 0) {
        if (arg2[0] == '\0') {
            return;
        }
        handle_knob_command(arg1, arg2);
    } else if (strcmp(cmd, "KNOB_PRESS") == 0) {
        if (arg2[0] == '\0') {
            return;
        }
        handle_knob_press(arg1, arg2);
    }
}

static void reopen_fifo() {
    if (remote_fd >= 0) {
        close(remote_fd);
        remote_fd = -1;
    }
    remote_fd = open(REMOTE_CTRL_FIFO, O_RDONLY | O_NONBLOCK);
}

void remote_control_init() {
    struct stat st;
    if (stat(REMOTE_CTRL_FIFO, &st) != 0) {
        mkfifo(REMOTE_CTRL_FIFO, 0666);
    } else if (!S_ISFIFO(st.st_mode)) {
        unlink(REMOTE_CTRL_FIFO);
        mkfifo(REMOTE_CTRL_FIFO, 0666);
    }

    reopen_fifo();
}

void remote_control_poll() {
    if (remote_fd < 0) {
        return;
    }

    char buf[128];
    ssize_t n = read(remote_fd, buf, sizeof(buf));

    if (n == 0) {
        reopen_fifo();
        return;
    }
    if (n < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            reopen_fifo();
        }
        return;
    }

    for (ssize_t i = 0; i < n; i++) {
        char c = buf[i];
        if (c == '\n') {
            line_buf[line_len] = '\0';
            if (line_len > 0) {
                handle_line(line_buf);
            }
            line_len = 0;
        } else if (line_len + 1 < sizeof(line_buf)) {
            line_buf[line_len++] = c;
        } else {
            line_len = 0;
        }
    }
}

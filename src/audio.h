/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>

#include <aether_radio/x6100_control/control.h>

#define AUDIO_PLAY_RATE     (44100)
#define AUDIO_CAPTURE_RATE  (44100)
// #define AUDIO_PLAY_RATE     (22050)
// #define AUDIO_CAPTURE_RATE  (22050)

typedef enum {
    AUDIO_PLAY_OFF, // No play, BASE will not stream audio from mic or this app
    AUDIO_PLAY_ON, // Play mode, BASE will stream audio from this app. Both mic gain set to minimal
    AUDIO_PLAY_VOICE_REC  // Rec mode, BASE will stream audio from hmic and this app
} audio_play_mode_t;

void audio_init();
void audio_mixer_setup(x6100_base_ver_t base_ver);
int audio_play(int16_t *buf, size_t samples);
void audio_play_wait();
void audio_set_play_mode(audio_play_mode_t mode);

void audio_gain_db(int16_t *buf, size_t samples, float gain, int16_t *out);
void audio_gain_db_transition(int16_t *buf, size_t samples, float gain1, float gain2, int16_t *out);

float audio_set_play_vol(float db);
float audio_set_rec_vol(float db);

float audio_get_peak_db();

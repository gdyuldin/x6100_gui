/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */
#define _GNU_SOURCE

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <math.h>

#include <pulse/pulseaudio.h>
#include <alsa/asoundlib.h>
#include <alsa/mixer.h>

#include "lvgl/lvgl.h"
#include "audio.h"
#include "meter.h"
#include "dsp.h"
#include "params/params.h"

#define AUDIO_RATE_MS   100

static pa_threaded_mainloop *mloop;
static pa_mainloop_api      *mlapi;
static pa_context           *ctx;

static pa_stream            *play_stm;
static char                 *play_device = "alsa_output.platform-sound.stereo-fallback";

static pa_stream            *capture_stm;
static char                 *capture_device = "alsa_input.platform-sound.stereo-fallback";

static pa_stream            *monitor_stm = NULL;

static float                peak_db = -60.0f;

static void record_monitor_setup();

static void on_state_change(pa_context *c, void *userdata) {
    pa_threaded_mainloop_signal(mloop, 0);
}

static void read_callback(pa_stream *s, size_t nbytes, void *udata) {
    int16_t *buf = NULL;

    pa_stream_peek(s, (const void**) &buf, &nbytes);
    dsp_put_audio_samples(nbytes / 2, buf);
    pa_stream_drop(s);
}

static void mixer_setup() {
    int res;
    // overall level
    res = system("amixer sset 'Headphone',0 58,58");
    // Play level from app to radio (for FT8)
    res = system("amixer sset 'AIF1 DA0',0 160,160");

    // capture audio from radio
    res = system("amixer sset 'Mic1',0 0,0 cap");
    // mic boost
    res = system("amixer sset 'Mic1 Boost',0 1");
    // disable capturing from mixer
    res = system("amixer sset 'Mixer',0 nocap");
    res = system("amixer sset 'ADC Gain',0 3");
    res = system("amixer sset 'AIF1 AD0',0 160,160");
    res = system("amixer sset 'AIF1 AD0 Stereo',0 'Mix Mono'");
    res = system("amixer sset 'AIF1 Data Digital ADC',0 cap");

    audio_set_rec_vol(0.0f);
    audio_set_play_vol(0.0f);
}

void audio_init() {
    mixer_setup();
    mloop = pa_threaded_mainloop_new();
    pa_threaded_mainloop_start(mloop);

    mlapi = pa_threaded_mainloop_get_api(mloop);
    ctx = pa_context_new(mlapi, "X6100 GUI");

    pa_threaded_mainloop_lock(mloop);
    pa_context_set_state_callback(ctx, on_state_change, NULL);
    pa_context_connect(ctx, NULL, 0, NULL);
    pa_threaded_mainloop_unlock(mloop);

    while (PA_CONTEXT_READY != pa_context_get_state(ctx))  {
        pa_threaded_mainloop_wait(mloop);
    }

    LV_LOG_USER("Conected");

    pa_buffer_attr  attr;

    pa_sample_spec  spec = {
        .format = PA_SAMPLE_S16NE,
        .channels = 1
    };

    memset(&attr, 0xff, sizeof(attr));

    /* Play */

    spec.rate = AUDIO_PLAY_RATE,
    attr.fragsize = pa_usec_to_bytes(AUDIO_RATE_MS * PA_USEC_PER_MSEC, &spec);
    attr.tlength = attr.fragsize * 8;

    play_stm = pa_stream_new(ctx, "X6100 GUI Play", &spec, NULL);

    pa_threaded_mainloop_lock(mloop);
    pa_stream_connect_playback(play_stm, play_device, &attr, PA_STREAM_ADJUST_LATENCY, NULL, NULL);
    pa_threaded_mainloop_unlock(mloop);

    /* Capture */

    spec.rate = AUDIO_CAPTURE_RATE,
    attr.fragsize = attr.tlength = pa_usec_to_bytes(AUDIO_RATE_MS * PA_USEC_PER_MSEC, &spec);

    capture_stm = pa_stream_new(ctx, "X6100 GUI Capture", &spec, NULL);

    pa_threaded_mainloop_lock(mloop);
    pa_stream_set_read_callback(capture_stm, read_callback, NULL);
    pa_stream_connect_record(capture_stm, capture_device, &attr, PA_STREAM_ADJUST_LATENCY);
    pa_threaded_mainloop_unlock(mloop);

    record_monitor_setup();
}

int audio_play(int16_t *samples_buf, size_t samples) {
    while (true) {
        size_t size;

        pa_threaded_mainloop_lock(mloop);
        size = pa_stream_writable_size(play_stm);
        pa_threaded_mainloop_unlock(mloop);

        if (size >= (samples * 2)) {
            break;
        }

        usleep(1000);
    }

    pa_threaded_mainloop_lock(mloop);
    int res = pa_stream_write(play_stm, samples_buf, samples * 2, NULL, 0, PA_SEEK_RELATIVE);
    pa_threaded_mainloop_unlock(mloop);

    if (res < 0) {
        LV_LOG_ERROR("pa_stream_write() failed: %s", pa_strerror(pa_context_errno(ctx)));
    }

    return res;
}

void audio_play_wait() {
    pa_operation *op;
    int r;

    pa_threaded_mainloop_lock(mloop);
    op = pa_stream_drain(play_stm, NULL, NULL);
    pa_threaded_mainloop_unlock(mloop);

    while (true) {
        pa_threaded_mainloop_lock(mloop);
        r = pa_operation_get_state(op);
        pa_threaded_mainloop_unlock(mloop);

        if (r == PA_OPERATION_DONE || r == PA_OPERATION_CANCELLED) {
            break;
        }

        usleep(1000);
    }

    pa_operation_unref(op);
}

void audio_gain_db(int16_t *buf, size_t samples, float gain, int16_t *out) {
    float scale = exp10f(gain / 20.0f);

    for (uint16_t i = 0; i < samples; i++) {
        int32_t x = buf[i] * scale;

        if (x > 32767) {
            x = 32767;
        }

        if (x < -32767) {
            x = -32767;
        }

        out[i] = x;
    }
}

void audio_gain_db_transition(int16_t *buf, size_t samples, float gain1, float gain2, int16_t *out) {
    float scale1 = exp10f(gain1 / 20.0f);
    float scale2 = exp10f(gain2 / 20.0f);
    float scale;
    for (uint16_t i = 0; i < samples; i++) {
        scale = scale1 + i * (scale2 - scale1) / samples;
        int32_t x = buf[i] * scale;

        if (x > 32767) {
            x = 32767;
        }

        if (x < -32767) {
            x = -32767;
        }

        out[i] = x;
    }
}

void audio_play_en(bool on) {
    if (on) {
        x6100_control_hmic_set(0);
        x6100_control_imic_set(0);
        x6100_control_record_set(true);
    } else {
        x6100_control_record_set(false);
        x6100_control_hmic_set(subject_get_int(cfg.hmic.val));
        x6100_control_imic_set(subject_get_int(cfg.imic.val));
    }
}

float audio_set_play_vol(float db) {
    snd_mixer_t *handle;
    snd_mixer_selem_id_t *sid;
    const char *card = "default";
    const char *selem_name = "AIF1 DA0";

    snd_mixer_open(&handle, 0);
    snd_mixer_attach(handle, card);
    snd_mixer_selem_register(handle, NULL, NULL);
    snd_mixer_load(handle);

    snd_mixer_selem_id_alloca(&sid);
    snd_mixer_selem_id_set_index(sid, 0);
    snd_mixer_selem_id_set_name(sid, selem_name);
    snd_mixer_elem_t* elem = snd_mixer_find_selem(handle, sid);

    snd_mixer_selem_set_playback_dB_all(elem, (long)(db * 100.0f), 0);
    long db_long;
    snd_mixer_selem_get_playback_dB(elem, SND_MIXER_SCHN_MONO, &db_long);
    snd_mixer_close(handle);
    return (float)db_long / 100.0f;
}

float audio_set_rec_vol(float db) {
    snd_mixer_t *handle;
    snd_mixer_selem_id_t *sid;
    const char *card = "default";
    const char *selem_name = "ADC";

    snd_mixer_open(&handle, 0);
    snd_mixer_attach(handle, card);
    snd_mixer_selem_register(handle, NULL, NULL);
    snd_mixer_load(handle);

    snd_mixer_selem_id_alloca(&sid);
    snd_mixer_selem_id_set_index(sid, 0);
    snd_mixer_selem_id_set_name(sid, selem_name);
    snd_mixer_elem_t* elem = snd_mixer_find_selem(handle, sid);

    snd_mixer_selem_set_capture_dB_all(elem, (long)(db * 100.0f), 0);
    long db_long;
    snd_mixer_selem_get_capture_dB(elem, SND_MIXER_SCHN_MONO, &db_long);
    snd_mixer_close(handle);
    return (float) db_long / 100.0f;
}

float audio_get_peak_db() {
    return peak_db;
}

static void monitor_cb(pa_stream *stream, size_t length, void *udata) {
    int16_t *buf = NULL;

    pa_stream_peek(stream, (const void**) &buf, &length);
    int16_t max_val = 1;
    int16_t cur_val;
    for (size_t i=0; i < length / 2; i++) {
        cur_val = buf[i];
        if (cur_val > max_val) {
            max_val = cur_val;
        }
    }
    peak_db = 20.0f * log10f((float) max_val / ((1UL << 15) - 1));
    pa_stream_drop(stream);
}


static void record_monitor_setup() {

    pa_sample_spec  spec = {
        .format = PA_SAMPLE_S16NE,
        .channels = 1
    };

    spec.rate = 30;

    monitor_stm = pa_stream_new(ctx, "X6100 GUI Monitor", &spec, NULL);

    pa_threaded_mainloop_lock(mloop);
    pa_stream_set_read_callback(monitor_stm, monitor_cb, NULL);
    pa_stream_connect_record(monitor_stm, capture_device, NULL, PA_STREAM_PEAK_DETECT);
    pa_threaded_mainloop_unlock(mloop);
}

/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <math.h>

#include <pulse/pulseaudio.h>

#include "lvgl/lvgl.h"
#include "audio.h"
#include "meter.h"
#include "dsp.h"
#include "params.h"

#define AUDIO_RATE_MS   100

static pa_threaded_mainloop *mloop;
static pa_mainloop_api      *mlapi;
static pa_context           *ctx;

static pa_stream            *play_stm;
static char                 *play_device = "alsa_output.platform-sound.stereo-fallback";

static pa_stream            *capture_stm;
static char                 *capture_device = "alsa_input.platform-sound.stereo-fallback";

static void on_state_change(pa_context *c, void *userdata) {
    pa_threaded_mainloop_signal(mloop, 0);
}

static void read_callback(pa_stream *s, size_t nbytes, void *udata) {
    int16_t *buf = NULL;

    pa_stream_peek(capture_stm, (const void**)&buf, &nbytes);
    dsp_put_audio_samples(nbytes / 2, buf);
    pa_stream_drop(capture_stm);
}

void audio_init() {
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
    
    LV_LOG_INFO("Conected");
    
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

int16_t* audio_gain(int16_t *buf, size_t samples, uint16_t gain) {
    int16_t *out_samples = malloc(samples * sizeof(int16_t));

    for (uint16_t i = 0; i < samples; i++) {
        int32_t x = buf[i] * gain / 100;
        
        if (x > 32767) {
            x = 32767;
        }
        
        if (x < -32767) {
            x = -32767;
        }
    
        out_samples[i] = x;
    }
    
    return out_samples;
}

void audio_play_en(bool on) {
    if (on) {
        x6100_control_hmic_set(0);
        x6100_control_imic_set(0);
        x6100_control_record_set(true);
    } else {
        x6100_control_record_set(false);
        x6100_control_hmic_set(params.hmic);
        x6100_control_imic_set(params.imic);
    }
}

/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <math.h>

#include <alsa/asoundlib.h>
#include <alsa/mixer.h>

#include "lvgl/lvgl.h"
#include "audio.h"
#include "meter.h"
#include "dsp.h"
#include "cfg/cfg_api.h"

#define AUDIO_RATE_MS   30

static pa_threaded_mainloop *mloop;
static pa_mainloop_api      *mlapi;
static pa_context           *ctx;

struct audio_player_s {
    pa_stream *stream;
    int is_paused;
};

static char      *default_play_device = "alsa_output.platform-sound.stereo-fallback";
static audio_player_t default_player;

static pa_stream            *capture_stm;
static char                 *capture_device = "alsa_input.platform-sound.stereo-fallback";

static pa_stream            *monitor_stm = NULL;

static float                peak_db = -60.0f;

static pa_stream *player_stream_create(uint32_t sample_rate, uint32_t ch, const char *name);
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

void audio_init() {
    audio_set_rec_vol(0.0f);
    audio_set_play_vol(0.0f);

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

    /* Default player */
    default_player.stream = player_stream_create(AUDIO_PLAY_RATE, 1, "X6100 GUI default player");
    if (!default_player.stream) {
        LV_LOG_ERROR("pa_stream_connect_playback() failed: %s", pa_strerror(pa_context_errno(ctx)));
    } else {
        // Pause
        pa_stream_cork(default_player.stream, 1, NULL, NULL);
        default_player.is_paused = true;
    }


    pa_buffer_attr  attr;

    pa_sample_spec  spec = {
        .format = PA_SAMPLE_S16NE,
        .channels = 1
    };

    memset(&attr, 0xff, sizeof(attr));
    int res;

    /* Capture */

    spec.rate = AUDIO_CAPTURE_RATE,
    attr.fragsize = attr.tlength = pa_usec_to_bytes(AUDIO_RATE_MS * PA_USEC_PER_MSEC, &spec);

    capture_stm = pa_stream_new(ctx, "X6100 GUI Capture", &spec, NULL);

    pa_threaded_mainloop_lock(mloop);
    pa_stream_set_read_callback(capture_stm, read_callback, NULL);
    res = pa_stream_connect_record(capture_stm, capture_device, &attr, PA_STREAM_ADJUST_LATENCY);
    if (res < 0) {
        LV_LOG_ERROR("pa_stream_connect_record() failed: %s", pa_strerror(pa_context_errno(ctx)));
    }
    pa_threaded_mainloop_unlock(mloop);

    record_monitor_setup();
}

void audio_mixer_setup(x6100_base_ver_t base_ver) {
    int res;
    // overall level
    res = system("amixer sset 'Headphone',0 63,63"); // max output level
    // Play level from app to radio (for FT8)
    res = system("amixer sset 'AIF1 DA0',0 160,160");

    // capture audio from radio
    res = system("amixer sset 'Mic1',0 0,0 cap");
    // mic boost
    if (base_ver.rev >= 12) {
        // no need for mic boost on r12 and later
        res = system("amixer sset 'Mic1 Boost',0 0");
    } else {
        res = system("amixer sset 'Mic1 Boost',0 1");
    }
    // disable capturing from mixer
    res = system("amixer sset 'Mixer',0 nocap");
    res = system("amixer sset 'ADC Gain',0 3");
    res = system("amixer sset 'AIF1 AD0',0 160,160");
    res = system("amixer sset 'AIF1 AD0 Stereo',0 'Mix Mono'"); // Another option - Sum Mono
    res = system("amixer sset 'AIF1 Data Digital ADC',0 cap");
}

int audio_play(int16_t *samples_buf, size_t samples) {
    audio_player_send(&default_player, samples_buf, samples);
}

void audio_play_wait() {
    audio_player_wait(&default_player);
}

static void stream_write_callback(pa_stream *stream, size_t length, void *userdata) {
    pa_threaded_mainloop_signal(mloop, 0);
}

static pa_stream *player_stream_create(uint32_t sample_rate, uint32_t ch, const char *name) {
    pa_buffer_attr  attr;

    pa_sample_spec  spec = {
        .format = PA_SAMPLE_S16NE,
        .channels = ch
    };

    memset(&attr, 0xff, sizeof(attr));
    int res;

    spec.rate = sample_rate,
    attr.fragsize = pa_usec_to_bytes(AUDIO_RATE_MS * PA_USEC_PER_MSEC, &spec);
    attr.tlength = attr.fragsize * 8;

    pa_stream *stream = pa_stream_new(ctx, name, &spec, NULL);

    pa_threaded_mainloop_lock(mloop);
    res = pa_stream_connect_playback(stream, default_play_device, &attr, PA_STREAM_ADJUST_LATENCY, NULL, NULL);
    if (res < 0) {
        return NULL;
    }
    pa_stream_set_write_callback(stream, stream_write_callback, NULL);
    pa_threaded_mainloop_unlock(mloop);
    return stream;
}

audio_player_t *audio_create_player(uint32_t sample_rate, uint32_t ch) {
    audio_player_t *player = malloc(sizeof(audio_player_t));
    if (!player) return NULL;
    player->stream = player_stream_create(sample_rate, ch, "X6100 GUI player");
    if (!player->stream) {
        LV_LOG_ERROR("pa_stream_connect_playback() failed: %s", pa_strerror(pa_context_errno(ctx)));
        return NULL;
    }

    // Pause
    pa_threaded_mainloop_lock(mloop);
    pa_stream_cork(player->stream, 1, NULL, NULL);
    pa_threaded_mainloop_unlock(mloop);
    player->is_paused = true;
    return player;
}

audio_player_t *audio_get_player(uint32_t sample_rate, uint32_t ch) {
    if ((sample_rate == AUDIO_PLAY_RATE) && (ch == 1)) return &default_player;
    return audio_create_player(sample_rate, ch);
}

int audio_player_send(audio_player_t *player, int16_t *samples_buf, size_t samples) {
    if (!player) return -1;

    if (player->is_paused) {
        pa_threaded_mainloop_lock(mloop);
        pa_stream_cork(player->stream, 0, NULL, NULL);
        pa_threaded_mainloop_unlock(mloop);
        player->is_paused = false;
    }

    uint8_t *src_ptr = (uint8_t *)samples_buf;
    size_t bytes_left = samples * 2;

    int res = 0;
    while (bytes_left > 0) {
        pa_threaded_mainloop_lock(mloop);
        size_t writable_size = pa_stream_writable_size(player->stream);

        if (writable_size == 0) {
            // Wait for buffer
            pa_threaded_mainloop_wait(mloop);
            pa_threaded_mainloop_unlock(mloop);
            continue;
        }
        size_t chunk_size = (bytes_left < writable_size) ? bytes_left : writable_size;

        res = pa_stream_write(player->stream, src_ptr, chunk_size, NULL, 0, PA_SEEK_RELATIVE);
        pa_threaded_mainloop_unlock(mloop);

        if (res < 0) {
            LV_LOG_ERROR("pa_stream_write() failed: %s", pa_strerror(pa_context_errno(ctx)));
            break;
        }

        src_ptr += chunk_size;
        bytes_left -= chunk_size;
    }
    return res;
}

static void stream_drain_callback(pa_stream *s, int success, void *userdata) {
    pa_threaded_mainloop_signal(mloop, 0);
}

void audio_player_wait(audio_player_t *player) {
    if (!player) return;
    pa_operation *op;
    int r;
    pa_threaded_mainloop_lock(mloop);
    op = pa_stream_drain(player->stream, stream_drain_callback, NULL);

    if (op) {
        while (pa_operation_get_state(op) == PA_OPERATION_RUNNING) {
            pa_threaded_mainloop_wait(mloop);
        }
        pa_operation_unref(op);
    }
    if (!player->is_paused) {
        pa_stream_cork(player->stream, 1, NULL, NULL);
        player->is_paused = true;
    }

    pa_threaded_mainloop_unlock(mloop);
}

void audio_player_release(audio_player_t *player) {
    if (!player) return;

    if (player == &default_player) return;

    pa_stream_disconnect(player->stream);

    pa_stream_set_write_callback(player->stream, NULL, NULL);
    pa_stream_set_read_callback(player->stream, NULL, NULL);
    pa_stream_set_state_callback(player->stream, NULL, NULL);

    pa_stream_unref(player->stream);

    free(player);
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

void audio_set_play_mode(audio_play_mode_t mode) {
    switch (mode)
    {
    case AUDIO_PLAY_OFF:
        x6100_control_record_set(false);
        x6100_control_hmic_set(param_i_get(cfg.hmic()));
        x6100_control_imic_set(param_i_get(cfg.imic()));
        break;

    case AUDIO_PLAY_ON:
        x6100_control_hmic_set(0);
    case AUDIO_PLAY_VOICE_REC:
        x6100_control_imic_set(0);
        x6100_control_record_set(true);
        break;

    default:
        break;
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

static void monitor_cb(pa_stream *s, size_t length, void *userdata) {
    const void *data;

    if (pa_stream_peek(s, &data, &length) < 0) {
        return;
    }

    if (!data || length < sizeof(float)) {
        /* No data available (can happen when the stream is corked) */
        pa_stream_drop(s);
        return;
    }
    float peak = *(const float *)data;
    pa_stream_drop(s);

    if (peak < 0.0)
        peak = 0.0;
    if (peak > 1.0)
        peak = 1.0;

    peak_db = 20.0f * log10f(peak);
    // printf("peak %f\n", peak_db);
}

static void record_monitor_setup() {

    pa_sample_spec spec = {
        .format   = PA_SAMPLE_FLOAT32,
        .channels = 1,
        .rate     = AUDIO_RATE_MS,
    };

    pa_buffer_attr attr;

    memset(&attr, 0, sizeof(attr));
    attr.fragsize  = sizeof(float); /* request one peak value per fragment */
    attr.maxlength = (uint32_t)-1;

    monitor_stm = pa_stream_new(ctx, "X6100 GUI Monitor", &spec, NULL);

    pa_threaded_mainloop_lock(mloop);
    pa_stream_set_read_callback(monitor_stm, monitor_cb, NULL);
    pa_stream_connect_record(monitor_stm, capture_device, &attr,
                             PA_STREAM_DONT_MOVE | PA_STREAM_PEAK_DETECT | PA_STREAM_ADJUST_LATENCY);
    pa_threaded_mainloop_unlock(mloop);
}

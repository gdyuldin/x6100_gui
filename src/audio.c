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
#include "bt.h"
#include "meter.h"
#include "dsp.h"
#include "scheduler.h"
#include "cfg/cfg_api.h"

#define AUDIO_RATE_MS   30

#define PA_PROFILE_A2DP "a2dp_sink"
#define PA_PROFILE_HFP "handsfree_head_unit"
#define PA_PROFILE_OFF "off"

/* A2DP transport Acquire() can fail with EIO (bluez drops the device's A2DP
 * endpoints); recover by reconnecting the device, then retrying the profile. */
#define BT_RECOVER_TICK_MS        500
#define BT_RECOVER_DISCONNECT_MS  2000
#define BT_RECOVER_CONNECT_MS     8000
#define BT_RECOVER_MAX            2

#define PA_LOOPBACK_MODULE "module-loopback"
#define PA_LOOPBACK_ARGS "source=%s sink=%s latency_msec=40 adjust_time=10 source_dont_move=true sink_dont_move=true"
#define PA_BT_SINK_PREFIX "bluez_sink."
#define PA_BT_SOURCE_PREFIX "bluez_source."
#define BT_LOOPBACK_MAX 8

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

typedef struct {
    char               card_name[80];
    bool               card_known;
    bool               has_a2dp;
    bool               has_hfp;
    audio_bt_profile_t active;
    bool               switching;
    bool               via_off;
    audio_bt_profile_t target;
    bool               recover;
    int                recover_attempts;
} bt_profile_state_t;

static bt_profile_state_t bt_prof;
static pthread_mutex_t    bt_prof_mutex = PTHREAD_MUTEX_INITIALIZER;

typedef enum {
    BT_RECOVER_IDLE = 0,
    BT_RECOVER_WAIT_DISCONNECT,
    BT_RECOVER_WAIT_CONNECT,
} bt_recover_state_t;

static bt_recover_state_t bt_recover_state = BT_RECOVER_IDLE;
static char               bt_recover_bdaddr[18];
static uint32_t           bt_recover_ticks = 0;

static bool card_refresh_pending = false;
static bool card_query_inflight  = false;
static bool card_list_found      = false;
static bool card_subscribed      = false;

typedef enum {
    LOOPBACK_NONE = 0,
    LOOPBACK_SINK,
    LOOPBACK_SOURCE,
} loopback_kind_t;

typedef struct {
    loopback_kind_t kind;
    uint32_t        dev_idx;
    uint32_t        module_idx;
    bool            pending;
    bool            cancelled;
    char            dev_name[256];
} bt_loopback_t;

/* PA thread only (all access under the mainloop lock) */
static bt_loopback_t bt_loopback[BT_LOOPBACK_MAX];

/* BT sink state cache, read from the UI thread */
static pthread_mutex_t bt_sink_mutex    = PTHREAD_MUTEX_INITIALIZER;
static bool            bt_sink_active   = false;
static char            bt_sink_name[256];
static int             bt_sink_volume   = -1; /* 0..AUDIO_BT_SINK_VOLUME_MAX */
static bool            bt_sink_mute     = false;
static uint8_t         bt_sink_channels = 2;

/* "Listening" playback tracking (media/TTS). */
static pthread_mutex_t playback_mutex    = PTHREAD_MUTEX_INITIALIZER;
static int             playback_refcount = 0;
/* PA thread only (mainloop lock held): requested loopback pause state. */
static bool loopback_pause_requested = false;

static pa_stream *player_stream_create(uint32_t sample_rate, uint32_t ch, const char *name, const char *device);
static void record_monitor_setup();

static void card_list_cb(pa_context *c, const pa_card_info *info, int eol, void *userdata);
static void card_subscribe_cb(pa_context *c, pa_subscription_event_type_t t, uint32_t idx, void *userdata);
static void set_profile_cb(pa_context *c, int success, void *userdata);
static void set_profile_off_cb(pa_context *c, int success, void *userdata);
static void bt_recover_timer_cb(lv_timer_t *t);
static void profile_retry_a2dp_now(void);
static void card_name_to_bdaddr(const char *card_name, char *out, size_t sz);
static void audio_bt_refresh_locked();

static void loopback_device_event(loopback_kind_t kind, unsigned type, uint32_t idx);
static void loopback_sink_info_cb(pa_context *c, const pa_sink_info *info, int eol, void *userdata);
static void loopback_source_info_cb(pa_context *c, const pa_source_info *info, int eol, void *userdata);
static void loopback_loaded_cb(pa_context *c, uint32_t idx, void *userdata);
static void loopback_unload_all(void);
static void bt_sink_state_refresh_locked(void);
static void sink_volume_info_cb(pa_context *c, const pa_sink_info *info, int eol, void *userdata);
static void bt_sink_cache_update(const pa_sink_info *info);
static void loopback_apply_pause_locked(void);
static void loopback_sink_input_info_cb(pa_context *c, const pa_sink_input_info *info, int eol, void *userdata);

static void on_state_change(pa_context *c, void *userdata) {
    pa_context_state_t state = pa_context_get_state(c);

    LV_LOG_USER("BT PA context state=%d", state);

    if (state == PA_CONTEXT_READY) {
        if (!card_subscribed) {
            card_subscribed = true;
            pa_context_set_subscribe_callback(c, card_subscribe_cb, NULL);
            pa_operation *op = pa_context_subscribe(
                c, PA_SUBSCRIPTION_MASK_CARD | PA_SUBSCRIPTION_MASK_SINK | PA_SUBSCRIPTION_MASK_SOURCE, NULL, NULL);
            if (op) {
                pa_operation_unref(op);
            }
            audio_bt_refresh_locked();
        }
    } else if (state == PA_CONTEXT_FAILED || state == PA_CONTEXT_TERMINATED) {
        card_subscribed = false;
        pthread_mutex_lock(&bt_prof_mutex);
        bt_prof.switching = false;
        bt_prof.via_off   = false;
        pthread_mutex_unlock(&bt_prof_mutex);
        memset(bt_loopback, 0, sizeof(bt_loopback));
        bt_sink_state_refresh_locked();
    }

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
    default_player.stream = player_stream_create(AUDIO_PLAY_RATE, 1, "X6100 GUI default player", default_play_device);
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

    lv_timer_create(bt_recover_timer_cb, BT_RECOVER_TICK_MS, NULL);
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

static pa_stream *player_stream_create(uint32_t sample_rate, uint32_t ch, const char *name, const char *device) {
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
    res = pa_stream_connect_playback(stream, device, &attr, PA_STREAM_ADJUST_LATENCY, NULL, NULL);
    if (res < 0) {
        return NULL;
    }
    pa_stream_set_write_callback(stream, stream_write_callback, NULL);
    pa_threaded_mainloop_unlock(mloop);
    return stream;
}

static audio_player_t *audio_create_player_on(const char *device, uint32_t sample_rate, uint32_t ch) {
    audio_player_t *player = malloc(sizeof(audio_player_t));
    if (!player) return NULL;
    player->stream = player_stream_create(sample_rate, ch, "X6100 GUI player", device);
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

audio_player_t *audio_create_player(uint32_t sample_rate, uint32_t ch) {
    return audio_create_player_on(default_play_device, sample_rate, ch);
}

audio_player_t *audio_get_player(uint32_t sample_rate, uint32_t ch) {
    if ((sample_rate == AUDIO_PLAY_RATE) && (ch == 1)) return &default_player;
    return audio_create_player(sample_rate, ch);
}

static bool bt_sink_name_copy(char *out, size_t sz) {
    bool ok = false;

    pthread_mutex_lock(&bt_sink_mutex);
    if (bt_sink_active && bt_sink_name[0] != '\0') {
        snprintf(out, sz, "%s", bt_sink_name);
        ok = true;
    }
    pthread_mutex_unlock(&bt_sink_mutex);
    return ok;
}

const char *audio_playback_bt_sink(void) {
    pthread_mutex_lock(&bt_sink_mutex);
    const char *name = (bt_sink_active && bt_sink_name[0] != '\0') ? bt_sink_name : NULL;
    pthread_mutex_unlock(&bt_sink_mutex);
    return name;
}

audio_player_t *audio_playback_get_player(audio_playback_t route, uint32_t rate, uint32_t ch) {
    if (route == AUDIO_PLAYBACK_BT) {
        char device[256];

        if (bt_sink_name_copy(device, sizeof(device))) {
            audio_player_t *player = audio_create_player_on(device, rate, ch);
            if (player) {
                return player;
            }
        }
    }
    return audio_get_player(rate, ch);
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

/* Bluetooth card profile */

static const char *profile_available_text(int available) {
    switch (available) {
    case PA_PORT_AVAILABLE_YES:
        return "yes";
    case PA_PORT_AVAILABLE_NO:
        return "no";
    default:
        return "unknown";
    }
}

static void scan_card_profiles(const pa_card_info *info, bool *has_a2dp, bool *has_hfp, audio_bt_profile_t *active) {
    *has_a2dp = false;
    *has_hfp  = false;
    *active   = AUDIO_BT_PROFILE_NONE;

    for (uint32_t i = 0; i < info->n_profiles; i++) {
        const char *name      = NULL;
        int         available = PA_PORT_AVAILABLE_UNKNOWN;

        if (info->profiles2 && info->profiles2[i]) {
            name      = info->profiles2[i]->name;
            available = info->profiles2[i]->available;
        } else if (info->profiles) {
            name = info->profiles[i].name;
        }
        if (!name) {
            continue;
        }
        LV_LOG_USER("BT card %s profile[%u] name=%s available=%s", info->name, i, name,
                    profile_available_text(available));
        if (strcmp(name, PA_PROFILE_A2DP) == 0) {
            *has_a2dp = true;
        }
        if (strcmp(name, PA_PROFILE_HFP) == 0) {
            *has_hfp = true;
        }
    }

    const char *active_name = NULL;
    if (info->active_profile2) {
        active_name = info->active_profile2->name;
    } else if (info->active_profile) {
        active_name = info->active_profile->name;
    }
    if (active_name) {
        if (strcmp(active_name, PA_PROFILE_A2DP) == 0) {
            *active = AUDIO_BT_PROFILE_A2DP;
        } else if (strcmp(active_name, PA_PROFILE_HFP) == 0) {
            *active = AUDIO_BT_PROFILE_HFP;
        }
    }
    LV_LOG_USER("BT card %s active_profile=%s has_a2dp=%d has_hfp=%d", info->name,
                active_name ? active_name : "(none)", *has_a2dp, *has_hfp);
}

static void card_cache_clear() {
    pthread_mutex_lock(&bt_prof_mutex);
    bool was_known  = bt_prof.card_known;
    bool was_a2dp   = bt_prof.has_a2dp;
    bool was_hfp    = bt_prof.has_hfp;
    int  was_active = bt_prof.active;
    bt_prof.card_known = false;
    bt_prof.has_a2dp   = false;
    bt_prof.has_hfp    = false;
    bt_prof.active     = AUDIO_BT_PROFILE_NONE;
    pthread_mutex_unlock(&bt_prof_mutex);
    LV_LOG_USER("BT card cache clear (was known=%d a2dp=%d hfp=%d active=%d)", was_known, was_a2dp, was_hfp,
                was_active);
}

static void card_list_cb(pa_context *c, const pa_card_info *info, int eol, void *userdata) {
    if (eol < 0) {
        LV_LOG_WARN("BT card list failed: %s", pa_strerror(pa_context_errno(c)));
        card_cache_clear();
        card_query_inflight = false;
        return;
    }

    if (eol > 0) {
        LV_LOG_USER("BT card list end: found=%d pending=%d", card_list_found, card_refresh_pending);
        if (!card_list_found) {
            card_cache_clear();
        }
        card_query_inflight = false;
        if (card_refresh_pending) {
            audio_bt_refresh_locked();
        }
        return;
    }

    if (card_list_found || !info || strncmp(info->name, "bluez_card.", 11) != 0) {
        return;
    }

    LV_LOG_USER("BT bluez card found: name=%s", info->name);

    bool               has_a2dp;
    bool               has_hfp;
    audio_bt_profile_t active;

    scan_card_profiles(info, &has_a2dp, &has_hfp, &active);
    if (!has_a2dp || !has_hfp) {
        LV_LOG_USER("BT card %s skipped: has_a2dp=%d has_hfp=%d", info->name, has_a2dp, has_hfp);
        return;
    }

    pthread_mutex_lock(&bt_prof_mutex);
    snprintf(bt_prof.card_name, sizeof(bt_prof.card_name), "%s", info->name);
    bt_prof.card_known = true;
    bt_prof.has_a2dp   = has_a2dp;
    bt_prof.has_hfp    = has_hfp;
    bt_prof.active     = active;
    pthread_mutex_unlock(&bt_prof_mutex);
    card_list_found = true;
    LV_LOG_USER("BT card %s accepted: active=%d", info->name, active);
}

static void card_subscribe_cb(pa_context *c, pa_subscription_event_type_t t, uint32_t idx, void *userdata) {
    unsigned facility = t & PA_SUBSCRIPTION_EVENT_FACILITY_MASK;
    unsigned type     = t & PA_SUBSCRIPTION_EVENT_TYPE_MASK;

    LV_LOG_USER("BT PA event: facility=%u type=%u idx=%u", facility, type, idx);

    if (facility == PA_SUBSCRIPTION_EVENT_CARD) {
        audio_bt_refresh_locked();
    } else if (facility == PA_SUBSCRIPTION_EVENT_SINK) {
        loopback_device_event(LOOPBACK_SINK, type, idx);
    } else if (facility == PA_SUBSCRIPTION_EVENT_SOURCE) {
        loopback_device_event(LOOPBACK_SOURCE, type, idx);
    }
}

static void audio_bt_refresh_locked() {
    if (!ctx) {
        return;
    }
    if (card_query_inflight) {
        LV_LOG_USER("BT card refresh deferred (query inflight)");
        card_refresh_pending = true;
        return;
    }

    card_query_inflight  = true;
    card_refresh_pending = false;
    card_list_found      = false;

    pa_operation *op = pa_context_get_card_info_list(ctx, card_list_cb, NULL);
    if (op) {
        pa_operation_unref(op);
    } else {
        LV_LOG_ERROR("BT get_card_info_list failed: %s", pa_strerror(pa_context_errno(ctx)));
        card_query_inflight = false;
    }
}

static void set_profile_cb(pa_context *c, int success, void *userdata) {
    audio_bt_profile_t target = (audio_bt_profile_t)(uintptr_t)userdata;
    pthread_mutex_lock(&bt_prof_mutex);
    if (success) {
        bt_prof.active           = target;
        bt_prof.recover          = false;
        bt_prof.recover_attempts = 0;
    } else if (target == AUDIO_BT_PROFILE_A2DP) {
        bt_prof.recover = true;
    }
    bt_prof.switching = false;
    bt_prof.via_off   = false;
    pthread_mutex_unlock(&bt_prof_mutex);
    if (success) {
        LV_LOG_USER("BT set profile ok: target=%d", target);
    } else {
        LV_LOG_ERROR("BT set profile failed: target=%d error=%s", target, pa_strerror(pa_context_errno(c)));
    }
}

static void set_profile_a2dp_step(pa_context *c) {
    char               card[80];
    audio_bt_profile_t target;

    pthread_mutex_lock(&bt_prof_mutex);
    if (!bt_prof.switching || !bt_prof.via_off) {
        pthread_mutex_unlock(&bt_prof_mutex);
        return;
    }
    snprintf(card, sizeof(card), "%s", bt_prof.card_name);
    target = bt_prof.target;
    pthread_mutex_unlock(&bt_prof_mutex);

    LV_LOG_USER("BT switching to a2dp after off: card=%s target=%d", card, target);

    pa_operation *op =
        pa_context_set_card_profile_by_name(c, card, PA_PROFILE_A2DP, set_profile_cb, (void *)(uintptr_t)target);
    if (op) {
        pa_operation_unref(op);
    } else {
        LV_LOG_ERROR("BT set_card_profile (a2dp after off) op rejected: card=%s error=%s", card,
                     pa_strerror(pa_context_errno(c)));
        pthread_mutex_lock(&bt_prof_mutex);
        bt_prof.switching = false;
        bt_prof.via_off   = false;
        pthread_mutex_unlock(&bt_prof_mutex);
    }
}

static void set_profile_off_cb(pa_context *c, int success, void *userdata) {
    LV_LOG_USER("BT set profile off step done: success=%d, switching to a2dp", success);
    set_profile_a2dp_step(c);
}

static void card_name_to_bdaddr(const char *card_name, char *out, size_t sz) {
    const char *p = card_name;
    if (strncmp(p, "bluez_card.", 11) == 0) {
        p += 11;
    }
    size_t i = 0;
    while (*p && i + 1 < sz) {
        out[i++] = (*p == '_') ? ':' : *p;
        p++;
    }
    out[i] = '\0';
}

/* Force the card back to a2dp_sink after a reconnect (UI thread). */
static void profile_retry_a2dp_now(void) {
    char card[80];

    if (!ctx || !mloop) {
        return;
    }
    pthread_mutex_lock(&bt_prof_mutex);
    if (!bt_prof.card_known || bt_prof.switching) {
        pthread_mutex_unlock(&bt_prof_mutex);
        return;
    }
    snprintf(card, sizeof(card), "%s", bt_prof.card_name);
    bt_prof.switching = true;
    bt_prof.via_off   = false;
    bt_prof.target    = AUDIO_BT_PROFILE_A2DP;
    pthread_mutex_unlock(&bt_prof_mutex);

    pa_threaded_mainloop_lock(mloop);
    pa_operation *op = pa_context_set_card_profile_by_name(ctx, card, PA_PROFILE_A2DP, set_profile_cb,
                                                           (void *)(uintptr_t)AUDIO_BT_PROFILE_A2DP);
    if (op) {
        pa_operation_unref(op);
    } else {
        LV_LOG_ERROR("BT set_card_profile (recover) op rejected: card=%s error=%s", card,
                     pa_strerror(pa_context_errno(ctx)));
        pthread_mutex_lock(&bt_prof_mutex);
        bt_prof.switching = false;
        pthread_mutex_unlock(&bt_prof_mutex);
    }
    pa_threaded_mainloop_unlock(mloop);
}

/* Runs on the UI thread (LVGL timer): reconnect the device after a failed A2DP
 * switch, then retry the profile. bt_* must only be called from this thread. */
static void bt_recover_timer_cb(lv_timer_t *t) {
    switch (bt_recover_state) {
    case BT_RECOVER_IDLE: {
        char bdaddr[18] = "";

        if (bt_is_busy()) {
            return;
        }
        pthread_mutex_lock(&bt_prof_mutex);
        if (!bt_prof.recover || !bt_prof.card_known) {
            pthread_mutex_unlock(&bt_prof_mutex);
            return;
        }
        if (bt_prof.recover_attempts >= BT_RECOVER_MAX) {
            LV_LOG_WARN("BT profile recover: giving up after %d attempts", bt_prof.recover_attempts);
            bt_prof.recover = false;
            pthread_mutex_unlock(&bt_prof_mutex);
            return;
        }
        bt_prof.recover = false;
        bt_prof.recover_attempts++;
        card_name_to_bdaddr(bt_prof.card_name, bdaddr, sizeof(bdaddr));
        int attempt = bt_prof.recover_attempts;
        pthread_mutex_unlock(&bt_prof_mutex);

        if (bdaddr[0] == '\0') {
            return;
        }
        snprintf(bt_recover_bdaddr, sizeof(bt_recover_bdaddr), "%s", bdaddr);
        LV_LOG_USER("BT profile recover: disconnect %s (attempt %d)", bt_recover_bdaddr, attempt);
        bt_disconnect(bt_recover_bdaddr);
        bt_recover_state = BT_RECOVER_WAIT_DISCONNECT;
        bt_recover_ticks = 0;
        return;
    }

    case BT_RECOVER_WAIT_DISCONNECT:
        if (++bt_recover_ticks * BT_RECOVER_TICK_MS < BT_RECOVER_DISCONNECT_MS) {
            return;
        }
        if (bt_is_busy()) {
            return;
        }
        LV_LOG_USER("BT profile recover: connect %s", bt_recover_bdaddr);
        bt_connect(bt_recover_bdaddr);
        bt_recover_state = BT_RECOVER_WAIT_CONNECT;
        bt_recover_ticks = 0;
        return;

    case BT_RECOVER_WAIT_CONNECT:
        bt_recover_ticks++;
        if ((!bt_is_busy() && bt_get_status() == BT_CONNECTED) ||
            bt_recover_ticks * BT_RECOVER_TICK_MS >= BT_RECOVER_CONNECT_MS) {
            LV_LOG_USER("BT profile recover: retry a2dp");
            profile_retry_a2dp_now();
            bt_recover_state = BT_RECOVER_IDLE;
        }
        return;
    }
}

bool audio_bt_profile_supported() {
    pthread_mutex_lock(&bt_prof_mutex);
    bool supported = bt_prof.card_known && bt_prof.has_a2dp && bt_prof.has_hfp;
    pthread_mutex_unlock(&bt_prof_mutex);
    return supported;
}

bool audio_bt_profile_switching() {
    pthread_mutex_lock(&bt_prof_mutex);
    bool switching = bt_prof.switching;
    pthread_mutex_unlock(&bt_prof_mutex);
    return switching;
}

audio_bt_profile_t audio_bt_profile_get() {
    pthread_mutex_lock(&bt_prof_mutex);
    audio_bt_profile_t active = bt_prof.active;
    pthread_mutex_unlock(&bt_prof_mutex);
    return active;
}

const char *audio_bt_profile_text() {
    switch (audio_bt_profile_get()) {
    case AUDIO_BT_PROFILE_A2DP:
        return "A2DP";
    case AUDIO_BT_PROFILE_HFP:
        return "HSP/HFP";
    default:
        return "";
    }
}

void audio_bt_profile_toggle() {
    if (!ctx || !mloop) {
        LV_LOG_WARN("BT profile toggle ignored: ctx=%p mloop=%p", (void *)ctx, (void *)mloop);
        return;
    }

    char               card[80];
    const char        *target;
    audio_bt_profile_t target_id;
    bool               via_off;

    pthread_mutex_lock(&bt_prof_mutex);
    if (!bt_prof.card_known || !bt_prof.has_a2dp || !bt_prof.has_hfp || bt_prof.switching) {
        LV_LOG_WARN("BT profile toggle blocked: known=%d a2dp=%d hfp=%d switching=%d active=%d", bt_prof.card_known,
                    bt_prof.has_a2dp, bt_prof.has_hfp, bt_prof.switching, bt_prof.active);
        pthread_mutex_unlock(&bt_prof_mutex);
        return;
    }
    snprintf(card, sizeof(card), "%s", bt_prof.card_name);
    bt_prof.recover          = false;
    bt_prof.recover_attempts = 0;
    if (bt_prof.active == AUDIO_BT_PROFILE_A2DP) {
        target    = PA_PROFILE_HFP;
        target_id = AUDIO_BT_PROFILE_HFP;
        via_off   = false;
    } else {
        target    = PA_PROFILE_A2DP;
        target_id = AUDIO_BT_PROFILE_A2DP;
        /* Going from HFP to A2DP requires an explicit "off" step, otherwise
         * bluez leaves the card in the off profile and A2DP cannot be activated. */
        via_off   = (bt_prof.active == AUDIO_BT_PROFILE_HFP);
    }
    bt_prof.switching = true;
    bt_prof.via_off   = via_off;
    bt_prof.target    = target_id;
    pthread_mutex_unlock(&bt_prof_mutex);

    LV_LOG_USER("BT profile toggle: card=%s active->target=%s (id=%d) via_off=%d", card, target, target_id, via_off);

    pa_threaded_mainloop_lock(mloop);
    loopback_unload_all();
    pa_operation *op = pa_context_set_card_profile_by_name(
        ctx, card, via_off ? PA_PROFILE_OFF : target, via_off ? set_profile_off_cb : set_profile_cb,
        (void *)(uintptr_t)target_id);
    if (op) {
        pa_operation_unref(op);
    } else {
        LV_LOG_ERROR("BT set_card_profile op rejected: card=%s target=%s error=%s", card, target,
                     pa_strerror(pa_context_errno(ctx)));
        pthread_mutex_lock(&bt_prof_mutex);
        bt_prof.switching = false;
        bt_prof.via_off   = false;
        pthread_mutex_unlock(&bt_prof_mutex);
    }
    pa_threaded_mainloop_unlock(mloop);
}

/* Bluetooth loopback routing */

static bt_loopback_t *loopback_find(loopback_kind_t kind, uint32_t dev_idx) {
    for (int i = 0; i < BT_LOOPBACK_MAX; i++) {
        if (bt_loopback[i].kind == kind && bt_loopback[i].dev_idx == dev_idx) {
            return &bt_loopback[i];
        }
    }
    return NULL;
}

static bt_loopback_t *loopback_alloc(loopback_kind_t kind, uint32_t dev_idx, const char *name) {
    for (int i = 0; i < BT_LOOPBACK_MAX; i++) {
        if (bt_loopback[i].kind == LOOPBACK_NONE) {
            bt_loopback[i].kind    = kind;
            bt_loopback[i].dev_idx = dev_idx;
            bt_loopback[i].pending = true;
            snprintf(bt_loopback[i].dev_name, sizeof(bt_loopback[i].dev_name), "%s", name);
            return &bt_loopback[i];
        }
    }
    LV_LOG_WARN("loopback table is full");
    return NULL;
}

static void loopback_clear(bt_loopback_t *slot) {
    memset(slot, 0, sizeof(*slot));
}

/* Unload every loopback before a card profile switch: a loopback holding the
 * bluez sink/source blocks bluez from reconfiguring the card, which then falls
 * back to the "off" profile and A2DP cannot be activated. Must be called with
 * the mainloop lock held; PA executes the unload before the following switch. */
static void loopback_unload_all(void) {
    for (int i = 0; i < BT_LOOPBACK_MAX; i++) {
        bt_loopback_t *slot = &bt_loopback[i];
        if (slot->kind == LOOPBACK_NONE) {
            continue;
        }
        if (slot->pending) {
            slot->cancelled = true;
            LV_LOG_USER("BT loopback unload before profile switch: %s (pending)", slot->dev_name);
            continue;
        }
        pa_operation *op = pa_context_unload_module(ctx, slot->module_idx, NULL, NULL);
        if (op) {
            pa_operation_unref(op);
        }
        LV_LOG_USER("BT loopback unload before profile switch: %s (module=%u)", slot->dev_name, slot->module_idx);
        loopback_clear(slot);
    }
    bt_sink_state_refresh_locked();
}

static void loopback_load(loopback_kind_t kind, uint32_t dev_idx, const char *name, const char *source,
                          const char *sink) {
    bt_loopback_t *slot = loopback_alloc(kind, dev_idx, name);
    if (!slot) {
        return;
    }

    char args[512];
    snprintf(args, sizeof(args), PA_LOOPBACK_ARGS, source, sink);

    LV_LOG_USER("BT loopback load: kind=%d dev=%s args=%s", kind, name, args);

    pa_operation *op = pa_context_load_module(ctx, PA_LOOPBACK_MODULE, args, loopback_loaded_cb, slot);
    if (op) {
        pa_operation_unref(op);
    } else {
        LV_LOG_ERROR("pa_context_load_module() failed: %s", pa_strerror(pa_context_errno(ctx)));
        loopback_clear(slot);
    }
}

static int value_from_cvolume(const pa_cvolume *v) {
    pa_volume_t avg = pa_cvolume_avg(v);
    int         val = (int)(((uint64_t)avg * AUDIO_BT_SINK_VOLUME_MAX + PA_VOLUME_NORM / 2) / PA_VOLUME_NORM);

    if (val < 0) {
        val = 0;
    } else if (val > AUDIO_BT_SINK_VOLUME_MAX) {
        val = AUDIO_BT_SINK_VOLUME_MAX;
    }
    return val;
}

static void cvolume_from_value(int value, pa_cvolume *out) {
    uint8_t channels = bt_sink_channels ? bt_sink_channels : 2;

    pa_cvolume_set(out, channels, (pa_volume_t)((uint64_t)PA_VOLUME_NORM * value / AUDIO_BT_SINK_VOLUME_MAX));
}

/* PA thread: refresh the cached sink volume/mute and notify the UI thread about
 * external (AVRCP) changes. Our own optimistic sets already updated the cache,
 * so their PA echo compares equal here and produces no message. */
static void bt_sink_cache_update(const pa_sink_info *info) {
    pthread_mutex_lock(&bt_sink_mutex);
    int   old_volume = bt_sink_volume;
    bool  old_mute   = bt_sink_mute;
    int   new_volume = value_from_cvolume(&info->volume);
    bool  new_mute   = info->mute;

    bt_sink_channels = info->volume.channels;
    bt_sink_volume   = new_volume;
    bt_sink_mute     = new_mute;
    pthread_mutex_unlock(&bt_sink_mutex);

    if (old_volume < 0 || abs(new_volume - old_volume) > AUDIO_BT_SINK_VOLUME_TOLERANCE) {
        scheduler_msg_send(MSG_BT_SINK_VOLUME_CHANGED, (void *)(uintptr_t)new_volume);
    }
    if (new_mute != old_mute) {
        scheduler_msg_send(MSG_BT_SINK_MUTE_CHANGED, (void *)(uintptr_t)new_mute);
    }
}

/* PA thread, mainloop lock held: recompute the active sink from the loopback
 * table and notify the UI thread on an active<->inactive edge. */
static void bt_sink_state_refresh_locked(void) {
    const char *name = NULL;

    for (int i = 0; i < BT_LOOPBACK_MAX; i++) {
        bt_loopback_t *slot = &bt_loopback[i];
        if (slot->kind == LOOPBACK_SINK && !slot->pending && !slot->cancelled) {
            name = slot->dev_name;
            break;
        }
    }

    bool active = (name != NULL);

    pthread_mutex_lock(&bt_sink_mutex);
    bool changed = (bt_sink_active != active);
    bt_sink_active = active;
    if (active) {
        snprintf(bt_sink_name, sizeof(bt_sink_name), "%s", name);
    } else {
        bt_sink_name[0] = '\0';
        bt_sink_volume  = -1;
        bt_sink_mute    = false;
    }
    pthread_mutex_unlock(&bt_sink_mutex);

    if (changed) {
        scheduler_msg_send(MSG_BT_AUDIO_STATE_CHANGED, (void *)(uintptr_t)active);
    }
}

static void loopback_device_event(loopback_kind_t kind, unsigned type, uint32_t idx) {
    if (!ctx) {
        return;
    }

    if (type == PA_SUBSCRIPTION_EVENT_NEW) {
        if (loopback_find(kind, idx)) {
            return;
        }
        if (kind == LOOPBACK_SINK) {
            pa_context_get_sink_info_by_index(ctx, idx, loopback_sink_info_cb, NULL);
        } else {
            pa_context_get_source_info_by_index(ctx, idx, loopback_source_info_cb, NULL);
        }
    } else if (type == PA_SUBSCRIPTION_EVENT_REMOVE) {
        bt_loopback_t *slot = loopback_find(kind, idx);
        if (!slot) {
            return;
        }
        if (slot->pending) {
            slot->cancelled = true;
            return;
        }
        pa_operation *op = pa_context_unload_module(ctx, slot->module_idx, NULL, NULL);
        if (op) {
            pa_operation_unref(op);
        }
        loopback_clear(slot);
        bt_sink_state_refresh_locked();
    } else if (type == PA_SUBSCRIPTION_EVENT_CHANGE && kind == LOOPBACK_SINK) {
        if (loopback_find(kind, idx)) {
            pa_context_get_sink_info_by_index(ctx, idx, sink_volume_info_cb, NULL);
        }
    }
}

static void loopback_sink_info_cb(pa_context *c, const pa_sink_info *info, int eol, void *userdata) {
    if (eol != 0 || !info) {
        return;
    }
    if (strncmp(info->name, PA_BT_SINK_PREFIX, sizeof(PA_BT_SINK_PREFIX) - 1) != 0) {
        return;
    }

    pthread_mutex_lock(&bt_sink_mutex);
    snprintf(bt_sink_name, sizeof(bt_sink_name), "%s", info->name);
    pthread_mutex_unlock(&bt_sink_mutex);
    bt_sink_cache_update(info);

    loopback_load(LOOPBACK_SINK, info->index, info->name, capture_device, info->name);
}

/* One-shot from a CHANGE event: refresh the cache only, does not call
 * loopback_load (external volume/mute changes must not re-route audio). */
static void sink_volume_info_cb(pa_context *c, const pa_sink_info *info, int eol, void *userdata) {
    if (eol != 0 || !info) {
        return;
    }
    bt_sink_cache_update(info);
}

static void loopback_source_info_cb(pa_context *c, const pa_source_info *info, int eol, void *userdata) {
    if (eol != 0 || !info) {
        return;
    }
    if (strncmp(info->name, PA_BT_SOURCE_PREFIX, sizeof(PA_BT_SOURCE_PREFIX) - 1) != 0) {
        return;
    }
    loopback_load(LOOPBACK_SOURCE, info->index, info->name, info->name, default_play_device);
}

static void loopback_loaded_cb(pa_context *c, uint32_t idx, void *userdata) {
    bt_loopback_t *slot = userdata;

    if (idx == PA_INVALID_INDEX) {
        LV_LOG_ERROR("module-loopback load failed: %s", pa_strerror(pa_context_errno(ctx)));
        loopback_clear(slot);
        bt_sink_state_refresh_locked();
        return;
    }
    if (slot->cancelled) {
        LV_LOG_USER("BT loopback loaded after cancel, unloading: %s (module=%u)", slot->dev_name, idx);
        pa_operation *op = pa_context_unload_module(ctx, idx, NULL, NULL);
        if (op) {
            pa_operation_unref(op);
        }
        loopback_clear(slot);
        bt_sink_state_refresh_locked();
        return;
    }
    slot->module_idx = idx;
    slot->pending    = false;
    LV_LOG_USER("BT loopback loaded: %s (module=%u)", slot->dev_name, idx);
    bt_sink_state_refresh_locked();
    if (slot->kind == LOOPBACK_SINK && loopback_pause_requested) {
        loopback_apply_pause_locked();
    }
}

/* PA thread, mainloop lock held: mute/unmute the loaded sink loopback's
 * sink-input (owner_module == module index). The playback stream itself has
 * owner_module == PA_INVALID_INDEX and is skipped. */
static void loopback_sink_input_info_cb(pa_context *c, const pa_sink_input_info *info, int eol, void *userdata) {
    if (eol != 0 || !info) {
        return;
    }

    bool match = false;

    for (int i = 0; i < BT_LOOPBACK_MAX; i++) {
        bt_loopback_t *slot = &bt_loopback[i];
        if (slot->kind == LOOPBACK_SINK && !slot->pending && !slot->cancelled &&
            slot->module_idx == info->owner_module) {
            match = true;
            break;
        }
    }
    if (!match) {
        return;
    }

    pa_operation *op = pa_context_set_sink_input_mute(c, info->index, loopback_pause_requested ? 1 : 0, NULL, NULL);
    if (op) {
        pa_operation_unref(op);
    }
}

/* PA thread, mainloop lock held. */
static void loopback_apply_pause_locked(void) {
    if (!ctx) {
        return;
    }
    pa_operation *op = pa_context_get_sink_input_info_list(ctx, loopback_sink_input_info_cb, NULL);
    if (op) {
        pa_operation_unref(op);
    }
}

/* Pause/resume the sink loopback; acquires the mainloop lock. */
static void loopback_pause(bool mute) {
    if (!ctx || !mloop) {
        return;
    }
    pa_threaded_mainloop_lock(mloop);
    loopback_pause_requested = mute;
    loopback_apply_pause_locked();
    pa_threaded_mainloop_unlock(mloop);
}

audio_playback_t audio_playback_begin(void) {
    if (!audio_bt_audio_active()) {
        audio_set_play_mode(AUDIO_PLAY_ON);
        return AUDIO_PLAYBACK_LOCAL;
    }

    pthread_mutex_lock(&playback_mutex);
    bool first = (playback_refcount++ == 0);
    pthread_mutex_unlock(&playback_mutex);

    if (first) {
        loopback_pause(true);
    }
    return AUDIO_PLAYBACK_BT;
}

void audio_playback_end(audio_playback_t route) {
    if (route == AUDIO_PLAYBACK_LOCAL) {
        audio_set_play_mode(AUDIO_PLAY_OFF);
        return;
    }

    pthread_mutex_lock(&playback_mutex);
    bool last = false;
    if (playback_refcount > 0 && --playback_refcount == 0) {
        last = true;
    }
    pthread_mutex_unlock(&playback_mutex);

    if (last) {
        loopback_pause(false);
    }
}

bool audio_bt_audio_active(void) {
    pthread_mutex_lock(&bt_sink_mutex);
    bool active = bt_sink_active;
    pthread_mutex_unlock(&bt_sink_mutex);
    return active;
}

int audio_bt_sink_volume_get(void) {
    pthread_mutex_lock(&bt_sink_mutex);
    int value = bt_sink_volume;
    pthread_mutex_unlock(&bt_sink_mutex);
    return value;
}

void audio_bt_sink_volume_set(int value) {
    char    name[256];
    pa_cvolume cv;

    if (value < 0) {
        value = 0;
    } else if (value > AUDIO_BT_SINK_VOLUME_MAX) {
        value = AUDIO_BT_SINK_VOLUME_MAX;
    }

    pthread_mutex_lock(&bt_sink_mutex);
    if (!bt_sink_active || bt_sink_name[0] == '\0') {
        pthread_mutex_unlock(&bt_sink_mutex);
        return;
    }
    snprintf(name, sizeof(name), "%s", bt_sink_name);
    cvolume_from_value(value, &cv);
    bt_sink_volume = value;
    pthread_mutex_unlock(&bt_sink_mutex);

    if (!ctx || !mloop) {
        return;
    }

    pa_threaded_mainloop_lock(mloop);
    pa_operation *op = pa_context_set_sink_volume_by_name(ctx, name, &cv, NULL, NULL);
    if (op) {
        pa_operation_unref(op);
    }
    pa_threaded_mainloop_unlock(mloop);
}

bool audio_bt_sink_muted(void) {
    pthread_mutex_lock(&bt_sink_mutex);
    bool muted = bt_sink_mute;
    pthread_mutex_unlock(&bt_sink_mutex);
    return muted;
}

void audio_bt_sink_mute_set(bool mute) {
    char name[256];

    pthread_mutex_lock(&bt_sink_mutex);
    if (!bt_sink_active || bt_sink_name[0] == '\0') {
        pthread_mutex_unlock(&bt_sink_mutex);
        return;
    }
    snprintf(name, sizeof(name), "%s", bt_sink_name);
    bt_sink_mute = mute;
    pthread_mutex_unlock(&bt_sink_mutex);

    if (!ctx || !mloop) {
        return;
    }

    pa_threaded_mainloop_lock(mloop);
    pa_operation *op = pa_context_set_sink_mute_by_name(ctx, name, mute ? 1 : 0, NULL, NULL);
    if (op) {
        pa_operation_unref(op);
    }
    pa_threaded_mainloop_unlock(mloop);
}

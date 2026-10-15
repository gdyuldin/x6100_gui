/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 *
 *  1KO125: the recorder writes WAV, mono, 12000 Hz, PCM 16 bit - the format
 *  read by the WEFAX, SSTV and NAVTEX "Test" functions.  The audio comes from
 *  the same DSP path the decoders use (DC blocker + resampler to 12 kHz), so
 *  a recording replayed as a test file is exactly what the decoder received.
 *  WAV needs no MP3 encoder (LAME) in libsndfile.
 */

#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include <sndfile.h>

#include "audio.h"
#include "recorder.h"
#include "dsp.h"
#include "msg.h"
#include "scheduler.h"

#define RECORDER_RATE   12000   /* = WEFAX/SSTV/NAVTEX decoder sample rate */

char            *recorder_path = "/mnt/rec";

static bool     on = false;
static SNDFILE  *file = NULL;

static uint32_t dsp_audio_sub_id = AUDIO_SUB_INVALID;

static void put_float_samples(size_t nsamples, float *samples);

static bool create_file() {
    char        filename[80];
    time_t      now = time(NULL);
    struct tm   t;

    if (mkdir(recorder_path, 0755) != 0 && errno != EEXIST) {
        LV_LOG_ERROR("Problem with create dir %s: %s", recorder_path, strerror(errno));
        return false;
    }

    localtime_r(&now, &t);
    snprintf(filename, sizeof(filename),
        "%s/REC_%04i%02i%02i_%02i%02i%02i.wav",
        recorder_path, t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec
    );

    SF_INFO sfinfo;

    memset(&sfinfo, 0, sizeof(sfinfo));
    sfinfo.samplerate = RECORDER_RATE;
    sfinfo.channels = 1;
    sfinfo.format = SF_FORMAT_WAV | SF_FORMAT_PCM_16;

    file = sf_open(filename, SFM_WRITE, &sfinfo);

    if (file == NULL) {
        LV_LOG_ERROR("Problem with create file %s: %s", filename, sf_strerror(NULL));
        unlink(filename);   /* sf_open may leave an empty file behind */
        return false;
    }

    /* Float -> PCM 16: clip peaks instead of wrapping around. */
    sf_command(file, SFC_SET_CLIPPING, NULL, SF_TRUE);

    return true;
}

void recorder_set_on(bool x) {
    if (x) {
        if (on) {
            return;
        }
        if (dsp_audio_sub_id == AUDIO_SUB_INVALID) {
            dsp_audio_sub_id = dsp_audio_subscribe_float(put_float_samples, RECORDER_RATE);
        }
        if (dsp_audio_sub_id == AUDIO_SUB_INVALID) {
            msg_update_text_fmt("Recorder: no audio stream");
            return;
        }
        if (!create_file()) {
            msg_update_text_fmt("Problem with create file");
            return;
        }
        msg_update_text_fmt("Recorder is on");
        on = true;
        dsp_audio_set_active(dsp_audio_sub_id, true);
        scheduler_msg_send(MSG_RECORDER_START, NULL);
    } else {
        if (!on) {
            return;
        }
        msg_update_text_fmt("Recorder is off");
        /* After this returns no audio callback is running (dsp mutex). */
        dsp_audio_set_active(dsp_audio_sub_id, false);
        on = false;
        if (file) {
            sf_close(file);
            file = NULL;
        }
        scheduler_msg_send(MSG_RECORDER_STOP, NULL);
    }
}

bool recorder_is_on() {
    return on;
}

/* 12 kHz float samples (full scale = 1.0) from the DSP audio thread. */
static void put_float_samples(size_t nsamples, float *samples) {
    if (file) {
        sf_write_float(file, samples, (sf_count_t)nsamples);
    }
}

/* Kept for compatibility with recorder.h; no longer used by the recorder. */
void recorder_put_audio_samples(size_t nsamples, int16_t *samples) {
    (void)nsamples;
    (void)samples;
}

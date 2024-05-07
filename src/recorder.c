/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

#include <time.h>
#include <sys/time.h>
#include <sndfile.h>

#include "audio.h"
#include "dialog_recorder.h"
#include "recorder.h"
#include "msg.h"
#include "params.h"

char            *recorder_path = "/mnt/rec";

static bool     on = false;
static SNDFILE  *file = NULL;

static bool create_file() {
    SF_INFO sfinfo;

    memset(&sfinfo, 0, sizeof(sfinfo));

    sfinfo.samplerate = AUDIO_CAPTURE_RATE;
    sfinfo.channels = 1;
    sfinfo.format = SF_FORMAT_MPEG | SF_FORMAT_MPEG_LAYER_III;
    
    char        filename[64];
    time_t      now = time(NULL);
    struct tm   *t = localtime(&now);

    snprintf(filename, sizeof(filename),
        "%s/REC_%04i%02i%02i_%02i%02i%02i.mp3", 
        recorder_path, t->tm_year + 1900, t->tm_mon + 1, t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec
    );
    
    file = sf_open(filename, SFM_WRITE, &sfinfo);
    
    if (file == NULL) {
        const char* err = sf_strerror(NULL);
        LV_LOG_ERROR("Problem with create file: %s", err);
        return false;
    }

    double q = 0.25;
    sf_command(file, SFC_SET_VBR_ENCODING_QUALITY, &q, sizeof(q));

    return true;
}

void recorder_set_on(bool x) {
    if (x) {
        if (!create_file()) {
            msg_set_text_fmt("Problem with create file");
            return;
        } else {
            msg_set_text_fmt("Recorder is on");
        }
        on = true;
    } else {
        msg_set_text_fmt("Recorder is off");
        on = false;
        sf_close(file);
    }

    dialog_recorder_set_on(on);
}

bool recorder_is_on() {
    return on;
}

void recorder_put_audio_samples(size_t nsamples, int16_t *samples) {
    int16_t *out_samples = audio_gain(samples, nsamples, params.rec_gain * 6);

    sf_write_short(file, out_samples, nsamples);
    free(out_samples);
}

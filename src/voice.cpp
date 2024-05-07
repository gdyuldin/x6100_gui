/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

#include "voice.h"

extern "C" {
#include <unistd.h>
#include <pthread.h>
#include <aether_radio/x6100_control/control.h>
#include <lvgl.h>

#include "audio.h"
#include "params.h"
#include "util.h"
#include "backlight.h"
#include "recorder.h"
#include "msg.h"
}

#include <memory>
#include <stdexcept>
#include <iostream>
#include <fstream>
#include <iterator>
#include <algorithm>

#include <RHVoice/core/engine.hpp>
#include <RHVoice/core/document.hpp>
#include <RHVoice/core/client.hpp>
#include <RHVoice/audio.hpp>

using namespace RHVoice;

class audio_player: public client {
public:
    audio_player();

    bool play_speech(const short* samples_buf, std::size_t count);
    void finish();

private:
    audio::playback_stream stream;
};

audio_player::audio_player() {
    stream.set_sample_rate(24000);
    stream.set_buffer_size(512);
    stream.open();
}

bool audio_player::play_speech(const short* buf, std::size_t count) {
    try {
        stream.write(buf, count);
        return true;
    } catch (...) {
        stream.close();
        return false;
    }
}

void audio_player::finish() {
    if (stream.is_open())
        stream.drain();
}

typedef struct {
    const char* name; 
    const char* label;
    const char* welcome;
} voice_item_t;

static std::shared_ptr<engine>      eng(new engine);
static voice_profile                profile;
static char                         buf[512];
static char                         prev[512];
static pthread_t                    thread;
static uint32_t                     delay = 0;
static bool                         run = false;
static uint16_t                     repeated = 0;
static bool                         sure = false;

static voice_item_t                 voice_item[VOICES_NUM] = {
    { .name = "lyubov",         .label = "Lyubov (En)",     .welcome = "Hello. This is voice Lyubov" },
    { .name = "slt",            .label = "SLT (En)",        .welcome = "Hello. This is voice S L T" },
    { .name = "alan",           .label = "Alan (En)",       .welcome = "Hello. This is voice Alan" },
    { .name = "evgeniy-eng",    .label = "Evgeniy (En)",    .welcome = "Hello. This is voice Evgeniy" },
};

static void * say_thread(void *arg) {
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

    if (delay) {
        usleep(delay);
    }
    
    run = true;

    profile = eng->create_voice_profile(voice_item[params.voice_lang.x].name);

    char *ptr = strchr(buf, '|');
    
    if (ptr != NULL) {
        if (strncmp(buf, prev, ptr - buf) == 0) {
            repeated++;
        
            if (repeated > 4) {
                repeated = 0;
                ptr = buf;
            } else {
                ptr++;
            }
        } else {
            repeated = 0;
            ptr = buf;
        }
    } else {
        repeated = 0;
        ptr = buf;
    }
    strcpy(prev, buf);

    audio_player                    player;
    std::istringstream              text{ptr};
    std::istreambuf_iterator<char>  text_start{text};
    std::istreambuf_iterator<char>  text_end;
    std::unique_ptr<document>       doc = document::create_from_plain_text(eng, text_start, text_end, content_text, profile);
    
    doc->speech_settings.relative.rate = params.voice_rate.x / 100.0;
    doc->speech_settings.relative.pitch = params.voice_pitch.x / 100.0;
    doc->speech_settings.relative.volume = params.voice_volume.x / 100.0;
    doc->set_owner(player);

    audio_play_en(true);
    doc->synthesize();
    player.finish();
    audio_play_en(false);
    
    run = false;
    sure = false;
    return NULL;
}

void voice_sure() {
    sure = true;
}

void voice_change_mode() {
    voice_sure();

    switch (params.voice_mode.x) {
        case VOICE_OFF:
            params_uint8_set(&params.voice_mode, VOICE_LCD);
            msg_set_text_fmt("Voice mode: LCD");
            voice_say_text("Voice mode|", "is LCD");
            break;
            
        case VOICE_LCD:
            params_uint8_set(&params.voice_mode, VOICE_ALWAYS);
            msg_set_text_fmt("Voice mode: Always");
            voice_say_text("Voice mode|", "is always");
            break;
            
        case VOICE_ALWAYS:
            params_uint8_set(&params.voice_mode, VOICE_OFF);
            msg_set_text_fmt("Voice mode: Off");
            voice_say_text("Voice mode|", "is off");
            break;
    }
}

bool voice_enable() {
    if (run || recorder_is_on()) {
        return false;
    }
    
    if (sure) {
        return true;
    }
    
    switch (params.voice_mode.x) {
        case VOICE_OFF:
            return false;
            
        case VOICE_ALWAYS:
            return true;
            
        case VOICE_LCD:
            return !backlight_is_on();
    }
    
    return false;
}

void voice_delay_say_text_fmt(const char * fmt, ...) {
    if (!voice_enable()) {
        return;
    }

    va_list args;

    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    if (thread) {
        pthread_cancel(thread);
        pthread_join(thread, NULL);
    }
    
    delay = 1000000;
    pthread_create(&thread, NULL, say_thread, NULL);
}

void voice_say_text_fmt(const char * fmt, ...) {
    if (!voice_enable()) {
        return;
    }

    va_list args;

    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    
    delay = 0;
    pthread_create(&thread, NULL, say_thread, NULL);
}

void voice_say_freq(uint64_t freq) {
    if (!voice_enable()) {
        return;
    }

    uint16_t    mhz, khz, hz;
    
    split_freq(freq, &mhz, &khz, &hz);
    
    if (hz) {
        snprintf(buf, sizeof(buf), "%i.|%i.%i", mhz, khz, hz);
    } else if (khz) {
        snprintf(buf, sizeof(buf), "%i.|%i", mhz, khz);
    } else {
        snprintf(buf, sizeof(buf), "%i", mhz);
    }

    if (thread) {
        pthread_cancel(thread);
        pthread_join(thread, NULL);
    }

    delay = 1000000;
    pthread_create(&thread, NULL, say_thread, NULL);
}

void voice_say_bool(const char *prompt, bool x) {
    voice_delay_say_text_fmt("%s|%s", prompt, x ? "is on" : "is off");
}

void voice_say_int(const char *prompt, int32_t x) {
    voice_delay_say_text_fmt("%s|%i", prompt, x);
}

void voice_say_float(const char *prompt, float x) {
    voice_delay_say_text_fmt("%s|%.1f", prompt, x);
}

void voice_say_float2(const char *prompt, float x) {
    voice_delay_say_text_fmt("%s|%.2f", prompt, x);
}

void voice_say_text(const char *prompt, const char *x) {
    voice_delay_say_text_fmt("%s|%s", prompt, x);
}

const char * voice_change(int16_t diff) {
    uint8_t x;

    if (diff) {
        x = params_uint8_change(&params.voice_lang, diff);
    } else {
        x = params.voice_lang.x;
    }

    return voice_item[x].label;
}

void voice_say_lang() {
    voice_delay_say_text_fmt(voice_item[params.voice_lang.x].welcome);
}

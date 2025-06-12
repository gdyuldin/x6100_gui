/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

#include "vol.h"

#include "msg.h"
#include "radio.h"
#include "main.h"
#include "params/params.h"
#include "voice.h"
#include "util.h"
#include "cfg/mode.h"
#include "knobs.h"

static vol_mode_t   vol_mode = VOL_VOL;

void vol_update(int16_t diff, bool voice, bool show) {
    int32_t     x;
    float       f;
    char        *s;
    bool        b;
    int32_t     freq;

    uint32_t    color = vol->mode == VOL_EDIT ? 0xFFFFFF : 0xBBBBBB;

    switch (vol_mode) {
        case VOL_VOL:
            x = radio_change_vol(diff);
            if(show) msg_update_text_fmt("#%3X Volume: %i", color, x);
            knobs_update_vol("Volume: %i", x);

            if (diff) {
                voice_say_int("Audio level", x);
            } else if (voice) {
                voice_say_text_fmt("Audio level");
            }
            break;

        case VOL_RFG:
            x = subject_get_int(cfg_cur.band->rfg.val);
            x = limit(x + diff, 0, 100);
            subject_set_int(cfg_cur.band->rfg.val, x);
            if(show) msg_update_text_fmt("#%3X RF gain: %i", color, x);
            knobs_update_vol("RF gain: %i", x);

            if (diff) {
                voice_say_int("RF gain", x);
            } else if (voice) {
                voice_say_text_fmt("RF gain");
            }
            break;

        case VOL_SQL:
            x = subject_get_int(cfg.sql.val);
            x = limit(x + diff, 0, 100);
            subject_set_int(cfg.sql.val, x);
            if(show) msg_update_text_fmt("#%3X Voice SQL: %i", color, x);
            knobs_update_vol("Voice SQL: %i", x);

            if (diff) {
                voice_say_int("Squelch level %i", x);
            } else if (voice) {
                voice_say_text_fmt("Squelch level");
            }
            break;

        case VOL_FILTER_LOW:
            x = subject_get_int(cfg_cur.filter.low);
            if (diff) {
                // TODO: make step depending on freq
                x = align_int(x + diff * 10, 10);
                x = cfg_mode_set_low_filter(x);
            }
            // x = radio_change_filter_low(freq);
            if(show) msg_update_text_fmt("#%3X Filter low: %i Hz", color, x);
            knobs_update_vol("Filter low: %i Hz", x);

            if (diff) {
                voice_delay_say_text_fmt("%i", x);
            } else if (voice) {
                voice_say_text_fmt("Low filter limit");
            }
            break;

        case VOL_FILTER_HIGH:
            // freq = params_current_mode_filter_high_get();
            x = subject_get_int(cfg_cur.filter.high);
            if (diff) {
                uint8_t freq_step;
                switch (subject_get_int(cfg_cur.mode)) {
                case x6100_mode_cw:
                case x6100_mode_cwr:
                    freq_step = 10;
                    break;
                default:
                    freq_step = 50;
                    break;
                }
                x = align_int(x + diff * freq_step, freq_step);
                x = cfg_mode_set_high_filter(x);
            }
            // x = radio_change_filter_high(freq);
            if(show) msg_update_text_fmt("#%3X Filter high: %i Hz", color, x);
            knobs_update_vol("Filter high: %i Hz", x);

            if (diff) {
                voice_say_int("High filter limit", x);
            } else if (voice) {
                voice_say_text_fmt("High filter limit");
            }
            break;

        case VOL_FILTER_BW:;
            uint32_t bw = subject_get_int(cfg_cur.filter.bw);
            if (diff) {
                bw = align_int(bw + diff * 20, 20);
                subject_set_int(cfg_cur.filter.bw, bw);
            }
            if(show) msg_update_text_fmt("#%3X Filter bw: %i Hz", color, bw);
            knobs_update_vol("Filter bw: %i Hz", bw);

            if (diff) {
                voice_delay_say_text_fmt("%i", bw);
            } else if (voice) {
                voice_say_text_fmt("Bandwidth filter limit");
            }
            break;

        case VOL_PWR:
            f = subject_get_float(cfg.pwr.val);
            f += diff * 0.1f;
            f = LV_MIN(10.0f, f);
            f = LV_MAX(0.1f, f);
            subject_set_float(cfg.pwr.val, f);
            if(show) msg_update_text_fmt("#%3X Power: %0.1f W", color, f);
            knobs_update_vol("Power: %0.1f W", f);

            if (diff) {
                voice_say_float("Transmit power", f);
            } else if (voice) {
                voice_say_text_fmt("Transmit power");
            }
            break;

        case VOL_MIC:
            x = radio_change_mic(diff);
            s = params_mic_str_get(x);
            if(show) msg_update_text_fmt("#%3X MIC: %s", color, s);
            knobs_update_vol("MIC: %s", s);

            if (diff) {
                voice_say_text("Mic selector", s);
            } else if (voice) {
                voice_say_text_fmt("Mic selector");
            }
            break;

        case VOL_HMIC:
            x = radio_change_hmic(diff);
            if(show) msg_update_text_fmt("#%3X H-MIC gain: %i", color, x);
            knobs_update_vol("H-MIC gain: %i", x);

            if (diff) {
                voice_say_int("Hand microphone gain", x);
            } else if (voice) {
                voice_say_text_fmt("Hand microphone gain");
            }
            break;

        case VOL_IMIC:
            x = radio_change_imic(diff);
            if(show) msg_update_text_fmt("#%3X I-MIC gain: %i", color, x);
            knobs_update_vol("I-MIC gain: %i", x);

            if (diff) {
                voice_say_int("Internal microphone gain", x);
            } else if (voice) {
                voice_say_text_fmt("Internal microphone gain");
            }
            break;

        case VOL_MONI:
            x = radio_change_moni(diff);
            if(show) msg_update_text_fmt("#%3X Moni level: %i", color, x);
            knobs_update_vol("Moni level: %i", x);

            if (diff) {
                voice_say_int("Monitor level", x);
            } else if (voice) {
                voice_say_text_fmt("Monitor level");
            }
            break;

        case VOL_VOICE_LANG:
            s = (char *)voice_change(diff);
            if(show) msg_update_text_fmt("#%3X Voice: %s", color, s);
            knobs_update_vol("Voice: %s", s);

            if (diff) {
                voice_say_lang();
            } else if (voice) {
                voice_say_text_fmt("Voice selector");
            }
            break;

        case VOL_VOICE_RATE:
            x = params_uint8_change(&params.voice_rate, diff);
            if(show) msg_update_text_fmt("#%3X Voice rate: %i", color, x);
            knobs_update_vol("Voice rate: %i", x);

            if (diff == 0 && voice) {
                voice_say_text_fmt(params.voice_rate.voice);
            }
            break;

        case VOL_VOICE_PITCH:
            x = params_uint8_change(&params.voice_pitch, diff);
            if(show) msg_update_text_fmt("#%3X Voice pitch: %i", color, x);
            knobs_update_vol("Voice pitch: %i", x);

            if (diff == 0 && voice) {
                voice_say_text_fmt(params.voice_pitch.voice);
            }
            break;

        case VOL_VOICE_VOLUME:
            x = params_uint8_change(&params.voice_volume, diff);
            if(show) msg_update_text_fmt("#%3X Voice volume: %i", color, x);
            knobs_update_vol("Voice volume: %i", x);

            if (diff == 0 && voice) {
                voice_say_text_fmt(params.voice_volume.voice);
            }
            break;

        default:
            break;
    }
}

void vol_change_mode(int16_t dir) {
    vol_mode = loop_modes(dir, vol_mode, params.vol_modes, VOL_LAST-1);
    vol_update(0, true, true);
}

void vol_set_mode(vol_mode_t mode) {
    vol_mode = mode;
    vol->mode = VOL_EDIT;
}

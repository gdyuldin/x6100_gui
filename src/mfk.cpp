/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */
#include "mfk.h"

#include "util.hpp"
#include "cw.h"
#include "voice.h"
#include "util.h"
#include "knobs.h"
#include "dsp.h"

#include <vector>

extern "C" {
    #include "params/params.h"
    #include "spectrum.h"
    #include "waterfall.h"
    #include "msg.h"
    #include "radio.h"
    #include "rtty.h"
    #include "info.h"
    #include "backlight.h"
    #include "cw_tune_ui.h"
    #include "band_info.h"
    #include "pubsub_ids.h"
    #include "meter.h"

    #include "lvgl/lvgl.h"
}

static bool show_msg = false;

template<typename... Args> void text_msg(const char * f, Args... args) {
    if (show_msg) {
        msg_update_text_fmt(f, args...);
    }
}

template <typename T> static T loop_items(std::vector<T> items, T cur, bool next);

mfk_state_t  mfk_state = MFK_STATE_EDIT;
cfg_ctrl_t   mfk_mode = CTRL_SPECTRUM_FACTOR;

void mfk_update(int16_t diff, bool voice) {
    int32_t     i;
    char        *str;
    bool        b;
    float       f;

    uint32_t    color = mfk_state == MFK_STATE_EDIT ? 0xFFFFFF : 0xBBBBBB;

    show_msg = !knobs_visible();

    switch (mfk_mode) {
        case CTRL_SPECTRUM_FACTOR:
            i = subject_get_int(cfg_cur.zoom);
            if (diff != 0) {
                if (diff > 0) {
                    i <<= diff;
                } else {
                    i >>= -diff;
                }
                i = clip(i, 1, 8);
                subject_set_int(cfg_cur.zoom, i);
            }
            text_msg("#%3X Spectrum zoom: x%i", color, i);

            if (diff) {
                voice_say_int("Spectrum zoom", i);
            } else if (voice) {
                voice_say_text_fmt("Spectrum zoom");
            }
            break;

        case CTRL_COMP:
            i = subject_get_int(cfg.comp.val);
            if (diff) {
                i = clip(i + diff, 1, 8);
                subject_set_int(cfg.comp.val, i);
            }
            text_msg("#%3X Compressor ratio: %s", color, params_comp_str_get(i));

            if (diff) {
                if (i > 1) {
                    voice_say_text_fmt("Compressor ratio %d to 1", i);
                } else {
                    voice_say_text_fmt("Compressor disabled");
                }
            } else if (voice) {
                voice_say_text_fmt("Compressor ratio");
            }
            break;

        case CTRL_KEY_SPEED:
            i = subject_get_int(cfg.key_speed.val);
            if (diff) {
                i = clip(i + diff, 5, 50);
                subject_set_int(cfg.key_speed.val, i);
            }
            text_msg("#%3X Key speed: %i wpm", color, i);

            if (diff) {
                voice_say_int("CW key speed", i);
            } else if (voice) {
                voice_say_text_fmt("CW key speed");
            }
            break;

        case CTRL_KEY_MODE:
            i = subject_get_int(cfg.key_mode.val);
            if (diff) {
                i = loop_items({x6100_key_manual, x6100_key_auto_left, x6100_key_auto_right}, (x6100_key_mode_t)i, diff > 0);
                subject_set_int(cfg.key_mode.val, i);
            }
            str = params_key_mode_str_get((x6100_key_mode_t)i);
            text_msg("#%3X Key mode: %s", color, str);

            if (diff) {
                voice_say_text("CW key mode", str);
            } else if (voice) {
                voice_say_text_fmt("CW key mode selector");
            }
            break;

        case CTRL_IAMBIC_MODE:
            i = subject_get_int(cfg.iambic_mode.val);
            if (diff) {
                i = loop_items({x6100_iambic_a, x6100_iambic_b}, (x6100_iambic_mode_t)i, diff > 0);
                subject_set_int(cfg.iambic_mode.val, i);
            }
            str = params_iambic_mode_str_ger((x6100_iambic_mode_t)i);
            text_msg("#%3X Iambic mode: %s", color, str);

            if (diff) {
                voice_say_text("Iambic mode", str);
            } else if (voice) {
                voice_say_text_fmt("Iambic mode selector");
            }
            break;

        case CTRL_KEY_TONE:
            i = subject_get_int(cfg.key_tone.val);
            if (diff) {
                i = clip(i + diff * 10, 400, 1200);
                subject_set_int(cfg.key_tone.val, i);
            }
            text_msg("#%3X Key tone: %i Hz", color, i);

            if (diff) {
                voice_say_int("CW key tone", i);
            } else if (voice) {
                voice_say_text_fmt("CW key tone");
            }
            break;

        case CTRL_KEY_VOL:
            i = subject_get_int(cfg.key_vol.val);
            if (diff) {
                i = clip(i + diff, 0, 32);
                subject_set_int(cfg.key_vol.val, i);
            }
            text_msg("#%3X Key volume: %i", color, i);

            if (diff) {
                voice_say_int("CW key volume level", i);
            } else if (voice) {
                voice_say_text_fmt("CW key volume level");
            }
            break;

        case CTRL_KEY_TRAIN:
            b = subject_get_int(cfg.key_train.val);
            if (diff) {
                b = !b;
                subject_set_int(cfg.key_train.val, b);
            }
            text_msg("#%3X Key train: %s", color, b ? "On" : "Off");

            if (diff) {
                voice_say_bool("CW key train", b);
            } else if (voice) {
                voice_say_text_fmt("CW key train switcher");
            }
            break;

        case CTRL_QSK_TIME:
            i = subject_get_int(cfg.qsk_time.val);
            if (diff) {
                i = clip(i + diff * 10, 0, 1000);
                subject_set_int(cfg.qsk_time.val, i);
            }
            text_msg("#%3X QSK time: %i ms", color, i);

            if (diff) {
                voice_say_int("CW key QSK time", i);
            } else if (voice) {
                voice_say_text_fmt("CW key QSK time");
            }
            break;

        case CTRL_KEY_RATIO:
            f = subject_get_float(cfg.key_ratio.val);
            if (diff) {
                f = clip(f + diff * 0.1f, 2.5f, 4.5f);
                subject_set_float(cfg.key_ratio.val, f);
            }
            text_msg("#%3X Key ratio: %.1f", color, f);

            if (diff) {
                voice_say_float("CW key ratio", f);
            } else if (voice) {
                voice_say_text_fmt("CW key ratio");
            }
            break;

        case CTRL_ANT:
            {
                int32_t ant = subject_get_int(cfg.ant_id.val);
                if (diff != 0) {
                    ant = clip(ant + diff, 1, 5);
                    subject_set_int(cfg.ant_id.val, ant);
                    // radio_load_atu();
                    // info_atu_update();
                }
                text_msg("#%3X Antenna : %i", color, ant);

                if (diff) {
                    voice_say_int("Antenna", ant);
                } else if (voice) {
                    voice_say_text_fmt("Antenna selector");
                }
            }
            break;

        case CTRL_RIT:
            i = subject_get_int(cfg.rit.val);
            if (diff) {
                i = clip(align(i + diff * 10, 10), -1500, +1500);
                subject_set_int(cfg.rit.val, i);
            }
            text_msg("#%3X RIT: %c%i", color, i < 0 ? '-' : '+', abs(i));

            if (diff) {
                voice_say_int("RIT", i);
            } else if (voice) {
                voice_say_text_fmt("RIT");
            }
            break;

        case CTRL_XIT:
            i = subject_get_int(cfg.xit.val);
            if (diff) {
                i = clip(align(i + diff * 10, 10), -1500, +1500);
                subject_set_int(cfg.xit.val, i);
            }
            text_msg("#%3X XIT: %c%i", color, i < 0 ? '-' : '+', abs(i));

            if (diff) {
                voice_say_int("XIT", i);
            } else if (voice) {
                voice_say_text_fmt("XIT");
            }
            break;

        case CTRL_DNF:
            b = subject_get_int(cfg.dnf.val);
            if (diff) {
                b = !b;
                subject_set_int(cfg.dnf.val, b);
            }
            text_msg("#%3X DNF: %s", color, b ? "On" : "Off");

            if (diff) {
                voice_say_bool("DNF", b);
            } else if (voice) {
                voice_say_text_fmt("DNF switcher");
            }
            break;

        case CTRL_DNF_CENTER:
            i = subject_get_int(cfg.dnf_center.val);
            if (diff) {
                i = clip(i + diff * 50, 100, 3000);
                subject_set_int(cfg.dnf_center.val, i);
            }
            text_msg("#%3X DNF center: %i Hz", color, i);

            if (diff) {
                voice_say_int("DNF center frequency", i);
            } else if (voice) {
                voice_say_text_fmt("DNF center frequency");
            }
            break;

        case CTRL_DNF_WIDTH:
            i = subject_get_int(cfg.dnf_width.val);
            if (diff) {
                i = clip(i + diff * 5, 10, 100);
                subject_set_int(cfg.dnf_width.val, i);
            }
            text_msg("#%3X DNF width: %i Hz", color, i);

            if (diff) {
                voice_say_int("DNF width", i);
            } else if (voice) {
                voice_say_text_fmt("DNF width");
            }
            break;

        case CTRL_DNF_AUTO:
            b = subject_get_int(cfg.dnf_auto.val);
            if (diff) {
                b = !b;
                subject_set_int(cfg.dnf_auto.val, b);
            }
            text_msg("#%3X DNF auto: %s", color, b ? "On" : "Off");

            if (diff) {
                voice_say_bool("DNF auto", b);
            } else if (voice) {
                voice_say_text_fmt("DNF auto switcher");
            }
            break;

        case CTRL_NB:
            b = subject_get_int(cfg.nb.val);
            if (diff) {
                b = !b;
                subject_set_int(cfg.nb.val, b);
            }
            text_msg("#%3X NB: %s", color, b ? "On" : "Off");

            if (diff) {
                voice_say_bool("NB", b);
            } else if (voice) {
                voice_say_text_fmt("NB switcher");
            }
            break;

        case CTRL_NB_LEVEL:
            i = subject_get_int(cfg.nb_level.val);
            if (diff) {
                i = clip(i + diff * 5, 0, 100);
                subject_set_int(cfg.nb_level.val, i);
            }
            text_msg("#%3X NB level: %i", color, i);

            if (diff) {
                voice_say_int("NB level", i);
            } else if (voice) {
                voice_say_text_fmt("NB level");
            }
            break;

        case CTRL_NB_WIDTH:
            i = subject_get_int(cfg.nb_width.val);
            if (diff) {
                i = clip(i + diff * 5, 0, 100);
                subject_set_int(cfg.nb_width.val, i);
            }
            text_msg("#%3X NB width: %i Hz", color, i);

            if (diff) {
                voice_say_int("NB width", i);
            } else if (voice) {
                voice_say_text_fmt("NB width");
            }
            break;

        case CTRL_NR:
            b = subject_get_int(cfg.nr.val);
            if (diff) {
                b = !b;
                subject_set_int(cfg.nr.val, b);
            }
            text_msg("#%3X NR: %s", color, b ? "On" : "Off");

            if (diff) {
                voice_say_bool("NR", b);
            } else if (voice) {
                voice_say_text_fmt("NR switcher");
            }
            break;

        case CTRL_NR_LEVEL:
            i = subject_get_int(cfg.nr_level.val);
            if (diff) {
                i = clip(i + diff * 5, 0, 60);
                subject_set_int(cfg.nr_level.val, i);
            }
            text_msg("#%3X NR level: %i", color, i);

            if (diff) {
                voice_say_int("NR level", i);
            } else if (voice) {
                voice_say_text_fmt("NR level");
            }
            break;

        case CTRL_AGC_HANG:
            b = subject_get_int(cfg.agc_hang.val);
            if (diff) {
                b = !b;
                subject_set_int(cfg.agc_hang.val, b);
            }
            text_msg("#%3X AGC hang: %s", color, b ? "On" : "Off");

            if (diff) {
                voice_say_bool("Auto gain hang", b);
            } else if (voice) {
                voice_say_text_fmt("Auto gain hang switcher");
            }
            break;

        case CTRL_AGC_KNEE:
            i = subject_get_int(cfg.agc_knee.val);
            if (diff) {
                i = clip(i + diff, -100, 0);
                subject_set_int(cfg.agc_knee.val, i);
            }
            text_msg("#%3X AGC knee: %i dB", color, i);

            if (diff) {
                voice_say_int("Auto gain knee level", i);
            } else if (voice) {
                voice_say_text_fmt("Auto gain knee level");
            }
            break;

        case CTRL_AGC_SLOPE:
            i = subject_get_int(cfg.agc_slope.val);
            if (diff) {
                i = clip(i + diff, 0, 10);
                subject_set_int(cfg.agc_slope.val, i);
            }
            text_msg("#%3X AGC slope: %i dB", color, i);

            if (diff) {
                voice_say_int("Auto gain slope level", i);
            } else if (voice) {
                voice_say_text_fmt("Auto gain slope level");
            }
            break;

        case CTRL_CW_DECODER:
            b = subject_get_int(cfg.cw_decoder.val);
            if (diff) {
                b = !b;
                subject_set_int(cfg.cw_decoder.val, b);
            }
            text_msg("#%3X CW decoder: %s", color, b ? "On" : "Off");

            if (diff) {
                voice_say_bool("CW decoder", b);
            } else if (voice) {
                voice_say_text_fmt("CW decoder switcher");
            }
            break;

        case CTRL_CW_TUNE:
            b = subject_get_int(cfg.cw_tune.val);
            if (diff) {
                b = !b;
                subject_set_int(cfg.cw_tune.val, b);
            }
            text_msg("#%3X CW tune: %s", color, b ? "On" : "Off");

            if (diff) {
                voice_say_bool("CW tune", b);
            } else if (voice) {
                voice_say_text_fmt("CW tune switcher");
            }
            break;

        case CTRL_CW_DECODER_SNR:
            f = subject_get_float(cfg.cw_decoder_snr.val);
            if (diff) {
                f = clip(f + diff * 0.1f, 3.0f, 30.0f);
                subject_set_float(cfg.cw_decoder_snr.val, f);
            }
            text_msg("#%3X CW decoder SNR: %.1f dB", color, f);

            if (diff) {
                voice_say_float("CW decoder SNR level", f);
            } else if (voice) {
                voice_say_text_fmt("CW decoder SNR level");
            }
            break;

        case CTRL_CW_DECODER_PEAK_BETA:
            f = subject_get_float(cfg.cw_decoder_peak_beta.val);
            if (diff) {
                f = clip(f + diff * 0.01f, 0.1f, 0.95f);
                subject_set_float(cfg.cw_decoder_peak_beta.val, f);
            }

            text_msg("#%3X CW decoder peak beta: %.2f", color, f);

            if (diff) {
                voice_say_float("CW decoder peak beta", f);
            } else if (voice) {
                voice_say_text_fmt("CW decoder peak beta");
            }
            break;

        case CTRL_CW_DECODER_NOISE_BETA:
            f = subject_get_float(cfg.cw_decoder_noise_beta.val);
            if (diff) {
                f = clip(f + diff * 0.01f, 0.1f, 0.95f);
                subject_set_float(cfg.cw_decoder_noise_beta.val, f);
            }

            text_msg("#%3X CW decoder noise beta: %.2f", color, f);

            if (diff) {
                voice_say_float("CW decoder noise beta", f);
            } else if (voice) {
                voice_say_text_fmt("CW decoder noise beta");
            }
            break;

        case CTRL_RTTY_RATE:
            f = rtty_change_rate(diff);
            text_msg("#%3X RTTY rate: %.2f", color, f);

            if (diff) {
                voice_say_float2("Teletype rate", f);
            } else if (voice) {
                voice_say_text_fmt("Teletype rate");
            }
            break;

        case CTRL_RTTY_SHIFT:
            i = rtty_change_shift(diff);
            text_msg("#%3X RTTY shift: %i Hz", color, i);

            if (diff) {
                voice_say_int("Teletype frequency shift", i);
            } else if (voice) {
                voice_say_text_fmt("Teletype frequency shift");
            }
            break;

        case CTRL_RTTY_CENTER:
            i = rtty_change_center(diff);
            text_msg("#%3X RTTY center: %i Hz", color, i);

            if (diff) {
                voice_say_int("Teletype frequency center", i);
            } else if (voice) {
                voice_say_text_fmt("Teletype frequency center");
            }
            break;

        case CTRL_RTTY_REVERSE:
            b = rtty_change_reverse(diff);
            text_msg("#%3X RTTY reverse: %s", color, b ? "On" : "Off");

            if (diff) {
                voice_say_bool("Teletype reverse", b);
            } else if (voice) {
                voice_say_text_fmt("Teletype reverse switcher");
            }
            break;

        case CTRL_IF_SHIFT:
            {
                x6100_base_ver_t base_ver = x6100_control_get_base_ver();
                if ((util_compare_version(base_ver, (x6100_base_ver_t){1, 1, 9, 0}) >= 0) || (base_ver.rev >= 8)) {
                    i = subject_get_int(cfg_cur.band->if_shift.val);
                    if (diff) {
                        i = clip(i + diff * 100, -40000, 40000);
                        subject_set_int(cfg_cur.band->if_shift.val, i);
                    }
                    text_msg("#%3X IF shift: %i Hz", color, i);

                    if (diff) {
                        voice_say_int("IF shift", i);
                    } else if (voice) {
                        voice_say_text_fmt("IF shift control");
                    }
                } else {
                    msg_update_text_fmt("IF shift is not supported");
                }
            }
            break;

        default:
            return;
    }
    knobs_set_mfk_param(mfk_mode);
}

void mfk_change_mode(int16_t dir) {
    uint64_t mask = subject_get_uint64(cfg.mfk_modes.val);
    int size = sizeof(cfg_encoder_mfk_modes_default) / sizeof(cfg_encoder_mfk_modes_default[0]);
    std::vector<cfg_ctrl_t> all_modes(cfg_encoder_mfk_modes_default, cfg_encoder_mfk_modes_default + size);
    mfk_mode = loop_modes(dir, mfk_mode,  mask, all_modes);
    mfk_update(0, true);
}

void mfk_set_mode(cfg_ctrl_t mode) {
    mfk_mode = mode;
    mfk_state = MFK_STATE_EDIT;
    knobs_set_mfk_mode(true);
}

template <typename T> static T loop_items(std::vector<T> items, T cur, bool next) {
    int id;
    size_t len = std::size(items);
    for (id = 0; id < len; id++) {
        if (items[id] == cur) {
            break;
        }
    }
    id = (id + len + (next ? 1 : -1)) % len;
    return items[id];
}

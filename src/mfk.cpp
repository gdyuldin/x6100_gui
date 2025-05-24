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
#include "cfg/cfg.h"

#include <vector>

extern "C" {
    #include "util.h"
    #include "params/params.h"
    #include "spectrum.h"
    #include "waterfall.h"
    #include "msg.h"
    #include "dsp.h"
    #include "radio.h"
    #include "rtty.h"
    #include "info.h"
    #include "backlight.h"
    #include "cw_tune_ui.h"
    #include "band_info.h"
    #include "pubsub_ids.h"
    #include "meter.h"
    #include "knobs.h"

    #include "lvgl/lvgl.h"
}

template <typename T> static T loop_items(std::vector<T> items, T cur, bool next);

mfk_state_t  mfk_state = MFK_STATE_EDIT;
mfk_mode_t   mfk_mode = MFK_MIN_LEVEL;

void mfk_update(int16_t diff, bool voice, bool show) {
    int32_t     i;
    char        *str;
    bool        b;
    float       f;

    uint32_t    color = mfk_state == MFK_STATE_EDIT ? 0xFFFFFF : 0xBBBBBB;

    switch (mfk_mode) {
        case MFK_MIN_LEVEL:
            i = subject_get_int(cfg_cur.band->grid.min.val);
            if (diff != 0) {
                i = limit(i + diff, S_MIN, S7);
                subject_set_int(cfg_cur.band->grid.min.val, i);
            }
            if(show) msg_update_text_fmt("#%3X Min level: %i dB", color, i);
            knobs_update_mfk("Min level: %i dB", i);

            if (diff) {
                voice_say_int("Spectrum min level", i);
            } else if (voice) {
                voice_say_text_fmt("Spectrum min level");
            }
            break;

        case MFK_MAX_LEVEL:
            i = subject_get_int(cfg_cur.band->grid.max.val);
            if (diff != 0) {
                i = limit(i + diff, S8, S9_40);
                subject_set_int(cfg_cur.band->grid.max.val, i);
            }
            if(show) msg_update_text_fmt("#%3X Max level: %i dB", color, i);
            knobs_update_mfk("Max level: %i dB", i);

            if (diff) {
                voice_say_int("Spectrum max level", i);
            } else if (voice) {
                voice_say_text_fmt("Spectrum max level");
            }
            break;

        case MFK_SPECTRUM_FACTOR:
            i = subject_get_int(cfg_cur.zoom);
            if (diff != 0) {
                i = limit(i + diff, 1, 8);
                subject_set_int(cfg_cur.zoom, i);
            }
            if(show) msg_update_text_fmt("#%3X Spectrum zoom: x%i", color, i);
            knobs_update_mfk("Spectrum zoom: x%i", i);

            if (diff) {
                voice_say_int("Spectrum zoom", i);
            } else if (voice) {
                voice_say_text_fmt("Spectrum zoom");
            }
            break;


        case MFK_SPECTRUM_BETA:
            if (diff != 0) {
                params_lock();
                params.spectrum_beta += (diff < 0) ? -5 : 5;

                if (params.spectrum_beta < 0) {
                    params.spectrum_beta = 0;
                } else if (params.spectrum_beta > 90) {
                    params.spectrum_beta = 90;
                }
                params_unlock(&params.dirty.spectrum_beta);

                dsp_set_spectrum_beta(params.spectrum_beta / 100.0f);
            }
            if(show) msg_update_text_fmt("#%3X Spectrum beta: %i", color, params.spectrum_beta);
            knobs_update_mfk("Spectrum beta: %i", params.spectrum_beta);

            if (diff) {
                voice_say_int("Spectrum beta", params.spectrum_beta);
            } else if (voice) {
                voice_say_text_fmt("Spectrum beta");
            }
            break;

        case MFK_SPECTRUM_FILL:
            if (diff != 0) {
                params_lock();
                params.spectrum_filled = !params.spectrum_filled;
                params_unlock(&params.dirty.spectrum_filled);
            }
            if(show) msg_update_text_fmt("#%3X Spectrum fill: %s", color, params.spectrum_filled ? "On" : "Off");
            knobs_update_mfk("Spectrum fill: %s", params.spectrum_filled ? "On" : "Off");

            if (diff) {
                voice_say_bool("Spectrum fill", params.spectrum_filled);
            } else if (voice) {
                voice_say_text_fmt("Spectrum fill switcher");
            }
            break;

        case MFK_SPECTRUM_PEAK:
            if (diff != 0) {
                params_lock();
                params.spectrum_peak = !params.spectrum_peak;
                params_unlock(&params.dirty.spectrum_peak);
            }
            if(show) msg_update_text_fmt("#%3X Spectrum peak: %s", color, params.spectrum_peak ? "On" : "Off");
            knobs_update_mfk("Spectrum peak: %s", params.spectrum_peak ? "On" : "Off");

            if (diff) {
                voice_say_bool("Spectrum peak", params.spectrum_peak);
            } else if (voice) {
                voice_say_text_fmt("Spectrum peak switcher");
            }
            break;

        case MFK_PEAK_HOLD:
            if (diff != 0) {
                i = params.spectrum_peak_hold + diff * 1000;

                if (i < 1000) {
                    i = 1000;
                } else if (i > 10000) {
                    i = 10000;
                }

                params_lock();
                params.spectrum_peak_hold = i;
                params_unlock(&params.dirty.spectrum_peak_hold);
            }
            if(show) msg_update_text_fmt("#%3X Peak hold: %i s", color, params.spectrum_peak_hold / 1000);
            knobs_update_mfk("Peak hold: %i s", params.spectrum_peak_hold / 1000);

            if (diff) {
                voice_say_int("Peak hold time", params.spectrum_peak_hold / 1000);
            } else if (voice) {
                voice_say_text_fmt("Peak hold time");
            }
            break;

        case MFK_COMP:
            i = subject_get_int(cfg.comp.val);
            if (diff) {
                i = clip(i + diff, 1, 8);
                subject_set_int(cfg.comp.val, i);
            }
            if(show) msg_update_text_fmt("#%3X Compressor ratio: %s", color, params_comp_str_get(i));
            knobs_update_mfk("Compressor ratio: %s", params_comp_str_get(i));

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

        case MFK_PEAK_SPEED:
            if (diff != 0) {
                f = params.spectrum_peak_speed + diff * 0.1f;

                if (f < 0.1f) {
                    f = 0.1f;
                } else if (f > 3.0f) {
                    f = 3.0f;
                }

                params_lock();
                params.spectrum_peak_speed = f;
                params_unlock(&params.dirty.spectrum_peak_speed);
            }
            if(show) msg_update_text_fmt("#%3X Peak speed: %.1f dB", color, params.spectrum_peak_speed);
            knobs_update_mfk("Peak speed: %.1f dB", params.spectrum_peak_speed);

            if (diff) {
                voice_say_float("Peak speed time", params.spectrum_peak_speed);
            } else if (voice) {
                voice_say_text_fmt("Peak speed time");
            }
            break;

        case MFK_KEY_SPEED:
            // i = radio_change_key_speed(diff);
            i = subject_get_int(cfg.key_speed.val);
            if (diff) {
                i = clip(i + diff, 5, 50);
                subject_set_int(cfg.key_speed.val, i);
            }
            if(show) msg_update_text_fmt("#%3X Key speed: %i wpm", color, i);
            knobs_update_mfk("Key speed: %i wpm", i);

            if (diff) {
                voice_say_int("CW key speed", i);
            } else if (voice) {
                voice_say_text_fmt("CW key speed");
            }
            break;

        case MFK_KEY_MODE:
            // i = radio_change_key_mode(diff);
            i = subject_get_int(cfg.key_mode.val);
            if (diff) {
                i = loop_items({x6100_key_manual, x6100_key_auto_left, x6100_key_auto_right}, (x6100_key_mode_t)i, diff > 0);
                subject_set_int(cfg.key_mode.val, i);
            }
            str = params_key_mode_str_get((x6100_key_mode_t)i);
            if(show) msg_update_text_fmt("#%3X Key mode: %s", color, str);
            knobs_update_mfk("Key mode: %s", str);

            if (diff) {
                voice_say_text("CW key mode", str);
            } else if (voice) {
                voice_say_text_fmt("CW key mode selector");
            }
            break;

        case MFK_IAMBIC_MODE:
            // i = radio_change_iambic_mode(diff);
            i = subject_get_int(cfg.key_mode.val);
            if (diff) {
                i = loop_items({x6100_iambic_a, x6100_iambic_b}, (x6100_iambic_mode_t)i, diff > 0);
                subject_set_int(cfg.key_mode.val, i);
            }
            str = params_iambic_mode_str_ger((x6100_iambic_mode_t)i);
            if(show) msg_update_text_fmt("#%3X Iambic mode: %s", color, str);
            knobs_update_mfk("Iambic mode: %s", str);

            if (diff) {
                voice_say_text("Iambic mode", str);
            } else if (voice) {
                voice_say_text_fmt("Iambic mode selector");
            }
            break;

        case MFK_KEY_TONE:
            // i = radio_change_key_tone(diff);
            i = subject_get_int(cfg.key_tone.val);
            if (diff) {
                i = clip(i + diff * 10, 400, 1200);
                subject_set_int(cfg.key_tone.val, i);
            }
            if(show) msg_update_text_fmt("#%3X Key tone: %i Hz", color, i);
            knobs_update_mfk("Key tone: %i Hz", i);

            if (diff) {
                voice_say_int("CW key tone", i);
            } else if (voice) {
                voice_say_text_fmt("CW key tone");
            }
            break;

        case MFK_KEY_VOL:
            // i = radio_change_key_vol(diff);
            i = subject_get_int(cfg.key_vol.val);
            if (diff) {
                i = clip(i + diff, 0, 32);
                subject_set_int(cfg.key_vol.val, i);
            }
            if(show) msg_update_text_fmt("#%3X Key volume: %i", color, i);
            knobs_update_mfk("Key volume: %i", i);

            if (diff) {
                voice_say_int("CW key volume level", i);
            } else if (voice) {
                voice_say_text_fmt("CW key volume level");
            }
            break;

        case MFK_KEY_TRAIN:
            // b = radio_change_key_train(diff);
            b = subject_get_int(cfg.key_train.val);
            if (diff) {
                b = !b;
                subject_set_int(cfg.key_train.val, b);
            }
            if(show) msg_update_text_fmt("#%3X Key train: %s", color, b ? "On" : "Off");
            knobs_update_mfk("Key train: %s", b ? "On" : "Off");

            if (diff) {
                voice_say_bool("CW key train", b);
            } else if (voice) {
                voice_say_text_fmt("CW key train switcher");
            }
            break;

        case MFK_QSK_TIME:
            // i = radio_change_qsk_time(diff);
            i = subject_get_int(cfg.qsk_time.val);
            if (diff) {
                i = clip(i + diff * 10, 0, 1000);
                subject_set_int(cfg.qsk_time.val, i);
            }
            if(show) msg_update_text_fmt("#%3X QSK time: %i ms", color, i);
            knobs_update_mfk("QSK time: %i ms", i);

            if (diff) {
                voice_say_int("CW key QSK time", i);
            } else if (voice) {
                voice_say_text_fmt("CW key QSK time");
            }
            break;

        case MFK_KEY_RATIO:
            // i = radio_change_key_ratio(diff);
            f = subject_get_float(cfg.key_ratio.val);
            if (diff) {
                f = clip(f + diff * 0.1f, 2.5f, 4.5f);
                subject_set_float(cfg.key_ratio.val, f);
            }
            if(show) msg_update_text_fmt("#%3X Key ratio: %.1f", color, f);
            knobs_update_mfk("Key ratio: %.1f", f);

            if (diff) {
                voice_say_float("CW key ratio", f);
            } else if (voice) {
                voice_say_text_fmt("CW key ratio");
            }
            break;

        case MFK_CHARGER:
            i = radio_change_charger(diff);
            str = params_charger_str_get((radio_charger_t)i);
            if(show) msg_update_text_fmt("#%3X Charger: %s", color, str);
            knobs_update_mfk("Charger: %s", str);

            if (diff) {
                voice_say_text("Charger mode", str);
            } else if (voice) {
                voice_say_text_fmt("Charger mode selector");
            }
            break;

        case MFK_ANT:
            {
                int32_t ant = subject_get_int(cfg.ant_id.val);
                if (diff != 0) {
                    ant = limit(ant + diff, 1, 5);
                    subject_set_int(cfg.ant_id.val, ant);
                    // radio_load_atu();
                    // info_atu_update();
                }
                if(show) msg_update_text_fmt("#%3X Antenna : %i", color, ant);
                knobs_update_mfk("Antenna : %i", ant);

                if (diff) {
                    voice_say_int("Antenna", ant);
                } else if (voice) {
                    voice_say_text_fmt("Antenna selector");
                }
            }
            break;

        case MFK_RIT:
            i = radio_change_rit(diff);
            if(show) msg_update_text_fmt("#%3X RIT: %c%i", color, i < 0 ? '-' : '+', abs(i));
            knobs_update_mfk("RIT: %c%i", i < 0 ? '-' : '+', abs(i));

            if (diff) {
                voice_say_int("RIT", i);
            } else if (voice) {
                voice_say_text_fmt("RIT");
            }
            break;

        case MFK_XIT:
            i = radio_change_xit(diff);
            if(show) msg_update_text_fmt("#%3X XIT: %c%i", color, i < 0 ? '-' : '+', abs(i));
            knobs_update_mfk("XIT: %c%i", i < 0 ? '-' : '+', abs(i));

            if (diff) {
                voice_say_int("XIT", i);
            } else if (voice) {
                voice_say_text_fmt("XIT");
            }
            break;

        case MFK_DNF:
            // b = radio_change_dnf(diff);
            b = subject_get_int(cfg.dnf.val);
            if (diff) {
                b = !b;
                subject_set_int(cfg.dnf.val, b);
            }
            if(show) msg_update_text_fmt("#%3X DNF: %s", color, b ? "On" : "Off");
            knobs_update_mfk("DNF: %s", b ? "On" : "Off");

            if (diff) {
                voice_say_bool("DNF", b);
            } else if (voice) {
                voice_say_text_fmt("DNF switcher");
            }
            break;

        case MFK_DNF_CENTER:
            // i = radio_change_dnf_center(diff);
            i = subject_get_int(cfg.dnf_center.val);
            if (diff) {
                i = limit(i + diff * 50, 100, 3000);
                subject_set_int(cfg.dnf_center.val, i);
            }
            if(show) msg_update_text_fmt("#%3X DNF center: %i Hz", color, i);
            knobs_update_mfk("DNF center: %i Hz", i);

            if (diff) {
                voice_say_int("DNF center frequency", i);
            } else if (voice) {
                voice_say_text_fmt("DNF center frequency");
            }
            break;

        case MFK_DNF_WIDTH:
            // i = radio_change_dnf_width(diff);
            i = subject_get_int(cfg.dnf_width.val);
            if (diff) {
                i = limit(i + diff * 5, 10, 100);
                subject_set_int(cfg.dnf_width.val, i);
            }
            if(show) msg_update_text_fmt("#%3X DNF width: %i Hz", color, i);
            knobs_update_mfk("DNF width: %i Hz", i);

            if (diff) {
                voice_say_int("DNF width", i);
            } else if (voice) {
                voice_say_text_fmt("DNF width");
            }
            break;

        case MFK_NB:
            // b = radio_change_nb(diff);
            b = subject_get_int(cfg.nb.val);
            if (diff) {
                b = !b;
                subject_set_int(cfg.nb.val, b);
            }
            if(show) msg_update_text_fmt("#%3X NB: %s", color, b ? "On" : "Off");
            knobs_update_mfk("NB: %s", b ? "On" : "Off");

            if (diff) {
                voice_say_bool("NB", b);
            } else if (voice) {
                voice_say_text_fmt("NB switcher");
            }
            break;

        case MFK_NB_LEVEL:
            // i = radio_change_nb_level(diff);
            // limit(params.nb_level + d * 5, 0, 100);
            i = subject_get_int(cfg.nb_level.val);
            if (diff) {
                i = limit(i + diff * 5, 0, 100);
                subject_set_int(cfg.nb_level.val, i);
            }
            if(show) msg_update_text_fmt("#%3X NB level: %i", color, i);
            knobs_update_mfk("NB level: %i", i);

            if (diff) {
                voice_say_int("NB level", i);
            } else if (voice) {
                voice_say_text_fmt("NB level");
            }
            break;

        case MFK_NB_WIDTH:
            // i = radio_change_nb_width(diff);
            // limit(params.nb_width + d * 5, 0, 100);
            i = subject_get_int(cfg.nb_width.val);
            if (diff) {
                i = limit(i + diff * 5, 0, 100);
                subject_set_int(cfg.nb_width.val, i);
            }
            if(show) msg_update_text_fmt("#%3X NB width: %i Hz", color, i);
            knobs_update_mfk("NB width: %i Hz", i);

            if (diff) {
                voice_say_int("NB width", i);
            } else if (voice) {
                voice_say_text_fmt("NB width");
            }
            break;

        case MFK_NR:
            // b = radio_change_nr(diff);
            b = subject_get_int(cfg.nr.val);
            if (diff) {
                b = !b;
                subject_set_int(cfg.nr.val, b);
            }
            if(show) msg_update_text_fmt("#%3X NR: %s", color, b ? "On" : "Off");
            knobs_update_mfk("NR: %s", b ? "On" : "Off");

            if (diff) {
                voice_say_bool("NR", b);
            } else if (voice) {
                voice_say_text_fmt("NR switcher");
            }
            break;

        case MFK_NR_LEVEL:
            // i = radio_change_nr_level(diff);
            // limit(params.nr_level + d * 5, 0, 60);
            i = subject_get_int(cfg.nr_level.val);
            if (diff) {
                i = limit(i + diff * 5, 0, 60);
                subject_set_int(cfg.nr_level.val, i);
            }
            if(show) msg_update_text_fmt("#%3X NR level: %i", color, i);
            knobs_update_mfk("NR level: %i", i);

            if (diff) {
                voice_say_int("NR level", i);
            } else if (voice) {
                voice_say_text_fmt("NR level");
            }
            break;

        case MFK_AGC_HANG:
            // b = radio_change_agc_hang(diff);
            b = subject_get_int(cfg.agc_hang.val);
            if (diff) {
                b = !b;
                subject_set_int(cfg.agc_hang.val, b);
            }
            if(show) msg_update_text_fmt("#%3X AGC hang: %s", color, b ? "On" : "Off");
            knobs_update_mfk("AGC hang: %s", b ? "On" : "Off");

            if (diff) {
                voice_say_bool("Auto gain hang", b);
            } else if (voice) {
                voice_say_text_fmt("Auto gain hang switcher");
            }
            break;

        case MFK_AGC_KNEE:
            // limit(params.agc_knee + d, -100, 0);
            // i = radio_change_agc_knee(diff);
            i = subject_get_int(cfg.agc_knee.val);
            if (diff) {
                i = limit(i + diff, -100, 0);
                subject_set_int(cfg.agc_knee.val, i);
            }
            if(show) msg_update_text_fmt("#%3X AGC knee: %i dB", color, i);
            knobs_update_mfk("AGC knee: %i dB", i);

            if (diff) {
                voice_say_int("Auto gain knee level", i);
            } else if (voice) {
                voice_say_text_fmt("Auto gain knee level");
            }
            break;

        case MFK_AGC_SLOPE:
            // limit(params.agc_slope + d, 0, 10);
            // i = radio_change_agc_slope(diff);
            i = subject_get_int(cfg.agc_slope.val);
            if (diff) {
                i = limit(i + diff, 0, 10);
                subject_set_int(cfg.agc_slope.val, i);
            }
            if(show) msg_update_text_fmt("#%3X AGC slope: %i dB", color, i);
            knobs_update_mfk("AGC slope: %i dB", i);

            if (diff) {
                voice_say_int("Auto gain slope level", i);
            } else if (voice) {
                voice_say_text_fmt("Auto gain slope level");
            }
            break;

        case MFK_CW_DECODER:
            // b = cw_change_decoder(diff);
            b = subject_get_int(cfg.cw_decoder.val);
            if (diff) {
                b = !b;
                subject_set_int(cfg.cw_decoder.val, b);
            }
            if(show) msg_update_text_fmt("#%3X CW decoder: %s", color, b ? "On" : "Off");
            knobs_update_mfk("CW decoder: %s", b ? "On" : "Off");

            if (diff) {
                voice_say_bool("CW decoder", b);
            } else if (voice) {
                voice_say_text_fmt("CW decoder switcher");
            }
            break;

        case MFK_CW_TUNE:
            // b = cw_tune_toggle(diff);
            b = subject_get_int(cfg.cw_tune.val);
            if (diff) {
                b = !b;
                subject_set_int(cfg.cw_tune.val, b);
            }
            if(show) msg_update_text_fmt("#%3X CW tune: %s", color, b ? "On" : "Off");
            knobs_update_mfk("CW tune: %s", b ? "On" : "Off");

            if (diff) {
                voice_say_bool("CW tune", b);
            } else if (voice) {
                voice_say_text_fmt("CW tune switcher");
            }
            break;

        case MFK_CW_DECODER_SNR:
            // f = cw_change_snr(diff);
            f = subject_get_float(cfg.cw_decoder_snr.val);
            if (diff) {
                f = clip(f + diff * 0.1f, 3.0f, 30.0f);
                subject_set_float(cfg.cw_decoder_snr.val, f);
            }
            if(show) msg_update_text_fmt("#%3X CW decoder SNR: %.1f dB", color, f);
            knobs_update_mfk("CW decoder SNR: %.1f dB", f);

            if (diff) {
                voice_say_float("CW decoder SNR level", f);
            } else if (voice) {
                voice_say_text_fmt("CW decoder SNR level");
            }
            break;

        case MFK_CW_DECODER_PEAK_BETA:
            f = subject_get_float(cfg.cw_decoder_peak_beta.val);
            if (diff) {
                f = clip(f + diff * 0.01f, 0.1f, 0.95f);
                subject_set_float(cfg.cw_decoder_peak_beta.val, f);
            }
            // f = cw_change_peak_beta(diff);
            if(show) msg_update_text_fmt("#%3X CW decoder peak beta: %.2f", color, f);
            knobs_update_mfk("CW decoder peak beta: %.2f", f);

            if (diff) {
                voice_say_float("CW decoder peak beta", f);
            } else if (voice) {
                voice_say_text_fmt("CW decoder peak beta");
            }
            break;

        case MFK_CW_DECODER_NOISE_BETA:
            f = subject_get_float(cfg.cw_decoder_noise_beta.val);
            if (diff) {
                f = clip(f + diff * 0.01f, 0.1f, 0.95f);
                subject_set_float(cfg.cw_decoder_noise_beta.val, f);
            }
            // f = cw_change_noise_beta(diff);
            if(show) msg_update_text_fmt("#%3X CW decoder noise beta: %.2f", color, f);
            knobs_update_mfk("CW decoder noise beta: %.2f", f);

            if (diff) {
                voice_say_float("CW decoder noise beta", f);
            } else if (voice) {
                voice_say_text_fmt("CW decoder noise beta");
            }
            break;

        case MFK_RTTY_RATE:
            f = rtty_change_rate(diff);
            if(show) msg_update_text_fmt("#%3X RTTY rate: %.2f", color, f);
            knobs_update_mfk("RTTY rate: %.2f", f);

            if (diff) {
                voice_say_float2("Teletype rate", f);
            } else if (voice) {
                voice_say_text_fmt("Teletype rate");
            }
            break;

        case MFK_RTTY_SHIFT:
            i = rtty_change_shift(diff);
            if(show) msg_update_text_fmt("#%3X RTTY shift: %i Hz", color, i);
            knobs_update_mfk("RTTY shift: %i Hz", i);

            if (diff) {
                voice_say_int("Teletype frequency shift", i);
            } else if (voice) {
                voice_say_text_fmt("Teletype frequency shift");
            }
            break;

        case MFK_RTTY_CENTER:
            i = rtty_change_center(diff);
            if(show) msg_update_text_fmt("#%3X RTTY center: %i Hz", color, i);
            knobs_update_mfk("RTTY center: %i Hz", i);

            if (diff) {
                voice_say_int("Teletype frequency center", i);
            } else if (voice) {
                voice_say_text_fmt("Teletype frequency center");
            }
            break;

        case MFK_RTTY_REVERSE:
            b = rtty_change_reverse(diff);
            if(show) msg_update_text_fmt("#%3X RTTY reverse: %s", color, b ? "On" : "Off");
            knobs_update_mfk("RTTY reverse: %s", b ? "On" : "Off");

            if (diff) {
                voice_say_bool("Teletype reverse", b);
            } else if (voice) {
                voice_say_text_fmt("Teletype reverse switcher");
            }
            break;

        default:
            break;
    }
}

void mfk_change_mode(int16_t dir) {
    mfk_mode = (mfk_mode_t)loop_modes(dir, mfk_mode, params.mfk_modes, MFK_LAST-1);
    mfk_update(0, true, true);
}

void mfk_set_mode(mfk_mode_t mode) {
    mfk_mode = mode;
    mfk_state = MFK_STATE_EDIT;
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

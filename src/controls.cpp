#include "controls.h"

#include <map>

#include "cfg/settings_manager.h"
#include "util.hpp"
#include "util.h"
#include "cw.h"
#include "voice.h"


extern "C" {
#include "msg.h"
#include "rtty.h"
}

template <typename T>
static inline bool toggle_param(Parameter<T> &param);

template <typename T, typename U, int N>
static T update_param(Parameter<T, U, N> &param, T diff, T min, T max);

template <typename T>
static T loop_items(std::vector<T> items, T cur, bool next);


static std::map<cfg_ctrl_t, std::string> control_name_voice{
    {CTRL_VOL, "Audio level"},
    {CTRL_RFG, "RF gain"},
    {CTRL_SQL, "Squelch level"},
    {CTRL_FILTER_LOW, "Low filter limit"},
    {CTRL_FILTER_HIGH, "High filter limit"},
    {CTRL_FILTER_BW, "Bandwidth filter limit"},
    {CTRL_PWR, "Transmit power"},
    {CTRL_MIC, "Mic selector"},
    {CTRL_HMIC, "Hand microphone gain"},
    {CTRL_IMIC, "Internal microphone gain"},
    {CTRL_MONI, "Monitor level"},

    {CTRL_SPECTRUM_FACTOR, "Zoom level"},
    {CTRL_COMP, "Compressor ratio"},
    {CTRL_VOX_ON, "VOX switcher"},
    {CTRL_VOX_GAIN, "VOX gain"},
    {CTRL_VOX_AG, "VOX anti gain"},
    {CTRL_VOX_DELAY, "VOX delay"},
    {CTRL_KEY_SPEED, "CW key speed"},
    {CTRL_KEY_MODE, "CW key mode selector"},
    {CTRL_IAMBIC_MODE, "Iambic mode selector"},
    {CTRL_KEY_TONE, "CW key tone"},
    {CTRL_KEY_VOL, "CW key volume level"},
    {CTRL_KEY_TRAIN, "CW key train switcher"},
    {CTRL_QSK_TIME, "CW key QSK time"},
    {CTRL_KEY_RATIO, "CW key ratio"},
    {CTRL_ANT, "Antenna selector"},
    {CTRL_RIT, "RIT"},
    {CTRL_XIT, "XIT"},
    {CTRL_DNF, "DNF switcher"},
    {CTRL_DNF_CENTER, "DNF center frequency"},
    {CTRL_DNF_WIDTH, "DNF width"},
    {CTRL_DNF_AUTO, "DNF auto switcher"},
    {CTRL_NB, "NB switcher"},
    {CTRL_NB_LEVEL, "NB level"},
    {CTRL_NB_WIDTH, "NB width"},
    {CTRL_NR, "NR switcher"},
    {CTRL_NR_LEVEL, "NR level"},
    {CTRL_AGC_HANG, "Auto gain hang switcher"},
    {CTRL_AGC_KNEE, "Auto gain knee level"},
    {CTRL_AGC_SLOPE, "Auto gain slope level"},
    {CTRL_CW_DECODER, "CW decoder switcher"},
    {CTRL_CW_TUNE, "CW tune switcher"},
    {CTRL_CW_DECODER_SNR, "CW decoder SNR level"},
    {CTRL_CW_DECODER_PEAK_BETA, "CW decoder peak beta"},
    {CTRL_CW_DECODER_NOISE_BETA, "CW decoder noise beta"},
    {CTRL_RTTY_RATE, "Teletype rate"},
    {CTRL_RTTY_SHIFT, "Teletype frequency shift"},
    {CTRL_RTTY_CENTER, "Teletype frequency center"},
    {CTRL_RTTY_REVERSE, "Teletype reverse switcher"},
    {CTRL_IF_SHIFT, "IF shift control"},
    {CTRL_CW_PEAK_ON, "CW peak switcher"},
    {CTRL_CW_PEAK_Q, "CW peak Q"},
    {CTRL_CW_ZAP, "CW zap"},
};

void control_name_say(cfg_ctrl_t ctrl) {
    auto item = control_name_voice.find(ctrl);
    if (item != control_name_voice.end()) {
        voice_say_text_fmt(item->second.c_str());
    } else {
        LV_LOG_ERROR("Ctrl %d has no voice", ctrl);
    }
}

void controls_toggle_agc_hang(button_data_t *data) {
    bool new_val = toggle_param(cfg_sm.p_agc_hang);
    voice_say_bool("Auto gain hang", new_val);
}

void controls_toggle_key_train(button_data_t *data) {
    bool new_val = toggle_param(cfg_sm.p_key_train);
    voice_say_bool("CW key train", new_val);
}

void controls_toggle_key_iambic_mode(button_data_t *data) {
    x6100_iambic_mode_t new_mode = cfg_sm.p_iambic_mode.get() == x6100_iambic_a ? x6100_iambic_b : x6100_iambic_a;
    cfg_sm.p_iambic_mode.set( new_mode);
    char *str = params_iambic_mode_str_ger(new_mode);
    voice_say_text("Iambic mode", str);
}

void controls_toggle_cw_decoder(button_data_t *data) {
    bool new_val = toggle_param(cfg_sm.p_cw_decoder);
    voice_say_bool("CW Decoder", new_val);
}

void controls_toggle_cw_tuner(button_data_t *data) {
    bool new_val = toggle_param(cfg_sm.p_cw_tune);
    voice_say_bool("CW Decoder", new_val);
}

void controls_toggle_cw_peak(button_data_t *data) {
    bool new_val = toggle_param(cfg_sm.p_cw_peak_on);
    voice_say_bool("CW Peak", new_val);
}

void controls_toggle_dnf(button_data_t *data) {
    bool new_val = toggle_param(cfg_sm.p_dnf);
    voice_say_bool("DNF", new_val);
}

void controls_toggle_dnf_auto(button_data_t *data) {
    bool new_val = toggle_param(cfg_sm.p_dnf_auto);
    voice_say_bool("DNF auto", new_val);
}

void controls_toggle_nb(button_data_t *data) {
    bool new_val = toggle_param(cfg_sm.p_nb);
    voice_say_bool("NB", new_val);
}

void controls_toggle_nr(button_data_t *data) {
    bool new_val = toggle_param(cfg_sm.p_nr);
    voice_say_bool("NR", new_val);
}

void controls_toggle_vox(button_data_t *data) {
    bool new_val = toggle_param(cfg_sm.p_vox_en);
    voice_say_bool("VOX", new_val);
}

void controls_cw_zap(button_data_t *data) {
    x6100_mode_t mode = (x6100_mode_t)(cfg_sm.cp_cur_mode.get());
    if ((mode != x6100_mode_cw) && (mode != x6100_mode_cwr)) {
        return;
    }
    float tone_freq = cw_get_tone_freq();
    int32_t key_tone = cfg_sm.p_key_tone.get();
    int32_t freq = cfg_sm.cp_fg_freq.get();
    LV_LOG_USER("tone_freq: %f, key_tone: %i", tone_freq, key_tone);
    if (mode == x6100_mode_cw) {
        freq += tone_freq - key_tone;
    } else {
        freq -= tone_freq - key_tone;
    }
    freq = (freq + 5) / 10 * 10;
    cfg_sm.cp_fg_freq.set(freq);
}

void controls_encoder_update(cfg_ctrl_t ctrl, int32_t diff, std::string &msg) {
    int32_t     i;
    float       f;
    char        *s;
    bool        b;
    int32_t     new_msg_len;

    switch (ctrl) {
        case CTRL_VOL:
            i = radio_change_vol(diff);
            snprintf(msg.data(), msg.capacity(), "Volume: %i", i);

            if (diff) {
                voice_say_int("Audio level", i);
            }
            break;

        case CTRL_RFG:
            i = update_param(cfg_sm.p_rfgain, diff, 0, 100);
            snprintf(msg.data(), msg.capacity(), "RF gain: %i", i);

            if (diff) {
                voice_say_int("RF gain", i);
            }
            break;

        case CTRL_SQL:
            i = update_param(cfg_sm.p_squelch, diff, 0, 100);
            snprintf(msg.data(), msg.capacity(), "Voice SQL: %i", i);

            if (diff) {
                voice_say_int("Squelch level", i);
            }
            break;

        case CTRL_FILTER_LOW:
            i = cfg_sm.cp_cur_filter_low.get();
            if (diff) {
                // TODO: make step depending on freq
                i = align_int(i + diff * 10, 10);
                cfg_sm.cp_cur_filter_low.set(i);
                i = cfg_sm.cp_cur_filter_low.get();
            }
            snprintf(msg.data(), msg.capacity(), "Filter low: %i Hz", i);

            if (diff) {
                voice_say_int("Low filter limit", i);
            }
            break;

        case CTRL_FILTER_HIGH:
            i = cfg_sm.cp_cur_filter_high.get();
            if (diff) {
                uint8_t freq_step;
                switch (cfg_sm.cp_cur_mode.get()) {
                case x6100_mode_cw:
                case x6100_mode_cwr:
                    freq_step = 10;
                    break;
                default:
                    freq_step = 50;
                    break;
                }
                i = align_int(i + diff * freq_step, freq_step);
                cfg_sm.cp_cur_filter_high.set(i);
                i = cfg_sm.cp_cur_filter_high.get();
            }

            snprintf(msg.data(), msg.capacity(), "Filter high: %i Hz", i);

            if (diff) {
                voice_say_int("High filter limit", i);
            }
            break;

        case CTRL_FILTER_BW:
            {
                int32_t bw = cfg_sm.cp_cur_filter_bw.get();
                if (diff) {
                    bw = align_int(bw + diff * 20, 20);
                    cfg_sm.cp_cur_filter_bw.set(bw);
                    bw = cfg_sm.cp_cur_filter_bw.get();
                }
                snprintf(msg.data(), msg.capacity(), "Filter bw: %i Hz", bw);

                if (diff) {
                    voice_say_int("Filter bandwidth", bw);
                }
            }
            break;

        case CTRL_PWR:
            f = update_param(cfg_sm.p_pwr, 0.1f * diff, 0.0f, 10.0f);
            snprintf(msg.data(), msg.capacity(), "Power: %0.1f W", f);

            if (diff) {
                voice_say_float("Transmit power", f);
            }
            break;

        case CTRL_MIC:
            i = cfg_sm.p_mic.get();
            // i range should be 0..2
            i = (i + diff + 3) % 3;
            cfg_sm.p_mic.set(i);
            s = params_mic_str_get((x6100_mic_sel_t)i);
            snprintf(msg.data(), msg.capacity(), "MIC: %s", s);

            if (diff) {
                voice_say_text("Mic selector", s);
            }
            break;

        case CTRL_HMIC:
            i = update_param(cfg_sm.p_hmic, diff, 0, 50);
            snprintf(msg.data(), msg.capacity(), "H-MIC gain: %i", i);

            if (diff) {
                voice_say_int("Hand microphone gain", i);
            }
            break;

        case CTRL_IMIC:
            i = update_param(cfg_sm.p_imic, diff, 0, 35);
            snprintf(msg.data(), msg.capacity(), "I-MIC gain: %i", i);

            if (diff) {
                voice_say_int("Internal microphone gain", i);
            }
            break;

        case CTRL_MONI:
            i = update_param(cfg_sm.p_moni, diff, 0, 100);
            snprintf(msg.data(), msg.capacity(), "Moni level: %i", i);

            if (diff) {
                voice_say_int("Monitor level", i);
            }
            break;

        case CTRL_SPECTRUM_FACTOR:
            i = cfg_sm.p_mode_zoom.get();
            if (diff != 0) {
                if (diff > 0) {
                    i <<= diff;
                } else {
                    i >>= -diff;
                }
                i = clip(i, 1, 8);
                cfg_sm.p_mode_zoom.set(i);
            }
            snprintf(msg.data(), msg.capacity(), "Zoom: x%i", i);

            if (diff) {
                voice_say_int("Zoom", i);
            }
            break;

        case CTRL_COMP:
            i = update_param(cfg_sm.p_comp, diff, 1, 8);
            snprintf(msg.data(), msg.capacity(), "Compressor ratio: %s", params_comp_str_get(i));

            if (diff) {
                if (i > 1) {
                    voice_say_text_fmt("Compressor ratio|%d to 1", i);
                } else {
                    voice_say_text_fmt("Compressor disabled");
                }
            }
            break;

        case CTRL_VOX_ON:
            b = cfg_sm.p_vox_en.get();
            if (diff) {
                b = !b;
                cfg_sm.p_vox_en.set(b);
            }
            snprintf(msg.data(), msg.capacity(), "VOX: %s", (b ? "On" : "Off"));

            if (diff) {
                voice_say_bool("VOX", b);
            }
            break;

        case CTRL_VOX_GAIN:
            i = update_param(cfg_sm.p_vox_gain, diff, 0, 100);
            snprintf(msg.data(), msg.capacity(), "VOX gain: %i", i);

            if (diff) {
                voice_say_int("VOX gain", i);
            }
            break;
        case CTRL_VOX_AG:
            i = update_param(cfg_sm.p_vox_ag, diff, 0, 100);
            snprintf(msg.data(), msg.capacity(), "VOX anti-gain: %i", i);

            if (diff) {
                voice_say_int("VOX anti gain", i);
            }
            break;
        case CTRL_VOX_DELAY:
            i = update_param(cfg_sm.p_vox_delay, diff * 50, 100, 2000);
            snprintf(msg.data(), msg.capacity(), "VOX delay: %i ms", i);

            if (diff) {
                voice_say_int("VOX delay", i);
            }

            break;

        case CTRL_KEY_SPEED:
            i = update_param(cfg_sm.p_key_speed, diff, 5, 50);
            snprintf(msg.data(), msg.capacity(), "Key speed: %i wpm", i);

            if (diff) {
                voice_say_int("CW key speed", i);
            }
            break;

        case CTRL_KEY_MODE:
            i = cfg_sm.p_key_mode.get();
            if (diff) {
                i = loop_items({x6100_key_manual, x6100_key_auto_left, x6100_key_auto_right}, (x6100_key_mode_t)i, diff > 0);
                cfg_sm.p_key_mode.set(i);
            }
            s = params_key_mode_str_get((x6100_key_mode_t)i);
            snprintf(msg.data(), msg.capacity(), "Key mode: %s", s);

            if (diff) {
                voice_say_text("CW key mode", s);
            }
            break;

        case CTRL_IAMBIC_MODE:
            i = cfg_sm.p_iambic_mode.get();
            if (diff) {
                i = loop_items({x6100_iambic_a, x6100_iambic_b}, (x6100_iambic_mode_t)i, diff > 0);
                cfg_sm.p_iambic_mode.set(i);
            }
            s = params_iambic_mode_str_ger((x6100_iambic_mode_t)i);
            snprintf(msg.data(), msg.capacity(), "Iambic mode: %s", s);

            if (diff) {
                voice_say_text("Iambic mode", s);
            }
            break;

        case CTRL_KEY_TONE:
            i = update_param(cfg_sm.p_key_tone, diff * 10, 400, 1200);
            snprintf(msg.data(), msg.capacity(), "Key tone: %i Hz", i);

            if (diff) {
                voice_say_int("CW key tone", i);
            }
            break;

        case CTRL_KEY_VOL:
            i = update_param(cfg_sm.p_key_vol, diff, 0, 32);
            snprintf(msg.data(), msg.capacity(), "Key volume: %i", i);

            if (diff) {
                voice_say_int("CW key volume level", i);
            }
            break;

        case CTRL_KEY_TRAIN:
            b = cfg_sm.p_key_train.get();
            if (diff) {
                b = !b;
                cfg_sm.p_key_train.set(b);
            }
            snprintf(msg.data(), msg.capacity(), "Key train: %s", (b ? "On" : "Off"));

            if (diff) {
                voice_say_bool("CW key train", b);
            }
            break;

        case CTRL_QSK_TIME:
            i = update_param(cfg_sm.p_qsk_time, diff * 10, 0, 1000);
            snprintf(msg.data(), msg.capacity(), "QSK time: %i ms", i);

            if (diff) {
                voice_say_int("CW key QSK time", i);
            }
            break;

        case CTRL_KEY_RATIO:
            f = update_param(cfg_sm.p_key_ratio, diff * 0.1f, 2.5f, 4.5f);
            snprintf(msg.data(), msg.capacity(), "Key ratio: %0.1f", f);

            if (diff) {
                voice_say_float("CW key ratio", f);
            }
            break;

        case CTRL_CW_PEAK_ON:
            b = cfg_sm.p_cw_peak_on.get();
            if (diff) {
                b = !b;
                cfg_sm.p_cw_peak_on.set(b);
            }
            snprintf(msg.data(), msg.capacity(), "CW peak: %s", (b ? "On" : "Off"));

            if (diff) {
                voice_say_bool("CW peak", b);
            }
            break;

        case CTRL_CW_PEAK_Q:
            i = update_param(cfg_sm.p_cw_peak_q, diff, 1, 16);
            snprintf(msg.data(), msg.capacity(), "CW peak Q: %i", i);

            if (diff) {
                voice_say_int("CW peak Q", i);
            }
            break;

        case CTRL_ANT:
            i = update_param(cfg_sm.p_ant_id, diff, 0, 5);
            snprintf(msg.data(), msg.capacity(), "Antenna: %i", i);

            if (diff) {
                voice_say_int("Antenna", i);
            }
            break;

        case CTRL_RIT:
            i = cfg_sm.p_rit.get();
            if (diff) {
                i = clip(align(i + diff * 10, 10), -1500, +1500);
                cfg_sm.p_rit.set(i);
            }
            snprintf(msg.data(), msg.capacity(), "RIT: %c%i", (i < 0 ? '-' : '+'), abs(i));

            if (diff) {
                voice_say_int("RIT", i);
            }
            break;

        case CTRL_XIT:
            i = cfg_sm.p_xit.get();
            if (diff) {
                i = clip(align(i + diff * 10, 10), -1500, +1500);
                cfg_sm.p_xit.set(i);
            }
            snprintf(msg.data(), msg.capacity(), "XIT: %c%i", (i < 0 ? '-' : '+'), abs(i));

            if (diff) {
                voice_say_int("XIT", i);
            }
            break;

        case CTRL_DNF:
            b = cfg_sm.p_dnf.get();
            if (diff) {
                b = !b;
                cfg_sm.p_dnf.set(b);
            }
            snprintf(msg.data(), msg.capacity(), "DNF: %s", (b ? "On" : "Off"));

            if (diff) {
                voice_say_bool("DNF", b);
            }
            break;

        case CTRL_DNF_CENTER:
            i = update_param(cfg_sm.p_dnf_center, diff * 50, 100, 3000);
            snprintf(msg.data(), msg.capacity(), "DNF center: %i Hz", i);

            if (diff) {
                voice_say_int("DNF center frequency", i);
            }
            break;

        case CTRL_DNF_WIDTH:
            i = update_param(cfg_sm.p_dnf_width, diff * 5, 10, 100);
            snprintf(msg.data(), msg.capacity(), "DNF width: %i Hz", i);

            if (diff) {
                voice_say_int("DNF width", i);
            }
            break;

        case CTRL_DNF_AUTO:
            b = cfg_sm.p_dnf_auto.get();
            if (diff) {
                b = !b;
                cfg_sm.p_dnf_auto.set(b);
            }
            snprintf(msg.data(), msg.capacity(), "DNF auto: %s", (b ? "On" : "Off"));

            if (diff) {
                voice_say_bool("DNF auto", b);
            }
            break;

        case CTRL_NB:
            b = cfg_sm.p_nb.get();
            if (diff) {
                b = !b;
                cfg_sm.p_nb.set(b);
            }
            snprintf(msg.data(), msg.capacity(), "NB: %s", (b ? "On" : "Off"));

            if (diff) {
                voice_say_bool("NB", b);
            }
            break;

        case CTRL_NB_LEVEL:
            i = update_param(cfg_sm.p_nb_level, diff * 5, 0, 100);
            snprintf(msg.data(), msg.capacity(), "NB level: %i", i);

            if (diff) {
                voice_say_int("NB level", i);
            }
            break;

        case CTRL_NB_WIDTH:
            i = update_param(cfg_sm.p_nb_width, diff * 5, 0, 100);
            snprintf(msg.data(), msg.capacity(), "NB width: %i", i);

            if (diff) {
                voice_say_int("NB width", i);
            }
            break;

        case CTRL_NR:
            b = cfg_sm.p_nr.get();
            if (diff) {
                b = !b;
                cfg_sm.p_nr.set(b);
            }
            snprintf(msg.data(), msg.capacity(), "NR: %s", (b ? "On" : "Off"));

            if (diff) {
                voice_say_bool("NR", b);
            }
            break;

        case CTRL_NR_LEVEL:
            i = update_param(cfg_sm.p_nr_level, diff * 5, 0, 60);
            snprintf(msg.data(), msg.capacity(), "NR level: %i", i);

            if (diff) {
                voice_say_int("NR level", i);
            }
            break;

        case CTRL_AGC_HANG:
            b = cfg_sm.p_agc_hang.get();
            if (diff) {
                b = !b;
                cfg_sm.p_agc_hang.set(b);
            }
            snprintf(msg.data(), msg.capacity(), "AGC hang: %s", (b ? "On" : "Off"));

            if (diff) {
                voice_say_bool("Auto gain hang", b);
            }
            break;

        case CTRL_AGC_KNEE:
            i = update_param(cfg_sm.p_agc_knee, diff, -100, 0);
            snprintf(msg.data(), msg.capacity(), "AGC knee: %i dB", i);

            if (diff) {
                voice_say_int("Auto gain knee level", i);
            }
            break;

        case CTRL_AGC_SLOPE:
            i = update_param(cfg_sm.p_agc_slope, diff, 0, 10);
            snprintf(msg.data(), msg.capacity(), "AGC slope: %i dB", i);

            if (diff) {
                voice_say_int("Auto gain slope level", i);
            }
            break;

        case CTRL_CW_DECODER:
            b = cfg_sm.p_cw_decoder.get();
            if (diff) {
                b = !b;
                cfg_sm.p_cw_decoder.set(b);
            }
            snprintf(msg.data(), msg.capacity(), "CW decoder: %s", (b ? "On" : "Off"));

            if (diff) {
                voice_say_bool("CW decoder", b);
            }
            break;

        case CTRL_CW_TUNE:
            b = cfg_sm.p_cw_tune.get();
            if (diff) {
                b = !b;
                cfg_sm.p_cw_tune.set(b);
            }
            snprintf(msg.data(), msg.capacity(), "CW tune: %s", (b ? "On" : "Off"));

            if (diff) {
                voice_say_bool("CW tune", b);
            }
            break;

        case CTRL_CW_DECODER_SNR:
            f = update_param(cfg_sm.p_cw_decoder_snr, diff * 0.1f, 3.0f, 30.0f);
            snprintf(msg.data(), msg.capacity(), "CW decoder SNR: %0.1f dB", f);

            if (diff) {
                voice_say_float("CW decoder SNR level", f);
            }
            break;

        case CTRL_RTTY_RATE:
            f = rtty_change_rate(diff);
            snprintf(msg.data(), msg.capacity(), "RTTY rate: %0.2f", f);

            if (diff) {
                voice_say_float2("Teletype rate", f);
            }
            break;

        case CTRL_RTTY_SHIFT:
            i = rtty_change_shift(diff);
            snprintf(msg.data(), msg.capacity(), "RTTY shift: %i Hz", i);

            if (diff) {
                voice_say_int("Teletype frequency shift", i);
            }
            break;

        case CTRL_RTTY_CENTER:
            i = rtty_change_center(diff);
            snprintf(msg.data(), msg.capacity(), "RTTY center: %i Hz", i);

            if (diff) {
                voice_say_int("Teletype frequency center", i);
            }
            break;

        case CTRL_RTTY_REVERSE:
            b = rtty_change_reverse(diff);
            snprintf(msg.data(), msg.capacity(), "RTTY reverse: %s", (b ? "On" : "Off"));

            if (diff) {
                voice_say_bool("Teletype reverse", b);
            }
            break;

        case CTRL_IF_SHIFT:
            {
                x6100_base_ver_t base_ver = x6100_control_get_base_ver();
                if ((util_compare_version(base_ver, (x6100_base_ver_t){1, 1, 9, 0}) >= 0) || (base_ver.rev >= 8)) {
                    i = update_param(cfg_sm.p_band_if_shift, diff * 100, -40000, 40000);
                    snprintf(msg.data(), msg.capacity(), "IF shift: %i Hz", i);

                    if (diff) {
                        voice_say_int("IF shift", i);
                    }
                } else {
                    msg_update_text_fmt("IF shift is not supported");
                }
            }
            break;

        default:
            return;
    }
}

cfg_ctrl_t controls_encoder_get_next(encoder_binds_t encoder, cfg_ctrl_t current, int16_t dir) {
    std::string binds = cfg_sm.p_encoder_bind.get();
    size_t n_binds = binds.size();
    // Initialization
    if (dir == 0) {
        if (current < n_binds) {
            if (binds[current] == encoder) {
                return current;
            }
        }
        dir = 1;
    }
    cfg_ctrl_t new_ctrl;
    if (dir > 0) {
        for (size_t i = current + 1; i < current + 1 + n_binds; i++)
        {
            new_ctrl = (cfg_ctrl_t)(i % n_binds);
            if (binds[new_ctrl] == encoder) {
                return new_ctrl;
            }
        }
    } else {
        for (size_t i = 2 * n_binds + current - 1; i > n_binds + current - 1; i--)
        {
            new_ctrl = (cfg_ctrl_t)(i % n_binds);
            if (binds[new_ctrl] == encoder) {
                return new_ctrl;
            }
        }
    }
    LV_LOG_ERROR("No next/prev control for %c", encoder);
    return current;
}

template <typename T>
static inline bool toggle_param(Parameter<T> &param) {
    bool new_val = !param.get();
    param.set(new_val);
    return new_val;
}

template <typename T>
static T loop_items(std::vector<T> items, T cur, bool next) {
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


template <typename T, typename U, int N>
static T update_param(Parameter<T, U, N> &param, T diff, T min, T max) {
    T val = param.get();
    val = clip(val + diff, min, max);
    param.set(val);
    return val;
}

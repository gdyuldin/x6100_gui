#include "controls.h"

#include <map>

#include "cfg/subjects.h"
#include "util.hpp"
#include "util.h"


extern "C" {
#include "cfg/cfg.h"
#include "cfg/mode.h"
#include "msg.h"
#include "voice.h"
#include "rtty.h"
}

static inline bool toggle_subj(Subject *subj);

template <typename T>
static T loop_items(std::vector<T> items, T cur, bool next);

template <typename T>
static T update_subject(Subject *s, T diff, T min, T max);

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
};

void control_name_say(cfg_ctrl_t ctrl) {
    auto item = control_name_voice.find(ctrl);
    if (item != control_name_voice.end()) {
        voice_say_text_fmt(item->second.c_str());
    } else {
        LV_LOG_ERROR("Ctrl %d has no voice", ctrl);
    }
}

void controls_toggle_agc_hang(button_item_t *btn) {
    bool new_val = toggle_subj(cfg.agc_hang.val);
    voice_say_bool("Auto gain hang", new_val);
}

void controls_toggle_key_train(button_item_t *btn) {
    bool new_val = toggle_subj(cfg.key_train.val);
    voice_say_bool("CW key train", new_val);
}

void controls_toggle_key_iambic_mode(button_item_t *btn) {
    x6100_iambic_mode_t new_mode = subject_get_int(cfg.iambic_mode.val) == x6100_iambic_a ? x6100_iambic_b : x6100_iambic_a;
    subject_set_int(cfg.iambic_mode.val, new_mode);
    char *str = params_iambic_mode_str_ger(new_mode);
    voice_say_text("Iambic mode", str);
}

void controls_toggle_cw_decoder(button_item_t *btn) {
    bool new_val = toggle_subj(cfg.cw_decoder.val);
    voice_say_bool("CW Decoder", new_val);
}

void controls_toggle_cw_tuner(button_item_t *btn) {
    bool new_val = toggle_subj(cfg.cw_tune.val);
    voice_say_bool("CW Decoder", new_val);
}

void controls_toggle_dnf(button_item_t *btn) {
    bool new_val = toggle_subj(cfg.dnf.val);
    voice_say_bool("DNF", new_val);
}

void controls_toggle_dnf_auto(button_item_t *btn) {
    bool new_val = toggle_subj(cfg.dnf_auto.val);
    voice_say_bool("DNF auto", new_val);
}

void controls_toggle_nb(button_item_t *btn) {
    bool new_val = toggle_subj(cfg.nb.val);
    voice_say_bool("NB", new_val);
}

void controls_toggle_nr(button_item_t *btn) {
    bool new_val = toggle_subj(cfg.nr.val);
    voice_say_bool("NR", new_val);
}

void controls_encoder_update(cfg_ctrl_t ctrl, int16_t diff, std::string &msg) {
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
            i = update_subject<int32_t>(cfg_cur.band->rfg.val, diff, 0, 100);
            snprintf(msg.data(), msg.capacity(), "RF gain: %i", i);

            if (diff) {
                voice_say_int("RF gain", i);
            }
            break;

        case CTRL_SQL:
            i = update_subject<int32_t>(cfg.sql.val, diff, 0, 100);
            snprintf(msg.data(), msg.capacity(), "Voice SQL: %i", i);

            if (diff) {
                voice_say_int("Squelch level", i);
            }
            break;

        case CTRL_FILTER_LOW:
            i = subject_get_int(cfg_cur.filter.low);
            if (diff) {
                // TODO: make step depending on freq
                i = align_int(i + diff * 10, 10);
                i = cfg_mode_set_low_filter(i);
            }
            snprintf(msg.data(), msg.capacity(), "Filter low: %i Hz", i);

            if (diff) {
                voice_say_int("Low filter limit", i);
            }
            break;

        case CTRL_FILTER_HIGH:
            i = subject_get_int(cfg_cur.filter.high);
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
                i = align_int(i + diff * freq_step, freq_step);
                i = cfg_mode_set_high_filter(i);
            }

            snprintf(msg.data(), msg.capacity(), "Filter high: %i Hz", i);

            if (diff) {
                voice_say_int("High filter limit", i);
            }
            break;

        case CTRL_FILTER_BW:
            {
                int32_t bw = subject_get_int(cfg_cur.filter.bw);
                if (diff) {
                    bw = align_int(bw + diff * 20, 20);
                    bw = clip(bw, 50, 7500);
                    subject_set_int(cfg_cur.filter.bw, bw);
                }
                snprintf(msg.data(), msg.capacity(), "Filter bw: %i Hz", bw);

                if (diff) {
                    voice_say_int("Filter bandwidth", bw);
                }
            }
            break;

        case CTRL_PWR:
            f = update_subject<float>(cfg.pwr.val, 0.1f * diff, 0, 10);
            printf("%f\n", diff * 0.1f);
            snprintf(msg.data(), msg.capacity(), "Power: %0.1f W", f);

            if (diff) {
                voice_say_float("Transmit power", f);
            }
            break;

        case CTRL_MIC:
            i = subject_get_int(cfg.mic.val);
            // i range should be 0..2
            i = (i + diff + 3) % 3;
            subject_set_int(cfg.mic.val, i);
            s = params_mic_str_get((x6100_mic_sel_t)i);
            snprintf(msg.data(), msg.capacity(), "MIC: %s", s);

            if (diff) {
                voice_say_text("Mic selector", s);
            }
            break;

        case CTRL_HMIC:
            i = update_subject<int32_t>(cfg.hmic.val, diff, 0, 50);
            snprintf(msg.data(), msg.capacity(), "H-MIC gain: %i", i);

            if (diff) {
                voice_say_int("Hand microphone gain", i);
            }
            break;

        case CTRL_IMIC:
            i = update_subject<int32_t>(cfg.imic.val, diff, 0, 35);
            snprintf(msg.data(), msg.capacity(), "I-MIC gain: %i", i);

            if (diff) {
                voice_say_int("Internal microphone gain", i);
            }
            break;

        case CTRL_MONI:
            i = update_subject<int32_t>(cfg.moni.val, diff, 0, 100);
            snprintf(msg.data(), msg.capacity(), "Moni level: %i", i);

            if (diff) {
                voice_say_int("Monitor level", i);
            }
            break;

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
            snprintf(msg.data(), msg.capacity(), "Zoom: x%i", i);

            if (diff) {
                voice_say_int("Zoom", i);
            }
            break;

        case CTRL_COMP:
            i = update_subject<int32_t>(cfg.comp.val, diff, 1, 8);
            snprintf(msg.data(), msg.capacity(), "Compressor ratio: %s", params_comp_str_get(i));

            if (diff) {
                if (i > 1) {
                    voice_say_text_fmt("Compressor ratio|%d to 1", i);
                } else {
                    voice_say_text_fmt("Compressor disabled");
                }
            }
            break;

        case CTRL_KEY_SPEED:
            i = update_subject<int32_t>(cfg.key_speed.val, diff, 5, 50);
            snprintf(msg.data(), msg.capacity(), "Key speed: %i wpm", i);

            if (diff) {
                voice_say_int("CW key speed", i);
            }
            break;

        case CTRL_KEY_MODE:
            i = subject_get_int(cfg.key_mode.val);
            if (diff) {
                i = loop_items({x6100_key_manual, x6100_key_auto_left, x6100_key_auto_right}, (x6100_key_mode_t)i, diff > 0);
                subject_set_int(cfg.key_mode.val, i);
            }
            s = params_key_mode_str_get((x6100_key_mode_t)i);
            snprintf(msg.data(), msg.capacity(), "Key mode: %s", s);

            if (diff) {
                voice_say_text("CW key mode", s);
            }
            break;

        case CTRL_IAMBIC_MODE:
            i = subject_get_int(cfg.iambic_mode.val);
            if (diff) {
                i = loop_items({x6100_iambic_a, x6100_iambic_b}, (x6100_iambic_mode_t)i, diff > 0);
                subject_set_int(cfg.iambic_mode.val, i);
            }
            s = params_iambic_mode_str_ger((x6100_iambic_mode_t)i);
            snprintf(msg.data(), msg.capacity(), "Iambic mode: %s", s);

            if (diff) {
                voice_say_text("Iambic mode", s);
            }
            break;

        case CTRL_KEY_TONE:
            i = update_subject<int32_t>(cfg.key_tone.val, diff * 10, 400, 1200);
            snprintf(msg.data(), msg.capacity(), "Key tone: %i Hz", i);

            if (diff) {
                voice_say_int("CW key tone", i);
            }
            break;

        case CTRL_KEY_VOL:
            i = update_subject<int32_t>(cfg.key_vol.val, diff, 0, 32);
            snprintf(msg.data(), msg.capacity(), "Key volume: %i", i);

            if (diff) {
                voice_say_int("CW key volume level", i);
            }
            break;

        case CTRL_KEY_TRAIN:
            b = subject_get_int(cfg.key_train.val);
            if (diff) {
                b = !b;
                subject_set_int(cfg.key_train.val, b);
            }
            snprintf(msg.data(), msg.capacity(), "Key train: %s", (b ? "On" : "Off"));

            if (diff) {
                voice_say_bool("CW key train", b);
            }
            break;

        case CTRL_QSK_TIME:
            i = update_subject<int32_t>(cfg.qsk_time.val, diff * 10, 0, 1000);
            snprintf(msg.data(), msg.capacity(), "QSK time: %i ms", i);

            if (diff) {
                voice_say_int("CW key QSK time", i);
            }
            break;

        case CTRL_KEY_RATIO:
            f = update_subject<float>(cfg.key_ratio.val, diff * 0.1f, 2.5f, 4.5f);
            snprintf(msg.data(), msg.capacity(), "Key ratio: %0.1f", f);

            if (diff) {
                voice_say_float("CW key ratio", f);
            }
            break;

        case CTRL_ANT:
            i = update_subject<int32_t>(cfg.ant_id.val, diff, 0, 5);
            snprintf(msg.data(), msg.capacity(), "Antenna: %i", i);

            if (diff) {
                voice_say_int("Antenna", i);
            }
            break;

        case CTRL_RIT:
            i = subject_get_int(cfg.rit.val);
            if (diff) {
                i = clip(align(i + diff * 10, 10), -1500, +1500);
                subject_set_int(cfg.rit.val, i);
            }
            snprintf(msg.data(), msg.capacity(), "RIT: %c%i", (i < 0 ? '-' : '+'), abs(i));

            if (diff) {
                voice_say_int("RIT", i);
            }
            break;

        case CTRL_XIT:
            i = subject_get_int(cfg.xit.val);
            if (diff) {
                i = clip(align(i + diff * 10, 10), -1500, +1500);
                subject_set_int(cfg.xit.val, i);
            }
            snprintf(msg.data(), msg.capacity(), "XIT: %c%i", (i < 0 ? '-' : '+'), abs(i));

            if (diff) {
                voice_say_int("XIT", i);
            }
            break;

        case CTRL_DNF:
            b = subject_get_int(cfg.dnf.val);
            if (diff) {
                b = !b;
                subject_set_int(cfg.dnf.val, b);
            }
            snprintf(msg.data(), msg.capacity(), "DNF: %s", (b ? "On" : "Off"));

            if (diff) {
                voice_say_bool("DNF", b);
            }
            break;

        case CTRL_DNF_CENTER:
            i = update_subject<int32_t>(cfg.dnf_center.val, diff * 50, 100, 3000);
            snprintf(msg.data(), msg.capacity(), "DNF center: %i Hz", i);

            if (diff) {
                voice_say_int("DNF center frequency", i);
            }
            break;

        case CTRL_DNF_WIDTH:
            i = update_subject<int32_t>(cfg.dnf_width.val, diff * 5, 10, 100);
            snprintf(msg.data(), msg.capacity(), "DNF width: %i Hz", i);

            if (diff) {
                voice_say_int("DNF width", i);
            }
            break;

        case CTRL_DNF_AUTO:
            b = subject_get_int(cfg.dnf_auto.val);
            if (diff) {
                b = !b;
                subject_set_int(cfg.dnf_auto.val, b);
            }
            snprintf(msg.data(), msg.capacity(), "DNF auto: %s", (b ? "On" : "Off"));

            if (diff) {
                voice_say_bool("DNF auto", b);
            }
            break;

        case CTRL_NB:
            b = subject_get_int(cfg.nb.val);
            if (diff) {
                b = !b;
                subject_set_int(cfg.nb.val, b);
            }
            snprintf(msg.data(), msg.capacity(), "NB: %s", (b ? "On" : "Off"));

            if (diff) {
                voice_say_bool("NB", b);
            }
            break;

        case CTRL_NB_LEVEL:
            i = update_subject<int32_t>(cfg.nb_level.val, diff * 5, 0, 100);
            snprintf(msg.data(), msg.capacity(), "NB level: %i", i);

            if (diff) {
                voice_say_int("NB level", i);
            }
            break;

        case CTRL_NB_WIDTH:
            i = update_subject<int32_t>(cfg.nb_width.val, diff * 5, 0, 100);
            snprintf(msg.data(), msg.capacity(), "NB width: %i", i);

            if (diff) {
                voice_say_int("NB width", i);
            }
            break;

        case CTRL_NR:
            b = subject_get_int(cfg.nr.val);
            if (diff) {
                b = !b;
                subject_set_int(cfg.nr.val, b);
            }
            snprintf(msg.data(), msg.capacity(), "NR: %s", (b ? "On" : "Off"));

            if (diff) {
                voice_say_bool("NR", b);
            }
            break;

        case CTRL_NR_LEVEL:
            i = update_subject<int32_t>(cfg.nr_level.val, diff * 5, 0, 60);
            snprintf(msg.data(), msg.capacity(), "NR level: %i", i);

            if (diff) {
                voice_say_int("NR level", i);
            }
            break;

        case CTRL_AGC_HANG:
            b = subject_get_int(cfg.agc_hang.val);
            if (diff) {
                b = !b;
                subject_set_int(cfg.agc_hang.val, b);
            }
            snprintf(msg.data(), msg.capacity(), "AGC hang: %s", (b ? "On" : "Off"));

            if (diff) {
                voice_say_bool("Auto gain hang", b);
            }
            break;

        case CTRL_AGC_KNEE:
            i = update_subject<int32_t>(cfg.agc_knee.val, diff, -100, 0);
            snprintf(msg.data(), msg.capacity(), "AGC knee: %i dB", i);

            if (diff) {
                voice_say_int("Auto gain knee level", i);
            }
            break;

        case CTRL_AGC_SLOPE:
            i = update_subject<int32_t>(cfg.agc_slope.val, diff, 0, 10);
            snprintf(msg.data(), msg.capacity(), "AGC slope: %i dB", i);

            if (diff) {
                voice_say_int("Auto gain slope level", i);
            }
            break;

        case CTRL_CW_DECODER:
            b = subject_get_int(cfg.cw_decoder.val);
            if (diff) {
                b = !b;
                subject_set_int(cfg.cw_decoder.val, b);
            }
            snprintf(msg.data(), msg.capacity(), "CW decoder: %s", (b ? "On" : "Off"));

            if (diff) {
                voice_say_bool("CW decoder", b);
            }
            break;

        case CTRL_CW_TUNE:
            b = subject_get_int(cfg.cw_tune.val);
            if (diff) {
                b = !b;
                subject_set_int(cfg.cw_tune.val, b);
            }
            snprintf(msg.data(), msg.capacity(), "CW tune: %s", (b ? "On" : "Off"));

            if (diff) {
                voice_say_bool("CW tune", b);
            }
            break;

        case CTRL_CW_DECODER_SNR:
            f = update_subject<float>(cfg.cw_decoder_snr.val, diff * 0.1, 3.0f, 30.0f);
            snprintf(msg.data(), msg.capacity(), "CW decoder SNR: %0.1f dB", f);

            if (diff) {
                voice_say_float("CW decoder SNR level", f);
            }
            break;

        case CTRL_CW_DECODER_PEAK_BETA:
            f = update_subject<float>(cfg.cw_decoder_peak_beta.val, diff * 0.01f, 0.1f, 0.95f);
            snprintf(msg.data(), msg.capacity(), "CW decoder peak beta: %0.2f", f);

            if (diff) {
                voice_say_float("CW decoder peak beta", f);
            }
            break;

        case CTRL_CW_DECODER_NOISE_BETA:
            f = update_subject<float>(cfg.cw_decoder_noise_beta.val, diff * 0.01f, 0.1f, 0.95f);
            snprintf(msg.data(), msg.capacity(), "CW decoder noise beta: %0.2f", f);

            if (diff) {
                voice_say_float("CW decoder noise beta", f);
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
                    i = update_subject<int32_t>(cfg_cur.band->if_shift.val, diff * 100, -40000, 40000);
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
    char *binds = subject_get_text(cfg.encoders_binds.val);
    size_t n_binds = strlen(binds);
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

static inline bool toggle_subj(Subject *subj) {
    bool new_val = !subject_get_int(subj);
    subject_set_int(subj, new_val);
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


template <typename T>
static T update_subject(Subject *s, T diff, T min, T max) {
    auto subj = static_cast<SubjectT<T>*>(s);
    T val = subj->get();
    val = clip(val + diff, min, max);
    subj->set(val);
    return val;
}

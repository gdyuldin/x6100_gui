#include "controls.h"

#include <iomanip>
#include <map>

#include "cfg/subjects.h"
#include "util.h"


extern "C" {
#include "cfg/cfg.h"
#include "cfg/mode.h"
#include "msg.h"
#include "voice.h"
}

static inline bool toggle_subj(Subject *subj);

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
    int32_t     x;
    float       f;
    char        *s;
    bool        b;

    std::stringstream ss;

    switch (ctrl) {
        case CTRL_VOL:
            x = radio_change_vol(diff);
            ss << "Volume: " << x;

            if (diff) {
                voice_say_int("Audio level", x);
            }
            break;

        case CTRL_RFG:
            x = subject_get_int(cfg_cur.band->rfg.val);
            x = limit(x + diff, 0, 100);
            subject_set_int(cfg_cur.band->rfg.val, x);
            ss << "RF gain: " << x;

            if (diff) {
                voice_say_int("RF gain", x);
            }
            break;

        case CTRL_SQL:
            x = subject_get_int(cfg.sql.val);
            x = limit(x + diff, 0, 100);
            subject_set_int(cfg.sql.val, x);
            ss << "Voice SQL: " << x;

            if (diff) {
                voice_say_int("Squelch level %i", x);
            }
            break;

        case CTRL_FILTER_LOW:
            x = subject_get_int(cfg_cur.filter.low);
            if (diff) {
                // TODO: make step depending on freq
                x = align_int(x + diff * 10, 10);
                x = cfg_mode_set_low_filter(x);
            }

            ss << "Filter low: " << x << " Hz";

            if (diff) {
                voice_delay_say_text_fmt("%i", x);
            }
            break;

        case CTRL_FILTER_HIGH:
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

            ss << "Filter high: " << x << " Hz";

            if (diff) {
                voice_say_int("High filter limit", x);
            }
            break;

        case CTRL_FILTER_BW:
            {
                uint32_t bw = subject_get_int(cfg_cur.filter.bw);
                if (diff) {
                    bw = align_int(bw + diff * 20, 20);
                    subject_set_int(cfg_cur.filter.bw, bw);
                }
                ss << "Filter bw: " << bw << " Hz";

                if (diff) {
                    voice_delay_say_text_fmt("%i", bw);
                }
            }
            break;

        case CTRL_PWR:
            f = subject_get_float(cfg.pwr.val);
            f += diff * 0.1f;
            f = LV_MIN(10.0f, f);
            f = LV_MAX(0.1f, f);
            subject_set_float(cfg.pwr.val, f);
            ss << "Power: " << std::fixed << std::setprecision(1) << f << " W";

            if (diff) {
                voice_say_float("Transmit power", f);
            }
            break;

        case CTRL_MIC:
            x = subject_get_int(cfg.mic.val);
            // x range should be 0..2
            x = (x + diff + 3) % 3;
            subject_set_int(cfg.mic.val, x);
            s = params_mic_str_get((x6100_mic_sel_t)x);
            ss << "MIC: " << s;

            if (diff) {
                voice_say_text("Mic selector", s);
            }
            break;

        case CTRL_HMIC:
            x = subject_get_int(cfg.hmic.val);
            x = limit(x + diff, 0, 50);
            subject_set_int(cfg.hmic.val, x);
            ss << "H-MIC gain: " << x;

            if (diff) {
                voice_say_int("Hand microphone gain", x);
            }
            break;

        case CTRL_IMIC:
            x = subject_get_int(cfg.imic.val);
            x = limit(x + diff, 0, 35);
            subject_set_int(cfg.imic.val, x);
            ss << "I-MIC gain: " << x;

            if (diff) {
                voice_say_int("Internal microphone gain", x);
            }
            break;

        case CTRL_MONI:
            x = subject_get_int(cfg.moni.val);
            x = limit(x + diff, 0, 100);
            subject_set_int(cfg.moni.val, x);
            ss << "Moni level: " << x;

            if (diff) {
                voice_say_int("Monitor level", x);
            }
            break;

        default:
            return;
    }
    msg = ss.str();
}

cfg_ctrl_t controls_encoder_get_next(encoder_binds_t encoder, cfg_ctrl_t current, int16_t dir) {
    char *binds = subject_get_text(cfg.encoders_binds.val);
    size_t n_binds = strlen(binds);
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

/*
 * X6100 CI-V command handlers and frame logic (Mfg 3087).
 *
 * Kept free of any I/O: feeds on a SettingsManager provided via
 * cat_frame_set_sm() (defaults to the production global cfg_sm) and touches
 * the outside world only through tx_info_refresh / meter_get_raw_db /
 * radio_get_state / radio_set_ptt, which host tests stub out.
 */

#include "cat.h"
#include "cat_internal.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "cfg/settings_manager.h"

extern "C" {
    #include "meter.h"
    #include "radio.h"
    #include "tx_info.h"
}

#include "lvgl/lvgl.h"

#define CODE_OK 0xFB
#define CODE_NG 0xFA

#define C_SND_MODE 0x01      /* Send mode data, Sc  for transceive mode does not ack */
#define C_RD_BAND 0x02       /* Read band edge frequencies */
#define C_RD_FREQ 0x03       /* Read display frequency */
#define C_RD_MODE 0x04       /* Read display mode */
#define C_SET_FREQ 0x05      /* Set frequency data(1) */
#define C_SET_MODE 0x06      /* Set mode data, Sc */
#define C_SET_VFO 0x07       /* Set VFO */
#define C_SET_MEM 0x08       /* Set channel, Sc(2) */
#define C_WR_MEM 0x09        /* Write memory */
#define C_MEM2VFO 0x0a       /* Memory to VFO */
#define C_CLR_MEM 0x0b       /* Memory clear */
#define C_RD_OFFS 0x0c       /* Read duplex offset frequency; default changes with HF/6M/2M */
#define C_SET_OFFS 0x0d      /* Set duplex offset frequency */
#define C_CTL_SCAN 0x0e      /* Control scan, Sc */
#define C_CTL_SPLT 0x0f      /* Control split, and duplex mode Sc */
#define C_SET_TS 0x10        /* Set tuning step, Sc */
#define C_CTL_ATT 0x11       /* Set/get attenuator, Sc */
#define C_CTL_ANT 0x12       /* Set/get antenna, Sc */
#define C_CTL_ANN 0x13       /* Control announce (speech synth.), Sc */
#define C_CTL_LVL 0x14       /* Set AF/RF/squelch, Sc */
#define C_RD_SQSM 0x15       /* Read squelch condition/S-meter level, Sc */
#define C_CTL_FUNC 0x16      /* Function settings (AGC,NB,etc.), Sc */
#define C_SND_CW 0x17        /* Send CW message */
#define C_SET_PWR 0x18       /* Set Power ON/OFF, Sc */
#define C_RD_TRXID 0x19      /* Read transceiver ID code */
#define C_CTL_MEM 0x1a       /* Misc memory/bank/rig control functions, Sc */
#define C_SET_TONE 0x1b      /* Set tone frequency */
#define C_CTL_PTT 0x1c       /* Control Transmit On/Off, Sc */
#define C_CTL_EDGE 0x1e      /* Band edges */
#define C_CTL_DVT 0x1f       /* Digital modes calsigns & messages */
#define C_CTL_DIG 0x20       /* Digital modes settings & status */
#define C_CTL_RIT 0x21       /* RIT/XIT control */
#define C_CTL_DSD 0x22       /* D-STAR Data */
#define C_SEND_SEL_FREQ 0x25 /* Send/Recv sel/unsel VFO frequency */
#define C_SEND_SEL_MODE 0x26
#define C_CTL_SCP 0x27   /* Scope control & data */
#define C_SND_VOICE 0x28 /* Transmit Voice Memory Contents */
#define C_CTL_MTEXT 0x70 /* Microtelecom Extension */
#define C_CTL_MISC 0x7f  /* Miscellaneous control, Sc */

#define S_VFOA 0x00      /* Set to VFO A */
#define S_VFOB 0x01      /* Set to VFO B */
#define S_BTOA 0xa0      /* VFO A=B */
#define S_XCHNG 0xb0     /* Switch VFO A and B */
#define S_SUBTOMAIN 0xb1 /* MAIN = SUB */
#define S_DUAL_OFF 0xc0  /* Dual watch off */
#define S_DUAL_ON 0xc1   /* Dual watch on */
#define S_DUAL 0xc2      /* Dual watch (0 = off, 1 = on) */
#define S_MAIN 0xd0      /* Select MAIN band */
#define S_SUB 0xd1       /* Select SUB band */
#define S_SUB_SEL 0xd2   /* Read/Set Main/Sub selection */
#define S_FRONTWIN 0xe0  /* Select front window */

// modes
#define M_LSB 0x00
#define M_USB 0x01
#define M_AM 0x02
#define M_CW 0x03
#define M_NFM 0x05
#define M_CWR 0x07

// memory/bank/rig control
#define MEM_BS_REG 0x01 /* Get band stacking register */
#define MEM_IF_FW 0x03  /* Get IF filter width */
#define MEM_LOCK 0x05   /* LOCK status */
#define MEM_DM_FG 0x06  /* Get data mode switch and filter group */

// SettingsManager backing the handlers. Production uses the global cfg_sm;
// host tests redirect it via cat_frame_set_sm().
static SettingsManager *cat_sm = &cfg_sm;

static void init_cmd_handlers();

void cat_frame_set_sm(SettingsManager *sm) {
    init_cmd_handlers();
    cat_sm = sm ? sm : &cfg_sm;
}

Frame::Frame(const char *data, const size_t len): pimpl_(std::make_unique<Impl>(data, len)) {};

Frame::Frame() : pimpl_(std::make_unique<Impl>(0, 0, 0)) {}

Frame::~Frame() = default;

std::vector<char> Frame::dump() const {
    return pimpl_->dump();
}

static std::string frame_to_string(const Impl &f) {
    std::string s;
    s.reserve(f.data.size() * 3 + 32);
    char tmp[32];

    snprintf(tmp, sizeof(tmp), "[%02X:%02X:%02X:%02X]-[%02X:",
             FRAME_PRE, FRAME_PRE, f.dst_addr, f.src_addr, f.command);
    s += tmp;
    for (uint8_t b : f.data) {
        snprintf(tmp, sizeof(tmp), "%02X:", b);
        s += tmp;
    }
    if (!f.data.empty()) {
        s.pop_back();
    }
    snprintf(tmp, sizeof(tmp), "]-[%02X]", FRAME_END);
    s += tmp;

    return s;
}

static void log_frame(const Impl &f, const char *prefix = nullptr) {
    std::string s = frame_to_string(f);
    if (prefix) {
        LV_LOG_USER("%s\t: %s\t(Len %i)", prefix, s.c_str(),
                    f.data.size() + FRAME_ADD_LEN + 1);
    } else {
        LV_LOG_USER("%s\t(Len %i)", s.c_str(),
                    f.data.size() + FRAME_ADD_LEN + 1);
    }
}

static void set_unsupported(const Impl &req, Impl &resp) {
    log_frame(req, "unsupported");
    resp.set_code(CODE_NG);
}

// Resolve the frequency parameter for a CI-V main/sub VFO selector. vfo_id is
// the CI-V selector (X6100_VFO_A = main/selected VFO, X6100_VFO_B = sub/inactive
// VFO); cur_vfo identifies which hardware VFO (A or B) is currently active.
static Parameter<int32_t>& vfo_freq(int vfo_id, int cur_vfo)
{
    if (vfo_id == X6100_VFO_A)
    {
        return cur_vfo == X6100_VFO_A ? cat_sm->p_band_vfoa_freq : cat_sm->p_band_vfob_freq;
    }
    else
    {
        return cur_vfo == X6100_VFO_A ? cat_sm->p_band_vfob_freq : cat_sm->p_band_vfoa_freq;
    }
}

static Parameter<int32_t>& vfo_mode(int vfo_id, int cur_vfo)
{
    if (vfo_id == X6100_VFO_A)
    {
        return cur_vfo == X6100_VFO_A ? cat_sm->p_band_vfoa_mode : cat_sm->p_band_vfob_mode;
    }
    else
    {
        return cur_vfo == X6100_VFO_A ? cat_sm->p_band_vfob_mode : cat_sm->p_band_vfoa_mode;
    }
}

void to_bcd(uint8_t bcd_data[], uint64_t data, uint8_t len) {
    int16_t i;

    for (i = 0; i < len / 2; i++) {
        uint8_t a = data % 10;

        data /= 10;
        a |= (data % 10) << 4;
        data /= 10;
        bcd_data[i] = a;
    }

    if (len & 1) {
        bcd_data[i] &= 0x0f;
        bcd_data[i] |= data % 10;
    }
}

void to_bcd_be(uint8_t bcd_data[], uint64_t data, uint8_t len) {
    int16_t i;

    for (i = (len / 2); i >= 0; i--) {
        uint8_t a = data % 10;

        data /= 10;
        a |= (data % 10) << 4;
        data /= 10;
        bcd_data[i] = a;
    }

    if (len & 1) {
        bcd_data[i] &= 0x0f;
        bcd_data[i] |= data % 10;
    }

}

uint64_t from_bcd(const uint8_t bcd_data[], uint8_t len) {
    int16_t     i;
    uint64_t    data = 0;

    if (len & 1) {
        data = bcd_data[len / 2] & 0x0F;
    }

    for (i = (len / 2) - 1; i >= 0; i--) {
        data *= 10;
        data += bcd_data[i] >> 4;
        data *= 10;
        data += bcd_data[i] & 0x0F;
    }

    return data;
}

uint64_t from_bcd_be(const uint8_t bcd_data[], uint8_t len) {
    int16_t     i = 0;
    uint64_t    data = 0;

    if (len & 1) {
        data = bcd_data[0] & 0x0F;
        i++;
    }

    for (; i <= (len / 2); i++) {
        data *= 10;
        data += bcd_data[i] >> 4;
        data *= 10;
        data += bcd_data[i] & 0x0F;
    }

    return data;
}

static x6100_mode_t ci_mode_2_x_mode(uint8_t mode, bool data_mode=false) {
    x6100_mode_t r_mode;

    switch (mode) {
        case M_LSB:
            r_mode = data_mode ? x6100_mode_lsb_dig : x6100_mode_lsb;
            break;
        case M_USB:
            r_mode = data_mode ? x6100_mode_usb_dig : x6100_mode_usb;
            break;
        case M_AM:
            r_mode = x6100_mode_am;
            break;
        case M_CW:
            r_mode = x6100_mode_cw;
            break;
        case M_NFM:
            r_mode = x6100_mode_nfm;
            break;
        case M_CWR:
            r_mode = x6100_mode_cwr;
            break;
        default:
            break;
    }
    return r_mode;
}

static uint8_t x_mode_2_ci_mode(x6100_mode_t mode, bool *data_mode=nullptr) {
    switch (mode) {
        case x6100_mode_lsb_dig:
            if (data_mode) *data_mode = true;
        case x6100_mode_lsb:
            return M_LSB;
            break;
        case x6100_mode_usb_dig:
            if (data_mode) *data_mode = true;
        case x6100_mode_usb:
            return M_USB;
            break;
        case x6100_mode_cw:
            return M_CW;
            break;
        case x6100_mode_cwr:
            return M_CWR;
            break;
        case x6100_mode_am:
            return M_AM;
            break;
        case x6100_mode_nfm:
            return M_NFM;
            break;
        default:
            return 0;
            break;
    }
}

static uint8_t get_if_bandwidth() {
    uint32_t bw = cat_sm->cp_cur_filter_bw.get();
    switch (cat_sm->cp_cur_mode.get()) {
        case x6100_mode_cw:
        case x6100_mode_cwr:
        case x6100_mode_lsb:
        case x6100_mode_lsb_dig:
        case x6100_mode_usb:
        case x6100_mode_usb_dig:
            if (bw <= 500) {
                return (bw - 25) / 50;
            } else {
                return (bw - 50) / 100 + 5;
            }
            break;
        case x6100_mode_am:
        case x6100_mode_nfm:
            return (bw - 100) / 200;
        default:
            return 31;
            break;
    }
}

static int32_t freq_step_from_ci(uint8_t val) {
    switch (val) {
        case 0x00:
            return 10;
        case 0x01:
            return 100;
        case 0x02:
            return 500;
        case 0x03:
            return 1000;
        case 0x04:
            return 5000;
    }
    return 500;

}

static uint8_t freq_step_to_ci(int32_t val) {
    switch (val) {
        case 1 ... 10:
            return 0x00;
        case 100:
            return 0x01;
        case 500:
            return 0x02;
        case 1000:
            return 0x03;
        case 5000:
            return 0x04;
    }
    return 0x02;
}

using cmd_handler_t = void(*)(const Impl &req, Impl &resp);

static void handle_snd_freq(const Impl &req, Impl &resp);
static void handle_rd_freq(const Impl &req, Impl &resp);
static void handle_rd_mode(const Impl &req, Impl &resp);
static void handle_set_freq(const Impl &req, Impl &resp);
static void handle_set_mode(const Impl &req, Impl &resp);
static void handle_set_vfo(const Impl &req, Impl &resp);
static void handle_ctl_splt(const Impl &req, Impl &resp);
static void handle_set_ts(const Impl &req, Impl &resp);
static void handle_ctl_att(const Impl &req, Impl &resp);
static void handle_ctl_lvl(const Impl &req, Impl &resp);
static void handle_rd_sqsm(const Impl &req, Impl &resp);
static void handle_ctl_func(const Impl &req, Impl &resp);
static void handle_rd_trxid(const Impl &req, Impl &resp);
static void handle_ctl_mem(const Impl &req, Impl &resp);
static void handle_ctl_ptt(const Impl &req, Impl &resp);
static void handle_send_sel_freq(const Impl &req, Impl &resp);
static void handle_send_sel_mode(const Impl &req, Impl &resp);
static void handle_ctl_scp(const Impl &req, Impl &resp);

static cmd_handler_t cmd_handlers[256] = {};
static bool cmd_handlers_ready = false;

static void init_cmd_handlers() {
    if (cmd_handlers_ready) {
        return;
    }
    cmd_handlers_ready = true;

    cmd_handlers[C_SND_FREQ]      = handle_snd_freq;
    cmd_handlers[C_RD_FREQ]       = handle_rd_freq;
    cmd_handlers[C_RD_MODE]       = handle_rd_mode;
    cmd_handlers[C_SET_FREQ]      = handle_set_freq;
    cmd_handlers[C_SET_MODE]      = handle_set_mode;
    cmd_handlers[C_SET_VFO]       = handle_set_vfo;
    cmd_handlers[C_CTL_SPLT]      = handle_ctl_splt;
    cmd_handlers[C_SET_TS]        = handle_set_ts;
    cmd_handlers[C_CTL_ATT]       = handle_ctl_att;
    cmd_handlers[C_CTL_LVL]       = handle_ctl_lvl;
    cmd_handlers[C_RD_SQSM]       = handle_rd_sqsm;
    cmd_handlers[C_CTL_FUNC]      = handle_ctl_func;
    cmd_handlers[C_RD_TRXID]      = handle_rd_trxid;
    cmd_handlers[C_CTL_MEM]       = handle_ctl_mem;
    cmd_handlers[C_CTL_PTT]       = handle_ctl_ptt;
    cmd_handlers[C_SEND_SEL_FREQ] = handle_send_sel_freq;
    cmd_handlers[C_SEND_SEL_MODE] = handle_send_sel_mode;
    cmd_handlers[C_CTL_SCP]       = handle_ctl_scp;
}

Frame Frame::process() const {
    Frame resp;
    resp.pimpl_->dst_addr = pimpl_->src_addr;
    resp.pimpl_->src_addr = LOCAL_ADDRESS;
    resp.pimpl_->command  = pimpl_->command;
    resp.pimpl_->data     = pimpl_->data;

    init_cmd_handlers();

    cmd_handler_t handler = cmd_handlers[pimpl_->command];
    if (handler) {
        handler(*pimpl_, *resp.pimpl_);
    } else {
        set_unsupported(*pimpl_, *resp.pimpl_);
    }

    return resp;
}

static void handle_snd_freq(const Impl &req, Impl &resp) {
    size_t data_size = req.data.size();
    if (data_size == 5) {
        cat_sm->cp_fg_freq.set(from_bcd(req.data.data(), 10));
        resp.set_code(CODE_OK);
    } else {
        set_unsupported(req, resp);
    }
}

static void handle_rd_freq(const Impl &req, Impl &resp) {
    uint8_t bcd[5];
    to_bcd(bcd, cat_sm->cp_fg_freq.get(), 10);
    resp.append(bcd, 5);
}

static void handle_rd_mode(const Impl &req, Impl &resp) {
    uint8_t v = x_mode_2_ci_mode((x6100_mode_t)cat_sm->cp_cur_mode.get());
    resp.write({v, v});
}

static void handle_set_freq(const Impl &req, Impl &resp) {
    size_t data_size = req.data.size();
    if (data_size == 5) {
        cat_sm->cp_fg_freq.set(from_bcd(req.data.data(), 10));
        resp.set_code(CODE_OK);
    } else {
        set_unsupported(req, resp);
    }
}

static void handle_set_mode(const Impl &req, Impl &resp) {
    size_t data_size = req.data.size();
    if ((data_size >= 1) && (data_size <= 2)) {
        cat_sm->cp_cur_mode.set(ci_mode_2_x_mode(req.data[0]));
        resp.set_code(CODE_OK);
    } else {
        set_unsupported(req, resp);
    }
}

static void handle_set_vfo(const Impl &req, Impl &resp) {
    size_t data_size = req.data.size();
    x6100_vfo_t cur_vfo = (x6100_vfo_t)cat_sm->p_band_current_vfo.get();

    if (data_size == 1) {
        switch (req.data[0]) {
            case S_VFOA:
                if (cur_vfo != X6100_VFO_A) {
                    cat_sm->p_band_current_vfo.set(X6100_VFO_A);
                }
                resp.set_code(CODE_OK);
                break;

            case S_VFOB:
                if (cur_vfo != X6100_VFO_B) {
                    cat_sm->p_band_current_vfo.set(X6100_VFO_B);
                }
                resp.set_code(CODE_OK);
                break;

            case S_XCHNG:
                cat_sm->p_band_current_vfo.set(
                    cur_vfo == X6100_VFO_A ? X6100_VFO_B : X6100_VFO_A);
                resp.set_code(CODE_OK);
                break;

            case S_BTOA:
                cat_sm->cfg_band_vfo_copy();
                resp.set_code(CODE_OK);
                break;

            default:
                set_unsupported(req, resp);
                break;
        }
    } else if (data_size == 0) {
        resp.write({static_cast<uint8_t>(cur_vfo == X6100_VFO_A ? S_VFOA : S_VFOB)});
    } else {
        set_unsupported(req, resp);
    }
}

static void handle_ctl_splt(const Impl &req, Impl &resp) {
    size_t data_size = req.data.size();
    if (data_size == 0) {
        resp.write({static_cast<uint8_t>(cat_sm->p_band_split.get())});
    } else if (data_size == 1) {
        cat_sm->p_band_split.set(req.data[0]);
        resp.set_code(CODE_OK);
    } else {
        set_unsupported(req, resp);
    }
}

static void handle_set_ts(const Impl &req, Impl &resp) {
    size_t data_size = req.data.size();
    if (data_size == 0) {
        resp.write({freq_step_to_ci(cat_sm->p_mode_freq_step.get())});
    } else if (data_size == 1) {
        cat_sm->p_mode_freq_step.set(freq_step_from_ci(req.data[0]));
        resp.set_code(CODE_OK);
    } else {
        set_unsupported(req, resp);
    }
}

static void handle_ctl_att(const Impl &req, Impl &resp) {
    size_t data_size = req.data.size();
    if (data_size == 0) {
        resp.write({static_cast<uint8_t>(cat_sm->cp_cur_att.get() * 0x20)});
    } else if (data_size == 1) {
        cat_sm->cp_cur_att.set(req.data[0]);
        resp.set_code(CODE_OK);
    } else {
        set_unsupported(req, resp);
    }
}

static void handle_ctl_lvl(const Impl &req, Impl &resp) {
    size_t data_size = req.data.size();
    uint8_t bcd[3] = {0, 0, 0};
    if (data_size >= 1) {
        switch (req.data[0]) {
            case 0x01:
                if (data_size == 1) {
                    to_bcd_be(&bcd[1], cat_sm->p_volume.get() * 255 / 55, 3);
                    resp.write({req.data[0], bcd[1], bcd[2]});
                } else if (data_size == 3) {
                    cat_sm->p_volume.set(from_bcd_be(&req.data[1], 3) * 55 / 255);
                }
                break;
            case 0x02:
                if (data_size == 1) {
                    to_bcd_be(&bcd[1], cat_sm->p_rfgain.get() * 255 / 100, 3);
                    resp.write({req.data[0], bcd[1], bcd[2]});
                } else if (data_size == 3) {
                    cat_sm->p_rfgain.set(from_bcd_be(&req.data[1], 3) * 100 / 255);
                }
                break;
            case 0x03:
                if (data_size == 1) {
                    to_bcd_be(&bcd[1], cat_sm->p_squelch.get() * 255 / 100, 3);
                    resp.write({req.data[0], bcd[1], bcd[2]});
                } else if (data_size == 3) {
                    cat_sm->p_squelch.set(from_bcd_be(&req.data[1], 3) * 100 / 255);
                }
                break;
            case 0x0a:
                if (data_size == 1) {
                    to_bcd_be(&bcd[1], std::round(cat_sm->p_pwr.get() * 255 / 10), 3);
                    resp.write({req.data[0], bcd[1], bcd[2]});
                } else if (data_size == 3) {
                    float pwr = from_bcd_be(&req.data[1], 3) * 10.0f / 255.0f;
                    pwr = LV_MIN(pwr, 10.0f);
                    cat_sm->p_pwr.set(pwr);
                }
                break;
            case 0x15:
                to_bcd_be(&bcd[1], cat_sm->p_moni.get() * 255 / 100, 3);
                resp.write({req.data[0], bcd[1], bcd[2]});
                break;
            default:
                set_unsupported(req, resp);
                break;
        }
    } else {
        set_unsupported(req, resp);
    }
}

static void handle_rd_sqsm(const Impl &req, Impl &resp) {
    size_t data_size = req.data.size();
    if (data_size == 1) {
        static float alc, pwr, swr;
        static uint8_t msg_id;
        tx_info_refresh(&msg_id, &alc, &pwr, &swr);
        uint8_t val;
        uint8_t bcd[3] = {0, 0, 0};
        switch (req.data[0]) {
            case 0x02: {
                int16_t db = meter_get_raw_db();
                val = db * 0.75f + 96;
                to_bcd_be(&bcd[1], val, 3);
                resp.write({req.data[0], bcd[1], bcd[2]});
                break;
            }
            case 0x11:
                val = -pwr * pwr + 35 * pwr;
                to_bcd_be(&bcd[1], val, 3);
                resp.write({req.data[0], bcd[1], bcd[2]});
                break;
            case 0x12:
                val = -21 * swr * swr + 134 * swr - 122;
                to_bcd_be(&bcd[1], val, 3);
                resp.write({req.data[0], bcd[1], bcd[2]});
                break;
            case 0x13:
                val = alc * 120 / 10;
                to_bcd_be(&bcd[1], val, 3);
                resp.write({req.data[0], bcd[1], bcd[2]});
                break;
            default:
                resp.set_code(CODE_NG);
                break;
        }
    } else {
        resp.set_code(CODE_NG);
    }
}

static void handle_ctl_func(const Impl &req, Impl &resp) {
    size_t data_size = req.data.size();
    if ((data_size == 1) || (data_size == 2)) {
        switch (req.data[0]) {
            case 0x02:
                if (data_size == 1) {
                    resp.write({req.data[0], static_cast<uint8_t>(cat_sm->cp_cur_pre.get())});
                } else {
                    cat_sm->cp_cur_pre.set(req.data[1] > 0);
                    resp.set_code(CODE_OK);
                }
                break;
            case 0x22:
                if (data_size == 1) {
                    resp.write({req.data[0], static_cast<uint8_t>(cat_sm->p_nb.get())});
                } else {
                    cat_sm->p_nb.set(req.data[1]);
                    resp.set_code(CODE_OK);
                }
                break;
            case 0x40:
                if (data_size == 1) {
                    resp.write({req.data[0], static_cast<uint8_t>(cat_sm->p_nr.get())});
                } else {
                    cat_sm->p_nr.set(req.data[1]);
                    resp.set_code(CODE_OK);
                }
                break;
            case 0x44:
                if (data_size == 1) {
                    resp.write({req.data[0], 0x00});
                } else {
                    resp.set_code(CODE_OK);
                }
                break;
            case 0x45:
                resp.set_code(CODE_NG);
                break;
            case 0x46:
                if (data_size == 1) {
                    resp.write({req.data[0], 0x00});
                } else {
                    resp.set_code(CODE_OK);
                }
                break;
            case 0x5D:
                resp.set_code(CODE_NG);
                break;
            default:
                set_unsupported(req, resp);
        }
    } else {
        set_unsupported(req, resp);
    }
}

static void handle_rd_trxid(const Impl &req, Impl &resp) {
    if ((req.data.size() == 1) && (req.data[0] == 0)) {
        resp.write({0x00, 0xA4});
    }
}

static void handle_ctl_mem(const Impl &req, Impl &resp) {
    size_t data_size = req.data.size();
    x6100_mode_t cur_mode = (x6100_mode_t)cat_sm->cp_cur_mode.get();

    if (data_size == 1) {
        switch (req.data[0]) {
            case MEM_IF_FW:
                resp.write({req.data[0], get_if_bandwidth()});
                break;

            case MEM_DM_FG:
                resp.write({req.data[0], x_mode_2_ci_mode(cur_mode),
                            static_cast<uint8_t>((cur_mode == x6100_mode_lsb_dig) || (cur_mode == x6100_mode_usb_dig)),
                            0x00});
                break;

            default:
                set_unsupported(req, resp);
                break;
        }
    } else {
        switch (req.data[0]) {
            case MEM_LOCK:
                resp.set_code(CODE_NG);
                break;
            case MEM_DM_FG: {
                x6100_mode_t new_mode = ci_mode_2_x_mode(req.data[1], req.data[2]);
                cat_sm->cp_cur_mode.set(new_mode);
                resp.set_code(CODE_OK);
                break;
            }
            default:
                set_unsupported(req, resp);
                break;
        }
    }
}

static void handle_ctl_ptt(const Impl &req, Impl &resp) {
    size_t data_size = req.data.size();
    if ((data_size >= 1) && (req.data[0] == 0x00)) {
        if (data_size == 1) {
            resp.write({0x00, static_cast<uint8_t>((radio_get_state() == RADIO_RX) ? 0 : 1)});
        } else {
            switch (req.data[1]) {
                case 0:
                    radio_set_ptt(false);
                    break;
                case 1:
                    radio_set_ptt(true);
                    break;
            }
            resp.write({0x00, CODE_OK});
        }
    }
}

static void handle_send_sel_freq(const Impl &req, Impl &resp) {
    size_t data_size = req.data.size();
    ComputedParameter<int32_t> *freq;

    if (req.data[0] == 0) {
        freq = &cat_sm->cp_fg_freq;
    } else {
        freq = &cat_sm->cp_bg_freq;
    }

    if (data_size == 1) {
        uint8_t bcd[6] = {req.data[0], 0, 0, 0, 0, 0};
        to_bcd(&bcd[1], freq->get(), 10);
        resp.append(bcd, sizeof(bcd));
    } else if (data_size == 6) {
        freq->set(from_bcd(&req.data[1], 10));
        resp.set_code(CODE_OK);
    } else {
        set_unsupported(req, resp);
    }
}

static void handle_send_sel_mode(const Impl &req, Impl &resp) {
    size_t data_size = req.data.size();
    Parameter<int32_t> *mode_par;

    if (req.data[0] == 0) {
        mode_par = cat_sm->p_band_current_vfo.get() == X6100_VFO_A
            ? &cat_sm->p_band_vfoa_mode
            : &cat_sm->p_band_vfob_mode;
    } else {
        mode_par = cat_sm->p_band_current_vfo.get() == X6100_VFO_B
            ? &cat_sm->p_band_vfoa_mode
            : &cat_sm->p_band_vfob_mode;
    }

    switch (data_size) {
        case 1: {
            bool data_mode = false;
            uint8_t v = x_mode_2_ci_mode((x6100_mode_t)mode_par->get(), &data_mode);
            resp.write({req.data[0], v, static_cast<uint8_t>(data_mode), 0x01});
            break;
        }
        case 4:
        case 3: {
            bool data_mode = req.data[2];
            x6100_mode_t new_mode = ci_mode_2_x_mode(req.data[1], data_mode);
            mode_par->set(new_mode);
            resp.set_code(CODE_OK);
            break;
        }
        case 2: {
            x6100_mode_t new_mode = ci_mode_2_x_mode(req.data[1], false);
            mode_par->set(new_mode);
            resp.set_code(CODE_OK);
            break;
        }
        default:
            set_unsupported(req, resp);
            break;
    }
}

static void handle_ctl_scp(const Impl &req, Impl &resp) {
    size_t data_size = req.data.size();
    if (data_size >= 1) {
        switch (req.data[0]) {
            case 0x10:
                if (data_size == 1) {
                    resp.write({req.data[0], 0x01});
                } else {
                    resp.write({req.data[0]});
                }
                break;
            case 0x11:
                if (data_size == 1) {
                    resp.write({req.data[0], 0x01});
                } else {
                    resp.write({req.data[0]});
                }
                break;
            case 0x13:
                resp.set_code(CODE_NG);
                break;
            case 0x14:
                if (data_size == 1) {
                    resp.write({req.data[0], 0x00, 0x00});
                } else {
                    resp.write({req.data[0]});
                }
                break;
            case 0x15:
                if (req.data[2] == FRAME_END) {
                    uint8_t bcd[7] = {req.data[0], 0, 0, 0, 0, 0, 0};
                    to_bcd(&bcd[2], 50000, 10);
                    resp.append(bcd, sizeof(bcd));
                } else {
                    resp.write({req.data[0]});
                }
                break;
            case 0x17:
                resp.set_code(CODE_NG);
                break;
            case 0x19:
                resp.write({req.data[0], 0x00, 0x00, 0x00, 0x00});
                break;
            case 0x1A:
                resp.set_code(CODE_NG);
                break;
            default:
                set_unsupported(req, resp);
                break;
        }
    } else {
        set_unsupported(req, resp);
    }
}

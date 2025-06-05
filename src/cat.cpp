/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

/*
 * X6100 protocol implementation (Mfg 3087)
 */

#include "cat.h"

#include "cfg/subjects.h"
#include "util.hpp"
#include "util.h"

#include <mutex>
#include <thread>
#include <vector>

extern "C" {
    // #include "cfg/cfg.h"
    #include "events.h"
    #include "main_screen.h"
    #include "meter.h"
    #include "params/params.h"
    #include "radio.h"
    #include "scheduler.h"
    #include "spectrum.h"
    #include "waterfall.h"
    #include "tx_info.h"

    #include "lvgl/lvgl.h"
    #include <aether_radio/x6100_control/low/gpio.h>
    #include <fcntl.h>
    #include <stdio.h>
    #include <stdlib.h>
    #include <string.h>
    #include <sys/poll.h>
    #include <termios.h>
    #include <unistd.h>
}


#define FRAME_PRE 0xFE
#define FRAME_END 0xFD

#define CODE_OK 0xFB
#define CODE_NG 0xFA

#define LOCAL_ADDRESS 0xA4

#define C_SND_FREQ 0x00      /* Send frequency data  transceive mode does not ack*/
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

#define FRAME_ADD_LEN 5 /* Header and end len */

static TSQueue<std::vector<char>> send_queue;

static void send_waterfall_data();

static void on_fg_freq_change(Subject *s, void *user_data);

struct Frame {
    uint8_t dst_addr;
    uint8_t src_addr;
    uint8_t command;
    std::vector<uint8_t> data;

    Frame(uint8_t dst, uint8_t src, uint8_t command): dst_addr(dst), src_addr(src), command(command) {};

    Frame(const char * data, const uint16_t len): data(len - FRAME_ADD_LEN - 1) {
        dst_addr = data[2];
        src_addr = data[3];
        command = data[4];
        std::copy(&data[5], &data[len - 1], this->data.begin());
    };

    Frame(const Frame *req) {
        // Swap address
        dst_addr = req->src_addr;
        src_addr = LOCAL_ADDRESS;
        command  = req->command;
        data = req->data;
    }

    void log(const char *prefix=nullptr) const {
        char buf[512];
        char *buf_ptr = buf;
        buf_ptr += sprintf(buf_ptr, "[%02X:%02X:", FRAME_PRE, FRAME_PRE);
        buf_ptr += sprintf(buf_ptr, "%02X:", dst_addr);
        buf_ptr += sprintf(buf_ptr, "%02X]-", src_addr);
        buf_ptr += sprintf(buf_ptr, "[%02X:", command);
        uint16_t remain_len = data.size();
        size_t i = 0;
        while (remain_len) {
            buf_ptr += sprintf(buf_ptr, "%02X:", data[i++]);
            remain_len--;
        }
        buf_ptr += sprintf(buf_ptr - 1, "]-[%02X]", FRAME_END);
        *(buf_ptr - 1) = '\0';
        if (prefix) {
            LV_LOG_USER("%s\t: %s\t(Len %i)", prefix, buf, data.size() + FRAME_ADD_LEN + 1);
        } else {
            LV_LOG_USER("%s\t(Len %i)", buf, data.size() + FRAME_ADD_LEN + 1);
        }
    }

    void set_code(uint8_t code) {
        set_payload_len(1);
        command = code;
    }

    void set_payload_len(size_t len) {
        data.resize(len - 1);
    }

    size_t get_len() const {
        return data.size() + 1 + FRAME_ADD_LEN;
    }

    std::vector<char> dump() const {
        std::vector<char> buf(data.size() + 1 + FRAME_ADD_LEN);
        buf[0] = FRAME_PRE;
        buf[1] = FRAME_PRE;
        buf[2] = dst_addr;
        buf[3] = src_addr;
        buf[4] = command;
        std::copy(data.begin(), data.end(), buf.begin() + 5);
        *--buf.end() = FRAME_END;
        return buf;
    }
};


class Connection {
    int        fd;
    char       buf[1024];
    const char header[2] = {FRAME_PRE, FRAME_PRE};
    size_t     start=0;
    size_t     end=0;

  protected:
    void write_buf(const char *buf, size_t len) {
        ssize_t l;
        while (len) {
            l = write(fd, buf, len);
            if (l < 0) {
                perror("Error during writing message");
                return;
            }
            len -= l;
        }
    }

  public:
    Connection(int fd) : fd(fd) {}
    Frame *feed() {
        if (start) {
            end -= start;
            memmove(buf, buf + start, end);
            start = 0;
        }
        while (1) {
            int res = read(fd, buf + end, sizeof(buf) - end);
            if (res < 0) {
                return nullptr;
            }
            if (res > 0) {
                end += res;
            }
            char *frame_start = (char *)memmem(buf, end, header, sizeof(header));
            if (frame_start == NULL) {
                // Move last byte to begin
                buf[0] = buf[end - 1];
                end    = 1;
                return nullptr;
            }
            if (frame_start != buf) {
                end = end + buf - frame_start;
                memmove(buf, frame_start, end);
                start = 0;
            }
            if (end >= FRAME_ADD_LEN) {
                char *end_pos = (char *)memchr(buf + FRAME_ADD_LEN, FRAME_END, end - FRAME_ADD_LEN);
                if (end_pos) {
                    size_t frame_len = end_pos - buf + 1;
                    start            = frame_len;
                    end              = start;
                    return new Frame(buf, frame_len);
                }
            }
        }
    }

    void send(const char * data, size_t len) {
        write_buf(data, len);
    }
    void send(const Frame * frame) {
        auto data = frame->dump();
        write_buf(data.data(), frame->get_len());
    }
};

static Connection *conn;

typedef struct {
    uint8_t start[2];
    uint8_t dst_addr;
    uint8_t src_addr;
    uint8_t command;
    uint8_t subcommand;
    uint8_t args[1500];
} frame_t;

static void set_vfo(void *arg) {
    if (!arg) {
        LV_LOG_ERROR("arg is NULL");
    }
    x6100_vfo_t vfo = *(x6100_vfo_t *)arg;
    subject_set_int(cfg_cur.band->vfo.val, vfo);
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
    uint32_t bw = subject_get_int(cfg_cur.filter.bw);
    switch (subject_get_int(cfg_cur.mode)) {
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

static void set_unsupported(const Frame *req, Frame *resp) {
    req->log("unsupported");
    resp->set_code(CODE_NG);
}

static Frame *process_req(const Frame *req) {
    auto resp = new Frame(req);

    int32_t        new_freq;
    x6100_vfo_t    cur_vfo    = (x6100_vfo_t)subject_get_int(cfg_cur.band->vfo.val);
    int32_t        cur_freq   = subject_get_int(cfg_cur.fg_freq);
    x6100_mode_t   cur_mode   = (x6100_mode_t)subject_get_int(cfg_cur.mode);
    x6100_vfo_t    target_vfo = cur_vfo;
    uint8_t        vfo_id;

    size_t data_size = req->data.size();

    struct vfo_params *vfo_params[2];
    if (cur_vfo == X6100_VFO_A) {
        vfo_params[0] = &cfg_cur.band->vfo_a;
        vfo_params[1] = &cfg_cur.band->vfo_b;
    } else {
        vfo_params[0] = &cfg_cur.band->vfo_b;
        vfo_params[1] = &cfg_cur.band->vfo_a;
    }

#if 0
    req->log("req");
#endif

    switch (req->command) {
        case C_SND_FREQ:
            if (data_size == 5) {
                subject_set_int(cfg_cur.fg_freq, from_bcd(req->data.data(), 10));
                resp->set_code(CODE_OK);
            } else {
                set_unsupported(req, resp);
            }
            break;

        case C_RD_FREQ:
            resp->set_payload_len(6);
            // bcd len - 5 bytes
            to_bcd(resp->data.data(), cur_freq, 10);
            break;

        case C_RD_MODE:
            {
                uint8_t v = x_mode_2_ci_mode(cur_mode);
                resp->set_payload_len(3);
                resp->data[0] = v;
                resp->data[1] = v;
            }
            break;

        case C_SET_FREQ:
            if (data_size == 5) {
                subject_set_int(cfg_cur.fg_freq, from_bcd(req->data.data(), 10));
                resp->set_code(CODE_OK);
            } else {
                set_unsupported(req, resp);
            }
            break;

        case C_SET_MODE:
            if ((data_size >= 1) && (data_size <= 2)) {
                subject_set_int(cfg_cur.mode, ci_mode_2_x_mode(req->data[0]));
                if (data_size == 2) {
                    // filter selector -> req->data[1]
                }
                resp->set_code(CODE_OK);
            } else {
                set_unsupported(req, resp);
            }
            break;

        case C_SET_VFO:
            if (data_size == 1) {
                x6100_vfo_t new_vfo;
                switch (req->data[0]) {
                    case S_VFOA:
                        if (cur_vfo != X6100_VFO_A) {
                            new_vfo = X6100_VFO_A;
                            subject_set_int(cfg_cur.band->vfo.val, new_vfo);
                        }

                        resp->set_code(CODE_OK);
                        break;

                    case S_VFOB:
                        if (cur_vfo != X6100_VFO_B) {
                            new_vfo = X6100_VFO_B;
                            subject_set_int(cfg_cur.band->vfo.val, new_vfo);
                        }
                        resp->set_code(CODE_OK);
                        break;

                    case S_XCHNG:
                        if (cur_vfo == X6100_VFO_A) {
                            new_vfo = X6100_VFO_B;
                        } else {
                            new_vfo = X6100_VFO_A;
                        }
                        subject_set_int(cfg_cur.band->vfo.val, new_vfo);
                        resp->set_code(CODE_OK);
                        break;

                    case S_BTOA:
                        cfg_band_vfo_copy();
                        resp->set_code(CODE_OK);
                        break;

                    default:
                        set_unsupported(req, resp);
                        break;
                }
            } else if (data_size == 0) {
                resp->set_payload_len(2);
                resp->data[0] = cur_vfo == X6100_VFO_A ? S_VFOA : S_VFOB;
            } else {
                set_unsupported(req, resp);
            }
            break;

        case C_CTL_SPLT:
            if (data_size == 0) {
                resp->set_payload_len(2);
                resp->data[0] = subject_get_int(cfg_cur.band->split.val);
            } else if (data_size == 1) {
                subject_set_int(cfg_cur.band->split.val, req->data[0]);
                resp->set_code(CODE_OK);
            } else {
                set_unsupported(req, resp);
            }
            break;

        case C_SET_TS:
            if (data_size == 0) {
                resp->set_payload_len(2);
                resp->data[0] = freq_step_to_ci(subject_get_int(cfg_cur.freq_step));
            } else if (data_size == 1) {
                subject_set_int(cfg_cur.freq_step, freq_step_from_ci(resp->data[0]));
                resp->set_code(CODE_OK);
            } else {
                set_unsupported(req, resp);
            }
            break;

        case C_CTL_ATT:
            if (data_size == 0) {
                resp->set_payload_len(2);
                resp->data[0] = subject_get_int(cfg_cur.att) * 0x20;
            } else if (data_size == 1) {
                subject_set_int(cfg_cur.att, req->data[0]);
                resp->set_code(CODE_OK);
            } else {
                set_unsupported(req, resp);
            }
            break;

        case C_CTL_LVL:
            if (data_size >= 1) {
                switch(req->data[0]) {
                    case 0x01:
                        // VOL
                        if (data_size == 1) {
                            resp->set_payload_len(4);
                            to_bcd_be(&resp->data[1], subject_get_int(cfg.vol.val) * 255 / 55, 3);
                        } else if (data_size == 3) {
                            subject_set_int(cfg.vol.val, from_bcd_be(&req->data[1], 3) * 55 / 255);
                        }
                        break;
                    case 0x02:
                        // RFG
                        if (data_size == 1) {
                            resp->set_payload_len(4);
                            to_bcd_be(&resp->data[1], subject_get_int(cfg_cur.band->rfg.val) * 255 / 100, 3);
                        } else if (data_size == 3) {
                            subject_set_int(cfg_cur.band->rfg.val, from_bcd_be(&req->data[1], 3) * 100 / 255);
                        }
                        break;
                    case 0x03:
                        // Squelch
                        if (data_size == 1) {
                            resp->set_payload_len(4);
                            to_bcd_be(&resp->data[1], subject_get_int(cfg.sql.val) * 255 / 100, 3);
                        } else if (data_size == 3) {
                            subject_set_int(cfg.sql.val, from_bcd_be(&req->data[1], 3) * 100 / 255);
                        }
                        break;
                    case 0x0a:
                        // PWR
                        if (data_size == 1) {
                            resp->set_payload_len(4);
                            to_bcd_be(&resp->data[1], subject_get_float(cfg.pwr.val) * 255 / 10, 3);
                        } else if (data_size == 3) {
                            subject_set_float(cfg.pwr.val, (from_bcd_be(&req->data[1], 3) * 100 / 255) / 10.0f);
                        }
                        break;
                    case 0x15:
                        // Monitor level
                        resp->set_payload_len(4);
                        to_bcd_be(&resp->data[1], params.moni * 255 / 100, 3);
                        break;
                    default:
                        set_unsupported(req, resp);
                        break;
                }
            } else {
                set_unsupported(req, resp);
            }
            break;

        case C_RD_SQSM:
            if (data_size == 1) {
                static float alc, pwr, swr;
                static uint8_t msg_id;
                tx_info_refresh(&msg_id, &alc, &pwr, &swr);
                uint8_t val;
                switch (req->data[0]) {
                    case 0x02: // S-meter
                        {
                            int16_t db = meter_get_raw_db();
                            if (db < S9) {
                                val = (db - S9) * 2.1f + 120;
                            } else {
                                val = (db - S9) * 2 + 120;
                            }
                            resp->set_payload_len(4);
                            to_bcd_be(&resp->data[1], val, 3);
                        }
                        break;
                    case 0x11:  // Power
                        val = -pwr * pwr + 35 * pwr;
                        resp->set_payload_len(4);
                        to_bcd_be(&resp->data[1], val, 3);
                        break;
                    case 0x12:  // SWR
                        val = -21 * swr * swr + 134 * swr - 122;
                        resp->set_payload_len(4);
                        to_bcd_be(&resp->data[1], val, 3);
                        break;
                    case 0x13:  // ALC
                        val = alc * 120 / 10;
                        resp->set_payload_len(4);
                        to_bcd_be(&resp->data[1], val, 3);
                        break;
                    default:
                        resp->set_code(CODE_NG);
                        break;
                }
            } else {
                resp->set_code(CODE_NG);
            }
            break;

        case C_CTL_FUNC:
            if ((data_size == 1) || (data_size == 2)) {
                switch (req->data[0]) {
                    case 0x02:
                        // PRE
                        if (data_size == 1) {
                            resp->set_payload_len(3);
                            resp->data[1] = subject_get_int(cfg_cur.pre);
                        } else {
                            subject_set_int(cfg_cur.pre, req->data[1] > 0);
                            resp->set_code(CODE_OK);
                        }
                        break;
                    case 0x22:
                        // NB
                        if (data_size == 1) {
                            resp->set_payload_len(3);
                            resp->data[1] = subject_get_int(cfg.nb.val);
                        } else {
                            subject_set_int(cfg.nb.val, req->data[1]);
                            resp->set_code(CODE_OK);
                        }
                        break;
                    case 0x40:
                        // NR
                        if (data_size == 1) {
                            resp->set_payload_len(3);
                            resp->data[1] = subject_get_int(cfg.nr.val);
                        } else {
                            subject_set_int(cfg.nr.val, req->data[1]);
                            resp->set_code(CODE_OK);
                        }
                        break;
                    case 0x44:
                        // COMP
                        if (data_size == 1) {
                            resp->set_payload_len(3);
                            resp->data[1] = 0x00;
                        } else {
                            // COMP set is not suported yet
                            resp->set_code(CODE_OK);
                        }
                        break;
                    case 0x45:
                        // Monitor [MONI] On/off
                        resp->set_code(CODE_NG);
                        break;
                    case 0x46:
                        // VOX
                        if (data_size == 1) {
                            resp->set_payload_len(3);
                            resp->data[1] = 0x00;
                        } else {
                            // VOX set is not suported yet
                            resp->set_code(CODE_OK);
                        }
                        break;
                    case 0x5D:
                        // Tone squelch function
                        resp->set_code(CODE_NG);
                        break;
                    default:
                        set_unsupported(req, resp);
                }
            } else {
                set_unsupported(req, resp);
            }
            break;

        case C_RD_TRXID:
            if ((data_size == 1) && (req->data[0] == 0)) {
                resp->set_payload_len(3);
                resp->data[1] = 0xA4;
            }
            break;

        case C_CTL_MEM:
            // TODO: Implement another options
            if (data_size == 1) {
                switch (req->data[0]) {
                    case MEM_IF_FW:
                        resp->set_payload_len(3);
                        resp->data[1] = get_if_bandwidth();
                        break;

                    case MEM_DM_FG:
                        resp->set_payload_len(5);
                        resp->data[1] = x_mode_2_ci_mode(cur_mode);
                        // data mode
                        resp->data[2] = (cur_mode == x6100_mode_lsb_dig) || (cur_mode == x6100_mode_usb_dig);
                        // filter group
                        resp->data[3] = 0;
                        break;

                    default:
                        set_unsupported(req, resp);
                        break;
                }
            } else {
                switch (req->data[0]) {
                    case MEM_LOCK:  // Various controls for icom, unsupported
                        resp->set_code(CODE_NG);
                        break;
                    case MEM_DM_FG:
                        {
                            x6100_mode_t new_mode  = ci_mode_2_x_mode(req->data[1], req->data[2]);
                            subject_set_int(cfg_cur.mode, new_mode);
                            resp->set_code(CODE_OK);
                        }
                        break;
                    default:
                        set_unsupported(req, resp);
                        break;
                }
            }
            break;

        case C_CTL_PTT:
            if ((data_size >= 1) && (req->data[0] == 0x00)) {
                if (data_size == 1) {
                    resp->set_payload_len(3);
                    resp->data[1] = (radio_get_state() == RADIO_RX) ? 0 : 1;
                } else {
                    switch (req->data[1]) {
                        case 0:
                            radio_set_ptt(false);
                            break;

                        case 1:
                            radio_set_ptt(true);
                            break;
                    }
                    resp->set_payload_len(3);
                    resp->data[1] = CODE_OK;
                }
            }
            break;

        case C_SEND_SEL_FREQ:
            if (data_size == 1) {
                vfo_id       = req->data[0] > 0;
                int32_t freq = subject_get_int(vfo_params[vfo_id]->freq.val);
                resp->set_payload_len(7);
                to_bcd(&resp->data[1], freq, 10);
            } else if (data_size == 6) {
                vfo_id       = req->data[0] > 0;
                int32_t freq = from_bcd(&req->data[1], 10);
                subject_set_int(vfo_params[vfo_id]->freq.val, freq);
                resp->set_code(CODE_OK);
            } else {
                set_unsupported(req, resp);
            }
            break;

        case C_SEND_SEL_MODE:
            {
                uint8_t v;
                x6100_mode_t new_mode;
                bool data_mode = false;
                switch (data_size) {
                    case 1:
                        vfo_id    = req->data[0] > 0;
                        v = x_mode_2_ci_mode((x6100_mode_t)subject_get_int(vfo_params[vfo_id]->mode.val), &data_mode);
                        resp->set_payload_len(5);
                        resp->data[1] = v;
                        resp->data[2] = data_mode;
                        // filter
                        resp->data[3] = 1;
                        break;

                    case 4:
                        // get filter
                    case 3:
                        // get data
                        data_mode = req->data[2];
                    case 2:
                        // get mode
                        vfo_id                = req->data[0] > 0;
                        new_mode = ci_mode_2_x_mode(req->data[1], data_mode);
                        subject_set_int(vfo_params[vfo_id]->mode.val, new_mode);
                        resp->set_code(CODE_OK);
                        break;
                    default:
                        set_unsupported(req, resp);
                        break;
                }

            }
            break;

        case C_CTL_SCP:
            if (data_size >= 1) {
                switch (req->data[0]) {
                    case 0x10:  // Send/read the Scope ON/OFF status
                        if (data_size == 1) {
                            resp->set_payload_len(3);
                            resp->data[1] = 1;
                        } else {
                            resp->set_payload_len(2);
                        }
                        break;
                    case 0x11:  // Send/read the Scope wave data output*4
                        if (data_size == 1) {
                            resp->set_payload_len(3);
                            resp->data[1] = 1;
                        } else {
                            resp->set_payload_len(2);
                        }
                        break;
                    case 0x13:  // Single/Dual scope setting
                        resp->set_code(CODE_NG);
                        break;
                    case 0x14:  // Send/read the Scope Center mode or Fixed mode setting
                        if (data_size == 1) {
                            // Report center mode
                            resp->set_payload_len(4);
                            resp->data[1] = 0;
                            resp->data[2] = 0;
                        } else {
                            resp->set_payload_len(2);
                        }
                        break;
                    case 0x15:  // Scope span settings
                        if (req->data[2] == FRAME_END) {
                            // Span +- 50kHz
                            resp->set_payload_len(8);
                            to_bcd(&resp->data[1] + 1, 50000, 10);
                        } else {
                            resp->set_payload_len(2);
                        }
                        break;
                    case 0x17:  // Scope hold function
                        resp->set_code(CODE_NG);
                        break;
                    case 0x19:
                        resp->set_payload_len(6);
                        resp->data[1] = 0;
                        resp->data[2] = 0;
                        resp->data[3] = 0;
                        resp->data[4] = 0;
                        break;
                    case 0x1A:
                        // Sweep speed setting
                        resp->set_code(CODE_NG);
                        break;
                    default:
                        set_unsupported(req, resp);
                        break;
                }
            } else {
                set_unsupported(req, resp);
            }
            break;

        default:
            set_unsupported(req, resp);
            break;
    }
    // send_waterfall_data();
    return resp;
}

static uint8_t counter = 0;

// static void send_waterfall_data() {
//     frame_t frame;
//     counter++;
//     if (counter < 10) {
//         return;
//     }
//     counter          = 0;
//     uint8_t *data    = (uint8_t *)malloc(1024);
//     frame.dst_addr   = 0x00;
//     frame.src_addr   = LOCAL_ADDRESS;
//     frame.command    = C_CTL_SCP;
//     frame.subcommand = 0;
//     uint8_t i        = 0;
//     // main
//     frame.args[i++] = 0;
//     // SequenceNumber[01-11]
//     frame.args[i++] = 1;
//     // TotalSeqneuceSize[01=LAN, 11=USB]
//     frame.args[i++] = 1;
//     // Center/Fixed (for lan radio)  [00=cent, 01=fixed]
//     frame.args[i++] = 0;
//     // Center freq
//     to_bcd(frame.args + i, subject_get_int(cfg_cur.fg_freq), 10);
//     i += 5;
//     // Span
//     to_bcd(frame.args + i, 50000, 10);
//     i += 5;
//     // Range information 00 = In range, 01 = Out of range
//     frame.args[i++] = 0;
//     // FFT data
//     // printf("%u\n", i);
//     size_t c;
//     for (c = 0; c < 300; c++) {
//         frame.args[i + c] = c % 160;
//     }
//     // printf("%u, %u\n", i, c);
//     send_frame(&frame, i + c + 3);
//     // printf("len: %u\n", i+c);
//     free(data);
// }

static void cat_thread() {
    while (true) {
        bool sleep = true;

        const Frame *req = conn->feed();
        if (req) {
            sleep = false;
            conn->send(req);
            auto resp = process_req(req);
            // resp->log("resp");
            conn->send(resp);
        }

        while (!send_queue.empty()) {
            sleep = false;
            auto data = send_queue.pop();
            conn->send(data.data(), data.size());
        }
        if (sleep) {
            sleep_usec(10000);
        }
    }
}

void cat_init() {
    /* UART */
    x6100_gpio_set(x6100_pin_usb, 1); /* USB -> CAT */

    int fd = open("/dev/ttyS2", O_RDWR | O_NONBLOCK | O_NOCTTY);

    if (fd > 0) {
        struct termios attr;

        tcgetattr(fd, &attr);

        cfsetispeed(&attr, B19200);
        cfsetospeed(&attr, B19200);
        // cfsetispeed(&attr, B115200);
        // cfsetospeed(&attr, B115200);
        cfmakeraw(&attr);

        if (tcsetattr(fd, 0, &attr) < 0) {
            close(fd);
            LV_LOG_ERROR("UART set speed");
        }
    } else {
        LV_LOG_ERROR("UART open");
    }

    conn = new Connection(fd);

    subject_add_observer(cfg_cur.fg_freq, on_fg_freq_change, NULL);

    /* * */
    std::thread thread(cat_thread);
    thread.detach();
}

static void on_fg_freq_change(Subject *s, void *user_data) {
    int32_t new_freq = subject_get_int(s);
    Frame frame{0, LOCAL_ADDRESS, C_SND_FREQ};
    // bcd len - 5 bytes
    frame.set_payload_len(6);
    to_bcd(frame.data.data(), new_freq, 10);
    send_queue.push(frame.dump());
}

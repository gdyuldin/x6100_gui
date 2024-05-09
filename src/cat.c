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

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/poll.h>

#include <aether_radio/x6100_control/low/gpio.h>
#include "lvgl/lvgl.h"

#include "cat.h"
#include "radio.h"
#include "params.h"
#include "util.h"
#include "events.h"
#include "waterfall.h"
#include "spectrum.h"

#define FRAME_PRE       0xFE
#define FRAME_END       0xFD

#define CODE_OK         0xFB
#define CODE_NG         0xFA

#define C_SND_FREQ      0x00    /* Send frequency data  transceive mode does not ack*/
#define C_SND_MODE      0x01    /* Send mode data, Sc  for transceive mode does not ack */
#define C_RD_BAND       0x02    /* Read band edge frequencies */
#define C_RD_FREQ       0x03    /* Read display frequency */
#define C_RD_MODE       0x04    /* Read display mode */
#define C_SET_FREQ      0x05    /* Set frequency data(1) */
#define C_SET_MODE      0x06    /* Set mode data, Sc */
#define C_SET_VFO       0x07    /* Set VFO */
#define C_SET_MEM       0x08    /* Set channel, Sc(2) */
#define C_WR_MEM        0x09    /* Write memory */
#define C_MEM2VFO       0x0a    /* Memory to VFO */
#define C_CLR_MEM       0x0b    /* Memory clear */
#define C_RD_OFFS       0x0c    /* Read duplex offset frequency; default changes with HF/6M/2M */
#define C_SET_OFFS      0x0d    /* Set duplex offset frequency */
#define C_CTL_SCAN      0x0e    /* Control scan, Sc */
#define C_CTL_SPLT      0x0f    /* Control split, and duplex mode Sc */
#define C_SET_TS        0x10    /* Set tuning step, Sc */
#define C_CTL_ATT       0x11    /* Set/get attenuator, Sc */
#define C_CTL_ANT       0x12    /* Set/get antenna, Sc */
#define C_CTL_ANN       0x13    /* Control announce (speech synth.), Sc */
#define C_CTL_LVL       0x14    /* Set AF/RF/squelch, Sc */
#define C_RD_SQSM       0x15    /* Read squelch condition/S-meter level, Sc */
#define C_CTL_FUNC      0x16    /* Function settings (AGC,NB,etc.), Sc */
#define C_SND_CW        0x17    /* Send CW message */
#define C_SET_PWR       0x18    /* Set Power ON/OFF, Sc */
#define C_RD_TRXID      0x19    /* Read transceiver ID code */
#define C_CTL_MEM       0x1a    /* Misc memory/bank/rig control functions, Sc */
#define C_SET_TONE      0x1b    /* Set tone frequency */
#define C_CTL_PTT       0x1c    /* Control Transmit On/Off, Sc */
#define C_CTL_EDGE      0x1e    /* Band edges */
#define C_CTL_DVT       0x1f    /* Digital modes calsigns & messages */
#define C_CTL_DIG       0x20    /* Digital modes settings & status */
#define C_CTL_RIT       0x21    /* RIT/XIT control */
#define C_CTL_DSD       0x22    /* D-STAR Data */
#define C_SEND_SEL_FREQ 0x25    /* Send/Recv sel/unsel VFO frequency */
#define C_SEND_SEL_MODE 0x26
#define C_CTL_SCP       0x27    /* Scope control & data */
#define C_SND_VOICE     0x28    /* Transmit Voice Memory Contents */
#define C_CTL_MTEXT     0x70    /* Microtelecom Extension */
#define C_CTL_MISC      0x7f    /* Miscellaneous control, Sc */

#define S_VFOA          0x00    /* Set to VFO A */
#define S_VFOB          0x01    /* Set to VFO B */
#define S_BTOA          0xa0    /* VFO A=B */
#define S_XCHNG         0xb0    /* Switch VFO A and B */
#define S_SUBTOMAIN     0xb1    /* MAIN = SUB */
#define S_DUAL_OFF      0xc0    /* Dual watch off */
#define S_DUAL_ON       0xc1    /* Dual watch on */
#define S_DUAL          0xc2    /* Dual watch (0 = off, 1 = on) */
#define S_MAIN          0xd0    /* Select MAIN band */
#define S_SUB           0xd1    /* Select SUB band */
#define S_SUB_SEL       0xd2    /* Read/Set Main/Sub selection */
#define S_FRONTWIN      0xe0    /* Select front window */

// modes
#define M_LSB           0x00
#define M_USB           0x01
#define M_AM            0x02
#define M_CW            0x03
#define M_NFM           0x05
#define M_CWR           0x07

// memory/bank/rig control
#define MEM_BS_REG      0x01    /* Get band stacking register */
#define MEM_IF_FW       0x03    /* Get IF filter width */
#define MEM_DM_FG       0x06    /* Get data mode switch and filter group */

static int      fd;

static uint8_t  frame[256];

static uint16_t frame_get() {
    uint16_t    len = 0;
    uint8_t     c;

    memset(frame, 0, sizeof(frame));

    while (true) {
        int res = read(fd, &c, 1);

        if (res > 0) {
            frame[len++] = c;

            if (c == FRAME_END) {
                return len;
            }

            if (len >= sizeof(frame)) {
                return 0;
            }
        } else {
            usleep(10000);
        }
    }

    return 0;
}

static void prepare_answer() {
    // set dst address from sender, src address is fixed - 0xA4
    frame[2] = frame[3];
    frame[3] = 0xA4;
}

static void send_frame(uint16_t len) {
    frame[len - 1] = FRAME_END;
    write(fd, frame, len);
}

static void send_code(uint8_t code) {
    frame[4] = code;
    send_frame(6);
}

static void set_freq(uint64_t freq) {
    if (params_bands_find(freq, &params.freq_band)) {
        bands_activate(&params.freq_band, NULL);
    }

    radio_set_freq(freq);
    event_send(lv_scr_act(), EVENT_SCREEN_UPDATE, NULL);
}


static x6100_mode_t ci_mode_2_x_mode(uint8_t mode, uint8_t *dig_mode) {
    x6100_mode_t r_mode;
    bool data_mode = (dig_mode != NULL) && *dig_mode;
    switch (mode)
    {
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

static uint8_t x_mode_2_ci_mode(x6100_mode_t mode) {
    switch (mode)
    {
    case x6100_mode_lsb:
    case x6100_mode_lsb_dig:
        return M_LSB;
        break;
    case x6100_mode_usb:
    case x6100_mode_usb_dig:
        return M_USB;
        break;
    case  x6100_mode_cw:
        return M_CW;
        break;
    case  x6100_mode_cwr:
        return M_CWR;
        break;
    case  x6100_mode_am:
        return M_AM;
        break;
    case  x6100_mode_nfm:
        return M_NFM;
        break;
    default:
        return 0;
        break;
    }
}


static uint8_t get_if_bandwidth() {
    int32_t bw;
    switch (params_band.vfo_x[params_band.vfo].mode)
    {
    case x6100_mode_cw:
    case x6100_mode_cwr:
    case x6100_mode_lsb:
    case x6100_mode_lsb_dig:
    case x6100_mode_usb:
    case x6100_mode_usb_dig:
        bw = params_mode.filter_high - params_mode.filter_low;
        if (bw <= 500)
        {
            return (bw - 25) / 50;
        } else {
            return (bw - 50) / 100 + 5;
        }
        break;
    case x6100_mode_am:
    case x6100_mode_nfm:
        bw = params_mode.filter_high  * 2;
        return (bw - 100) / 200;
    default:
        return 31;
        break;
    }
}

static void frame_parse(uint16_t len) {
    if (frame[0] != FRAME_PRE && frame[1] != FRAME_PRE) {
        LV_LOG_ERROR("Incorrect frame");
        return;
    }
    uint64_t new_freq;
    x6100_vfo_t cur_vfo = params_band.vfo;
    x6100_vfo_t target_vfo = cur_vfo;
    params_vfo_t* cur_vfo_params = &params_band.vfo_x[cur_vfo];

#if 0
    LV_LOG_WARN("Cmd %02X:%02X (Len %i)", frame[4], frame[5], len);
#endif

    // echo input frame
    send_frame(len);
    prepare_answer();

    switch (frame[4]) {
        case C_RD_FREQ:
            to_bcd(&frame[5], cur_vfo_params->freq, 10);
            send_frame(11);
            break;

        case C_RD_MODE: ;
            uint8_t v = x_mode_2_ci_mode(cur_vfo_params->mode);
            frame[5] = v;
            frame[6] = v;
            send_frame(8);
            break;

        case C_SET_FREQ:
            new_freq = from_bcd(&frame[5], 10);
            if (new_freq != cur_vfo_params->freq)
            {
                set_freq(new_freq);
            }
            
            send_code(CODE_OK);
            break;

        case C_SET_MODE: ;
            x6100_mode_t new_mode = ci_mode_2_x_mode(frame[5], NULL);
            if (new_mode != cur_vfo_params->mode) {
                radio_set_mode(cur_vfo, new_mode);
                event_send(lv_scr_act(), EVENT_SCREEN_UPDATE, NULL);
            }
            
            send_code(CODE_OK);
            break;

        case C_CTL_PTT:
            if (frame[5] == 0x00) {
                if (frame[6] == FRAME_END) {
                    frame[6] = (radio_get_state() == RADIO_RX) ? 0 : 1;
                    send_frame(8);
                } else {
                    switch (frame[6]) {
                        case 0:
                            radio_set_ptt(false);
                            break;

                        case 1:
                            radio_set_ptt(true);
                            break;
                    }
                    frame[6] = CODE_OK;
                    send_frame(8);
                }
            }
            break;

        case C_SET_VFO:
            switch (frame[5]) {
            case S_VFOA:
                if (cur_vfo != X6100_VFO_A) {
                    radio_set_vfo(X6100_VFO_A);
                    event_send(lv_scr_act(), EVENT_SCREEN_UPDATE, NULL);
                }
                
                send_code(CODE_OK);
                break;

            case S_VFOB:
                if (cur_vfo != X6100_VFO_B) {
                    radio_set_vfo(X6100_VFO_B);
                    event_send(lv_scr_act(), EVENT_SCREEN_UPDATE, NULL);
                }
                send_code(CODE_OK);
                break;

            default:
                send_code(CODE_NG);
                break;
            }
            break;

        case C_SEND_SEL_FREQ:
            if (frame[5]) {
                target_vfo = (cur_vfo == X6100_VFO_A) ? X6100_VFO_B : X6100_VFO_A;
            }
            if (frame[6] == FRAME_END) {
                uint64_t freq = params_band.vfo_x[target_vfo].freq;
                to_bcd(&frame[6], freq, 10);
                send_frame(12);
            } else {
                uint64_t freq = from_bcd(&frame[6], 10);
                if (params_band.vfo_x[target_vfo].freq != freq){
                    params_band.vfo_x[target_vfo].freq = freq;

                    if (cur_vfo == target_vfo) {
                        set_freq(freq);
                    }
                }
                send_code(CODE_OK);
            }
            break;

        case C_SEND_SEL_MODE: ;
            if (frame[5]) {
                target_vfo = (cur_vfo == X6100_VFO_A) ? X6100_VFO_B : X6100_VFO_A;
            }
            if (frame[6] == FRAME_END) {
                uint8_t v = x_mode_2_ci_mode(params_band.vfo_x[target_vfo].mode);
                frame[6] = v;
                frame[7] = 0;
                frame[8] = 1;
                send_frame(10);
            } else {
                // TODO: Add filters applying
                radio_set_mode(target_vfo, ci_mode_2_x_mode(frame[6], &frame[7]));
                event_send(lv_scr_act(), EVENT_SCREEN_UPDATE, NULL);
                send_code(CODE_OK);
            }
            break;

        case C_CTL_MEM:
            // TODO: Implement another options
            if (frame[6] == FRAME_END) {
                switch (frame[5])
                {
                case MEM_IF_FW:
                    frame[6] = get_if_bandwidth();
                    send_frame(8);
                    break;

                default:
                    send_code(CODE_NG);
                    break;
                }
            } else {
                LV_LOG_WARN("Unsuported %02X:%02X (Len %i)", frame[4], frame[5], len);
                send_code(CODE_NG);
            }
            break;

        default:
            LV_LOG_WARN("Unsuported %02X:%02X (Len %i)", frame[4], frame[5], len);
            send_code(CODE_NG);
            break;
    }
}

static void * cat_thread(void *arg) {
    while (true) {
        uint16_t len = frame_get();

        if (len >= 0) {
            frame_parse(len);
        }
    }
}

void cat_init() {
    /* UART */

    x6100_gpio_set(x6100_pin_usb, 1);  /* USB -> CAT */

    fd = open("/dev/ttyS2", O_RDWR | O_NONBLOCK | O_NOCTTY);

    if (fd > 0) {
        struct termios attr;

        tcgetattr(fd, &attr);

        cfsetispeed(&attr, B19200);
        cfsetospeed(&attr, B19200);
        cfmakeraw(&attr);

        if (tcsetattr(fd, 0, &attr) < 0) {
            close(fd);
            LV_LOG_ERROR("UART set speed");
        }
    } else {
        LV_LOG_ERROR("UART open");
    }

    /* * */

    pthread_t thread;

    pthread_create(&thread, NULL, cat_thread, NULL);
    pthread_detach(thread);
}

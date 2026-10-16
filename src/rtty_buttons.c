/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *  Copyright (c) 2026 Krzysztof Myszkowski SP2KMO
 *
 *  Xiegu X6100 LVGL GUI - RTTY button pages 2..6
 */

#include "rtty_buttons.h"

#include <math.h>
#include <stdio.h>

#include "lvgl/lvgl.h"

#include "mfk.h"
#include "msg.h"
#include "digi_qso.h"
#include "rtty_sql.h"
#include "rtty_tx.h"

#define RTTY_MACRO_SLOTS 12

/* ---- page buttons --------------------------------------------------- */

#define PAGE_BTN(name, text)                \
    static button_data_t name = {           \
        .type  = BTN_TEXT,                  \
        .label = text,                      \
        .press = button_next_page_cb,       \
        .hold  = button_prev_page_cb,       \
    }

PAGE_BTN(btn_rtty_p2, "(RTTY 2:6)");
PAGE_BTN(btn_rtty_p3, "(RTTY 3:6)");
PAGE_BTN(btn_rtty_p4, "(RTTY 4:6)");
PAGE_BTN(btn_rtty_p5, "(RTTY 5:6)");
PAGE_BTN(btn_rtty_p6, "(RTTY 6:6)");

#undef PAGE_BTN

/* ---- page 2: transmit and squelch ----------------------------------- */

static void send_cb(button_data_t *b)
{
    (void)b;
    rtty_tx_open_input();
}

static void stop_cb(button_data_t *b)
{
    (void)b;
    rtty_tx_stop();
}

static void new_macro_cb(button_data_t *b)
{
    (void)b;
    rtty_macros_open_menu();
}

static button_data_t btn_rtty_send = {
    .type  = BTN_TEXT,
    .label = "Free\nText",
    .press = send_cb,
};

static button_data_t btn_rtty_stop = {
    .type  = BTN_TEXT,
    .label = "Stop\nTX",
    .press = stop_cb,
};

static button_data_t btn_rtty_new = {
    .type  = BTN_TEXT,
    .label = "New\nMacro",
    .press = new_macro_cb,
};

/*
 * The threshold alone is useless without seeing the level it is compared
 * with: park on a quiet spot, read the lower number, set the upper one a
 * few dB above it. "*" means the gate is open.
 */
static char sql_label[32];

static const char *sql_label_getter(void)
{
    uint8_t v = rtty_change_squelch(0);
    int lvl = (int)lroundf(rtty_get_level_db());

    if (v == 0) {
        snprintf(sql_label, sizeof(sql_label), "SQL off\nlvl %d", lvl);
    } else {
        snprintf(sql_label, sizeof(sql_label), "SQL %u%s\nlvl %d",
                 v, rtty_squelch_is_open() ? "*" : "", lvl);
    }
    return sql_label;
}

/*
 * Set with the knob, like Rate, Shift and Center: press hands the MFK
 * knob to the control, then turn either way. The extra controls sit
 * beyond CTRL_FAST_ACCESS_LAST, where the stock button handler also
 * falls back to the MFK knob, so calling mfk_set_ctrl() directly is the
 * same behaviour.
 */
static void sql_press_cb(button_data_t *b)
{
    (void)b;
    mfk_set_ctrl(CTRL_RTTY_SQUELCH);
}

static button_data_t btn_rtty_sql = {
    .type     = BTN_TEXT_FN,
    .label_fn = sql_label_getter,
    .press    = sql_press_cb,
    .ctrl     = CTRL_RTTY_SQUELCH,
};

/* ---- pages 3..5: macros --------------------------------------------- */

static char macro_labels[RTTY_MACRO_SLOTS][RTTY_MACRO_NAME_LEN + 4];

static void macro_send_slot(int idx)
{
    if (!rtty_macro_send(idx)) {
        msg_update_text_fmt("#FF0000 RTTY TX busy");
    } else {
        msg_update_text_fmt("#FFFFFF RTTY TX...");
    }
}

/* button_data_t carries no user pointer, hence one pair of callbacks
 * per slot. */
#define MACRO_SLOT(n)                                                      \
    static void macro##n##_press_cb(button_data_t *b)                     \
    {                                                                      \
        (void)b;                                                           \
        macro_send_slot(n);                                                \
    }                                                                      \
    static void macro##n##_hold_cb(button_data_t *b)                      \
    {                                                                      \
        (void)b;                                                           \
        rtty_macros_edit_slot(n);                                          \
    }                                                                      \
    static button_data_t btn_rtty_macro##n = {                            \
        .type  = BTN_TEXT,                                                 \
        .label = macro_labels[n],                                          \
        .press = macro##n##_press_cb,                                      \
        .hold  = macro##n##_hold_cb,                                       \
    };

MACRO_SLOT(0)
MACRO_SLOT(1)
MACRO_SLOT(2)
MACRO_SLOT(3)
MACRO_SLOT(4)
MACRO_SLOT(5)
MACRO_SLOT(6)
MACRO_SLOT(7)
MACRO_SLOT(8)
MACRO_SLOT(9)
MACRO_SLOT(10)
MACRO_SLOT(11)

#undef MACRO_SLOT

static button_data_t *const macro_btns[RTTY_MACRO_SLOTS] = {
    &btn_rtty_macro0, &btn_rtty_macro1, &btn_rtty_macro2,  &btn_rtty_macro3,
    &btn_rtty_macro4, &btn_rtty_macro5, &btn_rtty_macro6,  &btn_rtty_macro7,
    &btn_rtty_macro8, &btn_rtty_macro9, &btn_rtty_macro10, &btn_rtty_macro11,
};

void rtty_refresh_macro_labels(void)
{
    int n = rtty_macros_count();

    for (int i = 0; i < RTTY_MACRO_SLOTS; i++) {
        rtty_macro_t *m = (i < n) ? rtty_macro_get(i) : NULL;

        snprintf(macro_labels[i], sizeof(macro_labels[i]), "%s",
                 (m && m->name[0]) ? m->name : "--");

        /* A caption on screen was copied when the page was loaded. */
        if (macro_btns[i]->disp_btn)
            buttons_refresh(macro_btns[i]);
    }
}

/* ---- page 6: QSO fields --------------------------------------------- */

/* The macros carry <CALL> and <RST>; these give them values. The call
 * is normally picked up off the air ("DE <call>"), so the button is
 * mostly there to correct it. */
static char dx_label[32];
static char rsts_label[24];
static char rstr_label[24];

static const char *dx_getter(void)
{
    const char *c = digi_qso_dx();

    snprintf(dx_label, sizeof(dx_label), "DX call\n%s", (c && *c) ? c : "---");
    return dx_label;
}

static const char *rsts_getter(void)
{
    snprintf(rsts_label, sizeof(rsts_label), "RST snt\n%s", digi_qso_rst_sent());
    return rsts_label;
}

static const char *rstr_getter(void)
{
    snprintf(rstr_label, sizeof(rstr_label), "RST rcv\n%s", digi_qso_rst_rcvd());
    return rstr_label;
}

static const char *grab_getter(void)
{
    return digi_qso_autograb() ? "Auto DX\non" : "Auto DX\noff";
}

static void dx_press_cb(button_data_t *b)
{
    (void)b;
    rtty_qso_open_dx_input();
}

static void dx_hold_cb(button_data_t *b)
{
    digi_qso_clear();
    msg_update_text_fmt("#FFFFFF QSO cleared");
    buttons_refresh(b);
}

static void rsts_press_cb(button_data_t *b)
{
    (void)b;
    mfk_set_ctrl(CTRL_RTTY_RST_SENT);
}

static void rstr_press_cb(button_data_t *b)
{
    (void)b;
    mfk_set_ctrl(CTRL_RTTY_RST_RCVD);
}

static void grab_cb(button_data_t *b)
{
    digi_qso_toggle_autograb();
    buttons_refresh(b);
}

static button_data_t btn_rtty_dx = {
    .type     = BTN_TEXT_FN,
    .label_fn = dx_getter,
    .press    = dx_press_cb,
    .hold     = dx_hold_cb,
};

static button_data_t btn_rtty_rsts = {
    .type     = BTN_TEXT_FN,
    .label_fn = rsts_getter,
    .press    = rsts_press_cb,
    .ctrl     = CTRL_RTTY_RST_SENT,
};

static button_data_t btn_rtty_rstr = {
    .type     = BTN_TEXT_FN,
    .label_fn = rstr_getter,
    .press    = rstr_press_cb,
    .ctrl     = CTRL_RTTY_RST_RCVD,
};

static button_data_t btn_rtty_grab = {
    .type     = BTN_TEXT_FN,
    .label_fn = grab_getter,
    .press    = grab_cb,
};

/* ---- pages ---------------------------------------------------------- */

buttons_page_t buttons_page_rtty_2 = {
    {&btn_rtty_p2, &btn_rtty_send, &btn_rtty_stop, &btn_rtty_new, &btn_rtty_sql}
};

buttons_page_t buttons_page_rtty_3 = {
    {&btn_rtty_p3, &btn_rtty_macro0, &btn_rtty_macro1, &btn_rtty_macro2, &btn_rtty_macro3}
};

buttons_page_t buttons_page_rtty_4 = {
    {&btn_rtty_p4, &btn_rtty_macro4, &btn_rtty_macro5, &btn_rtty_macro6, &btn_rtty_macro7}
};

buttons_page_t buttons_page_rtty_5 = {
    {&btn_rtty_p5, &btn_rtty_macro8, &btn_rtty_macro9, &btn_rtty_macro10, &btn_rtty_macro11}
};

buttons_page_t buttons_page_rtty_6 = {
    {&btn_rtty_p6, &btn_rtty_dx, &btn_rtty_rsts, &btn_rtty_rstr, &btn_rtty_grab}
};

/* ---- live repaint ---------------------------------------------------- */

/* BTN_TEXT_FN buttons without a subject are painted once when the page
 * loads. The SQL level moves all the time and the QSO fields change from
 * the decoder (Auto DX) and the knob, so whichever of them is on screen
 * is repainted here. */
static lv_timer_t *sql_timer = NULL;

static void sql_timer_cb(lv_timer_t *t)
{
    static button_data_t *const live[] = {
        &btn_rtty_sql, &btn_rtty_dx, &btn_rtty_rsts, &btn_rtty_rstr,
    };

    (void)t;
    for (size_t i = 0; i < sizeof(live) / sizeof(live[0]); i++) {
        if (live[i]->disp_btn)
            buttons_refresh(live[i]);
    }
}

void rtty_sql_timer_start(void)
{
    if (!sql_timer)
        sql_timer = lv_timer_create(sql_timer_cb, 400, NULL);
}

void rtty_sql_timer_stop(void)
{
    if (sql_timer) {
        lv_timer_del(sql_timer);
        sql_timer = NULL;
    }
}

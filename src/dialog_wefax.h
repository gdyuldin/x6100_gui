/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Hany El Imam - 1KO125 firmware for the Xiegu X6100
 *
 * Additional term under section 7(b) of the GNU GPL version 3: any work that
 * contains this file, or a work based on it, must preserve the author
 * attribution "Hany El Imam" and the credits of the WEFAX, NAVTEX/SITOR-B,
 * DSC and SSTV applications in the Appropriate Legal Notices displayed by
 * its user interface (in this program: the Info window of each application).
 * See NOTICE-1KO125.md.
 */
#pragma once

#include "dialog.h"

#ifdef __cplusplus
extern "C" {
#endif

extern dialog_t *dialog_wefax;
bool dialog_wefax_is_active(void);

/* Microphone UP (dir < 0) / DOWN (dir > 0) while WEFAX is open: previous /
 * next station of the stations list.  false: not handled (WEFAX closed). */
bool dialog_wefax_mic_step(int dir);

/* Microphone V/M while WEFAX is open: open / close the stations list.
 * Returns false (key not used) when WEFAX is not open. */
bool dialog_wefax_vm_toggle(void);

#ifdef __cplusplus
}
#endif

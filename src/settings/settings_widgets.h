#pragma once

#include "settings_context.h"

// Shared settings-widget layer: the SettingsPage factory methods are
// implemented in settings_widgets.cpp, and this callback is reused by every
// page that previews a focused/edited widget.

// Fades the dialog background image while a slider/spinbox is being edited or a
// switch is focused. `user_data` must be the owning SettingsPage*.
void settings_change_bg_opa_cb(lv_event_t *e);

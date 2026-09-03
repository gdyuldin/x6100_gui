#pragma once

#include <signal.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SCREEN_WIDTH 800
#define SCREEN_HEIGHT 480

extern volatile sig_atomic_t app_is_running;

#ifdef __cplusplus
}
#endif

#pragma once

#include <stddef.h>

#include "../ports/app_ports.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LOCAL_ADDRESS 0xA4


void cat_init(const app_ports_t *ports);
void cat_destruct();

#ifdef __cplusplus
}
#endif


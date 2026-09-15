#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "../../ports/app_ports.h"

#ifdef __cplusplus
extern "C" {
#endif

int  cat_lan_init(const app_ports_t *ports);
void cat_lan_destruct(void);

#ifdef __cplusplus
}
#endif
#pragma once

// Production application ports instance, defined in src/app_ports.cpp.
// Pass &app_ports to cat_init(), cat_lan_init() and tx_worker_construct().

#include "ports/app_ports.h"

extern const app_ports_t app_ports;

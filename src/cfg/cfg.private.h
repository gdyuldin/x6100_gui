#pragma once

#include "cfg.h"

#include <pthread.h>
#include <sqlite3.h>

struct dirty_t {
    bool            val;
    pthread_mutex_t mux;
};

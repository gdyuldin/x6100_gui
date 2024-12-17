#pragma once

#include "subjects.h"

#include <pthread.h>
#include <sqlite3.h>

typedef struct {
    const char     *db_name;
    subject_t       val;
    bool            dirty;
    pthread_mutex_t ditry_mux;
} cfg_item_t;

/* configuration struct. Should contain same types (for correct initialization) */
typedef struct {
    cfg_item_t key_tone;
    cfg_item_t vol;
} cfg_t;

extern cfg_t cfg;

int cfg_init(sqlite3 *db);

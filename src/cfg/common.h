#pragma once

#include "subjects.h"

typedef struct cfg_item_t {
    const char     *db_name;
    int             pk;
    Subject        *val;
    struct dirty_t *dirty;
    float           db_scale;
    int (*load)(struct cfg_item_t *item);
    int (*save)(struct cfg_item_t *item);
    void * (*db_to_val)(void *, Subject *);
    void * (*val_to_db)(void *, Subject *);
} cfg_item_t;

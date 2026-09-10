/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2024 Georgy Dyuldin aka R2RFE
 */

#pragma once

#include <stddef.h>
#include "pubsub_ids.h"

typedef void (* scheduler_fn_t)(void *);

#ifdef __cplusplus
extern "C" {
#endif

void scheduler_init();

/**
 * Schedule execution function in main thread
 */
void scheduler_put(scheduler_fn_t fn, void *arg, size_t arg_size);


/**
 * Schedule execution function without arguments in main thread
 */
void scheduler_put_noargs(scheduler_fn_t fn);

/**
 * Schedule sending lv_msg in main thread
 */
void scheduler_msg_send(enum msg_t id, void *user_data);

/**
 * Execute scheduled functions
 */
void scheduler_work();

#ifdef __cplusplus
}
#endif

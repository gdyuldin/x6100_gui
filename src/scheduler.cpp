/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2024 Georgy Dyuldin aka R2RFE
 */

#include "scheduler.h"

#include <queue>
#include <mutex>

extern "C" {
    #include "lvgl/lvgl.h"
}

#define QUEUE_MAX_SIZE  64

struct item_t {
    scheduler_fn_t fn;
    void    *arg;
};

static std::queue<item_t> queue;
static std::mutex m_mutex;

bool scheduler_put(scheduler_fn_t fn, void * arg, size_t arg_size) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (queue.size() > QUEUE_MAX_SIZE){
        LV_LOG_ERROR("Scheduler queue overflow");
        return false;
    }
    void *arg_copy = nullptr;
    if (arg_size) {
        arg_copy = malloc(arg_size);
        memcpy(arg_copy, arg, arg_size);
    }
    item_t item = {fn, arg_copy};
    queue.push(item);
    return true; // cppcheck-suppress memleak
}

bool scheduler_put_noargs(scheduler_fn_t fn) {
    return scheduler_put(fn, NULL, 0);
}

void scheduler_work() {
    item_t item;
    while (!queue.empty()) {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            item = queue.front();
            queue.pop();
        }
        item.fn(item.arg);
        if (item.arg) {
            free(item.arg);
        }
    }
}

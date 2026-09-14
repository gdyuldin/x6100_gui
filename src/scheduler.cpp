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
#include <thread>

#include "lvgl/lvgl.h"

#define QUEUE_MAX_SIZE  64


struct item_t {
    scheduler_fn_t fn;
    void    *arg;
};

struct msg_data_t {
    uint32_t id;
    void    *user_data;
};

struct scheduler_state_t {
    std::queue<item_t> queue;
    std::mutex         mutex;
};

static std::thread::id main_thread_id;

/*
 * The scheduler state outlives the process: it is allocated once and never
 * freed. This keeps the queue alive during process teardown, so a still
 * running producer thread (e.g. radio_thread) cannot push into a destroyed
 * queue.
 */
static scheduler_state_t &sched() {
    static scheduler_state_t *state = new scheduler_state_t();
    return *state;
}

static void msg_send_trampoline(void *arg);

void scheduler_put(scheduler_fn_t fn, void * arg, size_t arg_size) {
    std::lock_guard<std::mutex> lock(sched().mutex);
    if (sched().queue.size() > QUEUE_MAX_SIZE){
        LV_LOG_ERROR("Scheduler queue overflow");
        return;
    }
    void *arg_copy = nullptr;
    if (arg_size) {
        arg_copy = malloc(arg_size);
        memcpy(arg_copy, arg, arg_size);
    }
    item_t item = {fn, arg_copy};
    sched().queue.push(item);
    return; // cppcheck-suppress memleak
}

void scheduler_put_noargs(scheduler_fn_t fn) {
    return scheduler_put(fn, NULL, 0);
}

void scheduler_msg_send(msg_t id, void *user_data) {
    if (std::this_thread::get_id() == main_thread_id) {
        lv_msg_send(id, user_data);
    } else {
        msg_data_t data = msg_data_t{.id=id, user_data=user_data};
        scheduler_put(msg_send_trampoline, &data, sizeof(data));
    }
}

void scheduler_init() {
    main_thread_id = std::this_thread::get_id();
}

void scheduler_work() {
    item_t item;
    while (!sched().queue.empty()) {
        {
            std::lock_guard<std::mutex> lock(sched().mutex);
            item = sched().queue.front();
            sched().queue.pop();
        }
        item.fn(item.arg);
        if (item.arg) {
            free(item.arg);
        }
    }
}

static void msg_send_trampoline(void *arg) {
    if (!arg) {
        LV_LOG_ERROR("No data for scheduled msg send");
        return;
    }
    msg_data_t *data = static_cast<msg_data_t*>(arg);
    lv_msg_send(data->id, data->user_data);
}

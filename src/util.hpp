/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

#pragma once

#include <condition_variable>
#include <mutex>
#include <queue>
#include "cfg/encoder_bind_types.h"



template <typename T> inline T clip(T x, T min, T max) {
    if (x < min) {
        return min;
    } else if (x > max) {
        return max;
    }

    return x;
}

template <typename T, typename = typename std::enable_if<std::is_integral<T>::value, T>::type>
inline T align(T x, T step) {
    if (step == 0) {
        return x;
    }
    return x - (x % step);
}

template <typename T> class TSQueue {
  private:
    std::queue<T>           m_queue;
    std::mutex              m_mutex;
    std::condition_variable m_cond;

  public:
    void push(T item) {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_queue.push(item);
        m_cond.notify_one();
    }

    T pop() {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_cond.wait(lock, [this]() { return !m_queue.empty(); });
        T item = m_queue.front();
        m_queue.pop();
        return item;
    }
    bool empty() {
        std::unique_lock<std::mutex> lock(m_mutex);
        return m_queue.empty();
    }
};

cfg_ctrl_t loop_modes(int16_t dir, cfg_ctrl_t mode, const uint64_t mask, const std::vector<cfg_ctrl_t> all_modes);

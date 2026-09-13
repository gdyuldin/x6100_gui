#pragma once

#ifdef __cplusplus

#include <condition_variable>
#include <mutex>
#include <queue>

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

    // Non-blocking pop. Returns true if an item was dequeued, false if empty.
    bool try_pop(T &item) {
        std::unique_lock<std::mutex> lock(m_mutex);
        if (m_queue.empty()) {
            return false;
        }
        item = m_queue.front();
        m_queue.pop();
        return true;
    }

    bool empty() {
        std::unique_lock<std::mutex> lock(m_mutex);
        return m_queue.empty();
    }

    void clear() {
        std::unique_lock<std::mutex> lock(m_mutex);
        while (!m_queue.empty()) {
            m_queue.pop();
        }
    }
};

#endif

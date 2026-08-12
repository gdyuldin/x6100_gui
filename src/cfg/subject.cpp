#include "subject.h"

#include "../lvgl/lvgl.h"


void Observer::notify() {
    if (fn && subj) {
        fn(subj, user_data);
    }
}

void Observer::unsubscribe() {
    if (subj) {
        subj->unsubscribe(this);
        subj = nullptr;
    }
};

void ObserverDelayed::notify() {
    // Coalesce: only one deferred delivery per observer is queued at a time.
    // The pending entry carries no state to refresh, so a later notify() while
    // a delivery is already scheduled simply collapses into it (latest value
    // wins when the trampoline runs).
    bool expected = false;
    if (!scheduled_.compare_exchange_strong(expected, true)) {
        return;
    }
    lv_res_t res = lv_async_call(ObserverDelayed::async_trampoline, this);
    if (res != LV_RES_OK) {
        // Roll back so a later notify() retries.
        scheduled_.store(false);
    }
}

ObserverDelayed::~ObserverDelayed() {
    // Delete/unsubscribe happens only on the main thread, and async_trampoline
    // also runs on the main thread (lv_async), so the two never race here. The
    // atomic flag still closes the window against a background producer thread
    // calling notify() during destruction.
    if (scheduled_.exchange(false)) {
        lv_async_call_cancel(ObserverDelayed::async_trampoline, this);
    }
}

void ObserverDelayed::async_trampoline(void *user_data) {
    auto *obs = static_cast<ObserverDelayed *>(user_data);
    // Task is executing: nothing left to cancel, so release the schedule flag.
    obs->scheduled_.store(false);
    obs->Observer::notify();
}

Observer *Subject::subscribe(observer_cb fn, void *user_data) {
    const std::lock_guard<std::mutex> lock(mutex_subscribe);

    auto observer = new Observer(this, fn, user_data);
    observers.push_back(observer);
    return observer;
}
Observer *Subject::subscribe_and_notify(observer_cb fn, void *user_data) {
    auto *obs = subscribe(fn, user_data);
    obs->notify();
    return obs;
}

ObserverDelayed *Subject::subscribe_delayed(observer_cb fn, void *user_data) {
    const std::lock_guard<std::mutex> lock(mutex_subscribe);

    auto observer = new ObserverDelayed(this, fn, user_data);
    observers.push_back(observer);
    return observer;
}

ObserverDelayed *Subject::subscribe_delayed_and_notify(observer_cb fn, void *user_data) {
    auto *obs = subscribe_delayed(fn, user_data);
    static_cast<Observer *>(obs)->notify();
    return obs;
}

void Subject::unsubscribe(Observer *observer) {
    const std::lock_guard<std::mutex> lock(mutex_subscribe);
    observers.erase(std::find(observers.begin(), observers.end(), observer));
}

// Thread-local suppression state: a depth counter plus the queue of subjects
// that changed during a suppressed scope. One independent state per thread, so
// suppression is confined to the thread that created the guard.
static thread_local int                    suppress_depth_ = 0;
static thread_local std::vector<Subject *> suppressed_subjects_;

void Subject::push_suppress() {
    ++suppress_depth_;
}

void Subject::pop_suppress() {
    --suppress_depth_;
    if (suppress_depth_ != 0) {
        return; // nested scope: the outermost pop_suppress() delivers the batch
    }

    // Take the queued subjects into a local vector so a nested
    // push_suppress/pop_suppress (triggered from a callback below) uses a
    // freshly reset queue instead of corrupting this iteration.
    std::vector<Subject *> to_notify;
    to_notify.swap(suppressed_subjects_);

    // Deduplicate: the same subject may have changed several times within one
    // suppressed scope; deliver exactly one notification with its final value.
    std::sort(to_notify.begin(), to_notify.end());
    auto last = std::unique(to_notify.begin(), to_notify.end());

    for (auto it = to_notify.begin(); it != last; ++it) {
        (*it)->notify_impl();
    }
}

bool Subject::is_suppressed() {
    return suppress_depth_ > 0;
}

void Subject::notify() {
    if (is_suppressed()) {
        // The value is already updated inside SubjectT::set; defer the delivery
        // to pop_suppress(), which coalesces all changes of the scope.
        suppressed_subjects_.push_back(this);
        return;
    }
    notify_impl();
}

void Subject::notify_impl() {
    std::vector<Observer *> observers_copy;
    {
        const std::lock_guard<std::mutex> lock(mutex_subscribe);
        observers_copy = observers;
    }
    for (auto &observer : observers_copy) {
        observer->notify();
    }
}

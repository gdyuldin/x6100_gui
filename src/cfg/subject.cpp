#include "subject.h"

#include <queue>

void Observer::notify() {
    if (fn && subj) {
        fn(subj, user_data);
    }
}

void Observer::release() {
    if (refs_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        delete this;
    }
}

void Observer::unsubscribe() {
    if (subj) {
        subj->unsubscribe(this);
        // Clear the back-pointer before releasing the Subject's reference: the
        // release may destroy this observer, and the write to `subj` must happen
        // first.
        subj = nullptr;
        release();
    }
};

// ---- Delayed-observer queue (static, thread-safe) ----
// Producers (any thread) push; consumer (Subject::drain_delayed) drains on
// the main thread only. The queue is shallow — at most one entry per active
// ObserverDelayed because of the coalescing scheduled_ guard. Each queued entry
// holds one observer reference until drain()/shutdown() pops it.

static std::mutex                    delayed_mutex_;
static std::queue<ObserverDelayed *> delayed_queue_;

void ObserverDelayed::notify() {
    // Coalesce: only one deferred delivery per observer is queued at a time.
    bool expected = false;
    if (!scheduled_.compare_exchange_strong(expected, true)) {
        return;
    }
    // Hold a reference for as long as the observer is queued, so it cannot be
    // destroyed between the enqueue and the drain.
    add_ref();
    {
        std::lock_guard<std::mutex> lock(delayed_mutex_);
        delayed_queue_.push(this);
    }
}

ObserverDelayed::~ObserverDelayed() = default;

void ObserverDelayed::drain() {
    std::queue<ObserverDelayed *> batch;
    {
        std::lock_guard<std::mutex> lock(delayed_mutex_);
        batch.swap(delayed_queue_);
    }

    while (!batch.empty()) {
        auto *obs = batch.front();
        batch.pop();

        obs->scheduled_.store(false);

        // Observer::notify() is a no-op when the observer was already unsubscribed
        // (subj == nullptr). The queue reference keeps the observer alive across
        // the callback; release it afterwards.
        obs->Observer::notify();
        obs->release();
    }
}

void ObserverDelayed::shutdown() {
    std::queue<ObserverDelayed *> batch;
    {
        std::lock_guard<std::mutex> lock(delayed_mutex_);
        batch.swap(delayed_queue_);
    }

    // Drop every pending delivery without invoking callbacks and release the
    // queue reference. Observers that still have other references (Subject or
    // Subscription) are destroyed later by their owner.
    while (!batch.empty()) {
        auto *obs = batch.front();
        batch.pop();
        obs->scheduled_.store(false);
        obs->release();
    }
}

Observer *Subject::subscribe(observer_cb fn, void *user_data) {
    const std::lock_guard<std::mutex> lock(mutex_subscribe);

    auto observer = new Observer(this, fn, user_data);
    observers.push_back(observer);
    return observer;
}
Observer *Subject::subscribe_and_notify(observer_cb fn, void *user_data) {
    auto *obs = subscribe(fn, user_data);
    fn(this, user_data);
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
    fn(this, user_data);
    return obs;
}

void Subject::unsubscribe(Observer *observer) {
    const std::lock_guard<std::mutex> lock(mutex_subscribe);
    auto                              it = std::find(observers.begin(), observers.end(), observer);
    if (it != observers.end()) {
        observers.erase(it);
    }
}

Subject::~Subject() {
    // Detach every still-subscribed observer and release the Subject's
    // reference. Offsets the initial reference taken by subscribe(). Detaching
    // must happen under the lock, but the release itself is done outside of it:
    // the last release destroys the observer.
    std::vector<Observer *> detached;
    {
        const std::lock_guard<std::mutex> lock(mutex_subscribe);
        detached.swap(observers);
        for (Observer *o : detached) {
            o->subj = nullptr;
        }
    }
    for (Observer *o : detached) {
        o->release();
    }
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

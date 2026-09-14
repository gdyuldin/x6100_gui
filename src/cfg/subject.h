#pragma once

#include <algorithm>
#include <atomic>
#include <mutex>
#include <vector>

class Subject;

typedef void (*observer_cb)(Subject *, void *);

class Observer {
    friend class Subject;

  protected:
    Subject *subj;
    void (*fn)(Subject *, void *);
    void *user_data;

    // Intrusive reference count for the subscription lifetime. The initial
    // reference (refs_ == 1) is owned by the Subject that holds this observer in
    // its observer list. A Subscription, param_unsubscribe and the delayed queue
    // each hold one additional reference. The caller that releases the last
    // reference destroys the observer.
    std::atomic<int> refs_{1};

  public:
    Observer(Subject *subj, observer_cb fn, void *user_data) : subj(subj), fn(fn), user_data(user_data){};
    virtual ~Observer() = default;
    virtual void notify();

    // Ownership protocol (internal): acquire one reference.
    void add_ref() { refs_.fetch_add(1, std::memory_order_relaxed); }

    // Ownership protocol (internal): release one reference and destroy the
    // observer when it was the last one.
    void release();

    // Detach from the Subject and release the Subject's reference. A borrowed
    // observer (one returned by subscribe* but not wrapped in a Subscription or
    // otherwise referenced) is destroyed here; a Subscription keeps the observer
    // alive through its own reference until it is reset.
    void unsubscribe();

    // Accessor for the C-API layer: cfg_api's param_unsubscribe reads this to
    // free the per-subscription adapter stored as user_data.
    void *get_user_data() const { return user_data; }
};

class ObserverDelayed : public Observer {

  public:
    ObserverDelayed(Subject *subj, observer_cb fn, void *user_data) : Observer(subj, fn, user_data) {}
    ~ObserverDelayed() override;

    void notify() override;

    // Drain the delayed-observer queue on the main thread. Must be called
    // periodically from the main LVGL thread (same loop as lv_timer_handler).
    // Each queued ObserverDelayed gets its callback fired exactly once with the
    // latest value, then its queue reference is released.
    static void drain();

    // Clear the delayed queue at shutdown without invoking callbacks. Every
    // queued observer's queue reference is released; observers that still have
    // other references (Subject/Subscription) are destroyed later by their
    // owner. Call once from the main thread after all producers have stopped.
    static void shutdown();

  private:
    // Coalescing guard: true while one enqueue is pending for this observer.
    // A call to notify() while one is pending collapses into the single
    // queued delivery (latest value wins when drain() runs).
    std::atomic<bool> scheduled_{false};
};

// Move-only RAII handle for a subscription. Holds one reference to the Observer
// in addition to the Subject's reference, so the observer stays alive while the
// handle exists (even when the Subject is destroyed first). On reset or
// destruction it unsubscribes the observer and releases its reference: if that
// was the last reference, the observer is destroyed.
class Subscription {
  public:
    Subscription() = default;
    explicit Subscription(Observer *obs) : obs_(obs) {
        if (obs_) {
            obs_->add_ref();
        }
    }
    ~Subscription() { reset(); }

    Subscription(const Subscription &)            = delete;
    Subscription &operator=(const Subscription &) = delete;

    Subscription(Subscription &&other) noexcept : obs_(other.obs_) { other.obs_ = nullptr; }

    Subscription &operator=(Subscription &&other) noexcept {
        if (this != &other) {
            reset();
            obs_       = other.obs_;
            other.obs_ = nullptr;
        }
        return *this;
    }

    void reset() {
        if (obs_) {
            obs_->unsubscribe();
            obs_->release();
            obs_ = nullptr;
        }
    }

    Observer *get() const { return obs_; }
    Observer *operator->() const { return obs_; }

  private:
    Observer *obs_ = nullptr;
};

class Subject {
    // Observer::unsubscribe() removes itself through this method.
    friend class Observer;

    // Mutex to protect editing observers list
    std::mutex mutex_subscribe;

  protected:
    std::vector<Observer *> observers;

    // Notification invariant (do not break):
    // notify_impl() copies `observers` under mutex_subscribe and then runs each
    // callback OUTSIDE the lock. This is correct only because observers never
    // delete themselves or a peer during notification processing — deletion
    // happens later, from the main thread, outside the notify loop. Under that
    // invariant every pointer in the copy stays valid for the whole loop.
    //   - An observer must NOT unsubscribe+destroy itself or a peer from inside
    //     its own notify_impl() callback (or from a notify_impl() it triggers,
    //     e.g. via ComputedParameter::recompute / the ObserverDelayed
    //     trampoline).
    //   - subscribe()/unsubscribe() from within a callback take snapshot
    //     semantics: a newly added observer is not notified this round; a
    //     removed observer is still notified once. By design.
    //   - Cross-thread subscribe/unsubscribe is serialized with the copy by
    //     mutex_subscribe; callbacks run lock-free, so they must only read
    //     SubjectT values (which have their own mutexes).
    //   - ~Subject() detaches every still-subscribed observer and releases the
    //     Subject's reference, so observers that outlive the subject are either
    //     destroyed or kept alive solely by their Subscription reference.
    void notify_impl();

    // Notification entry point: during a suppressed scope (NotifySuppressGuard)
    // the subject is queued for a single delivery at pop_suppress() instead of
    // firing callbacks immediately; otherwise behaves exactly like notify_impl().
    void notify();

    // Destruction releases the Subject's reference on every still-subscribed
    // observer: each Observer::subj is cleared under mutex_subscribe and the
    // reference is released, so a borrowed observer is destroyed and a
    // Subscription-held one survives through its own reference. Contract: a
    // Subject must only be destroyed when no notify/subscribe is in flight on
    // another thread (notify_impl() copies the list under the lock but runs
    // callbacks unlocked).
    ~Subject();

    // Remove an observer from the list. Does NOT release the Subject's
    // reference; Observer::unsubscribe() / ~Subject() do that.
    void unsubscribe(Observer *o);

    // Force vtable for correct align with C-API opaque pointers
    virtual void force_vtable() {};

  public:
    // Nestable notification suppression (thread-local). While the depth is
    // non-zero, every notify() only records the subject in a thread-local
    // queue; pop_suppress() at depth 0 deduplicates the queue and delivers one
    // notify_impl() per unique subject, so a bulk update (memory load, band/
    // mode switch) fires exactly one callback per changed subject.
    static void push_suppress();
    static void pop_suppress();
    static bool is_suppressed();

    // Observer ownership: the Subject owns one reference while it holds the
    // observer in its list. The returned pointer is borrowed; wrap it in a
    // Subscription (or release it with param_unsubscribe) to keep an owned
    // reference.
    Observer        *subscribe(observer_cb fn, void *user_data = nullptr);
    Observer        *subscribe_and_notify(observer_cb fn, void *user_data = nullptr);
    ObserverDelayed *subscribe_delayed(observer_cb fn, void *user_data = nullptr);
    ObserverDelayed *subscribe_delayed_and_notify(observer_cb fn, void *user_data = nullptr);
};

// RAII wrapper for Subject::push_suppress/pop_suppress: suppresses all
// notifications for the whole scope and delivers one batch of coalesced
// callbacks (per changed subject) when the scope exits. Nestable.
class NotifySuppressGuard {
  public:
    NotifySuppressGuard() { Subject::push_suppress(); }
    ~NotifySuppressGuard() { Subject::pop_suppress(); }

    NotifySuppressGuard(const NotifySuppressGuard &)            = delete;
    NotifySuppressGuard &operator=(const NotifySuppressGuard &) = delete;
};

template <typename T> class SubjectT : public Subject {
    T                  val;
    mutable std::mutex mutex_;

  public:
    SubjectT(T val) : val(val){};

    const T get() const {
        std::lock_guard lock(mutex_);
        return val;
    };

    bool set(T new_val) {
        bool changed = false;
        {
            std::lock_guard lock(mutex_);
            if (val != new_val) {
                changed = true;
                val     = new_val;
            }
        }
        if (changed) {
            this->notify();
        }
        return changed;
    };
};

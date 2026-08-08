#pragma once

#include <mutex>
#include <vector>
#include <memory>
#include <atomic>
#include <algorithm>
// #include <type_traits>
// #include <thread>
// #include <functional>

class Subject;

typedef void (*observer_cb)(Subject *, void *);

class Observer {
  friend class Subject;

  protected:
    Subject *subj;
    void (*fn)(Subject *, void *);
    void *user_data;

  public:
    Observer(Subject *subj, observer_cb fn, void *user_data) : subj(subj), fn(fn), user_data(user_data) {};
    virtual ~Observer() = default;
    virtual void notify();

    void unsubscribe();

    // Accessor for the C-API layer: cfg_api's param_unsubscribe reads this to
    // free the per-subscription adapter stored as user_data.
    void* get_user_data() const { return user_data; }
};

// RAII-wrapper for deleting
struct ObserverDeleter {
    void operator()(Observer* obs) const {
        if (obs) {
            obs->unsubscribe();
            delete obs;
        }
    }
};

class ObserverDelayed : public Observer {

  public:
    ObserverDelayed(Subject *subj, observer_cb fn, void *user_data) : Observer(subj, fn, user_data) {}
    ~ObserverDelayed() override;

    void notify() override;

  private:
    // Coalescing guard: true while one lv_async_call is pending for this
    // observer. A call to notify() while one is pending collapses into the
    // single scheduled delivery (latest value wins).
    static void async_trampoline(void* user_data);
    std::atomic<bool> scheduled_{false};
};

using Subscription = std::unique_ptr<Observer, ObserverDeleter>;

class Subject {
    // Mutex to protect editing observers list
    std::mutex mutex_subscribe;


  protected:
    std::vector<Observer*> observers;

    // Notification invariant (do not break):
    // notify() copies `observers` under mutex_subscribe and then runs each
    // callback OUTSIDE the lock. This is correct only because observers never
    // delete themselves or a peer during notification processing — deletion
    // happens later, from the main thread, outside the notify loop. Under that
    // invariant every pointer in the copy stays valid for the whole loop.
    //   - An observer must NOT unsubscribe+destroy itself or a peer from inside
    //     its own notify() callback (or from a notify() it triggers, e.g. via
    //     ComputedParameter::recompute / the ObserverDelayed trampoline).
    //   - subscribe()/unsubscribe() from within a callback take snapshot
    //     semantics: a newly added observer is not notified this round; a
    //     removed observer is still notified once. By design.
    //   - Cross-thread subscribe/unsubscribe is serialized with the copy by
    //     mutex_subscribe; callbacks run lock-free, so they must only read
    //     SubjectT values (which have their own mutexes).
    void notify();

    // Subject is not designed to be deleted
    ~Subject() = default;

  public:
    // For C++ make sense to convert result to Subscription
    Observer* subscribe(observer_cb fn, void *user_data=nullptr);
    // For C++ make sense to convert result to Subscription
    ObserverDelayed* subscribe_delayed(observer_cb fn, void *user_data=nullptr);

    void unsubscribe(Observer *o);
};

template <typename T> class SubjectT : public Subject {
    T val;
    mutable std::mutex mutex_;

  public:
    SubjectT(T val) : val(val) {};

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
                val = new_val;
            }
        }
        if (changed) {
            this->notify();
        }
        return changed;
    };
};

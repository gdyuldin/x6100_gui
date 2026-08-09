#pragma once

#include <atomic>
#include <functional>
#include <vector>

#include "subject.h"

template <typename T>
class ComputedParameter : public appcfg::SubjectT<T> {
  public:
    using ComputeFn = std::function<T()>;
    using ReverseFn = std::function<void(const T&)>;

    // No default value: the initial value is computed immediately from the
    // sources in the base initializer list, before this object fully exists.
    ComputedParameter(ComputeFn compute, ReverseFn reverse = {})
        : appcfg::SubjectT<T>(compute ? compute() : T{}),
          compute_(std::move(compute)),
          reverse_(std::move(reverse)) {}

    // May be called multiple times — one subscription per source. The
    // Subscription is stored and released by the destructor (RAII): for
    // static global parameters this means the subscription is effectively
    // permanent, while stack-allocated instances (tests) clean up properly.
    void bind(appcfg::Subject& source) {
        subscriptions_.push_back(appcfg::Subscription(source.subscribe(source_notify, this)));
    }

    ~ComputedParameter() {
        subscriptions_.clear();  // unsubscribe + delete all bound observers
    }

    // Recompute from compute_() and notify subscribers only when the value
    // actually changed (comparison happens inside SubjectT::set).
    void recompute() {
        if (updating_.exchange(true)) {
            return; // re-entrant call, already inside a recompute/set
        }
        appcfg::SubjectT<T>::set(compute_());
        updating_.store(false);
    }

    // Hides the non-virtual appcfg::SubjectT<T>::set. Routes the value through the
    // optional reverse function (which may update the sources) and then
    // recomputes the final value. If the value already equals the current
    // one, reverse_ is skipped and the sources are left untouched.
    // Access ComposedParameter via its concrete type, never through a
    // appcfg::SubjectT<T>& reference, so that this set() is used.
    void set(const T& value) {
        if (updating_.exchange(true)) {
            return; // re-entrant call, already inside a recompute/set
        }
        if (value == appcfg::SubjectT<T>::get()) {
            updating_.store(false);
            return; // no change, skip reverse_ and recompute
        }
        if (reverse_) {
            reverse_(value);
        }
        appcfg::SubjectT<T>::set(compute_());
        updating_.store(false);
    }

  private:
    // observer_cb-compatible trampoline: user_data is `this`.
    static void source_notify(appcfg::Subject* /*subj*/, void* user_data) {
        static_cast<ComputedParameter*>(user_data)->recompute();
    }

    ComputeFn compute_;
    ReverseFn reverse_;
    std::atomic<bool> updating_{false};
    std::vector<appcfg::Subscription> subscriptions_;
};

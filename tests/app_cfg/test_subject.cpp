// test_subject.cpp
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <thread>
#include <vector>
#include "subject.h"  // SubjectT, Observer, Subscription, ObserverDeleter
#include "lvgl.h"     // lv_init / lv_timer_handler for delayed-notify tests


struct TestObserver {
    std::vector<int> values;
    void callback(Subject* subj, void* self) {
        auto* s = static_cast<SubjectT<int>*>(subj);
        values.push_back(s->get());
    }
    static void staticCallback(Subject* subj, void* user) {
        auto* self = static_cast<TestObserver*>(user);
        self->callback(subj, nullptr);
    }
};

TEST_CASE("SubjectT basic get/set", "[subject]") {
    SubjectT<int> s(42);
    REQUIRE(s.get() == 42);
    s.set(100);
    REQUIRE(s.get() == 100);
}

TEST_CASE("SubjectT set same value does not notify", "[subject]") {
    SubjectT<int> s(10);
    TestObserver obs;
    auto sub = s.subscribe(TestObserver::staticCallback, &obs);
    s.set(10);
    REQUIRE(obs.values.empty());
    delete sub;
}

TEST_CASE("SubjectT notifies observer on change", "[subject]") {
    SubjectT<int> s(0);
    TestObserver obs;
    auto sub = s.subscribe(TestObserver::staticCallback, &obs);
    s.set(5);
    REQUIRE(obs.values == std::vector<int>{5});
    s.set(10);
    REQUIRE(obs.values == std::vector<int>{5, 10});
    delete sub;
}

TEST_CASE("Subscription RAII unsubscribe", "[subject]") {
    SubjectT<int> s(0);
    TestObserver obs;
    {
        Subscription sub{ s.subscribe(TestObserver::staticCallback, &obs) };
        s.set(1);
        REQUIRE(obs.values == std::vector<int>{1});
    }
    s.set(2);
    REQUIRE(obs.values == std::vector<int>{1});
}

TEST_CASE("Unsubscribe observer", "[subject]") {
    SubjectT<int> s(0);
    TestObserver obs;
    auto sub = s.subscribe(TestObserver::staticCallback, &obs);
    s.set(5);
    REQUIRE(obs.values == std::vector<int>{5});
    sub->unsubscribe();
    s.set(10);
    REQUIRE(obs.values == std::vector<int>{5});
    delete sub;
}

TEST_CASE("Multiple observers", "[subject]") {
    SubjectT<int> s(0);
    TestObserver obs1, obs2;
    auto sub1 = s.subscribe(TestObserver::staticCallback, &obs1);
    auto sub2 = s.subscribe(TestObserver::staticCallback, &obs2);
    s.set(42);
    REQUIRE(obs1.values == std::vector<int>{42});
    REQUIRE(obs2.values == std::vector<int>{42});

    delete sub1;
    delete sub2;
}

TEST_CASE("Observer manual unsubscribe", "[subject]") {
    SubjectT<int> s(0);
    TestObserver obs;
    Observer* raw = s.subscribe(TestObserver::staticCallback, &obs);
    s.set(1);
    REQUIRE(obs.values.size() == 1);
    raw->unsubscribe();
    s.set(2);
    REQUIRE(obs.values.size() == 1);
    delete raw;
}

TEST_CASE("Concurrent set/get integrity", "[subject][threads]") {
    SubjectT<int> s(0);
    const int iterations = 1000;
    std::atomic<bool> start{false};

    auto writer = [&]() {
        while (!start.load()) {}
        for (int i = 0; i < iterations; ++i) {
            s.set(i);
        }
    };
    auto reader = [&]() {
        while (!start.load()) {}
        for (int i = 0; i < iterations; ++i) {
            int val = s.get();
            REQUIRE(val >= 0);
            REQUIRE(val < iterations);
        }
    };

    std::thread t1(writer);
    std::thread t2(reader);
    start.store(true);
    t1.join();
    t2.join();
}

// Delayed observers deliver through lvgl's async queue. lv_init() makes that
// queue functional in the test process; pending calls are run by
// lv_timer_handler().

TEST_CASE("ObserverDelayed coalesces many sets into one latest delivery", "[subject][delayed]") {
    lv_init();
    SubjectT<int> s(0);
    TestObserver obs;
    Subscription sub{ s.subscribe_delayed(TestObserver::staticCallback, &obs) };

    // Several rapid sets at once: the deferred delivery must collapse.
    s.set(1);
    s.set(2);
    s.set(3);

    // Nothing delivered yet (async work is pending, not run inline).
    REQUIRE(obs.values.empty());

    lv_timer_handler();

    // Exactly one callback, carrying the final (latest) value.
    REQUIRE(obs.values == std::vector<int>{3});
}

TEST_CASE("ObserverDelayed cancels pending delivery on destruction", "[subject][delayed]") {
    lv_init();
    SubjectT<int> s(0);
    TestObserver obs;
    {
        Subscription sub{ s.subscribe_delayed(TestObserver::staticCallback, &obs) };
        s.set(5);  // schedules a deferred delivery
        // Subscription is destroyed here: the pending async call is cancelled
        // so the stale observer is never invoked after it is gone.
    }
    lv_timer_handler();
    REQUIRE(obs.values.empty());
}


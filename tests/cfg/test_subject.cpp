// test_subject.cpp
#include <catch2/catch_test_macros.hpp>

#include "subject.h" // SubjectT, Observer, Subscription
#include <atomic>
#include <thread>
#include <vector>

struct TestObserver {
    std::vector<int> values;
    void             callback(Subject *subj, void *self) {
        auto *s = static_cast<SubjectT<int> *>(subj);
        values.push_back(s->get());
    }
    static void staticCallback(Subject *subj, void *user) {
        auto *self = static_cast<TestObserver *>(user);
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
    TestObserver  obs;
    Subscription  sub{s.subscribe(TestObserver::staticCallback, &obs)};
    s.set(10);
    REQUIRE(obs.values.empty());
}

TEST_CASE("SubjectT notifies observer on change", "[subject]") {
    SubjectT<int> s(0);
    TestObserver  obs;
    Subscription  sub{s.subscribe(TestObserver::staticCallback, &obs)};
    s.set(5);
    REQUIRE(obs.values == std::vector<int>{5});
    s.set(10);
    REQUIRE(obs.values == std::vector<int>{5, 10});
}

TEST_CASE("Subscription RAII unsubscribe", "[subject]") {
    SubjectT<int> s(0);
    TestObserver  obs;
    {
        Subscription sub{s.subscribe(TestObserver::staticCallback, &obs)};
        s.set(1);
        REQUIRE(obs.values == std::vector<int>{1});
    }
    s.set(2);
    REQUIRE(obs.values == std::vector<int>{1});
}

TEST_CASE("Unsubscribe observer", "[subject]") {
    SubjectT<int> s(0);
    TestObserver  obs;
    // A borrowed observer (no Subscription): unsubscribe() releases the only
    // reference and destroys it.
    Observer *sub = s.subscribe(TestObserver::staticCallback, &obs);
    s.set(5);
    REQUIRE(obs.values == std::vector<int>{5});
    sub->unsubscribe();
    s.set(10);
    REQUIRE(obs.values == std::vector<int>{5});
}

TEST_CASE("Multiple observers", "[subject]") {
    SubjectT<int> s(0);
    TestObserver  obs1, obs2;
    Subscription  sub1{s.subscribe(TestObserver::staticCallback, &obs1)};
    Subscription  sub2{s.subscribe(TestObserver::staticCallback, &obs2)};
    s.set(42);
    REQUIRE(obs1.values == std::vector<int>{42});
    REQUIRE(obs2.values == std::vector<int>{42});
}

TEST_CASE("Observer manual unsubscribe", "[subject]") {
    SubjectT<int> s(0);
    TestObserver  obs;
    Observer     *raw = s.subscribe(TestObserver::staticCallback, &obs);
    s.set(1);
    REQUIRE(obs.values.size() == 1);
    raw->unsubscribe();
    s.set(2);
    REQUIRE(obs.values.size() == 1);
}

TEST_CASE("Concurrent set/get integrity", "[subject][threads]") {
    SubjectT<int>     s(0);
    const int         iterations = 1000;
    std::atomic<bool> start{false};

    auto writer = [&]() {
        while (!start.load()) {
        }
        for (int i = 0; i < iterations; ++i) {
            s.set(i);
        }
    };
    auto reader = [&]() {
        while (!start.load()) {
        }
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
// ObserverDelayed::drain().

TEST_CASE("ObserverDelayed coalesces many sets into one latest delivery", "[subject][delayed]") {
    SubjectT<int> s(0);
    TestObserver  obs;
    Subscription  sub{s.subscribe_delayed(TestObserver::staticCallback, &obs)};

    // Several rapid sets at once: the deferred delivery must collapse.
    s.set(1);
    s.set(2);
    s.set(3);

    // Nothing delivered yet (async work is pending, not run inline).
    REQUIRE(obs.values.empty());

    ObserverDelayed::drain();

    // Exactly one callback, carrying the final (latest) value.
    REQUIRE(obs.values == std::vector<int>{3});
}

TEST_CASE("ObserverDelayed cancels pending delivery on destruction", "[subject][delayed]") {
    SubjectT<int> s(0);
    TestObserver  obs;
    {
        Subscription sub{s.subscribe_delayed(TestObserver::staticCallback, &obs)};
        s.set(5); // schedules a deferred delivery
        // Subscription is destroyed here: the pending async call is cancelled
        // so the stale observer is never invoked after it is gone.
    }
    ObserverDelayed::drain();
    REQUIRE(obs.values.empty());
}

// Notification suppression: push_suppress/pop_suppress (and the NotifySuppressGuard
// RAII wrapper) defer all observed changes of a scope into one batch delivery.

TEST_CASE("Suppression defers notifications until pop", "[subject][suppress]") {
    SubjectT<int> s(0);
    TestObserver  obs;
    Subscription  sub{s.subscribe(TestObserver::staticCallback, &obs)};

    Subject::push_suppress();
    s.set(5);
    s.set(10);
    REQUIRE(obs.values.empty());
    Subject::pop_suppress();

    // One deferred delivery carrying the final value.
    REQUIRE(obs.values == std::vector<int>{10});
}

TEST_CASE("Suppression deduplicates many sets of one subject", "[subject][suppress]") {
    SubjectT<int> s(0);
    TestObserver  obs;
    Subscription  sub{s.subscribe(TestObserver::staticCallback, &obs)};

    Subject::push_suppress();
    s.set(1);
    s.set(2);
    s.set(3);
    Subject::pop_suppress();

    // Exactly one callback, with the last value.
    REQUIRE(obs.values == std::vector<int>{3});
}

TEST_CASE("Suppression notifies each changed subject once", "[subject][suppress]") {
    SubjectT<int> s1(0), s2(0), s3(0);
    TestObserver  obs1, obs2, obs3;
    Subscription  sub1{s1.subscribe(TestObserver::staticCallback, &obs1)};
    Subscription  sub2{s2.subscribe(TestObserver::staticCallback, &obs2)};
    Subscription  sub3{s3.subscribe(TestObserver::staticCallback, &obs3)};

    Subject::push_suppress();
    s1.set(1);
    s2.set(2);
    s3.set(3);
    Subject::pop_suppress();

    REQUIRE(obs1.values == std::vector<int>{1});
    REQUIRE(obs2.values == std::vector<int>{2});
    REQUIRE(obs3.values == std::vector<int>{3});
}

TEST_CASE("Nested suppression batches into a single delivery", "[subject][suppress]") {
    SubjectT<int> s(0);
    TestObserver  obs;
    Subscription  sub{s.subscribe(TestObserver::staticCallback, &obs)};

    Subject::push_suppress();
    {
        Subject::push_suppress();
        s.set(1);
        Subject::pop_suppress(); // inner: depth still > 0, nothing delivered
        REQUIRE(obs.values.empty());
        s.set(2);
    }
    Subject::pop_suppress(); // outer: one coalesced delivery

    REQUIRE(obs.values == std::vector<int>{2});
}

TEST_CASE("Suppression delivers nothing for an unchanged value", "[subject][suppress]") {
    SubjectT<int> s(5);
    TestObserver  obs;
    Subscription  sub{s.subscribe(TestObserver::staticCallback, &obs)};

    Subject::push_suppress();
    s.set(5); // no change -> no notify() -> not queued
    Subject::pop_suppress();

    REQUIRE(obs.values.empty());
}

TEST_CASE("NotifySuppressGuard RAII suppresses the whole scope", "[subject][suppress]") {
    SubjectT<int> s(0);
    TestObserver  obs;
    Subscription  sub{s.subscribe(TestObserver::staticCallback, &obs)};

    {
        NotifySuppressGuard guard;
        s.set(7);
        REQUIRE(obs.values.empty());
    }
    // Guard destructor called pop_suppress().
    REQUIRE(obs.values == std::vector<int>{7});
}

TEST_CASE("ObserverDelayed keeps one coalesced delivery through suppression", "[subject][suppress][delayed]") {
    SubjectT<int> s(0);
    TestObserver  obs;
    Subscription  sub{s.subscribe_delayed(TestObserver::staticCallback, &obs)};

    Subject::push_suppress();
    s.set(1);
    s.set(2);
    Subject::pop_suppress();
    REQUIRE(obs.values.empty()); // delivery is async

    ObserverDelayed::drain();
    REQUIRE(obs.values == std::vector<int>{2});
}

// Subject lifetime: the Subject owns one reference per subscribed observer
// while it holds it in its list. A borrowed observer (subscribe* result kept as
// a raw pointer) is destroyed together with the Subject; a Subscription holds an
// extra reference and may outlive the Subject (e.g. a static Subscription
// destroyed after a global SettingsManager). ~Subject() detaches every observer
// and releases the Subject's reference, so the late unsubscribe is a no-op and
// never touches freed memory (checked by the test build's ASan/UBSan).

TEST_CASE("Subscription outliving its Subject is safe", "[subject][lifetime]") {
    auto        *s = new SubjectT<int>(0);
    TestObserver obs;
    Subscription sub{s->subscribe(TestObserver::staticCallback, &obs)};

    delete s; // subject destroyed first; sub destructor runs at scope exit
    REQUIRE(obs.values.empty());
}

TEST_CASE("Subject destruction unlinks multiple observers", "[subject][lifetime]") {
    auto        *s = new SubjectT<int>(0);
    TestObserver obs1, obs2;
    Subscription sub1{s->subscribe(TestObserver::staticCallback, &obs1)};
    Subscription sub2{s->subscribe(TestObserver::staticCallback, &obs2)};

    delete s;
    REQUIRE(obs1.values.empty());
    REQUIRE(obs2.values.empty());
}

TEST_CASE("Subject destruction frees borrowed observers", "[subject][lifetime]") {
    // Borrowed observers (fire-and-forget subscribe*) are owned by the Subject:
    // deleting it releases the only reference and destroys them. A leak here is
    // reported by the test build's LeakSanitizer.
    auto        *s = new SubjectT<int>(0);
    TestObserver obs;
    s->subscribe(TestObserver::staticCallback, &obs);
    s->subscribe_delayed(TestObserver::staticCallback, &obs);

    delete s;
    REQUIRE(obs.values.empty());
}

TEST_CASE("Queued ObserverDelayed skipped when Subject is destroyed", "[subject][lifetime][delayed]") {
    auto        *s = new SubjectT<int>(0);
    TestObserver obs;
    Subscription sub{s->subscribe_delayed(TestObserver::staticCallback, &obs)};

    s->set(5); // schedules a deferred delivery
    delete s;  // subject dies while the observer is still queued

    ObserverDelayed::drain(); // stale observer must be skipped, not invoked
    REQUIRE(obs.values.empty());
}

TEST_CASE("ObserverDelayed::shutdown clears pending deliveries without invoking callbacks",
          "[subject][lifetime][delayed]") {
    auto        *s = new SubjectT<int>(0);
    TestObserver obs;
    Subscription sub{s->subscribe_delayed(TestObserver::staticCallback, &obs)};

    s->set(5);                   // schedules a deferred delivery
    ObserverDelayed::shutdown(); // drops it without firing the callback
    REQUIRE(obs.values.empty());

    delete s; // still subscribed: Subject releases its reference, sub the other
}

TEST_CASE("Subscription destroyed before its Subject keeps normal behaviour", "[subject][lifetime]") {
    auto        *s = new SubjectT<int>(0);
    TestObserver obs;
    {
        Subscription sub{s->subscribe(TestObserver::staticCallback, &obs)};
        s->set(1);
        REQUIRE(obs.values == std::vector<int>{1});
    } // sub unsubscribes here
    delete s;
    REQUIRE(obs.values == std::vector<int>{1});
}


// test_computed.cpp
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <vector>

#include "computed_api.h"       // C-compatible opaque-types and set/get functions
#include "computed_parameter.h" // ComputedParameter<T>
#include "subject.h"            // SubjectT, Observer, Subscription


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

TEST_CASE("ComputedParameter computes initial value in constructor", "[computed]") {
    int source = 10;
    ComputedParameter<int> cp([&] { return source * 3; });
    REQUIRE(cp.get() == 30);
}

TEST_CASE("ComputedParameter explicit recompute", "[computed]") {
    int source = 1;
    ComputedParameter<int> cp([&] { return source * 2; });
    REQUIRE(cp.get() == 2);

    source = 50;
    REQUIRE(cp.get() == 2);  // not subscribed yet, value is stale

    cp.recompute();
    REQUIRE(cp.get() == 100);
}

TEST_CASE("ComputedParameter recomputes on source change", "[computed]") {
    SubjectT<int> source(10);
    ComputedParameter<int> cp([&] { return source.get() + 5; });
    cp.bind(source);
    REQUIRE(cp.get() == 15);

    source.set(100);
    REQUIRE(cp.get() == 105);
}

TEST_CASE("ComputedParameter aggregates multiple sources", "[computed]") {
    SubjectT<int> a(1), b(2);
    ComputedParameter<int> cp([&] { return a.get() + b.get(); });
    cp.bind(a);
    cp.bind(b);
    REQUIRE(cp.get() == 3);

    a.set(10);
    REQUIRE(cp.get() == 12);
    b.set(5);
    REQUIRE(cp.get() == 15);
}

TEST_CASE("ComputedParameter notifies its subscribers", "[computed]") {
    SubjectT<int> source(5);
    ComputedParameter<int> cp([&] { return source.get() * 2; });
    cp.bind(source);

    TestObserver obs;
    auto sub = cp.subscribe(TestObserver::staticCallback, &obs);
    source.set(7);
    REQUIRE(obs.values == std::vector<int>{14});
    delete sub;
}

TEST_CASE("ComputedParameter does not notify when recomputed value is equal", "[computed]") {
    SubjectT<int> a(5);
    int compute_evals = 0;
    ComputedParameter<int> cp([&] {
        ++compute_evals;
        return 10;  // constant: every recompute yields the same value
    });
    REQUIRE(cp.get() == 10);
    REQUIRE(compute_evals == 1);

    TestObserver obs;
    auto sub = cp.subscribe(TestObserver::staticCallback, &obs);
    cp.bind(a);

    a.set(10);  // recompute -> 10, same as current value -> no notify
    REQUIRE(obs.values.empty());
    REQUIRE(compute_evals == 2);

    a.set(7);  // recompute -> still 10 -> still no notify
    REQUIRE(obs.values.empty());
    REQUIRE(compute_evals == 3);
    delete sub;
}

TEST_CASE("ComputedParameter set routes through reverse fn", "[computed]") {
    SubjectT<int> source(10);
    int reverse_calls = 0;
    ComputedParameter<int> cp(
        [&] { return source.get() + 1; },
        [&](const int& v) {
            ++reverse_calls;
            source.set(v - 1);  // triggers recompute, guard prevents recursion
        });
    cp.bind(source);

    REQUIRE(cp.get() == 11);
    cp.set(100);
    REQUIRE(cp.get() == 100);   // reverse set source to 99, compute returns 100
    REQUIRE(source.get() == 99);
    REQUIRE(reverse_calls == 1);
}

TEST_CASE("ComputedParameter set same value skips reverse fn", "[computed]") {
    SubjectT<int> source(10);
    int reverse_calls = 0;
    ComputedParameter<int> cp(
        [&] { return source.get() + 1; },
        [&](const int& v) {
            ++reverse_calls;
            source.set(v - 1);
        });
    cp.bind(source);
    REQUIRE(cp.get() == 11);

    // Setting the current value must not invoke reverse and must not touch the source.
    cp.set(11);
    REQUIRE(cp.get() == 11);
    REQUIRE(reverse_calls == 0);
    REQUIRE(source.get() == 10);

    // Same via cp.get() round-trip.
    cp.set(cp.get());
    REQUIRE(reverse_calls == 0);
    REQUIRE(source.get() == 10);

    // A real change still goes through reverse.
    cp.set(20);
    REQUIRE(reverse_calls == 1);
    REQUIRE(cp.get() == 20);
    REQUIRE(source.get() == 19);
}

TEST_CASE("ComputedParameter self-binding does not recurse", "[computed]") {
    SubjectT<int> source(1);
    ComputedParameter<int> cp([&] { return source.get(); });
    cp.bind(source);
    cp.bind(cp);  // self-binding exercises the re-entrancy guard

    source.set(5);  // would recurse without the update guard
    REQUIRE(cp.get() == 5);
}

TEST_CASE("ComputedParameter C-API int set/get", "[computed][c_api]") {
    SubjectT<int32_t> source(10);
    int32_t captured = 0;
    ComputedParameter<int32_t> cp(
        [&] { return source.get() + 1; },
        [&](const int32_t& v) { captured = v; source.set(v - 1); });
    cp.bind(source);

    computed_param_int_set(reinterpret_cast<ComputedParamInt*>(&cp), 5);
    REQUIRE(captured == 5);
    REQUIRE(computed_param_int_get(reinterpret_cast<ComputedParamInt*>(&cp)) == 5);
    REQUIRE(source.get() == 4);
}

TEST_CASE("ComputedParameter C-API float set/get", "[computed][c_api]") {
    SubjectT<float> source(1.0f);
    float captured = 0.0f;
    ComputedParameter<float> cp(
        [&] { return source.get() * 2.0f; },
        [&](const float& v) { captured = v; source.set(v / 2.0f); });
    cp.bind(source);
    REQUIRE(cp.get() == 2.0f);

    computed_param_float_set(reinterpret_cast<ComputedParamFloat*>(&cp), 3.0f);
    REQUIRE(captured == 3.0f);
    REQUIRE(computed_param_float_get(reinterpret_cast<ComputedParamFloat*>(&cp)) == 3.0f);
    REQUIRE(source.get() == 1.5f);
}

TEST_CASE("ComputedParameter C-API text set", "[computed][c_api]") {
    SubjectT<std::string> source("hello");
    std::string captured;
    ComputedParameter<std::string> cp(
        [&] { return source.get() + "!"; },
        [&](const std::string& v) { captured = v; source.set(v.substr(0, v.size() - 1)); });
    cp.bind(source);
    REQUIRE(cp.get() == "hello!");

    computed_param_text_set(reinterpret_cast<ComputedParamText*>(&cp), "world");
    REQUIRE(captured == "world");
    // The stored value after a reverse fn is always the recomputed value,
    // not the raw input: reverse("world") -> source "worl" -> compute "worl!".
    REQUIRE(cp.get() == "worl!");
    REQUIRE(source.get() == "worl");
}

// Tests for cw::ToneLevelTracker: mark-level reference attack/seed, the
// slew-limited release that follows slow fades but not the key-up edge, and the
// reference kept across gaps (re-seeded at the first ON frame).

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "cw_config.h"
#include "tone_level_tracker.h"

using Catch::Approx;

namespace {

float db(float ratio) {
    return 10.0f * std::log10(ratio);
}

} // namespace

TEST_CASE("tone level tracker: seed and attack on a rising mark") {
    cw::ToneLevelTracker tr;
    REQUIRE_FALSE(tr.ref_valid());

    // First ON frame seeds the reference at the current level.
    tr.observe(1.0f, true);
    REQUIRE(tr.ref_valid());
    REQUIRE(tr.norm_db(1.0f) == Approx(0.0f));

    // A stronger frame within the attack bound raises the reference with it.
    tr.observe(2.0f, true);
    REQUIRE(tr.norm_db(2.0f) == Approx(0.0f));
    REQUIRE(tr.norm_db(1.0f) == Approx(-3.01f).margin(0.02f));
}

TEST_CASE("tone level tracker: attack bound rejects a single-frame spike") {
    cw::ToneLevelTracker tr;
    tr.observe(1.0f, true);

    // A +30 dB one-frame spike raises the reference by at most LEVEL_ATTACK_DB.
    tr.observe(1000.0f, true);
    const float max_level = 1.0f * std::pow(10.0f, cw::LEVEL_ATTACK_DB / 10.0f);
    REQUIRE(tr.norm_db(max_level) == Approx(0.0f).margin(0.01f));

    // The next normal frame is still above the classifier's release level, so
    // the spike does not cut the mark.
    tr.observe(1.0f, true);
    REQUIRE(tr.norm_db(1.0f) > -9.0f);
}

TEST_CASE("tone level tracker: slew-limited release does not chase key-up") {
    cw::ToneLevelTracker tr;
    tr.observe(4.0f, true);

    // One frame at noise level: the key-up edge must not drag the reference
    // down. The release is bounded to LEVEL_RELEASE_DB per frame.
    tr.observe(0.001f, true);

    const float expected_level = 4.0f * std::pow(10.0f, -cw::LEVEL_RELEASE_DB / 10.0f);
    REQUIRE(tr.norm_db(expected_level) == Approx(0.0f).margin(0.01f));
    REQUIRE(tr.norm_db(0.001f) < -30.0f); // still far below the reference
}

TEST_CASE("tone level tracker: release follows a slow fade") {
    cw::ToneLevelTracker tr;
    tr.observe(4.0f, true);

    // Ten frames of a slow fade: the reference tracks the signal down.
    float power = 4.0f;
    for (int i = 0; i < 10; ++i) {
        power *= std::pow(10.0f, -cw::LEVEL_RELEASE_DB / 10.0f);
        tr.observe(power, true);
        REQUIRE(tr.norm_db(power) == Approx(0.0f).margin(0.01f));
    }
}

TEST_CASE("tone level tracker: holds while OFF inside the hysteresis band") {
    cw::ToneLevelTracker tr;
    tr.observe(4.0f, true);
    const float level = tr.norm_db(4.0f);

    // Not a mark (classifier OFF): the reference is held, so the tracker does
    // not follow a level it does not consider a signal.
    tr.observe(0.5f, false);
    REQUIRE(tr.ref_valid());
    REQUIRE(tr.norm_db(4.0f) == Approx(level));
    REQUIRE(tr.norm_db(0.5f) == Approx(db(0.5f / 4.0f)).margin(0.01f));
}

TEST_CASE("tone level tracker: keeps the reference when the signal ends") {
    cw::ToneLevelTracker tr;
    tr.observe(4.0f, true);
    tr.observe(0.001f, false);

    // The reference is frozen across the gap so the classifier can interpolate
    // the next front edge against the previous envelope.
    REQUIRE(tr.ref_valid());
    REQUIRE(tr.norm_db(4.0f) == Approx(0.0f));
    REQUIRE(tr.norm_db(0.001f) < -30.0f);
}

TEST_CASE("tone level tracker: a weak mark after a strong one is not blocked") {
    cw::ToneLevelTracker tr;

    // Strong mark.
    tr.observe(100.0f, true);
    // Strong mark ends: the reference is kept at the strong level (-20 dB
    // relative to a weak mark, below the classifier's release level). The
    // classifier acquires on the absolute gate, so this must not matter.
    tr.observe(0.001f, false);
    REQUIRE(tr.ref_valid());
    REQUIRE(tr.norm_db(1.0f) == Approx(-20.0f).margin(0.1f));

    // Weak mark: re-seeded at its own level, so subsequent frames are relative
    // to the weak mark.
    tr.observe(1.0f, true);
    REQUIRE(tr.ref_valid());
    REQUIRE(tr.norm_db(1.0f) == Approx(0.0f));
}

// Tests for cw::TimeClassifier: Schmitt-trigger intervals, dot/dash and
// element/letter/word separation and the adaptive WPM estimate.
//
// Note: the classifier shares one leak counter for both histograms and the
// decision boundaries adapt as samples accumulate, so tests first run a short
// 20 WPM simulation to let the thresholds settle.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <vector>

#include "time_classifier.h"

using Catch::Approx;

namespace {

constexpr float ON_LLR  = 5.0f;
constexpr float OFF_LLR = -5.0f;

void feed(cw::TimeClassifier &tc, bool on, int frames, std::vector<cw::Token> *out = nullptr) {
    for (int i = 0; i < frames; ++i) {
        cw::Token t = tc.feed_frame_llr(on ? ON_LLR : OFF_LLR);
        if (out != nullptr && t != cw::CW_NONE)
            out->push_back(t);
    }
}

// Feeds `on_frames` marks and one space frame; returns the token emitted at the
// closing edge (the mark classification).
cw::Token close_mark(cw::TimeClassifier &tc, int on_frames) {
    for (int i = 0; i < on_frames; ++i)
        tc.feed_frame_llr(ON_LLR);
    return tc.feed_frame_llr(OFF_LLR);
}

// Feeds a mark of `on_frames`, then a space of `off_frames`, then one mark
// frame; returns the token emitted at the closing edge (the space
// classification).
cw::Token classify_space(cw::TimeClassifier &tc, int on_frames, int off_frames) {
    for (int i = 0; i < on_frames; ++i)
        tc.feed_frame_llr(ON_LLR);
    for (int i = 0; i < off_frames; ++i)
        tc.feed_frame_llr(OFF_LLR);
    return tc.feed_frame_llr(ON_LLR);
}

// 20 WPM: dot = 1200/20 = 60 ms = 6 frames of 10 ms, element space likewise.
void adapt_20_wpm(cw::TimeClassifier &tc) {
    for (int rep = 0; rep < 20; ++rep) {
        feed(tc, true, 6);
        feed(tc, false, 6);
    }
}

} // namespace

TEST_CASE("time classifier: 20 WPM dot and element space") {
    cw::TimeClassifier     tc;
    std::vector<cw::Token> tokens;

    for (int rep = 0; rep < 20; ++rep) {
        feed(tc, true, 6, &tokens);
        feed(tc, false, 6, &tokens);
    }

    REQUIRE(tokens.size() >= 4);
    REQUIRE(tokens[0] == cw::CW_DOT);
    REQUIRE(tokens[1] == cw::CW_ELEMENT_SPACE);

    for (size_t i = 0; i + 1 < tokens.size(); i += 2) {
        REQUIRE(tokens[i] == cw::CW_DOT);
        REQUIRE(tokens[i + 1] == cw::CW_ELEMENT_SPACE);
    }

    // The adapted dot/dash boundary is ~120 ms -> 2400/120 = 20 WPM.
    REQUIRE(tc.get_current_wpm() == Approx(20.0f).margin(3.0f));
}

TEST_CASE("time classifier: dot vs dash after adaptation") {
    cw::TimeClassifier tc;
    adapt_20_wpm(tc);
    REQUIRE(tc.get_current_wpm() == Approx(20.0f).margin(3.0f));

    REQUIRE(close_mark(tc, 6) == cw::CW_DOT);   // 60 ms
    REQUIRE(close_mark(tc, 18) == cw::CW_DASH); // 180 ms
}

TEST_CASE("time classifier: long spaces become letter and word spaces") {
    cw::TimeClassifier tc;
    adapt_20_wpm(tc);

    // Adapted off thresholds: element/letter ~120 ms, letter/word ~300 ms.
    REQUIRE(classify_space(tc, 20, 15) == cw::CW_LETTER_SPACE); // 150 ms
    REQUIRE(classify_space(tc, 20, 31) == cw::CW_WORD_SPACE);   // 310 ms
}

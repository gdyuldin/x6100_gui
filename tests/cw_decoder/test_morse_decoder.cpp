// Tests for cw::MorseDecoder: the binary-tree state machine and its text
// callback. The callback output is captured into a std::string.

#include <catch2/catch_test_macros.hpp>

#include <string>

#include "morse_decoder.h"

namespace {

struct DecoderFixture {
    std::string      out;
    cw::MorseDecoder dec{[this](const char *text) { out += text; }};

    void token(cw::Token t) { dec.handle_token(t); }
};

void put_code(const std::string &code, DecoderFixture &f) {
    for (char c : code) {
        if (c == '.') {
            f.token(cw::CW_DOT);
        } else if (c == '-') {
            f.token(cw::CW_DASH);
        } else if (c == ' ') {
            f.token(cw::CW_LETTER_SPACE);
            continue;
        } else {
            continue;
        }
    }
    f.token(cw::CW_LETTER_SPACE);
}

} // namespace

TEST_CASE("morse decoder: single letters") {
    DecoderFixture f;

    f.token(cw::CW_DOT);
    f.token(cw::CW_LETTER_SPACE);
    REQUIRE(f.out == "E");

    f.out.clear();
    f.token(cw::CW_DASH);
    f.token(cw::CW_LETTER_SPACE);
    REQUIRE(f.out == "T");

    f.out.clear();
    f.token(cw::CW_DOT);
    f.token(cw::CW_DOT);
    f.token(cw::CW_LETTER_SPACE);
    REQUIRE(f.out == "I");

    f.out.clear();
    f.token(cw::CW_DOT);
    f.token(cw::CW_DASH);
    f.token(cw::CW_LETTER_SPACE);
    REQUIRE(f.out == "A");
}

TEST_CASE("morse decoder: word space flushes the letter and a space") {
    DecoderFixture f;

    f.token(cw::CW_DOT);
    f.token(cw::CW_LETTER_SPACE);
    f.token(cw::CW_DASH);
    f.token(cw::CW_LETTER_SPACE);
    f.token(cw::CW_WORD_SPACE);

    REQUIRE(f.out == "ET ");
}

TEST_CASE("morse decoder: element spaces and CW_NONE are ignored") {
    DecoderFixture f;

    f.token(cw::CW_DOT);
    f.token(cw::CW_ELEMENT_SPACE);
    f.token(cw::CW_NONE);
    f.token(cw::CW_DOT);
    f.token(cw::CW_LETTER_SPACE);

    REQUIRE(f.out == "I");
}

TEST_CASE("morse decoder: reset discards the pending tree index") {
    DecoderFixture f;

    f.token(cw::CW_DOT);
    f.dec.reset();
    f.token(cw::CW_LETTER_SPACE);

    REQUIRE(f.out.empty());
}

TEST_CASE("morse decoder: exceptions decoding") {
    DecoderFixture f;

    put_code("-.-. --.-", f);
    REQUIRE(f.out == "CQ");

    f.out.clear();
    put_code("-.-.--.-", f);
    REQUIRE(f.out == "<CQ>");

    f.out.clear();
    put_code("...---...", f);
    REQUIRE(f.out == "<SOS>");

    f.out.clear();
    put_code("......", f);
    REQUIRE(f.out == "<ERR>");
}

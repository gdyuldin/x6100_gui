#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <vector>

#include "cw_config.h"
#include "spgram_real.h"

TEST_CASE("spgram real: exposes the newest unwindowed hop of an emitted frame") {
    cw::SpgramReal spgram;

    constexpr size_t   HOPS = 3;
    std::vector<float> samples(HOPS * cw::HOP_SIZE);
    for (size_t i = 0; i < samples.size(); ++i) {
        samples[i] = static_cast<float>(i + 1);
    }

    for (size_t i = 0; i < samples.size(); ++i) {
        bool ready = spgram.execute(samples[i]);
        if ((i + 1) % cw::HOP_SIZE == 0) {
            REQUIRE(ready);
        } else {
            REQUIRE_FALSE(ready);
        }
    }

    const cw::RawHop &hop = spgram.get_hop_raw();
    for (size_t i = 0; i < cw::HOP_SIZE; ++i) {
        REQUIRE(hop[i] == samples[(HOPS - 1) * cw::HOP_SIZE + i]);
    }
}

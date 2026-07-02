#include "../src/ft8/qso.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cstring>

using Catch::Matchers::Equals;

static ftx_qso_context_t test_ctx(ftx_qso_mode_t mode = FTX_QSO_MODE_MANUAL) {
    ftx_qso_reset();
    ftx_qso_context_t ctx = {
        .local_callsign = "R2RFE",
        .local_qth      = "LO02rq",
        .mode           = mode,
    };
    return ctx;
}

static ftx_decoded_msg_t make_msg(const char *text, int snr = 0,
                                  float freq_hz = 0.0f, bool odd = true) {
    ftx_decoded_msg_t msg = {
        .text     = text,
        .snr      = snr,
        .freq_hz  = freq_hz,
        .time_sec = 0.0f,
        .odd      = odd,
    };
    return msg;
}

/* Run one slot end with a batch of messages received in an odd slot. */
static void slot(const ftx_qso_context_t *ctx,
                 std::initializer_list<ftx_decoded_msg_t> msgs,
                 ftx_qso_response_t *response,
                 bool rx_odd = true) {
    ftx_decoded_msg_t buf[16];
    size_t n = 0;
    for (const auto &m : msgs) buf[n++] = m;
    ftx_qso_on_decoded_messages(ctx, buf, n, rx_odd, response);
}

TEST_CASE("Split text", "[ft8_qso]") {
    auto tokens = split_text(" CQ   EA0DX123 ");
    REQUIRE(tokens.size() == 2);
    REQUIRE(tokens[0] == "CQ");
    REQUIRE(tokens[1] == "EA0DX123");
}

TEST_CASE("Parse decoded message meta", "[ft8_qso]") {
    ftx_qso_context_t ctx = test_ctx();
    ftx_msg_meta_t meta;

    SECTION("CQ with grid") {
        ftx_qso_parse_rx_text(&ctx, "CQ EA0DX KO12", 12, 1010.0f, 3.5f, &meta);
        REQUIRE(meta.type == FTX_MSG_TYPE_CQ);
        REQUIRE(!meta.to_me);
        REQUIRE(meta.local_snr == 12);
        REQUIRE(meta.freq_hz == 1010.0f);
        REQUIRE(meta.time_sec == 3.5f);
        REQUIRE_THAT(meta.call_de, Equals("EA0DX"));
        REQUIRE_THAT(meta.grid, Equals("KO12"));
    }

    SECTION("Report to me") {
        ftx_qso_parse_rx_text(&ctx, "R2RFE EA0DX -08", 5, 0.0f, 0.0f, &meta);
        REQUIRE(meta.type == FTX_MSG_TYPE_REPORT);
        REQUIRE(meta.to_me);
        REQUIRE(meta.remote_snr == -8);
        REQUIRE_THAT(meta.call_de, Equals("EA0DX"));
    }

    SECTION("Callsign match is case-insensitive") {
        ftx_qso_parse_rx_text(&ctx, "r2rfe EA0DX rr73", 5, 0.0f, 0.0f, &meta);
        REQUIRE(meta.type == FTX_MSG_TYPE_RR73);
        REQUIRE(meta.to_me);
    }

    SECTION("DXpedition report") {
        ftx_qso_parse_rx_text(&ctx, "A2AA RR73; R2RFE <RP79AA> +05", 12, 0.0f, 0.0f, &meta);
        REQUIRE(meta.type == FTX_MSG_TYPE_REPORT);
        REQUIRE(meta.to_me);
        REQUIRE(meta.remote_snr == 5);
        REQUIRE_THAT(meta.call_de, Equals("RP79AA"));
    }

    SECTION("Malformed report payload does not throw") {
        ftx_qso_parse_rx_text(&ctx, "R2RFE EA0DX +", 5, 0.0f, 0.0f, &meta);
        REQUIRE(meta.type == FXT_MSG_TYPE_OTHER);
        ftx_qso_parse_rx_text(&ctx, "R2RFE EA0DX R-", 5, 0.0f, 0.0f, &meta);
        REQUIRE(meta.type == FXT_MSG_TYPE_OTHER);
    }
}

TEST_CASE("Manual mode requires an armed target", "[ft8_qso]") {
    ftx_qso_context_t ctx = test_ctx();
    ftx_qso_response_t response;

    slot(&ctx, {make_msg("R2RFE EA0DX KO12", 7)}, &response);
    REQUIRE(response.action == FTX_QSO_ACTION_RX);

    /* Empty slot is a valid input. */
    ftx_qso_on_decoded_messages(&ctx, nullptr, 0, false, &response);
    REQUIRE(response.action == FTX_QSO_ACTION_RX);
    REQUIRE(response.tx_odd);
}

TEST_CASE("Click arms the target and replies one hop", "[ft8_qso]") {
    ftx_qso_context_t ctx = test_ctx();
    ftx_qso_response_t response;

    SECTION("Click CQ") {
        ftx_qso_on_user_message(&ctx, "CQ EA0DX KO12", 9, 1200.0f, true, &response);
        REQUIRE(response.action == FTX_QSO_ACTION_TX);
        REQUIRE(!response.tx_odd);
        REQUIRE(response.freq_hz == 1200.0f);
        REQUIRE_THAT(response.tx_msg, Equals("EA0DX R2RFE LO02"));
    }

    SECTION("Click grid to me") {
        ftx_qso_on_user_message(&ctx, "R2RFE EA0DX KO12", -5, 0.0f, false, &response);
        REQUIRE(response.action == FTX_QSO_ACTION_TX);
        REQUIRE(response.tx_odd);
        REQUIRE_THAT(response.tx_msg, Equals("EA0DX R2RFE -05"));
    }

    SECTION("Click RR73 to me replies 73") {
        ftx_qso_on_user_message(&ctx, "R2RFE EA0DX RR73", 3, 0.0f, true, &response);
        REQUIRE(response.action == FTX_QSO_ACTION_TX);
        REQUIRE(!response.tx_odd);
        REQUIRE_THAT(response.tx_msg, Equals("EA0DX R2RFE 73"));
    }

    SECTION("Click final 73 to me stays RX") {
        ftx_qso_on_user_message(&ctx, "R2RFE EA0DX 73", 3, 0.0f, true, &response);
        REQUIRE(response.action == FTX_QSO_ACTION_RX);
    }

    SECTION("Click tail-ends someone else's QSO end") {
        ftx_qso_on_user_message(&ctx, "JA1XYZ EA0DX 73", 3, 0.0f, true, &response);
        REQUIRE(response.action == FTX_QSO_ACTION_TX);
        REQUIRE_THAT(response.tx_msg, Equals("EA0DX R2RFE LO02"));
    }
}

TEST_CASE("Manual QSO progression", "[ft8_qso]") {
    ftx_qso_context_t ctx = test_ctx();
    ftx_qso_response_t response;

    /* Arm on their CQ (odd slot). */
    ftx_qso_on_user_message(&ctx, "CQ EA0DX KO12", 9, 1000.0f, true, &response);
    REQUIRE(response.action == FTX_QSO_ACTION_TX);

    SECTION("Peer report advances to R-report") {
        slot(&ctx, {make_msg("R2RFE EA0DX -08", 4)}, &response);
        REQUIRE(response.action == FTX_QSO_ACTION_TX);
        REQUIRE(!response.tx_odd);
        REQUIRE_THAT(response.tx_msg, Equals("EA0DX R2RFE R+04"));
    }

    SECTION("Messages from others are ignored") {
        slot(&ctx, {make_msg("R2RFE JA1XYZ -10", 8),
                    make_msg("CQ VK5COL PF84", 20)}, &response);
        /* Not from the target: falls back to the sticky CQ reply. */
        REQUIRE(response.action == FTX_QSO_ACTION_TX);
        REQUIRE_THAT(response.tx_msg, Equals("EA0DX R2RFE LO02"));
    }

    SECTION("Wrong parity slot end stays RX") {
        slot(&ctx, {make_msg("R2RFE EA0DX -08", 4)}, &response, false);
        REQUIRE(response.action == FTX_QSO_ACTION_RX);
    }

    SECTION("Sticky retries then gives up") {
        for (int i = 0; i < 5; i++) {
            slot(&ctx, {}, &response);
            REQUIRE(response.action == FTX_QSO_ACTION_TX);
            REQUIRE_THAT(response.tx_msg, Equals("EA0DX R2RFE LO02"));
        }
        slot(&ctx, {}, &response);
        REQUIRE(response.action == FTX_QSO_ACTION_RX);
    }

    SECTION("Real message resets the sticky counter") {
        for (int i = 0; i < 4; i++) {
            slot(&ctx, {}, &response);
            REQUIRE(response.action == FTX_QSO_ACTION_TX);
        }
        slot(&ctx, {make_msg("R2RFE EA0DX -08", 4)}, &response);
        REQUIRE(response.action == FTX_QSO_ACTION_TX);
        for (int i = 0; i < 5; i++) {
            slot(&ctx, {}, &response);
            REQUIRE(response.action == FTX_QSO_ACTION_TX);
            REQUIRE_THAT(response.tx_msg, Equals("EA0DX R2RFE R+04"));
        }
        slot(&ctx, {}, &response);
        REQUIRE(response.action == FTX_QSO_ACTION_RX);
    }

    SECTION("RR73 is answered but never sticky") {
        slot(&ctx, {make_msg("R2RFE EA0DX RR73", 2)}, &response);
        REQUIRE(response.action == FTX_QSO_ACTION_TX);
        REQUIRE_THAT(response.tx_msg, Equals("EA0DX R2RFE 73"));

        /* Peer got our 73 and went silent: no sticky retransmit. */
        slot(&ctx, {}, &response);
        REQUIRE(response.action == FTX_QSO_ACTION_RX);

        /* Peer missed it and repeats RR73: reply again, every time. */
        for (int i = 0; i < 8; i++) {
            slot(&ctx, {make_msg("R2RFE EA0DX RR73", 2)}, &response);
            REQUIRE(response.action == FTX_QSO_ACTION_TX);
            REQUIRE_THAT(response.tx_msg, Equals("EA0DX R2RFE 73"));
        }
    }

    SECTION("Peer 73 closes the QSO") {
        slot(&ctx, {make_msg("R2RFE EA0DX -08", 4)}, &response);
        REQUIRE(response.action == FTX_QSO_ACTION_TX);
        slot(&ctx, {make_msg("R2RFE EA0DX 73", 4)}, &response);
        REQUIRE(response.action == FTX_QSO_ACTION_RX);
        /* Sticky cleared: silence stays silent. */
        slot(&ctx, {}, &response);
        REQUIRE(response.action == FTX_QSO_ACTION_RX);
    }
}

TEST_CASE("Auto mode SNR", "[ft8_qso]") {
    ftx_qso_context_t ctx = test_ctx(FTX_QSO_MODE_SNR);
    ftx_qso_response_t response;

    SECTION("Louder CQ wins") {
        slot(&ctx, {make_msg("CQ EA0DX KO12", -12, 800.0f),
                    make_msg("CQ VK5COL PF84", 3, 1500.0f)}, &response);
        REQUIRE(response.action == FTX_QSO_ACTION_TX);
        REQUIRE_THAT(response.tx_msg, Equals("VK5COL R2RFE LO02"));
        REQUIRE(response.freq_hz == 1500.0f);
    }

    SECTION("Deeper QSO progress beats a louder CQ") {
        slot(&ctx, {make_msg("CQ VK5COL PF84", 20),
                    make_msg("R2RFE EA0DX R-05", -15)}, &response);
        REQUIRE(response.action == FTX_QSO_ACTION_TX);
        REQUIRE_THAT(response.tx_msg, Equals("EA0DX R2RFE RR73"));
    }

    SECTION("last_call keeps the QSO from wandering") {
        slot(&ctx, {make_msg("CQ EA0DX KO12", 5)}, &response);
        REQUIRE_THAT(response.tx_msg, Equals("EA0DX R2RFE LO02"));
        /* Next slot: EA0DX repeats CQ; a louder station also calls CQ. */
        slot(&ctx, {make_msg("CQ EA0DX KO12", 5),
                    make_msg("CQ VK5COL PF84", 20)}, &response);
        REQUIRE_THAT(response.tx_msg, Equals("EA0DX R2RFE LO02"));
    }

    SECTION("Same TX text stops after three sends") {
        for (int i = 0; i < 3; i++) {
            slot(&ctx, {make_msg("CQ EA0DX KO12", 5)}, &response);
            REQUIRE(response.action == FTX_QSO_ACTION_TX);
        }
        slot(&ctx, {make_msg("CQ EA0DX KO12", 5)}, &response);
        REQUIRE(response.action == FTX_QSO_ACTION_RX);
    }

    SECTION("Final 73 to me is not answered") {
        slot(&ctx, {make_msg("R2RFE EA0DX 73", 5)}, &response);
        REQUIRE(response.action == FTX_QSO_ACTION_RX);
    }

    SECTION("Tail-end when nothing better is around") {
        slot(&ctx, {make_msg("JA1XYZ EA0DX RR73", 5)}, &response);
        REQUIRE(response.action == FTX_QSO_ACTION_TX);
        REQUIRE_THAT(response.tx_msg, Equals("EA0DX R2RFE LO02"));
    }
}

TEST_CASE("Auto mode distance", "[ft8_qso]") {
    ftx_qso_context_t ctx = test_ctx(FTX_QSO_MODE_DISTANCE);
    ftx_qso_response_t response;

    SECTION("Farther grid wins over louder signal") {
        /* KO85 (Moscow area) is near LO02; PF84 (Australia) is far. */
        slot(&ctx, {make_msg("CQ UA3XYZ KO85", 20),
                    make_msg("CQ VK5COL PF84", -20)}, &response);
        REQUIRE(response.action == FTX_QSO_ACTION_TX);
        REQUIRE_THAT(response.tx_msg, Equals("VK5COL R2RFE LO02"));
    }

    SECTION("No grid data falls back to SNR") {
        slot(&ctx, {make_msg("CQ UA3XYZ", -3),
                    make_msg("CQ VK5COL", 10)}, &response);
        REQUIRE(response.action == FTX_QSO_ACTION_TX);
        REQUIRE_THAT(response.tx_msg, Equals("VK5COL R2RFE LO02"));
    }
}

TEST_CASE("Auto mode random picks a valid candidate", "[ft8_qso]") {
    ftx_qso_context_t ctx = test_ctx(FTX_QSO_MODE_RANDOM);
    ftx_qso_response_t response;

    slot(&ctx, {make_msg("CQ EA0DX KO12", 5),
                make_msg("CQ VK5COL PF84", 5)}, &response);
    REQUIRE(response.action == FTX_QSO_ACTION_TX);
    bool ok = (strcmp(response.tx_msg, "EA0DX R2RFE LO02") == 0) ||
              (strcmp(response.tx_msg, "VK5COL R2RFE LO02") == 0);
    REQUIRE(ok);
}

TEST_CASE("Reset clears the armed target", "[ft8_qso]") {
    ftx_qso_context_t ctx = test_ctx();
    ftx_qso_response_t response;

    ftx_qso_on_user_message(&ctx, "CQ EA0DX KO12", 9, 0.0f, true, &response);
    REQUIRE(response.action == FTX_QSO_ACTION_TX);

    ftx_qso_reset();
    slot(&ctx, {make_msg("R2RFE EA0DX -08", 4)}, &response);
    REQUIRE(response.action == FTX_QSO_ACTION_RX);
}

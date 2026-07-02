#include "../src/ft8/qso.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

using Catch::Matchers::Equals;

static ftx_qso_context_t test_ctx(void) {
    return {
        .local_callsign = "R2RFE",
        .local_qth      = "LO02rq",
    };
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

TEST_CASE("Decoded batch hook is an insertion point", "[ft8_qso]") {
    ftx_qso_context_t ctx = test_ctx();
    ftx_decoded_msg_t msgs[] = {
        {
            .text     = "R2RFE EA0DX KO12",
            .snr      = 7,
            .freq_hz  = 1000.0f,
            .time_sec = 2.0f,
            .odd      = true,
        },
    };
    ftx_qso_response_t response;

    ftx_qso_on_decoded_messages(&ctx, msgs, 1, true, &response);
    REQUIRE(response.action == FTX_QSO_ACTION_RX);
    REQUIRE(!response.tx_odd);
    REQUIRE_THAT(response.tx_msg, Equals(""));

    /* An empty slot is a valid input: the engine must handle zero decodes. */
    ftx_qso_on_decoded_messages(&ctx, nullptr, 0, false, &response);
    REQUIRE(response.action == FTX_QSO_ACTION_RX);
    REQUIRE(response.tx_odd);
}

TEST_CASE("User message hook returns the opposite TX slot", "[ft8_qso]") {
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

    SECTION("Click RR73 to me") {
        ftx_qso_on_user_message(&ctx, "R2RFE EA0DX RR73", 3, 0.0f, true, &response);
        REQUIRE(response.action == FTX_QSO_ACTION_TX);
        REQUIRE(!response.tx_odd);
        REQUIRE_THAT(response.tx_msg, Equals("EA0DX R2RFE 73"));
    }

    SECTION("Click final 73 to me") {
        ftx_qso_on_user_message(&ctx, "R2RFE EA0DX 73", 3, 0.0f, true, &response);
        REQUIRE(response.action == FTX_QSO_ACTION_RX);
    }
}

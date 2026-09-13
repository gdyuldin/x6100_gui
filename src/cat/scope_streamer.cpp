#include "scope_streamer.h"
#include "civ_protocol.h"
#include "civ_internal.h"
#include "../cfg/settings_manager.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>

extern SettingsManager cfg_sm;

namespace {

// ============================================================================
// Scope static state
// ============================================================================

bool                    scope_on             = false;
bool                    scope_data_enabled   = false;
uint8_t                 scope_mode           = 0;      // 0=Center,1=Fixed,2=SCROLL-C,3=SCROLL-F
int32_t                 scope_span_hz        = 100000;
int32_t                 scope_edge_start_hz  = 0;
int32_t                 scope_edge_end_hz    = 0;
float                   scope_ref_level_dB   = 0.0f;
int32_t                 scope_center_freq_hz = 0;
uint16_t                scope_edge_num       = 1;      // 1-4

std::atomic<scope_notify_cb_t> notify_cb{nullptr};

constexpr uint16_t SCOPE_NFFT = 1024;
constexpr uint16_t SCOPE_NBINS = 475;

// ============================================================================
// Frequency range table for 0x27 0x1E
// ============================================================================

struct FreqRange {
    uint8_t code;
    float   low_mhz;
    float   high_mhz;
};

constexpr FreqRange FREQ_RANGES[] = {
    {1,   0.03f,   1.60f},
    {2,   1.60f,   2.00f},
    {3,   2.00f,   6.00f},
    {4,   6.00f,   8.00f},
    {5,   8.00f,  11.00f},
    {6,  11.00f,  15.00f},
    {7,  15.00f,  20.00f},
    {8,  20.00f,  22.00f},
    {9,  22.00f,  26.00f},
    {10, 26.00f,  30.00f},
    {11, 30.00f,  45.00f},
    {12, 45.00f,  60.00f},
    {13, 60.00f,  74.80f},
    {14, 74.80f, 108.00f},
    {15, 108.00f, 137.00f},
    {16, 137.00f, 200.00f},
    {17, 400.00f, 470.00f},
};

constexpr size_t NUM_RANGES = sizeof(FREQ_RANGES) / sizeof(FREQ_RANGES[0]);

uint8_t freq_range_code(int32_t freq_hz) {
    float freq_mhz = static_cast<float>(freq_hz) / 1.0e6f;
    for (size_t i = 0; i < NUM_RANGES; i++) {
        if (freq_mhz >= FREQ_RANGES[i].low_mhz && freq_mhz < FREQ_RANGES[i].high_mhz)
            return FREQ_RANGES[i].code;
    }
    return 1;
}

// BCD helpers — provided by civ::detail (civ_processor.cpp)
using namespace civ::detail;

// ============================================================================
// Reference level encode/decode (7-byte format)
// ============================================================================

void ref_level_encode(uint8_t buf[7], float dB) {
    bool neg = dB < 0.0f;
    float abs_val = neg ? -dB : dB;
    int tens   = static_cast<int>(abs_val / 10.0f);
    int ones   = static_cast<int>(std::fmod(abs_val, 10.0f));
    int tenths = static_cast<int>(std::round(std::fmod(abs_val, 1.0f) * 10.0f));
    tens   = std::clamp(tens, 0, 2);
    ones   = std::clamp(ones, 0, 9);
    tenths = (tenths >= 5) ? 5 : 0;
    buf[0] = 0x00;
    buf[1] = 0x00;
    buf[2] = static_cast<uint8_t>(tens);
    buf[3] = static_cast<uint8_t>(ones);
    buf[4] = static_cast<uint8_t>(tenths);
    buf[5] = 0x00;
    buf[6] = neg ? 1 : 0;
}

float ref_level_decode(std::string_view data) {
    if (data.size() < 7) return 0.0f;
    int tens   = static_cast<uint8_t>(data[2]);
    int ones   = static_cast<uint8_t>(data[3]);
    int tenths = static_cast<uint8_t>(data[4]);
    bool neg   = data[6] != 0;
    float val = static_cast<float>(tens * 10 + ones + tenths * 0.1f);
    return neg ? -val : val;
}

} // anonymous namespace

// ============================================================================
// Public API
// ============================================================================

void scope_streamer_set_notify(scope_notify_cb_t cb) {
    notify_cb.store(cb, std::memory_order_release);
}

void scope_streamer_set_center_freq(int32_t freq_hz) {
    scope_center_freq_hz = freq_hz;
}

void scope_streamer_push_data(const float *psd_db, size_t len,
                               uint32_t center_freq, uint32_t width_hz) {

    if (!scope_data_enabled) return;
    scope_notify_cb_t cb = notify_cb.load(std::memory_order_acquire);
    if (!cb) return;
    // Scale 1024 float dB values to uint8_t 0-200
    int32_t grid_min = cfg_sm.p_band_grid_min.get();
    int32_t grid_max = cfg_sm.p_band_grid_max.get();
    float min_db = static_cast<float>(grid_min);
    float max_db = static_cast<float>(grid_max);
    float range_db = max_db - min_db;
    if (range_db < 1.0f) range_db = 48.0f;

    uint8_t scaled[SCOPE_NFFT];
    for (size_t i = 0; i < len && i < SCOPE_NFFT; i++) {
        float v = (psd_db[i] - min_db) * 200.0f / range_db;
        if (v < 0.0f) v = 0.0f;
        if (v > 200.0f) v = 200.0f;
        scaled[i] = static_cast<uint8_t>(v + 0.5f);
    }

    // Downsample 1024 -> 475
    uint8_t binned[SCOPE_NBINS];
    uint32_t step = (SCOPE_NFFT << 12) / SCOPE_NBINS;
    uint32_t acc = step >> 1;
    for (uint16_t i = 0; i < SCOPE_NBINS; i++) {
        uint32_t src_idx = acc >> 12;
        uint32_t frac = (acc >> 3) & 0x1F;
        if (src_idx + 1 >= SCOPE_NFFT) {
            binned[i] = scaled[SCOPE_NFFT - 1];
        } else {
            int v0 = scaled[src_idx];
            int v1 = scaled[src_idx + 1];
            binned[i] = static_cast<uint8_t>(v0 + ((v1 - v0) * static_cast<int>(frac) >> 5));
        }
        acc += step;
    }

    // Build CI-V packet
    uint8_t tx_buf[512];
    CivTxPacker packer{tx_buf, 0, LOCAL_ADDRESS};

    auto after_cmd = packer.set_command(0x27)
        .append_byte(0x00) // Subcommand
        .append_byte(0x00) // 00 (Fixed)
        .append_byte(0x01) // Order of division data (Current): 01~11
        .append_byte(0x01) // Division number (Maximum): 01(WLAN), 11(USB)
        .append_byte(static_cast<uint8_t>(scope_mode)); // Spectrum scope mode data:

    /* Add waveform information */
    uint8_t bcd[5];
    if (scope_mode == 0) { // Center mode
        to_bcd(bcd, center_freq, 10);
        after_cmd.append_data(bcd, 5);
        to_bcd(bcd, width_hz, 10);
        after_cmd.append_data(bcd, 5);
    } else {
        int32_t half = static_cast<int32_t>(width_hz) / 2;
        int32_t lower = static_cast<int32_t>(center_freq) - half;
        int32_t upper = static_cast<int32_t>(center_freq) + half;
        to_bcd(bcd, lower, 10);
        after_cmd.append_data(bcd, 5);
        to_bcd(bcd, upper, 10);
        after_cmd.append_data(bcd, 5);
    }

    after_cmd.append_byte(0x00); // In range
    after_cmd.append_data(binned, SCOPE_NBINS); // Data

    std::string_view packet = after_cmd.get_packet();
    if (!packet.empty()) {
        cb(packet);
    }
}

std::string_view scope_streamer_handle_27(const CivPacketView &req, CivTxPacker &resp) {
    uint8_t subcmd    = req.get_subcommand();
    size_t  data_size = req.get_command_data().size();

    switch (subcmd) {
        case 0x10: // Send/read the Scope ON/OFF status
            if (data_size == 1) { // Read
                return resp.set_command(req.get_command())
                    .set_subcommand(subcmd)
                    .append_byte(scope_on ? 0x01 : 0x00)
                    .get_packet();
            } // Write
            scope_on = static_cast<uint8_t>(req.get_subcommand_data()[0]) != 0;
            return resp.set_ok().get_packet();

        case 0x11: // Send/read the Scope wave data output
            if (data_size == 1) // Read
                return resp.set_command(req.get_command())
                    .set_subcommand(subcmd)
                    .append_byte(scope_data_enabled ? 0x01 : 0x00)
                    .get_packet();
            // write
            scope_data_enabled = static_cast<uint8_t>(req.get_subcommand_data()[0]) != 0;
            return resp.set_ok().get_packet();

        case 0x13: // Send/read the Single/Dual scope setting
            if (data_size == 1)
                return resp.set_command(req.get_command())
                    .set_subcommand(subcmd)
                    .append_byte(0x00)
                    .get_packet();
            return resp.set_ok().get_packet();

        case 0x14: // Send/read the Scope Center mode
            /* ( 0000=CENTER mode,
            0001=FIX mode,
            0002=SCROLL-C mode,
            0003=SCROLL-F mode) */
            if (data_size == 1) {
                return resp.set_command(req.get_command())
                    .set_subcommand(subcmd)
                    .append_byte(0x00)
                    .append_byte(0x00)
                    .get_packet();
            }
            // Ignore set Scope Center mode
            return resp.set_ok().get_packet();

        case 0x15:
            {
                if (data_size == 2) {
                    uint8_t bcd[5];
                    std::memset(bcd, 0, sizeof(bcd));
                    to_bcd_be(bcd, scope_span_hz, 10);
                    return resp.set_command(req.get_command())
                        .set_subcommand(subcmd)
                        .append_byte(static_cast<uint8_t>(scope_mode))
                        .append_data(bcd, 5)
                        .get_packet();
                }
                auto data  = req.get_subcommand_data();
                scope_span_hz = static_cast<int32_t>(from_bcd_be(data, 10));
                // TODO: control zoom here
                scope_span_hz = scope_span_hz < 25'000 ? 25'000 : scope_span_hz;
                scope_span_hz = scope_span_hz > 100'000 ? 100'000 : scope_span_hz;
                return resp.set_ok().get_packet();
            }

        case 0x16:
            {
                if (data_size == 1) {
                    uint8_t edge_bytes[2] = {static_cast<uint8_t>(scope_edge_num >> 8),
                                             static_cast<uint8_t>(scope_edge_num & 0xFF)};
                    return resp.set_command(req.get_command())
                        .set_subcommand(subcmd)
                        .append_data(edge_bytes, 2)
                        .get_packet();
                }
                if (data_size >= 3) {
                    uint16_t val = static_cast<uint8_t>(req.get_subcommand_data()[0]) << 8 |
                                   static_cast<uint8_t>(req.get_subcommand_data()[1]);
                    if (val >= 1 && val <= 4)
                        scope_edge_num = val;
                }
                return resp.set_ok().get_packet();
            }

        case 0x19: // Send/read the Scope Reference level setting
            {
                if (data_size == 1) {
                    uint8_t enc[7];
                    ref_level_encode(enc, scope_ref_level_dB);
                    return resp.set_command(req.get_command()).set_subcommand(subcmd).append_data(enc, 7).get_packet();
                }
                if (data_size >= 8) {
                    scope_ref_level_dB = ref_level_decode(req.get_subcommand_data());
                }
                return resp.set_ok().get_packet();
            }

        case 0x1A: // Sweep speed setting (0000=FAST, 0001=MID, 0002=SLOW)
            return resp.set_ng().get_packet();

        case 0x1E:
            {
                if (data_size == 1) {
                    // GET: return freq_range + edge_num + lower_edge + higher_edge
                    uint8_t range_byte = freq_range_code(scope_edge_start_hz);
                    uint8_t edge[5], lower[5], higher[5];
                    to_bcd_be(edge, 0, 10);
                    to_bcd_be(lower, scope_edge_start_hz, 10);
                    to_bcd_be(higher, scope_edge_end_hz, 10);
                    return resp.set_command(req.get_command())
                        .set_subcommand(subcmd)
                        .append_byte(range_byte)
                        .append_byte(static_cast<uint8_t>(scope_edge_num))
                        .append_data(lower, 5)
                        .append_data(higher, 5)
                        .get_packet();
                }
                auto data = req.get_command_data();
                if (data_size >= 4) {
                    scope_edge_start_hz = static_cast<int32_t>(from_bcd_be(data.substr(2), 10));
                }
                if (data_size >= 9) {
                    scope_edge_end_hz = static_cast<int32_t>(from_bcd_be(data.substr(7), 10));
                }
                return resp.set_ok().get_packet();
            }

        default:
            // return resp.set_ng().get_packet();
            return civ::detail::set_unsupported(req, resp);
    }
}

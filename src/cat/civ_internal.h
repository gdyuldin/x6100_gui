#pragma once

#include "civ_protocol.h"
#include <string_view>

namespace civ::detail {

// Log raw CIV frame bytes for debugging (internal to cat/ module)
void log_frame_raw(std::string_view raw, const char *prefix);

// Return CODE_NG (0xFA) response for unsupported command (internal to cat/ module)
std::string_view set_unsupported(const CivPacketView &req, CivTxPacker &resp);

// BCD conversion helpers (internal to cat/ module)
void to_bcd(uint8_t bcd_data[], uint64_t data, uint8_t len);
void to_bcd_be(uint8_t bcd_data[], uint64_t data, uint8_t len);
uint64_t from_bcd(std::string_view bcd_data, uint8_t len);
uint64_t from_bcd_be(std::string_view bcd_data, uint8_t len);

} // namespace civ::detail

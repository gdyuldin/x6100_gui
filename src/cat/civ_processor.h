#pragma once

#include "civ_protocol.h"
#include <cstdint>

#include "../cfg/settings_manager.h"


// Redirect the SettingsManager used by CI-V command handlers.
void civ_set_sm(SettingsManager *sm);

// Process one CI-V request packet. Dispatches to the registered handler
// based on request.get_command(). Sets code 0xFA (CODE_NG) for unknown cmd.
std::string_view process_civ_message(const CivPacketView &request, CivTxPacker &response_packer);


std::string_view make_freq_response_00(int32_t freq, CivTxPacker &response_packer);

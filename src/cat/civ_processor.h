#pragma once

#include "civ_protocol.h"
#include <cstdint>

#include <aether_radio/x6100_control/control.h>

#include "../ports/app_ports.h"

// Inject the application ports used by the CI-V handlers (radio control and
// telemetry). Called by cat_init(); tests pass their own fakes.
void civ_set_ports(const app_ports_t *ports);

// Process one CI-V request packet. Dispatches to the registered handler
// based on request.get_command(). Sets code 0xFA (CODE_NG) for unknown cmd.
std::string_view process_civ_message(const CivPacketView &request, CivTxPacker &response_packer);


// Live data section (broadcast notifications about changes)
std::string_view pack_fg_freq_notify_00(int32_t freq, CivTxPacker &response_packer);
std::string_view pack_mode_notify_01(x6100_mode_t mode, CivTxPacker &response_packer);
std::string_view pack_vfo_notify_07(x6100_vfo_t vfo, CivTxPacker &response_packer);

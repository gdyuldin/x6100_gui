#pragma once


#include <cstdint>
#include <cstddef>
#include <cstring>
#include <string_view>

#define FRAME_PRE 0xFE
#define FRAME_END 0xFD
#define LOCAL_ADDRESS 0xA4

#define FRAME_ADD_LEN 5 /* Header and end len */

// Forward declarations
class CivTxPackerAfterCommand;
class CivTxPackerAfterData;
class CivTxPackerAfterCode;

// ============================================================================
// Ingress packet (Read-Only View)
// ============================================================================
class CivPacketView {
public:
    CivPacketView(const uint8_t* start, size_t length)
        : m_view(reinterpret_cast<const char*>(start), length) {}

    uint8_t get_radio_address() const;
    uint8_t get_controller_address() const;

    uint8_t get_command() const;
    std::string_view get_command_data() const;

    uint8_t get_subcommand() const;
    std::string_view get_subcommand_data() const;

    uint8_t get_vfo() const { return get_subcommand(); }
    std::string_view get_vfo_data() const { return get_subcommand_data(); }

    // Full packet bytes including frame markers (for logging, bypasses parsers)
    std::string_view raw() const { return m_view; }

private:
    std::string_view m_view;
};


// ============================================================================
// 2. Egress packer (Writable Builder)
// ============================================================================

// --- Stage 1: Pure packer ---
class CivTxPacker {
public:
    CivTxPacker(uint8_t* txBuffer, uint8_t radioAddr, uint8_t controllerAddr)
        : m_buf(txBuffer)
    {
        m_buf[0] = FRAME_PRE; m_buf[1] = FRAME_PRE;
        m_buf[2] = radioAddr; m_buf[3] = controllerAddr;
        m_currentSize = 4;
    }

    void set_controller_address(uint8_t controllerAddr);
    CivTxPackerAfterCommand set_command(uint8_t cmd);
    CivTxPackerAfterCode set_code(uint8_t code);

private:
    uint8_t* m_buf;
    size_t   m_currentSize;

    std::string_view finalizeAndReset();


    friend class CivTxPackerAfterCommand;
    friend class CivTxPackerAfterData;
    friend class CivTxPackerAfterCode;
};

// --- Stage 3: After command, subcommand, vfo or raw data ---
class CivTxPackerAfterData {
public:
    CivTxPackerAfterData(CivTxPacker& packer) : m_p(packer) {}

    CivTxPackerAfterData& append_data(const uint8_t* data, size_t length);

    std::string_view get_packet();

private:
    CivTxPacker& m_p;
};

// --- Stage 2: After command write ---
class CivTxPackerAfterCommand {
public:
    CivTxPackerAfterCommand(CivTxPacker& packer) : m_p(packer) {}

    CivTxPackerAfterData set_subcommand(uint8_t subcmd);
    CivTxPackerAfterData set_vfo(uint8_t vfoId);
    CivTxPackerAfterData append_data(const uint8_t* data, size_t length);
    std::string_view get_packet();

private:
    CivTxPacker& m_p;

    CivTxPackerAfterData write_next_byte(uint8_t byte);
};

// --- Stage: After set_code (terminal — ack/ng only, no data) ---
class CivTxPackerAfterCode {
public:
    CivTxPackerAfterCode(CivTxPacker& packer) : m_p(packer) {}
    std::string_view get_packet();
private:
    CivTxPacker& m_p;
};

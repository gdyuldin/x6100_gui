#include "civ_protocol.h"

// ============================================================================
// CivPacketView
// ============================================================================

uint8_t CivPacketView::get_dst_address() const {
    return static_cast<uint8_t>(m_view[2]);
}

uint8_t CivPacketView::get_src_address() const {
    return static_cast<uint8_t>(m_view[3]);
}

uint8_t CivPacketView::get_command() const {
    return static_cast<uint8_t>(m_view[4]);
}

std::string_view CivPacketView::get_command_data() const {
    size_t startIdx = 5;
    size_t endIdx = m_view.size() - 1;
    if (startIdx >= endIdx) return {};
    return m_view.substr(startIdx, endIdx - startIdx);
}

uint8_t CivPacketView::get_subcommand() const {
    return static_cast<uint8_t>(m_view[5]);
}

std::string_view CivPacketView::get_subcommand_data() const {
    size_t startIdx = 6;
    size_t endIdx = m_view.size() - 1;
    if (startIdx >= endIdx) return {};
    return m_view.substr(startIdx, endIdx - startIdx);
}

// ============================================================================
// CivTxPacker
// ============================================================================

void CivTxPacker::set_dst_addr(uint8_t dstAddr) {
    m_buf[2] = dstAddr;
}

CivTxPackerAfterCommand CivTxPacker::set_command(uint8_t cmd) {
    m_buf[4] = cmd;
    m_currentSize = 5;
    return CivTxPackerAfterCommand(*this);
}

std::string_view CivTxPacker::finalizeAndReset() {
    m_buf[m_currentSize] = FRAME_END;
    size_t totalLength   = m_currentSize + 1;

    std::string_view res(reinterpret_cast<const char *>(m_buf), totalLength);
    m_currentSize = 4;
    return res;
}

void CivTxPacker::append_data_imp(const uint8_t *data, size_t length) {
    if (data && length > 0) {
        std::memcpy(&m_buf[m_currentSize], data, length);
        m_currentSize += length;
    }
}

void CivTxPacker::append_byte_imp(const uint8_t data) {
    m_buf[m_currentSize++] = data;
}

// ============================================================================
// CivTxPackerAfterData
// ============================================================================

CivTxPackerAfterData& CivTxPackerAfterData::append_data(const uint8_t* data, size_t length) {
    m_p.append_data_imp(data, length);
    return *this;
}

CivTxPackerAfterData &CivTxPackerAfterData::append_byte(const uint8_t data) {
    m_p.append_byte_imp(data);
    return *this;
}

std::string_view CivTxPackerAfterData::get_packet() {
    return m_p.finalizeAndReset();
}

// ============================================================================
// CivTxPackerAfterCommand
// ============================================================================

CivTxPackerAfterData CivTxPackerAfterCommand::write_next_byte(uint8_t byte) {
    m_p.m_buf[m_p.m_currentSize++] = byte;
    return CivTxPackerAfterData(m_p);
}

CivTxPackerAfterData CivTxPackerAfterCommand::set_subcommand(uint8_t subcmd) {
    return write_next_byte(subcmd);
}

CivTxPackerAfterData CivTxPackerAfterCommand::set_vfo(uint8_t vfoId) {
    return write_next_byte(vfoId);
}

CivTxPackerAfterData CivTxPackerAfterCommand::append_data(const uint8_t* data, size_t length) {
    m_p.append_data_imp(data, length);
    return CivTxPackerAfterData(m_p);
}

CivTxPackerAfterData CivTxPackerAfterCommand::append_byte(const uint8_t data) {
    m_p.append_byte_imp(data);
    return CivTxPackerAfterData(m_p);
}

std::string_view CivTxPackerAfterCommand::get_packet() {
    return m_p.finalizeAndReset();
}

// ============================================================================
// CivTxPackerAfterCode
// ============================================================================

CivTxPackerAfterCode CivTxPacker::set_ok() {
    set_command(0xFB);
    return CivTxPackerAfterCode(*this);
}

CivTxPackerAfterCode CivTxPacker::set_ng() {
    set_command(0xFA);
    return CivTxPackerAfterCode(*this);
}

std::string_view CivTxPackerAfterCode::get_packet() {
    return m_p.finalizeAndReset();
}

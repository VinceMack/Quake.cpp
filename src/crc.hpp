// crc.hpp -- CRC-16 checksum function declarations
#pragma once
#include <cstdint>
#include "common.hpp"

namespace Common {

// Reference-based functions for modern C++ code
void CRC_Init(std::uint16_t& crcvalue) noexcept;
void CRC_ProcessByte(std::uint16_t& crcvalue, byte data) noexcept;

} // namespace Common

// crc.h -- CRC-16 checksum function declarations
#pragma once

namespace Common {

void CRC_Init(unsigned short* crcvalue);
void CRC_ProcessByte(unsigned short* crcvalue, byte data);

} // namespace Common

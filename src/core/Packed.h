// DSI "packed MS bit" format: every 7 data bytes go out as 8 MIDI bytes. The first byte of each packet holds
// the top bits (bit 0 = top bit of data byte 1, bit 1 = of byte 2, ...), then the 7 bytes with their top bit cleared.
// A last, shorter packet holds the remainder: 1024 data bytes -> 146 full packets + 1 packet of 2 -> 1171 bytes.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace t6
{

std::vector<uint8_t> packMsBit (const uint8_t* data, size_t size);
std::vector<uint8_t> unpackMsBit (const uint8_t* packed, size_t size);

constexpr size_t packedSize (size_t dataSize) { return dataSize + (dataSize + 6) / 7; }

} // namespace t6

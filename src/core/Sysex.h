// Trigon-6 SysEx: requests and the program dump parser. Messages include F0 ... F7.
#pragma once

#include <cstdint>
#include <optional>
#include <vector>

namespace t6
{

constexpr uint8_t kDsiId = 0x01;
constexpr uint8_t kTrigon6Id = 0x2D;
constexpr size_t kProgramBytes = 1024;
constexpr size_t kProgramPackedBytes = 1171;

std::vector<uint8_t> requestEditBuffer();                     // F0 01 2D 06 F7
std::vector<uint8_t> requestProgram (int bank, int program);  // F0 01 2D 05 bb pp F7
std::vector<uint8_t> requestDeviceInquiry();                  // F0 7E 7F 06 01 F7

// The 1024 unpacked program bytes from an edit buffer dump (F0 01 2D 03 <1171> F7)
// or a program dump (F0 01 2D 02 bb pp <1171> F7). Nothing for any other message.
std::optional<std::vector<uint8_t>> parseProgramDump (const uint8_t* msg, size_t size);

} // namespace t6

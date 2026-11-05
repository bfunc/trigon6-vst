// Trigon-6 SysEx: requests and the program dump parser. Messages include F0 ... F7.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace t6
{

constexpr uint8_t kDsiId = 0x01;
constexpr uint8_t kTrigon6Id = 0x2D;
constexpr size_t kProgramBytes = 1024;
constexpr size_t kProgramPackedBytes = 1171;

// id: the model byte after 01. 2D is the documented guess; SynthLink uses the family the synth reports
// in its device inquiry reply when that differs.
std::vector<uint8_t> requestEditBuffer (uint8_t id = kTrigon6Id);                     // F0 01 2D 06 F7
std::vector<uint8_t> requestProgram (int bank, int program, uint8_t id = kTrigon6Id);  // F0 01 2D 05 bb pp F7
std::vector<uint8_t> requestDeviceInquiry();                  // F0 7E 7F 06 01 F7
std::vector<uint8_t> requestGlobals (uint8_t id = kTrigon6Id);                        // F0 01 2D 0E F7

// Reply to the device inquiry: F0 7E cc 06 02 01 2D 01 00 00 <version> ... F7.
struct DeviceInfo
{
    bool isTrigon6 = false;  // DSI ID 01, family 2D
    int manufacturer = -1, family = -1;  // bytes 5 and 6 of the reply
    int versionMajor = -1, versionMinor = -1;  // version byte 0jjj nnnn: jjj minor, nnnn major
    std::string raw;  // the reply as hex, without F0/F7, for the Connection page
};
std::optional<DeviceInfo> parseDeviceInquiryReply (const uint8_t* msg, size_t size);

// Global parameters dump: F0 01 2D 0F <50 nibbles, LS then MS> F7 -> 25 values.
constexpr size_t kGlobalCount = 25;
std::optional<std::vector<int>> parseGlobalDump (const uint8_t* msg, size_t size, uint8_t id = kTrigon6Id);

enum class SysexKind { Other, DeviceInquiryReply, GlobalDump, EditBufferDump, ProgramDump };
SysexKind classifySysex (const uint8_t* msg, size_t size, uint8_t id = kTrigon6Id);

// The 1024 unpacked program bytes from an edit buffer dump (F0 01 2D 03 <1171> F7)
// or a program dump (F0 01 2D 02 bb pp <1171> F7). Nothing for any other message.
std::optional<std::vector<uint8_t>> parseProgramDump (const uint8_t* msg, size_t size, uint8_t id = kTrigon6Id);

} // namespace t6

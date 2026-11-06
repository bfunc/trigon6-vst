// Trigon-6 SysEx: requests and the program dump parser. Messages include F0 ... F7.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace t6
{

constexpr uint8_t kDsiId = 0x01;
constexpr uint8_t kTrigon6Id = 0x39;
constexpr size_t kProgramBytes = 1024;
constexpr size_t kProgramPackedBytes = 1171;

// id: the model byte after 01. 39 is what a Trigon-6 (firmware 1.0) reports in its device inquiry reply
// (7E 7F 06 02 01 39 01 00 00 01 01 01); SynthLink uses the family the synth reports when that differs.
std::vector<uint8_t> requestEditBuffer (uint8_t id = kTrigon6Id);                     // F0 01 39 06 F7
std::vector<uint8_t> requestProgram (int bank, int program, uint8_t id = kTrigon6Id);  // F0 01 39 05 bb pp F7
std::vector<uint8_t> requestDeviceInquiry();                  // F0 7E 7F 06 01 F7
std::vector<uint8_t> requestGlobals (uint8_t id = kTrigon6Id);                        // F0 01 39 0E F7

// Dumps that go to the synth, from the 1024 unpacked program bytes.
// A program dump (02) writes into that program's slot in the synth's memory; an edit buffer dump (03) is the
// same sound without a slot (how a sound saved on the PC is stored).
std::vector<uint8_t> programDump (int bank, int program, const std::vector<uint8_t>& data, uint8_t id = kTrigon6Id);
std::vector<uint8_t> editBufferDump (const std::vector<uint8_t>& data, uint8_t id = kTrigon6Id);

// Reply to the device inquiry: F0 7E cc 06 02 01 39 01 00 00 <version> ... F7.
struct DeviceInfo
{
    bool isTrigon6 = false;  // DSI ID 01, family 39
    int manufacturer = -1, family = -1;  // bytes 5 and 6 of the reply
    int versionMajor = -1, versionMinor = -1;  // version byte 0jjj nnnn: jjj minor, nnnn major
    std::string raw;  // the reply as hex, without F0/F7, for the Connection page
};
std::optional<DeviceInfo> parseDeviceInquiryReply (const uint8_t* msg, size_t size);

// Bytes as "7E 7F 06", at most maxBytes of them, then " ..." when there are more.
std::string toHex (const uint8_t* msg, size_t size, size_t maxBytes = 32);

// Global parameters dump: F0 01 39 0F <24 bytes, one per value> F7 (29 bytes, as the synth sends it).
constexpr size_t kGlobalCount = 24;
std::optional<std::vector<int>> parseGlobalDump (const uint8_t* msg, size_t size, uint8_t id = kTrigon6Id);

enum class SysexKind { Other, DeviceInquiryReply, GlobalDump, EditBufferDump, ProgramDump };
SysexKind classifySysex (const uint8_t* msg, size_t size, uint8_t id = kTrigon6Id);

// The 1024 unpacked program bytes from an edit buffer dump (F0 01 39 03 <1171> F7)
// or a program dump (F0 01 39 02 bb pp <1171> F7). Nothing for any other message.
std::optional<std::vector<uint8_t>> parseProgramDump (const uint8_t* msg, size_t size, uint8_t id = kTrigon6Id);

} // namespace t6

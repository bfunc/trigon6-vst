#include "Sysex.h"

#include "Packed.h"

namespace t6
{

std::vector<uint8_t> requestEditBuffer() { return { 0xF0, kDsiId, kTrigon6Id, 0x06, 0xF7 }; }

std::vector<uint8_t> requestProgram (int bank, int program)
{
    return { 0xF0, kDsiId, kTrigon6Id, 0x05, (uint8_t) (bank & 0x7F), (uint8_t) (program & 0x7F), 0xF7 };
}

std::vector<uint8_t> requestDeviceInquiry() { return { 0xF0, 0x7E, 0x7F, 0x06, 0x01, 0xF7 }; }

std::optional<std::vector<uint8_t>> parseProgramDump (const uint8_t* msg, size_t size)
{
    // Accept the message with or without F0/F7 (JUCE's getSysExData() strips them).
    if (size > 0 && msg[0] == 0xF0) { ++msg; --size; }
    if (size > 0 && msg[size - 1] == 0xF7) --size;

    if (size < 3 || msg[0] != kDsiId || msg[1] != kTrigon6Id)
        return std::nullopt;

    const size_t header = msg[2] == 0x03 ? 3 : msg[2] == 0x02 ? 5 : 0;
    if (header == 0 || size != header + kProgramPackedBytes)
        return std::nullopt;

    auto data = unpackMsBit (msg + header, kProgramPackedBytes);
    if (data.size() != kProgramBytes)
        return std::nullopt;
    return data;
}

} // namespace t6

#include "Sysex.h"

#include "Packed.h"

namespace t6
{

std::vector<uint8_t> requestEditBuffer (uint8_t id) { return { 0xF0, kDsiId, id, 0x06, 0xF7 }; }

std::vector<uint8_t> requestProgram (int bank, int program, uint8_t id)
{
    return { 0xF0, kDsiId, id, 0x05, (uint8_t) (bank & 0x7F), (uint8_t) (program & 0x7F), 0xF7 };
}

std::vector<uint8_t> requestDeviceInquiry() { return { 0xF0, 0x7E, 0x7F, 0x06, 0x01, 0xF7 }; }

std::vector<uint8_t> requestGlobals (uint8_t id) { return { 0xF0, kDsiId, id, 0x0E, 0xF7 }; }

namespace
{
// Accept a message with or without F0/F7 (JUCE's getSysExData() strips them).
void strip (const uint8_t*& msg, size_t& size)
{
    if (size > 0 && msg[0] == 0xF0) { ++msg; --size; }
    if (size > 0 && msg[size - 1] == 0xF7) --size;
}
} // namespace

std::optional<DeviceInfo> parseDeviceInquiryReply (const uint8_t* msg, size_t size)
{
    strip (msg, size);
    // 7E cc 06 02 <manufacturer> <family LS> <family MS> <member LS> <member MS> <version>
    if (size < 5 || msg[0] != 0x7E || msg[2] != 0x06 || msg[3] != 0x02)
        return std::nullopt;
    DeviceInfo info;
    info.isTrigon6 = size >= 6 && msg[4] == kDsiId && msg[5] == kTrigon6Id;
    if (size >= 6)
    {
        info.manufacturer = msg[4];
        info.family = msg[5];
    }
    info.raw = toHex (msg, size);
    if (size >= 10)
    {
        info.versionMajor = msg[9] & 0x0F;
        info.versionMinor = (msg[9] >> 4) & 0x07;
    }
    return info;
}

std::string toHex (const uint8_t* msg, size_t size, size_t maxBytes)
{
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (size_t i = 0; i < size && i < maxBytes; ++i)
    {
        if (i > 0)
            out += ' ';
        out += hex[msg[i] >> 4];
        out += hex[msg[i] & 0x0F];
    }
    if (size > maxBytes)
        out += " ...";
    return out;
}

std::optional<std::vector<int>> parseGlobalDump (const uint8_t* msg, size_t size, uint8_t id)
{
    strip (msg, size);
    if (size != 3 + 2 * kGlobalCount || msg[0] != kDsiId || msg[1] != id || msg[2] != 0x0F)
        return std::nullopt;
    std::vector<int> values (kGlobalCount);
    for (size_t i = 0; i < kGlobalCount; ++i)
        values[i] = (msg[3 + 2 * i] & 0x0F) | ((msg[4 + 2 * i] & 0x0F) << 4);
    return values;
}

SysexKind classifySysex (const uint8_t* msg, size_t size, uint8_t id)
{
    if (parseDeviceInquiryReply (msg, size))
        return SysexKind::DeviceInquiryReply;
    strip (msg, size);
    if (size < 3 || msg[0] != kDsiId || msg[1] != id)
        return SysexKind::Other;
    switch (msg[2])
    {
        case 0x0F: return SysexKind::GlobalDump;
        case 0x03: return SysexKind::EditBufferDump;
        case 0x02: return SysexKind::ProgramDump;
        default:   return SysexKind::Other;
    }
}

std::optional<std::vector<uint8_t>> parseProgramDump (const uint8_t* msg, size_t size, uint8_t id)
{
    strip (msg, size);

    if (size < 3 || msg[0] != kDsiId || msg[1] != id)
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

#include "Nrpn.h"

namespace t6
{

std::array<Cc, 4> encodeNrpn (int param, int value)
{
    return { { { 99, (uint8_t) ((param >> 7) & 0x7F) },
               { 98, (uint8_t) (param & 0x7F) },
               { 6, (uint8_t) ((value >> 7) & 0x7F) },
               { 38, (uint8_t) (value & 0x7F) } } };
}

std::array<uint8_t, 12> encodeNrpnBytes (int channel, int param, int value)
{
    const auto status = (uint8_t) (0xB0 | ((channel - 1) & 0x0F));
    std::array<uint8_t, 12> out {};
    size_t i = 0;
    for (auto cc : encodeNrpn (param, value))
    {
        out[i++] = status;
        out[i++] = cc.controller;
        out[i++] = cc.value;
    }
    return out;
}

std::optional<NrpnEvent> NrpnDecoder::feed (uint8_t controller, uint8_t value)
{
    switch (controller)
    {
        case 99: paramMsb = value; return std::nullopt;
        case 98: paramLsb = value; return std::nullopt;
        case 6:  valueMsb = value; return std::nullopt;
        case 38:
            if (paramMsb < 0 || paramLsb < 0)
                return std::nullopt;
            return NrpnEvent { (paramMsb << 7) | paramLsb, (valueMsb << 7) | value };
        default:
            return std::nullopt;
    }
}

void NrpnDecoder::reset()
{
    paramMsb = paramLsb = -1;
    valueMsb = 0;
}

} // namespace t6

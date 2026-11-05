#include "Packed.h"

namespace t6
{

std::vector<uint8_t> packMsBit (const uint8_t* data, size_t size)
{
    std::vector<uint8_t> out;
    out.reserve (packedSize (size));
    for (size_t start = 0; start < size; start += 7)
    {
        const size_t n = size - start < 7 ? size - start : 7;
        uint8_t high = 0;
        for (size_t i = 0; i < n; ++i)
            high |= (uint8_t) (((data[start + i] >> 7) & 1) << i);
        out.push_back (high);
        for (size_t i = 0; i < n; ++i)
            out.push_back ((uint8_t) (data[start + i] & 0x7F));
    }
    return out;
}

std::vector<uint8_t> unpackMsBit (const uint8_t* packed, size_t size)
{
    std::vector<uint8_t> out;
    out.reserve (size);
    for (size_t start = 0; start < size; start += 8)
    {
        const uint8_t high = packed[start];
        const size_t n = size - start - 1 < 7 ? size - start - 1 : 7;
        for (size_t i = 0; i < n; ++i)
            out.push_back ((uint8_t) ((packed[start + 1 + i] & 0x7F) | (((high >> i) & 1) << 7)));
    }
    return out;
}

} // namespace t6

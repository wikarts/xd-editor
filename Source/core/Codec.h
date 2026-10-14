#pragma once
#include <cstdint>
#include <vector>

namespace xd
{
/** Korg 7-bit SysEx packing: every 7 data bytes are preceded by one byte
    holding their MSBs (bit i = MSB of byte i). */
inline std::vector<uint8_t> pack7 (const uint8_t* data, size_t n)
{
    std::vector<uint8_t> out;
    out.reserve (n + n / 7 + 1);
    for (size_t i = 0; i < n; i += 7)
    {
        const size_t chunk = (n - i) < 7 ? (n - i) : 7;
        uint8_t msbs = 0;
        for (size_t j = 0; j < chunk; ++j)
            if (data[i + j] & 0x80) msbs |= (uint8_t) (1u << j);
        out.push_back (msbs);
        for (size_t j = 0; j < chunk; ++j)
            out.push_back ((uint8_t) (data[i + j] & 0x7F));
    }
    return out;
}

inline std::vector<uint8_t> unpack7 (const uint8_t* data, size_t n)
{
    std::vector<uint8_t> out;
    out.reserve (n);
    for (size_t i = 0; i < n; i += 8)
    {
        const uint8_t msbs = data[i];
        for (size_t j = 1; j < 8 && i + j < n; ++j)
            out.push_back ((uint8_t) ((data[i + j] & 0x7F) | (((msbs >> (j - 1)) & 1) << 7)));
    }
    return out;
}

constexpr size_t packedSize (size_t n) { return (n / 7) * 8 + ((n % 7) ? (n % 7) + 1 : 0); }

} // namespace xd

// Crc32.h — minimal, dependency-free CRC32 (polynomial 0xEDB88320).
//
// Used to fingerprint a texture's pixel data so the mod can recognize the
// Xbox button atlas no matter where in memory it happens to land each run.
//
// Portable across MSVC and MinGW-w64 / GCC.
#pragma once
#include <cstdint>
#include <cstddef>
#include <array>

// The lookup table is built once, lazily. A function-local static initialized
// from a lambda is guaranteed thread-safe by the C++11 "magic statics" rule,
// so concurrent first-callers can't race on the table contents.
inline const std::array<uint32_t, 256>& Crc32Table()
{
    static const std::array<uint32_t, 256> table = [] {
        std::array<uint32_t, 256> t{};
        for (uint32_t i = 0; i < 256; ++i)
        {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k)
                c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            t[i] = c;
        }
        return t;
    }();
    return table;
}

// Standard CRC32 over a byte range.
inline uint32_t Crc32(const void* data, size_t length)
{
    const auto& table = Crc32Table();
    const uint8_t* p = static_cast<const uint8_t*>(data);
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < length; ++i)
        crc = table[(crc ^ p[i]) & 0xFFu] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

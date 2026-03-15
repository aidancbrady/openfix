#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#define OPENFIX_HAS_SSE2 1
#else
#define OPENFIX_HAS_SSE2 0
#endif

inline uint8_t computeChecksum(const char* data, size_t len)
{
    uint32_t sum = 0;

#if OPENFIX_HAS_SSE2
    // process 16 bytes at a time using SSE2 SAD (sum-of-absolute-differences).
    const char* end = data + len;
    __m128i vsum = _mm_setzero_si128();
    const __m128i vzero = _mm_setzero_si128();

    while (data + 16 <= end) {
        __m128i v = _mm_loadu_si128(reinterpret_cast<const __m128i*>(data));
        vsum = _mm_add_epi64(vsum, _mm_sad_epu8(v, vzero));
        data += 16;
    }

    // collapse two 64-bit sums into one 32-bit sum
    sum = static_cast<uint32_t>(_mm_extract_epi16(vsum, 0))
        + static_cast<uint32_t>(_mm_extract_epi16(vsum, 4));

    // handle remaining bytes
    while (data < end)
        sum += static_cast<uint8_t>(*data++);
#else
    for (size_t i = 0; i < len; ++i)
        sum += static_cast<uint8_t>(data[i]);
#endif

    return static_cast<uint8_t>(sum & 0xFF);
}

inline uint8_t computeChecksum(std::string_view sv)
{
    return computeChecksum(sv.data(), sv.size());
}

struct ChecksumStr
{
    char buf[3];

    std::string_view view() const { return std::string_view(buf, 3); }
    const char* data() const { return buf; }
    size_t size() const { return 3; }
};

inline ChecksumStr formatChecksum(uint8_t checksum)
{
    ChecksumStr result;
    result.buf[0] = '0' + (checksum / 100);
    result.buf[1] = '0' + (checksum / 10 % 10);
    result.buf[2] = '0' + (checksum % 10);
    return result;
}

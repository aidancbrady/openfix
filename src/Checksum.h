#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#define OPENFIX_HAS_SSE2 1
#if defined(__AVX2__)
#define OPENFIX_HAS_AVX2 1
#else
#define OPENFIX_HAS_AVX2 0
#endif
#else
#define OPENFIX_HAS_SSE2 0
#define OPENFIX_HAS_AVX2 0
#endif

inline uint8_t computeChecksum(const char* data, size_t len)
{
    uint32_t sum = 0;
    const char* end = data + len;

#if OPENFIX_HAS_AVX2
    // process 32 bytes at a time using AVX2 SAD (sum-of-absolute-differences).
    __m256i vsum = _mm256_setzero_si256();
    const __m256i vzero = _mm256_setzero_si256();

    while (data + 32 <= end) {
        __m256i v = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(data));
        vsum = _mm256_add_epi64(vsum, _mm256_sad_epu8(v, vzero));
        data += 32;
    }

    // collapse four 64-bit sums into one 32-bit sum
    __m128i lo = _mm256_castsi256_si128(vsum);
    __m128i hi = _mm256_extracti128_si256(vsum, 1);
    __m128i combined = _mm_add_epi64(lo, hi);
    sum = static_cast<uint32_t>(_mm_extract_epi16(combined, 0))
        + static_cast<uint32_t>(_mm_extract_epi16(combined, 4));

    // handle remaining bytes (may still use SSE2 for 16-byte chunks)
    if (data + 16 <= end) {
        __m128i v = _mm_loadu_si128(reinterpret_cast<const __m128i*>(data));
        __m128i s = _mm_sad_epu8(v, _mm_setzero_si128());
        sum += static_cast<uint32_t>(_mm_extract_epi16(s, 0))
             + static_cast<uint32_t>(_mm_extract_epi16(s, 4));
        data += 16;
    }

    while (data < end)
        sum += static_cast<uint8_t>(*data++);

#elif OPENFIX_HAS_SSE2
    // process 16 bytes at a time using SSE2 SAD (sum-of-absolute-differences).
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

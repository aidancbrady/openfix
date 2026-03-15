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

/// Compute the FIX checksum (sum of all bytes mod 256) over raw data.
/// Uses SIMD on x86-64 for large buffers, scalar fallback otherwise.
inline uint8_t computeChecksum(const char* data, size_t len)
{
    uint32_t sum = 0;

#if OPENFIX_HAS_SSE2
    // Process 16 bytes at a time using SSE2 SAD (sum-of-absolute-differences).
    // _mm_sad_epu8(v, zero) computes the horizontal sum of unsigned bytes in v,
    // producing two 64-bit partial sums (lanes 0 and 4 of the 16-bit result).
    // We accumulate into a 32-bit sum which is safe for up to ~16 MB of input
    // (255 * 65536 = 16,711,680) before overflow — far beyond any FIX message.
    const char* end = data + len;
    __m128i vsum = _mm_setzero_si128();
    const __m128i vzero = _mm_setzero_si128();

    while (data + 16 <= end) {
        __m128i v = _mm_loadu_si128(reinterpret_cast<const __m128i*>(data));
        vsum = _mm_add_epi64(vsum, _mm_sad_epu8(v, vzero));
        data += 16;
    }

    // Extract the two 64-bit lane sums and collapse into scalar.
    sum = static_cast<uint32_t>(_mm_extract_epi16(vsum, 0))
        + static_cast<uint32_t>(_mm_extract_epi16(vsum, 4));

    // Handle remaining bytes.
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

/// Format checksum as a zero-padded 3-digit string (e.g. "007", "128").
inline std::string formatChecksum(uint8_t checksum)
{
    char buf[4];
    buf[0] = '0' + (checksum / 100);
    buf[1] = '0' + (checksum / 10 % 10);
    buf[2] = '0' + (checksum % 10);
    buf[3] = '\0';
    return std::string(buf, 3);
}

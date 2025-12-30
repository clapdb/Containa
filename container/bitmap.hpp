/*
 * Copyright 2025 ClapDB, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <span>
#include <stdexcept>
#include <type_traits>

// SIMD detection
#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#define BITMAP_HAS_SSE2 1
#if defined(__SSE4_2__) || defined(__POPCNT__)
#define BITMAP_HAS_POPCNT 1
#endif
#if defined(__AVX2__)
#define BITMAP_HAS_AVX2 1
#endif
#if defined(__AVX512F__) && defined(__AVX512BW__)
#define BITMAP_HAS_AVX512 1
#endif
#if defined(__AVX512VPOPCNTDQ__)
#define BITMAP_HAS_AVX512_VPOPCNT 1
#endif
#elif defined(__aarch64__) || defined(_M_ARM64)
#include <arm_neon.h>
#define BITMAP_HAS_NEON 1
#if defined(__ARM_FEATURE_SVE)
#include <arm_sve.h>
#define BITMAP_HAS_SVE 1
#endif
#endif

#ifndef BITMAP_ASSERT
#include <cassert>
#define BITMAP_ASSERT(expr, msg) assert((expr) && (msg))
#endif

namespace stdb::container {

namespace bitmap_detail {

// Cache line size for alignment
constexpr size_t kCacheLineSize = 64;

// Number of bits per word
constexpr size_t kBitsPerWord = 64;

// Calculate number of words needed for n bits
[[nodiscard]] constexpr size_t words_for_bits(size_t n) noexcept {
    return (n + kBitsPerWord - 1) / kBitsPerWord;
}

// Aligned memory allocation
[[nodiscard]] inline uint64_t* allocate_aligned(size_t num_words) {
    if (num_words == 0) return nullptr;
    size_t bytes = num_words * sizeof(uint64_t);
    // Round up to cache line boundary
    bytes = (bytes + kCacheLineSize - 1) & ~(kCacheLineSize - 1);
    void* ptr = std::aligned_alloc(kCacheLineSize, bytes);
    if (!ptr) throw std::bad_alloc();
    return static_cast<uint64_t*>(ptr);
}

inline void deallocate_aligned(uint64_t* ptr) noexcept {
    std::free(ptr);
}

// =============================================================================
// Scalar implementations
// =============================================================================

[[gnu::always_inline]] inline size_t popcount_scalar(const uint64_t* data, size_t word_count) noexcept {
    size_t count = 0;
    for (size_t i = 0; i < word_count; ++i) {
        count += __builtin_popcountll(data[i]);
    }
    return count;
}

[[gnu::always_inline]] inline void and_scalar(uint64_t* __restrict__ dst, const uint64_t* __restrict__ src,
                                               size_t word_count) noexcept {
    for (size_t i = 0; i < word_count; ++i) {
        dst[i] &= src[i];
    }
}

[[gnu::always_inline]] inline void or_scalar(uint64_t* __restrict__ dst, const uint64_t* __restrict__ src,
                                              size_t word_count) noexcept {
    for (size_t i = 0; i < word_count; ++i) {
        dst[i] |= src[i];
    }
}

[[gnu::always_inline]] inline void xor_scalar(uint64_t* __restrict__ dst, const uint64_t* __restrict__ src,
                                               size_t word_count) noexcept {
    for (size_t i = 0; i < word_count; ++i) {
        dst[i] ^= src[i];
    }
}

[[gnu::always_inline]] inline void andnot_scalar(uint64_t* __restrict__ dst, const uint64_t* __restrict__ src,
                                                  size_t word_count) noexcept {
    for (size_t i = 0; i < word_count; ++i) {
        dst[i] &= ~src[i];
    }
}

// =============================================================================
// x86 SIMD implementations
// =============================================================================

#if defined(BITMAP_HAS_SSE2)

[[gnu::always_inline]] inline void and_sse2(uint64_t* __restrict__ dst, const uint64_t* __restrict__ src,
                                             size_t word_count) noexcept {
    size_t i = 0;
    // Process 2 words (128 bits) at a time
    for (; i + 2 <= word_count; i += 2) {
        __m128i a = _mm_load_si128(reinterpret_cast<const __m128i*>(dst + i));
        __m128i b = _mm_load_si128(reinterpret_cast<const __m128i*>(src + i));
        _mm_store_si128(reinterpret_cast<__m128i*>(dst + i), _mm_and_si128(a, b));
    }
    // Scalar tail
    for (; i < word_count; ++i) {
        dst[i] &= src[i];
    }
}

[[gnu::always_inline]] inline void or_sse2(uint64_t* __restrict__ dst, const uint64_t* __restrict__ src,
                                            size_t word_count) noexcept {
    size_t i = 0;
    for (; i + 2 <= word_count; i += 2) {
        __m128i a = _mm_load_si128(reinterpret_cast<const __m128i*>(dst + i));
        __m128i b = _mm_load_si128(reinterpret_cast<const __m128i*>(src + i));
        _mm_store_si128(reinterpret_cast<__m128i*>(dst + i), _mm_or_si128(a, b));
    }
    for (; i < word_count; ++i) {
        dst[i] |= src[i];
    }
}

[[gnu::always_inline]] inline void xor_sse2(uint64_t* __restrict__ dst, const uint64_t* __restrict__ src,
                                             size_t word_count) noexcept {
    size_t i = 0;
    for (; i + 2 <= word_count; i += 2) {
        __m128i a = _mm_load_si128(reinterpret_cast<const __m128i*>(dst + i));
        __m128i b = _mm_load_si128(reinterpret_cast<const __m128i*>(src + i));
        _mm_store_si128(reinterpret_cast<__m128i*>(dst + i), _mm_xor_si128(a, b));
    }
    for (; i < word_count; ++i) {
        dst[i] ^= src[i];
    }
}

[[gnu::always_inline]] inline void andnot_sse2(uint64_t* __restrict__ dst, const uint64_t* __restrict__ src,
                                                size_t word_count) noexcept {
    size_t i = 0;
    for (; i + 2 <= word_count; i += 2) {
        __m128i a = _mm_load_si128(reinterpret_cast<const __m128i*>(dst + i));
        __m128i b = _mm_load_si128(reinterpret_cast<const __m128i*>(src + i));
        // andnot: ~b & a, but we want a & ~b, so swap operands
        _mm_store_si128(reinterpret_cast<__m128i*>(dst + i), _mm_andnot_si128(b, a));
    }
    for (; i < word_count; ++i) {
        dst[i] &= ~src[i];
    }
}

#endif  // BITMAP_HAS_SSE2

#if defined(BITMAP_HAS_AVX2)

[[gnu::always_inline]] inline void and_avx2(uint64_t* __restrict__ dst, const uint64_t* __restrict__ src,
                                             size_t word_count) noexcept {
    size_t i = 0;
    // Process 4 words (256 bits) at a time
    for (; i + 4 <= word_count; i += 4) {
        __m256i a = _mm256_load_si256(reinterpret_cast<const __m256i*>(dst + i));
        __m256i b = _mm256_load_si256(reinterpret_cast<const __m256i*>(src + i));
        _mm256_store_si256(reinterpret_cast<__m256i*>(dst + i), _mm256_and_si256(a, b));
    }
    // SSE2 for 2-word chunks
    for (; i + 2 <= word_count; i += 2) {
        __m128i a = _mm_load_si128(reinterpret_cast<const __m128i*>(dst + i));
        __m128i b = _mm_load_si128(reinterpret_cast<const __m128i*>(src + i));
        _mm_store_si128(reinterpret_cast<__m128i*>(dst + i), _mm_and_si128(a, b));
    }
    // Scalar tail
    for (; i < word_count; ++i) {
        dst[i] &= src[i];
    }
}

[[gnu::always_inline]] inline void or_avx2(uint64_t* __restrict__ dst, const uint64_t* __restrict__ src,
                                            size_t word_count) noexcept {
    size_t i = 0;
    for (; i + 4 <= word_count; i += 4) {
        __m256i a = _mm256_load_si256(reinterpret_cast<const __m256i*>(dst + i));
        __m256i b = _mm256_load_si256(reinterpret_cast<const __m256i*>(src + i));
        _mm256_store_si256(reinterpret_cast<__m256i*>(dst + i), _mm256_or_si256(a, b));
    }
    for (; i + 2 <= word_count; i += 2) {
        __m128i a = _mm_load_si128(reinterpret_cast<const __m128i*>(dst + i));
        __m128i b = _mm_load_si128(reinterpret_cast<const __m128i*>(src + i));
        _mm_store_si128(reinterpret_cast<__m128i*>(dst + i), _mm_or_si128(a, b));
    }
    for (; i < word_count; ++i) {
        dst[i] |= src[i];
    }
}

[[gnu::always_inline]] inline void xor_avx2(uint64_t* __restrict__ dst, const uint64_t* __restrict__ src,
                                             size_t word_count) noexcept {
    size_t i = 0;
    for (; i + 4 <= word_count; i += 4) {
        __m256i a = _mm256_load_si256(reinterpret_cast<const __m256i*>(dst + i));
        __m256i b = _mm256_load_si256(reinterpret_cast<const __m256i*>(src + i));
        _mm256_store_si256(reinterpret_cast<__m256i*>(dst + i), _mm256_xor_si256(a, b));
    }
    for (; i + 2 <= word_count; i += 2) {
        __m128i a = _mm_load_si128(reinterpret_cast<const __m128i*>(dst + i));
        __m128i b = _mm_load_si128(reinterpret_cast<const __m128i*>(src + i));
        _mm_store_si128(reinterpret_cast<__m128i*>(dst + i), _mm_xor_si128(a, b));
    }
    for (; i < word_count; ++i) {
        dst[i] ^= src[i];
    }
}

[[gnu::always_inline]] inline void andnot_avx2(uint64_t* __restrict__ dst, const uint64_t* __restrict__ src,
                                                size_t word_count) noexcept {
    size_t i = 0;
    for (; i + 4 <= word_count; i += 4) {
        __m256i a = _mm256_load_si256(reinterpret_cast<const __m256i*>(dst + i));
        __m256i b = _mm256_load_si256(reinterpret_cast<const __m256i*>(src + i));
        _mm256_store_si256(reinterpret_cast<__m256i*>(dst + i), _mm256_andnot_si256(b, a));
    }
    for (; i + 2 <= word_count; i += 2) {
        __m128i a = _mm_load_si128(reinterpret_cast<const __m128i*>(dst + i));
        __m128i b = _mm_load_si128(reinterpret_cast<const __m128i*>(src + i));
        _mm_store_si128(reinterpret_cast<__m128i*>(dst + i), _mm_andnot_si128(b, a));
    }
    for (; i < word_count; ++i) {
        dst[i] &= ~src[i];
    }
}

// AVX2 popcount using lookup table (Harley-Seal style)
[[gnu::always_inline]] inline size_t popcount_avx2(const uint64_t* data, size_t word_count) noexcept {
    // Lookup table for 4-bit popcount
    const __m256i lookup = _mm256_setr_epi8(
        0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4,
        0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4);
    const __m256i low_mask = _mm256_set1_epi8(0x0f);

    size_t i = 0;
    __m256i acc = _mm256_setzero_si256();
    size_t total = 0;

    // Process 32 bytes (4 words) at a time
    for (; i + 4 <= word_count; i += 4) {
        __m256i v = _mm256_load_si256(reinterpret_cast<const __m256i*>(data + i));
        __m256i lo = _mm256_and_si256(v, low_mask);
        __m256i hi = _mm256_and_si256(_mm256_srli_epi16(v, 4), low_mask);
        __m256i popcnt_lo = _mm256_shuffle_epi8(lookup, lo);
        __m256i popcnt_hi = _mm256_shuffle_epi8(lookup, hi);
        acc = _mm256_add_epi8(acc, popcnt_lo);
        acc = _mm256_add_epi8(acc, popcnt_hi);

        // Prevent overflow: accumulate every 31 iterations (255/8 = 31)
        if ((i & 0x7C) == 0x7C) {
            // Sum bytes horizontally
            acc = _mm256_sad_epu8(acc, _mm256_setzero_si256());
            total += _mm256_extract_epi64(acc, 0) + _mm256_extract_epi64(acc, 1) +
                     _mm256_extract_epi64(acc, 2) + _mm256_extract_epi64(acc, 3);
            acc = _mm256_setzero_si256();
        }
    }

    // Final horizontal sum
    acc = _mm256_sad_epu8(acc, _mm256_setzero_si256());
    total += _mm256_extract_epi64(acc, 0) + _mm256_extract_epi64(acc, 1) +
             _mm256_extract_epi64(acc, 2) + _mm256_extract_epi64(acc, 3);

    // Scalar tail
    for (; i < word_count; ++i) {
        total += __builtin_popcountll(data[i]);
    }

    return total;
}

#endif  // BITMAP_HAS_AVX2

#if defined(BITMAP_HAS_AVX512)

[[gnu::always_inline]] inline void and_avx512(uint64_t* __restrict__ dst, const uint64_t* __restrict__ src,
                                               size_t word_count) noexcept {
    size_t i = 0;
    // Process 8 words (512 bits) at a time
    for (; i + 8 <= word_count; i += 8) {
        __m512i a = _mm512_load_si512(dst + i);
        __m512i b = _mm512_load_si512(src + i);
        _mm512_store_si512(dst + i, _mm512_and_si512(a, b));
    }
    // AVX2 for 4-word chunks
    for (; i + 4 <= word_count; i += 4) {
        __m256i a = _mm256_load_si256(reinterpret_cast<const __m256i*>(dst + i));
        __m256i b = _mm256_load_si256(reinterpret_cast<const __m256i*>(src + i));
        _mm256_store_si256(reinterpret_cast<__m256i*>(dst + i), _mm256_and_si256(a, b));
    }
    // Scalar tail
    for (; i < word_count; ++i) {
        dst[i] &= src[i];
    }
}

[[gnu::always_inline]] inline void or_avx512(uint64_t* __restrict__ dst, const uint64_t* __restrict__ src,
                                              size_t word_count) noexcept {
    size_t i = 0;
    for (; i + 8 <= word_count; i += 8) {
        __m512i a = _mm512_load_si512(dst + i);
        __m512i b = _mm512_load_si512(src + i);
        _mm512_store_si512(dst + i, _mm512_or_si512(a, b));
    }
    for (; i + 4 <= word_count; i += 4) {
        __m256i a = _mm256_load_si256(reinterpret_cast<const __m256i*>(dst + i));
        __m256i b = _mm256_load_si256(reinterpret_cast<const __m256i*>(src + i));
        _mm256_store_si256(reinterpret_cast<__m256i*>(dst + i), _mm256_or_si256(a, b));
    }
    for (; i < word_count; ++i) {
        dst[i] |= src[i];
    }
}

[[gnu::always_inline]] inline void xor_avx512(uint64_t* __restrict__ dst, const uint64_t* __restrict__ src,
                                               size_t word_count) noexcept {
    size_t i = 0;
    for (; i + 8 <= word_count; i += 8) {
        __m512i a = _mm512_load_si512(dst + i);
        __m512i b = _mm512_load_si512(src + i);
        _mm512_store_si512(dst + i, _mm512_xor_si512(a, b));
    }
    for (; i + 4 <= word_count; i += 4) {
        __m256i a = _mm256_load_si256(reinterpret_cast<const __m256i*>(dst + i));
        __m256i b = _mm256_load_si256(reinterpret_cast<const __m256i*>(src + i));
        _mm256_store_si256(reinterpret_cast<__m256i*>(dst + i), _mm256_xor_si256(a, b));
    }
    for (; i < word_count; ++i) {
        dst[i] ^= src[i];
    }
}

[[gnu::always_inline]] inline void andnot_avx512(uint64_t* __restrict__ dst, const uint64_t* __restrict__ src,
                                                  size_t word_count) noexcept {
    size_t i = 0;
    for (; i + 8 <= word_count; i += 8) {
        __m512i a = _mm512_load_si512(dst + i);
        __m512i b = _mm512_load_si512(src + i);
        _mm512_store_si512(dst + i, _mm512_andnot_si512(b, a));
    }
    for (; i + 4 <= word_count; i += 4) {
        __m256i a = _mm256_load_si256(reinterpret_cast<const __m256i*>(dst + i));
        __m256i b = _mm256_load_si256(reinterpret_cast<const __m256i*>(src + i));
        _mm256_store_si256(reinterpret_cast<__m256i*>(dst + i), _mm256_andnot_si256(b, a));
    }
    for (; i < word_count; ++i) {
        dst[i] &= ~src[i];
    }
}

#if defined(BITMAP_HAS_AVX512_VPOPCNT)
[[gnu::always_inline]] inline size_t popcount_avx512(const uint64_t* data, size_t word_count) noexcept {
    size_t i = 0;
    __m512i acc = _mm512_setzero_si512();

    // Process 8 words at a time using VPOPCNT
    for (; i + 8 <= word_count; i += 8) {
        __m512i v = _mm512_load_si512(data + i);
        acc = _mm512_add_epi64(acc, _mm512_popcnt_epi64(v));
    }

    // Horizontal sum of 8 x 64-bit values
    size_t total = _mm512_reduce_add_epi64(acc);

    // Scalar tail
    for (; i < word_count; ++i) {
        total += __builtin_popcountll(data[i]);
    }

    return total;
}
#endif  // BITMAP_HAS_AVX512_VPOPCNT

#endif  // BITMAP_HAS_AVX512

// =============================================================================
// ARM NEON implementations
// =============================================================================

#if defined(BITMAP_HAS_NEON)

[[gnu::always_inline]] inline void and_neon(uint64_t* __restrict__ dst, const uint64_t* __restrict__ src,
                                             size_t word_count) noexcept {
    size_t i = 0;
    // Process 2 words (128 bits) at a time
    for (; i + 2 <= word_count; i += 2) {
        uint64x2_t a = vld1q_u64(dst + i);
        uint64x2_t b = vld1q_u64(src + i);
        vst1q_u64(dst + i, vandq_u64(a, b));
    }
    // Scalar tail
    for (; i < word_count; ++i) {
        dst[i] &= src[i];
    }
}

[[gnu::always_inline]] inline void or_neon(uint64_t* __restrict__ dst, const uint64_t* __restrict__ src,
                                            size_t word_count) noexcept {
    size_t i = 0;
    for (; i + 2 <= word_count; i += 2) {
        uint64x2_t a = vld1q_u64(dst + i);
        uint64x2_t b = vld1q_u64(src + i);
        vst1q_u64(dst + i, vorrq_u64(a, b));
    }
    for (; i < word_count; ++i) {
        dst[i] |= src[i];
    }
}

[[gnu::always_inline]] inline void xor_neon(uint64_t* __restrict__ dst, const uint64_t* __restrict__ src,
                                             size_t word_count) noexcept {
    size_t i = 0;
    for (; i + 2 <= word_count; i += 2) {
        uint64x2_t a = vld1q_u64(dst + i);
        uint64x2_t b = vld1q_u64(src + i);
        vst1q_u64(dst + i, veorq_u64(a, b));
    }
    for (; i < word_count; ++i) {
        dst[i] ^= src[i];
    }
}

[[gnu::always_inline]] inline void andnot_neon(uint64_t* __restrict__ dst, const uint64_t* __restrict__ src,
                                                size_t word_count) noexcept {
    size_t i = 0;
    for (; i + 2 <= word_count; i += 2) {
        uint64x2_t a = vld1q_u64(dst + i);
        uint64x2_t b = vld1q_u64(src + i);
        // a & ~b = vbicq(a, b)
        vst1q_u64(dst + i, vbicq_u64(a, b));
    }
    for (; i < word_count; ++i) {
        dst[i] &= ~src[i];
    }
}

[[gnu::always_inline]] inline size_t popcount_neon(const uint64_t* data, size_t word_count) noexcept {
    size_t i = 0;
    uint64x2_t acc = vdupq_n_u64(0);

    // Process 2 words at a time
    for (; i + 2 <= word_count; i += 2) {
        uint8x16_t v = vld1q_u8(reinterpret_cast<const uint8_t*>(data + i));
        // Count bits in each byte
        uint8x16_t cnt = vcntq_u8(v);
        // Sum pairs of bytes -> 8 x 16-bit
        uint16x8_t sum16 = vpaddlq_u8(cnt);
        // Sum pairs of 16-bit -> 4 x 32-bit
        uint32x4_t sum32 = vpaddlq_u16(sum16);
        // Sum pairs of 32-bit -> 2 x 64-bit
        uint64x2_t sum64 = vpaddlq_u32(sum32);
        acc = vaddq_u64(acc, sum64);
    }

    // Horizontal sum
    size_t total = vgetq_lane_u64(acc, 0) + vgetq_lane_u64(acc, 1);

    // Scalar tail
    for (; i < word_count; ++i) {
        total += __builtin_popcountll(data[i]);
    }

    return total;
}

#endif  // BITMAP_HAS_NEON

// =============================================================================
// ARM SVE implementations
// =============================================================================

#if defined(BITMAP_HAS_SVE)

[[gnu::always_inline]] inline void and_sve(uint64_t* __restrict__ dst, const uint64_t* __restrict__ src,
                                            size_t word_count) noexcept {
    size_t i = 0;
    size_t vec_len = svcntd();  // Number of 64-bit elements per vector

    while (i + vec_len <= word_count) {
        svbool_t pg = svptrue_b64();
        svuint64_t a = svld1_u64(pg, dst + i);
        svuint64_t b = svld1_u64(pg, src + i);
        svst1_u64(pg, dst + i, svand_u64_x(pg, a, b));
        i += vec_len;
    }

    // Tail with predicate
    if (i < word_count) {
        svbool_t pg = svwhilelt_b64(i, word_count);
        svuint64_t a = svld1_u64(pg, dst + i);
        svuint64_t b = svld1_u64(pg, src + i);
        svst1_u64(pg, dst + i, svand_u64_x(pg, a, b));
    }
}

[[gnu::always_inline]] inline void or_sve(uint64_t* __restrict__ dst, const uint64_t* __restrict__ src,
                                           size_t word_count) noexcept {
    size_t i = 0;
    size_t vec_len = svcntd();

    while (i + vec_len <= word_count) {
        svbool_t pg = svptrue_b64();
        svuint64_t a = svld1_u64(pg, dst + i);
        svuint64_t b = svld1_u64(pg, src + i);
        svst1_u64(pg, dst + i, svorr_u64_x(pg, a, b));
        i += vec_len;
    }

    if (i < word_count) {
        svbool_t pg = svwhilelt_b64(i, word_count);
        svuint64_t a = svld1_u64(pg, dst + i);
        svuint64_t b = svld1_u64(pg, src + i);
        svst1_u64(pg, dst + i, svorr_u64_x(pg, a, b));
    }
}

[[gnu::always_inline]] inline void xor_sve(uint64_t* __restrict__ dst, const uint64_t* __restrict__ src,
                                            size_t word_count) noexcept {
    size_t i = 0;
    size_t vec_len = svcntd();

    while (i + vec_len <= word_count) {
        svbool_t pg = svptrue_b64();
        svuint64_t a = svld1_u64(pg, dst + i);
        svuint64_t b = svld1_u64(pg, src + i);
        svst1_u64(pg, dst + i, sveor_u64_x(pg, a, b));
        i += vec_len;
    }

    if (i < word_count) {
        svbool_t pg = svwhilelt_b64(i, word_count);
        svuint64_t a = svld1_u64(pg, dst + i);
        svuint64_t b = svld1_u64(pg, src + i);
        svst1_u64(pg, dst + i, sveor_u64_x(pg, a, b));
    }
}

[[gnu::always_inline]] inline void andnot_sve(uint64_t* __restrict__ dst, const uint64_t* __restrict__ src,
                                               size_t word_count) noexcept {
    size_t i = 0;
    size_t vec_len = svcntd();

    while (i + vec_len <= word_count) {
        svbool_t pg = svptrue_b64();
        svuint64_t a = svld1_u64(pg, dst + i);
        svuint64_t b = svld1_u64(pg, src + i);
        // a & ~b = svbic(a, b)
        svst1_u64(pg, dst + i, svbic_u64_x(pg, a, b));
        i += vec_len;
    }

    if (i < word_count) {
        svbool_t pg = svwhilelt_b64(i, word_count);
        svuint64_t a = svld1_u64(pg, dst + i);
        svuint64_t b = svld1_u64(pg, src + i);
        svst1_u64(pg, dst + i, svbic_u64_x(pg, a, b));
    }
}

[[gnu::always_inline]] inline size_t popcount_sve(const uint64_t* data, size_t word_count) noexcept {
    size_t i = 0;
    size_t vec_len = svcntd();
    svuint64_t acc = svdup_u64(0);

    while (i + vec_len <= word_count) {
        svbool_t pg = svptrue_b64();
        svuint64_t v = svld1_u64(pg, data + i);
        acc = svadd_u64_x(pg, acc, svcnt_u64_x(pg, v));
        i += vec_len;
    }

    // Horizontal sum
    size_t total = svaddv_u64(svptrue_b64(), acc);

    // Scalar tail
    for (; i < word_count; ++i) {
        total += __builtin_popcountll(data[i]);
    }

    return total;
}

#endif  // BITMAP_HAS_SVE

// =============================================================================
// Dispatch functions - select best implementation at compile time
// =============================================================================

[[gnu::always_inline]] inline void bitmap_and(uint64_t* __restrict__ dst, const uint64_t* __restrict__ src,
                                               size_t word_count) noexcept {
#if defined(BITMAP_HAS_AVX512)
    and_avx512(dst, src, word_count);
#elif defined(BITMAP_HAS_AVX2)
    and_avx2(dst, src, word_count);
#elif defined(BITMAP_HAS_SSE2)
    and_sse2(dst, src, word_count);
#elif defined(BITMAP_HAS_SVE)
    and_sve(dst, src, word_count);
#elif defined(BITMAP_HAS_NEON)
    and_neon(dst, src, word_count);
#else
    and_scalar(dst, src, word_count);
#endif
}

[[gnu::always_inline]] inline void bitmap_or(uint64_t* __restrict__ dst, const uint64_t* __restrict__ src,
                                              size_t word_count) noexcept {
#if defined(BITMAP_HAS_AVX512)
    or_avx512(dst, src, word_count);
#elif defined(BITMAP_HAS_AVX2)
    or_avx2(dst, src, word_count);
#elif defined(BITMAP_HAS_SSE2)
    or_sse2(dst, src, word_count);
#elif defined(BITMAP_HAS_SVE)
    or_sve(dst, src, word_count);
#elif defined(BITMAP_HAS_NEON)
    or_neon(dst, src, word_count);
#else
    or_scalar(dst, src, word_count);
#endif
}

[[gnu::always_inline]] inline void bitmap_xor(uint64_t* __restrict__ dst, const uint64_t* __restrict__ src,
                                               size_t word_count) noexcept {
#if defined(BITMAP_HAS_AVX512)
    xor_avx512(dst, src, word_count);
#elif defined(BITMAP_HAS_AVX2)
    xor_avx2(dst, src, word_count);
#elif defined(BITMAP_HAS_SSE2)
    xor_sse2(dst, src, word_count);
#elif defined(BITMAP_HAS_SVE)
    xor_sve(dst, src, word_count);
#elif defined(BITMAP_HAS_NEON)
    xor_neon(dst, src, word_count);
#else
    xor_scalar(dst, src, word_count);
#endif
}

[[gnu::always_inline]] inline void bitmap_andnot(uint64_t* __restrict__ dst, const uint64_t* __restrict__ src,
                                                  size_t word_count) noexcept {
#if defined(BITMAP_HAS_AVX512)
    andnot_avx512(dst, src, word_count);
#elif defined(BITMAP_HAS_AVX2)
    andnot_avx2(dst, src, word_count);
#elif defined(BITMAP_HAS_SSE2)
    andnot_sse2(dst, src, word_count);
#elif defined(BITMAP_HAS_SVE)
    andnot_sve(dst, src, word_count);
#elif defined(BITMAP_HAS_NEON)
    andnot_neon(dst, src, word_count);
#else
    andnot_scalar(dst, src, word_count);
#endif
}

[[gnu::always_inline]] inline size_t bitmap_popcount(const uint64_t* data, size_t word_count) noexcept {
#if defined(BITMAP_HAS_AVX512_VPOPCNT)
    return popcount_avx512(data, word_count);
#elif defined(BITMAP_HAS_AVX2)
    return popcount_avx2(data, word_count);
#elif defined(BITMAP_HAS_SVE)
    return popcount_sve(data, word_count);
#elif defined(BITMAP_HAS_NEON)
    return popcount_neon(data, word_count);
#else
    return popcount_scalar(data, word_count);
#endif
}

}  // namespace bitmap_detail

// =============================================================================
// Dynamic bitmap class
// =============================================================================

class bitmap
{
public:
    using word_type = uint64_t;
    static constexpr size_t bits_per_word = bitmap_detail::kBitsPerWord;

private:
    word_type* _data = nullptr;
    size_t _size = 0;       // Number of bits
    size_t _capacity = 0;   // Number of words allocated

public:
    // -------------------------------------------------------------------------
    // Constructors / Destructor
    // -------------------------------------------------------------------------

    bitmap() noexcept = default;

    explicit bitmap(size_t num_bits) : _size(num_bits) {
        if (num_bits > 0) {
            _capacity = bitmap_detail::words_for_bits(num_bits);
            _data = bitmap_detail::allocate_aligned(_capacity);
            std::memset(_data, 0, _capacity * sizeof(word_type));
        }
    }

    bitmap(const bitmap& other) : _size(other._size), _capacity(other._capacity) {
        if (_capacity > 0) {
            _data = bitmap_detail::allocate_aligned(_capacity);
            std::memcpy(_data, other._data, _capacity * sizeof(word_type));
        }
    }

    bitmap(bitmap&& other) noexcept : _data(other._data), _size(other._size), _capacity(other._capacity) {
        other._data = nullptr;
        other._size = 0;
        other._capacity = 0;
    }

    ~bitmap() {
        bitmap_detail::deallocate_aligned(_data);
    }

    bitmap& operator=(const bitmap& other) {
        if (this != &other) {
            if (_capacity < other._capacity) {
                bitmap_detail::deallocate_aligned(_data);
                _capacity = other._capacity;
                _data = bitmap_detail::allocate_aligned(_capacity);
            }
            _size = other._size;
            if (_capacity > 0) {
                std::memcpy(_data, other._data, other._capacity * sizeof(word_type));
            }
        }
        return *this;
    }

    bitmap& operator=(bitmap&& other) noexcept {
        if (this != &other) {
            bitmap_detail::deallocate_aligned(_data);
            _data = other._data;
            _size = other._size;
            _capacity = other._capacity;
            other._data = nullptr;
            other._size = 0;
            other._capacity = 0;
        }
        return *this;
    }

    // -------------------------------------------------------------------------
    // Capacity
    // -------------------------------------------------------------------------

    [[nodiscard]] size_t size() const noexcept { return _size; }
    [[nodiscard]] bool empty() const noexcept { return _size == 0; }
    [[nodiscard]] size_t capacity_bits() const noexcept { return _capacity * bits_per_word; }
    [[nodiscard]] size_t word_count() const noexcept { return bitmap_detail::words_for_bits(_size); }

    void resize(size_t new_size) {
        size_t new_words = bitmap_detail::words_for_bits(new_size);
        if (new_words > _capacity) {
            word_type* new_data = bitmap_detail::allocate_aligned(new_words);
            if (_data) {
                std::memcpy(new_data, _data, _capacity * sizeof(word_type));
                std::memset(new_data + _capacity, 0, (new_words - _capacity) * sizeof(word_type));
                bitmap_detail::deallocate_aligned(_data);
            } else {
                std::memset(new_data, 0, new_words * sizeof(word_type));
            }
            _data = new_data;
            _capacity = new_words;
        }
        // Clear bits beyond new_size if shrinking
        if (new_size < _size && new_size > 0) {
            size_t last_word = (new_size - 1) / bits_per_word;
            size_t bit_in_word = new_size % bits_per_word;
            if (bit_in_word != 0) {
                _data[last_word] &= (word_type{1} << bit_in_word) - 1;
            }
            // Zero out words beyond
            for (size_t i = last_word + 1; i < bitmap_detail::words_for_bits(_size); ++i) {
                _data[i] = 0;
            }
        }
        _size = new_size;
    }

    void reserve(size_t num_bits) {
        size_t new_words = bitmap_detail::words_for_bits(num_bits);
        if (new_words > _capacity) {
            word_type* new_data = bitmap_detail::allocate_aligned(new_words);
            if (_data) {
                std::memcpy(new_data, _data, _capacity * sizeof(word_type));
                std::memset(new_data + _capacity, 0, (new_words - _capacity) * sizeof(word_type));
                bitmap_detail::deallocate_aligned(_data);
            } else {
                std::memset(new_data, 0, new_words * sizeof(word_type));
            }
            _data = new_data;
            _capacity = new_words;
        }
    }

    void clear() noexcept {
        if (_data) {
            std::memset(_data, 0, _capacity * sizeof(word_type));
        }
    }

    // -------------------------------------------------------------------------
    // Single bit operations
    // -------------------------------------------------------------------------

    void set(uint32_t id) noexcept {
        BITMAP_ASSERT(id < _size, "id out of range");
        _data[id / bits_per_word] |= word_type{1} << (id % bits_per_word);
    }

    void clear_bit(uint32_t id) noexcept {
        BITMAP_ASSERT(id < _size, "id out of range");
        _data[id / bits_per_word] &= ~(word_type{1} << (id % bits_per_word));
    }

    void flip(uint32_t id) noexcept {
        BITMAP_ASSERT(id < _size, "id out of range");
        _data[id / bits_per_word] ^= word_type{1} << (id % bits_per_word);
    }

    [[nodiscard]] bool test(uint32_t id) const noexcept {
        BITMAP_ASSERT(id < _size, "id out of range");
        return (_data[id / bits_per_word] & (word_type{1} << (id % bits_per_word))) != 0;
    }

    // Alias for test
    [[nodiscard]] bool operator[](uint32_t id) const noexcept { return test(id); }

    // -------------------------------------------------------------------------
    // Bulk IN operations
    // -------------------------------------------------------------------------

    // Returns count of matching ids, writes matching ids to result
    [[nodiscard]] size_t in(const uint32_t* ids, size_t count, uint32_t* result) const noexcept {
        size_t out = 0;
        for (size_t i = 0; i < count; ++i) {
            uint32_t id = ids[i];
            if (id < _size && test(id)) {
                result[out++] = id;
            }
        }
        return out;
    }

    // Writes matching flags to result bitmap (must be pre-sized to count bits)
    void in(const uint32_t* ids, size_t count, bitmap& result) const noexcept {
        result.resize(count);
        result.clear();
        for (size_t i = 0; i < count; ++i) {
            uint32_t id = ids[i];
            if (id < _size && test(id)) {
                result.set(static_cast<uint32_t>(i));
            }
        }
    }

    // Span-based versions
    [[nodiscard]] size_t in(std::span<const uint32_t> ids, std::span<uint32_t> result) const noexcept {
        return in(ids.data(), std::min(ids.size(), result.size()), result.data());
    }

    void in(std::span<const uint32_t> ids, bitmap& result) const noexcept {
        in(ids.data(), ids.size(), result);
    }

    // -------------------------------------------------------------------------
    // Bulk bitwise operations
    // -------------------------------------------------------------------------

    bitmap& and_(const bitmap& other) noexcept {
        size_t min_words = std::min(word_count(), other.word_count());
        bitmap_detail::bitmap_and(_data, other._data, min_words);
        // Zero out words beyond other's size
        for (size_t i = min_words; i < word_count(); ++i) {
            _data[i] = 0;
        }
        return *this;
    }

    bitmap& or_(const bitmap& other) noexcept {
        size_t min_words = std::min(word_count(), other.word_count());
        bitmap_detail::bitmap_or(_data, other._data, min_words);
        return *this;
    }

    bitmap& xor_(const bitmap& other) noexcept {
        size_t min_words = std::min(word_count(), other.word_count());
        bitmap_detail::bitmap_xor(_data, other._data, min_words);
        return *this;
    }

    bitmap& andnot_(const bitmap& other) noexcept {
        size_t min_words = std::min(word_count(), other.word_count());
        bitmap_detail::bitmap_andnot(_data, other._data, min_words);
        return *this;
    }

    // Operator versions
    bitmap& operator&=(const bitmap& other) noexcept { return and_(other); }
    bitmap& operator|=(const bitmap& other) noexcept { return or_(other); }
    bitmap& operator^=(const bitmap& other) noexcept { return xor_(other); }

    // -------------------------------------------------------------------------
    // Population count and iteration
    // -------------------------------------------------------------------------

    [[nodiscard]] size_t popcount() const noexcept {
        return bitmap_detail::bitmap_popcount(_data, word_count());
    }

    [[nodiscard]] int64_t find_first() const noexcept {
        size_t words = word_count();
        for (size_t i = 0; i < words; ++i) {
            if (_data[i] != 0) {
                int64_t bit = static_cast<int64_t>(i * bits_per_word + __builtin_ctzll(_data[i]));
                return bit < static_cast<int64_t>(_size) ? bit : -1;
            }
        }
        return -1;
    }

    [[nodiscard]] int64_t find_next(uint32_t pos) const noexcept {
        if (pos + 1 >= _size) return -1;

        uint32_t start = pos + 1;
        size_t word_idx = start / bits_per_word;
        size_t bit_idx = start % bits_per_word;

        // Check current word (mask out bits before start)
        word_type word = _data[word_idx] & (~word_type{0} << bit_idx);
        if (word != 0) {
            int64_t bit = static_cast<int64_t>(word_idx * bits_per_word + __builtin_ctzll(word));
            return bit < static_cast<int64_t>(_size) ? bit : -1;
        }

        // Check subsequent words
        size_t words = word_count();
        for (size_t i = word_idx + 1; i < words; ++i) {
            if (_data[i] != 0) {
                int64_t bit = static_cast<int64_t>(i * bits_per_word + __builtin_ctzll(_data[i]));
                return bit < static_cast<int64_t>(_size) ? bit : -1;
            }
        }

        return -1;
    }

    // -------------------------------------------------------------------------
    // Raw data access
    // -------------------------------------------------------------------------

    [[nodiscard]] word_type* data() noexcept { return _data; }
    [[nodiscard]] const word_type* data() const noexcept { return _data; }

    // -------------------------------------------------------------------------
    // Iterator for set bits
    // -------------------------------------------------------------------------

    class set_bit_iterator
    {
    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = uint32_t;
        using difference_type = std::ptrdiff_t;
        using pointer = const uint32_t*;
        using reference = uint32_t;

    private:
        const bitmap* _bm = nullptr;
        int64_t _pos = -1;

    public:
        set_bit_iterator() = default;
        set_bit_iterator(const bitmap* bm, int64_t pos) : _bm(bm), _pos(pos) {}

        reference operator*() const { return static_cast<uint32_t>(_pos); }

        set_bit_iterator& operator++() {
            _pos = _bm->find_next(static_cast<uint32_t>(_pos));
            return *this;
        }

        set_bit_iterator operator++(int) {
            auto tmp = *this;
            ++(*this);
            return tmp;
        }

        bool operator==(const set_bit_iterator& other) const { return _pos == other._pos; }
        bool operator!=(const set_bit_iterator& other) const { return _pos != other._pos; }
    };

    [[nodiscard]] set_bit_iterator begin() const { return set_bit_iterator(this, find_first()); }
    [[nodiscard]] set_bit_iterator end() const { return set_bit_iterator(this, -1); }
};

// =============================================================================
// Static bitmap class (compile-time size)
// =============================================================================

template <size_t NumBits>
class static_bitmap
{
public:
    using word_type = uint64_t;
    static constexpr size_t bits_per_word = bitmap_detail::kBitsPerWord;
    static constexpr size_t num_words = bitmap_detail::words_for_bits(NumBits);

private:
    alignas(bitmap_detail::kCacheLineSize) word_type _data[num_words] = {};

public:
    // -------------------------------------------------------------------------
    // Constructors
    // -------------------------------------------------------------------------

    static_bitmap() noexcept = default;

    static_bitmap(const static_bitmap& other) noexcept {
        std::memcpy(_data, other._data, sizeof(_data));
    }

    static_bitmap& operator=(const static_bitmap& other) noexcept {
        if (this != &other) {
            std::memcpy(_data, other._data, sizeof(_data));
        }
        return *this;
    }

    // -------------------------------------------------------------------------
    // Capacity
    // -------------------------------------------------------------------------

    [[nodiscard]] static constexpr size_t size() noexcept { return NumBits; }
    [[nodiscard]] static constexpr bool empty() noexcept { return NumBits == 0; }
    [[nodiscard]] static constexpr size_t word_count() noexcept { return num_words; }

    void clear() noexcept {
        std::memset(_data, 0, sizeof(_data));
    }

    void fill() noexcept {
        std::memset(_data, 0xFF, sizeof(_data));
        // Clear excess bits in last word
        if constexpr (NumBits % bits_per_word != 0) {
            _data[num_words - 1] &= (word_type{1} << (NumBits % bits_per_word)) - 1;
        }
    }

    // -------------------------------------------------------------------------
    // Single bit operations
    // -------------------------------------------------------------------------

    void set(uint32_t id) noexcept {
        BITMAP_ASSERT(id < NumBits, "id out of range");
        _data[id / bits_per_word] |= word_type{1} << (id % bits_per_word);
    }

    void clear_bit(uint32_t id) noexcept {
        BITMAP_ASSERT(id < NumBits, "id out of range");
        _data[id / bits_per_word] &= ~(word_type{1} << (id % bits_per_word));
    }

    void flip(uint32_t id) noexcept {
        BITMAP_ASSERT(id < NumBits, "id out of range");
        _data[id / bits_per_word] ^= word_type{1} << (id % bits_per_word);
    }

    [[nodiscard]] bool test(uint32_t id) const noexcept {
        BITMAP_ASSERT(id < NumBits, "id out of range");
        return (_data[id / bits_per_word] & (word_type{1} << (id % bits_per_word))) != 0;
    }

    [[nodiscard]] bool operator[](uint32_t id) const noexcept { return test(id); }

    // -------------------------------------------------------------------------
    // Bulk IN operations
    // -------------------------------------------------------------------------

    [[nodiscard]] size_t in(const uint32_t* ids, size_t count, uint32_t* result) const noexcept {
        size_t out = 0;
        for (size_t i = 0; i < count; ++i) {
            uint32_t id = ids[i];
            if (id < NumBits && test(id)) {
                result[out++] = id;
            }
        }
        return out;
    }

    template <size_t ResultBits>
    void in(const uint32_t* ids, size_t count, static_bitmap<ResultBits>& result) const noexcept {
        result.clear();
        for (size_t i = 0; i < count && i < ResultBits; ++i) {
            uint32_t id = ids[i];
            if (id < NumBits && test(id)) {
                result.set(static_cast<uint32_t>(i));
            }
        }
    }

    // -------------------------------------------------------------------------
    // Bulk bitwise operations
    // -------------------------------------------------------------------------

    static_bitmap& and_(const static_bitmap& other) noexcept {
        bitmap_detail::bitmap_and(_data, other._data, num_words);
        return *this;
    }

    static_bitmap& or_(const static_bitmap& other) noexcept {
        bitmap_detail::bitmap_or(_data, other._data, num_words);
        return *this;
    }

    static_bitmap& xor_(const static_bitmap& other) noexcept {
        bitmap_detail::bitmap_xor(_data, other._data, num_words);
        return *this;
    }

    static_bitmap& andnot_(const static_bitmap& other) noexcept {
        bitmap_detail::bitmap_andnot(_data, other._data, num_words);
        return *this;
    }

    static_bitmap& operator&=(const static_bitmap& other) noexcept { return and_(other); }
    static_bitmap& operator|=(const static_bitmap& other) noexcept { return or_(other); }
    static_bitmap& operator^=(const static_bitmap& other) noexcept { return xor_(other); }

    // -------------------------------------------------------------------------
    // Population count and iteration
    // -------------------------------------------------------------------------

    [[nodiscard]] size_t popcount() const noexcept {
        return bitmap_detail::bitmap_popcount(_data, num_words);
    }

    [[nodiscard]] int64_t find_first() const noexcept {
        for (size_t i = 0; i < num_words; ++i) {
            if (_data[i] != 0) {
                int64_t bit = static_cast<int64_t>(i * bits_per_word + __builtin_ctzll(_data[i]));
                return bit < static_cast<int64_t>(NumBits) ? bit : -1;
            }
        }
        return -1;
    }

    [[nodiscard]] int64_t find_next(uint32_t pos) const noexcept {
        if (pos + 1 >= NumBits) return -1;

        uint32_t start = pos + 1;
        size_t word_idx = start / bits_per_word;
        size_t bit_idx = start % bits_per_word;

        word_type word = _data[word_idx] & (~word_type{0} << bit_idx);
        if (word != 0) {
            int64_t bit = static_cast<int64_t>(word_idx * bits_per_word + __builtin_ctzll(word));
            return bit < static_cast<int64_t>(NumBits) ? bit : -1;
        }

        for (size_t i = word_idx + 1; i < num_words; ++i) {
            if (_data[i] != 0) {
                int64_t bit = static_cast<int64_t>(i * bits_per_word + __builtin_ctzll(_data[i]));
                return bit < static_cast<int64_t>(NumBits) ? bit : -1;
            }
        }

        return -1;
    }

    // -------------------------------------------------------------------------
    // Raw data access
    // -------------------------------------------------------------------------

    [[nodiscard]] word_type* data() noexcept { return _data; }
    [[nodiscard]] const word_type* data() const noexcept { return _data; }

    // -------------------------------------------------------------------------
    // Iterator for set bits
    // -------------------------------------------------------------------------

    class set_bit_iterator
    {
    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = uint32_t;
        using difference_type = std::ptrdiff_t;
        using pointer = const uint32_t*;
        using reference = uint32_t;

    private:
        const static_bitmap* _bm = nullptr;
        int64_t _pos = -1;

    public:
        set_bit_iterator() = default;
        set_bit_iterator(const static_bitmap* bm, int64_t pos) : _bm(bm), _pos(pos) {}

        reference operator*() const { return static_cast<uint32_t>(_pos); }

        set_bit_iterator& operator++() {
            _pos = _bm->find_next(static_cast<uint32_t>(_pos));
            return *this;
        }

        set_bit_iterator operator++(int) {
            auto tmp = *this;
            ++(*this);
            return tmp;
        }

        bool operator==(const set_bit_iterator& other) const { return _pos == other._pos; }
        bool operator!=(const set_bit_iterator& other) const { return _pos != other._pos; }
    };

    [[nodiscard]] set_bit_iterator begin() const { return set_bit_iterator(this, find_first()); }
    [[nodiscard]] set_bit_iterator end() const { return set_bit_iterator(this, -1); }
};

}  // namespace stdb::container

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
#include <bit>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <memory>
#include <memory_resource>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

// SIMD support detection
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <immintrin.h>
#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#define DENSE_MAP_SSE2 1
#endif
#if defined(__AVX2__)
#define DENSE_MAP_AVX2 1
#endif
#if defined(__AVX512F__) && defined(__AVX512BW__)
#define DENSE_MAP_AVX512 1
#endif
#elif defined(__aarch64__) || defined(_M_ARM64)
#include <arm_neon.h>
#define DENSE_MAP_NEON 1
#endif

// Compiler hints
#if defined(__GNUC__) || defined(__clang__)
#define DENSE_MAP_LIKELY(x) __builtin_expect(!!(x), 1)
#define DENSE_MAP_UNLIKELY(x) __builtin_expect(!!(x), 0)
#define DENSE_MAP_ALWAYS_INLINE [[gnu::always_inline]] inline
#define DENSE_MAP_NOINLINE [[gnu::noinline]]
#define DENSE_MAP_PREFETCH(addr) __builtin_prefetch(addr)
#elif defined(_MSC_VER)
#include <intrin.h>
#define DENSE_MAP_LIKELY(x) (x)
#define DENSE_MAP_UNLIKELY(x) (x)
#define DENSE_MAP_ALWAYS_INLINE __forceinline
#define DENSE_MAP_NOINLINE __declspec(noinline)
#define DENSE_MAP_PREFETCH(addr) _mm_prefetch(reinterpret_cast<const char*>(addr), _MM_HINT_T0)
#else
#define DENSE_MAP_LIKELY(x) (x)
#define DENSE_MAP_UNLIKELY(x) (x)
#define DENSE_MAP_ALWAYS_INLINE inline
#define DENSE_MAP_NOINLINE
#define DENSE_MAP_PREFETCH(addr) ((void)0)
#endif

namespace stdb::container {

// ============================================================================
// wyhash - Fast, high-quality hash function
// Based on wyhash by Wang Yi: https://github.com/wangyi-fudan/wyhash
// ============================================================================
namespace detail {

DENSE_MAP_ALWAYS_INLINE constexpr uint64_t wymum(uint64_t a, uint64_t b) {
#if defined(__SIZEOF_INT128__)
    __uint128_t r = static_cast<__uint128_t>(a) * b;
    return static_cast<uint64_t>(r ^ (r >> 64));
#else
    // Fallback for platforms without 128-bit integers
    uint64_t ha = a >> 32, hb = b >> 32;
    uint64_t la = static_cast<uint32_t>(a), lb = static_cast<uint32_t>(b);
    uint64_t hi = ha * hb;
    uint64_t lo = la * lb;
    uint64_t rh = ha * lb;
    uint64_t rl = la * hb;
    uint64_t t = rl + (lo >> 32);
    uint64_t c = t < rl;
    lo = (t << 32) | static_cast<uint32_t>(lo);
    hi += rh + (t >> 32) + c;
    return hi ^ lo;
#endif
}

DENSE_MAP_ALWAYS_INLINE uint64_t wyr8(const uint8_t* p) {
    uint64_t v;
    std::memcpy(&v, p, 8);
    return v;
}

DENSE_MAP_ALWAYS_INLINE uint64_t wyr4(const uint8_t* p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}

DENSE_MAP_ALWAYS_INLINE constexpr uint64_t wyr3(const uint8_t* p, size_t k) {
    return (static_cast<uint64_t>(p[0]) << 16) |
           (static_cast<uint64_t>(p[k >> 1]) << 8) | p[k - 1];
}

// wyhash constants
inline constexpr uint64_t wyp0 = 0xa0761d6478bd642full;
inline constexpr uint64_t wyp1 = 0xe7037ed1a0b428dbull;
inline constexpr uint64_t wyp2 = 0x8ebc6af09c88c6e3ull;
inline constexpr uint64_t wyp3 = 0x589965cc75374cc3ull;

DENSE_MAP_ALWAYS_INLINE uint64_t wyhash(const void* key, size_t len, uint64_t seed = 0) {
    const auto* p = static_cast<const uint8_t*>(key);
    seed ^= wyp0;
    uint64_t a, b;

    if (DENSE_MAP_LIKELY(len <= 16)) {
        if (DENSE_MAP_LIKELY(len >= 4)) {
            a = (wyr4(p) << 32) | wyr4(p + ((len >> 3) << 2));
            b = (wyr4(p + len - 4) << 32) | wyr4(p + len - 4 - ((len >> 3) << 2));
        } else if (DENSE_MAP_LIKELY(len > 0)) {
            a = wyr3(p, len);
            b = 0;
        } else {
            a = b = 0;
        }
    } else {
        size_t i = len;
        if (DENSE_MAP_UNLIKELY(i > 48)) {
            uint64_t see1 = seed, see2 = seed;
            do {
                seed = wymum(wyr8(p) ^ wyp1, wyr8(p + 8) ^ seed);
                see1 = wymum(wyr8(p + 16) ^ wyp2, wyr8(p + 24) ^ see1);
                see2 = wymum(wyr8(p + 32) ^ wyp3, wyr8(p + 40) ^ see2);
                p += 48;
                i -= 48;
            } while (DENSE_MAP_LIKELY(i > 48));
            seed ^= see1 ^ see2;
        }
        while (DENSE_MAP_UNLIKELY(i > 16)) {
            seed = wymum(wyr8(p) ^ wyp1, wyr8(p + 8) ^ seed);
            i -= 16;
            p += 16;
        }
        a = wyr8(p + i - 16);
        b = wyr8(p + i - 8);
    }
    return wymum(wyp1 ^ len, wymum(a ^ wyp1, b ^ seed));
}

// wyhash mix - 128-bit multiply with XOR fold (same as ankerl)
DENSE_MAP_ALWAYS_INLINE uint64_t wyhash_mix(uint64_t a, uint64_t b) {
#if defined(__SIZEOF_INT128__)
    __uint128_t r = static_cast<__uint128_t>(a) * b;
    return static_cast<uint64_t>(r) ^ static_cast<uint64_t>(r >> 64);
#else
    uint64_t ha = a >> 32, hb = b >> 32;
    uint64_t la = static_cast<uint32_t>(a), lb = static_cast<uint32_t>(b);
    uint64_t rh = ha * hb, rm0 = ha * lb, rm1 = hb * la, rl = la * lb;
    uint64_t t = rl + (rm0 << 32), c = t < rl;
    uint64_t lo = t + (rm1 << 32); c += lo < t;
    uint64_t hi = rh + (rm0 >> 32) + (rm1 >> 32) + c;
    return lo ^ hi;
#endif
}

// Hash for integral types - wyhash mix (same quality as ankerl)
template <typename T>
DENSE_MAP_ALWAYS_INLINE uint64_t hash_integral(T value) {
    static_assert(std::is_integral_v<T> || std::is_enum_v<T>);
    return wyhash_mix(static_cast<uint64_t>(value), 0x9e3779b97f4a7c15ull);
}

}  // namespace detail

// ============================================================================
// Default hash function - wyhash based
// ============================================================================
template <typename T, typename Enable = void>
struct dense_hash {
    size_t operator()(const T& value) const noexcept {
        return static_cast<size_t>(detail::wyhash(&value, sizeof(T)));
    }
};

// Specialization for integral types
template <typename T>
struct dense_hash<T, std::enable_if_t<std::is_integral_v<T> || std::is_enum_v<T>>> {
    size_t operator()(T value) const noexcept {
        return static_cast<size_t>(detail::hash_integral(value));
    }
};

// Specialization for pointers
template <typename T>
struct dense_hash<T*> {
    size_t operator()(T* ptr) const noexcept {
        return static_cast<size_t>(
            detail::hash_integral(reinterpret_cast<uintptr_t>(ptr)));
    }
};

// Specialization for std::string and string_view
template <>
struct dense_hash<std::string> {
    size_t operator()(const std::string& s) const noexcept {
        return static_cast<size_t>(detail::wyhash(s.data(), s.size()));
    }
};

template <>
struct dense_hash<std::string_view> {
    size_t operator()(std::string_view s) const noexcept {
        return static_cast<size_t>(detail::wyhash(s.data(), s.size()));
    }
};

// Transparent hash for string types
struct string_hash {
    using is_transparent = void;

    size_t operator()(const char* s) const noexcept {
        return static_cast<size_t>(detail::wyhash(s, std::strlen(s)));
    }
    size_t operator()(std::string_view s) const noexcept {
        return static_cast<size_t>(detail::wyhash(s.data(), s.size()));
    }
    size_t operator()(const std::string& s) const noexcept {
        return static_cast<size_t>(detail::wyhash(s.data(), s.size()));
    }
};

// ============================================================================
// SIMD utilities for Swiss Table probing
// ============================================================================
namespace detail {

// Control byte values
inline constexpr int8_t kEmpty = -128;     // 0b10000000
inline constexpr int8_t kDeleted = -2;     // 0b11111110
inline constexpr int8_t kSentinel = -1;    // 0b11111111

// Check if control byte is empty
DENSE_MAP_ALWAYS_INLINE constexpr bool is_empty(int8_t c) { return c == kEmpty; }

// Check if control byte is deleted
DENSE_MAP_ALWAYS_INLINE constexpr bool is_deleted(int8_t c) { return c == kDeleted; }

// Check if control byte is empty or deleted
DENSE_MAP_ALWAYS_INLINE constexpr bool is_empty_or_deleted(int8_t c) { return c < kSentinel; }

// Check if control byte is full (has an element)
DENSE_MAP_ALWAYS_INLINE constexpr bool is_full(int8_t c) { return c >= 0; }

// Extract H2 hash (top 7 bits mapped to 0-127)
DENSE_MAP_ALWAYS_INLINE constexpr int8_t h2(size_t hash) {
    return static_cast<int8_t>(hash >> (sizeof(size_t) * 8 - 7));
}

// Extract H1 hash (all bits except top 7)
DENSE_MAP_ALWAYS_INLINE constexpr size_t h1(size_t hash) { return hash; }

// Group width for SIMD operations
#if defined(DENSE_MAP_AVX512)
inline constexpr size_t kGroupWidth = 64;
#elif defined(DENSE_MAP_AVX2)
inline constexpr size_t kGroupWidth = 32;
#elif defined(DENSE_MAP_SSE2) || defined(DENSE_MAP_NEON)
inline constexpr size_t kGroupWidth = 16;
#else
inline constexpr size_t kGroupWidth = 8;
#endif

// BitMask helper for iterating over set bits
// Use 64-bit mask for AVX-512, 32-bit otherwise
struct BitMask {
#if defined(DENSE_MAP_AVX512)
    uint64_t mask;

    explicit BitMask(uint64_t m) : mask(m) {}

    explicit operator bool() const { return mask != 0; }

    uint32_t lowest_set_bit() const {
        return static_cast<uint32_t>(std::countr_zero(mask));
    }

    BitMask& remove_lowest_bit() {
        mask &= mask - 1;
        return *this;
    }

    uint32_t trailing_zeros() const {
        return static_cast<uint32_t>(std::countr_zero(mask));
    }
#else
    uint32_t mask;

    explicit BitMask(uint32_t m) : mask(m) {}

    explicit operator bool() const { return mask != 0; }

    uint32_t lowest_set_bit() const {
        return static_cast<uint32_t>(std::countr_zero(mask));
    }

    BitMask& remove_lowest_bit() {
        mask &= mask - 1;
        return *this;
    }

    uint32_t trailing_zeros() const {
        return static_cast<uint32_t>(std::countr_zero(mask));
    }
#endif
};

// Group operations - SIMD-accelerated probe operations
struct Group {
#if defined(DENSE_MAP_AVX512)
    __m512i ctrl;

    explicit Group(const int8_t* pos) {
        ctrl = _mm512_loadu_si512(reinterpret_cast<const __m512i*>(pos));
    }

    BitMask match(int8_t h) const {
        auto match_vec = _mm512_set1_epi8(h);
        // AVX-512BW: _mm512_cmpeq_epi8_mask returns 64-bit mask directly
        return BitMask(_mm512_cmpeq_epi8_mask(match_vec, ctrl));
    }

    BitMask match_empty() const {
        // Empty is exactly 0x80 (-128). Must use exact comparison, not high-bit check.
        // kDeleted (0xFE = -2) also has high bit set, so movemask alone would match both.
        auto empty = _mm512_set1_epi8(static_cast<int8_t>(kEmpty));
        return BitMask(_mm512_cmpeq_epi8_mask(ctrl, empty));
    }

    BitMask match_empty_or_deleted() const {
        // Empty (0x80) and Deleted (0xFE) both have high bit set
        // kSentinel (0x7F) is greater than both in signed comparison
        auto special = _mm512_set1_epi8(static_cast<int8_t>(kSentinel));
        return BitMask(_mm512_cmpgt_epi8_mask(special, ctrl));
    }

#elif defined(DENSE_MAP_AVX2)
    __m256i ctrl;

    explicit Group(const int8_t* pos) {
        ctrl = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(pos));
    }

    BitMask match(int8_t h) const {
        auto match_vec = _mm256_set1_epi8(h);
        return BitMask(static_cast<uint32_t>(
            _mm256_movemask_epi8(_mm256_cmpeq_epi8(match_vec, ctrl))));
    }

    BitMask match_empty() const {
        // Empty is exactly 0x80 (-128). Must use exact comparison, not high-bit check.
        // kDeleted (0xFE = -2) also has high bit set, so movemask alone would match both.
        auto empty = _mm256_set1_epi8(static_cast<int8_t>(kEmpty));
        return BitMask(static_cast<uint32_t>(
            _mm256_movemask_epi8(_mm256_cmpeq_epi8(ctrl, empty))));
    }

    BitMask match_empty_or_deleted() const {
        // Empty and Deleted both have high bit set and bit 0 clear
        auto special = _mm256_set1_epi8(static_cast<int8_t>(kSentinel));
        return BitMask(static_cast<uint32_t>(
            _mm256_movemask_epi8(_mm256_cmpgt_epi8(special, ctrl))));
    }

#elif defined(DENSE_MAP_SSE2)
    __m128i ctrl;

    explicit Group(const int8_t* pos) {
        ctrl = _mm_loadu_si128(reinterpret_cast<const __m128i*>(pos));
    }

    BitMask match(int8_t h) const {
        auto match_vec = _mm_set1_epi8(h);
        return BitMask(static_cast<uint32_t>(
            _mm_movemask_epi8(_mm_cmpeq_epi8(match_vec, ctrl))));
    }

    BitMask match_empty() const {
        // Empty is exactly 0x80 (-128). Must use exact comparison, not high-bit check.
        // kDeleted (0xFE = -2) also has high bit set, so movemask alone would match both.
        auto empty = _mm_set1_epi8(static_cast<int8_t>(kEmpty));
        return BitMask(static_cast<uint32_t>(
            _mm_movemask_epi8(_mm_cmpeq_epi8(ctrl, empty))));
    }

    BitMask match_empty_or_deleted() const {
        auto special = _mm_set1_epi8(static_cast<int8_t>(kSentinel));
        return BitMask(static_cast<uint32_t>(
            _mm_movemask_epi8(_mm_cmpgt_epi8(special, ctrl))));
    }

#elif defined(DENSE_MAP_NEON)
    uint8x16_t ctrl;

    explicit Group(const int8_t* pos) {
        ctrl = vld1q_u8(reinterpret_cast<const uint8_t*>(pos));
    }

    BitMask match(int8_t h) const {
        auto match_vec = vdupq_n_u8(static_cast<uint8_t>(h));
        auto cmp = vceqq_u8(match_vec, ctrl);
        // Convert to bitmask using horizontal sum (correct for multiple matches)
        static const uint8_t shifts[] = {1, 2, 4, 8, 16, 32, 64, 128,
                                         1, 2, 4, 8, 16, 32, 64, 128};
        auto bits = vld1q_u8(shifts);
        auto masked = vandq_u8(cmp, bits);
        // Sum each 8-byte half separately to get 8-bit bitmasks
        uint8_t lo = vaddv_u8(vget_low_u8(masked));
        uint8_t hi = vaddv_u8(vget_high_u8(masked));
        uint16_t result = static_cast<uint16_t>(lo) | (static_cast<uint16_t>(hi) << 8);
        return BitMask(result);
    }

    BitMask match_empty() const {
        // Empty is exactly 0x80 (-128). Must use exact comparison, not high-bit check.
        // kDeleted (0xFE = -2) also has high bit set, so msb check alone would match both.
        auto empty = vdupq_n_s8(static_cast<int8_t>(kEmpty));
        auto eq = vceqq_s8(vreinterpretq_s8_u8(ctrl), empty);
        auto matches = vreinterpretq_u8_s8(eq);
        static const uint8_t shifts[] = {1, 2, 4, 8, 16, 32, 64, 128,
                                         1, 2, 4, 8, 16, 32, 64, 128};
        auto bits = vld1q_u8(shifts);
        auto masked = vandq_u8(matches, bits);
        // Sum each 8-byte half separately to get 8-bit bitmasks
        uint8_t lo = vaddv_u8(vget_low_u8(masked));
        uint8_t hi = vaddv_u8(vget_high_u8(masked));
        uint16_t result = static_cast<uint16_t>(lo) | (static_cast<uint16_t>(hi) << 8);
        return BitMask(result);
    }

    BitMask match_empty_or_deleted() const {
        auto special = vdupq_n_s8(kSentinel);
        auto cmp = vcgtq_s8(special, vreinterpretq_s8_u8(ctrl));
        static const uint8_t shifts[] = {1, 2, 4, 8, 16, 32, 64, 128,
                                         1, 2, 4, 8, 16, 32, 64, 128};
        auto bits = vld1q_u8(shifts);
        auto masked = vandq_u8(cmp, bits);  // vcgtq_s8 returns uint8x16_t
        // Sum each 8-byte half separately to get 8-bit bitmasks
        uint8_t lo = vaddv_u8(vget_low_u8(masked));
        uint8_t hi = vaddv_u8(vget_high_u8(masked));
        uint16_t result = static_cast<uint16_t>(lo) | (static_cast<uint16_t>(hi) << 8);
        return BitMask(result);
    }

#else
    // Portable fallback
    int8_t ctrl[8];

    explicit Group(const int8_t* pos) { std::memcpy(ctrl, pos, 8); }

    BitMask match(int8_t h) const {
        uint32_t mask = 0;
        for (int i = 0; i < 8; ++i) {
            if (ctrl[i] == h) mask |= (1u << i);
        }
        return BitMask(mask);
    }

    BitMask match_empty() const {
        uint32_t mask = 0;
        for (int i = 0; i < 8; ++i) {
            if (ctrl[i] == kEmpty) mask |= (1u << i);
        }
        return BitMask(mask);
    }

    BitMask match_empty_or_deleted() const {
        uint32_t mask = 0;
        for (int i = 0; i < 8; ++i) {
            if (ctrl[i] < kSentinel) mask |= (1u << i);
        }
        return BitMask(mask);
    }
#endif
};

// Probe sequence - linear probing for better cache locality
class ProbeSeq {
public:
    ProbeSeq(size_t hash, size_t mask) : mask_(mask), offset_(hash & mask) {}

    size_t offset() const { return offset_; }
    size_t offset(size_t i) const { return (offset_ + i) & mask_; }

    void next() {
        offset_ = (offset_ + kGroupWidth) & mask_;
    }

private:
    size_t mask_;
    size_t offset_;
};

}  // namespace detail

// ============================================================================
// Set/Map type trait
// ============================================================================
namespace detail {
template <typename T>
constexpr bool is_map_v = !std::is_void_v<T>;
}  // namespace detail

// ============================================================================
// Memory Policy - Selects storage layout based on type characteristics
// ============================================================================

// Storage layout tags
struct inline_storage_tag {};    // Swiss Table: ctrl bytes + inline slots
struct indirect_storage_tag {};  // Swiss Table + indirect values (for string keys)
struct flat_storage_tag {};      // ankerl-style: Robin Hood buckets + contiguous values

// Default memory policy: select storage based on type characteristics
// T can be void for set mode
template <typename Key, typename T>
struct default_memory_policy {
    static constexpr bool is_map = detail::is_map_v<T>;
    using value_type = std::conditional_t<is_map, std::pair<Key, T>, Key>;

    // For small trivially copyable keys (int, pointers, etc.), use flat storage (ankerl-style)
    // This gives better cache efficiency: fingerprint + index in same bucket
    static constexpr bool use_flat =
        sizeof(Key) <= 8 &&
        std::is_trivially_copyable_v<Key>;

    // Use Swiss Table indirect for larger/non-trivial keys (like strings)
    // SIMD h2 matching saves expensive key comparisons
    static constexpr bool use_indirect = !use_flat;

    using storage_tag = std::conditional_t<use_flat,
                                           flat_storage_tag,
                                           indirect_storage_tag>;
};

// Force inline storage (Swiss Table style)
// Best for: find-heavy workloads, small value types
// Trade-off: Fast find (1 cache line), but slow iteration (skip empty slots)
struct force_inline_policy {
    using storage_tag = inline_storage_tag;
};

// Force indirect storage (Swiss Table + indirect values)
// Best for: string keys, large value types
// Trade-off: SIMD matching + pointer stability, but 2 cache lines for find
struct force_indirect_policy {
    using storage_tag = indirect_storage_tag;
};

// Force flat storage (ankerl/Robin Hood style - best for small keys)
// Best for: balanced workloads with iteration
// Trade-off: Fast iteration + insert, find needs 2 memory accesses
struct force_flat_policy {
    using storage_tag = flat_storage_tag;
};

// ============================================================================
// User-friendly storage policy aliases
// ============================================================================

// Inline storage: key-value pairs stored directly in hash table slots
// Similar to: tsl::robin_map, absl::flat_hash_map
// Best for:
// - Large maps (1M+ elements) with high miss rates
// - Workloads where most finds() return end() (key not found)
// Trade-offs:
// - Slower iteration (must skip empty slots)
// - No pointer/iterator stability across rehash
// - More memory movement on insert (shift entire KV pairs)
using inline_storage_policy = force_inline_policy;

// Flat storage: buckets store index, values in separate contiguous array
// Similar to: ankerl::unordered_dense
// Best for:
// - Fast iteration over all elements (contiguous memory)
// - Balanced insert/find/iterate performance
// - High hit rate workloads
// - Pointer stability for values (not invalidated by non-rehashing insert)
// - Large value types (only 8-byte buckets are shifted)
using flat_storage_policy = force_flat_policy;

// Legacy/convenience aliases
using swiss_table_policy = inline_storage_policy;
using robin_hood_policy = flat_storage_policy;  // Note: different from tsl::robin_map!

// ============================================================================
// Bucket for flat storage (ankerl-style Robin Hood)
// ============================================================================
namespace detail {

// Bucket stores fingerprint+distance and index into values array
// Layout matches ankerl::unordered_dense for optimal comparison:
// - dist_and_fingerprint: upper 24 bits = distance, lower 8 bits = fingerprint
// - This allows single integer comparison for Robin Hood probing
struct Bucket {
    static constexpr uint32_t kDistInc = 1U << 8U;           // Increment for distance (skip fingerprint byte)
    static constexpr uint32_t kFingerprintMask = kDistInc - 1;  // Mask for 8-bit fingerprint

    uint32_t dist_and_fingerprint;  // [distance:24][fingerprint:8] - 0 means empty
    uint32_t value_idx;             // Index into values array

    Bucket() : dist_and_fingerprint(0), value_idx(0) {}
    Bucket(uint32_t daf, uint32_t idx) : dist_and_fingerprint(daf), value_idx(idx) {}

    // Empty bucket has dist_and_fingerprint == 0
    bool is_empty() const { return dist_and_fingerprint == 0; }

    // Create dist_and_fingerprint from hash (starting distance = 1)
    static uint32_t make_dist_and_fingerprint(uint64_t hash) {
        return kDistInc | (static_cast<uint32_t>(hash) & kFingerprintMask);
    }

    // Increment distance portion only
    static uint32_t inc_dist(uint32_t daf) {
        return daf + kDistInc;
    }

    void set_empty() { dist_and_fingerprint = 0; value_idx = 0; }
};

}  // namespace detail

// ============================================================================
// dense_map - High-performance hash map/set with type-specialized storage
// When T=void, acts as a set. Otherwise acts as a map.
// ============================================================================
template <typename Key, typename T = void, typename Hash = dense_hash<Key>,
          typename KeyEqual = std::equal_to<Key>,
          typename Allocator = std::allocator<std::conditional_t<detail::is_map_v<T>, std::pair<Key, T>, Key>>,
          typename MemoryPolicy = default_memory_policy<Key, T>>
class dense_map {
public:
    static constexpr bool is_map = detail::is_map_v<T>;
    using key_type = Key;
    using mapped_type = T;  // void for set
    using value_type = std::conditional_t<is_map, std::pair<Key, T>, Key>;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    using hasher = Hash;
    using key_equal = KeyEqual;
    using allocator_type = Allocator;
    using reference = value_type&;
    using const_reference = const value_type&;
    using pointer = typename std::allocator_traits<Allocator>::pointer;
    using const_pointer = typename std::allocator_traits<Allocator>::const_pointer;

private:
    using AllocTraits = std::allocator_traits<Allocator>;
    using CtrlAlloc =
        typename AllocTraits::template rebind_alloc<int8_t>;
    using SlotAlloc =
        typename AllocTraits::template rebind_alloc<value_type>;
    using IndexAlloc =
        typename AllocTraits::template rebind_alloc<uint32_t>;
    using BucketAlloc =
        typename AllocTraits::template rebind_alloc<detail::Bucket>;

    // Storage policy selection
    using storage_tag = typename MemoryPolicy::storage_tag;
    static constexpr bool kUseInline = std::is_same_v<storage_tag, inline_storage_tag>;
    static constexpr bool kUseFlat = std::is_same_v<storage_tag, flat_storage_tag>;
    static constexpr bool kUseIndirect = std::is_same_v<storage_tag, indirect_storage_tag>;

    static constexpr size_t kGroupWidth = detail::kGroupWidth;
    static constexpr float kMaxLoadFactor = 0.875f;
    static constexpr size_t kMinCapacity = kUseInline ? kGroupWidth : 8;

    // Helper to extract key from value_type (for map: pair.first, for set: value itself)
    static constexpr const Key& get_key(const value_type& v) {
        if constexpr (is_map) {
            return v.first;
        } else {
            return v;
        }
    }

    // ========== Inline storage (Swiss Table) ==========
    // Used when kUseInline == true
    value_type* slots_ = nullptr;      // Inline key-value pairs

    // ========== Flat storage (ankerl/Robin Hood style) ==========
    // Used when kUseFlat == true
    // Robin Hood linear probing with fingerprint in bucket
    detail::Bucket* buckets_ = nullptr;  // [value_idx | dist_and_h2]

    // ========== Indirect storage (Swiss Table + Contiguous Values) ==========
    // Used when kUseIndirect == true
    // Still uses ctrl_ for SIMD probing, but stores value indices instead of inline values
    uint32_t* value_indices_ = nullptr;  // Maps slot -> value index in values array

    // ========== Shared for Flat and Indirect: Contiguous values array ==========
    value_type* values_ = nullptr;       // Contiguous values array
    size_t values_capacity_ = 0;         // Capacity of values array

    // ========== Shared for Inline and Indirect: Control bytes for Swiss Table ==========
    int8_t* ctrl_ = nullptr;           // Control bytes (h2 fingerprint)

    // ========== Common ==========
    size_t size_ = 0;
    size_t capacity_ = 0;       // Bucket/slot capacity
    size_t growth_left_ = 0;
    uint8_t shift_ = 64;        // For flat storage: bucket_idx = hash >> shift_ (uses high bits)

    [[no_unique_address]] Hash hash_;
    [[no_unique_address]] KeyEqual key_equal_;
    [[no_unique_address]] Allocator alloc_;

public:
    // ========== Iterator for inline storage (Swiss Table) ==========
    class inline_iterator {
    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = dense_map::value_type;
        using difference_type = std::ptrdiff_t;
        using pointer = value_type*;
        using reference = value_type&;

        inline_iterator() = default;
        inline_iterator(const int8_t* ctrl, value_type* slot, const int8_t* ctrl_end)
            : ctrl_(ctrl), slot_(slot), ctrl_end_(ctrl_end) {}

        reference operator*() const { return *slot_; }
        pointer operator->() const { return slot_; }

        inline_iterator& operator++() {
            ++ctrl_; ++slot_;
            skip_empty_or_deleted();
            return *this;
        }
        inline_iterator operator++(int) { auto tmp = *this; ++*this; return tmp; }

        friend bool operator==(const inline_iterator& a, const inline_iterator& b) { return a.ctrl_ == b.ctrl_; }
        friend bool operator!=(const inline_iterator& a, const inline_iterator& b) { return a.ctrl_ != b.ctrl_; }

        void skip_empty_or_deleted() {
            while (ctrl_ != ctrl_end_ && detail::is_empty_or_deleted(*ctrl_)) { ++ctrl_; ++slot_; }
            if (ctrl_ == ctrl_end_) { ctrl_ = nullptr; slot_ = nullptr; ctrl_end_ = nullptr; }
        }

        const int8_t* ctrl_ = nullptr;
        value_type* slot_ = nullptr;
        const int8_t* ctrl_end_ = nullptr;
    };

    class const_inline_iterator {
    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = const dense_map::value_type;
        using difference_type = std::ptrdiff_t;
        using pointer = const value_type*;
        using reference = const value_type&;

        const_inline_iterator() = default;
        const_inline_iterator(const inline_iterator& it) : ctrl_(it.ctrl_), slot_(it.slot_), ctrl_end_(it.ctrl_end_) {}
        const_inline_iterator(const int8_t* ctrl, const value_type* slot, const int8_t* ctrl_end)
            : ctrl_(ctrl), slot_(slot), ctrl_end_(ctrl_end) {}

        reference operator*() const { return *slot_; }
        pointer operator->() const { return slot_; }

        const_inline_iterator& operator++() {
            ++ctrl_; ++slot_;
            skip_empty_or_deleted();
            return *this;
        }
        const_inline_iterator operator++(int) { auto tmp = *this; ++*this; return tmp; }

        friend bool operator==(const const_inline_iterator& a, const const_inline_iterator& b) { return a.ctrl_ == b.ctrl_; }
        friend bool operator!=(const const_inline_iterator& a, const const_inline_iterator& b) { return a.ctrl_ != b.ctrl_; }

        void skip_empty_or_deleted() {
            while (ctrl_ != ctrl_end_ && detail::is_empty_or_deleted(*ctrl_)) { ++ctrl_; ++slot_; }
            if (ctrl_ == ctrl_end_) { ctrl_ = nullptr; slot_ = nullptr; ctrl_end_ = nullptr; }
        }

        const int8_t* ctrl_ = nullptr;
        const value_type* slot_ = nullptr;
        const int8_t* ctrl_end_ = nullptr;
    };

    // Select iterator type based on storage policy
    using iterator = std::conditional_t<kUseInline, inline_iterator, value_type*>;
    using const_iterator = std::conditional_t<kUseInline, const_inline_iterator, const value_type*>;

    // Constructors
    dense_map() = default;

    // Allocator-only constructor (required for PMR support)
    explicit dense_map(const Allocator& alloc)
        : alloc_(alloc) {}

    explicit dense_map(size_type bucket_count, const Hash& hash = Hash(),
                       const KeyEqual& equal = KeyEqual(),
                       const Allocator& alloc = Allocator())
        : hash_(hash), key_equal_(equal), alloc_(alloc) {
        if (bucket_count > 0) {
            initialize(bucket_count);
        }
    }

    dense_map(const dense_map& other)
        : hash_(other.hash_),
          key_equal_(other.key_equal_),
          alloc_(AllocTraits::select_on_container_copy_construction(other.alloc_)) {
        if (other.size_ > 0) {
            initialize(other.capacity_);
            if constexpr (kUseInline) {
                for (size_t i = 0; i < other.capacity_; ++i) {
                    if (detail::is_full(other.ctrl_[i])) {
                        ctrl_[i] = other.ctrl_[i];
                        AllocTraits::construct(alloc_, slots_ + i, other.slots_[i]);
                    }
                }
            } else if constexpr (kUseFlat) {
                // Copy buckets
                std::memcpy(buckets_, other.buckets_, other.capacity_ * sizeof(detail::Bucket));
                // Ensure values_ has enough capacity (initialize() may have allocated less)
                if (values_capacity_ < other.size_) {
                    SlotAlloc slot_alloc(alloc_);
                    std::allocator_traits<SlotAlloc>::deallocate(slot_alloc, values_, values_capacity_);
                    values_capacity_ = other.values_capacity_;
                    values_ = std::allocator_traits<SlotAlloc>::allocate(slot_alloc, values_capacity_);
                }
                // Copy values
                for (size_t i = 0; i < other.size_; ++i) {
                    AllocTraits::construct(alloc_, values_ + i, other.values_[i]);
                }
            } else {
                // Copy ctrl and value_indices
                std::memcpy(ctrl_, other.ctrl_, other.capacity_ + kGroupWidth + 1);
                std::memcpy(value_indices_, other.value_indices_, other.capacity_ * sizeof(uint32_t));
                // Ensure values_ has enough capacity (initialize() may have allocated less)
                if (values_capacity_ < other.size_) {
                    SlotAlloc slot_alloc(alloc_);
                    std::allocator_traits<SlotAlloc>::deallocate(slot_alloc, values_, values_capacity_);
                    values_capacity_ = other.values_capacity_;
                    values_ = std::allocator_traits<SlotAlloc>::allocate(slot_alloc, values_capacity_);
                }
                // Copy values
                for (size_t i = 0; i < other.size_; ++i) {
                    AllocTraits::construct(alloc_, values_ + i, other.values_[i]);
                }
            }
            size_ = other.size_;
            growth_left_ = other.growth_left_;
        }
    }

    // Allocator-extended copy constructor (required for PMR support)
    dense_map(const dense_map& other, const Allocator& alloc)
        : hash_(other.hash_),
          key_equal_(other.key_equal_),
          alloc_(alloc) {
        if (other.size_ > 0) {
            initialize(other.capacity_);
            if constexpr (kUseInline) {
                for (size_t i = 0; i < other.capacity_; ++i) {
                    if (detail::is_full(other.ctrl_[i])) {
                        ctrl_[i] = other.ctrl_[i];
                        AllocTraits::construct(alloc_, slots_ + i, other.slots_[i]);
                    }
                }
            } else if constexpr (kUseFlat) {
                std::memcpy(buckets_, other.buckets_, other.capacity_ * sizeof(detail::Bucket));
                // Ensure values_ has enough capacity (initialize() may have allocated less)
                if (values_capacity_ < other.size_) {
                    SlotAlloc slot_alloc(alloc_);
                    std::allocator_traits<SlotAlloc>::deallocate(slot_alloc, values_, values_capacity_);
                    values_capacity_ = other.values_capacity_;
                    values_ = std::allocator_traits<SlotAlloc>::allocate(slot_alloc, values_capacity_);
                }
                for (size_t i = 0; i < other.size_; ++i) {
                    AllocTraits::construct(alloc_, values_ + i, other.values_[i]);
                }
            } else {
                std::memcpy(ctrl_, other.ctrl_, other.capacity_ + kGroupWidth + 1);
                std::memcpy(value_indices_, other.value_indices_, other.capacity_ * sizeof(uint32_t));
                // Ensure values_ has enough capacity (initialize() may have allocated less)
                if (values_capacity_ < other.size_) {
                    SlotAlloc slot_alloc(alloc_);
                    std::allocator_traits<SlotAlloc>::deallocate(slot_alloc, values_, values_capacity_);
                    values_capacity_ = other.values_capacity_;
                    values_ = std::allocator_traits<SlotAlloc>::allocate(slot_alloc, values_capacity_);
                }
                for (size_t i = 0; i < other.size_; ++i) {
                    AllocTraits::construct(alloc_, values_ + i, other.values_[i]);
                }
            }
            size_ = other.size_;
            growth_left_ = other.growth_left_;
        }
    }

    dense_map(dense_map&& other) noexcept
        : slots_(other.slots_),
          buckets_(other.buckets_),
          value_indices_(other.value_indices_),
          values_(other.values_),
          values_capacity_(other.values_capacity_),
          ctrl_(other.ctrl_),
          size_(other.size_),
          capacity_(other.capacity_),
          growth_left_(other.growth_left_),
          shift_(other.shift_),
          hash_(std::move(other.hash_)),
          key_equal_(std::move(other.key_equal_)),
          alloc_(std::move(other.alloc_)) {
        other.slots_ = nullptr;
        other.buckets_ = nullptr;
        other.value_indices_ = nullptr;
        other.values_ = nullptr;
        other.values_capacity_ = 0;
        other.ctrl_ = nullptr;
        other.size_ = 0;
        other.capacity_ = 0;
        other.growth_left_ = 0;
        other.shift_ = 64;
    }

    // Allocator-extended move constructor (required for PMR support)
    dense_map(dense_map&& other, const Allocator& alloc)
        : hash_(std::move(other.hash_)),
          key_equal_(std::move(other.key_equal_)),
          alloc_(alloc) {
        if constexpr (AllocTraits::is_always_equal::value) {
            // Allocators are always equal, can steal resources
            move_resources_from(other);
        } else {
            if (alloc_ == other.alloc_) {
                // Allocators are equal, can steal resources
                move_resources_from(other);
            } else {
                // Allocators differ, must move elements individually
                if (other.size_ > 0) {
                    initialize(other.capacity_);
                    move_elements_from(other);
                }
            }
        }
    }

    dense_map(std::initializer_list<value_type> init, size_type bucket_count = 0,
              const Hash& hash = Hash(), const KeyEqual& equal = KeyEqual(),
              const Allocator& alloc = Allocator())
        : hash_(hash), key_equal_(equal), alloc_(alloc) {
        size_t min_size = init.size() > 0 ? init.size() : 1;
        size_t cap = bucket_count > 0 ? bucket_count : min_size;
        reserve(cap);
        for (const auto& item : init) {
            insert(item);
        }
    }

    // Iterator range constructor
    template <typename InputIt>
    dense_map(InputIt first, InputIt last, size_type bucket_count = 0,
              const Hash& hash = Hash(), const KeyEqual& equal = KeyEqual(),
              const Allocator& alloc = Allocator())
        : hash_(hash), key_equal_(equal), alloc_(alloc) {
        if (bucket_count > 0) {
            reserve(bucket_count);
        }
        for (; first != last; ++first) {
            if constexpr (std::is_void_v<T>) {
                insert(*first);
            } else {
                insert(*first);
            }
        }
    }

    ~dense_map() { destroy(); }

    dense_map& operator=(const dense_map& other) {
        if (this != &other) {
            if constexpr (AllocTraits::propagate_on_container_copy_assignment::value) {
                // Allocator propagates: use allocator-extended copy constructor
                dense_map tmp(other, other.alloc_);
                swap(tmp);
                alloc_ = other.alloc_;
            } else {
                // Allocator does not propagate (PMR): keep our allocator, copy elements
                destroy();
                hash_ = other.hash_;
                key_equal_ = other.key_equal_;
                if (other.size_ > 0) {
                    initialize(other.capacity_);
                    if constexpr (kUseInline) {
                        for (size_t i = 0; i < other.capacity_; ++i) {
                            if (detail::is_full(other.ctrl_[i])) {
                                ctrl_[i] = other.ctrl_[i];
                                AllocTraits::construct(alloc_, slots_ + i, other.slots_[i]);
                            }
                        }
                    } else if constexpr (kUseFlat) {
                        std::memcpy(buckets_, other.buckets_, other.capacity_ * sizeof(detail::Bucket));
                        // Ensure values array is large enough
                        if (other.size_ > values_capacity_) {
                            SlotAlloc slot_alloc(alloc_);
                            std::allocator_traits<SlotAlloc>::deallocate(slot_alloc, values_, values_capacity_);
                            values_capacity_ = other.values_capacity_;
                            values_ = std::allocator_traits<SlotAlloc>::allocate(slot_alloc, values_capacity_);
                        }
                        for (size_t i = 0; i < other.size_; ++i) {
                            AllocTraits::construct(alloc_, values_ + i, other.values_[i]);
                        }
                    } else {
                        std::memcpy(ctrl_, other.ctrl_, other.capacity_ + kGroupWidth + 1);
                        std::memcpy(value_indices_, other.value_indices_, other.capacity_ * sizeof(uint32_t));
                        // Ensure values array is large enough
                        if (other.size_ > values_capacity_) {
                            SlotAlloc slot_alloc(alloc_);
                            std::allocator_traits<SlotAlloc>::deallocate(slot_alloc, values_, values_capacity_);
                            values_capacity_ = other.values_capacity_;
                            values_ = std::allocator_traits<SlotAlloc>::allocate(slot_alloc, values_capacity_);
                        }
                        for (size_t i = 0; i < other.size_; ++i) {
                            AllocTraits::construct(alloc_, values_ + i, other.values_[i]);
                        }
                    }
                    size_ = other.size_;
                    growth_left_ = other.growth_left_;
                }
            }
        }
        return *this;
    }

    dense_map& operator=(dense_map&& other) noexcept(
        AllocTraits::propagate_on_container_move_assignment::value ||
        AllocTraits::is_always_equal::value) {
        if (this != &other) {
            hash_ = std::move(other.hash_);
            key_equal_ = std::move(other.key_equal_);

            if constexpr (AllocTraits::propagate_on_container_move_assignment::value) {
                // Allocator propagates: destroy our resources and steal other's
                destroy();
                alloc_ = std::move(other.alloc_);
                move_resources_from(other);
            } else if constexpr (AllocTraits::is_always_equal::value) {
                // Allocators are always equal: can steal resources
                destroy();
                move_resources_from(other);
            } else {
                // Allocators may differ (PMR case)
                if (alloc_ == other.alloc_) {
                    // Allocators are equal: can steal resources
                    destroy();
                    move_resources_from(other);
                } else {
                    // Allocators differ: must move elements individually
                    destroy();
                    if (other.size_ > 0) {
                        initialize(other.capacity_);
                        move_elements_from(other);
                    }
                }
            }
        }
        return *this;
    }

    // Iterators
    iterator begin() {
        if (empty()) return end();
        if constexpr (kUseInline) {
            iterator it(ctrl_, slots_, ctrl_ + capacity_);
            if (detail::is_empty_or_deleted(*it.ctrl_)) {
                ++it;
            }
            return it;
        } else {
            return values_;
        }
    }

    const_iterator begin() const {
        if (empty()) return end();
        if constexpr (kUseInline) {
            const_iterator it(ctrl_, slots_, ctrl_ + capacity_);
            if (detail::is_empty_or_deleted(*it.ctrl_)) {
                ++it;
            }
            return it;
        } else {
            return values_;
        }
    }

    const_iterator cbegin() const { return begin(); }

    iterator end() {
        if constexpr (kUseInline) {
            return iterator(nullptr, nullptr, nullptr);
        } else {
            return values_ + size_;
        }
    }

    const_iterator end() const {
        if constexpr (kUseInline) {
            return const_iterator(nullptr, nullptr, nullptr);
        } else {
            return values_ + size_;
        }
    }

    const_iterator cend() const { return end(); }

    // Capacity
    [[nodiscard]] bool empty() const noexcept { return size_ == 0; }
    size_type size() const noexcept { return size_; }
    size_type max_size() const noexcept { return std::numeric_limits<size_type>::max() / 2; }
    size_type capacity() const noexcept { return capacity_; }

    // Modifiers
    void clear() noexcept {
        if (size_ == 0) return;
        if constexpr (kUseInline) {
            for (size_t i = 0; i < capacity_; ++i) {
                if (detail::is_full(ctrl_[i])) {
                    AllocTraits::destroy(alloc_, slots_ + i);
                    ctrl_[i] = detail::kEmpty;
                }
            }
        } else if constexpr (kUseFlat) {
            // Flat storage: destroy values and reset buckets
            for (size_t i = 0; i < size_; ++i) {
                AllocTraits::destroy(alloc_, values_ + i);
            }
            std::memset(buckets_, 0, capacity_ * sizeof(detail::Bucket));
        } else {
            // Indirect storage: destroy values and reset control bytes
            for (size_t i = 0; i < size_; ++i) {
                AllocTraits::destroy(alloc_, values_ + i);
            }
            std::memset(ctrl_, static_cast<int>(detail::kEmpty), capacity_ + kGroupWidth);
        }
        size_ = 0;
        reset_growth();
    }

    std::pair<iterator, bool> insert(const value_type& value) {
        return emplace_impl(get_key(value), value);
    }

    std::pair<iterator, bool> insert(value_type&& value) {
        return emplace_impl(get_key(value), std::move(value));
    }

    template <typename P, typename = std::enable_if_t<
                              std::is_constructible_v<value_type, P&&>>>
    std::pair<iterator, bool> insert(P&& value) {
        return emplace(std::forward<P>(value));
    }

    template <typename InputIt>
    void insert(InputIt first, InputIt last) {
        for (; first != last; ++first) {
            insert(*first);
        }
    }

    void insert(std::initializer_list<value_type> ilist) {
        insert(ilist.begin(), ilist.end());
    }

    template <typename Mapped, typename U = T, std::enable_if_t<!std::is_void_v<U>, int> = 0>
    std::pair<iterator, bool> insert_or_assign(const Key& key, Mapped&& obj) {
        auto result = try_emplace(key, std::forward<Mapped>(obj));
        if (!result.second) {
            result.first->second = std::forward<Mapped>(obj);
        }
        return result;
    }

    template <typename Mapped, typename U = T, std::enable_if_t<!std::is_void_v<U>, int> = 0>
    std::pair<iterator, bool> insert_or_assign(Key&& key, Mapped&& obj) {
        auto result = try_emplace(std::move(key), std::forward<Mapped>(obj));
        if (!result.second) {
            result.first->second = std::forward<Mapped>(obj);
        }
        return result;
    }

    template <typename... Args>
    std::pair<iterator, bool> emplace(Args&&... args) {
        // Construct the value to get the key
        alignas(value_type) unsigned char storage[sizeof(value_type)];
        value_type* value = reinterpret_cast<value_type*>(storage);
        AllocTraits::construct(alloc_, value, std::forward<Args>(args)...);

        auto result = emplace_impl(get_key(*value), std::move(*value));
        if (!result.second) {
            AllocTraits::destroy(alloc_, value);
        }
        return result;
    }

    // try_emplace is map-only (takes key separately from mapped value args)
    template <typename U = T, typename... Args, std::enable_if_t<!std::is_void_v<U>, int> = 0>
    std::pair<iterator, bool> try_emplace(const Key& key, Args&&... args) {
        return try_emplace_impl(key, std::forward<Args>(args)...);
    }

    template <typename U = T, typename... Args, std::enable_if_t<!std::is_void_v<U>, int> = 0>
    std::pair<iterator, bool> try_emplace(Key&& key, Args&&... args) {
        return try_emplace_impl(std::move(key), std::forward<Args>(args)...);
    }

    iterator erase(const_iterator pos) {
        if constexpr (kUseInline) {
            assert(pos.ctrl_ != nullptr);
            size_t index = static_cast<size_t>(pos.ctrl_ - ctrl_);
            erase_at(index);

            // Return iterator to next element
            iterator next(ctrl_ + index, slots_ + index, ctrl_ + capacity_);
            if (detail::is_empty_or_deleted(*next.ctrl_)) {
                ++next;
            }
            return next;
        } else {
            // pos is a pointer into values array
            size_t value_idx = static_cast<size_t>(pos - values_);
            erase_value_at(value_idx);
            // Return iterator to element now at this position (or end)
            return values_ + std::min(value_idx, size_);
        }
    }

    iterator erase(const_iterator first, const_iterator last) {
        if constexpr (kUseInline) {
            while (first != last) {
                first = erase(first);
            }
            return iterator(const_cast<int8_t*>(first.ctrl_),
                            const_cast<value_type*>(first.slot_),
                            const_cast<int8_t*>(first.ctrl_end_));
        } else {
            // For indirect storage, erase from end to start to avoid index shifting issues
            size_t count = static_cast<size_t>(last - first);
            size_t start_idx = static_cast<size_t>(first - values_);
            for (size_t i = 0; i < count; ++i) {
                erase_value_at(start_idx);
            }
            return values_ + std::min(start_idx, size_);
        }
    }

    size_type erase(const Key& key) {
        auto it = find(key);
        if (it == end()) return 0;
        erase(it);
        return 1;
    }

    void swap(dense_map& other) noexcept {
        using std::swap;
        swap(slots_, other.slots_);
        swap(buckets_, other.buckets_);
        swap(ctrl_, other.ctrl_);
        swap(value_indices_, other.value_indices_);
        swap(values_, other.values_);
        swap(values_capacity_, other.values_capacity_);
        swap(size_, other.size_);
        swap(capacity_, other.capacity_);
        swap(growth_left_, other.growth_left_);
        swap(shift_, other.shift_);
        swap(hash_, other.hash_);
        swap(key_equal_, other.key_equal_);
        if constexpr (AllocTraits::propagate_on_container_swap::value) {
            swap(alloc_, other.alloc_);
        }
    }

    // Lookup (map-only methods - use deduced U to defer void& check)
    template <typename U = T, std::enable_if_t<!std::is_void_v<U>, int> = 0>
    U& at(const Key& key) {
        auto it = find(key);
        if (it == end()) {
            throw std::out_of_range("dense_map::at: key not found");
        }
        return it->second;
    }

    template <typename U = T, std::enable_if_t<!std::is_void_v<U>, int> = 0>
    const U& at(const Key& key) const {
        auto it = find(key);
        if (it == end()) {
            throw std::out_of_range("dense_map::at: key not found");
        }
        return it->second;
    }

    template <typename U = T, std::enable_if_t<!std::is_void_v<U>, int> = 0>
    U& operator[](const Key& key) {
        auto result = try_emplace(key);
        return result.first->second;
    }

    template <typename U = T, std::enable_if_t<!std::is_void_v<U>, int> = 0>
    U& operator[](Key&& key) {
        auto result = try_emplace(std::move(key));
        return result.first->second;
    }

    size_type count(const Key& key) const { return contains(key) ? 1 : 0; }

    template <typename K>
    size_type count(const K& key) const
        requires requires { hash_(key); key_equal_(key, std::declval<Key>()); }
    {
        return contains(key) ? 1 : 0;
    }

    iterator find(const Key& key) {
        if (capacity_ == 0) return end();
        return find_impl(key);
    }

    const_iterator find(const Key& key) const {
        if (capacity_ == 0) return end();
        return const_cast<dense_map*>(this)->find_impl(key);
    }

    template <typename K>
    iterator find(const K& key)
        requires requires { hash_(key); key_equal_(key, std::declval<Key>()); }
    {
        if (capacity_ == 0) return end();
        return find_impl(key);
    }

    template <typename K>
    const_iterator find(const K& key) const
        requires requires { hash_(key); key_equal_(key, std::declval<Key>()); }
    {
        if (capacity_ == 0) return end();
        return const_cast<dense_map*>(this)->find_impl(key);
    }

    bool contains(const Key& key) const { return find(key) != end(); }

    template <typename K>
    bool contains(const K& key) const
        requires requires { hash_(key); key_equal_(key, std::declval<Key>()); }
    {
        return find(key) != end();
    }

    std::pair<iterator, iterator> equal_range(const Key& key) {
        auto it = find(key);
        if (it == end()) return {it, it};
        auto next = it;
        ++next;
        return {it, next};
    }

    std::pair<const_iterator, const_iterator> equal_range(const Key& key) const {
        auto it = find(key);
        if (it == end()) return {it, it};
        auto next = it;
        ++next;
        return {it, next};
    }

    // Bucket interface
    size_type bucket_count() const noexcept { return capacity_; }
    size_type max_bucket_count() const noexcept { return max_size(); }

    // Hash policy
    float load_factor() const noexcept {
        return capacity_ == 0 ? 0.0f : static_cast<float>(size_) / capacity_;
    }

    float max_load_factor() const noexcept { return kMaxLoadFactor; }

    void rehash(size_type count) {
        size_t min_size = static_cast<size_t>(
            std::ceil(static_cast<float>(size_) / kMaxLoadFactor));
        count = std::max({count, min_size, kMinCapacity});
        count = normalize_capacity(count);
        if (count != capacity_) {
            rehash_impl(count);
        }
    }

    void reserve(size_type count) {
        size_t required = static_cast<size_t>(
            std::ceil(static_cast<float>(count) / kMaxLoadFactor));
        if (required > capacity_) {
            rehash(required);
        }
    }

    // Observers
    hasher hash_function() const { return hash_; }
    key_equal key_eq() const { return key_equal_; }
    allocator_type get_allocator() const noexcept { return alloc_; }

private:
    // Round up to power of 2, minimum kGroupWidth
    static size_t normalize_capacity(size_t n) {
        if (n < kMinCapacity) return kMinCapacity;
        return size_t{1} << (64 - std::countl_zero(n - 1));
    }

    // Calculate how many elements we can insert before rehash
    size_t capacity_to_growth(size_t cap) const {
        return static_cast<size_t>(cap * kMaxLoadFactor);
    }

    void reset_growth() {
        growth_left_ = capacity_to_growth(capacity_) - size_;
    }

    void initialize(size_t cap) {
        cap = normalize_capacity(cap);
        capacity_ = cap;
        // For flat storage: shift_ = 64 - log2(capacity), so hash >> shift_ gives bucket index
        shift_ = static_cast<uint8_t>(64 - std::countr_zero(cap));

        if constexpr (kUseInline) {
            // Inline storage: allocate control bytes + sentinel group + slots
            CtrlAlloc ctrl_alloc(alloc_);
            ctrl_ = std::allocator_traits<CtrlAlloc>::allocate(
                ctrl_alloc, cap + kGroupWidth + 1);
            std::memset(ctrl_, static_cast<int>(detail::kEmpty), cap + kGroupWidth);
            ctrl_[cap + kGroupWidth] = detail::kSentinel;

            SlotAlloc slot_alloc(alloc_);
            slots_ = std::allocator_traits<SlotAlloc>::allocate(slot_alloc, cap);
        } else if constexpr (kUseFlat) {
            // Flat storage (ankerl/Robin Hood style):
            // Allocate buckets array (fingerprint + value_idx per bucket)
            BucketAlloc bucket_alloc(alloc_);
            buckets_ = std::allocator_traits<BucketAlloc>::allocate(bucket_alloc, cap);
            // Initialize all buckets as empty (Bucket is trivially copyable, set_empty() zeros both fields)
            std::memset(buckets_, 0, cap * sizeof(detail::Bucket));

            // Allocate values array - start smaller, let grow_values_array handle growth
            values_capacity_ = std::min(cap, size_t(8));
            SlotAlloc slot_alloc(alloc_);
            values_ = std::allocator_traits<SlotAlloc>::allocate(slot_alloc, values_capacity_);
        } else {
            // Indirect storage (Swiss Table + Contiguous Values):
            // Allocate control bytes + sentinel group
            CtrlAlloc ctrl_alloc(alloc_);
            ctrl_ = std::allocator_traits<CtrlAlloc>::allocate(
                ctrl_alloc, cap + kGroupWidth + 1);
            std::memset(ctrl_, static_cast<int>(detail::kEmpty), cap + kGroupWidth);
            ctrl_[cap + kGroupWidth] = detail::kSentinel;

            // Allocate value indices array (maps slot -> value index)
            IndexAlloc index_alloc(alloc_);
            value_indices_ = std::allocator_traits<IndexAlloc>::allocate(index_alloc, cap);
            // Initialize to avoid undefined behavior when ctrl_ has stale "full" markers
            std::memset(value_indices_, 0, cap * sizeof(uint32_t));

            // Allocate initial values array - start smaller
            values_capacity_ = std::min(cap, size_t(8));
            SlotAlloc slot_alloc(alloc_);
            values_ = std::allocator_traits<SlotAlloc>::allocate(slot_alloc, values_capacity_);
        }

        reset_growth();
    }

    // Helper: steal resources from other (used when allocators are equal)
    void move_resources_from(dense_map& other) noexcept {
        slots_ = other.slots_;
        buckets_ = other.buckets_;
        value_indices_ = other.value_indices_;
        values_ = other.values_;
        values_capacity_ = other.values_capacity_;
        ctrl_ = other.ctrl_;
        size_ = other.size_;
        capacity_ = other.capacity_;
        growth_left_ = other.growth_left_;
        shift_ = other.shift_;

        other.slots_ = nullptr;
        other.buckets_ = nullptr;
        other.value_indices_ = nullptr;
        other.values_ = nullptr;
        other.values_capacity_ = 0;
        other.ctrl_ = nullptr;
        other.size_ = 0;
        other.capacity_ = 0;
        other.growth_left_ = 0;
        other.shift_ = 64;
    }

    // Helper: move elements from other (used when allocators differ)
    void move_elements_from(dense_map& other) {
        if constexpr (kUseInline) {
            for (size_t i = 0; i < other.capacity_; ++i) {
                if (detail::is_full(other.ctrl_[i])) {
                    ctrl_[i] = other.ctrl_[i];
                    AllocTraits::construct(alloc_, slots_ + i, std::move(other.slots_[i]));
                }
            }
        } else if constexpr (kUseFlat) {
            std::memcpy(buckets_, other.buckets_, other.capacity_ * sizeof(detail::Bucket));
            // Ensure values array is large enough
            if (other.size_ > values_capacity_) {
                SlotAlloc slot_alloc(alloc_);
                std::allocator_traits<SlotAlloc>::deallocate(slot_alloc, values_, values_capacity_);
                values_capacity_ = other.values_capacity_;
                values_ = std::allocator_traits<SlotAlloc>::allocate(slot_alloc, values_capacity_);
            }
            for (size_t i = 0; i < other.size_; ++i) {
                AllocTraits::construct(alloc_, values_ + i, std::move(other.values_[i]));
            }
        } else {
            std::memcpy(ctrl_, other.ctrl_, other.capacity_ + kGroupWidth + 1);
            std::memcpy(value_indices_, other.value_indices_, other.capacity_ * sizeof(uint32_t));
            // Ensure values array is large enough
            if (other.size_ > values_capacity_) {
                SlotAlloc slot_alloc(alloc_);
                std::allocator_traits<SlotAlloc>::deallocate(slot_alloc, values_, values_capacity_);
                values_capacity_ = other.values_capacity_;
                values_ = std::allocator_traits<SlotAlloc>::allocate(slot_alloc, values_capacity_);
            }
            for (size_t i = 0; i < other.size_; ++i) {
                AllocTraits::construct(alloc_, values_ + i, std::move(other.values_[i]));
            }
        }
        size_ = other.size_;
        growth_left_ = other.growth_left_;
    }

    void destroy() {
        if constexpr (kUseInline) {
            if (ctrl_) {
                for (size_t i = 0; i < capacity_; ++i) {
                    if (detail::is_full(ctrl_[i])) {
                        AllocTraits::destroy(alloc_, slots_ + i);
                    }
                }
                CtrlAlloc ctrl_alloc(alloc_);
                std::allocator_traits<CtrlAlloc>::deallocate(
                    ctrl_alloc, ctrl_, capacity_ + kGroupWidth + 1);
                SlotAlloc slot_alloc(alloc_);
                std::allocator_traits<SlotAlloc>::deallocate(slot_alloc, slots_, capacity_);
            }
            ctrl_ = nullptr;
            slots_ = nullptr;
        } else if constexpr (kUseFlat) {
            if (buckets_) {
                // Destroy all values (stored contiguously)
                for (size_t i = 0; i < size_; ++i) {
                    AllocTraits::destroy(alloc_, values_ + i);
                }
                BucketAlloc bucket_alloc(alloc_);
                std::allocator_traits<BucketAlloc>::deallocate(bucket_alloc, buckets_, capacity_);
                SlotAlloc slot_alloc(alloc_);
                std::allocator_traits<SlotAlloc>::deallocate(slot_alloc, values_, values_capacity_);
            }
            buckets_ = nullptr;
            values_ = nullptr;
            values_capacity_ = 0;
        } else {
            if (ctrl_) {
                // Destroy all values
                for (size_t i = 0; i < size_; ++i) {
                    AllocTraits::destroy(alloc_, values_ + i);
                }
                CtrlAlloc ctrl_alloc(alloc_);
                std::allocator_traits<CtrlAlloc>::deallocate(
                    ctrl_alloc, ctrl_, capacity_ + kGroupWidth + 1);
                IndexAlloc index_alloc(alloc_);
                std::allocator_traits<IndexAlloc>::deallocate(index_alloc, value_indices_, capacity_);
                SlotAlloc slot_alloc(alloc_);
                std::allocator_traits<SlotAlloc>::deallocate(slot_alloc, values_, values_capacity_);
            }
            ctrl_ = nullptr;
            value_indices_ = nullptr;
            values_ = nullptr;
            values_capacity_ = 0;
        }
        size_ = 0;
        capacity_ = 0;
        growth_left_ = 0;
    }

    // Robin Hood: place bucket at position and shift up displaced entries
    DENSE_MAP_ALWAYS_INLINE void place_and_shift_up(size_t bucket_idx, uint32_t dist_and_fp, uint32_t value_idx) {
        const size_t mask = capacity_ - 1;
        detail::Bucket to_insert{dist_and_fp, value_idx};

        // Robin Hood invariant: only steal from entries with smaller distance
        while (buckets_[bucket_idx].dist_and_fingerprint != 0) {
            if (to_insert.dist_and_fingerprint > buckets_[bucket_idx].dist_and_fingerprint) {
                std::swap(to_insert, buckets_[bucket_idx]);
            }
            to_insert.dist_and_fingerprint = detail::Bucket::inc_dist(to_insert.dist_and_fingerprint);
            bucket_idx = (bucket_idx + 1) & mask;
        }
        buckets_[bucket_idx] = to_insert;
    }

    template <typename K>
    DENSE_MAP_ALWAYS_INLINE iterator find_impl(const K& key) {
        if constexpr (kUseInline) {
            // Swiss Table: SIMD probing
            const size_t hash = hash_(key);
            const int8_t h2_val = detail::h2(hash);
            const size_t mask = capacity_ - 1;

            detail::ProbeSeq seq(hash, mask);
            for (;;) {
                const detail::Group g(ctrl_ + seq.offset());
                for (auto match_mask = g.match(h2_val); match_mask; match_mask.remove_lowest_bit()) {
                    const size_t idx = seq.offset(match_mask.lowest_set_bit());
                    if (DENSE_MAP_LIKELY(key_equal_(key, get_key(slots_[idx])))) {
                        return iterator(ctrl_ + idx, slots_ + idx, ctrl_ + capacity_);
                    }
                }
                if (DENSE_MAP_LIKELY(g.match_empty())) {
                    return end();
                }
                seq.next();
            }
        } else if constexpr (kUseFlat) {
            // Flat storage: Robin Hood linear probing (ankerl-style optimized)
            // Use HIGH bits for bucket index, LOW 8 bits for fingerprint (independent bits!)
            const size_t hash = hash_(key);
            const size_t mask = capacity_ - 1;
            size_t bucket_idx = hash >> shift_;  // HIGH bits for bucket index
            auto dist_and_fp = detail::Bucket::make_dist_and_fingerprint(hash);
            const auto* bucket = buckets_ + bucket_idx;

            // Unrolled first two iterations (ankerl-style) - most keys found in first 2 probes
            if (dist_and_fp == bucket->dist_and_fingerprint &&
                DENSE_MAP_LIKELY(key_equal_(key, get_key(values_[bucket->value_idx])))) {
                return values_ + bucket->value_idx;
            }
            dist_and_fp = detail::Bucket::inc_dist(dist_and_fp);
            bucket_idx = (bucket_idx + 1) & mask;
            bucket = buckets_ + bucket_idx;

            if (dist_and_fp == bucket->dist_and_fingerprint &&
                DENSE_MAP_LIKELY(key_equal_(key, get_key(values_[bucket->value_idx])))) {
                return values_ + bucket->value_idx;
            }
            dist_and_fp = detail::Bucket::inc_dist(dist_and_fp);
            bucket_idx = (bucket_idx + 1) & mask;
            bucket = buckets_ + bucket_idx;

            // Main loop with early-out check
            while (dist_and_fp <= bucket->dist_and_fingerprint) {
                if (dist_and_fp == bucket->dist_and_fingerprint &&
                    DENSE_MAP_LIKELY(key_equal_(key, get_key(values_[bucket->value_idx])))) {
                    return values_ + bucket->value_idx;
                }
                dist_and_fp = detail::Bucket::inc_dist(dist_and_fp);
                bucket_idx = (bucket_idx + 1) & mask;
                bucket = buckets_ + bucket_idx;
            }
            return end();
        } else {
            // Swiss Table + Indirect: SIMD probing with value_indices lookup
            const size_t hash = hash_(key);
            const int8_t h2_val = detail::h2(hash);
            const size_t mask = capacity_ - 1;

            detail::ProbeSeq seq(hash, mask);
            for (;;) {
                const detail::Group g(ctrl_ + seq.offset());
                for (auto match_mask = g.match(h2_val); match_mask; match_mask.remove_lowest_bit()) {
                    const size_t idx = seq.offset(match_mask.lowest_set_bit());
                    const uint32_t value_idx = value_indices_[idx];
                    if (DENSE_MAP_LIKELY(key_equal_(key, get_key(values_[value_idx])))) {
                        return values_ + value_idx;
                    }
                }
                if (DENSE_MAP_LIKELY(g.match_empty())) {
                    return end();
                }
                seq.next();
            }
        }
    }

    template <typename K>
    DENSE_MAP_ALWAYS_INLINE size_t find_slot_for_insert(const K& key, size_t hash) {
        const int8_t h2_val = detail::h2(hash);
        const size_t mask = capacity_ - 1;
        const size_t pos = hash & mask;

        // Fast path: if first position is empty, use it directly (common case)
        if (DENSE_MAP_LIKELY(detail::is_empty(ctrl_[pos]))) {
            return pos;
        }

        // Check if first position has matching key
        if (ctrl_[pos] == h2_val && key_equal_(key, get_key(slots_[pos]))) {
            return pos;
        }

        // Fall back to SIMD probing
        size_t first_available = detail::is_deleted(ctrl_[pos]) ? pos : ~size_t{0};
        detail::ProbeSeq seq(hash, mask);

        for (;;) {
            const detail::Group g(ctrl_ + seq.offset());

            // Check for matching keys
            for (auto match_mask = g.match(h2_val); match_mask; match_mask.remove_lowest_bit()) {
                const size_t idx = seq.offset(match_mask.lowest_set_bit());
                if (key_equal_(key, get_key(slots_[idx]))) {
                    return idx;
                }
            }

            // Track first available slot
            if (first_available == ~size_t{0}) {
                if (auto empty_mask = g.match_empty_or_deleted()) {
                    first_available = seq.offset(empty_mask.lowest_set_bit());
                }
            }

            // If we found an empty slot, we know key doesn't exist
            if (g.match_empty()) {
                return first_available;
            }
            seq.next();
        }
    }

    template <typename K, typename V>
    std::pair<iterator, bool> emplace_impl(const K& key, V&& value) {
        if (DENSE_MAP_UNLIKELY(capacity_ == 0)) {
            initialize(kMinCapacity);
        } else if (DENSE_MAP_UNLIKELY(growth_left_ == 0)) {
            rehash_impl(capacity_ * 2);
        }

        if constexpr (kUseInline) {
            // Swiss Table: inline storage
            size_t hash = hash_(key);
            size_t idx = find_slot_for_insert(key, hash);

            if (detail::is_full(ctrl_[idx])) {
                return {iterator(ctrl_ + idx, slots_ + idx, ctrl_ + capacity_), false};
            }

            const bool was_deleted = detail::is_deleted(ctrl_[idx]);
            const int8_t h2_val = detail::h2(hash);
            ctrl_[idx] = h2_val;
            // Mirror first kGroupWidth control bytes for SIMD wrap-around
            if (idx < kGroupWidth) {
                ctrl_[capacity_ + idx] = h2_val;
            }

            AllocTraits::construct(alloc_, slots_ + idx, std::forward<V>(value));

            ++size_;
            growth_left_ -= !was_deleted;

            return {iterator(ctrl_ + idx, slots_ + idx, ctrl_ + capacity_), true};
        } else if constexpr (kUseFlat) {
            // Flat storage: Robin Hood linear probing (ankerl-style optimized)
            // Use HIGH bits for bucket index, LOW 8 bits for fingerprint
            const size_t hash = hash_(key);
            const size_t mask = capacity_ - 1;
            size_t bucket_idx = hash >> shift_;  // HIGH bits for bucket index
            auto dist_and_fp = detail::Bucket::make_dist_and_fingerprint(hash);
            auto* bucket = buckets_ + bucket_idx;

            // Find existing key or insertion point
            while (dist_and_fp <= bucket->dist_and_fingerprint) {
                if (dist_and_fp == bucket->dist_and_fingerprint &&
                    DENSE_MAP_LIKELY(key_equal_(key, get_key(values_[bucket->value_idx])))) {
                    return {values_ + bucket->value_idx, false};  // Key exists
                }
                dist_and_fp = detail::Bucket::inc_dist(dist_and_fp);
                bucket_idx = (bucket_idx + 1) & mask;
                bucket = buckets_ + bucket_idx;
            }

            // Ensure values array has capacity
            if (DENSE_MAP_UNLIKELY(size_ >= values_capacity_)) {
                grow_values_array();
            }

            // Insert the value at end of values array
            const uint32_t new_value_idx = static_cast<uint32_t>(size_);
            value_type* result_ptr = values_ + new_value_idx;
            AllocTraits::construct(alloc_, result_ptr, std::forward<V>(value));
            ++size_;
            --growth_left_;

            // Robin Hood insertion (ankerl-style): shift up until empty slot
            place_and_shift_up(bucket_idx, dist_and_fp, new_value_idx);

            return {result_ptr, true};
        } else {
            // Indirect storage (Swiss Table + values array)
            const size_t hash = hash_(key);
            const int8_t h2_val = detail::h2(hash);
            const size_t mask = capacity_ - 1;

            // Find existing key or insertion slot
            size_t insert_slot = ~size_t{0};
            detail::ProbeSeq seq(hash, mask);
            for (;;) {
                const detail::Group g(ctrl_ + seq.offset());

                // Check for matching keys
                for (auto match_mask = g.match(h2_val); match_mask; match_mask.remove_lowest_bit()) {
                    const size_t idx = seq.offset(match_mask.lowest_set_bit());
                    const uint32_t vidx = value_indices_[idx];
                    if (key_equal_(key, get_key(values_[vidx]))) {
                        return {values_ + vidx, false};  // Key exists
                    }
                }

                // Track first available slot
                if (insert_slot == ~size_t{0}) {
                    if (auto empty_mask = g.match_empty_or_deleted()) {
                        insert_slot = seq.offset(empty_mask.lowest_set_bit());
                    }
                }

                // If we found an empty slot, key doesn't exist
                if (g.match_empty()) {
                    break;
                }
                seq.next();
            }

            // Ensure values array has capacity
            if (DENSE_MAP_UNLIKELY(size_ >= values_capacity_)) {
                grow_values_array();
            }

            // Insert new value
            const bool was_deleted = detail::is_deleted(ctrl_[insert_slot]);
            ctrl_[insert_slot] = h2_val;
            if (insert_slot < kGroupWidth) {
                ctrl_[capacity_ + insert_slot] = h2_val;
            }

            value_indices_[insert_slot] = static_cast<uint32_t>(size_);
            AllocTraits::construct(alloc_, values_ + size_, std::forward<V>(value));

            ++size_;
            growth_left_ -= !was_deleted;

            return {values_ + value_indices_[insert_slot], true};
        }
    }

    template <typename K, typename... Args>
    std::pair<iterator, bool> try_emplace_impl(K&& key, Args&&... args) {
        if (DENSE_MAP_UNLIKELY(capacity_ == 0)) {
            initialize(kMinCapacity);
        } else if (DENSE_MAP_UNLIKELY(growth_left_ == 0)) {
            rehash_impl(capacity_ * 2);
        }

        if constexpr (kUseInline) {
            // Swiss Table: inline storage
            size_t hash = hash_(key);
            size_t idx = find_slot_for_insert(key, hash);

            if (detail::is_full(ctrl_[idx])) {
                return {iterator(ctrl_ + idx, slots_ + idx, ctrl_ + capacity_), false};
            }

            const bool was_deleted = detail::is_deleted(ctrl_[idx]);
            const int8_t h2_val = detail::h2(hash);
            ctrl_[idx] = h2_val;
            if (idx < kGroupWidth) {
                ctrl_[capacity_ + idx] = h2_val;
            }

            AllocTraits::construct(alloc_, slots_ + idx,
                                   std::piecewise_construct,
                                   std::forward_as_tuple(std::forward<K>(key)),
                                   std::forward_as_tuple(std::forward<Args>(args)...));

            ++size_;
            growth_left_ -= !was_deleted;

            return {iterator(ctrl_ + idx, slots_ + idx, ctrl_ + capacity_), true};
        } else if constexpr (kUseFlat) {
            // Flat storage: Robin Hood linear probing (ankerl-style optimized)
            // Use HIGH bits for bucket index, LOW 8 bits for fingerprint
            const size_t hash = hash_(key);
            const size_t mask = capacity_ - 1;
            size_t bucket_idx = hash >> shift_;  // HIGH bits for bucket index
            auto dist_and_fp = detail::Bucket::make_dist_and_fingerprint(hash);
            auto* bucket = buckets_ + bucket_idx;

            // Find existing key or insertion point
            while (dist_and_fp <= bucket->dist_and_fingerprint) {
                if (dist_and_fp == bucket->dist_and_fingerprint &&
                    DENSE_MAP_LIKELY(key_equal_(key, get_key(values_[bucket->value_idx])))) {
                    return {values_ + bucket->value_idx, false};  // Key exists
                }
                dist_and_fp = detail::Bucket::inc_dist(dist_and_fp);
                bucket_idx = (bucket_idx + 1) & mask;
                bucket = buckets_ + bucket_idx;
            }

            // Ensure values array has capacity
            if (DENSE_MAP_UNLIKELY(size_ >= values_capacity_)) {
                grow_values_array();
            }

            // Construct new value at end of values array
            const uint32_t new_value_idx = static_cast<uint32_t>(size_);
            AllocTraits::construct(alloc_, values_ + new_value_idx,
                                   std::piecewise_construct,
                                   std::forward_as_tuple(std::forward<K>(key)),
                                   std::forward_as_tuple(std::forward<Args>(args)...));
            ++size_;
            --growth_left_;

            // Robin Hood insertion (ankerl-style): shift up until empty slot
            place_and_shift_up(bucket_idx, dist_and_fp, new_value_idx);

            return {values_ + new_value_idx, true};
        } else {
            // Swiss Table + Indirect: SIMD probing with values array
            const size_t hash = hash_(key);
            const int8_t h2_val = detail::h2(hash);
            const size_t mask = capacity_ - 1;

            // Find existing key or insertion slot
            size_t insert_slot = ~size_t{0};
            detail::ProbeSeq seq(hash, mask);
            for (;;) {
                const detail::Group g(ctrl_ + seq.offset());

                // Check for matching keys
                for (auto match_mask = g.match(h2_val); match_mask; match_mask.remove_lowest_bit()) {
                    const size_t idx = seq.offset(match_mask.lowest_set_bit());
                    const uint32_t vidx = value_indices_[idx];
                    if (key_equal_(key, get_key(values_[vidx]))) {
                        return {values_ + vidx, false};  // Key exists
                    }
                }

                // Track first available slot
                if (insert_slot == ~size_t{0}) {
                    if (auto empty_mask = g.match_empty_or_deleted()) {
                        insert_slot = seq.offset(empty_mask.lowest_set_bit());
                    }
                }

                // If we found an empty slot, key doesn't exist
                if (g.match_empty()) {
                    break;
                }
                seq.next();
            }

            // Ensure values array has capacity
            if (size_ >= values_capacity_) {
                grow_values_array();
            }

            // Construct new value at end of values array
            const uint32_t new_value_idx = static_cast<uint32_t>(size_);
            AllocTraits::construct(alloc_, values_ + new_value_idx,
                                   std::piecewise_construct,
                                   std::forward_as_tuple(std::forward<K>(key)),
                                   std::forward_as_tuple(std::forward<Args>(args)...));

            // Insert into slot
            const bool was_deleted = detail::is_deleted(ctrl_[insert_slot]);
            ctrl_[insert_slot] = h2_val;
            if (insert_slot < kGroupWidth) {
                ctrl_[capacity_ + insert_slot] = h2_val;
            }
            value_indices_[insert_slot] = new_value_idx;

            ++size_;
            growth_left_ -= !was_deleted;

            return {values_ + new_value_idx, true};
        }
    }

    // Grow values array for indirect storage
    void grow_values_array() {
        size_t new_capacity = values_capacity_ == 0 ? 8 : values_capacity_ * 2;
        SlotAlloc slot_alloc(alloc_);
        value_type* new_values = std::allocator_traits<SlotAlloc>::allocate(slot_alloc, new_capacity);

        // Move existing values - use memcpy for trivially copyable types
        if constexpr (std::is_trivially_copyable_v<value_type>) {
            std::memcpy(new_values, values_, size_ * sizeof(value_type));
        } else {
            for (size_t i = 0; i < size_; ++i) {
                AllocTraits::construct(alloc_, new_values + i, std::move(values_[i]));
                AllocTraits::destroy(alloc_, values_ + i);
            }
        }

        if (values_) {
            std::allocator_traits<SlotAlloc>::deallocate(slot_alloc, values_, values_capacity_);
        }

        values_ = new_values;
        values_capacity_ = new_capacity;
    }

    // Erase at slot index (inline storage only)
    void erase_at(size_t idx) {
        static_assert(kUseInline, "erase_at only for inline storage");
        assert(detail::is_full(ctrl_[idx]));
        AllocTraits::destroy(alloc_, slots_ + idx);

        size_t mask = capacity_ - 1;
        size_t next_idx = (idx + 1) & mask;
        bool should_delete = detail::is_full(ctrl_[next_idx]) ||
                             detail::is_deleted(ctrl_[next_idx]);

        ctrl_[idx] = should_delete ? detail::kDeleted : detail::kEmpty;
        if (idx < kGroupWidth) {
            ctrl_[capacity_ + idx] = ctrl_[idx];
        }

        --size_;
        if (!should_delete) {
            ++growth_left_;
        }
    }

    // Erase value at index in values array (indirect storage only)
    void erase_value_at(size_t value_idx) {
        static_assert(!kUseInline, "erase_value_at only for indirect/flat storage");
        const size_t mask = capacity_ - 1;

        if constexpr (kUseFlat) {
            // Flat storage: Robin Hood backward shift deletion (ankerl-style)
            // Find the bucket that points to this value
            const size_t hash = hash_(get_key(values_[value_idx]));
            size_t bucket_idx = hash >> shift_;  // HIGH bits for bucket index

            // Find the bucket containing this value
            while (buckets_[bucket_idx].value_idx != static_cast<uint32_t>(value_idx)) {
                bucket_idx = (bucket_idx + 1) & mask;
            }

            // Backward shift deletion: shift subsequent entries back
            size_t next_idx = (bucket_idx + 1) & mask;
            // Prefetch ahead for better cache utilization
            DENSE_MAP_PREFETCH(&buckets_[(next_idx + 1) & mask]);
            while (buckets_[next_idx].dist_and_fingerprint >= detail::Bucket::kDistInc * 2) {
                // Prefetch next bucket
                DENSE_MAP_PREFETCH(&buckets_[(next_idx + 2) & mask]);
                // Move next bucket back and decrement its distance
                buckets_[bucket_idx].value_idx = buckets_[next_idx].value_idx;
                buckets_[bucket_idx].dist_and_fingerprint =
                    buckets_[next_idx].dist_and_fingerprint - detail::Bucket::kDistInc;
                bucket_idx = next_idx;
                next_idx = (next_idx + 1) & mask;
            }
            buckets_[bucket_idx].set_empty();

            // Swap-and-pop: move last value to deleted position
            const size_t last_idx = size_ - 1;
            if (value_idx != last_idx) {
                // Find and update bucket pointing to last value
                const size_t last_hash = hash_(get_key(values_[last_idx]));
                size_t last_pos = last_hash >> shift_;  // HIGH bits for bucket index

                // Prefetch the expected bucket location
                DENSE_MAP_PREFETCH(&buckets_[last_pos]);

                while (buckets_[last_pos].value_idx != static_cast<uint32_t>(last_idx)) {
                    last_pos = (last_pos + 1) & mask;
                }
                buckets_[last_pos].value_idx = static_cast<uint32_t>(value_idx);

                // Move last value to deleted position - use memcpy for trivial types
                if constexpr (std::is_trivially_copyable_v<value_type>) {
                    std::memcpy(values_ + value_idx, values_ + last_idx, sizeof(value_type));
                } else {
                    values_[value_idx] = std::move(values_[last_idx]);
                }
            }
            AllocTraits::destroy(alloc_, values_ + last_idx);

            --size_;
            ++growth_left_;
        } else {
            // Indirect storage (Swiss Table + Indirect)
            // Find the slot that points to this value
            const size_t hash = hash_(get_key(values_[value_idx]));
            const int8_t h2_val = detail::h2(hash);

            size_t slot_idx = ~size_t{0};
            detail::ProbeSeq seq(hash, mask);
            for (;;) {
                const detail::Group g(ctrl_ + seq.offset());
                for (auto match_mask = g.match(h2_val); match_mask; match_mask.remove_lowest_bit()) {
                    const size_t idx = seq.offset(match_mask.lowest_set_bit());
                    if (value_indices_[idx] == static_cast<uint32_t>(value_idx)) {
                        slot_idx = idx;
                        break;
                    }
                }
                if (slot_idx != ~size_t{0}) break;
                // Safety check: if we hit an empty slot, the element doesn't exist
                // This should never happen in a valid map state
                if (DENSE_MAP_UNLIKELY(g.match_empty())) {
                    assert(false && "dense_map::erase_value_at: slot not found for value_idx");
                    std::abort();
                }
                seq.next();
            }

            // IMPORTANT: Do swap-and-pop BEFORE marking slot as deleted/empty.
            // If we mark slot_idx as kEmpty first, it can break probe sequences
            // for other keys (like last_idx) whose probe path passes through slot_idx.
            const size_t last_idx = size_ - 1;
            if (value_idx != last_idx) {
                // Find and update slot pointing to last value
                const size_t last_hash = hash_(get_key(values_[last_idx]));
                const int8_t last_h2 = detail::h2(last_hash);

                detail::ProbeSeq last_seq(last_hash, mask);
                size_t probe_count = 0;
                for (;;) {
                    const detail::Group g(ctrl_ + last_seq.offset());
                    for (auto match_mask = g.match(last_h2); match_mask; match_mask.remove_lowest_bit()) {
                        const size_t idx = last_seq.offset(match_mask.lowest_set_bit());
                        if (value_indices_[idx] == static_cast<uint32_t>(last_idx)) {
                            value_indices_[idx] = static_cast<uint32_t>(value_idx);
                            goto found_last;
                        }
                    }
                    // Safety check: if we hit an empty slot, the element doesn't exist
                    // This should never happen in a valid map state
                    if (DENSE_MAP_UNLIKELY(g.match_empty())) {
                        // Debug output before abort
                        fprintf(stderr, "DEBUG: erase_value_at failed for last_idx\n");
                        fprintf(stderr, "  value_idx=%zu, last_idx=%zu, size_=%zu, capacity_=%zu\n",
                                value_idx, last_idx, size_, capacity_);
                        fprintf(stderr, "  last_hash=0x%lx, last_h2=%d, mask=0x%lx\n",
                                static_cast<unsigned long>(last_hash),
                                static_cast<int>(last_h2),
                                static_cast<unsigned long>(mask));
                        fprintf(stderr, "  probe_count=%zu, offset=%zu\n", probe_count, last_seq.offset());
                        fprintf(stderr, "  ctrl bytes at offset: ");
                        for (size_t i = 0; i < kGroupWidth && last_seq.offset() + i < capacity_; ++i) {
                            fprintf(stderr, "%02x ", static_cast<unsigned char>(ctrl_[last_seq.offset() + i]));
                        }
                        fprintf(stderr, "\n");
                        fprintf(stderr, "  Searching for slot where value_indices_[slot]==%zu\n", last_idx);
                        // Dump all slots with matching h2
                        fprintf(stderr, "  All slots with h2=%d: ", static_cast<int>(last_h2));
                        for (size_t i = 0; i < capacity_; ++i) {
                            if (ctrl_[i] == last_h2) {
                                fprintf(stderr, "[slot=%zu, vidx=%u] ", i, value_indices_[i]);
                            }
                        }
                        fprintf(stderr, "\n");
                        fflush(stderr);
                        assert(false && "dense_map::erase_value_at: slot not found for last_idx");
                        std::abort();
                    }
                    last_seq.next();
                    ++probe_count;
                }
                found_last:
                // Move last value to deleted position - use memcpy for trivial types
                if constexpr (std::is_trivially_copyable_v<value_type>) {
                    std::memcpy(values_ + value_idx, values_ + last_idx, sizeof(value_type));
                } else {
                    values_[value_idx] = std::move(values_[last_idx]);
                }
            }
            AllocTraits::destroy(alloc_, values_ + last_idx);

            // Now mark slot as deleted AFTER swap-and-pop is complete
            // IMPORTANT: Always use kDeleted, never kEmpty, in indirect storage mode.
            // Using kEmpty can break probe chains for other elements whose probe path
            // passes through this slot. The should_delete heuristic (checking next slot)
            // only works for single-probe-chain scenarios but not for open addressing
            // where multiple keys can have overlapping probe sequences.
            ctrl_[slot_idx] = detail::kDeleted;
            if (slot_idx < kGroupWidth) {
                ctrl_[capacity_ + slot_idx] = ctrl_[slot_idx];
            }

            --size_;
            ++growth_left_;
        }
    }

    void rehash_impl(size_t new_capacity) {
        new_capacity = normalize_capacity(new_capacity);

        if constexpr (kUseInline) {
            // Swiss Table: inline storage
            int8_t* old_ctrl = ctrl_;
            value_type* old_slots = slots_;
            size_t old_capacity = capacity_;

            ctrl_ = nullptr;
            slots_ = nullptr;
            capacity_ = 0;
            size_ = 0;
            growth_left_ = 0;

            initialize(new_capacity);

            for (size_t i = 0; i < old_capacity; ++i) {
                if (detail::is_full(old_ctrl[i])) {
                    size_t hash = hash_(get_key(old_slots[i]));
                    size_t idx = find_first_empty(hash);

                    ctrl_[idx] = detail::h2(hash);
                    if (idx < kGroupWidth) {
                        ctrl_[capacity_ + idx] = ctrl_[idx];
                    }

                    if constexpr (std::is_nothrow_move_constructible_v<value_type>) {
                        AllocTraits::construct(alloc_, slots_ + idx, std::move(old_slots[i]));
                    } else {
                        AllocTraits::construct(alloc_, slots_ + idx, old_slots[i]);
                    }
                    AllocTraits::destroy(alloc_, old_slots + i);
                    ++size_;
                    --growth_left_;
                }
            }

            if (old_ctrl) {
                CtrlAlloc ctrl_alloc(alloc_);
                std::allocator_traits<CtrlAlloc>::deallocate(
                    ctrl_alloc, old_ctrl, old_capacity + kGroupWidth + 1);
                SlotAlloc slot_alloc(alloc_);
                std::allocator_traits<SlotAlloc>::deallocate(slot_alloc, old_slots, old_capacity);
            }
        } else if constexpr (kUseFlat) {
            // Flat storage: rehash buckets, keep values array
            detail::Bucket* old_buckets = buckets_;
            size_t old_capacity = capacity_;
            size_t old_size = size_;

            // Allocate new buckets
            BucketAlloc bucket_alloc(alloc_);
            buckets_ = std::allocator_traits<BucketAlloc>::allocate(bucket_alloc, new_capacity);
            std::memset(buckets_, 0, new_capacity * sizeof(detail::Bucket));

            // Allocate values array if not already allocated
            if (!values_) {
                values_capacity_ = new_capacity;
                SlotAlloc slot_alloc(alloc_);
                values_ = std::allocator_traits<SlotAlloc>::allocate(slot_alloc, values_capacity_);
            }

            capacity_ = new_capacity;
            shift_ = static_cast<uint8_t>(64 - std::countr_zero(new_capacity));  // Update shift for new capacity
            size_ = 0;
            growth_left_ = capacity_to_growth(capacity_);

            // Reinsert all values (values stay in place!) using ankerl-style
            for (size_t i = 0; i < old_size; ++i) {
                const size_t hash = hash_(get_key(values_[i]));
                auto dist_and_fp = detail::Bucket::make_dist_and_fingerprint(hash);
                size_t bucket_idx = hash >> shift_;  // HIGH bits for bucket index

                // Robin Hood insertion: place and shift up
                place_and_shift_up(bucket_idx, dist_and_fp, static_cast<uint32_t>(i));
                ++size_;
                --growth_left_;
            }

            // Deallocate old buckets
            if (old_buckets) {
                std::allocator_traits<BucketAlloc>::deallocate(
                    bucket_alloc, old_buckets, old_capacity);
            }
        } else {
            // Swiss Table + Indirect: rehash ctrl + indices, keep values array
            int8_t* old_ctrl = ctrl_;
            uint32_t* old_indices = value_indices_;
            size_t old_capacity = capacity_;
            size_t old_size = size_;

            // Allocate new ctrl + indices
            CtrlAlloc ctrl_alloc(alloc_);
            ctrl_ = std::allocator_traits<CtrlAlloc>::allocate(
                ctrl_alloc, new_capacity + kGroupWidth + 1);
            std::memset(ctrl_, static_cast<int>(detail::kEmpty), new_capacity + kGroupWidth);
            ctrl_[new_capacity + kGroupWidth] = detail::kSentinel;

            IndexAlloc index_alloc(alloc_);
            value_indices_ = std::allocator_traits<IndexAlloc>::allocate(index_alloc, new_capacity);
            // Initialize to avoid undefined behavior when ctrl_ has stale "full" markers
            std::memset(value_indices_, 0, new_capacity * sizeof(uint32_t));

            capacity_ = new_capacity;
            size_ = 0;
            growth_left_ = capacity_to_growth(capacity_);

            // Reinsert all slots (values stay in place!)
            for (size_t i = 0; i < old_size; ++i) {
                const size_t hash = hash_(get_key(values_[i]));
                const int8_t h2_val = detail::h2(hash);
                size_t idx = find_first_empty(hash);

                ctrl_[idx] = h2_val;
                if (idx < kGroupWidth) {
                    ctrl_[capacity_ + idx] = h2_val;
                }
                value_indices_[idx] = static_cast<uint32_t>(i);
                ++size_;
                --growth_left_;
            }

            // Deallocate old ctrl + indices
            if (old_ctrl) {
                std::allocator_traits<CtrlAlloc>::deallocate(
                    ctrl_alloc, old_ctrl, old_capacity + kGroupWidth + 1);
                std::allocator_traits<IndexAlloc>::deallocate(
                    index_alloc, old_indices, old_capacity);
            }
        }
    }

    size_t find_first_empty(size_t hash) {
        size_t mask = capacity_ - 1;
        for (detail::ProbeSeq seq(detail::h1(hash), mask);; seq.next()) {
            detail::Group g(ctrl_ + seq.offset());
            if (auto mask_bits = g.match_empty_or_deleted()) {
                return seq.offset(mask_bits.lowest_set_bit());
            }
        }
    }
};

// Non-member functions
template <typename K, typename V, typename H, typename E, typename A, typename M>
bool operator==(const dense_map<K, V, H, E, A, M>& lhs,
                const dense_map<K, V, H, E, A, M>& rhs) {
    if (lhs.size() != rhs.size()) return false;
    if constexpr (detail::is_map_v<V>) {
        // Map mode: check key-value pairs
        for (const auto& [key, value] : lhs) {
            auto it = rhs.find(key);
            if (it == rhs.end() || it->second != value) return false;
        }
    } else {
        // Set mode: just check containment
        for (const auto& key : lhs) {
            if (!rhs.contains(key)) return false;
        }
    }
    return true;
}

template <typename K, typename V, typename H, typename E, typename A, typename M>
bool operator!=(const dense_map<K, V, H, E, A, M>& lhs,
                const dense_map<K, V, H, E, A, M>& rhs) {
    return !(lhs == rhs);
}

template <typename K, typename V, typename H, typename E, typename A, typename M>
void swap(dense_map<K, V, H, E, A, M>& lhs,
          dense_map<K, V, H, E, A, M>& rhs) noexcept {
    lhs.swap(rhs);
}

// Type alias for convenience
template <typename Key, typename Value>
using fast_map = dense_map<Key, Value>;

// ============================================================================
// PMR (Polymorphic Memory Resource) type aliases
// ============================================================================
namespace pmr {

template <typename Key, typename T = void, typename Hash = dense_hash<Key>,
          typename KeyEqual = std::equal_to<Key>,
          typename MemoryPolicy = default_memory_policy<Key, T>>
using dense_map = stdb::container::dense_map<
    Key, T, Hash, KeyEqual,
    std::pmr::polymorphic_allocator<std::conditional_t<detail::is_map_v<T>, std::pair<Key, T>, Key>>,
    MemoryPolicy>;

// Convenience aliases
template <typename Key, typename Value>
using fast_map = dense_map<Key, Value>;

}  // namespace pmr

}  // namespace stdb::container

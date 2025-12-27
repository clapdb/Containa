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
#include <cstdint>
#include <cstring>
#include <functional>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <utility>

#if __cplusplus >= 202002L || (defined(_MSVC_LANG) && _MSVC_LANG >= 202002L)
#include <compare>
#endif

#include "vectra.hpp"

#define BTREE_DEBUG 0

// Portable assume macro for compiler optimization hints
#if defined(__clang__)
#define BTREE_ASSUME(x) __builtin_assume(x)
#elif defined(__GNUC__)
#define BTREE_ASSUME(x)                    \
    do {                                   \
        if (!(x)) __builtin_unreachable(); \
    } while (0)
#else
#define BTREE_ASSUME(x) ((void)0)
#endif

// SIMD support detection
#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#define BTREE_HAS_SSE2 1
#if defined(__AVX2__)
#define BTREE_HAS_AVX2 1
#endif
#if defined(__AVX512F__) && defined(__AVX512BW__)
#define BTREE_HAS_AVX512 1
#endif
#elif defined(__aarch64__) || defined(_M_ARM64)
#include <arm_neon.h>
#define BTREE_HAS_NEON 1
#if defined(__ARM_FEATURE_SVE)
#include <arm_sve.h>
#define BTREE_HAS_SVE 1
#endif
#endif

namespace stdb::container {

/*
 * btree_map is a B-tree based ordered map container.
 *
 * Key features:
 *   - Cache-friendly: Multiple keys per node (unlike std::map's 1 key/node)
 *   - Memory efficient: ~5 bytes per element vs ~40 bytes for std::map
 *   - Fast lookup: O(log N) with better constants due to cache efficiency
 *   - Ordered: Supports in-order iteration
 *
 * Trade-offs vs std::map:
 *   - No iterator stability (iterators invalidated on insert/erase)
 *   - No pointer stability
 *   - Better for small-to-medium sized keys/values
 *
 * Template parameters:
 *   Key - Key type (must be comparable)
 *   Value - Mapped value type
 *   Compare - Comparison function (default: std::less<Key>)
 *   TargetNodeSize - Target node size in bytes (default: 256)
 */

// Concept to detect string-like types (have data() and size(), expensive comparison)
// Binary search is preferred for these types to reduce comparison count
template <typename T>
concept string_like = requires(const T& a) {
    { a.data() };
    { a.size() } -> std::convertible_to<std::size_t>;
};

// Helper to calculate optimal node size for a given key-value pair type
// Aims for at least 15 slots per node for good cache utilization
template <typename Key, typename Value>
constexpr std::size_t optimal_node_size() {
    constexpr std::size_t header_size = 24;  // parent + count + position + is_leaf + padding
    constexpr std::size_t target_slots = 15;
    constexpr std::size_t pair_size = sizeof(std::pair<Key, Value>);
    constexpr std::size_t min_size = header_size + pair_size * target_slots;
    // Round up to power of 2 for cache alignment
    if (min_size <= 256) return 256;
    if (min_size <= 512) return 512;
    if (min_size <= 1024) return 1024;
    if (min_size <= 2048) return 2048;
    return 4096;
}

template <typename Key, typename Value, typename Compare = std::less<Key>,
          std::size_t TargetNodeSize = optimal_node_size<Key, Value>()>
class btree_map
{
   public:
    using key_type = Key;
    using mapped_type = Value;
    using value_type = std::pair<const Key, Value>;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    using key_compare = Compare;
    using reference = value_type&;
    using const_reference = const value_type&;
    using pointer = value_type*;
    using const_pointer = const value_type*;

    // value_compare - compares value_type by key (std::map compatible)
    class value_compare
    {
        friend class btree_map;

       protected:
        Compare comp;
        explicit value_compare(Compare c) : comp(c) {}

       public:
        using result_type [[deprecated]] = bool;
        using first_argument_type [[deprecated]] = value_type;
        using second_argument_type [[deprecated]] = value_type;

        bool operator()(const value_type& lhs, const value_type& rhs) const { return comp(lhs.first, rhs.first); }
    };

   private:
    // Storage type without const (for internal manipulation)
    using storage_type = std::pair<Key, Value>;

    // Calculate optimal number of slots per node
    // Node layout: [header] + [slots (key-value pairs)] + [children for internal]
    static constexpr size_type kNodeHeaderSize = sizeof(void*) * 2 + 8;  // parent + padding
    static constexpr size_type kMinSlots = 4;

    // Calculate slots for leaf node (no child pointers)
    static constexpr size_type calculate_leaf_slots() {
        size_type available = TargetNodeSize - kNodeHeaderSize;
        size_type slot_size = sizeof(storage_type);
        size_type slots = available / slot_size;
        return slots > kMinSlots ? slots : kMinSlots;
    }

    // Calculate slots for internal node (with child pointers)
    static constexpr size_type calculate_internal_slots() {
        size_type available = TargetNodeSize - kNodeHeaderSize;
        size_type slot_size = sizeof(storage_type);
        size_type ptr_size = sizeof(void*);
        // slots * slot_size + (slots + 1) * ptr_size <= available
        size_type slots = (available - ptr_size) / (slot_size + ptr_size);
        return slots > kMinSlots ? slots : kMinSlots;
    }

    static constexpr size_type kLeafSlots = calculate_leaf_slots();
    static constexpr size_type kInternalSlots = calculate_internal_slots();
    static constexpr size_type kMaxSlots = kLeafSlots > kInternalSlots ? kLeafSlots : kInternalSlots;

    // Minimum slots before underflow (except root)
    // For B-tree merges: 2*(min-1) + 1 <= max_slots, so min <= (max_slots+1)/2
    // But we use min-1 for the underflowing node, so effective formula is max_slots/2
    static constexpr size_type kMinLeafSlots = kLeafSlots / 2;
    static constexpr size_type kMinInternalSlots = kInternalSlots / 2;

    struct node_base
    {
        node_base* parent = nullptr;
        uint16_t count = 0;     // Number of keys in this node
        uint16_t position = 0;  // Position in parent's children array
        uint8_t is_leaf = 1;    // 1 for leaf, 0 for internal
        uint8_t padding[3] = {};

        [[nodiscard]] auto is_leaf_node() const noexcept -> bool { return is_leaf != 0; }
        [[nodiscard]] auto is_full() const noexcept -> bool { return count >= (is_leaf ? kLeafSlots : kInternalSlots); }
    };

    struct leaf_node : node_base
    {
        // Key-value pairs stored together for proper iteration
        storage_type slots[kLeafSlots];

        leaf_node() { this->is_leaf = 1; }

        [[nodiscard]] auto key(size_type i) const noexcept -> const Key& { return slots[i].first; }
        [[nodiscard]] auto key(size_type i) noexcept -> Key& { return slots[i].first; }
        [[nodiscard]] auto value(size_type i) const noexcept -> const Value& { return slots[i].second; }
        [[nodiscard]] auto value(size_type i) noexcept -> Value& { return slots[i].second; }
    };

    struct internal_node : node_base
    {
        storage_type slots[kInternalSlots];
        node_base* children[kInternalSlots + 1] = {};

        internal_node() { this->is_leaf = 0; }

        [[nodiscard]] auto key(size_type i) const noexcept -> const Key& { return slots[i].first; }
        [[nodiscard]] auto key(size_type i) noexcept -> Key& { return slots[i].first; }
        [[nodiscard]] auto value(size_type i) const noexcept -> const Value& { return slots[i].second; }
        [[nodiscard]] auto value(size_type i) noexcept -> Value& { return slots[i].second; }
    };

    // Root node and tree state
    node_base* _root = nullptr;
    size_type _size = 0;
    Compare _comp;

    // Helper to get key at position
    [[nodiscard]] auto get_key(const node_base* node, size_type pos) const noexcept -> const Key& {
        if (node->is_leaf_node()) {
            return static_cast<const leaf_node*>(node)->key(pos);
        }
        return static_cast<const internal_node*>(node)->key(pos);
    }

    // Helper to get value at position
    [[nodiscard]] auto get_value(node_base* node, size_type pos) noexcept -> Value& {
        if (node->is_leaf_node()) {
            return static_cast<leaf_node*>(node)->value(pos);
        }
        return static_cast<internal_node*>(node)->value(pos);
    }

    [[nodiscard]] auto get_value(const node_base* node, size_type pos) const noexcept -> const Value& {
        if (node->is_leaf_node()) {
            return static_cast<const leaf_node*>(node)->value(pos);
        }
        return static_cast<const internal_node*>(node)->value(pos);
    }

    // Helper to get slot (key-value pair) at position
    [[nodiscard]] auto get_slot(node_base* node, size_type pos) noexcept -> storage_type& {
        if (node->is_leaf_node()) {
            return static_cast<leaf_node*>(node)->slots[pos];
        }
        return static_cast<internal_node*>(node)->slots[pos];
    }

    [[nodiscard]] auto get_slot(const node_base* node, size_type pos) const noexcept -> const storage_type& {
        if (node->is_leaf_node()) {
            return static_cast<const leaf_node*>(node)->slots[pos];
        }
        return static_cast<const internal_node*>(node)->slots[pos];
    }

    // Helper to get child at position
    [[nodiscard]] auto get_child(node_base* node, size_type pos) noexcept -> node_base* {
        Assert(!node->is_leaf_node(), "get_child on leaf node");
        return static_cast<internal_node*>(node)->children[pos];
    }

    // Set child at position
    void set_child(node_base* node, size_type pos, node_base* child) {
        Assert(!node->is_leaf_node(), "set_child on leaf node");
        static_cast<internal_node*>(node)->children[pos] = child;
        if (child != nullptr) {
            child->parent = node;
            child->position = static_cast<uint16_t>(pos);
        }
    }

    // SIMD search helpers
#ifdef BTREE_HAS_SSE2
    // SSE2 lower_bound for int32_t keys (signed)
    template <typename T>
    static auto simd_lower_bound_s32(const T* slots, size_type count, int32_t target) noexcept -> size_type
    requires(sizeof(T) >= sizeof(int32_t))
    {
        constexpr size_type stride = sizeof(T) / sizeof(int32_t);
        const auto* keys = reinterpret_cast<const int32_t*>(slots);

        __m128i target_vec = _mm_set1_epi32(target);
        size_type i = 0;

        while (i + 4 <= count) {
            __m128i key_vec =
              _mm_set_epi32(keys[(i + 3) * stride], keys[(i + 2) * stride], keys[(i + 1) * stride], keys[i * stride]);
            __m128i lt = _mm_cmplt_epi32(key_vec, target_vec);
            int mask = _mm_movemask_ps(_mm_castsi128_ps(lt));

            if (mask != 0xF) {
                return i + __builtin_ctz(~mask & 0xF);
            }
            i += 4;
        }

        for (; i < count; ++i) {
            if (keys[i * stride] >= target) return i;
        }
        return count;
    }

    // SSE2 lower_bound for uint32_t keys (unsigned)
    template <typename T>
    static auto simd_lower_bound_u32(const T* slots, size_type count, uint32_t target) noexcept -> size_type
    requires(sizeof(T) >= sizeof(uint32_t))
    {
        constexpr size_type stride = sizeof(T) / sizeof(uint32_t);
        const auto* keys = reinterpret_cast<const uint32_t*>(slots);

        // XOR with sign bit to convert unsigned to signed comparison
        __m128i sign_bit = _mm_set1_epi32(static_cast<int32_t>(0x80000000));
        __m128i target_vec = _mm_xor_si128(_mm_set1_epi32(static_cast<int32_t>(target)), sign_bit);
        size_type i = 0;

        while (i + 4 <= count) {
            __m128i key_vec =
              _mm_set_epi32(static_cast<int32_t>(keys[(i + 3) * stride]), static_cast<int32_t>(keys[(i + 2) * stride]),
                            static_cast<int32_t>(keys[(i + 1) * stride]), static_cast<int32_t>(keys[i * stride]));
            key_vec = _mm_xor_si128(key_vec, sign_bit);
            __m128i lt = _mm_cmplt_epi32(key_vec, target_vec);
            int mask = _mm_movemask_ps(_mm_castsi128_ps(lt));

            if (mask != 0xF) {
                return i + __builtin_ctz(~mask & 0xF);
            }
            i += 4;
        }

        for (; i < count; ++i) {
            if (keys[i * stride] >= target) return i;
        }
        return count;
    }

    // SSE2 lower_bound for int16_t keys (signed) - processes 8 keys at a time
    template <typename T>
    static auto simd_lower_bound_s16(const T* slots, size_type count, int16_t target) noexcept -> size_type
    requires(sizeof(T) >= sizeof(int16_t))
    {
        constexpr size_type stride = sizeof(T) / sizeof(int16_t);
        const auto* keys = reinterpret_cast<const int16_t*>(slots);

        __m128i target_vec = _mm_set1_epi16(target);
        size_type i = 0;

        while (i + 8 <= count) {
            __m128i key_vec = _mm_set_epi16(keys[(i + 7) * stride], keys[(i + 6) * stride], keys[(i + 5) * stride],
                                            keys[(i + 4) * stride], keys[(i + 3) * stride], keys[(i + 2) * stride],
                                            keys[(i + 1) * stride], keys[i * stride]);
            __m128i lt = _mm_cmplt_epi16(key_vec, target_vec);
            int mask = _mm_movemask_epi8(lt);

            // Each 16-bit element produces 2 bits in mask (both 1 if <, both 0 if >=)
            if (mask != 0xFFFF) {
                return i + (__builtin_ctz(~mask) >> 1);
            }
            i += 8;
        }

        for (; i < count; ++i) {
            if (keys[i * stride] >= target) return i;
        }
        return count;
    }

    // SSE2 lower_bound for uint16_t keys (unsigned) - processes 8 keys at a time
    template <typename T>
    static auto simd_lower_bound_u16(const T* slots, size_type count, uint16_t target) noexcept -> size_type
    requires(sizeof(T) >= sizeof(uint16_t))
    {
        constexpr size_type stride = sizeof(T) / sizeof(uint16_t);
        const auto* keys = reinterpret_cast<const uint16_t*>(slots);

        // XOR with sign bit to convert unsigned to signed comparison
        __m128i sign_bit = _mm_set1_epi16(static_cast<int16_t>(0x8000));
        __m128i target_vec = _mm_xor_si128(_mm_set1_epi16(static_cast<int16_t>(target)), sign_bit);
        size_type i = 0;

        while (i + 8 <= count) {
            __m128i key_vec =
              _mm_set_epi16(static_cast<int16_t>(keys[(i + 7) * stride]), static_cast<int16_t>(keys[(i + 6) * stride]),
                            static_cast<int16_t>(keys[(i + 5) * stride]), static_cast<int16_t>(keys[(i + 4) * stride]),
                            static_cast<int16_t>(keys[(i + 3) * stride]), static_cast<int16_t>(keys[(i + 2) * stride]),
                            static_cast<int16_t>(keys[(i + 1) * stride]), static_cast<int16_t>(keys[i * stride]));
            key_vec = _mm_xor_si128(key_vec, sign_bit);
            __m128i lt = _mm_cmplt_epi16(key_vec, target_vec);
            int mask = _mm_movemask_epi8(lt);

            if (mask != 0xFFFF) {
                return i + (__builtin_ctz(~mask) >> 1);
            }
            i += 8;
        }

        for (; i < count; ++i) {
            if (keys[i * stride] >= target) return i;
        }
        return count;
    }

    // SSE2 lower_bound for int8_t keys (signed) - processes 16 keys at a time
    template <typename T>
    static auto simd_lower_bound_s8(const T* slots, size_type count, int8_t target) noexcept -> size_type
    requires(sizeof(T) >= sizeof(int8_t))
    {
        constexpr size_type stride = sizeof(T) / sizeof(int8_t);
        const auto* keys = reinterpret_cast<const int8_t*>(slots);

        __m128i target_vec = _mm_set1_epi8(target);
        size_type i = 0;

        while (i + 16 <= count) {
            __m128i key_vec = _mm_set_epi8(
              keys[(i + 15) * stride], keys[(i + 14) * stride], keys[(i + 13) * stride], keys[(i + 12) * stride],
              keys[(i + 11) * stride], keys[(i + 10) * stride], keys[(i + 9) * stride], keys[(i + 8) * stride],
              keys[(i + 7) * stride], keys[(i + 6) * stride], keys[(i + 5) * stride], keys[(i + 4) * stride],
              keys[(i + 3) * stride], keys[(i + 2) * stride], keys[(i + 1) * stride], keys[i * stride]);
            __m128i lt = _mm_cmplt_epi8(key_vec, target_vec);
            int mask = _mm_movemask_epi8(lt);

            if (mask != 0xFFFF) {
                return i + __builtin_ctz(~mask);
            }
            i += 16;
        }

        for (; i < count; ++i) {
            if (keys[i * stride] >= target) return i;
        }
        return count;
    }

    // SSE2 lower_bound for uint8_t keys (unsigned) - processes 16 keys at a time
    template <typename T>
    static auto simd_lower_bound_u8(const T* slots, size_type count, uint8_t target) noexcept -> size_type
    requires(sizeof(T) >= sizeof(uint8_t))
    {
        constexpr size_type stride = sizeof(T) / sizeof(uint8_t);
        const auto* keys = reinterpret_cast<const uint8_t*>(slots);

        // XOR with sign bit to convert unsigned to signed comparison
        __m128i sign_bit = _mm_set1_epi8(static_cast<int8_t>(0x80));
        __m128i target_vec = _mm_xor_si128(_mm_set1_epi8(static_cast<int8_t>(target)), sign_bit);
        size_type i = 0;

        while (i + 16 <= count) {
            __m128i key_vec =
              _mm_set_epi8(static_cast<int8_t>(keys[(i + 15) * stride]), static_cast<int8_t>(keys[(i + 14) * stride]),
                           static_cast<int8_t>(keys[(i + 13) * stride]), static_cast<int8_t>(keys[(i + 12) * stride]),
                           static_cast<int8_t>(keys[(i + 11) * stride]), static_cast<int8_t>(keys[(i + 10) * stride]),
                           static_cast<int8_t>(keys[(i + 9) * stride]), static_cast<int8_t>(keys[(i + 8) * stride]),
                           static_cast<int8_t>(keys[(i + 7) * stride]), static_cast<int8_t>(keys[(i + 6) * stride]),
                           static_cast<int8_t>(keys[(i + 5) * stride]), static_cast<int8_t>(keys[(i + 4) * stride]),
                           static_cast<int8_t>(keys[(i + 3) * stride]), static_cast<int8_t>(keys[(i + 2) * stride]),
                           static_cast<int8_t>(keys[(i + 1) * stride]), static_cast<int8_t>(keys[i * stride]));
            key_vec = _mm_xor_si128(key_vec, sign_bit);
            __m128i lt = _mm_cmplt_epi8(key_vec, target_vec);
            int mask = _mm_movemask_epi8(lt);

            if (mask != 0xFFFF) {
                return i + __builtin_ctz(~mask);
            }
            i += 16;
        }

        for (; i < count; ++i) {
            if (keys[i * stride] >= target) return i;
        }
        return count;
    }

    // SSE lower_bound for float keys - processes 4 keys at a time
    template <typename T>
    static auto simd_lower_bound_float(const T* slots, size_type count, float target) noexcept -> size_type
    requires(sizeof(T) >= sizeof(float))
    {
        constexpr size_type stride = sizeof(T) / sizeof(float);
        const auto* keys = reinterpret_cast<const float*>(slots);

        __m128 target_vec = _mm_set1_ps(target);
        size_type i = 0;

        while (i + 4 <= count) {
            __m128 key_vec =
              _mm_set_ps(keys[(i + 3) * stride], keys[(i + 2) * stride], keys[(i + 1) * stride], keys[i * stride]);
            __m128 lt = _mm_cmplt_ps(key_vec, target_vec);
            int mask = _mm_movemask_ps(lt);

            if (mask != 0xF) {
                return i + __builtin_ctz(~mask & 0xF);
            }
            i += 4;
        }

        for (; i < count; ++i) {
            if (keys[i * stride] >= target) return i;
        }
        return count;
    }

    // SSE2 lower_bound for double keys - processes 2 keys at a time
    template <typename T>
    static auto simd_lower_bound_double(const T* slots, size_type count, double target) noexcept -> size_type
    requires(sizeof(T) >= sizeof(double))
    {
        constexpr size_type stride = sizeof(T) / sizeof(double);
        const auto* keys = reinterpret_cast<const double*>(slots);

        __m128d target_vec = _mm_set1_pd(target);
        size_type i = 0;

        while (i + 2 <= count) {
            __m128d key_vec = _mm_set_pd(keys[(i + 1) * stride], keys[i * stride]);
            __m128d lt = _mm_cmplt_pd(key_vec, target_vec);
            int mask = _mm_movemask_pd(lt);

            if (mask != 0x3) {
                return i + __builtin_ctz(~mask & 0x3);
            }
            i += 2;
        }

        if (i < count && keys[i * stride] >= target) return i;
        return count;
    }
#endif

#ifdef BTREE_HAS_AVX2
    // AVX2 lower_bound for int64_t keys (signed)
    template <typename T>
    static auto simd_lower_bound_s64(const T* slots, size_type count, int64_t target) noexcept -> size_type
    requires(sizeof(T) >= sizeof(int64_t))
    {
        constexpr size_type stride = sizeof(T) / sizeof(int64_t);
        const auto* keys = reinterpret_cast<const int64_t*>(slots);

        __m256i target_vec = _mm256_set1_epi64x(target);
        size_type i = 0;

        while (i + 4 <= count) {
            __m256i key_vec = _mm256_set_epi64x(keys[(i + 3) * stride], keys[(i + 2) * stride], keys[(i + 1) * stride],
                                                keys[i * stride]);
            __m256i lt = _mm256_cmpgt_epi64(target_vec, key_vec);
            int mask = _mm256_movemask_pd(_mm256_castsi256_pd(lt));

            if (mask != 0xF) {
                return i + __builtin_ctz(~mask & 0xF);
            }
            i += 4;
        }

        for (; i < count; ++i) {
            if (keys[i * stride] >= target) return i;
        }
        return count;
    }

    // AVX2 lower_bound for uint64_t keys (unsigned)
    template <typename T>
    static auto simd_lower_bound_u64(const T* slots, size_type count, uint64_t target) noexcept -> size_type
    requires(sizeof(T) >= sizeof(uint64_t))
    {
        constexpr size_type stride = sizeof(T) / sizeof(uint64_t);
        const auto* keys = reinterpret_cast<const uint64_t*>(slots);

        // XOR with sign bit to convert unsigned to signed comparison
        __m256i sign_bit = _mm256_set1_epi64x(static_cast<int64_t>(0x8000000000000000ULL));
        __m256i target_vec = _mm256_xor_si256(_mm256_set1_epi64x(static_cast<int64_t>(target)), sign_bit);
        size_type i = 0;

        while (i + 4 <= count) {
            __m256i key_vec = _mm256_set_epi64x(
              static_cast<int64_t>(keys[(i + 3) * stride]), static_cast<int64_t>(keys[(i + 2) * stride]),
              static_cast<int64_t>(keys[(i + 1) * stride]), static_cast<int64_t>(keys[i * stride]));
            key_vec = _mm256_xor_si256(key_vec, sign_bit);
            __m256i lt = _mm256_cmpgt_epi64(target_vec, key_vec);
            int mask = _mm256_movemask_pd(_mm256_castsi256_pd(lt));

            if (mask != 0xF) {
                return i + __builtin_ctz(~mask & 0xF);
            }
            i += 4;
        }

        for (; i < count; ++i) {
            if (keys[i * stride] >= target) return i;
        }
        return count;
    }

    // AVX lower_bound for double keys - processes 4 keys at a time
    template <typename T>
    static auto simd_lower_bound_double_avx(const T* slots, size_type count, double target) noexcept -> size_type
    requires(sizeof(T) >= sizeof(double))
    {
        constexpr size_type stride = sizeof(T) / sizeof(double);
        const auto* keys = reinterpret_cast<const double*>(slots);

        __m256d target_vec = _mm256_set1_pd(target);
        size_type i = 0;

        while (i + 4 <= count) {
            __m256d key_vec =
              _mm256_set_pd(keys[(i + 3) * stride], keys[(i + 2) * stride], keys[(i + 1) * stride], keys[i * stride]);
            __m256d lt = _mm256_cmp_pd(key_vec, target_vec, _CMP_LT_OQ);
            int mask = _mm256_movemask_pd(lt);

            if (mask != 0xF) {
                return i + __builtin_ctz(~mask & 0xF);
            }
            i += 4;
        }

        for (; i < count; ++i) {
            if (keys[i * stride] >= target) return i;
        }
        return count;
    }
#endif

#ifdef BTREE_HAS_AVX512
    // AVX-512 lower_bound for int64_t keys (signed) - processes 8 keys at a time
    template <typename T>
    static auto avx512_lower_bound_s64(const T* slots, size_type count, int64_t target) noexcept -> size_type
    requires(sizeof(T) >= sizeof(int64_t))
    {
        constexpr size_type stride = sizeof(T) / sizeof(int64_t);
        const auto* keys = reinterpret_cast<const int64_t*>(slots);

        __m512i target_vec = _mm512_set1_epi64(target);
        size_type i = 0;

        while (i + 8 <= count) {
            __m512i key_vec = _mm512_set_epi64(keys[(i + 7) * stride], keys[(i + 6) * stride], keys[(i + 5) * stride],
                                               keys[(i + 4) * stride], keys[(i + 3) * stride], keys[(i + 2) * stride],
                                               keys[(i + 1) * stride], keys[i * stride]);
            // Compare: mask bit is 1 where key < target
            __mmask8 lt_mask = _mm512_cmplt_epi64_mask(key_vec, target_vec);

            if (lt_mask != 0xFF) {
                // Find first position where key >= target
                return i + __builtin_ctz(static_cast<unsigned>(~lt_mask & 0xFF));
            }
            i += 8;
        }

        for (; i < count; ++i) {
            if (keys[i * stride] >= target) return i;
        }
        return count;
    }

    // AVX-512 lower_bound for uint64_t keys (unsigned) - processes 8 keys at a time
    template <typename T>
    static auto avx512_lower_bound_u64(const T* slots, size_type count, uint64_t target) noexcept -> size_type
    requires(sizeof(T) >= sizeof(uint64_t))
    {
        constexpr size_type stride = sizeof(T) / sizeof(uint64_t);
        const auto* keys = reinterpret_cast<const uint64_t*>(slots);

        __m512i target_vec = _mm512_set1_epi64(static_cast<int64_t>(target));
        size_type i = 0;

        while (i + 8 <= count) {
            __m512i key_vec = _mm512_set_epi64(
              static_cast<int64_t>(keys[(i + 7) * stride]), static_cast<int64_t>(keys[(i + 6) * stride]),
              static_cast<int64_t>(keys[(i + 5) * stride]), static_cast<int64_t>(keys[(i + 4) * stride]),
              static_cast<int64_t>(keys[(i + 3) * stride]), static_cast<int64_t>(keys[(i + 2) * stride]),
              static_cast<int64_t>(keys[(i + 1) * stride]), static_cast<int64_t>(keys[i * stride]));
            // AVX-512 has native unsigned comparison
            __mmask8 lt_mask = _mm512_cmplt_epu64_mask(key_vec, target_vec);

            if (lt_mask != 0xFF) {
                return i + __builtin_ctz(static_cast<unsigned>(~lt_mask & 0xFF));
            }
            i += 8;
        }

        for (; i < count; ++i) {
            if (keys[i * stride] >= target) return i;
        }
        return count;
    }

    // AVX-512 lower_bound for int32_t keys (signed) - processes 16 keys at a time
    template <typename T>
    static auto avx512_lower_bound_s32(const T* slots, size_type count, int32_t target) noexcept -> size_type
    requires(sizeof(T) >= sizeof(int32_t))
    {
        constexpr size_type stride = sizeof(T) / sizeof(int32_t);
        const auto* keys = reinterpret_cast<const int32_t*>(slots);

        __m512i target_vec = _mm512_set1_epi32(target);
        size_type i = 0;

        while (i + 16 <= count) {
            __m512i key_vec = _mm512_set_epi32(
              keys[(i + 15) * stride], keys[(i + 14) * stride], keys[(i + 13) * stride], keys[(i + 12) * stride],
              keys[(i + 11) * stride], keys[(i + 10) * stride], keys[(i + 9) * stride], keys[(i + 8) * stride],
              keys[(i + 7) * stride], keys[(i + 6) * stride], keys[(i + 5) * stride], keys[(i + 4) * stride],
              keys[(i + 3) * stride], keys[(i + 2) * stride], keys[(i + 1) * stride], keys[i * stride]);
            __mmask16 lt_mask = _mm512_cmplt_epi32_mask(key_vec, target_vec);

            if (lt_mask != 0xFFFF) {
                return i + __builtin_ctz(static_cast<unsigned>(~lt_mask & 0xFFFF));
            }
            i += 16;
        }

        for (; i < count; ++i) {
            if (keys[i * stride] >= target) return i;
        }
        return count;
    }

    // AVX-512 lower_bound for uint32_t keys (unsigned) - processes 16 keys at a time
    template <typename T>
    static auto avx512_lower_bound_u32(const T* slots, size_type count, uint32_t target) noexcept -> size_type
    requires(sizeof(T) >= sizeof(uint32_t))
    {
        constexpr size_type stride = sizeof(T) / sizeof(uint32_t);
        const auto* keys = reinterpret_cast<const uint32_t*>(slots);

        __m512i target_vec = _mm512_set1_epi32(static_cast<int32_t>(target));
        size_type i = 0;

        while (i + 16 <= count) {
            __m512i key_vec = _mm512_set_epi32(
              static_cast<int32_t>(keys[(i + 15) * stride]), static_cast<int32_t>(keys[(i + 14) * stride]),
              static_cast<int32_t>(keys[(i + 13) * stride]), static_cast<int32_t>(keys[(i + 12) * stride]),
              static_cast<int32_t>(keys[(i + 11) * stride]), static_cast<int32_t>(keys[(i + 10) * stride]),
              static_cast<int32_t>(keys[(i + 9) * stride]), static_cast<int32_t>(keys[(i + 8) * stride]),
              static_cast<int32_t>(keys[(i + 7) * stride]), static_cast<int32_t>(keys[(i + 6) * stride]),
              static_cast<int32_t>(keys[(i + 5) * stride]), static_cast<int32_t>(keys[(i + 4) * stride]),
              static_cast<int32_t>(keys[(i + 3) * stride]), static_cast<int32_t>(keys[(i + 2) * stride]),
              static_cast<int32_t>(keys[(i + 1) * stride]), static_cast<int32_t>(keys[i * stride]));
            __mmask16 lt_mask = _mm512_cmplt_epu32_mask(key_vec, target_vec);

            if (lt_mask != 0xFFFF) {
                return i + __builtin_ctz(static_cast<unsigned>(~lt_mask & 0xFFFF));
            }
            i += 16;
        }

        for (; i < count; ++i) {
            if (keys[i * stride] >= target) return i;
        }
        return count;
    }

    // AVX-512 lower_bound for double keys - processes 8 keys at a time
    template <typename T>
    static auto avx512_lower_bound_double(const T* slots, size_type count, double target) noexcept -> size_type
    requires(sizeof(T) >= sizeof(double))
    {
        constexpr size_type stride = sizeof(T) / sizeof(double);
        const auto* keys = reinterpret_cast<const double*>(slots);

        __m512d target_vec = _mm512_set1_pd(target);
        size_type i = 0;

        while (i + 8 <= count) {
            __m512d key_vec = _mm512_set_pd(keys[(i + 7) * stride], keys[(i + 6) * stride], keys[(i + 5) * stride],
                                            keys[(i + 4) * stride], keys[(i + 3) * stride], keys[(i + 2) * stride],
                                            keys[(i + 1) * stride], keys[i * stride]);
            __mmask8 lt_mask = _mm512_cmp_pd_mask(key_vec, target_vec, _CMP_LT_OQ);

            if (lt_mask != 0xFF) {
                return i + __builtin_ctz(static_cast<unsigned>(~lt_mask & 0xFF));
            }
            i += 8;
        }

        for (; i < count; ++i) {
            if (keys[i * stride] >= target) return i;
        }
        return count;
    }

    // AVX-512 lower_bound for float keys - processes 16 keys at a time
    template <typename T>
    static auto avx512_lower_bound_float(const T* slots, size_type count, float target) noexcept -> size_type
    requires(sizeof(T) >= sizeof(float))
    {
        constexpr size_type stride = sizeof(T) / sizeof(float);
        const auto* keys = reinterpret_cast<const float*>(slots);

        __m512 target_vec = _mm512_set1_ps(target);
        size_type i = 0;

        while (i + 16 <= count) {
            __m512 key_vec = _mm512_set_ps(
              keys[(i + 15) * stride], keys[(i + 14) * stride], keys[(i + 13) * stride], keys[(i + 12) * stride],
              keys[(i + 11) * stride], keys[(i + 10) * stride], keys[(i + 9) * stride], keys[(i + 8) * stride],
              keys[(i + 7) * stride], keys[(i + 6) * stride], keys[(i + 5) * stride], keys[(i + 4) * stride],
              keys[(i + 3) * stride], keys[(i + 2) * stride], keys[(i + 1) * stride], keys[i * stride]);
            __mmask16 lt_mask = _mm512_cmp_ps_mask(key_vec, target_vec, _CMP_LT_OQ);

            if (lt_mask != 0xFFFF) {
                return i + __builtin_ctz(static_cast<unsigned>(~lt_mask & 0xFFFF));
            }
            i += 16;
        }

        for (; i < count; ++i) {
            if (keys[i * stride] >= target) return i;
        }
        return count;
    }
#endif

#ifdef BTREE_HAS_NEON
    // NEON lower_bound for int32_t keys (signed)
    template <typename T>
    static auto neon_lower_bound_s32(const T* slots, size_type count, int32_t target) noexcept -> size_type
    requires(sizeof(T) >= sizeof(int32_t))
    {
        constexpr size_type stride = sizeof(T) / sizeof(int32_t);
        const auto* keys = reinterpret_cast<const int32_t*>(slots);

        int32x4_t target_vec = vdupq_n_s32(target);
        size_type i = 0;

        while (i + 4 <= count) {
            int32x4_t key_vec = {keys[i * stride], keys[(i + 1) * stride], keys[(i + 2) * stride],
                                 keys[(i + 3) * stride]};
            uint32x4_t lt = vcltq_s32(key_vec, target_vec);

            if (vmaxvq_u32(lt) != 0xFFFFFFFF) {
                for (size_type j = 0; j < 4; ++j) {
                    if (keys[(i + j) * stride] >= target) return i + j;
                }
            }
            i += 4;
        }

        for (; i < count; ++i) {
            if (keys[i * stride] >= target) return i;
        }
        return count;
    }

    // NEON lower_bound for uint32_t keys (unsigned)
    template <typename T>
    static auto neon_lower_bound_u32(const T* slots, size_type count, uint32_t target) noexcept -> size_type
    requires(sizeof(T) >= sizeof(uint32_t))
    {
        constexpr size_type stride = sizeof(T) / sizeof(uint32_t);
        const auto* keys = reinterpret_cast<const uint32_t*>(slots);

        uint32x4_t target_vec = vdupq_n_u32(target);
        size_type i = 0;

        while (i + 4 <= count) {
            uint32x4_t key_vec = {keys[i * stride], keys[(i + 1) * stride], keys[(i + 2) * stride],
                                  keys[(i + 3) * stride]};
            uint32x4_t lt = vcltq_u32(key_vec, target_vec);

            if (vmaxvq_u32(lt) != 0xFFFFFFFF) {
                for (size_type j = 0; j < 4; ++j) {
                    if (keys[(i + j) * stride] >= target) return i + j;
                }
            }
            i += 4;
        }

        for (; i < count; ++i) {
            if (keys[i * stride] >= target) return i;
        }
        return count;
    }

    // NEON lower_bound for int64_t keys (signed)
    template <typename T>
    static auto neon_lower_bound_s64(const T* slots, size_type count, int64_t target) noexcept -> size_type
    requires(sizeof(T) >= sizeof(int64_t))
    {
        constexpr size_type stride = sizeof(T) / sizeof(int64_t);
        const auto* keys = reinterpret_cast<const int64_t*>(slots);

        int64x2_t target_vec = vdupq_n_s64(target);
        size_type i = 0;

        while (i + 2 <= count) {
            int64x2_t key_vec = {keys[i * stride], keys[(i + 1) * stride]};
            uint64x2_t lt = vcltq_s64(key_vec, target_vec);

            if (vmaxvq_u64(lt) != 0xFFFFFFFFFFFFFFFFULL) {
                if (keys[i * stride] >= target) return i;
                if (keys[(i + 1) * stride] >= target) return i + 1;
            }
            i += 2;
        }

        if (i < count && keys[i * stride] >= target) return i;
        return count;
    }

    // NEON lower_bound for uint64_t keys (unsigned)
    template <typename T>
    static auto neon_lower_bound_u64(const T* slots, size_type count, uint64_t target) noexcept -> size_type
    requires(sizeof(T) >= sizeof(uint64_t))
    {
        constexpr size_type stride = sizeof(T) / sizeof(uint64_t);
        const auto* keys = reinterpret_cast<const uint64_t*>(slots);

        uint64x2_t target_vec = vdupq_n_u64(target);
        size_type i = 0;

        while (i + 2 <= count) {
            uint64x2_t key_vec = {keys[i * stride], keys[(i + 1) * stride]};
            uint64x2_t lt = vcltq_u64(key_vec, target_vec);

            if (vmaxvq_u64(lt) != 0xFFFFFFFFFFFFFFFFULL) {
                if (keys[i * stride] >= target) return i;
                if (keys[(i + 1) * stride] >= target) return i + 1;
            }
            i += 2;
        }

        if (i < count && keys[i * stride] >= target) return i;
        return count;
    }

    // NEON lower_bound for int16_t keys (signed) - processes 8 keys at a time
    template <typename T>
    static auto neon_lower_bound_s16(const T* slots, size_type count, int16_t target) noexcept -> size_type
    requires(sizeof(T) >= sizeof(int16_t))
    {
        constexpr size_type stride = sizeof(T) / sizeof(int16_t);
        const auto* keys = reinterpret_cast<const int16_t*>(slots);

        int16x8_t target_vec = vdupq_n_s16(target);
        size_type i = 0;

        while (i + 8 <= count) {
            int16x8_t key_vec = {keys[i * stride],       keys[(i + 1) * stride], keys[(i + 2) * stride],
                                 keys[(i + 3) * stride], keys[(i + 4) * stride], keys[(i + 5) * stride],
                                 keys[(i + 6) * stride], keys[(i + 7) * stride]};
            uint16x8_t lt = vcltq_s16(key_vec, target_vec);

            if (vmaxvq_u16(lt) != 0xFFFF) {
                for (size_type j = 0; j < 8; ++j) {
                    if (keys[(i + j) * stride] >= target) return i + j;
                }
            }
            i += 8;
        }

        for (; i < count; ++i) {
            if (keys[i * stride] >= target) return i;
        }
        return count;
    }

    // NEON lower_bound for uint16_t keys (unsigned) - processes 8 keys at a time
    template <typename T>
    static auto neon_lower_bound_u16(const T* slots, size_type count, uint16_t target) noexcept -> size_type
    requires(sizeof(T) >= sizeof(uint16_t))
    {
        constexpr size_type stride = sizeof(T) / sizeof(uint16_t);
        const auto* keys = reinterpret_cast<const uint16_t*>(slots);

        uint16x8_t target_vec = vdupq_n_u16(target);
        size_type i = 0;

        while (i + 8 <= count) {
            uint16x8_t key_vec = {keys[i * stride],       keys[(i + 1) * stride], keys[(i + 2) * stride],
                                  keys[(i + 3) * stride], keys[(i + 4) * stride], keys[(i + 5) * stride],
                                  keys[(i + 6) * stride], keys[(i + 7) * stride]};
            uint16x8_t lt = vcltq_u16(key_vec, target_vec);

            if (vmaxvq_u16(lt) != 0xFFFF) {
                for (size_type j = 0; j < 8; ++j) {
                    if (keys[(i + j) * stride] >= target) return i + j;
                }
            }
            i += 8;
        }

        for (; i < count; ++i) {
            if (keys[i * stride] >= target) return i;
        }
        return count;
    }

    // NEON lower_bound for int8_t keys (signed) - processes 16 keys at a time
    template <typename T>
    static auto neon_lower_bound_s8(const T* slots, size_type count, int8_t target) noexcept -> size_type
    requires(sizeof(T) >= sizeof(int8_t))
    {
        constexpr size_type stride = sizeof(T) / sizeof(int8_t);
        const auto* keys = reinterpret_cast<const int8_t*>(slots);

        int8x16_t target_vec = vdupq_n_s8(target);
        size_type i = 0;

        while (i + 16 <= count) {
            int8x16_t key_vec = {
              keys[i * stride],        keys[(i + 1) * stride],  keys[(i + 2) * stride],  keys[(i + 3) * stride],
              keys[(i + 4) * stride],  keys[(i + 5) * stride],  keys[(i + 6) * stride],  keys[(i + 7) * stride],
              keys[(i + 8) * stride],  keys[(i + 9) * stride],  keys[(i + 10) * stride], keys[(i + 11) * stride],
              keys[(i + 12) * stride], keys[(i + 13) * stride], keys[(i + 14) * stride], keys[(i + 15) * stride]};
            uint8x16_t lt = vcltq_s8(key_vec, target_vec);

            if (vmaxvq_u8(lt) != 0xFF) {
                for (size_type j = 0; j < 16; ++j) {
                    if (keys[(i + j) * stride] >= target) return i + j;
                }
            }
            i += 16;
        }

        for (; i < count; ++i) {
            if (keys[i * stride] >= target) return i;
        }
        return count;
    }

    // NEON lower_bound for uint8_t keys (unsigned) - processes 16 keys at a time
    template <typename T>
    static auto neon_lower_bound_u8(const T* slots, size_type count, uint8_t target) noexcept -> size_type
    requires(sizeof(T) >= sizeof(uint8_t))
    {
        constexpr size_type stride = sizeof(T) / sizeof(uint8_t);
        const auto* keys = reinterpret_cast<const uint8_t*>(slots);

        uint8x16_t target_vec = vdupq_n_u8(target);
        size_type i = 0;

        while (i + 16 <= count) {
            uint8x16_t key_vec = {
              keys[i * stride],        keys[(i + 1) * stride],  keys[(i + 2) * stride],  keys[(i + 3) * stride],
              keys[(i + 4) * stride],  keys[(i + 5) * stride],  keys[(i + 6) * stride],  keys[(i + 7) * stride],
              keys[(i + 8) * stride],  keys[(i + 9) * stride],  keys[(i + 10) * stride], keys[(i + 11) * stride],
              keys[(i + 12) * stride], keys[(i + 13) * stride], keys[(i + 14) * stride], keys[(i + 15) * stride]};
            uint8x16_t lt = vcltq_u8(key_vec, target_vec);

            if (vmaxvq_u8(lt) != 0xFF) {
                for (size_type j = 0; j < 16; ++j) {
                    if (keys[(i + j) * stride] >= target) return i + j;
                }
            }
            i += 16;
        }

        for (; i < count; ++i) {
            if (keys[i * stride] >= target) return i;
        }
        return count;
    }

    // NEON lower_bound for float keys - processes 4 keys at a time
    template <typename T>
    static auto neon_lower_bound_float(const T* slots, size_type count, float target) noexcept -> size_type
    requires(sizeof(T) >= sizeof(float))
    {
        constexpr size_type stride = sizeof(T) / sizeof(float);
        const auto* keys = reinterpret_cast<const float*>(slots);

        float32x4_t target_vec = vdupq_n_f32(target);
        size_type i = 0;

        while (i + 4 <= count) {
            float32x4_t key_vec = {keys[i * stride], keys[(i + 1) * stride], keys[(i + 2) * stride],
                                   keys[(i + 3) * stride]};
            uint32x4_t lt = vcltq_f32(key_vec, target_vec);

            if (vmaxvq_u32(lt) != 0xFFFFFFFF) {
                for (size_type j = 0; j < 4; ++j) {
                    if (keys[(i + j) * stride] >= target) return i + j;
                }
            }
            i += 4;
        }

        for (; i < count; ++i) {
            if (keys[i * stride] >= target) return i;
        }
        return count;
    }

    // NEON lower_bound for double keys - processes 2 keys at a time
    template <typename T>
    static auto neon_lower_bound_double(const T* slots, size_type count, double target) noexcept -> size_type
    requires(sizeof(T) >= sizeof(double))
    {
        constexpr size_type stride = sizeof(T) / sizeof(double);
        const auto* keys = reinterpret_cast<const double*>(slots);

        float64x2_t target_vec = vdupq_n_f64(target);
        size_type i = 0;

        while (i + 2 <= count) {
            float64x2_t key_vec = {keys[i * stride], keys[(i + 1) * stride]};
            uint64x2_t lt = vcltq_f64(key_vec, target_vec);

            if (vmaxvq_u64(lt) != 0xFFFFFFFFFFFFFFFFULL) {
                if (keys[i * stride] >= target) return i;
                if (keys[(i + 1) * stride] >= target) return i + 1;
            }
            i += 2;
        }

        if (i < count && keys[i * stride] >= target) return i;
        return count;
    }
#endif

#ifdef BTREE_HAS_SVE
    // SVE lower_bound for int32_t keys - processes svcntw() keys at a time (hardware dependent)
    // SVE vector length can be 128-2048 bits, so 4-64 int32_t elements per vector
    template <typename T>
    static auto sve_lower_bound_s32(const T* slots, size_type count, int32_t target) noexcept -> size_type
    requires(sizeof(T) >= sizeof(int32_t))
    {
        constexpr size_type stride = sizeof(T) / sizeof(int32_t);
        const auto* keys = reinterpret_cast<const int32_t*>(slots);

        svint32_t target_vec = svdup_s32(target);
        size_type i = 0;
        const size_type vec_len = svcntw();  // Number of 32-bit elements per vector

        while (i + vec_len <= count) {
            svbool_t pg = svptrue_b32();

            svint32_t key_vec;
            if constexpr (stride == 1) {
                key_vec = svld1_s32(pg, &keys[i]);
            } else {
                svuint32_t indices = svindex_u32(0, stride);
                key_vec = svld1_gather_u32index_s32(pg, &keys[i * stride], indices);
            }

            // Compare: true where key >= target
            svbool_t ge = svcmpge_s32(pg, key_vec, target_vec);

            if (svptest_any(pg, ge)) {
                // Found at least one key >= target, count leading false bits
                // svbrkb sets all bits after first true to false
                svbool_t first_ge = svbrkb_z(pg, ge);
                // Count number of true bits before the break = position of first match
                return i + svcntp_b32(pg, svnot_z(pg, first_ge));
            }
            i += vec_len;
        }

        for (; i < count; ++i) {
            if (keys[i * stride] >= target) return i;
        }
        return count;
    }

    // SVE lower_bound for uint32_t keys
    template <typename T>
    static auto sve_lower_bound_u32(const T* slots, size_type count, uint32_t target) noexcept -> size_type
    requires(sizeof(T) >= sizeof(uint32_t))
    {
        constexpr size_type stride = sizeof(T) / sizeof(uint32_t);
        const auto* keys = reinterpret_cast<const uint32_t*>(slots);

        svuint32_t target_vec = svdup_u32(target);
        size_type i = 0;
        const size_type vec_len = svcntw();

        while (i + vec_len <= count) {
            svbool_t pg = svptrue_b32();

            svuint32_t key_vec;
            if constexpr (stride == 1) {
                key_vec = svld1_u32(pg, &keys[i]);
            } else {
                svuint32_t indices = svindex_u32(0, stride);
                key_vec = svld1_gather_u32index_u32(pg, &keys[i * stride], indices);
            }

            svbool_t ge = svcmpge_u32(pg, key_vec, target_vec);

            if (svptest_any(pg, ge)) {
                svbool_t first_ge = svbrkb_z(pg, ge);
                return i + svcntp_b32(pg, svnot_z(pg, first_ge));
            }
            i += vec_len;
        }

        for (; i < count; ++i) {
            if (keys[i * stride] >= target) return i;
        }
        return count;
    }

    // SVE lower_bound for int64_t keys - processes svcntd() keys at a time
    template <typename T>
    static auto sve_lower_bound_s64(const T* slots, size_type count, int64_t target) noexcept -> size_type
    requires(sizeof(T) >= sizeof(int64_t))
    {
        constexpr size_type stride = sizeof(T) / sizeof(int64_t);
        const auto* keys = reinterpret_cast<const int64_t*>(slots);

        svint64_t target_vec = svdup_s64(target);
        size_type i = 0;
        const size_type vec_len = svcntd();

        while (i + vec_len <= count) {
            svbool_t pg = svptrue_b64();

            svint64_t key_vec;
            if constexpr (stride == 1) {
                key_vec = svld1_s64(pg, &keys[i]);
            } else {
                svuint64_t indices = svindex_u64(0, stride);
                key_vec = svld1_gather_u64index_s64(pg, &keys[i * stride], indices);
            }

            svbool_t ge = svcmpge_s64(pg, key_vec, target_vec);

            if (svptest_any(pg, ge)) {
                svbool_t first_ge = svbrkb_z(pg, ge);
                return i + svcntp_b64(pg, svnot_z(pg, first_ge));
            }
            i += vec_len;
        }

        for (; i < count; ++i) {
            if (keys[i * stride] >= target) return i;
        }
        return count;
    }

    // SVE lower_bound for uint64_t keys
    template <typename T>
    static auto sve_lower_bound_u64(const T* slots, size_type count, uint64_t target) noexcept -> size_type
    requires(sizeof(T) >= sizeof(uint64_t))
    {
        constexpr size_type stride = sizeof(T) / sizeof(uint64_t);
        const auto* keys = reinterpret_cast<const uint64_t*>(slots);

        svuint64_t target_vec = svdup_u64(target);
        size_type i = 0;
        const size_type vec_len = svcntd();

        while (i + vec_len <= count) {
            svbool_t pg = svptrue_b64();

            svuint64_t key_vec;
            if constexpr (stride == 1) {
                key_vec = svld1_u64(pg, &keys[i]);
            } else {
                svuint64_t indices = svindex_u64(0, stride);
                key_vec = svld1_gather_u64index_u64(pg, &keys[i * stride], indices);
            }

            svbool_t ge = svcmpge_u64(pg, key_vec, target_vec);

            if (svptest_any(pg, ge)) {
                svbool_t first_ge = svbrkb_z(pg, ge);
                return i + svcntp_b64(pg, svnot_z(pg, first_ge));
            }
            i += vec_len;
        }

        for (; i < count; ++i) {
            if (keys[i * stride] >= target) return i;
        }
        return count;
    }

    // SVE lower_bound for float keys
    template <typename T>
    static auto sve_lower_bound_float(const T* slots, size_type count, float target) noexcept -> size_type
    requires(sizeof(T) >= sizeof(float))
    {
        constexpr size_type stride = sizeof(T) / sizeof(float);
        const auto* keys = reinterpret_cast<const float*>(slots);

        svfloat32_t target_vec = svdup_f32(target);
        size_type i = 0;
        const size_type vec_len = svcntw();

        while (i + vec_len <= count) {
            svbool_t pg = svptrue_b32();

            svfloat32_t key_vec;
            if constexpr (stride == 1) {
                key_vec = svld1_f32(pg, &keys[i]);
            } else {
                svuint32_t indices = svindex_u32(0, stride);
                key_vec = svld1_gather_u32index_f32(pg, &keys[i * stride], indices);
            }

            svbool_t ge = svcmpge_f32(pg, key_vec, target_vec);

            if (svptest_any(pg, ge)) {
                svbool_t first_ge = svbrkb_z(pg, ge);
                return i + svcntp_b32(pg, svnot_z(pg, first_ge));
            }
            i += vec_len;
        }

        for (; i < count; ++i) {
            if (keys[i * stride] >= target) return i;
        }
        return count;
    }

    // SVE lower_bound for double keys
    template <typename T>
    static auto sve_lower_bound_double(const T* slots, size_type count, double target) noexcept -> size_type
    requires(sizeof(T) >= sizeof(double))
    {
        constexpr size_type stride = sizeof(T) / sizeof(double);
        const auto* keys = reinterpret_cast<const double*>(slots);

        svfloat64_t target_vec = svdup_f64(target);
        size_type i = 0;
        const size_type vec_len = svcntd();

        while (i + vec_len <= count) {
            svbool_t pg = svptrue_b64();

            svfloat64_t key_vec;
            if constexpr (stride == 1) {
                key_vec = svld1_f64(pg, &keys[i]);
            } else {
                svuint64_t indices = svindex_u64(0, stride);
                key_vec = svld1_gather_u64index_f64(pg, &keys[i * stride], indices);
            }

            svbool_t ge = svcmpge_f64(pg, key_vec, target_vec);

            if (svptest_any(pg, ge)) {
                svbool_t first_ge = svbrkb_z(pg, ge);
                return i + svcntp_b64(pg, svnot_z(pg, first_ge));
            }
            i += vec_len;
        }

        for (; i < count; ++i) {
            if (keys[i * stride] >= target) return i;
        }
        return count;
    }
#endif

    // Linear search within a node - faster for small counts due to:
    // 1. Sequential memory access (better cache/prefetch behavior)
    // 2. Better branch prediction
    // 3. Lower overhead per iteration
    // Returns first position where key >= slot
    template <typename Slots>
    [[nodiscard]] __attribute__((always_inline, flatten)) auto linear_search_in_slots(
      const Slots* __restrict__ slots, size_type count, const Key& __restrict__ key) const noexcept -> size_type {
        BTREE_ASSUME(count <= 32);
        for (size_type i = 0; i < count; ++i) {
            if (!_comp(slots[i].first, key)) {
                return i;
            }
        }
        return count;
    }

    // Binary search within a node using only keys for comparison
    // Returns first position where key >= slot
    template <typename Slots>
    [[nodiscard]] __attribute__((always_inline, flatten)) auto binary_search_in_slots(
      const Slots* __restrict__ slots, size_type count, const Key& __restrict__ key) const noexcept -> size_type {
        // Hint to compiler about expected count range (typical btree has 15 slots)
        BTREE_ASSUME(count <= 32);

        size_type lo = 0;
        size_type hi = count;
        while (lo < hi) {
            size_type mid = lo + ((hi - lo) >> 1);
            if (_comp(slots[mid].first, key)) {
                lo = mid + 1;
            } else {
                hi = mid;
            }
        }
        return lo;
    }

    // Specialized search for leaf node
    [[nodiscard]] __attribute__((always_inline, flatten)) auto lower_bound_in_leaf(
      const leaf_node* __restrict__ leaf, const Key& __restrict__ key) const noexcept -> size_type {
        // AVX-512 paths (highest priority - fastest)
#ifdef BTREE_HAS_AVX512
        if constexpr (std::is_same_v<Key, int64_t> && std::is_same_v<Compare, std::less<Key>>) {
            return avx512_lower_bound_s64(leaf->slots, leaf->count, key);
        }
        if constexpr (std::is_same_v<Key, uint64_t> && std::is_same_v<Compare, std::less<Key>>) {
            return avx512_lower_bound_u64(leaf->slots, leaf->count, key);
        }
        if constexpr (std::is_same_v<Key, int32_t> && std::is_same_v<Compare, std::less<Key>>) {
            return avx512_lower_bound_s32(leaf->slots, leaf->count, key);
        }
        if constexpr (std::is_same_v<Key, uint32_t> && std::is_same_v<Compare, std::less<Key>>) {
            return avx512_lower_bound_u32(leaf->slots, leaf->count, key);
        }
        if constexpr (std::is_same_v<Key, double> && std::is_same_v<Compare, std::less<Key>>) {
            return avx512_lower_bound_double(leaf->slots, leaf->count, key);
        }
        if constexpr (std::is_same_v<Key, float> && std::is_same_v<Compare, std::less<Key>>) {
            return avx512_lower_bound_float(leaf->slots, leaf->count, key);
        }
#endif
        // x86 SSE2/AVX2 paths
#ifdef BTREE_HAS_SSE2
        if constexpr (std::is_same_v<Key, int8_t> && std::is_same_v<Compare, std::less<Key>>) {
            return simd_lower_bound_s8(leaf->slots, leaf->count, key);
        }
        if constexpr (std::is_same_v<Key, uint8_t> && std::is_same_v<Compare, std::less<Key>>) {
            return simd_lower_bound_u8(leaf->slots, leaf->count, key);
        }
        if constexpr (std::is_same_v<Key, int16_t> && std::is_same_v<Compare, std::less<Key>>) {
            return simd_lower_bound_s16(leaf->slots, leaf->count, key);
        }
        if constexpr (std::is_same_v<Key, uint16_t> && std::is_same_v<Compare, std::less<Key>>) {
            return simd_lower_bound_u16(leaf->slots, leaf->count, key);
        }
#ifndef BTREE_HAS_AVX512  // Use SSE2 only if AVX-512 not available
        if constexpr (std::is_same_v<Key, int32_t> && std::is_same_v<Compare, std::less<Key>>) {
            return simd_lower_bound_s32(leaf->slots, leaf->count, key);
        }
        if constexpr (std::is_same_v<Key, uint32_t> && std::is_same_v<Compare, std::less<Key>>) {
            return simd_lower_bound_u32(leaf->slots, leaf->count, key);
        }
        if constexpr (std::is_same_v<Key, float> && std::is_same_v<Compare, std::less<Key>>) {
            return simd_lower_bound_float(leaf->slots, leaf->count, key);
        }
        if constexpr (std::is_same_v<Key, double> && std::is_same_v<Compare, std::less<Key>>) {
            return simd_lower_bound_double(leaf->slots, leaf->count, key);
        }
#endif
#endif
#ifdef BTREE_HAS_AVX2
#ifndef BTREE_HAS_AVX512  // Use AVX2 only if AVX-512 not available
        if constexpr (std::is_same_v<Key, int64_t> && std::is_same_v<Compare, std::less<Key>>) {
            return simd_lower_bound_s64(leaf->slots, leaf->count, key);
        }
        if constexpr (std::is_same_v<Key, uint64_t> && std::is_same_v<Compare, std::less<Key>>) {
            return simd_lower_bound_u64(leaf->slots, leaf->count, key);
        }
        if constexpr (std::is_same_v<Key, double> && std::is_same_v<Compare, std::less<Key>>) {
            return simd_lower_bound_double_avx(leaf->slots, leaf->count, key);
        }
#endif
#endif
        // ARM SVE paths (highest priority on ARM - scalable vectors)
#ifdef BTREE_HAS_SVE
        if constexpr (std::is_same_v<Key, int32_t> && std::is_same_v<Compare, std::less<Key>>) {
            return sve_lower_bound_s32(leaf->slots, leaf->count, key);
        }
        if constexpr (std::is_same_v<Key, uint32_t> && std::is_same_v<Compare, std::less<Key>>) {
            return sve_lower_bound_u32(leaf->slots, leaf->count, key);
        }
        if constexpr (std::is_same_v<Key, int64_t> && std::is_same_v<Compare, std::less<Key>>) {
            return sve_lower_bound_s64(leaf->slots, leaf->count, key);
        }
        if constexpr (std::is_same_v<Key, uint64_t> && std::is_same_v<Compare, std::less<Key>>) {
            return sve_lower_bound_u64(leaf->slots, leaf->count, key);
        }
        if constexpr (std::is_same_v<Key, float> && std::is_same_v<Compare, std::less<Key>>) {
            return sve_lower_bound_float(leaf->slots, leaf->count, key);
        }
        if constexpr (std::is_same_v<Key, double> && std::is_same_v<Compare, std::less<Key>>) {
            return sve_lower_bound_double(leaf->slots, leaf->count, key);
        }
#endif
        // ARM NEON paths (fallback for int8/int16 or when SVE not available)
#ifdef BTREE_HAS_NEON
        if constexpr (std::is_same_v<Key, int8_t> && std::is_same_v<Compare, std::less<Key>>) {
            return neon_lower_bound_s8(leaf->slots, leaf->count, key);
        }
        if constexpr (std::is_same_v<Key, uint8_t> && std::is_same_v<Compare, std::less<Key>>) {
            return neon_lower_bound_u8(leaf->slots, leaf->count, key);
        }
        if constexpr (std::is_same_v<Key, int16_t> && std::is_same_v<Compare, std::less<Key>>) {
            return neon_lower_bound_s16(leaf->slots, leaf->count, key);
        }
        if constexpr (std::is_same_v<Key, uint16_t> && std::is_same_v<Compare, std::less<Key>>) {
            return neon_lower_bound_u16(leaf->slots, leaf->count, key);
        }
#ifndef BTREE_HAS_SVE  // Use NEON only if SVE not available for these types
        if constexpr (std::is_same_v<Key, int32_t> && std::is_same_v<Compare, std::less<Key>>) {
            return neon_lower_bound_s32(leaf->slots, leaf->count, key);
        }
        if constexpr (std::is_same_v<Key, uint32_t> && std::is_same_v<Compare, std::less<Key>>) {
            return neon_lower_bound_u32(leaf->slots, leaf->count, key);
        }
        if constexpr (std::is_same_v<Key, int64_t> && std::is_same_v<Compare, std::less<Key>>) {
            return neon_lower_bound_s64(leaf->slots, leaf->count, key);
        }
        if constexpr (std::is_same_v<Key, uint64_t> && std::is_same_v<Compare, std::less<Key>>) {
            return neon_lower_bound_u64(leaf->slots, leaf->count, key);
        }
        if constexpr (std::is_same_v<Key, float> && std::is_same_v<Compare, std::less<Key>>) {
            return neon_lower_bound_float(leaf->slots, leaf->count, key);
        }
        if constexpr (std::is_same_v<Key, double> && std::is_same_v<Compare, std::less<Key>>) {
            return neon_lower_bound_double(leaf->slots, leaf->count, key);
        }
#endif
#endif
        // For string-like types, binary search is better because:
        // - Reduces expensive string comparisons from O(n) to O(log n)
        // - Sequential insert patterns benefit from fewer comparisons
        if constexpr (string_like<Key>) {
            return binary_search_in_slots(leaf->slots, leaf->count, key);
        }
        // Linear search is faster for small fixed-size types at typical node sizes
        // (sequential access, better cache prefetching, early exit)
        return linear_search_in_slots(leaf->slots, leaf->count, key);
    }

    // Specialized search for internal node
    [[nodiscard]] __attribute__((always_inline, flatten)) auto lower_bound_in_internal(
      const internal_node* __restrict__ internal, const Key& __restrict__ key) const noexcept -> size_type {
        // AVX-512 paths (highest priority - fastest)
#ifdef BTREE_HAS_AVX512
        if constexpr (std::is_same_v<Key, int64_t> && std::is_same_v<Compare, std::less<Key>>) {
            return avx512_lower_bound_s64(internal->slots, internal->count, key);
        }
        if constexpr (std::is_same_v<Key, uint64_t> && std::is_same_v<Compare, std::less<Key>>) {
            return avx512_lower_bound_u64(internal->slots, internal->count, key);
        }
        if constexpr (std::is_same_v<Key, int32_t> && std::is_same_v<Compare, std::less<Key>>) {
            return avx512_lower_bound_s32(internal->slots, internal->count, key);
        }
        if constexpr (std::is_same_v<Key, uint32_t> && std::is_same_v<Compare, std::less<Key>>) {
            return avx512_lower_bound_u32(internal->slots, internal->count, key);
        }
        if constexpr (std::is_same_v<Key, double> && std::is_same_v<Compare, std::less<Key>>) {
            return avx512_lower_bound_double(internal->slots, internal->count, key);
        }
        if constexpr (std::is_same_v<Key, float> && std::is_same_v<Compare, std::less<Key>>) {
            return avx512_lower_bound_float(internal->slots, internal->count, key);
        }
#endif
        // x86 SSE2/AVX2 paths
#ifdef BTREE_HAS_SSE2
        if constexpr (std::is_same_v<Key, int8_t> && std::is_same_v<Compare, std::less<Key>>) {
            return simd_lower_bound_s8(internal->slots, internal->count, key);
        }
        if constexpr (std::is_same_v<Key, uint8_t> && std::is_same_v<Compare, std::less<Key>>) {
            return simd_lower_bound_u8(internal->slots, internal->count, key);
        }
        if constexpr (std::is_same_v<Key, int16_t> && std::is_same_v<Compare, std::less<Key>>) {
            return simd_lower_bound_s16(internal->slots, internal->count, key);
        }
        if constexpr (std::is_same_v<Key, uint16_t> && std::is_same_v<Compare, std::less<Key>>) {
            return simd_lower_bound_u16(internal->slots, internal->count, key);
        }
#ifndef BTREE_HAS_AVX512  // Use SSE2 only if AVX-512 not available
        if constexpr (std::is_same_v<Key, int32_t> && std::is_same_v<Compare, std::less<Key>>) {
            return simd_lower_bound_s32(internal->slots, internal->count, key);
        }
        if constexpr (std::is_same_v<Key, uint32_t> && std::is_same_v<Compare, std::less<Key>>) {
            return simd_lower_bound_u32(internal->slots, internal->count, key);
        }
        if constexpr (std::is_same_v<Key, float> && std::is_same_v<Compare, std::less<Key>>) {
            return simd_lower_bound_float(internal->slots, internal->count, key);
        }
        if constexpr (std::is_same_v<Key, double> && std::is_same_v<Compare, std::less<Key>>) {
            return simd_lower_bound_double(internal->slots, internal->count, key);
        }
#endif
#endif
#ifdef BTREE_HAS_AVX2
#ifndef BTREE_HAS_AVX512  // Use AVX2 only if AVX-512 not available
        if constexpr (std::is_same_v<Key, int64_t> && std::is_same_v<Compare, std::less<Key>>) {
            return simd_lower_bound_s64(internal->slots, internal->count, key);
        }
        if constexpr (std::is_same_v<Key, uint64_t> && std::is_same_v<Compare, std::less<Key>>) {
            return simd_lower_bound_u64(internal->slots, internal->count, key);
        }
        if constexpr (std::is_same_v<Key, double> && std::is_same_v<Compare, std::less<Key>>) {
            return simd_lower_bound_double_avx(internal->slots, internal->count, key);
        }
#endif
#endif
        // ARM SVE paths (highest priority on ARM - scalable vectors)
#ifdef BTREE_HAS_SVE
        if constexpr (std::is_same_v<Key, int32_t> && std::is_same_v<Compare, std::less<Key>>) {
            return sve_lower_bound_s32(internal->slots, internal->count, key);
        }
        if constexpr (std::is_same_v<Key, uint32_t> && std::is_same_v<Compare, std::less<Key>>) {
            return sve_lower_bound_u32(internal->slots, internal->count, key);
        }
        if constexpr (std::is_same_v<Key, int64_t> && std::is_same_v<Compare, std::less<Key>>) {
            return sve_lower_bound_s64(internal->slots, internal->count, key);
        }
        if constexpr (std::is_same_v<Key, uint64_t> && std::is_same_v<Compare, std::less<Key>>) {
            return sve_lower_bound_u64(internal->slots, internal->count, key);
        }
        if constexpr (std::is_same_v<Key, float> && std::is_same_v<Compare, std::less<Key>>) {
            return sve_lower_bound_float(internal->slots, internal->count, key);
        }
        if constexpr (std::is_same_v<Key, double> && std::is_same_v<Compare, std::less<Key>>) {
            return sve_lower_bound_double(internal->slots, internal->count, key);
        }
#endif
        // ARM NEON paths (fallback for int8/int16 or when SVE not available)
#ifdef BTREE_HAS_NEON
        if constexpr (std::is_same_v<Key, int8_t> && std::is_same_v<Compare, std::less<Key>>) {
            return neon_lower_bound_s8(internal->slots, internal->count, key);
        }
        if constexpr (std::is_same_v<Key, uint8_t> && std::is_same_v<Compare, std::less<Key>>) {
            return neon_lower_bound_u8(internal->slots, internal->count, key);
        }
        if constexpr (std::is_same_v<Key, int16_t> && std::is_same_v<Compare, std::less<Key>>) {
            return neon_lower_bound_s16(internal->slots, internal->count, key);
        }
        if constexpr (std::is_same_v<Key, uint16_t> && std::is_same_v<Compare, std::less<Key>>) {
            return neon_lower_bound_u16(internal->slots, internal->count, key);
        }
#ifndef BTREE_HAS_SVE  // Use NEON only if SVE not available for these types
        if constexpr (std::is_same_v<Key, int32_t> && std::is_same_v<Compare, std::less<Key>>) {
            return neon_lower_bound_s32(internal->slots, internal->count, key);
        }
        if constexpr (std::is_same_v<Key, uint32_t> && std::is_same_v<Compare, std::less<Key>>) {
            return neon_lower_bound_u32(internal->slots, internal->count, key);
        }
        if constexpr (std::is_same_v<Key, int64_t> && std::is_same_v<Compare, std::less<Key>>) {
            return neon_lower_bound_s64(internal->slots, internal->count, key);
        }
        if constexpr (std::is_same_v<Key, uint64_t> && std::is_same_v<Compare, std::less<Key>>) {
            return neon_lower_bound_u64(internal->slots, internal->count, key);
        }
        if constexpr (std::is_same_v<Key, float> && std::is_same_v<Compare, std::less<Key>>) {
            return neon_lower_bound_float(internal->slots, internal->count, key);
        }
        if constexpr (std::is_same_v<Key, double> && std::is_same_v<Compare, std::less<Key>>) {
            return neon_lower_bound_double(internal->slots, internal->count, key);
        }
#endif
#endif
        // For string-like types, binary search reduces expensive comparisons
        if constexpr (string_like<Key>) {
            return binary_search_in_slots(internal->slots, internal->count, key);
        }
        // Linear search is faster for non-SIMD types at typical node sizes
        return linear_search_in_slots(internal->slots, internal->count, key);
    }

    // Generic search (for compatibility) - uses node type dispatch
    [[nodiscard]] auto lower_bound_in_node(const node_base* node, const Key& key) const noexcept -> size_type {
        if (node->is_leaf_node()) {
            return lower_bound_in_leaf(static_cast<const leaf_node*>(node), key);
        }
        return lower_bound_in_internal(static_cast<const internal_node*>(node), key);
    }

    // Create a new leaf node
    [[nodiscard]] auto create_leaf() -> leaf_node* { return new leaf_node(); }

    // Create a new internal node
    [[nodiscard]] auto create_internal() -> internal_node* { return new internal_node(); }

    // Destroy a node recursively
    void destroy_node(node_base* node) {
        if (node == nullptr) return;

        if (!node->is_leaf_node()) {
            auto* internal = static_cast<internal_node*>(node);
            for (size_type i = 0; i <= internal->count; ++i) {
                destroy_node(internal->children[i]);
            }
            delete internal;
        } else {
            delete static_cast<leaf_node*>(node);
        }
    }

    // Deep copy a node and all its children - O(n) tree copy
    [[nodiscard]] auto deep_copy_node(const node_base* src, node_base* parent) -> node_base* {
        if (src == nullptr) return nullptr;

        if (src->is_leaf_node()) {
            const auto* src_leaf = static_cast<const leaf_node*>(src);
            auto* dst_leaf = create_leaf();
            dst_leaf->parent = parent;
            dst_leaf->position = src_leaf->position;
            dst_leaf->count = src_leaf->count;

            // Copy all slots
            for (size_type i = 0; i < src_leaf->count; ++i) {
                dst_leaf->slots[i] = src_leaf->slots[i];
            }
            return dst_leaf;
        } else {
            const auto* src_internal = static_cast<const internal_node*>(src);
            auto* dst_internal = create_internal();
            dst_internal->parent = parent;
            dst_internal->position = src_internal->position;
            dst_internal->count = src_internal->count;

            // Copy all slots
            for (size_type i = 0; i < src_internal->count; ++i) {
                dst_internal->slots[i] = src_internal->slots[i];
            }

            // Recursively copy children
            for (size_type i = 0; i <= src_internal->count; ++i) {
                dst_internal->children[i] = deep_copy_node(src_internal->children[i], dst_internal);
            }
            return dst_internal;
        }
    }

    // Insert key-value into a leaf node at position (node must have space)
    // Uses perfect forwarding for efficient insertion of rvalues
    template <typename K, typename V>
    void insert_into_leaf(leaf_node* leaf, size_type pos, K&& key, V&& value) {
        size_type count = leaf->count;
        if (pos < count) {
            // Use memmove for trivially copyable types
            if constexpr (std::is_trivially_copyable_v<storage_type>) {
                std::memmove(&leaf->slots[pos + 1], &leaf->slots[pos], (count - pos) * sizeof(storage_type));
            } else {
                // Use std::move_backward for potentially better optimization
                std::move_backward(&leaf->slots[pos], &leaf->slots[count], &leaf->slots[count + 1]);
            }
        }
        leaf->slots[pos].first = std::forward<K>(key);
        leaf->slots[pos].second = std::forward<V>(value);
        ++leaf->count;
    }

    // Insert key-value-child into an internal node at position
    // Uses perfect forwarding for efficient insertion
    template <typename K, typename V>
    void insert_into_internal(internal_node* node, size_type pos, K&& key, V&& value, node_base* right_child) {
        size_type count = node->count;
        if (pos < count) {
            // Use memmove for trivially copyable types
            if constexpr (std::is_trivially_copyable_v<storage_type>) {
                std::memmove(&node->slots[pos + 1], &node->slots[pos], (count - pos) * sizeof(storage_type));
            } else {
                // Use std::move_backward for potentially better optimization
                std::move_backward(&node->slots[pos], &node->slots[count], &node->slots[count + 1]);
            }
            // Move children pointers
            std::memmove(&node->children[pos + 2], &node->children[pos + 1], (count - pos) * sizeof(node_base*));
            // Update positions for shifted children
            for (size_type i = pos + 2; i <= count + 1; ++i) {
                if (node->children[i]) {
                    node->children[i]->position = static_cast<uint16_t>(i);
                }
            }
        }
        node->slots[pos].first = std::forward<K>(key);
        node->slots[pos].second = std::forward<V>(value);
        node->children[pos + 1] = right_child;
        if (right_child) {
            right_child->parent = node;
            right_child->position = static_cast<uint16_t>(pos + 1);
        }
        ++node->count;
    }

    // Split a full leaf node, returns the new right node and the median key/value
    auto split_leaf(leaf_node* left) -> std::tuple<leaf_node*, Key, Value> {
        auto* right = create_leaf();
        size_type mid = left->count / 2;

        // Move upper half to right node
        size_type right_count = left->count - mid;
        for (size_type i = 0; i < right_count; ++i) {
            right->slots[i] = std::move(left->slots[mid + i]);
        }
        right->count = static_cast<uint16_t>(right_count);

        // Get the median (first key of right node for B+ tree style, but we use B-tree)
        // For B-tree, median goes up to parent
        Key median_key = right->slots[0].first;
        Value median_value = right->slots[0].second;

        // Shift right node to remove median
        for (size_type i = 0; i < right->count - 1; ++i) {
            right->slots[i] = std::move(right->slots[i + 1]);
        }
        --right->count;

        left->count = static_cast<uint16_t>(mid);

        return {right, std::move(median_key), std::move(median_value)};
    }

    // Split a full internal node
    auto split_internal(internal_node* left) -> std::tuple<internal_node*, Key, Value> {
        auto* right = create_internal();
        size_type mid = left->count / 2;

        // Median goes up
        Key median_key = std::move(left->slots[mid].first);
        Value median_value = std::move(left->slots[mid].second);

        // Move upper half to right node (excluding median)
        size_type right_count = left->count - mid - 1;
        for (size_type i = 0; i < right_count; ++i) {
            right->slots[i] = std::move(left->slots[mid + 1 + i]);
        }

        // Move children
        for (size_type i = 0; i <= right_count; ++i) {
            right->children[i] = left->children[mid + 1 + i];
            if (right->children[i]) {
                right->children[i]->parent = right;
                right->children[i]->position = static_cast<uint16_t>(i);
            }
            left->children[mid + 1 + i] = nullptr;
        }

        right->count = static_cast<uint16_t>(right_count);
        left->count = static_cast<uint16_t>(mid);

        return {right, std::move(median_key), std::move(median_value)};
    }

    // Insert and handle splits up the tree
    // Returns the (node, position) where the key-value was inserted (for leaf inserts only)
    // Uses perfect forwarding for efficient insertion
    template <typename K, typename V>
    auto insert_and_split_impl(node_base* node, size_type pos, K&& key, V&& value, node_base* right_child = nullptr)
      -> std::pair<node_base*, size_type> {
        if (node->is_leaf_node()) {
            auto* leaf = static_cast<leaf_node*>(node);
            if (!leaf->is_full()) {
                insert_into_leaf(leaf, pos, std::forward<K>(key), std::forward<V>(value));
                return {leaf, pos};
            }

            // Need to split - create new right node first
            auto* new_right = create_leaf();
            size_type mid = (leaf->count + 1) / 2;  // Include new element in count

            // Track where the element ends up
            node_base* inserted_node = nullptr;
            size_type inserted_pos = 0;

            // Determine which node the new element goes into
            Key median_key;
            Value median_value;

            if (pos < mid) {
                // New element goes to left
                // Median is the original element at position mid-1
                // Move elements [mid, count) to right, keep [0, mid-1) in left, median at mid-1

                // First, save the median (at position mid-1 before any modification)
                median_key = std::move(leaf->slots[mid - 1].first);
                median_value = std::move(leaf->slots[mid - 1].second);

                // Move elements [mid, count) to right
                if constexpr (std::is_trivially_copyable_v<storage_type>) {
                    std::memcpy(new_right->slots, &leaf->slots[mid], (leaf->count - mid) * sizeof(storage_type));
                } else {
                    for (size_type i = mid; i < leaf->count; ++i) {
                        new_right->slots[i - mid] = std::move(leaf->slots[i]);
                    }
                }
                new_right->count = static_cast<uint16_t>(leaf->count - mid);
                leaf->count = static_cast<uint16_t>(mid - 1);  // Left keeps [0, mid-2] = mid-1 elements

                // Insert new element into left
                insert_into_leaf(leaf, pos, std::forward<K>(key), std::forward<V>(value));
                inserted_node = leaf;
                inserted_pos = pos;
            } else if (pos == mid) {
                // New element IS the median - it goes to parent, not a leaf
#if BTREE_DEBUG
                std::cerr << "[DEBUG] pos==mid case: pos=" << pos << " mid=" << mid
                          << " key type=" << typeid(Key).name() << std::endl;
#endif
                // Move elements [mid, count) to right
                if constexpr (std::is_trivially_copyable_v<storage_type>) {
                    std::memcpy(new_right->slots, &leaf->slots[mid], (leaf->count - mid) * sizeof(storage_type));
                } else {
                    for (size_type i = mid; i < leaf->count; ++i) {
                        new_right->slots[i - mid] = std::move(leaf->slots[i]);
                    }
                }
                new_right->count = static_cast<uint16_t>(leaf->count - mid);
                leaf->count = static_cast<uint16_t>(mid);

                median_key = std::forward<K>(key);
                median_value = std::forward<V>(value);
#if BTREE_DEBUG
                std::cerr << "[DEBUG] median assigned, inserted_node=nullptr" << std::endl;
#endif
                // Element goes to internal node - we'll return the parent position later
                inserted_node = nullptr;  // Will be set after parent insert
                inserted_pos = 0;
            } else {
                // New element goes to right
                // Median is the original element at position mid

                // First, save the median (at position mid before any modification)
                median_key = std::move(leaf->slots[mid].first);
                median_value = std::move(leaf->slots[mid].second);

                // Move elements [mid+1, count) to right
                if constexpr (std::is_trivially_copyable_v<storage_type>) {
                    std::memcpy(new_right->slots, &leaf->slots[mid + 1],
                                (leaf->count - mid - 1) * sizeof(storage_type));
                } else {
                    for (size_type i = mid + 1; i < leaf->count; ++i) {
                        new_right->slots[i - mid - 1] = std::move(leaf->slots[i]);
                    }
                }
                new_right->count = static_cast<uint16_t>(leaf->count - mid - 1);
                leaf->count = static_cast<uint16_t>(mid);  // Left keeps [0, mid-1] = mid elements

                // Insert into right node
                size_type right_pos = pos - mid - 1;  // Position in right node
                insert_into_leaf(new_right, right_pos, std::forward<K>(key), std::forward<V>(value));
                inserted_node = new_right;
                inserted_pos = right_pos;
            }

            if (leaf->parent == nullptr) {
                // Create new root
                auto* new_root = create_internal();
                new_root->slots[0].first = std::move(median_key);
                new_root->slots[0].second = std::move(median_value);
                new_root->children[0] = leaf;
                new_root->children[1] = new_right;
                new_root->count = 1;
                leaf->parent = new_root;
                leaf->position = 0;
                new_right->parent = new_root;
                new_right->position = 1;
                _root = new_root;
                // If element was the median, it's now in the root
                if (inserted_node == nullptr) {
#if BTREE_DEBUG
                    std::cerr << "[DEBUG] Returning new_root, pos=0, slot[0].second='" << new_root->slots[0].second
                              << "'" << std::endl;
#endif
                    return {new_root, 0};
                }
            } else {
                // Insert median into parent
                auto* parent = static_cast<internal_node*>(leaf->parent);
                size_type parent_pos = leaf->position;
#if BTREE_DEBUG
                std::cerr << "[DEBUG] Inserting median into parent at pos=" << parent_pos << std::endl;
#endif
                auto [pnode, ppos] =
                  insert_and_split_impl(parent, parent_pos, std::move(median_key), std::move(median_value), new_right);
                // If element was the median, return the parent position
                if (inserted_node == nullptr) {
#if BTREE_DEBUG
                    std::cerr << "[DEBUG] Returning parent result: ppos=" << ppos << std::endl;
#endif
                    return {pnode, ppos};
                }
            }
#if BTREE_DEBUG
            std::cerr << "[DEBUG] Returning leaf result: node=" << (void*)inserted_node << " pos=" << inserted_pos
                      << std::endl;
#endif
            return {inserted_node, inserted_pos};
        } else {
            auto* internal = static_cast<internal_node*>(node);
            if (!internal->is_full()) {
                insert_into_internal(internal, pos, std::forward<K>(key), std::forward<V>(value), right_child);
                return {internal, pos};
            }

            // Need to split internal node
            auto* new_right = create_internal();
            size_type mid = (internal->count + 1) / 2;

            // Track where the element was inserted
            node_base* inserted_node = nullptr;
            size_type inserted_pos = 0;

            Key median_key;
            Value median_value;

            if (pos < mid) {
                // New element goes to left
                median_key = std::move(internal->slots[mid - 1].first);
                median_value = std::move(internal->slots[mid - 1].second);

                // Move [mid, count) to right with children
                for (size_type i = mid; i < internal->count; ++i) {
                    new_right->slots[i - mid] = std::move(internal->slots[i]);
                }
                for (size_type i = mid; i <= internal->count; ++i) {
                    new_right->children[i - mid] = internal->children[i];
                    if (new_right->children[i - mid]) {
                        new_right->children[i - mid]->parent = new_right;
                        new_right->children[i - mid]->position = static_cast<uint16_t>(i - mid);
                    }
                    internal->children[i] = nullptr;
                }
                new_right->count = static_cast<uint16_t>(internal->count - mid);
                internal->count = static_cast<uint16_t>(mid - 1);

                insert_into_internal(internal, pos, std::forward<K>(key), std::forward<V>(value), right_child);
                inserted_node = internal;
                inserted_pos = pos;
            } else if (pos == mid) {
                // New element IS the median
                median_key = std::forward<K>(key);
                median_value = std::forward<V>(value);

                // Move [mid, count) to right
                for (size_type i = mid; i < internal->count; ++i) {
                    new_right->slots[i - mid] = std::move(internal->slots[i]);
                }
                new_right->children[0] = right_child;
                if (right_child) {
                    right_child->parent = new_right;
                    right_child->position = 0;
                }
                for (size_type i = mid + 1; i <= internal->count; ++i) {
                    new_right->children[i - mid] = internal->children[i];
                    if (new_right->children[i - mid]) {
                        new_right->children[i - mid]->parent = new_right;
                        new_right->children[i - mid]->position = static_cast<uint16_t>(i - mid);
                    }
                    internal->children[i] = nullptr;
                }
                new_right->count = static_cast<uint16_t>(internal->count - mid);
                internal->count = static_cast<uint16_t>(mid);
                // Element goes to parent - will be set after parent insert
                inserted_node = nullptr;
            } else {
                // New element goes to right
                median_key = std::move(internal->slots[mid].first);
                median_value = std::move(internal->slots[mid].second);

                // Move [mid+1, count) to right
                for (size_type i = mid + 1; i < internal->count; ++i) {
                    new_right->slots[i - mid - 1] = std::move(internal->slots[i]);
                }
                for (size_type i = mid + 1; i <= internal->count; ++i) {
                    new_right->children[i - mid - 1] = internal->children[i];
                    if (new_right->children[i - mid - 1]) {
                        new_right->children[i - mid - 1]->parent = new_right;
                        new_right->children[i - mid - 1]->position = static_cast<uint16_t>(i - mid - 1);
                    }
                    internal->children[i] = nullptr;
                }
                new_right->count = static_cast<uint16_t>(internal->count - mid - 1);
                internal->count = static_cast<uint16_t>(mid);

                size_type right_pos = pos - mid - 1;
                insert_into_internal(new_right, right_pos, std::forward<K>(key), std::forward<V>(value), right_child);
                inserted_node = new_right;
                inserted_pos = right_pos;
            }

            if (internal->parent == nullptr) {
                // Create new root
                auto* new_root = create_internal();
                new_root->slots[0].first = std::move(median_key);
                new_root->slots[0].second = std::move(median_value);
                new_root->children[0] = internal;
                new_root->children[1] = new_right;
                new_root->count = 1;
                internal->parent = new_root;
                internal->position = 0;
                new_right->parent = new_root;
                new_right->position = 1;
                _root = new_root;
                if (inserted_node == nullptr) {
                    return {new_root, 0};
                }
            } else {
                auto* parent = static_cast<internal_node*>(internal->parent);
                size_type parent_pos = internal->position;
                auto [pnode, ppos] =
                  insert_and_split_impl(parent, parent_pos, std::move(median_key), std::move(median_value), new_right);
                if (inserted_node == nullptr) {
                    return {pnode, ppos};
                }
            }
            return {inserted_node, inserted_pos};
        }
    }

    // ==================== Deletion Helpers ====================

    // Remove slot at position from leaf node (shifts remaining elements)
    void remove_slot_from_leaf(leaf_node* leaf, size_type pos) {
        for (size_type i = pos; i < leaf->count - 1; ++i) {
            leaf->slots[i] = std::move(leaf->slots[i + 1]);
        }
        --leaf->count;
    }

    // Remove slot at position from internal node (shifts remaining elements and children)
    void remove_slot_from_internal(internal_node* node, size_type pos) {
        for (size_type i = pos; i < node->count - 1; ++i) {
            node->slots[i] = std::move(node->slots[i + 1]);
            node->children[i + 1] = node->children[i + 2];
            if (node->children[i + 1]) {
                node->children[i + 1]->position = static_cast<uint16_t>(i + 1);
            }
        }
        --node->count;
    }

    // Get the rightmost (maximum) key in subtree rooted at node
    auto get_predecessor(node_base* node) -> std::pair<node_base*, size_type> {
        while (!node->is_leaf_node()) {
            auto* internal = static_cast<internal_node*>(node);
            node = internal->children[internal->count];
        }
        auto* leaf = static_cast<leaf_node*>(node);
        return {leaf, leaf->count - 1};
    }

    // Get the leftmost (minimum) key in subtree rooted at node
    auto get_successor(node_base* node) -> std::pair<node_base*, size_type> {
        while (!node->is_leaf_node()) {
            auto* internal = static_cast<internal_node*>(node);
            node = internal->children[0];
        }
        return {node, 0};
    }

    // Check if leaf node can spare an element
    [[nodiscard]] auto can_spare_leaf(const leaf_node* leaf) const noexcept -> bool {
        return leaf->count > kMinLeafSlots;
    }

    // Check if internal node can spare an element
    [[nodiscard]] auto can_spare_internal(const internal_node* node) const noexcept -> bool {
        return node->count > kMinInternalSlots;
    }

    // Borrow from left sibling for leaf
    // In B-tree: parent separator moves down to leaf, left sibling's last key moves up to parent
    void borrow_from_left_leaf(leaf_node* leaf, leaf_node* left_sibling, internal_node* parent, size_type parent_pos) {
        // Shift leaf elements right to make room at position 0
        for (size_type i = leaf->count; i > 0; --i) {
            leaf->slots[i] = std::move(leaf->slots[i - 1]);
        }
        ++leaf->count;

        // Move parent separator down to leaf[0]
        leaf->slots[0] = std::move(parent->slots[parent_pos - 1]);

        // Move left sibling's last element up to parent
        parent->slots[parent_pos - 1] = std::move(left_sibling->slots[left_sibling->count - 1]);
        --left_sibling->count;
    }

    // Borrow from right sibling for leaf
    // In B-tree: parent separator moves down to leaf, right sibling's first key moves up to parent
    void borrow_from_right_leaf(leaf_node* leaf, leaf_node* right_sibling, internal_node* parent, size_type parent_pos) {
        // Move parent separator down to end of leaf
        leaf->slots[leaf->count] = std::move(parent->slots[parent_pos]);
        ++leaf->count;

        // Move right sibling's first element up to parent
        parent->slots[parent_pos] = std::move(right_sibling->slots[0]);

        // Shift right sibling elements left
        for (size_type i = 0; i < right_sibling->count - 1; ++i) {
            right_sibling->slots[i] = std::move(right_sibling->slots[i + 1]);
        }
        --right_sibling->count;
    }

    // Merge leaf with right sibling
    // In B-tree: parent separator moves down, then all right elements move to left
    void merge_leaves(leaf_node* left, leaf_node* right, internal_node* parent, size_type parent_pos) {
        // Move parent separator down to left leaf
        left->slots[left->count] = std::move(parent->slots[parent_pos]);
        ++left->count;

        // Move all elements from right to left
        for (size_type i = 0; i < right->count; ++i) {
            left->slots[left->count + i] = std::move(right->slots[i]);
        }
        left->count += right->count;

        // Remove separator from parent (this also removes children[parent_pos + 1])
        remove_slot_from_internal(parent, parent_pos);

        // Delete right node
        delete right;
    }

    // Borrow from left sibling for internal node
    void borrow_from_left_internal(internal_node* node, internal_node* left_sibling, internal_node* parent,
                                   size_type parent_pos) {
        // Shift node elements and children right
        node->children[node->count + 1] = node->children[node->count];
        if (node->children[node->count + 1]) {
            node->children[node->count + 1]->position = static_cast<uint16_t>(node->count + 1);
        }
        for (size_type i = node->count; i > 0; --i) {
            node->slots[i] = std::move(node->slots[i - 1]);
            node->children[i] = node->children[i - 1];
            if (node->children[i]) {
                node->children[i]->position = static_cast<uint16_t>(i);
            }
        }
        ++node->count;

        // Move parent separator down
        node->slots[0] = std::move(parent->slots[parent_pos - 1]);

        // Move child from left sibling
        node->children[0] = left_sibling->children[left_sibling->count];
        if (node->children[0]) {
            node->children[0]->parent = node;
            node->children[0]->position = 0;
        }

        // Move element from left sibling up to parent
        parent->slots[parent_pos - 1] = std::move(left_sibling->slots[left_sibling->count - 1]);
        left_sibling->children[left_sibling->count] = nullptr;
        --left_sibling->count;
    }

    // Borrow from right sibling for internal node
    void borrow_from_right_internal(internal_node* node, internal_node* right_sibling, internal_node* parent,
                                    size_type parent_pos) {
        // Move parent separator down
        node->slots[node->count] = std::move(parent->slots[parent_pos]);
        ++node->count;

        // Move child from right sibling
        node->children[node->count] = right_sibling->children[0];
        if (node->children[node->count]) {
            node->children[node->count]->parent = node;
            node->children[node->count]->position = static_cast<uint16_t>(node->count);
        }

        // Move element from right sibling up to parent
        parent->slots[parent_pos] = std::move(right_sibling->slots[0]);

        // Shift right sibling elements and children left
        for (size_type i = 0; i < right_sibling->count - 1; ++i) {
            right_sibling->slots[i] = std::move(right_sibling->slots[i + 1]);
            right_sibling->children[i] = right_sibling->children[i + 1];
            if (right_sibling->children[i]) {
                right_sibling->children[i]->position = static_cast<uint16_t>(i);
            }
        }
        right_sibling->children[right_sibling->count - 1] = right_sibling->children[right_sibling->count];
        if (right_sibling->children[right_sibling->count - 1]) {
            right_sibling->children[right_sibling->count - 1]->position =
              static_cast<uint16_t>(right_sibling->count - 1);
        }
        --right_sibling->count;
    }

    // Merge internal node with right sibling
    void merge_internal_nodes(internal_node* left, internal_node* right, internal_node* parent, size_type parent_pos) {
        // Move parent separator down
        left->slots[left->count] = std::move(parent->slots[parent_pos]);
        ++left->count;

        // Move all elements and children from right to left
        for (size_type i = 0; i < right->count; ++i) {
            left->slots[left->count + i] = std::move(right->slots[i]);
            left->children[left->count + i] = right->children[i];
            if (left->children[left->count + i]) {
                left->children[left->count + i]->parent = left;
                left->children[left->count + i]->position = static_cast<uint16_t>(left->count + i);
            }
        }
        left->children[left->count + right->count] = right->children[right->count];
        if (left->children[left->count + right->count]) {
            left->children[left->count + right->count]->parent = left;
            left->children[left->count + right->count]->position = static_cast<uint16_t>(left->count + right->count);
        }
        left->count += right->count;

        // Remove separator from parent
        remove_slot_from_internal(parent, parent_pos);

        // Delete right node
        delete right;
    }

    // Rebalance after deletion - handles underflow
    void rebalance_after_erase(node_base* node) {
        while (node != _root) {
            auto* parent = static_cast<internal_node*>(node->parent);
            size_type pos = node->position;

            bool is_leaf = node->is_leaf_node();
            size_type min_slots = is_leaf ? kMinLeafSlots : kMinInternalSlots;

            if (node->count >= min_slots) {
                return;  // No underflow
            }

            // Try to borrow from left sibling
            if (pos > 0) {
                node_base* left_sibling = parent->children[pos - 1];
                if (is_leaf) {
                    auto* left_leaf = static_cast<leaf_node*>(left_sibling);
                    if (can_spare_leaf(left_leaf)) {
                        borrow_from_left_leaf(static_cast<leaf_node*>(node), left_leaf, parent, pos);
                        return;
                    }
                } else {
                    auto* left_internal = static_cast<internal_node*>(left_sibling);
                    if (can_spare_internal(left_internal)) {
                        borrow_from_left_internal(static_cast<internal_node*>(node), left_internal, parent, pos);
                        return;
                    }
                }
            }

            // Try to borrow from right sibling
            if (pos < parent->count) {
                node_base* right_sibling = parent->children[pos + 1];
                if (is_leaf) {
                    auto* right_leaf = static_cast<leaf_node*>(right_sibling);
                    if (can_spare_leaf(right_leaf)) {
                        borrow_from_right_leaf(static_cast<leaf_node*>(node), right_leaf, parent, pos);
                        return;
                    }
                } else {
                    auto* right_internal = static_cast<internal_node*>(right_sibling);
                    if (can_spare_internal(right_internal)) {
                        borrow_from_right_internal(static_cast<internal_node*>(node), right_internal, parent, pos);
                        return;
                    }
                }
            }

            // Must merge - prefer merging with left sibling
            if (pos > 0) {
                node_base* left_sibling = parent->children[pos - 1];
                if (is_leaf) {
                    merge_leaves(static_cast<leaf_node*>(left_sibling), static_cast<leaf_node*>(node), parent, pos - 1);
                } else {
                    merge_internal_nodes(static_cast<internal_node*>(left_sibling), static_cast<internal_node*>(node),
                                         parent, pos - 1);
                }
            } else {
                // Merge with right sibling
                node_base* right_sibling = parent->children[pos + 1];
                if (is_leaf) {
                    merge_leaves(static_cast<leaf_node*>(node), static_cast<leaf_node*>(right_sibling), parent, pos);
                } else {
                    merge_internal_nodes(static_cast<internal_node*>(node), static_cast<internal_node*>(right_sibling),
                                         parent, pos);
                }
            }

            // Check if parent needs rebalancing
            if (parent == _root && parent->count == 0) {
                // Root became empty, promote the merged child as new root
                _root = parent->children[0];
                if (_root) {
                    _root->parent = nullptr;
                }
                delete parent;
                return;
            }

            node = parent;
        }
    }

    // Main erase implementation
    void erase_impl(node_base* node, size_type pos) {
        if (node->is_leaf_node()) {
            // Simple case: remove from leaf
            auto* leaf = static_cast<leaf_node*>(node);
            remove_slot_from_leaf(leaf, pos);
            --_size;

            // Handle root leaf becoming empty
            if (leaf == _root && leaf->count == 0) {
                delete leaf;
                _root = nullptr;
                return;
            }

            // Rebalance if needed
            if (leaf != _root) {
                rebalance_after_erase(leaf);
            }
        } else {
            // Internal node: replace with predecessor and delete predecessor
            auto* internal = static_cast<internal_node*>(node);
            auto [pred_node, pred_pos] = get_predecessor(internal->children[pos]);
            auto* pred_leaf = static_cast<leaf_node*>(pred_node);

            // Replace key-value with predecessor
            internal->slots[pos] = std::move(pred_leaf->slots[pred_pos]);

            // Remove predecessor from leaf
            remove_slot_from_leaf(pred_leaf, pred_pos);
            --_size;

            // Rebalance predecessor's leaf if needed
            if (pred_leaf != _root) {
                rebalance_after_erase(pred_leaf);
            }
        }
    }

    // ==================== End Deletion Helpers ====================

    // Find the leftmost leaf
    [[nodiscard]] auto leftmost_leaf() const noexcept -> const leaf_node* {
        if (_root == nullptr) return nullptr;
        const node_base* node = _root;
        while (!node->is_leaf_node()) {
            node = static_cast<const internal_node*>(node)->children[0];
        }
        return static_cast<const leaf_node*>(node);
    }

    [[nodiscard]] auto leftmost_leaf() noexcept -> leaf_node* {
        return const_cast<leaf_node*>(static_cast<const btree_map*>(this)->leftmost_leaf());
    }

    // Find the rightmost leaf
    [[nodiscard]] auto rightmost_leaf() const noexcept -> const leaf_node* {
        if (_root == nullptr) return nullptr;
        const node_base* node = _root;
        while (!node->is_leaf_node()) {
            const auto* internal = static_cast<const internal_node*>(node);
            node = internal->children[internal->count];
        }
        return static_cast<const leaf_node*>(node);
    }

   public:
    // Iterator class
    class iterator
    {
       public:
        using iterator_category = std::bidirectional_iterator_tag;
        using difference_type = std::ptrdiff_t;
        using value_type = std::pair<const Key, Value>;
        using pointer = value_type*;
        using reference = value_type&;

       private:
        node_base* _node = nullptr;
        size_type _pos = 0;

        friend class btree_map;

        iterator(node_base* node, size_type pos) : _node(node), _pos(pos) {}

        // Move to next position in tree
        void increment() {
            if (_node == nullptr) return;

            if (_node->is_leaf_node()) {
                auto* leaf = static_cast<leaf_node*>(_node);
                ++_pos;
                if (_pos < leaf->count) {
                    return;  // More elements in current leaf
                }

                // Move to parent and find next
                while (_node->parent != nullptr) {
                    size_type parent_pos = _node->position;
                    _node = _node->parent;
                    if (parent_pos < _node->count) {
                        // Found next key in parent
                        _pos = parent_pos;
                        return;
                    }
                }
                // End of tree
                _node = nullptr;
                _pos = 0;
            } else {
                // Internal node: go to next subtree's leftmost leaf
                auto* internal = static_cast<internal_node*>(_node);
                node_base* child = internal->children[_pos + 1];
                while (!child->is_leaf_node()) {
                    child = static_cast<internal_node*>(child)->children[0];
                }
                _node = child;
                _pos = 0;
            }
        }

        // Move to previous position in tree
        void decrement() {
            if (_node == nullptr) return;

            if (_node->is_leaf_node()) {
                if (_pos > 0) {
                    --_pos;
                    return;  // Previous element in current leaf
                }

                // Move to parent and find previous
                while (_node->parent != nullptr) {
                    size_type child_pos = _node->position;
                    _node = _node->parent;
                    if (child_pos > 0) {
                        // Go to the rightmost element of the left subtree
                        auto* internal = static_cast<internal_node*>(_node);
                        _pos = child_pos - 1;
                        // Check if parent key or need to descend
                        node_base* child = internal->children[child_pos];
                        // We came from child at child_pos, so previous is either parent[child_pos-1]
                        // or the rightmost in children[child_pos-1] subtree
                        // Actually, for a B-tree, we return to parent key at pos = child_pos - 1
                        return;
                    }
                }
                // Beginning of tree - undefined behavior, but set to invalid
                _node = nullptr;
                _pos = 0;
            } else {
                // Internal node: go to previous subtree's rightmost leaf
                auto* internal = static_cast<internal_node*>(_node);
                node_base* child = internal->children[_pos];
                while (!child->is_leaf_node()) {
                    auto* ichild = static_cast<internal_node*>(child);
                    child = ichild->children[ichild->count];
                }
                _node = child;
                _pos = static_cast<leaf_node*>(child)->count - 1;
            }
        }

       public:
        iterator() = default;

        [[nodiscard]] auto operator*() const -> reference {
            if (_node->is_leaf_node()) {
                auto* leaf = static_cast<leaf_node*>(_node);
                return reinterpret_cast<reference>(leaf->slots[_pos]);
            }
            auto* internal = static_cast<internal_node*>(_node);
            return reinterpret_cast<reference>(internal->slots[_pos]);
        }

        [[nodiscard]] auto operator->() const -> pointer { return &**this; }

        auto operator++() -> iterator& {
            increment();
            return *this;
        }

        auto operator++(int) -> iterator {
            auto tmp = *this;
            increment();
            return tmp;
        }

        auto operator--() -> iterator& {
            decrement();
            return *this;
        }

        auto operator--(int) -> iterator {
            auto tmp = *this;
            decrement();
            return tmp;
        }

        friend auto operator==(const iterator& a, const iterator& b) -> bool {
            return a._node == b._node && a._pos == b._pos;
        }

        friend auto operator!=(const iterator& a, const iterator& b) -> bool { return !(a == b); }
    };

    class const_iterator
    {
       public:
        using iterator_category = std::bidirectional_iterator_tag;
        using difference_type = std::ptrdiff_t;
        using value_type = const std::pair<const Key, Value>;
        using pointer = const value_type*;
        using reference = const value_type&;

       private:
        const node_base* _node = nullptr;
        size_type _pos = 0;

        friend class btree_map;

        const_iterator(const node_base* node, size_type pos) : _node(node), _pos(pos) {}

        void increment() {
            if (_node == nullptr) return;

            if (_node->is_leaf_node()) {
                auto* leaf = static_cast<const leaf_node*>(_node);
                ++_pos;
                if (_pos < leaf->count) {
                    return;
                }

                while (_node->parent != nullptr) {
                    size_type parent_pos = _node->position;
                    _node = _node->parent;
                    if (parent_pos < _node->count) {
                        _pos = parent_pos;
                        return;
                    }
                }
                _node = nullptr;
                _pos = 0;
            } else {
                auto* internal = static_cast<const internal_node*>(_node);
                const node_base* child = internal->children[_pos + 1];
                while (!child->is_leaf_node()) {
                    child = static_cast<const internal_node*>(child)->children[0];
                }
                _node = child;
                _pos = 0;
            }
        }

        void decrement() {
            if (_node == nullptr) return;

            if (_node->is_leaf_node()) {
                if (_pos > 0) {
                    --_pos;
                    return;
                }

                while (_node->parent != nullptr) {
                    size_type child_pos = _node->position;
                    _node = _node->parent;
                    if (child_pos > 0) {
                        _pos = child_pos - 1;
                        return;
                    }
                }
                _node = nullptr;
                _pos = 0;
            } else {
                auto* internal = static_cast<const internal_node*>(_node);
                const node_base* child = internal->children[_pos];
                while (!child->is_leaf_node()) {
                    auto* ichild = static_cast<const internal_node*>(child);
                    child = ichild->children[ichild->count];
                }
                _node = child;
                _pos = static_cast<const leaf_node*>(child)->count - 1;
            }
        }

       public:
        const_iterator() = default;
        const_iterator(const iterator& it) : _node(it._node), _pos(it._pos) {}

        [[nodiscard]] auto operator*() const -> reference {
            if (_node->is_leaf_node()) {
                auto* leaf = static_cast<const leaf_node*>(_node);
                return reinterpret_cast<reference>(leaf->slots[_pos]);
            }
            auto* internal = static_cast<const internal_node*>(_node);
            return reinterpret_cast<reference>(internal->slots[_pos]);
        }

        [[nodiscard]] auto operator->() const -> pointer { return &**this; }

        auto operator++() -> const_iterator& {
            increment();
            return *this;
        }

        auto operator++(int) -> const_iterator {
            auto tmp = *this;
            increment();
            return tmp;
        }

        auto operator--() -> const_iterator& {
            decrement();
            return *this;
        }

        auto operator--(int) -> const_iterator {
            auto tmp = *this;
            decrement();
            return tmp;
        }

        friend auto operator==(const const_iterator& a, const const_iterator& b) -> bool {
            return a._node == b._node && a._pos == b._pos;
        }

        friend auto operator!=(const const_iterator& a, const const_iterator& b) -> bool { return !(a == b); }
    };

    // Custom reverse iterator (std::reverse_iterator doesn't work with our end() iterator)
    class reverse_iterator
    {
       public:
        using iterator_category = std::bidirectional_iterator_tag;
        using difference_type = std::ptrdiff_t;
        using value_type = std::pair<const Key, Value>;
        using pointer = value_type*;
        using reference = value_type&;

       private:
        node_base* _node = nullptr;
        size_type _pos = 0;
        bool _is_end = true;  // True when pointing before begin (reverse end)

        friend class btree_map;

        reverse_iterator(node_base* node, size_type pos, bool is_end = false)
            : _node(node), _pos(pos), _is_end(is_end) {}

       public:
        reverse_iterator() = default;

        [[nodiscard]] auto operator*() const -> reference {
            if (_node->is_leaf_node()) {
                return reinterpret_cast<reference>(static_cast<leaf_node*>(_node)->slots[_pos]);
            }
            return reinterpret_cast<reference>(static_cast<internal_node*>(_node)->slots[_pos]);
        }

        [[nodiscard]] auto operator->() const -> pointer { return &**this; }

        auto operator++() -> reverse_iterator& {
            // Increment in reverse = decrement in forward
            if (_node == nullptr) return *this;
            if (_node->is_leaf_node()) {
                if (_pos > 0) {
                    --_pos;
                    return *this;
                }
                while (_node->parent != nullptr) {
                    size_type child_pos = _node->position;
                    _node = _node->parent;
                    if (child_pos > 0) {
                        _pos = child_pos - 1;
                        return *this;
                    }
                }
                _is_end = true;
                return *this;
            } else {
                auto* internal = static_cast<internal_node*>(_node);
                node_base* child = internal->children[_pos];
                while (!child->is_leaf_node()) {
                    auto* ichild = static_cast<internal_node*>(child);
                    child = ichild->children[ichild->count];
                }
                _node = child;
                _pos = static_cast<leaf_node*>(child)->count - 1;
            }
            return *this;
        }

        auto operator++(int) -> reverse_iterator {
            auto tmp = *this;
            ++*this;
            return tmp;
        }

        auto operator--() -> reverse_iterator& {
            // Decrement in reverse = increment in forward
            if (_node == nullptr) return *this;
            if (_node->is_leaf_node()) {
                auto* leaf = static_cast<leaf_node*>(_node);
                ++_pos;
                if (_pos < leaf->count) {
                    _is_end = false;
                    return *this;
                }
                while (_node->parent != nullptr) {
                    size_type parent_pos = _node->position;
                    _node = _node->parent;
                    if (parent_pos < _node->count) {
                        _pos = parent_pos;
                        _is_end = false;
                        return *this;
                    }
                }
                _node = nullptr;
                _pos = 0;
            } else {
                auto* internal = static_cast<internal_node*>(_node);
                node_base* child = internal->children[_pos + 1];
                while (!child->is_leaf_node()) {
                    child = static_cast<internal_node*>(child)->children[0];
                }
                _node = child;
                _pos = 0;
                _is_end = false;
            }
            return *this;
        }

        auto operator--(int) -> reverse_iterator {
            auto tmp = *this;
            --*this;
            return tmp;
        }

        [[nodiscard]] auto base() const -> iterator { return iterator(_node, _pos); }

        friend auto operator==(const reverse_iterator& a, const reverse_iterator& b) -> bool {
            if (a._is_end && b._is_end) return true;
            if (a._is_end || b._is_end) return false;
            return a._node == b._node && a._pos == b._pos;
        }

        friend auto operator!=(const reverse_iterator& a, const reverse_iterator& b) -> bool { return !(a == b); }
    };

    class const_reverse_iterator
    {
       public:
        using iterator_category = std::bidirectional_iterator_tag;
        using difference_type = std::ptrdiff_t;
        using value_type = const std::pair<const Key, Value>;
        using pointer = const value_type*;
        using reference = const value_type&;

       private:
        const node_base* _node = nullptr;
        size_type _pos = 0;
        bool _is_end = true;

        friend class btree_map;

        const_reverse_iterator(const node_base* node, size_type pos, bool is_end = false)
            : _node(node), _pos(pos), _is_end(is_end) {}

       public:
        const_reverse_iterator() = default;
        const_reverse_iterator(const reverse_iterator& it) : _node(it._node), _pos(it._pos), _is_end(it._is_end) {}

        [[nodiscard]] auto operator*() const -> reference {
            if (_node->is_leaf_node()) {
                return reinterpret_cast<reference>(static_cast<const leaf_node*>(_node)->slots[_pos]);
            }
            return reinterpret_cast<reference>(static_cast<const internal_node*>(_node)->slots[_pos]);
        }

        [[nodiscard]] auto operator->() const -> pointer { return &**this; }

        auto operator++() -> const_reverse_iterator& {
            if (_node == nullptr) return *this;
            if (_node->is_leaf_node()) {
                if (_pos > 0) {
                    --_pos;
                    return *this;
                }
                while (_node->parent != nullptr) {
                    size_type child_pos = _node->position;
                    _node = _node->parent;
                    if (child_pos > 0) {
                        _pos = child_pos - 1;
                        return *this;
                    }
                }
                _is_end = true;
                return *this;
            } else {
                auto* internal = static_cast<const internal_node*>(_node);
                const node_base* child = internal->children[_pos];
                while (!child->is_leaf_node()) {
                    auto* ichild = static_cast<const internal_node*>(child);
                    child = ichild->children[ichild->count];
                }
                _node = child;
                _pos = static_cast<const leaf_node*>(child)->count - 1;
            }
            return *this;
        }

        auto operator++(int) -> const_reverse_iterator {
            auto tmp = *this;
            ++*this;
            return tmp;
        }

        auto operator--() -> const_reverse_iterator& {
            if (_node == nullptr) return *this;
            if (_node->is_leaf_node()) {
                auto* leaf = static_cast<const leaf_node*>(_node);
                ++_pos;
                if (_pos < leaf->count) {
                    _is_end = false;
                    return *this;
                }
                while (_node->parent != nullptr) {
                    size_type parent_pos = _node->position;
                    _node = _node->parent;
                    if (parent_pos < _node->count) {
                        _pos = parent_pos;
                        _is_end = false;
                        return *this;
                    }
                }
                _node = nullptr;
                _pos = 0;
            } else {
                auto* internal = static_cast<const internal_node*>(_node);
                const node_base* child = internal->children[_pos + 1];
                while (!child->is_leaf_node()) {
                    child = static_cast<const internal_node*>(child)->children[0];
                }
                _node = child;
                _pos = 0;
                _is_end = false;
            }
            return *this;
        }

        auto operator--(int) -> const_reverse_iterator {
            auto tmp = *this;
            --*this;
            return tmp;
        }

        [[nodiscard]] auto base() const -> const_iterator { return const_iterator(_node, _pos); }

        friend auto operator==(const const_reverse_iterator& a, const const_reverse_iterator& b) -> bool {
            if (a._is_end && b._is_end) return true;
            if (a._is_end || b._is_end) return false;
            return a._node == b._node && a._pos == b._pos;
        }

        friend auto operator!=(const const_reverse_iterator& a, const const_reverse_iterator& b) -> bool {
            return !(a == b);
        }
    };

    // Constructors
    btree_map() = default;

    explicit btree_map(const Compare& comp) : _comp(comp) {}

    btree_map(std::initializer_list<value_type> init, const Compare& comp = Compare()) : _comp(comp) {
        for (const auto& [k, v] : init) {
            insert(k, v);
        }
    }

    ~btree_map() { destroy_node(_root); }

    // Copy constructor - O(n) deep copy of tree structure
    btree_map(const btree_map& other) : _comp(other._comp), _size(other._size) {
        _root = deep_copy_node(other._root, nullptr);
    }

    // Move constructor
    btree_map(btree_map&& other) noexcept : _root(other._root), _size(other._size), _comp(std::move(other._comp)) {
        other._root = nullptr;
        other._size = 0;
    }

    // Copy assignment - O(n) deep copy of tree structure
    auto operator=(const btree_map& other) -> btree_map& {
        if (this != &other) {
            destroy_node(_root);
            _comp = other._comp;
            _size = other._size;
            _root = deep_copy_node(other._root, nullptr);
        }
        return *this;
    }

    // Move assignment
    auto operator=(btree_map&& other) noexcept -> btree_map& {
        if (this != &other) {
            destroy_node(_root);
            _root = other._root;
            _size = other._size;
            _comp = std::move(other._comp);
            other._root = nullptr;
            other._size = 0;
        }
        return *this;
    }

    // Capacity
    [[nodiscard]] auto empty() const noexcept -> bool { return _size == 0; }
    [[nodiscard]] auto size() const noexcept -> size_type { return _size; }

    // Clear all elements
    void clear() noexcept {
        destroy_node(_root);
        _root = nullptr;
        _size = 0;
    }

    // Insert a key-value pair (const lvalue version)
    auto insert(const Key& key, const Value& value) -> std::pair<iterator, bool> { return insert_impl(key, value); }

    // Insert a key-value pair (rvalue version for value - avoids copy)
    auto insert(const Key& key, Value&& value) -> std::pair<iterator, bool> {
        return insert_impl(key, std::move(value));
    }

    // Insert with value_type
    auto insert(const value_type& kv) -> std::pair<iterator, bool> { return insert(kv.first, kv.second); }

    auto insert(value_type&& kv) -> std::pair<iterator, bool> {
        return insert_impl(std::move(kv.first), std::move(kv.second));
    }

    // Insert with hint - uses hint for O(1) amortized insertion for sequential patterns
    // Hint is used for optimization; correctness is guaranteed even if hint is wrong
    auto insert(const_iterator hint, const value_type& kv) -> iterator {
        return insert_with_hint_impl(hint, kv.first, kv.second);
    }

    auto insert(const_iterator hint, value_type&& kv) -> iterator {
        return insert_with_hint_impl(hint, std::move(kv.first), std::move(kv.second));
    }

    // Emplace - construct in place
    template <typename... Args>
    auto emplace(Args&&... args) -> std::pair<iterator, bool> {
        value_type val(std::forward<Args>(args)...);
        return insert_impl(std::move(val.first), std::move(val.second));
    }

    // emplace_hint - uses hint for O(1) amortized insertion for sequential patterns (C++11)
    template <typename... Args>
    auto emplace_hint(const_iterator hint, Args&&... args) -> iterator {
        value_type val(std::forward<Args>(args)...);
        return insert_with_hint_impl(hint, std::move(val.first), std::move(val.second));
    }

    // insert_or_assign - inserts or updates value (C++17)
    // Single traversal implementation for optimal performance
    template <typename M>
    auto insert_or_assign(const Key& key, M&& value) -> std::pair<iterator, bool> {
        return insert_or_assign_impl(key, std::forward<M>(value));
    }

    template <typename M>
    auto insert_or_assign(Key&& key, M&& value) -> std::pair<iterator, bool> {
        return insert_or_assign_impl(std::move(key), std::forward<M>(value));
    }

    // insert_or_assign with hint (hint is ignored)
    template <typename M>
    auto insert_or_assign([[maybe_unused]] const_iterator hint, const Key& key, M&& value) -> iterator {
        return insert_or_assign(key, std::forward<M>(value)).first;
    }

    template <typename M>
    auto insert_or_assign([[maybe_unused]] const_iterator hint, Key&& key, M&& value) -> iterator {
        return insert_or_assign(std::move(key), std::forward<M>(value)).first;
    }

    // try_emplace - only constructs value if key doesn't exist (C++17)
    // Single traversal: finds position and inserts in one pass
    template <typename... Args>
    auto try_emplace(const Key& key, Args&&... args) -> std::pair<iterator, bool> {
        return try_emplace_impl(key, std::forward<Args>(args)...);
    }

    template <typename... Args>
    auto try_emplace(Key&& key, Args&&... args) -> std::pair<iterator, bool> {
        return try_emplace_impl(std::move(key), std::forward<Args>(args)...);
    }

    // try_emplace with hint (hint is ignored)
    template <typename... Args>
    auto try_emplace([[maybe_unused]] const_iterator hint, const Key& key, Args&&... args) -> iterator {
        return try_emplace(key, std::forward<Args>(args)...).first;
    }

    template <typename... Args>
    auto try_emplace([[maybe_unused]] const_iterator hint, Key&& key, Args&&... args) -> iterator {
        return try_emplace(std::move(key), std::forward<Args>(args)...).first;
    }

   private:
    // Internal insert implementation with perfect forwarding
    template <typename K, typename V>
    __attribute__((hot)) auto insert_impl(K&& key, V&& value) -> std::pair<iterator, bool> {
        if (_root == nullptr) [[unlikely]] {
            _root = create_leaf();
            auto* leaf = static_cast<leaf_node*>(_root);
            leaf->slots[0] = storage_type(std::forward<K>(key), std::forward<V>(value));
            leaf->count = 1;
            ++_size;
            return {iterator(leaf, 0), true};
        }

        // Fast path: check if key > max key (sequential append case)
        // Only for cheap-to-compare types - strings have expensive comparison
        if constexpr (!string_like<Key>) {
            auto* right_leaf = const_cast<leaf_node*>(rightmost_leaf());
            if (right_leaf->count > 0 && _comp(right_leaf->key(right_leaf->count - 1), key)) {
                // Key is greater than all existing keys - append to rightmost leaf
                auto [inserted_node, inserted_pos] =
                  insert_and_split_impl(right_leaf, right_leaf->count, std::forward<K>(key), std::forward<V>(value));
                ++_size;
                return {iterator(inserted_node, inserted_pos), true};
            }
        }

        // Find insertion point - traverse to leaf using specialized search
        node_base* node = _root;
        while (!node->is_leaf_node()) [[likely]] {
            auto* internal = static_cast<internal_node*>(node);
            size_type pos = lower_bound_in_internal(internal, key);

            // Check for exact match (single comparison optimization)
            if (pos < internal->count && !_comp(key, internal->key(pos))) [[unlikely]] {
                return {iterator(node, pos), false};  // Key already exists
            }

            // Prefetch next node before traversing
            __builtin_prefetch(internal->children[pos], 0, 3);
            node = internal->children[pos];
        }

        // Now at leaf - use specialized search
        auto* leaf = static_cast<leaf_node*>(node);
        size_type pos = lower_bound_in_leaf(leaf, key);

        // Check for exact match
        if (pos < leaf->count && !_comp(key, leaf->key(pos))) [[unlikely]] {
            return {iterator(leaf, pos), false};  // Key already exists
        }

        // Insert and get the position where it was inserted
        auto [inserted_node, inserted_pos] =
          insert_and_split_impl(leaf, pos, std::forward<K>(key), std::forward<V>(value));
        ++_size;

        return {iterator(inserted_node, inserted_pos), true};
    }

    // try_emplace implementation - single traversal, constructs value only if needed
    template <typename K, typename... Args>
    __attribute__((hot)) auto try_emplace_impl(K&& key, Args&&... args) -> std::pair<iterator, bool> {
        if (_root == nullptr) [[unlikely]] {
            _root = create_leaf();
            auto* leaf = static_cast<leaf_node*>(_root);
            leaf->slots[0] = storage_type(std::forward<K>(key), Value(std::forward<Args>(args)...));
            leaf->count = 1;
            ++_size;
            return {iterator(leaf, 0), true};
        }

        // Fast path: check if key > max key (sequential append case)
        // Only for cheap-to-compare types - strings have expensive comparison
        if constexpr (!string_like<Key>) {
            auto* right_leaf = const_cast<leaf_node*>(rightmost_leaf());
            if (right_leaf->count > 0 && _comp(right_leaf->key(right_leaf->count - 1), key)) {
                // Key is greater than all existing keys - append to rightmost leaf
                auto [inserted_node, inserted_pos] = insert_and_split_impl(
                  right_leaf, right_leaf->count, std::forward<K>(key), Value(std::forward<Args>(args)...));
                ++_size;
                return {iterator(inserted_node, inserted_pos), true};
            }
        }

        // Find insertion point - traverse to leaf
        node_base* node = _root;
        while (!node->is_leaf_node()) [[likely]] {
            auto* internal = static_cast<internal_node*>(node);
            size_type pos = lower_bound_in_internal(internal, key);

            if (pos < internal->count && !_comp(key, internal->key(pos))) [[unlikely]] {
                return {iterator(node, pos), false};  // Key exists
            }

            // Prefetch next node before traversing
            __builtin_prefetch(internal->children[pos], 0, 3);
            node = internal->children[pos];
        }

        auto* leaf = static_cast<leaf_node*>(node);
        size_type pos = lower_bound_in_leaf(leaf, key);

        if (pos < leaf->count && !_comp(key, leaf->key(pos))) [[unlikely]] {
            return {iterator(leaf, pos), false};  // Key exists
        }

        // Key doesn't exist - now construct value and insert
        auto [inserted_node, inserted_pos] =
          insert_and_split_impl(leaf, pos, std::forward<K>(key), Value(std::forward<Args>(args)...));
        ++_size;

        return {iterator(inserted_node, inserted_pos), true};
    }

    // insert_or_assign implementation - single traversal, updates if exists, inserts if not
    template <typename K, typename V>
    __attribute__((hot)) auto insert_or_assign_impl(K&& key, V&& value) -> std::pair<iterator, bool> {
        if (_root == nullptr) [[unlikely]] {
            _root = create_leaf();
            auto* leaf = static_cast<leaf_node*>(_root);
            leaf->slots[0] = storage_type(std::forward<K>(key), std::forward<V>(value));
            leaf->count = 1;
            ++_size;
            return {iterator(leaf, 0), true};
        }

        // Fast path: check if key > max key (sequential append case)
        // Only for cheap-to-compare types - strings have expensive comparison
        if constexpr (!string_like<Key>) {
            auto* right_leaf = const_cast<leaf_node*>(rightmost_leaf());
            if (right_leaf->count > 0 && _comp(right_leaf->key(right_leaf->count - 1), key)) {
                // Key is greater than all existing keys - append to rightmost leaf
                auto [inserted_node, inserted_pos] =
                  insert_and_split_impl(right_leaf, right_leaf->count, std::forward<K>(key), std::forward<V>(value));
                ++_size;
                return {iterator(inserted_node, inserted_pos), true};
            }
        }

        // Find insertion point - traverse to leaf
        node_base* node = _root;
        while (!node->is_leaf_node()) [[likely]] {
            auto* internal = static_cast<internal_node*>(node);
            size_type pos = lower_bound_in_internal(internal, key);

            if (pos < internal->count && !_comp(key, internal->key(pos))) [[unlikely]] {
                // Key exists in internal node - update value
                internal->value(pos) = std::forward<V>(value);
                return {iterator(node, pos), false};
            }

            __builtin_prefetch(internal->children[pos], 0, 3);
            node = internal->children[pos];
        }

        auto* leaf = static_cast<leaf_node*>(node);
        size_type pos = lower_bound_in_leaf(leaf, key);

        if (pos < leaf->count && !_comp(key, leaf->key(pos))) [[unlikely]] {
            // Key exists in leaf - update value
            leaf->value(pos) = std::forward<V>(value);
            return {iterator(leaf, pos), false};
        }

        // Key doesn't exist - insert
        auto [inserted_node, inserted_pos] =
          insert_and_split_impl(leaf, pos, std::forward<K>(key), std::forward<V>(value));
        ++_size;

        return {iterator(inserted_node, inserted_pos), true};
    }

    // Insert with hint implementation - O(1) when hint is correct for sequential inserts
    template <typename K, typename V>
    __attribute__((hot)) auto insert_with_hint_impl(const_iterator hint, K&& key, V&& value) -> iterator {
        if (_root == nullptr) [[unlikely]] {
            _root = create_leaf();
            auto* leaf = static_cast<leaf_node*>(_root);
            leaf->slots[0] = storage_type(std::forward<K>(key), std::forward<V>(value));
            leaf->count = 1;
            ++_size;
            return iterator(leaf, 0);
        }

        // Fast path: hint is end() and key > all existing keys (append case)
        if (hint._node == nullptr) {
            // Hint is end() - check if we can append to rightmost leaf
            auto* right_leaf = const_cast<leaf_node*>(rightmost_leaf());
            if (right_leaf != nullptr && right_leaf->count > 0) {
                // Check if key > last key (most common case for sequential insert)
                if (_comp(right_leaf->key(right_leaf->count - 1), key)) {
                    // Can append at end of rightmost leaf
                    auto [inserted_node, inserted_pos] =
                      insert_and_split_impl(right_leaf, right_leaf->count, std::forward<K>(key), std::forward<V>(value));
                    ++_size;
                    return iterator(inserted_node, inserted_pos);
                }
            }
        } else if (hint._node->is_leaf_node()) {
            // Hint points to a leaf position
            auto* hint_leaf = const_cast<leaf_node*>(static_cast<const leaf_node*>(hint._node));
            size_type hint_pos = hint._pos;

            // Check if hint is correct: prev_key < key < hint_key (or key < first_key)
            bool key_less_than_hint = (hint_pos < hint_leaf->count) && _comp(key, hint_leaf->key(hint_pos));
            bool key_greater_than_prev = true;

            if (hint_pos > 0) {
                // Check previous key in same leaf
                key_greater_than_prev = _comp(hint_leaf->key(hint_pos - 1), key);
            } else if (hint_leaf->parent != nullptr) {
                // Need to check parent's separator
                auto* parent = static_cast<internal_node*>(hint_leaf->parent);
                if (hint_leaf->position > 0) {
                    key_greater_than_prev = _comp(parent->key(hint_leaf->position - 1), key);
                }
            }

            if (key_less_than_hint && key_greater_than_prev) {
                // Hint is correct! Insert directly at hint position
                auto [inserted_node, inserted_pos] =
                  insert_and_split_impl(hint_leaf, hint_pos, std::forward<K>(key), std::forward<V>(value));
                ++_size;
                return iterator(inserted_node, inserted_pos);
            }

            // Check for duplicate key at hint position
            if (hint_pos < hint_leaf->count && !_comp(key, hint_leaf->key(hint_pos)) &&
                !_comp(hint_leaf->key(hint_pos), key)) {
                // Key already exists at hint
                return iterator(hint_leaf, hint_pos);
            }
        }

        // Hint is not useful - fall back to normal insert
        auto [it, inserted] = insert_impl(std::forward<K>(key), std::forward<V>(value));
        return it;
    }

   public:
    // Find - optimized with type-specialized search and minimal branching
    [[nodiscard]] __attribute__((hot)) auto find(const Key& key) -> iterator {
        node_base* node = _root;
        if (node == nullptr) [[unlikely]] {
            return end();
        }

        // Traverse internal nodes - most trees are shallow (2-4 levels)
        while (!node->is_leaf_node()) [[likely]] {
            auto* internal = static_cast<internal_node*>(node);
            size_type pos = lower_bound_in_internal(internal, key);

            // Check for exact match in internal node (rare for most insertions)
            if (pos < internal->count) [[likely]] {
                if (!_comp(key, internal->key(pos))) [[unlikely]] {
                    return iterator(internal, pos);
                }
            }

            // Prefetch next node before traversing
            __builtin_prefetch(internal->children[pos], 0, 3);
            node = internal->children[pos];
        }

        // At leaf node - most finds end here
        auto* leaf = static_cast<leaf_node*>(node);
        size_type pos = lower_bound_in_leaf(leaf, key);
        if (pos < leaf->count && !_comp(key, leaf->key(pos))) [[likely]] {
            return iterator(leaf, pos);
        }
        return end();
    }

    [[nodiscard]] auto find(const Key& key) const -> const_iterator {
        return const_iterator(const_cast<btree_map*>(this)->find(key));
    }

    // operator[] - uses try_emplace to avoid constructing Value if key exists
    auto operator[](const Key& key) -> Value& {
        auto [it, inserted] = try_emplace(key);
        if (it._node->is_leaf_node()) {
            return static_cast<leaf_node*>(it._node)->value(it._pos);
        }
        return static_cast<internal_node*>(it._node)->value(it._pos);
    }

    // operator[] with rvalue key - avoids key copy
    auto operator[](Key&& key) -> Value& {
        auto [it, inserted] = try_emplace(std::move(key));
        if (it._node->is_leaf_node()) {
            return static_cast<leaf_node*>(it._node)->value(it._pos);
        }
        return static_cast<internal_node*>(it._node)->value(it._pos);
    }

    // at
    [[nodiscard]] auto at(const Key& key) -> Value& {
        auto it = find(key);
        if (it == end()) {
            throw std::out_of_range("btree_map::at: key not found");
        }
        if (it._node->is_leaf_node()) {
            return static_cast<leaf_node*>(it._node)->value(it._pos);
        }
        return static_cast<internal_node*>(it._node)->value(it._pos);
    }

    [[nodiscard]] auto at(const Key& key) const -> const Value& {
        auto it = find(key);
        if (it == end()) {
            throw std::out_of_range("btree_map::at: key not found");
        }
        if (it._node->is_leaf_node()) {
            return static_cast<const leaf_node*>(it._node)->value(it._pos);
        }
        return static_cast<const internal_node*>(it._node)->value(it._pos);
    }

    // contains
    [[nodiscard]] auto contains(const Key& key) const -> bool { return find(key) != end(); }

    // count
    [[nodiscard]] auto count(const Key& key) const -> size_type { return contains(key) ? 1 : 0; }

    // lower_bound
    [[nodiscard]] auto lower_bound(const Key& key) -> iterator {
        if (_root == nullptr) return end();

        node_base* node = _root;
        iterator result = end();

        while (true) {
            size_type pos = lower_bound_in_node(node, key);

            if (node->is_leaf_node()) {
                auto* leaf = static_cast<leaf_node*>(node);
                if (pos < leaf->count) {
                    return iterator(leaf, pos);
                }
                return result;
            }

            auto* internal = static_cast<internal_node*>(node);
            if (pos < internal->count) {
                if (!_comp(internal->key(pos), key)) {
                    result = iterator(internal, pos);
                }
            }
            // Prefetch next node before traversing
            __builtin_prefetch(internal->children[pos], 0, 3);
            node = internal->children[pos];
        }
    }

    [[nodiscard]] auto lower_bound(const Key& key) const -> const_iterator {
        return const_iterator(const_cast<btree_map*>(this)->lower_bound(key));
    }

    // upper_bound
    [[nodiscard]] auto upper_bound(const Key& key) -> iterator {
        auto it = lower_bound(key);
        if (it != end() && !_comp(key, it->first) && !_comp(it->first, key)) {
            ++it;
        }
        return it;
    }

    [[nodiscard]] auto upper_bound(const Key& key) const -> const_iterator {
        return const_iterator(const_cast<btree_map*>(this)->upper_bound(key));
    }

    // Iterators
    [[nodiscard]] auto begin() noexcept -> iterator {
        if (_root == nullptr) return end();
        auto* leaf = leftmost_leaf();
        return iterator(leaf, 0);
    }

    [[nodiscard]] auto begin() const noexcept -> const_iterator {
        if (_root == nullptr) return end();
        auto* leaf = leftmost_leaf();
        return const_iterator(leaf, 0);
    }

    [[nodiscard]] auto cbegin() const noexcept -> const_iterator { return begin(); }

    [[nodiscard]] auto end() noexcept -> iterator { return iterator(nullptr, 0); }

    [[nodiscard]] auto end() const noexcept -> const_iterator { return const_iterator(nullptr, 0); }

    [[nodiscard]] auto cend() const noexcept -> const_iterator { return end(); }

    // Reverse iterators
    [[nodiscard]] auto rbegin() noexcept -> reverse_iterator {
        if (_root == nullptr) return reverse_iterator(nullptr, 0, true);
        auto* leaf = rightmost_leaf();
        return reverse_iterator(const_cast<node_base*>(static_cast<const node_base*>(leaf)), leaf->count - 1, false);
    }

    [[nodiscard]] auto rbegin() const noexcept -> const_reverse_iterator {
        if (_root == nullptr) return const_reverse_iterator(nullptr, 0, true);
        auto* leaf = rightmost_leaf();
        return const_reverse_iterator(leaf, leaf->count - 1, false);
    }

    [[nodiscard]] auto crbegin() const noexcept -> const_reverse_iterator { return rbegin(); }

    [[nodiscard]] auto rend() noexcept -> reverse_iterator {
        return reverse_iterator(nullptr, 0, true);  // Before first element
    }

    [[nodiscard]] auto rend() const noexcept -> const_reverse_iterator {
        return const_reverse_iterator(nullptr, 0, true);  // Before first element
    }

    [[nodiscard]] auto crend() const noexcept -> const_reverse_iterator { return rend(); }

    // Erase by key - O(log n) with proper B-tree rebalancing
    auto erase(const Key& key) -> size_type {
        auto it = find(key);
        if (it == end()) {
            return 0;
        }

        erase_impl(it._node, it._pos);
        return 1;
    }

    // Erase by iterator - returns iterator to next element
    auto erase(iterator pos) -> iterator {
        if (pos == end()) {
            return end();
        }

        // For leaf nodes, we can often avoid a full tree traversal
        if (pos._node->is_leaf_node()) {
            auto* leaf = static_cast<leaf_node*>(pos._node);
            size_type erase_pos = pos._pos;

            // Check if this is a simple case: leaf won't underflow
            // (count > kMinLeafSlots or node is root)
            bool simple_case = (leaf->count > kMinLeafSlots) || (leaf == _root);

            if (simple_case) {
                // Simple case: just remove and return next position
                remove_slot_from_leaf(leaf, erase_pos);
                --_size;

                if (leaf->count == 0) {
                    // Root became empty
                    delete leaf;
                    _root = nullptr;
                    return end();
                }

                if (erase_pos < leaf->count) {
                    // There's a next element in the same leaf
                    return iterator(leaf, erase_pos);
                } else {
                    // Need to find next leaf - traverse up
                    iterator it(leaf, leaf->count - 1);
                    ++it;  // Move to next element
                    if (it._node == nullptr) return end();
                    return it;
                }
            }
        }

        // Complex case: might need rebalancing
        // Optimization: if next element is in a different subtree (not sibling), it survives rebalancing
        iterator next = pos;
        ++next;

        if (next != end() && next._node != pos._node) {
            // Next is in a different node - check if it's in right sibling AND could be merged
            auto* pos_leaf = static_cast<leaf_node*>(pos._node);
            bool next_is_safe = true;

            if (pos_leaf->parent != nullptr) {
                auto* parent = static_cast<internal_node*>(pos_leaf->parent);
                size_type pos_idx = pos_leaf->position;

                // Check if next is in the right sibling
                if (pos_idx < parent->count && parent->children[pos_idx + 1] == next._node) {
                    // Right sibling merge only happens when pos_idx == 0
                    // (rebalance_after_erase prefers merging with left sibling)
                    // If pos_idx > 0, we merge left, so right sibling is safe
                    if (pos_idx == 0) {
                        // Could merge with right sibling - check if it's actually needed
                        // Merge happens when: can't borrow from left (pos_idx=0 means no left)
                        // AND can't borrow from right (right sibling can't spare)
                        auto* right_sibling = static_cast<leaf_node*>(parent->children[1]);
                        // If right sibling can spare OR we won't underflow, next is safe
                        if (pos_leaf->count > kMinLeafSlots || right_sibling->count > kMinLeafSlots + 1) {
                            next_is_safe = true;  // Won't merge with right
                        } else {
                            next_is_safe = false;  // Will merge with right sibling
                        }
                    }
                    // else: pos_idx > 0, will merge with left if needed, right sibling safe
                }
            }

            if (next_is_safe) {
                // Next is safe - won't be affected by rebalancing
                erase_impl(pos._node, pos._pos);
                return next;
            }
        }

        // Fall back to lower_bound - move key from internal storage instead of copying
        auto* leaf = static_cast<leaf_node*>(pos._node);
        Key erased_key = std::move(leaf->slots[pos._pos].first);
        erase_impl(pos._node, pos._pos);
        return lower_bound(erased_key);
    }

    auto erase(const_iterator pos) -> iterator { return erase(iterator(const_cast<node_base*>(pos._node), pos._pos)); }

    // Erase range [first, last) - uses optimized erase(iterator)
    auto erase(const_iterator first, const_iterator last) -> iterator {
        // Convert to non-const iterator
        auto it = iterator(const_cast<node_base*>(first._node), first._pos);
        auto last_it = iterator(const_cast<node_base*>(last._node), last._pos);

        while (it != last_it && it != end()) {
            it = erase(it);
        }
        return it;
    }

    // equal_range - returns pair of iterators (lower_bound, upper_bound)
    // Optimized: single traversal since this is a unique-key map
    [[nodiscard]] auto equal_range(const Key& key) -> std::pair<iterator, iterator> {
        auto lb = lower_bound(key);
        if (lb == end() || _comp(key, lb->first)) {
            // Key not found: lower_bound == upper_bound
            return {lb, lb};
        }
        // Key found: upper_bound is next element
        auto ub = lb;
        ++ub;
        return {lb, ub};
    }

    [[nodiscard]] auto equal_range(const Key& key) const -> std::pair<const_iterator, const_iterator> {
        auto lb = lower_bound(key);
        if (lb == end() || _comp(key, lb->first)) {
            return {lb, lb};
        }
        auto ub = lb;
        ++ub;
        return {lb, ub};
    }

    // swap
    void swap(btree_map& other) noexcept {
        std::swap(_root, other._root);
        std::swap(_size, other._size);
        std::swap(_comp, other._comp);
    }

    // max_size - theoretical maximum
    [[nodiscard]] static constexpr auto max_size() noexcept -> size_type {
        return std::numeric_limits<size_type>::max() / sizeof(storage_type);
    }

    // key_comp - returns the key comparison function
    [[nodiscard]] auto key_comp() const -> key_compare { return _comp; }

    // value_comp - returns the value comparison function
    [[nodiscard]] auto value_comp() const -> value_compare { return value_compare(_comp); }

    // merge - merge elements from another btree_map (C++17)
    template <typename C2, std::size_t N2>
    void merge(btree_map<Key, Value, C2, N2>& source) {
        for (auto it = source.begin(); it != source.end();) {
            auto [insert_it, inserted] = insert(it->first, it->second);
            if (inserted) {
                auto next = it;
                ++next;
                source.erase(it);
                it = next;
            } else {
                ++it;
            }
        }
    }

    template <typename C2, std::size_t N2>
    void merge(btree_map<Key, Value, C2, N2>&& source) {
        merge(source);
    }

    // insert with iterator range
    template <typename InputIt>
    void insert(InputIt first, InputIt last) {
        for (; first != last; ++first) {
            insert(*first);
        }
    }

    // insert_range - insert elements from a range (C++23)
    template <typename Range>
    void insert_range(Range&& range) {
        for (auto&& elem : range) {
            insert(std::forward<decltype(elem)>(elem));
        }
    }

    // Comparison operators
    friend auto operator==(const btree_map& lhs, const btree_map& rhs) -> bool {
        if (lhs.size() != rhs.size()) return false;
        auto it1 = lhs.begin();
        auto it2 = rhs.begin();
        while (it1 != lhs.end()) {
            if (it1->first != it2->first || it1->second != it2->second) {
                return false;
            }
            ++it1;
            ++it2;
        }
        return true;
    }

    friend auto operator!=(const btree_map& lhs, const btree_map& rhs) -> bool { return !(lhs == rhs); }

    // Lexicographical comparison operators (C++20)
    friend auto operator<(const btree_map& lhs, const btree_map& rhs) -> bool {
        return std::lexicographical_compare(lhs.begin(), lhs.end(), rhs.begin(), rhs.end(),
                                            [](const value_type& a, const value_type& b) {
                                                if (a.first < b.first) return true;
                                                if (b.first < a.first) return false;
                                                return a.second < b.second;
                                            });
    }

    friend auto operator<=(const btree_map& lhs, const btree_map& rhs) -> bool { return !(rhs < lhs); }
    friend auto operator>(const btree_map& lhs, const btree_map& rhs) -> bool { return rhs < lhs; }
    friend auto operator>=(const btree_map& lhs, const btree_map& rhs) -> bool { return !(lhs < rhs); }

#if __cplusplus >= 202002L || (defined(_MSVC_LANG) && _MSVC_LANG >= 202002L)
    // Spaceship operator (C++20)
    friend auto operator<=>(const btree_map& lhs, const btree_map& rhs) {
        auto it1 = lhs.begin();
        auto it2 = rhs.begin();
        while (it1 != lhs.end() && it2 != rhs.end()) {
            if (auto cmp = it1->first <=> it2->first; cmp != 0) return cmp;
            if (auto cmp = it1->second <=> it2->second; cmp != 0) return cmp;
            ++it1;
            ++it2;
        }
        return lhs.size() <=> rhs.size();
    }
#endif

    // Debug info
    [[nodiscard]] static constexpr auto leaf_slots() noexcept -> size_type { return kLeafSlots; }
    [[nodiscard]] static constexpr auto internal_slots() noexcept -> size_type { return kInternalSlots; }
};

// btree_map now automatically selects optimal node size by default.
// btree_map_auto is kept for backward compatibility but is now identical to btree_map.
template <typename Key, typename Value, typename Compare = std::less<Key>>
using btree_map_auto = btree_map<Key, Value, Compare>;

// Explicit 256-byte node size for when you want minimal memory footprint
// at the cost of potentially fewer slots for large value types
template <typename Key, typename Value, typename Compare = std::less<Key>>
using btree_map_compact = btree_map<Key, Value, Compare, 256>;

// Non-member swap (C++17)
template <typename Key, typename Value, typename Compare, std::size_t N>
void swap(btree_map<Key, Value, Compare, N>& lhs, btree_map<Key, Value, Compare, N>& rhs) noexcept {
    lhs.swap(rhs);
}

// erase_if - erase all elements satisfying predicate (C++20)
template <typename Key, typename Value, typename Compare, std::size_t N, typename Pred>
typename btree_map<Key, Value, Compare, N>::size_type erase_if(btree_map<Key, Value, Compare, N>& c, Pred pred) {
    auto old_size = c.size();
    for (auto it = c.begin(); it != c.end();) {
        if (pred(*it)) {
            it = c.erase(it);
        } else {
            ++it;
        }
    }
    return old_size - c.size();
}

}  // namespace stdb::container

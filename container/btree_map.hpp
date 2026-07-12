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

// PMR support requires C++17
#if __cplusplus >= 201703L || (defined(_MSVC_LANG) && _MSVC_LANG >= 201703L)
#include <memory_resource>
#define BTREE_HAS_PMR 1
#endif

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
// Requires compare() method for three-way comparison optimization
template <typename T>
concept string_like = requires(const T& a, const T& b) {
    { a.data() };
    { a.size() } -> std::convertible_to<std::size_t>;
    { a.compare(b) } -> std::convertible_to<int>;
};

// Trait to detect transparent comparators (have is_transparent type member)
// Guard to avoid redefinition when included with skiplist_map.hpp
#ifndef STDB_CONTAINER_IS_TRANSPARENT_COMPARATOR_DEFINED
#define STDB_CONTAINER_IS_TRANSPARENT_COMPARATOR_DEFINED

template <typename, typename = void>
struct is_transparent_comparator : std::false_type {};

template <typename T>
struct is_transparent_comparator<T, std::void_t<typename T::is_transparent>> : std::true_type {};

template <typename T>
inline constexpr bool is_transparent_comparator_v = is_transparent_comparator<T>::value;

// Compressed pair that uses [[no_unique_address]] for empty types
// This optimizes btree_set storage where Value is an empty type
template <typename First, typename Second>
struct compressed_pair
{
    First first;
    [[no_unique_address]] Second second;

    compressed_pair() = default;
    compressed_pair(const compressed_pair&) = default;
    compressed_pair(compressed_pair&&) noexcept = default;
    compressed_pair& operator=(const compressed_pair&) = default;
    compressed_pair& operator=(compressed_pair&&) noexcept = default;

    template <typename F, typename S>
    constexpr compressed_pair(F&& f, S&& s) : first(std::forward<F>(f)), second(std::forward<S>(s)) {}

    constexpr compressed_pair(const First& f, const Second& s) : first(f), second(s) {}
    constexpr compressed_pair(First&& f, Second&& s) : first(std::move(f)), second(std::move(s)) {}

    // Conversion to std::pair for API compatibility
    operator std::pair<const First, Second>() const { return {first, second}; }
};

#endif  // STDB_CONTAINER_IS_TRANSPARENT_COMPARATOR_DEFINED

// Empty value type for set implementation
// When used as Value type, btree_map operates in "set mode"
struct btree_set_empty_value
{
    constexpr bool operator==(const btree_set_empty_value&) const noexcept { return true; }
    constexpr bool operator!=(const btree_set_empty_value&) const noexcept { return false; }
    constexpr auto operator<=>(const btree_set_empty_value&) const noexcept { return std::strong_ordering::equal; }
};

// =============================================================================
// Duplicate key policies for btree containers
// =============================================================================

// Policy for unique keys (btree_map, btree_set) - rejects duplicate keys
struct btree_unique_policy
{
    static constexpr bool allow_duplicates = false;
};

// Policy for multi keys (btree_multimap, btree_multiset) - allows duplicate keys
struct btree_multi_policy
{
    static constexpr bool allow_duplicates = true;
};

// Helper to calculate optimal node size for a given key-value pair type
// Aims for at least 15 slots per node for good cache utilization
// Note: For btree_set (Value = btree_set_empty_value), compressed_pair's
// [[no_unique_address]] already optimizes the empty value to zero size,
// so no specialization is needed.
template <typename Key, typename Value>
constexpr std::size_t optimal_node_size() {
    constexpr std::size_t header_size = 24;  // parent + count + position + is_leaf + padding
    constexpr std::size_t target_slots = 15;
    // Use compressed_pair size for accurate calculation
    constexpr std::size_t pair_size = sizeof(compressed_pair<Key, Value>);
    constexpr std::size_t min_size = header_size + pair_size * target_slots;
    // Round up to power of 2 for cache alignment
    if (min_size <= 256) return 256;
    if (min_size <= 512) return 512;
    if (min_size <= 1024) return 1024;
    if (min_size <= 2048) return 2048;
    return 4096;
}

template <typename Key, typename Value, typename Compare = std::less<Key>,
          typename Allocator = std::allocator<std::pair<const Key, Value>>,
          std::size_t TargetNodeSize = optimal_node_size<Key, Value>(),
          typename DuplicatePolicy = btree_unique_policy>
class btree_map
{
   public:
    // Set mode detection: when Value is btree_set_empty_value, we operate as a set
    static constexpr bool is_set_mode = std::is_same_v<Value, btree_set_empty_value>;
    // Multi mode detection: when policy allows duplicates (multimap/multiset)
    static constexpr bool is_multi_mode = DuplicatePolicy::allow_duplicates;

    using key_type = Key;
    using mapped_type = Value;
    // In set mode, value_type is just Key; in map mode, it's pair<const Key, Value>
    using value_type = std::conditional_t<is_set_mode, Key, std::pair<const Key, Value>>;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    using key_compare = Compare;
    using allocator_type = Allocator;
    // In set mode, references are const Key&; in map mode, value_type&
    using reference = std::conditional_t<is_set_mode, const Key&, value_type&>;
    using const_reference = std::conditional_t<is_set_mode, const Key&, const value_type&>;
    using pointer = std::conditional_t<is_set_mode, const Key*, value_type*>;
    using const_pointer = std::conditional_t<is_set_mode, const Key*, const value_type*>;

    // value_compare - compares value_type by key (std::map/std::set compatible)
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

        bool operator()(const value_type& lhs, const value_type& rhs) const {
            if constexpr (is_set_mode) {
                return comp(lhs, rhs);
            } else {
                return comp(lhs.first, rhs.first);
            }
        }
    };

    // Node handle for extract/insert operations (C++17)
    // In set mode, stores just Key; in map mode, stores pair<Key, Value>
    class node_type
    {
        friend class btree_map;

       public:
        using key_type = Key;
        using mapped_type = Value;

        node_type() noexcept = default;
        node_type(node_type&& other) noexcept : _storage(std::move(other._storage)), _valid(other._valid) {
            other._valid = false;
        }

        node_type& operator=(node_type&& other) noexcept {
            if (this != &other) {
                _storage = std::move(other._storage);
                _valid = other._valid;
                other._valid = false;
            }
            return *this;
        }

        ~node_type() = default;

        [[nodiscard]] bool empty() const noexcept { return !_valid; }
        explicit operator bool() const noexcept { return _valid; }

        // Map mode accessors
        [[nodiscard]] key_type& key() requires(!is_set_mode) { return _storage.first; }
        [[nodiscard]] mapped_type& mapped() requires(!is_set_mode) { return _storage.second; }

        // Set mode accessor - returns the value (which is just the key)
        [[nodiscard]] key_type& value() requires(is_set_mode) { return _storage; }

        void swap(node_type& other) noexcept {
            std::swap(_storage, other._storage);
            std::swap(_valid, other._valid);
        }

       private:
        // Map mode constructors
        explicit node_type(Key&& k, Value&& v) requires(!is_set_mode)
            : _storage(std::move(k), std::move(v)), _valid(true) {}
        explicit node_type(const Key& k, Value&& v) requires(!is_set_mode)
            : _storage(k, std::move(v)), _valid(true) {}

        // Set mode constructors
        explicit node_type(Key&& k) requires(is_set_mode) : _storage(std::move(k)), _valid(true) {}
        explicit node_type(const Key& k) requires(is_set_mode) : _storage(k), _valid(true) {}

        // Storage type: pair for map mode, Key for set mode
        using storage_t = std::conditional_t<is_set_mode, Key, std::pair<Key, Value>>;
        storage_t _storage;
        bool _valid = false;
    };

   private:
    // Storage type without const (for internal manipulation)
    // Uses compressed_pair for [[no_unique_address]] optimization of empty value types
    using storage_type = compressed_pair<Key, Value>;

    // Calculate optimal number of slots per node
    // Node layout: [header] + [slots (key-value pairs)] + [children for internal]
    // node_base is: parent(8) + count(2) + position(2) + is_leaf(1) + padding(3) = 16 bytes
    static constexpr size_type kNodeHeaderSize = 16;
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

    // Allocator types for node allocation
    using leaf_allocator_type = typename std::allocator_traits<Allocator>::template rebind_alloc<leaf_node>;
    using internal_allocator_type = typename std::allocator_traits<Allocator>::template rebind_alloc<internal_node>;

    // Root node and tree state
    node_base* _root = nullptr;
    leaf_node* _rightmost_leaf = nullptr;  // Cached for O(1) end()
    size_type _size = 0;
    [[no_unique_address]] Compare _comp;
    [[no_unique_address]] leaf_allocator_type _leaf_alloc;
    [[no_unique_address]] internal_allocator_type _internal_alloc;

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
    // Optimized: use direct _mm_loadu_si128 load for contiguous keys
    template <typename T>
    static auto simd_lower_bound_s32(const T* slots, size_type count, int32_t target) noexcept -> size_type
    requires(sizeof(T) >= sizeof(int32_t))
    {
        constexpr size_type stride = sizeof(T) / sizeof(int32_t);
        const auto* keys = reinterpret_cast<const int32_t*>(slots);

        __m128i target_vec = _mm_set1_epi32(target);
        size_type i = 0;

        while (i + 4 <= count) {
            __m128i key_vec;
            if constexpr (stride == 1) {
                key_vec = _mm_loadu_si128(reinterpret_cast<const __m128i*>(&keys[i]));
            } else {
                key_vec = _mm_set_epi32(keys[(i + 3) * stride], keys[(i + 2) * stride],
                                        keys[(i + 1) * stride], keys[i * stride]);
            }
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
    // Optimized: use direct _mm_loadu_si128 load for contiguous keys
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
            __m128i key_vec;
            if constexpr (stride == 1) {
                key_vec = _mm_loadu_si128(reinterpret_cast<const __m128i*>(&keys[i]));
            } else {
                key_vec = _mm_set_epi32(static_cast<int32_t>(keys[(i + 3) * stride]),
                                        static_cast<int32_t>(keys[(i + 2) * stride]),
                                        static_cast<int32_t>(keys[(i + 1) * stride]),
                                        static_cast<int32_t>(keys[i * stride]));
            }
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
    // Optimized: use direct _mm_loadu_si128 load for contiguous keys
    template <typename T>
    static auto simd_lower_bound_s16(const T* slots, size_type count, int16_t target) noexcept -> size_type
    requires(sizeof(T) >= sizeof(int16_t))
    {
        constexpr size_type stride = sizeof(T) / sizeof(int16_t);
        const auto* keys = reinterpret_cast<const int16_t*>(slots);

        __m128i target_vec = _mm_set1_epi16(target);
        size_type i = 0;

        while (i + 8 <= count) {
            __m128i key_vec;
            if constexpr (stride == 1) {
                key_vec = _mm_loadu_si128(reinterpret_cast<const __m128i*>(&keys[i]));
            } else {
                key_vec = _mm_set_epi16(keys[(i + 7) * stride], keys[(i + 6) * stride], keys[(i + 5) * stride],
                                        keys[(i + 4) * stride], keys[(i + 3) * stride], keys[(i + 2) * stride],
                                        keys[(i + 1) * stride], keys[i * stride]);
            }
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

    // SSE2 lower_bound for uint16_t keys (unsigned) - processes 8 keys at a time
    // Optimized: use direct _mm_loadu_si128 load for contiguous keys
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
            __m128i key_vec;
            if constexpr (stride == 1) {
                key_vec = _mm_loadu_si128(reinterpret_cast<const __m128i*>(&keys[i]));
            } else {
                key_vec = _mm_set_epi16(static_cast<int16_t>(keys[(i + 7) * stride]),
                                        static_cast<int16_t>(keys[(i + 6) * stride]),
                                        static_cast<int16_t>(keys[(i + 5) * stride]),
                                        static_cast<int16_t>(keys[(i + 4) * stride]),
                                        static_cast<int16_t>(keys[(i + 3) * stride]),
                                        static_cast<int16_t>(keys[(i + 2) * stride]),
                                        static_cast<int16_t>(keys[(i + 1) * stride]),
                                        static_cast<int16_t>(keys[i * stride]));
            }
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
    // Optimized: use direct _mm_loadu_si128 load for contiguous keys
    template <typename T>
    static auto simd_lower_bound_s8(const T* slots, size_type count, int8_t target) noexcept -> size_type
    requires(sizeof(T) >= sizeof(int8_t))
    {
        constexpr size_type stride = sizeof(T) / sizeof(int8_t);
        const auto* keys = reinterpret_cast<const int8_t*>(slots);

        __m128i target_vec = _mm_set1_epi8(target);
        size_type i = 0;

        while (i + 16 <= count) {
            __m128i key_vec;
            if constexpr (stride == 1) {
                key_vec = _mm_loadu_si128(reinterpret_cast<const __m128i*>(&keys[i]));
            } else {
                key_vec = _mm_set_epi8(
                  keys[(i + 15) * stride], keys[(i + 14) * stride], keys[(i + 13) * stride], keys[(i + 12) * stride],
                  keys[(i + 11) * stride], keys[(i + 10) * stride], keys[(i + 9) * stride], keys[(i + 8) * stride],
                  keys[(i + 7) * stride], keys[(i + 6) * stride], keys[(i + 5) * stride], keys[(i + 4) * stride],
                  keys[(i + 3) * stride], keys[(i + 2) * stride], keys[(i + 1) * stride], keys[i * stride]);
            }
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
    // Optimized: use direct _mm_loadu_si128 load for contiguous keys
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
            __m128i key_vec;
            if constexpr (stride == 1) {
                key_vec = _mm_loadu_si128(reinterpret_cast<const __m128i*>(&keys[i]));
            } else {
                key_vec = _mm_set_epi8(
                  static_cast<int8_t>(keys[(i + 15) * stride]), static_cast<int8_t>(keys[(i + 14) * stride]),
                  static_cast<int8_t>(keys[(i + 13) * stride]), static_cast<int8_t>(keys[(i + 12) * stride]),
                  static_cast<int8_t>(keys[(i + 11) * stride]), static_cast<int8_t>(keys[(i + 10) * stride]),
                  static_cast<int8_t>(keys[(i + 9) * stride]), static_cast<int8_t>(keys[(i + 8) * stride]),
                  static_cast<int8_t>(keys[(i + 7) * stride]), static_cast<int8_t>(keys[(i + 6) * stride]),
                  static_cast<int8_t>(keys[(i + 5) * stride]), static_cast<int8_t>(keys[(i + 4) * stride]),
                  static_cast<int8_t>(keys[(i + 3) * stride]), static_cast<int8_t>(keys[(i + 2) * stride]),
                  static_cast<int8_t>(keys[(i + 1) * stride]), static_cast<int8_t>(keys[i * stride]));
            }
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
    // Optimized: use direct _mm_loadu_ps load for contiguous keys
    template <typename T>
    static auto simd_lower_bound_float(const T* slots, size_type count, float target) noexcept -> size_type
    requires(sizeof(T) >= sizeof(float))
    {
        constexpr size_type stride = sizeof(T) / sizeof(float);
        const auto* keys = reinterpret_cast<const float*>(slots);

        __m128 target_vec = _mm_set1_ps(target);
        size_type i = 0;

        while (i + 4 <= count) {
            __m128 key_vec;
            if constexpr (stride == 1) {
                key_vec = _mm_loadu_ps(&keys[i]);
            } else {
                key_vec = _mm_set_ps(keys[(i + 3) * stride], keys[(i + 2) * stride],
                                     keys[(i + 1) * stride], keys[i * stride]);
            }
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
    // Optimized: use direct _mm_loadu_pd load for contiguous keys
    template <typename T>
    static auto simd_lower_bound_double(const T* slots, size_type count, double target) noexcept -> size_type
    requires(sizeof(T) >= sizeof(double))
    {
        constexpr size_type stride = sizeof(T) / sizeof(double);
        const auto* keys = reinterpret_cast<const double*>(slots);

        __m128d target_vec = _mm_set1_pd(target);
        size_type i = 0;

        while (i + 2 <= count) {
            __m128d key_vec;
            if constexpr (stride == 1) {
                key_vec = _mm_loadu_pd(&keys[i]);
            } else {
                key_vec = _mm_set_pd(keys[(i + 1) * stride], keys[i * stride]);
            }
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
    // Optimized: use direct _mm256_loadu_si256 load for contiguous keys
    template <typename T>
    static auto simd_lower_bound_s64(const T* slots, size_type count, int64_t target) noexcept -> size_type
    requires(sizeof(T) >= sizeof(int64_t))
    {
        constexpr size_type stride = sizeof(T) / sizeof(int64_t);
        const auto* keys = reinterpret_cast<const int64_t*>(slots);

        __m256i target_vec = _mm256_set1_epi64x(target);
        size_type i = 0;

        while (i + 4 <= count) {
            __m256i key_vec;
            if constexpr (stride == 1) {
                key_vec = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&keys[i]));
            } else {
                key_vec = _mm256_set_epi64x(keys[(i + 3) * stride], keys[(i + 2) * stride],
                                            keys[(i + 1) * stride], keys[i * stride]);
            }
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
    // Optimized: use direct _mm256_loadu_si256 load for contiguous keys
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
            __m256i key_vec;
            if constexpr (stride == 1) {
                key_vec = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&keys[i]));
            } else {
                key_vec = _mm256_set_epi64x(static_cast<int64_t>(keys[(i + 3) * stride]),
                                            static_cast<int64_t>(keys[(i + 2) * stride]),
                                            static_cast<int64_t>(keys[(i + 1) * stride]),
                                            static_cast<int64_t>(keys[i * stride]));
            }
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
    // Optimized: use direct _mm256_loadu_pd load for contiguous keys
    template <typename T>
    static auto simd_lower_bound_double_avx(const T* slots, size_type count, double target) noexcept -> size_type
    requires(sizeof(T) >= sizeof(double))
    {
        constexpr size_type stride = sizeof(T) / sizeof(double);
        const auto* keys = reinterpret_cast<const double*>(slots);

        __m256d target_vec = _mm256_set1_pd(target);
        size_type i = 0;

        while (i + 4 <= count) {
            __m256d key_vec;
            if constexpr (stride == 1) {
                key_vec = _mm256_loadu_pd(&keys[i]);
            } else {
                key_vec = _mm256_set_pd(keys[(i + 3) * stride], keys[(i + 2) * stride],
                                        keys[(i + 1) * stride], keys[i * stride]);
            }
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

    // AVX2 lower_bound for int32_t - processes 8 keys at a time
    // Optimized: use direct _mm256_loadu_si256 load for contiguous keys
    template <typename T>
    static auto simd_lower_bound_s32_avx2(const T* slots, size_type count, int32_t target) noexcept -> size_type
    requires(sizeof(T) >= sizeof(int32_t))
    {
        constexpr size_type stride = sizeof(T) / sizeof(int32_t);
        const auto* keys = reinterpret_cast<const int32_t*>(slots);

        __m256i target_vec = _mm256_set1_epi32(target);
        size_type i = 0;

        while (i + 8 <= count) {
            __m256i key_vec;
            if constexpr (stride == 1) {
                key_vec = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&keys[i]));
            } else {
                key_vec = _mm256_set_epi32(
                    keys[(i + 7) * stride], keys[(i + 6) * stride],
                    keys[(i + 5) * stride], keys[(i + 4) * stride],
                    keys[(i + 3) * stride], keys[(i + 2) * stride],
                    keys[(i + 1) * stride], keys[i * stride]);
            }
            __m256i lt = _mm256_cmpgt_epi32(target_vec, key_vec);
            int mask = _mm256_movemask_ps(_mm256_castsi256_ps(lt));

            if (mask != 0xFF) {
                return i + __builtin_ctz(~mask & 0xFF);
            }
            i += 8;
        }

        // Handle remaining elements
        for (; i < count; ++i) {
            if (keys[i * stride] >= target) return i;
        }
        return count;
    }

    // AVX2 lower_bound for uint32_t - processes 8 keys at a time
    // Optimized: use direct _mm256_loadu_si256 load for contiguous keys
    template <typename T>
    static auto simd_lower_bound_u32_avx2(const T* slots, size_type count, uint32_t target) noexcept -> size_type
    requires(sizeof(T) >= sizeof(uint32_t))
    {
        constexpr size_type stride = sizeof(T) / sizeof(uint32_t);
        const auto* keys = reinterpret_cast<const uint32_t*>(slots);

        __m256i sign_bit = _mm256_set1_epi32(static_cast<int32_t>(0x80000000));
        __m256i target_vec = _mm256_xor_si256(_mm256_set1_epi32(static_cast<int32_t>(target)), sign_bit);
        size_type i = 0;

        while (i + 8 <= count) {
            __m256i key_vec;
            if constexpr (stride == 1) {
                key_vec = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&keys[i]));
            } else {
                key_vec = _mm256_set_epi32(
                    static_cast<int32_t>(keys[(i + 7) * stride]), static_cast<int32_t>(keys[(i + 6) * stride]),
                    static_cast<int32_t>(keys[(i + 5) * stride]), static_cast<int32_t>(keys[(i + 4) * stride]),
                    static_cast<int32_t>(keys[(i + 3) * stride]), static_cast<int32_t>(keys[(i + 2) * stride]),
                    static_cast<int32_t>(keys[(i + 1) * stride]), static_cast<int32_t>(keys[i * stride]));
            }
            key_vec = _mm256_xor_si256(key_vec, sign_bit);
            __m256i lt = _mm256_cmpgt_epi32(target_vec, key_vec);
            int mask = _mm256_movemask_ps(_mm256_castsi256_ps(lt));

            if (mask != 0xFF) {
                return i + __builtin_ctz(~mask & 0xFF);
            }
            i += 8;
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
    // Optimized: use direct vld1q load for contiguous keys, vld2q for stride==2, bitmask for first match
    template <typename T>
    static auto neon_lower_bound_s32(const T* slots, size_type count, int32_t target) noexcept -> size_type
    requires(sizeof(T) >= sizeof(int32_t))
    {
        constexpr size_type stride = sizeof(T) / sizeof(int32_t);
        const auto* keys = reinterpret_cast<const int32_t*>(slots);

        int32x4_t target_vec = vdupq_n_s32(target);
        size_type i = 0;

        while (i + 4 <= count) {
            int32x4_t key_vec;
            if constexpr (stride == 1) {
                key_vec = vld1q_s32(&keys[i]);
            } else if constexpr (stride == 2) {
                // Use vld2 for efficient strided load (e.g., btree_map<int32_t, int32_t>)
                int32x4x2_t interleaved = vld2q_s32(&keys[i * 2]);
                key_vec = interleaved.val[0];  // Keys are at even indices
            } else {
                key_vec = int32x4_t{keys[i * stride], keys[(i + 1) * stride], keys[(i + 2) * stride],
                                    keys[(i + 3) * stride]};
            }
            uint32x4_t ge = vcgeq_s32(key_vec, target_vec);

            uint16x4_t narrow = vmovn_u32(ge);
            uint64_t mask = vget_lane_u64(vreinterpret_u64_u16(narrow), 0);

            if (mask != 0) {
                return i + static_cast<size_type>(__builtin_ctzll(mask) / 16);
            }
            i += 4;
        }

        for (; i < count; ++i) {
            if (keys[i * stride] >= target) return i;
        }
        return count;
    }

    // NEON lower_bound for uint32_t keys (unsigned)
    // Optimized: use direct vld1q load for contiguous keys, vld2q for stride==2, bitmask for first match
    template <typename T>
    static auto neon_lower_bound_u32(const T* slots, size_type count, uint32_t target) noexcept -> size_type
    requires(sizeof(T) >= sizeof(uint32_t))
    {
        constexpr size_type stride = sizeof(T) / sizeof(uint32_t);
        const auto* keys = reinterpret_cast<const uint32_t*>(slots);

        uint32x4_t target_vec = vdupq_n_u32(target);
        size_type i = 0;

        while (i + 4 <= count) {
            uint32x4_t key_vec;
            if constexpr (stride == 1) {
                key_vec = vld1q_u32(&keys[i]);
            } else if constexpr (stride == 2) {
                // Use vld2 for efficient strided load (e.g., btree_map<uint32_t, uint32_t>)
                uint32x4x2_t interleaved = vld2q_u32(&keys[i * 2]);
                key_vec = interleaved.val[0];  // Keys are at even indices
            } else {
                key_vec = uint32x4_t{keys[i * stride], keys[(i + 1) * stride], keys[(i + 2) * stride],
                                     keys[(i + 3) * stride]};
            }
            uint32x4_t ge = vcgeq_u32(key_vec, target_vec);

            uint16x4_t narrow = vmovn_u32(ge);
            uint64_t mask = vget_lane_u64(vreinterpret_u64_u16(narrow), 0);

            if (mask != 0) {
                return i + static_cast<size_type>(__builtin_ctzll(mask) / 16);
            }
            i += 4;
        }

        for (; i < count; ++i) {
            if (keys[i * stride] >= target) return i;
        }
        return count;
    }

    // NEON lower_bound for int64_t keys (signed)
    // Optimized: use direct vld1q load for contiguous keys, bitmask for first match
    template <typename T>
    static auto neon_lower_bound_s64(const T* slots, size_type count, int64_t target) noexcept -> size_type
    requires(sizeof(T) >= sizeof(int64_t))
    {
        constexpr size_type stride = sizeof(T) / sizeof(int64_t);
        const auto* keys = reinterpret_cast<const int64_t*>(slots);

        int64x2_t target_vec = vdupq_n_s64(target);
        size_type i = 0;

        while (i + 2 <= count) {
            int64x2_t key_vec;
            if constexpr (stride == 1) {
                key_vec = vld1q_s64(&keys[i]);
            } else {
                key_vec = int64x2_t{keys[i * stride], keys[(i + 1) * stride]};
            }
            uint64x2_t ge = vcgeq_s64(key_vec, target_vec);

            uint32x2_t narrow = vmovn_u64(ge);
            uint64_t mask = vget_lane_u64(vreinterpret_u64_u32(narrow), 0);

            if (mask != 0) {
                return i + static_cast<size_type>(__builtin_ctzll(mask) / 32);
            }
            i += 2;
        }

        if (i < count && keys[i * stride] >= target) return i;
        return count;
    }

    // NEON lower_bound for uint64_t keys (unsigned)
    // Optimized: use direct vld1q load for contiguous keys, bitmask for first match
    template <typename T>
    static auto neon_lower_bound_u64(const T* slots, size_type count, uint64_t target) noexcept -> size_type
    requires(sizeof(T) >= sizeof(uint64_t))
    {
        constexpr size_type stride = sizeof(T) / sizeof(uint64_t);
        const auto* keys = reinterpret_cast<const uint64_t*>(slots);

        uint64x2_t target_vec = vdupq_n_u64(target);
        size_type i = 0;

        while (i + 2 <= count) {
            uint64x2_t key_vec;
            if constexpr (stride == 1) {
                key_vec = vld1q_u64(&keys[i]);
            } else {
                key_vec = uint64x2_t{keys[i * stride], keys[(i + 1) * stride]};
            }
            uint64x2_t ge = vcgeq_u64(key_vec, target_vec);

            uint32x2_t narrow = vmovn_u64(ge);
            uint64_t mask = vget_lane_u64(vreinterpret_u64_u32(narrow), 0);

            if (mask != 0) {
                return i + static_cast<size_type>(__builtin_ctzll(mask) / 32);
            }
            i += 2;
        }

        if (i < count && keys[i * stride] >= target) return i;
        return count;
    }

    // NEON lower_bound for int16_t keys (signed) - processes 8 keys at a time
    // Optimized: use direct vld1q load for contiguous keys, vld2q for stride==2, bitmask for first match
    template <typename T>
    static auto neon_lower_bound_s16(const T* slots, size_type count, int16_t target) noexcept -> size_type
    requires(sizeof(T) >= sizeof(int16_t))
    {
        constexpr size_type stride = sizeof(T) / sizeof(int16_t);
        const auto* keys = reinterpret_cast<const int16_t*>(slots);

        int16x8_t target_vec = vdupq_n_s16(target);
        size_type i = 0;

        while (i + 8 <= count) {
            int16x8_t key_vec;
            if constexpr (stride == 1) {
                key_vec = vld1q_s16(&keys[i]);
            } else if constexpr (stride == 2) {
                // Use vld2 for efficient strided load (e.g., btree_map<int16_t, int16_t>)
                int16x8x2_t interleaved = vld2q_s16(&keys[i * 2]);
                key_vec = interleaved.val[0];  // Keys are at even indices
            } else {
                key_vec = int16x8_t{keys[i * stride],       keys[(i + 1) * stride], keys[(i + 2) * stride],
                                    keys[(i + 3) * stride], keys[(i + 4) * stride], keys[(i + 5) * stride],
                                    keys[(i + 6) * stride], keys[(i + 7) * stride]};
            }
            uint16x8_t ge = vcgeq_s16(key_vec, target_vec);

            uint8x8_t narrow = vmovn_u16(ge);
            uint64_t mask = vget_lane_u64(vreinterpret_u64_u8(narrow), 0);

            if (mask != 0) {
                return i + static_cast<size_type>(__builtin_ctzll(mask) / 8);
            }
            i += 8;
        }

        for (; i < count; ++i) {
            if (keys[i * stride] >= target) return i;
        }
        return count;
    }

    // NEON lower_bound for uint16_t keys (unsigned) - processes 8 keys at a time
    // Optimized: use direct vld1q load for contiguous keys, vld2q for stride==2, bitmask for first match
    template <typename T>
    static auto neon_lower_bound_u16(const T* slots, size_type count, uint16_t target) noexcept -> size_type
    requires(sizeof(T) >= sizeof(uint16_t))
    {
        constexpr size_type stride = sizeof(T) / sizeof(uint16_t);
        const auto* keys = reinterpret_cast<const uint16_t*>(slots);

        uint16x8_t target_vec = vdupq_n_u16(target);
        size_type i = 0;

        while (i + 8 <= count) {
            uint16x8_t key_vec;
            if constexpr (stride == 1) {
                key_vec = vld1q_u16(&keys[i]);
            } else if constexpr (stride == 2) {
                // Use vld2 for efficient strided load (e.g., btree_map<uint16_t, uint16_t>)
                uint16x8x2_t interleaved = vld2q_u16(&keys[i * 2]);
                key_vec = interleaved.val[0];  // Keys are at even indices
            } else {
                key_vec = uint16x8_t{keys[i * stride],       keys[(i + 1) * stride], keys[(i + 2) * stride],
                                     keys[(i + 3) * stride], keys[(i + 4) * stride], keys[(i + 5) * stride],
                                     keys[(i + 6) * stride], keys[(i + 7) * stride]};
            }
            uint16x8_t ge = vcgeq_u16(key_vec, target_vec);

            uint8x8_t narrow = vmovn_u16(ge);
            uint64_t mask = vget_lane_u64(vreinterpret_u64_u8(narrow), 0);

            if (mask != 0) {
                return i + static_cast<size_type>(__builtin_ctzll(mask) / 8);
            }
            i += 8;
        }

        for (; i < count; ++i) {
            if (keys[i * stride] >= target) return i;
        }
        return count;
    }

    // NEON lower_bound for int8_t keys (signed) - processes 16 keys at a time
    // Optimized: use direct vld1q load for contiguous keys, vld2q for stride==2, bitmask for first match
    template <typename T>
    static auto neon_lower_bound_s8(const T* slots, size_type count, int8_t target) noexcept -> size_type
    requires(sizeof(T) >= sizeof(int8_t))
    {
        constexpr size_type stride = sizeof(T) / sizeof(int8_t);
        const auto* keys = reinterpret_cast<const int8_t*>(slots);

        int8x16_t target_vec = vdupq_n_s8(target);
        size_type i = 0;

        while (i + 16 <= count) {
            int8x16_t key_vec;
            if constexpr (stride == 1) {
                key_vec = vld1q_s8(&keys[i]);
            } else if constexpr (stride == 2) {
                // Use vld2 for efficient strided load (e.g., btree_map<int8_t, int8_t>)
                int8x16x2_t interleaved = vld2q_s8(&keys[i * 2]);
                key_vec = interleaved.val[0];  // Keys are at even indices
            } else {
                key_vec = int8x16_t{
                  keys[i * stride],        keys[(i + 1) * stride],  keys[(i + 2) * stride],  keys[(i + 3) * stride],
                  keys[(i + 4) * stride],  keys[(i + 5) * stride],  keys[(i + 6) * stride],  keys[(i + 7) * stride],
                  keys[(i + 8) * stride],  keys[(i + 9) * stride],  keys[(i + 10) * stride], keys[(i + 11) * stride],
                  keys[(i + 12) * stride], keys[(i + 13) * stride], keys[(i + 14) * stride], keys[(i + 15) * stride]};
            }
            uint8x16_t ge = vcgeq_s8(key_vec, target_vec);

            uint64_t mask_lo = vgetq_lane_u64(vreinterpretq_u64_u8(ge), 0);
            if (mask_lo != 0) {
                return i + static_cast<size_type>(__builtin_ctzll(mask_lo) / 8);
            }
            uint64_t mask_hi = vgetq_lane_u64(vreinterpretq_u64_u8(ge), 1);
            if (mask_hi != 0) {
                return i + 8 + static_cast<size_type>(__builtin_ctzll(mask_hi) / 8);
            }
            i += 16;
        }

        for (; i < count; ++i) {
            if (keys[i * stride] >= target) return i;
        }
        return count;
    }

    // NEON lower_bound for uint8_t keys (unsigned) - processes 16 keys at a time
    // Optimized: use direct vld1q load for contiguous keys, vld2q for stride==2, bitmask for first match
    template <typename T>
    static auto neon_lower_bound_u8(const T* slots, size_type count, uint8_t target) noexcept -> size_type
    requires(sizeof(T) >= sizeof(uint8_t))
    {
        constexpr size_type stride = sizeof(T) / sizeof(uint8_t);
        const auto* keys = reinterpret_cast<const uint8_t*>(slots);

        uint8x16_t target_vec = vdupq_n_u8(target);
        size_type i = 0;

        while (i + 16 <= count) {
            uint8x16_t key_vec;
            if constexpr (stride == 1) {
                key_vec = vld1q_u8(&keys[i]);
            } else if constexpr (stride == 2) {
                // Use vld2 for efficient strided load (e.g., btree_map<uint8_t, uint8_t>)
                uint8x16x2_t interleaved = vld2q_u8(&keys[i * 2]);
                key_vec = interleaved.val[0];  // Keys are at even indices
            } else {
                key_vec = uint8x16_t{
                  keys[i * stride],        keys[(i + 1) * stride],  keys[(i + 2) * stride],  keys[(i + 3) * stride],
                  keys[(i + 4) * stride],  keys[(i + 5) * stride],  keys[(i + 6) * stride],  keys[(i + 7) * stride],
                  keys[(i + 8) * stride],  keys[(i + 9) * stride],  keys[(i + 10) * stride], keys[(i + 11) * stride],
                  keys[(i + 12) * stride], keys[(i + 13) * stride], keys[(i + 14) * stride], keys[(i + 15) * stride]};
            }
            uint8x16_t ge = vcgeq_u8(key_vec, target_vec);

            uint64_t mask_lo = vgetq_lane_u64(vreinterpretq_u64_u8(ge), 0);
            if (mask_lo != 0) {
                return i + static_cast<size_type>(__builtin_ctzll(mask_lo) / 8);
            }
            uint64_t mask_hi = vgetq_lane_u64(vreinterpretq_u64_u8(ge), 1);
            if (mask_hi != 0) {
                return i + 8 + static_cast<size_type>(__builtin_ctzll(mask_hi) / 8);
            }
            i += 16;
        }

        for (; i < count; ++i) {
            if (keys[i * stride] >= target) return i;
        }
        return count;
    }

    // NEON lower_bound for float keys - processes 4 keys at a time
    // Optimized: use direct vld1q load for contiguous keys, vld2q for stride==2, bitmask for first match
    template <typename T>
    static auto neon_lower_bound_float(const T* slots, size_type count, float target) noexcept -> size_type
    requires(sizeof(T) >= sizeof(float))
    {
        constexpr size_type stride = sizeof(T) / sizeof(float);
        const auto* keys = reinterpret_cast<const float*>(slots);

        float32x4_t target_vec = vdupq_n_f32(target);
        size_type i = 0;

        while (i + 4 <= count) {
            float32x4_t key_vec;
            if constexpr (stride == 1) {
                key_vec = vld1q_f32(&keys[i]);
            } else if constexpr (stride == 2) {
                // Use vld2 for efficient strided load (e.g., btree_map<float, float>)
                float32x4x2_t interleaved = vld2q_f32(&keys[i * 2]);
                key_vec = interleaved.val[0];  // Keys are at even indices
            } else {
                key_vec = float32x4_t{keys[i * stride], keys[(i + 1) * stride], keys[(i + 2) * stride],
                                      keys[(i + 3) * stride]};
            }
            uint32x4_t ge = vcgeq_f32(key_vec, target_vec);

            uint16x4_t narrow = vmovn_u32(ge);
            uint64_t mask = vget_lane_u64(vreinterpret_u64_u16(narrow), 0);

            if (mask != 0) {
                return i + static_cast<size_type>(__builtin_ctzll(mask) / 16);
            }
            i += 4;
        }

        for (; i < count; ++i) {
            if (keys[i * stride] >= target) return i;
        }
        return count;
    }

    // NEON lower_bound for double keys - processes 2 keys at a time
    // Optimized: use direct vld1q load for contiguous keys, bitmask for first match
    template <typename T>
    static auto neon_lower_bound_double(const T* slots, size_type count, double target) noexcept -> size_type
    requires(sizeof(T) >= sizeof(double))
    {
        constexpr size_type stride = sizeof(T) / sizeof(double);
        const auto* keys = reinterpret_cast<const double*>(slots);

        float64x2_t target_vec = vdupq_n_f64(target);
        size_type i = 0;

        while (i + 2 <= count) {
            float64x2_t key_vec;
            if constexpr (stride == 1) {
                key_vec = vld1q_f64(&keys[i]);
            } else {
                key_vec = float64x2_t{keys[i * stride], keys[(i + 1) * stride]};
            }
            uint64x2_t ge = vcgeq_f64(key_vec, target_vec);

            uint32x2_t narrow = vmovn_u64(ge);
            uint64_t mask = vget_lane_u64(vreinterpret_u64_u32(narrow), 0);

            if (mask != 0) {
                return i + static_cast<size_type>(__builtin_ctzll(mask) / 32);
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

    // ================================================================================
    // SIMD upper_bound implementations
    // upper_bound finds first position where slot > key (i.e., key < slot)
    // ================================================================================

#ifdef BTREE_HAS_SSE2
    // SSE2 upper_bound for int32_t keys (signed) - finds first slot > target
    template <typename T>
    static auto simd_upper_bound_s32(const T* slots, size_type count, int32_t target) noexcept -> size_type
    requires(sizeof(T) >= sizeof(int32_t))
    {
        constexpr size_type stride = sizeof(T) / sizeof(int32_t);
        const auto* keys = reinterpret_cast<const int32_t*>(slots);

        __m128i target_vec = _mm_set1_epi32(target);
        size_type i = 0;

        while (i + 4 <= count) {
            __m128i key_vec;
            if constexpr (stride == 1) {
                key_vec = _mm_loadu_si128(reinterpret_cast<const __m128i*>(&keys[i]));
            } else {
                key_vec = _mm_set_epi32(keys[(i + 3) * stride], keys[(i + 2) * stride],
                                        keys[(i + 1) * stride], keys[i * stride]);
            }
            // upper_bound: find slot > target, i.e., key_vec > target_vec
            __m128i gt = _mm_cmpgt_epi32(key_vec, target_vec);
            int mask = _mm_movemask_ps(_mm_castsi128_ps(gt));

            if (mask != 0) {
                return i + __builtin_ctz(mask);
            }
            i += 4;
        }

        for (; i < count; ++i) {
            if (keys[i * stride] > target) return i;
        }
        return count;
    }

    // SSE2 upper_bound for uint32_t keys (unsigned)
    template <typename T>
    static auto simd_upper_bound_u32(const T* slots, size_type count, uint32_t target) noexcept -> size_type
    requires(sizeof(T) >= sizeof(uint32_t))
    {
        constexpr size_type stride = sizeof(T) / sizeof(uint32_t);
        const auto* keys = reinterpret_cast<const uint32_t*>(slots);

        // XOR with sign bit to convert unsigned to signed comparison
        __m128i sign_bit = _mm_set1_epi32(static_cast<int32_t>(0x80000000));
        __m128i target_vec = _mm_xor_si128(_mm_set1_epi32(static_cast<int32_t>(target)), sign_bit);
        size_type i = 0;

        while (i + 4 <= count) {
            __m128i key_vec;
            if constexpr (stride == 1) {
                key_vec = _mm_loadu_si128(reinterpret_cast<const __m128i*>(&keys[i]));
            } else {
                key_vec = _mm_set_epi32(static_cast<int32_t>(keys[(i + 3) * stride]),
                                        static_cast<int32_t>(keys[(i + 2) * stride]),
                                        static_cast<int32_t>(keys[(i + 1) * stride]),
                                        static_cast<int32_t>(keys[i * stride]));
            }
            key_vec = _mm_xor_si128(key_vec, sign_bit);
            __m128i gt = _mm_cmpgt_epi32(key_vec, target_vec);
            int mask = _mm_movemask_ps(_mm_castsi128_ps(gt));

            if (mask != 0) {
                return i + __builtin_ctz(mask);
            }
            i += 4;
        }

        for (; i < count; ++i) {
            if (keys[i * stride] > target) return i;
        }
        return count;
    }

    // SSE upper_bound for float keys
    template <typename T>
    static auto simd_upper_bound_float(const T* slots, size_type count, float target) noexcept -> size_type
    requires(sizeof(T) >= sizeof(float))
    {
        constexpr size_type stride = sizeof(T) / sizeof(float);
        const auto* keys = reinterpret_cast<const float*>(slots);

        __m128 target_vec = _mm_set1_ps(target);
        size_type i = 0;

        while (i + 4 <= count) {
            __m128 key_vec;
            if constexpr (stride == 1) {
                key_vec = _mm_loadu_ps(&keys[i]);
            } else {
                key_vec = _mm_set_ps(keys[(i + 3) * stride], keys[(i + 2) * stride],
                                     keys[(i + 1) * stride], keys[i * stride]);
            }
            __m128 gt = _mm_cmpgt_ps(key_vec, target_vec);
            int mask = _mm_movemask_ps(gt);

            if (mask != 0) {
                return i + __builtin_ctz(mask);
            }
            i += 4;
        }

        for (; i < count; ++i) {
            if (keys[i * stride] > target) return i;
        }
        return count;
    }

    // SSE2 upper_bound for double keys
    template <typename T>
    static auto simd_upper_bound_double(const T* slots, size_type count, double target) noexcept -> size_type
    requires(sizeof(T) >= sizeof(double))
    {
        constexpr size_type stride = sizeof(T) / sizeof(double);
        const auto* keys = reinterpret_cast<const double*>(slots);

        __m128d target_vec = _mm_set1_pd(target);
        size_type i = 0;

        while (i + 2 <= count) {
            __m128d key_vec;
            if constexpr (stride == 1) {
                key_vec = _mm_loadu_pd(&keys[i]);
            } else {
                key_vec = _mm_set_pd(keys[(i + 1) * stride], keys[i * stride]);
            }
            __m128d gt = _mm_cmpgt_pd(key_vec, target_vec);
            int mask = _mm_movemask_pd(gt);

            if (mask != 0) {
                return i + __builtin_ctz(mask);
            }
            i += 2;
        }

        if (i < count && keys[i * stride] > target) return i;
        return count;
    }
#endif

#ifdef BTREE_HAS_AVX2
    // AVX2 upper_bound for int64_t keys (signed)
    template <typename T>
    static auto simd_upper_bound_s64(const T* slots, size_type count, int64_t target) noexcept -> size_type
    requires(sizeof(T) >= sizeof(int64_t))
    {
        constexpr size_type stride = sizeof(T) / sizeof(int64_t);
        const auto* keys = reinterpret_cast<const int64_t*>(slots);

        __m256i target_vec = _mm256_set1_epi64x(target);
        size_type i = 0;

        while (i + 4 <= count) {
            __m256i key_vec;
            if constexpr (stride == 1) {
                key_vec = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&keys[i]));
            } else {
                key_vec = _mm256_set_epi64x(keys[(i + 3) * stride], keys[(i + 2) * stride],
                                            keys[(i + 1) * stride], keys[i * stride]);
            }
            __m256i gt = _mm256_cmpgt_epi64(key_vec, target_vec);
            int mask = _mm256_movemask_pd(_mm256_castsi256_pd(gt));

            if (mask != 0) {
                return i + __builtin_ctz(mask);
            }
            i += 4;
        }

        for (; i < count; ++i) {
            if (keys[i * stride] > target) return i;
        }
        return count;
    }

    // AVX2 upper_bound for uint64_t keys (unsigned)
    template <typename T>
    static auto simd_upper_bound_u64(const T* slots, size_type count, uint64_t target) noexcept -> size_type
    requires(sizeof(T) >= sizeof(uint64_t))
    {
        constexpr size_type stride = sizeof(T) / sizeof(uint64_t);
        const auto* keys = reinterpret_cast<const uint64_t*>(slots);

        // XOR with sign bit to convert unsigned to signed comparison
        __m256i sign_bit = _mm256_set1_epi64x(static_cast<int64_t>(0x8000000000000000ULL));
        __m256i target_vec = _mm256_xor_si256(_mm256_set1_epi64x(static_cast<int64_t>(target)), sign_bit);
        size_type i = 0;

        while (i + 4 <= count) {
            __m256i key_vec;
            if constexpr (stride == 1) {
                key_vec = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&keys[i]));
            } else {
                key_vec = _mm256_set_epi64x(static_cast<int64_t>(keys[(i + 3) * stride]),
                                            static_cast<int64_t>(keys[(i + 2) * stride]),
                                            static_cast<int64_t>(keys[(i + 1) * stride]),
                                            static_cast<int64_t>(keys[i * stride]));
            }
            key_vec = _mm256_xor_si256(key_vec, sign_bit);
            __m256i gt = _mm256_cmpgt_epi64(key_vec, target_vec);
            int mask = _mm256_movemask_pd(_mm256_castsi256_pd(gt));

            if (mask != 0) {
                return i + __builtin_ctz(mask);
            }
            i += 4;
        }

        for (; i < count; ++i) {
            if (keys[i * stride] > target) return i;
        }
        return count;
    }
#endif

#ifdef BTREE_HAS_NEON
    // NEON upper_bound for int32_t keys (signed)
    template <typename T>
    static auto neon_upper_bound_s32(const T* slots, size_type count, int32_t target) noexcept -> size_type
    requires(sizeof(T) >= sizeof(int32_t))
    {
        constexpr size_type stride = sizeof(T) / sizeof(int32_t);
        const auto* keys = reinterpret_cast<const int32_t*>(slots);

        int32x4_t target_vec = vdupq_n_s32(target);
        size_type i = 0;

        while (i + 4 <= count) {
            int32x4_t key_vec;
            if constexpr (stride == 1) {
                key_vec = vld1q_s32(&keys[i]);
            } else if constexpr (stride == 2) {
                int32x4x2_t interleaved = vld2q_s32(&keys[i * 2]);
                key_vec = interleaved.val[0];
            } else {
                key_vec = int32x4_t{keys[i * stride], keys[(i + 1) * stride], keys[(i + 2) * stride],
                                    keys[(i + 3) * stride]};
            }
            // upper_bound: find slot > target
            uint32x4_t gt = vcgtq_s32(key_vec, target_vec);

            uint16x4_t narrow = vmovn_u32(gt);
            uint64_t mask = vget_lane_u64(vreinterpret_u64_u16(narrow), 0);

            if (mask != 0) {
                return i + static_cast<size_type>(__builtin_ctzll(mask) / 16);
            }
            i += 4;
        }

        for (; i < count; ++i) {
            if (keys[i * stride] > target) return i;
        }
        return count;
    }

    // NEON upper_bound for uint32_t keys (unsigned)
    template <typename T>
    static auto neon_upper_bound_u32(const T* slots, size_type count, uint32_t target) noexcept -> size_type
    requires(sizeof(T) >= sizeof(uint32_t))
    {
        constexpr size_type stride = sizeof(T) / sizeof(uint32_t);
        const auto* keys = reinterpret_cast<const uint32_t*>(slots);

        uint32x4_t target_vec = vdupq_n_u32(target);
        size_type i = 0;

        while (i + 4 <= count) {
            uint32x4_t key_vec;
            if constexpr (stride == 1) {
                key_vec = vld1q_u32(&keys[i]);
            } else if constexpr (stride == 2) {
                uint32x4x2_t interleaved = vld2q_u32(&keys[i * 2]);
                key_vec = interleaved.val[0];
            } else {
                key_vec = uint32x4_t{keys[i * stride], keys[(i + 1) * stride], keys[(i + 2) * stride],
                                     keys[(i + 3) * stride]};
            }
            uint32x4_t gt = vcgtq_u32(key_vec, target_vec);

            uint16x4_t narrow = vmovn_u32(gt);
            uint64_t mask = vget_lane_u64(vreinterpret_u64_u16(narrow), 0);

            if (mask != 0) {
                return i + static_cast<size_type>(__builtin_ctzll(mask) / 16);
            }
            i += 4;
        }

        for (; i < count; ++i) {
            if (keys[i * stride] > target) return i;
        }
        return count;
    }

    // NEON upper_bound for int64_t keys (signed)
    template <typename T>
    static auto neon_upper_bound_s64(const T* slots, size_type count, int64_t target) noexcept -> size_type
    requires(sizeof(T) >= sizeof(int64_t))
    {
        constexpr size_type stride = sizeof(T) / sizeof(int64_t);
        const auto* keys = reinterpret_cast<const int64_t*>(slots);

        int64x2_t target_vec = vdupq_n_s64(target);
        size_type i = 0;

        while (i + 2 <= count) {
            int64x2_t key_vec;
            if constexpr (stride == 1) {
                key_vec = vld1q_s64(&keys[i]);
            } else {
                key_vec = int64x2_t{keys[i * stride], keys[(i + 1) * stride]};
            }
            uint64x2_t gt = vcgtq_s64(key_vec, target_vec);

            uint32x2_t narrow = vmovn_u64(gt);
            uint64_t mask = vget_lane_u64(vreinterpret_u64_u32(narrow), 0);

            if (mask != 0) {
                return i + static_cast<size_type>(__builtin_ctzll(mask) / 32);
            }
            i += 2;
        }

        if (i < count && keys[i * stride] > target) return i;
        return count;
    }

    // NEON upper_bound for uint64_t keys (unsigned)
    template <typename T>
    static auto neon_upper_bound_u64(const T* slots, size_type count, uint64_t target) noexcept -> size_type
    requires(sizeof(T) >= sizeof(uint64_t))
    {
        constexpr size_type stride = sizeof(T) / sizeof(uint64_t);
        const auto* keys = reinterpret_cast<const uint64_t*>(slots);

        uint64x2_t target_vec = vdupq_n_u64(target);
        size_type i = 0;

        while (i + 2 <= count) {
            uint64x2_t key_vec;
            if constexpr (stride == 1) {
                key_vec = vld1q_u64(&keys[i]);
            } else {
                key_vec = uint64x2_t{keys[i * stride], keys[(i + 1) * stride]};
            }
            uint64x2_t gt = vcgtq_u64(key_vec, target_vec);

            uint32x2_t narrow = vmovn_u64(gt);
            uint64_t mask = vget_lane_u64(vreinterpret_u64_u32(narrow), 0);

            if (mask != 0) {
                return i + static_cast<size_type>(__builtin_ctzll(mask) / 32);
            }
            i += 2;
        }

        if (i < count && keys[i * stride] > target) return i;
        return count;
    }

    // NEON upper_bound for float keys
    template <typename T>
    static auto neon_upper_bound_float(const T* slots, size_type count, float target) noexcept -> size_type
    requires(sizeof(T) >= sizeof(float))
    {
        constexpr size_type stride = sizeof(T) / sizeof(float);
        const auto* keys = reinterpret_cast<const float*>(slots);

        float32x4_t target_vec = vdupq_n_f32(target);
        size_type i = 0;

        while (i + 4 <= count) {
            float32x4_t key_vec;
            if constexpr (stride == 1) {
                key_vec = vld1q_f32(&keys[i]);
            } else {
                key_vec = float32x4_t{keys[i * stride], keys[(i + 1) * stride], keys[(i + 2) * stride],
                                      keys[(i + 3) * stride]};
            }
            uint32x4_t gt = vcgtq_f32(key_vec, target_vec);

            uint16x4_t narrow = vmovn_u32(gt);
            uint64_t mask = vget_lane_u64(vreinterpret_u64_u16(narrow), 0);

            if (mask != 0) {
                return i + static_cast<size_type>(__builtin_ctzll(mask) / 16);
            }
            i += 4;
        }

        for (; i < count; ++i) {
            if (keys[i * stride] > target) return i;
        }
        return count;
    }

    // NEON upper_bound for double keys
    template <typename T>
    static auto neon_upper_bound_double(const T* slots, size_type count, double target) noexcept -> size_type
    requires(sizeof(T) >= sizeof(double))
    {
        constexpr size_type stride = sizeof(T) / sizeof(double);
        const auto* keys = reinterpret_cast<const double*>(slots);

        float64x2_t target_vec = vdupq_n_f64(target);
        size_type i = 0;

        while (i + 2 <= count) {
            float64x2_t key_vec;
            if constexpr (stride == 1) {
                key_vec = vld1q_f64(&keys[i]);
            } else {
                key_vec = float64x2_t{keys[i * stride], keys[(i + 1) * stride]};
            }
            uint64x2_t gt = vcgtq_f64(key_vec, target_vec);

            uint32x2_t narrow = vmovn_u64(gt);
            uint64_t mask = vget_lane_u64(vreinterpret_u64_u32(narrow), 0);

            if (mask != 0) {
                return i + static_cast<size_type>(__builtin_ctzll(mask) / 32);
            }
            i += 2;
        }

        if (i < count && keys[i * stride] > target) return i;
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

    // Linear search for upper_bound within a node
    // Returns first position where slot > key (i.e., key < slot)
    template <typename Slots>
    [[nodiscard]] __attribute__((always_inline, flatten)) auto linear_search_upper_bound_in_slots(
      const Slots* __restrict__ slots, size_type count, const Key& __restrict__ key) const noexcept -> size_type {
        BTREE_ASSUME(count <= 32);
        for (size_type i = 0; i < count; ++i) {
            if (_comp(key, slots[i].first)) {  // key < slot
                return i;
            }
        }
        return count;
    }

    // Binary search for upper_bound within a node
    // Returns first position where slot > key (i.e., key < slot)
    template <typename Slots>
    [[nodiscard]] __attribute__((always_inline, flatten)) auto binary_search_upper_bound_in_slots(
      const Slots* __restrict__ slots, size_type count, const Key& __restrict__ key) const noexcept -> size_type {
        BTREE_ASSUME(count <= 32);

        size_type lo = 0;
        size_type hi = count;
        while (lo < hi) {
            size_type mid = lo + ((hi - lo) >> 1);
            if (_comp(key, slots[mid].first)) {  // key < slot[mid]
                hi = mid;
            } else {
                lo = mid + 1;
            }
        }
        return lo;
    }

    // Three-way binary search for string-like types
    // Returns {position, exact_match} - avoids extra equality check after search
    // This is faster for strings because compare() gives <0/0/>0 in one call
    template <typename Slots>
    [[nodiscard]] __attribute__((always_inline, flatten)) auto binary_search_three_way(
      const Slots* __restrict__ slots, size_type count, const Key& __restrict__ key) const noexcept
      -> std::pair<size_type, bool> {
        BTREE_ASSUME(count <= 32);

        size_type lo = 0;
        size_type hi = count;
        while (lo < hi) {
            size_type mid = lo + ((hi - lo) >> 1);
            int cmp = key.compare(slots[mid].first);
            if (cmp > 0) {
                lo = mid + 1;
            } else if (cmp < 0) {
                hi = mid;
            } else {
                return {mid, true};  // Exact match found during search
            }
        }
        return {lo, false};  // No exact match
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
#if defined(BTREE_HAS_AVX2) && !defined(BTREE_HAS_AVX512)
        // AVX2 paths (process 8 elements for 32-bit)
        if constexpr (std::is_same_v<Key, int32_t> && std::is_same_v<Compare, std::less<Key>>) {
            return simd_lower_bound_s32_avx2(leaf->slots, leaf->count, key);
        }
        if constexpr (std::is_same_v<Key, uint32_t> && std::is_same_v<Compare, std::less<Key>>) {
            return simd_lower_bound_u32_avx2(leaf->slots, leaf->count, key);
        }
        if constexpr (std::is_same_v<Key, int64_t> && std::is_same_v<Compare, std::less<Key>>) {
            return simd_lower_bound_s64(leaf->slots, leaf->count, key);
        }
        if constexpr (std::is_same_v<Key, uint64_t> && std::is_same_v<Compare, std::less<Key>>) {
            return simd_lower_bound_u64(leaf->slots, leaf->count, key);
        }
        if constexpr (std::is_same_v<Key, float> && std::is_same_v<Compare, std::less<Key>>) {
            return simd_lower_bound_float(leaf->slots, leaf->count, key);
        }
        if constexpr (std::is_same_v<Key, double> && std::is_same_v<Compare, std::less<Key>>) {
            return simd_lower_bound_double_avx(leaf->slots, leaf->count, key);
        }
#elif defined(BTREE_HAS_SSE2) && !defined(BTREE_HAS_AVX512)
        // SSE2 fallback (no AVX2)
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
#if defined(BTREE_HAS_AVX2) && !defined(BTREE_HAS_AVX512)
        // AVX2 paths (process 8 elements for 32-bit)
        if constexpr (std::is_same_v<Key, int32_t> && std::is_same_v<Compare, std::less<Key>>) {
            return simd_lower_bound_s32_avx2(internal->slots, internal->count, key);
        }
        if constexpr (std::is_same_v<Key, uint32_t> && std::is_same_v<Compare, std::less<Key>>) {
            return simd_lower_bound_u32_avx2(internal->slots, internal->count, key);
        }
        if constexpr (std::is_same_v<Key, int64_t> && std::is_same_v<Compare, std::less<Key>>) {
            return simd_lower_bound_s64(internal->slots, internal->count, key);
        }
        if constexpr (std::is_same_v<Key, uint64_t> && std::is_same_v<Compare, std::less<Key>>) {
            return simd_lower_bound_u64(internal->slots, internal->count, key);
        }
        if constexpr (std::is_same_v<Key, float> && std::is_same_v<Compare, std::less<Key>>) {
            return simd_lower_bound_float(internal->slots, internal->count, key);
        }
        if constexpr (std::is_same_v<Key, double> && std::is_same_v<Compare, std::less<Key>>) {
            return simd_lower_bound_double_avx(internal->slots, internal->count, key);
        }
#elif defined(BTREE_HAS_SSE2) && !defined(BTREE_HAS_AVX512)
        // SSE2 fallback (no AVX2)
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

    // Upper bound search in leaf node - finds first slot > key
    // SIMD-optimized for common integer/float types with std::less comparator
    [[nodiscard]] __attribute__((always_inline, flatten)) auto upper_bound_in_leaf(
      const leaf_node* __restrict__ leaf, const Key& __restrict__ key) const noexcept -> size_type {
        // x86 SIMD paths (SSE2/AVX2)
#ifdef BTREE_HAS_SSE2
#if defined(BTREE_HAS_AVX2)
        // AVX2 paths for 64-bit types
        if constexpr (std::is_same_v<Key, int64_t> && std::is_same_v<Compare, std::less<Key>>) {
            return simd_upper_bound_s64(leaf->slots, leaf->count, key);
        }
        if constexpr (std::is_same_v<Key, uint64_t> && std::is_same_v<Compare, std::less<Key>>) {
            return simd_upper_bound_u64(leaf->slots, leaf->count, key);
        }
#endif
        // SSE2 paths for 32-bit types
        if constexpr (std::is_same_v<Key, int32_t> && std::is_same_v<Compare, std::less<Key>>) {
            return simd_upper_bound_s32(leaf->slots, leaf->count, key);
        }
        if constexpr (std::is_same_v<Key, uint32_t> && std::is_same_v<Compare, std::less<Key>>) {
            return simd_upper_bound_u32(leaf->slots, leaf->count, key);
        }
        if constexpr (std::is_same_v<Key, float> && std::is_same_v<Compare, std::less<Key>>) {
            return simd_upper_bound_float(leaf->slots, leaf->count, key);
        }
        if constexpr (std::is_same_v<Key, double> && std::is_same_v<Compare, std::less<Key>>) {
            return simd_upper_bound_double(leaf->slots, leaf->count, key);
        }
#endif
        // ARM NEON paths
#ifdef BTREE_HAS_NEON
        if constexpr (std::is_same_v<Key, int32_t> && std::is_same_v<Compare, std::less<Key>>) {
            return neon_upper_bound_s32(leaf->slots, leaf->count, key);
        }
        if constexpr (std::is_same_v<Key, uint32_t> && std::is_same_v<Compare, std::less<Key>>) {
            return neon_upper_bound_u32(leaf->slots, leaf->count, key);
        }
        if constexpr (std::is_same_v<Key, int64_t> && std::is_same_v<Compare, std::less<Key>>) {
            return neon_upper_bound_s64(leaf->slots, leaf->count, key);
        }
        if constexpr (std::is_same_v<Key, uint64_t> && std::is_same_v<Compare, std::less<Key>>) {
            return neon_upper_bound_u64(leaf->slots, leaf->count, key);
        }
        if constexpr (std::is_same_v<Key, float> && std::is_same_v<Compare, std::less<Key>>) {
            return neon_upper_bound_float(leaf->slots, leaf->count, key);
        }
        if constexpr (std::is_same_v<Key, double> && std::is_same_v<Compare, std::less<Key>>) {
            return neon_upper_bound_double(leaf->slots, leaf->count, key);
        }
#endif
        // For string-like types, binary search reduces expensive comparisons
        if constexpr (string_like<Key>) {
            return binary_search_upper_bound_in_slots(leaf->slots, leaf->count, key);
        }
        // Linear search for small fixed-size types
        return linear_search_upper_bound_in_slots(leaf->slots, leaf->count, key);
    }

    // Upper bound search in internal node - finds first slot > key
    // SIMD-optimized for common integer/float types with std::less comparator
    [[nodiscard]] __attribute__((always_inline, flatten)) auto upper_bound_in_internal(
      const internal_node* __restrict__ internal, const Key& __restrict__ key) const noexcept -> size_type {
        // x86 SIMD paths (SSE2/AVX2)
#ifdef BTREE_HAS_SSE2
#if defined(BTREE_HAS_AVX2)
        // AVX2 paths for 64-bit types
        if constexpr (std::is_same_v<Key, int64_t> && std::is_same_v<Compare, std::less<Key>>) {
            return simd_upper_bound_s64(internal->slots, internal->count, key);
        }
        if constexpr (std::is_same_v<Key, uint64_t> && std::is_same_v<Compare, std::less<Key>>) {
            return simd_upper_bound_u64(internal->slots, internal->count, key);
        }
#endif
        // SSE2 paths for 32-bit types
        if constexpr (std::is_same_v<Key, int32_t> && std::is_same_v<Compare, std::less<Key>>) {
            return simd_upper_bound_s32(internal->slots, internal->count, key);
        }
        if constexpr (std::is_same_v<Key, uint32_t> && std::is_same_v<Compare, std::less<Key>>) {
            return simd_upper_bound_u32(internal->slots, internal->count, key);
        }
        if constexpr (std::is_same_v<Key, float> && std::is_same_v<Compare, std::less<Key>>) {
            return simd_upper_bound_float(internal->slots, internal->count, key);
        }
        if constexpr (std::is_same_v<Key, double> && std::is_same_v<Compare, std::less<Key>>) {
            return simd_upper_bound_double(internal->slots, internal->count, key);
        }
#endif
        // ARM NEON paths
#ifdef BTREE_HAS_NEON
        if constexpr (std::is_same_v<Key, int32_t> && std::is_same_v<Compare, std::less<Key>>) {
            return neon_upper_bound_s32(internal->slots, internal->count, key);
        }
        if constexpr (std::is_same_v<Key, uint32_t> && std::is_same_v<Compare, std::less<Key>>) {
            return neon_upper_bound_u32(internal->slots, internal->count, key);
        }
        if constexpr (std::is_same_v<Key, int64_t> && std::is_same_v<Compare, std::less<Key>>) {
            return neon_upper_bound_s64(internal->slots, internal->count, key);
        }
        if constexpr (std::is_same_v<Key, uint64_t> && std::is_same_v<Compare, std::less<Key>>) {
            return neon_upper_bound_u64(internal->slots, internal->count, key);
        }
        if constexpr (std::is_same_v<Key, float> && std::is_same_v<Compare, std::less<Key>>) {
            return neon_upper_bound_float(internal->slots, internal->count, key);
        }
        if constexpr (std::is_same_v<Key, double> && std::is_same_v<Compare, std::less<Key>>) {
            return neon_upper_bound_double(internal->slots, internal->count, key);
        }
#endif
        // For string-like types, binary search reduces expensive comparisons
        if constexpr (string_like<Key>) {
            return binary_search_upper_bound_in_slots(internal->slots, internal->count, key);
        }
        // Linear search for non-string types
        return linear_search_upper_bound_in_slots(internal->slots, internal->count, key);
    }

    // Generic upper bound search - uses node type dispatch
    [[nodiscard]] auto upper_bound_in_node(const node_base* node, const Key& key) const noexcept -> size_type {
        if (node->is_leaf_node()) {
            return upper_bound_in_leaf(static_cast<const leaf_node*>(node), key);
        }
        return upper_bound_in_internal(static_cast<const internal_node*>(node), key);
    }

    // Three-way search for leaf node (string-like types only)
    // Returns {position, exact_match} - uses compare() to avoid extra equality check
    [[nodiscard]] __attribute__((always_inline, flatten)) auto lower_bound_with_match_in_leaf(
      const leaf_node* __restrict__ leaf, const Key& __restrict__ key) const noexcept
      -> std::pair<size_type, bool> {
        static_assert(string_like<Key>, "Only for string-like types");
        return binary_search_three_way(leaf->slots, leaf->count, key);
    }

    // Three-way search for internal node (string-like types only)
    [[nodiscard]] __attribute__((always_inline, flatten)) auto lower_bound_with_match_in_internal(
      const internal_node* __restrict__ internal, const Key& __restrict__ key) const noexcept
      -> std::pair<size_type, bool> {
        static_assert(string_like<Key>, "Only for string-like types");
        return binary_search_three_way(internal->slots, internal->count, key);
    }

    // Create a new leaf node
    [[nodiscard]] auto create_leaf() -> leaf_node* {
        leaf_node* ptr = std::allocator_traits<leaf_allocator_type>::allocate(_leaf_alloc, 1);
        std::allocator_traits<leaf_allocator_type>::construct(_leaf_alloc, ptr);
        return ptr;
    }

    // Create a new internal node
    [[nodiscard]] auto create_internal() -> internal_node* {
        internal_node* ptr = std::allocator_traits<internal_allocator_type>::allocate(_internal_alloc, 1);
        std::allocator_traits<internal_allocator_type>::construct(_internal_alloc, ptr);
        return ptr;
    }

    // Destroy a leaf node
    void destroy_leaf(leaf_node* node) {
        std::allocator_traits<leaf_allocator_type>::destroy(_leaf_alloc, node);
        std::allocator_traits<leaf_allocator_type>::deallocate(_leaf_alloc, node, 1);
    }

    // Destroy an internal node
    void destroy_internal(internal_node* node) {
        std::allocator_traits<internal_allocator_type>::destroy(_internal_alloc, node);
        std::allocator_traits<internal_allocator_type>::deallocate(_internal_alloc, node, 1);
    }

    // Destroy a node recursively
    void destroy_node(node_base* node) {
        if (node == nullptr) return;

        if (!node->is_leaf_node()) {
            auto* internal = static_cast<internal_node*>(node);
            for (size_type i = 0; i <= internal->count; ++i) {
                destroy_node(internal->children[i]);
            }
            destroy_internal(internal);
        } else {
            destroy_leaf(static_cast<leaf_node*>(node));
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

    // ==================== Rebalance Before Split Helpers ====================

    // Move elements from a full leaf to its left sibling to make room for insertion
    // Returns the new insertion position (may be in left sibling or original node)
    // to_move: number of elements to move (including the separator)
    void rebalance_leaf_right_to_left(leaf_node* leaf, leaf_node* left_sibling, internal_node* parent,
                                      size_type parent_pos, size_type to_move) {
        // Move parent separator down to left sibling's end
        left_sibling->slots[left_sibling->count] = std::move(parent->slots[parent_pos - 1]);
        ++left_sibling->count;

        // Move (to_move - 1) elements from leaf to left sibling
        for (size_type i = 0; i < to_move - 1; ++i) {
            left_sibling->slots[left_sibling->count + i] = std::move(leaf->slots[i]);
        }
        left_sibling->count += static_cast<uint16_t>(to_move - 1);

        // New separator is the element at position (to_move - 1) in leaf
        parent->slots[parent_pos - 1] = std::move(leaf->slots[to_move - 1]);

        // Shift remaining elements in leaf to the beginning
        size_type remaining = leaf->count - to_move;
        if constexpr (std::is_trivially_copyable_v<storage_type>) {
            std::memmove(&leaf->slots[0], &leaf->slots[to_move], remaining * sizeof(storage_type));
        } else {
            for (size_type i = 0; i < remaining; ++i) {
                leaf->slots[i] = std::move(leaf->slots[to_move + i]);
            }
        }
        leaf->count = static_cast<uint16_t>(remaining);
    }

    // Move elements from a full leaf to its right sibling to make room for insertion
    void rebalance_leaf_left_to_right(leaf_node* leaf, leaf_node* right_sibling, internal_node* parent,
                                      size_type parent_pos, size_type to_move) {
        // Shift right sibling elements to make room
        size_type right_count = right_sibling->count;
        if constexpr (std::is_trivially_copyable_v<storage_type>) {
            std::memmove(&right_sibling->slots[to_move], &right_sibling->slots[0], right_count * sizeof(storage_type));
        } else {
            for (size_type i = right_count; i > 0; --i) {
                right_sibling->slots[i + to_move - 1] = std::move(right_sibling->slots[i - 1]);
            }
        }

        // Move parent separator to right sibling's first position
        right_sibling->slots[to_move - 1] = std::move(parent->slots[parent_pos]);

        // Move (to_move - 1) elements from leaf end to right sibling beginning
        size_type start = leaf->count - (to_move - 1);
        for (size_type i = 0; i < to_move - 1; ++i) {
            right_sibling->slots[i] = std::move(leaf->slots[start + i]);
        }
        right_sibling->count += static_cast<uint16_t>(to_move);

        // New separator is the element before what we moved
        parent->slots[parent_pos] = std::move(leaf->slots[start - 1]);
        leaf->count = static_cast<uint16_t>(start - 1);
    }

    // Move elements from a full internal node to its left sibling
    void rebalance_internal_right_to_left(internal_node* node, internal_node* left_sibling, internal_node* parent,
                                          size_type parent_pos, size_type to_move) {
        // Move parent separator down to left sibling
        left_sibling->slots[left_sibling->count] = std::move(parent->slots[parent_pos - 1]);
        ++left_sibling->count;

        // Move children and slots from node to left sibling
        left_sibling->children[left_sibling->count] = node->children[0];
        if (left_sibling->children[left_sibling->count]) {
            left_sibling->children[left_sibling->count]->parent = left_sibling;
            left_sibling->children[left_sibling->count]->position = static_cast<uint16_t>(left_sibling->count);
        }

        for (size_type i = 0; i < to_move - 1; ++i) {
            left_sibling->slots[left_sibling->count + i] = std::move(node->slots[i]);
            left_sibling->children[left_sibling->count + i + 1] = node->children[i + 1];
            if (left_sibling->children[left_sibling->count + i + 1]) {
                left_sibling->children[left_sibling->count + i + 1]->parent = left_sibling;
                left_sibling->children[left_sibling->count + i + 1]->position =
                  static_cast<uint16_t>(left_sibling->count + i + 1);
            }
        }
        left_sibling->count += static_cast<uint16_t>(to_move - 1);

        // New separator
        parent->slots[parent_pos - 1] = std::move(node->slots[to_move - 1]);

        // Shift remaining elements in node
        size_type remaining = node->count - to_move;
        for (size_type i = 0; i < remaining; ++i) {
            node->slots[i] = std::move(node->slots[to_move + i]);
            node->children[i] = node->children[to_move + i];
            if (node->children[i]) {
                node->children[i]->position = static_cast<uint16_t>(i);
            }
        }
        node->children[remaining] = node->children[node->count];
        if (node->children[remaining]) {
            node->children[remaining]->position = static_cast<uint16_t>(remaining);
        }
        node->count = static_cast<uint16_t>(remaining);
    }

    // Move elements from a full internal node to its right sibling
    void rebalance_internal_left_to_right(internal_node* node, internal_node* right_sibling, internal_node* parent,
                                          size_type parent_pos, size_type to_move) {
        size_type right_count = right_sibling->count;

        // Shift right sibling elements to make room
        for (size_type i = right_count; i > 0; --i) {
            right_sibling->slots[i + to_move - 1] = std::move(right_sibling->slots[i - 1]);
            right_sibling->children[i + to_move] = right_sibling->children[i];
            if (right_sibling->children[i + to_move]) {
                right_sibling->children[i + to_move]->position = static_cast<uint16_t>(i + to_move);
            }
        }
        right_sibling->children[to_move] = right_sibling->children[0];
        if (right_sibling->children[to_move]) {
            right_sibling->children[to_move]->position = static_cast<uint16_t>(to_move);
        }

        // Move parent separator to right sibling
        right_sibling->slots[to_move - 1] = std::move(parent->slots[parent_pos]);

        // Move elements and children from node to right sibling
        size_type start = node->count - (to_move - 1);
        for (size_type i = 0; i < to_move - 1; ++i) {
            right_sibling->slots[i] = std::move(node->slots[start + i]);
            right_sibling->children[i] = node->children[start + i];
            if (right_sibling->children[i]) {
                right_sibling->children[i]->parent = right_sibling;
                right_sibling->children[i]->position = static_cast<uint16_t>(i);
            }
        }
        right_sibling->children[to_move - 1] = node->children[node->count];
        if (right_sibling->children[to_move - 1]) {
            right_sibling->children[to_move - 1]->parent = right_sibling;
            right_sibling->children[to_move - 1]->position = static_cast<uint16_t>(to_move - 1);
        }
        right_sibling->count += static_cast<uint16_t>(to_move);

        // New separator
        parent->slots[parent_pos] = std::move(node->slots[start - 1]);
        node->count = static_cast<uint16_t>(start - 1);
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
    // Optimized: single copy operation instead of copy-then-shift
    auto split_leaf(leaf_node* left) -> std::tuple<leaf_node*, Key, Value> {
        auto* right = create_leaf();
        size_type mid = left->count / 2;

        // Extract median first (the element at mid goes up to parent)
        Key median_key = std::move(left->slots[mid].first);
        Value median_value = std::move(left->slots[mid].second);

        // Move elements after median directly to right node (skip the two-step process)
        size_type right_count = left->count - mid - 1;  // Exclude median
        if constexpr (std::is_trivially_copyable_v<storage_type>) {
            if (right_count > 0) {
                std::memcpy(&right->slots[0], &left->slots[mid + 1], right_count * sizeof(storage_type));
            }
        } else {
            for (size_type i = 0; i < right_count; ++i) {
                right->slots[i] = std::move(left->slots[mid + 1 + i]);
            }
        }

        right->count = static_cast<uint16_t>(right_count);
        left->count = static_cast<uint16_t>(mid);

        return {right, std::move(median_key), std::move(median_value)};
    }

    // Split a full internal node
    // Optimized: use memcpy for slots and children pointers
    auto split_internal(internal_node* left) -> std::tuple<internal_node*, Key, Value> {
        auto* right = create_internal();
        size_type mid = left->count / 2;

        // Median goes up
        Key median_key = std::move(left->slots[mid].first);
        Value median_value = std::move(left->slots[mid].second);

        // Move upper half to right node (excluding median)
        size_type right_count = left->count - mid - 1;
        if constexpr (std::is_trivially_copyable_v<storage_type>) {
            if (right_count > 0) {
                std::memcpy(&right->slots[0], &left->slots[mid + 1], right_count * sizeof(storage_type));
            }
        } else {
            for (size_type i = 0; i < right_count; ++i) {
                right->slots[i] = std::move(left->slots[mid + 1 + i]);
            }
        }

        // Move children pointers with memcpy, then update parent/position
        size_type children_count = right_count + 1;
        std::memcpy(&right->children[0], &left->children[mid + 1], children_count * sizeof(node_base*));

        // Update parent and position for moved children, clear old pointers
        for (size_type i = 0; i < children_count; ++i) {
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

            // Node is full - try to rebalance to siblings before splitting
            // For string types: skip rebalance for non-rightmost inserts (cache miss overhead > benefit)
            if (leaf->parent != nullptr) {
                auto* parent = static_cast<internal_node*>(leaf->parent);
                size_type node_pos = leaf->position;
                bool is_rightmost = (node_pos == parent->count);
                bool is_append = (pos == leaf->count);

                // For strings, skip rebalance for random inserts (high cache miss cost)
                // Only consider rebalance for rightmost append (sorted insert pattern)
                if constexpr (string_like<Key>) {
                    if (!(is_rightmost && is_append)) {
                        goto do_split;
                    }
                }

                // Try left sibling first (skip for rightmost append - just split is faster)
                if (node_pos > 0 && !(is_rightmost && is_append)) {
                    auto* left_sibling = static_cast<leaf_node*>(parent->children[node_pos - 1]);
                    size_type left_space = kLeafSlots - left_sibling->count;
                    if (left_space >= 1) {
                        size_type to_move = 1;
                        [[maybe_unused]] size_type new_leaf_count = leaf->count - to_move;

                        if (pos == 0 && left_space >= 2) {
                            size_type old_left_count = left_sibling->count;
                            rebalance_leaf_right_to_left(leaf, left_sibling, parent, node_pos, to_move);
                            insert_into_leaf(left_sibling, old_left_count + 1, std::forward<K>(key), std::forward<V>(value));
                            return {left_sibling, old_left_count + 1};
                        } else if (pos >= to_move) {
                            rebalance_leaf_right_to_left(leaf, left_sibling, parent, node_pos, to_move);
                            size_type new_pos = pos - to_move;
                            insert_into_leaf(leaf, new_pos, std::forward<K>(key), std::forward<V>(value));
                            return {leaf, new_pos};
                        }
                    }
                }

                // Try right sibling
                if (node_pos < parent->count) {
                    auto* right_sibling = static_cast<leaf_node*>(parent->children[node_pos + 1]);
                    size_type right_space = kLeafSlots - right_sibling->count;
                    if (right_space >= 1) {
                        size_type to_move = 1;
                        size_type new_leaf_count = leaf->count - to_move;

                        if (is_append && right_space >= 2) {
                            rebalance_leaf_left_to_right(leaf, right_sibling, parent, node_pos, to_move);
                            insert_into_leaf(right_sibling, 0, std::forward<K>(key), std::forward<V>(value));
                            return {right_sibling, 0};
                        } else if (pos <= new_leaf_count) {
                            rebalance_leaf_left_to_right(leaf, right_sibling, parent, node_pos, to_move);
                            insert_into_leaf(leaf, pos, std::forward<K>(key), std::forward<V>(value));
                            return {leaf, pos};
                        }
                    }
                }
            }

            do_split:
            // Rebalancing not possible - need to split
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

            // Update rightmost leaf cache if we just split the rightmost leaf
            if (leaf == _rightmost_leaf) {
                _rightmost_leaf = new_right;
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

            // Node is full - try to rebalance to siblings before splitting
            if (internal->parent != nullptr) {
                auto* parent_node = static_cast<internal_node*>(internal->parent);
                size_type node_pos = internal->position;

                // Try left sibling first
                if (node_pos > 0) {
                    auto* left_sibling = static_cast<internal_node*>(parent_node->children[node_pos - 1]);
                    size_type left_space = kInternalSlots - left_sibling->count;
                    if (left_space >= 1) {
                        size_type to_move = 1;  // Move just 1 to make room
                        [[maybe_unused]] size_type new_node_count = internal->count - to_move;

                        if (pos == 0 && left_space >= 2) {
                            // Element goes into left sibling
                            size_type old_left_count = left_sibling->count;
                            rebalance_internal_right_to_left(internal, left_sibling, parent_node, node_pos, to_move);
                            insert_into_internal(left_sibling, old_left_count + 1, std::forward<K>(key),
                                                 std::forward<V>(value), right_child);
                            return {left_sibling, old_left_count + 1};
                        } else if (pos >= to_move) {
                            // Element stays in internal
                            rebalance_internal_right_to_left(internal, left_sibling, parent_node, node_pos, to_move);
                            size_type new_pos = pos - to_move;
                            insert_into_internal(internal, new_pos, std::forward<K>(key), std::forward<V>(value),
                                                 right_child);
                            return {internal, new_pos};
                        }
                    }
                }

                // Try right sibling
                if (node_pos < parent_node->count) {
                    auto* right_sibling = static_cast<internal_node*>(parent_node->children[node_pos + 1]);
                    size_type right_space = kInternalSlots - right_sibling->count;
                    if (right_space >= 1) {
                        size_type to_move = 1;
                        size_type new_node_count = internal->count - to_move;

                        if (pos == internal->count && right_space >= 2) {
                            // Element goes into right sibling
                            rebalance_internal_left_to_right(internal, right_sibling, parent_node, node_pos, to_move);
                            insert_into_internal(right_sibling, 0, std::forward<K>(key), std::forward<V>(value),
                                                 right_child);
                            return {right_sibling, 0};
                        } else if (pos <= new_node_count) {
                            // Element stays in internal
                            rebalance_internal_left_to_right(internal, right_sibling, parent_node, node_pos, to_move);
                            insert_into_internal(internal, pos, std::forward<K>(key), std::forward<V>(value),
                                                 right_child);
                            return {internal, pos};
                        }
                    }
                }
            }

            // Rebalancing not possible - need to split
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
        size_type remaining = leaf->count - 1 - pos;
        if (remaining > 0) {
            if constexpr (std::is_trivially_copyable_v<storage_type>) {
                std::memmove(&leaf->slots[pos], &leaf->slots[pos + 1], remaining * sizeof(storage_type));
            } else {
                for (size_type i = pos; i < leaf->count - 1; ++i) {
                    leaf->slots[i] = std::move(leaf->slots[i + 1]);
                }
            }
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
        if (leaf->count > 0) {
            if constexpr (std::is_trivially_copyable_v<storage_type>) {
                std::memmove(&leaf->slots[1], &leaf->slots[0], leaf->count * sizeof(storage_type));
            } else {
                for (size_type i = leaf->count; i > 0; --i) {
                    leaf->slots[i] = std::move(leaf->slots[i - 1]);
                }
            }
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
        size_type remaining = right_sibling->count - 1;
        if (remaining > 0) {
            if constexpr (std::is_trivially_copyable_v<storage_type>) {
                std::memmove(&right_sibling->slots[0], &right_sibling->slots[1], remaining * sizeof(storage_type));
            } else {
                for (size_type i = 0; i < remaining; ++i) {
                    right_sibling->slots[i] = std::move(right_sibling->slots[i + 1]);
                }
            }
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
        if (right->count > 0) {
            if constexpr (std::is_trivially_copyable_v<storage_type>) {
                std::memcpy(&left->slots[left->count], &right->slots[0], right->count * sizeof(storage_type));
            } else {
                for (size_type i = 0; i < right->count; ++i) {
                    left->slots[left->count + i] = std::move(right->slots[i]);
                }
            }
        }
        left->count += right->count;

        // Remove separator from parent (this also removes children[parent_pos + 1])
        remove_slot_from_internal(parent, parent_pos);

        // Update rightmost leaf cache if we're deleting the rightmost leaf
        if (right == _rightmost_leaf) {
            _rightmost_leaf = left;
        }

        // Delete right node
        destroy_leaf(right);
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
        destroy_internal(right);
    }

    // Rebalance after deletion - handles underflow and tracks iterator position
    // res_node/res_pos: on entry, the position of the "next" element (in the erased leaf)
    //                   on exit, adjusted to reflect moves during rebalancing
    void rebalance_after_erase_with_iterator(node_base* node, node_base*& res_node, size_type& res_pos) {
        bool first_iteration = true;

        while (node != _root) {
            auto* parent = static_cast<internal_node*>(node->parent);
            size_type pos = node->position;

            bool is_leaf = node->is_leaf_node();
            size_type min_slots = is_leaf ? kMinLeafSlots : kMinInternalSlots;

            if (node->count >= min_slots) [[likely]] {
                return;  // No underflow
            }

            // Track if result is in this node (only matters on first iteration for leaves)
            bool result_in_node = first_iteration && is_leaf && (res_node == node);

            // Try to borrow from left sibling
            if (pos > 0) {
                node_base* left_sibling = parent->children[pos - 1];
                if (is_leaf) {
                    auto* left_leaf = static_cast<leaf_node*>(left_sibling);
                    if (can_spare_leaf(left_leaf)) {
                        borrow_from_left_leaf(static_cast<leaf_node*>(node), left_leaf, parent, pos);
                        // Borrow from left shifts all elements right by 1
                        if (result_in_node) {
                            res_pos += 1;
                        }
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
                        // Borrow from right doesn't change positions of existing elements
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
                    auto* left_leaf = static_cast<leaf_node*>(left_sibling);
                    size_type left_count = left_leaf->count;
                    merge_leaves(left_leaf, static_cast<leaf_node*>(node), parent, pos - 1);
                    // Elements moved to left: new_pos = left_count + 1 (separator) + old_pos
                    if (result_in_node) {
                        res_node = left_sibling;
                        res_pos = left_count + 1 + res_pos;
                    }
                } else {
                    merge_internal_nodes(static_cast<internal_node*>(left_sibling), static_cast<internal_node*>(node),
                                         parent, pos - 1);
                }
            } else {
                // Merge with right sibling
                node_base* right_sibling = parent->children[pos + 1];
                if (is_leaf) {
                    auto* right_leaf = static_cast<leaf_node*>(right_sibling);
                    // Check if result is in the right sibling
                    bool result_in_right = first_iteration && (res_node == right_sibling);
                    size_type node_count = static_cast<leaf_node*>(node)->count;
                    merge_leaves(static_cast<leaf_node*>(node), right_leaf, parent, pos);
                    // Right's elements moved to us: new_pos = node_count + 1 (separator) + old_pos
                    if (result_in_right) {
                        res_node = node;
                        res_pos = node_count + 1 + res_pos;
                    }
                    // If result was in our node, position doesn't change
                } else {
                    merge_internal_nodes(static_cast<internal_node*>(node), static_cast<internal_node*>(right_sibling),
                                         parent, pos);
                }
            }

            first_iteration = false;

            // Check if parent needs rebalancing
            if (parent == _root && parent->count == 0) {
                // Root became empty, promote the merged child as new root
                _root = parent->children[0];
                if (_root) {
                    _root->parent = nullptr;
                }
                destroy_internal(parent);
                return;
            }

            node = parent;
        }
    }

    // Original version without iterator tracking (for internal use)
    void rebalance_after_erase(node_base* node) {
        node_base* dummy_node = nullptr;
        size_type dummy_pos = 0;
        rebalance_after_erase_with_iterator(node, dummy_node, dummy_pos);
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
                destroy_leaf(leaf);
                _root = nullptr;
                _rightmost_leaf = nullptr;
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
    // Iterator class - supports both map mode (returns pair) and set mode (returns key)
    class iterator
    {
       public:
        using iterator_category = std::bidirectional_iterator_tag;
        using difference_type = std::ptrdiff_t;
        // In set mode, value_type is const Key; in map mode, it's pair<const Key, Value>
        using value_type = std::conditional_t<is_set_mode, const Key, std::pair<const Key, Value>>;
        using pointer = std::conditional_t<is_set_mode, const Key*, value_type*>;
        using reference = std::conditional_t<is_set_mode, const Key&, value_type&>;

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

                // Past end of this leaf, look for next in parents
                node_base* current = leaf;
                while (current->parent != nullptr) {
                    size_type parent_pos = current->position;
                    auto* parent = current->parent;
                    if (parent_pos < parent->count) {
                        // Found next key in parent
                        _node = parent;
                        _pos = parent_pos;
                        return;
                    }
                    current = parent;
                }
                // End of tree - stay at this leaf with pos = count (end representation)
                // _node is still leaf, _pos is already count
                return;
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
                        // Return to parent key at pos = child_pos - 1
                        _pos = child_pos - 1;
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
            if constexpr (is_set_mode) {
                // Set mode: return just the key
                if (_node->is_leaf_node()) {
                    return static_cast<leaf_node*>(_node)->slots[_pos].first;
                }
                return static_cast<internal_node*>(_node)->slots[_pos].first;
            } else {
                // Map mode: return the pair
                if (_node->is_leaf_node()) {
                    auto* leaf = static_cast<leaf_node*>(_node);
                    return reinterpret_cast<reference>(leaf->slots[_pos]);
                }
                auto* internal = static_cast<internal_node*>(_node);
                return reinterpret_cast<reference>(internal->slots[_pos]);
            }
        }

        [[nodiscard]] auto operator->() const -> pointer {
            if constexpr (is_set_mode) {
                if (_node->is_leaf_node()) {
                    return &static_cast<leaf_node*>(_node)->slots[_pos].first;
                }
                return &static_cast<internal_node*>(_node)->slots[_pos].first;
            } else {
                return &**this;
            }
        }

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

        // O(1) difference when iterators are in the same leaf node
        // Falls back to optimized tree traversal otherwise
        friend auto operator-(const iterator& a, const iterator& b) -> difference_type {
            if (a._node == b._node && a._node != nullptr && a._node->is_leaf_node()) {
                return static_cast<difference_type>(a._pos) - static_cast<difference_type>(b._pos);
            }
            return distance_slow(a, b);
        }

      private:
        // Optimized distance calculation using tree structure
        // Based on absl's btree_iterator::distance_slow algorithm
        // Complexity: O(log n + number of leaf nodes traversed) instead of O(k)
        static auto distance_slow(const iterator& end_it, const iterator& begin_it) -> difference_type {
            const node_base* end_node = end_it._node;
            size_type end_pos = end_it._pos;

            const node_base* node = begin_it._node;
            // Compensate for double counting if begin is in a leaf node
            difference_type count = node->is_leaf_node()
                                        ? -static_cast<difference_type>(begin_it._pos)
                                        : 0;

            // If begin is in internal node, count its key and go to next subtree
            if (!node->is_leaf_node()) {
                ++count;
                node = static_cast<const internal_node*>(node)->children[begin_it._pos + 1];
            }

            // Navigate to leftmost leaf
            while (!node->is_leaf_node()) {
                node = static_cast<const internal_node*>(node)->children[0];
            }

            // Now traverse leaf nodes, counting elements
            size_type pos = node->position;
            const node_base* parent = node->parent;

            for (;;) {
                // Count leaf nodes going right within current parent
                while (pos <= parent->count) {
                    node = static_cast<const internal_node*>(parent)->children[pos];

                    // Navigate to leftmost leaf if internal
                    if (!node->is_leaf_node()) {
                        while (!node->is_leaf_node()) {
                            node = static_cast<const internal_node*>(node)->children[0];
                        }
                        pos = node->position;
                        parent = node->parent;
                    }

                    // Check if we reached end
                    if (node == end_node) {
                        return count + static_cast<difference_type>(end_pos);
                    }
                    if (parent == end_node && pos == end_pos) {
                        return count + static_cast<difference_type>(node->count);
                    }

                    // Add this leaf's count + 1 for parent's key
                    count += node->count + 1;
                    ++pos;
                }

                // Go up to find next sibling
                while (parent->parent != nullptr) {
                    node = parent;
                    pos = node->position;
                    parent = node->parent;

                    // Check if end is at this internal position
                    if (parent == end_node && pos == end_pos) {
                        return count - 1;  // -1 because we over-counted
                    }

                    ++pos;
                    if (pos <= parent->count) {
                        break;  // Found next sibling
                    }
                }

                // If we've exhausted the tree, we're done
                if (parent->parent == nullptr && pos > parent->count) {
                    return count;
                }
            }
        }
    };

    class const_iterator
    {
       public:
        using iterator_category = std::bidirectional_iterator_tag;
        using difference_type = std::ptrdiff_t;
        // In set mode, value_type is const Key; in map mode, it's const pair<const Key, Value>
        using value_type = std::conditional_t<is_set_mode, const Key, const std::pair<const Key, Value>>;
        using pointer = std::conditional_t<is_set_mode, const Key*, const value_type*>;
        using reference = std::conditional_t<is_set_mode, const Key&, const value_type&>;

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

                // Past end of this leaf, look for next in parents
                const node_base* current = leaf;
                while (current->parent != nullptr) {
                    size_type parent_pos = current->position;
                    auto* parent = current->parent;
                    if (parent_pos < parent->count) {
                        _node = parent;
                        _pos = parent_pos;
                        return;
                    }
                    current = parent;
                }
                // End of tree - stay at this leaf with pos = count.
                //
                // Say it, do not just intend it. _pos was incremented above, so it is count + 1 here
                // whenever we entered at count -- and entering at count is exactly what end() is, since
                // end() is const_iterator(_rightmost_leaf, _rightmost_leaf->count). Leaving _pos there
                // makes the incremented iterator compare *unequal* to end(), so a `while (it != end())`
                // loop runs on past the last element and dereferences the leaf beyond its keys.
                //
                // For an iterator that was on a real element this assignment changes nothing: _pos was
                // count - 1, ++ made it count, and count is what we want. It only matters when the
                // iterator was already at the end, and then it is what keeps ++end() == end().
                _pos = leaf->count;
                return;
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
            if constexpr (is_set_mode) {
                // Set mode: return just the key
                if (_node->is_leaf_node()) {
                    return static_cast<const leaf_node*>(_node)->slots[_pos].first;
                }
                return static_cast<const internal_node*>(_node)->slots[_pos].first;
            } else {
                // Map mode: return the pair
                if (_node->is_leaf_node()) {
                    auto* leaf = static_cast<const leaf_node*>(_node);
                    return reinterpret_cast<reference>(leaf->slots[_pos]);
                }
                auto* internal = static_cast<const internal_node*>(_node);
                return reinterpret_cast<reference>(internal->slots[_pos]);
            }
        }

        [[nodiscard]] auto operator->() const -> pointer {
            if constexpr (is_set_mode) {
                if (_node->is_leaf_node()) {
                    return &static_cast<const leaf_node*>(_node)->slots[_pos].first;
                }
                return &static_cast<const internal_node*>(_node)->slots[_pos].first;
            } else {
                return &**this;
            }
        }

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

        // O(1) difference when iterators are in the same leaf node
        // Falls back to optimized tree traversal otherwise
        friend auto operator-(const const_iterator& a, const const_iterator& b) -> difference_type {
            if (a._node == b._node && a._node != nullptr && a._node->is_leaf_node()) {
                return static_cast<difference_type>(a._pos) - static_cast<difference_type>(b._pos);
            }
            return distance_slow(a, b);
        }

      private:
        // Optimized distance calculation using tree structure
        // Based on absl's btree_iterator::distance_slow algorithm
        static auto distance_slow(const const_iterator& end_it, const const_iterator& begin_it) -> difference_type {
            const node_base* end_node = end_it._node;
            size_type end_pos = end_it._pos;

            const node_base* node = begin_it._node;
            difference_type count = node->is_leaf_node()
                                        ? -static_cast<difference_type>(begin_it._pos)
                                        : 0;

            if (!node->is_leaf_node()) {
                ++count;
                node = static_cast<const internal_node*>(node)->children[begin_it._pos + 1];
            }

            while (!node->is_leaf_node()) {
                node = static_cast<const internal_node*>(node)->children[0];
            }

            size_type pos = node->position;
            const node_base* parent = node->parent;

            for (;;) {
                while (pos <= parent->count) {
                    node = static_cast<const internal_node*>(parent)->children[pos];

                    if (!node->is_leaf_node()) {
                        while (!node->is_leaf_node()) {
                            node = static_cast<const internal_node*>(node)->children[0];
                        }
                        pos = node->position;
                        parent = node->parent;
                    }

                    if (node == end_node) {
                        return count + static_cast<difference_type>(end_pos);
                    }
                    if (parent == end_node && pos == end_pos) {
                        return count + static_cast<difference_type>(node->count);
                    }

                    count += node->count + 1;
                    ++pos;
                }

                while (parent->parent != nullptr) {
                    node = parent;
                    pos = node->position;
                    parent = node->parent;

                    if (parent == end_node && pos == end_pos) {
                        return count - 1;
                    }

                    ++pos;
                    if (pos <= parent->count) {
                        break;
                    }
                }

                if (parent->parent == nullptr && pos > parent->count) {
                    return count;
                }
            }
        }
    };

    // Custom reverse iterator (std::reverse_iterator doesn't work with our end() iterator)
    class reverse_iterator
    {
       public:
        using iterator_category = std::bidirectional_iterator_tag;
        using difference_type = std::ptrdiff_t;
        // In set mode, value_type is const Key; in map mode, it's pair<const Key, Value>
        using value_type = std::conditional_t<is_set_mode, const Key, std::pair<const Key, Value>>;
        using pointer = std::conditional_t<is_set_mode, const Key*, value_type*>;
        using reference = std::conditional_t<is_set_mode, const Key&, value_type&>;

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
            if constexpr (is_set_mode) {
                if (_node->is_leaf_node()) {
                    return static_cast<leaf_node*>(_node)->slots[_pos].first;
                }
                return static_cast<internal_node*>(_node)->slots[_pos].first;
            } else {
                if (_node->is_leaf_node()) {
                    return reinterpret_cast<reference>(static_cast<leaf_node*>(_node)->slots[_pos]);
                }
                return reinterpret_cast<reference>(static_cast<internal_node*>(_node)->slots[_pos]);
            }
        }

        [[nodiscard]] auto operator->() const -> pointer {
            if constexpr (is_set_mode) {
                if (_node->is_leaf_node()) {
                    return &static_cast<leaf_node*>(_node)->slots[_pos].first;
                }
                return &static_cast<internal_node*>(_node)->slots[_pos].first;
            } else {
                return &**this;
            }
        }

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

        [[nodiscard]] auto base() const -> iterator {
            // Standard semantics: *rit == *std::prev(rit.base())
            // So base() returns iterator one position ahead
            iterator it(_node, _pos);
            ++it;
            return it;
        }

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
        // In set mode, value_type is const Key; in map mode, it's const pair<const Key, Value>
        using value_type = std::conditional_t<is_set_mode, const Key, const std::pair<const Key, Value>>;
        using pointer = std::conditional_t<is_set_mode, const Key*, const value_type*>;
        using reference = std::conditional_t<is_set_mode, const Key&, const value_type&>;

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
            if constexpr (is_set_mode) {
                if (_node->is_leaf_node()) {
                    return static_cast<const leaf_node*>(_node)->slots[_pos].first;
                }
                return static_cast<const internal_node*>(_node)->slots[_pos].first;
            } else {
                if (_node->is_leaf_node()) {
                    return reinterpret_cast<reference>(static_cast<const leaf_node*>(_node)->slots[_pos]);
                }
                return reinterpret_cast<reference>(static_cast<const internal_node*>(_node)->slots[_pos]);
            }
        }

        [[nodiscard]] auto operator->() const -> pointer {
            if constexpr (is_set_mode) {
                if (_node->is_leaf_node()) {
                    return &static_cast<const leaf_node*>(_node)->slots[_pos].first;
                }
                return &static_cast<const internal_node*>(_node)->slots[_pos].first;
            } else {
                return &**this;
            }
        }

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

        [[nodiscard]] auto base() const -> const_iterator {
            // Standard semantics: *rit == *std::prev(rit.base())
            // So base() returns iterator one position ahead
            const_iterator it(_node, _pos);
            ++it;
            return it;
        }

        friend auto operator==(const const_reverse_iterator& a, const const_reverse_iterator& b) -> bool {
            if (a._is_end && b._is_end) return true;
            if (a._is_end || b._is_end) return false;
            return a._node == b._node && a._pos == b._pos;
        }

        friend auto operator!=(const const_reverse_iterator& a, const const_reverse_iterator& b) -> bool {
            return !(a == b);
        }
    };

    // Insert return type for node handle insertion (C++17)
    struct insert_return_type {
        iterator position;
        bool inserted;
        node_type node;
    };

    // Constructors
    btree_map() = default;

    explicit btree_map(const Compare& comp, const Allocator& alloc = Allocator())
        : _comp(comp), _leaf_alloc(alloc), _internal_alloc(alloc) {}

    explicit btree_map(const Allocator& alloc)
        : _leaf_alloc(alloc), _internal_alloc(alloc) {}

    btree_map(std::initializer_list<value_type> init, const Compare& comp = Compare(),
              const Allocator& alloc = Allocator())
        : _comp(comp), _leaf_alloc(alloc), _internal_alloc(alloc) {
        if constexpr (is_set_mode) {
            for (const auto& key : init) {
                insert(key);
            }
        } else {
            for (const auto& [k, v] : init) {
                insert(k, v);
            }
        }
    }

    btree_map(std::initializer_list<value_type> init, const Allocator& alloc)
        : _leaf_alloc(alloc), _internal_alloc(alloc) {
        if constexpr (is_set_mode) {
            for (const auto& key : init) {
                insert(key);
            }
        } else {
            for (const auto& [k, v] : init) {
                insert(k, v);
            }
        }
    }

    // Range constructor - construct from iterator range
    template <typename InputIt>
        requires requires(InputIt it) {
            { *it } -> std::convertible_to<value_type>;
            ++it;
        }
    btree_map(InputIt first, InputIt last, const Compare& comp = Compare(),
              const Allocator& alloc = Allocator())
        : _comp(comp), _leaf_alloc(alloc), _internal_alloc(alloc) {
        for (; first != last; ++first) {
            insert(*first);
        }
    }

    template <typename InputIt>
        requires requires(InputIt it) {
            { *it } -> std::convertible_to<value_type>;
            ++it;
        }
    btree_map(InputIt first, InputIt last, const Allocator& alloc)
        : _leaf_alloc(alloc), _internal_alloc(alloc) {
        for (; first != last; ++first) {
            insert(*first);
        }
    }

    ~btree_map() { destroy_node(_root); }

    // Copy constructor - O(n) deep copy of tree structure
    btree_map(const btree_map& other)
        : _size(other._size),
          _comp(other._comp),
          _leaf_alloc(std::allocator_traits<leaf_allocator_type>::select_on_container_copy_construction(
              other._leaf_alloc)),
          _internal_alloc(std::allocator_traits<internal_allocator_type>::select_on_container_copy_construction(
              other._internal_alloc)) {
        _root = deep_copy_node(other._root, nullptr);
        _rightmost_leaf = const_cast<leaf_node*>(rightmost_leaf());
    }

    // Copy constructor with allocator
    btree_map(const btree_map& other, const Allocator& alloc)
        : _size(other._size), _comp(other._comp), _leaf_alloc(alloc), _internal_alloc(alloc) {
        _root = deep_copy_node(other._root, nullptr);
        _rightmost_leaf = const_cast<leaf_node*>(rightmost_leaf());
    }

    // Move constructor
    btree_map(btree_map&& other) noexcept
        : _root(other._root),
          _rightmost_leaf(other._rightmost_leaf),
          _size(other._size),
          _comp(std::move(other._comp)),
          _leaf_alloc(std::move(other._leaf_alloc)),
          _internal_alloc(std::move(other._internal_alloc)) {
        other._root = nullptr;
        other._rightmost_leaf = nullptr;
        other._size = 0;
    }

    // Move constructor with allocator
    btree_map(btree_map&& other, const Allocator& alloc)
        : _comp(std::move(other._comp)), _leaf_alloc(alloc), _internal_alloc(alloc) {
        if (_leaf_alloc == other._leaf_alloc) {
            // Same allocator - can steal resources
            _root = other._root;
            _rightmost_leaf = other._rightmost_leaf;
            _size = other._size;
            other._root = nullptr;
            other._rightmost_leaf = nullptr;
            other._size = 0;
        } else {
            // Different allocator - must copy elements
            _size = 0;
            for (auto&& [k, v] : other) {
                insert(std::move(const_cast<Key&>(k)), std::move(v));
            }
            other.clear();
        }
    }

    // Copy assignment - O(n) deep copy of tree structure
    auto operator=(const btree_map& other) -> btree_map& {
        if (this != &other) {
            using AllocTraits = std::allocator_traits<Allocator>;
            if constexpr (AllocTraits::propagate_on_container_copy_assignment::value) {
                // Allocator propagates: destroy with old allocator, then copy allocator
                destroy_node(_root);
                _leaf_alloc = other._leaf_alloc;
                _internal_alloc = other._internal_alloc;
            } else {
                // Allocator does not propagate (PMR): keep our allocator
                destroy_node(_root);
            }
            _comp = other._comp;
            _size = other._size;
            _root = deep_copy_node(other._root, nullptr);
            _rightmost_leaf = const_cast<leaf_node*>(rightmost_leaf());
        }
        return *this;
    }

    // Move assignment
    auto operator=(btree_map&& other) noexcept(
        std::allocator_traits<Allocator>::propagate_on_container_move_assignment::value ||
        std::allocator_traits<Allocator>::is_always_equal::value) -> btree_map& {
        if (this != &other) {
            using AllocTraits = std::allocator_traits<Allocator>;
            _comp = std::move(other._comp);

            if constexpr (AllocTraits::propagate_on_container_move_assignment::value) {
                // Allocator propagates: destroy with old allocator, steal resources, take allocator
                destroy_node(_root);
                _leaf_alloc = std::move(other._leaf_alloc);
                _internal_alloc = std::move(other._internal_alloc);
                move_resources_from(other);
            } else if constexpr (AllocTraits::is_always_equal::value) {
                // Allocators are always equal: can steal resources
                destroy_node(_root);
                move_resources_from(other);
            } else {
                // Allocators may differ (PMR case)
                if (_leaf_alloc == other._leaf_alloc) {
                    // Allocators are equal: can steal resources
                    destroy_node(_root);
                    move_resources_from(other);
                } else {
                    // Allocators differ: must move elements individually
                    clear();
                    for (auto&& [k, v] : other) {
                        if constexpr (is_set_mode) {
                            insert(std::move(const_cast<Key&>(k)));
                        } else {
                            insert(std::move(const_cast<Key&>(k)), std::move(const_cast<Value&>(v)));
                        }
                    }
                    other.clear();
                }
            }
        }
        return *this;
    }

private:
    // Helper: steal resources from other (used when allocators are equal)
    void move_resources_from(btree_map& other) noexcept {
        _root = other._root;
        _rightmost_leaf = other._rightmost_leaf;
        _size = other._size;
        other._root = nullptr;
        other._rightmost_leaf = nullptr;
        other._size = 0;
    }

public:

    // Capacity
    [[nodiscard]] auto empty() const noexcept -> bool { return _size == 0; }
    [[nodiscard]] auto size() const noexcept -> size_type { return _size; }

    // Clear all elements
    void clear() noexcept {
        destroy_node(_root);
        _root = nullptr;
        _rightmost_leaf = nullptr;
        _size = 0;
    }

    // Insert a key-value pair with perfect forwarding (map mode)
    template <typename K, typename V>
        requires(!is_set_mode) && std::is_convertible_v<K&&, Key> && std::is_convertible_v<V&&, Value>
    auto insert(K&& key, V&& value) -> std::pair<iterator, bool> {
        return insert_impl(std::forward<K>(key), std::forward<V>(value));
    }

    // Insert with value_type (map mode)
    auto insert(const value_type& kv) -> std::pair<iterator, bool>
        requires(!is_set_mode)
    {
        return insert(kv.first, kv.second);
    }

    auto insert(value_type&& kv) -> std::pair<iterator, bool>
        requires(!is_set_mode)
    {
        return insert_impl(std::move(kv.first), std::move(kv.second));
    }

    // Insert key only (set mode)
    auto insert(const Key& key) -> std::pair<iterator, bool>
        requires(is_set_mode)
    {
        return insert_impl(key, btree_set_empty_value{});
    }

    auto insert(Key&& key) -> std::pair<iterator, bool>
        requires(is_set_mode)
    {
        return insert_impl(std::move(key), btree_set_empty_value{});
    }

    // Insert with hint (map mode)
    auto insert(const_iterator hint, const value_type& kv) -> iterator
        requires(!is_set_mode)
    {
        return insert_with_hint_impl(hint, kv.first, kv.second);
    }

    auto insert(const_iterator hint, value_type&& kv) -> iterator
        requires(!is_set_mode)
    {
        return insert_with_hint_impl(hint, std::move(kv.first), std::move(kv.second));
    }

    // Insert with hint (set mode)
    auto insert(const_iterator hint, const Key& key) -> iterator
        requires(is_set_mode)
    {
        return insert_with_hint_impl(hint, key, btree_set_empty_value{});
    }

    auto insert(const_iterator hint, Key&& key) -> iterator
        requires(is_set_mode)
    {
        return insert_with_hint_impl(hint, std::move(key), btree_set_empty_value{});
    }

    // Emplace - construct in place (map mode)
    template <typename... Args>
        requires(!is_set_mode)
    auto emplace(Args&&... args) -> std::pair<iterator, bool> {
        std::pair<const Key, Value> val(std::forward<Args>(args)...);
        return insert_impl(std::move(const_cast<Key&>(val.first)), std::move(val.second));
    }

    // Emplace - construct in place (set mode)
    template <typename... Args>
        requires(is_set_mode)
    auto emplace(Args&&... args) -> std::pair<iterator, bool> {
        Key key(std::forward<Args>(args)...);
        return insert_impl(std::move(key), btree_set_empty_value{});
    }

    // emplace_hint - uses hint for O(1) amortized insertion for sequential patterns (C++11) (map mode)
    // Optimized overload for (key, value) case - avoids temporary pair construction
    // Inlined fast path for end() hint to minimize overhead for sorted insert pattern
    template <typename K, typename V>
        requires(!is_set_mode && std::is_constructible_v<Key, K&&> && std::is_constructible_v<Value, V&&>)
    [[gnu::hot]] auto emplace_hint(const_iterator hint, K&& key, V&& value) -> iterator {
        // Fast path: hint is end() - most common case for sorted insert
        // If key > max, insert directly at rightmost leaf (O(1) amortized)
        if (hint._node == nullptr && _root != nullptr) [[likely]] {
            if (_rightmost_leaf->count > 0 && _comp(_rightmost_leaf->key(_rightmost_leaf->count - 1), key)) [[likely]] {
                auto [inserted_node, inserted_pos] =
                  insert_and_split_impl(_rightmost_leaf, _rightmost_leaf->count, std::forward<K>(key), std::forward<V>(value));
                ++_size;
                return iterator(inserted_node, inserted_pos);
            }
        }
        return insert_with_hint_impl(hint, std::forward<K>(key), std::forward<V>(value));
    }

    // General emplace_hint for constructing pair from arbitrary args (map mode)
    template <typename... Args>
        requires(!is_set_mode && sizeof...(Args) != 2)
    auto emplace_hint(const_iterator hint, Args&&... args) -> iterator {
        std::pair<const Key, Value> val(std::forward<Args>(args)...);
        return insert_with_hint_impl(hint, std::move(const_cast<Key&>(val.first)), std::move(val.second));
    }

    // emplace_hint (set mode)
    template <typename... Args>
        requires(is_set_mode)
    auto emplace_hint(const_iterator hint, Args&&... args) -> iterator {
        Key key(std::forward<Args>(args)...);
        return insert_with_hint_impl(hint, std::move(key), btree_set_empty_value{});
    }

    // insert_or_assign - inserts or updates value (C++17) - map mode only
    // Single traversal implementation for optimal performance
    template <typename M>
        requires(!is_set_mode)
    auto insert_or_assign(const Key& key, M&& value) -> std::pair<iterator, bool> {
        return insert_or_assign_impl(key, std::forward<M>(value));
    }

    template <typename M>
        requires(!is_set_mode)
    auto insert_or_assign(Key&& key, M&& value) -> std::pair<iterator, bool> {
        return insert_or_assign_impl(std::move(key), std::forward<M>(value));
    }

    // insert_or_assign with hint (hint is ignored) - map mode only
    template <typename M>
        requires(!is_set_mode)
    auto insert_or_assign([[maybe_unused]] const_iterator hint, const Key& key, M&& value) -> iterator {
        return insert_or_assign(key, std::forward<M>(value)).first;
    }

    template <typename M>
        requires(!is_set_mode)
    auto insert_or_assign([[maybe_unused]] const_iterator hint, Key&& key, M&& value) -> iterator {
        return insert_or_assign(std::move(key), std::forward<M>(value)).first;
    }

    // try_emplace - only constructs value if key doesn't exist (C++17) - map mode only
    // Single traversal: finds position and inserts in one pass
    template <typename... Args>
        requires(!is_set_mode)
    auto try_emplace(const Key& key, Args&&... args) -> std::pair<iterator, bool> {
        return try_emplace_impl(key, std::forward<Args>(args)...);
    }

    template <typename... Args>
        requires(!is_set_mode)
    auto try_emplace(Key&& key, Args&&... args) -> std::pair<iterator, bool> {
        return try_emplace_impl(std::move(key), std::forward<Args>(args)...);
    }

    // try_emplace with hint (hint is ignored) - map mode only
    template <typename... Args>
        requires(!is_set_mode)
    auto try_emplace([[maybe_unused]] const_iterator hint, const Key& key, Args&&... args) -> iterator {
        return try_emplace(key, std::forward<Args>(args)...).first;
    }

    template <typename... Args>
        requires(!is_set_mode)
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
            _rightmost_leaf = leaf;
            ++_size;
            return {iterator(leaf, 0), true};
        }

        // Fast path: check if key > max key (sequential append case)
        if (_rightmost_leaf->count > 0 && _comp(_rightmost_leaf->key(_rightmost_leaf->count - 1), key)) {
            auto [inserted_node, inserted_pos] =
              insert_and_split_impl(_rightmost_leaf, _rightmost_leaf->count, std::forward<K>(key), std::forward<V>(value));
            ++_size;
            return {iterator(inserted_node, inserted_pos), true};
        }

        // Find insertion point - traverse to leaf using specialized search
        node_base* node = _root;

        // For string-like types, use three-way comparison to avoid extra equality check
        if constexpr (string_like<Key>) {
            while (!node->is_leaf_node()) [[likely]] {
                auto* internal = static_cast<internal_node*>(node);
                auto [pos, exact_match] = lower_bound_with_match_in_internal(internal, key);

                // In unique mode, reject duplicates; in multi mode, allow them
                if constexpr (!is_multi_mode) {
                    if (exact_match) [[unlikely]] {
                        return {iterator(node, pos), false};  // Key already exists
                    }
                }

                node = internal->children[pos];
            }

            // Now at leaf
            auto* leaf = static_cast<leaf_node*>(node);
            auto [pos, exact_match] = lower_bound_with_match_in_leaf(leaf, key);

            // In unique mode, reject duplicates; in multi mode, allow them
            if constexpr (!is_multi_mode) {
                if (exact_match) [[unlikely]] {
                    return {iterator(leaf, pos), false};  // Key already exists
                }
            }

            // Insert and get the position where it was inserted
            auto [inserted_node, inserted_pos] =
              insert_and_split_impl(leaf, pos, std::forward<K>(key), std::forward<V>(value));
            ++_size;

            return {iterator(inserted_node, inserted_pos), true};
        } else {
            while (!node->is_leaf_node()) [[likely]] {
                auto* internal = static_cast<internal_node*>(node);
                size_type pos = lower_bound_in_internal(internal, key);

                // In unique mode, reject duplicates; in multi mode, allow them
                if constexpr (!is_multi_mode) {
                    // Check for exact match (single comparison optimization)
                    if (pos < internal->count && !_comp(key, internal->key(pos))) [[unlikely]] {
                        return {iterator(node, pos), false};  // Key already exists
                    }
                }

                // Prefetch next node before traversing
                __builtin_prefetch(internal->children[pos], 0, 3);
                node = internal->children[pos];
            }

            // Now at leaf - use specialized search
            auto* leaf = static_cast<leaf_node*>(node);
            size_type pos = lower_bound_in_leaf(leaf, key);

            // In unique mode, reject duplicates; in multi mode, allow them
            if constexpr (!is_multi_mode) {
                // Check for exact match
                if (pos < leaf->count && !_comp(key, leaf->key(pos))) [[unlikely]] {
                    return {iterator(leaf, pos), false};  // Key already exists
                }
            }

            // Insert and get the position where it was inserted
            auto [inserted_node, inserted_pos] =
              insert_and_split_impl(leaf, pos, std::forward<K>(key), std::forward<V>(value));
            ++_size;

            return {iterator(inserted_node, inserted_pos), true};
        }
    }

    // try_emplace implementation - single traversal, constructs value only if needed
    template <typename K, typename... Args>
    __attribute__((hot)) auto try_emplace_impl(K&& key, Args&&... args) -> std::pair<iterator, bool> {
        if (_root == nullptr) [[unlikely]] {
            _root = create_leaf();
            auto* leaf = static_cast<leaf_node*>(_root);
            leaf->slots[0] = storage_type(std::forward<K>(key), Value(std::forward<Args>(args)...));
            leaf->count = 1;
            _rightmost_leaf = leaf;
            ++_size;
            return {iterator(leaf, 0), true};
        }

        // Fast path: check if key > max key (sequential append case)
        if constexpr (string_like<Key>) {
            if (!_root->is_leaf_node()) {
                if (_rightmost_leaf->count > 0 && _comp(_rightmost_leaf->key(_rightmost_leaf->count - 1), key)) {
                    auto [inserted_node, inserted_pos] = insert_and_split_impl(
                      _rightmost_leaf, _rightmost_leaf->count, std::forward<K>(key), Value(std::forward<Args>(args)...));
                    ++_size;
                    return {iterator(inserted_node, inserted_pos), true};
                }
            }
        } else {
            if (_rightmost_leaf->count > 0 && _comp(_rightmost_leaf->key(_rightmost_leaf->count - 1), key)) {
                auto [inserted_node, inserted_pos] = insert_and_split_impl(
                  _rightmost_leaf, _rightmost_leaf->count, std::forward<K>(key), Value(std::forward<Args>(args)...));
                ++_size;
                return {iterator(inserted_node, inserted_pos), true};
            }
        }

        // Find insertion point - traverse to leaf
        node_base* node = _root;

        // For string-like types, use three-way comparison to avoid extra equality check
        if constexpr (string_like<Key>) {
            while (!node->is_leaf_node()) [[likely]] {
                auto* internal = static_cast<internal_node*>(node);
                auto [pos, exact_match] = lower_bound_with_match_in_internal(internal, key);

                if (exact_match) [[unlikely]] {
                    return {iterator(node, pos), false};  // Key exists
                }

                node = internal->children[pos];
            }

            auto* leaf = static_cast<leaf_node*>(node);
            auto [pos, exact_match] = lower_bound_with_match_in_leaf(leaf, key);

            if (exact_match) [[unlikely]] {
                return {iterator(leaf, pos), false};  // Key exists
            }

            // Key doesn't exist - now construct value and insert
            auto [inserted_node, inserted_pos] =
              insert_and_split_impl(leaf, pos, std::forward<K>(key), Value(std::forward<Args>(args)...));
            ++_size;

            return {iterator(inserted_node, inserted_pos), true};
        } else {
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
    }

    // insert_or_assign implementation - single traversal, updates if exists, inserts if not
    template <typename K, typename V>
    __attribute__((hot)) auto insert_or_assign_impl(K&& key, V&& value) -> std::pair<iterator, bool> {
        if (_root == nullptr) [[unlikely]] {
            _root = create_leaf();
            auto* leaf = static_cast<leaf_node*>(_root);
            leaf->slots[0] = storage_type(std::forward<K>(key), std::forward<V>(value));
            leaf->count = 1;
            _rightmost_leaf = leaf;
            ++_size;
            return {iterator(leaf, 0), true};
        }

        // Fast path: check if key > max key (sequential append case)
        if constexpr (string_like<Key>) {
            if (!_root->is_leaf_node()) {
                if (_rightmost_leaf->count > 0 && _comp(_rightmost_leaf->key(_rightmost_leaf->count - 1), key)) {
                    auto [inserted_node, inserted_pos] =
                      insert_and_split_impl(_rightmost_leaf, _rightmost_leaf->count, std::forward<K>(key), std::forward<V>(value));
                    ++_size;
                    return {iterator(inserted_node, inserted_pos), true};
                }
            }
        } else {
            if (_rightmost_leaf->count > 0 && _comp(_rightmost_leaf->key(_rightmost_leaf->count - 1), key)) {
                auto [inserted_node, inserted_pos] =
                  insert_and_split_impl(_rightmost_leaf, _rightmost_leaf->count, std::forward<K>(key), std::forward<V>(value));
                ++_size;
                return {iterator(inserted_node, inserted_pos), true};
            }
        }

        // Find insertion point - traverse to leaf
        node_base* node = _root;

        // For string-like types, use three-way comparison to avoid extra equality check
        if constexpr (string_like<Key>) {
            while (!node->is_leaf_node()) [[likely]] {
                auto* internal = static_cast<internal_node*>(node);
                auto [pos, exact_match] = lower_bound_with_match_in_internal(internal, key);

                if (exact_match) [[unlikely]] {
                    // Key exists in internal node - update value
                    internal->value(pos) = std::forward<V>(value);
                    return {iterator(node, pos), false};
                }

                node = internal->children[pos];
            }

            auto* leaf = static_cast<leaf_node*>(node);
            auto [pos, exact_match] = lower_bound_with_match_in_leaf(leaf, key);

            if (exact_match) [[unlikely]] {
                // Key exists in leaf - update value
                leaf->value(pos) = std::forward<V>(value);
                return {iterator(leaf, pos), false};
            }

            // Key doesn't exist - insert
            auto [inserted_node, inserted_pos] =
              insert_and_split_impl(leaf, pos, std::forward<K>(key), std::forward<V>(value));
            ++_size;

            return {iterator(inserted_node, inserted_pos), true};
        } else {
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
    }

    // Insert with hint implementation - O(1) when hint is correct for sequential inserts
    template <typename K, typename V>
    [[gnu::hot]] auto insert_with_hint_impl(const_iterator hint, K&& key, V&& value) -> iterator {
        if (_root == nullptr) [[unlikely]] {
            _root = create_leaf();
            auto* leaf = static_cast<leaf_node*>(_root);
            leaf->slots[0] = storage_type(std::forward<K>(key), std::forward<V>(value));
            leaf->count = 1;
            _rightmost_leaf = leaf;
            ++_size;
            return iterator(leaf, 0);
        }

        // Fast path: check if key > max key (sequential append case)
        // This is the most common case for sorted insert - check it first regardless of hint
        if (_rightmost_leaf->count > 0 && _comp(_rightmost_leaf->key(_rightmost_leaf->count - 1), key)) {
            auto [inserted_node, inserted_pos] =
              insert_and_split_impl(_rightmost_leaf, _rightmost_leaf->count, std::forward<K>(key), std::forward<V>(value));
            ++_size;
            return iterator(inserted_node, inserted_pos);
        }

        // Try to use hint if it points to a valid leaf position
        // For end() hint (most common), this branch is skipped entirely
        if (hint._node != nullptr && hint._node->is_leaf_node()) [[unlikely]] {
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

    // Internal find implementation supporting heterogeneous lookup
    template <typename K>
    [[nodiscard]] auto find_impl(const K& key) -> iterator {
        node_base* node = _root;
        if (node == nullptr) [[unlikely]] {
            return end();
        }

        // Traverse internal nodes
        while (!node->is_leaf_node()) [[likely]] {
            auto* internal = static_cast<internal_node*>(node);
            size_type pos = lower_bound_generic(internal, key);

            // Check for exact match in internal node
            if (pos < internal->count) [[likely]] {
                // Use equivalence check: !comp(a,b) && !comp(b,a)
                if (!_comp(key, internal->key(pos)) && !_comp(internal->key(pos), key)) [[unlikely]] {
                    return iterator(internal, pos);
                }
            }

            node = internal->children[pos];
        }

        // At leaf node
        auto* leaf = static_cast<leaf_node*>(node);
        size_type pos = lower_bound_generic(leaf, key);
        if (pos < leaf->count && !_comp(key, leaf->key(pos)) && !_comp(leaf->key(pos), key)) [[likely]] {
            return iterator(leaf, pos);
        }
        return end();
    }

    // Generic lower_bound for heterogeneous lookup (no SIMD, uses comparator)
    template <typename K, typename Node>
    [[nodiscard]] auto lower_bound_generic(const Node* node, const K& key) const noexcept -> size_type {
        size_type lo = 0;
        size_type hi = node->count;
        while (lo < hi) {
            size_type mid = lo + (hi - lo) / 2;
            if (_comp(node->key(mid), key)) {
                lo = mid + 1;
            } else {
                hi = mid;
            }
        }
        return lo;
    }

    // Generic upper_bound for heterogeneous lookup (no SIMD, uses comparator)
    // Returns first position where slot > key (i.e., key < slot)
    template <typename K, typename Node>
    [[nodiscard]] auto upper_bound_generic(const Node* node, const K& key) const noexcept -> size_type {
        size_type lo = 0;
        size_type hi = node->count;
        while (lo < hi) {
            size_type mid = lo + (hi - lo) / 2;
            if (_comp(key, node->key(mid))) {  // key < slot[mid]
                hi = mid;
            } else {
                lo = mid + 1;
            }
        }
        return lo;
    }

    // Internal lower_bound implementation supporting heterogeneous lookup
    template <typename K>
    [[nodiscard]] auto lower_bound_impl(const K& key) -> iterator {
        if (_root == nullptr) return end();

        node_base* node = _root;
        iterator result = end();

        while (true) {
            if (node->is_leaf_node()) {
                auto* leaf = static_cast<leaf_node*>(node);
                size_type pos = lower_bound_generic(leaf, key);
                if (pos < leaf->count) {
                    return iterator(leaf, pos);
                }
                return result;
            }

            auto* internal = static_cast<internal_node*>(node);
            size_type pos = lower_bound_generic(internal, key);
            if (pos < internal->count) {
                if (!_comp(internal->key(pos), key)) {
                    result = iterator(internal, pos);
                }
            }
            node = internal->children[pos];
        }
    }

    // Internal upper_bound implementation supporting heterogeneous lookup
    template <typename K>
    [[nodiscard]] auto upper_bound_impl(const K& key) -> iterator {
        if (_root == nullptr) return end();

        node_base* node = _root;
        iterator result = end();

        while (true) {
            if (node->is_leaf_node()) {
                auto* leaf = static_cast<leaf_node*>(node);
                size_type pos = upper_bound_generic(leaf, key);
                if (pos < leaf->count) {
                    return iterator(leaf, pos);
                }
                return result;
            }

            auto* internal = static_cast<internal_node*>(node);
            size_type pos = upper_bound_generic(internal, key);
            if (pos < internal->count) {
                // slot[pos] > key, so this is a candidate
                if (_comp(key, internal->key(pos))) {
                    result = iterator(internal, pos);
                }
            }
            node = internal->children[pos];
        }
    }

   public:
    // Find - optimized with type-specialized search and minimal branching
    [[nodiscard]] __attribute__((hot)) auto find(const Key& key) -> iterator {
        node_base* node = _root;
        if (node == nullptr) [[unlikely]] {
            return end();
        }

        // For string-like types, use three-way comparison to avoid extra equality check
        if constexpr (string_like<Key>) {
            while (!node->is_leaf_node()) [[likely]] {
                auto* internal = static_cast<internal_node*>(node);
                auto [pos, exact_match] = lower_bound_with_match_in_internal(internal, key);

                if (exact_match) [[unlikely]] {
                    return iterator(internal, pos);
                }

                node = internal->children[pos];
            }

            auto* leaf = static_cast<leaf_node*>(node);
            auto [pos, exact_match] = lower_bound_with_match_in_leaf(leaf, key);
            if (exact_match) [[likely]] {
                return iterator(leaf, pos);
            }
            return end();
        } else {
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
    }

    [[nodiscard]] auto find(const Key& key) const -> const_iterator {
        return const_iterator(const_cast<btree_map*>(this)->find(key));
    }

    // Heterogeneous lookup - find with transparent comparator
    template <typename K>
        requires is_transparent_comparator_v<Compare>
    [[nodiscard]] auto find(const K& key) -> iterator {
        return find_impl(key);
    }

    template <typename K>
        requires is_transparent_comparator_v<Compare>
    [[nodiscard]] auto find(const K& key) const -> const_iterator {
        return const_iterator(const_cast<btree_map*>(this)->find_impl(key));
    }

    // operator[] - uses try_emplace to avoid constructing Value if key exists (map mode only)
    auto operator[](const Key& key) -> Value&
        requires(!is_set_mode)
    {
        auto [it, inserted] = try_emplace(key);
        if (it._node->is_leaf_node()) {
            return static_cast<leaf_node*>(it._node)->value(it._pos);
        }
        return static_cast<internal_node*>(it._node)->value(it._pos);
    }

    // operator[] with rvalue key - avoids key copy (map mode only)
    auto operator[](Key&& key) -> Value&
        requires(!is_set_mode)
    {
        auto [it, inserted] = try_emplace(std::move(key));
        if (it._node->is_leaf_node()) {
            return static_cast<leaf_node*>(it._node)->value(it._pos);
        }
        return static_cast<internal_node*>(it._node)->value(it._pos);
    }

    // at (map mode only)
    [[nodiscard]] auto at(const Key& key) -> Value&
        requires(!is_set_mode)
    {
        auto it = find(key);
        if (it == end()) {
            throw std::out_of_range("btree_map::at: key not found");
        }
        if (it._node->is_leaf_node()) {
            return static_cast<leaf_node*>(it._node)->value(it._pos);
        }
        return static_cast<internal_node*>(it._node)->value(it._pos);
    }

    [[nodiscard]] auto at(const Key& key) const -> const Value&
        requires(!is_set_mode)
    {
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

    // Heterogeneous contains
    template <typename K>
        requires is_transparent_comparator_v<Compare>
    [[nodiscard]] auto contains(const K& key) const -> bool {
        return const_cast<btree_map*>(this)->find_impl(key) != end();
    }

    // count - returns number of elements with matching key
    // In unique mode: returns 0 or 1
    // In multi mode: optimized counting - avoids iterator traversal when duplicates fit in one leaf
    [[nodiscard]] auto count(const Key& key) const -> size_type {
        if constexpr (is_multi_mode) {
            return count_multi_impl(key);
        } else {
            return contains(key) ? 1 : 0;
        }
    }

    // Heterogeneous count
    template <typename K>
        requires is_transparent_comparator_v<Compare>
    [[nodiscard]] auto count(const K& key) const -> size_type {
        if constexpr (is_multi_mode) {
            return count_multi_impl(key);
        } else {
            return contains(key) ? 1 : 0;
        }
    }

  private:
    // Optimized count for multi mode - single tree traversal with fast path
    template <typename K>
    [[nodiscard]] __attribute__((always_inline, hot)) auto count_multi_impl(const K& key) const -> size_type {
        if (_root == nullptr) [[unlikely]] return 0;

        // Single traversal to find lower_bound leaf position
        const node_base* node = _root;
        while (!node->is_leaf_node()) {
            auto* internal = static_cast<const internal_node*>(node);
            size_type pos = lower_bound_in_internal(internal, key);
            node = internal->children[pos];
        }

        auto* leaf = static_cast<const leaf_node*>(node);
        size_type lb_pos = lower_bound_in_leaf(leaf, key);

        // Check if key exists (90% of benchmark queries are for non-existent keys)
        if (lb_pos >= leaf->count || _comp(key, leaf->key(lb_pos))) [[likely]] {
            return 0;
        }

        // Fast path: count duplicates in same leaf with SIMD upper_bound
        size_type ub_pos = upper_bound_in_leaf(leaf, key);
        if (ub_pos < leaf->count) [[likely]] {
            return ub_pos - lb_pos;
        }

        // Slow path: duplicates extend beyond this leaf
        // Walk through leaves using iterator, but use SIMD per leaf for efficiency
        size_type result = leaf->count - lb_pos;
        const_iterator it(leaf, leaf->count);
        ++it;
        const node_base* current_leaf = leaf;

        while (it != end()) {
            // Check if we've moved to a new leaf
            if (it._node != current_leaf && it._node->is_leaf_node()) {
                current_leaf = it._node;
                auto* iter_leaf = static_cast<const leaf_node*>(current_leaf);

                // Check first element - if it's greater than key, we're done
                if (_comp(key, iter_leaf->key(0))) break;

                // Use SIMD to find upper_bound in this leaf
                size_type ub = upper_bound_in_leaf(iter_leaf, key);
                result += ub;

                // If upper_bound is within leaf, we found all duplicates
                if (ub < iter_leaf->count) break;

                // Move iterator to end of this leaf
                it = const_iterator(iter_leaf, iter_leaf->count);
                ++it;
            } else {
                // Fallback: count one element at a time (handles edge cases)
                const Key& iter_key = get_key_from_iterator(it);
                if (_comp(key, iter_key)) break;
                ++result;
                ++it;
            }
        }
        return result;
    }

    // Helper to get key from iterator position
    template <typename IterT>
    [[nodiscard]] auto get_key_from_iterator(const IterT& it) const -> const Key& {
        if constexpr (is_set_mode) {
            return *it;
        } else {
            return it->first;
        }
    }

    // Optimized equal_range for multi mode - single tree traversal for common case
    template <typename K>
    [[nodiscard]] __attribute__((always_inline, hot)) auto equal_range_multi_impl(const K& key) -> std::pair<iterator, iterator> {
        if (_root == nullptr) [[unlikely]] return {end(), end()};

        // Single traversal to find lower_bound leaf position
        node_base* node = _root;
        while (!node->is_leaf_node()) {
            auto* internal = static_cast<internal_node*>(node);
            size_type pos = lower_bound_in_internal(internal, key);
            if constexpr (!string_like<Key>) {
                __builtin_prefetch(internal->children[pos], 0, 3);
            }
            node = internal->children[pos];
        }

        auto* leaf = static_cast<leaf_node*>(node);
        size_type lb_pos = lower_bound_in_leaf(leaf, key);

        // Check if key exists
        if (lb_pos >= leaf->count || _comp(key, leaf->key(lb_pos))) [[likely]] {
            iterator it(leaf, lb_pos < leaf->count ? lb_pos : leaf->count);
            return {it, it};
        }

        iterator lb_iter(leaf, lb_pos);

        // Fast path: find upper_bound in same leaf with SIMD
        size_type ub_pos = upper_bound_in_leaf(leaf, key);
        if (ub_pos < leaf->count) [[likely]] {
            return {lb_iter, iterator(leaf, ub_pos)};
        }

        // Slow path: duplicates extend beyond this leaf - use regular upper_bound
        // This requires a second tree traversal but handles edge cases correctly
        return {lb_iter, upper_bound(key)};
    }

    template <typename K>
    [[nodiscard]] __attribute__((always_inline, hot)) auto equal_range_multi_impl(const K& key) const -> std::pair<const_iterator, const_iterator> {
        auto result = const_cast<btree_map*>(this)->equal_range_multi_impl(key);
        return {const_iterator(result.first), const_iterator(result.second)};
    }

  public:
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
            // Prefetch next node before traversing (skip for strings - doesn't help)
            if constexpr (!string_like<Key>) {
                __builtin_prefetch(internal->children[pos], 0, 3);
            }
            node = internal->children[pos];
        }
    }

    [[nodiscard]] auto lower_bound(const Key& key) const -> const_iterator {
        return const_iterator(const_cast<btree_map*>(this)->lower_bound(key));
    }

    // upper_bound - finds first element where key < element
    // Uses O(log n) tree traversal instead of iterating through duplicates
    [[nodiscard]] auto upper_bound(const Key& key) -> iterator {
        if (_root == nullptr) return end();

        node_base* node = _root;
        iterator result = end();

        while (true) {
            size_type pos = upper_bound_in_node(node, key);

            if (node->is_leaf_node()) {
                auto* leaf = static_cast<leaf_node*>(node);
                if (pos < leaf->count) {
                    return iterator(leaf, pos);
                }
                return result;
            }

            auto* internal = static_cast<internal_node*>(node);
            if (pos < internal->count) {
                // slot[pos] > key, so this is a candidate
                if (_comp(key, internal->key(pos))) {
                    result = iterator(internal, pos);
                }
            }
            // Prefetch next node before traversing
            if constexpr (!string_like<Key>) {
                __builtin_prefetch(internal->children[pos], 0, 3);
            }
            node = internal->children[pos];
        }
    }

    [[nodiscard]] auto upper_bound(const Key& key) const -> const_iterator {
        return const_iterator(const_cast<btree_map*>(this)->upper_bound(key));
    }

    // Heterogeneous lower_bound
    template <typename K>
        requires is_transparent_comparator_v<Compare>
    [[nodiscard]] auto lower_bound(const K& key) -> iterator {
        return lower_bound_impl(key);
    }

    template <typename K>
        requires is_transparent_comparator_v<Compare>
    [[nodiscard]] auto lower_bound(const K& key) const -> const_iterator {
        return const_iterator(const_cast<btree_map*>(this)->lower_bound_impl(key));
    }

    // Heterogeneous upper_bound
    template <typename K>
        requires is_transparent_comparator_v<Compare>
    [[nodiscard]] auto upper_bound(const K& key) -> iterator {
        return upper_bound_impl(key);
    }

    template <typename K>
        requires is_transparent_comparator_v<Compare>
    [[nodiscard]] auto upper_bound(const K& key) const -> const_iterator {
        return const_iterator(const_cast<btree_map*>(this)->upper_bound_impl(key));
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

    [[nodiscard]] auto end() noexcept -> iterator {
        if (_rightmost_leaf == nullptr) return iterator(nullptr, 0);
        return iterator(static_cast<node_base*>(_rightmost_leaf), _rightmost_leaf->count);
    }

    [[nodiscard]] auto end() const noexcept -> const_iterator {
        if (_rightmost_leaf == nullptr) return const_iterator(nullptr, 0);
        return const_iterator(static_cast<const node_base*>(_rightmost_leaf), _rightmost_leaf->count);
    }

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
    // In unique mode: erases at most 1 element
    // In multi mode: erases all elements with matching key
    auto erase(const Key& key) -> size_type {
        if constexpr (is_multi_mode) {
            // Multi mode: erase all elements with matching key
            size_type erased = 0;
            auto it = find(key);
            while (it != end()) {
                // Get the key from iterator
                const Key& iter_key = [&]() -> const Key& {
                    if constexpr (is_set_mode) {
                        return *it;
                    } else {
                        return it->first;
                    }
                }();
                // Check if still matching
                if (_comp(key, iter_key) || _comp(iter_key, key)) {
                    break;  // No longer matching
                }
                it = erase(it);  // erase(iterator) returns next iterator
                ++erased;
            }
            return erased;
        } else {
            auto it = find(key);
            if (it == end()) {
                return 0;
            }
            erase_impl(it._node, it._pos);
            return 1;
        }
    }

    // Heterogeneous erase by key
    template <typename K>
        requires is_transparent_comparator_v<Compare>
    auto erase(const K& key) -> size_type {
        if constexpr (is_multi_mode) {
            size_type erased = 0;
            auto it = find_impl(key);
            while (it != end()) {
                const Key& iter_key = [&]() -> const Key& {
                    if constexpr (is_set_mode) {
                        return *it;
                    } else {
                        return it->first;
                    }
                }();
                if (_comp(key, iter_key) || _comp(iter_key, key)) {
                    break;
                }
                it = erase(it);
                ++erased;
            }
            return erased;
        } else {
            auto it = find_impl(key);
            if (it == end()) {
                return 0;
            }
            erase_impl(it._node, it._pos);
            return 1;
        }
    }

    // Erase by iterator - returns iterator to next element
    // Simplified structure following absl's approach
    auto erase(iterator pos) -> iterator {
        if (pos == end()) {
            return end();
        }

        // Handle internal node: replace with predecessor, then erase from leaf
        if (!pos._node->is_leaf_node()) {
            iterator next = pos;
            ++next;
            erase_impl(pos._node, pos._pos);
            return next;
        }

        // Leaf node deletion
        auto* leaf = static_cast<leaf_node*>(pos._node);
        size_type erase_pos = pos._pos;

        // Check if rebalancing will be needed BEFORE removing element
        bool will_underflow = (leaf != _root && leaf->count <= kMinLeafSlots);

        // Remove the element
        remove_slot_from_leaf(leaf, erase_pos);
        --_size;

        // Handle empty root
        if (leaf == _root && leaf->count == 0) {
            destroy_leaf(leaf);
            _root = nullptr;
            _rightmost_leaf = nullptr;
            return end();
        }

        // Rebalance if needed
        if (will_underflow) {
            node_base* res_node = leaf;
            size_type res_pos = erase_pos;
            bool was_last = (erase_pos >= leaf->count);
            rebalance_after_erase_with_iterator(leaf, res_node, res_pos);

            if (_root == nullptr) {
                return end();
            }

            auto* res_leaf = static_cast<leaf_node*>(res_node);
            if (was_last || res_pos >= res_leaf->count) {
                if (res_leaf->count > 0) {
                    iterator it(res_leaf, res_leaf->count - 1);
                    ++it;
                    return it;
                }
                return end();
            }
            return iterator(res_node, res_pos);
        }

        // No rebalancing needed - element at erase_pos is now the next element
        if (erase_pos >= leaf->count) {
            // Erased last element - advance to next node
            if (leaf->count > 0) {
                iterator it(leaf, leaf->count - 1);
                ++it;
                return it;
            }
            return end();
        }
        return iterator(leaf, erase_pos);
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

    // Extract node by position (C++17)
    auto extract(const_iterator pos) -> node_type {
        if (pos == end()) {
            return node_type{};
        }

        if constexpr (is_set_mode) {
            // Set mode: just get the key
            Key key = *pos;
            erase(pos);
            return node_type(std::move(key));
        } else {
            // Map mode: get key and value before erasing
            Key key = pos->first;
            Value value = std::move(const_cast<Value&>(pos->second));
            erase(pos);
            return node_type(std::move(key), std::move(value));
        }
    }

    // Extract node by key (C++17)
    auto extract(const Key& key) -> node_type {
        auto it = find(key);
        if (it == end()) {
            return node_type{};
        }
        return extract(it);
    }

    // Heterogeneous extract by key
    template <typename K>
        requires is_transparent_comparator_v<Compare>
    auto extract(const K& key) -> node_type {
        auto it = find_impl(key);
        if (it == end()) {
            return node_type{};
        }
        return extract(const_iterator(it._node, it._pos));
    }

    // Extract and get next iterator (absl extension)
    auto extract_and_get_next(const_iterator pos) -> std::pair<node_type, iterator> {
        if (pos == end()) {
            return {node_type{}, end()};
        }

        if constexpr (is_set_mode) {
            // Set mode: just get the key
            Key key = *pos;
            iterator next = erase(pos);
            return {node_type(std::move(key)), next};
        } else {
            // Map mode: get key and value before erasing
            Key key = pos->first;
            Value value = std::move(const_cast<Value&>(pos->second));
            iterator next = erase(pos);
            return {node_type(std::move(key), std::move(value)), next};
        }
    }

    // Insert node handle (C++17). Constrained so a braced-init-list (e.g.
    // insert({1, "one"})) cannot match — only value_type's overloads can — while
    // staying rvalue-only: requiring NH == node_type (not remove_cvref) deduces to
    // the bare type only for rvalue arguments, so a named lvalue handle is rejected
    // (the caller must std::move it), matching std::map's insert(node_type&&).
    template <typename NH>
        requires std::is_same_v<NH, node_type>
    auto insert(NH&& nh) -> insert_return_type {
        if (nh.empty()) {
            return {end(), false, std::move(nh)};
        }

        if constexpr (is_set_mode) {
            auto [it, inserted] = insert(std::move(nh._storage));
            if (inserted) {
                nh._valid = false;
                return {it, true, node_type{}};
            }
            return {it, false, std::move(nh)};
        } else {
            auto [it, inserted] = insert(std::move(nh._storage.first), std::move(nh._storage.second));
            if (inserted) {
                nh._valid = false;
                return {it, true, node_type{}};
            }
            return {it, false, std::move(nh)};
        }
    }

    // Insert node handle with hint (C++17)
    // Note: hint parameter is for API compatibility, currently ignored
    auto insert([[maybe_unused]] const_iterator hint, node_type&& nh) -> iterator {
        if (nh.empty()) {
            return end();
        }

        auto result = insert(std::move(nh));
        return result.position;
    }

    // equal_range - returns pair of iterators (lower_bound, upper_bound)
    // In unique mode: optimized single traversal
    // In multi mode: optimized single tree traversal with SIMD upper_bound
    [[nodiscard]] auto equal_range(const Key& key) -> std::pair<iterator, iterator> {
        if constexpr (is_multi_mode) {
            // Multi mode: use optimized single-traversal implementation
            return equal_range_multi_impl(key);
        } else {
            auto lb = lower_bound(key);
            // Check if key matches (handle both set mode and map mode)
            auto key_matches = [&]() {
                if (lb == end()) return false;
                if constexpr (is_set_mode) {
                    return !_comp(key, *lb) && !_comp(*lb, key);
                } else {
                    return !_comp(key, lb->first) && !_comp(lb->first, key);
                }
            };
            if (!key_matches()) {
                return {lb, lb};
            }
            // Unique mode: upper_bound is next element
            auto ub = lb;
            ++ub;
            return {lb, ub};
        }
    }

    [[nodiscard]] auto equal_range(const Key& key) const -> std::pair<const_iterator, const_iterator> {
        if constexpr (is_multi_mode) {
            return equal_range_multi_impl(key);
        } else {
            auto lb = lower_bound(key);
            auto key_matches = [&]() {
                if (lb == end()) return false;
                if constexpr (is_set_mode) {
                    return !_comp(key, *lb) && !_comp(*lb, key);
                } else {
                    return !_comp(key, lb->first) && !_comp(lb->first, key);
                }
            };
            if (!key_matches()) {
                return {lb, lb};
            }
            auto ub = lb;
            ++ub;
            return {lb, ub};
        }
    }

    // Heterogeneous equal_range
    template <typename K>
        requires is_transparent_comparator_v<Compare>
    [[nodiscard]] auto equal_range(const K& key) -> std::pair<iterator, iterator> {
        if constexpr (is_multi_mode) {
            return equal_range_multi_impl(key);
        } else {
            auto lb = lower_bound(key);
            auto key_matches = [&]() {
                if (lb == end()) return false;
                if constexpr (is_set_mode) {
                    return !_comp(key, *lb) && !_comp(*lb, key);
                } else {
                    return !_comp(key, lb->first) && !_comp(lb->first, key);
                }
            };
            if (!key_matches()) {
                return {lb, lb};
            }
            auto ub = lb;
            ++ub;
            return {lb, ub};
        }
    }

    template <typename K>
        requires is_transparent_comparator_v<Compare>
    [[nodiscard]] auto equal_range(const K& key) const -> std::pair<const_iterator, const_iterator> {
        if constexpr (is_multi_mode) {
            return equal_range_multi_impl(key);
        } else {
            auto lb = lower_bound(key);
            auto key_matches = [&]() {
                if (lb == end()) return false;
                if constexpr (is_set_mode) {
                    return !_comp(key, *lb) && !_comp(*lb, key);
                } else {
                    return !_comp(key, lb->first) && !_comp(lb->first, key);
                }
            };
            if (!key_matches()) {
                return {lb, lb};
            }
            auto ub = lb;
            ++ub;
            return {lb, ub};
        }
    }

    // swap
    void swap(btree_map& other) noexcept {
        std::swap(_root, other._root);
        std::swap(_size, other._size);
        std::swap(_comp, other._comp);
        // Swap allocators if propagate_on_container_swap is true
        if constexpr (std::allocator_traits<Allocator>::propagate_on_container_swap::value) {
            std::swap(_leaf_alloc, other._leaf_alloc);
            std::swap(_internal_alloc, other._internal_alloc);
        }
    }

    // max_size - theoretical maximum
    [[nodiscard]] static constexpr auto max_size() noexcept -> size_type {
        return std::numeric_limits<size_type>::max() / sizeof(storage_type);
    }

    // key_comp - returns the key comparison function
    [[nodiscard]] auto key_comp() const -> key_compare { return _comp; }

    // value_comp - returns the value comparison function
    [[nodiscard]] auto value_comp() const -> value_compare { return value_compare(_comp); }

    // get_allocator - returns the allocator (C++11)
    [[nodiscard]] auto get_allocator() const noexcept -> allocator_type {
        return allocator_type(_leaf_alloc);
    }

    // merge - merge elements from another btree_map (C++17)
    template <typename C2, typename A2, std::size_t N2>
    void merge(btree_map<Key, Value, C2, A2, N2>& source) {
        for (auto it = source.begin(); it != source.end();) {
            auto [insert_it, inserted] = insert(it->first, it->second);
            if (inserted) {
                // Use erase return value - getting next before erase is unsafe
                // because erase can rebalance and free nodes
                it = source.erase(it);
            } else {
                ++it;
            }
        }
    }

    template <typename C2, typename A2, std::size_t N2>
    void merge(btree_map<Key, Value, C2, A2, N2>&& source) {
        merge(source);
    }

    // insert with iterator range
    template <typename InputIt>
        requires requires(InputIt it) {
            { *it } -> std::convertible_to<value_type>;
            ++it;
        }
    void insert(InputIt first, InputIt last) {
        for (; first != last; ++first) {
            insert(*first);
        }
    }

    // insert with initializer_list
    void insert(std::initializer_list<value_type> ilist) {
        for (const auto& elem : ilist) {
            insert(elem);
        }
    }

    // insert_range - insert elements from a range (C++23)
    template <typename Range>
    void insert_range(Range&& range) {
        for (auto&& elem : range) {
            insert(std::forward<decltype(elem)>(elem));
        }
    }

    // insert_sorted - optimized insert for pre-sorted input (ascending order)
    // PRECONDITION: Input range must be sorted in ascending order according to Compare
    // PRECONDITION: All keys in input must be greater than existing keys in the map
    // This avoids the comparison check and tree traversal for each insert
    template <typename InputIt>
        requires requires(InputIt it) {
            { *it } -> std::convertible_to<value_type>;
            ++it;
        }
    void insert_sorted(InputIt first, InputIt last) {
        if (first == last) return;

        // Handle empty tree
        if (_root == nullptr) [[unlikely]] {
            _root = create_leaf();
            _rightmost_leaf = static_cast<leaf_node*>(_root);
        }

        // Use the cached rightmost leaf
        leaf_node* right_leaf = _rightmost_leaf;

        while (first != last) {
            // Fill current leaf as much as possible
            size_type available = kLeafSlots - right_leaf->count;

            while (available > 0 && first != last) {
                const auto& [key, value] = *first;
                right_leaf->slots[right_leaf->count] = storage_type(key, value);
                ++right_leaf->count;
                ++_size;
                --available;
                ++first;
            }

            // If we've exhausted input, we're done
            if (first == last) break;

            // Leaf is full, need to split - use optimized append split
            right_leaf = split_rightmost_leaf_for_append(right_leaf);
            _rightmost_leaf = right_leaf;
        }
    }

   private:
    // Optimized split for appending to rightmost leaf
    // Returns the new rightmost leaf (which is the new right sibling)
    // Optimization: Don't move elements - just take last element as median
    // and start fresh in right. Left stays nearly full, no memcpy needed.
    leaf_node* split_rightmost_leaf_for_append(leaf_node* leaf) {
        // Create new empty right sibling
        auto* new_right = create_leaf();
        new_right->count = 0;

        // Take the last element of left as median (goes to parent only)
        --leaf->count;
        Key median_key = std::move(leaf->slots[leaf->count].first);
        Value median_value = std::move(leaf->slots[leaf->count].second);

        // Left keeps [0, count-1], right starts empty
        // This is much faster than moving half the elements

        // Now propagate split up the tree
        propagate_split_to_parent(leaf, new_right, std::move(median_key), std::move(median_value));

        return new_right;
    }

    // Propagate a leaf split up to parent nodes
    void propagate_split_to_parent(node_base* left_child, node_base* right_child, Key median_key, Value median_value) {
        if (left_child->parent == nullptr) {
            // Splitting the root - create new root
            auto* new_root = create_internal();
            new_root->slots[0] = storage_type(std::move(median_key), std::move(median_value));
            new_root->children[0] = left_child;
            new_root->children[1] = right_child;
            new_root->count = 1;

            left_child->parent = new_root;
            left_child->position = 0;
            right_child->parent = new_root;
            right_child->position = 1;

            _root = new_root;
            return;
        }

        auto* parent = static_cast<internal_node*>(left_child->parent);
        size_type insert_pos = left_child->position;

        if (parent->count < kInternalSlots) {
            // Parent has room - insert directly
            // Shift elements right
            for (size_type i = parent->count; i > insert_pos; --i) {
                parent->slots[i] = std::move(parent->slots[i - 1]);
                parent->children[i + 1] = parent->children[i];
                if (parent->children[i + 1]) {
                    parent->children[i + 1]->position = static_cast<uint16_t>(i + 1);
                }
            }
            parent->slots[insert_pos] = storage_type(std::move(median_key), std::move(median_value));
            parent->children[insert_pos + 1] = right_child;
            right_child->parent = parent;
            right_child->position = static_cast<uint16_t>(insert_pos + 1);
            ++parent->count;
        } else {
            // Parent is full - need to split parent too
            split_internal_for_append(parent, insert_pos, right_child, std::move(median_key), std::move(median_value));
        }
    }

    // Split internal node when inserting at the rightmost position
    void split_internal_for_append(internal_node* internal, size_type insert_pos,
                                   node_base* new_child, Key key, Value value) {
        auto* new_right = create_internal();
        size_type mid = kInternalSlots / 2;

        Key parent_median_key;
        Value parent_median_value;

        if (insert_pos >= mid) {
            // New element goes to right half or becomes median
            parent_median_key = std::move(internal->slots[mid].first);
            parent_median_value = std::move(internal->slots[mid].second);

            // Move elements after mid to new_right
            size_type right_idx = 0;
            for (size_type i = mid + 1; i < internal->count; ++i) {
                if (i == insert_pos) {
                    new_right->slots[right_idx] = storage_type(std::move(key), std::move(value));
                    new_right->children[right_idx + 1] = new_child;
                    new_child->parent = new_right;
                    new_child->position = static_cast<uint16_t>(right_idx + 1);
                    ++right_idx;
                }
                new_right->slots[right_idx] = std::move(internal->slots[i]);
                ++right_idx;
            }
            // Move children
            for (size_type i = mid + 1; i <= internal->count; ++i) {
                size_type dest = i - mid - 1;
                if (i > insert_pos) dest++;
                new_right->children[dest] = internal->children[i];
                if (new_right->children[dest]) {
                    new_right->children[dest]->parent = new_right;
                    new_right->children[dest]->position = static_cast<uint16_t>(dest);
                }
            }

            // Handle insertion at the very end
            if (insert_pos >= internal->count) {
                new_right->slots[right_idx] = storage_type(std::move(key), std::move(value));
                new_right->children[right_idx + 1] = new_child;
                new_child->parent = new_right;
                new_child->position = static_cast<uint16_t>(right_idx + 1);
                ++right_idx;
            }

            new_right->count = static_cast<uint16_t>(right_idx);
            internal->count = static_cast<uint16_t>(mid);
        } else {
            // New element goes to left half
            parent_median_key = std::move(internal->slots[mid - 1].first);
            parent_median_value = std::move(internal->slots[mid - 1].second);

            // Move elements from mid to new_right
            for (size_type i = mid; i < internal->count; ++i) {
                new_right->slots[i - mid] = std::move(internal->slots[i]);
            }
            for (size_type i = mid; i <= internal->count; ++i) {
                new_right->children[i - mid] = internal->children[i];
                if (new_right->children[i - mid]) {
                    new_right->children[i - mid]->parent = new_right;
                    new_right->children[i - mid]->position = static_cast<uint16_t>(i - mid);
                }
            }
            new_right->count = static_cast<uint16_t>(internal->count - mid);
            internal->count = static_cast<uint16_t>(mid - 1);

            // Insert into left half
            for (size_type i = internal->count; i > insert_pos; --i) {
                internal->slots[i] = std::move(internal->slots[i - 1]);
                internal->children[i + 1] = internal->children[i];
                if (internal->children[i + 1]) {
                    internal->children[i + 1]->position = static_cast<uint16_t>(i + 1);
                }
            }
            internal->slots[insert_pos] = storage_type(std::move(key), std::move(value));
            internal->children[insert_pos + 1] = new_child;
            new_child->parent = internal;
            new_child->position = static_cast<uint16_t>(insert_pos + 1);
            ++internal->count;
        }

        // Propagate split up
        propagate_split_to_parent(internal, new_right, std::move(parent_median_key), std::move(parent_median_value));
    }

   public:

    // Comparison operators
    friend auto operator==(const btree_map& lhs, const btree_map& rhs) -> bool {
        if (lhs.size() != rhs.size()) return false;
        auto it1 = lhs.begin();
        auto it2 = rhs.begin();
        while (it1 != lhs.end()) {
            if constexpr (is_set_mode) {
                if (*it1 != *it2) {
                    return false;
                }
            } else {
                if (it1->first != it2->first || it1->second != it2->second) {
                    return false;
                }
            }
            ++it1;
            ++it2;
        }
        return true;
    }

    friend auto operator!=(const btree_map& lhs, const btree_map& rhs) -> bool { return !(lhs == rhs); }

    // Lexicographical comparison operators (C++20)
    friend auto operator<(const btree_map& lhs, const btree_map& rhs) -> bool {
        if constexpr (is_set_mode) {
            return std::lexicographical_compare(lhs.begin(), lhs.end(), rhs.begin(), rhs.end());
        } else {
            return std::lexicographical_compare(lhs.begin(), lhs.end(), rhs.begin(), rhs.end(),
                                                [](const value_type& a, const value_type& b) {
                                                    if (a.first < b.first) return true;
                                                    if (b.first < a.first) return false;
                                                    return a.second < b.second;
                                                });
        }
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
            if constexpr (is_set_mode) {
                if (auto cmp = *it1 <=> *it2; cmp != 0) return cmp;
            } else {
                if (auto cmp = it1->first <=> it2->first; cmp != 0) return cmp;
                if (auto cmp = it1->second <=> it2->second; cmp != 0) return cmp;
            }
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
template <typename Key, typename Value, typename Compare = std::less<Key>,
          typename Allocator = std::allocator<std::pair<const Key, Value>>>
using btree_map_compact = btree_map<Key, Value, Compare, Allocator, 256>;

// Non-member swap (C++17)
template <typename Key, typename Value, typename Compare, typename Allocator, std::size_t N, typename DuplicatePolicy>
void swap(btree_map<Key, Value, Compare, Allocator, N, DuplicatePolicy>& lhs,
          btree_map<Key, Value, Compare, Allocator, N, DuplicatePolicy>& rhs) noexcept {
    lhs.swap(rhs);
}

// erase_if - erase all elements satisfying predicate (C++20)
template <typename Key, typename Value, typename Compare, typename Allocator, std::size_t N, typename DuplicatePolicy, typename Pred>
typename btree_map<Key, Value, Compare, Allocator, N, DuplicatePolicy>::size_type
erase_if(btree_map<Key, Value, Compare, Allocator, N, DuplicatePolicy>& c, Pred pred) {
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

// =============================================================================
// btree_set - B-tree based ordered set (implemented as btree_map with empty value)
// =============================================================================

// btree_set is a template alias for btree_map in "set mode"
// When Value is btree_set_empty_value:
//   - value_type is Key (not pair<const Key, Value>)
//   - Iterators dereference to const Key&
//   - insert() takes just a key, not a key-value pair
//   - operator[] and at() are disabled
//   - Memory is optimized (empty value uses [[no_unique_address]])
template <typename Key, typename Compare = std::less<Key>,
          typename Allocator = std::allocator<std::pair<const Key, btree_set_empty_value>>,
          std::size_t TargetNodeSize = optimal_node_size<Key, btree_set_empty_value>()>
using btree_set = btree_map<Key, btree_set_empty_value, Compare, Allocator, TargetNodeSize>;

// btree_set_auto is kept for backward compatibility
template <typename Key, typename Compare = std::less<Key>>
using btree_set_auto = btree_set<Key, Compare>;

// Explicit 256-byte node size for minimal memory footprint
template <typename Key, typename Compare = std::less<Key>,
          typename Allocator = std::allocator<std::pair<const Key, btree_set_empty_value>>>
using btree_set_compact = btree_set<Key, Compare, Allocator, 256>;

// =============================================================================
// btree_multimap - B-tree based ordered multimap (allows duplicate keys)
// =============================================================================

template <typename Key, typename Value, typename Compare = std::less<Key>,
          typename Allocator = std::allocator<std::pair<const Key, Value>>,
          std::size_t TargetNodeSize = optimal_node_size<Key, Value>()>
using btree_multimap = btree_map<Key, Value, Compare, Allocator, TargetNodeSize, btree_multi_policy>;

// btree_multimap_compact with 256-byte node size
template <typename Key, typename Value, typename Compare = std::less<Key>,
          typename Allocator = std::allocator<std::pair<const Key, Value>>>
using btree_multimap_compact = btree_multimap<Key, Value, Compare, Allocator, 256>;

// =============================================================================
// btree_multiset - B-tree based ordered multiset (allows duplicate keys)
// =============================================================================

template <typename Key, typename Compare = std::less<Key>,
          typename Allocator = std::allocator<std::pair<const Key, btree_set_empty_value>>,
          std::size_t TargetNodeSize = optimal_node_size<Key, btree_set_empty_value>()>
using btree_multiset = btree_map<Key, btree_set_empty_value, Compare, Allocator, TargetNodeSize, btree_multi_policy>;

// btree_multiset_compact with 256-byte node size
template <typename Key, typename Compare = std::less<Key>,
          typename Allocator = std::allocator<std::pair<const Key, btree_set_empty_value>>>
using btree_multiset_compact = btree_multiset<Key, Compare, Allocator, 256>;

}  // namespace stdb::container

// =============================================================================
// PMR (Polymorphic Memory Resource) support
// =============================================================================
#ifdef BTREE_HAS_PMR

namespace stdb::pmr {

// btree_map with polymorphic allocator
template <typename Key, typename Value, typename Compare = std::less<Key>,
          std::size_t TargetNodeSize = container::optimal_node_size<Key, Value>()>
using btree_map = container::btree_map<Key, Value, Compare,
                                       std::pmr::polymorphic_allocator<std::pair<const Key, Value>>,
                                       TargetNodeSize>;

// btree_map_compact with polymorphic allocator (256-byte node size)
template <typename Key, typename Value, typename Compare = std::less<Key>>
using btree_map_compact = container::btree_map<Key, Value, Compare,
                                               std::pmr::polymorphic_allocator<std::pair<const Key, Value>>,
                                               256>;

// btree_set with polymorphic allocator
template <typename Key, typename Compare = std::less<Key>,
          std::size_t TargetNodeSize = container::optimal_node_size<Key, container::btree_set_empty_value>()>
using btree_set = container::btree_set<Key, Compare,
                                       std::pmr::polymorphic_allocator<std::pair<const Key, container::btree_set_empty_value>>,
                                       TargetNodeSize>;

// btree_set_compact with polymorphic allocator (256-byte node size)
template <typename Key, typename Compare = std::less<Key>>
using btree_set_compact = container::btree_set<Key, Compare,
                                               std::pmr::polymorphic_allocator<std::pair<const Key, container::btree_set_empty_value>>,
                                               256>;

// btree_multimap with polymorphic allocator
template <typename Key, typename Value, typename Compare = std::less<Key>,
          std::size_t TargetNodeSize = container::optimal_node_size<Key, Value>()>
using btree_multimap = container::btree_multimap<Key, Value, Compare,
                                                 std::pmr::polymorphic_allocator<std::pair<const Key, Value>>,
                                                 TargetNodeSize>;

// btree_multimap_compact with polymorphic allocator (256-byte node size)
template <typename Key, typename Value, typename Compare = std::less<Key>>
using btree_multimap_compact = container::btree_multimap<Key, Value, Compare,
                                                         std::pmr::polymorphic_allocator<std::pair<const Key, Value>>,
                                                         256>;

// btree_multiset with polymorphic allocator
template <typename Key, typename Compare = std::less<Key>,
          std::size_t TargetNodeSize = container::optimal_node_size<Key, container::btree_set_empty_value>()>
using btree_multiset = container::btree_multiset<Key, Compare,
                                                 std::pmr::polymorphic_allocator<std::pair<const Key, container::btree_set_empty_value>>,
                                                 TargetNodeSize>;

// btree_multiset_compact with polymorphic allocator (256-byte node size)
template <typename Key, typename Compare = std::less<Key>>
using btree_multiset_compact = container::btree_multiset<Key, Compare,
                                                         std::pmr::polymorphic_allocator<std::pair<const Key, container::btree_set_empty_value>>,
                                                         256>;

}  // namespace stdb::pmr

#endif  // BTREE_HAS_PMR

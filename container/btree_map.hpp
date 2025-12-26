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
#include <memory>
#include <utility>

#include "vectra.hpp"

#define BTREE_DEBUG 0

// SIMD support detection
#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#define BTREE_HAS_SSE2 1
#if defined(__AVX2__)
#define BTREE_HAS_AVX2 1
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

template <typename Key, typename Value, typename Compare = std::less<Key>, std::size_t TargetNodeSize = 256>
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
    // SSE2 lower_bound for int32_t keys - returns first index where keys[i] >= target
    template <typename T>
    static auto simd_lower_bound_int32(const T* slots, size_type count, int32_t target) noexcept -> size_type
    requires(sizeof(T) >= sizeof(int32_t))
    {
        // Calculate stride between keys (for pair<Key,Value> layout)
        constexpr size_type stride = sizeof(T) / sizeof(int32_t);
        const auto* keys = reinterpret_cast<const int32_t*>(slots);

        __m128i target_vec = _mm_set1_epi32(target);
        size_type i = 0;

        // Process 4 elements at a time
        while (i + 4 <= count) {
            // Gather 4 keys (accounting for stride)
            __m128i key_vec =
              _mm_set_epi32(keys[(i + 3) * stride], keys[(i + 2) * stride], keys[(i + 1) * stride], keys[i * stride]);

            // Compare: mask of keys >= target (i.e., NOT keys < target)
            __m128i lt = _mm_cmplt_epi32(key_vec, target_vec);
            int mask = _mm_movemask_ps(_mm_castsi128_ps(lt));

            // If any key is NOT less than target (i.e., >= target), find first
            if (mask != 0xF) {  // Not all are less than target
                // Find first bit that is 0 (first key >= target)
                int first_ge = __builtin_ctz(~mask & 0xF);
                return i + first_ge;
            }
            i += 4;
        }

        // Handle remaining elements with scalar code
        for (; i < count; ++i) {
            if (keys[i * stride] >= target) {
                return i;
            }
        }
        return count;
    }

#endif

#ifdef BTREE_HAS_AVX2
    // AVX2 lower_bound for int64_t keys - returns first index where keys[i] >= target
    template <typename T>
    static auto simd_lower_bound_int64(const T* slots, size_type count, int64_t target) noexcept -> size_type
    requires(sizeof(T) >= sizeof(int64_t))
    {
        // Calculate stride between keys (for pair<Key,Value> layout)
        constexpr size_type stride = sizeof(T) / sizeof(int64_t);
        const auto* keys = reinterpret_cast<const int64_t*>(slots);

        __m256i target_vec = _mm256_set1_epi64x(target);
        size_type i = 0;

        // Process 4 elements at a time with AVX2
        while (i + 4 <= count) {
            // Gather 4 keys (accounting for stride)
            __m256i key_vec = _mm256_set_epi64x(
                keys[(i + 3) * stride], keys[(i + 2) * stride],
                keys[(i + 1) * stride], keys[i * stride]);

            // Compare: mask of keys < target
            __m256i lt = _mm256_cmpgt_epi64(target_vec, key_vec);
            int mask = _mm256_movemask_pd(_mm256_castsi256_pd(lt));

            // If any key is NOT less than target (i.e., >= target), find first
            if (mask != 0xF) {
                int first_ge = __builtin_ctz(~mask & 0xF);
                return i + first_ge;
            }
            i += 4;
        }

        // Handle remaining elements with scalar code
        for (; i < count; ++i) {
            if (keys[i * stride] >= target) {
                return i;
            }
        }
        return count;
    }
#endif

    // Type traits for SIMD eligibility
    template <typename T>
    static constexpr bool is_simd32_eligible_v = std::is_same_v<T, int32_t> || std::is_same_v<T, uint32_t>;

    template <typename T>
    static constexpr bool is_simd64_eligible_v = std::is_same_v<T, int64_t> || std::is_same_v<T, uint64_t>;

    // Binary search within a node using only keys for comparison
    // Returns first position where key >= slot
    template <typename Slots>
    [[nodiscard]] __attribute__((always_inline, flatten)) auto binary_search_in_slots(
        const Slots* __restrict__ slots, size_type count, const Key& __restrict__ key) const noexcept -> size_type {
        // Hint to compiler about expected count range (typical btree has 15 slots)
        __builtin_assume(count <= 32);

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
#ifdef BTREE_HAS_SSE2
        if constexpr (is_simd32_eligible_v<Key> && std::is_same_v<Compare, std::less<Key>>) {
            return simd_lower_bound_int32(leaf->slots, leaf->count, static_cast<int32_t>(key));
        }
#endif
#ifdef BTREE_HAS_AVX2
        if constexpr (is_simd64_eligible_v<Key> && std::is_same_v<Compare, std::less<Key>>) {
            return simd_lower_bound_int64(leaf->slots, leaf->count, static_cast<int64_t>(key));
        }
#endif
        return binary_search_in_slots(leaf->slots, leaf->count, key);
    }

    // Specialized search for internal node
    [[nodiscard]] __attribute__((always_inline, flatten)) auto lower_bound_in_internal(
        const internal_node* __restrict__ internal, const Key& __restrict__ key) const noexcept -> size_type {
#ifdef BTREE_HAS_SSE2
        if constexpr (is_simd32_eligible_v<Key> && std::is_same_v<Compare, std::less<Key>>) {
            return simd_lower_bound_int32(internal->slots, internal->count, static_cast<int32_t>(key));
        }
#endif
#ifdef BTREE_HAS_AVX2
        if constexpr (is_simd64_eligible_v<Key> && std::is_same_v<Compare, std::less<Key>>) {
            return simd_lower_bound_int64(internal->slots, internal->count, static_cast<int64_t>(key));
        }
#endif
        return binary_search_in_slots(internal->slots, internal->count, key);
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
                for (size_type i = count; i > pos; --i) {
                    leaf->slots[i] = std::move(leaf->slots[i - 1]);
                }
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
                for (size_type i = count; i > pos; --i) {
                    node->slots[i] = std::move(node->slots[i - 1]);
                }
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
    auto insert_and_split_impl(node_base* node, size_type pos, K&& key, V&& value,
                               node_base* right_child = nullptr) -> std::pair<node_base*, size_type> {
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
                std::cerr << "[DEBUG] pos==mid case: pos=" << pos << " mid=" << mid << " key type=" << typeid(Key).name() << std::endl;
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
                    std::memcpy(new_right->slots, &leaf->slots[mid + 1], (leaf->count - mid - 1) * sizeof(storage_type));
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
                    std::cerr << "[DEBUG] Returning new_root, pos=0, slot[0].second='" << new_root->slots[0].second << "'" << std::endl;
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
                auto [pnode, ppos] = insert_and_split_impl(parent, parent_pos, std::move(median_key), std::move(median_value), new_right);
                // If element was the median, return the parent position
                if (inserted_node == nullptr) {
#if BTREE_DEBUG
                    std::cerr << "[DEBUG] Returning parent result: ppos=" << ppos << std::endl;
#endif
                    return {pnode, ppos};
                }
            }
#if BTREE_DEBUG
            std::cerr << "[DEBUG] Returning leaf result: node=" << (void*)inserted_node << " pos=" << inserted_pos << std::endl;
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
                auto [pnode, ppos] = insert_and_split_impl(parent, parent_pos, std::move(median_key), std::move(median_value), new_right);
                if (inserted_node == nullptr) {
                    return {pnode, ppos};
                }
            }
            return {inserted_node, inserted_pos};
        }
    }

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

        friend auto operator==(const const_iterator& a, const const_iterator& b) -> bool {
            return a._node == b._node && a._pos == b._pos;
        }

        friend auto operator!=(const const_iterator& a, const const_iterator& b) -> bool { return !(a == b); }
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

    // Copy constructor
    btree_map(const btree_map& other) : _comp(other._comp) {
        for (const auto& [k, v] : other) {
            insert(k, v);
        }
    }

    // Move constructor
    btree_map(btree_map&& other) noexcept : _root(other._root), _size(other._size), _comp(std::move(other._comp)) {
        other._root = nullptr;
        other._size = 0;
    }

    // Copy assignment
    auto operator=(const btree_map& other) -> btree_map& {
        if (this != &other) {
            clear();
            _comp = other._comp;
            for (const auto& [k, v] : other) {
                insert(k, v);
            }
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
    auto insert(const Key& key, const Value& value) -> std::pair<iterator, bool> {
        return insert_impl(key, value);
    }

    // Insert a key-value pair (rvalue version for value - avoids copy)
    auto insert(const Key& key, Value&& value) -> std::pair<iterator, bool> {
        return insert_impl(key, std::move(value));
    }

    // Insert with value_type
    auto insert(const value_type& kv) -> std::pair<iterator, bool> { return insert(kv.first, kv.second); }

    auto insert(value_type&& kv) -> std::pair<iterator, bool> {
        return insert_impl(std::move(kv.first), std::move(kv.second));
    }

    // Emplace - construct in place
    template <typename... Args>
    auto emplace(Args&&... args) -> std::pair<iterator, bool> {
        value_type val(std::forward<Args>(args)...);
        return insert_impl(std::move(val.first), std::move(val.second));
    }

  private:
    // Internal insert implementation with perfect forwarding
    template <typename K, typename V>
    __attribute__((hot)) auto insert_impl(K&& key, V&& value) -> std::pair<iterator, bool> {
        if (_root == nullptr) [[unlikely]] {
            _root = create_leaf();
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
        auto [inserted_node, inserted_pos] = insert_and_split_impl(leaf, pos, std::forward<K>(key), std::forward<V>(value));
        ++_size;

        return {iterator(inserted_node, inserted_pos), true};
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

    // operator[]
    auto operator[](const Key& key) -> Value& {
        auto [it, inserted] = insert(key, Value{});
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

    // Erase (basic implementation - can be optimized)
    auto erase(const Key& key) -> size_type {
        auto it = find(key);
        if (it == end()) {
            return 0;
        }

        // For now, rebuild tree without this element (simple but not optimal)
        // TODO: Implement proper B-tree deletion with rebalancing
        std::vector<std::pair<Key, Value>> elements;
        elements.reserve(_size - 1);
        for (const auto& [k, v] : *this) {
            if (_comp(k, key) || _comp(key, k)) {
                elements.emplace_back(k, v);
            }
        }

        clear();
        for (const auto& [k, v] : elements) {
            insert(k, v);
        }

        return 1;
    }

    // Comparison
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

    // Debug info
    [[nodiscard]] static constexpr auto leaf_slots() noexcept -> size_type { return kLeafSlots; }
    [[nodiscard]] static constexpr auto internal_slots() noexcept -> size_type { return kInternalSlots; }
};

// Convenience alias that automatically selects optimal node size
// Ensures at least 15 slots per node regardless of key/value size
template <typename Key, typename Value, typename Compare = std::less<Key>>
using btree_map_auto = btree_map<Key, Value, Compare, optimal_node_size<Key, Value>()>;

}  // namespace stdb::container

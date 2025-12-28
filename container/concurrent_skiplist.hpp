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

#include <atomic>
#include <cstdint>
#include <functional>
#include <optional>
#include <thread>

namespace stdb::container {

/*
 * High-performance lock-free concurrent skiplist.
 *
 * Optimizations:
 *   1. Fast xorshift64 RNG instead of mt19937
 *   2. Minimal atomic operations in hot path
 *   3. Relaxed memory ordering where safe
 *   4. No epoch tracking for read-only operations
 *   5. Simplified node structure
 */

template <typename Key, typename Value, typename Compare = std::less<Key>, uint8_t MaxLevel = 12>
class concurrent_skiplist {
   public:
    using key_type = Key;
    using mapped_type = Value;
    using value_type = std::pair<const Key, Value>;
    using size_type = std::size_t;

   private:
    // Marked pointer: use low bit to indicate logical deletion
    template <typename T>
    struct MarkedPtr {
        uintptr_t _bits;

        MarkedPtr() : _bits(0) {}
        MarkedPtr(T* p, bool mark = false) : _bits(reinterpret_cast<uintptr_t>(p) | (mark ? 1 : 0)) {}

        T* ptr() const { return reinterpret_cast<T*>(_bits & ~uintptr_t(1)); }
        bool marked() const { return _bits & 1; }
        MarkedPtr with_mark() const { return MarkedPtr(ptr(), true); }

        bool operator==(MarkedPtr o) const { return _bits == o._bits; }
        bool operator!=(MarkedPtr o) const { return _bits != o._bits; }
    };

    struct Node {
        Key key;
        Value value;
        uint8_t height;
        std::atomic<MarkedPtr<Node>> next[1];  // flexible array

        static Node* create(const Key& k, const Value& v, uint8_t h) {
            void* mem = ::operator new(sizeof(Node) + h * sizeof(std::atomic<MarkedPtr<Node>>));
            Node* node = static_cast<Node*>(mem);
            new (&node->key) Key(k);
            new (&node->value) Value(v);
            node->height = h;
            for (uint8_t i = 0; i <= h; ++i) {
                new (&node->next[i]) std::atomic<MarkedPtr<Node>>(MarkedPtr<Node>());
            }
            return node;
        }

        static Node* create(Key&& k, Value&& v, uint8_t h) {
            void* mem = ::operator new(sizeof(Node) + h * sizeof(std::atomic<MarkedPtr<Node>>));
            Node* node = static_cast<Node*>(mem);
            new (&node->key) Key(std::move(k));
            new (&node->value) Value(std::move(v));
            node->height = h;
            for (uint8_t i = 0; i <= h; ++i) {
                new (&node->next[i]) std::atomic<MarkedPtr<Node>>(MarkedPtr<Node>());
            }
            return node;
        }

        void destroy() {
            key.~Key();
            value.~Value();
            ::operator delete(this);
        }
    };

    // Ultra-fast xorshift64 RNG (thread-local state)
    static uint64_t xorshift64() {
        static thread_local uint64_t state = 0x853c49e6748fea9bULL ^
            (uint64_t)std::hash<std::thread::id>{}(std::this_thread::get_id());
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        return state;
    }

    uint8_t random_height() {
        // Geometric distribution with p=0.5 using bit counting
        uint64_t r = xorshift64();
        uint8_t h = __builtin_ctzll(r | (1ULL << MaxLevel));  // Ensure h <= MaxLevel
        return h;
    }

    // Head sentinel - aligned to avoid false sharing
    alignas(64) std::atomic<MarkedPtr<Node>> _head[MaxLevel + 1];

    // Separate cache line for frequently modified atomics
    alignas(64) std::atomic<size_type> _size{0};
    std::atomic<uint8_t> _height{0};

    // Comparator on its own to avoid interference
    alignas(64) Compare _cmp;

    // Simplified memory management:
    // - Don't free nodes during operation (leave in list, just marked)
    // - Only clean up in destructor
    // This is safe and simple, though it leaks memory during heavy delete workloads

   public:
    concurrent_skiplist() {
        for (uint8_t i = 0; i <= MaxLevel; ++i) {
            _head[i].store(MarkedPtr<Node>(), std::memory_order_relaxed);
        }
    }

    ~concurrent_skiplist() {
        // Clean up all nodes in the list (including marked/retired ones)
        Node* curr = _head[0].load(std::memory_order_relaxed).ptr();
        while (curr) {
            Node* next = curr->next[0].load(std::memory_order_relaxed).ptr();
            curr->destroy();
            curr = next;
        }
    }

    concurrent_skiplist(const concurrent_skiplist&) = delete;
    concurrent_skiplist& operator=(const concurrent_skiplist&) = delete;

    // =========================================================================
    // Find - ultra-optimized for speed
    // =========================================================================
    // =========================================================================
    // Memory Ordering Analysis:
    //
    // Insert publishes node with: CAS(..., release)
    // Reader must use: load(..., acquire) to see node's key/value
    //
    // Delete marks with: CAS(..., release)
    // Reader should use: load(..., acquire) to see mark promptly
    //
    // On x86: acquire ≈ relaxed (TSO provides acquire-release automatically)
    // On ARM: acquire adds barrier, necessary for correctness
    // =========================================================================

    std::optional<Value> find(const Key& key) const {
        Node* pred = nullptr;
        int h = _height.load(std::memory_order_relaxed);  // height is approximate, relaxed OK

        for (int i = h; i >= 0; --i) {
            // acquire: synchronize with insert's release to see node data
            Node* curr = (pred ? pred->next[i] : _head[i]).load(std::memory_order_acquire).ptr();

            while (__builtin_expect(curr != nullptr, 1)) {
                const Key& k = curr->key;  // safe: we used acquire above
                if (_cmp(k, key)) {
                    pred = curr;
                    // acquire: need to see next node's data
                    curr = curr->next[i].load(std::memory_order_acquire).ptr();
                } else if (!_cmp(key, k)) {
                    // acquire: synchronize with delete's release to see mark
                    if (__builtin_expect(!curr->next[0].load(std::memory_order_acquire).marked(), 1)) {
                        return curr->value;
                    }
                    return std::nullopt;  // Deleted
                } else {
                    break;
                }
            }
        }
        return std::nullopt;
    }

    // Optimized contains - avoids value copy
    bool contains(const Key& key) const {
        Node* pred = nullptr;
        int h = _height.load(std::memory_order_relaxed);

        for (int i = h; i >= 0; --i) {
            // acquire: synchronize with insert's release
            Node* curr = (pred ? pred->next[i] : _head[i]).load(std::memory_order_acquire).ptr();

            while (curr) {
                const Key& k = curr->key;
                if (_cmp(k, key)) {
                    pred = curr;
                    curr = curr->next[i].load(std::memory_order_acquire).ptr();
                } else if (!_cmp(key, k)) {
                    // acquire: synchronize with delete's release
                    return !curr->next[0].load(std::memory_order_acquire).marked();
                } else {
                    break;
                }
            }
        }
        return false;
    }

    // =========================================================================
    // Insert
    // =========================================================================
    bool insert(const Key& key, const Value& value) {
        uint8_t height = random_height();
        Node* new_node = Node::create(key, value, height);

        // Update max height
        uint8_t old_h = _height.load(std::memory_order_relaxed);
        while (height > old_h) {
            if (_height.compare_exchange_weak(old_h, height, std::memory_order_relaxed)) break;
        }

        Node* preds[MaxLevel + 1];
        Node* succs[MaxLevel + 1];

        while (true) {
            // Find insert position
            if (!find_insert_position(key, preds, succs)) {
                // Key exists
                new_node->destroy();
                return false;
            }

            // Set new node's forward pointers
            for (uint8_t i = 0; i <= height; ++i) {
                new_node->next[i].store(MarkedPtr<Node>(succs[i]), std::memory_order_relaxed);
            }

            // CAS at level 0
            MarkedPtr<Node> expected(succs[0]);
            MarkedPtr<Node> desired(new_node);

            std::atomic<MarkedPtr<Node>>* pred_next = preds[0] ? &preds[0]->next[0] : &_head[0];

            if (!pred_next->compare_exchange_strong(expected, desired,
                                                     std::memory_order_release,
                                                     std::memory_order_relaxed)) {
                continue;  // Retry
            }

            // Link higher levels
            for (uint8_t i = 1; i <= height; ++i) {
                while (true) {
                    MarkedPtr<Node> next = new_node->next[i].load(std::memory_order_relaxed);
                    if (next.marked()) {
                        // Node deleted while linking
                        goto done;
                    }

                    expected = MarkedPtr<Node>(succs[i]);
                    desired = MarkedPtr<Node>(new_node);
                    pred_next = preds[i] ? &preds[i]->next[i] : &_head[i];

                    if (pred_next->compare_exchange_strong(expected, desired,
                                                           std::memory_order_release,
                                                           std::memory_order_relaxed)) {
                        break;
                    }

                    // Re-find position for this level
                    find_insert_position(key, preds, succs);

                    // Update new_node's next if successor changed
                    if (succs[i] != new_node->next[i].load(std::memory_order_relaxed).ptr()) {
                        new_node->next[i].store(MarkedPtr<Node>(succs[i]), std::memory_order_relaxed);
                    }
                }
            }

        done:
            _size.fetch_add(1, std::memory_order_relaxed);
            return true;
        }
    }

    bool insert(Key&& key, Value&& value) {
        uint8_t height = random_height();
        Node* new_node = Node::create(std::move(key), std::move(value), height);

        uint8_t old_h = _height.load(std::memory_order_relaxed);
        while (height > old_h) {
            if (_height.compare_exchange_weak(old_h, height, std::memory_order_relaxed)) break;
        }

        Node* preds[MaxLevel + 1];
        Node* succs[MaxLevel + 1];

        while (true) {
            if (!find_insert_position(new_node->key, preds, succs)) {
                new_node->destroy();
                return false;
            }

            for (uint8_t i = 0; i <= height; ++i) {
                new_node->next[i].store(MarkedPtr<Node>(succs[i]), std::memory_order_relaxed);
            }

            MarkedPtr<Node> expected(succs[0]);
            MarkedPtr<Node> desired(new_node);
            std::atomic<MarkedPtr<Node>>* pred_next = preds[0] ? &preds[0]->next[0] : &_head[0];

            if (!pred_next->compare_exchange_strong(expected, desired,
                                                     std::memory_order_release,
                                                     std::memory_order_relaxed)) {
                continue;
            }

            for (uint8_t i = 1; i <= height; ++i) {
                while (true) {
                    MarkedPtr<Node> next = new_node->next[i].load(std::memory_order_relaxed);
                    if (next.marked()) goto done2;

                    expected = MarkedPtr<Node>(succs[i]);
                    desired = MarkedPtr<Node>(new_node);
                    pred_next = preds[i] ? &preds[i]->next[i] : &_head[i];

                    if (pred_next->compare_exchange_strong(expected, desired,
                                                           std::memory_order_release,
                                                           std::memory_order_relaxed)) {
                        break;
                    }
                    find_insert_position(new_node->key, preds, succs);
                    if (succs[i] != new_node->next[i].load(std::memory_order_relaxed).ptr()) {
                        new_node->next[i].store(MarkedPtr<Node>(succs[i]), std::memory_order_relaxed);
                    }
                }
            }
        done2:
            _size.fetch_add(1, std::memory_order_relaxed);
            return true;
        }
    }

    // =========================================================================
    // Erase
    // =========================================================================
    bool erase(const Key& key) {
        Node* preds[MaxLevel + 1];
        Node* succs[MaxLevel + 1];

        while (true) {
            if (!find_delete_position(key, preds, succs)) {
                return false;  // Not found
            }

            Node* victim = succs[0];
            if (!victim || _cmp(key, victim->key) || _cmp(victim->key, key)) {
                return false;  // Not found
            }

            // Mark all levels from top to bottom
            // Use release so readers with acquire can see the mark
            for (int i = victim->height; i >= 0; --i) {
                MarkedPtr<Node> next = victim->next[i].load(std::memory_order_relaxed);
                while (!next.marked()) {
                    if (victim->next[i].compare_exchange_weak(next, next.with_mark(),
                                                               std::memory_order_release,
                                                               std::memory_order_relaxed)) {
                        break;
                    }
                }
            }

            // Check if we successfully marked level 0
            MarkedPtr<Node> next0 = victim->next[0].load(std::memory_order_acquire);
            if (!next0.marked()) {
                continue;  // Someone else unmarked? Retry
            }

            // Physically unlink (best effort, will be done by future traversals too)
            for (int i = victim->height; i >= 0; --i) {
                std::atomic<MarkedPtr<Node>>* pred_next = preds[i] ? &preds[i]->next[i] : &_head[i];
                MarkedPtr<Node> expected(victim);
                MarkedPtr<Node> succ = victim->next[i].load(std::memory_order_relaxed);
                pred_next->compare_exchange_strong(expected, MarkedPtr<Node>(succ.ptr()),
                                                    std::memory_order_relaxed);
            }

            _size.fetch_sub(1, std::memory_order_relaxed);
            // Node stays in list (marked), will be cleaned up by destructor
            return true;
        }
    }

    // =========================================================================
    // Utilities
    // =========================================================================
    size_type size() const { return _size.load(std::memory_order_relaxed); }
    bool empty() const { return size() == 0; }

    template <typename Func>
    void for_each(Func&& fn) const {
        Node* curr = _head[0].load(std::memory_order_acquire).ptr();
        while (curr) {
            MarkedPtr<Node> next = curr->next[0].load(std::memory_order_acquire);
            if (!next.marked()) {
                fn(curr->key, curr->value);
            }
            curr = next.ptr();
        }
    }

   private:
    // Find position for insert, returns false if key exists
    bool find_insert_position(const Key& key, Node* preds[], Node* succs[]) {
    retry:
        int top = _height.load(std::memory_order_relaxed);
        Node* pred = nullptr;

        for (int i = top; i >= 0; --i) {
            std::atomic<MarkedPtr<Node>>* pred_next = pred ? &pred->next[i] : &_head[i];
            // acquire: need to see node data when we dereference curr.ptr()
            MarkedPtr<Node> curr = pred_next->load(std::memory_order_acquire);

            while (true) {
                if (!curr.ptr()) break;

                // acquire: need to see succ node's data and mark
                MarkedPtr<Node> succ = curr.ptr()->next[i].load(std::memory_order_acquire);

                // Help remove marked nodes
                if (succ.marked() || curr.marked()) {
                    MarkedPtr<Node> next_unmarked = succ;
                    while (next_unmarked.ptr() && next_unmarked.marked()) {
                        next_unmarked = next_unmarked.ptr()->next[i].load(std::memory_order_acquire);
                    }
                    if (!pred_next->compare_exchange_strong(curr, MarkedPtr<Node>(next_unmarked.ptr()),
                                                             std::memory_order_release,
                                                             std::memory_order_relaxed)) {
                        // CAS failed, restart from top
                        pred = nullptr;
                        goto retry;
                    }
                    curr = MarkedPtr<Node>(next_unmarked.ptr());
                    continue;
                }

                if (_cmp(curr.ptr()->key, key)) {
                    pred = curr.ptr();
                    pred_next = &pred->next[i];
                    curr = succ;
                } else {
                    if (!_cmp(key, curr.ptr()->key)) {
                        // Key exists
                        return false;
                    }
                    break;
                }
            }

            preds[i] = pred;
            succs[i] = curr.ptr();
        }

        // Fill remaining levels with pred=nullptr (head)
        for (int i = top + 1; i <= MaxLevel; ++i) {
            preds[i] = nullptr;
            succs[i] = nullptr;
        }

        return true;
    }

    // Find position for delete
    bool find_delete_position(const Key& key, Node* preds[], Node* succs[]) {
        int top = _height.load(std::memory_order_relaxed);
        Node* pred = nullptr;
        bool found = false;

        for (int i = top; i >= 0; --i) {
            std::atomic<MarkedPtr<Node>>* pred_next = pred ? &pred->next[i] : &_head[i];
            // acquire: need to see node data
            MarkedPtr<Node> curr = pred_next->load(std::memory_order_acquire);

            while (curr.ptr()) {
                Node* curr_node = curr.ptr();
                // acquire: need to see mark and successor's data
                MarkedPtr<Node> succ = curr_node->next[i].load(std::memory_order_acquire);

                // Skip marked nodes
                if (succ.marked()) {
                    curr = succ;
                    continue;
                }

                if (_cmp(curr_node->key, key)) {
                    pred = curr_node;
                    pred_next = &pred->next[i];
                    curr = succ;
                } else {
                    if (!_cmp(key, curr_node->key)) {
                        found = true;
                    }
                    break;
                }
            }

            preds[i] = pred;
            succs[i] = curr.ptr();
        }

        return found;
    }
};

template <typename Key, typename Value, typename Compare = std::less<Key>>
using concurrent_skiplist_map = concurrent_skiplist<Key, Value, Compare>;

}  // namespace stdb::container

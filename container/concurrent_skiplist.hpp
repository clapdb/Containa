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
#include <limits>
#include <memory>
#include <optional>
#include <random>
#include <thread>
#include <vector>

namespace stdb::container {

/*
 * Lock-free concurrent skiplist implementation.
 *
 * Based on the algorithm from:
 *   "A Pragmatic Implementation of Non-Blocking Linked-Lists"
 *   by Timothy L. Harris (2001)
 * and
 *   "A Simple Optimistic Skiplist Algorithm"
 *   by Maurice Herlihy et al.
 *
 * Key techniques:
 *   1. Marked pointers: Use low bit to mark nodes for logical deletion
 *   2. CAS operations: Atomic compare-and-swap for pointer updates
 *   3. Helping: Threads help physically remove marked nodes
 *   4. Epoch-based reclamation: Safe memory management
 *
 * Thread safety:
 *   - All operations (find, insert, erase) are lock-free
 *   - Multiple readers and writers can operate concurrently
 *   - No starvation: every operation completes in finite steps
 */

// Forward declaration
template <typename Key, typename Value, typename Compare, uint8_t MaxLevel>
class concurrent_skiplist;

// =============================================================================
// Epoch-based memory reclamation
// =============================================================================

class EpochManager {
   public:
    static constexpr size_t kNumEpochs = 3;
    static constexpr size_t kMaxThreads = 128;

    EpochManager() : _global_epoch(0) {
        for (auto& e : _thread_epochs) {
            e.store(kInactiveEpoch, std::memory_order_relaxed);
        }
    }

    ~EpochManager() {
        // Clean up all retired nodes
        for (auto& epoch_list : _retired) {
            for (auto* node : epoch_list) {
                ::operator delete(node);
            }
            epoch_list.clear();
        }
    }

    // Called when a thread starts an operation
    void enter() {
        size_t tid = thread_id();
        _thread_epochs[tid].store(_global_epoch.load(std::memory_order_acquire),
                                   std::memory_order_release);
    }

    // Called when a thread finishes an operation
    void exit() {
        size_t tid = thread_id();
        _thread_epochs[tid].store(kInactiveEpoch, std::memory_order_release);
    }

    // Retire a node for later deletion
    void retire(void* node) {
        size_t epoch = _global_epoch.load(std::memory_order_relaxed);
        std::lock_guard<std::mutex> lock(_retire_mutex);
        _retired[epoch % kNumEpochs].push_back(node);

        // Try to advance epoch
        try_advance();
    }

   private:
    static constexpr size_t kInactiveEpoch = std::numeric_limits<size_t>::max();

    void try_advance() {
        size_t current = _global_epoch.load(std::memory_order_relaxed);
        size_t oldest = current;

        // Find the oldest active epoch
        for (size_t i = 0; i < kMaxThreads; ++i) {
            size_t e = _thread_epochs[i].load(std::memory_order_acquire);
            if (e != kInactiveEpoch && e < oldest) {
                oldest = e;
            }
        }

        // If we can advance by at least 2, we can reclaim the oldest epoch
        if (current >= oldest + 2) {
            size_t reclaim_epoch = (current + 1) % kNumEpochs;
            for (auto* node : _retired[reclaim_epoch]) {
                ::operator delete(node);
            }
            _retired[reclaim_epoch].clear();
        }

        // Advance global epoch
        _global_epoch.fetch_add(1, std::memory_order_release);
    }

    size_t thread_id() {
        thread_local size_t tid = _next_tid.fetch_add(1, std::memory_order_relaxed);
        return tid % kMaxThreads;
    }

    std::atomic<size_t> _global_epoch;
    std::array<std::atomic<size_t>, kMaxThreads> _thread_epochs;
    std::array<std::vector<void*>, kNumEpochs> _retired;
    std::mutex _retire_mutex;
    inline static std::atomic<size_t> _next_tid{0};
};

// RAII guard for epoch entry/exit
class EpochGuard {
   public:
    explicit EpochGuard(EpochManager& em) : _em(em) { _em.enter(); }
    ~EpochGuard() { _em.exit(); }

    EpochGuard(const EpochGuard&) = delete;
    EpochGuard& operator=(const EpochGuard&) = delete;

   private:
    EpochManager& _em;
};

// =============================================================================
// Marked pointer utilities
// =============================================================================

template <typename T>
class MarkedPtr {
   public:
    MarkedPtr() : _ptr(0) {}
    explicit MarkedPtr(T* ptr, bool marked = false)
        : _ptr(reinterpret_cast<uintptr_t>(ptr) | (marked ? 1 : 0)) {}

    T* ptr() const { return reinterpret_cast<T*>(_ptr & ~uintptr_t(1)); }
    bool marked() const { return _ptr & 1; }

    MarkedPtr with_mark() const {
        MarkedPtr result;
        result._ptr = _ptr | 1;
        return result;
    }

    MarkedPtr without_mark() const {
        MarkedPtr result;
        result._ptr = _ptr & ~uintptr_t(1);
        return result;
    }

    bool operator==(const MarkedPtr& other) const { return _ptr == other._ptr; }
    bool operator!=(const MarkedPtr& other) const { return _ptr != other._ptr; }

    uintptr_t raw() const { return _ptr; }

   private:
    uintptr_t _ptr;
};

// =============================================================================
// Concurrent Skiplist
// =============================================================================

template <typename Key, typename Value, typename Compare = std::less<Key>, uint8_t MaxLevel = 24>
class concurrent_skiplist {
   public:
    using key_type = Key;
    using mapped_type = Value;
    using value_type = std::pair<const Key, Value>;
    using size_type = std::size_t;
    using key_compare = Compare;

   private:
    struct Node {
        value_type data;
        uint8_t height;
        std::atomic<MarkedPtr<Node>> next[1];  // Flexible array

        Node(const Key& k, const Value& v, uint8_t h)
            : data(k, v), height(h) {
            for (uint8_t i = 0; i <= h; ++i) {
                next[i].store(MarkedPtr<Node>(), std::memory_order_relaxed);
            }
        }

        Node(Key&& k, Value&& v, uint8_t h)
            : data(std::move(k), std::move(v)), height(h) {
            for (uint8_t i = 0; i <= h; ++i) {
                next[i].store(MarkedPtr<Node>(), std::memory_order_relaxed);
            }
        }

        static size_t alloc_size(uint8_t height) {
            return sizeof(Node) + height * sizeof(std::atomic<MarkedPtr<Node>>);
        }
    };

    // Head node (sentinel, no real data)
    struct HeadNode {
        uint8_t height;
        std::atomic<MarkedPtr<Node>> next[MaxLevel + 1];

        HeadNode() : height(MaxLevel) {
            for (uint8_t i = 0; i <= MaxLevel; ++i) {
                next[i].store(MarkedPtr<Node>(), std::memory_order_relaxed);
            }
        }
    };

   public:
    concurrent_skiplist() : _head(), _size(0), _max_height(0) {}

    ~concurrent_skiplist() {
        // Clean up all nodes
        Node* curr = _head.next[0].load(std::memory_order_relaxed).ptr();
        while (curr) {
            Node* next = curr->next[0].load(std::memory_order_relaxed).ptr();
            destroy_node(curr);
            curr = next;
        }
    }

    // Non-copyable, non-movable (due to atomic members)
    concurrent_skiplist(const concurrent_skiplist&) = delete;
    concurrent_skiplist& operator=(const concurrent_skiplist&) = delete;
    concurrent_skiplist(concurrent_skiplist&&) = delete;
    concurrent_skiplist& operator=(concurrent_skiplist&&) = delete;

    // =========================================================================
    // Find
    // =========================================================================

    std::optional<Value> find(const Key& key) const {
        EpochGuard guard(_epoch);

        Node* pred = nullptr;
        Node* curr = nullptr;
        Node* succ = nullptr;

        // Start from top level
        for (int level = _max_height.load(std::memory_order_relaxed); level >= 0; --level) {
            if (level == _max_height.load(std::memory_order_relaxed)) {
                curr = _head.next[level].load(std::memory_order_acquire).ptr();
            } else {
                curr = (pred ? pred->next[level] : _head.next[level])
                           .load(std::memory_order_acquire)
                           .ptr();
            }

            while (curr) {
                MarkedPtr<Node> curr_next = curr->next[level].load(std::memory_order_acquire);
                // Skip marked nodes
                while (curr_next.marked()) {
                    curr = curr_next.ptr();
                    if (!curr) break;
                    curr_next = curr->next[level].load(std::memory_order_acquire);
                }
                if (!curr) break;

                if (_comp(curr->data.first, key)) {
                    pred = curr;
                    curr = curr_next.ptr();
                } else {
                    break;
                }
            }
        }

        if (curr && !_comp(key, curr->data.first)) {
            // Found exact match
            MarkedPtr<Node> curr_next = curr->next[0].load(std::memory_order_acquire);
            if (!curr_next.marked()) {
                return curr->data.second;
            }
        }

        return std::nullopt;
    }

    bool contains(const Key& key) const { return find(key).has_value(); }

    // =========================================================================
    // Insert
    // =========================================================================

    bool insert(const Key& key, const Value& value) {
        EpochGuard guard(_epoch);

        uint8_t height = random_height();
        Node* new_node = create_node(key, value, height);

        while (true) {
            // Find position and predecessors
            std::array<Node*, MaxLevel + 1> preds;
            std::array<Node*, MaxLevel + 1> succs;

            if (!find_position(key, preds, succs)) {
                // Key already exists
                destroy_node(new_node);
                return false;
            }

            // Set new node's next pointers
            for (uint8_t i = 0; i <= height; ++i) {
                new_node->next[i].store(MarkedPtr<Node>(succs[i]), std::memory_order_relaxed);
            }

            // Try to insert at level 0 first
            Node* pred = preds[0];
            Node* succ = succs[0];

            MarkedPtr<Node> expected(succ);
            MarkedPtr<Node> desired(new_node);

            std::atomic<MarkedPtr<Node>>* pred_next =
                pred ? &pred->next[0] : &_head.next[0];

            if (!pred_next->compare_exchange_strong(expected, desired,
                                                     std::memory_order_release,
                                                     std::memory_order_relaxed)) {
                // CAS failed, retry
                continue;
            }

            // Successfully inserted at level 0, now link higher levels
            for (uint8_t i = 1; i <= height; ++i) {
                while (true) {
                    pred = preds[i];
                    succ = succs[i];

                    // Update new_node's next pointer if needed
                    MarkedPtr<Node> new_next = new_node->next[i].load(std::memory_order_relaxed);
                    if (new_next.marked()) {
                        // Node was deleted while we were linking, give up
                        goto done;
                    }
                    if (new_next.ptr() != succ) {
                        new_node->next[i].store(MarkedPtr<Node>(succ), std::memory_order_relaxed);
                    }

                    expected = MarkedPtr<Node>(succ);
                    desired = MarkedPtr<Node>(new_node);

                    pred_next = pred ? &pred->next[i] : &_head.next[i];

                    if (pred_next->compare_exchange_strong(expected, desired,
                                                           std::memory_order_release,
                                                           std::memory_order_relaxed)) {
                        break;  // Success at this level
                    }

                    // CAS failed, re-find position for this level
                    find_position(key, preds, succs);

                    // Check if our node was deleted
                    if (new_node->next[0].load(std::memory_order_acquire).marked()) {
                        goto done;
                    }
                }
            }

        done:
            // Update max height
            uint8_t old_height = _max_height.load(std::memory_order_relaxed);
            while (height > old_height) {
                if (_max_height.compare_exchange_weak(old_height, height,
                                                       std::memory_order_relaxed)) {
                    break;
                }
            }

            _size.fetch_add(1, std::memory_order_relaxed);
            return true;
        }
    }

    template <typename... Args>
    bool emplace(const Key& key, Args&&... args) {
        return insert(key, Value(std::forward<Args>(args)...));
    }

    // =========================================================================
    // Erase
    // =========================================================================

    bool erase(const Key& key) {
        EpochGuard guard(_epoch);

        std::array<Node*, MaxLevel + 1> preds;
        std::array<Node*, MaxLevel + 1> succs;

        while (true) {
            if (!find_position_for_delete(key, preds, succs)) {
                return false;  // Not found
            }

            Node* node_to_delete = succs[0];
            if (!node_to_delete || _comp(key, node_to_delete->data.first)) {
                return false;  // Not found
            }

            // Mark all levels from top to bottom
            bool marked = false;
            for (int i = node_to_delete->height; i >= 0; --i) {
                MarkedPtr<Node> succ = node_to_delete->next[i].load(std::memory_order_acquire);

                while (!succ.marked()) {
                    MarkedPtr<Node> marked_succ = succ.with_mark();
                    if (node_to_delete->next[i].compare_exchange_strong(
                            succ, marked_succ, std::memory_order_release,
                            std::memory_order_relaxed)) {
                        marked = true;
                        break;
                    }
                }
            }

            if (marked) {
                // Help physically remove the node
                find_position(key, preds, succs);
                _size.fetch_sub(1, std::memory_order_relaxed);
                _epoch.retire(node_to_delete);
                return true;
            }

            // Someone else marked it, retry
        }
    }

    // =========================================================================
    // Utilities
    // =========================================================================

    size_type size() const { return _size.load(std::memory_order_relaxed); }
    bool empty() const { return size() == 0; }

    // Clear all elements (NOT thread-safe, call only when no other threads are accessing)
    void clear() {
        Node* curr = _head.next[0].load(std::memory_order_relaxed).ptr();
        while (curr) {
            Node* next = curr->next[0].load(std::memory_order_relaxed).ptr();
            destroy_node(curr);
            curr = next;
        }

        for (uint8_t i = 0; i <= MaxLevel; ++i) {
            _head.next[i].store(MarkedPtr<Node>(), std::memory_order_relaxed);
        }
        _size.store(0, std::memory_order_relaxed);
        _max_height.store(0, std::memory_order_relaxed);
    }

    // Iterate (snapshot semantics - may see concurrent modifications)
    template <typename Func>
    void for_each(Func&& func) const {
        EpochGuard guard(_epoch);

        Node* curr = _head.next[0].load(std::memory_order_acquire).ptr();
        while (curr) {
            MarkedPtr<Node> next = curr->next[0].load(std::memory_order_acquire);
            if (!next.marked()) {
                func(curr->data.first, curr->data.second);
            }
            curr = next.ptr();
        }
    }

   private:
    // Find position for insert, returns false if key exists
    bool find_position(const Key& key, std::array<Node*, MaxLevel + 1>& preds,
                       std::array<Node*, MaxLevel + 1>& succs) const {
        bool found = false;

    retry:
        Node* pred = nullptr;

        for (int level = MaxLevel; level >= 0; --level) {
            std::atomic<MarkedPtr<Node>>* pred_next =
                pred ? &pred->next[level] : const_cast<std::atomic<MarkedPtr<Node>>*>(&_head.next[level]);

            MarkedPtr<Node> curr_mp = pred_next->load(std::memory_order_acquire);
            Node* curr = curr_mp.ptr();

            while (curr) {
                MarkedPtr<Node> succ_mp = curr->next[level].load(std::memory_order_acquire);
                Node* succ = succ_mp.ptr();

                // Help remove marked nodes
                while (succ_mp.marked()) {
                    MarkedPtr<Node> expected = MarkedPtr<Node>(curr);
                    MarkedPtr<Node> desired = MarkedPtr<Node>(succ);

                    if (!pred_next->compare_exchange_strong(expected, desired,
                                                             std::memory_order_release,
                                                             std::memory_order_relaxed)) {
                        goto retry;
                    }

                    curr = succ;
                    if (!curr) break;
                    succ_mp = curr->next[level].load(std::memory_order_acquire);
                    succ = succ_mp.ptr();
                }

                if (!curr) break;

                if (_comp(curr->data.first, key)) {
                    pred = curr;
                    pred_next = &pred->next[level];
                    curr_mp = pred_next->load(std::memory_order_acquire);
                    curr = curr_mp.ptr();
                } else {
                    if (!_comp(key, curr->data.first)) {
                        found = true;
                    }
                    break;
                }
            }

            preds[level] = pred;
            succs[level] = curr;
        }

        return !found;
    }

    // Find position for delete (similar but doesn't help remove)
    bool find_position_for_delete(const Key& key, std::array<Node*, MaxLevel + 1>& preds,
                                   std::array<Node*, MaxLevel + 1>& succs) const {
        bool found = false;
        Node* pred = nullptr;

        for (int level = MaxLevel; level >= 0; --level) {
            std::atomic<MarkedPtr<Node>>* pred_next =
                pred ? &pred->next[level] : const_cast<std::atomic<MarkedPtr<Node>>*>(&_head.next[level]);

            Node* curr = pred_next->load(std::memory_order_acquire).ptr();

            while (curr) {
                MarkedPtr<Node> succ_mp = curr->next[level].load(std::memory_order_acquire);

                // Skip marked nodes
                while (succ_mp.marked()) {
                    curr = succ_mp.ptr();
                    if (!curr) break;
                    succ_mp = curr->next[level].load(std::memory_order_acquire);
                }

                if (!curr) break;

                if (_comp(curr->data.first, key)) {
                    pred = curr;
                    pred_next = &pred->next[level];
                    curr = pred_next->load(std::memory_order_acquire).ptr();
                } else {
                    if (!_comp(key, curr->data.first)) {
                        found = true;
                    }
                    break;
                }
            }

            preds[level] = pred;
            succs[level] = curr;
        }

        return found;
    }

    Node* create_node(const Key& key, const Value& value, uint8_t height) {
        void* mem = ::operator new(Node::alloc_size(height));
        return new (mem) Node(key, value, height);
    }

    void destroy_node(Node* node) {
        node->~Node();
        ::operator delete(node);
    }

    uint8_t random_height() {
        thread_local std::mt19937 rng(std::random_device{}());
        thread_local std::geometric_distribution<uint8_t> dist(0.5);

        uint8_t height = dist(rng);
        return std::min(height, static_cast<uint8_t>(MaxLevel));
    }

    HeadNode _head;
    std::atomic<size_type> _size;
    std::atomic<uint8_t> _max_height;
    mutable EpochManager _epoch;
    Compare _comp;
};

// Convenience alias
template <typename Key, typename Value, typename Compare = std::less<Key>>
using concurrent_skiplist_map = concurrent_skiplist<Key, Value, Compare>;

}  // namespace stdb::container

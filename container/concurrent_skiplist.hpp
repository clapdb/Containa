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
#include <memory>
#include <memory_resource>
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

template <typename Key, typename Value, typename Compare = std::less<Key>,
          typename Alloc = std::allocator<std::pair<const Key, Value>>, uint8_t MaxLevel = 12>
class concurrent_skiplist {
   public:
    using key_type = Key;
    using mapped_type = Value;
    using value_type = std::pair<const Key, Value>;
    using size_type = std::size_t;
    using allocator_type = Alloc;

   private:
    using AllocTraits = std::allocator_traits<Alloc>;
    // Rebind to byte allocator for variable-sized node allocation
    using ByteAlloc = typename AllocTraits::template rebind_alloc<std::byte>;
    using ByteAllocTraits = std::allocator_traits<ByteAlloc>;

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
        // Ownership after erase. A deleted node is unlinked from every level now, so the level-0 chain
        // can no longer be used to find it at destruction time -- `retired_next` threads it onto the
        // skiplist's retired list instead, and `retired` says which of the two owns it. Its next[]
        // pointers are deliberately left intact: nothing is ever freed before the destructor, so a
        // reader that is still standing on this node must be able to walk off it.
        bool retired = false;
        Node* retired_next = nullptr;
        std::atomic<MarkedPtr<Node>> next[1];  // flexible array

        static size_t alloc_size(uint8_t h) {
            return sizeof(Node) + h * sizeof(std::atomic<MarkedPtr<Node>>);
        }

        static Node* create(const Key& k, const Value& v, uint8_t h, ByteAlloc& alloc) {
            size_t size = alloc_size(h);
            void* mem = ByteAllocTraits::allocate(alloc, size);
            Node* node = static_cast<Node*>(mem);
            try {
                new (&node->key) Key(k);
                try {
                    new (&node->value) Value(v);
                } catch (...) {
                    node->key.~Key();
                    throw;
                }
            } catch (...) {
                ByteAllocTraits::deallocate(alloc, static_cast<std::byte*>(mem), size);
                throw;
            }
            node->height = h;
            node->retired = false;
            node->retired_next = nullptr;
            for (uint8_t i = 0; i <= h; ++i) {
                new (&node->next[i]) std::atomic<MarkedPtr<Node>>(MarkedPtr<Node>());
            }
            return node;
        }

        static Node* create(Key&& k, Value&& v, uint8_t h, ByteAlloc& alloc) {
            size_t size = alloc_size(h);
            void* mem = ByteAllocTraits::allocate(alloc, size);
            Node* node = static_cast<Node*>(mem);
            try {
                new (&node->key) Key(std::move(k));
                try {
                    new (&node->value) Value(std::move(v));
                } catch (...) {
                    node->key.~Key();
                    throw;
                }
            } catch (...) {
                ByteAllocTraits::deallocate(alloc, static_cast<std::byte*>(mem), size);
                throw;
            }
            node->height = h;
            node->retired = false;
            node->retired_next = nullptr;
            for (uint8_t i = 0; i <= h; ++i) {
                new (&node->next[i]) std::atomic<MarkedPtr<Node>>(MarkedPtr<Node>());
            }
            return node;
        }

        void destroy(ByteAlloc& alloc) {
            size_t size = alloc_size(height);
            key.~Key();
            value.~Value();
            ByteAllocTraits::deallocate(alloc, reinterpret_cast<std::byte*>(this), size);
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

    // Erased nodes. Nothing is reclaimed until the destructor, so this is not a reclamation scheme --
    // it is only how the destructor still finds nodes that erase() has unlinked from every level.
    std::atomic<Node*> _retired{nullptr};

    // Comparator on its own to avoid interference
    alignas(64) Compare _cmp;

    // Allocator (mutable because allocation may happen in const-qualified contexts internally)
    mutable ByteAlloc _alloc;

    // Simplified memory management:
    // - Don't free nodes during operation (leave in list, just marked)
    // - Only clean up in destructor
    // This is safe and simple, though it leaks memory during heavy delete workloads
    //
    // NOTE: For PMR usage, the underlying memory_resource must be thread-safe
    // (e.g., std::pmr::synchronized_pool_resource). Using non-thread-safe resources
    // with concurrent operations is undefined behavior.

   public:
    concurrent_skiplist() : _alloc() {
        for (uint8_t i = 0; i <= MaxLevel; ++i) {
            _head[i].store(MarkedPtr<Node>(), std::memory_order_relaxed);
        }
    }

    explicit concurrent_skiplist(const Alloc& alloc) : _alloc(alloc) {
        for (uint8_t i = 0; i <= MaxLevel; ++i) {
            _head[i].store(MarkedPtr<Node>(), std::memory_order_relaxed);
        }
    }

    ~concurrent_skiplist() {
        // Live nodes hang off the level-0 chain; erased ones hang off the retired list. A retired node
        // is normally already unlinked, but a lost CAS in erase() can leave one in the chain as well,
        // so skip those here and let the retired walk below free them -- exactly once either way.
        Node* curr = _head[0].load(std::memory_order_relaxed).ptr();
        while (curr) {
            Node* next = curr->next[0].load(std::memory_order_relaxed).ptr();
            if (!curr->retired) {
                curr->destroy(_alloc);
            }
            curr = next;
        }

        Node* retired = _retired.load(std::memory_order_relaxed);
        while (retired) {
            Node* next = retired->retired_next;
            retired->destroy(_alloc);
            retired = next;
        }
    }

    concurrent_skiplist(const concurrent_skiplist&) = delete;
    concurrent_skiplist& operator=(const concurrent_skiplist&) = delete;

    [[nodiscard]] auto get_allocator() const noexcept -> allocator_type {
        return allocator_type(_alloc);
    }

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
        Node* new_node = Node::create(key, value, height, _alloc);

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
                new_node->destroy(_alloc);
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
        Node* new_node = Node::create(std::move(key), std::move(value), height, _alloc);

        uint8_t old_h = _height.load(std::memory_order_relaxed);
        while (height > old_h) {
            if (_height.compare_exchange_weak(old_h, height, std::memory_order_relaxed)) break;
        }

        Node* preds[MaxLevel + 1];
        Node* succs[MaxLevel + 1];

        while (true) {
            if (!find_insert_position(new_node->key, preds, succs)) {
                new_node->destroy(_alloc);
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

            // Mark the levels above 0 first, top down. These are pure helping -- whoever gets there
            // sets the mark, and losing the CAS is fine because it means someone else already did.
            // Use release so readers with acquire can see the mark.
            for (int i = victim->height; i >= 1; --i) {
                MarkedPtr<Node> next = victim->next[i].load(std::memory_order_relaxed);
                while (!next.marked()) {
                    if (victim->next[i].compare_exchange_weak(next, next.with_mark(),
                                                               std::memory_order_release,
                                                               std::memory_order_relaxed)) {
                        break;
                    }
                }
            }

            // Level 0 decides ownership: exactly one thread flips this mark, and only that thread
            // unlinks the node, retires it and decrements the size.
            //
            // The loop above deliberately cannot tell you that, because it exits on `!next.marked()`
            // -- which is also true for the thread that *lost* the CAS to whoever marked it first. So
            // two threads erasing the same key would both come out of it believing they had marked the
            // node. That was survivable when an erased node simply stayed in the list; it is not
            // survivable now that erase() hands the node to the retired list, because the node would
            // be pushed twice and the destructor would free it twice. Take ownership explicitly.
            MarkedPtr<Node> next0 = victim->next[0].load(std::memory_order_relaxed);
            bool owner = false;
            while (!next0.marked()) {
                if (victim->next[0].compare_exchange_weak(next0, next0.with_mark(),
                                                           std::memory_order_release,
                                                           std::memory_order_relaxed)) {
                    owner = true;
                    break;
                }
            }
            if (!owner) {
                return false;  // another thread erased this node; it is gone either way
            }

            // Physically unlink at every level, level 0 included. Leaving the victim wired into the
            // level-0 chain is what used to hang insert(): find_insert_position() skipped it and
            // reported the *logical* successor in succs[0], while pred->next[0] still physically
            // pointed at the victim, so insert()'s CAS compared succs[0] against a corpse, failed,
            // retried, and failed again forever. If the CAS below loses to a racing thread, the
            // helper in find_insert_position() finishes the unlink.
            for (int i = victim->height; i >= 0; --i) {
                std::atomic<MarkedPtr<Node>>* pred_next = preds[i] ? &preds[i]->next[i] : &_head[i];
                MarkedPtr<Node> expected(victim);
                // acquire on the load, release on the CAS -- and neither is optional.
                //
                // This CAS *publishes* victim's successor into pred->next[i]. A reader that acquire-loads
                // pred->next[i] and finds that node has to see the node's contents, and it only does if
                // the store that put it there was a release. Relaxed here breaks the chain: the node was
                // released into the list by whoever inserted it, but that release was on a *different*
                // location, and re-publishing it under relaxed carries none of it forward. The reader
                // then reaches a node it is not guaranteed to see the construction of. TSan calls it
                // exactly that -- a data race between Node::create() and the traversal load.
                MarkedPtr<Node> succ = victim->next[i].load(std::memory_order_acquire);
                pred_next->compare_exchange_strong(expected, MarkedPtr<Node>(succ.ptr()),
                                                    std::memory_order_release, std::memory_order_relaxed);
            }

            // The destructor used to find erased nodes by walking the level-0 chain. They are not in
            // it any more, so thread the victim onto the retired list, which now owns it. `retired`
            // keeps the destructor from freeing it twice if a lost CAS above left it still linked.
            victim->retired = true;
            Node* head = _retired.load(std::memory_order_relaxed);
            do {
                victim->retired_next = head;
            } while (!_retired.compare_exchange_weak(head, victim, std::memory_order_release,
                                                     std::memory_order_relaxed));

            _size.fetch_sub(1, std::memory_order_relaxed);
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

                // Help remove marked nodes, at every level including 0.
                //
                // Level 0 used to be special-cased into "skip it but leave it linked", so that the
                // destructor could still find erased nodes by walking the chain. That is what made
                // insert() spin: it CASes pred->next[0] against succs[0], and succs[0] is the successor
                // this search *reports*, with the marked node skipped -- while pred->next[0] still
                // physically points at the marked node. The two can never be equal, so the CAS can never
                // succeed, and insert()'s retry loop never terminates. Erased nodes are on the retired
                // list now, so level 0 no longer has to carry them and no longer does.
                // The two marks mean different things and must not be handled together.
                //
                // curr.marked() is the mark on the pointer we just read out of pred->next, and by the
                // Harris convention a mark on x->next says *x* is deleted. So it says pred is being
                // erased. It says nothing whatever about curr, which may be a live node that someone
                // inserted after pred a moment ago. Splicing pred->next in that state -- which is what
                // handling the two cases with one CAS did -- rewrites the dying predecessor's next
                // pointer straight past that live node. The eraser then unlinks pred and takes the live
                // node with it: still counted in _size, no longer reachable from level 0, and not on the
                // retired list either, so the destructor cannot free it. Reproduced: size() reports 1500
                // where only 1499 nodes are reachable.
                //
                // So do not touch a marked predecessor. Start the search again; the pass that meets pred
                // as curr will see the mark on *its* next and unlink it properly.
                if (curr.marked()) {
                    pred = nullptr;
                    goto retry;
                }

                // succ.marked() is the mark on curr->next: curr itself is deleted. Splice it, and any
                // run of marked nodes behind it, out of pred->next -- which is unmarked here, so pred is
                // alive and its next pointer is ours to rewrite.
                if (succ.marked()) {
                    // Walk to the first *live* node after curr.
                    //
                    // The mark on a pointer belongs to the node the pointer came out of, not to the node
                    // it points at: succ is curr->next, and its mark says curr is deleted. So to decide
                    // whether the next node n is deleted you have to look at n->next, and if it is not
                    // marked then n is alive and n is the answer.
                    //
                    // The loop this replaces walked one node too far. It started with next_unmarked =
                    // succ -- already marked, because curr is deleted -- stepped to n->next, found that
                    // unmarked because n is alive, and stopped holding *n's successor*. The CAS below
                    // then spliced pred straight past n. n was live, still counted in _size, and now
                    // reachable from nothing: reproduced as size() == 1500 with 1499 nodes reachable.
                    Node* live = succ.ptr();
                    while (live != nullptr) {
                        MarkedPtr<Node> live_next = live->next[i].load(std::memory_order_acquire);
                        if (!live_next.marked()) {
                            break;  // `live` is not deleted -- this is the first survivor
                        }
                        live = live_next.ptr();
                    }
                    if (!pred_next->compare_exchange_strong(curr, MarkedPtr<Node>(live),
                                                             std::memory_order_release,
                                                             std::memory_order_relaxed)) {
                        // CAS failed, restart from top
                        pred = nullptr;
                        goto retry;
                    }
                    curr = MarkedPtr<Node>(live);
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

template <typename Key, typename Value, typename Compare = std::less<Key>,
          typename Alloc = std::allocator<std::pair<const Key, Value>>>
using concurrent_skiplist_map = concurrent_skiplist<Key, Value, Compare, Alloc>;

}  // namespace stdb::container

// PMR type aliases
// NOTE: When using PMR with concurrent_skiplist, the underlying memory_resource
// MUST be thread-safe (e.g., std::pmr::synchronized_pool_resource).
// Using non-thread-safe resources like monotonic_buffer_resource or
// unsynchronized_pool_resource with concurrent operations is undefined behavior.
namespace stdb::pmr {
template <typename Key, typename Value, typename Compare = std::less<Key>, uint8_t MaxLevel = 12>
using concurrent_skiplist =
    container::concurrent_skiplist<Key, Value, Compare,
                                   std::pmr::polymorphic_allocator<std::pair<const Key, Value>>, MaxLevel>;

template <typename Key, typename Value, typename Compare = std::less<Key>>
using concurrent_skiplist_map =
    container::concurrent_skiplist<Key, Value, Compare,
                                   std::pmr::polymorphic_allocator<std::pair<const Key, Value>>>;
}  // namespace stdb::pmr

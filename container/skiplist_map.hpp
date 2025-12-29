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
#include <initializer_list>
#include <iterator>
#include <limits>
#include <memory>
#include <random>
#include <stdexcept>
#include <utility>

#if __cplusplus >= 202002L || (defined(_MSVC_LANG) && _MSVC_LANG >= 202002L)
#include <compare>
#endif

namespace stdb::container {

// Trait to detect transparent comparators (have is_transparent type member)
// Guard to avoid redefinition when included with btree_map.hpp
#ifndef STDB_CONTAINER_IS_TRANSPARENT_COMPARATOR_DEFINED
#define STDB_CONTAINER_IS_TRANSPARENT_COMPARATOR_DEFINED

template <typename, typename = void>
struct is_transparent_comparator : std::false_type {};

template <typename T>
struct is_transparent_comparator<T, std::void_t<typename T::is_transparent>> : std::true_type {};

template <typename T>
inline constexpr bool is_transparent_comparator_v = is_transparent_comparator<T>::value;

#endif  // STDB_CONTAINER_IS_TRANSPARENT_COMPARATOR_DEFINED

/*
 * skiplist_map is a skip list based ordered map container.
 *
 * Key features:
 *   - O(log N) average lookup, insert, and erase operations
 *   - Iterator stability: iterators remain valid after insert (only erased element invalidated)
 *   - Pointer stability: pointers/references to elements remain valid
 *   - Good concurrent access potential (not implemented in this version)
 *
 * Trade-offs vs btree_map:
 *   - Less cache-friendly (nodes are scattered in memory)
 *   - Higher memory overhead per element (~2 pointers average)
 *   - Simpler implementation
 *   - Better iterator/pointer stability
 *
 * Trade-offs vs std::map:
 *   - Forward iterator only (no reverse iteration)
 *   - Similar memory overhead
 *   - Probabilistic balance vs strict balance
 *
 * Template parameters:
 *   Key - Key type (must be comparable)
 *   Value - Mapped value type
 *   Compare - Comparison function (default: std::less<Key>)
 *   Allocator - Allocator type
 *   MaxLevel - Maximum number of levels (default: 32)
 *   Probability - 1/P chance to promote to next level (default: 4 = 25%)
 */

template <typename Key, typename Value, typename Compare = std::less<Key>,
          typename Allocator = std::allocator<std::pair<const Key, Value>>,
          uint8_t MaxLevel = 32, uint8_t Probability = 4>
class skiplist_map
{
   public:
    using key_type = Key;
    using mapped_type = Value;
    using value_type = std::pair<const Key, Value>;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    using key_compare = Compare;
    using allocator_type = Allocator;
    using reference = value_type&;
    using const_reference = const value_type&;
    using pointer = value_type*;
    using const_pointer = const value_type*;

    // value_compare - compares value_type by key (std::map compatible)
    class value_compare
    {
        friend class skiplist_map;

       protected:
        Compare comp;
        explicit value_compare(Compare c) : comp(c) {}

       public:
        using result_type [[deprecated]] = bool;
        using first_argument_type [[deprecated]] = value_type;
        using second_argument_type [[deprecated]] = value_type;

        bool operator()(const value_type& lhs, const value_type& rhs) const { return comp(lhs.first, rhs.first); }
    };

    // Node handle for extract/insert operations (C++17)
    class node_type
    {
        friend class skiplist_map;

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

        [[nodiscard]] key_type& key() { return _storage.first; }
        [[nodiscard]] mapped_type& mapped() { return _storage.second; }

        void swap(node_type& other) noexcept {
            std::swap(_storage, other._storage);
            std::swap(_valid, other._valid);
        }

       private:
        explicit node_type(Key&& k, Value&& v) : _storage(std::move(k), std::move(v)), _valid(true) {}
        explicit node_type(const Key& k, Value&& v) : _storage(k, std::move(v)), _valid(true) {}

        std::pair<Key, Value> _storage;
        bool _valid = false;
    };

    // Forward declarations for insert_return_type
    class iterator;

    // Insert return type for node handle insert
    struct insert_return_type
    {
        iterator position;
        bool inserted;
        node_type node;
    };

   private:
    // Storage type without const (for internal manipulation)
    using storage_type = std::pair<Key, Value>;

    // Skip list node structure
    // Uses flexible array member pattern for variable-size forward pointer array
    struct node_base
    {
        uint8_t level;  // Number of forward pointers (0 to MaxLevel)
        // forward pointers follow immediately after in memory
    };

    // Actual node with data
    struct node : node_base
    {
        storage_type data;
        // forward[0..level] pointers follow
        // Access via get_forward()

        [[nodiscard]] auto& key() noexcept { return data.first; }
        [[nodiscard]] const auto& key() const noexcept { return data.first; }
        [[nodiscard]] auto& value() noexcept { return data.second; }
        [[nodiscard]] const auto& value() const noexcept { return data.second; }

        // Get offset to forward pointer array (must be pointer-aligned)
        static constexpr size_type forward_offset() noexcept {
            constexpr size_type base_size = sizeof(node);
            constexpr size_type ptr_align = alignof(node*);
            return (base_size + ptr_align - 1) & ~(ptr_align - 1);
        }

        // Get forward pointer array (located after data member, properly aligned)
        [[nodiscard]] node** get_forward() noexcept {
            return reinterpret_cast<node**>(reinterpret_cast<char*>(this) + forward_offset());
        }
        [[nodiscard]] node* const* get_forward() const noexcept {
            return reinterpret_cast<node* const*>(reinterpret_cast<const char*>(this) + forward_offset());
        }

        // Get forward pointer at level i
        [[nodiscard]] node* forward(uint8_t i) const noexcept { return get_forward()[i]; }
        void set_forward(uint8_t i, node* n) noexcept { get_forward()[i] = n; }
    };

    // Calculate node allocation size for a given level
    static constexpr size_type node_size(uint8_t level) noexcept {
        return node::forward_offset() + (static_cast<size_type>(level) + 1) * sizeof(node*);
    }

    // Head node (sentinel, no data, only forward pointers)
    struct head_node
    {
        uint8_t level = 0;  // Current maximum level in the list
        node* forward[MaxLevel + 1] = {};

        [[nodiscard]] node* get_forward(uint8_t i) const noexcept { return forward[i]; }
        void set_forward(uint8_t i, node* n) noexcept { forward[i] = n; }
    };

    // Allocator rebinding for node allocation
    using byte_allocator_type = typename std::allocator_traits<Allocator>::template rebind_alloc<char>;

    // Member variables
    head_node _head;
    size_type _size = 0;
    [[no_unique_address]] Compare _comp;
    [[no_unique_address]] byte_allocator_type _alloc;
    uint64_t _rng_state;  // Xorshift64 state for random level generation

    // Xorshift64 random number generator (fast, good quality)
    uint64_t xorshift64() noexcept {
        uint64_t x = _rng_state;
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        _rng_state = x;
        return x;
    }

    // Generate random level for new node using geometric distribution
    uint8_t random_level() noexcept {
        uint8_t level = 0;
        // Each level has 1/Probability chance of promotion
        while (level < MaxLevel && (xorshift64() % Probability) == 0) {
            ++level;
        }
        return level;
    }

    // Allocate a new node with given level
    node* allocate_node(uint8_t level) {
        size_type size = node_size(level);
        char* mem = std::allocator_traits<byte_allocator_type>::allocate(_alloc, size);
        std::memset(mem, 0, size);
        node* n = reinterpret_cast<node*>(mem);
        n->level = level;
        return n;
    }

    // Deallocate a node
    void deallocate_node(node* n) {
        if (n) {
            size_type size = node_size(n->level);
            // Destroy the data
            n->data.~storage_type();
            std::allocator_traits<byte_allocator_type>::deallocate(_alloc, reinterpret_cast<char*>(n), size);
        }
    }

    // Find node and populate update array
    // Returns the node at position >= key, or nullptr if not found
    // update[i] points to the last node at level i before the target position
    template <typename K>
    node* find_node(const K& key, node* update[MaxLevel + 1]) const noexcept {
        node* current = nullptr;

        // Start from highest level
        for (int i = static_cast<int>(_head.level); i >= 0; --i) {
            node* next = (current == nullptr) ? _head.forward[i] : current->forward(static_cast<uint8_t>(i));

            while (next != nullptr && _comp(next->key(), key)) {
                current = next;
                next = current->forward(static_cast<uint8_t>(i));
            }
            update[i] = current;
        }

        // Move to the candidate node at level 0
        node* candidate = (current == nullptr) ? _head.forward[0] : current->forward(0);
        return candidate;
    }

    // Find node without update array (for const operations)
    template <typename K>
    node* find_node(const K& key) const noexcept {
        node* current = nullptr;

        for (int i = static_cast<int>(_head.level); i >= 0; --i) {
            node* next = (current == nullptr) ? _head.forward[i] : current->forward(static_cast<uint8_t>(i));

            while (next != nullptr && _comp(next->key(), key)) {
                current = next;
                next = current->forward(static_cast<uint8_t>(i));
            }
        }

        node* candidate = (current == nullptr) ? _head.forward[0] : current->forward(0);
        return candidate;
    }

    // Copy all nodes from another skiplist
    void copy_from(const skiplist_map& other) {
        node* update[MaxLevel + 1] = {};
        for (uint8_t i = 0; i <= MaxLevel; ++i) {
            update[i] = nullptr;
        }

        for (const node* src = other._head.forward[0]; src != nullptr; src = src->forward(0)) {
            node* new_node = allocate_node(src->level);
            new (&new_node->data) storage_type(src->data);

            // Link at all levels
            for (uint8_t i = 0; i <= src->level; ++i) {
                if (update[i] == nullptr) {
                    new_node->set_forward(i, _head.forward[i]);
                    _head.forward[i] = new_node;
                } else {
                    new_node->set_forward(i, update[i]->forward(i));
                    update[i]->set_forward(i, new_node);
                }
                update[i] = new_node;
            }

            if (src->level > _head.level) {
                _head.level = src->level;
            }
        }
        _size = other._size;
    }

   public:
    // Forward iterator (forward_iterator_tag)
    class iterator
    {
        friend class skiplist_map;

       public:
        using iterator_category = std::forward_iterator_tag;
        using difference_type = std::ptrdiff_t;
        using value_type = skiplist_map::value_type;
        using pointer = value_type*;
        using reference = value_type&;

        iterator() noexcept : _node(nullptr) {}

        reference operator*() const noexcept {
            return reinterpret_cast<reference>(_node->data);
        }

        pointer operator->() const noexcept {
            return reinterpret_cast<pointer>(&_node->data);
        }

        iterator& operator++() noexcept {
            _node = _node->forward(0);
            return *this;
        }

        iterator operator++(int) noexcept {
            iterator tmp = *this;
            ++(*this);
            return tmp;
        }

        bool operator==(const iterator& other) const noexcept { return _node == other._node; }
        bool operator!=(const iterator& other) const noexcept { return _node != other._node; }

       private:
        explicit iterator(node* n) noexcept : _node(n) {}
        node* _node;
    };

    // Const forward iterator
    class const_iterator
    {
        friend class skiplist_map;

       public:
        using iterator_category = std::forward_iterator_tag;
        using difference_type = std::ptrdiff_t;
        using value_type = const skiplist_map::value_type;
        using pointer = const value_type*;
        using reference = const value_type&;

        const_iterator() noexcept : _node(nullptr) {}
        const_iterator(const iterator& it) noexcept : _node(it._node) {}

        reference operator*() const noexcept {
            return reinterpret_cast<reference>(_node->data);
        }

        pointer operator->() const noexcept {
            return reinterpret_cast<pointer>(&_node->data);
        }

        const_iterator& operator++() noexcept {
            _node = _node->forward(0);
            return *this;
        }

        const_iterator operator++(int) noexcept {
            const_iterator tmp = *this;
            ++(*this);
            return tmp;
        }

        bool operator==(const const_iterator& other) const noexcept { return _node == other._node; }
        bool operator!=(const const_iterator& other) const noexcept { return _node != other._node; }

       private:
        explicit const_iterator(const node* n) noexcept : _node(n) {}
        const node* _node;
    };

    // Constructors
    skiplist_map() : _rng_state(std::random_device{}()) {
        // Ensure non-zero RNG state
        if (_rng_state == 0) _rng_state = 1;
    }

    explicit skiplist_map(const Compare& comp, const Allocator& alloc = Allocator())
        : _comp(comp), _alloc(alloc), _rng_state(std::random_device{}()) {
        if (_rng_state == 0) _rng_state = 1;
    }

    explicit skiplist_map(const Allocator& alloc)
        : _alloc(alloc), _rng_state(std::random_device{}()) {
        if (_rng_state == 0) _rng_state = 1;
    }

    template <typename InputIt>
    skiplist_map(InputIt first, InputIt last, const Compare& comp = Compare(), const Allocator& alloc = Allocator())
        : _comp(comp), _alloc(alloc), _rng_state(std::random_device{}()) {
        if (_rng_state == 0) _rng_state = 1;
        insert(first, last);
    }

    skiplist_map(std::initializer_list<value_type> init, const Compare& comp = Compare(),
                 const Allocator& alloc = Allocator())
        : _comp(comp), _alloc(alloc), _rng_state(std::random_device{}()) {
        if (_rng_state == 0) _rng_state = 1;
        insert(init);
    }

    // Copy constructor
    skiplist_map(const skiplist_map& other)
        : _comp(other._comp), _alloc(other._alloc), _rng_state(other._rng_state) {
        copy_from(other);
    }

    skiplist_map(const skiplist_map& other, const Allocator& alloc)
        : _comp(other._comp), _alloc(alloc), _rng_state(other._rng_state) {
        copy_from(other);
    }

    // Move constructor
    skiplist_map(skiplist_map&& other) noexcept
        : _head(other._head),
          _size(other._size),
          _comp(std::move(other._comp)),
          _alloc(std::move(other._alloc)),
          _rng_state(other._rng_state) {
        other._head = head_node{};
        other._size = 0;
    }

    skiplist_map(skiplist_map&& other, const Allocator& alloc)
        : _comp(std::move(other._comp)), _alloc(alloc), _rng_state(other._rng_state) {
        if (_alloc == other._alloc) {
            _head = other._head;
            _size = other._size;
            other._head = head_node{};
            other._size = 0;
        } else {
            copy_from(other);
            other.clear();
        }
    }

    // Destructor
    ~skiplist_map() { clear(); }

    // Copy assignment
    skiplist_map& operator=(const skiplist_map& other) {
        if (this != &other) {
            clear();
            _comp = other._comp;
            copy_from(other);
        }
        return *this;
    }

    // Move assignment
    skiplist_map& operator=(skiplist_map&& other) noexcept {
        if (this != &other) {
            clear();
            _head = other._head;
            _size = other._size;
            _comp = std::move(other._comp);
            _alloc = std::move(other._alloc);
            _rng_state = other._rng_state;
            other._head = head_node{};
            other._size = 0;
        }
        return *this;
    }

    // Initializer list assignment
    skiplist_map& operator=(std::initializer_list<value_type> init) {
        clear();
        insert(init);
        return *this;
    }

    // Allocator
    [[nodiscard]] allocator_type get_allocator() const noexcept {
        return allocator_type(_alloc);
    }

    // Capacity
    [[nodiscard]] bool empty() const noexcept { return _size == 0; }
    [[nodiscard]] size_type size() const noexcept { return _size; }
    [[nodiscard]] static constexpr size_type max_size() noexcept {
        return std::numeric_limits<size_type>::max() / sizeof(node);
    }

    // Clear
    void clear() noexcept {
        node* current = _head.forward[0];
        while (current != nullptr) {
            node* next = current->forward(0);
            deallocate_node(current);
            current = next;
        }
        _head = head_node{};
        _size = 0;
    }

    // Iterators
    [[nodiscard]] iterator begin() noexcept { return iterator(_head.forward[0]); }
    [[nodiscard]] const_iterator begin() const noexcept { return const_iterator(_head.forward[0]); }
    [[nodiscard]] const_iterator cbegin() const noexcept { return const_iterator(_head.forward[0]); }
    [[nodiscard]] iterator end() noexcept { return iterator(nullptr); }
    [[nodiscard]] const_iterator end() const noexcept { return const_iterator(nullptr); }
    [[nodiscard]] const_iterator cend() const noexcept { return const_iterator(nullptr); }

    // Insert
    // Constraint: K must not be an iterator type (to avoid ambiguity with hint-based insert)
    template <typename K, typename V, typename = std::enable_if_t<
        !std::is_same_v<std::decay_t<K>, iterator> &&
        !std::is_same_v<std::decay_t<K>, const_iterator>>>
    auto insert(K&& key, V&& value) -> std::pair<iterator, bool> {
        node* update[MaxLevel + 1];
        node* candidate = find_node(key, update);

        // Check if key already exists
        if (candidate != nullptr && !_comp(key, candidate->key()) && !_comp(candidate->key(), key)) {
            return {iterator(candidate), false};
        }

        // Create new node
        uint8_t level = random_level();
        node* new_node = allocate_node(level);
        new (&new_node->data) storage_type(std::forward<K>(key), std::forward<V>(value));

        // Update head level if necessary
        if (level > _head.level) {
            for (uint8_t i = _head.level + 1; i <= level; ++i) {
                update[i] = nullptr;
            }
            _head.level = level;
        }

        // Insert node at all levels
        for (uint8_t i = 0; i <= level; ++i) {
            if (update[i] == nullptr) {
                new_node->set_forward(i, _head.forward[i]);
                _head.forward[i] = new_node;
            } else {
                new_node->set_forward(i, update[i]->forward(i));
                update[i]->set_forward(i, new_node);
            }
        }

        ++_size;
        return {iterator(new_node), true};
    }

    auto insert(const value_type& kv) -> std::pair<iterator, bool> {
        return insert(kv.first, kv.second);
    }

    auto insert(value_type&& kv) -> std::pair<iterator, bool> {
        return insert(std::move(const_cast<Key&>(kv.first)), std::move(kv.second));
    }

    // Insert with hint (hint is ignored for skiplist, provided for API compatibility)
    auto insert(const_iterator /*hint*/, const value_type& kv) -> iterator {
        return insert(kv).first;
    }

    auto insert(const_iterator /*hint*/, value_type&& kv) -> iterator {
        return insert(std::move(kv)).first;
    }

    // Range insert
    template <typename InputIt, typename = std::void_t<decltype(*std::declval<InputIt&>())>>
    void insert(InputIt first, InputIt last) {
        for (; first != last; ++first) {
            insert(*first);
        }
    }

    void insert(std::initializer_list<value_type> ilist) {
        insert(ilist.begin(), ilist.end());
    }

    // Emplace
    template <typename... Args>
    auto emplace(Args&&... args) -> std::pair<iterator, bool> {
        // Construct a temporary to extract key
        storage_type tmp(std::forward<Args>(args)...);
        return insert(std::move(tmp.first), std::move(tmp.second));
    }

    template <typename... Args>
    auto emplace_hint(const_iterator /*hint*/, Args&&... args) -> iterator {
        return emplace(std::forward<Args>(args)...).first;
    }

    // try_emplace (C++17)
    template <typename... Args>
    auto try_emplace(const Key& key, Args&&... args) -> std::pair<iterator, bool> {
        node* update[MaxLevel + 1];
        node* candidate = find_node(key, update);

        if (candidate != nullptr && !_comp(key, candidate->key()) && !_comp(candidate->key(), key)) {
            return {iterator(candidate), false};
        }

        uint8_t level = random_level();
        node* new_node = allocate_node(level);
        new (&new_node->data) storage_type(
            std::piecewise_construct,
            std::forward_as_tuple(key),
            std::forward_as_tuple(std::forward<Args>(args)...)
        );

        if (level > _head.level) {
            for (uint8_t i = _head.level + 1; i <= level; ++i) {
                update[i] = nullptr;
            }
            _head.level = level;
        }

        for (uint8_t i = 0; i <= level; ++i) {
            if (update[i] == nullptr) {
                new_node->set_forward(i, _head.forward[i]);
                _head.forward[i] = new_node;
            } else {
                new_node->set_forward(i, update[i]->forward(i));
                update[i]->set_forward(i, new_node);
            }
        }

        ++_size;
        return {iterator(new_node), true};
    }

    template <typename... Args>
    auto try_emplace(Key&& key, Args&&... args) -> std::pair<iterator, bool> {
        node* update[MaxLevel + 1];
        node* candidate = find_node(key, update);

        if (candidate != nullptr && !_comp(key, candidate->key()) && !_comp(candidate->key(), key)) {
            return {iterator(candidate), false};
        }

        uint8_t level = random_level();
        node* new_node = allocate_node(level);
        new (&new_node->data) storage_type(
            std::piecewise_construct,
            std::forward_as_tuple(std::move(key)),
            std::forward_as_tuple(std::forward<Args>(args)...)
        );

        if (level > _head.level) {
            for (uint8_t i = _head.level + 1; i <= level; ++i) {
                update[i] = nullptr;
            }
            _head.level = level;
        }

        for (uint8_t i = 0; i <= level; ++i) {
            if (update[i] == nullptr) {
                new_node->set_forward(i, _head.forward[i]);
                _head.forward[i] = new_node;
            } else {
                new_node->set_forward(i, update[i]->forward(i));
                update[i]->set_forward(i, new_node);
            }
        }

        ++_size;
        return {iterator(new_node), true};
    }

    template <typename... Args>
    auto try_emplace(const_iterator /*hint*/, const Key& key, Args&&... args) -> iterator {
        return try_emplace(key, std::forward<Args>(args)...).first;
    }

    template <typename... Args>
    auto try_emplace(const_iterator /*hint*/, Key&& key, Args&&... args) -> iterator {
        return try_emplace(std::move(key), std::forward<Args>(args)...).first;
    }

    // insert_or_assign (C++17)
    template <typename M>
    auto insert_or_assign(const Key& key, M&& value) -> std::pair<iterator, bool> {
        node* update[MaxLevel + 1];
        node* candidate = find_node(key, update);

        if (candidate != nullptr && !_comp(key, candidate->key()) && !_comp(candidate->key(), key)) {
            candidate->value() = std::forward<M>(value);
            return {iterator(candidate), false};
        }

        uint8_t level = random_level();
        node* new_node = allocate_node(level);
        new (&new_node->data) storage_type(key, std::forward<M>(value));

        if (level > _head.level) {
            for (uint8_t i = _head.level + 1; i <= level; ++i) {
                update[i] = nullptr;
            }
            _head.level = level;
        }

        for (uint8_t i = 0; i <= level; ++i) {
            if (update[i] == nullptr) {
                new_node->set_forward(i, _head.forward[i]);
                _head.forward[i] = new_node;
            } else {
                new_node->set_forward(i, update[i]->forward(i));
                update[i]->set_forward(i, new_node);
            }
        }

        ++_size;
        return {iterator(new_node), true};
    }

    template <typename M>
    auto insert_or_assign(Key&& key, M&& value) -> std::pair<iterator, bool> {
        node* update[MaxLevel + 1];
        node* candidate = find_node(key, update);

        if (candidate != nullptr && !_comp(key, candidate->key()) && !_comp(candidate->key(), key)) {
            candidate->value() = std::forward<M>(value);
            return {iterator(candidate), false};
        }

        uint8_t level = random_level();
        node* new_node = allocate_node(level);
        new (&new_node->data) storage_type(std::move(key), std::forward<M>(value));

        if (level > _head.level) {
            for (uint8_t i = _head.level + 1; i <= level; ++i) {
                update[i] = nullptr;
            }
            _head.level = level;
        }

        for (uint8_t i = 0; i <= level; ++i) {
            if (update[i] == nullptr) {
                new_node->set_forward(i, _head.forward[i]);
                _head.forward[i] = new_node;
            } else {
                new_node->set_forward(i, update[i]->forward(i));
                update[i]->set_forward(i, new_node);
            }
        }

        ++_size;
        return {iterator(new_node), true};
    }

    template <typename M>
    auto insert_or_assign(const_iterator /*hint*/, const Key& key, M&& value) -> iterator {
        return insert_or_assign(key, std::forward<M>(value)).first;
    }

    template <typename M>
    auto insert_or_assign(const_iterator /*hint*/, Key&& key, M&& value) -> iterator {
        return insert_or_assign(std::move(key), std::forward<M>(value)).first;
    }

    // Erase
    auto erase(const Key& key) -> size_type {
        node* update[MaxLevel + 1];
        node* candidate = find_node(key, update);

        if (candidate == nullptr || _comp(key, candidate->key()) || _comp(candidate->key(), key)) {
            return 0;
        }

        // Unlink from all levels
        for (uint8_t i = 0; i <= candidate->level; ++i) {
            if (update[i] == nullptr) {
                _head.forward[i] = candidate->forward(i);
            } else {
                update[i]->set_forward(i, candidate->forward(i));
            }
        }

        // Update head level if necessary
        while (_head.level > 0 && _head.forward[_head.level] == nullptr) {
            --_head.level;
        }

        deallocate_node(candidate);
        --_size;
        return 1;
    }

    // Erase with transparent comparator
    template <typename K>
    auto erase(const K& key) -> size_type
        requires is_transparent_comparator_v<Compare>
    {
        node* update[MaxLevel + 1];
        node* candidate = find_node(key, update);

        if (candidate == nullptr || _comp(key, candidate->key()) || _comp(candidate->key(), key)) {
            return 0;
        }

        for (uint8_t i = 0; i <= candidate->level; ++i) {
            if (update[i] == nullptr) {
                _head.forward[i] = candidate->forward(i);
            } else {
                update[i]->set_forward(i, candidate->forward(i));
            }
        }

        while (_head.level > 0 && _head.forward[_head.level] == nullptr) {
            --_head.level;
        }

        deallocate_node(candidate);
        --_size;
        return 1;
    }

    auto erase(iterator pos) -> iterator {
        if (pos._node == nullptr) return end();

        node* to_erase = pos._node;
        iterator next_it(to_erase->forward(0));

        // Find update pointers
        node* update[MaxLevel + 1];
        find_node(to_erase->key(), update);

        // Unlink
        for (uint8_t i = 0; i <= to_erase->level; ++i) {
            if (update[i] == nullptr) {
                _head.forward[i] = to_erase->forward(i);
            } else {
                update[i]->set_forward(i, to_erase->forward(i));
            }
        }

        while (_head.level > 0 && _head.forward[_head.level] == nullptr) {
            --_head.level;
        }

        deallocate_node(to_erase);
        --_size;
        return next_it;
    }

    auto erase(const_iterator pos) -> iterator {
        return erase(iterator(const_cast<node*>(pos._node)));
    }

    auto erase(const_iterator first, const_iterator last) -> iterator {
        while (first != last) {
            first = erase(first);
        }
        return iterator(const_cast<node*>(last._node));
    }

    // Extract (C++17)
    auto extract(const_iterator pos) -> node_type {
        if (pos._node == nullptr) return node_type{};

        node* to_extract = const_cast<node*>(pos._node);
        node_type result(std::move(to_extract->key()), std::move(to_extract->value()));

        // Find update pointers and remove
        node* update[MaxLevel + 1];
        find_node(to_extract->key(), update);

        for (uint8_t i = 0; i <= to_extract->level; ++i) {
            if (update[i] == nullptr) {
                _head.forward[i] = to_extract->forward(i);
            } else {
                update[i]->set_forward(i, to_extract->forward(i));
            }
        }

        while (_head.level > 0 && _head.forward[_head.level] == nullptr) {
            --_head.level;
        }

        deallocate_node(to_extract);
        --_size;
        return result;
    }

    auto extract(const Key& key) -> node_type {
        auto it = find(key);
        if (it == end()) return node_type{};
        return extract(it);
    }

    template <typename K>
    auto extract(const K& key) -> node_type
        requires is_transparent_comparator_v<Compare>
    {
        auto it = find(key);
        if (it == end()) return node_type{};
        return extract(it);
    }

    // Insert node handle
    auto insert(node_type&& nh) -> insert_return_type {
        if (nh.empty()) {
            return {end(), false, std::move(nh)};
        }

        auto [it, inserted] = insert(std::move(nh.key()), std::move(nh.mapped()));
        if (inserted) {
            nh._valid = false;
            return {it, true, node_type{}};
        }
        return {it, false, std::move(nh)};
    }

    auto insert(const_iterator /*hint*/, node_type&& nh) -> iterator {
        return insert(std::move(nh)).position;
    }

    // Swap
    void swap(skiplist_map& other) noexcept {
        std::swap(_head, other._head);
        std::swap(_size, other._size);
        std::swap(_comp, other._comp);
        std::swap(_alloc, other._alloc);
        std::swap(_rng_state, other._rng_state);
    }

    // Merge
    template <typename C2, typename A2, uint8_t M2, uint8_t P2>
    void merge(skiplist_map<Key, Value, C2, A2, M2, P2>& source) {
        auto it = source.begin();
        while (it != source.end()) {
            auto next = std::next(it);
            auto [pos, inserted] = insert(std::move(const_cast<Key&>(it->first)), std::move(it->second));
            if (inserted) {
                source.erase(it);
            }
            it = next;
        }
    }

    template <typename C2, typename A2, uint8_t M2, uint8_t P2>
    void merge(skiplist_map<Key, Value, C2, A2, M2, P2>&& source) {
        merge(source);
    }

    // Lookup
    [[nodiscard]] auto find(const Key& key) -> iterator {
        node* candidate = find_node(key);
        if (candidate != nullptr && !_comp(key, candidate->key()) && !_comp(candidate->key(), key)) {
            return iterator(candidate);
        }
        return end();
    }

    [[nodiscard]] auto find(const Key& key) const -> const_iterator {
        node* candidate = find_node(key);
        if (candidate != nullptr && !_comp(key, candidate->key()) && !_comp(candidate->key(), key)) {
            return const_iterator(candidate);
        }
        return end();
    }

    template <typename K>
    [[nodiscard]] auto find(const K& key) -> iterator
        requires is_transparent_comparator_v<Compare>
    {
        node* candidate = find_node(key);
        if (candidate != nullptr && !_comp(key, candidate->key()) && !_comp(candidate->key(), key)) {
            return iterator(candidate);
        }
        return end();
    }

    template <typename K>
    [[nodiscard]] auto find(const K& key) const -> const_iterator
        requires is_transparent_comparator_v<Compare>
    {
        node* candidate = find_node(key);
        if (candidate != nullptr && !_comp(key, candidate->key()) && !_comp(candidate->key(), key)) {
            return const_iterator(candidate);
        }
        return end();
    }

    [[nodiscard]] auto count(const Key& key) const -> size_type {
        return find(key) != end() ? 1 : 0;
    }

    template <typename K>
    [[nodiscard]] auto count(const K& key) const -> size_type
        requires is_transparent_comparator_v<Compare>
    {
        return find(key) != end() ? 1 : 0;
    }

    [[nodiscard]] auto contains(const Key& key) const -> bool {
        return find(key) != end();
    }

    template <typename K>
    [[nodiscard]] auto contains(const K& key) const -> bool
        requires is_transparent_comparator_v<Compare>
    {
        return find(key) != end();
    }

    // Element access
    [[nodiscard]] auto operator[](const Key& key) -> Value& {
        auto [it, _] = try_emplace(key);
        return it->second;
    }

    [[nodiscard]] auto operator[](Key&& key) -> Value& {
        auto [it, _] = try_emplace(std::move(key));
        return it->second;
    }

    [[nodiscard]] auto at(const Key& key) -> Value& {
        auto it = find(key);
        if (it == end()) {
            throw std::out_of_range("skiplist_map::at: key not found");
        }
        return it->second;
    }

    [[nodiscard]] auto at(const Key& key) const -> const Value& {
        auto it = find(key);
        if (it == end()) {
            throw std::out_of_range("skiplist_map::at: key not found");
        }
        return it->second;
    }

    // Bounds
    [[nodiscard]] auto lower_bound(const Key& key) -> iterator {
        node* candidate = find_node(key);
        return iterator(candidate);
    }

    [[nodiscard]] auto lower_bound(const Key& key) const -> const_iterator {
        node* candidate = find_node(key);
        return const_iterator(candidate);
    }

    template <typename K>
    [[nodiscard]] auto lower_bound(const K& key) -> iterator
        requires is_transparent_comparator_v<Compare>
    {
        node* candidate = find_node(key);
        return iterator(candidate);
    }

    template <typename K>
    [[nodiscard]] auto lower_bound(const K& key) const -> const_iterator
        requires is_transparent_comparator_v<Compare>
    {
        node* candidate = find_node(key);
        return const_iterator(candidate);
    }

    [[nodiscard]] auto upper_bound(const Key& key) -> iterator {
        node* candidate = find_node(key);
        // If candidate exists and equals key, move to next
        if (candidate != nullptr && !_comp(key, candidate->key()) && !_comp(candidate->key(), key)) {
            return iterator(candidate->forward(0));
        }
        return iterator(candidate);
    }

    [[nodiscard]] auto upper_bound(const Key& key) const -> const_iterator {
        node* candidate = find_node(key);
        if (candidate != nullptr && !_comp(key, candidate->key()) && !_comp(candidate->key(), key)) {
            return const_iterator(candidate->forward(0));
        }
        return const_iterator(candidate);
    }

    template <typename K>
    [[nodiscard]] auto upper_bound(const K& key) -> iterator
        requires is_transparent_comparator_v<Compare>
    {
        node* candidate = find_node(key);
        if (candidate != nullptr && !_comp(key, candidate->key()) && !_comp(candidate->key(), key)) {
            return iterator(candidate->forward(0));
        }
        return iterator(candidate);
    }

    template <typename K>
    [[nodiscard]] auto upper_bound(const K& key) const -> const_iterator
        requires is_transparent_comparator_v<Compare>
    {
        node* candidate = find_node(key);
        if (candidate != nullptr && !_comp(key, candidate->key()) && !_comp(candidate->key(), key)) {
            return const_iterator(candidate->forward(0));
        }
        return const_iterator(candidate);
    }

    [[nodiscard]] auto equal_range(const Key& key) -> std::pair<iterator, iterator> {
        return {lower_bound(key), upper_bound(key)};
    }

    [[nodiscard]] auto equal_range(const Key& key) const -> std::pair<const_iterator, const_iterator> {
        return {lower_bound(key), upper_bound(key)};
    }

    template <typename K>
    [[nodiscard]] auto equal_range(const K& key) -> std::pair<iterator, iterator>
        requires is_transparent_comparator_v<Compare>
    {
        return {lower_bound(key), upper_bound(key)};
    }

    template <typename K>
    [[nodiscard]] auto equal_range(const K& key) const -> std::pair<const_iterator, const_iterator>
        requires is_transparent_comparator_v<Compare>
    {
        return {lower_bound(key), upper_bound(key)};
    }

    // Comparators
    [[nodiscard]] auto key_comp() const -> key_compare { return _comp; }
    [[nodiscard]] auto value_comp() const -> value_compare { return value_compare(_comp); }

    // Comparison operators
    friend bool operator==(const skiplist_map& lhs, const skiplist_map& rhs) {
        if (lhs.size() != rhs.size()) return false;
        return std::equal(lhs.begin(), lhs.end(), rhs.begin());
    }

    friend bool operator!=(const skiplist_map& lhs, const skiplist_map& rhs) {
        return !(lhs == rhs);
    }

    friend bool operator<(const skiplist_map& lhs, const skiplist_map& rhs) {
        return std::lexicographical_compare(lhs.begin(), lhs.end(), rhs.begin(), rhs.end());
    }

    friend bool operator<=(const skiplist_map& lhs, const skiplist_map& rhs) {
        return !(rhs < lhs);
    }

    friend bool operator>(const skiplist_map& lhs, const skiplist_map& rhs) {
        return rhs < lhs;
    }

    friend bool operator>=(const skiplist_map& lhs, const skiplist_map& rhs) {
        return !(lhs < rhs);
    }

#if __cplusplus >= 202002L || (defined(_MSVC_LANG) && _MSVC_LANG >= 202002L)
    friend auto operator<=>(const skiplist_map& lhs, const skiplist_map& rhs) {
        return std::lexicographical_compare_three_way(lhs.begin(), lhs.end(), rhs.begin(), rhs.end());
    }
#endif

    // Debug info
    [[nodiscard]] static constexpr uint8_t max_level() noexcept { return MaxLevel; }
    [[nodiscard]] uint8_t current_level() const noexcept { return _head.level; }
};

// Non-member functions
template <typename Key, typename Value, typename Compare, typename Allocator, uint8_t M, uint8_t P>
void swap(skiplist_map<Key, Value, Compare, Allocator, M, P>& lhs,
          skiplist_map<Key, Value, Compare, Allocator, M, P>& rhs) noexcept {
    lhs.swap(rhs);
}

// erase_if (C++20)
template <typename Key, typename Value, typename Compare, typename Allocator, uint8_t M, uint8_t P, typename Pred>
typename skiplist_map<Key, Value, Compare, Allocator, M, P>::size_type
erase_if(skiplist_map<Key, Value, Compare, Allocator, M, P>& c, Pred pred) {
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

// Type aliases
template <typename Key, typename Value, typename Compare = std::less<Key>>
using skiplist_map_default = skiplist_map<Key, Value, Compare>;

}  // namespace stdb::container

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

#include "skiplist_map.hpp"

namespace stdb::container {

// Empty value type for set implementation (zero-cost abstraction)
struct skiplist_set_empty_value
{
    constexpr bool operator==(const skiplist_set_empty_value&) const noexcept { return true; }
    constexpr bool operator!=(const skiplist_set_empty_value&) const noexcept { return false; }
#if __cplusplus >= 202002L || (defined(_MSVC_LANG) && _MSVC_LANG >= 202002L)
    constexpr auto operator<=>(const skiplist_set_empty_value&) const noexcept { return std::strong_ordering::equal; }
#endif
};

/*
 * skiplist_set is a skip list based ordered set container.
 *
 * This is a thin wrapper around skiplist_map<Key, empty_value>.
 *
 * Key features:
 *   - O(log N) average lookup, insert, and erase operations
 *   - Iterator stability: iterators remain valid after insert
 *   - Pointer stability: pointers/references to elements remain valid
 *
 * Trade-offs vs std::set:
 *   - Forward iterator only (no reverse iteration)
 *   - Probabilistic balance vs strict balance
 */

template <typename Key, typename Compare = std::less<Key>,
          typename Allocator = std::allocator<Key>,
          uint8_t MaxLevel = 32, uint8_t Probability = 4>
class skiplist_set
{
   private:
    // Rebind allocator for internal map storage
    using internal_allocator = typename std::allocator_traits<Allocator>::template rebind_alloc<
        std::pair<const Key, skiplist_set_empty_value>>;
    using map_type = skiplist_map<Key, skiplist_set_empty_value, Compare, internal_allocator, MaxLevel, Probability>;

    map_type _map;

   public:
    using key_type = Key;
    using value_type = Key;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    using key_compare = Compare;
    using value_compare = Compare;
    using allocator_type = Allocator;
    using reference = const value_type&;
    using const_reference = const value_type&;
    using pointer = const value_type*;
    using const_pointer = const value_type*;

    // Node handle for extract/insert operations (C++17)
    class node_type
    {
        friend class skiplist_set;

       public:
        using value_type = Key;

        node_type() noexcept = default;
        node_type(node_type&& other) noexcept = default;
        node_type& operator=(node_type&& other) noexcept = default;
        ~node_type() = default;

        [[nodiscard]] bool empty() const noexcept { return _map_node.empty(); }
        explicit operator bool() const noexcept { return !empty(); }

        [[nodiscard]] value_type& value() { return _map_node.key(); }

        void swap(node_type& other) noexcept { _map_node.swap(other._map_node); }

       private:
        explicit node_type(typename map_type::node_type&& mn) : _map_node(std::move(mn)) {}
        typename map_type::node_type _map_node;
    };

    // Forward declaration for insert_return_type
    class iterator;

    // Insert return type for node handle insert
    struct insert_return_type
    {
        iterator position;
        bool inserted;
        node_type node;
    };

    // Iterator wrapper - returns key instead of pair
    class iterator
    {
        friend class skiplist_set;

       public:
        using iterator_category = std::forward_iterator_tag;
        using difference_type = std::ptrdiff_t;
        using value_type = const Key;
        using pointer = const Key*;
        using reference = const Key&;

        iterator() noexcept = default;

        reference operator*() const noexcept { return _it->first; }
        pointer operator->() const noexcept { return &_it->first; }

        iterator& operator++() noexcept {
            ++_it;
            return *this;
        }

        iterator operator++(int) noexcept {
            iterator tmp = *this;
            ++_it;
            return tmp;
        }

        bool operator==(const iterator& other) const noexcept { return _it == other._it; }
        bool operator!=(const iterator& other) const noexcept { return _it != other._it; }

       private:
        explicit iterator(typename map_type::iterator it) noexcept : _it(it) {}
        typename map_type::iterator _it;
    };

    // Const iterator wrapper
    class const_iterator
    {
        friend class skiplist_set;

       public:
        using iterator_category = std::forward_iterator_tag;
        using difference_type = std::ptrdiff_t;
        using value_type = const Key;
        using pointer = const Key*;
        using reference = const Key&;

        const_iterator() noexcept = default;
        const_iterator(const iterator& it) noexcept : _it(it._it) {}

        reference operator*() const noexcept { return _it->first; }
        pointer operator->() const noexcept { return &_it->first; }

        const_iterator& operator++() noexcept {
            ++_it;
            return *this;
        }

        const_iterator operator++(int) noexcept {
            const_iterator tmp = *this;
            ++_it;
            return tmp;
        }

        bool operator==(const const_iterator& other) const noexcept { return _it == other._it; }
        bool operator!=(const const_iterator& other) const noexcept { return _it != other._it; }

       private:
        explicit const_iterator(typename map_type::const_iterator it) noexcept : _it(it) {}
        typename map_type::const_iterator _it;
    };

    // Constructors
    skiplist_set() = default;

    explicit skiplist_set(const Compare& comp, const Allocator& alloc = Allocator())
        : _map(comp, internal_allocator(alloc)) {}

    explicit skiplist_set(const Allocator& alloc) : _map(internal_allocator(alloc)) {}

    template <typename InputIt>
    skiplist_set(InputIt first, InputIt last, const Compare& comp = Compare(), const Allocator& alloc = Allocator())
        : _map(comp, internal_allocator(alloc)) {
        insert(first, last);
    }

    skiplist_set(std::initializer_list<Key> init, const Compare& comp = Compare(),
                 const Allocator& alloc = Allocator())
        : _map(comp, internal_allocator(alloc)) {
        insert(init);
    }

    skiplist_set(const skiplist_set&) = default;
    skiplist_set(skiplist_set&&) noexcept = default;

    skiplist_set(const skiplist_set& other, const Allocator& alloc)
        : _map(other._map, internal_allocator(alloc)) {}

    skiplist_set(skiplist_set&& other, const Allocator& alloc)
        : _map(std::move(other._map), internal_allocator(alloc)) {}

    ~skiplist_set() = default;

    skiplist_set& operator=(const skiplist_set&) = default;
    skiplist_set& operator=(skiplist_set&&) noexcept = default;

    skiplist_set& operator=(std::initializer_list<Key> init) {
        clear();
        insert(init);
        return *this;
    }

    // Allocator
    [[nodiscard]] allocator_type get_allocator() const noexcept {
        return allocator_type(_map.get_allocator());
    }

    // Capacity
    [[nodiscard]] bool empty() const noexcept { return _map.empty(); }
    [[nodiscard]] size_type size() const noexcept { return _map.size(); }
    [[nodiscard]] static constexpr size_type max_size() noexcept { return map_type::max_size(); }

    // Clear
    void clear() noexcept { _map.clear(); }

    // Iterators
    [[nodiscard]] iterator begin() noexcept { return iterator(_map.begin()); }
    [[nodiscard]] const_iterator begin() const noexcept { return const_iterator(_map.begin()); }
    [[nodiscard]] const_iterator cbegin() const noexcept { return const_iterator(_map.cbegin()); }
    [[nodiscard]] iterator end() noexcept { return iterator(_map.end()); }
    [[nodiscard]] const_iterator end() const noexcept { return const_iterator(_map.end()); }
    [[nodiscard]] const_iterator cend() const noexcept { return const_iterator(_map.cend()); }

    // Insert
    auto insert(const Key& key) -> std::pair<iterator, bool> {
        auto [it, inserted] = _map.insert(key, skiplist_set_empty_value{});
        return {iterator(it), inserted};
    }

    auto insert(Key&& key) -> std::pair<iterator, bool> {
        auto [it, inserted] = _map.insert(std::move(key), skiplist_set_empty_value{});
        return {iterator(it), inserted};
    }

    auto insert(const_iterator /*hint*/, const Key& key) -> iterator {
        return insert(key).first;
    }

    auto insert(const_iterator /*hint*/, Key&& key) -> iterator {
        return insert(std::move(key)).first;
    }

    template <typename InputIt>
    void insert(InputIt first, InputIt last) {
        for (; first != last; ++first) {
            insert(*first);
        }
    }

    void insert(std::initializer_list<Key> ilist) {
        insert(ilist.begin(), ilist.end());
    }

    // Emplace
    template <typename... Args>
    auto emplace(Args&&... args) -> std::pair<iterator, bool> {
        Key key(std::forward<Args>(args)...);
        return insert(std::move(key));
    }

    template <typename... Args>
    auto emplace_hint(const_iterator /*hint*/, Args&&... args) -> iterator {
        return emplace(std::forward<Args>(args)...).first;
    }

    // Node handle insert
    auto insert(node_type&& nh) -> insert_return_type {
        if (nh.empty()) {
            return {end(), false, std::move(nh)};
        }
        auto result = _map.insert(std::move(nh._map_node));
        return {iterator(result.position), result.inserted,
                result.node.empty() ? node_type{} : node_type(std::move(result.node))};
    }

    auto insert(const_iterator /*hint*/, node_type&& nh) -> iterator {
        return insert(std::move(nh)).position;
    }

    // Erase
    auto erase(const Key& key) -> size_type {
        return _map.erase(key);
    }

    template <typename K>
    auto erase(const K& key) -> size_type
        requires is_transparent_comparator_v<Compare>
    {
        return _map.erase(key);
    }

    auto erase(iterator pos) -> iterator {
        return iterator(_map.erase(pos._it));
    }

    auto erase(const_iterator pos) -> iterator {
        return iterator(_map.erase(pos._it));
    }

    auto erase(const_iterator first, const_iterator last) -> iterator {
        return iterator(_map.erase(first._it, last._it));
    }

    // Extract
    auto extract(const_iterator pos) -> node_type {
        return node_type(_map.extract(pos._it));
    }

    auto extract(const Key& key) -> node_type {
        return node_type(_map.extract(key));
    }

    template <typename K>
    auto extract(const K& key) -> node_type
        requires is_transparent_comparator_v<Compare>
    {
        return node_type(_map.extract(key));
    }

    // Swap
    void swap(skiplist_set& other) noexcept {
        _map.swap(other._map);
    }

    // Merge
    template <typename C2, typename A2, uint8_t M2, uint8_t P2>
    void merge(skiplist_set<Key, C2, A2, M2, P2>& source) {
        auto it = source.begin();
        while (it != source.end()) {
            auto next = std::next(it);
            auto [pos, inserted] = insert(*it);
            if (inserted) {
                source.erase(it);
            }
            it = next;
        }
    }

    template <typename C2, typename A2, uint8_t M2, uint8_t P2>
    void merge(skiplist_set<Key, C2, A2, M2, P2>&& source) {
        merge(source);
    }

    // Lookup
    [[nodiscard]] auto find(const Key& key) -> iterator {
        return iterator(_map.find(key));
    }

    [[nodiscard]] auto find(const Key& key) const -> const_iterator {
        return const_iterator(_map.find(key));
    }

    template <typename K>
    [[nodiscard]] auto find(const K& key) -> iterator
        requires is_transparent_comparator_v<Compare>
    {
        return iterator(_map.find(key));
    }

    template <typename K>
    [[nodiscard]] auto find(const K& key) const -> const_iterator
        requires is_transparent_comparator_v<Compare>
    {
        return const_iterator(_map.find(key));
    }

    [[nodiscard]] auto count(const Key& key) const -> size_type {
        return _map.count(key);
    }

    template <typename K>
    [[nodiscard]] auto count(const K& key) const -> size_type
        requires is_transparent_comparator_v<Compare>
    {
        return _map.count(key);
    }

    [[nodiscard]] auto contains(const Key& key) const -> bool {
        return _map.contains(key);
    }

    template <typename K>
    [[nodiscard]] auto contains(const K& key) const -> bool
        requires is_transparent_comparator_v<Compare>
    {
        return _map.contains(key);
    }

    // Bounds
    [[nodiscard]] auto lower_bound(const Key& key) -> iterator {
        return iterator(_map.lower_bound(key));
    }

    [[nodiscard]] auto lower_bound(const Key& key) const -> const_iterator {
        return const_iterator(_map.lower_bound(key));
    }

    template <typename K>
    [[nodiscard]] auto lower_bound(const K& key) -> iterator
        requires is_transparent_comparator_v<Compare>
    {
        return iterator(_map.lower_bound(key));
    }

    template <typename K>
    [[nodiscard]] auto lower_bound(const K& key) const -> const_iterator
        requires is_transparent_comparator_v<Compare>
    {
        return const_iterator(_map.lower_bound(key));
    }

    [[nodiscard]] auto upper_bound(const Key& key) -> iterator {
        return iterator(_map.upper_bound(key));
    }

    [[nodiscard]] auto upper_bound(const Key& key) const -> const_iterator {
        return const_iterator(_map.upper_bound(key));
    }

    template <typename K>
    [[nodiscard]] auto upper_bound(const K& key) -> iterator
        requires is_transparent_comparator_v<Compare>
    {
        return iterator(_map.upper_bound(key));
    }

    template <typename K>
    [[nodiscard]] auto upper_bound(const K& key) const -> const_iterator
        requires is_transparent_comparator_v<Compare>
    {
        return const_iterator(_map.upper_bound(key));
    }

    [[nodiscard]] auto equal_range(const Key& key) -> std::pair<iterator, iterator> {
        auto [first, second] = _map.equal_range(key);
        return {iterator(first), iterator(second)};
    }

    [[nodiscard]] auto equal_range(const Key& key) const -> std::pair<const_iterator, const_iterator> {
        auto [first, second] = _map.equal_range(key);
        return {const_iterator(first), const_iterator(second)};
    }

    template <typename K>
    [[nodiscard]] auto equal_range(const K& key) -> std::pair<iterator, iterator>
        requires is_transparent_comparator_v<Compare>
    {
        auto [first, second] = _map.equal_range(key);
        return {iterator(first), iterator(second)};
    }

    template <typename K>
    [[nodiscard]] auto equal_range(const K& key) const -> std::pair<const_iterator, const_iterator>
        requires is_transparent_comparator_v<Compare>
    {
        auto [first, second] = _map.equal_range(key);
        return {const_iterator(first), const_iterator(second)};
    }

    // Comparators
    [[nodiscard]] auto key_comp() const -> key_compare { return _map.key_comp(); }
    [[nodiscard]] auto value_comp() const -> value_compare { return _map.key_comp(); }

    // Comparison operators
    friend bool operator==(const skiplist_set& lhs, const skiplist_set& rhs) {
        if (lhs.size() != rhs.size()) return false;
        return std::equal(lhs.begin(), lhs.end(), rhs.begin());
    }

    friend bool operator!=(const skiplist_set& lhs, const skiplist_set& rhs) {
        return !(lhs == rhs);
    }

    friend bool operator<(const skiplist_set& lhs, const skiplist_set& rhs) {
        return std::lexicographical_compare(lhs.begin(), lhs.end(), rhs.begin(), rhs.end());
    }

    friend bool operator<=(const skiplist_set& lhs, const skiplist_set& rhs) {
        return !(rhs < lhs);
    }

    friend bool operator>(const skiplist_set& lhs, const skiplist_set& rhs) {
        return rhs < lhs;
    }

    friend bool operator>=(const skiplist_set& lhs, const skiplist_set& rhs) {
        return !(lhs < rhs);
    }

#if __cplusplus >= 202002L || (defined(_MSVC_LANG) && _MSVC_LANG >= 202002L)
    friend auto operator<=>(const skiplist_set& lhs, const skiplist_set& rhs) {
        return std::lexicographical_compare_three_way(lhs.begin(), lhs.end(), rhs.begin(), rhs.end());
    }
#endif

    // Debug info
    [[nodiscard]] static constexpr uint8_t max_level() noexcept { return MaxLevel; }
    [[nodiscard]] uint8_t current_level() const noexcept { return _map.current_level(); }
};

// Non-member functions
template <typename Key, typename Compare, typename Allocator, uint8_t M, uint8_t P>
void swap(skiplist_set<Key, Compare, Allocator, M, P>& lhs,
          skiplist_set<Key, Compare, Allocator, M, P>& rhs) noexcept {
    lhs.swap(rhs);
}

// erase_if (C++20)
template <typename Key, typename Compare, typename Allocator, uint8_t M, uint8_t P, typename Pred>
typename skiplist_set<Key, Compare, Allocator, M, P>::size_type
erase_if(skiplist_set<Key, Compare, Allocator, M, P>& c, Pred pred) {
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

// Type alias
template <typename Key, typename Compare = std::less<Key>>
using skiplist_set_default = skiplist_set<Key, Compare>;

}  // namespace stdb::container

// ============================================================================
// PMR (Polymorphic Memory Resource) type aliases
// ============================================================================
namespace stdb::pmr {

template <typename Key, typename Compare = std::less<Key>,
          uint8_t MaxLevel = 32, uint8_t Probability = 4>
using skiplist_set = container::skiplist_set<
    Key, Compare,
    std::pmr::polymorphic_allocator<Key>,
    MaxLevel, Probability>;

}  // namespace stdb::pmr

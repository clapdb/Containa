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
#include "btree_map.hpp"

#if __cplusplus >= 202302L || (defined(_MSVC_LANG) && _MSVC_LANG >= 202302L)
#include <ranges>
#endif

namespace stdb::container {

/*
 * btree_set is a B-tree based ordered set container.
 *
 * This is a thin wrapper around btree_map<Key, empty_value, Compare> that provides
 * a std::set-compatible interface.
 *
 * Key features:
 *   - Cache-friendly: Multiple keys per node (unlike std::set's 1 key/node)
 *   - Memory efficient: ~5 bytes per element vs ~40 bytes for std::set
 *   - Fast lookup: O(log N) with better constants due to cache efficiency
 *   - Ordered: Supports in-order iteration
 *
 * Trade-offs vs std::set:
 *   - No iterator stability (iterators invalidated on insert/erase)
 *   - No pointer stability
 *   - Better for small-to-medium sized keys
 *
 * Template parameters:
 *   Key - Key type (must be comparable)
 *   Compare - Comparison function (default: std::less<Key>)
 *   Allocator - Allocator type (default: std::allocator<Key>)
 *   TargetNodeSize - Target node size in bytes (default: auto-calculated)
 */

// Empty value type for set implementation
struct btree_set_empty_value
{
    constexpr bool operator==(const btree_set_empty_value&) const noexcept { return true; }
    constexpr bool operator!=(const btree_set_empty_value&) const noexcept { return false; }
    constexpr auto operator<=>(const btree_set_empty_value&) const noexcept { return std::strong_ordering::equal; }
};

// Helper to calculate optimal node size for set (key-only)
template <typename Key>
constexpr std::size_t optimal_set_node_size() {
    constexpr std::size_t header_size = 24;
    constexpr std::size_t target_slots = 15;
    // For set, we store pair<Key, empty> which should optimize to just Key
    constexpr std::size_t slot_size = sizeof(std::pair<Key, btree_set_empty_value>);
    constexpr std::size_t min_size = header_size + slot_size * target_slots;
    if (min_size <= 256) return 256;
    if (min_size <= 512) return 512;
    if (min_size <= 1024) return 1024;
    if (min_size <= 2048) return 2048;
    return 4096;
}

template <typename Key, typename Compare = std::less<Key>, typename Allocator = std::allocator<Key>,
          std::size_t TargetNodeSize = optimal_set_node_size<Key>()>
class btree_set
{
   private:
    // Internal map type using empty value
    using map_allocator_type =
      typename std::allocator_traits<Allocator>::template rebind_alloc<std::pair<const Key, btree_set_empty_value>>;
    using map_type = btree_map<Key, btree_set_empty_value, Compare, map_allocator_type, TargetNodeSize>;

    map_type _map;

   public:
    using key_type = Key;
    using value_type = Key;
    using size_type = typename map_type::size_type;
    using difference_type = typename map_type::difference_type;
    using key_compare = Compare;
    using value_compare = Compare;
    using allocator_type = Allocator;
    using reference = const Key&;
    using const_reference = const Key&;
    using pointer = const Key*;
    using const_pointer = const Key*;

    // Iterator wrapper that returns Key instead of pair<const Key, empty>
    class iterator
    {
       public:
        using iterator_category = std::bidirectional_iterator_tag;
        using difference_type = std::ptrdiff_t;
        using value_type = const Key;
        using pointer = const Key*;
        using reference = const Key&;

       private:
        typename map_type::iterator _it;
        friend class btree_set;
        explicit iterator(typename map_type::iterator it) : _it(it) {}

       public:
        iterator() = default;

        [[nodiscard]] auto operator*() const -> reference { return _it->first; }
        [[nodiscard]] auto operator->() const -> pointer { return &_it->first; }

        auto operator++() -> iterator& {
            ++_it;
            return *this;
        }
        auto operator++(int) -> iterator {
            auto tmp = *this;
            ++_it;
            return tmp;
        }
        auto operator--() -> iterator& {
            --_it;
            return *this;
        }
        auto operator--(int) -> iterator {
            auto tmp = *this;
            --_it;
            return tmp;
        }

        friend auto operator==(const iterator& a, const iterator& b) -> bool { return a._it == b._it; }
        friend auto operator!=(const iterator& a, const iterator& b) -> bool { return a._it != b._it; }
    };

    class const_iterator
    {
       public:
        using iterator_category = std::bidirectional_iterator_tag;
        using difference_type = std::ptrdiff_t;
        using value_type = const Key;
        using pointer = const Key*;
        using reference = const Key&;

       private:
        typename map_type::const_iterator _it;
        friend class btree_set;
        explicit const_iterator(typename map_type::const_iterator it) : _it(it) {}

       public:
        const_iterator() = default;
        const_iterator(const iterator& it) : _it(it._it) {}

        [[nodiscard]] auto operator*() const -> reference { return _it->first; }
        [[nodiscard]] auto operator->() const -> pointer { return &_it->first; }

        auto operator++() -> const_iterator& {
            ++_it;
            return *this;
        }
        auto operator++(int) -> const_iterator {
            auto tmp = *this;
            ++_it;
            return tmp;
        }
        auto operator--() -> const_iterator& {
            --_it;
            return *this;
        }
        auto operator--(int) -> const_iterator {
            auto tmp = *this;
            --_it;
            return tmp;
        }

        friend auto operator==(const const_iterator& a, const const_iterator& b) -> bool { return a._it == b._it; }
        friend auto operator!=(const const_iterator& a, const const_iterator& b) -> bool { return a._it != b._it; }
    };

    class reverse_iterator
    {
       public:
        using iterator_category = std::bidirectional_iterator_tag;
        using difference_type = std::ptrdiff_t;
        using value_type = const Key;
        using pointer = const Key*;
        using reference = const Key&;

       private:
        typename map_type::reverse_iterator _it;
        friend class btree_set;
        explicit reverse_iterator(typename map_type::reverse_iterator it) : _it(it) {}

       public:
        reverse_iterator() = default;

        [[nodiscard]] auto operator*() const -> reference { return _it->first; }
        [[nodiscard]] auto operator->() const -> pointer { return &_it->first; }

        auto operator++() -> reverse_iterator& {
            ++_it;
            return *this;
        }
        auto operator++(int) -> reverse_iterator {
            auto tmp = *this;
            ++_it;
            return tmp;
        }
        auto operator--() -> reverse_iterator& {
            --_it;
            return *this;
        }
        auto operator--(int) -> reverse_iterator {
            auto tmp = *this;
            --_it;
            return tmp;
        }

        [[nodiscard]] auto base() const -> iterator { return iterator(_it.base()); }

        friend auto operator==(const reverse_iterator& a, const reverse_iterator& b) -> bool { return a._it == b._it; }
        friend auto operator!=(const reverse_iterator& a, const reverse_iterator& b) -> bool { return a._it != b._it; }
    };

    class const_reverse_iterator
    {
       public:
        using iterator_category = std::bidirectional_iterator_tag;
        using difference_type = std::ptrdiff_t;
        using value_type = const Key;
        using pointer = const Key*;
        using reference = const Key&;

       private:
        typename map_type::const_reverse_iterator _it;
        friend class btree_set;
        explicit const_reverse_iterator(typename map_type::const_reverse_iterator it) : _it(it) {}

       public:
        const_reverse_iterator() = default;
        const_reverse_iterator(const reverse_iterator& it) : _it(it._it) {}

        [[nodiscard]] auto operator*() const -> reference { return _it->first; }
        [[nodiscard]] auto operator->() const -> pointer { return &_it->first; }

        auto operator++() -> const_reverse_iterator& {
            ++_it;
            return *this;
        }
        auto operator++(int) -> const_reverse_iterator {
            auto tmp = *this;
            ++_it;
            return tmp;
        }
        auto operator--() -> const_reverse_iterator& {
            --_it;
            return *this;
        }
        auto operator--(int) -> const_reverse_iterator {
            auto tmp = *this;
            --_it;
            return tmp;
        }

        [[nodiscard]] auto base() const -> const_iterator { return const_iterator(_it.base()); }

        friend auto operator==(const const_reverse_iterator& a, const const_reverse_iterator& b) -> bool {
            return a._it == b._it;
        }
        friend auto operator!=(const const_reverse_iterator& a, const const_reverse_iterator& b) -> bool {
            return a._it != b._it;
        }
    };

    // Node handle for extract/insert operations (C++17)
    class node_type
    {
        friend class btree_set;

       public:
        using key_type = Key;
        using value_type = Key;

        node_type() noexcept = default;
        node_type(node_type&&) noexcept = default;
        node_type& operator=(node_type&&) noexcept = default;
        ~node_type() = default;

        [[nodiscard]] bool empty() const noexcept { return !_valid; }
        explicit operator bool() const noexcept { return _valid; }

        [[nodiscard]] key_type& value() { return _key; }

        void swap(node_type& other) noexcept {
            std::swap(_key, other._key);
            std::swap(_valid, other._valid);
        }

       private:
        explicit node_type(Key&& k) : _key(std::move(k)), _valid(true) {}
        explicit node_type(const Key& k) : _key(k), _valid(true) {}

        Key _key;
        bool _valid = false;
    };

    // Insert return type for node handle insertion (C++17)
    struct insert_return_type
    {
        iterator position;
        bool inserted;
        node_type node;
    };

    // Constructors
    btree_set() = default;

    explicit btree_set(const Compare& comp, const Allocator& alloc = Allocator())
        : _map(comp, map_allocator_type(alloc)) {}

    explicit btree_set(const Allocator& alloc) : _map(map_allocator_type(alloc)) {}

    btree_set(std::initializer_list<Key> init, const Compare& comp = Compare(), const Allocator& alloc = Allocator())
        : _map(comp, map_allocator_type(alloc)) {
        for (const auto& k : init) {
            insert(k);
        }
    }

    btree_set(std::initializer_list<Key> init, const Allocator& alloc) : _map(map_allocator_type(alloc)) {
        for (const auto& k : init) {
            insert(k);
        }
    }

    // Range constructor
    template <typename InputIt>
    requires requires(InputIt it) {
        { *it } -> std::convertible_to<Key>;
        ++it;
    }
    btree_set(InputIt first, InputIt last, const Compare& comp = Compare(), const Allocator& alloc = Allocator())
        : _map(comp, map_allocator_type(alloc)) {
        for (; first != last; ++first) {
            insert(*first);
        }
    }

    template <typename InputIt>
    requires requires(InputIt it) {
        { *it } -> std::convertible_to<Key>;
        ++it;
    }
    btree_set(InputIt first, InputIt last, const Allocator& alloc) : _map(map_allocator_type(alloc)) {
        for (; first != last; ++first) {
            insert(*first);
        }
    }

    // Copy/Move constructors and assignment operators
    btree_set(const btree_set&) = default;
    btree_set(btree_set&&) noexcept = default;
    btree_set& operator=(const btree_set&) = default;
    btree_set& operator=(btree_set&&) noexcept = default;
    ~btree_set() = default;

    // Assignment from initializer_list
    auto operator=(std::initializer_list<Key> ilist) -> btree_set& {
        clear();
        for (const auto& k : ilist) {
            insert(k);
        }
        return *this;
    }

    // Allocator
    [[nodiscard]] auto get_allocator() const noexcept -> allocator_type { return allocator_type(_map.get_allocator()); }

    // Iterators
    [[nodiscard]] auto begin() noexcept -> iterator { return iterator(_map.begin()); }
    [[nodiscard]] auto begin() const noexcept -> const_iterator { return const_iterator(_map.begin()); }
    [[nodiscard]] auto cbegin() const noexcept -> const_iterator { return const_iterator(_map.cbegin()); }

    [[nodiscard]] auto end() noexcept -> iterator { return iterator(_map.end()); }
    [[nodiscard]] auto end() const noexcept -> const_iterator { return const_iterator(_map.end()); }
    [[nodiscard]] auto cend() const noexcept -> const_iterator { return const_iterator(_map.cend()); }

    [[nodiscard]] auto rbegin() noexcept -> reverse_iterator { return reverse_iterator(_map.rbegin()); }
    [[nodiscard]] auto rbegin() const noexcept -> const_reverse_iterator {
        return const_reverse_iterator(_map.rbegin());
    }
    [[nodiscard]] auto crbegin() const noexcept -> const_reverse_iterator {
        return const_reverse_iterator(_map.crbegin());
    }

    [[nodiscard]] auto rend() noexcept -> reverse_iterator { return reverse_iterator(_map.rend()); }
    [[nodiscard]] auto rend() const noexcept -> const_reverse_iterator { return const_reverse_iterator(_map.rend()); }
    [[nodiscard]] auto crend() const noexcept -> const_reverse_iterator { return const_reverse_iterator(_map.crend()); }

    // Capacity
    [[nodiscard]] auto empty() const noexcept -> bool { return _map.empty(); }
    [[nodiscard]] auto size() const noexcept -> size_type { return _map.size(); }
    [[nodiscard]] auto max_size() const noexcept -> size_type { return _map.max_size(); }

    // Modifiers
    void clear() noexcept { _map.clear(); }

    // Insert single element
    auto insert(const Key& key) -> std::pair<iterator, bool> {
        auto [it, inserted] = _map.insert(key, btree_set_empty_value{});
        return {iterator(it), inserted};
    }

    auto insert(Key&& key) -> std::pair<iterator, bool> {
        auto [it, inserted] = _map.insert(std::move(key), btree_set_empty_value{});
        return {iterator(it), inserted};
    }

    // Insert with hint
    auto insert(const_iterator hint, const Key& key) -> iterator {
        return iterator(_map.insert(hint._it, {key, btree_set_empty_value{}}));
    }

    auto insert(const_iterator hint, Key&& key) -> iterator {
        return iterator(_map.insert(hint._it, {std::move(key), btree_set_empty_value{}}));
    }

    // Insert range
    template <typename InputIt>
    requires requires(InputIt it) {
        { *it } -> std::convertible_to<Key>;
        ++it;
    }
    void insert(InputIt first, InputIt last) {
        for (; first != last; ++first) {
            insert(*first);
        }
    }

    // Insert initializer_list
    void insert(std::initializer_list<Key> ilist) {
        for (const auto& k : ilist) {
            insert(k);
        }
    }

#if __cplusplus >= 202302L || (defined(_MSVC_LANG) && _MSVC_LANG >= 202302L)
    // Insert range (C++23)
    template <std::ranges::input_range R>
    requires std::convertible_to<std::ranges::range_reference_t<R>, Key>
    void insert_range(R&& range) {
        for (auto&& elem : range) {
            insert(std::forward<decltype(elem)>(elem));
        }
    }
#endif

    // Emplace
    template <typename... Args>
    auto emplace(Args&&... args) -> std::pair<iterator, bool> {
        Key key(std::forward<Args>(args)...);
        return insert(std::move(key));
    }

    template <typename... Args>
    auto emplace_hint(const_iterator hint, Args&&... args) -> iterator {
        Key key(std::forward<Args>(args)...);
        return insert(hint, std::move(key));
    }

    // Erase
    auto erase(const_iterator pos) -> iterator { return iterator(_map.erase(pos._it)); }

    auto erase(const_iterator first, const_iterator last) -> iterator {
        return iterator(_map.erase(first._it, last._it));
    }

    auto erase(const Key& key) -> size_type { return _map.erase(key); }

    // Heterogeneous erase (C++23)
    template <typename K>
    requires is_transparent_comparator_v<Compare> && (!std::is_same_v<std::remove_cvref_t<K>, iterator>) &&
             (!std::is_same_v<std::remove_cvref_t<K>, const_iterator>)
    auto erase(const K& key) -> size_type {
        return _map.erase(key);
    }

    // Swap
    void swap(btree_set& other) noexcept { _map.swap(other._map); }

    // Extract (C++17)
    auto extract(const_iterator pos) -> node_type {
        if (pos == end()) return node_type();
        Key key = *pos;
        erase(pos);
        return node_type(std::move(key));
    }

    auto extract(const Key& key) -> node_type {
        auto it = find(key);
        if (it == end()) return node_type();
        return extract(it);
    }

    // Heterogeneous extract (C++23)
    template <typename K>
    requires is_transparent_comparator_v<Compare> && (!std::is_same_v<std::remove_cvref_t<K>, iterator>) &&
             (!std::is_same_v<std::remove_cvref_t<K>, const_iterator>)
    auto extract(const K& key) -> node_type {
        auto it = find(key);
        if (it == end()) return node_type();
        return extract(it);
    }

    // Extract and get next iterator (absl extension)
    auto extract_and_get_next(const_iterator pos) -> std::pair<node_type, iterator> {
        if (pos == end()) return {node_type(), end()};
        Key key = *pos;
        auto next = erase(pos);
        return {node_type(std::move(key)), next};
    }

    // Insert node_type (C++17)
    auto insert(node_type&& nh) -> insert_return_type {
        if (nh.empty()) {
            return {end(), false, std::move(nh)};
        }
        auto [it, inserted] = insert(std::move(nh._key));
        if (inserted) {
            nh._valid = false;
            return {it, true, node_type()};
        }
        return {it, false, std::move(nh)};
    }

    auto insert(const_iterator hint, node_type&& nh) -> iterator {
        if (nh.empty()) return end();
        auto it = insert(hint, std::move(nh._key));
        nh._valid = false;
        return it;
    }

    // Merge (C++17)
    template <typename C2, typename A2, std::size_t N2>
    void merge(btree_set<Key, C2, A2, N2>& source) {
        for (auto it = source.begin(); it != source.end();) {
            auto [insert_it, inserted] = insert(*it);
            if (inserted) {
                it = source.erase(it);
            } else {
                ++it;
            }
        }
    }

    template <typename C2, typename A2, std::size_t N2>
    void merge(btree_set<Key, C2, A2, N2>&& source) {
        merge(source);
    }

    // Lookup
    [[nodiscard]] auto count(const Key& key) const -> size_type { return _map.count(key); }

    template <typename K>
    requires is_transparent_comparator_v<Compare>
    [[nodiscard]] auto count(const K& key) const -> size_type {
        return _map.count(key);
    }

    [[nodiscard]] auto find(const Key& key) -> iterator { return iterator(_map.find(key)); }

    [[nodiscard]] auto find(const Key& key) const -> const_iterator { return const_iterator(_map.find(key)); }

    template <typename K>
    requires is_transparent_comparator_v<Compare>
    [[nodiscard]] auto find(const K& key) -> iterator {
        return iterator(_map.find(key));
    }

    template <typename K>
    requires is_transparent_comparator_v<Compare>
    [[nodiscard]] auto find(const K& key) const -> const_iterator {
        return const_iterator(_map.find(key));
    }

    [[nodiscard]] auto contains(const Key& key) const -> bool { return _map.contains(key); }

    template <typename K>
    requires is_transparent_comparator_v<Compare>
    [[nodiscard]] auto contains(const K& key) const -> bool {
        return _map.contains(key);
    }

    [[nodiscard]] auto equal_range(const Key& key) -> std::pair<iterator, iterator> {
        auto [first, last] = _map.equal_range(key);
        return {iterator(first), iterator(last)};
    }

    [[nodiscard]] auto equal_range(const Key& key) const -> std::pair<const_iterator, const_iterator> {
        auto [first, last] = _map.equal_range(key);
        return {const_iterator(first), const_iterator(last)};
    }

    template <typename K>
    requires is_transparent_comparator_v<Compare>
    [[nodiscard]] auto equal_range(const K& key) -> std::pair<iterator, iterator> {
        auto [first, last] = _map.equal_range(key);
        return {iterator(first), iterator(last)};
    }

    template <typename K>
    requires is_transparent_comparator_v<Compare>
    [[nodiscard]] auto equal_range(const K& key) const -> std::pair<const_iterator, const_iterator> {
        auto [first, last] = _map.equal_range(key);
        return {const_iterator(first), const_iterator(last)};
    }

    [[nodiscard]] auto lower_bound(const Key& key) -> iterator { return iterator(_map.lower_bound(key)); }

    [[nodiscard]] auto lower_bound(const Key& key) const -> const_iterator {
        return const_iterator(_map.lower_bound(key));
    }

    template <typename K>
    requires is_transparent_comparator_v<Compare>
    [[nodiscard]] auto lower_bound(const K& key) -> iterator {
        return iterator(_map.lower_bound(key));
    }

    template <typename K>
    requires is_transparent_comparator_v<Compare>
    [[nodiscard]] auto lower_bound(const K& key) const -> const_iterator {
        return const_iterator(_map.lower_bound(key));
    }

    [[nodiscard]] auto upper_bound(const Key& key) -> iterator { return iterator(_map.upper_bound(key)); }

    [[nodiscard]] auto upper_bound(const Key& key) const -> const_iterator {
        return const_iterator(_map.upper_bound(key));
    }

    template <typename K>
    requires is_transparent_comparator_v<Compare>
    [[nodiscard]] auto upper_bound(const K& key) -> iterator {
        return iterator(_map.upper_bound(key));
    }

    template <typename K>
    requires is_transparent_comparator_v<Compare>
    [[nodiscard]] auto upper_bound(const K& key) const -> const_iterator {
        return const_iterator(_map.upper_bound(key));
    }

    // Observers
    [[nodiscard]] auto key_comp() const -> key_compare { return _map.key_comp(); }
    [[nodiscard]] auto value_comp() const -> value_compare { return _map.key_comp(); }

    // Comparison operators
    friend auto operator==(const btree_set& lhs, const btree_set& rhs) -> bool {
        if (lhs.size() != rhs.size()) return false;
        auto it1 = lhs.begin();
        auto it2 = rhs.begin();
        while (it1 != lhs.end()) {
            if (*it1 != *it2) return false;
            ++it1;
            ++it2;
        }
        return true;
    }

    friend auto operator!=(const btree_set& lhs, const btree_set& rhs) -> bool { return !(lhs == rhs); }

    friend auto operator<(const btree_set& lhs, const btree_set& rhs) -> bool {
        return std::lexicographical_compare(lhs.begin(), lhs.end(), rhs.begin(), rhs.end());
    }

    friend auto operator<=(const btree_set& lhs, const btree_set& rhs) -> bool { return !(rhs < lhs); }
    friend auto operator>(const btree_set& lhs, const btree_set& rhs) -> bool { return rhs < lhs; }
    friend auto operator>=(const btree_set& lhs, const btree_set& rhs) -> bool { return !(lhs < rhs); }

#if __cplusplus >= 202002L || (defined(_MSVC_LANG) && _MSVC_LANG >= 202002L)
    friend auto operator<=>(const btree_set& lhs, const btree_set& rhs) {
        auto it1 = lhs.begin();
        auto it2 = rhs.begin();
        while (it1 != lhs.end() && it2 != rhs.end()) {
            if (auto cmp = *it1 <=> *it2; cmp != 0) return cmp;
            ++it1;
            ++it2;
        }
        return lhs.size() <=> rhs.size();
    }
#endif

    // Debug info
    [[nodiscard]] static constexpr auto leaf_slots() noexcept -> size_type { return map_type::leaf_slots(); }
    [[nodiscard]] static constexpr auto internal_slots() noexcept -> size_type { return map_type::internal_slots(); }
};

// Type aliases for convenience
template <typename Key, typename Compare = std::less<Key>>
using btree_set_auto = btree_set<Key, Compare>;

template <typename Key, typename Compare = std::less<Key>, typename Allocator = std::allocator<Key>>
using btree_set_compact = btree_set<Key, Compare, Allocator, 256>;

// Non-member swap (C++17)
template <typename Key, typename Compare, typename Allocator, std::size_t N>
void swap(btree_set<Key, Compare, Allocator, N>& lhs, btree_set<Key, Compare, Allocator, N>& rhs) noexcept {
    lhs.swap(rhs);
}

// erase_if - erase all elements satisfying predicate (C++20)
template <typename Key, typename Compare, typename Allocator, std::size_t N, typename Pred>
typename btree_set<Key, Compare, Allocator, N>::size_type erase_if(btree_set<Key, Compare, Allocator, N>& c,
                                                                   Pred pred) {
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

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
#include <optional>

#include "vectra.hpp"

namespace stdb::container {

/*
 * ring_buffer is a fixed-size circular buffer.
 * When full, new elements overwrite the oldest elements.
 *
 * Template parameters:
 *   T - Element type
 *   N - Buffer capacity (fixed at compile time)
 *
 * Key features:
 *   - Fixed capacity, no dynamic allocation
 *   - FIFO semantics: push_back adds to tail, pop_front removes from head
 *   - When full, push_back overwrites the oldest element
 *   - O(1) push_back, pop_front, front, back operations
 *   - Supports iteration over all elements (oldest to newest)
 *
 * Use cases:
 *   - Logging buffers (keep last N entries)
 *   - Audio/video sample buffers
 *   - Sliding window algorithms
 *   - Producer-consumer patterns with bounded buffer
 */
template <typename T, std::size_t N>
class ring_buffer
{
    static_assert(N > 0, "ring_buffer capacity must be greater than 0");

   public:
    using size_type = std::size_t;
    using value_type = T;
    using difference_type = std::ptrdiff_t;
    using pointer = T*;
    using const_pointer = const T*;
    using reference = T&;
    using const_reference = const T&;

    static constexpr size_type static_capacity = N;

   private:
    alignas(T) std::byte _storage[N * sizeof(T)];
    size_type _head = 0;  // Index of the oldest element (front)
    size_type _size = 0;  // Current number of elements

    [[nodiscard, gnu::always_inline]] auto storage_ptr() noexcept -> T* { return reinterpret_cast<T*>(_storage); }

    [[nodiscard, gnu::always_inline]] auto storage_ptr() const noexcept -> const T* {
        return reinterpret_cast<const T*>(_storage);
    }

    // Get the actual index in storage for a logical index
    [[nodiscard, gnu::always_inline]] constexpr auto actual_index(size_type logical_idx) const noexcept -> size_type {
        return (_head + logical_idx) % N;
    }

    // Get the tail index (where next element will be written)
    [[nodiscard, gnu::always_inline]] constexpr auto tail_index() const noexcept -> size_type {
        return (_head + _size) % N;
    }

   public:
    // Default constructor
    constexpr ring_buffer() noexcept = default;

    // Constructor with initial elements
    ring_buffer(std::initializer_list<T> init) {
        for (const auto& elem : init) {
            push_back(elem);
        }
    }

    // Copy constructor
    ring_buffer(const ring_buffer& other) : _head(0), _size(0) {
        for (size_type i = 0; i < other._size; ++i) {
            push_back(other[i]);
        }
    }

    // Move constructor
    ring_buffer(ring_buffer&& other) noexcept : _head(0), _size(0) {
        for (size_type i = 0; i < other._size; ++i) {
            push_back(std::move(other[i]));
        }
        other.clear();
    }

    // Copy assignment
    auto operator=(const ring_buffer& other) -> ring_buffer& {
        if (this == &other) [[unlikely]] {
            return *this;
        }
        clear();
        for (size_type i = 0; i < other._size; ++i) {
            push_back(other[i]);
        }
        return *this;
    }

    // Move assignment
    auto operator=(ring_buffer&& other) noexcept -> ring_buffer& {
        if (this == &other) [[unlikely]] {
            return *this;
        }
        clear();
        for (size_type i = 0; i < other._size; ++i) {
            push_back(std::move(other[i]));
        }
        other.clear();
        return *this;
    }

    ~ring_buffer() { clear(); }

    // Capacity
    [[nodiscard, gnu::always_inline]] constexpr auto size() const noexcept -> size_type { return _size; }

    [[nodiscard, gnu::always_inline]] static constexpr auto capacity() noexcept -> size_type { return N; }

    [[nodiscard, gnu::always_inline]] static constexpr auto max_size() noexcept -> size_type { return N; }

    [[nodiscard, gnu::always_inline]] constexpr auto empty() const noexcept -> bool { return _size == 0; }

    [[nodiscard, gnu::always_inline]] constexpr auto full() const noexcept -> bool { return _size == N; }

    [[nodiscard, gnu::always_inline]] constexpr auto available() const noexcept -> size_type { return N - _size; }

    // Element access
    [[nodiscard, gnu::always_inline]] auto operator[](size_type index) noexcept -> reference {
        Assert(index < _size, "index out of range");
        return storage_ptr()[actual_index(index)];
    }

    [[nodiscard, gnu::always_inline]] auto operator[](size_type index) const noexcept -> const_reference {
        Assert(index < _size, "index out of range");
        return storage_ptr()[actual_index(index)];
    }

    [[nodiscard]] auto at(size_type index) -> reference {
        if (index >= _size) [[unlikely]] {
            throw std::out_of_range("ring_buffer::at: index out of range");
        }
        return storage_ptr()[actual_index(index)];
    }

    [[nodiscard]] auto at(size_type index) const -> const_reference {
        if (index >= _size) [[unlikely]] {
            throw std::out_of_range("ring_buffer::at: index out of range");
        }
        return storage_ptr()[actual_index(index)];
    }

    [[nodiscard, gnu::always_inline]] auto front() noexcept -> reference {
        Assert(_size > 0, "front on empty buffer");
        return storage_ptr()[_head];
    }

    [[nodiscard, gnu::always_inline]] auto front() const noexcept -> const_reference {
        Assert(_size > 0, "front on empty buffer");
        return storage_ptr()[_head];
    }

    [[nodiscard, gnu::always_inline]] auto back() noexcept -> reference {
        Assert(_size > 0, "back on empty buffer");
        return storage_ptr()[actual_index(_size - 1)];
    }

    [[nodiscard, gnu::always_inline]] auto back() const noexcept -> const_reference {
        Assert(_size > 0, "back on empty buffer");
        return storage_ptr()[actual_index(_size - 1)];
    }

    // Modifiers

    // Push element to the back. If full, overwrites the oldest element.
    void push_back(const value_type& value) {
        if (_size == N) {
            // Overwrite oldest element
            storage_ptr()[_head] = value;
            _head = (_head + 1) % N;
        } else {
            new (storage_ptr() + tail_index()) T(value);
            ++_size;
        }
    }

    void push_back(value_type&& value) {
        if (_size == N) {
            // Overwrite oldest element
            storage_ptr()[_head] = std::move(value);
            _head = (_head + 1) % N;
        } else {
            new (storage_ptr() + tail_index()) T(std::move(value));
            ++_size;
        }
    }

    template <typename... Args>
    auto emplace_back(Args&&... args) -> reference {
        if (_size == N) {
            // Destroy oldest and construct new in its place
            T* ptr = storage_ptr() + _head;
            ptr->~T();
            new (ptr) T(std::forward<Args>(args)...);
            _head = (_head + 1) % N;
            return *ptr;
        } else {
            T* ptr = storage_ptr() + tail_index();
            new (ptr) T(std::forward<Args>(args)...);
            ++_size;
            return *ptr;
        }
    }

    // Push element, but don't overwrite if full. Returns false if buffer was full.
    auto try_push_back(const value_type& value) -> bool {
        if (_size == N) {
            return false;
        }
        new (storage_ptr() + tail_index()) T(value);
        ++_size;
        return true;
    }

    auto try_push_back(value_type&& value) -> bool {
        if (_size == N) {
            return false;
        }
        new (storage_ptr() + tail_index()) T(std::move(value));
        ++_size;
        return true;
    }

    // Pop from front (oldest element)
    void pop_front() noexcept {
        Assert(_size > 0, "pop_front on empty buffer");
        destroy_ptr(storage_ptr() + _head);
        _head = (_head + 1) % N;
        --_size;
    }

    // Pop from back (newest element)
    void pop_back() noexcept {
        Assert(_size > 0, "pop_back on empty buffer");
        --_size;
        destroy_ptr(storage_ptr() + actual_index(_size));
    }

    // Try to pop front, returns the value if successful
    [[nodiscard]] auto try_pop_front() -> std::optional<T> {
        if (_size == 0) {
            return std::nullopt;
        }
        T* ptr = storage_ptr() + _head;
        std::optional<T> result(std::move(*ptr));
        destroy_ptr(ptr);
        _head = (_head + 1) % N;
        --_size;
        return result;
    }

    void clear() noexcept {
        for (size_type i = 0; i < _size; ++i) {
            destroy_ptr(storage_ptr() + actual_index(i));
        }
        _head = 0;
        _size = 0;
    }

    // Iterator support
    template <bool Const>
    class IteratorT
    {
       public:
        using iterator_category = std::random_access_iterator_tag;
        using difference_type = std::ptrdiff_t;
        using value_type = std::conditional_t<Const, const T, T>;
        using pointer = std::conditional_t<Const, const T*, T*>;
        using reference = std::conditional_t<Const, const T&, T&>;

       private:
        using buffer_ptr = std::conditional_t<Const, const ring_buffer*, ring_buffer*>;
        buffer_ptr _buffer;
        size_type _index;  // Logical index (0 = front)

       public:
        IteratorT() : _buffer(nullptr), _index(0) {}
        IteratorT(buffer_ptr buf, size_type idx) : _buffer(buf), _index(idx) {}
        ~IteratorT() = default;
        IteratorT(const IteratorT&) = default;
        IteratorT(IteratorT&&) noexcept = default;
        auto operator=(const IteratorT&) -> IteratorT& = default;
        auto operator=(IteratorT&&) noexcept -> IteratorT& = default;

        // Convert mutable to const iterator
        template <bool OtherConst>
        requires(!OtherConst && Const)
        IteratorT(const IteratorT<OtherConst>& other) : _buffer(other._buffer), _index(other._index) {}

        [[nodiscard]] auto operator*() const -> reference { return (*_buffer)[_index]; }

        [[nodiscard]] auto operator->() const -> pointer { return &(*_buffer)[_index]; }

        [[nodiscard]] auto operator[](difference_type n) const -> reference { return (*_buffer)[_index + n]; }

        auto operator++() -> IteratorT& {
            ++_index;
            return *this;
        }
        auto operator++(int) -> IteratorT {
            auto tmp = *this;
            ++_index;
            return tmp;
        }
        auto operator--() -> IteratorT& {
            --_index;
            return *this;
        }
        auto operator--(int) -> IteratorT {
            auto tmp = *this;
            --_index;
            return tmp;
        }

        auto operator+=(difference_type n) -> IteratorT& {
            _index += n;
            return *this;
        }
        auto operator-=(difference_type n) -> IteratorT& {
            _index -= n;
            return *this;
        }

        friend auto operator+(const IteratorT& it, difference_type n) -> IteratorT {
            return IteratorT{it._buffer, it._index + n};
        }
        friend auto operator+(difference_type n, const IteratorT& it) -> IteratorT {
            return IteratorT{it._buffer, it._index + n};
        }
        friend auto operator-(const IteratorT& it, difference_type n) -> IteratorT {
            return IteratorT{it._buffer, it._index - n};
        }
        friend auto operator-(const IteratorT& lhs, const IteratorT& rhs) -> difference_type {
            return static_cast<difference_type>(lhs._index) - static_cast<difference_type>(rhs._index);
        }

        friend auto operator==(const IteratorT& lhs, const IteratorT& rhs) -> bool {
            return lhs._buffer == rhs._buffer && lhs._index == rhs._index;
        }
        friend auto operator!=(const IteratorT& lhs, const IteratorT& rhs) -> bool { return !(lhs == rhs); }
        friend auto operator<(const IteratorT& lhs, const IteratorT& rhs) -> bool { return lhs._index < rhs._index; }
        friend auto operator<=(const IteratorT& lhs, const IteratorT& rhs) -> bool { return lhs._index <= rhs._index; }
        friend auto operator>(const IteratorT& lhs, const IteratorT& rhs) -> bool { return lhs._index > rhs._index; }
        friend auto operator>=(const IteratorT& lhs, const IteratorT& rhs) -> bool { return lhs._index >= rhs._index; }

        // Allow const_iterator to access private members of iterator
        template <bool>
        friend class IteratorT;
    };

    using iterator = IteratorT<false>;
    using const_iterator = IteratorT<true>;
    using reverse_iterator = std::reverse_iterator<iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;

    [[nodiscard]] auto begin() noexcept -> iterator { return iterator(this, 0); }
    [[nodiscard]] auto begin() const noexcept -> const_iterator { return const_iterator(this, 0); }
    [[nodiscard]] auto cbegin() const noexcept -> const_iterator { return const_iterator(this, 0); }
    [[nodiscard]] auto end() noexcept -> iterator { return iterator(this, _size); }
    [[nodiscard]] auto end() const noexcept -> const_iterator { return const_iterator(this, _size); }
    [[nodiscard]] auto cend() const noexcept -> const_iterator { return const_iterator(this, _size); }
    [[nodiscard]] auto rbegin() noexcept -> reverse_iterator { return std::make_reverse_iterator(end()); }
    [[nodiscard]] auto rbegin() const noexcept -> const_reverse_iterator { return std::make_reverse_iterator(end()); }
    [[nodiscard]] auto crbegin() const noexcept -> const_reverse_iterator { return std::make_reverse_iterator(cend()); }
    [[nodiscard]] auto rend() noexcept -> reverse_iterator { return std::make_reverse_iterator(begin()); }
    [[nodiscard]] auto rend() const noexcept -> const_reverse_iterator { return std::make_reverse_iterator(begin()); }
    [[nodiscard]] auto crend() const noexcept -> const_reverse_iterator { return std::make_reverse_iterator(cbegin()); }

    // Search operations (no SIMD - circular buffer is not contiguous)
    [[nodiscard]] auto find(const_reference value) noexcept -> iterator {
        for (size_type i = 0; i < _size; ++i) {
            if ((*this)[i] == value) return iterator(this, i);
        }
        return end();
    }

    [[nodiscard]] auto find(const_reference value) const noexcept -> const_iterator {
        for (size_type i = 0; i < _size; ++i) {
            if ((*this)[i] == value) return const_iterator(this, i);
        }
        return cend();
    }

    [[nodiscard]] auto contains(const_reference value) const noexcept -> bool {
        for (size_type i = 0; i < _size; ++i) {
            if ((*this)[i] == value) return true;
        }
        return false;
    }

    [[nodiscard]] auto count(const_reference value) const noexcept -> size_type {
        size_type result = 0;
        for (size_type i = 0; i < _size; ++i) {
            if ((*this)[i] == value) ++result;
        }
        return result;
    }

    // Swap
    void swap(ring_buffer& other) noexcept {
        ring_buffer temp(std::move(*this));
        *this = std::move(other);
        other = std::move(temp);
    }

    // Linearize: copy elements to a contiguous array (oldest first)
    template <typename OutputIt>
    void copy_to(OutputIt out) const {
        for (size_type i = 0; i < _size; ++i) {
            *out++ = (*this)[i];
        }
    }
};

// Comparison operators
template <typename T, std::size_t N>
auto operator==(const ring_buffer<T, N>& lhs, const ring_buffer<T, N>& rhs) -> bool {
    if (lhs.size() != rhs.size()) return false;
    for (std::size_t i = 0; i < lhs.size(); ++i) {
        if (lhs[i] != rhs[i]) return false;
    }
    return true;
}

template <typename T, std::size_t N>
auto operator!=(const ring_buffer<T, N>& lhs, const ring_buffer<T, N>& rhs) -> bool {
    return !(lhs == rhs);
}

}  // namespace stdb::container

namespace std {

template <typename T, std::size_t N>
void swap(stdb::container::ring_buffer<T, N>& lhs, stdb::container::ring_buffer<T, N>& rhs) noexcept {
    lhs.swap(rhs);
}

}  // namespace std

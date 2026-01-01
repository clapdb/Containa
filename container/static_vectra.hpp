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
#include <stdexcept>

#include "container_base.hpp"

namespace stdb::container {

/*
 * static_vectra is a fixed-capacity vector with embedded storage.
 * It NEVER performs dynamic memory allocation.
 *
 * Template parameters:
 *   T - Element type
 *   N - Maximum capacity (fixed at compile time)
 *
 * Key features:
 *   - Zero heap allocation - all storage is inline
 *   - Fixed maximum capacity known at compile time
 *   - Suitable for embedded systems, real-time systems, and latency-sensitive code
 *   - Throws std::length_error when capacity is exceeded (in Safe mode)
 *   - Supports Safe/Unsafe modes like vectra
 *
 * Use cases:
 *   - Embedded systems without heap
 *   - Real-time systems requiring deterministic memory behavior
 *   - Stack-allocated buffers with known maximum size
 *   - Performance-critical code avoiding allocation overhead
 */
template <typename T, std::size_t N>
class static_vectra
{
    static_assert(N > 0, "static_vectra capacity must be greater than 0");

   public:
    using size_type = std::size_t;
    using value_type = T;
    using difference_type = std::ptrdiff_t;
    using pointer = T*;
    using const_pointer = const T*;
    using reference = T&;
    using const_reference = const T&;
    using rvalue_reference = T&&;

    static constexpr size_type static_capacity = N;

   private:
    alignas(T) std::byte _storage[N * sizeof(T)];
    size_type _size = 0;

    [[nodiscard, gnu::always_inline]] auto data_ptr() noexcept -> T* { return reinterpret_cast<T*>(_storage); }

    [[nodiscard, gnu::always_inline]] auto data_ptr() const noexcept -> const T* {
        return reinterpret_cast<const T*>(_storage);
    }

    void throw_if_full() const {
        if (_size >= N) [[unlikely]] {
            throw std::length_error("static_vectra capacity exceeded");
        }
    }

    void throw_if_insufficient(size_type required) const {
        if (required > N) [[unlikely]] {
            throw std::length_error("static_vectra capacity exceeded");
        }
    }

   public:
    // Default constructor
    constexpr static_vectra() noexcept = default;

    // Constructor with size (default-initialized elements)
    explicit static_vectra(size_type count) : _size(0) {
        throw_if_insufficient(count);
        _size = count;
        if (count > 0) {
            construct_range(data_ptr(), data_ptr() + count);
        }
    }

    // Constructor with size and value
    static_vectra(size_type count, const T& value) : _size(0) {
        throw_if_insufficient(count);
        _size = count;
        if (count > 0) {
            construct_range_with_cref(data_ptr(), data_ptr() + count, value);
        }
    }

    // Constructor from iterators
    template <std::forward_iterator InputIt>
    static_vectra(InputIt first, InputIt last) : _size(0) {
        auto dist = std::distance(first, last);
        Assert(dist >= 0, "invalid iterator range");
        size_type count = static_cast<size_type>(dist);
        throw_if_insufficient(count);
        if (count > 0) {
            copy_from_iterator(data_ptr(), first, last);
            _size = count;
        }
    }

    // Constructor from initializer list
    static_vectra(std::initializer_list<T> init) : static_vectra(init.begin(), init.end()) {}

    // Copy constructor
    static_vectra(const static_vectra& other) : _size(0) {
        if (other._size > 0) {
            copy_range(data_ptr(), other.data_ptr(), other.data_ptr() + other._size);
            _size = other._size;
        }
    }

    // Move constructor
    static_vectra(static_vectra&& other) noexcept : _size(0) {
        if (other._size > 0) {
            (void)move_range_without_overlap(data_ptr(), other.data_ptr(), other.data_ptr() + other._size);
            destroy_range(other.data_ptr(), other.data_ptr() + other._size);
            _size = other._size;
            other._size = 0;
        }
    }

    // Copy assignment
    auto operator=(const static_vectra& other) -> static_vectra& {
        if (this == &other) [[unlikely]] {
            return *this;
        }
        destroy_range(data_ptr(), data_ptr() + _size);
        if (other._size > 0) {
            copy_range(data_ptr(), other.data_ptr(), other.data_ptr() + other._size);
        }
        _size = other._size;
        return *this;
    }

    // Move assignment
    auto operator=(static_vectra&& other) noexcept -> static_vectra& {
        if (this == &other) [[unlikely]] {
            return *this;
        }
        destroy_range(data_ptr(), data_ptr() + _size);
        if (other._size > 0) {
            (void)move_range_without_overlap(data_ptr(), other.data_ptr(), other.data_ptr() + other._size);
            destroy_range(other.data_ptr(), other.data_ptr() + other._size);
        }
        _size = other._size;
        other._size = 0;
        return *this;
    }

    ~static_vectra() { destroy_range(data_ptr(), data_ptr() + _size); }

    // Capacity
    [[nodiscard, gnu::always_inline]] constexpr auto size() const noexcept -> size_type { return _size; }

    [[nodiscard, gnu::always_inline]] static constexpr auto capacity() noexcept -> size_type { return N; }

    [[nodiscard, gnu::always_inline]] static constexpr auto max_size() noexcept -> size_type { return N; }

    [[nodiscard, gnu::always_inline]] constexpr auto empty() const noexcept -> bool { return _size == 0; }

    [[nodiscard, gnu::always_inline]] constexpr auto full() const noexcept -> bool { return _size == N; }

    [[nodiscard, gnu::always_inline]] constexpr auto available() const noexcept -> size_type { return N - _size; }

    // reserve is a no-op for static_vectra (capacity is fixed)
    // but we provide it for API compatibility
    void reserve(size_type new_cap) {
        if (new_cap > N) [[unlikely]] {
            throw std::length_error("static_vectra::reserve: capacity exceeded");
        }
    }

    // shrink_to_fit is a no-op (capacity is fixed)
    constexpr void shrink_to_fit() noexcept {}

    // Element access
    [[nodiscard, gnu::always_inline]] constexpr auto operator[](size_type index) noexcept -> reference {
        Assert(index < _size, "index out of range");
        return data_ptr()[index];
    }

    [[nodiscard, gnu::always_inline]] constexpr auto operator[](size_type index) const noexcept -> const_reference {
        Assert(index < _size, "index out of range");
        return data_ptr()[index];
    }

    [[nodiscard, gnu::always_inline]] constexpr auto at(size_type index) -> reference {
        if (index >= _size) [[unlikely]] {
            throw std::out_of_range("static_vectra::at: index out of range");
        }
        return data_ptr()[index];
    }

    [[nodiscard, gnu::always_inline]] constexpr auto at(size_type index) const -> const_reference {
        if (index >= _size) [[unlikely]] {
            throw std::out_of_range("static_vectra::at: index out of range");
        }
        return data_ptr()[index];
    }

    [[nodiscard, gnu::always_inline]] constexpr auto data() noexcept -> pointer { return data_ptr(); }

    [[nodiscard, gnu::always_inline]] constexpr auto data() const noexcept -> const_pointer { return data_ptr(); }

    [[nodiscard, gnu::always_inline]] constexpr auto front() noexcept -> reference {
        Assert(_size > 0, "front on empty container");
        return data_ptr()[0];
    }

    [[nodiscard, gnu::always_inline]] constexpr auto front() const noexcept -> const_reference {
        Assert(_size > 0, "front on empty container");
        return data_ptr()[0];
    }

    [[nodiscard, gnu::always_inline]] constexpr auto back() noexcept -> reference {
        Assert(_size > 0, "back on empty container");
        return data_ptr()[_size - 1];
    }

    [[nodiscard, gnu::always_inline]] constexpr auto back() const noexcept -> const_reference {
        Assert(_size > 0, "back on empty container");
        return data_ptr()[_size - 1];
    }

    // Iterator support
    template <bool Const>
    struct IteratorT
    {
        using iterator_category = std::contiguous_iterator_tag;
        using difference_type = std::ptrdiff_t;
        using value_type = std::conditional_t<Const, const T, T>;
        using pointer = std::conditional_t<Const, const T*, T*>;
        using reference = std::conditional_t<Const, const T&, T&>;

       private:
        pointer _ptr;

       public:
        IteratorT() : _ptr(nullptr) {}
        ~IteratorT() = default;
        explicit IteratorT(pointer ptr) : _ptr(ptr) {}
        IteratorT(IteratorT&& rhs) noexcept : _ptr(std::exchange(rhs._ptr, nullptr)) {}
        IteratorT(const IteratorT&) noexcept = default;

        template <bool OtherConst>
        requires(!OtherConst && Const)
        IteratorT(IteratorT<OtherConst> rhs) noexcept : _ptr(rhs.operator->()) {}

        auto operator=(IteratorT&& rhs) noexcept -> IteratorT& {
            _ptr = std::exchange(rhs._ptr, nullptr);
            return *this;
        }
        auto operator=(const IteratorT&) noexcept -> IteratorT& = default;

        [[gnu::always_inline]] constexpr auto operator++() noexcept -> IteratorT& {
            ++_ptr;
            return *this;
        }
        [[gnu::always_inline, nodiscard]] constexpr auto operator++(int) noexcept -> IteratorT {
            auto tmp = *this;
            ++_ptr;
            return tmp;
        }
        [[gnu::always_inline]] constexpr auto operator--() noexcept -> IteratorT& {
            --_ptr;
            return *this;
        }
        [[gnu::always_inline, nodiscard]] constexpr auto operator--(int) noexcept -> IteratorT {
            auto tmp = *this;
            --_ptr;
            return tmp;
        }
        [[gnu::always_inline]] constexpr auto operator+=(difference_type n) noexcept -> IteratorT& {
            _ptr += n;
            return *this;
        }
        [[gnu::always_inline]] constexpr auto operator-=(difference_type n) noexcept -> IteratorT& {
            _ptr -= n;
            return *this;
        }

        [[nodiscard, gnu::always_inline]] constexpr auto operator*() const noexcept -> reference { return *_ptr; }
        [[nodiscard, gnu::always_inline]] constexpr auto operator->() const noexcept -> pointer { return _ptr; }
        [[nodiscard, gnu::always_inline]] constexpr auto operator[](size_type pos) const noexcept -> reference {
            return *(_ptr + pos);
        }

        friend auto operator<=>(const IteratorT& lhs, const IteratorT& rhs) noexcept -> std::strong_ordering = default;
        friend auto operator-(const IteratorT& lhs, const IteratorT& rhs) noexcept -> difference_type {
            return lhs._ptr - rhs._ptr;
        }
        friend auto operator+(const IteratorT& lhs, difference_type offset) noexcept -> IteratorT {
            return IteratorT{lhs._ptr + offset};
        }
        friend auto operator-(const IteratorT& lhs, difference_type offset) noexcept -> IteratorT {
            return IteratorT{lhs._ptr - offset};
        }
        friend auto operator+(difference_type offset, const IteratorT& rhs) noexcept -> IteratorT {
            return IteratorT{rhs._ptr + offset};
        }
    };

    using iterator = IteratorT<false>;
    using const_iterator = IteratorT<true>;
    using reverse_iterator = std::reverse_iterator<iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;

    [[nodiscard]] auto begin() noexcept -> iterator { return iterator(data_ptr()); }
    [[nodiscard]] auto begin() const noexcept -> const_iterator { return const_iterator(data_ptr()); }
    [[nodiscard]] auto cbegin() const noexcept -> const_iterator { return const_iterator(data_ptr()); }
    [[nodiscard]] auto end() noexcept -> iterator { return iterator(data_ptr() + _size); }
    [[nodiscard]] auto end() const noexcept -> const_iterator { return const_iterator(data_ptr() + _size); }
    [[nodiscard]] auto cend() const noexcept -> const_iterator { return const_iterator(data_ptr() + _size); }
    [[nodiscard]] auto rbegin() noexcept -> reverse_iterator { return std::make_reverse_iterator(end()); }
    [[nodiscard]] auto rbegin() const noexcept -> const_reverse_iterator { return std::make_reverse_iterator(end()); }
    [[nodiscard]] auto crbegin() const noexcept -> const_reverse_iterator { return std::make_reverse_iterator(cend()); }
    [[nodiscard]] auto rend() noexcept -> reverse_iterator { return std::make_reverse_iterator(begin()); }
    [[nodiscard]] auto rend() const noexcept -> const_reverse_iterator { return std::make_reverse_iterator(begin()); }
    [[nodiscard]] auto crend() const noexcept -> const_reverse_iterator { return std::make_reverse_iterator(cbegin()); }

    // SIMD-accelerated search operations
    [[nodiscard]] auto find(const_reference value) noexcept -> iterator {
        if constexpr (simd::is_simd_comparable_v<T>) {
            std::size_t idx = simd::simd_find(data_ptr(), _size, value);
            return iterator(data_ptr() + idx);
        } else {
            for (T* p = data_ptr(); p != data_ptr() + _size; ++p) {
                if (*p == value) return iterator(p);
            }
            return end();
        }
    }

    [[nodiscard]] auto find(const_reference value) const noexcept -> const_iterator {
        if constexpr (simd::is_simd_comparable_v<T>) {
            std::size_t idx = simd::simd_find(data_ptr(), _size, value);
            return const_iterator(data_ptr() + idx);
        } else {
            for (const T* p = data_ptr(); p != data_ptr() + _size; ++p) {
                if (*p == value) return const_iterator(p);
            }
            return cend();
        }
    }

    [[nodiscard]] auto contains(const_reference value) const noexcept -> bool {
        if constexpr (simd::is_simd_comparable_v<T>) {
            return simd::simd_find(data_ptr(), _size, value) < _size;
        } else {
            for (const T* p = data_ptr(); p != data_ptr() + _size; ++p) {
                if (*p == value) return true;
            }
            return false;
        }
    }

    [[nodiscard]] auto count(const_reference value) const noexcept -> size_type {
        size_type result = 0;
        for (const T* p = data_ptr(); p != data_ptr() + _size; ++p) {
            if (*p == value) ++result;
        }
        return result;
    }

    // Modifiers
    template <Safety safety = Safety::Safe>
    void push_back(const value_type& value) {
        if constexpr (safety == Safety::Safe) {
            throw_if_full();
        } else {
            Assert(_size < N, "push_back on full container");
            VECTRA_ASSUME(_size < N);
        }
        copy_cref(data_ptr() + _size, value);
        ++_size;
    }

    template <Safety safety = Safety::Safe>
    void push_back(value_type&& value) {
        if constexpr (safety == Safety::Safe) {
            throw_if_full();
        } else {
            Assert(_size < N, "push_back on full container");
            VECTRA_ASSUME(_size < N);
        }
        copy_value(data_ptr() + _size, std::move(value));
        ++_size;
    }

    template <Safety safety = Safety::Safe, typename... Args>
    auto emplace_back(Args&&... args) -> reference {
        if constexpr (safety == Safety::Safe) {
            throw_if_full();
        } else {
            Assert(_size < N, "emplace_back on full container");
            VECTRA_ASSUME(_size < N);
        }
        new (data_ptr() + _size) T(std::forward<Args>(args)...);
        ++_size;
        return data_ptr()[_size - 1];
    }

    void pop_back() noexcept {
        Assert(_size > 0, "pop_back on empty container");
        --_size;
        destroy_ptr(data_ptr() + _size);
    }

    void clear() noexcept {
        destroy_range(data_ptr(), data_ptr() + _size);
        _size = 0;
    }

    void resize(size_type count) {
        if (count > _size) {
            throw_if_insufficient(count);
            construct_range(data_ptr() + _size, data_ptr() + count);
        } else if (count < _size) {
            destroy_range(data_ptr() + count, data_ptr() + _size);
        }
        _size = count;
    }

    void resize(size_type count, const value_type& value) {
        if (count > _size) {
            throw_if_insufficient(count);
            construct_range_with_cref(data_ptr() + _size, data_ptr() + count, value);
        } else if (count < _size) {
            destroy_range(data_ptr() + count, data_ptr() + _size);
        }
        _size = count;
    }

    void assign(size_type count, const value_type& value) {
        throw_if_insufficient(count);
        destroy_range(data_ptr(), data_ptr() + _size);
        _size = count;
        if (count > 0) {
            construct_range_with_cref(data_ptr(), data_ptr() + count, value);
        }
    }

    template <std::forward_iterator InputIt>
    void assign(InputIt first, InputIt last) {
        auto dist = std::distance(first, last);
        Assert(dist >= 0, "invalid iterator range");
        size_type count = static_cast<size_type>(dist);
        throw_if_insufficient(count);
        destroy_range(data_ptr(), data_ptr() + _size);
        _size = count;
        if (count > 0) {
            copy_from_iterator(data_ptr(), first, last);
        }
    }

    void assign(std::initializer_list<T> ilist) { assign(ilist.begin(), ilist.end()); }

    // Erase operations
    auto erase(const_iterator pos) -> iterator {
        Assert(pos >= cbegin() && pos < cend(), "invalid iterator");
        T* pos_ptr = const_cast<T*>(pos.operator->());
        destroy_ptr(pos_ptr);
        move_range_forward(pos_ptr, pos_ptr + 1, data_ptr() + _size);
        --_size;
        return iterator(pos_ptr);
    }

    auto erase(const_iterator first, const_iterator last) -> iterator {
        Assert(first >= cbegin() && last <= cend(), "invalid range");
        Assert(last >= first, "invalid range");
        T* first_ptr = const_cast<T*>(first.operator->());
        T* last_ptr = const_cast<T*>(last.operator->());
        if (first_ptr != last_ptr) {
            destroy_range(first_ptr, last_ptr);
            move_range_forward(first_ptr, last_ptr, data_ptr() + _size);
            _size -= static_cast<size_type>(last_ptr - first_ptr);
        }
        return iterator(first_ptr);
    }

    // Insert operations
    template <Safety safety = Safety::Safe>
    auto insert(const_iterator pos, const value_type& value) -> iterator {
        if constexpr (safety == Safety::Safe) {
            throw_if_full();
        } else {
            Assert(_size < N, "insert on full container");
        }

        T* pos_ptr = const_cast<T*>(pos.operator->());
        T* end_ptr = data_ptr() + _size;

        if (pos_ptr == end_ptr) {
            copy_cref(end_ptr, value);
            ++_size;
            return iterator(pos_ptr);
        }

        // Move elements backward
        if (pos_ptr < end_ptr - 1) {
            move_backward(pos_ptr + 1, pos_ptr, end_ptr);
        } else {
            move_range_forward(pos_ptr + 1, pos_ptr, end_ptr);
        }
        copy_cref(pos_ptr, value);
        ++_size;
        return iterator(pos_ptr);
    }

    template <Safety safety = Safety::Safe>
    auto insert(const_iterator pos, value_type&& value) -> iterator {
        if constexpr (safety == Safety::Safe) {
            throw_if_full();
        } else {
            Assert(_size < N, "insert on full container");
        }

        T* pos_ptr = const_cast<T*>(pos.operator->());
        T* end_ptr = data_ptr() + _size;

        if (pos_ptr == end_ptr) {
            copy_value(end_ptr, std::move(value));
            ++_size;
            return iterator(pos_ptr);
        }

        if (pos_ptr < end_ptr - 1) {
            move_backward(pos_ptr + 1, pos_ptr, end_ptr);
        } else {
            move_range_forward(pos_ptr + 1, pos_ptr, end_ptr);
        }
        copy_value(pos_ptr, std::move(value));
        ++_size;
        return iterator(pos_ptr);
    }

    template <Safety safety = Safety::Safe>
    auto insert(const_iterator pos, size_type count, const value_type& value) -> iterator {
        if constexpr (safety == Safety::Safe) {
            throw_if_insufficient(_size + count);
        } else {
            Assert(_size + count <= N, "insert would exceed capacity");
        }

        T* pos_ptr = const_cast<T*>(pos.operator->());
        T* end_ptr = data_ptr() + _size;

        if (pos_ptr + count >= end_ptr) {
            move_range_forward(pos_ptr + count, pos_ptr, end_ptr);
        } else {
            move_backward(pos_ptr + count, pos_ptr, end_ptr);
        }
        construct_range_with_cref(pos_ptr, pos_ptr + count, value);
        _size += count;
        return iterator(pos_ptr);
    }

    void swap(static_vectra& other) noexcept {
        size_type min_size = std::min(_size, other._size);

        // Swap common elements
        for (size_type i = 0; i < min_size; ++i) {
            std::swap(data_ptr()[i], other.data_ptr()[i]);
        }

        // Move extra elements
        if (_size > other._size) {
            (void)move_range_without_overlap(other.data_ptr() + min_size, data_ptr() + min_size, data_ptr() + _size);
            destroy_range(data_ptr() + min_size, data_ptr() + _size);
        } else if (other._size > _size) {
            (void)move_range_without_overlap(data_ptr() + min_size, other.data_ptr() + min_size,
                                             other.data_ptr() + other._size);
            destroy_range(other.data_ptr() + min_size, other.data_ptr() + other._size);
        }

        std::swap(_size, other._size);
    }

   private:
    void move_backward(T* dst, T* src, T* src_end) {
        Assert(dst != nullptr && src != nullptr, "null pointers");
        Assert(src < src_end, "invalid range");

        size_type count = src_end - src;
        T* dst_end = dst + count - 1;
        T* src_ptr = src_end - 1;

        if constexpr (IsRelocatable<T>) {
            for (size_type i = 0; i < count; ++i) {
                *(dst_end--) = *(src_ptr--);
            }
        } else if constexpr (std::is_move_constructible_v<T>) {
            static_assert(std::is_nothrow_move_constructible_v<T>);
            while (src_ptr >= src) {
                new (dst_end--) T(std::move(*src_ptr));
                if constexpr (NeedsCleanUp<T>) {
                    src_ptr->~T();
                }
                --src_ptr;
            }
        } else {
            static_assert(std::is_copy_constructible_v<T>);
            while (src_ptr >= src) {
                new (dst_end) T(*src_ptr);
                src_ptr->~T();
                --dst_end;
                --src_ptr;
            }
        }
    }
};

// Comparison operators
template <typename T, std::size_t N>
auto operator==(const static_vectra<T, N>& lhs, const static_vectra<T, N>& rhs) -> bool {
    if (lhs.size() != rhs.size()) return false;
    if (lhs.size() == 0) return true;
    // Use memcmp only for types with unique object representations (no padding)
    if constexpr (std::has_unique_object_representations_v<T>) {
        return std::memcmp(lhs.data(), rhs.data(), lhs.size() * sizeof(T)) == 0;
    } else {
        for (std::size_t i = 0; i < lhs.size(); ++i) {
            if (lhs[i] != rhs[i]) return false;
        }
        return true;
    }
}

template <typename T, std::size_t N>
auto operator<=>(const static_vectra<T, N>& lhs, const static_vectra<T, N>& rhs) -> std::strong_ordering {
    for (std::size_t i = 0; i < std::min(lhs.size(), rhs.size()); ++i) {
        if (lhs[i] < rhs[i]) return std::strong_ordering::less;
        if (lhs[i] > rhs[i]) return std::strong_ordering::greater;
    }
    if (lhs.size() < rhs.size()) return std::strong_ordering::less;
    if (lhs.size() > rhs.size()) return std::strong_ordering::greater;
    return std::strong_ordering::equal;
}

// Cross-size comparison (different N values)
template <typename T, std::size_t N1, std::size_t N2>
requires(N1 != N2)
auto operator==(const static_vectra<T, N1>& lhs, const static_vectra<T, N2>& rhs) -> bool {
    if (lhs.size() != rhs.size()) return false;
    if (lhs.size() == 0) return true;
    // Use memcmp only for types with unique object representations (no padding)
    if constexpr (std::has_unique_object_representations_v<T>) {
        return std::memcmp(lhs.data(), rhs.data(), lhs.size() * sizeof(T)) == 0;
    } else {
        for (std::size_t i = 0; i < lhs.size(); ++i) {
            if (lhs[i] != rhs[i]) return false;
        }
        return true;
    }
}

}  // namespace stdb::container

namespace std {

template <typename T, std::size_t N>
constexpr void swap(stdb::container::static_vectra<T, N>& lhs, stdb::container::static_vectra<T, N>& rhs) noexcept {
    lhs.swap(rhs);
}

template <typename T, std::size_t N, typename U>
constexpr auto erase(stdb::container::static_vectra<T, N>& vec, const U& value) -> std::size_t {
    auto it = std::remove(vec.begin(), vec.end(), value);
    auto count = vec.end() - it;
    vec.erase(it, vec.end());
    return count;
}

template <typename T, std::size_t N, typename Pred>
constexpr auto erase_if(stdb::container::static_vectra<T, N>& vec, Pred pred) -> std::size_t {
    auto it = std::remove_if(vec.begin(), vec.end(), pred);
    auto count = vec.end() - it;
    vec.erase(it, vec.end());
    return count;
}

}  // namespace std

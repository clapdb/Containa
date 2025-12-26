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

#include "vectra.hpp"

namespace stdb::container {

/*
 * small_vectra is a vector-like container with inline storage optimization.
 * It stores up to N elements inline (no heap allocation), and automatically
 * switches to heap allocation when capacity exceeds N.
 *
 * Template parameters:
 *   T - Element type
 *   N - Number of elements to store inline (default: calculated from 64 bytes)
 *   Alloc - Allocator type (for API compatibility, currently unused)
 *
 * Key features:
 *   - Zero heap allocation for small sizes (≤ N elements)
 *   - Automatic transition to heap when capacity > N
 *   - Same API as vectra/std::vector
 *   - Supports Safe/Unsafe modes like vectra
 */
template <typename T, std::size_t N = (sizeof(T) >= 64 ? 1 : 64 / sizeof(T)), typename Alloc = std::allocator<T>>
class small_vectra
{
   public:
    using size_type = std::size_t;
    using value_type = T;
    using difference_type = std::ptrdiff_t;
    using pointer = T*;
    using const_pointer = const T*;
    using reference = T&;
    using const_reference = const T&;
    using rvalue_reference = T&&;
    using allocator_type = Alloc;

    static constexpr size_type inline_capacity = N;

   private:
    // Inline storage - properly aligned for T
    alignas(T) std::byte _inline_storage[N * sizeof(T)];

    T* _start;   // buffer start
    T* _finish;  // valid end (one past last element)
    T* _edge;    // buffer end (capacity boundary)

    [[nodiscard, gnu::always_inline]] auto inline_ptr() noexcept -> T* { return reinterpret_cast<T*>(_inline_storage); }

    [[nodiscard, gnu::always_inline]] auto inline_ptr() const noexcept -> const T* {
        return reinterpret_cast<const T*>(_inline_storage);
    }

    [[nodiscard, gnu::always_inline]] auto is_inline() const noexcept -> bool {
        return _start == reinterpret_cast<const T*>(_inline_storage);
    }

    void allocate_heap(size_type cap) {
        Assert(cap > N, "allocate_heap should only be called when cap > N");
        _start = static_cast<T*>(std::malloc(cap * sizeof(T)));
        if (_start == nullptr) [[unlikely]] {
            throw std::bad_alloc();
        }
        _edge = _start + cap;
    }

    void free_heap() noexcept {
        if (!is_inline()) {
            std::free(_start);
        }
    }

    // Migrate from inline to heap storage
    void migrate_to_heap(size_type new_cap) {
        Assert(is_inline(), "migrate_to_heap should only be called when inline");
        Assert(new_cap > N, "new_cap should be larger than inline capacity");

        T* old_start = _start;
        T* old_finish = _finish;
        size_type old_size = size();

        allocate_heap(new_cap);
        _finish = _start + old_size;

        if (old_size > 0) {
            (void)move_range_without_overlap(_start, old_start, old_finish);
        }
    }

    // Reallocate heap storage with existing data (grow only)
    void realloc_heap(size_type new_cap) {
        Assert(!is_inline(), "realloc_heap should only be called when on heap");
        Assert(new_cap > capacity(), "new_cap should be larger than current capacity");

        T* old_start = _start;
        T* old_finish = _finish;
        size_type old_size = size();

        allocate_heap(new_cap);
        _finish = _start + old_size;

        if (old_size > 0) {
            (void)move_range_without_overlap(_start, old_start, old_finish);
        }
        std::free(old_start);
    }

    // Shrink heap storage to fit current size
    void shrink_heap(size_type new_cap) {
        Assert(!is_inline(), "shrink_heap should only be called when on heap");
        Assert(new_cap >= size(), "new_cap should be at least current size");
        Assert(new_cap < capacity(), "new_cap should be smaller than current capacity");

        T* old_start = _start;
        T* old_finish = _finish;
        size_type old_size = size();

        _start = static_cast<T*>(std::malloc(new_cap * sizeof(T)));
        if (_start == nullptr) [[unlikely]] {
            _start = old_start;  // Restore on failure
            throw std::bad_alloc();
        }
        _edge = _start + new_cap;
        _finish = _start + old_size;

        if (old_size > 0) {
            (void)move_range_without_overlap(_start, old_start, old_finish);
        }
        std::free(old_start);
    }

    // Reallocate (handles both inline->heap and heap->larger heap)
    void realloc_with_old_data(size_type new_cap) {
        if (is_inline()) {
            migrate_to_heap(new_cap);
        } else {
            realloc_heap(new_cap);
        }
    }

    // Reallocate and emplace an element at the end
    template <typename... Args>
    void realloc_and_emplace_back(size_type new_cap, Args&&... args) {
        Assert(new_cap > size(), "new_cap should be larger than current size");

        T* old_start = _start;
        T* old_finish = _finish;
        size_type old_size = size();
        bool was_inline = is_inline();

        allocate_heap(new_cap);
        _finish = _start + old_size;
        new (_finish++) T(std::forward<Args>(args)...);

        if (old_size > 0) {
            (void)move_range_without_overlap(_start, old_start, old_finish);
        }

        if (!was_inline) {
            std::free(old_start);
        }
    }

    [[nodiscard]] auto compute_new_capacity(size_type new_size) const -> size_type {
        Assert(new_size > capacity(), "new_size should be larger than current capacity");
        if (auto next_cap = compute_next_capacity(); next_cap > new_size) {
            return next_cap;
        }
        return new_size;
    }

    [[nodiscard]] auto compute_next_capacity() const -> size_type {
        auto cap = capacity();
        if (cap < 4096 * 32 / sizeof(T) && cap >= N) [[likely]] {
            return (cap * 3 + 1) / 2;
        }
        if (cap >= 4096 * 32 / sizeof(T)) [[likely]] {
            return cap * 2;
        }
        // Initial expansion from inline: at least double the inline capacity
        return std::max(N * 2, size_type(1));
    }

   public:
    // Default constructor - uses inline storage
    constexpr small_vectra() noexcept
        : _start(reinterpret_cast<T*>(_inline_storage)),
          _finish(reinterpret_cast<T*>(_inline_storage)),
          _edge(reinterpret_cast<T*>(_inline_storage) + N) {}

    constexpr explicit small_vectra([[maybe_unused]] const Alloc& alloc) noexcept : small_vectra() {}

    // Constructor with size
    explicit small_vectra(size_type count) : small_vectra() {
        if (count > N) {
            allocate_heap(count);
        }
        _finish = _start + count;
        if (count > 0) {
            construct_range(_start, _finish);
        }
    }

    // Constructor with size and value
    small_vectra(size_type count, const T& value) : small_vectra() {
        if (count > N) {
            allocate_heap(count);
        }
        _finish = _start + count;
        if (count > 0) {
            construct_range_with_cref(_start, _finish, value);
        }
    }

    // Constructor from iterators
    template <std::forward_iterator InputIt>
    small_vectra(InputIt first, InputIt last) : small_vectra() {
        auto dist = std::distance(first, last);
        Assert(dist >= 0, "invalid iterator range");
        size_type count = static_cast<size_type>(dist);

        if (count > N) {
            allocate_heap(count);
        }
        if (count > 0) {
            copy_from_iterator(_start, first, last);
            _finish = _start + count;
        }
    }

    // Constructor from initializer list
    small_vectra(std::initializer_list<T> init) : small_vectra(init.begin(), init.end()) {}

    // Copy constructor
    small_vectra(const small_vectra& other) : small_vectra() {
        size_type other_size = other.size();
        if (other_size > N) {
            allocate_heap(other_size);
        }
        if (other_size > 0) {
            copy_range(_start, other._start, other._finish);
            _finish = _start + other_size;
        }
    }

    // Move constructor
    small_vectra(small_vectra&& other) noexcept : small_vectra() {
        if (other.is_inline()) {
            // Move elements from other's inline storage to our inline storage
            size_type other_size = other.size();
            if (other_size > 0) {
                (void)move_range_without_overlap(_start, other._start, other._finish);
                _finish = _start + other_size;
            }
            other._finish = other._start;
        } else {
            // Steal the heap pointer
            _start = other._start;
            _finish = other._finish;
            _edge = other._edge;
            // Reset other to inline state
            other._start = other.inline_ptr();
            other._finish = other._start;
            other._edge = other._start + N;
        }
    }

    // Copy assignment
    auto operator=(const small_vectra& other) -> small_vectra& {
        if (this == &other) [[unlikely]] {
            return *this;
        }

        size_type other_size = other.size();

        // Destroy current elements
        destroy_range(_start, _finish);

        if (other_size > capacity()) {
            // Need more capacity
            free_heap();
            if (other_size > N) {
                allocate_heap(other_size);
            } else {
                // Switch back to inline
                _start = inline_ptr();
                _edge = _start + N;
            }
        }

        if (other_size > 0) {
            copy_range(_start, other._start, other._finish);
        }
        _finish = _start + other_size;

        return *this;
    }

    // Move assignment
    auto operator=(small_vectra&& other) noexcept -> small_vectra& {
        if (this == &other) [[unlikely]] {
            return *this;
        }

        // Destroy current elements and free heap if needed
        destroy_range(_start, _finish);
        free_heap();

        if (other.is_inline()) {
            // Move elements from other's inline storage
            _start = inline_ptr();
            _edge = _start + N;
            size_type other_size = other.size();
            if (other_size > 0) {
                (void)move_range_without_overlap(_start, other._start, other._finish);
            }
            _finish = _start + other_size;
            other._finish = other._start;
        } else {
            // Steal heap pointer
            _start = other._start;
            _finish = other._finish;
            _edge = other._edge;
            // Reset other to inline
            other._start = other.inline_ptr();
            other._finish = other._start;
            other._edge = other._start + N;
        }

        return *this;
    }

    ~small_vectra() {
        destroy_range(_start, _finish);
        free_heap();
    }

    // Capacity
    [[nodiscard, gnu::always_inline]] constexpr auto size() const noexcept -> size_type {
        return static_cast<size_type>(_finish - _start);
    }

    [[nodiscard, gnu::always_inline]] constexpr auto capacity() const noexcept -> size_type {
        return static_cast<size_type>(_edge - _start);
    }

    [[nodiscard, gnu::always_inline]] constexpr auto empty() const noexcept -> bool { return _finish == _start; }

    [[nodiscard, gnu::always_inline]] constexpr auto max_size() const noexcept -> size_type {
        return kFastVectorMaxSize / sizeof(T);
    }

    [[nodiscard, gnu::always_inline]] auto is_small() const noexcept -> bool { return is_inline(); }

    void reserve(size_type new_cap) {
        if (new_cap > capacity()) {
            realloc_with_old_data(new_cap);
        }
    }

    void shrink_to_fit() {
        size_type sz = size();
        if (sz == capacity()) {
            return;
        }

        if (sz <= N && !is_inline()) {
            // Can move back to inline storage
            T* old_start = _start;
            T* old_finish = _finish;
            _start = inline_ptr();
            _edge = _start + N;
            if (sz > 0) {
                (void)move_range_without_overlap(_start, old_start, old_finish);
            }
            _finish = _start + sz;
            std::free(old_start);
        } else if (sz > N && sz < capacity()) {
            // Shrink heap allocation
            shrink_heap(sz);
        }
    }

    // Element access
    [[nodiscard, gnu::always_inline]] constexpr auto operator[](size_type index) noexcept -> reference {
        Assert(index < size(), "index out of range");
        return _start[index];
    }

    [[nodiscard, gnu::always_inline]] constexpr auto operator[](size_type index) const noexcept -> const_reference {
        Assert(index < size(), "index out of range");
        return _start[index];
    }

    [[nodiscard, gnu::always_inline]] constexpr auto at(size_type index) -> reference {
        Assert(index < size(), "index out of range");
        return _start[index];
    }

    [[nodiscard, gnu::always_inline]] constexpr auto at(size_type index) const -> const_reference {
        Assert(index < size(), "index out of range");
        return _start[index];
    }

    [[nodiscard, gnu::always_inline]] constexpr auto data() noexcept -> pointer { return _start; }
    [[nodiscard, gnu::always_inline]] constexpr auto data() const noexcept -> const_pointer { return _start; }

    [[nodiscard, gnu::always_inline]] constexpr auto front() noexcept -> reference {
        Assert(size() > 0, "front on empty container");
        return *_start;
    }

    [[nodiscard, gnu::always_inline]] constexpr auto front() const noexcept -> const_reference {
        Assert(size() > 0, "front on empty container");
        return *_start;
    }

    [[nodiscard, gnu::always_inline]] constexpr auto back() noexcept -> reference {
        Assert(size() > 0, "back on empty container");
        return *(_finish - 1);
    }

    [[nodiscard, gnu::always_inline]] constexpr auto back() const noexcept -> const_reference {
        Assert(size() > 0, "back on empty container");
        return *(_finish - 1);
    }

    // Iterator support (reuse vectra's iterator)
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

    [[nodiscard]] auto begin() noexcept -> iterator { return iterator(_start); }
    [[nodiscard]] auto begin() const noexcept -> const_iterator { return const_iterator(_start); }
    [[nodiscard]] auto cbegin() const noexcept -> const_iterator { return const_iterator(_start); }
    [[nodiscard]] auto end() noexcept -> iterator { return iterator(_finish); }
    [[nodiscard]] auto end() const noexcept -> const_iterator { return const_iterator(_finish); }
    [[nodiscard]] auto cend() const noexcept -> const_iterator { return const_iterator(_finish); }
    [[nodiscard]] auto rbegin() noexcept -> reverse_iterator { return std::make_reverse_iterator(end()); }
    [[nodiscard]] auto rbegin() const noexcept -> const_reverse_iterator { return std::make_reverse_iterator(end()); }
    [[nodiscard]] auto crbegin() const noexcept -> const_reverse_iterator { return std::make_reverse_iterator(cend()); }
    [[nodiscard]] auto rend() noexcept -> reverse_iterator { return std::make_reverse_iterator(begin()); }
    [[nodiscard]] auto rend() const noexcept -> const_reverse_iterator { return std::make_reverse_iterator(begin()); }
    [[nodiscard]] auto crend() const noexcept -> const_reverse_iterator { return std::make_reverse_iterator(cbegin()); }

    // Modifiers
    template <Safety safety = Safety::Safe>
    void push_back(const value_type& value) {
        if constexpr (safety == Safety::Safe) {
            if (_finish == _edge) [[unlikely]] {
                realloc_and_emplace_back(compute_next_capacity(), value);
                return;
            }
        } else {
            Assert(_finish != _edge, "push_back on full container");
            VECTRA_ASSUME(_finish < _edge);
        }
        copy_cref(_finish, value);
        ++_finish;
    }

    template <Safety safety = Safety::Safe>
    void push_back(value_type&& value) {
        if constexpr (safety == Safety::Safe) {
            if (_finish == _edge) [[unlikely]] {
                realloc_and_emplace_back(compute_next_capacity(), std::move(value));
                return;
            }
        } else {
            Assert(_finish != _edge, "push_back on full container");
            VECTRA_ASSUME(_finish < _edge);
        }
        copy_value(_finish, std::move(value));
        ++_finish;
    }

    template <Safety safety = Safety::Safe, typename... Args>
    auto emplace_back(Args&&... args) -> reference {
        if constexpr (safety == Safety::Safe) {
            if (_finish == _edge) [[unlikely]] {
                realloc_and_emplace_back(compute_next_capacity(), std::forward<Args>(args)...);
                return *(_finish - 1);
            }
        } else {
            Assert(_finish != _edge, "emplace_back on full container");
            VECTRA_ASSUME(_finish < _edge);
        }
        new (_finish) T(std::forward<Args>(args)...);
        ++_finish;
        return *(_finish - 1);
    }

    void pop_back() noexcept {
        Assert(size() > 0, "pop_back on empty container");
        --_finish;
        destroy_ptr(_finish);
    }

    void clear() noexcept {
        destroy_range(_start, _finish);
        _finish = _start;
    }

    void resize(size_type count) {
        if (count > size()) {
            if (count > capacity()) {
                realloc_with_old_data(count);
            }
            T* old_finish = _finish;
            _finish = _start + count;
            construct_range(old_finish, _finish);
        } else if (count < size()) {
            T* new_finish = _start + count;
            destroy_range(new_finish, _finish);
            _finish = new_finish;
        }
    }

    void resize(size_type count, const value_type& value) {
        if (count > size()) {
            if (count > capacity()) {
                realloc_with_old_data(count);
            }
            T* old_finish = _finish;
            _finish = _start + count;
            construct_range_with_cref(old_finish, _finish, value);
        } else if (count < size()) {
            T* new_finish = _start + count;
            destroy_range(new_finish, _finish);
            _finish = new_finish;
        }
    }

    void assign(size_type count, const value_type& value) {
        destroy_range(_start, _finish);
        if (count > capacity()) {
            free_heap();
            if (count > N) {
                allocate_heap(count);
            } else {
                _start = inline_ptr();
                _edge = _start + N;
            }
        }
        _finish = _start + count;
        if (count > 0) {
            construct_range_with_cref(_start, _finish, value);
        }
    }

    template <std::forward_iterator InputIt>
    void assign(InputIt first, InputIt last) {
        auto dist = std::distance(first, last);
        Assert(dist >= 0, "invalid iterator range");
        size_type count = static_cast<size_type>(dist);

        destroy_range(_start, _finish);
        if (count > capacity()) {
            free_heap();
            if (count > N) {
                allocate_heap(count);
            } else {
                _start = inline_ptr();
                _edge = _start + N;
            }
        }
        _finish = _start + count;
        if (count > 0) {
            copy_from_iterator(_start, first, last);
        }
    }

    void assign(std::initializer_list<T> ilist) { assign(ilist.begin(), ilist.end()); }

    // Erase operations
    auto erase(const_iterator pos) -> iterator {
        Assert(pos >= cbegin() && pos < cend(), "invalid iterator");
        T* pos_ptr = const_cast<T*>(pos.operator->());
        destroy_ptr(pos_ptr);
        move_range_forward(pos_ptr, pos_ptr + 1, _finish);
        --_finish;
        return iterator(pos_ptr);
    }

    auto erase(const_iterator first, const_iterator last) -> iterator {
        Assert(first >= cbegin() && last <= cend(), "invalid range");
        Assert(last >= first, "invalid range");
        T* first_ptr = const_cast<T*>(first.operator->());
        T* last_ptr = const_cast<T*>(last.operator->());
        if (first_ptr != last_ptr) {
            destroy_range(first_ptr, last_ptr);
            move_range_forward(first_ptr, last_ptr, _finish);
            _finish -= (last_ptr - first_ptr);
        }
        return iterator(first_ptr);
    }

    // Insert operations
    template <Safety safety = Safety::Safe>
    auto insert(const_iterator pos, const value_type& value) -> iterator {
        T* pos_ptr = const_cast<T*>(pos.operator->());
        if constexpr (safety == Safety::Safe) {
            if (_finish == _edge) [[unlikely]] {
                difference_type pos_index = pos_ptr - _start;
                reserve(compute_next_capacity());
                pos_ptr = _start + pos_index;
            }
        }
        Assert(_finish != _edge, "insert on full container");

        if (pos_ptr == _finish) {
            copy_cref(_finish++, value);
            return iterator(pos_ptr);
        }

        // Move elements backward
        if (pos_ptr < _finish - 1) {
            move_backward(pos_ptr + 1, pos_ptr, _finish);
        } else {
            move_range_forward(pos_ptr + 1, pos_ptr, _finish);
        }
        copy_cref(pos_ptr, value);
        ++_finish;
        return iterator(pos_ptr);
    }

    template <Safety safety = Safety::Safe>
    auto insert(const_iterator pos, value_type&& value) -> iterator {
        T* pos_ptr = const_cast<T*>(pos.operator->());
        if constexpr (safety == Safety::Safe) {
            if (_finish == _edge) [[unlikely]] {
                difference_type pos_index = pos_ptr - _start;
                reserve(compute_next_capacity());
                pos_ptr = _start + pos_index;
            }
        }
        Assert(_finish != _edge, "insert on full container");

        if (pos_ptr == _finish) {
            copy_value(_finish++, std::move(value));
            return iterator(pos_ptr);
        }

        if (pos_ptr < _finish - 1) {
            move_backward(pos_ptr + 1, pos_ptr, _finish);
        } else {
            move_range_forward(pos_ptr + 1, pos_ptr, _finish);
        }
        copy_value(pos_ptr, std::move(value));
        ++_finish;
        return iterator(pos_ptr);
    }

    void swap(small_vectra& other) noexcept {
        if (is_inline() && other.is_inline()) {
            // Both inline: swap elements
            size_type this_size = size();
            size_type other_size = other.size();
            size_type min_size = std::min(this_size, other_size);

            // Swap common elements
            for (size_type i = 0; i < min_size; ++i) {
                std::swap(_start[i], other._start[i]);
            }

            // Move extra elements
            if (this_size > other_size) {
                (void)move_range_without_overlap(other._start + min_size, _start + min_size, _finish);
                destroy_range(_start + min_size, _finish);
            } else if (other_size > this_size) {
                (void)move_range_without_overlap(_start + min_size, other._start + min_size, other._finish);
                destroy_range(other._start + min_size, other._finish);
            }

            _finish = _start + other_size;
            other._finish = other._start + this_size;
        } else if (!is_inline() && !other.is_inline()) {
            // Both heap: swap pointers
            std::swap(_start, other._start);
            std::swap(_finish, other._finish);
            std::swap(_edge, other._edge);
        } else {
            // One inline, one heap: move the inline one to temp, then swap
            small_vectra temp(std::move(*this));
            *this = std::move(other);
            other = std::move(temp);
        }
    }

   private:
    // Helper for backward move (handles overlap correctly)
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
auto operator==(const small_vectra<T, N>& lhs, const small_vectra<T, N>& rhs) -> bool {
    if (lhs.size() != rhs.size()) return false;
    for (std::size_t i = 0; i < lhs.size(); ++i) {
        if (lhs[i] != rhs[i]) return false;
    }
    return true;
}

template <typename T, std::size_t N>
auto operator<=>(const small_vectra<T, N>& lhs, const small_vectra<T, N>& rhs) -> std::strong_ordering {
    for (std::size_t i = 0; i < std::min(lhs.size(), rhs.size()); ++i) {
        if (lhs[i] < rhs[i]) return std::strong_ordering::less;
        if (lhs[i] > rhs[i]) return std::strong_ordering::greater;
    }
    if (lhs.size() < rhs.size()) return std::strong_ordering::less;
    if (lhs.size() > rhs.size()) return std::strong_ordering::greater;
    return std::strong_ordering::equal;
}

}  // namespace stdb::container

namespace std {

template <typename T, std::size_t N>
constexpr void swap(stdb::container::small_vectra<T, N>& lhs, stdb::container::small_vectra<T, N>& rhs) noexcept {
    lhs.swap(rhs);
}

template <typename T, std::size_t N, typename U>
constexpr auto erase(stdb::container::small_vectra<T, N>& vec, const U& value) -> std::size_t {
    auto it = std::remove(vec.begin(), vec.end(), value);
    auto count = vec.end() - it;
    vec.erase(it, vec.end());
    return count;
}

template <typename T, std::size_t N, typename Pred>
constexpr auto erase_if(stdb::container::small_vectra<T, N>& vec, Pred pred) -> std::size_t {
    auto it = std::remove_if(vec.begin(), vec.end(), pred);
    auto count = vec.end() - it;
    vec.erase(it, vec.end());
    return count;
}

}  // namespace std

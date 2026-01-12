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
#include <memory_resource>
#include <stdexcept>

#include "container_base.hpp"

namespace stdb::container {

/*
 * devectra is a double-ended vector that supports efficient insertion
 * and removal at both ends.
 *
 * Template parameters:
 *   T - Element type
 *   Alloc - Allocator type (for API compatibility, currently unused)
 *
 * Key features:
 *   - O(1) amortized push_front and push_back
 *   - O(1) pop_front and pop_back
 *   - O(1) random access
 *   - Contiguous storage (unlike std::deque)
 *   - Reserves space at both ends to minimize reallocations
 *
 * Use cases:
 *   - Double-ended queue operations
 *   - Undo/redo stacks
 *   - Sliding window with additions at both ends
 *   - When you need both push_front and push_back performance
 */
template <typename T, typename Alloc = std::allocator<T>>
class devectra
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
    using AllocTraits = std::allocator_traits<Alloc>;

   private:
    T* _buffer = nullptr;     // Start of allocated buffer
    size_type _offset = 0;    // Offset from buffer start to first element
    size_type _size = 0;      // Number of elements
    size_type _capacity = 0;  // Total buffer capacity
    [[no_unique_address]] Alloc _alloc;

    static constexpr size_type kDefaultCapacity = 8;
    // Use 2x growth factor (matches libstdc++ std::vector for better performance)
    static constexpr size_type kGrowthFactor = 2;

    [[nodiscard, gnu::always_inline]] auto data_start() noexcept -> T* { return _buffer + _offset; }

    [[nodiscard, gnu::always_inline]] auto data_start() const noexcept -> const T* { return _buffer + _offset; }

    [[nodiscard, gnu::always_inline]] auto data_end() noexcept -> T* { return _buffer + _offset + _size; }

    [[nodiscard, gnu::always_inline]] auto data_end() const noexcept -> const T* { return _buffer + _offset + _size; }

    // Space available at front (before first element)
    [[nodiscard]] auto front_spare() const noexcept -> size_type { return _offset; }

    // Space available at back (after last element)
    [[nodiscard]] auto back_spare() const noexcept -> size_type { return _capacity - _offset - _size; }

    void allocate(size_type cap) {
        _buffer = AllocTraits::allocate(_alloc, cap);
        _capacity = cap;
    }

    void deallocate() noexcept {
        if (_buffer != nullptr) {
            AllocTraits::deallocate(_alloc, _buffer, _capacity);
            _buffer = nullptr;
            _capacity = 0;
        }
    }

    // Grow the buffer, centering elements to leave space at both ends
    void grow(size_type min_capacity) {
        size_type new_cap = std::max(min_capacity, _capacity * kGrowthFactor);
        if (new_cap < kDefaultCapacity) {
            new_cap = kDefaultCapacity;
        }

        T* new_buffer = AllocTraits::allocate(_alloc, new_cap);

        // Center elements in new buffer
        size_type new_offset = (new_cap - _size) / 2;

        if (_size > 0) {
            T* old_start = data_start();
            T* old_end = data_end();
            (void)move_range_without_overlap(new_buffer + new_offset, old_start, old_end);
            destroy_range(old_start, old_end);
        }

        deallocate();
        _buffer = new_buffer;
        _offset = new_offset;
        _capacity = new_cap;
    }

    // Make room at front by shifting elements or growing
    // Optimized to avoid shifting on every alternating push_front/push_back
    // [[gnu::noinline, gnu::cold]] helps compiler optimize the hot path in push_front
    [[gnu::noinline, gnu::cold]] void make_room_front(size_type count = 1) {
        if (front_spare() >= count) {
            return;
        }

        size_type total_spare = front_spare() + back_spare();

        // Only shift if back has SIGNIFICANTLY more space than we need
        // This prevents shifting on every alternating push_front/push_back
        constexpr size_type kShiftThreshold = 4;
        if (total_spare >= count && back_spare() >= count * kShiftThreshold) {
            // Shift elements to center (not all the way to back)
            // Leave some slack on both sides for future operations
            size_type target_front = (total_spare + count) / 2;
            if (target_front < count) target_front = count;
            size_type new_offset = target_front;
            if (new_offset + _size > _capacity) new_offset = _capacity - _size;

            if (new_offset > _offset && _size > 0) {
                // Shift right
                T* src = data_start();
                T* dst = _buffer + new_offset;
                if constexpr (IsRelocatable<T>) {
                    std::memmove(dst, src, _size * sizeof(T));
                } else {
                    // Move from end to start to handle overlap
                    for (size_type i = _size; i > 0; --i) {
                        new (dst + i - 1) T(std::move(src[i - 1]));
                        if constexpr (NeedsCleanUp<T>) {
                            src[i - 1].~T();
                        }
                    }
                }
                _offset = new_offset;
            }
            return;
        }

        // Grow and center elements - leave room for both front and back operations
        size_type min_cap = count + _size + count;  // Extra slack for back too
        size_type new_cap = std::max(min_cap, _capacity * kGrowthFactor);
        if (new_cap < kDefaultCapacity) {
            new_cap = kDefaultCapacity;
        }

        T* new_buffer = AllocTraits::allocate(_alloc, new_cap);

        // Center elements in new buffer
        size_type new_offset = (new_cap - _size) / 2;
        if (new_offset < count) {
            new_offset = count;
        }

        if (_size > 0) {
            T* old_start = data_start();
            T* old_end = data_end();
            (void)move_range_without_overlap(new_buffer + new_offset, old_start, old_end);
            destroy_range(old_start, old_end);
        }

        deallocate();
        _buffer = new_buffer;
        _offset = new_offset;
        _capacity = new_cap;
    }

    // Make room at back by shifting elements or growing
    // Optimized to avoid shifting on every alternating push_front/push_back
    // [[gnu::noinline, gnu::cold]] helps compiler optimize the hot path in push_back
    [[gnu::noinline, gnu::cold]] void make_room_back(size_type count = 1) {
        if (back_spare() >= count) {
            return;
        }

        size_type total_spare = front_spare() + back_spare();

        // Only shift if front has SIGNIFICANTLY more space than we need
        constexpr size_type kShiftThreshold = 4;
        if (total_spare >= count && front_spare() >= count * kShiftThreshold) {
            // Shift elements to center (not all the way to front)
            size_type target_back = (total_spare + count) / 2;
            if (target_back < count) target_back = count;
            size_type new_offset = _capacity - _size - target_back;
            if (new_offset > _offset) new_offset = _offset;  // Don't shift right

            if (new_offset < _offset && _size > 0) {
                // Shift left
                T* src = data_start();
                T* dst = _buffer + new_offset;
                if constexpr (IsRelocatable<T>) {
                    std::memmove(dst, src, _size * sizeof(T));
                } else {
                    for (size_type i = 0; i < _size; ++i) {
                        new (dst + i) T(std::move(src[i]));
                        if constexpr (NeedsCleanUp<T>) {
                            src[i].~T();
                        }
                    }
                }
                _offset = new_offset;
            }
            return;
        }

        // Grow and center elements - leave room for both front and back operations
        size_type min_cap = count + _size + count;  // Extra slack for front too
        size_type new_cap = std::max(min_cap, _capacity * kGrowthFactor);
        if (new_cap < kDefaultCapacity) {
            new_cap = kDefaultCapacity;
        }

        T* new_buffer = AllocTraits::allocate(_alloc, new_cap);

        // Center elements in new buffer
        size_type new_offset = (new_cap - _size) / 2;
        if (new_offset + _size + count > new_cap) {
            new_offset = new_cap - _size - count;
        }

        if (_size > 0) {
            T* old_start = data_start();
            T* old_end = data_end();
            (void)move_range_without_overlap(new_buffer + new_offset, old_start, old_end);
            destroy_range(old_start, old_end);
        }

        deallocate();
        _buffer = new_buffer;
        _offset = new_offset;
        _capacity = new_cap;
    }

   public:
    // Default constructor
    constexpr devectra() noexcept = default;

    // Allocator-only constructor
    explicit devectra(const Alloc& alloc) noexcept : _alloc(alloc) {}

    // Constructor with size
    explicit devectra(size_type count, const Alloc& alloc = Alloc()) : _alloc(alloc) {
        if (count > 0) {
            allocate(count);
            _offset = 0;
            _size = count;
            construct_range(data_start(), data_end());
        }
    }

    // Constructor with size and value
    devectra(size_type count, const T& value, const Alloc& alloc = Alloc()) : _alloc(alloc) {
        if (count > 0) {
            allocate(count);
            _offset = 0;
            _size = count;
            construct_range_with_cref(data_start(), data_end(), value);
        }
    }

    // Constructor from iterators
    template <std::forward_iterator InputIt>
    devectra(InputIt first, InputIt last, const Alloc& alloc = Alloc()) : _alloc(alloc) {
        auto dist = std::distance(first, last);
        if (dist > 0) {
            size_type count = static_cast<size_type>(dist);
            allocate(count);
            _offset = 0;
            _size = count;
            copy_from_iterator(data_start(), first, last);
        }
    }

    // Constructor from initializer list
    devectra(std::initializer_list<T> init, const Alloc& alloc = Alloc())
        : devectra(init.begin(), init.end(), alloc) {}

    // Copy constructor
    devectra(const devectra& other)
        : _alloc(AllocTraits::select_on_container_copy_construction(other._alloc)) {
        if (other._size > 0) {
            allocate(other._size);
            _offset = 0;
            _size = other._size;
            copy_range(data_start(), other.data_start(), other.data_end());
        }
    }

    // Allocator-extended copy constructor
    devectra(const devectra& other, const Alloc& alloc) : _alloc(alloc) {
        if (other._size > 0) {
            allocate(other._size);
            _offset = 0;
            _size = other._size;
            copy_range(data_start(), other.data_start(), other.data_end());
        }
    }

    // Move constructor
    devectra(devectra&& other) noexcept
        : _buffer(other._buffer),
          _offset(other._offset),
          _size(other._size),
          _capacity(other._capacity),
          _alloc(std::move(other._alloc)) {
        other._buffer = nullptr;
        other._offset = 0;
        other._size = 0;
        other._capacity = 0;
    }

    // Allocator-extended move constructor
    devectra(devectra&& other, const Alloc& alloc) : _alloc(alloc) {
        if constexpr (AllocTraits::is_always_equal::value) {
            // Allocators always equal - can steal resources
            _buffer = other._buffer;
            _offset = other._offset;
            _size = other._size;
            _capacity = other._capacity;
            other._buffer = nullptr;
            other._offset = 0;
            other._size = 0;
            other._capacity = 0;
        } else if (_alloc == other._alloc) {
            // Same allocator - can steal resources
            _buffer = other._buffer;
            _offset = other._offset;
            _size = other._size;
            _capacity = other._capacity;
            other._buffer = nullptr;
            other._offset = 0;
            other._size = 0;
            other._capacity = 0;
        } else {
            // Different allocators - must move elements
            if (other._size > 0) {
                allocate(other._size);
                _offset = 0;
                _size = other._size;
                (void)move_range_without_overlap(data_start(), other.data_start(), other.data_end());
            }
        }
    }

    // Copy assignment
    auto operator=(const devectra& other) -> devectra& {
        if (this == &other) [[unlikely]] {
            return *this;
        }

        constexpr bool propagate = AllocTraits::propagate_on_container_copy_assignment::value;
        const bool alloc_equal = _alloc == other._alloc;

        destroy_range(data_start(), data_end());

        // If allocators differ and we're propagating, we must reallocate
        if constexpr (propagate) {
            if (!alloc_equal) {
                deallocate();
                _alloc = other._alloc;
                if (other._size > 0) {
                    allocate(other._size);
                }
            } else if (other._size > _capacity) {
                deallocate();
                allocate(other._size);
            }
        } else {
            if (other._size > _capacity) {
                deallocate();
                allocate(other._size);
            }
        }

        _offset = 0;
        _size = other._size;
        if (_size > 0) {
            copy_range(data_start(), other.data_start(), other.data_end());
        }
        return *this;
    }

    // Move assignment
    auto operator=(devectra&& other) noexcept(
        AllocTraits::propagate_on_container_move_assignment::value ||
        AllocTraits::is_always_equal::value) -> devectra& {
        if (this == &other) [[unlikely]] {
            return *this;
        }

        constexpr bool propagate = AllocTraits::propagate_on_container_move_assignment::value;
        constexpr bool always_equal = AllocTraits::is_always_equal::value;

        destroy_range(data_start(), data_end());

        if constexpr (propagate || always_equal) {
            // Can always steal resources
            deallocate();
            _buffer = other._buffer;
            _offset = other._offset;
            _size = other._size;
            _capacity = other._capacity;
            if constexpr (propagate) {
                _alloc = std::move(other._alloc);
            }
            other._buffer = nullptr;
            other._offset = 0;
            other._size = 0;
            other._capacity = 0;
        } else {
            // Must check allocator equality at runtime
            if (_alloc == other._alloc) {
                // Same allocator - steal resources
                deallocate();
                _buffer = other._buffer;
                _offset = other._offset;
                _size = other._size;
                _capacity = other._capacity;
                other._buffer = nullptr;
                other._offset = 0;
                other._size = 0;
                other._capacity = 0;
            } else {
                // Different allocators - move elements
                if (other._size > _capacity) {
                    deallocate();
                    allocate(other._size);
                }
                _offset = 0;
                _size = other._size;
                if (_size > 0) {
                    (void)move_range_without_overlap(data_start(), other.data_start(), other.data_end());
                }
            }
        }
        return *this;
    }

    ~devectra() {
        destroy_range(data_start(), data_end());
        deallocate();
    }

    // Capacity
    [[nodiscard, gnu::always_inline]] constexpr auto size() const noexcept -> size_type { return _size; }

    [[nodiscard, gnu::always_inline]] constexpr auto capacity() const noexcept -> size_type { return _capacity; }

    [[nodiscard, gnu::always_inline]] constexpr auto empty() const noexcept -> bool { return _size == 0; }

    [[nodiscard, gnu::always_inline]] constexpr auto max_size() const noexcept -> size_type {
        return kFastVectorMaxSize / sizeof(T);
    }

    [[nodiscard]] auto front_capacity() const noexcept -> size_type { return front_spare(); }

    [[nodiscard]] auto back_capacity() const noexcept -> size_type { return back_spare(); }

    [[nodiscard]] auto get_allocator() const noexcept -> allocator_type { return _alloc; }

    void reserve(size_type new_cap) {
        if (new_cap > _capacity) {
            grow(new_cap);
        }
    }

    void reserve_front(size_type count) {
        if (front_spare() < count) {
            make_room_front(count);
        }
    }

    void reserve_back(size_type count) {
        if (back_spare() < count) {
            make_room_back(count);
        }
    }

    void shrink_to_fit() {
        if (_size == 0) {
            deallocate();
            _offset = 0;
            return;
        }

        if (_size == _capacity && _offset == 0) {
            return;
        }

        T* new_buffer = nullptr;
        try {
            new_buffer = AllocTraits::allocate(_alloc, _size);
        } catch (...) {
            return;  // Don't throw, just keep current allocation
        }

        T* old_start = data_start();
        T* old_end = data_end();
        (void)move_range_without_overlap(new_buffer, old_start, old_end);
        destroy_range(old_start, old_end);
        deallocate();
        _buffer = new_buffer;
        _offset = 0;
        _capacity = _size;
    }

    // Element access
    [[nodiscard, gnu::always_inline]] auto operator[](size_type index) noexcept -> reference {
        Assert(index < _size, "index out of range");
        return data_start()[index];
    }

    [[nodiscard, gnu::always_inline]] auto operator[](size_type index) const noexcept -> const_reference {
        Assert(index < _size, "index out of range");
        return data_start()[index];
    }

    [[nodiscard]] auto at(size_type index) -> reference {
        if (index >= _size) [[unlikely]] {
            throw std::out_of_range("devectra::at: index out of range");
        }
        return data_start()[index];
    }

    [[nodiscard]] auto at(size_type index) const -> const_reference {
        if (index >= _size) [[unlikely]] {
            throw std::out_of_range("devectra::at: index out of range");
        }
        return data_start()[index];
    }

    [[nodiscard, gnu::always_inline]] auto data() noexcept -> pointer { return data_start(); }

    [[nodiscard, gnu::always_inline]] auto data() const noexcept -> const_pointer { return data_start(); }

    [[nodiscard, gnu::always_inline]] auto front() noexcept -> reference {
        Assert(_size > 0, "front on empty container");
        return *data_start();
    }

    [[nodiscard, gnu::always_inline]] auto front() const noexcept -> const_reference {
        Assert(_size > 0, "front on empty container");
        return *data_start();
    }

    [[nodiscard, gnu::always_inline]] auto back() noexcept -> reference {
        Assert(_size > 0, "back on empty container");
        return *(data_end() - 1);
    }

    [[nodiscard, gnu::always_inline]] auto back() const noexcept -> const_reference {
        Assert(_size > 0, "back on empty container");
        return *(data_end() - 1);
    }

    // Modifiers - Front operations
    // Optimization: inline the fast path check, only call make_room_front when needed
    void push_front(const value_type& value) {
        if (_offset == 0) [[unlikely]] {
            make_room_front(1);
        }
        --_offset;
        copy_cref(_buffer + _offset, value);
        ++_size;
    }

    void push_front(value_type&& value) {
        if (_offset == 0) [[unlikely]] {
            make_room_front(1);
        }
        --_offset;
        copy_value(_buffer + _offset, std::move(value));
        ++_size;
    }

    template <typename... Args>
    auto emplace_front(Args&&... args) -> reference {
        if (_offset == 0) [[unlikely]] {
            make_room_front(1);
        }
        --_offset;
        new (_buffer + _offset) T(std::forward<Args>(args)...);
        ++_size;
        return front();
    }

    void pop_front() noexcept {
        Assert(_size > 0, "pop_front on empty container");
        destroy_ptr(data_start());
        ++_offset;
        --_size;
    }

    // Modifiers - Back operations
    // Optimization: inline the fast path check, only call make_room_back when needed
    void push_back(const value_type& value) {
        if (back_spare() == 0) [[unlikely]] {
            make_room_back(1);
        }
        copy_cref(_buffer + _offset + _size, value);
        ++_size;
    }

    void push_back(value_type&& value) {
        if (back_spare() == 0) [[unlikely]] {
            make_room_back(1);
        }
        copy_value(_buffer + _offset + _size, std::move(value));
        ++_size;
    }

    template <typename... Args>
    auto emplace_back(Args&&... args) -> reference {
        if (back_spare() == 0) [[unlikely]] {
            make_room_back(1);
        }
        new (_buffer + _offset + _size) T(std::forward<Args>(args)...);
        ++_size;
        return back();
    }

    void pop_back() noexcept {
        Assert(_size > 0, "pop_back on empty container");
        --_size;
        destroy_ptr(data_end());
    }

    void clear() noexcept {
        destroy_range(data_start(), data_end());
        // Center the offset for future insertions
        _offset = _capacity / 2;
        _size = 0;
    }

    void resize(size_type count) {
        if (count > _size) {
            if (count > _capacity) {
                grow(count);
            } else if (_offset + count > _capacity) {
                // Need to shift left
                make_room_back(count - _size);
            }
            T* old_end = data_end();
            _size = count;
            construct_range(old_end, data_end());
        } else if (count < _size) {
            T* new_end = data_start() + count;
            destroy_range(new_end, data_end());
            _size = count;
        }
    }

    void resize(size_type count, const value_type& value) {
        if (count > _size) {
            if (count > _capacity) {
                grow(count);
            } else if (_offset + count > _capacity) {
                make_room_back(count - _size);
            }
            T* old_end = data_end();
            _size = count;
            construct_range_with_cref(old_end, data_end(), value);
        } else if (count < _size) {
            T* new_end = data_start() + count;
            destroy_range(new_end, data_end());
            _size = count;
        }
    }

    void assign(size_type count, const value_type& value) {
        destroy_range(data_start(), data_end());
        if (count > _capacity) {
            deallocate();
            allocate(count);
        }
        _offset = (_capacity - count) / 2;
        _size = count;
        if (count > 0) {
            construct_range_with_cref(data_start(), data_end(), value);
        }
    }

    template <std::forward_iterator InputIt>
    void assign(InputIt first, InputIt last) {
        auto dist = std::distance(first, last);
        Assert(dist >= 0, "invalid iterator range");
        size_type count = static_cast<size_type>(dist);

        destroy_range(data_start(), data_end());
        if (count > _capacity) {
            deallocate();
            allocate(count);
        }
        _offset = (_capacity - count) / 2;
        _size = count;
        if (count > 0) {
            copy_from_iterator(data_start(), first, last);
        }
    }

    void assign(std::initializer_list<T> ilist) { assign(ilist.begin(), ilist.end()); }

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

    [[nodiscard]] auto begin() noexcept -> iterator { return iterator(data_start()); }
    [[nodiscard]] auto begin() const noexcept -> const_iterator { return const_iterator(data_start()); }
    [[nodiscard]] auto cbegin() const noexcept -> const_iterator { return const_iterator(data_start()); }
    [[nodiscard]] auto end() noexcept -> iterator { return iterator(data_end()); }
    [[nodiscard]] auto end() const noexcept -> const_iterator { return const_iterator(data_end()); }
    [[nodiscard]] auto cend() const noexcept -> const_iterator { return const_iterator(data_end()); }
    [[nodiscard]] auto rbegin() noexcept -> reverse_iterator { return std::make_reverse_iterator(end()); }
    [[nodiscard]] auto rbegin() const noexcept -> const_reverse_iterator { return std::make_reverse_iterator(end()); }
    [[nodiscard]] auto crbegin() const noexcept -> const_reverse_iterator { return std::make_reverse_iterator(cend()); }
    [[nodiscard]] auto rend() noexcept -> reverse_iterator { return std::make_reverse_iterator(begin()); }
    [[nodiscard]] auto rend() const noexcept -> const_reverse_iterator { return std::make_reverse_iterator(begin()); }
    [[nodiscard]] auto crend() const noexcept -> const_reverse_iterator { return std::make_reverse_iterator(cbegin()); }

    // SIMD-accelerated search operations
    [[nodiscard]] auto find(const_reference value) noexcept -> iterator {
        if constexpr (simd::is_simd_comparable_v<T>) {
            std::size_t idx = simd::simd_find(data_start(), _size, value);
            return iterator(data_start() + idx);
        } else {
            for (T* p = data_start(); p != data_end(); ++p) {
                if (*p == value) return iterator(p);
            }
            return end();
        }
    }

    [[nodiscard]] auto find(const_reference value) const noexcept -> const_iterator {
        if constexpr (simd::is_simd_comparable_v<T>) {
            std::size_t idx = simd::simd_find(data_start(), _size, value);
            return const_iterator(data_start() + idx);
        } else {
            for (const T* p = data_start(); p != data_end(); ++p) {
                if (*p == value) return const_iterator(p);
            }
            return cend();
        }
    }

    [[nodiscard]] auto contains(const_reference value) const noexcept -> bool {
        if constexpr (simd::is_simd_comparable_v<T>) {
            return simd::simd_find(data_start(), _size, value) < _size;
        } else {
            for (const T* p = data_start(); p != data_end(); ++p) {
                if (*p == value) return true;
            }
            return false;
        }
    }

    [[nodiscard]] auto count(const_reference value) const noexcept -> size_type {
        size_type result = 0;
        for (const T* p = data_start(); p != data_end(); ++p) {
            if (*p == value) ++result;
        }
        return result;
    }

    // Swap
    void swap(devectra& other) noexcept(
        AllocTraits::propagate_on_container_swap::value ||
        AllocTraits::is_always_equal::value) {
        // For PMR: only swap if allocators are equal or propagate_on_container_swap is true
        // UB if neither condition is met and allocators differ (per standard)
        if constexpr (AllocTraits::propagate_on_container_swap::value) {
            std::swap(_alloc, other._alloc);
        }
        // Assert allocators are equal if not propagating (UB otherwise per standard)
        Assert(AllocTraits::propagate_on_container_swap::value ||
                   AllocTraits::is_always_equal::value || _alloc == other._alloc,
               "swap with unequal allocators is undefined behavior");
        std::swap(_buffer, other._buffer);
        std::swap(_offset, other._offset);
        std::swap(_size, other._size);
        std::swap(_capacity, other._capacity);
    }

    // Erase
    auto erase(const_iterator pos) -> iterator {
        Assert(pos >= cbegin() && pos < cend(), "invalid iterator");
        T* pos_ptr = const_cast<T*>(pos.operator->());
        destroy_ptr(pos_ptr);
        move_range_forward(pos_ptr, pos_ptr + 1, data_end());
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
            move_range_forward(first_ptr, last_ptr, data_end());
            _size -= static_cast<size_type>(last_ptr - first_ptr);
        }
        return iterator(first_ptr);
    }

    // Insert
    auto insert(const_iterator pos, const value_type& value) -> iterator {
        size_type index = pos.operator->() - data_start();
        Assert(index <= _size, "invalid iterator");

        // Decide whether to shift left or right based on position
        if (index <= _size / 2) {
            // Closer to front - shift front elements left
            make_room_front(1);
            --_offset;
            ++_size;
            T* new_pos = data_start() + index;
            if (index > 0) {
                // Move elements [0, index) one position left
                move_range_forward(data_start(), data_start() + 1, new_pos + 1);
            }
            copy_cref(new_pos, value);
            return iterator(new_pos);
        } else {
            // Closer to back - shift back elements right
            make_room_back(1);
            T* pos_ptr = data_start() + index;
            if (index < _size) {
                // Move elements [index, size) one position right
                // Use backward move to handle overlap
                T* src_end = data_end();
                T* dst_end = src_end + 1;
                if constexpr (IsRelocatable<T>) {
                    std::memmove(pos_ptr + 1, pos_ptr, (_size - index) * sizeof(T));
                } else {
                    for (T* p = src_end - 1; p >= pos_ptr; --p) {
                        new (p + 1) T(std::move(*p));
                        if constexpr (NeedsCleanUp<T>) {
                            p->~T();
                        }
                    }
                }
            }
            copy_cref(pos_ptr, value);
            ++_size;
            return iterator(pos_ptr);
        }
    }

    auto insert(const_iterator pos, value_type&& value) -> iterator {
        size_type index = pos.operator->() - data_start();
        Assert(index <= _size, "invalid iterator");

        if (index <= _size / 2) {
            make_room_front(1);
            --_offset;
            ++_size;
            T* new_pos = data_start() + index;
            if (index > 0) {
                move_range_forward(data_start(), data_start() + 1, new_pos + 1);
            }
            copy_value(new_pos, std::move(value));
            return iterator(new_pos);
        } else {
            make_room_back(1);
            T* pos_ptr = data_start() + index;
            if (index < _size) {
                if constexpr (IsRelocatable<T>) {
                    std::memmove(pos_ptr + 1, pos_ptr, (_size - index) * sizeof(T));
                } else {
                    for (T* p = data_end() - 1; p >= pos_ptr; --p) {
                        new (p + 1) T(std::move(*p));
                        if constexpr (NeedsCleanUp<T>) {
                            p->~T();
                        }
                    }
                }
            }
            copy_value(pos_ptr, std::move(value));
            ++_size;
            return iterator(pos_ptr);
        }
    }
};

// Comparison operators
template <typename T, typename Alloc>
auto operator==(const devectra<T, Alloc>& lhs, const devectra<T, Alloc>& rhs) -> bool {
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

template <typename T, typename Alloc>
auto operator<=>(const devectra<T, Alloc>& lhs, const devectra<T, Alloc>& rhs) -> std::strong_ordering {
    for (std::size_t i = 0; i < std::min(lhs.size(), rhs.size()); ++i) {
        if (lhs[i] < rhs[i]) return std::strong_ordering::less;
        if (lhs[i] > rhs[i]) return std::strong_ordering::greater;
    }
    if (lhs.size() < rhs.size()) return std::strong_ordering::less;
    if (lhs.size() > rhs.size()) return std::strong_ordering::greater;
    return std::strong_ordering::equal;
}

}  // namespace stdb::container

// PMR type alias
namespace stdb::pmr {
template <typename T>
using devectra = container::devectra<T, std::pmr::polymorphic_allocator<T>>;
}  // namespace stdb::pmr

namespace std {

template <typename T, typename Alloc>
constexpr void swap(stdb::container::devectra<T, Alloc>& lhs,
                    stdb::container::devectra<T, Alloc>& rhs) noexcept(noexcept(lhs.swap(rhs))) {
    lhs.swap(rhs);
}

template <typename T, typename Alloc, typename U>
constexpr auto erase(stdb::container::devectra<T, Alloc>& vec, const U& value) -> std::size_t {
    auto it = std::remove(vec.begin(), vec.end(), value);
    auto count = vec.end() - it;
    vec.erase(it, vec.end());
    return count;
}

template <typename T, typename Alloc, typename Pred>
constexpr auto erase_if(stdb::container::devectra<T, Alloc>& vec, Pred pred) -> std::size_t {
    auto it = std::remove_if(vec.begin(), vec.end(), pred);
    auto count = vec.end() - it;
    vec.erase(it, vec.end());
    return count;
}

}  // namespace std

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
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <new>
#include <stdexcept>
#include <type_traits>

// SIMD detection
#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#if defined(__AVX2__)
#define BOOLVEC_HAS_AVX2 1
#elif defined(__SSE4_1__)
#define BOOLVEC_HAS_SSE4 1
#elif defined(__SSE2__)
#define BOOLVEC_HAS_SSE2 1
#endif
#elif defined(__aarch64__) || defined(_M_ARM64)
#include <arm_neon.h>
#define BOOLVEC_HAS_NEON 1
#endif

#ifndef BOOLVEC_ASSERT
#include <cassert>
#define BOOLVEC_ASSERT(expr, msg) assert((expr) && (msg))
#endif

// Compiler hints for optimization
#if defined(__GNUC__) || defined(__clang__)
#define BOOLVEC_ALWAYS_INLINE [[gnu::always_inline]] inline
#define BOOLVEC_RESTRICT __restrict__
#define BOOLVEC_LIKELY(x) __builtin_expect(!!(x), 1)
#define BOOLVEC_UNLIKELY(x) __builtin_expect(!!(x), 0)
#define BOOLVEC_PREFETCH(addr) __builtin_prefetch(addr)
#else
#define BOOLVEC_ALWAYS_INLINE inline
#define BOOLVEC_RESTRICT
#define BOOLVEC_LIKELY(x) (x)
#define BOOLVEC_UNLIKELY(x) (x)
#define BOOLVEC_PREFETCH(addr)
#endif

namespace stdb::container {

// =============================================================================
// boolean_vector: A high-performance bool vector with SBO and SIMD
// =============================================================================

class boolean_vector {
public:
    using value_type = bool;
    using size_type = size_t;
    using difference_type = ptrdiff_t;
    using reference = bool&;
    using const_reference = const bool&;
    using pointer = bool*;
    using const_pointer = const bool*;
    using iterator = bool*;
    using const_iterator = const bool*;
    using reverse_iterator = std::reverse_iterator<iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;

private:
    // Small Buffer Optimization: store small vectors inline
    static constexpr size_t kAlignment = 64;
    static constexpr size_t kSBOCapacity = 64;  // Inline buffer for <= 64 bools

    alignas(kAlignment) bool _sbo_buffer[kSBOCapacity];
    bool* _data;          // Always points to current data (SBO or heap)
    size_type _size = 0;
    size_type _capacity = kSBOCapacity;

    [[nodiscard]] BOOLVEC_ALWAYS_INLINE bool is_sbo() const noexcept {
        return _data == _sbo_buffer;
    }

    [[nodiscard]] static constexpr size_type grow_capacity(size_type current_cap) noexcept {
        return current_cap < 64 ? 64 : current_cap + current_cap / 2;
    }

    [[nodiscard]] static bool* allocate_heap(size_type count) {
        size_type bytes = count * sizeof(bool);
        bytes = (bytes + kAlignment - 1) & ~(kAlignment - 1);
        void* ptr = std::aligned_alloc(kAlignment, bytes);
        if (!ptr) throw std::bad_alloc();
        return static_cast<bool*>(ptr);
    }

    static void deallocate_heap(bool* ptr) noexcept {
        std::free(ptr);
    }

    void grow_to_fit(size_type new_size) {
        if (BOOLVEC_UNLIKELY(new_size > _capacity)) {
            size_type new_cap = _capacity;
            while (new_cap < new_size) {
                new_cap = grow_capacity(new_cap);
            }
            reserve(new_cap);
        }
    }

public:
    // -------------------------------------------------------------------------
    // Constructors / Destructor
    // -------------------------------------------------------------------------

    boolean_vector() noexcept : _data(_sbo_buffer) {}

    explicit boolean_vector(size_type count, bool value = false) : _data(_sbo_buffer) {
        if (count > 0) {
            if (count > kSBOCapacity) {
                _capacity = grow_capacity(count);
                _data = allocate_heap(_capacity);
            }
            _size = count;
            std::memset(_data, value ? 1 : 0, count);
        }
    }

    boolean_vector(std::initializer_list<bool> init) : boolean_vector() {
        reserve(init.size());
        for (bool v : init) {
            push_back(v);
        }
    }

    template <typename InputIt, typename = std::enable_if_t<
        std::is_base_of_v<std::input_iterator_tag,
            typename std::iterator_traits<InputIt>::iterator_category>>>
    boolean_vector(InputIt first, InputIt last) : boolean_vector() {
        if constexpr (std::is_base_of_v<std::random_access_iterator_tag,
                          typename std::iterator_traits<InputIt>::iterator_category>) {
            reserve(std::distance(first, last));
        }
        for (; first != last; ++first) {
            push_back(static_cast<bool>(*first));
        }
    }

    boolean_vector(const boolean_vector& other) : _data(_sbo_buffer) {
        if (other._size > 0) {
            if (other._size > kSBOCapacity) {
                _capacity = other._capacity;
                _data = allocate_heap(_capacity);
            }
            _size = other._size;
            std::memcpy(_data, other._data, _size * sizeof(bool));
        }
    }

    boolean_vector(boolean_vector&& other) noexcept : _data(_sbo_buffer) {
        if (other.is_sbo()) {
            _size = other._size;
            std::memcpy(_sbo_buffer, other._sbo_buffer, _size);
        } else {
            _data = other._data;
            _size = other._size;
            _capacity = other._capacity;
            other._data = other._sbo_buffer;
            other._capacity = kSBOCapacity;
        }
        other._size = 0;
    }

    ~boolean_vector() {
        if (!is_sbo()) {
            deallocate_heap(_data);
        }
    }

    boolean_vector& operator=(const boolean_vector& other) {
        if (this != &other) {
            if (other._size > _capacity) {
                if (!is_sbo()) {
                    deallocate_heap(_data);
                }
                _capacity = other._capacity > kSBOCapacity ? other._capacity : kSBOCapacity;
                if (_capacity > kSBOCapacity) {
                    _data = allocate_heap(_capacity);
                } else {
                    _data = _sbo_buffer;
                }
            }
            _size = other._size;
            if (_size > 0) {
                std::memcpy(_data, other._data, _size * sizeof(bool));
            }
        }
        return *this;
    }

    boolean_vector& operator=(boolean_vector&& other) noexcept {
        if (this != &other) {
            if (!is_sbo()) {
                deallocate_heap(_data);
            }
            if (other.is_sbo()) {
                _data = _sbo_buffer;
                _capacity = kSBOCapacity;
                _size = other._size;
                std::memcpy(_sbo_buffer, other._sbo_buffer, _size);
            } else {
                _data = other._data;
                _size = other._size;
                _capacity = other._capacity;
                other._data = other._sbo_buffer;
                other._capacity = kSBOCapacity;
            }
            other._size = 0;
        }
        return *this;
    }

    boolean_vector& operator=(std::initializer_list<bool> init) {
        clear();
        reserve(init.size());
        for (bool v : init) {
            push_back(v);
        }
        return *this;
    }

    void assign(size_type count, bool value) {
        clear();
        resize(count, value);
    }

    template <typename InputIt, typename = std::enable_if_t<
        std::is_base_of_v<std::input_iterator_tag,
            typename std::iterator_traits<InputIt>::iterator_category>>>
    void assign(InputIt first, InputIt last) {
        clear();
        if constexpr (std::is_base_of_v<std::random_access_iterator_tag,
                          typename std::iterator_traits<InputIt>::iterator_category>) {
            reserve(std::distance(first, last));
        }
        for (; first != last; ++first) {
            push_back(static_cast<bool>(*first));
        }
    }

    void assign(std::initializer_list<bool> init) {
        *this = init;
    }

    // -------------------------------------------------------------------------
    // Element access - zero overhead (direct pointer access)
    // -------------------------------------------------------------------------

    [[nodiscard]] BOOLVEC_ALWAYS_INLINE reference operator[](size_type pos) noexcept {
        BOOLVEC_ASSERT(pos < _size, "index out of range");
        return _data[pos];
    }

    [[nodiscard]] BOOLVEC_ALWAYS_INLINE const_reference operator[](size_type pos) const noexcept {
        BOOLVEC_ASSERT(pos < _size, "index out of range");
        return _data[pos];
    }

    [[nodiscard]] reference at(size_type pos) {
        if (BOOLVEC_UNLIKELY(pos >= _size)) {
            throw std::out_of_range("boolean_vector::at");
        }
        return _data[pos];
    }

    [[nodiscard]] const_reference at(size_type pos) const {
        if (BOOLVEC_UNLIKELY(pos >= _size)) {
            throw std::out_of_range("boolean_vector::at");
        }
        return _data[pos];
    }

    [[nodiscard]] BOOLVEC_ALWAYS_INLINE reference front() noexcept {
        BOOLVEC_ASSERT(_size > 0, "front() on empty vector");
        return _data[0];
    }

    [[nodiscard]] BOOLVEC_ALWAYS_INLINE const_reference front() const noexcept {
        BOOLVEC_ASSERT(_size > 0, "front() on empty vector");
        return _data[0];
    }

    [[nodiscard]] BOOLVEC_ALWAYS_INLINE reference back() noexcept {
        BOOLVEC_ASSERT(_size > 0, "back() on empty vector");
        return _data[_size - 1];
    }

    [[nodiscard]] BOOLVEC_ALWAYS_INLINE const_reference back() const noexcept {
        BOOLVEC_ASSERT(_size > 0, "back() on empty vector");
        return _data[_size - 1];
    }

    [[nodiscard]] BOOLVEC_ALWAYS_INLINE pointer data() noexcept { return _data; }
    [[nodiscard]] BOOLVEC_ALWAYS_INLINE const_pointer data() const noexcept { return _data; }

    // -------------------------------------------------------------------------
    // Iterators
    // -------------------------------------------------------------------------

    [[nodiscard]] BOOLVEC_ALWAYS_INLINE iterator begin() noexcept { return _data; }
    [[nodiscard]] BOOLVEC_ALWAYS_INLINE const_iterator begin() const noexcept { return _data; }
    [[nodiscard]] BOOLVEC_ALWAYS_INLINE const_iterator cbegin() const noexcept { return _data; }

    [[nodiscard]] BOOLVEC_ALWAYS_INLINE iterator end() noexcept { return _data + _size; }
    [[nodiscard]] BOOLVEC_ALWAYS_INLINE const_iterator end() const noexcept { return _data + _size; }
    [[nodiscard]] BOOLVEC_ALWAYS_INLINE const_iterator cend() const noexcept { return _data + _size; }

    [[nodiscard]] reverse_iterator rbegin() noexcept { return reverse_iterator(end()); }
    [[nodiscard]] const_reverse_iterator rbegin() const noexcept { return const_reverse_iterator(end()); }
    [[nodiscard]] const_reverse_iterator crbegin() const noexcept { return const_reverse_iterator(cend()); }

    [[nodiscard]] reverse_iterator rend() noexcept { return reverse_iterator(begin()); }
    [[nodiscard]] const_reverse_iterator rend() const noexcept { return const_reverse_iterator(begin()); }
    [[nodiscard]] const_reverse_iterator crend() const noexcept { return const_reverse_iterator(cbegin()); }

    // -------------------------------------------------------------------------
    // Capacity
    // -------------------------------------------------------------------------

    [[nodiscard]] BOOLVEC_ALWAYS_INLINE bool empty() const noexcept { return _size == 0; }
    [[nodiscard]] BOOLVEC_ALWAYS_INLINE size_type size() const noexcept { return _size; }
    [[nodiscard]] size_type max_size() const noexcept { return std::numeric_limits<size_type>::max(); }
    [[nodiscard]] BOOLVEC_ALWAYS_INLINE size_type capacity() const noexcept { return _capacity; }

    void reserve(size_type new_cap) {
        if (new_cap > _capacity) {
            bool* new_data = allocate_heap(new_cap);
            if (_size > 0) {
                std::memcpy(new_data, _data, _size * sizeof(bool));
            }
            if (!is_sbo()) {
                deallocate_heap(_data);
            }
            _data = new_data;
            _capacity = new_cap;
        }
    }

    void shrink_to_fit() {
        if (_size == 0) {
            if (!is_sbo()) {
                deallocate_heap(_data);
                _data = _sbo_buffer;
                _capacity = kSBOCapacity;
            }
            return;
        }
        if (_size <= kSBOCapacity && !is_sbo()) {
            // Move back to SBO
            bool* heap = _data;
            std::memcpy(_sbo_buffer, heap, _size);
            deallocate_heap(heap);
            _data = _sbo_buffer;
            _capacity = kSBOCapacity;
        } else if (_size < _capacity && _size > kSBOCapacity) {
            bool* new_data = allocate_heap(_size);
            std::memcpy(new_data, _data, _size * sizeof(bool));
            deallocate_heap(_data);
            _data = new_data;
            _capacity = _size;
        }
    }

    // -------------------------------------------------------------------------
    // Modifiers
    // -------------------------------------------------------------------------

    BOOLVEC_ALWAYS_INLINE void clear() noexcept {
        _size = 0;
    }

    iterator insert(const_iterator pos, bool value) {
        size_type index = pos - _data;
        BOOLVEC_ASSERT(index <= _size, "insert position out of range");

        grow_to_fit(_size + 1);

        if (index < _size) {
            std::memmove(_data + index + 1, _data + index, (_size - index) * sizeof(bool));
        }

        _data[index] = value;
        ++_size;
        return _data + index;
    }

    iterator insert(const_iterator pos, size_type count, bool value) {
        if (count == 0) return const_cast<iterator>(pos);

        size_type index = pos - _data;
        BOOLVEC_ASSERT(index <= _size, "insert position out of range");

        grow_to_fit(_size + count);

        if (index < _size) {
            std::memmove(_data + index + count, _data + index, (_size - index) * sizeof(bool));
        }

        std::memset(_data + index, value ? 1 : 0, count);
        _size += count;
        return _data + index;
    }

    template <typename InputIt, typename = std::enable_if_t<
        std::is_base_of_v<std::input_iterator_tag,
            typename std::iterator_traits<InputIt>::iterator_category>>>
    iterator insert(const_iterator pos, InputIt first, InputIt last) {
        size_type index = pos - _data;
        size_type orig_index = index;
        for (auto it = first; it != last; ++it) {
            insert(_data + index, static_cast<bool>(*it));
            ++index;
        }
        return _data + orig_index;
    }

    iterator insert(const_iterator pos, std::initializer_list<bool> init) {
        return insert(pos, init.begin(), init.end());
    }

    iterator erase(const_iterator pos) {
        size_type index = pos - _data;
        BOOLVEC_ASSERT(index < _size, "erase position out of range");

        if (index + 1 < _size) {
            std::memmove(_data + index, _data + index + 1, (_size - index - 1) * sizeof(bool));
        }
        --_size;
        return _data + index;
    }

    iterator erase(const_iterator first, const_iterator last) {
        size_type start = first - _data;
        size_type count = last - first;
        if (count == 0) return _data + start;

        if (start + count < _size) {
            std::memmove(_data + start, _data + start + count, (_size - start - count) * sizeof(bool));
        }
        _size -= count;
        return _data + start;
    }

    BOOLVEC_ALWAYS_INLINE void push_back(bool value) {
        grow_to_fit(_size + 1);
        _data[_size] = value;
        ++_size;
    }

    template <typename... Args>
    reference emplace_back(Args&&... args) {
        push_back(bool(std::forward<Args>(args)...));
        return back();
    }

    BOOLVEC_ALWAYS_INLINE void pop_back() noexcept {
        BOOLVEC_ASSERT(_size > 0, "pop_back() on empty vector");
        --_size;
    }

    void resize(size_type count) {
        resize(count, false);
    }

    void resize(size_type count, bool value) {
        if (count > _size) {
            grow_to_fit(count);
            std::memset(_data + _size, value ? 1 : 0, count - _size);
        }
        _size = count;
    }

    void swap(boolean_vector& other) noexcept {
        if (is_sbo() && other.is_sbo()) {
            // Both SBO - swap inline data
            bool tmp[kSBOCapacity];
            std::memcpy(tmp, _sbo_buffer, _size);
            std::memcpy(_sbo_buffer, other._sbo_buffer, other._size);
            std::memcpy(other._sbo_buffer, tmp, _size);
            std::swap(_size, other._size);
        } else if (!is_sbo() && !other.is_sbo()) {
            // Both heap - swap pointers
            std::swap(_data, other._data);
            std::swap(_size, other._size);
            std::swap(_capacity, other._capacity);
        } else {
            // Mixed - use move semantics
            boolean_vector tmp(std::move(*this));
            *this = std::move(other);
            other = std::move(tmp);
        }
    }

    // -------------------------------------------------------------------------
    // boolean_vector specific operations - SIMD optimized
    // -------------------------------------------------------------------------

    void flip() noexcept {
        bool* BOOLVEC_RESTRICT p = _data;
        size_type i = 0;

#if defined(BOOLVEC_HAS_AVX2)
        const __m256i ones = _mm256_set1_epi8(1);
        for (; i + 32 <= _size; i += 32) {
            __m256i v = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(p + i));
            v = _mm256_xor_si256(v, ones);
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(p + i), v);
        }
#elif defined(BOOLVEC_HAS_SSE4) || defined(BOOLVEC_HAS_SSE2)
        const __m128i ones = _mm_set1_epi8(1);
        for (; i + 16 <= _size; i += 16) {
            __m128i v = _mm_loadu_si128(reinterpret_cast<const __m128i*>(p + i));
            v = _mm_xor_si128(v, ones);
            _mm_storeu_si128(reinterpret_cast<__m128i*>(p + i), v);
        }
#elif defined(BOOLVEC_HAS_NEON)
        const uint8x16_t ones = vdupq_n_u8(1);
        for (; i + 16 <= _size; i += 16) {
            uint8x16_t v = vld1q_u8(reinterpret_cast<const uint8_t*>(p + i));
            v = veorq_u8(v, ones);
            vst1q_u8(reinterpret_cast<uint8_t*>(p + i), v);
        }
#endif
        for (; i < _size; ++i) {
            p[i] = !p[i];
        }
    }

    [[nodiscard]] BOOLVEC_ALWAYS_INLINE bool test(size_type pos) const noexcept {
        BOOLVEC_ASSERT(pos < _size, "index out of range");
        return _data[pos];
    }

    [[nodiscard]] size_type count() const noexcept {
        const bool* BOOLVEC_RESTRICT p = _data;
        size_type total = 0;
        size_type i = 0;

        // The vector accumulators below add the bytes with add_epi8, so every lane is 8 bits wide.
        // Each bool contributes 0 or 1, so a lane holds at most 255 after 255 iterations and the
        // *256th* wraps it silently to zero -- count() then under-reports by 256 per wrapped lane.
        // On an all-true vector that first bites at 256 * 16 = 4096 elements on SSE and 256 * 32 =
        // 8192 on AVX2. Fold the byte accumulator into 64-bit lanes with sad_epu8 once per block of
        // 255 iterations, before it can get there, and keep the running total in those 64-bit lanes.
        // (NEON below is already safe: it widens to 64 bits every iteration.)
        //
        // Folding every iteration instead of every 255th is simpler and also correct, but measurably
        // slower -- 32 GB/s vs 43 on SSE4 and 64 vs 75-91 on AVX2, on a 4M-element vector.
        // maybe_unused: a platform with neither the x86 nor the NEON path falls through to the
        // scalar loop below and never reads this, and -Wunused-variable is an error here.
        [[maybe_unused]] constexpr size_type kFoldEvery = 255;

#if defined(BOOLVEC_HAS_AVX2)
        constexpr size_type kStep = 32;
        const __m256i zero = _mm256_setzero_si256();
        __m256i total64 = zero;
        while (i + kStep <= _size) {
            size_type block_end = i + kFoldEvery * kStep;
            if (block_end > _size) {
                block_end = _size;
            }
            __m256i acc = zero;
            for (; i + kStep <= block_end; i += kStep) {
                acc = _mm256_add_epi8(acc, _mm256_loadu_si256(reinterpret_cast<const __m256i*>(p + i)));
            }
            total64 = _mm256_add_epi64(total64, _mm256_sad_epu8(acc, zero));
        }
        alignas(32) uint64_t tmp[4];
        _mm256_store_si256(reinterpret_cast<__m256i*>(tmp), total64);
        total = tmp[0] + tmp[1] + tmp[2] + tmp[3];
#elif defined(BOOLVEC_HAS_SSE4) || defined(BOOLVEC_HAS_SSE2)
        // One body for both: every intrinsic here is SSE2, and SSE4 gained nothing but a shorter
        // horizontal extract, which is not worth a second copy of the loop.
        constexpr size_type kStep = 16;
        const __m128i zero = _mm_setzero_si128();
        __m128i total64 = zero;
        while (i + kStep <= _size) {
            size_type block_end = i + kFoldEvery * kStep;
            if (block_end > _size) {
                block_end = _size;
            }
            __m128i acc = zero;
            for (; i + kStep <= block_end; i += kStep) {
                acc = _mm_add_epi8(acc, _mm_loadu_si128(reinterpret_cast<const __m128i*>(p + i)));
            }
            total64 = _mm_add_epi64(total64, _mm_sad_epu8(acc, zero));
        }
        alignas(16) uint64_t tmp[2];
        _mm_store_si128(reinterpret_cast<__m128i*>(tmp), total64);
        total = tmp[0] + tmp[1];
#elif defined(BOOLVEC_HAS_NEON)
        uint64x2_t acc = vdupq_n_u64(0);
        for (; i + 16 <= _size; i += 16) {
            uint8x16_t v = vld1q_u8(reinterpret_cast<const uint8_t*>(p + i));
            uint16x8_t sum16 = vpaddlq_u8(v);
            uint32x4_t sum32 = vpaddlq_u16(sum16);
            uint64x2_t sum64 = vpaddlq_u32(sum32);
            acc = vaddq_u64(acc, sum64);
        }
        total = vgetq_lane_u64(acc, 0) + vgetq_lane_u64(acc, 1);
#endif
        for (; i < _size; ++i) {
            total += p[i];
        }
        return total;
    }

    [[nodiscard]] bool any() const noexcept {
        const bool* BOOLVEC_RESTRICT p = _data;
        size_type i = 0;

#if defined(BOOLVEC_HAS_AVX2)
        for (; i + 32 <= _size; i += 32) {
            __m256i v = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(p + i));
            if (!_mm256_testz_si256(v, v)) return true;
        }
#elif defined(BOOLVEC_HAS_SSE4)
        for (; i + 16 <= _size; i += 16) {
            __m128i v = _mm_loadu_si128(reinterpret_cast<const __m128i*>(p + i));
            if (!_mm_testz_si128(v, v)) return true;
        }
#elif defined(BOOLVEC_HAS_SSE2)
        const __m128i zeros = _mm_setzero_si128();
        for (; i + 16 <= _size; i += 16) {
            __m128i v = _mm_loadu_si128(reinterpret_cast<const __m128i*>(p + i));
            __m128i cmp = _mm_cmpeq_epi8(v, zeros);
            if (_mm_movemask_epi8(cmp) != 0xFFFF) return true;
        }
#elif defined(BOOLVEC_HAS_NEON)
        for (; i + 16 <= _size; i += 16) {
            uint8x16_t v = vld1q_u8(reinterpret_cast<const uint8_t*>(p + i));
            if (vmaxvq_u8(v) != 0) return true;
        }
#endif
        for (; i < _size; ++i) {
            if (p[i]) return true;
        }
        return false;
    }

    [[nodiscard]] bool all() const noexcept {
        const bool* BOOLVEC_RESTRICT p = _data;
        size_type i = 0;

#if defined(BOOLVEC_HAS_AVX2)
        const __m256i ones = _mm256_set1_epi8(1);
        for (; i + 32 <= _size; i += 32) {
            __m256i v = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(p + i));
            __m256i cmp = _mm256_cmpeq_epi8(v, ones);
            if (_mm256_movemask_epi8(cmp) != -1) return false;
        }
#elif defined(BOOLVEC_HAS_SSE4) || defined(BOOLVEC_HAS_SSE2)
        const __m128i ones = _mm_set1_epi8(1);
        for (; i + 16 <= _size; i += 16) {
            __m128i v = _mm_loadu_si128(reinterpret_cast<const __m128i*>(p + i));
            __m128i cmp = _mm_cmpeq_epi8(v, ones);
            if (_mm_movemask_epi8(cmp) != 0xFFFF) return false;
        }
#elif defined(BOOLVEC_HAS_NEON)
        for (; i + 16 <= _size; i += 16) {
            uint8x16_t v = vld1q_u8(reinterpret_cast<const uint8_t*>(p + i));
            if (vminvq_u8(v) == 0) return false;
        }
#endif
        for (; i < _size; ++i) {
            if (!p[i]) return false;
        }
        return true;
    }

    [[nodiscard]] bool none() const noexcept {
        return !any();
    }

    // -------------------------------------------------------------------------
    // Comparison operators
    // -------------------------------------------------------------------------

    [[nodiscard]] bool operator==(const boolean_vector& other) const noexcept {
        if (_size != other._size) return false;
        return std::memcmp(_data, other._data, _size * sizeof(bool)) == 0;
    }

    [[nodiscard]] bool operator!=(const boolean_vector& other) const noexcept {
        return !(*this == other);
    }

    [[nodiscard]] bool operator<(const boolean_vector& other) const noexcept {
        size_type min_size = std::min(_size, other._size);
        int cmp = std::memcmp(_data, other._data, min_size * sizeof(bool));
        if (cmp != 0) return cmp < 0;
        return _size < other._size;
    }

    [[nodiscard]] bool operator<=(const boolean_vector& other) const noexcept {
        return !(other < *this);
    }

    [[nodiscard]] bool operator>(const boolean_vector& other) const noexcept {
        return other < *this;
    }

    [[nodiscard]] bool operator>=(const boolean_vector& other) const noexcept {
        return !(*this < other);
    }
};

inline void swap(boolean_vector& lhs, boolean_vector& rhs) noexcept {
    lhs.swap(rhs);
}

}  // namespace stdb::container

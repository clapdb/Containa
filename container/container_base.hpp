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

#include <cassert>
#include <compare>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

namespace stdb::container {

// Safety mode for container operations
enum class Safety { Safe, Unsafe };

// Maximum size for containers (to avoid overflow issues)
inline constexpr std::size_t kFastVectorMaxSize = std::size_t{1} << 48;

// Type traits - use is_trivially_copyable for aggressive optimization
template <typename T>
inline constexpr bool IsRelocatable = std::is_trivially_copyable_v<T>;

template <typename T>
inline constexpr bool NeedsCleanUp = !std::is_trivially_destructible_v<T>;

// Assert macro - completely removed in release builds
#ifdef NDEBUG
#define Assert(cond, msg) ((void)0)
#else
#define Assert(cond, msg) assert((cond) && (msg))
#endif

// Compiler hint for optimization
#if defined(__GNUC__) || defined(__clang__)
#define VECTRA_ASSUME(cond) \
    do {                    \
        if (!(cond))        \
            __builtin_unreachable(); \
    } while (0)
#define VECTRA_ALWAYS_INLINE [[gnu::always_inline]] inline
#elif defined(_MSC_VER)
#define VECTRA_ASSUME(cond) __assume(cond)
#define VECTRA_ALWAYS_INLINE __forceinline
#else
#define VECTRA_ASSUME(cond) ((void)0)
#define VECTRA_ALWAYS_INLINE inline
#endif

// ============================================================================
// Optimized helper functions
// ============================================================================

// Value construct a range of elements (zero-initialize for POD types)
template <typename T>
VECTRA_ALWAYS_INLINE void construct_range(T* first, T* last) {
    if constexpr (std::is_trivially_default_constructible_v<T>) {
        // Zero-initialize for trivial types (matches std::vector behavior)
        std::memset(first, 0, static_cast<std::size_t>(last - first) * sizeof(T));
    } else {
        std::uninitialized_value_construct(first, last);
    }
}

// Construct a range with a value
template <typename T>
VECTRA_ALWAYS_INLINE void construct_range_with_cref(T* first, T* last, const T& value) {
    if constexpr (sizeof(T) == 1 && std::is_trivially_copyable_v<T>) {
        // Use memset for single-byte trivial types
        std::memset(first, *reinterpret_cast<const unsigned char*>(&value),
                    static_cast<std::size_t>(last - first));
    } else {
        std::uninitialized_fill(first, last, value);
    }
}

// Destroy a range of elements - no-op for trivially destructible
template <typename T>
VECTRA_ALWAYS_INLINE void destroy_range(T* first, T* last) {
    if constexpr (NeedsCleanUp<T>) {
        std::destroy(first, last);
    }
    // No-op for trivially destructible types
}

// Destroy a single element - no-op for trivially destructible
template <typename T>
VECTRA_ALWAYS_INLINE void destroy_ptr(T* ptr) {
    if constexpr (NeedsCleanUp<T>) {
        ptr->~T();
    }
    // No-op for trivially destructible types
}

// Copy construct at a location - optimized for trivial types
template <typename T>
VECTRA_ALWAYS_INLINE void copy_cref(T* dst, const T& value) {
    if constexpr (std::is_trivially_copyable_v<T>) {
        *dst = value;
    } else {
        new (dst) T(value);
    }
}

// Move construct at a location - optimized for trivial types
template <typename T>
VECTRA_ALWAYS_INLINE void copy_value(T* dst, T&& value) {
    if constexpr (std::is_trivially_copyable_v<T>) {
        *dst = value;
    } else {
        new (dst) T(std::move(value));
    }
}

// Copy a range to uninitialized memory (no overlap)
template <typename T>
VECTRA_ALWAYS_INLINE void copy_range(T* dst, const T* src_first, const T* src_last) {
    if constexpr (std::is_trivially_copyable_v<T>) {
        std::memcpy(dst, src_first, static_cast<std::size_t>(src_last - src_first) * sizeof(T));
    } else {
        std::uninitialized_copy(src_first, src_last, dst);
    }
}

// Copy from iterator to uninitialized memory
template <typename T, typename InputIt>
VECTRA_ALWAYS_INLINE void copy_from_iterator(T* dst, InputIt first, InputIt last) {
    if constexpr (std::is_trivially_copyable_v<T> &&
                  std::is_same_v<InputIt, const T*>) {
        std::memcpy(dst, first, static_cast<std::size_t>(last - first) * sizeof(T));
    } else {
        std::uninitialized_copy(first, last, dst);
    }
}

// Move a range to uninitialized memory (no overlap)
// Source objects are left in moved-from state, caller must destroy them if needed
// Returns pointer past last destination element
template <typename T>
VECTRA_ALWAYS_INLINE T* move_range_without_overlap(T* dst, T* src_first, T* src_last) {
    std::size_t count = static_cast<std::size_t>(src_last - src_first);
    if constexpr (IsRelocatable<T>) {
        std::memcpy(dst, src_first, count * sizeof(T));
        return dst + count;
    } else {
        T* d = dst;
        for (T* s = src_first; s != src_last; ++s, ++d) {
            new (d) T(std::move(*s));
        }
        return d;
    }
}

// Move a range forward (handles overlap when dst < src, destroys source)
template <typename T>
VECTRA_ALWAYS_INLINE void move_range_forward(T* dst, T* src_first, T* src_last) {
    if (src_first == src_last) return;

    if constexpr (IsRelocatable<T>) {
        std::size_t count = static_cast<std::size_t>(src_last - src_first);
        std::memmove(dst, src_first, count * sizeof(T));
    } else {
        // Forward move for overlapping ranges where dst < src
        T* d = dst;
        for (T* s = src_first; s != src_last; ++s, ++d) {
            new (d) T(std::move(*s));
            if constexpr (NeedsCleanUp<T>) {
                s->~T();
            }
        }
    }
}

// ============================================================================
// SIMD namespace for optimized operations
// ============================================================================
namespace simd {

template <typename T>
inline constexpr bool is_simd_comparable_v =
    std::is_integral_v<T> && (sizeof(T) == 1 || sizeof(T) == 2 || sizeof(T) == 4 || sizeof(T) == 8);

// Linear search fallback - can be replaced with SIMD implementation
template <typename T>
VECTRA_ALWAYS_INLINE std::size_t simd_find(const T* data, std::size_t size, const T& value) {
    for (std::size_t i = 0; i < size; ++i) {
        if (data[i] == value) return i;
    }
    return size;
}

}  // namespace simd

}  // namespace stdb::container

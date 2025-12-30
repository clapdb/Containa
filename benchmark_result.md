# Containa btree_map Benchmark Results

## Test Environment

### CPU Information
- **Model**: Intel Core i7-10700 @ 2.90GHz
- **Cores/Threads**: 8 cores / 16 threads
- **L3 Cache**: 16 MB
- **Architecture**: x86_64

### Compiler Versions
- **Clang**: 21.1

### Standard Libraries
- **libstdc++**: 20251211
- **libc++**: 210107

### Build Configuration
- **Optimization**: `-O3 -DNDEBUG -march=native`
- **C++ Standard**: C++20

---

## Performance vs Abseil btree_map

### Summary

| Operation | vs Abseil (libstdc++) | vs Abseil (libc++) |
|-----------|----------------------|---------------------|
| **Sorted Insert** | 2-3.4x faster | 2.4-3.3x faster |
| **Random Insert** | 1.02-1.12x faster | 1.06-1.19x faster |
| **Find** | 1.01-1.15x faster | 1.0-1.20x faster |
| **Erase** | 1.09-1.22x faster | 1.11-1.17x faster |
| **Iterate (int)** | 0.89-0.93x | 0.80-0.87x |
| **Iterate (string)** | 2.1-2.4x faster | 1.4-1.5x faster |

---

## Detailed Results

### Clang 21.1 + libstdc++

#### Integer Keys (10K elements)

| Operation | btree_map | absl::btree_map | vs Abseil |
|-----------|-----------|-----------------|-----------|
| **Sorted Insert** | 100 µs | 337 µs | **3.4x faster** |
| **Random Insert** | 563 µs | 624 µs | **1.11x faster** |
| **Find** | 385 µs | 442 µs | **1.15x faster** |
| **Erase** | 1,067 µs | 1,300 µs | **1.22x faster** |
| Iterate | 9.8 µs | 8.7 µs | 0.89x |

#### Integer Keys (100K elements)

| Operation | btree_map | absl::btree_map | vs Abseil |
|-----------|-----------|-----------------|-----------|
| **Sorted Insert** | 1,187 µs | 3,662 µs | **3.1x faster** |
| **Random Insert** | 7,838 µs | 8,810 µs | **1.12x faster** |
| **Find** | 6,518 µs | 6,610 µs | **1.01x faster** |
| **Erase** | 15,376 µs | 16,817 µs | **1.09x faster** |
| Iterate | 117 µs | 109 µs | 0.93x |

#### String Keys (10K elements)

| Operation | btree_map | absl::btree_map | vs Abseil |
|-----------|-----------|-----------------|-----------|
| **Sorted Insert** | 297 µs | 632 µs | **2.1x faster** |
| **Random Insert** | 1,922 µs | 1,961 µs | **1.02x faster** |
| **Find** | 1,381 µs | 1,401 µs | **1.01x faster** |
| **Iterate** | 16 µs | 33 µs | **2.1x faster** |

#### String Keys (100K elements)

| Operation | btree_map | absl::btree_map | vs Abseil |
|-----------|-----------|-----------------|-----------|
| **Sorted Insert** | 3,895 µs | 7,378 µs | **1.9x faster** |
| **Random Insert** | 24,163 µs | 25,864 µs | **1.07x faster** |
| **Find** | 19,185 µs | 20,551 µs | **1.07x faster** |
| **Iterate** | 166 µs | 396 µs | **2.4x faster** |

---

### Clang 21.1 + libc++

#### Integer Keys (10K elements)

| Operation | btree_map | absl::btree_map | vs Abseil |
|-----------|-----------|-----------------|-----------|
| **Sorted Insert** | 100 µs | 280 µs | **2.8x faster** |
| **Random Insert** | 555 µs | 660 µs | **1.19x faster** |
| **Find** | 405 µs | 484 µs | **1.20x faster** |
| **Erase** | 1,121 µs | 1,312 µs | **1.17x faster** |
| Iterate | 11.2 µs | 9.0 µs | 0.80x |

#### Integer Keys (100K elements)

| Operation | btree_map | absl::btree_map | vs Abseil |
|-----------|-----------|-----------------|-----------|
| **Sorted Insert** | 1,196 µs | 3,992 µs | **3.3x faster** |
| **Random Insert** | 7,805 µs | 8,481 µs | **1.09x faster** |
| **Find** | 6,366 µs | 6,766 µs | **1.06x faster** |
| **Erase** | 15,054 µs | 16,729 µs | **1.11x faster** |
| Iterate | 125 µs | 109 µs | 0.87x |

#### String Keys (10K elements)

| Operation | btree_map | absl::btree_map | vs Abseil |
|-----------|-----------|-----------------|-----------|
| **Sorted Insert** | 236 µs | 626 µs | **2.6x faster** |
| **Random Insert** | 1,633 µs | 1,795 µs | **1.10x faster** |
| **Find** | 1,391 µs | 1,387 µs | ~1.0x |
| **Iterate** | 18.5 µs | 25.3 µs | **1.4x faster** |

#### String Keys (100K elements)

| Operation | btree_map | absl::btree_map | vs Abseil |
|-----------|-----------|-----------------|-----------|
| **Sorted Insert** | 2,962 µs | 7,247 µs | **2.4x faster** |
| **Random Insert** | 22,500 µs | 23,900 µs | **1.06x faster** |
| **Find** | 19,844 µs | 20,205 µs | **1.02x faster** |
| **Iterate** | 219 µs | 335 µs | **1.5x faster** |

---

## Key Optimizations

### SIMD Leaf Search
- **AVX2**: Processes 8 x int32 elements at a time
- **SSE2**: Processes 4 x int32 elements at a time (fallback)
- **AVX-512**: Processes 16 x int32 or 8 x int64 at a time
- **NEON/SVE**: ARM support

### O(1) end() Optimization
- Cached `_rightmost_leaf` pointer eliminates tree traversal for end()

### Split Optimization
- `split_leaf`: Reduced from 2 loops to 1 memcpy by extracting median first
- `split_internal`: Use memcpy for slots and children pointers
- Use `std::is_trivially_copyable_v` to select memcpy vs std::move

---

## Understanding the Ratio

- **< 1.0x**: btree_map is faster (e.g., 0.38x means btree_map is 2.6x faster)
- **= 1.0x**: Same performance
- **> 1.0x**: btree_map is slower (e.g., 1.1x means btree_map takes 10% more time)

---

## Notes

- Benchmarks were run with CPU frequency scaling enabled (powersave governor), which may introduce some variance
- All tests use identical key sets and random seeds for fair comparison
- Error percentages are generally <1% indicating stable results

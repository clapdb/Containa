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

## btree_set vs absl::btree_set

### Test Environment (ARM64)

- **Platform**: AWS Graviton (ARM64)
- **Compiler**: GCC with `-O3 -DNDEBUG`
- **C++ Standard**: C++20

### Summary

| Operation | Speedup vs absl |
|-----------|-----------------|
| **Sorted Insert** | 10-12x faster |
| **Random Insert** | 2-3x faster |
| **Find** | 1.0-1.8x faster |
| **Lower_bound** | 0.9-1.7x faster |
| **Iterate** | 5-7x faster |
| **Erase** | 1.3-2.6x faster |

### Detailed Results (ns/op, lower is better)

#### 10K elements

| Container | SortIns | RandIns | Find | LowerBnd | Iterate | Erase |
|-----------|---------|---------|------|----------|---------|-------|
| stdb::btree_set | 12 | 80 | 54 | 55 | 2 | 80 |
| absl::btree_set | 149 | 228 | 97 | 92 | 15 | 212 |

#### 100K elements

| Container | SortIns | RandIns | Find | LowerBnd | Iterate | Erase |
|-----------|---------|---------|------|----------|---------|-------|
| stdb::btree_set | 14 | 104 | 80 | 77 | 2 | 109 |
| absl::btree_set | 158 | 253 | 120 | 113 | 15 | 238 |

#### 1000K elements

| Container | SortIns | RandIns | Find | LowerBnd | Iterate | Erase |
|-----------|---------|---------|------|----------|---------|-------|
| stdb::btree_set | 16 | 174 | 215 | 209 | 3 | 247 |
| absl::btree_set | 179 | 334 | 196 | 188 | 17 | 322 |

### btree_set Optimizations

- **Direct SIMD vector loads**: For btree_set (stride=1), uses `vld1q_*` (NEON), `_mm_loadu_si128` (SSE2), `_mm256_loadu_si256` (AVX2) instead of element-by-element construction
- **[[no_unique_address]]**: Empty value type uses zero storage, making key access contiguous
- **~33% lower_bound improvement**: Direct load optimization specifically benefits btree_set

---

## NEON vld2q Stride==2 Optimization (btree_map)

For `btree_map<K, V>` where `sizeof(K) == sizeof(V)`, keys are stored at stride==2 (interleaved with values). The `vld2q` instruction efficiently loads interleaved data, extracting keys in a single instruction.

### Performance Improvements

| Key Type | Elements | Before | After | Improvement |
|----------|----------|--------|-------|-------------|
| int32_t | 10K | 72 ns | 64 ns | **+11%** |
| int32_t | 100K | 113 ns | 100 ns | **+12%** |
| int32_t | 1000K | 274 ns | 263 ns | **+4%** |
| int16_t | 10K | 66 ns | 47 ns | **+29%** |
| int16_t | 100K | 88 ns | 68 ns | **+23%** |
| int8_t | 100 | 41 ns | 18 ns | **+56%** |
| int8_t | 200 | 45 ns | 28 ns | **+38%** |

### Implementation

```cpp
// Before: scalar element-by-element construction
key_vec = int32x4_t{keys[i*2], keys[(i+1)*2], keys[(i+2)*2], keys[(i+3)*2]};

// After: single vld2q instruction for stride==2
if constexpr (stride == 2) {
    int32x4x2_t interleaved = vld2q_s32(&keys[i * 2]);
    key_vec = interleaved.val[0];  // Keys at even indices
}
```

### Applicable Types

| Type | vld2 Instruction | Elements/Vector |
|------|------------------|-----------------|
| int8_t/uint8_t | vld2q_s8/u8 | 16 |
| int16_t/uint16_t | vld2q_s16/u16 | 8 |
| int32_t/uint32_t | vld2q_s32/u32 | 4 |

Note: int64_t/uint64_t tested but showed no improvement (only 2 elements per vector).

---

## dense_map Benchmark Results (ARM64)

### Test Environment

- **Platform**: AWS Graviton3 (ARM64)
- **Compiler**: Clang with `-O3 -DNDEBUG -mcpu=native`
- **C++ Standard**: C++20

### Performance Comparison (dense_map = 1.0x baseline)

> **Reading**: `1.0x` = same as dense_map, `2.0x` = 2x slower, `0.5x` = 2x faster

#### Integer Keys (100K elements)

| Operation | dense_map | ankerl | tsl::robin | absl::flat | std::unordered |
|-----------|-----------|--------|------------|------------|----------------|
| Insert | **1.0x** | 1.04x | 0.94x | 0.86x | 5.10x |
| Find (50%) | **1.0x** | 1.00x | 0.80x | - | - |
| Find (100%) | **1.0x** | 1.01x | 0.70x | - | - |
| Iterate | **1.0x** | 1.02x | 6.26x | 4.54x | 3.37x |
| Erase | **1.0x** | 1.39x | 1.35x | 1.38x | 3.65x |

#### Integer Keys (1M elements)

| Operation | dense_map | ankerl | tsl::robin | absl::flat | std::unordered |
|-----------|-----------|--------|------------|------------|----------------|
| Insert | **1.0x** | 0.94x | 1.04x | 1.22x | 3.45x |
| Find (50%) | **1.0x** | 1.04x | 0.56x | - | - |
| Find (100%) | **1.0x** | 1.20x | 0.75x | 1.97x | 1.85x |
| Iterate | **1.0x** | 0.92x | 4.61x | 7.61x | 2.56x |
| Erase | **1.0x** | 1.23x | 1.31x | 1.86x | 2.73x |

#### String Keys 16B (100K elements)

| Operation | dense_map | ankerl | tsl::robin | absl::flat | std::unordered |
|-----------|-----------|--------|------------|------------|----------------|
| Insert | **1.0x** | 1.17x | 3.03x | 1.96x | 3.77x |
| Find | **1.0x** | 1.61x | 2.54x | 1.91x | 2.53x |
| Iterate | **1.0x** | 0.99x | 9.50x | 2.70x | 47.19x |

### Summary

| Metric | vs ankerl | vs tsl | vs absl | vs std |
|--------|-----------|--------|---------|--------|
| Insert (int) | ~1.0x | ~1.0x | 0.9-1.2x | 3-5x faster |
| Find (int) | ~1.0x | 0.6-0.8x slower | 1.9x faster | 1.9x faster |
| Iterate | ~1.0x | 5-10x faster | 5-8x faster | 3-47x faster |
| Erase | 1.2-1.4x faster | 1.3x faster | 1.4-1.9x faster | 2.7-3.7x faster |
| String ops | 1.2-1.6x faster | 2.5-9x faster | 1.9-2.7x faster | 2.5-47x faster |

### Memory Usage (100K int64 → int64)

| Container | Memory | vs dense_map |
|-----------|--------|--------------|
| dense_map | 2,176 KB | **1.0x** |
| ankerl | 2,074 KB | 0.95x |
| tsl::robin | 4,352 KB | 2.0x |
| std::unordered | 4,476 KB | 2.1x |

### Key Optimizations

- **NEON SIMD probing**: `vceqq_u8` for parallel h2 fingerprint matching (Swiss Table)
- **Robin Hood flat storage**: For small trivial keys (≤8B), fingerprint+index buckets
- **Contiguous values**: Fast iteration for both storage modes
- **vaddv_u8 bitmask**: Fixed NEON bitmask (replaced magic multiply with horizontal sum)

---

## Notes

- Benchmarks were run with CPU frequency scaling enabled (powersave governor), which may introduce some variance
- All tests use identical key sets and random seeds for fair comparison
- Error percentages are generally <1% indicating stable results

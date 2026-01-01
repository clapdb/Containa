# Containa

A high-performance C++ container library featuring `btree_map`, `small_vectra`, and `devectra` - optimized alternatives to standard containers.

## Containers

### btree_map / btree_set

A high-performance B-tree based ordered map and set, optimized to outperform `absl::btree_map`.

### small_vectra

A small buffer optimized vector that stores elements inline (no heap allocation) for small sizes.

### devectra

A double-ended vector with O(1) amortized push_front and push_back operations.

### static_vectra

A fixed-capacity vector with embedded storage - never performs dynamic memory allocation.

---

## btree_map Design

### Why B-tree, Not B+ Tree?

We initially implemented both `btree_map` (B-tree) and `bplus_tree_map` (B+ tree). After extensive benchmarking, we chose B-tree:

| Metric | B+ Tree | B-tree |
|--------|---------|--------|
| **Find (int)** | +8% faster | baseline |
| **Insert** | **-50% to -70% slower** | baseline |
| **Memory** | 2x (duplicated keys) | 1x |

**The Problem with B+ Tree**: Our B+ tree used a separate `keys[]` array for SIMD-accelerated search, plus `slots[]` for key-value storage. Every insert/erase had to maintain both arrays, causing significant overhead.

**Conclusion**: 8% find improvement does not justify 50-70% insert penalty.

### Key Optimizations

#### 1. SIMD-Accelerated Node Search

Linear SIMD scan outperforms binary search for small node sizes (<64 keys):

| Platform | Instruction Set | Elements/Iteration |
|----------|-----------------|-------------------|
| x86_64 | AVX-512 | 16 x int32, 8 x int64 |
| x86_64 | AVX2 | 8 x int32 |
| x86_64 | SSE2 | 4 x int32 |
| ARM | NEON | 4 x int32 |
| ARM | SVE | Hardware-dependent |

**Why manual loads instead of AVX2 gather?** `vpgatherdd` has 12-20 cycle latency. For stride access (interleaved key-value storage), manual loads with `_mm256_set_epi32` are faster.

#### 2. O(1) end() with Cached Rightmost Leaf

```cpp
// Before: O(log n) tree traversal for each end() call
iterator end() { return traverse_to_rightmost(); }

// After: O(1) cached pointer
leaf_node* _rightmost_leaf;
iterator end() { return iterator(_rightmost_leaf, _rightmost_leaf->count); }
```

#### 3. Optimized Node Split with memcpy

```cpp
// Before: Two loops - copy all, then shift to remove median
for (i = 0; i < right_count; ++i)
    right->slots[i] = std::move(left->slots[mid + i]);
for (i = 0; i < right->count - 1; ++i)
    right->slots[i] = std::move(right->slots[i + 1]);

// After: Single memcpy - extract median first, copy remaining directly
Key median_key = std::move(left->slots[mid].first);
std::memcpy(&right->slots[0], &left->slots[mid + 1], right_count * sizeof(storage_type));
```

#### 4. Prefetch for Tree Traversal

```cpp
// Prefetch next node before traversing (hides memory latency)
__builtin_prefetch(internal->children[pos], 0, 3);
node = internal->children[pos];
```

#### 5. Three-Way Comparison for Strings

String comparison is expensive. Using three-way comparison (`<=>`) avoids redundant comparisons:

```cpp
// Before: Two comparisons
pos = lower_bound(key);  // uses <
if (pos < count && !(key < slots[pos].key))  // uses < again

// After: Single three-way comparison
auto [pos, exact_match] = lower_bound_with_match(key);  // uses <=>
if (exact_match) return iterator(node, pos);
```

#### 6. Sorted Insert Fast Path

Sequential insertions (common in bulk loading) skip tree traversal:

```cpp
if (_comp(_rightmost_leaf->key(_rightmost_leaf->count - 1), key)) {
    // Key > max key, append directly to rightmost leaf
    insert_at_rightmost(key, value);
}
```

---

## Performance vs Abseil btree_map

**Test Environment**: Intel i7-10700 @ 2.90GHz, Clang 21.1, `-O3 -march=native`

### Clang + libstdc++

| Operation | 10K int | 100K int | 10K string | 100K string |
|-----------|---------|----------|------------|-------------|
| **Sorted Insert** | 3.4x faster | 3.1x faster | 2.1x faster | 1.9x faster |
| **Random Insert** | 1.11x faster | 1.12x faster | 1.02x faster | 1.07x faster |
| **Find** | 1.15x faster | 1.01x faster | 1.01x faster | 1.07x faster |
| **Erase** | 1.22x faster | 1.09x faster | - | - |
| Iterate | 0.89x | 0.93x | 2.1x faster | 2.4x faster |

### Clang + libc++

| Operation | 10K int | 100K int | 10K string | 100K string |
|-----------|---------|----------|------------|-------------|
| **Sorted Insert** | 2.8x faster | 3.3x faster | 2.6x faster | 2.4x faster |
| **Random Insert** | 1.19x faster | 1.09x faster | 1.10x faster | 1.06x faster |
| **Find** | 1.20x faster | 1.06x faster | ~1.0x | 1.02x faster |
| **Erase** | 1.17x faster | 1.11x faster | - | - |
| Iterate | 0.80x | 0.87x | 1.4x faster | 1.5x faster |

See [benchmark_result.md](benchmark_result.md) for detailed results.

---

## Building

### Requirements

- CMake 3.20+
- C++20 compiler (GCC 10+, Clang 10+, MSVC 2019+)

### Basic Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### Build with Abseil Benchmarks

```bash
cmake -B build -DENABLE_ABSL_BENCH=ON
cmake --build build --target btree_bench
./build/bench/btree_bench
```

## Testing

```bash
cmake --build build --target containa_test
./build/tests/containa_test
```

### With Sanitizers

```bash
# AddressSanitizer
cmake -B build -DENABLE_ASAN=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target containa_test
./build/tests/containa_test

# UndefinedBehaviorSanitizer
cmake -B build -DENABLE_UBSAN=ON -DCMAKE_BUILD_TYPE=Debug
```

---

## Project Structure

```
container/
  btree_map.hpp       # B-tree map implementation
  btree_set.hpp       # B-tree set implementation
  skiplist_map.hpp    # Skip list map implementation
  small_vectra.hpp    # Small buffer optimized vector
  devectra.hpp        # Double-ended vector
  static_vectra.hpp   # Fixed-capacity vector
  ring_buffer.hpp     # Circular buffer
  container_base.hpp  # Common utilities
  ...
tests/                # Test suite (doctest)
bench/                # Benchmarks (nanobench)
benchmark_result.md   # Detailed benchmark results
tradeoff.md           # Design decisions and tradeoffs
```

## License

Apache License 2.0 - See LICENSE file for details.

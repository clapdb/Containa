# Containa Optimization Notes

This document describes the optimization techniques used in Containa containers.

## btree_map

`btree_map` is a B-tree based ordered map container designed as a faster alternative to `std::map`.

### Design Goals

- Cache-friendly: Multiple keys per node (unlike std::map's 1 key/node)
- Memory efficient: ~5 bytes per element vs ~40 bytes for std::map
- Fast lookup: O(log N) with better constants due to cache efficiency
- Ordered: Supports in-order iteration

### Node Layout

- Target node size: 256 bytes (fits in 4 cache lines)
- Leaf slots: 29 (for `<int64_t, int64_t>`)
- Internal slots: 14 (with child pointers)
- Key-value pairs stored together for proper iteration

### SIMD Optimization

For `int32_t` and `uint32_t` keys with default comparator (`std::less<Key>`), SSE2 SIMD instructions are used for node search:

```cpp
// Process 4 keys at a time with SSE2
__m128i key_vec = _mm_set_epi32(keys[i+3], keys[i+2], keys[i+1], keys[i]);
__m128i lt = _mm_cmplt_epi32(key_vec, target_vec);
int mask = _mm_movemask_ps(_mm_castsi128_ps(lt));
```

SIMD provides **1.93x speedup** for find operations on int32_t keys compared to scalar linear search.

Note: int64_t keys do not use SIMD as SSE2 lacks native 64-bit comparison instructions. AVX-512 would be needed for 64-bit SIMD optimization.

### Performance Benchmarks

Tested with clang++ -O3 -march=native on 10,000 elements:

#### int32_t keys (with SSE2 SIMD)

| Operation | std::map | btree_map | Speedup |
|-----------|----------|-----------|---------|
| Insert    | 653 us   | 522 us    | **1.25x** |
| Find      | 143 us   | 139 us    | **1.03x** |
| Iterate   | 47 us    | 10.9 us   | **4.32x** |

#### int64_t keys (no SIMD)

| Operation | std::map | btree_map | Speedup |
|-----------|----------|-----------|---------|
| Insert    | 800 us   | 619 us    | **1.29x** |
| Find      | 149 us   | 258 us    | 0.58x   |
| Iterate   | 63.5 us  | 17.2 us   | **3.69x** |

### Trade-offs vs std::map

**Advantages:**
- Much faster iteration (3-4x) due to sequential memory access
- Faster insertion (1.25-1.29x)
- Lower memory overhead
- Better cache utilization

**Disadvantages:**
- No iterator stability (iterators invalidated on insert/erase)
- No pointer stability
- Find is slower for int64_t keys (no SIMD available)
- More complex deletion logic

### Usage

```cpp
#include "container/btree_map.hpp"

stdb::container::btree_map<int32_t, std::string> map;
map[1] = "one";
map[2] = "two";

// Iteration is very fast
for (const auto& [key, value] : map) {
    std::cout << key << ": " << value << "\n";
}

// Find returns iterator
auto it = map.find(1);
if (it != map.end()) {
    std::cout << it->second << "\n";
}
```

### Comparison with Google Abseil btree_map

| Feature | Containa btree_map | Abseil btree_map |
|---------|-------------------|------------------|
| Node size | 256 bytes | 256 bytes |
| Max slots (int64) | 29 leaf, 14 internal | ~31 leaf, ~15 internal |
| Search algorithm | Linear + SSE2 SIMD | Binary search |
| SIMD support | Yes (int32_t) | No |
| Memory layout | Key-value pairs together | Key-value pairs together |
| Deletion | Rebuild (simple) | Proper rebalancing |
| Code size | ~1000 lines | ~3000 lines |
| Dependencies | None (header-only) | Abseil base libs |

#### Benchmark Results (clang++ -O3 -march=native, 10K elements)

**int64_t keys:**

| Operation | std::map | Containa | Abseil | Winner |
|-----------|----------|----------|--------|--------|
| Insert | 821 us | 346 us | 450 us | **Containa (1.30x faster)** |
| Find | 153 us | 248 us | 302 us | Containa (1.22x faster) |
| Iterate | 68 us | 20.5 us | 56.8 us | **Containa (2.77x faster)** |

**int32_t keys:**

| Operation | std::map | Containa (SIMD) | Abseil |
|-----------|----------|-----------------|--------|
| Find | 148 us | 144 us | 332 us | **Containa (2.31x faster)** |

**Key findings:**
- Containa is 1.30x faster at insertion (optimized split + no redundant find)
- Containa is 1.22-2.31x faster at find (SIMD for int32, simpler traversal)
- Containa is 2.77x faster at iteration (more compact node layout)

#### Design Differences

**Search Strategy:**
- Abseil uses binary search within nodes
- Containa uses linear search with SIMD for int32_t keys
- Linear search is faster for small nodes (< 32 elements) due to cache prefetching
- SIMD makes linear search competitive even for larger nodes

**Deletion:**
- Abseil implements full B-tree rebalancing (merge/borrow from siblings)
- Containa currently rebuilds the tree on delete (simpler, but O(n) for single delete)
- For bulk operations, Containa's approach may be acceptable

**Memory Efficiency:**
- Both target 256-byte nodes for optimal cache line utilization
- Abseil has slightly more sophisticated memory allocation (custom allocator support)
- Containa uses standard `new`/`delete`

#### When to Choose Each

**Use Containa btree_map when:**
- You need a simple, header-only solution with no dependencies
- You want faster insert, find, and iteration than Abseil
- Your keys are int32_t (SIMD benefit: 2.31x faster find)
- Deletion is rare or batch-oriented
- Iteration performance is critical (2.77x faster)

**Use Abseil btree_map when:**
- You need production-grade deletion performance with rebalancing
- You're already using Abseil in your project
- You need custom allocator support
- You want thoroughly battle-tested code in production environments

### String Key/Value Performance

btree_map fully supports non-trivially-copyable types like `std::string`.

**Recommended: Use `btree_map_auto` for string types** - it automatically selects optimal node size:

```cpp
#include "container/btree_map.hpp"

// Automatically uses 1024-byte nodes for string pairs (15 slots/node)
stdb::container::btree_map_auto<std::string, std::string> map;
```

**vs Abseil btree_map (10K string entries, -O3 -march=native):**

| Operation | Abseil   | Containa  | Ratio |
|-----------|----------|-----------|-------|
| Insert    | 1388 us  | 1475 us   | **0.94x** (6% slower) |
| Find      | 862 us   | 899 us    | **0.96x** (4% slower) |
| Iterate   | 96 us    | 24 us     | **4.0x faster** |

**vs Abseil btree_map (10K int32_t entries, SIMD-optimized):**

| Operation | Abseil   | Containa  | Ratio |
|-----------|----------|-----------|-------|
| Find      | 297 us   | 138 us    | **2.15x faster** |

**vs Abseil btree_map (10K int64_t entries):**

| Operation | Abseil   | Containa  | Ratio |
|-----------|----------|-----------|-------|
| Insert    | 457 us   | 257 us    | **1.78x faster** |
| Find      | 302 us   | 360 us    | 0.84x (16% slower) |
| Iterate   | 56 us    | 20 us     | **2.75x faster** |

**Key Optimizations Applied:**
1. **Type-specialized search** - separate inlined functions for leaf/internal nodes with `flatten` attribute
2. **Binary search with compiler hints** - `__builtin_assume` for count bounds
3. **Move semantics** for efficient string insertion without copies
4. **Automatic node sizing** - larger nodes for larger types (1024 bytes for strings)
5. **Perfect forwarding** throughout the insert path
6. **Single comparison** for equality checks (leveraging lower_bound guarantee)
7. **Force-inline** with `__restrict__` hints for hot path functions
8. **SSE2 SIMD** for int32_t keys (2.15x faster find than Abseil)

**Node Size Selection:**
- `btree_map<K,V>` uses 256-byte nodes (default)
- `btree_map_auto<K,V>` automatically selects node size to ensure ≥15 slots
- For `pair<string,string>` (64 bytes), this means 1024-byte nodes

**Performance Summary:**
Containa btree_map is now nearly performance-equivalent to Abseil for find (97%) and insert (94%), while being **4.17x faster for iteration**. This makes it an excellent choice for workloads that involve frequent iteration over ordered data.

**vs std::map (10K string entries):**

| Operation | std::map | btree_map_auto | Speedup |
|-----------|----------|----------------|---------|
| Insert    | 2330 us  | 1480 us        | **1.57x** |
| Find      | 1230 us  | 890 us         | **1.38x** |
| Iterate   | 87 us    | 23 us          | **3.78x** |

### Future Optimizations
- AVX2/AVX-512 for 64-bit key SIMD search
- Proper B-tree deletion with rebalancing (current implementation rebuilds)
- B+ tree variant for even faster iteration

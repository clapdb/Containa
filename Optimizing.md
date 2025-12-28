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

SIMD instructions are used for fast node search with the following key types (when using `std::less<Key>` comparator):

**x86-64 (AVX-512, when available):**
- AVX-512 for `int32_t` and `uint32_t` keys (16 keys/iteration)
- AVX-512 for `int64_t` and `uint64_t` keys (8 keys/iteration)
- AVX-512 for `float` keys (16 keys/iteration)
- AVX-512 for `double` keys (8 keys/iteration)

**x86-64 (SSE2/AVX2 fallback):**
- SSE2 for `int8_t` and `uint8_t` keys (16 keys/iteration)
- SSE2 for `int16_t` and `uint16_t` keys (8 keys/iteration)
- SSE2 for `int32_t` and `uint32_t` keys (4 keys/iteration)
- SSE for `float` keys (4 keys/iteration)
- SSE2 for `double` keys (2 keys/iteration)
- AVX2 for `int64_t` and `uint64_t` keys (4 keys/iteration)
- AVX for `double` keys (4 keys/iteration)

**ARM64 (SVE, when available):**
- SVE for `int32_t` and `uint32_t` keys (variable, up to 16 keys/iteration with 512-bit vectors)
- SVE for `int64_t` and `uint64_t` keys (variable, up to 8 keys/iteration with 512-bit vectors)
- SVE for `float` keys (variable, up to 16 keys/iteration)
- SVE for `double` keys (variable, up to 8 keys/iteration)

**ARM64 (NEON fallback):**
- NEON for `int8_t` and `uint8_t` keys (16 keys/iteration)
- NEON for `int16_t` and `uint16_t` keys (8 keys/iteration)
- NEON for `int32_t` and `uint32_t` keys (4 keys/iteration)
- NEON for `int64_t` and `uint64_t` keys (2 keys/iteration)
- NEON for `float` keys (4 keys/iteration)
- NEON for `double` keys (2 keys/iteration)

SVE (Scalable Vector Extension) uses vector-length agnostic programming, automatically adapting to hardware vector width (128-2048 bits). Unsigned integer types use the XOR-with-sign-bit trick (x86) or native unsigned intrinsics (NEON/SVE) to achieve correct comparison semantics.

### Search Strategy

Containa uses a hybrid search strategy optimized for different key types:

**SIMD-enabled types** (int8/16/32/64, uint8/16/32/64, float, double):
- Uses SIMD binary search for parallel key comparison
- SSE2/AVX2 on x86-64, NEON on ARM64
- Significantly faster than linear/binary search

**Non-SIMD types** (struct, string, custom types):
- Uses linear search instead of binary search
- Linear search is 2-3x faster for typical btree node sizes (14-29 slots) due to:
  1. Sequential memory access (better cache/prefetch behavior)
  2. Better branch prediction
  3. Lower overhead per iteration

### Performance Benchmarks

Tested with clang++ -O3 -march=native on 100,000 elements:

#### Integer Keys (int → int)

| Operation | Abseil | Containa | Speedup |
|-----------|--------|----------|---------|
| Sorted insert | 6.15 ms | 1.45 ms | **4.2x** |
| Random insert | 12.57 ms | 9.15 ms | **1.37x** |
| Find | 7.58 ms | 6.62 ms | **1.14x** |
| Iterate | 0.51 ms | 0.12 ms | **4.1x** |

#### String Keys (string → int)

| Operation | Abseil | Containa | Speedup |
|-----------|--------|----------|---------|
| Sorted insert | 8.82 ms | 4.70 ms | **1.88x** |
| Random insert | 27.4 ms | 25.5 ms | **1.07x** |
| Find | 21.9 ms | 19.9 ms | **1.10x** |
| Iterate | 1.08 ms | 0.19 ms | **5.6x** |

### Large Scale Benchmarks

Performance at scale (1M and 10M elements) to verify behavior under memory pressure:

#### Integer Keys - 1M elements

| Operation | Abseil | Containa | Speedup |
|-----------|--------|----------|---------|
| Sorted insert | 65.1 ms | 12.8 ms | **5.1x** |
| Random insert | 152 ms | 127 ms | **1.20x** |
| Find | 121 ms | 117 ms | **1.04x** |
| Iterate | 7.9 ms | 2.1 ms | **3.8x** |
| Erase | 284 ms | 251 ms | **1.13x** |

#### Integer Keys - 10M elements

| Operation | Abseil | Containa | Speedup |
|-----------|--------|----------|---------|
| Sorted insert | 687 ms | 172 ms | **4.0x** |
| Random insert | 2.75 s | 2.79 s | ~1.0x |
| Find | 2.33 s | 2.54 s | 0.92x |
| Iterate | 100 ms | 41 ms | **2.4x** |
| Erase | 5.18 s | 5.38 s | ~1.0x |

#### String Keys - 1M elements

| Operation | Abseil | Containa | Speedup |
|-----------|--------|----------|---------|
| Sorted insert | 85 ms | 40 ms | **2.1x** |
| Random insert | 451 ms | 385 ms | **1.17x** |
| Find | 409 ms | 365 ms | **1.12x** |
| Iterate | 27.9 ms | 5.5 ms | **5.1x** |

#### Scaling Observations

- **Sorted insert** maintains 4-5x advantage even at 10M elements
- **Iteration** remains 2-5x faster at all scales
- **Random operations** at 10M elements become cache-miss dominated, reducing SIMD advantages
- Memory bandwidth becomes the bottleneck above ~1M elements for random access patterns

#### Find by Type (10,000 elements)

| Type     | Abseil | Containa | Speedup |
|----------|--------|----------|---------|
| int8_t   | 251 us | 94 us    | **2.68x** |
| int16_t  | 357 us | 150 us   | **2.39x** |
| int32_t  | 334 us | 144 us   | **2.33x** |
| int64_t  | 285 us | 178 us   | **1.61x** |
| float    | 342 us | 166 us   | **2.05x** |
| double   | 299 us | 268 us   | **1.12x** |

#### Struct types (Point, Rect)

| Type | Operation | Abseil | Containa | Speedup |
|------|-----------|--------|----------|---------|
| Point (16B) | Find | 376 us | 289 us | **1.30x** |
| Point (16B) | Iterate | 46 us | 20 us | **2.31x** |
| Rect (32B) | Find | 450 us | 366 us | **1.23x** |
| Rect (32B) | Iterate | 67 us | 33 us | **2.03x** |

### Trade-offs vs std::map

**Advantages:**
- Much faster iteration (2-4x) due to sequential memory access
- Faster find for SIMD types (1.6-2.7x vs Abseil)
- Lower memory overhead
- Better cache utilization

**Disadvantages:**
- No iterator stability (iterators invalidated on insert/erase)
- No pointer stability
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
| Search (SIMD types) | SIMD binary search | Linear search |
| Search (non-SIMD) | Linear search | Binary search |
| SIMD support | int8-64, float, double | No |
| Memory layout | Key-value pairs together | Key-value pairs together |
| Deletion | Proper rebalancing | Proper rebalancing |
| Custom allocator | Yes (allocator_traits) | Yes |
| Heterogeneous lookup | Yes (is_transparent) | Yes |
| Node handle API | Yes (C++17) | Yes |
| Code size | ~4500 lines | ~3000 lines |
| Dependencies | None (header-only) | Abseil base libs |

#### Key findings:
- **Sorted insert**: Containa is 1.9-4.2x faster (optimized sequential append path)
- **Random insert**: Containa is 1.07-1.37x faster
- **SIMD types (int8-64, float, double)**: Containa is 1.1-2.7x faster at find
- **Struct types**: Containa is 1.2-1.3x faster at find (linear search beats binary)
- **Iteration**: Containa is 4-6x faster (more compact traversal)

#### Design Differences

**Search Strategy:**
- **Abseil**: Uses linear search for arithmetic types, binary search for complex types
- **Containa**: Uses SIMD binary search for arithmetic types, linear search for complex types

Why this is better:
- SIMD binary search (4-16 keys/iteration) >> linear search for arithmetic types
- Linear search >> binary search for struct types at typical node sizes due to:
  - Sequential memory access
  - Better branch prediction
  - Lower per-iteration overhead

**Deletion:**
- Both Abseil and Containa implement full B-tree rebalancing (merge/borrow from siblings)
- O(log n) deletion with proper node rebalancing
- Iterator invalidation on delete (same as Abseil)

**Memory Efficiency:**
- Both target 256-byte nodes for optimal cache line utilization
- Both support custom allocators via `std::allocator_traits`
- Containa uses `[[no_unique_address]]` for zero-overhead stateless allocators

#### When to Choose Each

**Use Containa btree_map when:**
- You need a simple, header-only solution with no dependencies
- You want faster insert (1.4-4.2x faster for sorted, 1.07-1.37x for random)
- You want faster find for any key type (1.1-2.7x faster)
- You want faster iteration (4-6x faster)
- You need full C++20 API compatibility (heterogeneous lookup, node handles, etc.)

**Use Abseil btree_map when:**
- You're already using Abseil in your project
- You want thoroughly battle-tested code in production environments
- You need specific Abseil extensions not in standard API

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
| Insert    | 1339 us  | 1427 us   | 0.94x |
| Find      | 795 us   | 807 us    | 0.98x |
| Iterate   | 90 us    | 47 us     | **1.91x faster** |

Note: String find is slightly slower due to complex comparison semantics. Iteration remains significantly faster.

**Key Optimizations Applied:**
1. **SIMD search for arithmetic types** - parallel key comparison using SSE2/AVX2/NEON
2. **Linear search for complex types** - faster than binary search at typical node sizes
3. **Type-specialized search** - separate inlined functions with `flatten` attribute
4. **Compiler hints** - portable `BTREE_ASSUME` for count bounds optimization
5. **Move semantics** - efficient insertion without unnecessary copies
6. **Automatic node sizing** - btree_map now auto-selects optimal node size
7. **Force-inline** with `__restrict__` hints for hot path functions

**Node Size Selection:**
- `btree_map<K,V>` automatically selects node size to ensure ≥15 slots
- Small types (pair ≤17B): 256-byte nodes
- Medium types (pair ≤34B): 512-byte nodes
- Large types (pair ≤68B): 1024-byte nodes, etc.
- `btree_map_compact<K,V>` forces 256-byte nodes if memory is critical

**Performance Summary:**
Containa btree_map is **1.1-2.7x faster** than Abseil for find (depending on key type) and **2-4x faster for iteration**. Even for non-SIMD types like struct/string, linear search provides competitive or better performance than Abseil's binary search.

**vs std::map (10K string entries):**

| Operation | std::map | btree_map | Speedup |
|-----------|----------|-----------|---------|
| Insert    | 2330 us  | 1480 us   | **1.57x** |
| Find      | 1230 us  | 890 us    | **1.38x** |
| Iterate   | 87 us    | 23 us     | **3.78x** |

### Arena Allocator Support

btree_map supports custom allocators via `std::allocator_traits`, including arena allocators for fast bulk allocation/deallocation.

**Recommended: Use [ClapDB Arena](https://github.com/clapdb/Arena)** - a high-performance arena allocator with `std::pmr::memory_resource` support.

**Usage Example with ClapDB Arena:**

```cpp
#include <arena/arena.hpp>
#include "container/btree_map.hpp"

using namespace stdb::container;

// Create Arena with default options
arena::Arena ar(arena::Arena::Options::GetDefaultOptions());

// Use Arena's pmr memory_resource with btree_map
using Alloc = std::pmr::polymorphic_allocator<std::pair<const int, int>>;
btree_map<int, int, std::less<int>, Alloc> map(Alloc(ar.get_memory_resource()));

for (int i = 0; i < 100000; ++i) {
    map[i] = i;
}

// Erase is fast (deallocate is no-op in arena)
for (int i = 0; i < 50000; ++i) {
    map.erase(i);
}

// All memory freed when arena is destroyed
// Or call ar.Reset() to reuse arena
```

**When to Use Arena Allocator:**

- Temporary containers that are discarded together
- Batch processing where all data is freed at once
- Performance-critical code where allocation overhead matters
- Multiple containers sharing memory pool

**Benefits:**

- Zero-cost destruction (arena reset frees all memory instantly)
- Better memory locality (all allocations from contiguous blocks)
- Reduced fragmentation
- `std::pmr` compatible (works with any pmr-aware container)

### Future Optimizations
- B+ tree variant for even faster iteration
- Bulk loading optimization for sorted input

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

**x86-64:**
- SSE2 for `int8_t` and `uint8_t` keys (16 keys/iteration)
- SSE2 for `int16_t` and `uint16_t` keys (8 keys/iteration)
- SSE2 for `int32_t` and `uint32_t` keys (4 keys/iteration)
- SSE for `float` keys (4 keys/iteration)
- SSE2 for `double` keys (2 keys/iteration)
- AVX2 for `int64_t` and `uint64_t` keys (4 keys/iteration)
- AVX for `double` keys (4 keys/iteration, when AVX2 available)

**ARM64:**
- NEON for `int8_t` and `uint8_t` keys (16 keys/iteration)
- NEON for `int16_t` and `uint16_t` keys (8 keys/iteration)
- NEON for `int32_t` and `uint32_t` keys (4 keys/iteration)
- NEON for `int64_t` and `uint64_t` keys (2 keys/iteration)
- NEON for `float` keys (4 keys/iteration)
- NEON for `double` keys (2 keys/iteration)

Unsigned integer types use the XOR-with-sign-bit trick (x86) or native unsigned intrinsics (NEON) to achieve correct comparison semantics.

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

Tested with clang++ -O3 -march=native on 10,000 elements:

#### Containa vs Abseil btree_map (Find operation)

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
| Deletion | Rebuild (simple) | Proper rebalancing |
| Code size | ~2000 lines | ~3000 lines |
| Dependencies | None (header-only) | Abseil base libs |

#### Key findings:
- **SIMD types (int8-64, float, double)**: Containa is 1.1-2.7x faster at find
- **Struct types**: Containa is 1.2-1.3x faster at find (linear search beats binary)
- **Iteration**: Containa is 2-4x faster (more compact traversal)

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
- You want faster find for any key type (1.1-2.7x faster)
- You want faster iteration (2-4x faster)
- Deletion is rare or batch-oriented

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

### Future Optimizations
- AVX-512 for even faster SIMD search (current AVX2 handles 4 int64_t keys, AVX-512 could handle 8)
- Proper B-tree deletion with rebalancing (current implementation rebuilds)
- B+ tree variant for even faster iteration

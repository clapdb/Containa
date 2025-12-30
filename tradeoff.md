# btree_map Design Tradeoffs

This document records optimization attempts and their results to guide future development.

## Prefetch Optimization

### Integer Keys: Effective
- Prefetching next node during tree traversal improves find performance
- Already implemented in `lower_bound_in_internal` for integer paths

### String Keys: Not Effective
- **Attempted**: Adding `__builtin_prefetch(internal->children[pos], 0, 3)` before traversing to child node in string-like find path
- **Result**: Performance regression
- **Reason**: String comparison is CPU-bound (iterating through characters). By the time comparison completes, the prefetched data may have been evicted from L1 cache. The prefetch instruction overhead isn't amortized.
- **Recommendation**: Don't add prefetch for string-like types

## AVX2 Gather vs Manual Loads

### Gather Instructions (`_mm256_i32gather_epi32`): Not Effective for Stride Access
- **Attempted**: Using AVX2 gather to load 8 keys with stride (interleaved key-value storage)
- **Result**: 41% performance regression
- **Reason**: `vpgatherdd` has 12-20 cycle latency on modern CPUs. For stride access patterns, manual loads with `_mm256_set_epi32` are faster.

### Manual Loads with AVX2 (`_mm256_set_epi32`): Effective
- **Implemented**: Load 8 keys manually into AVX2 register
- **Result**: ~8% improvement over SSE2 (4 elements) for integer find
- **Recommendation**: Use manual loads for stride access patterns, reserve gather for truly scattered access

## SIMD Lower Bound

### Integer Keys (4-8 byte): SSE2/AVX2 Effective
- SSE2: Process 4 keys per iteration
- AVX2: Process 8 keys per iteration (when available)
- Linear SIMD scan outperforms binary search for small node sizes (<64 keys)

### String Keys: Not Applicable
- SIMD acceleration doesn't apply to variable-length string comparison
- Binary search with three-way comparison is optimal

## bplus_tree_map (Removed)

### Design Flaw: Dual Key Storage
- **Approach**: Separate `keys[]` array for SIMD search + `slots[]` for key-value storage
- **Find improvement**: ~8% faster for integer keys
- **Insert overhead**: 50-70% slower due to dual array maintenance
- **Decision**: Removed. The find improvement didn't justify the insert regression.

## Node Size

- Default: Dynamic based on key-value size (256-4096 bytes)
- Target: ~15 slots per node for good cache utilization
- Larger nodes = fewer tree levels but more comparison per node
- Current balance optimizes for both find and insert performance

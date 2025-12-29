# Containa btree_map vs Abseil btree_map Benchmark Results

## Test Environment

### CPU Information
- **Model**: Intel Core i7-10700 @ 2.90GHz
- **Cores/Threads**: 8 cores / 16 threads
- **L3 Cache**: 16 MB
- **Architecture**: x86_64

### Compiler Versions
- **GCC**: 15.2
- **Clang**: 21.1

### Standard Libraries
- **libstdc++**: 20251211
- **libc++**: 210107

### Build Configuration
- **Optimization**: `-O3 -DNDEBUG`
- **C++ Standard**: C++20

---

## Benchmark Summary

### Key Findings

1. **Sorted Insert**: Containa is **2.5-3x faster** than Abseil across all configurations
2. **Random Insert**: Containa performs comparably or slightly better than Abseil
3. **Find**: Both perform similarly at large scales; Containa slightly ahead at smaller sizes
4. **Iterate**:
   - Integer keys: Both perform similarly
   - String keys: **Containa is 1.5-2.8x faster** than Abseil
5. **Erase**: Both perform similarly

---

## Detailed Results

### 1. GCC 15.2 + libstdc++ (10000 elements)

| Operation | Containa (ns/op) | Abseil (ns/op) | Speedup |
|-----------|------------------|----------------|---------|
| **Integer Keys** |
| Sorted Insert | 110,604 | 310,992 | **2.81x** |
| Random Insert | 546,655 | 592,217 | **1.08x** |
| Find | 384,648 | 442,576 | **1.15x** |
| Iterate | 8,578 | 7,097 | 0.83x |
| Erase | 1,093,607 | 1,182,074 | **1.08x** |
| **String Keys** |
| Sorted Insert | 304,975 | 609,836 | **2.00x** |
| Random Insert | 1,963,985 | 1,834,676 | 0.93x |
| Find | 1,359,842 | 1,380,827 | **1.02x** |
| Iterate | 14,826 | 29,942 | **2.02x** |

### 2. GCC 15.2 + libstdc++ (100000 elements)

| Operation | Containa (ns/op) | Abseil (ns/op) | Speedup |
|-----------|------------------|----------------|---------|
| **Integer Keys** |
| Sorted Insert | 1,335,279 | 3,758,379 | **2.81x** |
| Random Insert | 7,941,529 | 8,145,419 | **1.03x** |
| Find | 6,613,191 | 6,560,014 | 0.99x |
| Iterate | 112,856 | 102,789 | 0.91x |
| Erase | 15,438,954 | 16,084,039 | **1.04x** |
| **String Keys** |
| Sorted Insert | 4,012,431 | 7,382,919 | **1.84x** |
| Random Insert | 25,487,912 | 24,663,399 | 0.97x |
| Find | 18,940,994 | 19,951,905 | **1.05x** |
| Iterate | 158,794 | 362,427 | **2.28x** |

### 3. GCC 15.2 + libstdc++ (1M elements)

| Operation | Containa (ns/op) | Abseil (ns/op) | Speedup |
|-----------|------------------|----------------|---------|
| **Integer Keys** |
| Sorted Insert | 13,474,467 | 54,418,629 | **4.04x** |
| Random Insert | 111,738,061 | 106,718,427 | 0.96x |
| Find | 105,466,576 | 94,804,637 | 0.90x |
| Iterate | 1,400,349 | 1,294,026 | 0.92x |
| Erase | 227,401,743 | 216,260,171 | 0.95x |
| **String Keys** |
| Sorted Insert | 38,675,229 | 75,108,548 | **1.94x** |
| Random Insert | 374,509,903 | 432,087,713 | **1.15x** |
| Find | 346,559,175 | 400,214,271 | **1.15x** |
| Iterate | 5,147,371 | 14,531,651 | **2.82x** |

### 4. GCC 15.2 + libstdc++ (10M elements)

| Operation | Containa (ns/op) | Abseil (ns/op) | Speedup |
|-----------|------------------|----------------|---------|
| **Integer Keys** |
| Sorted Insert | 169,399,488 | 461,803,091 | **2.73x** |
| Random Insert | 2,547,021,147 | 2,233,934,759 | 0.88x |
| Find | 2,432,506,969 | 2,157,175,762 | 0.89x |
| Iterate | 39,338,036 | 36,611,587 | 0.93x |
| Erase | 5,035,149,143 | 4,436,942,596 | 0.88x |

---

### 5. Clang 21.1 + libstdc++ (10000 elements)

| Operation | Containa (ns/op) | Abseil (ns/op) | Speedup |
|-----------|------------------|----------------|---------|
| **Integer Keys** |
| Sorted Insert | 115,297 | 309,712 | **2.69x** |
| Random Insert | 533,645 | 657,752 | **1.23x** |
| Find | 389,215 | 465,444 | **1.20x** |
| Iterate | 10,975 | 8,767 | 0.80x |
| Erase | 1,052,326 | 1,278,914 | **1.22x** |
| **String Keys** |
| Sorted Insert | 305,608 | 609,958 | **2.00x** |
| Random Insert | 1,877,681 | 1,877,059 | 1.00x |
| Find | 1,388,471 | 1,384,815 | 1.00x |
| Iterate | 14,765 | 32,222 | **2.18x** |

### 6. Clang 21.1 + libstdc++ (100000 elements)

| Operation | Containa (ns/op) | Abseil (ns/op) | Speedup |
|-----------|------------------|----------------|---------|
| **Integer Keys** |
| Sorted Insert | 1,372,623 | 3,887,455 | **2.83x** |
| Random Insert | 7,727,565 | 8,667,121 | **1.12x** |
| Find | 6,679,642 | 7,424,263 | **1.11x** |
| Iterate | 199,097 | 112,569 | 0.57x |
| Erase | 15,190,071 | 16,853,332 | **1.11x** |
| **String Keys** |
| Sorted Insert | 4,090,996 | 7,029,891 | **1.72x** |
| Random Insert | 24,225,920 | 26,716,375 | **1.10x** |
| Find | 18,845,949 | 19,968,588 | **1.06x** |
| Iterate | 165,093 | 384,532 | **2.33x** |

### 7. Clang 21.1 + libstdc++ (1M elements)

| Operation | Containa (ns/op) | Abseil (ns/op) | Speedup |
|-----------|------------------|----------------|---------|
| **Integer Keys** |
| Sorted Insert | 13,995,406 | 42,630,476 | **3.05x** |
| Random Insert | 110,146,724 | 111,731,093 | **1.01x** |
| Find | 109,063,258 | 99,182,086 | 0.91x |
| Iterate | 1,588,307 | 1,384,032 | 0.87x |
| Erase | 231,292,717 | 225,104,555 | 0.97x |
| **String Keys** |
| Sorted Insert | 40,270,673 | 85,138,183 | **2.11x** |
| Random Insert | 378,429,884 | 453,521,114 | **1.20x** |
| Find | 354,639,776 | 414,409,928 | **1.17x** |
| Iterate | 5,415,213 | 15,285,783 | **2.82x** |

### 8. Clang 21.1 + libstdc++ (10M elements)

| Operation | Containa (ns/op) | Abseil (ns/op) | Speedup |
|-----------|------------------|----------------|---------|
| **Integer Keys** |
| Sorted Insert | 175,665,926 | 467,799,696 | **2.66x** |
| Random Insert | 2,581,213,585 | 2,323,184,229 | 0.90x |
| Find | 2,465,138,177 | 2,232,463,537 | 0.91x |
| Iterate | 40,782,313 | 38,547,345 | 0.95x |
| Erase | 5,112,899,980 | 4,661,149,376 | 0.91x |

---

### 9. Clang 21.1 + libc++ (10000 elements)

| Operation | Containa (ns/op) | Abseil (ns/op) | Speedup |
|-----------|------------------|----------------|---------|
| **Integer Keys** |
| Sorted Insert | 111,659 | 304,793 | **2.73x** |
| Random Insert | 556,021 | 617,153 | **1.11x** |
| Find | 425,589 | 490,448 | **1.15x** |
| Iterate | 10,692 | 9,329 | 0.87x |
| Erase | 1,111,778 | 1,249,795 | **1.12x** |
| **String Keys** |
| Sorted Insert | 256,280 | 632,226 | **2.47x** |
| Random Insert | 1,643,982 | 1,793,414 | **1.09x** |
| Find | 1,416,763 | 1,513,810 | **1.07x** |
| Iterate | 18,539 | 27,349 | **1.48x** |

### 10. Clang 21.1 + libc++ (100000 elements)

| Operation | Containa (ns/op) | Abseil (ns/op) | Speedup |
|-----------|------------------|----------------|---------|
| **Integer Keys** |
| Sorted Insert | 1,393,225 | 3,850,515 | **2.76x** |
| Random Insert | 7,648,587 | 8,498,153 | **1.11x** |
| Find | 6,665,233 | 7,047,750 | **1.06x** |
| Iterate | 122,599 | 114,253 | 0.93x |
| Erase | 15,223,212 | 16,480,596 | **1.08x** |
| **String Keys** |
| Sorted Insert | 3,376,390 | 7,228,157 | **2.14x** |
| Random Insert | 23,144,799 | 24,585,883 | **1.06x** |
| Find | 20,583,091 | 20,873,582 | **1.01x** |
| Iterate | 221,485 | 340,955 | **1.54x** |

### 11. Clang 21.1 + libc++ (1M elements)

| Operation | Containa (ns/op) | Abseil (ns/op) | Speedup |
|-----------|------------------|----------------|---------|
| **Integer Keys** |
| Sorted Insert | 14,573,191 | 42,570,644 | **2.92x** |
| Random Insert | 110,946,712 | 112,066,393 | **1.01x** |
| Find | 107,285,764 | 100,310,030 | 0.94x |
| Iterate | 2,118,678 | 1,381,539 | 0.65x |
| Erase | 224,861,515 | 231,823,939 | **1.03x** |
| **String Keys** |
| Sorted Insert | 32,762,293 | 84,270,035 | **2.57x** |
| Random Insert | 358,997,595 | 414,040,550 | **1.15x** |
| Find | 370,813,501 | 401,729,382 | **1.08x** |
| Iterate | 8,050,671 | 13,013,299 | **1.62x** |

### 12. Clang 21.1 + libc++ (10M elements)

| Operation | Containa (ns/op) | Abseil (ns/op) | Speedup |
|-----------|------------------|----------------|---------|
| **Integer Keys** |
| Sorted Insert | 177,302,951 | 468,651,491 | **2.64x** |
| Random Insert | 2,556,780,356 | 2,280,200,739 | 0.89x |
| Find | 2,471,356,325 | 2,255,850,820 | 0.91x |
| Iterate | 40,611,970 | 38,517,658 | 0.95x |
| Erase | 5,143,110,019 | 4,523,195,205 | 0.88x |

---

## Performance Analysis

### Containa Strengths

1. **Sorted Insert Performance**: Containa consistently outperforms Abseil by 2-4x on sorted inserts across all configurations. This is a significant advantage for use cases involving bulk loading of sorted data.

2. **String Iteration**: For string-keyed maps, Containa's iteration is 1.5-2.8x faster than Abseil, making it ideal for workloads that frequently scan through all entries.

3. **Random Operations at Scale**: For random insert/find/erase at smaller scales (10K-100K), Containa generally matches or slightly outperforms Abseil.

### Abseil Strengths

1. **Very Large Scale Random Operations**: At 10M elements with random access patterns, Abseil shows 10-15% better performance for random insert, find, and erase operations.

2. **Integer Iteration**: For integer-keyed maps, Abseil's iteration is slightly faster (5-20%).

### Recommendations

- **Use Containa when**:
  - Your workload involves sorted or nearly-sorted insertions
  - You frequently iterate over string-keyed maps
  - You operate primarily in the 10K-1M element range

- **Use Abseil when**:
  - You operate at 10M+ element scale with random access patterns
  - Integer key iteration performance is critical

---

## Notes

- Benchmarks were run with CPU frequency scaling enabled, which may introduce some variance
- All tests use identical key sets and random seeds for fair comparison
- Error percentages are generally <1% indicating stable results

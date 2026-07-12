# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

**Containa** is a high-performance C++ container library providing optimized implementations of common data structures. The library is header-only and requires C++20.

## Build Commands

```bash
# Configure and build (Release mode by default)
cmake -B build && cmake --build build

# Build with Debug mode
cmake -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build

# Build with sanitizers
cmake -B build -DENABLE_ASAN=ON -DENABLE_UBSAN=ON && cmake --build build

# Build with Abseil comparison benchmarks
cmake -B build -DENABLE_ABSL_BENCH=ON && cmake --build build

# Run tests
./build/tests/containa_test

# Run benchmarks
./build/bench/btree_bench
./build/bench/container_bench
./build/bench/skiplist_bench
```

## Architecture

- **Namespace**: `stdb::container` - All container implementations
- **Type Traits**: `stdb::Relocatable`, `stdb::ZeroInitable` - For optimization hints
- **Build System**: CMake 3.20+, C++20

## Code Organization

```
container/
├── small_vectra.hpp    # Small buffer optimized vector
├── devectra.hpp        # Double-ended vector
├── ring_buffer.hpp     # Circular buffer
├── btree_map.hpp       # B-tree based map
├── btree_set.hpp       # B-tree based set
├── skiplist_map.hpp    # Skip list map
├── skiplist_set.hpp    # Skip list set
├── static_vectra.hpp   # Fixed-capacity vector
└── bitmap.hpp          # Bitmap implementation

tests/                  # Test suite (doctest)
bench/                  # Benchmarks (nanobench)
doctest/                # Testing framework
nanobench/              # Benchmark framework
```

## Key Features

- **small_vectra**: Small buffer optimized vector
- **devectra**: Double-ended vector with O(1) push_front/push_back
- **btree_map/set**: Cache-friendly B-tree containers
- **skiplist**: Lock-free concurrent data structures
- **Type optimization**: Special handling for relocatable and zero-initializable types
- **SIMD**: Uses `-march=native` for AVX2/AVX512 optimizations in Release builds

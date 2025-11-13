# Containa

A high-performance C++ container library centered around the `vectra` class - an optimized vector implementation.

## Features

- **High Performance**: Optimized vector implementation with 64-byte default capacity
- **Safety Modes**: Safe and Unsafe variants for performance-critical operations
- **Type Optimization**: Special handling for relocatable and zero-initializable types
- **Modern C++**: Requires C++20

## Building

### Requirements

- CMake 3.20 or higher
- C++20 compatible compiler (GCC 10+, Clang 10+, or MSVC 2019+)

### Basic Build

```bash
cmake -B build
cmake --build build
```

### Build Types

- **Release** (default): Optimized build with `-O3`
- **Debug**: Debug build with symbols and no optimization

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

## Testing

### Running Tests

```bash
cmake --build build --target containa_test
build/tests/containa_test
```

### Memory Leak Detection with AddressSanitizer (ASAN)

The project supports AddressSanitizer for detecting memory leaks, buffer overflows, use-after-free, and other memory errors.

#### Build with ASAN

```bash
# Using Clang (recommended for ASAN)
CC=clang CXX=clang++ cmake -B build -DENABLE_ASAN=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target containa_test

# Or with GCC
cmake -B build -DENABLE_ASAN=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target containa_test
```

#### Run Tests with ASAN

```bash
build/tests/containa_test
```

ASAN will automatically report any memory errors detected during test execution. If memory issues are found, ASAN will:
- Print detailed error reports with stack traces
- Exit with a non-zero status code
- Show the exact location and type of memory error

#### Additional Sanitizers

**UndefinedBehaviorSanitizer (UBSan)**: Detects undefined behavior

```bash
cmake -B build -DENABLE_UBSAN=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build
build/tests/containa_test
```

**Both ASAN and UBSan**: Enable both sanitizers together

```bash
cmake -B build -DENABLE_ASAN=ON -DENABLE_UBSAN=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build
build/tests/containa_test
```

### ASAN Environment Variables

You can customize ASAN behavior using environment variables:

```bash
# Detect memory leaks
ASAN_OPTIONS=detect_leaks=1 build/tests/containa_test

# Continue after first error (useful for finding multiple issues)
ASAN_OPTIONS=halt_on_error=0 build/tests/containa_test

# More verbose output
ASAN_OPTIONS=verbosity=1 build/tests/containa_test
```

## Benchmarks

```bash
cmake --build build --target vb
build/bench/vb
```

Benchmarks also support ASAN for memory leak detection during performance testing.

## Project Structure

- `vectra.hpp` - Main library implementation (header-only)
- `tests/` - Test suite using doctest
- `bench/` - Performance benchmarks using nanobench
- `doctest/` - Doctest framework (git submodule)
- `nanobench/` - Nanobench framework (git submodule)

## License

Apache License 2.0 - See LICENSE file for details.

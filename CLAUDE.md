# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is **Containa**, a high-performance C++ container library centered around the `vectra` class - an optimized vector implementation. The main component is `vectra.hpp`, a single-header library that provides the `stdb::container::vectra` template class.

## Architecture

- **Core Library**: `vectra.hpp` - Single header containing the entire `vectra` implementation
- **Namespace Structure**: 
  - `stdb` - Base namespace containing type traits (`Relocatable`, `ZeroInitable`)
  - `stdb::container` - Container implementations, primarily `vectra`
- **Dependencies**: 
  - Missing `assert_config.hpp` (referenced but not present in codebase)
  - Missing `container/vectra.hpp` (referenced by tests/benchmarks but not present)

## Key Features

The `vectra` is designed as a high-performance replacement for `std::vector` with:
- **Memory optimization** with 64-byte default capacity
- **Safety modes**: Safe and Unsafe variants for performance-critical operations
- **Type optimization**: Special handling for relocatable and zero-initializable types
- **Advanced operations**: `get_writebuffer()`, unsafe variants of standard operations
- **Template specialization**: Optimized paths for trivially copyable types

## Build and Test Commands

The codebase currently lacks build configuration files. Based on the structure:

- **Test Framework**: Uses doctest (referenced in `tests/vectra_test.cc`)
- **Benchmark Framework**: Uses nanobench (referenced in `bench/vector_bench.cc`)

To work with this codebase you'll need to:
1. Create appropriate build system (CMakeLists.txt, Makefile, or meson.build)
2. Provide missing header files (`assert_config.hpp`, `container/vectra.hpp`)
3. Set up nanobench and doctest dependencies

## Code Organization

- `/vectra.hpp` - Main library implementation
- `/tests/vectra_test.cc` - Test suite using doctest
- `/bench/vector_bench.cc` - Performance benchmarks using nanobench  
- `/LICENSE` - Apache License 2.0

## Important Notes

- The project references missing header files that need to be created or paths corrected
- No build system configuration present - needs to be added for compilation
- The `vectra` supports both safe and unsafe operations via template parameters
- Extensive type trait system for optimization based on type characteristics
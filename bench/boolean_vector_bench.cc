/*
 * Copyright 2025 ClapDB, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define ANKERL_NANOBENCH_IMPLEMENT
#include <algorithm>
#include <array>
#include <cstring>
#include <iostream>
#include <random>
#include <vector>

#include "../nanobench/src/include/nanobench.h"
#include "container/boolean_vector.hpp"

using namespace stdb::container;

// NOLINTBEGIN

constexpr size_t kSmallSize = 64;
constexpr size_t kMediumSize = 1024;
constexpr size_t kLargeSize = 1024 * 64;   // 64K
constexpr size_t kHugeSize = 1024 * 1024;  // 1M

// =============================================================================
// Sequential read benchmarks
// =============================================================================

template <size_t N>
void bench_sequential_read_array_bool() {
    std::array<bool, N> arr;
    arr.fill(true);
    for (size_t i = 0; i < N; i += 2) {
        arr[i] = false;
    }

    size_t count = 0;
    for (size_t i = 0; i < N; ++i) {
        if (arr[i]) ++count;
    }
    ankerl::nanobench::doNotOptimizeAway(count);
}

template <size_t N>
void bench_sequential_read_boolean_vector() {
    boolean_vector vec(N, true);
    for (size_t i = 0; i < N; i += 2) {
        vec[i] = false;
    }

    size_t count = 0;
    for (size_t i = 0; i < N; ++i) {
        if (vec[i]) ++count;
    }
    ankerl::nanobench::doNotOptimizeAway(count);
}

// =============================================================================
// Sequential write benchmarks
// =============================================================================

template <size_t N>
void bench_sequential_write_array_bool() {
    std::array<bool, N> arr;
    for (size_t i = 0; i < N; ++i) {
        arr[i] = (i % 2 == 0);
    }
    ankerl::nanobench::doNotOptimizeAway(arr);
}

template <size_t N>
void bench_sequential_write_boolean_vector() {
    boolean_vector vec(N);
    for (size_t i = 0; i < N; ++i) {
        vec[i] = (i % 2 == 0);
    }
    ankerl::nanobench::doNotOptimizeAway(vec);
}

// =============================================================================
// Random access read benchmarks (pre-constructed)
// =============================================================================

template <size_t N>
void bench_random_access_array_bool(const std::vector<size_t>& indices, std::array<bool, N>& arr) {
    size_t count = 0;
    for (size_t idx : indices) {
        if (arr[idx]) ++count;
    }
    ankerl::nanobench::doNotOptimizeAway(count);
}

template <size_t N>
void bench_random_access_boolean_vector(const std::vector<size_t>& indices, boolean_vector& vec) {
    size_t count = 0;
    for (size_t idx : indices) {
        if (vec[idx]) ++count;
    }
    ankerl::nanobench::doNotOptimizeAway(count);
}

// =============================================================================
// Random write benchmarks (pre-constructed)
// =============================================================================

template <size_t N>
void bench_random_write_array_bool(const std::vector<size_t>& indices, std::array<bool, N>& arr) {
    for (size_t idx : indices) {
        arr[idx] = true;
    }
    ankerl::nanobench::doNotOptimizeAway(arr);
}

template <size_t N>
void bench_random_write_boolean_vector(const std::vector<size_t>& indices, boolean_vector& vec) {
    for (size_t idx : indices) {
        vec[idx] = true;
    }
    ankerl::nanobench::doNotOptimizeAway(vec);
}

// =============================================================================
// Fill benchmarks
// =============================================================================

template <size_t N>
void bench_fill_array_bool() {
    std::array<bool, N> arr;
    arr.fill(true);
    ankerl::nanobench::doNotOptimizeAway(arr);
}

template <size_t N>
void bench_fill_boolean_vector() {
    boolean_vector vec(N, true);
    ankerl::nanobench::doNotOptimizeAway(vec);
}

// =============================================================================
// Count true benchmarks (loop-based)
// =============================================================================

template <size_t N>
void bench_count_loop_array_bool() {
    std::array<bool, N> arr;
    for (size_t i = 0; i < N; ++i) {
        arr[i] = (i % 3 == 0);
    }

    size_t count = 0;
    for (size_t i = 0; i < N; ++i) {
        if (arr[i]) ++count;
    }
    ankerl::nanobench::doNotOptimizeAway(count);
}

template <size_t N>
void bench_count_loop_boolean_vector() {
    boolean_vector vec(N);
    for (size_t i = 0; i < N; ++i) {
        vec[i] = (i % 3 == 0);
    }

    size_t count = 0;
    for (size_t i = 0; i < N; ++i) {
        if (vec[i]) ++count;
    }
    ankerl::nanobench::doNotOptimizeAway(count);
}

// =============================================================================
// Count true benchmarks (SIMD optimized count() method)
// =============================================================================

template <size_t N>
void bench_count_simd_boolean_vector() {
    boolean_vector vec(N);
    for (size_t i = 0; i < N; ++i) {
        vec[i] = (i % 3 == 0);
    }

    size_t count = vec.count();
    ankerl::nanobench::doNotOptimizeAway(count);
}

template <size_t N>
void bench_count_std_count_array() {
    std::array<bool, N> arr;
    for (size_t i = 0; i < N; ++i) {
        arr[i] = (i % 3 == 0);
    }

    size_t count = std::count(arr.begin(), arr.end(), true);
    ankerl::nanobench::doNotOptimizeAway(count);
}

// =============================================================================
// Test/check exist benchmarks
// =============================================================================

template <size_t N>
void bench_test_array_bool(const std::vector<size_t>& indices) {
    std::array<bool, N> arr;
    for (size_t i = 0; i < N; ++i) {
        arr[i] = (i % 3 == 0);
    }

    size_t count = 0;
    for (size_t idx : indices) {
        if (arr[idx]) ++count;
    }
    ankerl::nanobench::doNotOptimizeAway(count);
}

template <size_t N>
void bench_test_boolean_vector(const std::vector<size_t>& indices) {
    boolean_vector vec(N);
    for (size_t i = 0; i < N; ++i) {
        vec[i] = (i % 3 == 0);
    }

    size_t count = 0;
    for (size_t idx : indices) {
        if (vec.test(idx)) ++count;
    }
    ankerl::nanobench::doNotOptimizeAway(count);
}

// =============================================================================
// any/all/none benchmarks
// =============================================================================

template <size_t N>
void bench_any_array_bool() {
    std::array<bool, N> arr;
    arr.fill(false);
    arr[N - 1] = true;  // Only last element is true

    bool result = false;
    for (size_t i = 0; i < N; ++i) {
        if (arr[i]) { result = true; break; }
    }
    ankerl::nanobench::doNotOptimizeAway(result);
}

template <size_t N>
void bench_any_boolean_vector() {
    boolean_vector vec(N, false);
    vec[N - 1] = true;  // Only last element is true

    bool result = vec.any();
    ankerl::nanobench::doNotOptimizeAway(result);
}

template <size_t N>
void bench_all_array_bool() {
    std::array<bool, N> arr;
    arr.fill(true);
    arr[N - 1] = false;  // Only last element is false

    bool result = true;
    for (size_t i = 0; i < N; ++i) {
        if (!arr[i]) { result = false; break; }
    }
    ankerl::nanobench::doNotOptimizeAway(result);
}

template <size_t N>
void bench_all_boolean_vector() {
    boolean_vector vec(N, true);
    vec[N - 1] = false;  // Only last element is false

    bool result = vec.all();
    ankerl::nanobench::doNotOptimizeAway(result);
}

// =============================================================================
// Flip/Toggle all benchmarks
// =============================================================================

template <size_t N>
void bench_flip_array_bool() {
    std::array<bool, N> arr;
    arr.fill(true);

    for (size_t i = 0; i < N; ++i) {
        arr[i] = !arr[i];
    }
    ankerl::nanobench::doNotOptimizeAway(arr);
}

template <size_t N>
void bench_flip_boolean_vector() {
    boolean_vector vec(N, true);
    vec.flip();
    ankerl::nanobench::doNotOptimizeAway(vec);
}

// =============================================================================
// Memory size comparison (informational)
// =============================================================================

template <size_t N>
void print_memory_usage() {
    std::cout << "Memory usage for " << N << " bools:\n";
    std::cout << "  std::array<bool, " << N << ">: " << sizeof(std::array<bool, N>) << " bytes\n";
    std::cout << "  boolean_vector(" << N << "): " << N << " bytes (data only)\n";
    std::cout << "  Both use 1 byte per bool (same memory footprint)\n\n";
}

// =============================================================================
// Single operator[] access benchmarks (isolated overhead test)
// Pre-constructed containers for fair comparison
// =============================================================================

// Global pre-constructed containers (to exclude construction from timing)
static std::array<bool, kMediumSize> g_arr_medium;
static std::array<bool, kLargeSize> g_arr_large;
static boolean_vector g_vec_medium(kMediumSize, true);
static boolean_vector g_vec_large(kLargeSize, true);

template <size_t N>
void bench_single_read_array_bool(const std::vector<size_t>& indices, std::array<bool, N>& arr) {
    bool result = false;
    for (size_t idx : indices) {
        result ^= arr[idx];  // XOR to prevent optimization
    }
    ankerl::nanobench::doNotOptimizeAway(result);
}

template <size_t N>
void bench_single_read_boolean_vector(const std::vector<size_t>& indices, boolean_vector& vec) {
    bool result = false;
    for (size_t idx : indices) {
        result ^= vec[idx];  // XOR to prevent optimization
    }
    ankerl::nanobench::doNotOptimizeAway(result);
}

template <size_t N>
void bench_single_write_array_bool(const std::vector<size_t>& indices, std::array<bool, N>& arr) {
    for (size_t idx : indices) {
        arr[idx] = true;
    }
    ankerl::nanobench::doNotOptimizeAway(arr);
}

template <size_t N>
void bench_single_write_boolean_vector(const std::vector<size_t>& indices, boolean_vector& vec) {
    for (size_t idx : indices) {
        vec[idx] = true;
    }
    ankerl::nanobench::doNotOptimizeAway(vec);
}

// =============================================================================
// Copy benchmarks
// =============================================================================

template <size_t N>
void bench_copy_array_bool() {
    std::array<bool, N> src;
    src.fill(true);
    std::array<bool, N> dst;
    std::memcpy(dst.data(), src.data(), N);
    ankerl::nanobench::doNotOptimizeAway(dst);
}

template <size_t N>
void bench_copy_boolean_vector() {
    boolean_vector src(N, true);
    boolean_vector dst(src);
    ankerl::nanobench::doNotOptimizeAway(dst);
}

// =============================================================================
// Main
// =============================================================================

int main() {
    std::cout << "=== boolean_vector vs std::array<bool> (both 1 byte per bool) ===\n\n";

    // Print memory usage comparison
    print_memory_usage<kMediumSize>();
    print_memory_usage<kLargeSize>();

    // Generate random indices for random access tests
    std::mt19937 rng(42);
    std::vector<size_t> indices_medium(kMediumSize);
    std::vector<size_t> indices_large(kLargeSize);

    for (size_t i = 0; i < kMediumSize; ++i) {
        indices_medium[i] = rng() % kMediumSize;
    }
    for (size_t i = 0; i < kLargeSize; ++i) {
        indices_large[i] = rng() % kLargeSize;
    }

    // Initialize global containers
    g_arr_medium.fill(true);
    g_arr_large.fill(true);

    ankerl::nanobench::Bench bench;
    bench.warmup(10).minEpochIterations(100);

    // ----- operator[] read (isolated, pre-constructed) -----
    std::cout << "--- operator[] read (random access, pre-constructed) ---\n";
    bench.run("std::array<bool> operator[] read 1K", [&]{ bench_single_read_array_bool<kMediumSize>(indices_medium, g_arr_medium); });
    bench.run("boolean_vector operator[] read 1K", [&]{ bench_single_read_boolean_vector<kMediumSize>(indices_medium, g_vec_medium); });
    bench.run("std::array<bool> operator[] read 64K", [&]{ bench_single_read_array_bool<kLargeSize>(indices_large, g_arr_large); });
    bench.run("boolean_vector operator[] read 64K", [&]{ bench_single_read_boolean_vector<kLargeSize>(indices_large, g_vec_large); });

    // ----- operator[] write (isolated, pre-constructed) -----
    std::cout << "\n--- operator[] write (random access, pre-constructed) ---\n";
    bench.run("std::array<bool> operator[] write 1K", [&]{ bench_single_write_array_bool<kMediumSize>(indices_medium, g_arr_medium); });
    bench.run("boolean_vector operator[] write 1K", [&]{ bench_single_write_boolean_vector<kMediumSize>(indices_medium, g_vec_medium); });
    bench.run("std::array<bool> operator[] write 64K", [&]{ bench_single_write_array_bool<kLargeSize>(indices_large, g_arr_large); });
    bench.run("boolean_vector operator[] write 64K", [&]{ bench_single_write_boolean_vector<kLargeSize>(indices_large, g_vec_large); });

    // ----- Fill/Initialize -----
    std::cout << "\n--- Fill/Initialize ---\n";
    bench.run("std::array<bool> fill 1K", bench_fill_array_bool<kMediumSize>);
    bench.run("boolean_vector fill 1K", bench_fill_boolean_vector<kMediumSize>);
    bench.run("std::array<bool> fill 64K", bench_fill_array_bool<kLargeSize>);
    bench.run("boolean_vector fill 64K", bench_fill_boolean_vector<kLargeSize>);

    // ----- Sequential read -----
    std::cout << "\n--- Sequential read ---\n";
    bench.run("std::array<bool> seq read 1K", bench_sequential_read_array_bool<kMediumSize>);
    bench.run("boolean_vector seq read 1K", bench_sequential_read_boolean_vector<kMediumSize>);
    bench.run("std::array<bool> seq read 64K", bench_sequential_read_array_bool<kLargeSize>);
    bench.run("boolean_vector seq read 64K", bench_sequential_read_boolean_vector<kLargeSize>);

    // ----- Sequential write -----
    std::cout << "\n--- Sequential write ---\n";
    bench.run("std::array<bool> seq write 1K", bench_sequential_write_array_bool<kMediumSize>);
    bench.run("boolean_vector seq write 1K", bench_sequential_write_boolean_vector<kMediumSize>);
    bench.run("std::array<bool> seq write 64K", bench_sequential_write_array_bool<kLargeSize>);
    bench.run("boolean_vector seq write 64K", bench_sequential_write_boolean_vector<kLargeSize>);

    // ----- Random access read (pre-constructed) -----
    std::cout << "\n--- Random access read (pre-constructed) ---\n";
    bench.run("std::array<bool> random read 1K", [&]{ bench_random_access_array_bool<kMediumSize>(indices_medium, g_arr_medium); });
    bench.run("boolean_vector random read 1K", [&]{ bench_random_access_boolean_vector<kMediumSize>(indices_medium, g_vec_medium); });
    bench.run("std::array<bool> random read 64K", [&]{ bench_random_access_array_bool<kLargeSize>(indices_large, g_arr_large); });
    bench.run("boolean_vector random read 64K", [&]{ bench_random_access_boolean_vector<kLargeSize>(indices_large, g_vec_large); });

    // ----- Random write (pre-constructed) -----
    std::cout << "\n--- Random write (pre-constructed) ---\n";
    bench.run("std::array<bool> random write 1K", [&]{ bench_random_write_array_bool<kMediumSize>(indices_medium, g_arr_medium); });
    bench.run("boolean_vector random write 1K", [&]{ bench_random_write_boolean_vector<kMediumSize>(indices_medium, g_vec_medium); });
    bench.run("std::array<bool> random write 64K", [&]{ bench_random_write_array_bool<kLargeSize>(indices_large, g_arr_large); });
    bench.run("boolean_vector random write 64K", [&]{ bench_random_write_boolean_vector<kLargeSize>(indices_large, g_vec_large); });

    // ----- Test/Check exist -----
    std::cout << "\n--- Test/Check exist (random positions) ---\n";
    bench.run("std::array<bool> test 1K", [&]{ bench_test_array_bool<kMediumSize>(indices_medium); });
    bench.run("boolean_vector test 1K", [&]{ bench_test_boolean_vector<kMediumSize>(indices_medium); });
    bench.run("std::array<bool> test 64K", [&]{ bench_test_array_bool<kLargeSize>(indices_large); });
    bench.run("boolean_vector test 64K", [&]{ bench_test_boolean_vector<kLargeSize>(indices_large); });

    // ----- Count (loop vs SIMD) -----
    std::cout << "\n--- Count true values (loop-based) ---\n";
    bench.run("std::array<bool> count loop 1K", bench_count_loop_array_bool<kMediumSize>);
    bench.run("boolean_vector count loop 1K", bench_count_loop_boolean_vector<kMediumSize>);
    bench.run("std::array<bool> count loop 64K", bench_count_loop_array_bool<kLargeSize>);
    bench.run("boolean_vector count loop 64K", bench_count_loop_boolean_vector<kLargeSize>);

    std::cout << "\n--- Count true values (optimized methods) ---\n";
    bench.run("std::count(array) 1K", bench_count_std_count_array<kMediumSize>);
    bench.run("boolean_vector.count() 1K", bench_count_simd_boolean_vector<kMediumSize>);
    bench.run("std::count(array) 64K", bench_count_std_count_array<kLargeSize>);
    bench.run("boolean_vector.count() 64K", bench_count_simd_boolean_vector<kLargeSize>);

    // ----- any/all -----
    std::cout << "\n--- any() (worst case: last element is true) ---\n";
    bench.run("std::array<bool> any 1K", bench_any_array_bool<kMediumSize>);
    bench.run("boolean_vector any 1K", bench_any_boolean_vector<kMediumSize>);
    bench.run("std::array<bool> any 64K", bench_any_array_bool<kLargeSize>);
    bench.run("boolean_vector any 64K", bench_any_boolean_vector<kLargeSize>);

    std::cout << "\n--- all() (worst case: last element is false) ---\n";
    bench.run("std::array<bool> all 1K", bench_all_array_bool<kMediumSize>);
    bench.run("boolean_vector all 1K", bench_all_boolean_vector<kMediumSize>);
    bench.run("std::array<bool> all 64K", bench_all_array_bool<kLargeSize>);
    bench.run("boolean_vector all 64K", bench_all_boolean_vector<kLargeSize>);

    // ----- Flip -----
    std::cout << "\n--- Flip all bits ---\n";
    bench.run("std::array<bool> flip 1K", bench_flip_array_bool<kMediumSize>);
    bench.run("boolean_vector flip 1K", bench_flip_boolean_vector<kMediumSize>);
    bench.run("std::array<bool> flip 64K", bench_flip_array_bool<kLargeSize>);
    bench.run("boolean_vector flip 64K", bench_flip_boolean_vector<kLargeSize>);

    // ----- Copy -----
    std::cout << "\n--- Copy ---\n";
    bench.run("std::array<bool> copy 1K", bench_copy_array_bool<kMediumSize>);
    bench.run("boolean_vector copy 1K", bench_copy_boolean_vector<kMediumSize>);
    bench.run("std::array<bool> copy 64K", bench_copy_array_bool<kLargeSize>);
    bench.run("boolean_vector copy 64K", bench_copy_boolean_vector<kLargeSize>);

    return 0;
}

// NOLINTEND

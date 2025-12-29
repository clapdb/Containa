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
#include "../nanobench/src/include/nanobench.h"

#include <iostream>
#include <random>
#include <string>
#include <vector>

#include "container/btree_map.hpp"

#ifdef ENABLE_ABSL
#include <absl/container/btree_map.h>
#endif

using namespace stdb::container;

template <size_t N>
void run_int_benchmark(const std::string& label) {
    std::vector<int> random_keys(N);
    std::vector<int> sorted_keys(N);
    for (size_t i = 0; i < N; ++i) sorted_keys[i] = static_cast<int>(i);
    random_keys = sorted_keys;
    std::mt19937 rng(42);
    std::shuffle(random_keys.begin(), random_keys.end(), rng);

    ankerl::nanobench::Bench bench;
    bench.warmup(2).epochs(3).relative(true);

    std::cout << "\n=== Integer Keys: " << label << " (" << N << " elements) ===\n";

    // Sorted insert
    bench.run("stdb sorted insert", [&] {
        btree_map<int, int> map;
        for (int k : sorted_keys) map[k] = k;
        ankerl::nanobench::doNotOptimizeAway(map);
    });
#ifdef ENABLE_ABSL
    bench.run("absl sorted insert", [&] {
        absl::btree_map<int, int> map;
        for (int k : sorted_keys) map[k] = k;
        ankerl::nanobench::doNotOptimizeAway(map);
    });
#endif

    // Random insert
    bench.run("stdb random insert", [&] {
        btree_map<int, int> map;
        for (int k : random_keys) map[k] = k;
        ankerl::nanobench::doNotOptimizeAway(map);
    });
#ifdef ENABLE_ABSL
    bench.run("absl random insert", [&] {
        absl::btree_map<int, int> map;
        for (int k : random_keys) map[k] = k;
        ankerl::nanobench::doNotOptimizeAway(map);
    });
#endif

    // Prepare maps
    btree_map<int, int> stdb_map;
    for (int k : random_keys) {
        stdb_map[k] = k;
    }
#ifdef ENABLE_ABSL
    absl::btree_map<int, int> absl_map;
    for (int k : random_keys) {
        absl_map[k] = k;
    }
#endif

    // Find
    bench.run("stdb find", [&] {
        int sum = 0;
        for (int k : random_keys) {
            auto it = stdb_map.find(k);
            if (it != stdb_map.end()) sum += it->second;
        }
        ankerl::nanobench::doNotOptimizeAway(sum);
    });
#ifdef ENABLE_ABSL
    bench.run("absl find", [&] {
        int sum = 0;
        for (int k : random_keys) {
            auto it = absl_map.find(k);
            if (it != absl_map.end()) sum += it->second;
        }
        ankerl::nanobench::doNotOptimizeAway(sum);
    });
#endif

    // Iterate
    bench.run("stdb iterate", [&] {
        int sum = 0;
        for (auto& [k, v] : stdb_map) sum += v;
        ankerl::nanobench::doNotOptimizeAway(sum);
    });
#ifdef ENABLE_ABSL
    bench.run("absl iterate", [&] {
        int sum = 0;
        for (auto& [k, v] : absl_map) sum += v;
        ankerl::nanobench::doNotOptimizeAway(sum);
    });
#endif

    // Erase
    bench.run("stdb erase", [&] {
        btree_map<int, int> map;
        for (int k : random_keys) map[k] = k;
        for (int k : random_keys) map.erase(k);
        ankerl::nanobench::doNotOptimizeAway(map);
    });
#ifdef ENABLE_ABSL
    bench.run("absl erase", [&] {
        absl::btree_map<int, int> map;
        for (int k : random_keys) map[k] = k;
        for (int k : random_keys) map.erase(k);
        ankerl::nanobench::doNotOptimizeAway(map);
    });
#endif
}

template <size_t N>
void run_string_benchmark(const std::string& label) {
    std::vector<std::string> random_keys(N);
    std::vector<std::string> sorted_keys(N);

    for (size_t i = 0; i < N; ++i) {
        sorted_keys[i] = "key_" + std::to_string(i);
    }
    random_keys = sorted_keys;
    std::mt19937 rng(42);
    std::shuffle(random_keys.begin(), random_keys.end(), rng);

    // Sort for sorted insert test
    std::vector<std::string> sorted_string_keys = random_keys;
    std::sort(sorted_string_keys.begin(), sorted_string_keys.end());

    ankerl::nanobench::Bench bench;
    bench.warmup(2).epochs(3).relative(true);

    std::cout << "\n=== String Keys: " << label << " (" << N << " elements) ===\n";

    // Sorted insert
    bench.run("stdb sorted insert", [&] {
        btree_map<std::string, int> map;
        int i = 0;
        for (const auto& k : sorted_string_keys) map[k] = i++;
        ankerl::nanobench::doNotOptimizeAway(map);
    });
#ifdef ENABLE_ABSL
    bench.run("absl sorted insert", [&] {
        absl::btree_map<std::string, int> map;
        int i = 0;
        for (const auto& k : sorted_string_keys) map[k] = i++;
        ankerl::nanobench::doNotOptimizeAway(map);
    });
#endif

    // Random insert
    bench.run("stdb random insert", [&] {
        btree_map<std::string, int> map;
        int i = 0;
        for (const auto& k : random_keys) map[k] = i++;
        ankerl::nanobench::doNotOptimizeAway(map);
    });
#ifdef ENABLE_ABSL
    bench.run("absl random insert", [&] {
        absl::btree_map<std::string, int> map;
        int i = 0;
        for (const auto& k : random_keys) map[k] = i++;
        ankerl::nanobench::doNotOptimizeAway(map);
    });
#endif

    // Prepare maps
    btree_map<std::string, int> stdb_map;
    int idx = 0;
    for (const auto& k : random_keys) {
        stdb_map[k] = idx++;
    }
#ifdef ENABLE_ABSL
    absl::btree_map<std::string, int> absl_map;
    idx = 0;
    for (const auto& k : random_keys) {
        absl_map[k] = idx++;
    }
#endif

    // Find
    bench.run("stdb find", [&] {
        int sum = 0;
        for (const auto& k : random_keys) {
            auto it = stdb_map.find(k);
            if (it != stdb_map.end()) sum += it->second;
        }
        ankerl::nanobench::doNotOptimizeAway(sum);
    });
#ifdef ENABLE_ABSL
    bench.run("absl find", [&] {
        int sum = 0;
        for (const auto& k : random_keys) {
            auto it = absl_map.find(k);
            if (it != absl_map.end()) sum += it->second;
        }
        ankerl::nanobench::doNotOptimizeAway(sum);
    });
#endif

    // Iterate
    bench.run("stdb iterate", [&] {
        int sum = 0;
        for (auto& [k, v] : stdb_map) sum += v;
        ankerl::nanobench::doNotOptimizeAway(sum);
    });
#ifdef ENABLE_ABSL
    bench.run("absl iterate", [&] {
        int sum = 0;
        for (auto& [k, v] : absl_map) sum += v;
        ankerl::nanobench::doNotOptimizeAway(sum);
    });
#endif
}

int main(int argc, char** argv) {
    bool run_large = (argc > 1 && std::string(argv[1]) == "--large");

    std::cout << "============================================================\n";
    std::cout << "Containa btree_map Benchmark\n";
    std::cout << "============================================================\n";
    std::cout << "Compiler: " <<
#if defined(__clang__)
        "Clang " << __clang_major__ << "." << __clang_minor__
#elif defined(__GNUC__)
        "GCC " << __GNUC__ << "." << __GNUC_MINOR__
#else
        "Unknown"
#endif
        << "\n";
    std::cout << "Standard Library: " <<
#if defined(_LIBCPP_VERSION)
        "libc++ " << _LIBCPP_VERSION
#elif defined(__GLIBCXX__)
        "libstdc++ " << __GLIBCXX__
#else
        "Unknown"
#endif
        << "\n";
#ifdef ENABLE_ABSL
    std::cout << "Abseil: Enabled\n";
#else
    std::cout << "Abseil: Disabled (use -DENABLE_ABSL_BENCH=ON to enable)\n";
#endif
    std::cout << "============================================================\n";

    // Standard benchmarks
    run_int_benchmark<10000>("10K");
    run_int_benchmark<100000>("100K");
    run_string_benchmark<10000>("10K");
    run_string_benchmark<100000>("100K");

    // Large scale benchmarks (optional)
    if (run_large) {
        run_int_benchmark<1000000>("1M");
        run_int_benchmark<10000000>("10M");
        run_string_benchmark<1000000>("1M");
    }

    return 0;
}

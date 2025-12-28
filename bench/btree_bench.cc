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
#include <absl/container/btree_map.h>

#include <arena/arena.hpp>
#include <iostream>
#include <memory_resource>
#include <random>
#include <string>
#include <vector>

#include "../nanobench/src/include/nanobench.h"
#include "container/btree_map.hpp"

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
    bench.run("absl sorted insert", [&] {
        absl::btree_map<int, int> map;
        for (int k : sorted_keys) map[k] = k;
        ankerl::nanobench::doNotOptimizeAway(map);
    });

    // Random insert
    bench.run("stdb random insert", [&] {
        btree_map<int, int> map;
        for (int k : random_keys) map[k] = k;
        ankerl::nanobench::doNotOptimizeAway(map);
    });
    bench.run("absl random insert", [&] {
        absl::btree_map<int, int> map;
        for (int k : random_keys) map[k] = k;
        ankerl::nanobench::doNotOptimizeAway(map);
    });

    // Prepare maps
    btree_map<int, int> stdb_map;
    absl::btree_map<int, int> absl_map;
    for (int k : random_keys) {
        stdb_map[k] = k;
        absl_map[k] = k;
    }

    // Find
    bench.run("stdb find", [&] {
        int sum = 0;
        for (int k : random_keys) {
            auto it = stdb_map.find(k);
            if (it != stdb_map.end()) sum += it->second;
        }
        ankerl::nanobench::doNotOptimizeAway(sum);
    });
    bench.run("absl find", [&] {
        int sum = 0;
        for (int k : random_keys) {
            auto it = absl_map.find(k);
            if (it != absl_map.end()) sum += it->second;
        }
        ankerl::nanobench::doNotOptimizeAway(sum);
    });

    // Iterate
    bench.run("stdb iterate", [&] {
        int sum = 0;
        for (auto& [k, v] : stdb_map) sum += v;
        ankerl::nanobench::doNotOptimizeAway(sum);
    });
    bench.run("absl iterate", [&] {
        int sum = 0;
        for (auto& [k, v] : absl_map) sum += v;
        ankerl::nanobench::doNotOptimizeAway(sum);
    });

    // Erase
    bench.run("stdb erase", [&] {
        btree_map<int, int> map;
        for (int k : random_keys) map[k] = k;
        for (int k : random_keys) map.erase(k);
        ankerl::nanobench::doNotOptimizeAway(map);
    });
    bench.run("absl erase", [&] {
        absl::btree_map<int, int> map;
        for (int k : random_keys) map[k] = k;
        for (int k : random_keys) map.erase(k);
        ankerl::nanobench::doNotOptimizeAway(map);
    });
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
    bench.run("absl sorted insert", [&] {
        absl::btree_map<std::string, int> map;
        int i = 0;
        for (const auto& k : sorted_string_keys) map[k] = i++;
        ankerl::nanobench::doNotOptimizeAway(map);
    });

    // Random insert
    bench.run("stdb random insert", [&] {
        btree_map<std::string, int> map;
        int i = 0;
        for (const auto& k : random_keys) map[k] = i++;
        ankerl::nanobench::doNotOptimizeAway(map);
    });
    bench.run("absl random insert", [&] {
        absl::btree_map<std::string, int> map;
        int i = 0;
        for (const auto& k : random_keys) map[k] = i++;
        ankerl::nanobench::doNotOptimizeAway(map);
    });

    // Prepare maps
    btree_map<std::string, int> stdb_map;
    absl::btree_map<std::string, int> absl_map;
    int i = 0;
    for (const auto& k : random_keys) {
        stdb_map[k] = i;
        absl_map[k] = i++;
    }

    // Find
    bench.run("stdb find", [&] {
        int sum = 0;
        for (const auto& k : random_keys) {
            auto it = stdb_map.find(k);
            if (it != stdb_map.end()) sum += it->second;
        }
        ankerl::nanobench::doNotOptimizeAway(sum);
    });
    bench.run("absl find", [&] {
        int sum = 0;
        for (const auto& k : random_keys) {
            auto it = absl_map.find(k);
            if (it != absl_map.end()) sum += it->second;
        }
        ankerl::nanobench::doNotOptimizeAway(sum);
    });

    // Iterate
    bench.run("stdb iterate", [&] {
        int sum = 0;
        for (auto& [k, v] : stdb_map) sum += v;
        ankerl::nanobench::doNotOptimizeAway(sum);
    });
    bench.run("absl iterate", [&] {
        int sum = 0;
        for (auto& [k, v] : absl_map) sum += v;
        ankerl::nanobench::doNotOptimizeAway(sum);
    });
}

template <size_t N>
void run_arena_benchmark(const std::string& label) {
    std::vector<int> random_keys(N);
    std::vector<int> sorted_keys(N);
    for (size_t i = 0; i < N; ++i) sorted_keys[i] = static_cast<int>(i);
    random_keys = sorted_keys;
    std::mt19937 rng(42);
    std::shuffle(random_keys.begin(), random_keys.end(), rng);

    ankerl::nanobench::Bench bench;
    bench.warmup(2).epochs(3).relative(true);

    std::cout << "\n=== Arena Allocator: " << label << " (" << N << " elements) ===\n";

    using PmrAlloc = std::pmr::polymorphic_allocator<std::pair<const int, int>>;

    // Default allocator - sorted insert
    bench.run("stdb default sorted", [&] {
        btree_map<int, int> map;
        for (int k : sorted_keys) map[k] = k;
        ankerl::nanobench::doNotOptimizeAway(map);
    });

    // Arena allocator - sorted insert
    bench.run("stdb arena sorted", [&] {
        arena::Arena ar(arena::Arena::Options::GetDefaultOptions());
        btree_map<int, int, std::less<int>, PmrAlloc> map(PmrAlloc(ar.get_memory_resource()));
        for (int k : sorted_keys) map[k] = k;
        ankerl::nanobench::doNotOptimizeAway(map);
    });

    // Default allocator - random insert
    bench.run("stdb default random", [&] {
        btree_map<int, int> map;
        for (int k : random_keys) map[k] = k;
        ankerl::nanobench::doNotOptimizeAway(map);
    });

    // Arena allocator - random insert
    bench.run("stdb arena random", [&] {
        arena::Arena ar(arena::Arena::Options::GetDefaultOptions());
        btree_map<int, int, std::less<int>, PmrAlloc> map(PmrAlloc(ar.get_memory_resource()));
        for (int k : random_keys) map[k] = k;
        ankerl::nanobench::doNotOptimizeAway(map);
    });

    // Prepare maps for find/iterate benchmarks
    arena::Arena ar_arena(arena::Arena::Options::GetDefaultOptions());
    btree_map<int, int, std::less<int>, PmrAlloc> arena_map(PmrAlloc(ar_arena.get_memory_resource()));
    btree_map<int, int> default_map;
    for (int k : random_keys) {
        arena_map[k] = k;
        default_map[k] = k;
    }

    // Find
    bench.run("stdb default find", [&] {
        int sum = 0;
        for (int k : random_keys) {
            auto it = default_map.find(k);
            if (it != default_map.end()) sum += it->second;
        }
        ankerl::nanobench::doNotOptimizeAway(sum);
    });
    bench.run("stdb arena find", [&] {
        int sum = 0;
        for (int k : random_keys) {
            auto it = arena_map.find(k);
            if (it != arena_map.end()) sum += it->second;
        }
        ankerl::nanobench::doNotOptimizeAway(sum);
    });

    // Iterate
    bench.run("stdb default iterate", [&] {
        int sum = 0;
        for (auto& [k, v] : default_map) sum += v;
        ankerl::nanobench::doNotOptimizeAway(sum);
    });
    bench.run("stdb arena iterate", [&] {
        int sum = 0;
        for (auto& [k, v] : arena_map) sum += v;
        ankerl::nanobench::doNotOptimizeAway(sum);
    });
}

int main(int argc, char** argv) {
    bool run_large = (argc > 1 && std::string(argv[1]) == "--large");
    bool run_arena = (argc > 1 && std::string(argv[1]) == "--arena");

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

    // Arena allocator benchmarks
    if (run_arena) {
        run_arena_benchmark<10000>("10K");
        run_arena_benchmark<100000>("100K");
        if (run_large) {
            run_arena_benchmark<1000000>("1M");
        }
    }

    return 0;
}

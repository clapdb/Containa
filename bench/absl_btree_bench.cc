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

/*
 * Benchmark comparing Containa containers vs Abseil btree
 *
 * Containers compared:
 * - Containa btree_map vs absl::btree_map
 * - Containa skiplist_map vs absl::btree_map
 *
 * Operations tested:
 * - Sequential insert
 * - Random insert
 * - Sequential lookup
 * - Random lookup
 * - Iteration
 * - Erase
 */

#define ANKERL_NANOBENCH_IMPLEMENT
#include "nanobench/src/include/nanobench.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <map>
#include <random>
#include <string>
#include <vector>

// Containa containers
#include "container/btree_map.hpp"
#include "container/skiplist_map.hpp"

// Abseil containers
#include "absl/container/btree_map.h"
#include "absl/container/flat_hash_map.h"

using namespace stdb::container;

// Benchmark configuration
constexpr size_t SMALL_SIZE = 100;
constexpr size_t MEDIUM_SIZE = 10'000;
constexpr size_t LARGE_SIZE = 1'000'000;

// Generate random keys
std::vector<int64_t> generate_random_keys(size_t n, uint64_t seed = 12345) {
    std::vector<int64_t> keys(n);
    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<int64_t> dist(0, static_cast<int64_t>(n) * 10);
    for (size_t i = 0; i < n; ++i) {
        keys[i] = dist(rng);
    }
    return keys;
}

// Generate sequential keys
std::vector<int64_t> generate_sequential_keys(size_t n) {
    std::vector<int64_t> keys(n);
    for (size_t i = 0; i < n; ++i) {
        keys[i] = static_cast<int64_t>(i);
    }
    return keys;
}

// Shuffle keys for random access
std::vector<int64_t> shuffle_keys(const std::vector<int64_t>& keys, uint64_t seed = 54321) {
    std::vector<int64_t> shuffled = keys;
    std::mt19937_64 rng(seed);
    std::shuffle(shuffled.begin(), shuffled.end(), rng);
    return shuffled;
}

template <typename Map>
void benchmark_sequential_insert(ankerl::nanobench::Bench& bench, const std::string& name, size_t n) {
    auto keys = generate_sequential_keys(n);

    bench.run(name + " sequential insert", [&] {
        Map map;
        for (auto key : keys) {
            map[key] = key;
        }
        ankerl::nanobench::doNotOptimizeAway(map.size());
    });
}

template <typename Map>
void benchmark_random_insert(ankerl::nanobench::Bench& bench, const std::string& name, size_t n) {
    auto keys = generate_random_keys(n);

    bench.run(name + " random insert", [&] {
        Map map;
        for (auto key : keys) {
            map[key] = key;
        }
        ankerl::nanobench::doNotOptimizeAway(map.size());
    });
}

template <typename Map>
void benchmark_lookup(ankerl::nanobench::Bench& bench, const std::string& name, size_t n) {
    auto keys = generate_sequential_keys(n);
    auto lookup_keys = shuffle_keys(keys);

    Map map;
    for (auto key : keys) {
        map[key] = key;
    }

    bench.run(name + " random lookup", [&] {
        int64_t sum = 0;
        for (auto key : lookup_keys) {
            auto it = map.find(key);
            if (it != map.end()) {
                sum += it->second;
            }
        }
        ankerl::nanobench::doNotOptimizeAway(sum);
    });
}

template <typename Map>
void benchmark_iteration(ankerl::nanobench::Bench& bench, const std::string& name, size_t n) {
    auto keys = generate_random_keys(n);

    Map map;
    for (auto key : keys) {
        map[key] = key;
    }

    bench.run(name + " iteration", [&] {
        int64_t sum = 0;
        for (const auto& [k, v] : map) {
            sum += v;
        }
        ankerl::nanobench::doNotOptimizeAway(sum);
    });
}

template <typename Map>
void benchmark_erase(ankerl::nanobench::Bench& bench, const std::string& name, size_t n) {
    auto keys = generate_sequential_keys(n);
    auto erase_keys = shuffle_keys(keys);

    bench.run(name + " random erase", [&] {
        Map map;
        for (auto key : keys) {
            map[key] = key;
        }
        for (auto key : erase_keys) {
            map.erase(key);
        }
        ankerl::nanobench::doNotOptimizeAway(map.size());
    });
}

template <typename Map>
void benchmark_mixed_workload(ankerl::nanobench::Bench& bench, const std::string& name, size_t n) {
    // 80% read, 10% insert, 10% erase
    auto keys = generate_random_keys(n);
    std::mt19937_64 rng(98765);
    std::uniform_int_distribution<int> op_dist(0, 99);
    std::uniform_int_distribution<int64_t> key_dist(0, static_cast<int64_t>(n) * 10);

    // Pre-populate with half the keys
    Map map;
    for (size_t i = 0; i < n / 2; ++i) {
        map[keys[i]] = keys[i];
    }

    bench.run(name + " mixed (80r/10i/10e)", [&] {
        int64_t sum = 0;
        for (size_t i = 0; i < n; ++i) {
            int op = op_dist(rng);
            int64_t key = key_dist(rng);
            if (op < 80) {
                // Read
                auto it = map.find(key);
                if (it != map.end()) {
                    sum += it->second;
                }
            } else if (op < 90) {
                // Insert
                map[key] = key;
            } else {
                // Erase
                map.erase(key);
            }
        }
        ankerl::nanobench::doNotOptimizeAway(sum);
        ankerl::nanobench::doNotOptimizeAway(map.size());
    });
}

void run_benchmarks(size_t n, const std::string& size_name) {
    std::cout << "\n========================================\n";
    std::cout << "Benchmark: " << size_name << " (" << n << " elements)\n";
    std::cout << "========================================\n";

    ankerl::nanobench::Bench bench;
    bench.title(size_name)
         .warmup(3)
         .epochs(10)
         .minEpochIterations(1);

    // Sequential Insert
    std::cout << "\n--- Sequential Insert ---\n";
    benchmark_sequential_insert<btree_map<int64_t, int64_t>>(bench, "Containa btree_map", n);
    benchmark_sequential_insert<skiplist_map<int64_t, int64_t>>(bench, "Containa skiplist_map", n);
    benchmark_sequential_insert<absl::btree_map<int64_t, int64_t>>(bench, "absl::btree_map", n);
    benchmark_sequential_insert<std::map<int64_t, int64_t>>(bench, "std::map", n);

    // Random Insert
    std::cout << "\n--- Random Insert ---\n";
    benchmark_random_insert<btree_map<int64_t, int64_t>>(bench, "Containa btree_map", n);
    benchmark_random_insert<skiplist_map<int64_t, int64_t>>(bench, "Containa skiplist_map", n);
    benchmark_random_insert<absl::btree_map<int64_t, int64_t>>(bench, "absl::btree_map", n);
    benchmark_random_insert<std::map<int64_t, int64_t>>(bench, "std::map", n);

    // Random Lookup
    std::cout << "\n--- Random Lookup ---\n";
    benchmark_lookup<btree_map<int64_t, int64_t>>(bench, "Containa btree_map", n);
    benchmark_lookup<skiplist_map<int64_t, int64_t>>(bench, "Containa skiplist_map", n);
    benchmark_lookup<absl::btree_map<int64_t, int64_t>>(bench, "absl::btree_map", n);
    benchmark_lookup<std::map<int64_t, int64_t>>(bench, "std::map", n);

    // Iteration
    std::cout << "\n--- Iteration ---\n";
    benchmark_iteration<btree_map<int64_t, int64_t>>(bench, "Containa btree_map", n);
    benchmark_iteration<skiplist_map<int64_t, int64_t>>(bench, "Containa skiplist_map", n);
    benchmark_iteration<absl::btree_map<int64_t, int64_t>>(bench, "absl::btree_map", n);
    benchmark_iteration<std::map<int64_t, int64_t>>(bench, "std::map", n);

    // Erase
    std::cout << "\n--- Random Erase ---\n";
    benchmark_erase<btree_map<int64_t, int64_t>>(bench, "Containa btree_map", n);
    benchmark_erase<skiplist_map<int64_t, int64_t>>(bench, "Containa skiplist_map", n);
    benchmark_erase<absl::btree_map<int64_t, int64_t>>(bench, "absl::btree_map", n);
    benchmark_erase<std::map<int64_t, int64_t>>(bench, "std::map", n);

    // Mixed workload
    std::cout << "\n--- Mixed Workload ---\n";
    benchmark_mixed_workload<btree_map<int64_t, int64_t>>(bench, "Containa btree_map", n);
    benchmark_mixed_workload<skiplist_map<int64_t, int64_t>>(bench, "Containa skiplist_map", n);
    benchmark_mixed_workload<absl::btree_map<int64_t, int64_t>>(bench, "absl::btree_map", n);
    benchmark_mixed_workload<std::map<int64_t, int64_t>>(bench, "std::map", n);
}

void run_string_benchmarks(size_t n) {
    std::cout << "\n========================================\n";
    std::cout << "String Key Benchmark (" << n << " elements)\n";
    std::cout << "========================================\n";

    // Generate string keys
    std::vector<std::string> keys(n);
    for (size_t i = 0; i < n; ++i) {
        keys[i] = "key_" + std::to_string(i) + "_padding_for_realistic_size";
    }

    auto shuffled_keys = keys;
    std::mt19937_64 rng(11111);
    std::shuffle(shuffled_keys.begin(), shuffled_keys.end(), rng);

    ankerl::nanobench::Bench bench;
    bench.title("String Keys")
         .warmup(3)
         .epochs(10)
         .minEpochIterations(1);

    // Random Insert with string keys
    std::cout << "\n--- String Random Insert ---\n";

    bench.run("Containa btree_map<string> insert", [&] {
        btree_map<std::string, int64_t> map;
        for (size_t i = 0; i < n; ++i) {
            map[shuffled_keys[i]] = static_cast<int64_t>(i);
        }
        ankerl::nanobench::doNotOptimizeAway(map.size());
    });

    bench.run("Containa skiplist_map<string> insert", [&] {
        skiplist_map<std::string, int64_t> map;
        for (size_t i = 0; i < n; ++i) {
            map[shuffled_keys[i]] = static_cast<int64_t>(i);
        }
        ankerl::nanobench::doNotOptimizeAway(map.size());
    });

    bench.run("absl::btree_map<string> insert", [&] {
        absl::btree_map<std::string, int64_t> map;
        for (size_t i = 0; i < n; ++i) {
            map[shuffled_keys[i]] = static_cast<int64_t>(i);
        }
        ankerl::nanobench::doNotOptimizeAway(map.size());
    });

    bench.run("std::map<string> insert", [&] {
        std::map<std::string, int64_t> map;
        for (size_t i = 0; i < n; ++i) {
            map[shuffled_keys[i]] = static_cast<int64_t>(i);
        }
        ankerl::nanobench::doNotOptimizeAway(map.size());
    });

    // Lookup with string keys
    std::cout << "\n--- String Random Lookup ---\n";

    btree_map<std::string, int64_t> containa_btree;
    skiplist_map<std::string, int64_t> containa_skiplist;
    absl::btree_map<std::string, int64_t> absl_btree;
    std::map<std::string, int64_t> std_map;

    for (size_t i = 0; i < n; ++i) {
        containa_btree[keys[i]] = static_cast<int64_t>(i);
        containa_skiplist[keys[i]] = static_cast<int64_t>(i);
        absl_btree[keys[i]] = static_cast<int64_t>(i);
        std_map[keys[i]] = static_cast<int64_t>(i);
    }

    bench.run("Containa btree_map<string> lookup", [&] {
        int64_t sum = 0;
        for (const auto& key : shuffled_keys) {
            auto it = containa_btree.find(key);
            if (it != containa_btree.end()) {
                sum += it->second;
            }
        }
        ankerl::nanobench::doNotOptimizeAway(sum);
    });

    bench.run("Containa skiplist_map<string> lookup", [&] {
        int64_t sum = 0;
        for (const auto& key : shuffled_keys) {
            auto it = containa_skiplist.find(key);
            if (it != containa_skiplist.end()) {
                sum += it->second;
            }
        }
        ankerl::nanobench::doNotOptimizeAway(sum);
    });

    bench.run("absl::btree_map<string> lookup", [&] {
        int64_t sum = 0;
        for (const auto& key : shuffled_keys) {
            auto it = absl_btree.find(key);
            if (it != absl_btree.end()) {
                sum += it->second;
            }
        }
        ankerl::nanobench::doNotOptimizeAway(sum);
    });

    bench.run("std::map<string> lookup", [&] {
        int64_t sum = 0;
        for (const auto& key : shuffled_keys) {
            auto it = std_map.find(key);
            if (it != std_map.end()) {
                sum += it->second;
            }
        }
        ankerl::nanobench::doNotOptimizeAway(sum);
    });
}

// =============================================================================
// Multimap benchmarks
// =============================================================================

template <typename Multimap>
void benchmark_multimap_insert_duplicates(ankerl::nanobench::Bench& bench, const std::string& name,
                                          size_t num_keys, size_t dups_per_key) {
    bench.run(name + " insert " + std::to_string(dups_per_key) + " dups/key", [&] {
        Multimap mmap;
        for (size_t k = 0; k < num_keys; ++k) {
            for (size_t d = 0; d < dups_per_key; ++d) {
                mmap.insert(std::make_pair(static_cast<int64_t>(k), static_cast<int64_t>(d)));
            }
        }
        ankerl::nanobench::doNotOptimizeAway(mmap.size());
    });
}

template <typename Multimap>
void benchmark_multimap_count(ankerl::nanobench::Bench& bench, const std::string& name,
                              size_t num_keys, size_t dups_per_key) {
    Multimap mmap;
    for (size_t k = 0; k < num_keys; ++k) {
        for (size_t d = 0; d < dups_per_key; ++d) {
            mmap.insert(std::make_pair(static_cast<int64_t>(k), static_cast<int64_t>(d)));
        }
    }

    auto keys = generate_random_keys(num_keys, 99999);

    bench.run(name + " count(" + std::to_string(dups_per_key) + " dups/key)", [&] {
        size_t total = 0;
        for (size_t i = 0; i < num_keys; ++i) {
            total += mmap.count(keys[i % num_keys]);
        }
        ankerl::nanobench::doNotOptimizeAway(total);
    });
}

template <typename Multimap>
void benchmark_multimap_equal_range(ankerl::nanobench::Bench& bench, const std::string& name,
                                    size_t num_keys, size_t dups_per_key) {
    Multimap mmap;
    for (size_t k = 0; k < num_keys; ++k) {
        for (size_t d = 0; d < dups_per_key; ++d) {
            mmap.insert(std::make_pair(static_cast<int64_t>(k), static_cast<int64_t>(d)));
        }
    }

    bench.run(name + " equal_range(" + std::to_string(dups_per_key) + " dups/key)", [&] {
        size_t total = 0;
        for (size_t k = 0; k < num_keys; ++k) {
            auto [lb, ub] = mmap.equal_range(static_cast<int64_t>(k));
            while (lb != ub) {
                ++total;
                ++lb;
            }
        }
        ankerl::nanobench::doNotOptimizeAway(total);
    });
}

template <typename Multimap>
void benchmark_multimap_erase(ankerl::nanobench::Bench& bench, const std::string& name,
                              size_t num_keys, size_t dups_per_key) {
    bench.run(name + " erase(" + std::to_string(dups_per_key) + " dups/key)", [&] {
        Multimap mmap;
        for (size_t k = 0; k < num_keys; ++k) {
            for (size_t d = 0; d < dups_per_key; ++d) {
                mmap.insert(std::make_pair(static_cast<int64_t>(k), static_cast<int64_t>(d)));
            }
        }
        // Erase half the keys
        for (size_t k = 0; k < num_keys; k += 2) {
            mmap.erase(static_cast<int64_t>(k));
        }
        ankerl::nanobench::doNotOptimizeAway(mmap.size());
    });
}

void run_multimap_benchmarks(size_t num_keys, size_t dups_per_key, const std::string& label) {
    std::cout << "\n============================================================\n";
    std::cout << "Multimap Benchmarks - " << label << " (" << num_keys << " keys, " << dups_per_key << " dups/key)\n";
    std::cout << "============================================================\n";

    ankerl::nanobench::Bench bench;
    bench.title(label + " Multimap")
         .warmup(3)
         .minEpochIterations(5)
         .relative(true);

    // Insert benchmarks
    benchmark_multimap_insert_duplicates<btree_multimap<int64_t, int64_t>>(bench, "Containa btree_multimap", num_keys, dups_per_key);
    benchmark_multimap_insert_duplicates<absl::btree_multimap<int64_t, int64_t>>(bench, "absl btree_multimap", num_keys, dups_per_key);

    // Count benchmarks
    benchmark_multimap_count<btree_multimap<int64_t, int64_t>>(bench, "Containa btree_multimap", num_keys, dups_per_key);
    benchmark_multimap_count<absl::btree_multimap<int64_t, int64_t>>(bench, "absl btree_multimap", num_keys, dups_per_key);

    // Equal range benchmarks
    benchmark_multimap_equal_range<btree_multimap<int64_t, int64_t>>(bench, "Containa btree_multimap", num_keys, dups_per_key);
    benchmark_multimap_equal_range<absl::btree_multimap<int64_t, int64_t>>(bench, "absl btree_multimap", num_keys, dups_per_key);

    // Erase benchmarks
    benchmark_multimap_erase<btree_multimap<int64_t, int64_t>>(bench, "Containa btree_multimap", num_keys, dups_per_key);
    benchmark_multimap_erase<absl::btree_multimap<int64_t, int64_t>>(bench, "absl btree_multimap", num_keys, dups_per_key);
}

int main(int argc, char** argv) {
    std::cout << "============================================================\n";
    std::cout << "Containa vs Abseil btree Performance Comparison\n";
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
    std::cout << "============================================================\n";

    // Small dataset
    run_benchmarks(SMALL_SIZE, "Small (100)");

    // Medium dataset
    run_benchmarks(MEDIUM_SIZE, "Medium (10K)");

    // Large dataset
    run_benchmarks(LARGE_SIZE, "Large (1M)");

    // String key benchmarks
    run_string_benchmarks(MEDIUM_SIZE);

    // Multimap benchmarks
    run_multimap_benchmarks(1000, 10, "Small multimap");
    run_multimap_benchmarks(1000, 50, "Medium multimap");
    run_multimap_benchmarks(1000, 100, "Large multimap");

    return 0;
}

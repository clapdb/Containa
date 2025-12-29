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
#include <iostream>
#include <map>
#include <random>
#include <set>
#include <string>
#include <vector>

#include "../nanobench/src/include/nanobench.h"
#include "container/btree_map.hpp"
#include "container/btree_set.hpp"
#include "container/skiplist_map.hpp"
#include "container/skiplist_set.hpp"

using namespace stdb::container;

// =============================================================================
// Integer Key Benchmarks
// =============================================================================

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

    // -------------------------------------------------------------------------
    // Sorted Insert
    // -------------------------------------------------------------------------
    bench.run("skiplist sorted insert", [&] {
        skiplist_map<int, int> map;
        for (int k : sorted_keys) map[k] = k;
        ankerl::nanobench::doNotOptimizeAway(map);
    });
    bench.run("btree sorted insert", [&] {
        btree_map<int, int> map;
        for (int k : sorted_keys) map[k] = k;
        ankerl::nanobench::doNotOptimizeAway(map);
    });
    bench.run("std::map sorted insert", [&] {
        std::map<int, int> map;
        for (int k : sorted_keys) map[k] = k;
        ankerl::nanobench::doNotOptimizeAway(map);
    });

    // -------------------------------------------------------------------------
    // Random Insert
    // -------------------------------------------------------------------------
    bench.run("skiplist random insert", [&] {
        skiplist_map<int, int> map;
        for (int k : random_keys) map[k] = k;
        ankerl::nanobench::doNotOptimizeAway(map);
    });
    bench.run("btree random insert", [&] {
        btree_map<int, int> map;
        for (int k : random_keys) map[k] = k;
        ankerl::nanobench::doNotOptimizeAway(map);
    });
    bench.run("std::map random insert", [&] {
        std::map<int, int> map;
        for (int k : random_keys) map[k] = k;
        ankerl::nanobench::doNotOptimizeAway(map);
    });

    // Prepare maps for lookup benchmarks
    skiplist_map<int, int> skiplist;
    btree_map<int, int> btree;
    std::map<int, int> stdmap;
    for (int k : random_keys) {
        skiplist[k] = k;
        btree[k] = k;
        stdmap[k] = k;
    }

    // -------------------------------------------------------------------------
    // Find (all keys exist)
    // -------------------------------------------------------------------------
    bench.run("skiplist find", [&] {
        int sum = 0;
        for (int k : random_keys) {
            auto it = skiplist.find(k);
            if (it != skiplist.end()) sum += it->second;
        }
        ankerl::nanobench::doNotOptimizeAway(sum);
    });
    bench.run("btree find", [&] {
        int sum = 0;
        for (int k : random_keys) {
            auto it = btree.find(k);
            if (it != btree.end()) sum += it->second;
        }
        ankerl::nanobench::doNotOptimizeAway(sum);
    });
    bench.run("std::map find", [&] {
        int sum = 0;
        for (int k : random_keys) {
            auto it = stdmap.find(k);
            if (it != stdmap.end()) sum += it->second;
        }
        ankerl::nanobench::doNotOptimizeAway(sum);
    });

    // -------------------------------------------------------------------------
    // Find (50% miss rate)
    // -------------------------------------------------------------------------
    std::vector<int> mixed_keys(N);
    for (size_t i = 0; i < N; ++i) {
        mixed_keys[i] = (i % 2 == 0) ? random_keys[i] : static_cast<int>(N + i);
    }
    std::shuffle(mixed_keys.begin(), mixed_keys.end(), rng);

    bench.run("skiplist find 50% miss", [&] {
        int sum = 0;
        for (int k : mixed_keys) {
            auto it = skiplist.find(k);
            if (it != skiplist.end()) sum += it->second;
        }
        ankerl::nanobench::doNotOptimizeAway(sum);
    });
    bench.run("btree find 50% miss", [&] {
        int sum = 0;
        for (int k : mixed_keys) {
            auto it = btree.find(k);
            if (it != btree.end()) sum += it->second;
        }
        ankerl::nanobench::doNotOptimizeAway(sum);
    });
    bench.run("std::map find 50% miss", [&] {
        int sum = 0;
        for (int k : mixed_keys) {
            auto it = stdmap.find(k);
            if (it != stdmap.end()) sum += it->second;
        }
        ankerl::nanobench::doNotOptimizeAway(sum);
    });

    // -------------------------------------------------------------------------
    // Iterate
    // -------------------------------------------------------------------------
    bench.run("skiplist iterate", [&] {
        int sum = 0;
        for (auto& [k, v] : skiplist) sum += v;
        ankerl::nanobench::doNotOptimizeAway(sum);
    });
    bench.run("btree iterate", [&] {
        int sum = 0;
        for (auto& [k, v] : btree) sum += v;
        ankerl::nanobench::doNotOptimizeAway(sum);
    });
    bench.run("std::map iterate", [&] {
        int sum = 0;
        for (auto& [k, v] : stdmap) sum += v;
        ankerl::nanobench::doNotOptimizeAway(sum);
    });

    // -------------------------------------------------------------------------
    // Lower Bound
    // -------------------------------------------------------------------------
    bench.run("skiplist lower_bound", [&] {
        int sum = 0;
        for (int k : random_keys) {
            auto it = skiplist.lower_bound(k);
            if (it != skiplist.end()) sum += it->second;
        }
        ankerl::nanobench::doNotOptimizeAway(sum);
    });
    bench.run("btree lower_bound", [&] {
        int sum = 0;
        for (int k : random_keys) {
            auto it = btree.lower_bound(k);
            if (it != btree.end()) sum += it->second;
        }
        ankerl::nanobench::doNotOptimizeAway(sum);
    });
    bench.run("std::map lower_bound", [&] {
        int sum = 0;
        for (int k : random_keys) {
            auto it = stdmap.lower_bound(k);
            if (it != stdmap.end()) sum += it->second;
        }
        ankerl::nanobench::doNotOptimizeAway(sum);
    });

    // -------------------------------------------------------------------------
    // Erase
    // -------------------------------------------------------------------------
    bench.run("skiplist erase", [&] {
        skiplist_map<int, int> map;
        for (int k : random_keys) map[k] = k;
        for (int k : random_keys) map.erase(k);
        ankerl::nanobench::doNotOptimizeAway(map);
    });
    bench.run("btree erase", [&] {
        btree_map<int, int> map;
        for (int k : random_keys) map[k] = k;
        for (int k : random_keys) map.erase(k);
        ankerl::nanobench::doNotOptimizeAway(map);
    });
    bench.run("std::map erase", [&] {
        std::map<int, int> map;
        for (int k : random_keys) map[k] = k;
        for (int k : random_keys) map.erase(k);
        ankerl::nanobench::doNotOptimizeAway(map);
    });
}

// =============================================================================
// String Key Benchmarks
// =============================================================================

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

    std::vector<std::string> sorted_string_keys = random_keys;
    std::sort(sorted_string_keys.begin(), sorted_string_keys.end());

    ankerl::nanobench::Bench bench;
    bench.warmup(2).epochs(3).relative(true);

    std::cout << "\n=== String Keys: " << label << " (" << N << " elements) ===\n";

    // -------------------------------------------------------------------------
    // Sorted Insert
    // -------------------------------------------------------------------------
    bench.run("skiplist sorted insert", [&] {
        skiplist_map<std::string, int> map;
        int i = 0;
        for (const auto& k : sorted_string_keys) map[k] = i++;
        ankerl::nanobench::doNotOptimizeAway(map);
    });
    bench.run("btree sorted insert", [&] {
        btree_map<std::string, int> map;
        int i = 0;
        for (const auto& k : sorted_string_keys) map[k] = i++;
        ankerl::nanobench::doNotOptimizeAway(map);
    });
    bench.run("std::map sorted insert", [&] {
        std::map<std::string, int> map;
        int i = 0;
        for (const auto& k : sorted_string_keys) map[k] = i++;
        ankerl::nanobench::doNotOptimizeAway(map);
    });

    // -------------------------------------------------------------------------
    // Random Insert
    // -------------------------------------------------------------------------
    bench.run("skiplist random insert", [&] {
        skiplist_map<std::string, int> map;
        int i = 0;
        for (const auto& k : random_keys) map[k] = i++;
        ankerl::nanobench::doNotOptimizeAway(map);
    });
    bench.run("btree random insert", [&] {
        btree_map<std::string, int> map;
        int i = 0;
        for (const auto& k : random_keys) map[k] = i++;
        ankerl::nanobench::doNotOptimizeAway(map);
    });
    bench.run("std::map random insert", [&] {
        std::map<std::string, int> map;
        int i = 0;
        for (const auto& k : random_keys) map[k] = i++;
        ankerl::nanobench::doNotOptimizeAway(map);
    });

    // Prepare maps
    skiplist_map<std::string, int> skiplist;
    btree_map<std::string, int> btree;
    std::map<std::string, int> stdmap;
    int i = 0;
    for (const auto& k : random_keys) {
        skiplist[k] = i;
        btree[k] = i;
        stdmap[k] = i++;
    }

    // -------------------------------------------------------------------------
    // Find
    // -------------------------------------------------------------------------
    bench.run("skiplist find", [&] {
        int sum = 0;
        for (const auto& k : random_keys) {
            auto it = skiplist.find(k);
            if (it != skiplist.end()) sum += it->second;
        }
        ankerl::nanobench::doNotOptimizeAway(sum);
    });
    bench.run("btree find", [&] {
        int sum = 0;
        for (const auto& k : random_keys) {
            auto it = btree.find(k);
            if (it != btree.end()) sum += it->second;
        }
        ankerl::nanobench::doNotOptimizeAway(sum);
    });
    bench.run("std::map find", [&] {
        int sum = 0;
        for (const auto& k : random_keys) {
            auto it = stdmap.find(k);
            if (it != stdmap.end()) sum += it->second;
        }
        ankerl::nanobench::doNotOptimizeAway(sum);
    });

    // -------------------------------------------------------------------------
    // Iterate
    // -------------------------------------------------------------------------
    bench.run("skiplist iterate", [&] {
        int sum = 0;
        for (auto& [k, v] : skiplist) sum += v;
        ankerl::nanobench::doNotOptimizeAway(sum);
    });
    bench.run("btree iterate", [&] {
        int sum = 0;
        for (auto& [k, v] : btree) sum += v;
        ankerl::nanobench::doNotOptimizeAway(sum);
    });
    bench.run("std::map iterate", [&] {
        int sum = 0;
        for (auto& [k, v] : stdmap) sum += v;
        ankerl::nanobench::doNotOptimizeAway(sum);
    });
}

// =============================================================================
// Set Benchmarks
// =============================================================================

template <size_t N>
void run_set_benchmark(const std::string& label) {
    std::vector<int> random_keys(N);
    std::vector<int> sorted_keys(N);
    for (size_t i = 0; i < N; ++i) sorted_keys[i] = static_cast<int>(i);
    random_keys = sorted_keys;
    std::mt19937 rng(42);
    std::shuffle(random_keys.begin(), random_keys.end(), rng);

    ankerl::nanobench::Bench bench;
    bench.warmup(2).epochs(3).relative(true);

    std::cout << "\n=== Set Operations: " << label << " (" << N << " elements) ===\n";

    // -------------------------------------------------------------------------
    // Random Insert
    // -------------------------------------------------------------------------
    bench.run("skiplist_set insert", [&] {
        skiplist_set<int> set;
        for (int k : random_keys) set.insert(k);
        ankerl::nanobench::doNotOptimizeAway(set);
    });
    bench.run("btree_set insert", [&] {
        btree_set<int> set;
        for (int k : random_keys) set.insert(k);
        ankerl::nanobench::doNotOptimizeAway(set);
    });
    bench.run("std::set insert", [&] {
        std::set<int> set;
        for (int k : random_keys) set.insert(k);
        ankerl::nanobench::doNotOptimizeAway(set);
    });

    // Prepare sets
    skiplist_set<int> skiplist;
    btree_set<int> btree;
    std::set<int> stdset;
    for (int k : random_keys) {
        skiplist.insert(k);
        btree.insert(k);
        stdset.insert(k);
    }

    // -------------------------------------------------------------------------
    // Contains
    // -------------------------------------------------------------------------
    bench.run("skiplist_set contains", [&] {
        int count = 0;
        for (int k : random_keys) {
            if (skiplist.contains(k)) ++count;
        }
        ankerl::nanobench::doNotOptimizeAway(count);
    });
    bench.run("btree_set contains", [&] {
        int count = 0;
        for (int k : random_keys) {
            if (btree.contains(k)) ++count;
        }
        ankerl::nanobench::doNotOptimizeAway(count);
    });
    bench.run("std::set contains", [&] {
        int count = 0;
        for (int k : random_keys) {
            if (stdset.contains(k)) ++count;
        }
        ankerl::nanobench::doNotOptimizeAway(count);
    });

    // -------------------------------------------------------------------------
    // Iterate
    // -------------------------------------------------------------------------
    bench.run("skiplist_set iterate", [&] {
        int sum = 0;
        for (int v : skiplist) sum += v;
        ankerl::nanobench::doNotOptimizeAway(sum);
    });
    bench.run("btree_set iterate", [&] {
        int sum = 0;
        for (int v : btree) sum += v;
        ankerl::nanobench::doNotOptimizeAway(sum);
    });
    bench.run("std::set iterate", [&] {
        int sum = 0;
        for (int v : stdset) sum += v;
        ankerl::nanobench::doNotOptimizeAway(sum);
    });
}

// =============================================================================
// Mixed Workload Benchmark (simulates real-world usage patterns)
// =============================================================================

template <size_t N>
void run_mixed_workload(const std::string& label) {
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> key_dist(0, static_cast<int>(N * 2));
    std::uniform_int_distribution<int> op_dist(0, 99);

    ankerl::nanobench::Bench bench;
    bench.warmup(2).epochs(3).relative(true);

    std::cout << "\n=== Mixed Workload: " << label << " (" << N << " ops) ===\n";
    std::cout << "    (70% find, 20% insert, 10% erase)\n";

    // Pre-generate operations
    struct Op {
        int type;  // 0-69: find, 70-89: insert, 90-99: erase
        int key;
    };
    std::vector<Op> ops(N);
    for (size_t i = 0; i < N; ++i) {
        ops[i] = {op_dist(rng), key_dist(rng)};
    }

    // Pre-populate with some data
    std::vector<int> initial_keys(N / 2);
    for (size_t i = 0; i < N / 2; ++i) {
        initial_keys[i] = key_dist(rng);
    }

    bench.run("skiplist mixed", [&] {
        skiplist_map<int, int> map;
        for (int k : initial_keys) map[k] = k;
        int sum = 0;
        for (const auto& op : ops) {
            if (op.type < 70) {
                auto it = map.find(op.key);
                if (it != map.end()) sum += it->second;
            } else if (op.type < 90) {
                map[op.key] = op.key;
            } else {
                map.erase(op.key);
            }
        }
        ankerl::nanobench::doNotOptimizeAway(sum);
        ankerl::nanobench::doNotOptimizeAway(map);
    });

    bench.run("btree mixed", [&] {
        btree_map<int, int> map;
        for (int k : initial_keys) map[k] = k;
        int sum = 0;
        for (const auto& op : ops) {
            if (op.type < 70) {
                auto it = map.find(op.key);
                if (it != map.end()) sum += it->second;
            } else if (op.type < 90) {
                map[op.key] = op.key;
            } else {
                map.erase(op.key);
            }
        }
        ankerl::nanobench::doNotOptimizeAway(sum);
        ankerl::nanobench::doNotOptimizeAway(map);
    });

    bench.run("std::map mixed", [&] {
        std::map<int, int> map;
        for (int k : initial_keys) map[k] = k;
        int sum = 0;
        for (const auto& op : ops) {
            if (op.type < 70) {
                auto it = map.find(op.key);
                if (it != map.end()) sum += it->second;
            } else if (op.type < 90) {
                map[op.key] = op.key;
            } else {
                map.erase(op.key);
            }
        }
        ankerl::nanobench::doNotOptimizeAway(sum);
        ankerl::nanobench::doNotOptimizeAway(map);
    });
}

// =============================================================================
// Memory Usage Comparison
// =============================================================================

template <size_t N>
void run_memory_benchmark(const std::string& label) {
    std::vector<int> keys(N);
    for (size_t i = 0; i < N; ++i) keys[i] = static_cast<int>(i);
    std::mt19937 rng(42);
    std::shuffle(keys.begin(), keys.end(), rng);

    std::cout << "\n=== Memory Usage: " << label << " (" << N << " elements) ===\n";

    // Measure approximate memory by tracking allocations
    // Note: This is a rough estimate based on container size

    skiplist_map<int, int> skiplist;
    btree_map<int, int> btree;
    std::map<int, int> stdmap;

    for (int k : keys) {
        skiplist[k] = k;
        btree[k] = k;
        stdmap[k] = k;
    }

    std::cout << "  skiplist_map size: " << skiplist.size() << " elements\n";
    std::cout << "  btree_map size:    " << btree.size() << " elements\n";
    std::cout << "  std::map size:     " << stdmap.size() << " elements\n";

    // Theoretical memory estimates
    // skiplist: each node has ~1.33 pointers on average (geometric with p=1/4)
    // btree: nodes are packed, very cache efficient
    // std::map: red-black tree with 3 pointers per node + color

    size_t skiplist_node_size = sizeof(std::pair<const int, int>) + sizeof(uint8_t) +
                                 2 * sizeof(void*);  // ~2 pointers average
    size_t stdmap_node_size = sizeof(std::pair<const int, int>) + 3 * sizeof(void*) + sizeof(bool);

    std::cout << "  Estimated skiplist node size: ~" << skiplist_node_size << " bytes\n";
    std::cout << "  Estimated std::map node size: ~" << stdmap_node_size << " bytes\n";
}

// =============================================================================
// Main
// =============================================================================

int main(int argc, char** argv) {
    bool run_large = false;
    bool run_memory = false;
    bool run_sets = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--large") run_large = true;
        if (arg == "--memory") run_memory = true;
        if (arg == "--sets") run_sets = true;
        if (arg == "--all") {
            run_large = true;
            run_memory = true;
            run_sets = true;
        }
    }

    std::cout << "Skiplist Benchmark\n";
    std::cout << "==================\n";
    std::cout << "Comparing skiplist_map vs btree_map vs std::map\n";

    // Standard benchmarks
    run_int_benchmark<1000>("1K");
    run_int_benchmark<10000>("10K");
    run_int_benchmark<100000>("100K");

    run_string_benchmark<1000>("1K");
    run_string_benchmark<10000>("10K");

    // Mixed workload
    run_mixed_workload<10000>("10K");
    run_mixed_workload<100000>("100K");

    // Large scale benchmarks
    if (run_large) {
        std::cout << "\n--- Large Scale Benchmarks ---\n";
        run_int_benchmark<1000000>("1M");
        run_string_benchmark<100000>("100K");
        run_mixed_workload<1000000>("1M");
    }

    // Set benchmarks
    if (run_sets) {
        std::cout << "\n--- Set Benchmarks ---\n";
        run_set_benchmark<10000>("10K");
        run_set_benchmark<100000>("100K");
    }

    // Memory benchmarks
    if (run_memory) {
        run_memory_benchmark<10000>("10K");
        run_memory_benchmark<100000>("100K");
    }

    return 0;
}

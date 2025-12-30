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

#include <algorithm>
#include <array>
#include <iostream>
#include <memory_resource>
#include <random>
#include <string>
#include <vector>

#include "container/btree_map.hpp"
#include "arena/arena.hpp"

using namespace stdb::container;

// =============================================================================
// Benchmark: btree_map with different allocators
// =============================================================================

template <size_t N>
void run_btree_map_insert_benchmark() {
    std::vector<int> random_keys(N);
    for (size_t i = 0; i < N; ++i) random_keys[i] = static_cast<int>(i);
    std::mt19937 rng(42);
    std::shuffle(random_keys.begin(), random_keys.end(), rng);

    ankerl::nanobench::Bench bench;
    bench.warmup(3).epochs(5).relative(true);

    std::cout << "\n=== btree_map<int,int> Insert: " << N << " elements ===\n";

    // 1. std::allocator (baseline)
    bench.run("std::allocator", [&] {
        btree_map<int, int> map;
        for (int k : random_keys) map[k] = k;
        ankerl::nanobench::doNotOptimizeAway(map);
    });

    // 2. std::pmr::monotonic_buffer_resource
    bench.run("pmr::monotonic", [&] {
        // Pre-allocate buffer large enough for the btree nodes
        std::vector<std::byte> buffer(N * 64);  // ~64 bytes per element estimate
        std::pmr::monotonic_buffer_resource pool{buffer.data(), buffer.size()};
        stdb::pmr::btree_map<int, int> map{&pool};
        for (int k : random_keys) map[k] = k;
        ankerl::nanobench::doNotOptimizeAway(map);
    });

    // 3. std::pmr::unsynchronized_pool_resource
    bench.run("pmr::unsync_pool", [&] {
        std::pmr::unsynchronized_pool_resource pool;
        stdb::pmr::btree_map<int, int> map{&pool};
        for (int k : random_keys) map[k] = k;
        ankerl::nanobench::doNotOptimizeAway(map);
    });

    // 4. ClapDB Arena
    bench.run("arena::Arena", [&] {
        auto opts = arena::Arena::Options::GetDefaultOptions();
        opts.suggested_init_block_size = N * 64;  // Pre-size for better performance
        arena::Arena arena{opts};
        stdb::pmr::btree_map<int, int> map{arena.get_memory_resource()};
        for (int k : random_keys) map[k] = k;
        ankerl::nanobench::doNotOptimizeAway(map);
    });
}

template <size_t N>
void run_btree_map_find_benchmark() {
    std::vector<int> random_keys(N);
    for (size_t i = 0; i < N; ++i) random_keys[i] = static_cast<int>(i);
    std::mt19937 rng(42);
    std::shuffle(random_keys.begin(), random_keys.end(), rng);

    ankerl::nanobench::Bench bench;
    bench.warmup(3).epochs(5).relative(true);

    std::cout << "\n=== btree_map<int,int> Find: " << N << " elements ===\n";

    // Prepare maps for find benchmark
    btree_map<int, int> std_map;
    for (int k : random_keys) std_map[k] = k;

    std::vector<std::byte> mono_buffer(N * 64);
    std::pmr::monotonic_buffer_resource mono_pool{mono_buffer.data(), mono_buffer.size()};
    stdb::pmr::btree_map<int, int> mono_map{&mono_pool};
    for (int k : random_keys) mono_map[k] = k;

    std::pmr::unsynchronized_pool_resource unsync_pool;
    stdb::pmr::btree_map<int, int> unsync_map{&unsync_pool};
    for (int k : random_keys) unsync_map[k] = k;

    auto opts = arena::Arena::Options::GetDefaultOptions();
    opts.suggested_init_block_size = N * 64;
    arena::Arena arena{opts};
    stdb::pmr::btree_map<int, int> arena_map{arena.get_memory_resource()};
    for (int k : random_keys) arena_map[k] = k;

    // Find benchmarks (allocation strategy shouldn't affect find performance much)
    bench.run("std::allocator", [&] {
        int sum = 0;
        for (int k : random_keys) {
            auto it = std_map.find(k);
            if (it != std_map.end()) sum += it->second;
        }
        ankerl::nanobench::doNotOptimizeAway(sum);
    });

    bench.run("pmr::monotonic", [&] {
        int sum = 0;
        for (int k : random_keys) {
            auto it = mono_map.find(k);
            if (it != mono_map.end()) sum += it->second;
        }
        ankerl::nanobench::doNotOptimizeAway(sum);
    });

    bench.run("pmr::unsync_pool", [&] {
        int sum = 0;
        for (int k : random_keys) {
            auto it = unsync_map.find(k);
            if (it != unsync_map.end()) sum += it->second;
        }
        ankerl::nanobench::doNotOptimizeAway(sum);
    });

    bench.run("arena::Arena", [&] {
        int sum = 0;
        for (int k : random_keys) {
            auto it = arena_map.find(k);
            if (it != arena_map.end()) sum += it->second;
        }
        ankerl::nanobench::doNotOptimizeAway(sum);
    });
}

template <size_t N>
void run_btree_map_mixed_benchmark() {
    std::vector<int> keys(N);
    for (size_t i = 0; i < N; ++i) keys[i] = static_cast<int>(i);
    std::mt19937 rng(42);
    std::shuffle(keys.begin(), keys.end(), rng);

    ankerl::nanobench::Bench bench;
    bench.warmup(3).epochs(5).relative(true);

    std::cout << "\n=== btree_map<int,int> Mixed (insert + find + erase): " << N << " elements ===\n";

    // Mixed workload: insert N, find N, erase N/2
    bench.run("std::allocator", [&] {
        btree_map<int, int> map;
        for (int k : keys) map[k] = k;
        int sum = 0;
        for (int k : keys) {
            auto it = map.find(k);
            if (it != map.end()) sum += it->second;
        }
        for (size_t i = 0; i < N / 2; ++i) map.erase(keys[i]);
        ankerl::nanobench::doNotOptimizeAway(sum);
        ankerl::nanobench::doNotOptimizeAway(map);
    });

    bench.run("pmr::monotonic", [&] {
        std::vector<std::byte> buffer(N * 128);
        std::pmr::monotonic_buffer_resource pool{buffer.data(), buffer.size()};
        stdb::pmr::btree_map<int, int> map{&pool};
        for (int k : keys) map[k] = k;
        int sum = 0;
        for (int k : keys) {
            auto it = map.find(k);
            if (it != map.end()) sum += it->second;
        }
        for (size_t i = 0; i < N / 2; ++i) map.erase(keys[i]);
        ankerl::nanobench::doNotOptimizeAway(sum);
        ankerl::nanobench::doNotOptimizeAway(map);
    });

    bench.run("pmr::unsync_pool", [&] {
        std::pmr::unsynchronized_pool_resource pool;
        stdb::pmr::btree_map<int, int> map{&pool};
        for (int k : keys) map[k] = k;
        int sum = 0;
        for (int k : keys) {
            auto it = map.find(k);
            if (it != map.end()) sum += it->second;
        }
        for (size_t i = 0; i < N / 2; ++i) map.erase(keys[i]);
        ankerl::nanobench::doNotOptimizeAway(sum);
        ankerl::nanobench::doNotOptimizeAway(map);
    });

    bench.run("arena::Arena", [&] {
        auto opts = arena::Arena::Options::GetDefaultOptions();
        opts.suggested_init_block_size = N * 128;
        arena::Arena arena{opts};
        stdb::pmr::btree_map<int, int> map{arena.get_memory_resource()};
        for (int k : keys) map[k] = k;
        int sum = 0;
        for (int k : keys) {
            auto it = map.find(k);
            if (it != map.end()) sum += it->second;
        }
        for (size_t i = 0; i < N / 2; ++i) map.erase(keys[i]);
        ankerl::nanobench::doNotOptimizeAway(sum);
        ankerl::nanobench::doNotOptimizeAway(map);
    });
}

// =============================================================================
// Benchmark: btree_set with different allocators
// =============================================================================

template <size_t N>
void run_btree_set_insert_benchmark() {
    std::vector<int> random_keys(N);
    for (size_t i = 0; i < N; ++i) random_keys[i] = static_cast<int>(i);
    std::mt19937 rng(42);
    std::shuffle(random_keys.begin(), random_keys.end(), rng);

    ankerl::nanobench::Bench bench;
    bench.warmup(3).epochs(5).relative(true);

    std::cout << "\n=== btree_set<int> Insert: " << N << " elements ===\n";

    bench.run("std::allocator", [&] {
        btree_set<int> set;
        for (int k : random_keys) set.insert(k);
        ankerl::nanobench::doNotOptimizeAway(set);
    });

    bench.run("pmr::monotonic", [&] {
        std::vector<std::byte> buffer(N * 32);
        std::pmr::monotonic_buffer_resource pool{buffer.data(), buffer.size()};
        stdb::pmr::btree_set<int> set{&pool};
        for (int k : random_keys) set.insert(k);
        ankerl::nanobench::doNotOptimizeAway(set);
    });

    bench.run("pmr::unsync_pool", [&] {
        std::pmr::unsynchronized_pool_resource pool;
        stdb::pmr::btree_set<int> set{&pool};
        for (int k : random_keys) set.insert(k);
        ankerl::nanobench::doNotOptimizeAway(set);
    });

    bench.run("arena::Arena", [&] {
        auto opts = arena::Arena::Options::GetDefaultOptions();
        opts.suggested_init_block_size = N * 32;
        arena::Arena arena{opts};
        stdb::pmr::btree_set<int> set{arena.get_memory_resource()};
        for (int k : random_keys) set.insert(k);
        ankerl::nanobench::doNotOptimizeAway(set);
    });
}

// =============================================================================
// Benchmark: String keys with PMR allocators
// =============================================================================

template <size_t N>
void run_btree_map_string_benchmark() {
    std::vector<std::string> keys(N);
    for (size_t i = 0; i < N; ++i) {
        keys[i] = "key_" + std::to_string(i);
    }
    std::mt19937 rng(42);
    std::shuffle(keys.begin(), keys.end(), rng);

    ankerl::nanobench::Bench bench;
    bench.warmup(3).epochs(5).relative(true);

    std::cout << "\n=== btree_map<string,int> Insert: " << N << " elements ===\n";

    bench.run("std::allocator", [&] {
        btree_map<std::string, int> map;
        int i = 0;
        for (const auto& k : keys) map[k] = i++;
        ankerl::nanobench::doNotOptimizeAway(map);
    });

    bench.run("pmr::monotonic", [&] {
        std::vector<std::byte> buffer(N * 128);
        std::pmr::monotonic_buffer_resource pool{buffer.data(), buffer.size()};
        stdb::pmr::btree_map<std::string, int> map{&pool};
        int i = 0;
        for (const auto& k : keys) map[k] = i++;
        ankerl::nanobench::doNotOptimizeAway(map);
    });

    bench.run("pmr::unsync_pool", [&] {
        std::pmr::unsynchronized_pool_resource pool;
        stdb::pmr::btree_map<std::string, int> map{&pool};
        int i = 0;
        for (const auto& k : keys) map[k] = i++;
        ankerl::nanobench::doNotOptimizeAway(map);
    });

    bench.run("arena::Arena", [&] {
        auto opts = arena::Arena::Options::GetDefaultOptions();
        opts.suggested_init_block_size = N * 128;
        arena::Arena arena{opts};
        stdb::pmr::btree_map<std::string, int> map{arena.get_memory_resource()};
        int i = 0;
        for (const auto& k : keys) map[k] = i++;
        ankerl::nanobench::doNotOptimizeAway(map);
    });
}

// =============================================================================
// Benchmark: Repeated create/destroy cycles (arena reset advantage)
// =============================================================================

template <size_t N>
void run_lifecycle_benchmark() {
    std::vector<int> keys(N);
    for (size_t i = 0; i < N; ++i) keys[i] = static_cast<int>(i);
    std::mt19937 rng(42);
    std::shuffle(keys.begin(), keys.end(), rng);

    constexpr size_t cycles = 100;

    ankerl::nanobench::Bench bench;
    bench.warmup(2).epochs(3).relative(true);

    std::cout << "\n=== Lifecycle: " << cycles << " create/destroy cycles, " << N << " elements each ===\n";

    bench.run("std::allocator", [&] {
        for (size_t c = 0; c < cycles; ++c) {
            btree_map<int, int> map;
            for (int k : keys) map[k] = k;
            ankerl::nanobench::doNotOptimizeAway(map);
        }
    });

    bench.run("pmr::monotonic (reuse)", [&] {
        std::vector<std::byte> buffer(N * 64);
        for (size_t c = 0; c < cycles; ++c) {
            std::pmr::monotonic_buffer_resource pool{buffer.data(), buffer.size()};
            stdb::pmr::btree_map<int, int> map{&pool};
            for (int k : keys) map[k] = k;
            ankerl::nanobench::doNotOptimizeAway(map);
            // pool destroyed and buffer reused
        }
    });

    bench.run("pmr::unsync_pool", [&] {
        std::pmr::unsynchronized_pool_resource pool;
        for (size_t c = 0; c < cycles; ++c) {
            stdb::pmr::btree_map<int, int> map{&pool};
            for (int k : keys) map[k] = k;
            ankerl::nanobench::doNotOptimizeAway(map);
        }
    });

    bench.run("arena::Arena (reset)", [&] {
        auto opts = arena::Arena::Options::GetDefaultOptions();
        opts.suggested_init_block_size = N * 64;
        arena::Arena arena{opts};
        for (size_t c = 0; c < cycles; ++c) {
            stdb::pmr::btree_map<int, int> map{arena.get_memory_resource()};
            for (int k : keys) map[k] = k;
            ankerl::nanobench::doNotOptimizeAway(map);
            arena.Reset();  // Fast reset without deallocation
        }
    });
}

int main(int argc, char** argv) {
    bool run_large = (argc > 1 && std::string(argv[1]) == "--large");

    std::cout << "============================================================\n";
    std::cout << "PMR + Arena Allocator Benchmark for btree_map/btree_set\n";
    std::cout << "============================================================\n";
    std::cout << "Allocators tested:\n";
    std::cout << "  - std::allocator (baseline)\n";
    std::cout << "  - std::pmr::monotonic_buffer_resource\n";
    std::cout << "  - std::pmr::unsynchronized_pool_resource\n";
    std::cout << "  - arena::Arena (ClapDB Arena)\n";
    std::cout << "============================================================\n";

    // btree_map insert benchmarks
    run_btree_map_insert_benchmark<1000>();
    run_btree_map_insert_benchmark<10000>();
    run_btree_map_insert_benchmark<100000>();

    // btree_map find benchmarks
    run_btree_map_find_benchmark<10000>();
    run_btree_map_find_benchmark<100000>();

    // btree_map mixed workload
    run_btree_map_mixed_benchmark<10000>();

    // btree_set benchmarks
    run_btree_set_insert_benchmark<10000>();
    run_btree_set_insert_benchmark<100000>();

    // String key benchmarks
    run_btree_map_string_benchmark<10000>();

    // Lifecycle benchmarks (arena reset advantage)
    run_lifecycle_benchmark<1000>();
    run_lifecycle_benchmark<10000>();

    if (run_large) {
        std::cout << "\n=== Large Scale Benchmarks ===\n";
        run_btree_map_insert_benchmark<1000000>();
        run_btree_map_find_benchmark<1000000>();
        run_btree_set_insert_benchmark<1000000>();
    }

    return 0;
}

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
#include <malloc.h>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

#include "container/dense_map.hpp"
#include "container/flat_map.hpp"

// Competitors
#include "../third_party/unordered_dense.h"  // ankerl::unordered_dense
#include "../third_party/robin_map.h"         // tsl::robin_map

#ifdef ENABLE_ABSL
#include <absl/container/flat_hash_map.h>
#endif

using namespace stdb::container;

// Generate random strings for string key benchmarks
std::vector<std::string> generate_random_strings(size_t count, size_t min_len, size_t max_len) {
    std::vector<std::string> result;
    result.reserve(count);
    std::mt19937 rng(42);
    std::uniform_int_distribution<size_t> len_dist(min_len, max_len);
    std::uniform_int_distribution<int> char_dist('a', 'z');

    for (size_t i = 0; i < count; ++i) {
        size_t len = len_dist(rng);
        std::string s;
        s.reserve(len);
        for (size_t j = 0; j < len; ++j) {
            s.push_back(static_cast<char>(char_dist(rng)));
        }
        result.push_back(std::move(s));
    }
    return result;
}

template <size_t N>
void run_int_benchmark(const std::string& label) {
    std::vector<int64_t> keys(N);
    for (size_t i = 0; i < N; ++i) keys[i] = static_cast<int64_t>(i);
    std::mt19937_64 rng(42);
    std::shuffle(keys.begin(), keys.end(), rng);

    // Generate lookup keys (mix of existing and non-existing)
    std::vector<int64_t> lookup_keys(N);
    for (size_t i = 0; i < N; ++i) {
        lookup_keys[i] = static_cast<int64_t>(i * 2);  // 50% hit rate
    }
    std::shuffle(lookup_keys.begin(), lookup_keys.end(), rng);

    ankerl::nanobench::Bench bench;
    bench.warmup(3).epochs(5).relative(true);

    std::cout << "\n=== Integer Keys: " << label << " (" << N << " elements) ===\n";

    // Type aliases for different policies
    using dense_map_default = dense_map<int64_t, int64_t>;  // flat storage (like ankerl)
    using dense_map_inline = dense_map<int64_t, int64_t, dense_hash<int64_t>,
                                        std::equal_to<int64_t>,
                                        std::allocator<std::pair<int64_t, int64_t>>,
                                        inline_storage_policy>;  // inline storage (like tsl)

    // ========== INSERT BENCHMARK ==========
    bench.run("dense_map insert", [&] {
        dense_map_default map;
        map.reserve(N);
        for (auto k : keys) map[k] = k;
        ankerl::nanobench::doNotOptimizeAway(map);
    });

    bench.run("dense_map (inline) insert", [&] {
        dense_map_inline map;
        map.reserve(N);
        for (auto k : keys) map[k] = k;
        ankerl::nanobench::doNotOptimizeAway(map);
    });

    bench.run("flat_map insert", [&] {
        flat_map<int64_t, int64_t> map;
        map.reserve(N);
        for (auto k : keys) map[k] = k;
        ankerl::nanobench::doNotOptimizeAway(map);
    });

    bench.run("ankerl::unordered_dense insert", [&] {
        ankerl::unordered_dense::map<int64_t, int64_t> map;
        map.reserve(N);
        for (auto k : keys) map[k] = k;
        ankerl::nanobench::doNotOptimizeAway(map);
    });

    bench.run("tsl::robin_map insert", [&] {
        tsl::robin_map<int64_t, int64_t> map;
        map.reserve(N);
        for (auto k : keys) map[k] = k;
        ankerl::nanobench::doNotOptimizeAway(map);
    });

#ifdef ENABLE_ABSL
    bench.run("absl::flat_hash_map insert", [&] {
        absl::flat_hash_map<int64_t, int64_t> map;
        map.reserve(N);
        for (auto k : keys) map[k] = k;
        ankerl::nanobench::doNotOptimizeAway(map);
    });
#endif

    bench.run("std::unordered_map insert", [&] {
        std::unordered_map<int64_t, int64_t> map;
        map.reserve(N);
        for (auto k : keys) map[k] = k;
        ankerl::nanobench::doNotOptimizeAway(map);
    });

    // ========== TRY_EMPLACE BENCHMARK ==========
    bench.run("dense_map try_emplace", [&] {
        dense_map<int64_t, int64_t> map;
        map.reserve(N);
        for (auto k : keys) map.try_emplace(k, k);
        ankerl::nanobench::doNotOptimizeAway(map);
    });

    bench.run("ankerl::unordered_dense try_emplace", [&] {
        ankerl::unordered_dense::map<int64_t, int64_t> map;
        map.reserve(N);
        for (auto k : keys) map.try_emplace(k, k);
        ankerl::nanobench::doNotOptimizeAway(map);
    });

    bench.run("tsl::robin_map try_emplace", [&] {
        tsl::robin_map<int64_t, int64_t> map;
        map.reserve(N);
        for (auto k : keys) map.try_emplace(k, k);
        ankerl::nanobench::doNotOptimizeAway(map);
    });

    bench.run("std::unordered_map try_emplace", [&] {
        std::unordered_map<int64_t, int64_t> map;
        map.reserve(N);
        for (auto k : keys) map.try_emplace(k, k);
        ankerl::nanobench::doNotOptimizeAway(map);
    });

    // ========== EMPLACE BENCHMARK ==========
    bench.run("dense_map emplace", [&] {
        dense_map<int64_t, int64_t> map;
        map.reserve(N);
        for (auto k : keys) map.emplace(k, k);
        ankerl::nanobench::doNotOptimizeAway(map);
    });

    bench.run("ankerl::unordered_dense emplace", [&] {
        ankerl::unordered_dense::map<int64_t, int64_t> map;
        map.reserve(N);
        for (auto k : keys) map.emplace(k, k);
        ankerl::nanobench::doNotOptimizeAway(map);
    });

    bench.run("tsl::robin_map emplace", [&] {
        tsl::robin_map<int64_t, int64_t> map;
        map.reserve(N);
        for (auto k : keys) map.emplace(k, k);
        ankerl::nanobench::doNotOptimizeAway(map);
    });

    bench.run("std::unordered_map emplace", [&] {
        std::unordered_map<int64_t, int64_t> map;
        map.reserve(N);
        for (auto k : keys) map.emplace(k, k);
        ankerl::nanobench::doNotOptimizeAway(map);
    });

    // ========== PREPARE MAPS FOR LOOKUP ==========
    dense_map_default dense;
    dense.reserve(N);
    for (auto k : keys) dense[k] = k;

    dense_map_inline dense_inline;
    dense_inline.reserve(N);
    for (auto k : keys) dense_inline[k] = k;

    flat_map<int64_t, int64_t> flat;
    flat.reserve(N);
    for (auto k : keys) flat[k] = k;

    ankerl::unordered_dense::map<int64_t, int64_t> ankerl_map;
    ankerl_map.reserve(N);
    for (auto k : keys) ankerl_map[k] = k;

    tsl::robin_map<int64_t, int64_t> robin;
    robin.reserve(N);
    for (auto k : keys) robin[k] = k;

#ifdef ENABLE_ABSL
    absl::flat_hash_map<int64_t, int64_t> absl_map;
    absl_map.reserve(N);
    for (auto k : keys) absl_map[k] = k;
#endif

    std::unordered_map<int64_t, int64_t> std_map;
    std_map.reserve(N);
    for (auto k : keys) std_map[k] = k;

    // ========== FIND BENCHMARK (50% hit rate) ==========
    bench.run("dense_map find (50% hit)", [&] {
        int64_t sum = 0;
        for (auto k : lookup_keys) {
            auto it = dense.find(k);
            if (it != dense.end()) sum += it->second;
        }
        ankerl::nanobench::doNotOptimizeAway(sum);
    });

    bench.run("dense_map (inline) find (50% hit)", [&] {
        int64_t sum = 0;
        for (auto k : lookup_keys) {
            auto it = dense_inline.find(k);
            if (it != dense_inline.end()) sum += it->second;
        }
        ankerl::nanobench::doNotOptimizeAway(sum);
    });

    bench.run("ankerl find (50% hit)", [&] {
        int64_t sum = 0;
        for (auto k : lookup_keys) {
            auto it = ankerl_map.find(k);
            if (it != ankerl_map.end()) sum += it->second;
        }
        ankerl::nanobench::doNotOptimizeAway(sum);
    });

    bench.run("tsl::robin find (50% hit)", [&] {
        int64_t sum = 0;
        for (auto k : lookup_keys) {
            auto it = robin.find(k);
            if (it != robin.end()) sum += it->second;
        }
        ankerl::nanobench::doNotOptimizeAway(sum);
    });

    // ========== FIND BENCHMARK (100% hit rate) ==========
    bench.run("dense_map find (100% hit)", [&] {
        int64_t sum = 0;
        for (auto k : keys) {  // Use keys that all exist
            auto it = dense.find(k);
            sum += it->second;
        }
        ankerl::nanobench::doNotOptimizeAway(sum);
    });

    bench.run("dense_map (inline) find (100% hit)", [&] {
        int64_t sum = 0;
        for (auto k : keys) {
            auto it = dense_inline.find(k);
            sum += it->second;
        }
        ankerl::nanobench::doNotOptimizeAway(sum);
    });

    bench.run("ankerl find (100% hit)", [&] {
        int64_t sum = 0;
        for (auto k : keys) {
            auto it = ankerl_map.find(k);
            sum += it->second;
        }
        ankerl::nanobench::doNotOptimizeAway(sum);
    });

    bench.run("tsl::robin find (100% hit)", [&] {
        int64_t sum = 0;
        for (auto k : keys) {
            auto it = robin.find(k);
            sum += it->second;
        }
        ankerl::nanobench::doNotOptimizeAway(sum);
    });

    bench.run("flat_map find", [&] {
        int64_t sum = 0;
        for (auto k : lookup_keys) {
            auto it = flat.find(k);
            if (it != flat.end()) sum += it->second;
        }
        ankerl::nanobench::doNotOptimizeAway(sum);
    });

    bench.run("tsl::robin_map find", [&] {
        int64_t sum = 0;
        for (auto k : lookup_keys) {
            auto it = robin.find(k);
            if (it != robin.end()) sum += it->second;
        }
        ankerl::nanobench::doNotOptimizeAway(sum);
    });

#ifdef ENABLE_ABSL
    bench.run("absl::flat_hash_map find", [&] {
        int64_t sum = 0;
        for (auto k : lookup_keys) {
            auto it = absl_map.find(k);
            if (it != absl_map.end()) sum += it->second;
        }
        ankerl::nanobench::doNotOptimizeAway(sum);
    });
#endif

    bench.run("std::unordered_map find", [&] {
        int64_t sum = 0;
        for (auto k : lookup_keys) {
            auto it = std_map.find(k);
            if (it != std_map.end()) sum += it->second;
        }
        ankerl::nanobench::doNotOptimizeAway(sum);
    });

    // ========== ITERATION BENCHMARK ==========
    bench.run("dense_map iterate", [&] {
        int64_t sum = 0;
        for (const auto& [k, v] : dense) {
            sum += v;
        }
        ankerl::nanobench::doNotOptimizeAway(sum);
    });

    bench.run("dense_map (inline) iterate", [&] {
        int64_t sum = 0;
        for (const auto& [k, v] : dense_inline) {
            sum += v;
        }
        ankerl::nanobench::doNotOptimizeAway(sum);
    });

    bench.run("flat_map iterate", [&] {
        int64_t sum = 0;
        for (const auto& [k, v] : flat) {
            sum += v;
        }
        ankerl::nanobench::doNotOptimizeAway(sum);
    });

    bench.run("ankerl::unordered_dense iterate", [&] {
        int64_t sum = 0;
        for (const auto& [k, v] : ankerl_map) {
            sum += v;
        }
        ankerl::nanobench::doNotOptimizeAway(sum);
    });

    bench.run("tsl::robin_map iterate", [&] {
        int64_t sum = 0;
        for (const auto& [k, v] : robin) {
            sum += v;
        }
        ankerl::nanobench::doNotOptimizeAway(sum);
    });

#ifdef ENABLE_ABSL
    bench.run("absl::flat_hash_map iterate", [&] {
        int64_t sum = 0;
        for (const auto& [k, v] : absl_map) {
            sum += v;
        }
        ankerl::nanobench::doNotOptimizeAway(sum);
    });
#endif

    bench.run("std::unordered_map iterate", [&] {
        int64_t sum = 0;
        for (const auto& [k, v] : std_map) {
            sum += v;
        }
        ankerl::nanobench::doNotOptimizeAway(sum);
    });


    // ========== ERASE BENCHMARK ==========
    bench.run("dense_map erase", [&] {
        dense_map<int64_t, int64_t> map;
        map.reserve(N);
        for (auto k : keys) map[k] = k;
        for (size_t i = 0; i < N / 2; ++i) {
            map.erase(keys[i]);
        }
        ankerl::nanobench::doNotOptimizeAway(map);
    });

    bench.run("flat_map erase", [&] {
        flat_map<int64_t, int64_t> map;
        map.reserve(N);
        for (auto k : keys) map[k] = k;
        for (size_t i = 0; i < N / 2; ++i) {
            map.erase(keys[i]);
        }
        ankerl::nanobench::doNotOptimizeAway(map);
    });

    bench.run("ankerl::unordered_dense erase", [&] {
        ankerl::unordered_dense::map<int64_t, int64_t> map;
        map.reserve(N);
        for (auto k : keys) map[k] = k;
        for (size_t i = 0; i < N / 2; ++i) {
            map.erase(keys[i]);
        }
        ankerl::nanobench::doNotOptimizeAway(map);
    });

    bench.run("tsl::robin_map erase", [&] {
        tsl::robin_map<int64_t, int64_t> map;
        map.reserve(N);
        for (auto k : keys) map[k] = k;
        for (size_t i = 0; i < N / 2; ++i) {
            map.erase(keys[i]);
        }
        ankerl::nanobench::doNotOptimizeAway(map);
    });

#ifdef ENABLE_ABSL
    bench.run("absl::flat_hash_map erase", [&] {
        absl::flat_hash_map<int64_t, int64_t> map;
        map.reserve(N);
        for (auto k : keys) map[k] = k;
        for (size_t i = 0; i < N / 2; ++i) {
            map.erase(keys[i]);
        }
        ankerl::nanobench::doNotOptimizeAway(map);
    });
#endif

    bench.run("std::unordered_map erase", [&] {
        std::unordered_map<int64_t, int64_t> map;
        map.reserve(N);
        for (auto k : keys) map[k] = k;
        for (size_t i = 0; i < N / 2; ++i) {
            map.erase(keys[i]);
        }
        ankerl::nanobench::doNotOptimizeAway(map);
    });

}

template <size_t N>
void run_string_benchmark(const std::string& label, size_t key_len) {
    auto keys = generate_random_strings(N, key_len, key_len);

    ankerl::nanobench::Bench bench;
    bench.warmup(3).epochs(5).relative(true);

    std::cout << "\n=== String Keys (" << key_len << " chars): " << label
              << " (" << N << " elements) ===\n";

    // ========== INSERT ==========
    bench.run("dense_map insert", [&] {
        dense_map<std::string, int64_t> map;
        map.reserve(N);
        int64_t v = 0;
        for (const auto& k : keys) map[k] = v++;
        ankerl::nanobench::doNotOptimizeAway(map);
    });

    bench.run("flat_map insert", [&] {
        flat_map<std::string, int64_t> map;
        map.reserve(N);
        int64_t v = 0;
        for (const auto& k : keys) map[k] = v++;
        ankerl::nanobench::doNotOptimizeAway(map);
    });

    bench.run("ankerl::unordered_dense insert", [&] {
        ankerl::unordered_dense::map<std::string, int64_t> map;
        map.reserve(N);
        int64_t v = 0;
        for (const auto& k : keys) map[k] = v++;
        ankerl::nanobench::doNotOptimizeAway(map);
    });

    bench.run("tsl::robin_map insert", [&] {
        tsl::robin_map<std::string, int64_t> map;
        map.reserve(N);
        int64_t v = 0;
        for (const auto& k : keys) map[k] = v++;
        ankerl::nanobench::doNotOptimizeAway(map);
    });

#ifdef ENABLE_ABSL
    bench.run("absl::flat_hash_map insert", [&] {
        absl::flat_hash_map<std::string, int64_t> map;
        map.reserve(N);
        int64_t v = 0;
        for (const auto& k : keys) map[k] = v++;
        ankerl::nanobench::doNotOptimizeAway(map);
    });
#endif

    bench.run("std::unordered_map insert", [&] {
        std::unordered_map<std::string, int64_t> map;
        map.reserve(N);
        int64_t v = 0;
        for (const auto& k : keys) map[k] = v++;
        ankerl::nanobench::doNotOptimizeAway(map);
    });


    // Prepare maps
    dense_map<std::string, int64_t> dense;
    dense.reserve(N);
    int64_t v = 0;
    for (const auto& k : keys) dense[k] = v++;

    flat_map<std::string, int64_t> flat;
    flat.reserve(N);
    v = 0;
    for (const auto& k : keys) flat[k] = v++;

    ankerl::unordered_dense::map<std::string, int64_t> ankerl_map;
    ankerl_map.reserve(N);
    v = 0;
    for (const auto& k : keys) ankerl_map[k] = v++;

    tsl::robin_map<std::string, int64_t> robin;
    robin.reserve(N);
    v = 0;
    for (const auto& k : keys) robin[k] = v++;

#ifdef ENABLE_ABSL
    absl::flat_hash_map<std::string, int64_t> absl_map;
    absl_map.reserve(N);
    v = 0;
    for (const auto& k : keys) absl_map[k] = v++;
#endif

    std::unordered_map<std::string, int64_t> std_map;
    std_map.reserve(N);
    v = 0;
    for (const auto& k : keys) std_map[k] = v++;


    // ========== FIND ==========
    bench.run("dense_map find", [&] {
        int64_t sum = 0;
        for (const auto& k : keys) {
            auto it = dense.find(k);
            if (it != dense.end()) sum += it->second;
        }
        ankerl::nanobench::doNotOptimizeAway(sum);
    });

    bench.run("flat_map find", [&] {
        int64_t sum = 0;
        for (const auto& k : keys) {
            auto it = flat.find(k);
            if (it != flat.end()) sum += it->second;
        }
        ankerl::nanobench::doNotOptimizeAway(sum);
    });

    bench.run("ankerl::unordered_dense find", [&] {
        int64_t sum = 0;
        for (const auto& k : keys) {
            auto it = ankerl_map.find(k);
            if (it != ankerl_map.end()) sum += it->second;
        }
        ankerl::nanobench::doNotOptimizeAway(sum);
    });

    bench.run("tsl::robin_map find", [&] {
        int64_t sum = 0;
        for (const auto& k : keys) {
            auto it = robin.find(k);
            if (it != robin.end()) sum += it->second;
        }
        ankerl::nanobench::doNotOptimizeAway(sum);
    });

#ifdef ENABLE_ABSL
    bench.run("absl::flat_hash_map find", [&] {
        int64_t sum = 0;
        for (const auto& k : keys) {
            auto it = absl_map.find(k);
            if (it != absl_map.end()) sum += it->second;
        }
        ankerl::nanobench::doNotOptimizeAway(sum);
    });
#endif

    bench.run("std::unordered_map find", [&] {
        int64_t sum = 0;
        for (const auto& k : keys) {
            auto it = std_map.find(k);
            if (it != std_map.end()) sum += it->second;
        }
        ankerl::nanobench::doNotOptimizeAway(sum);
    });


    // ========== ITERATION ==========
    bench.run("dense_map iterate", [&] {
        int64_t sum = 0;
        for (const auto& [k, v] : dense) {
            sum += v;
        }
        ankerl::nanobench::doNotOptimizeAway(sum);
    });

    bench.run("flat_map iterate", [&] {
        int64_t sum = 0;
        for (const auto& [k, v] : flat) {
            sum += v;
        }
        ankerl::nanobench::doNotOptimizeAway(sum);
    });

    bench.run("ankerl::unordered_dense iterate", [&] {
        int64_t sum = 0;
        for (const auto& [k, v] : ankerl_map) {
            sum += v;
        }
        ankerl::nanobench::doNotOptimizeAway(sum);
    });

    bench.run("tsl::robin_map iterate", [&] {
        int64_t sum = 0;
        for (const auto& [k, v] : robin) {
            sum += v;
        }
        ankerl::nanobench::doNotOptimizeAway(sum);
    });

#ifdef ENABLE_ABSL
    bench.run("absl::flat_hash_map iterate", [&] {
        int64_t sum = 0;
        for (const auto& [k, v] : absl_map) {
            sum += v;
        }
        ankerl::nanobench::doNotOptimizeAway(sum);
    });
#endif

    bench.run("std::unordered_map iterate", [&] {
        int64_t sum = 0;
        for (const auto& [k, v] : std_map) {
            sum += v;
        }
        ankerl::nanobench::doNotOptimizeAway(sum);
    });

}

void run_memory_benchmark() {
    std::cout << "\n=== Memory Usage Comparison (100K int64 -> int64) ===\n";

    constexpr size_t N = 100000;
    std::vector<int64_t> keys(N);
    for (size_t i = 0; i < N; ++i) keys[i] = static_cast<int64_t>(i);

    dense_map<int64_t, int64_t> dense;
    for (auto k : keys) dense[k] = k;

    ankerl::unordered_dense::map<int64_t, int64_t> ankerl_map;
    for (auto k : keys) ankerl_map[k] = k;

    tsl::robin_map<int64_t, int64_t> robin;
    for (auto k : keys) robin[k] = k;

    std::unordered_map<int64_t, int64_t> std_map;
    for (auto k : keys) std_map[k] = k;

    // Use mallinfo2 to measure actual memory usage including fragmentation
    auto measure_memory = []() -> size_t {
        #if defined(__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 33))
        auto info = mallinfo2();
        return info.uordblks;  // Total allocated space
        #else
        auto info = mallinfo();
        return static_cast<size_t>(info.uordblks);
        #endif
    };

    // Measure each container separately
    size_t baseline = measure_memory();

    {
        dense_map<int64_t, int64_t> dm;
        for (auto k : keys) dm[k] = k;
        size_t dense_mem = measure_memory() - baseline;
        std::cout << "dense_map:             " << dense_mem / 1024 << " KB (capacity=" << dm.capacity()
                  << ", load=" << dm.load_factor() << ")\n";
    }

    baseline = measure_memory();
    {
        ankerl::unordered_dense::map<int64_t, int64_t> am;
        for (auto k : keys) am[k] = k;
        size_t ankerl_mem = measure_memory() - baseline;
        std::cout << "ankerl::unordered_dense: " << ankerl_mem / 1024 << " KB (size=" << am.size()
                  << ", buckets=" << am.bucket_count() << ")\n";
    }

    baseline = measure_memory();
    {
        tsl::robin_map<int64_t, int64_t> rm;
        for (auto k : keys) rm[k] = k;
        size_t robin_mem = measure_memory() - baseline;
        std::cout << "tsl::robin_map:        " << robin_mem / 1024 << " KB (buckets=" << rm.bucket_count()
                  << ", load=" << rm.load_factor() << ")\n";
    }

    baseline = measure_memory();
    {
        std::unordered_map<int64_t, int64_t> sm;
        for (auto k : keys) sm[k] = k;
        size_t std_mem = measure_memory() - baseline;
        std::cout << "std::unordered_map:    " << std_mem / 1024 << " KB (buckets=" << sm.bucket_count()
                  << ", load=" << sm.load_factor() << ")\n";
    }
}

int main() {
    std::cout << "=== Dense Map Benchmark vs Top Hash Maps ===\n";
    std::cout << "Competitors: ankerl::unordered_dense, tsl::robin_map";
#ifdef ENABLE_ABSL
    std::cout << ", absl::flat_hash_map";
#endif
    std::cout << ", std::unordered_map\n";

    // Integer key benchmarks
    run_int_benchmark<10000>("10K");
    run_int_benchmark<100000>("100K");
    run_int_benchmark<1000000>("1M");

    // String key benchmarks
    run_string_benchmark<100000>("100K", 16);

    // Memory comparison
    run_memory_benchmark();

    return 0;
}

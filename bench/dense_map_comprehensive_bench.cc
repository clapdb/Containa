/*
 * Comprehensive Benchmark: dense_map vs ankerl::unordered_dense vs tsl::robin_map
 *
 * Tests across:
 * - Key types: int32, int64, string (short/medium/long)
 * - Value types: int64, string, large struct
 * - Data distributions: sequential, random, clustered, skewed
 * - Operations: insert, find (100%/50%/0% hit), iterate
 */

#define ANKERL_NANOBENCH_IMPLEMENT
#include "../nanobench/src/include/nanobench.h"

#include <algorithm>
#include <array>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#include "container/dense_map.hpp"
#include "../third_party/unordered_dense.h"
#include "../third_party/robin_map.h"

using namespace stdb::container;

// ============================================================================
// Large Value Type (128 bytes)
// ============================================================================
struct LargeValue {
    std::array<uint64_t, 16> data;
    LargeValue() : data{} {}
    LargeValue(uint64_t v) { for (auto& d : data) d = v; }
    bool operator==(const LargeValue& o) const { return data == o.data; }
};

// ============================================================================
// Data Generators
// ============================================================================
template<typename T>
std::vector<T> gen_sequential(size_t n) {
    std::vector<T> keys(n);
    for (size_t i = 0; i < n; ++i) keys[i] = static_cast<T>(i);
    return keys;
}

template<typename T>
std::vector<T> gen_random(size_t n, uint64_t seed = 42) {
    auto keys = gen_sequential<T>(n);
    std::mt19937_64 rng(seed);
    std::shuffle(keys.begin(), keys.end(), rng);
    return keys;
}

template<typename T>
std::vector<T> gen_clustered(size_t n, uint64_t seed = 42) {
    std::vector<T> keys;
    keys.reserve(n);
    std::mt19937_64 rng(seed);
    T base = 0;
    while (keys.size() < n) {
        for (size_t i = 0; i < 100 && keys.size() < n; ++i) {
            keys.push_back(base + static_cast<T>(i));
        }
        base += static_cast<T>(100 + rng() % 10000);
    }
    std::shuffle(keys.begin(), keys.end(), rng);
    return keys;
}

std::vector<std::string> gen_strings(size_t n, size_t len, uint64_t seed = 42) {
    std::vector<std::string> keys;
    keys.reserve(n);
    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<int> dist('a', 'z');
    for (size_t i = 0; i < n; ++i) {
        std::string s(len, ' ');
        for (auto& c : s) c = static_cast<char>(dist(rng));
        keys.push_back(std::move(s));
    }
    return keys;
}

// ============================================================================
// Result Storage
// ============================================================================
struct Result {
    std::string name;
    double dense_ns, ankerl_ns, tsl_ns;
};
std::vector<Result> g_results;

void record(const std::string& name, double d, double a, double t) {
    g_results.push_back({name, d, a, t});
}

// ============================================================================
// Benchmark Helpers
// ============================================================================
#define BENCH_INSERT(Name, KeyType, ValType, keys, gen_val) do { \
    const size_t N = keys.size(); \
    double d_time = 0, a_time = 0, t_time = 0; \
    bench.run("dense " Name, [&] { \
        dense_map<KeyType, ValType> m; m.reserve(N); \
        for (size_t i = 0; i < N; ++i) m[keys[i]] = gen_val(i); \
        ankerl::nanobench::doNotOptimizeAway(m); \
    }); d_time = bench.results().back().median(ankerl::nanobench::Result::Measure::elapsed) * 1e9; \
    bench.run("ankerl " Name, [&] { \
        ankerl::unordered_dense::map<KeyType, ValType> m; m.reserve(N); \
        for (size_t i = 0; i < N; ++i) m[keys[i]] = gen_val(i); \
        ankerl::nanobench::doNotOptimizeAway(m); \
    }); a_time = bench.results().back().median(ankerl::nanobench::Result::Measure::elapsed) * 1e9; \
    bench.run("tsl " Name, [&] { \
        tsl::robin_map<KeyType, ValType> m; m.reserve(N); \
        for (size_t i = 0; i < N; ++i) m[keys[i]] = gen_val(i); \
        ankerl::nanobench::doNotOptimizeAway(m); \
    }); t_time = bench.results().back().median(ankerl::nanobench::Result::Measure::elapsed) * 1e9; \
    record(std::string(Name) + " insert", d_time, a_time, t_time); \
} while(0)

#define BENCH_FIND(Name, dense, ankerl_m, tsl_m, lookup) do { \
    double d_time = 0, a_time = 0, t_time = 0; \
    bench.run("dense find " Name, [&] { \
        int64_t sum = 0; \
        for (const auto& k : lookup) { auto it = dense.find(k); if (it != dense.end()) sum++; } \
        ankerl::nanobench::doNotOptimizeAway(sum); \
    }); d_time = bench.results().back().median(ankerl::nanobench::Result::Measure::elapsed) * 1e9; \
    bench.run("ankerl find " Name, [&] { \
        int64_t sum = 0; \
        for (const auto& k : lookup) { auto it = ankerl_m.find(k); if (it != ankerl_m.end()) sum++; } \
        ankerl::nanobench::doNotOptimizeAway(sum); \
    }); a_time = bench.results().back().median(ankerl::nanobench::Result::Measure::elapsed) * 1e9; \
    bench.run("tsl find " Name, [&] { \
        int64_t sum = 0; \
        for (const auto& k : lookup) { auto it = tsl_m.find(k); if (it != tsl_m.end()) sum++; } \
        ankerl::nanobench::doNotOptimizeAway(sum); \
    }); t_time = bench.results().back().median(ankerl::nanobench::Result::Measure::elapsed) * 1e9; \
    record(std::string(Name) + " find", d_time, a_time, t_time); \
} while(0)

#define BENCH_ITER(Name, dense, ankerl_m, tsl_m) do { \
    double d_time = 0, a_time = 0, t_time = 0; \
    bench.run("dense iter " Name, [&] { \
        int64_t sum = 0; for (const auto& [k,v] : dense) sum++; \
        ankerl::nanobench::doNotOptimizeAway(sum); \
    }); d_time = bench.results().back().median(ankerl::nanobench::Result::Measure::elapsed) * 1e9; \
    bench.run("ankerl iter " Name, [&] { \
        int64_t sum = 0; for (const auto& [k,v] : ankerl_m) sum++; \
        ankerl::nanobench::doNotOptimizeAway(sum); \
    }); a_time = bench.results().back().median(ankerl::nanobench::Result::Measure::elapsed) * 1e9; \
    bench.run("tsl iter " Name, [&] { \
        int64_t sum = 0; for (const auto& [k,v] : tsl_m) sum++; \
        ankerl::nanobench::doNotOptimizeAway(sum); \
    }); t_time = bench.results().back().median(ankerl::nanobench::Result::Measure::elapsed) * 1e9; \
    record(std::string(Name) + " iterate", d_time, a_time, t_time); \
} while(0)

#define BENCH_ERASE(Name, KeyType, ValType, keys, gen_val) do { \
    const size_t N = keys.size(); \
    double d_time = 0, a_time = 0, t_time = 0; \
    bench.run("dense erase " Name, [&] { \
        dense_map<KeyType, ValType> m; m.reserve(N); \
        for (size_t i = 0; i < N; ++i) m[keys[i]] = gen_val(i); \
        for (size_t i = 0; i < N; ++i) m.erase(keys[i]); \
        ankerl::nanobench::doNotOptimizeAway(m.size()); \
    }); d_time = bench.results().back().median(ankerl::nanobench::Result::Measure::elapsed) * 1e9; \
    bench.run("ankerl erase " Name, [&] { \
        ankerl::unordered_dense::map<KeyType, ValType> m; m.reserve(N); \
        for (size_t i = 0; i < N; ++i) m[keys[i]] = gen_val(i); \
        for (size_t i = 0; i < N; ++i) m.erase(keys[i]); \
        ankerl::nanobench::doNotOptimizeAway(m.size()); \
    }); a_time = bench.results().back().median(ankerl::nanobench::Result::Measure::elapsed) * 1e9; \
    bench.run("tsl erase " Name, [&] { \
        tsl::robin_map<KeyType, ValType> m; m.reserve(N); \
        for (size_t i = 0; i < N; ++i) m[keys[i]] = gen_val(i); \
        for (size_t i = 0; i < N; ++i) m.erase(keys[i]); \
        ankerl::nanobench::doNotOptimizeAway(m.size()); \
    }); t_time = bench.results().back().median(ankerl::nanobench::Result::Measure::elapsed) * 1e9; \
    record(std::string(Name) + " erase", d_time, a_time, t_time); \
} while(0)

// ============================================================================
// Test Functions
// ============================================================================

void test_int_keys() {
    std::cout << "\n=== INT KEY TESTS (N=100K) ===\n";
    constexpr size_t N = 100000;
    ankerl::nanobench::Bench bench;
    bench.warmup(3).epochs(5).relative(true);
    auto gen_val = [](size_t i) { return static_cast<int64_t>(i); };

    // int32 -> int64
    {
        auto keys = gen_random<int32_t>(N);
        BENCH_INSERT("int32->i64", int32_t, int64_t, keys, gen_val);

        dense_map<int32_t, int64_t> dm; dm.reserve(N);
        ankerl::unordered_dense::map<int32_t, int64_t> am; am.reserve(N);
        tsl::robin_map<int32_t, int64_t> tm; tm.reserve(N);
        for (size_t i = 0; i < N; ++i) { dm[keys[i]] = gen_val(i); am[keys[i]] = gen_val(i); tm[keys[i]] = gen_val(i); }

        BENCH_FIND("int32->i64", dm, am, tm, keys);
        BENCH_ITER("int32->i64", dm, am, tm);
    }

    // int64 -> int64
    {
        auto keys = gen_random<int64_t>(N);
        BENCH_INSERT("int64->i64", int64_t, int64_t, keys, gen_val);

        dense_map<int64_t, int64_t> dm; dm.reserve(N);
        ankerl::unordered_dense::map<int64_t, int64_t> am; am.reserve(N);
        tsl::robin_map<int64_t, int64_t> tm; tm.reserve(N);
        for (size_t i = 0; i < N; ++i) { dm[keys[i]] = gen_val(i); am[keys[i]] = gen_val(i); tm[keys[i]] = gen_val(i); }

        BENCH_FIND("int64->i64", dm, am, tm, keys);
        BENCH_ITER("int64->i64", dm, am, tm);
    }
}

void test_string_keys() {
    std::cout << "\n=== STRING KEY TESTS (N=100K) ===\n";
    constexpr size_t N = 100000;
    ankerl::nanobench::Bench bench;
    bench.warmup(3).epochs(5).relative(true);
    auto gen_val = [](size_t i) { return static_cast<int64_t>(i); };

    // Short strings (8 chars - SSO)
    {
        auto keys = gen_strings(N, 8);
        BENCH_INSERT("str8->i64", std::string, int64_t, keys, gen_val);

        dense_map<std::string, int64_t> dm; dm.reserve(N);
        ankerl::unordered_dense::map<std::string, int64_t> am; am.reserve(N);
        tsl::robin_map<std::string, int64_t> tm; tm.reserve(N);
        for (size_t i = 0; i < N; ++i) { dm[keys[i]] = gen_val(i); am[keys[i]] = gen_val(i); tm[keys[i]] = gen_val(i); }

        BENCH_FIND("str8->i64", dm, am, tm, keys);
        BENCH_ITER("str8->i64", dm, am, tm);
    }

    // Medium strings (32 chars)
    {
        auto keys = gen_strings(N, 32);
        BENCH_INSERT("str32->i64", std::string, int64_t, keys, gen_val);

        dense_map<std::string, int64_t> dm; dm.reserve(N);
        ankerl::unordered_dense::map<std::string, int64_t> am; am.reserve(N);
        tsl::robin_map<std::string, int64_t> tm; tm.reserve(N);
        for (size_t i = 0; i < N; ++i) { dm[keys[i]] = gen_val(i); am[keys[i]] = gen_val(i); tm[keys[i]] = gen_val(i); }

        BENCH_FIND("str32->i64", dm, am, tm, keys);
        BENCH_ITER("str32->i64", dm, am, tm);
    }

    // Long strings (128 chars)
    {
        auto keys = gen_strings(N, 128);
        BENCH_INSERT("str128->i64", std::string, int64_t, keys, gen_val);

        dense_map<std::string, int64_t> dm; dm.reserve(N);
        ankerl::unordered_dense::map<std::string, int64_t> am; am.reserve(N);
        tsl::robin_map<std::string, int64_t> tm; tm.reserve(N);
        for (size_t i = 0; i < N; ++i) { dm[keys[i]] = gen_val(i); am[keys[i]] = gen_val(i); tm[keys[i]] = gen_val(i); }

        BENCH_FIND("str128->i64", dm, am, tm, keys);
        BENCH_ITER("str128->i64", dm, am, tm);
    }
}

void test_value_types() {
    std::cout << "\n=== VALUE TYPE TESTS (Key=int64, N=100K) ===\n";
    constexpr size_t N = 100000;
    ankerl::nanobench::Bench bench;
    bench.warmup(3).epochs(5).relative(true);
    auto keys = gen_random<int64_t>(N);

    // int64 -> string
    {
        auto gen_val = [](size_t i) { return "value_" + std::to_string(i); };
        BENCH_INSERT("i64->string", int64_t, std::string, keys, gen_val);

        dense_map<int64_t, std::string> dm; dm.reserve(N);
        ankerl::unordered_dense::map<int64_t, std::string> am; am.reserve(N);
        tsl::robin_map<int64_t, std::string> tm; tm.reserve(N);
        for (size_t i = 0; i < N; ++i) { dm[keys[i]] = gen_val(i); am[keys[i]] = gen_val(i); tm[keys[i]] = gen_val(i); }

        BENCH_FIND("i64->string", dm, am, tm, keys);
        BENCH_ITER("i64->string", dm, am, tm);
    }

    // int64 -> LargeValue (128 bytes)
    {
        auto gen_val = [](size_t i) { return LargeValue(static_cast<uint64_t>(i)); };
        BENCH_INSERT("i64->Large128B", int64_t, LargeValue, keys, gen_val);

        dense_map<int64_t, LargeValue> dm; dm.reserve(N);
        ankerl::unordered_dense::map<int64_t, LargeValue> am; am.reserve(N);
        tsl::robin_map<int64_t, LargeValue> tm; tm.reserve(N);
        for (size_t i = 0; i < N; ++i) { dm[keys[i]] = gen_val(i); am[keys[i]] = gen_val(i); tm[keys[i]] = gen_val(i); }

        BENCH_FIND("i64->Large128B", dm, am, tm, keys);
        BENCH_ITER("i64->Large128B", dm, am, tm);
    }
}

void test_distributions() {
    std::cout << "\n=== DATA DISTRIBUTION TESTS (int64->int64, N=100K) ===\n";
    constexpr size_t N = 100000;
    ankerl::nanobench::Bench bench;
    bench.warmup(3).epochs(5).relative(true);
    auto gen_val = [](size_t i) { return static_cast<int64_t>(i); };

    // Sequential
    {
        auto keys = gen_sequential<int64_t>(N);
        BENCH_INSERT("Sequential", int64_t, int64_t, keys, gen_val);

        dense_map<int64_t, int64_t> dm; dm.reserve(N);
        ankerl::unordered_dense::map<int64_t, int64_t> am; am.reserve(N);
        tsl::robin_map<int64_t, int64_t> tm; tm.reserve(N);
        for (size_t i = 0; i < N; ++i) { dm[keys[i]] = gen_val(i); am[keys[i]] = gen_val(i); tm[keys[i]] = gen_val(i); }

        BENCH_FIND("Sequential", dm, am, tm, keys);
    }

    // Random
    {
        auto keys = gen_random<int64_t>(N);
        BENCH_INSERT("Random", int64_t, int64_t, keys, gen_val);

        dense_map<int64_t, int64_t> dm; dm.reserve(N);
        ankerl::unordered_dense::map<int64_t, int64_t> am; am.reserve(N);
        tsl::robin_map<int64_t, int64_t> tm; tm.reserve(N);
        for (size_t i = 0; i < N; ++i) { dm[keys[i]] = gen_val(i); am[keys[i]] = gen_val(i); tm[keys[i]] = gen_val(i); }

        BENCH_FIND("Random", dm, am, tm, keys);
    }

    // Clustered
    {
        auto keys = gen_clustered<int64_t>(N);
        BENCH_INSERT("Clustered", int64_t, int64_t, keys, gen_val);

        dense_map<int64_t, int64_t> dm; dm.reserve(N);
        ankerl::unordered_dense::map<int64_t, int64_t> am; am.reserve(N);
        tsl::robin_map<int64_t, int64_t> tm; tm.reserve(N);
        for (size_t i = 0; i < N; ++i) { dm[keys[i]] = gen_val(i); am[keys[i]] = gen_val(i); tm[keys[i]] = gen_val(i); }

        BENCH_FIND("Clustered", dm, am, tm, keys);
    }
}

void test_hit_rates() {
    std::cout << "\n=== FIND HIT RATE TESTS (int64->int64, N=100K) ===\n";
    constexpr size_t N = 100000;
    ankerl::nanobench::Bench bench;
    bench.warmup(3).epochs(5).relative(true);

    auto keys = gen_random<int64_t>(N);

    dense_map<int64_t, int64_t> dm; dm.reserve(N);
    ankerl::unordered_dense::map<int64_t, int64_t> am; am.reserve(N);
    tsl::robin_map<int64_t, int64_t> tm; tm.reserve(N);
    for (size_t i = 0; i < N; ++i) {
        dm[keys[i]] = static_cast<int64_t>(i);
        am[keys[i]] = static_cast<int64_t>(i);
        tm[keys[i]] = static_cast<int64_t>(i);
    }

    // 100% hit
    BENCH_FIND("100%hit", dm, am, tm, keys);

    // 50% hit
    std::vector<int64_t> keys_50;
    keys_50.reserve(N);
    std::mt19937_64 rng(123);
    for (size_t i = 0; i < N; ++i) {
        keys_50.push_back(i % 2 == 0 ? keys[i] : static_cast<int64_t>(N + rng()));
    }
    BENCH_FIND("50%hit", dm, am, tm, keys_50);

    // 0% hit
    std::vector<int64_t> keys_0;
    keys_0.reserve(N);
    for (size_t i = 0; i < N; ++i) keys_0.push_back(static_cast<int64_t>(N * 2 + i));
    BENCH_FIND("0%hit", dm, am, tm, keys_0);
}

void test_scale() {
    std::cout << "\n=== SCALE TESTS (int64->int64, Random) ===\n";
    auto gen_val = [](size_t i) { return static_cast<int64_t>(i); };

    // 10K
    {
        constexpr size_t N = 10000;
        ankerl::nanobench::Bench bench;
        bench.warmup(2).epochs(3).relative(true);
        auto keys = gen_random<int64_t>(N);
        BENCH_INSERT("10K", int64_t, int64_t, keys, gen_val);

        dense_map<int64_t, int64_t> dm; dm.reserve(N);
        ankerl::unordered_dense::map<int64_t, int64_t> am; am.reserve(N);
        tsl::robin_map<int64_t, int64_t> tm; tm.reserve(N);
        for (size_t i = 0; i < N; ++i) { dm[keys[i]] = gen_val(i); am[keys[i]] = gen_val(i); tm[keys[i]] = gen_val(i); }
        BENCH_FIND("10K", dm, am, tm, keys);
        BENCH_ITER("10K", dm, am, tm);
    }

    // 100K
    {
        constexpr size_t N = 100000;
        ankerl::nanobench::Bench bench;
        bench.warmup(2).epochs(3).relative(true);
        auto keys = gen_random<int64_t>(N);
        BENCH_INSERT("100K", int64_t, int64_t, keys, gen_val);

        dense_map<int64_t, int64_t> dm; dm.reserve(N);
        ankerl::unordered_dense::map<int64_t, int64_t> am; am.reserve(N);
        tsl::robin_map<int64_t, int64_t> tm; tm.reserve(N);
        for (size_t i = 0; i < N; ++i) { dm[keys[i]] = gen_val(i); am[keys[i]] = gen_val(i); tm[keys[i]] = gen_val(i); }
        BENCH_FIND("100K", dm, am, tm, keys);
        BENCH_ITER("100K", dm, am, tm);
    }

    // 1M
    {
        constexpr size_t N = 1000000;
        ankerl::nanobench::Bench bench;
        bench.warmup(2).epochs(3).relative(true);
        auto keys = gen_random<int64_t>(N);
        BENCH_INSERT("1M", int64_t, int64_t, keys, gen_val);

        dense_map<int64_t, int64_t> dm; dm.reserve(N);
        ankerl::unordered_dense::map<int64_t, int64_t> am; am.reserve(N);
        tsl::robin_map<int64_t, int64_t> tm; tm.reserve(N);
        for (size_t i = 0; i < N; ++i) { dm[keys[i]] = gen_val(i); am[keys[i]] = gen_val(i); tm[keys[i]] = gen_val(i); }
        BENCH_FIND("1M", dm, am, tm, keys);
        BENCH_ITER("1M", dm, am, tm);
    }
}

void test_string_scale() {
    std::cout << "\n=== STRING SCALE TESTS (str16->int64) ===\n";
    auto gen_val = [](size_t i) { return static_cast<int64_t>(i); };

    // 10K strings
    {
        constexpr size_t N = 10000;
        ankerl::nanobench::Bench bench;
        bench.warmup(2).epochs(3).relative(true);
        auto keys = gen_strings(N, 16);
        BENCH_INSERT("str16_10K", std::string, int64_t, keys, gen_val);

        dense_map<std::string, int64_t> dm; dm.reserve(N);
        ankerl::unordered_dense::map<std::string, int64_t> am; am.reserve(N);
        tsl::robin_map<std::string, int64_t> tm; tm.reserve(N);
        for (size_t i = 0; i < N; ++i) { dm[keys[i]] = gen_val(i); am[keys[i]] = gen_val(i); tm[keys[i]] = gen_val(i); }
        BENCH_FIND("str16_10K", dm, am, tm, keys);
        BENCH_ITER("str16_10K", dm, am, tm);
    }

    // 100K strings
    {
        constexpr size_t N = 100000;
        ankerl::nanobench::Bench bench;
        bench.warmup(2).epochs(3).relative(true);
        auto keys = gen_strings(N, 16);
        BENCH_INSERT("str16_100K", std::string, int64_t, keys, gen_val);

        dense_map<std::string, int64_t> dm; dm.reserve(N);
        ankerl::unordered_dense::map<std::string, int64_t> am; am.reserve(N);
        tsl::robin_map<std::string, int64_t> tm; tm.reserve(N);
        for (size_t i = 0; i < N; ++i) { dm[keys[i]] = gen_val(i); am[keys[i]] = gen_val(i); tm[keys[i]] = gen_val(i); }
        BENCH_FIND("str16_100K", dm, am, tm, keys);
        BENCH_ITER("str16_100K", dm, am, tm);
    }

    // 500K strings
    {
        constexpr size_t N = 500000;
        ankerl::nanobench::Bench bench;
        bench.warmup(2).epochs(3).relative(true);
        auto keys = gen_strings(N, 16);
        BENCH_INSERT("str16_500K", std::string, int64_t, keys, gen_val);

        dense_map<std::string, int64_t> dm; dm.reserve(N);
        ankerl::unordered_dense::map<std::string, int64_t> am; am.reserve(N);
        tsl::robin_map<std::string, int64_t> tm; tm.reserve(N);
        for (size_t i = 0; i < N; ++i) { dm[keys[i]] = gen_val(i); am[keys[i]] = gen_val(i); tm[keys[i]] = gen_val(i); }
        BENCH_FIND("str16_500K", dm, am, tm, keys);
        BENCH_ITER("str16_500K", dm, am, tm);
    }
}

// ============================================================================
// NEW: Erase Performance Tests
// ============================================================================
void test_erase() {
    std::cout << "\n=== ERASE TESTS (N=100K) ===\n";
    constexpr size_t N = 100000;
    ankerl::nanobench::Bench bench;
    bench.warmup(2).epochs(3).relative(true);
    auto gen_val = [](size_t i) { return static_cast<int64_t>(i); };

    // int64 keys
    {
        auto keys = gen_random<int64_t>(N);
        BENCH_ERASE("int64", int64_t, int64_t, keys, gen_val);
    }

    // string keys
    {
        auto keys = gen_strings(N, 16);
        BENCH_ERASE("str16", std::string, int64_t, keys, gen_val);
    }
}

// ============================================================================
// NEW: Mixed Operations (realistic workload)
// ============================================================================
void test_mixed_ops() {
    std::cout << "\n=== MIXED OPERATIONS (80% find, 10% insert, 10% erase) ===\n";
    constexpr size_t N = 50000;       // Initial size
    constexpr size_t OPS = 100000;    // Total operations
    ankerl::nanobench::Bench bench;
    bench.warmup(2).epochs(5).relative(true);

    auto keys = gen_random<int64_t>(N * 2);  // Pool of keys
    std::mt19937_64 rng(42);

    double d_time = 0, a_time = 0, t_time = 0;

    bench.run("dense mixed", [&] {
        dense_map<int64_t, int64_t> m;
        m.reserve(N);
        // Pre-populate
        for (size_t i = 0; i < N; ++i) m[keys[i]] = static_cast<int64_t>(i);

        std::mt19937_64 local_rng(123);
        int64_t sum = 0;
        for (size_t op = 0; op < OPS; ++op) {
            size_t r = local_rng() % 100;
            size_t idx = local_rng() % (N * 2);
            if (r < 80) {           // 80% find
                auto it = m.find(keys[idx]);
                if (it != m.end()) sum += it->second;
            } else if (r < 90) {    // 10% insert
                m[keys[idx]] = static_cast<int64_t>(op);
            } else {                // 10% erase
                m.erase(keys[idx]);
            }
        }
        ankerl::nanobench::doNotOptimizeAway(sum);
    });
    d_time = bench.results().back().median(ankerl::nanobench::Result::Measure::elapsed) * 1e9;

    bench.run("ankerl mixed", [&] {
        ankerl::unordered_dense::map<int64_t, int64_t> m;
        m.reserve(N);
        for (size_t i = 0; i < N; ++i) m[keys[i]] = static_cast<int64_t>(i);

        std::mt19937_64 local_rng(123);
        int64_t sum = 0;
        for (size_t op = 0; op < OPS; ++op) {
            size_t r = local_rng() % 100;
            size_t idx = local_rng() % (N * 2);
            if (r < 80) {
                auto it = m.find(keys[idx]);
                if (it != m.end()) sum += it->second;
            } else if (r < 90) {
                m[keys[idx]] = static_cast<int64_t>(op);
            } else {
                m.erase(keys[idx]);
            }
        }
        ankerl::nanobench::doNotOptimizeAway(sum);
    });
    a_time = bench.results().back().median(ankerl::nanobench::Result::Measure::elapsed) * 1e9;

    bench.run("tsl mixed", [&] {
        tsl::robin_map<int64_t, int64_t> m;
        m.reserve(N);
        for (size_t i = 0; i < N; ++i) m[keys[i]] = static_cast<int64_t>(i);

        std::mt19937_64 local_rng(123);
        int64_t sum = 0;
        for (size_t op = 0; op < OPS; ++op) {
            size_t r = local_rng() % 100;
            size_t idx = local_rng() % (N * 2);
            if (r < 80) {
                auto it = m.find(keys[idx]);
                if (it != m.end()) sum += it->second;
            } else if (r < 90) {
                m[keys[idx]] = static_cast<int64_t>(op);
            } else {
                m.erase(keys[idx]);
            }
        }
        ankerl::nanobench::doNotOptimizeAway(sum);
    });
    t_time = bench.results().back().median(ankerl::nanobench::Result::Measure::elapsed) * 1e9;

    record("mixed 80/10/10", d_time, a_time, t_time);
}

// ============================================================================
// NEW: Reserve vs Grow (with and without pre-allocation)
// ============================================================================
void test_reserve_vs_grow() {
    std::cout << "\n=== RESERVE VS GROW TESTS ===\n";
    constexpr size_t N = 100000;
    ankerl::nanobench::Bench bench;
    bench.warmup(2).epochs(5).relative(true);
    auto gen_val = [](size_t i) { return static_cast<int64_t>(i); };
    auto keys = gen_random<int64_t>(N);

    double d_time = 0, a_time = 0, t_time = 0;

    // With reserve
    bench.run("dense reserved", [&] {
        dense_map<int64_t, int64_t> m;
        m.reserve(N);
        for (size_t i = 0; i < N; ++i) m[keys[i]] = gen_val(i);
        ankerl::nanobench::doNotOptimizeAway(m);
    });
    d_time = bench.results().back().median(ankerl::nanobench::Result::Measure::elapsed) * 1e9;

    bench.run("ankerl reserved", [&] {
        ankerl::unordered_dense::map<int64_t, int64_t> m;
        m.reserve(N);
        for (size_t i = 0; i < N; ++i) m[keys[i]] = gen_val(i);
        ankerl::nanobench::doNotOptimizeAway(m);
    });
    a_time = bench.results().back().median(ankerl::nanobench::Result::Measure::elapsed) * 1e9;

    bench.run("tsl reserved", [&] {
        tsl::robin_map<int64_t, int64_t> m;
        m.reserve(N);
        for (size_t i = 0; i < N; ++i) m[keys[i]] = gen_val(i);
        ankerl::nanobench::doNotOptimizeAway(m);
    });
    t_time = bench.results().back().median(ankerl::nanobench::Result::Measure::elapsed) * 1e9;

    record("reserved insert", d_time, a_time, t_time);

    // Without reserve (grow dynamically)
    bench.run("dense grow", [&] {
        dense_map<int64_t, int64_t> m;
        for (size_t i = 0; i < N; ++i) m[keys[i]] = gen_val(i);
        ankerl::nanobench::doNotOptimizeAway(m);
    });
    d_time = bench.results().back().median(ankerl::nanobench::Result::Measure::elapsed) * 1e9;

    bench.run("ankerl grow", [&] {
        ankerl::unordered_dense::map<int64_t, int64_t> m;
        for (size_t i = 0; i < N; ++i) m[keys[i]] = gen_val(i);
        ankerl::nanobench::doNotOptimizeAway(m);
    });
    a_time = bench.results().back().median(ankerl::nanobench::Result::Measure::elapsed) * 1e9;

    bench.run("tsl grow", [&] {
        tsl::robin_map<int64_t, int64_t> m;
        for (size_t i = 0; i < N; ++i) m[keys[i]] = gen_val(i);
        ankerl::nanobench::doNotOptimizeAway(m);
    });
    t_time = bench.results().back().median(ankerl::nanobench::Result::Measure::elapsed) * 1e9;

    record("grow insert", d_time, a_time, t_time);
}

// ============================================================================
// NEW: Hot Key Access (Zipf distribution - some keys accessed way more)
// ============================================================================
void test_zipf_access() {
    std::cout << "\n=== ZIPF (HOT KEY) ACCESS PATTERN ===\n";
    constexpr size_t N = 100000;
    constexpr size_t LOOKUPS = 500000;
    ankerl::nanobench::Bench bench;
    bench.warmup(2).epochs(5).relative(true);

    auto keys = gen_random<int64_t>(N);

    // Generate Zipf-like access pattern (top 20% keys get 80% of accesses)
    std::vector<int64_t> lookup_keys;
    lookup_keys.reserve(LOOKUPS);
    std::mt19937_64 rng(99);
    for (size_t i = 0; i < LOOKUPS; ++i) {
        double r = static_cast<double>(rng()) / std::mt19937_64::max();
        size_t idx;
        if (r < 0.8) {
            // 80% of accesses go to top 20% of keys
            idx = rng() % (N / 5);
        } else {
            // 20% of accesses to remaining 80% of keys
            idx = N / 5 + rng() % (N - N / 5);
        }
        lookup_keys.push_back(keys[idx]);
    }

    dense_map<int64_t, int64_t> dm; dm.reserve(N);
    ankerl::unordered_dense::map<int64_t, int64_t> am; am.reserve(N);
    tsl::robin_map<int64_t, int64_t> tm; tm.reserve(N);
    for (size_t i = 0; i < N; ++i) {
        dm[keys[i]] = static_cast<int64_t>(i);
        am[keys[i]] = static_cast<int64_t>(i);
        tm[keys[i]] = static_cast<int64_t>(i);
    }

    double d_time = 0, a_time = 0, t_time = 0;

    bench.run("dense zipf", [&] {
        int64_t sum = 0;
        for (const auto& k : lookup_keys) {
            auto it = dm.find(k);
            if (it != dm.end()) sum += it->second;
        }
        ankerl::nanobench::doNotOptimizeAway(sum);
    });
    d_time = bench.results().back().median(ankerl::nanobench::Result::Measure::elapsed) * 1e9;

    bench.run("ankerl zipf", [&] {
        int64_t sum = 0;
        for (const auto& k : lookup_keys) {
            auto it = am.find(k);
            if (it != am.end()) sum += it->second;
        }
        ankerl::nanobench::doNotOptimizeAway(sum);
    });
    a_time = bench.results().back().median(ankerl::nanobench::Result::Measure::elapsed) * 1e9;

    bench.run("tsl zipf", [&] {
        int64_t sum = 0;
        for (const auto& k : lookup_keys) {
            auto it = tm.find(k);
            if (it != tm.end()) sum += it->second;
        }
        ankerl::nanobench::doNotOptimizeAway(sum);
    });
    t_time = bench.results().back().median(ankerl::nanobench::Result::Measure::elapsed) * 1e9;

    record("zipf find", d_time, a_time, t_time);
}

// ============================================================================
// NEW: Small Map Performance (N < 100)
// ============================================================================
void test_small_maps() {
    std::cout << "\n=== SMALL MAP TESTS ===\n";
    ankerl::nanobench::Bench bench;
    bench.warmup(5).epochs(10).relative(true);
    auto gen_val = [](size_t i) { return static_cast<int64_t>(i); };

    // N = 8 (single cache line territory)
    {
        constexpr size_t N = 8;
        auto keys = gen_random<int64_t>(N);

        double d_time = 0, a_time = 0, t_time = 0;

        bench.run("dense N=8 insert", [&] {
            dense_map<int64_t, int64_t> m;
            for (size_t i = 0; i < N; ++i) m[keys[i]] = gen_val(i);
            ankerl::nanobench::doNotOptimizeAway(m);
        });
        d_time = bench.results().back().median(ankerl::nanobench::Result::Measure::elapsed) * 1e9;

        bench.run("ankerl N=8 insert", [&] {
            ankerl::unordered_dense::map<int64_t, int64_t> m;
            for (size_t i = 0; i < N; ++i) m[keys[i]] = gen_val(i);
            ankerl::nanobench::doNotOptimizeAway(m);
        });
        a_time = bench.results().back().median(ankerl::nanobench::Result::Measure::elapsed) * 1e9;

        bench.run("tsl N=8 insert", [&] {
            tsl::robin_map<int64_t, int64_t> m;
            for (size_t i = 0; i < N; ++i) m[keys[i]] = gen_val(i);
            ankerl::nanobench::doNotOptimizeAway(m);
        });
        t_time = bench.results().back().median(ankerl::nanobench::Result::Measure::elapsed) * 1e9;

        record("N=8 insert", d_time, a_time, t_time);

        dense_map<int64_t, int64_t> dm;
        ankerl::unordered_dense::map<int64_t, int64_t> am;
        tsl::robin_map<int64_t, int64_t> tm;
        for (size_t i = 0; i < N; ++i) { dm[keys[i]] = gen_val(i); am[keys[i]] = gen_val(i); tm[keys[i]] = gen_val(i); }

        bench.run("dense N=8 find", [&] {
            int64_t sum = 0;
            for (size_t rep = 0; rep < 1000; ++rep)
                for (const auto& k : keys) { auto it = dm.find(k); if (it != dm.end()) sum++; }
            ankerl::nanobench::doNotOptimizeAway(sum);
        });
        d_time = bench.results().back().median(ankerl::nanobench::Result::Measure::elapsed) * 1e9;

        bench.run("ankerl N=8 find", [&] {
            int64_t sum = 0;
            for (size_t rep = 0; rep < 1000; ++rep)
                for (const auto& k : keys) { auto it = am.find(k); if (it != am.end()) sum++; }
            ankerl::nanobench::doNotOptimizeAway(sum);
        });
        a_time = bench.results().back().median(ankerl::nanobench::Result::Measure::elapsed) * 1e9;

        bench.run("tsl N=8 find", [&] {
            int64_t sum = 0;
            for (size_t rep = 0; rep < 1000; ++rep)
                for (const auto& k : keys) { auto it = tm.find(k); if (it != tm.end()) sum++; }
            ankerl::nanobench::doNotOptimizeAway(sum);
        });
        t_time = bench.results().back().median(ankerl::nanobench::Result::Measure::elapsed) * 1e9;

        record("N=8 find", d_time, a_time, t_time);
    }

    // N = 32
    {
        constexpr size_t N = 32;
        auto keys = gen_random<int64_t>(N);

        double d_time = 0, a_time = 0, t_time = 0;

        bench.run("dense N=32 insert", [&] {
            dense_map<int64_t, int64_t> m;
            for (size_t i = 0; i < N; ++i) m[keys[i]] = gen_val(i);
            ankerl::nanobench::doNotOptimizeAway(m);
        });
        d_time = bench.results().back().median(ankerl::nanobench::Result::Measure::elapsed) * 1e9;

        bench.run("ankerl N=32 insert", [&] {
            ankerl::unordered_dense::map<int64_t, int64_t> m;
            for (size_t i = 0; i < N; ++i) m[keys[i]] = gen_val(i);
            ankerl::nanobench::doNotOptimizeAway(m);
        });
        a_time = bench.results().back().median(ankerl::nanobench::Result::Measure::elapsed) * 1e9;

        bench.run("tsl N=32 insert", [&] {
            tsl::robin_map<int64_t, int64_t> m;
            for (size_t i = 0; i < N; ++i) m[keys[i]] = gen_val(i);
            ankerl::nanobench::doNotOptimizeAway(m);
        });
        t_time = bench.results().back().median(ankerl::nanobench::Result::Measure::elapsed) * 1e9;

        record("N=32 insert", d_time, a_time, t_time);

        dense_map<int64_t, int64_t> dm;
        ankerl::unordered_dense::map<int64_t, int64_t> am;
        tsl::robin_map<int64_t, int64_t> tm;
        for (size_t i = 0; i < N; ++i) { dm[keys[i]] = gen_val(i); am[keys[i]] = gen_val(i); tm[keys[i]] = gen_val(i); }

        bench.run("dense N=32 find", [&] {
            int64_t sum = 0;
            for (size_t rep = 0; rep < 500; ++rep)
                for (const auto& k : keys) { auto it = dm.find(k); if (it != dm.end()) sum++; }
            ankerl::nanobench::doNotOptimizeAway(sum);
        });
        d_time = bench.results().back().median(ankerl::nanobench::Result::Measure::elapsed) * 1e9;

        bench.run("ankerl N=32 find", [&] {
            int64_t sum = 0;
            for (size_t rep = 0; rep < 500; ++rep)
                for (const auto& k : keys) { auto it = am.find(k); if (it != am.end()) sum++; }
            ankerl::nanobench::doNotOptimizeAway(sum);
        });
        a_time = bench.results().back().median(ankerl::nanobench::Result::Measure::elapsed) * 1e9;

        bench.run("tsl N=32 find", [&] {
            int64_t sum = 0;
            for (size_t rep = 0; rep < 500; ++rep)
                for (const auto& k : keys) { auto it = tm.find(k); if (it != tm.end()) sum++; }
            ankerl::nanobench::doNotOptimizeAway(sum);
        });
        t_time = bench.results().back().median(ankerl::nanobench::Result::Measure::elapsed) * 1e9;

        record("N=32 find", d_time, a_time, t_time);
    }

    // N = 64
    {
        constexpr size_t N = 64;
        auto keys = gen_random<int64_t>(N);

        double d_time = 0, a_time = 0, t_time = 0;

        bench.run("dense N=64 insert", [&] {
            dense_map<int64_t, int64_t> m;
            for (size_t i = 0; i < N; ++i) m[keys[i]] = gen_val(i);
            ankerl::nanobench::doNotOptimizeAway(m);
        });
        d_time = bench.results().back().median(ankerl::nanobench::Result::Measure::elapsed) * 1e9;

        bench.run("ankerl N=64 insert", [&] {
            ankerl::unordered_dense::map<int64_t, int64_t> m;
            for (size_t i = 0; i < N; ++i) m[keys[i]] = gen_val(i);
            ankerl::nanobench::doNotOptimizeAway(m);
        });
        a_time = bench.results().back().median(ankerl::nanobench::Result::Measure::elapsed) * 1e9;

        bench.run("tsl N=64 insert", [&] {
            tsl::robin_map<int64_t, int64_t> m;
            for (size_t i = 0; i < N; ++i) m[keys[i]] = gen_val(i);
            ankerl::nanobench::doNotOptimizeAway(m);
        });
        t_time = bench.results().back().median(ankerl::nanobench::Result::Measure::elapsed) * 1e9;

        record("N=64 insert", d_time, a_time, t_time);

        dense_map<int64_t, int64_t> dm;
        ankerl::unordered_dense::map<int64_t, int64_t> am;
        tsl::robin_map<int64_t, int64_t> tm;
        for (size_t i = 0; i < N; ++i) { dm[keys[i]] = gen_val(i); am[keys[i]] = gen_val(i); tm[keys[i]] = gen_val(i); }

        bench.run("dense N=64 find", [&] {
            int64_t sum = 0;
            for (size_t rep = 0; rep < 250; ++rep)
                for (const auto& k : keys) { auto it = dm.find(k); if (it != dm.end()) sum++; }
            ankerl::nanobench::doNotOptimizeAway(sum);
        });
        d_time = bench.results().back().median(ankerl::nanobench::Result::Measure::elapsed) * 1e9;

        bench.run("ankerl N=64 find", [&] {
            int64_t sum = 0;
            for (size_t rep = 0; rep < 250; ++rep)
                for (const auto& k : keys) { auto it = am.find(k); if (it != am.end()) sum++; }
            ankerl::nanobench::doNotOptimizeAway(sum);
        });
        a_time = bench.results().back().median(ankerl::nanobench::Result::Measure::elapsed) * 1e9;

        bench.run("tsl N=64 find", [&] {
            int64_t sum = 0;
            for (size_t rep = 0; rep < 250; ++rep)
                for (const auto& k : keys) { auto it = tm.find(k); if (it != tm.end()) sum++; }
            ankerl::nanobench::doNotOptimizeAway(sum);
        });
        t_time = bench.results().back().median(ankerl::nanobench::Result::Measure::elapsed) * 1e9;

        record("N=64 find", d_time, a_time, t_time);
    }
}

// ============================================================================
// Summary
// ============================================================================
void print_summary() {
    std::cout << "\n" << std::string(120, '=') << "\n";
    std::cout << "                    SUMMARY: dense_map vs ankerl vs tsl (ratio < 1.0 = dense faster)\n";
    std::cout << std::string(120, '=') << "\n\n";

    std::cout << std::left << std::setw(35) << "Test"
              << std::right << std::setw(14) << "dense(ns)"
              << std::setw(14) << "ankerl(ns)"
              << std::setw(14) << "tsl(ns)"
              << std::setw(12) << "d/ankerl"
              << std::setw(12) << "d/tsl"
              << std::setw(14) << "Best" << "\n";
    std::cout << std::string(120, '-') << "\n";

    int d_vs_a_wins = 0, d_vs_t_wins = 0, a_wins = 0, t_wins = 0;

    for (const auto& r : g_results) {
        double ra = r.dense_ns / r.ankerl_ns;
        double rt = r.dense_ns / r.tsl_ns;

        std::string best;
        if (r.dense_ns <= r.ankerl_ns && r.dense_ns <= r.tsl_ns) {
            best = "DENSE";
            d_vs_a_wins++; d_vs_t_wins++;
        } else if (r.ankerl_ns <= r.tsl_ns) {
            best = "ankerl";
            a_wins++;
        } else {
            best = "tsl";
            t_wins++;
        }

        std::cout << std::left << std::setw(35) << r.name
                  << std::right << std::fixed << std::setprecision(0)
                  << std::setw(14) << r.dense_ns
                  << std::setw(14) << r.ankerl_ns
                  << std::setw(14) << r.tsl_ns
                  << std::setprecision(2)
                  << std::setw(12) << ra
                  << std::setw(12) << rt
                  << std::setw(14) << best << "\n";
    }

    std::cout << std::string(120, '-') << "\n";
    std::cout << "\nWin counts: dense=" << d_vs_a_wins << ", ankerl=" << a_wins << ", tsl=" << t_wins << "\n";

    // Calculate averages
    double insert_ra = 0, find_ra = 0, iter_ra = 0, erase_ra = 0;
    double insert_rt = 0, find_rt = 0, iter_rt = 0, erase_rt = 0;
    int ins_c = 0, find_c = 0, iter_c = 0, erase_c = 0;

    for (const auto& r : g_results) {
        double ra = r.dense_ns / r.ankerl_ns;
        double rt = r.dense_ns / r.tsl_ns;
        if (r.name.find("insert") != std::string::npos) { insert_ra += ra; insert_rt += rt; ins_c++; }
        else if (r.name.find("find") != std::string::npos) { find_ra += ra; find_rt += rt; find_c++; }
        else if (r.name.find("iterate") != std::string::npos) { iter_ra += ra; iter_rt += rt; iter_c++; }
        else if (r.name.find("erase") != std::string::npos) { erase_ra += ra; erase_rt += rt; erase_c++; }
    }

    std::cout << "\nAverage ratios (dense/competitor, <1.0 = dense faster):\n";
    std::cout << "           vs ankerl    vs tsl\n";
    if (ins_c > 0) std::cout << "  Insert:  " << std::fixed << std::setprecision(2) << (insert_ra/ins_c) << "          " << (insert_rt/ins_c) << "\n";
    if (find_c > 0) std::cout << "  Find:    " << (find_ra/find_c) << "          " << (find_rt/find_c) << "\n";
    if (iter_c > 0) std::cout << "  Iterate: " << (iter_ra/iter_c) << "          " << (iter_rt/iter_c) << "\n";
    if (erase_c > 0) std::cout << "  Erase:   " << (erase_ra/erase_c) << "          " << (erase_rt/erase_c) << "\n";
}

int main() {
    std::cout << "========================================================\n";
    std::cout << "  COMPREHENSIVE BENCHMARK: dense_map vs ankerl vs tsl\n";
    std::cout << "========================================================\n";

    test_int_keys();
    test_string_keys();
    test_value_types();
    test_distributions();
    test_hit_rates();
    test_scale();
    test_string_scale();

    // New tests
    test_erase();
    test_mixed_ops();
    test_reserve_vs_grow();
    test_zipf_access();
    test_small_maps();

    print_summary();
    return 0;
}

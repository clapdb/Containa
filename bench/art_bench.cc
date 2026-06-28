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
#include <chrono>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#include "container/art_map.hpp"
#include "container/btree_map.hpp"

#ifdef ENABLE_ABSL
#include <absl/container/btree_map.h>
#endif

using stdb::container::art_map;
using stdb::container::btree_map;

template <std::size_t N>
static void bench_int(const std::string& label) {
    std::mt19937_64 rng(0xC0FFEE);
    std::vector<uint64_t> keys(N);
    for (auto& k : keys) k = rng();
    std::vector<uint64_t> shuffled = keys;
    std::shuffle(shuffled.begin(), shuffled.end(), rng);

    std::cout << "\n=== uint64 keys: " << label << " (" << N << " elems) ===\n";
    ankerl::nanobench::Bench b;
    b.warmup(3).epochs(11).minEpochTime(std::chrono::milliseconds(40)).relative(true);

    b.run("art_map   insert", [&] {
        art_map<uint64_t, uint64_t> m;
        for (auto k : keys) m[k] = k;
        ankerl::nanobench::doNotOptimizeAway(m.size());
    });
    b.run("btree_map insert", [&] {
        btree_map<uint64_t, uint64_t> m;
        for (auto k : keys) m[k] = k;
        ankerl::nanobench::doNotOptimizeAway(m.size());
    });
#ifdef ENABLE_ABSL
    b.run("absl_btree insert", [&] {
        absl::btree_map<uint64_t, uint64_t> m;
        for (auto k : keys) m[k] = k;
        ankerl::nanobench::doNotOptimizeAway(m.size());
    });
#endif

    art_map<uint64_t, uint64_t> art;
    btree_map<uint64_t, uint64_t> bt;
    for (auto k : keys) {
        art[k] = k;
        bt[k] = k;
    }
#ifdef ENABLE_ABSL
    absl::btree_map<uint64_t, uint64_t> ab;
    for (auto k : keys) ab[k] = k;
#endif

    b.run("art_map   lookup", [&] {
        uint64_t sum = 0;
        for (auto k : shuffled) sum += art.find(k)->second;
        ankerl::nanobench::doNotOptimizeAway(sum);
    });
    b.run("btree_map lookup", [&] {
        uint64_t sum = 0;
        for (auto k : shuffled) sum += bt.find(k)->second;
        ankerl::nanobench::doNotOptimizeAway(sum);
    });
#ifdef ENABLE_ABSL
    b.run("absl_btree lookup", [&] {
        uint64_t sum = 0;
        for (auto k : shuffled) sum += ab.find(k)->second;
        ankerl::nanobench::doNotOptimizeAway(sum);
    });
#endif

    b.run("art_map   scan", [&] {
        uint64_t sum = 0;
        for (const auto& kv : art) sum += kv.second;
        ankerl::nanobench::doNotOptimizeAway(sum);
    });
    b.run("art_map   foreach", [&] {
        uint64_t sum = 0;
        art.for_each([&](const std::pair<const uint64_t, uint64_t>& kv) { sum += kv.second; });
        ankerl::nanobench::doNotOptimizeAway(sum);
    });
    b.run("btree_map scan", [&] {
        uint64_t sum = 0;
        for (const auto& kv : bt) sum += kv.second;
        ankerl::nanobench::doNotOptimizeAway(sum);
    });
#ifdef ENABLE_ABSL
    b.run("absl_btree scan", [&] {
        uint64_t sum = 0;
        for (const auto& kv : ab) sum += kv.second;
        ankerl::nanobench::doNotOptimizeAway(sum);
    });
#endif
}

template <std::size_t N>
static void bench_string(const std::string& label, int keylen, bool shared_prefix) {
    std::mt19937_64 rng(0xBEEF);
    std::vector<std::string> keys;
    keys.reserve(N);
    const char* prefix = "user/session/2026/";
    for (std::size_t i = 0; i < N; ++i) {
        std::string s;
        if (shared_prefix) s = prefix;
        while (static_cast<int>(s.size()) < keylen) s.push_back(static_cast<char>('a' + (rng() % 26)));
        keys.push_back(std::move(s));
    }
    std::sort(keys.begin(), keys.end());
    keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
    std::vector<std::string> shuffled = keys;
    std::shuffle(shuffled.begin(), shuffled.end(), rng);

    std::cout << "\n=== string keys: " << label << " (" << keys.size()
              << " elems, len=" << keylen << (shared_prefix ? ", shared prefix" : "") << ") ===\n";
    ankerl::nanobench::Bench b;
    b.warmup(3).epochs(11).minEpochTime(std::chrono::milliseconds(40)).relative(true);

    b.run("art_map   insert", [&] {
        art_map<std::string, uint64_t> m;
        uint64_t i = 0;
        for (auto& k : keys) m[k] = i++;
        ankerl::nanobench::doNotOptimizeAway(m.size());
    });
    b.run("btree_map insert", [&] {
        btree_map<std::string, uint64_t> m;
        uint64_t i = 0;
        for (auto& k : keys) m[k] = i++;
        ankerl::nanobench::doNotOptimizeAway(m.size());
    });

    art_map<std::string, uint64_t> art;
    btree_map<std::string, uint64_t> bt;
    uint64_t i = 0;
    for (auto& k : keys) {
        art[k] = i;
        bt[k] = i;
        ++i;
    }

    b.run("art_map   lookup", [&] {
        uint64_t sum = 0;
        for (auto& k : shuffled) sum += art.find(k)->second;
        ankerl::nanobench::doNotOptimizeAway(sum);
    });
    b.run("btree_map lookup", [&] {
        uint64_t sum = 0;
        for (auto& k : shuffled) sum += bt.find(k)->second;
        ankerl::nanobench::doNotOptimizeAway(sum);
    });

    b.run("art_map   scan", [&] {
        uint64_t sum = 0;
        for (const auto& kv : art) sum += kv.second;
        ankerl::nanobench::doNotOptimizeAway(sum);
    });
    b.run("art_map   foreach", [&] {
        uint64_t sum = 0;
        art.for_each([&](const std::pair<const std::string, uint64_t>& kv) { sum += kv.second; });
        ankerl::nanobench::doNotOptimizeAway(sum);
    });
    b.run("btree_map scan", [&] {
        uint64_t sum = 0;
        for (const auto& kv : bt) sum += kv.second;
        ankerl::nanobench::doNotOptimizeAway(sum);
    });
}

int main() {
    bench_int<100000>("100K");
    bench_int<1000000>("1M");
    bench_string<200000>("random", 16, false);
    bench_string<200000>("shared-prefix", 28, true);
    return 0;
}

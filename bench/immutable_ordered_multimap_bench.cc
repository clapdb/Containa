/*
 * Copyright (C) STDB Holdings Limited
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * immutable_ordered_multimap vs containa btree_multimap: build / point / range,
 * over an immutable build-once-read-many workload.
 */

#include "../nanobench/src/include/nanobench.h"

#include <cstdint>
#include <random>
#include <utility>
#include <vector>

#include "container/btree_map.hpp"
#include "container/immutable_ordered_multimap.hpp"

using stdb::container::btree_multimap;
using stdb::container::immutable_ordered_multimap;

namespace {

auto make_pairs(uint32_t n, int64_t distinct) -> std::vector<std::pair<int64_t, uint32_t>> {
    std::mt19937_64 rng(12345);
    std::uniform_int_distribution<int64_t> dist(0, distinct - 1);
    std::vector<std::pair<int64_t, uint32_t>> pairs;
    pairs.reserve(n);
    for (uint32_t i = 0; i < n; ++i) {
        pairs.emplace_back(dist(rng), i);
    }
    return pairs;
}

void scenario(uint32_t n, int64_t distinct, const char* title) {
    const auto pairs = make_pairs(n, distinct);

    std::mt19937_64 rng(999);
    std::uniform_int_distribution<int64_t> dist(0, distinct - 1);
    std::vector<int64_t> probes(50000);
    for (auto& p : probes) {
        p = dist(rng);
    }
    const int64_t width = std::max<int64_t>(1, distinct / 100);

    ankerl::nanobench::Bench bench;
    bench.title(title).warmup(1).epochs(3).relative(true);

    // ---- build ----
    bench.run("build immutable_ordered_multimap", [&] {
        auto m = immutable_ordered_multimap<int64_t, uint32_t>::build(pairs);
        ankerl::nanobench::doNotOptimizeAway(m.value_count());
    });
    bench.run("build btree_multimap", [&] {
        btree_multimap<int64_t, uint32_t> m;
        for (const auto& [k, v] : pairs) {
            m.emplace(k, v);
        }
        ankerl::nanobench::doNotOptimizeAway(m.size());
    });

    auto im = immutable_ordered_multimap<int64_t, uint32_t>::build(pairs);
    btree_multimap<int64_t, uint32_t> bt;
    for (const auto& [k, v] : pairs) {
        bt.emplace(k, v);
    }

    // ---- point: sum matching row ids (touch the values, no allocation) ----
    bench.run("point immutable_ordered_multimap", [&] {
        uint64_t acc = 0;
        for (int64_t x : probes) {
            for (uint32_t r : im.equal_range(x)) {
                acc += r;
            }
        }
        ankerl::nanobench::doNotOptimizeAway(acc);
    });
    bench.run("point btree_multimap", [&] {
        uint64_t acc = 0;
        for (int64_t x : probes) {
            auto [b, e] = bt.equal_range(x);
            for (auto it = b; it != e; ++it) {
                acc += it->second;
            }
        }
        ankerl::nanobench::doNotOptimizeAway(acc);
    });

    // ---- range [x, x+width] ----
    bench.run("range immutable_ordered_multimap", [&] {
        uint64_t acc = 0;
        for (int64_t x : probes) {
            for (uint32_t r : im.range(x, true, x + width, true)) {
                acc += r;
            }
        }
        ankerl::nanobench::doNotOptimizeAway(acc);
    });
    bench.run("range btree_multimap", [&] {
        uint64_t acc = 0;
        for (int64_t x : probes) {
            for (auto it = bt.lower_bound(x); it != bt.end() && it->first <= x + width; ++it) {
                acc += it->second;
            }
        }
        ankerl::nanobench::doNotOptimizeAway(acc);
    });
}

}  // namespace

auto main() -> int {
    scenario(22000, 22000, "per-segment high-card (n=22000, distinct=22000)");
    scenario(22000, 200, "per-segment low-card (n=22000, distinct=200)");
    scenario(1000000, 1000000, "1M high-card (n=1000000, distinct=1000000)");
    return 0;
}

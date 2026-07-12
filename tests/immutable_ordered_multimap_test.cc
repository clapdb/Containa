/*
 * Copyright (C) STDB Holdings Limited
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "container/immutable_ordered_multimap.hpp"

#include <algorithm>
#include <cstdint>
#include <map>
#include <random>
#include <vector>

#include "doctest/doctest/doctest.h"

namespace stdb::container {

namespace {

auto to_vec(std::span<const uint32_t> s) -> std::vector<uint32_t> {
    std::vector<uint32_t> v(s.begin(), s.end());
    std::sort(v.begin(), v.end());
    return v;
}

}  // namespace

TEST_CASE("immutable_ordered_multimap basic") {
    // key -> rows:  50->{0,2}  7->{1,4}  99->{3}
    std::vector<std::pair<int64_t, uint32_t>> pairs{{50, 0}, {7, 1}, {50, 2}, {99, 3}, {7, 4}};
    auto m = immutable_ordered_multimap<int64_t, uint32_t>::build(pairs);

    CHECK(m.key_count() == 3);
    CHECK(m.value_count() == 5);

    SUBCASE("equal_range") {
        CHECK(to_vec(m.equal_range(50)) == std::vector<uint32_t>{0, 2});
        CHECK(to_vec(m.equal_range(7)) == std::vector<uint32_t>{1, 4});
        CHECK(to_vec(m.equal_range(99)) == std::vector<uint32_t>{3});
        CHECK(m.equal_range(123).empty());
        CHECK(m.equal_range(0).empty());
        CHECK(m.contains(50));
        CHECK(!m.contains(8));
        CHECK(m.count(7) == 2);
    }
    SUBCASE("range bounds") {
        // [7, 50]  -> keys 7,50 -> rows 0,1,2,4
        CHECK(to_vec(m.range(7, true, 50, true)) == std::vector<uint32_t>{0, 1, 2, 4});
        // (7, 99)  -> key 50    -> rows 0,2
        CHECK(to_vec(m.range(7, false, 99, false)) == std::vector<uint32_t>{0, 2});
        // [7, 99)  -> keys 7,50 -> rows 0,1,2,4
        CHECK(to_vec(m.range(7, true, 99, false)) == std::vector<uint32_t>{0, 1, 2, 4});
        // >= 50 -> keys 50,99 -> rows 0,2,3
        CHECK(to_vec(m.at_least(50)) == std::vector<uint32_t>{0, 2, 3});
        // > 7  -> keys 50,99 -> rows 0,2,3
        CHECK(to_vec(m.at_least(7, false)) == std::vector<uint32_t>{0, 2, 3});
        // <= 7 -> rows 1,4
        CHECK(to_vec(m.at_most(7)) == std::vector<uint32_t>{1, 4});
        // < 50 -> rows 1,4
        CHECK(to_vec(m.at_most(50, false)) == std::vector<uint32_t>{1, 4});
    }
}

TEST_CASE("immutable_ordered_multimap from_parts round-trips build") {
    std::vector<std::pair<int64_t, uint32_t>> pairs{{50, 0}, {7, 1}, {50, 2}, {99, 3}, {7, 4}};
    auto built = immutable_ordered_multimap<int64_t, uint32_t>::build(pairs);

    // Reconstruct from the exposed CSR parts; must answer queries identically.
    std::vector<int64_t> keys(built.keys().begin(), built.keys().end());
    std::vector<uint32_t> offsets(built.offsets().begin(), built.offsets().end());
    std::vector<uint32_t> values(built.values().begin(), built.values().end());
    auto rebuilt = immutable_ordered_multimap<int64_t, uint32_t>::from_parts(std::move(keys), std::move(offsets),
                                                                            std::move(values));

    CHECK(rebuilt.key_count() == built.key_count());
    CHECK(rebuilt.value_count() == built.value_count());
    CHECK(to_vec(rebuilt.equal_range(50)) == std::vector<uint32_t>{0, 2});
    CHECK(to_vec(rebuilt.equal_range(7)) == std::vector<uint32_t>{1, 4});
    CHECK(to_vec(rebuilt.range(7, true, 50, true)) == std::vector<uint32_t>{0, 1, 2, 4});
    CHECK(rebuilt.equal_range(123).empty());
}

TEST_CASE("immutable_ordered_multimap matches std::multimap (randomized)") {
    std::mt19937_64 rng(2026);
    std::uniform_int_distribution<int64_t> dist(0, 500);
    std::vector<std::pair<int64_t, uint32_t>> pairs;
    std::multimap<int64_t, uint32_t> ref;
    for (uint32_t i = 0; i < 5000; ++i) {
        const int64_t key = dist(rng);
        pairs.emplace_back(key, i);
        ref.emplace(key, i);
    }
    auto m = immutable_ordered_multimap<int64_t, uint32_t>::build(pairs);

    for (int64_t key = -5; key <= 505; ++key) {
        auto [b, e] = ref.equal_range(key);
        std::vector<uint32_t> expect;
        for (auto it = b; it != e; ++it) {
            expect.push_back(it->second);
        }
        std::sort(expect.begin(), expect.end());
        CHECK(to_vec(m.equal_range(key)) == expect);
    }

    // range [lo, hi]
    for (int trial = 0; trial < 200; ++trial) {
        int64_t lo = dist(rng);
        int64_t hi = dist(rng);
        if (lo > hi) {
            std::swap(lo, hi);
        }
        std::vector<uint32_t> expect;
        for (auto it = ref.lower_bound(lo); it != ref.upper_bound(hi); ++it) {
            expect.push_back(it->second);
        }
        std::sort(expect.begin(), expect.end());
        CHECK(to_vec(m.range(lo, true, hi, true)) == expect);
    }
}

}  // namespace stdb::container

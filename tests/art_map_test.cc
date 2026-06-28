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

#include "container/art_map.hpp"

#include <algorithm>
#include <limits>
#include <map>
#include <memory_resource>
#include <random>
#include <string>
#include <vector>

#include "doctest/doctest/doctest.h"

namespace stdb::container {

TEST_CASE("art_map::basic") {
    SUBCASE("default constructor") {
        art_map<int, int> m;
        CHECK(m.empty());
        CHECK_EQ(m.size(), 0);
        CHECK(m.begin() == m.end());
    }

    SUBCASE("insert and find") {
        art_map<int, int> m;
        auto [it, ok] = m.insert({42, 100});
        CHECK(ok);
        CHECK_EQ(it->first, 42);
        CHECK_EQ(it->second, 100);
        CHECK_EQ(m.size(), 1);

        auto [it2, ok2] = m.insert({42, 999});
        CHECK_FALSE(ok2);
        CHECK_EQ(it2->second, 100);  // not overwritten
        CHECK_EQ(m.size(), 1);

        CHECK(m.contains(42));
        CHECK_FALSE(m.contains(7));
        CHECK_EQ(m.count(42), 1);
        CHECK_EQ(m.find(42)->second, 100);
        CHECK(m.find(7) == m.end());
    }

    SUBCASE("operator[] and at") {
        art_map<int, std::string> m;
        m[1] = "one";
        m[2] = "two";
        CHECK_EQ(m.size(), 2);
        CHECK_EQ(m[1], "one");
        CHECK_EQ(m.at(2), "two");
        CHECK_THROWS_AS(m.at(99), std::out_of_range);
        m[1] = "ONE";
        CHECK_EQ(m.at(1), "ONE");
    }

    SUBCASE("insert_or_assign") {
        art_map<int, int> m;
        auto [it, ok] = m.insert_or_assign(5, 50);
        CHECK(ok);
        CHECK_EQ(it->second, 50);
        auto [it2, ok2] = m.insert_or_assign(5, 55);
        CHECK_FALSE(ok2);
        CHECK_EQ(m.at(5), 55);
    }

    SUBCASE("initializer list") {
        art_map<int, int> m{{3, 30}, {1, 10}, {2, 20}};
        CHECK_EQ(m.size(), 3);
        CHECK_EQ(m.at(1), 10);
        CHECK_EQ(m.at(2), 20);
        CHECK_EQ(m.at(3), 30);
    }
}

TEST_CASE("art_map::erase") {
    art_map<int, int> m;
    for (int i = 0; i < 100; ++i) m[i] = i * 2;
    CHECK_EQ(m.size(), 100);
    CHECK_EQ(m.erase(50), 1);
    CHECK_EQ(m.erase(50), 0);
    CHECK_FALSE(m.contains(50));
    CHECK_EQ(m.size(), 99);
    for (int i = 0; i < 100; ++i) m.erase(i);
    CHECK(m.empty());
    CHECK(m.begin() == m.end());
}

TEST_CASE("art_map::ordered_iteration") {
    art_map<int, int> m;
    std::vector<int> keys = {5, -3, 100, 0, -1000, 7, 42, -50};
    for (int k : keys) m[k] = k;
    std::vector<int> sorted = keys;
    std::sort(sorted.begin(), sorted.end());
    std::vector<int> got;
    for (const auto& kv : m) got.push_back(kv.first);
    CHECK(got == sorted);

    // reverse
    std::vector<int> rgot;
    for (auto it = m.rbegin(); it != m.rend(); ++it) rgot.push_back(it->first);
    std::reverse(sorted.begin(), sorted.end());
    CHECK(rgot == sorted);
}

TEST_CASE("art_map::bounds") {
    art_map<int, int> m;
    for (int i = 0; i < 20; ++i) m[i * 10] = i;  // 0,10,...,190
    CHECK_EQ(m.lower_bound(0)->first, 0);
    CHECK_EQ(m.lower_bound(5)->first, 10);
    CHECK_EQ(m.lower_bound(10)->first, 10);
    CHECK_EQ(m.upper_bound(10)->first, 20);
    CHECK_EQ(m.lower_bound(195), m.end());
    CHECK_EQ(m.upper_bound(190), m.end());
    auto [lo, hi] = m.equal_range(50);
    CHECK_EQ(lo->first, 50);
    CHECK_EQ(hi->first, 60);
}

TEST_CASE("art_map::string_keys") {
    art_map<std::string, int> m;
    m["apple"] = 1;
    m["app"] = 2;        // prefix-of "apple"
    m["application"] = 3;
    m[""] = 4;           // empty key
    m["banana"] = 5;
    CHECK_EQ(m.size(), 5);
    CHECK_EQ(m.at(""), 4);
    CHECK_EQ(m.at("app"), 2);
    CHECK_EQ(m.at("apple"), 1);

    std::vector<std::string> got;
    for (const auto& kv : m) got.push_back(kv.first);
    std::vector<std::string> expect = {"", "app", "apple", "application", "banana"};
    CHECK(got == expect);

    CHECK_EQ(m.erase("app"), 1);
    CHECK_FALSE(m.contains("app"));
    CHECK(m.contains("apple"));
    CHECK(m.contains("application"));
}

TEST_CASE("art_map::bulk_load") {
    SUBCASE("matches inserts and stays mutable") {
        std::map<int, int> oracle;
        std::mt19937 rng(77);
        for (int i = 0; i < 5000; ++i) oracle[static_cast<int>(rng() % 3000) - 1500] = static_cast<int>(rng());
        std::vector<std::pair<const int, int>> sorted(oracle.begin(), oracle.end());

        art_map<int, int> m;
        m.bulk_load(sorted.begin(), sorted.end());
        CHECK_EQ(m.size(), oracle.size());

        auto oi = oracle.begin();
        auto mi = m.begin();
        for (; oi != oracle.end(); ++oi, ++mi) {
            REQUIRE(mi != m.end());
            CHECK_EQ(mi->first, oi->first);
            CHECK_EQ(mi->second, oi->second);
        }
        CHECK(mi == m.end());
        // still mutable after bulk load
        m[999999] = 1;
        CHECK(m.contains(999999));
        CHECK_EQ(m.erase(sorted.front().first), 1);
    }

    SUBCASE("empty / single / strings with shared prefix") {
        art_map<int, int> e;
        std::vector<std::pair<const int, int>> none;
        e.bulk_load(none.begin(), none.end());
        CHECK(e.empty());

        std::map<std::string, int> so;
        so["user/2026/a"] = 1;
        so["user/2026/aa"] = 2;  // prefix-of
        so["user/2026/b"] = 3;
        so["user/2027/zzzzzzzzzzzzzzzz"] = 4;  // long shared prefix > cap
        std::vector<std::pair<const std::string, int>> sv(so.begin(), so.end());
        art_map<std::string, int> sm;
        sm.bulk_load(sv.begin(), sv.end());
        CHECK_EQ(sm.size(), 4);
        std::vector<std::string> got;
        for (const auto& kv : sm) got.push_back(kv.first);
        std::vector<std::string> expect;
        for (auto& kv : so) expect.push_back(kv.first);
        CHECK(got == expect);
    }
}

TEST_CASE("art_map::for_each_ordered") {
    art_map<int, int> m;
    std::vector<int> keys = {5, -3, 100, 0, -1000, 7, 42, -50};
    for (int k : keys) m[k] = k * 2;
    std::vector<int> sorted = keys;
    std::sort(sorted.begin(), sorted.end());
    std::vector<int> got;
    int prev_val_sum = 0;
    m.for_each([&](const std::pair<const int, int>& kv) {
        got.push_back(kv.first);
        prev_val_sum += kv.second;
    });
    CHECK(got == sorted);
    CHECK_EQ(prev_val_sum, [&] { int s = 0; for (int k : keys) s += k * 2; return s; }());

    // empty map
    art_map<int, int> e;
    int count = 0;
    e.for_each([&](const std::pair<const int, int>&) { ++count; });
    CHECK_EQ(count, 0);
}

TEST_CASE("art_map::copy_move") {
    art_map<int, int> m;
    for (int i = 0; i < 50; ++i) m[i] = i;
    art_map<int, int> copy = m;
    CHECK_EQ(copy.size(), 50);
    for (int i = 0; i < 50; ++i) CHECK_EQ(copy.at(i), i);
    copy[1000] = 1;
    CHECK_FALSE(m.contains(1000));  // deep copy independence

    art_map<int, int> moved = std::move(m);
    CHECK_EQ(moved.size(), 50);
    CHECK(m.empty());
}

TEST_CASE("art_map::signed_and_float_ordering") {
    SUBCASE("signed ints sort correctly across zero") {
        art_map<int, int> m;
        for (int k : {-5, 3, -1, 0, 7, -100, 50}) m[k] = k;
        int prev = std::numeric_limits<int>::min();
        for (const auto& kv : m) {
            CHECK(kv.first > prev);
            prev = kv.first;
        }
    }
    SUBCASE("doubles sort correctly incl negatives") {
        art_map<double, int> m;
        for (double k : {-1.5, 2.5, 0.0, -0.5, 100.0, -100.0}) m[k] = 1;
        double prev = -1e300;
        for (const auto& kv : m) {
            CHECK(kv.first > prev);
            prev = kv.first;
        }
    }
}

// Regression: erasing keys that share a prefix longer than the path-compression
// cap builds single-child chains; collapse on erase must cascade to the parent,
// or the tree is corrupted (was a SEGV on destroy). Wide alphabet exercises
// node16/48/256 shrink paths too.
TEST_CASE("art_map::erase_cascade_long_prefix") {
    for (unsigned seed = 0; seed < 30; ++seed) {
        std::mt19937 rng(seed);
        std::map<std::string, int> oracle;
        art_map<std::string, int> art;
        int plen = 10 + static_cast<int>(seed % 12);  // shared prefix crosses cap=8
        std::string pre(static_cast<size_t>(plen), 'p');
        int alpha = 2 + static_cast<int>(seed % 25);
        int n = 1500 + static_cast<int>(seed) * 40;
        for (int i = 0; i < n; ++i) {
            std::string s = pre;
            int t = static_cast<int>(rng() % 7);
            for (int j = 0; j < t; ++j) s.push_back(static_cast<char>('a' + rng() % alpha));
            oracle[s] = i;
            art.insert_or_assign(s, i);
        }
        REQUIRE_EQ(art.size(), oracle.size());
        // erase everything in shuffled order, checking counts
        std::vector<std::string> keys;
        for (auto& kv : oracle) keys.push_back(kv.first);
        std::shuffle(keys.begin(), keys.end(), rng);
        size_t remaining = art.size();
        for (auto& k : keys) {
            CHECK_EQ(art.erase(k), 1);
            CHECK_EQ(art.size(), --remaining);
        }
        CHECK(art.empty());
        CHECK(art.begin() == art.end());
    }
}

// Regression (review P1): a node256 holding all 256 byte values must not let its
// child count wrap; erasing a sibling end_leaf must not free the full node.
TEST_CASE("art_map::node256_full_with_end_leaf") {
    art_map<std::string, int> m;
    m[""] = -1;  // ends at root -> end_leaf
    for (int b = 0; b < 256; ++b) {
        std::string k(1, static_cast<char>(b));  // 256 distinct first bytes -> root node256
        m[k] = b;
    }
    CHECK_EQ(m.size(), 257);
    CHECK_EQ(m.erase(""), 1);  // remove the end_leaf from the (full) node256
    CHECK_EQ(m.size(), 256);
    for (int b = 0; b < 256; ++b) {
        std::string k(1, static_cast<char>(b));
        REQUIRE(m.contains(k));
        CHECK_EQ(m.at(k), b);
    }
    // erase them all; tree must end empty and consistent
    for (int b = 0; b < 256; ++b) CHECK_EQ(m.erase(std::string(1, static_cast<char>(b))), 1);
    CHECK(m.empty());
}

// Regression (review P2): a moved-from map must be reusable without corruption.
TEST_CASE("art_map::move_then_reuse_source") {
    art_map<int, int> a;
    for (int i = 0; i < 5000; ++i) a[i] = i;
    art_map<int, int> b = std::move(a);
    CHECK_EQ(b.size(), 5000);
    CHECK(a.empty());
    // reuse the moved-from source: must allocate fresh, not into b's slabs
    for (int i = 0; i < 5000; ++i) a[i] = i * 2;
    CHECK_EQ(a.size(), 5000);
    for (int i = 0; i < 5000; ++i) CHECK_EQ(a.at(i), i * 2);
    // both independent
    CHECK_EQ(b.at(10), 10);
    a.clear();
    CHECK_EQ(b.size(), 5000);  // b unaffected by a's destruction path

    // move assignment variant
    art_map<int, int> c;
    c = std::move(b);
    CHECK_EQ(c.size(), 5000);
    b[7] = 7;  // reuse moved-from
    CHECK(b.contains(7));
}

// Regression (review P2): move assignment across unequal, non-propagating
// allocators (distinct PMR resources) must move elements, not steal slabs.
TEST_CASE("art_map::move_assign_allocator_aware") {
    using PA = std::pmr::polymorphic_allocator<std::pair<const int, int>>;
    std::pmr::monotonic_buffer_resource r1, r2;

    SUBCASE("unequal allocators -> element-wise move") {
        art_map<int, int, PA> a(PA{&r1});
        for (int i = 0; i < 2000; ++i) a[i] = i * 3;
        art_map<int, int, PA> b(PA{&r2});
        b[42] = -1;
        b = std::move(a);
        CHECK_EQ(b.size(), 2000);
        for (int i = 0; i < 2000; ++i) CHECK_EQ(b.at(i), i * 3);
        a.clear();  // moved-from is reusable
        a[7] = 7;
        CHECK(a.contains(7));
    }
    SUBCASE("equal allocators -> steal") {
        art_map<int, int, PA> c(PA{&r1}), d(PA{&r1});
        for (int i = 0; i < 1500; ++i) c[i] = i;
        d = std::move(c);
        CHECK_EQ(d.size(), 1500);
        for (int i = 0; i < 1500; ++i) CHECK_EQ(d.at(i), i);
    }
}

// Differential test against std::map as oracle.
TEST_CASE("art_map::differential_vs_std_map") {
    SUBCASE("int keys, insert/erase churn") {
        std::mt19937 rng(12345);
        std::map<int, int> oracle;
        art_map<int, int> art;
        for (int i = 0; i < 20000; ++i) {
            int k = static_cast<int>(rng() % 1000) - 500;
            if (rng() % 3 == 2) {
                CHECK_EQ(oracle.erase(k), art.erase(k));
            } else {
                int v = static_cast<int>(rng());
                oracle[k] = v;
                art.insert_or_assign(k, v);
            }
            REQUIRE_EQ(oracle.size(), art.size());
        }
        // full ordered comparison
        auto oi = oracle.begin();
        auto ai = art.begin();
        for (; oi != oracle.end(); ++oi, ++ai) {
            REQUIRE(ai != art.end());
            CHECK_EQ(oi->first, ai->first);
            CHECK_EQ(oi->second, ai->second);
        }
        CHECK(ai == art.end());
    }

    SUBCASE("string keys with shared prefixes") {
        std::mt19937 rng(999);
        std::map<std::string, int> oracle;
        art_map<std::string, int> art;
        auto mk = [&] {
            int len = static_cast<int>(rng() % 10);
            std::string s;
            for (int i = 0; i < len; ++i) s.push_back(static_cast<char>('a' + (rng() % 3)));
            return s;
        };
        for (int i = 0; i < 20000; ++i) {
            std::string k = mk();
            if (rng() % 3 == 2) {
                CHECK_EQ(oracle.erase(k), art.erase(k));
            } else {
                int v = static_cast<int>(rng());
                oracle[k] = v;
                art.insert_or_assign(k, v);
            }
            REQUIRE_EQ(oracle.size(), art.size());
        }
        auto oi = oracle.begin();
        auto ai = art.begin();
        for (; oi != oracle.end(); ++oi, ++ai) {
            REQUIRE(ai != art.end());
            CHECK_EQ(oi->first, ai->first);
        }
        CHECK(ai == art.end());

        // lower_bound probes
        for (int t = 0; t < 2000; ++t) {
            int len = static_cast<int>(rng() % 10);
            std::string p;
            for (int i = 0; i < len; ++i) p.push_back(static_cast<char>('a' + (rng() % 3)));
            auto ol = oracle.lower_bound(p);
            auto al = art.lower_bound(p);
            CHECK_EQ(ol == oracle.end(), al == art.end());
            if (ol != oracle.end() && al != art.end()) CHECK_EQ(ol->first, al->first);
        }
    }
}

}  // namespace stdb::container

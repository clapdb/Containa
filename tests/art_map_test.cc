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

// Regression (review): for_each on a const map must pass const refs for ALL
// entries, including the end_leaf (e.g. empty-string key). A generic callback
// would otherwise be instantiated with a mutable ref for that entry — caught
// here at compile time by the static_assert inside the lambda.
TEST_CASE("art_map::for_each_const_end_leaf") {
    art_map<std::string, int> m;
    m[""] = 1;  // stored as end_leaf at the root
    m["a"] = 2;
    m["bb"] = 3;
    const auto& cm = m;
    int n = 0;
    cm.for_each([&](auto& kv) {
        static_assert(std::is_const_v<std::remove_reference_t<decltype(kv)>>,
                      "for_each must hand const refs on a const map");
        ++n;
    });
    CHECK_EQ(n, 3);
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
    SUBCASE("negative zero is the same key as positive zero") {
        art_map<double, int> m;
        m.insert({-0.0, 1});
        m.insert({0.0, 2});  // -0.0 == 0.0: same key, so insert is a no-op (first write wins)
        CHECK_EQ(m.size(), 1);
        auto it = m.find(0.0);
        REQUIRE(it != m.end());
        CHECK_EQ(it->second, 1);
        CHECK(m.find(-0.0) != m.end());  // both signs locate the single entry
    }
    SUBCASE("negative zero is the same key as positive zero (float, operator[] overwrites)") {
        art_map<float, int> m;
        m[-0.0F] = 1;
        m[0.0F] = 2;  // operator[] on the same key overwrites
        CHECK_EQ(m.size(), 1);
        CHECK_EQ(m[0.0F], 2);
        CHECK_EQ(m[-0.0F], 2);
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
    SUBCASE("copy ctor selects allocator (no dangling source resource)") {
        art_map<int, int, PA> src(PA{&r1});
        for (int i = 0; i < 1000; ++i) src[i] = i;
        art_map<int, int, PA> cp = src;  // must NOT retain &r1
        CHECK(cp.get_allocator().resource() != &r1);
        CHECK(cp.get_allocator().resource() == std::pmr::get_default_resource());
        CHECK_EQ(cp.size(), 1000);
        for (int i = 0; i < 1000; ++i) CHECK_EQ(cp.at(i), i);
    }
}

// Regression (review P2): a propagating (POCCA) allocator must be adopted on
// copy assignment before cloning.
namespace pocca_test {
template <typename T>
struct IdAlloc {
    using value_type = T;
    using propagate_on_container_copy_assignment = std::true_type;
    using propagate_on_container_move_assignment = std::true_type;
    int id = 0;
    IdAlloc() = default;
    explicit IdAlloc(int i) : id(i) {}
    template <typename U>
    IdAlloc(const IdAlloc<U>& o) : id(o.id) {}
    T* allocate(std::size_t n) { return std::allocator<T>{}.allocate(n); }
    void deallocate(T* p, std::size_t n) { std::allocator<T>{}.deallocate(p, n); }
    bool operator==(const IdAlloc& o) const { return id == o.id; }
    bool operator!=(const IdAlloc& o) const { return id != o.id; }
};
}  // namespace pocca_test

TEST_CASE("art_map::copy_assign_propagates_allocator") {
    using A = pocca_test::IdAlloc<std::pair<const int, int>>;
    art_map<int, int, A> a(A{1});
    for (int i = 0; i < 1000; ++i) a[i] = i;
    art_map<int, int, A> b(A{2});
    b[9] = 9;
    b = a;  // POCCA -> b adopts allocator id 1
    CHECK_EQ(b.get_allocator().id, 1);
    CHECK_EQ(b.size(), 1000);
    for (int i = 0; i < 1000; ++i) CHECK_EQ(b.at(i), i);
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

// Allocator that throws std::bad_alloc on the Nth allocation once armed, to exercise the
// exception-safety of clone() (copy) and the insert split paths.
namespace {
struct throw_ctl
{
    long remaining = -1;   // -1 = disabled
    long allocations = 0;  // total slab allocations observed (for leak/reuse assertions)
    long live = 0;         // currently-outstanding slab allocations (released == 0)
};
inline throw_ctl& g_throw_ctl() {
    static throw_ctl ctl;
    return ctl;
}
// A value type whose default construction throws on demand, to exercise make_leaf's slot
// cleanup when key/value construction fails.
inline bool& g_value_throws() {
    static bool b = false;
    return b;
}
struct ThrowingValue
{
    int v = 0;
    ThrowingValue() {
        if (g_value_throws()) throw std::runtime_error("ThrowingValue");
    }
    explicit ThrowingValue(int x) : v(x) {}
};
template <typename T>
struct throwing_alloc
{
    using value_type = T;
    throwing_alloc() = default;
    template <typename U>
    throwing_alloc(const throwing_alloc<U>&) noexcept {}
    T* allocate(std::size_t n) {
        auto& ctl = g_throw_ctl();
        ++ctl.allocations;
        if (ctl.remaining == 0) {
            ctl.remaining = -1;
            throw std::bad_alloc();
        }
        if (ctl.remaining > 0) --ctl.remaining;
        ++ctl.live;
        return static_cast<T*>(::operator new(n * sizeof(T)));
    }
    void deallocate(T* p, std::size_t) noexcept {
        --g_throw_ctl().live;
        ::operator delete(p);
    }
    template <typename U>
    bool operator==(const throwing_alloc<U>&) const noexcept {
        return true;
    }
    template <typename U>
    bool operator!=(const throwing_alloc<U>&) const noexcept {
        return false;
    }
};
}  // namespace

TEST_CASE("art_map::exception_safety_on_copy_and_split") {
    using Map = art_map<std::string, int, throwing_alloc<std::pair<const std::string, int>>>;

    SUBCASE("copy construction that throws mid-clone leaves the source intact") {
        Map m;
        g_throw_ctl().remaining = -1;
        for (int i = 0; i < 2000; ++i) {
            m[std::string("prefix/path/") + std::to_string(i)] = i;
            m[std::string(1, char('a' + (i % 26))) + std::to_string(i)] = i;
        }
        const std::size_t expect = m.size();

        // Sweep the throw point across every allocation the copy makes. Each failed copy
        // must leave the source unchanged (and, under ASAN, must not leak/UAF).
        bool saw_throw = false;
        for (long k = 0;; ++k) {
            g_throw_ctl().remaining = k;
            try {
                Map c = m;
                g_throw_ctl().remaining = -1;
                CHECK_EQ(c.size(), m.size());
                CHECK_EQ(c.at("prefix/path/0"), 0);
                break;  // k exceeded the copy's allocation count
            } catch (const std::bad_alloc&) {
                saw_throw = true;
            }
            g_throw_ctl().remaining = -1;
            CHECK_EQ(m.size(), expect);
            CHECK_EQ(m.at("prefix/path/1234"), 1234);
        }
        CHECK(saw_throw);
    }

    SUBCASE("a split insert that throws leaves the existing entry reachable") {
        const std::string k1 = std::string(80, 'x') + "AAA";  // share a prefix >> kCap
        const std::string k2 = std::string(80, 'x') + "BBB";
        bool saw_throw = false;
        for (long k = 0;; ++k) {
            Map mm;
            g_throw_ctl().remaining = -1;
            mm[k1] = 1;
            g_throw_ctl().remaining = k;
            try {
                mm.insert({k2, 2});
                g_throw_ctl().remaining = -1;
                CHECK_EQ(mm.size(), 2);
                CHECK_EQ(mm.at(k1), 1);
                CHECK_EQ(mm.at(k2), 2);
                break;
            } catch (const std::bad_alloc&) {
                saw_throw = true;
            }
            g_throw_ctl().remaining = -1;
            CHECK_EQ(mm.size(), 1);
            CHECK(mm.contains(k1));
            CHECK_EQ(mm.at(k1), 1);
            CHECK_FALSE(mm.contains(k2));
        }
        CHECK(saw_throw);
    }

    SUBCASE("initializer-list construction that throws does not leak") {
        // ~art_map() does not run on constructor failure; a throw partway must still tear
        // down the staged tree (verified leak-free under ASAN).
        bool saw_throw = false;
        for (long k = 0;; ++k) {
            g_throw_ctl().remaining = k;
            try {
                Map m({{"alpha", 1}, {"beta", 2}, {"gamma", 3}, {"delta", 4}, {"epsilon", 5}});
                g_throw_ctl().remaining = -1;
                CHECK_EQ(m.size(), 5);
                break;
            } catch (const std::bad_alloc&) {
                saw_throw = true;
            }
            g_throw_ctl().remaining = -1;
        }
        CHECK(saw_throw);
    }

    SUBCASE("bulk_load that throws frees the staged leaves and leaves the map empty") {
        // Fixed-width keys "k1000".."k1399" are already in ascending (encoded) order.
        std::vector<std::pair<const std::string, int>> data;
        for (int i = 0; i < 400; ++i) data.emplace_back(std::string("k") + std::to_string(1000 + i), i);

        bool saw_throw = false;
        for (long k = 0;; ++k) {
            Map m;
            g_throw_ctl().remaining = -1;
            m["preexisting"] = 1;  // bulk_load clears this first
            g_throw_ctl().remaining = k;
            try {
                m.bulk_load(data.begin(), data.end());
                g_throw_ctl().remaining = -1;
                CHECK_EQ(m.size(), data.size());
                break;
            } catch (const std::bad_alloc&) {
                saw_throw = true;
            }
            g_throw_ctl().remaining = -1;
            // bulk_load cleared first, so a failed load leaves an empty, valid map (and,
            // under ASAN, must not leak the staged leaves).
            CHECK(m.empty());
            CHECK_EQ(m.size(), 0);
        }
        CHECK(saw_throw);
    }

    SUBCASE("erase whose shrink allocation fails still removes the element") {
        Map m;
        g_throw_ctl().remaining = -1;
        // bulk_load builds a node16 directly (5 runs), so the node4 pool stays empty and
        // the shrink-on-erase make4() is the very next slab allocation we can force to fail.
        std::vector<std::pair<const std::string, int>> data = {
          {"a", 1}, {"b", 2}, {"c", 3}, {"d", 4}, {"e", 5}};
        m.bulk_load(data.begin(), data.end());
        CHECK_EQ(m.size(), 5);

        g_throw_ctl().remaining = 0;  // next slab allocation (the shrink's make4) throws
        CHECK_NOTHROW(m.erase("a"));  // erase must succeed despite the failed shrink
        // The counter is reset to -1 only when a throw actually fires, so this confirms the
        // shrink allocation really was attempted and failed (the path under test).
        CHECK_EQ(g_throw_ctl().remaining, -1);
        g_throw_ctl().remaining = -1;

        CHECK_EQ(m.size(), 4);
        CHECK_FALSE(m.contains("a"));
        for (const char* k : {"b", "c", "d", "e"}) CHECK(m.contains(k));
        // the map (with the un-shrunk node) is still fully usable
        m["f"] = 6;
        CHECK_EQ(m.at("f"), 6);
        CHECK_EQ(m.size(), 5);
    }

    SUBCASE("insert returning a deep iterator path (beyond inline stack) is correct") {
        art_map<std::string, int> m;  // default allocator
        const int L = 40;
        const std::string base(L, 'a');
        // Force a branch at every depth so the route to `base` has ~L inner nodes — more
        // than the iterator stack's inline capacity (16) — exercising the up-front path
        // reserve in do_insert.
        for (int p = 0; p < L; ++p) m[base.substr(0, p) + "b"] = p;

        auto res = m.insert({base, 999});
        CHECK(res.second);
        REQUIRE(res.first != m.end());
        CHECK_EQ(res.first->first, base);
        CHECK_EQ(res.first->second, 999);

        // a re-insert (no structural change) must also return a valid deep iterator
        auto res2 = m.insert({base, 111});
        CHECK_FALSE(res2.second);
        REQUIRE(res2.first != m.end());
        CHECK_EQ(res2.first->second, 999);

        // the returned iterator is positioned: it can walk forward over the siblings
        CHECK_EQ(std::next(res.first) != m.end(), true);
    }

    SUBCASE("a leaf whose value construction throws returns its pool slot") {
        using TMap = art_map<int, ThrowingValue, throwing_alloc<std::pair<const int, ThrowingValue>>>;
        TMap m;
        g_throw_ctl().remaining = -1;  // allocator counts but never throws here
        g_value_throws() = false;
        m[-1];  // warm up: allocate the first leaf slab, size 1
        const long allocs0 = g_throw_ctl().allocations;

        // Every one of these inserts fails inside make_leaf (default-constructing the
        // value throws). The slot must be returned to the free list and reused, so the
        // leaf pool must NOT keep allocating fresh 512-slot slabs.
        for (int i = 0; i < 4000; ++i) {
            g_value_throws() = true;
            CHECK_THROWS_AS(m[i], std::runtime_error);
        }
        g_value_throws() = false;
        const long new_slabs = g_throw_ctl().allocations - allocs0;
        CHECK(new_slabs <= 1);  // without slot reuse this would be ~4000/512 slabs

        CHECK_EQ(m.size(), 1);  // nothing was actually inserted
        m[42] = ThrowingValue(42);
        CHECK_EQ(m.size(), 2);
        CHECK_EQ(m.at(42).v, 42);
    }

    SUBCASE("a long-prefix split whose mid-chain node allocation fails stays consistent") {
        // A shared prefix long enough that split_leaf's node4 chain spans more than one
        // 512-slot pool slab, so a make4() partway through the chain hits a fresh slab we
        // can force to fail while earlier shells are already staged in `head`.
        const std::string pre(5000, 'z');
        const std::string k1 = pre + "A";
        const std::string k2 = pre + "B";
        using IMap = art_map<std::string, int, throwing_alloc<std::pair<const std::string, int>>>;
        IMap m;
        g_throw_ctl().remaining = -1;
        m[k1] = 1;  // single leaf; node4 pool still empty

        g_throw_ctl().remaining = 1;  // first node4 slab succeeds, the mid-chain second fails
        CHECK_THROWS_AS(m.insert({k2, 2}), std::bad_alloc);
        g_throw_ctl().remaining = -1;

        // The existing entry is untouched and the map stays usable; the staged chain shells
        // were returned to the pool (verified clean under ASAN — no leak/UAF/double-free),
        // so the retry below reuses them and succeeds.
        CHECK_EQ(m.size(), 1);
        CHECK(m.contains(k1));
        CHECK_FALSE(m.contains(k2));

        m[k2] = 2;  // retry now succeeds
        CHECK_EQ(m.size(), 2);
        CHECK_EQ(m.at(k1), 1);
        CHECK_EQ(m.at(k2), 2);
    }
}

// Regression (review): a bulk_load that throws while bulk_build() is allocating
// inner shells must release those slabs immediately, not retain them until the
// next clear()/destroy. Sweep the throw point across leaf + shell allocations
// and assert no slab stays outstanding (live == 0) after a failed load.
TEST_CASE("art_map::bulk_load_releases_slabs_on_throw") {
    using Map = art_map<int, int, throwing_alloc<std::pair<const int, int>>>;
    std::vector<std::pair<const int, int>> kv;
    for (int i = 0; i < 6000; ++i) kv.push_back({i, i});  // sorted -> a real multi-node tree
    auto& ctl = g_throw_ctl();
    bool saw_throw = false;
    for (long arm = 1; arm <= 60; ++arm) {
        ctl.remaining = -1;
        ctl.live = 0;
        Map m;  // empty: no allocations yet
        ctl.remaining = arm;
        bool threw = false;
        try {
            m.bulk_load(kv.begin(), kv.end());
        } catch (const std::bad_alloc&) {
            threw = true;
        }
        ctl.remaining = -1;
        if (threw) {
            saw_throw = true;
            CHECK(m.empty());
            CHECK_EQ(ctl.live, 0);  // every slab (leaves + orphaned shells) released
        }
    }
    CHECK(saw_throw);
}

TEST_CASE("art_map::pmr_mapped_type_uses_map_resource") {
    using PV = std::pair<const int, std::pmr::string>;
    using PMap = art_map<int, std::pmr::string, std::pmr::polymorphic_allocator<PV>>;
    // A string long enough to force a heap allocation (beyond the SSO buffer); that heap
    // block must come from the map's resource, not the default one.
    const char* long_str = "the quick brown fox jumps over the lazy dog 0123456789abcdef";

    std::pmr::monotonic_buffer_resource res;
    PMap m{std::pmr::polymorphic_allocator<PV>{&res}};

    SUBCASE("try_emplace constructs the value on the map resource") {
        m.try_emplace(1, long_str);
        auto it = m.find(1);
        REQUIRE(it != m.end());
        CHECK_EQ(it->second.get_allocator().resource(), &res);
        CHECK_EQ(it->second, std::pmr::string(long_str));
    }
    SUBCASE("operator[] then assign keeps the value on the map resource") {
        m[2] = long_str;
        auto it = m.find(2);
        REQUIRE(it != m.end());
        CHECK_EQ(it->second.get_allocator().resource(), &res);
    }
    SUBCASE("insert of a value moves it onto the map resource") {
        m.insert(PV{3, std::pmr::string(long_str)});
        auto it = m.find(3);
        REQUIRE(it != m.end());
        CHECK_EQ(it->second.get_allocator().resource(), &res);
    }
}

}  // namespace stdb::container

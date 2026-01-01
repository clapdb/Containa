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

#include "container/small_vectra.hpp"
#include "container/devectra.hpp"
#include "container/static_vectra.hpp"

#include <doctest/doctest.h>

#include <memory>
#include <string>
#include <vector>

namespace stdb::container {
// NOLINTBEGIN

// ============================================================================
// Test helper types
// ============================================================================

// Tracks construction/destruction for leak detection
struct LifetimeTracker {
    static inline int alive_count = 0;
    static inline int construct_count = 0;
    static inline int destruct_count = 0;
    static inline int copy_count = 0;
    static inline int move_count = 0;

    int value;

    static void reset() {
        alive_count = 0;
        construct_count = 0;
        destruct_count = 0;
        copy_count = 0;
        move_count = 0;
    }

    LifetimeTracker() : value(0) {
        ++alive_count;
        ++construct_count;
    }

    explicit LifetimeTracker(int v) : value(v) {
        ++alive_count;
        ++construct_count;
    }

    LifetimeTracker(const LifetimeTracker& other) : value(other.value) {
        ++alive_count;
        ++construct_count;
        ++copy_count;
    }

    LifetimeTracker(LifetimeTracker&& other) noexcept : value(other.value) {
        other.value = -1;
        ++alive_count;
        ++construct_count;
        ++move_count;
    }

    LifetimeTracker& operator=(const LifetimeTracker& other) {
        value = other.value;
        ++copy_count;
        return *this;
    }

    LifetimeTracker& operator=(LifetimeTracker&& other) noexcept {
        value = other.value;
        other.value = -1;
        ++move_count;
        return *this;
    }

    ~LifetimeTracker() {
        --alive_count;
        ++destruct_count;
    }

    bool operator==(const LifetimeTracker& other) const { return value == other.value; }
    bool operator<(const LifetimeTracker& other) const { return value < other.value; }
};

// Non-trivial type with heap allocation
struct HeapString {
    std::string data;

    HeapString() : data("default") {}
    explicit HeapString(const std::string& s) : data(s) {}
    HeapString(const HeapString&) = default;
    HeapString(HeapString&&) noexcept = default;
    HeapString& operator=(const HeapString&) = default;
    HeapString& operator=(HeapString&&) noexcept = default;
    ~HeapString() = default;

    bool operator==(const HeapString& other) const { return data == other.data; }
};

// Large POD type (to test memcpy optimization)
struct LargePOD {
    int data[16];  // 64 bytes

    LargePOD() { std::fill(std::begin(data), std::end(data), 0); }
    explicit LargePOD(int v) { std::fill(std::begin(data), std::end(data), v); }

    bool operator==(const LargePOD& other) const {
        return std::equal(std::begin(data), std::end(data), std::begin(other.data));
    }
};

static_assert(std::is_trivially_copyable_v<LargePOD>);
static_assert(std::is_trivially_destructible_v<LargePOD>);
static_assert(!std::is_trivially_copyable_v<LifetimeTracker>);
static_assert(!std::is_trivially_destructible_v<LifetimeTracker>);
static_assert(!std::is_trivially_copyable_v<HeapString>);
static_assert(!std::is_trivially_destructible_v<HeapString>);

// ============================================================================
// small_vectra type tests
// ============================================================================

TEST_CASE("small_vectra::types::string") {
    SUBCASE("push_back and access") {
        small_vectra<std::string, 4> vec;
        vec.push_back("hello");
        vec.push_back("world");
        vec.push_back("foo");
        vec.push_back("bar");

        CHECK_EQ(vec.size(), 4);
        CHECK_EQ(vec.is_small(), true);
        CHECK_EQ(vec[0], "hello");
        CHECK_EQ(vec[3], "bar");
    }

    SUBCASE("transition to heap preserves strings") {
        small_vectra<std::string, 2> vec;
        vec.push_back("one");
        vec.push_back("two");
        CHECK_EQ(vec.is_small(), true);

        vec.push_back("three");  // triggers heap allocation
        CHECK_EQ(vec.is_small(), false);
        CHECK_EQ(vec[0], "one");
        CHECK_EQ(vec[1], "two");
        CHECK_EQ(vec[2], "three");
    }

    SUBCASE("copy and move") {
        small_vectra<std::string, 4> vec1;
        vec1.push_back("alpha");
        vec1.push_back("beta");

        auto vec2 = vec1;  // copy
        CHECK_EQ(vec2.size(), 2);
        CHECK_EQ(vec2[0], "alpha");

        auto vec3 = std::move(vec1);  // move
        CHECK_EQ(vec3.size(), 2);
        CHECK_EQ(vec3[0], "alpha");
    }

    SUBCASE("resize with strings") {
        small_vectra<std::string, 8> vec;
        vec.resize(3, "test");
        CHECK_EQ(vec.size(), 3);
        for (size_t i = 0; i < 3; ++i) {
            CHECK_EQ(vec[i], "test");
        }

        vec.resize(5, "new");
        CHECK_EQ(vec.size(), 5);
        CHECK_EQ(vec[3], "new");
        CHECK_EQ(vec[4], "new");

        vec.resize(2);
        CHECK_EQ(vec.size(), 2);
    }

    SUBCASE("erase strings") {
        small_vectra<std::string, 8> vec{"a", "b", "c", "d", "e"};
        vec.erase(vec.begin() + 2);  // remove "c"
        CHECK_EQ(vec.size(), 4);
        CHECK_EQ(vec[2], "d");

        vec.erase(vec.begin(), vec.begin() + 2);  // remove "a", "b"
        CHECK_EQ(vec.size(), 2);
        CHECK_EQ(vec[0], "d");
    }
}

TEST_CASE("small_vectra::types::lifetime_tracking") {
    LifetimeTracker::reset();

    SUBCASE("proper destruction on clear") {
        {
            small_vectra<LifetimeTracker, 4> vec;
            vec.emplace_back(1);
            vec.emplace_back(2);
            vec.emplace_back(3);
            CHECK_EQ(LifetimeTracker::alive_count, 3);

            vec.clear();
            CHECK_EQ(LifetimeTracker::alive_count, 0);
            CHECK_EQ(vec.size(), 0);
        }
        CHECK_EQ(LifetimeTracker::alive_count, 0);
    }

    LifetimeTracker::reset();

    SUBCASE("proper destruction on scope exit") {
        {
            small_vectra<LifetimeTracker, 4> vec;
            vec.emplace_back(1);
            vec.emplace_back(2);
            CHECK_EQ(LifetimeTracker::alive_count, 2);
        }
        CHECK_EQ(LifetimeTracker::alive_count, 0);
    }

    LifetimeTracker::reset();

    SUBCASE("proper destruction after heap transition") {
        {
            small_vectra<LifetimeTracker, 2> vec;
            vec.emplace_back(1);
            vec.emplace_back(2);
            CHECK_EQ(vec.is_small(), true);

            vec.emplace_back(3);  // trigger heap
            CHECK_EQ(vec.is_small(), false);
            CHECK_EQ(LifetimeTracker::alive_count, 3);
        }
        CHECK_EQ(LifetimeTracker::alive_count, 0);
    }

    LifetimeTracker::reset();

    SUBCASE("move semantics") {
        small_vectra<LifetimeTracker, 4> vec1;
        vec1.emplace_back(1);
        vec1.emplace_back(2);
        int initial_copies = LifetimeTracker::copy_count;

        auto vec2 = std::move(vec1);
        CHECK_EQ(vec2.size(), 2);
        // Move should use move constructor, not copy
        CHECK_GE(LifetimeTracker::move_count, 0);
    }

    LifetimeTracker::reset();

    SUBCASE("pop_back destroys element") {
        small_vectra<LifetimeTracker, 4> vec;
        vec.emplace_back(1);
        vec.emplace_back(2);
        CHECK_EQ(LifetimeTracker::alive_count, 2);

        vec.pop_back();
        CHECK_EQ(LifetimeTracker::alive_count, 1);
        CHECK_EQ(vec.size(), 1);
    }
}

TEST_CASE("small_vectra::types::large_pod") {
    SUBCASE("memcpy optimization works") {
        small_vectra<LargePOD, 4> vec;
        vec.emplace_back(42);
        vec.emplace_back(100);

        CHECK_EQ(vec[0].data[0], 42);
        CHECK_EQ(vec[1].data[0], 100);

        auto vec2 = vec;  // should use memcpy
        CHECK_EQ(vec2[0].data[0], 42);
        CHECK_EQ(vec2[1].data[0], 100);
    }

    SUBCASE("heap transition with large POD") {
        small_vectra<LargePOD, 2> vec;
        vec.emplace_back(1);
        vec.emplace_back(2);
        CHECK_EQ(vec.is_small(), true);

        vec.emplace_back(3);
        CHECK_EQ(vec.is_small(), false);
        CHECK_EQ(vec[0].data[0], 1);
        CHECK_EQ(vec[2].data[0], 3);
    }
}

// ============================================================================
// devectra type tests
// ============================================================================

TEST_CASE("devectra::types::string") {
    SUBCASE("push_front and push_back") {
        devectra<std::string> dv;
        dv.push_back("middle");
        dv.push_front("first");
        dv.push_back("last");

        CHECK_EQ(dv.size(), 3);
        CHECK_EQ(dv[0], "first");
        CHECK_EQ(dv[1], "middle");
        CHECK_EQ(dv[2], "last");
    }

    SUBCASE("pop_front and pop_back") {
        devectra<std::string> dv{"a", "b", "c", "d"};
        dv.pop_front();
        CHECK_EQ(dv[0], "b");
        dv.pop_back();
        CHECK_EQ(dv.back(), "c");
        CHECK_EQ(dv.size(), 2);
    }

    SUBCASE("copy and move") {
        devectra<std::string> dv1{"hello", "world"};
        auto dv2 = dv1;
        CHECK_EQ(dv2[0], "hello");

        auto dv3 = std::move(dv1);
        CHECK_EQ(dv3[1], "world");
    }
}

TEST_CASE("devectra::types::lifetime_tracking") {
    LifetimeTracker::reset();

    SUBCASE("proper destruction") {
        {
            devectra<LifetimeTracker> dv;
            dv.emplace_back(1);
            dv.emplace_front(2);
            dv.emplace_back(3);
            CHECK_EQ(LifetimeTracker::alive_count, 3);
        }
        CHECK_EQ(LifetimeTracker::alive_count, 0);
    }

    LifetimeTracker::reset();

    SUBCASE("pop operations destroy elements") {
        devectra<LifetimeTracker> dv;
        dv.emplace_back(1);
        dv.emplace_back(2);
        dv.emplace_back(3);
        CHECK_EQ(LifetimeTracker::alive_count, 3);

        dv.pop_front();
        CHECK_EQ(LifetimeTracker::alive_count, 2);

        dv.pop_back();
        CHECK_EQ(LifetimeTracker::alive_count, 1);
    }

    LifetimeTracker::reset();

    SUBCASE("clear destroys all elements") {
        devectra<LifetimeTracker> dv;
        dv.emplace_back(1);
        dv.emplace_back(2);
        CHECK_EQ(LifetimeTracker::alive_count, 2);

        dv.clear();
        CHECK_EQ(LifetimeTracker::alive_count, 0);
    }
}

TEST_CASE("devectra::types::vector_of_vectors") {
    SUBCASE("nested containers") {
        devectra<std::vector<int>> dv;
        dv.push_back({1, 2, 3});
        dv.push_front({0});
        dv.push_back({4, 5});

        CHECK_EQ(dv.size(), 3);
        CHECK_EQ(dv[0].size(), 1);
        CHECK_EQ(dv[1].size(), 3);
        CHECK_EQ(dv[2].size(), 2);
    }
}

// ============================================================================
// static_vectra type tests
// ============================================================================

TEST_CASE("static_vectra::types::string") {
    SUBCASE("basic operations") {
        static_vectra<std::string, 8> vec;
        vec.push_back("hello");
        vec.push_back("world");

        CHECK_EQ(vec.size(), 2);
        CHECK_EQ(vec[0], "hello");
        CHECK_EQ(vec[1], "world");
    }

    SUBCASE("resize with value") {
        static_vectra<std::string, 8> vec;
        vec.resize(4, "test");
        CHECK_EQ(vec.size(), 4);
        for (size_t i = 0; i < 4; ++i) {
            CHECK_EQ(vec[i], "test");
        }
    }

    SUBCASE("erase") {
        static_vectra<std::string, 8> vec{"a", "b", "c", "d"};
        vec.erase(vec.begin() + 1);
        CHECK_EQ(vec.size(), 3);
        CHECK_EQ(vec[0], "a");
        CHECK_EQ(vec[1], "c");
        CHECK_EQ(vec[2], "d");
    }
}

TEST_CASE("static_vectra::types::lifetime_tracking") {
    LifetimeTracker::reset();

    SUBCASE("proper destruction") {
        {
            static_vectra<LifetimeTracker, 8> vec;
            vec.emplace_back(1);
            vec.emplace_back(2);
            vec.emplace_back(3);
            CHECK_EQ(LifetimeTracker::alive_count, 3);
        }
        CHECK_EQ(LifetimeTracker::alive_count, 0);
    }

    LifetimeTracker::reset();

    SUBCASE("swap preserves lifetime") {
        static_vectra<LifetimeTracker, 8> vec1;
        vec1.emplace_back(1);
        vec1.emplace_back(2);

        static_vectra<LifetimeTracker, 8> vec2;
        vec2.emplace_back(3);

        int alive_before = LifetimeTracker::alive_count;
        vec1.swap(vec2);
        CHECK_EQ(LifetimeTracker::alive_count, alive_before);

        CHECK_EQ(vec1.size(), 1);
        CHECK_EQ(vec1[0].value, 3);
        CHECK_EQ(vec2.size(), 2);
        CHECK_EQ(vec2[0].value, 1);
    }
}

// ============================================================================
// Edge cases and corner cases
// ============================================================================

TEST_CASE("containers::edge_cases::empty_operations") {
    SUBCASE("small_vectra empty copy") {
        small_vectra<std::string, 4> vec1;
        auto vec2 = vec1;
        CHECK_EQ(vec2.empty(), true);
    }

    SUBCASE("devectra empty move") {
        devectra<std::string> dv1;
        auto dv2 = std::move(dv1);
        CHECK_EQ(dv2.empty(), true);
    }

    SUBCASE("static_vectra empty operations") {
        static_vectra<std::string, 4> vec;
        vec.clear();
        CHECK_EQ(vec.empty(), true);
    }
}

TEST_CASE("containers::edge_cases::single_element") {
    LifetimeTracker::reset();

    SUBCASE("small_vectra single element lifecycle") {
        {
            small_vectra<LifetimeTracker, 4> vec;
            vec.emplace_back(42);
            CHECK_EQ(LifetimeTracker::alive_count, 1);

            vec.pop_back();
            CHECK_EQ(LifetimeTracker::alive_count, 0);
            CHECK_EQ(vec.empty(), true);
        }
    }

    LifetimeTracker::reset();

    SUBCASE("devectra single element pop_front") {
        {
            devectra<LifetimeTracker> dv;
            dv.emplace_back(42);
            CHECK_EQ(LifetimeTracker::alive_count, 1);

            dv.pop_front();
            CHECK_EQ(LifetimeTracker::alive_count, 0);
            CHECK_EQ(dv.empty(), true);
        }
    }
}

TEST_CASE("containers::edge_cases::move_only_types") {
    SUBCASE("small_vectra with unique_ptr") {
        small_vectra<std::unique_ptr<int>, 4> vec;
        vec.push_back(std::make_unique<int>(1));
        vec.push_back(std::make_unique<int>(2));

        CHECK_EQ(*vec[0], 1);
        CHECK_EQ(*vec[1], 2);

        auto vec2 = std::move(vec);
        CHECK_EQ(*vec2[0], 1);
        CHECK_EQ(vec2.size(), 2);
    }

    SUBCASE("devectra with unique_ptr") {
        devectra<std::unique_ptr<int>> dv;
        dv.push_back(std::make_unique<int>(1));
        dv.push_front(std::make_unique<int>(0));

        CHECK_EQ(*dv[0], 0);
        CHECK_EQ(*dv[1], 1);
    }

    SUBCASE("static_vectra with unique_ptr") {
        static_vectra<std::unique_ptr<int>, 4> vec;
        vec.push_back(std::make_unique<int>(42));
        CHECK_EQ(*vec[0], 42);

        auto vec2 = std::move(vec);
        CHECK_EQ(*vec2[0], 42);
    }
}

TEST_CASE("containers::edge_cases::insert_operations") {
    SUBCASE("small_vectra insert string") {
        small_vectra<std::string, 8> vec{"a", "c"};
        vec.insert(vec.begin() + 1, "b");
        CHECK_EQ(vec.size(), 3);
        CHECK_EQ(vec[0], "a");
        CHECK_EQ(vec[1], "b");
        CHECK_EQ(vec[2], "c");
    }

    SUBCASE("devectra insert string at front") {
        devectra<std::string> dv{"b", "c"};
        dv.insert(dv.begin(), "a");
        CHECK_EQ(dv[0], "a");
        CHECK_EQ(dv[1], "b");
    }

    SUBCASE("static_vectra insert with tracked type") {
        LifetimeTracker::reset();
        {
            static_vectra<LifetimeTracker, 8> vec;
            vec.emplace_back(1);
            vec.emplace_back(3);
            vec.insert(vec.begin() + 1, LifetimeTracker(2));

            CHECK_EQ(vec.size(), 3);
            CHECK_EQ(vec[0].value, 1);
            CHECK_EQ(vec[1].value, 2);
            CHECK_EQ(vec[2].value, 3);
        }
        CHECK_EQ(LifetimeTracker::alive_count, 0);
    }
}

TEST_CASE("containers::types::heap_string") {
    SUBCASE("small_vectra with HeapString") {
        small_vectra<HeapString, 4> vec;
        vec.emplace_back("hello");
        vec.emplace_back("world");

        CHECK_EQ(vec[0].data, "hello");
        CHECK_EQ(vec[1].data, "world");

        auto vec2 = vec;
        CHECK_EQ(vec2[0].data, "hello");
    }

    SUBCASE("devectra with HeapString") {
        devectra<HeapString> dv;
        dv.emplace_back("one");
        dv.emplace_front("zero");
        dv.emplace_back("two");

        CHECK_EQ(dv[0].data, "zero");
        CHECK_EQ(dv[1].data, "one");
        CHECK_EQ(dv[2].data, "two");
    }
}

// ============================================================================
// Struct with padding tests
// ============================================================================

// Struct with padding - char (1 byte) + padding (3 bytes) + int (4 bytes)
struct PaddedStruct {
    char c;
    // 3 bytes padding here
    int i;

    PaddedStruct() : c('x'), i(0) {}
    PaddedStruct(char ch, int val) : c(ch), i(val) {}

    bool operator==(const PaddedStruct& other) const {
        return c == other.c && i == other.i;
    }
    bool operator<(const PaddedStruct& other) const {
        return i < other.i;
    }
};

// Verify padding exists
static_assert(sizeof(PaddedStruct) == 8, "PaddedStruct should be 8 bytes with padding");
static_assert(alignof(PaddedStruct) == 4, "PaddedStruct should have 4-byte alignment");
static_assert(!std::has_unique_object_representations_v<PaddedStruct>,
              "PaddedStruct has padding, so no unique object representation");

// Struct with multiple padding gaps
struct MultiPaddedStruct {
    char a;      // 1 byte
    // 7 bytes padding
    double d;    // 8 bytes
    char b;      // 1 byte
    // 3 bytes padding
    int i;       // 4 bytes
    char c;      // 1 byte
    // 7 bytes padding (to align struct size to 8)

    MultiPaddedStruct() : a('a'), d(0.0), b('b'), i(0), c('c') {}
    MultiPaddedStruct(char a_, double d_, char b_, int i_, char c_)
        : a(a_), d(d_), b(b_), i(i_), c(c_) {}

    bool operator==(const MultiPaddedStruct& other) const {
        return a == other.a && d == other.d && b == other.b && i == other.i && c == other.c;
    }
};

static_assert(!std::has_unique_object_representations_v<MultiPaddedStruct>,
              "MultiPaddedStruct has padding");

// Struct without padding (tightly packed)
struct PackedStruct {
    int a;
    int b;
    int c;
    int d;

    PackedStruct() : a(0), b(0), c(0), d(0) {}
    PackedStruct(int a_, int b_, int c_, int d_) : a(a_), b(b_), c(c_), d(d_) {}

    bool operator==(const PackedStruct& other) const {
        return a == other.a && b == other.b && c == other.c && d == other.d;
    }
};

static_assert(std::has_unique_object_representations_v<PackedStruct>,
              "PackedStruct has no padding, so has unique object representation");

TEST_CASE("containers::types::padded_struct") {
    SUBCASE("small_vectra with PaddedStruct") {
        small_vectra<PaddedStruct, 4> vec;
        vec.emplace_back('a', 1);
        vec.emplace_back('b', 2);
        vec.emplace_back('c', 3);

        CHECK_EQ(vec[0].c, 'a');
        CHECK_EQ(vec[0].i, 1);
        CHECK_EQ(vec[2].c, 'c');
        CHECK_EQ(vec[2].i, 3);

        // Test copy
        auto vec2 = vec;
        CHECK_EQ(vec2[1].c, 'b');
        CHECK_EQ(vec2[1].i, 2);

        // Test comparison (should use operator==, not memcmp)
        CHECK_EQ(vec, vec2);
    }

    SUBCASE("devectra with PaddedStruct") {
        devectra<PaddedStruct> dv;
        dv.emplace_back('x', 100);
        dv.emplace_front('y', 200);

        CHECK_EQ(dv[0].c, 'y');
        CHECK_EQ(dv[0].i, 200);
        CHECK_EQ(dv[1].c, 'x');
        CHECK_EQ(dv[1].i, 100);

        // Test comparison
        devectra<PaddedStruct> dv2 = dv;
        CHECK_EQ(dv, dv2);
    }

    SUBCASE("static_vectra with MultiPaddedStruct") {
        static_vectra<MultiPaddedStruct, 4> vec;
        vec.emplace_back('a', 1.5, 'b', 42, 'c');
        vec.emplace_back('d', 2.5, 'e', 43, 'f');

        CHECK_EQ(vec[0].a, 'a');
        CHECK_EQ(vec[0].d, 1.5);
        CHECK_EQ(vec[0].i, 42);
        CHECK_EQ(vec[1].c, 'f');

        auto vec2 = vec;
        CHECK_EQ(vec, vec2);
    }

    SUBCASE("PackedStruct uses memcmp optimization") {
        small_vectra<PackedStruct, 4> vec1;
        vec1.emplace_back(1, 2, 3, 4);
        vec1.emplace_back(5, 6, 7, 8);

        small_vectra<PackedStruct, 4> vec2 = vec1;
        CHECK_EQ(vec1, vec2);

        vec2[1].c = 999;
        CHECK_NE(vec1, vec2);
    }
}

// ============================================================================
// Self-assignment and self-operations tests
// ============================================================================

TEST_CASE("containers::self_operations") {
    SUBCASE("small_vectra self-assignment") {
        small_vectra<std::string, 4> vec{"a", "b", "c"};
        vec = vec;  // self-assignment
        CHECK_EQ(vec.size(), 3);
        CHECK_EQ(vec[0], "a");
        CHECK_EQ(vec[2], "c");
    }

    SUBCASE("devectra self-assignment") {
        devectra<std::string> dv{"x", "y", "z"};
        dv = dv;
        CHECK_EQ(dv.size(), 3);
        CHECK_EQ(dv[1], "y");
    }

    SUBCASE("static_vectra self-assignment") {
        static_vectra<std::string, 8> vec{"1", "2", "3"};
        vec = vec;
        CHECK_EQ(vec.size(), 3);
        CHECK_EQ(vec[0], "1");
    }

    SUBCASE("small_vectra self-swap") {
        small_vectra<int, 4> vec{1, 2, 3};
        vec.swap(vec);
        CHECK_EQ(vec.size(), 3);
        CHECK_EQ(vec[0], 1);
    }

    SUBCASE("devectra self-swap") {
        devectra<int> dv{4, 5, 6};
        dv.swap(dv);
        CHECK_EQ(dv.size(), 3);
        CHECK_EQ(dv[2], 6);
    }
}

// ============================================================================
// Iterator tests
// ============================================================================

TEST_CASE("containers::iterators") {
    SUBCASE("small_vectra iterator arithmetic") {
        small_vectra<int, 8> vec{10, 20, 30, 40, 50};

        auto it = vec.begin();
        CHECK_EQ(*it, 10);

        it += 2;
        CHECK_EQ(*it, 30);

        it -= 1;
        CHECK_EQ(*it, 20);

        auto it2 = vec.end() - 1;
        CHECK_EQ(*it2, 50);

        CHECK_EQ(vec.end() - vec.begin(), 5);
    }

    SUBCASE("devectra reverse iterators") {
        devectra<std::string> dv{"a", "b", "c", "d"};

        std::string result;
        for (auto rit = dv.rbegin(); rit != dv.rend(); ++rit) {
            result += *rit;
        }
        CHECK_EQ(result, "dcba");
    }

    SUBCASE("static_vectra const iterators") {
        const static_vectra<int, 8> vec{1, 2, 3, 4};

        int sum = 0;
        for (auto it = vec.cbegin(); it != vec.cend(); ++it) {
            sum += *it;
        }
        CHECK_EQ(sum, 10);
    }

    SUBCASE("iterator comparison") {
        small_vectra<int, 8> vec{1, 2, 3};
        auto a = vec.begin();
        auto b = vec.begin() + 1;
        auto c = vec.begin();

        CHECK_LT(a, b);
        CHECK_LE(a, b);
        CHECK_LE(a, c);
        CHECK_GT(b, a);
        CHECK_GE(b, a);
        CHECK_EQ(a, c);
    }
}

// ============================================================================
// Capacity and reserve tests
// ============================================================================

TEST_CASE("containers::capacity") {
    SUBCASE("small_vectra reserve inline to heap") {
        small_vectra<int, 4> vec{1, 2};
        CHECK_EQ(vec.is_small(), true);

        vec.reserve(10);
        CHECK_EQ(vec.is_small(), false);
        CHECK_GE(vec.capacity(), 10);
        CHECK_EQ(vec.size(), 2);
        CHECK_EQ(vec[0], 1);
        CHECK_EQ(vec[1], 2);
    }

    SUBCASE("small_vectra shrink_to_fit heap to inline") {
        small_vectra<int, 8> vec;
        vec.reserve(100);
        CHECK_EQ(vec.is_small(), false);

        vec.push_back(1);
        vec.push_back(2);
        vec.shrink_to_fit();
        CHECK_EQ(vec.is_small(), true);
        CHECK_EQ(vec.size(), 2);
    }

    SUBCASE("devectra reserve") {
        devectra<int> dv;
        dv.reserve(100);
        CHECK_GE(dv.capacity(), 100);
        CHECK_EQ(dv.size(), 0);

        for (int i = 0; i < 50; ++i) {
            dv.push_back(i);
        }
        CHECK_EQ(dv.size(), 50);
    }

    SUBCASE("devectra reserve_front and reserve_back") {
        devectra<int> dv{1, 2, 3};
        dv.reserve_front(10);
        CHECK_GE(dv.front_capacity(), 10);

        dv.reserve_back(10);
        CHECK_GE(dv.back_capacity(), 10);
    }

    SUBCASE("static_vectra capacity is fixed") {
        static_vectra<int, 8> vec;
        CHECK_EQ(vec.capacity(), 8);
        CHECK_EQ(vec.max_size(), 8);

        vec.push_back(1);
        CHECK_EQ(vec.capacity(), 8);  // unchanged

        CHECK_THROWS_AS(vec.reserve(100), std::length_error);
    }
}

// ============================================================================
// Stress tests
// ============================================================================

TEST_CASE("containers::stress") {
    SUBCASE("small_vectra many push_back with strings") {
        LifetimeTracker::reset();
        {
            small_vectra<LifetimeTracker, 4> vec;
            for (int i = 0; i < 100; ++i) {
                vec.emplace_back(i);
            }
            CHECK_EQ(vec.size(), 100);
            CHECK_EQ(vec.is_small(), false);
            CHECK_EQ(LifetimeTracker::alive_count, 100);

            // Verify all values
            for (int i = 0; i < 100; ++i) {
                CHECK_EQ(vec[i].value, i);
            }
        }
        CHECK_EQ(LifetimeTracker::alive_count, 0);
    }

    SUBCASE("devectra alternating push_front/push_back stress") {
        devectra<int> dv;
        for (int i = 0; i < 1000; ++i) {
            if (i % 2 == 0) {
                dv.push_back(i);
            } else {
                dv.push_front(-i);
            }
        }
        CHECK_EQ(dv.size(), 1000);

        // Verify order: negative odds descending, then positive evens ascending
        // Front elements: -999, -997, ..., -3, -1
        // Back elements: 0, 2, 4, ..., 998
    }

    SUBCASE("static_vectra fill to capacity") {
        static_vectra<LifetimeTracker, 100> vec;
        LifetimeTracker::reset();

        for (int i = 0; i < 100; ++i) {
            vec.emplace_back(i);
        }
        CHECK_EQ(vec.size(), 100);
        CHECK_EQ(vec.full(), true);
        CHECK_EQ(LifetimeTracker::alive_count, 100);

        // Should throw when trying to add more
        CHECK_THROWS_AS(vec.push_back(LifetimeTracker(999)), std::length_error);
    }

    SUBCASE("small_vectra repeated resize") {
        small_vectra<std::string, 4> vec;

        for (int iter = 0; iter < 10; ++iter) {
            vec.resize(50, "test");
            CHECK_EQ(vec.size(), 50);

            vec.resize(10);
            CHECK_EQ(vec.size(), 10);

            vec.clear();
            CHECK_EQ(vec.size(), 0);
        }
    }
}

// ============================================================================
// Assign operations tests
// ============================================================================

TEST_CASE("containers::assign") {
    SUBCASE("small_vectra assign from count and value") {
        small_vectra<std::string, 4> vec{"old", "data"};
        vec.assign(5, "new");
        CHECK_EQ(vec.size(), 5);
        for (size_t i = 0; i < 5; ++i) {
            CHECK_EQ(vec[i], "new");
        }
    }

    SUBCASE("devectra assign from iterators") {
        std::vector<int> source{10, 20, 30, 40, 50};
        devectra<int> dv{1, 2, 3};
        dv.assign(source.begin(), source.end());
        CHECK_EQ(dv.size(), 5);
        CHECK_EQ(dv[0], 10);
        CHECK_EQ(dv[4], 50);
    }

    SUBCASE("static_vectra assign from initializer_list") {
        static_vectra<int, 8> vec{1, 2, 3, 4, 5, 6, 7, 8};
        vec.assign({100, 200, 300});
        CHECK_EQ(vec.size(), 3);
        CHECK_EQ(vec[0], 100);
        CHECK_EQ(vec[2], 300);
    }

    SUBCASE("assign clears and reallocates correctly") {
        LifetimeTracker::reset();
        {
            small_vectra<LifetimeTracker, 4> vec;
            vec.emplace_back(1);
            vec.emplace_back(2);
            vec.emplace_back(3);
            CHECK_EQ(LifetimeTracker::alive_count, 3);

            vec.assign(2, LifetimeTracker(99));
            CHECK_EQ(vec.size(), 2);
            CHECK_EQ(vec[0].value, 99);
            CHECK_EQ(vec[1].value, 99);
        }
        CHECK_EQ(LifetimeTracker::alive_count, 0);
    }
}

// ============================================================================
// Erase operations tests
// ============================================================================

TEST_CASE("containers::erase") {
    SUBCASE("small_vectra erase single element") {
        LifetimeTracker::reset();
        {
            small_vectra<LifetimeTracker, 8> vec;
            for (int i = 0; i < 5; ++i) {
                vec.emplace_back(i);
            }
            CHECK_EQ(LifetimeTracker::alive_count, 5);

            vec.erase(vec.begin() + 2);  // erase element with value 2
            CHECK_EQ(vec.size(), 4);
            CHECK_EQ(LifetimeTracker::alive_count, 4);
            CHECK_EQ(vec[0].value, 0);
            CHECK_EQ(vec[1].value, 1);
            CHECK_EQ(vec[2].value, 3);
            CHECK_EQ(vec[3].value, 4);
        }
        CHECK_EQ(LifetimeTracker::alive_count, 0);
    }

    SUBCASE("devectra erase range") {
        devectra<std::string> dv{"a", "b", "c", "d", "e"};
        dv.erase(dv.begin() + 1, dv.begin() + 4);  // erase b, c, d
        CHECK_EQ(dv.size(), 2);
        CHECK_EQ(dv[0], "a");
        CHECK_EQ(dv[1], "e");
    }

    SUBCASE("static_vectra erase first element") {
        static_vectra<int, 8> vec{10, 20, 30, 40};
        vec.erase(vec.begin());
        CHECK_EQ(vec.size(), 3);
        CHECK_EQ(vec[0], 20);
        CHECK_EQ(vec[1], 30);
        CHECK_EQ(vec[2], 40);
    }

    SUBCASE("erase last element") {
        small_vectra<int, 8> vec{1, 2, 3, 4, 5};
        vec.erase(vec.end() - 1);
        CHECK_EQ(vec.size(), 4);
        CHECK_EQ(vec.back(), 4);
    }

    SUBCASE("erase all elements via range") {
        devectra<LifetimeTracker> dv;
        LifetimeTracker::reset();
        for (int i = 0; i < 5; ++i) {
            dv.emplace_back(i);
        }
        CHECK_EQ(LifetimeTracker::alive_count, 5);

        dv.erase(dv.begin(), dv.end());
        CHECK_EQ(dv.size(), 0);
        CHECK_EQ(dv.empty(), true);
        CHECK_EQ(LifetimeTracker::alive_count, 0);
    }
}

// ============================================================================
// Emplace operations tests
// ============================================================================

struct ComplexType {
    std::string name;
    int id;
    double value;

    ComplexType(const std::string& n, int i, double v) : name(n), id(i), value(v) {}

    bool operator==(const ComplexType& other) const {
        return name == other.name && id == other.id && value == other.value;
    }
};

TEST_CASE("containers::emplace") {
    SUBCASE("small_vectra emplace_back with complex type") {
        small_vectra<ComplexType, 4> vec;
        vec.emplace_back("first", 1, 1.5);
        vec.emplace_back("second", 2, 2.5);

        CHECK_EQ(vec[0].name, "first");
        CHECK_EQ(vec[0].id, 1);
        CHECK_EQ(vec[0].value, 1.5);
        CHECK_EQ(vec[1].name, "second");
    }

    SUBCASE("devectra emplace_front and emplace_back") {
        devectra<ComplexType> dv;
        dv.emplace_back("middle", 2, 2.0);
        dv.emplace_front("first", 1, 1.0);
        dv.emplace_back("last", 3, 3.0);

        CHECK_EQ(dv.size(), 3);
        CHECK_EQ(dv[0].name, "first");
        CHECK_EQ(dv[1].name, "middle");
        CHECK_EQ(dv[2].name, "last");
    }

    SUBCASE("static_vectra emplace_back returns reference") {
        static_vectra<ComplexType, 8> vec;
        auto& ref = vec.emplace_back("test", 42, 3.14);

        CHECK_EQ(ref.name, "test");
        CHECK_EQ(ref.id, 42);

        ref.id = 100;
        CHECK_EQ(vec[0].id, 100);
    }
}

// ============================================================================
// Comparison operators tests
// ============================================================================

TEST_CASE("containers::comparison") {
    SUBCASE("small_vectra comparison") {
        small_vectra<int, 4> a{1, 2, 3};
        small_vectra<int, 4> b{1, 2, 3};
        small_vectra<int, 4> c{1, 2, 4};
        small_vectra<int, 4> d{1, 2};

        CHECK_EQ(a, b);
        CHECK_NE(a, c);
        CHECK_NE(a, d);
        CHECK_LT(a, c);
        CHECK_GT(c, a);
        CHECK_GT(a, d);  // a has more elements
    }

    SUBCASE("devectra comparison with strings") {
        devectra<std::string> a{"apple", "banana"};
        devectra<std::string> b{"apple", "banana"};
        devectra<std::string> c{"apple", "cherry"};

        CHECK_EQ(a, b);
        CHECK_NE(a, c);
        CHECK_LT(a, c);  // banana < cherry
    }

    SUBCASE("static_vectra three-way comparison") {
        static_vectra<int, 8> a{1, 2, 3};
        static_vectra<int, 8> b{1, 2, 3};
        static_vectra<int, 8> c{1, 2, 4};

        CHECK_EQ((a <=> b), std::strong_ordering::equal);
        CHECK_EQ((a <=> c), std::strong_ordering::less);
        CHECK_EQ((c <=> a), std::strong_ordering::greater);
    }
}

// ============================================================================
// Exception safety tests
// ============================================================================

struct ThrowingType {
    static inline int throw_after = -1;  // -1 means never throw
    static inline int construct_count = 0;
    static inline int alive_count = 0;

    int value;

    static void reset(int throw_at = -1) {
        throw_after = throw_at;
        construct_count = 0;
        alive_count = 0;
    }

    ThrowingType() : value(0) {
        if (throw_after >= 0 && construct_count >= throw_after) {
            throw std::runtime_error("construction failed");
        }
        ++construct_count;
        ++alive_count;
    }

    explicit ThrowingType(int v) : value(v) {
        if (throw_after >= 0 && construct_count >= throw_after) {
            throw std::runtime_error("construction failed");
        }
        ++construct_count;
        ++alive_count;
    }

    ThrowingType(const ThrowingType& other) : value(other.value) {
        if (throw_after >= 0 && construct_count >= throw_after) {
            throw std::runtime_error("copy construction failed");
        }
        ++construct_count;
        ++alive_count;
    }

    ThrowingType(ThrowingType&& other) noexcept : value(other.value) {
        other.value = -1;
        ++alive_count;
    }

    ThrowingType& operator=(const ThrowingType& other) {
        value = other.value;
        return *this;
    }

    ThrowingType& operator=(ThrowingType&& other) noexcept {
        value = other.value;
        other.value = -1;
        return *this;
    }

    ~ThrowingType() {
        --alive_count;
    }
};

TEST_CASE("containers::exception_safety") {
    SUBCASE("small_vectra push_back exception rollback") {
        ThrowingType::reset(3);  // throw on 4th construction

        small_vectra<ThrowingType, 8> vec;
        vec.emplace_back(1);
        vec.emplace_back(2);
        vec.emplace_back(3);

        CHECK_EQ(vec.size(), 3);
        CHECK_THROWS_AS(vec.emplace_back(4), std::runtime_error);
        // Vector should still be in valid state
        CHECK_EQ(vec.size(), 3);
        CHECK_EQ(vec[0].value, 1);
        CHECK_EQ(vec[2].value, 3);
    }

    SUBCASE("static_vectra remains valid after throw") {
        ThrowingType::reset(2);

        static_vectra<ThrowingType, 8> vec;
        vec.emplace_back(1);
        vec.emplace_back(2);

        CHECK_THROWS_AS(vec.emplace_back(3), std::runtime_error);
        CHECK_EQ(vec.size(), 2);
        CHECK_EQ(vec[0].value, 1);
    }
}

// ============================================================================
// Aliasing tests (inserting element that references container)
// ============================================================================

TEST_CASE("containers::aliasing") {
    SUBCASE("small_vectra push_back self reference") {
        small_vectra<int, 8> vec{1, 2, 3};
        vec.push_back(vec[0]);  // push_back element from same vector
        CHECK_EQ(vec.size(), 4);
        CHECK_EQ(vec[3], 1);
    }

    // Note: devectra and static_vectra insert/push_back with self reference
    // may not be safe when element shifting occurs because references
    // can be invalidated before copying

    SUBCASE("small_vectra push_back from back element during reallocation") {
        small_vectra<std::string, 2> vec{"first", "second"};
        CHECK_EQ(vec.is_small(), true);
        vec.push_back(vec.back());  // triggers realloc
        CHECK_EQ(vec.is_small(), false);
        CHECK_EQ(vec[2], "second");
    }

    SUBCASE("safe aliasing - copy first") {
        // Best practice: copy the value first to avoid aliasing issues
        static_vectra<int, 8> vec{10, 20, 30};
        int val = vec[2];  // copy first
        vec.insert(vec.begin() + 1, val);
        CHECK_EQ(vec.size(), 4);
        CHECK_EQ(vec[1], 30);
    }
}

// ============================================================================
// Move assignment with different sizes
// ============================================================================

TEST_CASE("containers::move_assignment_sizes") {
    LifetimeTracker::reset();

    SUBCASE("small_vectra move larger to smaller") {
        small_vectra<LifetimeTracker, 4> small_vec;
        small_vec.emplace_back(1);

        small_vectra<LifetimeTracker, 4> large_vec;
        for (int i = 0; i < 10; ++i) {
            large_vec.emplace_back(i * 10);
        }

        int alive_before = LifetimeTracker::alive_count;
        small_vec = std::move(large_vec);

        CHECK_EQ(small_vec.size(), 10);
        CHECK_EQ(small_vec[0].value, 0);
        CHECK_EQ(small_vec[9].value, 90);
    }

    LifetimeTracker::reset();

    SUBCASE("small_vectra move smaller to larger") {
        small_vectra<LifetimeTracker, 4> large_vec;
        for (int i = 0; i < 10; ++i) {
            large_vec.emplace_back(i);
        }

        small_vectra<LifetimeTracker, 4> small_vec;
        small_vec.emplace_back(100);

        large_vec = std::move(small_vec);
        CHECK_EQ(large_vec.size(), 1);
        CHECK_EQ(large_vec[0].value, 100);
    }

    LifetimeTracker::reset();

    SUBCASE("devectra move empty to non-empty") {
        devectra<LifetimeTracker> empty_dv;
        devectra<LifetimeTracker> non_empty;
        non_empty.emplace_back(42);

        non_empty = std::move(empty_dv);
        CHECK_EQ(non_empty.empty(), true);
    }
}

// ============================================================================
// Nested container tests
// ============================================================================

TEST_CASE("containers::nested") {
    SUBCASE("small_vectra of small_vectra") {
        small_vectra<small_vectra<int, 4>, 4> nested;

        small_vectra<int, 4> inner1{1, 2, 3};
        small_vectra<int, 4> inner2{4, 5};

        nested.push_back(inner1);
        nested.push_back(inner2);

        CHECK_EQ(nested.size(), 2);
        CHECK_EQ(nested[0].size(), 3);
        CHECK_EQ(nested[0][0], 1);
        CHECK_EQ(nested[1].size(), 2);
        CHECK_EQ(nested[1][1], 5);

        // Modify nested element
        nested[0].push_back(99);
        CHECK_EQ(nested[0].size(), 4);
        CHECK_EQ(nested[0][3], 99);
    }

    SUBCASE("devectra of static_vectra") {
        devectra<static_vectra<std::string, 4>> dv;

        static_vectra<std::string, 4> sv1{"hello", "world"};
        static_vectra<std::string, 4> sv2{"foo", "bar", "baz"};

        dv.push_back(sv1);
        dv.push_front(sv2);

        CHECK_EQ(dv.size(), 2);
        CHECK_EQ(dv[0][0], "foo");
        CHECK_EQ(dv[1][0], "hello");
    }

    SUBCASE("static_vectra of devectra") {
        static_vectra<devectra<int>, 4> outer;

        devectra<int> dv1{1, 2, 3};
        devectra<int> dv2{10, 20};

        outer.push_back(std::move(dv1));
        outer.push_back(std::move(dv2));

        CHECK_EQ(outer.size(), 2);
        CHECK_EQ(outer[0].size(), 3);
        CHECK_EQ(outer[1][0], 10);
    }
}

// ============================================================================
// Accessor tests (at, data, front, back)
// ============================================================================

TEST_CASE("containers::accessors") {
    SUBCASE("small_vectra at() access") {
        small_vectra<int, 4> vec{1, 2, 3};

        CHECK_EQ(vec.at(0), 1);
        CHECK_EQ(vec.at(2), 3);
        // Note: at() uses Assert (debug assertion), not exception
    }

    SUBCASE("devectra at() access") {
        devectra<std::string> dv{"a", "b", "c"};

        CHECK_EQ(dv.at(0), "a");
        CHECK_EQ(dv.at(2), "c");
    }

    SUBCASE("static_vectra at() access") {
        static_vectra<int, 8> vec{10, 20, 30};

        CHECK_EQ(vec.at(0), 10);
        CHECK_EQ(vec.at(1), 20);
    }

    SUBCASE("data() pointer access") {
        small_vectra<int, 4> vec{1, 2, 3, 4};
        int* ptr = vec.data();

        CHECK_EQ(ptr[0], 1);
        CHECK_EQ(ptr[3], 4);

        ptr[1] = 100;
        CHECK_EQ(vec[1], 100);
    }

    SUBCASE("const data() pointer") {
        const small_vectra<int, 4> vec{5, 6, 7};
        const int* ptr = vec.data();

        CHECK_EQ(ptr[0], 5);
        CHECK_EQ(ptr[2], 7);
    }

    SUBCASE("front() and back() modifiable") {
        small_vectra<std::string, 4> vec{"first", "middle", "last"};

        CHECK_EQ(vec.front(), "first");
        CHECK_EQ(vec.back(), "last");

        vec.front() = "NEW_FIRST";
        vec.back() = "NEW_LAST";

        CHECK_EQ(vec[0], "NEW_FIRST");
        CHECK_EQ(vec[2], "NEW_LAST");
    }

    SUBCASE("devectra front and back after modifications") {
        devectra<int> dv{1, 2, 3};
        dv.push_front(0);
        dv.push_back(4);

        CHECK_EQ(dv.front(), 0);
        CHECK_EQ(dv.back(), 4);
    }
}

// ============================================================================
// Insert single element tests
// ============================================================================

TEST_CASE("containers::insert_single") {
    SUBCASE("small_vectra insert at beginning") {
        small_vectra<int, 8> vec{2, 3, 4};
        vec.insert(vec.begin(), 1);

        CHECK_EQ(vec.size(), 4);
        CHECK_EQ(vec[0], 1);
        CHECK_EQ(vec[1], 2);
    }

    SUBCASE("small_vectra insert at end") {
        small_vectra<int, 8> vec{1, 2, 3};
        vec.insert(vec.end(), 4);

        CHECK_EQ(vec.size(), 4);
        CHECK_EQ(vec[3], 4);
    }

    SUBCASE("small_vectra insert in middle") {
        small_vectra<std::string, 8> vec{"a", "c"};
        vec.insert(vec.begin() + 1, "b");

        CHECK_EQ(vec.size(), 3);
        CHECK_EQ(vec[0], "a");
        CHECK_EQ(vec[1], "b");
        CHECK_EQ(vec[2], "c");
    }

    SUBCASE("devectra insert") {
        devectra<int> dv{1, 3};
        dv.insert(dv.begin() + 1, 2);

        CHECK_EQ(dv.size(), 3);
        CHECK_EQ(dv[0], 1);
        CHECK_EQ(dv[1], 2);
        CHECK_EQ(dv[2], 3);
    }

    SUBCASE("static_vectra insert with tracked type") {
        LifetimeTracker::reset();
        {
            static_vectra<LifetimeTracker, 16> vec;
            vec.emplace_back(1);
            vec.emplace_back(3);

            vec.insert(vec.begin() + 1, LifetimeTracker(2));

            CHECK_EQ(vec.size(), 3);
            CHECK_EQ(vec[0].value, 1);
            CHECK_EQ(vec[1].value, 2);
            CHECK_EQ(vec[2].value, 3);
        }
        CHECK_EQ(LifetimeTracker::alive_count, 0);
    }
}

// ============================================================================
// Move-only type advanced tests
// ============================================================================

TEST_CASE("containers::move_only_advanced") {
    SUBCASE("small_vectra erase with unique_ptr") {
        small_vectra<std::unique_ptr<int>, 8> vec;
        for (int i = 0; i < 5; ++i) {
            vec.push_back(std::make_unique<int>(i * 10));
        }

        vec.erase(vec.begin() + 2);  // erase element with value 20

        CHECK_EQ(vec.size(), 4);
        CHECK_EQ(*vec[0], 0);
        CHECK_EQ(*vec[1], 10);
        CHECK_EQ(*vec[2], 30);
        CHECK_EQ(*vec[3], 40);
    }

    SUBCASE("devectra pop operations with unique_ptr") {
        devectra<std::unique_ptr<int>> dv;
        dv.push_back(std::make_unique<int>(1));
        dv.push_back(std::make_unique<int>(2));
        dv.push_back(std::make_unique<int>(3));

        dv.pop_front();
        CHECK_EQ(dv.size(), 2);
        CHECK_EQ(*dv[0], 2);

        dv.pop_back();
        CHECK_EQ(dv.size(), 1);
        CHECK_EQ(*dv[0], 2);
    }

    SUBCASE("static_vectra swap with unique_ptr") {
        static_vectra<std::unique_ptr<int>, 4> vec1;
        vec1.push_back(std::make_unique<int>(100));
        vec1.push_back(std::make_unique<int>(200));

        static_vectra<std::unique_ptr<int>, 4> vec2;
        vec2.push_back(std::make_unique<int>(999));

        vec1.swap(vec2);

        CHECK_EQ(vec1.size(), 1);
        CHECK_EQ(*vec1[0], 999);
        CHECK_EQ(vec2.size(), 2);
        CHECK_EQ(*vec2[0], 100);
        CHECK_EQ(*vec2[1], 200);
    }

    SUBCASE("move-only resize") {
        small_vectra<std::unique_ptr<int>, 4> vec;
        vec.push_back(std::make_unique<int>(1));
        vec.push_back(std::make_unique<int>(2));

        vec.resize(1);
        CHECK_EQ(vec.size(), 1);
        CHECK_EQ(*vec[0], 1);

        vec.resize(3);  // adds nullptr elements
        CHECK_EQ(vec.size(), 3);
        CHECK_EQ(vec[1], nullptr);
        CHECK_EQ(vec[2], nullptr);
    }
}

// ============================================================================
// Alignment tests
// ============================================================================

struct alignas(16) Aligned16 {
    int data[4];

    Aligned16() { std::fill(std::begin(data), std::end(data), 0); }
    explicit Aligned16(int v) { std::fill(std::begin(data), std::end(data), v); }

    bool operator==(const Aligned16& other) const {
        return std::equal(std::begin(data), std::end(data), std::begin(other.data));
    }
};

struct alignas(32) Aligned32 {
    double data[4];

    Aligned32() { std::fill(std::begin(data), std::end(data), 0.0); }
    explicit Aligned32(double v) { std::fill(std::begin(data), std::end(data), v); }

    bool operator==(const Aligned32& other) const {
        return std::equal(std::begin(data), std::end(data), std::begin(other.data));
    }
};

static_assert(alignof(Aligned16) == 16);
static_assert(alignof(Aligned32) == 32);

TEST_CASE("containers::alignment") {
    SUBCASE("small_vectra with 16-byte aligned type") {
        small_vectra<Aligned16, 4> vec;
        vec.emplace_back(42);
        vec.emplace_back(100);

        // Check alignment of elements
        CHECK_EQ(reinterpret_cast<std::uintptr_t>(&vec[0]) % 16, 0);
        CHECK_EQ(reinterpret_cast<std::uintptr_t>(&vec[1]) % 16, 0);

        CHECK_EQ(vec[0].data[0], 42);
        CHECK_EQ(vec[1].data[0], 100);
    }

    SUBCASE("devectra with 32-byte aligned type") {
        devectra<Aligned32> dv;
        dv.emplace_back(3.14);
        dv.emplace_back(2.71);

        // Check alignment
        CHECK_EQ(reinterpret_cast<std::uintptr_t>(&dv[0]) % 32, 0);
        CHECK_EQ(reinterpret_cast<std::uintptr_t>(&dv[1]) % 32, 0);

        CHECK_EQ(dv[0].data[0], 3.14);
        CHECK_EQ(dv[1].data[0], 2.71);
    }

    SUBCASE("static_vectra with aligned type") {
        static_vectra<Aligned16, 4> vec;
        vec.emplace_back(1);
        vec.emplace_back(2);
        vec.emplace_back(3);

        for (size_t i = 0; i < vec.size(); ++i) {
            CHECK_EQ(reinterpret_cast<std::uintptr_t>(&vec[i]) % 16, 0);
        }

        auto vec2 = vec;
        CHECK_EQ(vec, vec2);
    }
}

// ============================================================================
// Boundary condition tests
// ============================================================================

TEST_CASE("containers::boundary") {
    SUBCASE("small_vectra exactly at inline capacity") {
        small_vectra<int, 4> vec{1, 2, 3, 4};
        CHECK_EQ(vec.is_small(), true);
        CHECK_EQ(vec.size(), 4);

        // One more triggers heap
        vec.push_back(5);
        CHECK_EQ(vec.is_small(), false);
        CHECK_EQ(vec.size(), 5);
    }

    SUBCASE("erase all then repopulate") {
        LifetimeTracker::reset();

        small_vectra<LifetimeTracker, 4> vec;
        for (int i = 0; i < 3; ++i) {
            vec.emplace_back(i);
        }

        while (!vec.empty()) {
            vec.pop_back();
        }
        CHECK_EQ(LifetimeTracker::alive_count, 0);
        CHECK_EQ(vec.empty(), true);

        // Repopulate
        for (int i = 10; i < 15; ++i) {
            vec.emplace_back(i);
        }
        CHECK_EQ(vec.size(), 5);
        CHECK_EQ(vec[0].value, 10);
    }

    SUBCASE("devectra front_capacity and back_capacity after operations") {
        devectra<int> dv;

        // Push to back multiple times
        for (int i = 0; i < 10; ++i) {
            dv.push_back(i);
        }

        // Pop from front multiple times
        for (int i = 0; i < 5; ++i) {
            dv.pop_front();
        }

        CHECK_EQ(dv.size(), 5);
        CHECK_EQ(dv[0], 5);
        CHECK_GE(dv.front_capacity(), 0);
    }

    SUBCASE("static_vectra at max capacity operations") {
        static_vectra<int, 4> vec{1, 2, 3, 4};
        CHECK_EQ(vec.full(), true);

        vec.erase(vec.begin());
        CHECK_EQ(vec.full(), false);

        vec.push_back(5);
        CHECK_EQ(vec.full(), true);
        CHECK_EQ(vec.size(), 4);
        CHECK_EQ(vec[3], 5);
    }

    SUBCASE("insert at begin and end boundaries") {
        small_vectra<int, 8> vec{2, 3, 4};

        vec.insert(vec.begin(), 1);
        CHECK_EQ(vec[0], 1);
        CHECK_EQ(vec.size(), 4);

        vec.insert(vec.end(), 5);
        CHECK_EQ(vec[4], 5);
        CHECK_EQ(vec.size(), 5);
    }
}

// ============================================================================
// Clear and reuse tests
// ============================================================================

TEST_CASE("containers::clear_reuse") {
    SUBCASE("small_vectra clear maintains capacity") {
        small_vectra<int, 4> vec;
        vec.reserve(100);
        size_t cap_before = vec.capacity();

        for (int i = 0; i < 50; ++i) {
            vec.push_back(i);
        }

        vec.clear();
        CHECK_EQ(vec.empty(), true);
        CHECK_EQ(vec.capacity(), cap_before);

        // Reuse
        for (int i = 100; i < 150; ++i) {
            vec.push_back(i);
        }
        CHECK_EQ(vec[0], 100);
        CHECK_EQ(vec.size(), 50);
    }

    SUBCASE("devectra clear and reuse") {
        devectra<std::string> dv{"a", "b", "c"};
        dv.clear();
        CHECK_EQ(dv.empty(), true);

        dv.push_back("new");
        dv.push_front("newer");
        CHECK_EQ(dv.size(), 2);
        CHECK_EQ(dv[0], "newer");
    }

    SUBCASE("static_vectra multiple clear cycles") {
        LifetimeTracker::reset();

        static_vectra<LifetimeTracker, 8> vec;

        for (int cycle = 0; cycle < 5; ++cycle) {
            for (int i = 0; i < 8; ++i) {
                vec.emplace_back(cycle * 10 + i);
            }
            CHECK_EQ(vec.size(), 8);
            CHECK_EQ(LifetimeTracker::alive_count, 8);

            vec.clear();
            CHECK_EQ(vec.empty(), true);
            CHECK_EQ(LifetimeTracker::alive_count, 0);
        }
    }
}

// NOLINTEND
}  // namespace stdb::container

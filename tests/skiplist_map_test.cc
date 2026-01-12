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

#include "container/skiplist_map.hpp"
#include "container/skiplist_set.hpp"

#include <atomic>
#include <map>
#include <random>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "doctest/doctest/doctest.h"

// Counting allocator for testing allocator support
namespace test_skiplist_alloc {
inline std::atomic<int> alloc_count{0};
inline std::atomic<int> dealloc_count{0};

template <typename T>
struct CountingAllocator {
    using value_type = T;

    CountingAllocator() = default;
    template <typename U>
    CountingAllocator(const CountingAllocator<U>&) noexcept {}

    T* allocate(std::size_t n) {
        ++alloc_count;
        return static_cast<T*>(::operator new(n * sizeof(T)));
    }

    void deallocate(T* p, std::size_t) noexcept {
        ++dealloc_count;
        ::operator delete(p);
    }

    template <typename U>
    bool operator==(const CountingAllocator<U>&) const noexcept { return true; }
};
}  // namespace test_skiplist_alloc

namespace stdb::container {

// ============================================================================
// skiplist_map basic tests
// ============================================================================

TEST_CASE("skiplist_map::basic") {
    SUBCASE("default constructor") {
        skiplist_map<int, int> map;
        CHECK(map.empty());
        CHECK_EQ(map.size(), 0);
    }

    SUBCASE("initializer list") {
        skiplist_map<int, std::string> map{{1, "one"}, {2, "two"}, {3, "three"}};
        CHECK_EQ(map.size(), 3);
        CHECK_EQ(map.at(1), "one");
        CHECK_EQ(map.at(2), "two");
        CHECK_EQ(map.at(3), "three");
    }

    SUBCASE("range constructor from vector") {
        std::vector<std::pair<int, std::string>> vec{{1, "one"}, {2, "two"}, {3, "three"}};
        skiplist_map<int, std::string> map(vec.begin(), vec.end());
        CHECK_EQ(map.size(), 3);
        CHECK_EQ(map.at(1), "one");
        CHECK_EQ(map.at(2), "two");
        CHECK_EQ(map.at(3), "three");
    }

    SUBCASE("range constructor from std::map") {
        std::map<int, int> stdmap{{1, 10}, {2, 20}, {3, 30}};
        skiplist_map<int, int> map(stdmap.begin(), stdmap.end());
        CHECK_EQ(map.size(), 3);
        CHECK_EQ(map.at(1), 10);
        CHECK_EQ(map.at(2), 20);
        CHECK_EQ(map.at(3), 30);
    }

    SUBCASE("range constructor with custom comparator") {
        std::vector<std::pair<int, int>> vec{{1, 10}, {2, 20}, {3, 30}};
        skiplist_map<int, int, std::greater<int>> map(vec.begin(), vec.end(), std::greater<int>{});
        CHECK_EQ(map.size(), 3);
        // Check reverse order iteration
        auto it = map.begin();
        CHECK_EQ(it->first, 3);
        ++it;
        CHECK_EQ(it->first, 2);
        ++it;
        CHECK_EQ(it->first, 1);
    }

    SUBCASE("copy constructor") {
        skiplist_map<int, int> map1{{1, 10}, {2, 20}, {3, 30}};
        skiplist_map<int, int> map2(map1);
        CHECK_EQ(map2.size(), 3);
        CHECK_EQ(map2.at(1), 10);
        CHECK_EQ(map2.at(2), 20);
        CHECK_EQ(map2.at(3), 30);

        // Verify independence
        map1[1] = 100;
        CHECK_EQ(map2.at(1), 10);
    }

    SUBCASE("move constructor") {
        skiplist_map<int, int> map1{{1, 10}, {2, 20}};
        skiplist_map<int, int> map2(std::move(map1));
        CHECK_EQ(map2.size(), 2);
        CHECK(map1.empty());  // NOLINT: testing moved-from state
    }

    SUBCASE("copy assignment") {
        skiplist_map<int, int> map1{{1, 10}, {2, 20}};
        skiplist_map<int, int> map2;
        map2 = map1;
        CHECK_EQ(map2.size(), 2);
        CHECK_EQ(map2.at(1), 10);

        // Verify independence
        map1[1] = 100;
        CHECK_EQ(map2.at(1), 10);
    }

    SUBCASE("move assignment") {
        skiplist_map<int, int> map1{{1, 10}, {2, 20}};
        skiplist_map<int, int> map2{{3, 30}};
        map2 = std::move(map1);
        CHECK_EQ(map2.size(), 2);
        CHECK(map1.empty());  // NOLINT: testing moved-from state
    }

    SUBCASE("initializer list assignment") {
        skiplist_map<int, int> map{{1, 10}};
        map = {{2, 20}, {3, 30}};
        CHECK_EQ(map.size(), 2);
        CHECK_FALSE(map.contains(1));
        CHECK(map.contains(2));
        CHECK(map.contains(3));
    }

    SUBCASE("self assignment") {
        skiplist_map<int, int> map{{1, 10}, {2, 20}};
        map = map;
        CHECK_EQ(map.size(), 2);
        CHECK_EQ(map.at(1), 10);
    }
}

// ============================================================================
// skiplist_map insert tests
// ============================================================================

TEST_CASE("skiplist_map::insert") {
    SUBCASE("insert single element") {
        skiplist_map<int, int> map;
        auto [it, inserted] = map.insert(1, 100);
        CHECK(inserted);
        CHECK_EQ(it->first, 1);
        CHECK_EQ(it->second, 100);
        CHECK_EQ(map.size(), 1);
    }

    SUBCASE("insert duplicate key") {
        skiplist_map<int, int> map;
        map.insert(1, 100);
        auto [it, inserted] = map.insert(1, 200);
        CHECK_FALSE(inserted);
        CHECK_EQ(it->second, 100);  // Original value preserved
        CHECK_EQ(map.size(), 1);
    }

    SUBCASE("insert multiple elements") {
        skiplist_map<int, int> map;
        for (int i = 0; i < 100; ++i) {
            auto [it, inserted] = map.insert(i, i * 10);
            CHECK(inserted);
        }
        CHECK_EQ(map.size(), 100);

        // Verify all elements
        for (int i = 0; i < 100; ++i) {
            CHECK_EQ(map.at(i), i * 10);
        }
    }

    SUBCASE("insert in reverse order") {
        skiplist_map<int, int> map;
        for (int i = 99; i >= 0; --i) {
            map.insert(i, i * 10);
        }
        CHECK_EQ(map.size(), 100);

        // Verify ordering
        int expected = 0;
        for (const auto& [k, v] : map) {
            CHECK_EQ(k, expected);
            ++expected;
        }
    }

    SUBCASE("insert random order") {
        skiplist_map<int, int> map;
        std::vector<int> keys;
        for (int i = 0; i < 1000; ++i) {
            keys.push_back(i);
        }

        std::mt19937 rng(42);
        std::shuffle(keys.begin(), keys.end(), rng);

        for (int key : keys) {
            map.insert(key, key * 10);
        }

        CHECK_EQ(map.size(), 1000);

        // Verify ordering
        int expected = 0;
        for (const auto& [k, v] : map) {
            CHECK_EQ(k, expected);
            CHECK_EQ(v, expected * 10);
            ++expected;
        }
    }

    SUBCASE("insert with value_type") {
        skiplist_map<int, int> map;
        std::pair<const int, int> kv{42, 420};
        auto [it, inserted] = map.insert(kv);
        CHECK(inserted);
        CHECK_EQ(it->first, 42);
        CHECK_EQ(it->second, 420);
    }

    SUBCASE("insert with rvalue") {
        skiplist_map<int, std::string> map;
        auto [it, inserted] = map.insert(std::make_pair(1, std::string("hello")));
        CHECK(inserted);
        CHECK_EQ(it->second, "hello");
    }

    SUBCASE("insert range") {
        skiplist_map<int, int> map;
        std::vector<std::pair<int, int>> vec{{1, 10}, {2, 20}, {3, 30}};
        map.insert(vec.begin(), vec.end());
        CHECK_EQ(map.size(), 3);
    }

    SUBCASE("insert initializer list") {
        skiplist_map<int, int> map;
        map.insert({{1, 10}, {2, 20}, {3, 30}});
        CHECK_EQ(map.size(), 3);
    }

    SUBCASE("insert with hint (hint ignored)") {
        skiplist_map<int, int> map{{1, 10}, {3, 30}};
        auto hint = map.find(1);
        auto it = map.insert(hint, std::pair<const int, int>{2, 20});
        CHECK_EQ(it->first, 2);
        CHECK_EQ(it->second, 20);
        CHECK_EQ(map.size(), 3);
    }

    SUBCASE("insert initializer_list with duplicates") {
        skiplist_map<int, std::string> map;
        map.insert({{1, "one"}, {2, "two"}, {3, "three"}});
        CHECK_EQ(map.size(), 3);

        // Insert with duplicates - should ignore duplicates
        map.insert({{2, "TWO"}, {4, "four"}, {5, "five"}});
        CHECK_EQ(map.size(), 5);
        CHECK_EQ(map[2], "two");  // Original preserved
        CHECK_EQ(map[4], "four");
        CHECK_EQ(map[5], "five");
    }
}

// ============================================================================
// skiplist_map emplace tests
// ============================================================================

TEST_CASE("skiplist_map::emplace") {
    SUBCASE("emplace basic") {
        skiplist_map<int, std::string> map;
        auto [it, inserted] = map.emplace(1, "hello");
        CHECK(inserted);
        CHECK_EQ(it->first, 1);
        CHECK_EQ(it->second, "hello");
    }

    SUBCASE("emplace duplicate") {
        skiplist_map<int, std::string> map;
        map.emplace(1, "hello");
        auto [it, inserted] = map.emplace(1, "world");
        CHECK_FALSE(inserted);
        CHECK_EQ(it->second, "hello");
    }

    SUBCASE("emplace_hint") {
        skiplist_map<int, std::string> map;
        map.emplace(1, "one");
        auto hint = map.begin();
        auto it = map.emplace_hint(hint, 2, "two");
        CHECK_EQ(it->first, 2);
        CHECK_EQ(it->second, "two");
    }
}

// ============================================================================
// skiplist_map try_emplace tests
// ============================================================================

TEST_CASE("skiplist_map::try_emplace") {
    SUBCASE("try_emplace new key") {
        skiplist_map<int, std::string> map;
        auto [it, inserted] = map.try_emplace(1, "hello");
        CHECK(inserted);
        CHECK_EQ(it->first, 1);
        CHECK_EQ(it->second, "hello");
    }

    SUBCASE("try_emplace existing key") {
        skiplist_map<int, std::string> map;
        map.try_emplace(1, "hello");
        auto [it, inserted] = map.try_emplace(1, "world");
        CHECK_FALSE(inserted);
        CHECK_EQ(it->second, "hello");  // Value not modified
    }

    SUBCASE("try_emplace with rvalue key") {
        skiplist_map<std::string, int> map;
        std::string key = "test";
        auto [it, inserted] = map.try_emplace(std::move(key), 42);
        CHECK(inserted);
        CHECK_EQ(it->second, 42);
    }

    SUBCASE("try_emplace with hint") {
        skiplist_map<int, std::string> map;
        map.try_emplace(1, "one");
        auto hint = map.begin();
        auto it = map.try_emplace(hint, 2, "two");
        CHECK_EQ(it->first, 2);
    }
}

// ============================================================================
// skiplist_map insert_or_assign tests
// ============================================================================

TEST_CASE("skiplist_map::insert_or_assign") {
    SUBCASE("insert_or_assign new key") {
        skiplist_map<int, std::string> map;
        auto [it, inserted] = map.insert_or_assign(1, "hello");
        CHECK(inserted);
        CHECK_EQ(it->second, "hello");
    }

    SUBCASE("insert_or_assign existing key") {
        skiplist_map<int, std::string> map;
        map.insert(1, "hello");
        auto [it, inserted] = map.insert_or_assign(1, "world");
        CHECK_FALSE(inserted);
        CHECK_EQ(it->second, "world");  // Value updated
    }

    SUBCASE("insert_or_assign with rvalue key") {
        skiplist_map<std::string, int> map;
        std::string key = "test";
        auto [it, inserted] = map.insert_or_assign(std::move(key), 42);
        CHECK(inserted);
    }

    SUBCASE("insert_or_assign with hint") {
        skiplist_map<int, std::string> map;
        map.insert(1, "one");
        auto hint = map.begin();
        auto it = map.insert_or_assign(hint, 2, "two");
        CHECK_EQ(it->first, 2);
    }
}

// ============================================================================
// skiplist_map find tests
// ============================================================================

TEST_CASE("skiplist_map::find") {
    skiplist_map<int, std::string> map;
    map.insert(10, "ten");
    map.insert(20, "twenty");
    map.insert(30, "thirty");
    map.insert(5, "five");
    map.insert(15, "fifteen");

    SUBCASE("find existing key") {
        auto it = map.find(20);
        CHECK(it != map.end());
        CHECK_EQ(it->first, 20);
        CHECK_EQ(it->second, "twenty");
    }

    SUBCASE("find non-existing key") {
        auto it = map.find(25);
        CHECK(it == map.end());
    }

    SUBCASE("find first key") {
        auto it = map.find(5);
        CHECK(it != map.end());
        CHECK_EQ(it->second, "five");
    }

    SUBCASE("find last key") {
        auto it = map.find(30);
        CHECK(it != map.end());
        CHECK_EQ(it->second, "thirty");
    }

    SUBCASE("const find") {
        const auto& cmap = map;
        auto it = cmap.find(20);
        CHECK(it != cmap.end());
        CHECK_EQ(it->second, "twenty");
    }
}

// ============================================================================
// skiplist_map contains and count tests
// ============================================================================

TEST_CASE("skiplist_map::contains_count") {
    skiplist_map<int, int> map{{1, 10}, {2, 20}, {3, 30}};

    SUBCASE("contains existing key") {
        CHECK(map.contains(1));
        CHECK(map.contains(2));
        CHECK(map.contains(3));
    }

    SUBCASE("contains non-existing key") {
        CHECK_FALSE(map.contains(0));
        CHECK_FALSE(map.contains(4));
        CHECK_FALSE(map.contains(100));
    }

    SUBCASE("count existing key") {
        CHECK_EQ(map.count(1), 1);
        CHECK_EQ(map.count(2), 1);
        CHECK_EQ(map.count(3), 1);
    }

    SUBCASE("count non-existing key") {
        CHECK_EQ(map.count(0), 0);
        CHECK_EQ(map.count(100), 0);
    }
}

// ============================================================================
// skiplist_map operator[] tests
// ============================================================================

TEST_CASE("skiplist_map::operator[]") {
    skiplist_map<int, int> map;

    SUBCASE("insert via operator[]") {
        map[1] = 100;
        CHECK_EQ(map.size(), 1);
        CHECK_EQ(map[1], 100);
    }

    SUBCASE("modify via operator[]") {
        map[1] = 100;
        map[1] = 200;
        CHECK_EQ(map.size(), 1);
        CHECK_EQ(map[1], 200);
    }

    SUBCASE("access creates default value") {
        int val = map[42];
        CHECK_EQ(val, 0);  // Default int
        CHECK_EQ(map.size(), 1);
        CHECK(map.contains(42));
    }

    SUBCASE("access existing element") {
        map[1] = 10;
        CHECK_EQ(map[1], 10);
        CHECK_EQ(map.size(), 1);  // Size unchanged
    }

    SUBCASE("operator[] with rvalue key") {
        skiplist_map<std::string, int> smap;
        std::string key = "test";
        smap[std::move(key)] = 42;
        CHECK_EQ(smap.at("test"), 42);
    }
}

// ============================================================================
// skiplist_map at tests
// ============================================================================

TEST_CASE("skiplist_map::at") {
    skiplist_map<int, std::string> map{{1, "one"}, {2, "two"}};

    SUBCASE("at existing key") {
        CHECK_EQ(map.at(1), "one");
        CHECK_EQ(map.at(2), "two");
    }

    SUBCASE("at non-existing key throws") {
        CHECK_THROWS_AS(map.at(3), std::out_of_range);
        CHECK_THROWS_AS(map.at(0), std::out_of_range);
    }

    SUBCASE("const at") {
        const auto& cmap = map;
        CHECK_EQ(cmap.at(1), "one");
        CHECK_THROWS_AS(cmap.at(100), std::out_of_range);
    }

    SUBCASE("at allows modification") {
        map.at(1) = "ONE";
        CHECK_EQ(map.at(1), "ONE");
    }
}

// ============================================================================
// skiplist_map erase tests
// ============================================================================

TEST_CASE("skiplist_map::erase") {
    SUBCASE("erase by key - existing") {
        skiplist_map<int, int> map{{1, 10}, {2, 20}, {3, 30}};
        auto count = map.erase(2);
        CHECK_EQ(count, 1);
        CHECK_EQ(map.size(), 2);
        CHECK_FALSE(map.contains(2));
        CHECK(map.contains(1));
        CHECK(map.contains(3));
    }

    SUBCASE("erase by key - non-existing") {
        skiplist_map<int, int> map{{1, 10}};
        auto count = map.erase(42);
        CHECK_EQ(count, 0);
        CHECK_EQ(map.size(), 1);
    }

    SUBCASE("erase by iterator") {
        skiplist_map<int, int> map{{1, 10}, {2, 20}, {3, 30}};
        auto it = map.find(2);
        auto next = map.erase(it);
        CHECK_EQ(map.size(), 2);
        CHECK(next != map.end());
        CHECK_EQ(next->first, 3);
    }

    SUBCASE("erase by const_iterator") {
        skiplist_map<int, int> map{{1, 10}, {2, 20}, {3, 30}};
        auto it = map.cbegin();
        ++it;  // Points to (2, 20)
        auto next = map.erase(it);
        CHECK_EQ(map.size(), 2);
        CHECK_EQ(next->first, 3);
    }

    SUBCASE("erase range") {
        skiplist_map<int, int> map{{1, 10}, {2, 20}, {3, 30}, {4, 40}, {5, 50}};
        auto first = map.find(2);
        auto last = map.find(4);
        map.erase(first, last);
        CHECK_EQ(map.size(), 3);
        CHECK(map.contains(1));
        CHECK_FALSE(map.contains(2));
        CHECK_FALSE(map.contains(3));
        CHECK(map.contains(4));
        CHECK(map.contains(5));
    }

    SUBCASE("erase all elements one by one") {
        skiplist_map<int, int> map;
        for (int i = 0; i < 100; ++i) {
            map.insert(i, i);
        }

        for (int i = 0; i < 100; ++i) {
            CHECK_EQ(map.erase(i), 1);
        }
        CHECK(map.empty());
    }

    SUBCASE("erase in random order") {
        skiplist_map<int, int> map;
        std::vector<int> keys;
        for (int i = 0; i < 100; ++i) {
            map.insert(i, i);
            keys.push_back(i);
        }

        std::mt19937 rng(42);
        std::shuffle(keys.begin(), keys.end(), rng);

        for (int key : keys) {
            CHECK_EQ(map.erase(key), 1);
        }
        CHECK(map.empty());
    }

    SUBCASE("erase preserves order") {
        skiplist_map<int, int> map;
        for (int i = 0; i < 100; ++i) {
            map.insert(i, i * 10);
        }
        map.erase(50);

        int expected = 0;
        for (const auto& [k, v] : map) {
            if (expected == 50) ++expected;  // Skip erased
            CHECK_EQ(k, expected);
            ++expected;
        }
    }

    SUBCASE("erase first element") {
        skiplist_map<int, int> map{{1, 10}, {2, 20}, {3, 30}};
        map.erase(map.begin());
        CHECK_EQ(map.size(), 2);
        CHECK_FALSE(map.contains(1));
        CHECK_EQ(map.begin()->first, 2);
    }

    SUBCASE("erase last element") {
        skiplist_map<int, int> map{{1, 10}, {2, 20}, {3, 30}};
        auto it = map.find(3);
        map.erase(it);
        CHECK_EQ(map.size(), 2);
        CHECK_FALSE(map.contains(3));
    }
}

// ============================================================================
// skiplist_map clear tests
// ============================================================================

TEST_CASE("skiplist_map::clear") {
    skiplist_map<int, int> map{{1, 10}, {2, 20}, {3, 30}};

    SUBCASE("clear non-empty map") {
        map.clear();
        CHECK(map.empty());
        CHECK_EQ(map.size(), 0);
        CHECK(map.begin() == map.end());
    }

    SUBCASE("clear empty map") {
        skiplist_map<int, int> empty_map;
        empty_map.clear();
        CHECK(empty_map.empty());
    }

    SUBCASE("insert after clear") {
        map.clear();
        map.insert(4, 40);
        CHECK_EQ(map.size(), 1);
        CHECK_EQ(map.at(4), 40);
    }

    SUBCASE("multiple clear calls") {
        map.clear();
        map.clear();
        CHECK(map.empty());
    }
}

// ============================================================================
// skiplist_map bounds tests
// ============================================================================

TEST_CASE("skiplist_map::bounds") {
    skiplist_map<int, int> map;
    for (int i = 0; i < 100; i += 10) {
        map.insert(i, i);
    }
    // Map contains: 0, 10, 20, 30, 40, 50, 60, 70, 80, 90

    SUBCASE("lower_bound existing key") {
        auto it = map.lower_bound(30);
        CHECK(it != map.end());
        CHECK_EQ(it->first, 30);
    }

    SUBCASE("lower_bound between keys") {
        auto it = map.lower_bound(35);
        CHECK(it != map.end());
        CHECK_EQ(it->first, 40);
    }

    SUBCASE("lower_bound before first") {
        auto it = map.lower_bound(-5);
        CHECK(it != map.end());
        CHECK_EQ(it->first, 0);
    }

    SUBCASE("lower_bound after last") {
        auto it = map.lower_bound(100);
        CHECK(it == map.end());
    }

    SUBCASE("lower_bound at first") {
        auto it = map.lower_bound(0);
        CHECK(it == map.begin());
    }

    SUBCASE("upper_bound existing key") {
        auto it = map.upper_bound(30);
        CHECK(it != map.end());
        CHECK_EQ(it->first, 40);
    }

    SUBCASE("upper_bound between keys") {
        auto it = map.upper_bound(35);
        CHECK(it != map.end());
        CHECK_EQ(it->first, 40);
    }

    SUBCASE("upper_bound at last key") {
        auto it = map.upper_bound(90);
        CHECK(it == map.end());
    }

    SUBCASE("upper_bound after last") {
        auto it = map.upper_bound(100);
        CHECK(it == map.end());
    }

    SUBCASE("equal_range existing key") {
        auto [first, second] = map.equal_range(30);
        CHECK(first != map.end());
        CHECK_EQ(first->first, 30);
        CHECK(second != map.end());
        CHECK_EQ(second->first, 40);
    }

    SUBCASE("equal_range non-existing key") {
        auto [first, second] = map.equal_range(35);
        CHECK(first == second);
        CHECK(first != map.end());
        CHECK_EQ(first->first, 40);
    }

    SUBCASE("const bounds") {
        const auto& cmap = map;
        auto it = cmap.lower_bound(30);
        CHECK_EQ(it->first, 30);
        auto it2 = cmap.upper_bound(30);
        CHECK_EQ(it2->first, 40);
    }
}

// ============================================================================
// skiplist_map iteration tests
// ============================================================================

TEST_CASE("skiplist_map::iteration") {
    SUBCASE("forward iteration in order") {
        skiplist_map<int, int> map;
        for (int i = 0; i < 100; ++i) {
            map.insert(i, i * 10);
        }

        int expected = 0;
        for (const auto& [k, v] : map) {
            CHECK_EQ(k, expected);
            CHECK_EQ(v, expected * 10);
            ++expected;
        }
        CHECK_EQ(expected, 100);
    }

    SUBCASE("const iteration") {
        skiplist_map<int, int> map{{1, 10}, {2, 20}, {3, 30}};
        const auto& cmap = map;
        int count = 0;
        for (const auto& [k, v] : cmap) {
            CHECK_EQ(v, k * 10);
            ++count;
        }
        CHECK_EQ(count, 3);
    }

    SUBCASE("empty map iteration") {
        skiplist_map<int, int> map;
        CHECK(map.begin() == map.end());
        CHECK(map.cbegin() == map.cend());

        int count = 0;
        for (const auto& kv : map) {
            (void)kv;
            ++count;
        }
        CHECK_EQ(count, 0);
    }

    SUBCASE("single element iteration") {
        skiplist_map<int, int> map{{42, 100}};
        auto it = map.begin();
        CHECK_EQ(it->first, 42);
        CHECK_EQ(it->second, 100);
        ++it;
        CHECK(it == map.end());
    }

    SUBCASE("iterator modification") {
        skiplist_map<int, int> map{{1, 10}, {2, 20}, {3, 30}};
        for (auto& [k, v] : map) {
            v *= 2;
        }
        CHECK_EQ(map.at(1), 20);
        CHECK_EQ(map.at(2), 40);
        CHECK_EQ(map.at(3), 60);
    }

    SUBCASE("iterator post-increment") {
        skiplist_map<int, int> map{{1, 10}, {2, 20}};
        auto it = map.begin();
        auto old = it++;
        CHECK_EQ(old->first, 1);
        CHECK_EQ(it->first, 2);
    }
}

// ============================================================================
// skiplist_map swap tests
// ============================================================================

TEST_CASE("skiplist_map::swap") {
    SUBCASE("swap non-empty maps") {
        skiplist_map<int, int> map1{{1, 10}, {2, 20}};
        skiplist_map<int, int> map2{{3, 30}, {4, 40}, {5, 50}};

        map1.swap(map2);

        CHECK_EQ(map1.size(), 3);
        CHECK_EQ(map2.size(), 2);
        CHECK(map1.contains(3));
        CHECK(map2.contains(1));
    }

    SUBCASE("swap with empty map") {
        skiplist_map<int, int> map1{{1, 10}};
        skiplist_map<int, int> map2;

        map1.swap(map2);

        CHECK(map1.empty());
        CHECK_EQ(map2.size(), 1);
    }

    SUBCASE("swap empty maps") {
        skiplist_map<int, int> map1;
        skiplist_map<int, int> map2;

        map1.swap(map2);

        CHECK(map1.empty());
        CHECK(map2.empty());
    }

    SUBCASE("non-member swap") {
        skiplist_map<int, int> map1{{1, 10}};
        skiplist_map<int, int> map2{{2, 20}};

        swap(map1, map2);

        CHECK(map1.contains(2));
        CHECK(map2.contains(1));
    }
}

// ============================================================================
// skiplist_map comparison tests
// ============================================================================

TEST_CASE("skiplist_map::comparison") {
    SUBCASE("equal maps") {
        skiplist_map<int, int> map1{{1, 10}, {2, 20}, {3, 30}};
        skiplist_map<int, int> map2{{1, 10}, {2, 20}, {3, 30}};
        CHECK(map1 == map2);
        CHECK_FALSE(map1 != map2);
    }

    SUBCASE("different size") {
        skiplist_map<int, int> map1{{1, 10}, {2, 20}};
        skiplist_map<int, int> map2{{1, 10}, {2, 20}, {3, 30}};
        CHECK(map1 != map2);
        CHECK_FALSE(map1 == map2);
    }

    SUBCASE("different keys") {
        skiplist_map<int, int> map1{{1, 10}, {2, 20}};
        skiplist_map<int, int> map2{{1, 10}, {3, 30}};
        CHECK(map1 != map2);
    }

    SUBCASE("different values") {
        skiplist_map<int, int> map1{{1, 10}, {2, 20}};
        skiplist_map<int, int> map2{{1, 10}, {2, 999}};
        CHECK(map1 != map2);
    }

    SUBCASE("empty maps are equal") {
        skiplist_map<int, int> map1;
        skiplist_map<int, int> map2;
        CHECK(map1 == map2);
    }

    SUBCASE("lexicographical ordering") {
        skiplist_map<int, int> map1{{1, 10}, {2, 20}};
        skiplist_map<int, int> map2{{1, 10}, {3, 30}};
        CHECK(map1 < map2);
        CHECK(map1 <= map2);
        CHECK(map2 > map1);
        CHECK(map2 >= map1);
    }

    SUBCASE("ordering by size") {
        skiplist_map<int, int> map1{{1, 10}};
        skiplist_map<int, int> map2{{1, 10}, {2, 20}};
        CHECK(map1 < map2);
    }
}

// ============================================================================
// skiplist_map extract and node_handle tests
// ============================================================================

TEST_CASE("skiplist_map::node_handle") {
    skiplist_map<int, std::string> map;
    map[1] = "one";
    map[2] = "two";
    map[3] = "three";

    SUBCASE("extract by iterator") {
        auto it = map.find(2);
        auto nh = map.extract(it);

        CHECK_FALSE(nh.empty());
        CHECK_EQ(nh.key(), 2);
        CHECK_EQ(nh.mapped(), "two");
        CHECK_EQ(map.size(), 2);
        CHECK_FALSE(map.contains(2));
    }

    SUBCASE("extract by key") {
        auto nh = map.extract(2);

        CHECK_FALSE(nh.empty());
        CHECK_EQ(nh.key(), 2);
        CHECK_EQ(nh.mapped(), "two");
        CHECK_EQ(map.size(), 2);
    }

    SUBCASE("extract non-existent key") {
        auto nh = map.extract(999);
        CHECK(nh.empty());
        CHECK_EQ(map.size(), 3);
    }

    SUBCASE("insert node handle") {
        auto nh = map.extract(2);
        CHECK_EQ(map.size(), 2);

        skiplist_map<int, std::string> map2;
        map2[10] = "ten";

        auto result = map2.insert(std::move(nh));
        CHECK(result.inserted);
        CHECK_EQ(result.position->first, 2);
        CHECK_EQ(result.position->second, "two");
        CHECK(result.node.empty());
        CHECK_EQ(map2.size(), 2);
    }

    SUBCASE("insert duplicate key via node handle") {
        skiplist_map<int, std::string> map2;
        map2[2] = "TWO";  // Duplicate key

        auto nh = map.extract(2);
        auto result = map2.insert(std::move(nh));

        CHECK_FALSE(result.inserted);
        CHECK_EQ(result.position->second, "TWO");  // Original value preserved
        CHECK_FALSE(result.node.empty());  // Node returned back
        CHECK_EQ(result.node.mapped(), "two");
    }

    SUBCASE("node handle move semantics") {
        auto nh1 = map.extract(1);
        CHECK_FALSE(nh1.empty());

        auto nh2 = std::move(nh1);
        CHECK(nh1.empty());  // NOLINT: testing moved-from state
        CHECK_FALSE(nh2.empty());
        CHECK_EQ(nh2.key(), 1);
    }

    SUBCASE("insert empty node handle") {
        skiplist_map<int, std::string>::node_type nh;
        CHECK(nh.empty());

        auto result = map.insert(std::move(nh));
        CHECK_FALSE(result.inserted);
        CHECK(result.position == map.end());
        CHECK_EQ(map.size(), 3);
    }

    SUBCASE("node handle swap") {
        auto nh1 = map.extract(1);
        auto nh2 = map.extract(2);

        nh1.swap(nh2);

        CHECK_EQ(nh1.key(), 2);
        CHECK_EQ(nh2.key(), 1);
    }
}

// ============================================================================
// skiplist_map merge tests
// ============================================================================

TEST_CASE("skiplist_map::merge") {
    SUBCASE("merge non-overlapping") {
        skiplist_map<int, int> map1{{1, 10}, {2, 20}};
        skiplist_map<int, int> map2{{3, 30}, {4, 40}};

        map1.merge(map2);

        CHECK_EQ(map1.size(), 4);
        CHECK(map2.empty());
        CHECK(map1.contains(1));
        CHECK(map1.contains(4));
    }

    SUBCASE("merge overlapping") {
        skiplist_map<int, int> map1{{1, 10}, {2, 20}};
        skiplist_map<int, int> map2{{2, 200}, {3, 30}};

        map1.merge(map2);

        CHECK_EQ(map1.size(), 3);
        CHECK_EQ(map1.at(2), 20);  // Original value preserved
        CHECK_EQ(map2.size(), 1);  // Duplicate not moved
        CHECK_EQ(map2.at(2), 200);
    }

    SUBCASE("merge empty into non-empty") {
        skiplist_map<int, int> map1{{1, 10}};
        skiplist_map<int, int> map2;

        map1.merge(map2);

        CHECK_EQ(map1.size(), 1);
        CHECK(map2.empty());
    }

    SUBCASE("merge non-empty into empty") {
        skiplist_map<int, int> map1;
        skiplist_map<int, int> map2{{1, 10}, {2, 20}};

        map1.merge(map2);

        CHECK_EQ(map1.size(), 2);
        CHECK(map2.empty());
    }

    SUBCASE("merge with rvalue") {
        skiplist_map<int, int> map1{{1, 10}};
        skiplist_map<int, int> map2{{2, 20}};

        map1.merge(std::move(map2));

        CHECK_EQ(map1.size(), 2);
    }
}

// ============================================================================
// skiplist_map string key tests
// ============================================================================

TEST_CASE("skiplist_map::string_keys") {
    skiplist_map<std::string, int> map;

    SUBCASE("insert and find string keys") {
        map.insert("apple", 1);
        map.insert("banana", 2);
        map.insert("cherry", 3);
        map.insert("date", 4);
        map.insert("elderberry", 5);

        CHECK_EQ(map.size(), 5);
        CHECK_EQ(map.at("apple"), 1);
        CHECK_EQ(map.at("banana"), 2);
        CHECK_EQ(map.at("cherry"), 3);
        CHECK_EQ(map.at("date"), 4);
        CHECK_EQ(map.at("elderberry"), 5);
    }

    SUBCASE("iteration order (lexicographical)") {
        map.insert("zebra", 1);
        map.insert("apple", 2);
        map.insert("mango", 3);

        std::vector<std::string> keys;
        for (const auto& [k, v] : map) {
            keys.push_back(k);
        }

        CHECK_EQ(keys.size(), 3);
        CHECK_EQ(keys[0], "apple");
        CHECK_EQ(keys[1], "mango");
        CHECK_EQ(keys[2], "zebra");
    }

    SUBCASE("large strings") {
        std::string long_key(1000, 'k');
        map[long_key] = 42;
        CHECK_EQ(map.at(long_key), 42);
    }

    SUBCASE("many string entries") {
        for (int i = 0; i < 1000; ++i) {
            std::string key = "key_" + std::to_string(i);
            map[key] = i * 2;
        }
        CHECK_EQ(map.size(), 1000);

        for (int i = 0; i < 1000; ++i) {
            std::string key = "key_" + std::to_string(i);
            CHECK_EQ(map.at(key), i * 2);
        }
    }

    SUBCASE("erase string keys") {
        map["a"] = 1;
        map["b"] = 2;
        map["c"] = 3;

        CHECK_EQ(map.erase("b"), 1);
        CHECK_EQ(map.size(), 2);
        CHECK(map.find("b") == map.end());
        CHECK_EQ(map.at("a"), 1);
        CHECK_EQ(map.at("c"), 3);
    }
}

// ============================================================================
// skiplist_map heterogeneous lookup tests
// ============================================================================

TEST_CASE("skiplist_map::heterogeneous_lookup") {
    // Use std::less<> for transparent comparison
    skiplist_map<std::string, int, std::less<>> map;

    map["apple"] = 1;
    map["banana"] = 2;
    map["cherry"] = 3;
    map["date"] = 4;

    SUBCASE("find with string_view") {
        std::string_view sv = "banana";
        auto it = map.find(sv);
        CHECK(it != map.end());
        CHECK_EQ(it->second, 2);

        auto it2 = map.find(std::string_view("notfound"));
        CHECK(it2 == map.end());
    }

    SUBCASE("find with const char*") {
        auto it = map.find("cherry");
        CHECK(it != map.end());
        CHECK_EQ(it->second, 3);
    }

    SUBCASE("contains with string_view") {
        CHECK(map.contains(std::string_view("apple")));
        CHECK_FALSE(map.contains(std::string_view("grape")));
    }

    SUBCASE("count with string_view") {
        CHECK_EQ(map.count(std::string_view("date")), 1);
        CHECK_EQ(map.count(std::string_view("fig")), 0);
    }

    SUBCASE("lower_bound with string_view") {
        auto it = map.lower_bound(std::string_view("banana"));
        CHECK(it != map.end());
        CHECK_EQ(it->first, "banana");

        auto it2 = map.lower_bound(std::string_view("blueberry"));
        CHECK(it2 != map.end());
        CHECK_EQ(it2->first, "cherry");
    }

    SUBCASE("upper_bound with string_view") {
        auto it = map.upper_bound(std::string_view("banana"));
        CHECK(it != map.end());
        CHECK_EQ(it->first, "cherry");
    }

    SUBCASE("equal_range with string_view") {
        auto [lb, ub] = map.equal_range(std::string_view("cherry"));
        CHECK(lb != map.end());
        CHECK_EQ(lb->first, "cherry");
        CHECK(ub != map.end());
        CHECK_EQ(ub->first, "date");
    }

    SUBCASE("erase with string_view") {
        CHECK_EQ(map.erase(std::string_view("banana")), 1);
        CHECK_EQ(map.size(), 3);
        CHECK_FALSE(map.contains(std::string_view("banana")));

        CHECK_EQ(map.erase(std::string_view("notexist")), 0);
        CHECK_EQ(map.size(), 3);
    }

    SUBCASE("const find with string_view") {
        const auto& cmap = map;
        auto it = cmap.find(std::string_view("apple"));
        CHECK(it != cmap.end());
        CHECK_EQ(it->second, 1);
    }
}

// ============================================================================
// skiplist_map complex types tests
// ============================================================================

TEST_CASE("skiplist_map::complex_types") {
    struct Point
    {
        int x, y;
        bool operator<(const Point& other) const {
            if (x != other.x) return x < other.x;
            return y < other.y;
        }
        bool operator==(const Point& other) const { return x == other.x && y == other.y; }
    };

    SUBCASE("struct as key") {
        skiplist_map<Point, std::string> map;
        map.insert(Point{1, 2}, "first");
        map.insert(Point{3, 4}, "second");
        map.insert(Point{1, 3}, "third");

        CHECK_EQ(map.size(), 3);
        CHECK_EQ(map.at(Point{1, 2}), "first");
        CHECK_EQ(map.at(Point{3, 4}), "second");
        CHECK_EQ(map.at(Point{1, 3}), "third");

        // Check ordering
        std::vector<Point> keys;
        for (const auto& [k, v] : map) {
            keys.push_back(k);
        }
        CHECK_EQ(keys[0], Point{1, 2});
        CHECK_EQ(keys[1], Point{1, 3});
        CHECK_EQ(keys[2], Point{3, 4});
    }

    SUBCASE("struct as value") {
        skiplist_map<int, Point> map;
        map[1] = Point{10, 20};
        map[2] = Point{30, 40};

        CHECK_EQ(map.at(1).x, 10);
        CHECK_EQ(map.at(1).y, 20);
        CHECK_EQ(map.at(2).x, 30);
        CHECK_EQ(map.at(2).y, 40);
    }
}

// ============================================================================
// skiplist_map move semantics tests
// ============================================================================

TEST_CASE("skiplist_map::move_semantics") {
    SUBCASE("move construct with strings") {
        skiplist_map<std::string, std::string> map1;
        map1["a"] = "1";
        map1["b"] = "2";

        skiplist_map<std::string, std::string> map2(std::move(map1));
        CHECK_EQ(map2.size(), 2);
        CHECK_EQ(map2.at("a"), "1");
        CHECK_EQ(map2.at("b"), "2");
        CHECK_EQ(map1.size(), 0);  // NOLINT: testing moved-from state
    }

    SUBCASE("move assign with strings") {
        skiplist_map<std::string, std::string> map1;
        map1["x"] = "10";

        skiplist_map<std::string, std::string> map2;
        map2["y"] = "20";

        map2 = std::move(map1);
        CHECK_EQ(map2.size(), 1);
        CHECK_EQ(map2.at("x"), "10");
    }

    SUBCASE("insert with moved value") {
        skiplist_map<int, std::string> map;
        std::string value = "test_value";
        map.insert(1, std::move(value));
        CHECK_EQ(map.at(1), "test_value");
    }
}

// ============================================================================
// skiplist_map allocator tests
// ============================================================================

TEST_CASE("skiplist_map::allocator") {
    SUBCASE("default allocator") {
        skiplist_map<int, int> map;
        auto alloc = map.get_allocator();
        CHECK((std::is_same_v<decltype(alloc), std::allocator<std::pair<const int, int>>>));
    }

    SUBCASE("constructor with allocator") {
        std::allocator<std::pair<const int, int>> alloc;
        skiplist_map<int, int> map(alloc);
        map[1] = 10;
        CHECK_EQ(map.size(), 1);
    }

    SUBCASE("constructor with comparator and allocator") {
        std::allocator<std::pair<const int, int>> alloc;
        skiplist_map<int, int, std::greater<int>> map(std::greater<int>{}, alloc);
        map[1] = 10;
        map[2] = 20;
        CHECK_EQ(map.size(), 2);
        auto it = map.begin();
        CHECK_EQ(it->first, 2);  // Greater comparator
    }

    SUBCASE("custom allocator is actually used") {
        test_skiplist_alloc::alloc_count = 0;
        test_skiplist_alloc::dealloc_count = 0;

        using Alloc = test_skiplist_alloc::CountingAllocator<std::pair<const int, int>>;
        {
            skiplist_map<int, int, std::less<int>, Alloc> map;

            for (int i = 0; i < 100; ++i) {
                map[i] = i;
            }
            CHECK(test_skiplist_alloc::alloc_count > 0);
            CHECK_EQ(map.size(), 100);

            for (int i = 0; i < 50; ++i) {
                map.erase(i);
            }
            CHECK(test_skiplist_alloc::dealloc_count > 0);
            CHECK_EQ(map.size(), 50);

            map.clear();
            CHECK(map.empty());
        }

        CHECK_EQ(test_skiplist_alloc::alloc_count.load(), test_skiplist_alloc::dealloc_count.load());
    }
}

// ============================================================================
// skiplist_map large scale tests
// ============================================================================

TEST_CASE("skiplist_map::large_scale") {
    constexpr int N = 10000;

    SUBCASE("insert many elements") {
        skiplist_map<int, int> map;
        for (int i = 0; i < N; ++i) {
            map.insert(i, i * 2);
        }
        CHECK_EQ(map.size(), N);

        for (int i = 0; i < N; ++i) {
            CHECK_EQ(map.at(i), i * 2);
        }
    }

    SUBCASE("random access after large insert") {
        skiplist_map<int, int> map;
        for (int i = 0; i < N; ++i) {
            map.insert(i, i * 2);
        }

        std::mt19937 rng(123);
        std::uniform_int_distribution<int> dist(0, N - 1);

        for (int i = 0; i < 1000; ++i) {
            int key = dist(rng);
            CHECK_EQ(map.at(key), key * 2);
        }
    }

    SUBCASE("ordering preserved with many elements") {
        skiplist_map<int, int> map;
        std::vector<int> keys;
        for (int i = 0; i < N; ++i) {
            keys.push_back(i);
        }

        std::mt19937 rng(456);
        std::shuffle(keys.begin(), keys.end(), rng);

        for (int key : keys) {
            map.insert(key, key);
        }

        int expected = 0;
        for (const auto& [k, v] : map) {
            CHECK_EQ(k, expected);
            ++expected;
        }
        CHECK_EQ(expected, N);
    }
}

// ============================================================================
// skiplist_map compare with std::map tests
// ============================================================================

TEST_CASE("skiplist_map::compare_with_std_map") {
    skiplist_map<int, std::string> smap;
    std::map<int, std::string> stdmap;

    std::mt19937 rng(789);
    std::uniform_int_distribution<int> key_dist(0, 999);

    // Insert same elements
    for (int i = 0; i < 500; ++i) {
        int key = key_dist(rng);
        std::string value = "value_" + std::to_string(key);
        smap.insert(key, value);
        stdmap.insert({key, value});
    }

    CHECK_EQ(smap.size(), stdmap.size());

    // Compare contents
    auto sit = smap.begin();
    auto mit = stdmap.begin();
    while (sit != smap.end() && mit != stdmap.end()) {
        CHECK_EQ(sit->first, mit->first);
        CHECK_EQ(sit->second, mit->second);
        ++sit;
        ++mit;
    }
    CHECK(sit == smap.end());
    CHECK(mit == stdmap.end());
}

// ============================================================================
// skiplist_map stress tests
// ============================================================================

TEST_CASE("skiplist_map::stress_random_operations") {
    skiplist_map<int, int> map;
    std::map<int, int> ref;  // Reference implementation
    std::mt19937 rng(12345);
    std::uniform_int_distribution<int> key_dist(0, 1000);
    std::uniform_int_distribution<int> op_dist(0, 2);

    for (int i = 0; i < 10000; ++i) {
        int key = key_dist(rng);
        int op = op_dist(rng);

        switch (op) {
            case 0: {  // Insert
                int value = i;
                auto [it1, ins1] = map.insert(key, value);
                auto [it2, ins2] = ref.insert({key, value});
                CHECK_EQ(ins1, ins2);
                break;
            }
            case 1: {  // Erase
                size_t cnt1 = map.erase(key);
                size_t cnt2 = ref.erase(key);
                CHECK_EQ(cnt1, cnt2);
                break;
            }
            case 2: {  // Find
                auto it1 = map.find(key);
                auto it2 = ref.find(key);
                CHECK_EQ(it1 != map.end(), it2 != ref.end());
                if (it1 != map.end()) {
                    CHECK_EQ(it1->first, it2->first);
                    CHECK_EQ(it1->second, it2->second);
                }
                break;
            }
        }

        if (i % 1000 == 0) {
            CHECK_EQ(map.size(), ref.size());
        }
    }

    CHECK_EQ(map.size(), ref.size());

    auto sit = map.begin();
    auto rit = ref.begin();
    while (sit != map.end()) {
        CHECK_EQ(sit->first, rit->first);
        CHECK_EQ(sit->second, rit->second);
        ++sit;
        ++rit;
    }
}

// ============================================================================
// skiplist_map iterator stability tests
// ============================================================================

TEST_CASE("skiplist_map::iterator_stability") {
    skiplist_map<int, int> map;
    for (int i = 0; i < 10; ++i) {
        map.insert(i * 2, i * 20);  // 0, 2, 4, 6, 8, 10, 12, 14, 16, 18
    }

    SUBCASE("insert does not invalidate iterators") {
        auto it = map.find(10);
        CHECK(it != map.end());
        CHECK_EQ(it->first, 10);
        CHECK_EQ(it->second, 100);

        // Insert before and after
        map.insert(9, 90);
        map.insert(11, 110);

        // Iterator still valid
        CHECK_EQ(it->first, 10);
        CHECK_EQ(it->second, 100);
    }

    SUBCASE("erase invalidates only erased element") {
        auto it2 = map.find(2);
        auto it4 = map.find(4);
        auto it6 = map.find(6);

        map.erase(it4);

        // it2 and it6 still valid
        CHECK_EQ(it2->first, 2);
        CHECK_EQ(it6->first, 6);
    }

    SUBCASE("pointer stability") {
        auto it = map.find(10);
        const int* key_ptr = &it->first;
        int* val_ptr = &it->second;

        map.insert(100, 1000);
        map.insert(200, 2000);

        CHECK_EQ(*key_ptr, 10);
        CHECK_EQ(*val_ptr, 100);
    }
}

// ============================================================================
// skiplist_map erase_if tests (C++20)
// ============================================================================

TEST_CASE("skiplist_map::erase_if") {
    skiplist_map<int, int> map{{1, 10}, {2, 20}, {3, 30}, {4, 40}, {5, 50}};

    SUBCASE("erase even keys") {
        auto count = erase_if(map, [](const auto& kv) { return kv.first % 2 == 0; });
        CHECK_EQ(count, 2);
        CHECK_EQ(map.size(), 3);
        CHECK(map.contains(1));
        CHECK_FALSE(map.contains(2));
        CHECK(map.contains(3));
        CHECK_FALSE(map.contains(4));
        CHECK(map.contains(5));
    }

    SUBCASE("erase by value") {
        auto count = erase_if(map, [](const auto& kv) { return kv.second > 30; });
        CHECK_EQ(count, 2);
        CHECK_EQ(map.size(), 3);
    }

    SUBCASE("erase all") {
        auto count = erase_if(map, [](const auto&) { return true; });
        CHECK_EQ(count, 5);
        CHECK(map.empty());
    }

    SUBCASE("erase none") {
        auto count = erase_if(map, [](const auto&) { return false; });
        CHECK_EQ(count, 0);
        CHECK_EQ(map.size(), 5);
    }
}

// ============================================================================
// skiplist_map edge cases
// ============================================================================

TEST_CASE("skiplist_map::edge_cases") {
    SUBCASE("single element operations") {
        skiplist_map<int, int> map;
        map[42] = 100;

        CHECK_EQ(map.size(), 1);
        CHECK_EQ(map.begin()->first, 42);

        auto it = map.begin();
        ++it;
        CHECK(it == map.end());

        map.erase(42);
        CHECK(map.empty());
    }

    SUBCASE("duplicate insert returns existing") {
        skiplist_map<int, int> map;
        auto [it1, ins1] = map.insert(1, 10);
        auto [it2, ins2] = map.insert(1, 20);

        CHECK(ins1);
        CHECK_FALSE(ins2);
        CHECK(it1 == it2);
        CHECK_EQ(it1->second, 10);
    }

    SUBCASE("find on empty map") {
        skiplist_map<int, int> map;
        CHECK(map.find(1) == map.end());
        CHECK_EQ(map.count(1), 0);
        CHECK_FALSE(map.contains(1));
    }

    SUBCASE("bounds on empty map") {
        skiplist_map<int, int> map;
        CHECK(map.lower_bound(1) == map.end());
        CHECK(map.upper_bound(1) == map.end());
        auto [lb, ub] = map.equal_range(1);
        CHECK(lb == map.end());
        CHECK(ub == map.end());
    }

    SUBCASE("negative keys") {
        skiplist_map<int, int> map;
        map[-5] = 50;
        map[-10] = 100;
        map[0] = 0;
        map[5] = 50;

        CHECK_EQ(map.size(), 4);
        CHECK_EQ(map.begin()->first, -10);

        std::vector<int> keys;
        for (const auto& [k, v] : map) {
            keys.push_back(k);
        }
        CHECK_EQ(keys, std::vector<int>{-10, -5, 0, 5});
    }

    SUBCASE("max_size") {
        skiplist_map<int, int> map;
        CHECK(map.max_size() > 0);
    }

    SUBCASE("comparator access") {
        skiplist_map<int, int, std::greater<int>> map;
        auto comp = map.key_comp();
        CHECK(comp(2, 1));  // 2 > 1 is true with greater<>

        auto vcomp = map.value_comp();
        std::pair<const int, int> a{2, 0};
        std::pair<const int, int> b{1, 0};
        CHECK(vcomp(a, b));
    }
}

// ============================================================================
// skiplist_set tests
// ============================================================================

TEST_CASE("skiplist_set::basic") {
    SUBCASE("default constructor") {
        skiplist_set<int> set;
        CHECK(set.empty());
        CHECK_EQ(set.size(), 0);
    }

    SUBCASE("initializer list") {
        skiplist_set<int> set{3, 1, 2};
        CHECK_EQ(set.size(), 3);
        CHECK(set.contains(1));
        CHECK(set.contains(2));
        CHECK(set.contains(3));
    }

    SUBCASE("range constructor") {
        std::vector<int> vec{3, 1, 2, 1};  // With duplicate
        skiplist_set<int> set(vec.begin(), vec.end());
        CHECK_EQ(set.size(), 3);  // Duplicate ignored
    }

    SUBCASE("copy constructor") {
        skiplist_set<int> set1{1, 2, 3};
        skiplist_set<int> set2(set1);
        CHECK_EQ(set2.size(), 3);
        CHECK(set2.contains(2));

        // Verify independence
        set1.insert(4);
        CHECK_FALSE(set2.contains(4));
    }

    SUBCASE("move constructor") {
        skiplist_set<int> set1{1, 2, 3};
        skiplist_set<int> set2(std::move(set1));
        CHECK_EQ(set2.size(), 3);
        CHECK(set1.empty());  // NOLINT
    }

    SUBCASE("assignment operators") {
        skiplist_set<int> set1{1, 2};
        skiplist_set<int> set2{3, 4, 5};

        set2 = set1;
        CHECK_EQ(set2.size(), 2);
        CHECK(set2.contains(1));

        skiplist_set<int> set3;
        set3 = std::move(set2);
        CHECK_EQ(set3.size(), 2);
    }

    SUBCASE("initializer list assignment") {
        skiplist_set<int> set{1, 2};
        set = {3, 4, 5};
        CHECK_EQ(set.size(), 3);
        CHECK_FALSE(set.contains(1));
        CHECK(set.contains(3));
    }
}

TEST_CASE("skiplist_set::insert") {
    SUBCASE("insert single element") {
        skiplist_set<int> set;
        auto [it, inserted] = set.insert(42);
        CHECK(inserted);
        CHECK_EQ(*it, 42);
    }

    SUBCASE("insert duplicate") {
        skiplist_set<int> set{1, 2, 3};
        auto [it, inserted] = set.insert(2);
        CHECK_FALSE(inserted);
        CHECK_EQ(*it, 2);
        CHECK_EQ(set.size(), 3);
    }

    SUBCASE("insert many elements") {
        skiplist_set<int> set;
        for (int i = 0; i < 100; ++i) {
            set.insert(i);
        }
        CHECK_EQ(set.size(), 100);

        int expected = 0;
        for (int val : set) {
            CHECK_EQ(val, expected);
            ++expected;
        }
    }

    SUBCASE("insert random order") {
        skiplist_set<int> set;
        std::vector<int> values;
        for (int i = 0; i < 100; ++i) {
            values.push_back(i);
        }

        std::mt19937 rng(42);
        std::shuffle(values.begin(), values.end(), rng);

        for (int v : values) {
            set.insert(v);
        }

        int expected = 0;
        for (int val : set) {
            CHECK_EQ(val, expected);
            ++expected;
        }
    }

    SUBCASE("insert range") {
        std::vector<int> vec{5, 3, 1, 4, 2};
        skiplist_set<int> set;
        set.insert(vec.begin(), vec.end());
        CHECK_EQ(set.size(), 5);
    }

    SUBCASE("insert initializer list") {
        skiplist_set<int> set;
        set.insert({1, 2, 3});
        CHECK_EQ(set.size(), 3);
    }
}

TEST_CASE("skiplist_set::emplace") {
    SUBCASE("emplace basic") {
        skiplist_set<std::string> set;
        auto [it, inserted] = set.emplace("hello");
        CHECK(inserted);
        CHECK_EQ(*it, "hello");
    }

    SUBCASE("emplace duplicate") {
        skiplist_set<int> set{1, 2, 3};
        auto [it, inserted] = set.emplace(2);
        CHECK_FALSE(inserted);
    }
}

TEST_CASE("skiplist_set::find") {
    skiplist_set<int> set{10, 20, 30, 40, 50};

    SUBCASE("find existing") {
        auto it = set.find(30);
        CHECK(it != set.end());
        CHECK_EQ(*it, 30);
    }

    SUBCASE("find non-existing") {
        auto it = set.find(25);
        CHECK(it == set.end());
    }

    SUBCASE("const find") {
        const auto& cset = set;
        auto it = cset.find(30);
        CHECK_EQ(*it, 30);
    }
}

TEST_CASE("skiplist_set::erase") {
    SUBCASE("erase by value") {
        skiplist_set<int> set{1, 2, 3, 4, 5};
        auto count = set.erase(3);
        CHECK_EQ(count, 1);
        CHECK_EQ(set.size(), 4);
        CHECK_FALSE(set.contains(3));
    }

    SUBCASE("erase non-existing") {
        skiplist_set<int> set{1, 2, 3};
        auto count = set.erase(100);
        CHECK_EQ(count, 0);
        CHECK_EQ(set.size(), 3);
    }

    SUBCASE("erase by iterator") {
        skiplist_set<int> set{1, 2, 3};
        auto it = set.find(2);
        auto next = set.erase(it);
        CHECK_EQ(set.size(), 2);
        CHECK_EQ(*next, 3);
    }

    SUBCASE("erase range") {
        skiplist_set<int> set{1, 2, 3, 4, 5};
        auto first = set.find(2);
        auto last = set.find(4);
        set.erase(first, last);
        CHECK_EQ(set.size(), 3);
        CHECK(set.contains(1));
        CHECK_FALSE(set.contains(2));
        CHECK_FALSE(set.contains(3));
        CHECK(set.contains(4));
    }
}

TEST_CASE("skiplist_set::bounds") {
    skiplist_set<int> set{10, 20, 30, 40, 50};

    SUBCASE("lower_bound") {
        auto it = set.lower_bound(20);
        CHECK_EQ(*it, 20);

        it = set.lower_bound(25);
        CHECK_EQ(*it, 30);

        it = set.lower_bound(60);
        CHECK(it == set.end());
    }

    SUBCASE("upper_bound") {
        auto it = set.upper_bound(20);
        CHECK_EQ(*it, 30);

        it = set.upper_bound(50);
        CHECK(it == set.end());
    }

    SUBCASE("equal_range") {
        auto [first, second] = set.equal_range(30);
        CHECK_EQ(*first, 30);
        CHECK_EQ(*second, 40);
    }
}

TEST_CASE("skiplist_set::comparison") {
    SUBCASE("equal sets") {
        skiplist_set<int> set1{1, 2, 3};
        skiplist_set<int> set2{1, 2, 3};
        CHECK(set1 == set2);
        CHECK_FALSE(set1 != set2);
    }

    SUBCASE("unequal sets") {
        skiplist_set<int> set1{1, 2, 3};
        skiplist_set<int> set2{1, 2, 4};
        CHECK(set1 != set2);
        CHECK(set1 < set2);
        CHECK(set2 > set1);
    }

    SUBCASE("empty sets are equal") {
        skiplist_set<int> set1;
        skiplist_set<int> set2;
        CHECK(set1 == set2);
    }
}

TEST_CASE("skiplist_set::merge") {
    SUBCASE("merge non-overlapping") {
        skiplist_set<int> set1{1, 2};
        skiplist_set<int> set2{3, 4};
        set1.merge(set2);
        CHECK_EQ(set1.size(), 4);
        CHECK(set2.empty());
    }

    SUBCASE("merge overlapping") {
        skiplist_set<int> set1{1, 2};
        skiplist_set<int> set2{2, 3};
        set1.merge(set2);
        CHECK_EQ(set1.size(), 3);
        CHECK_EQ(set2.size(), 1);  // Duplicate not moved
        CHECK(set2.contains(2));
    }
}

TEST_CASE("skiplist_set::extract") {
    skiplist_set<int> set{1, 2, 3};

    SUBCASE("extract by value") {
        auto nh = set.extract(2);
        CHECK(!nh.empty());
        CHECK_EQ(nh.value(), 2);
        CHECK_EQ(set.size(), 2);
        CHECK_FALSE(set.contains(2));
    }

    SUBCASE("extract non-existing") {
        auto nh = set.extract(100);
        CHECK(nh.empty());
        CHECK_EQ(set.size(), 3);
    }

    SUBCASE("insert extracted node") {
        auto nh = set.extract(2);
        skiplist_set<int> set2{10};
        auto result = set2.insert(std::move(nh));
        CHECK(result.inserted);
        CHECK_EQ(set2.size(), 2);
    }
}

TEST_CASE("skiplist_set::iterator_stability") {
    skiplist_set<int> set{10, 20, 30, 40, 50};

    SUBCASE("insert does not invalidate iterators") {
        auto it = set.find(30);
        CHECK_EQ(*it, 30);

        set.insert(25);
        set.insert(35);

        CHECK_EQ(*it, 30);
    }
}

TEST_CASE("skiplist_set::erase_if") {
    skiplist_set<int> set{1, 2, 3, 4, 5, 6, 7, 8, 9, 10};

    auto count = erase_if(set, [](int x) { return x % 2 == 0; });
    CHECK_EQ(count, 5);
    CHECK_EQ(set.size(), 5);

    for (int val : set) {
        CHECK(val % 2 == 1);
    }
}

TEST_CASE("skiplist_set::string_values") {
    skiplist_set<std::string> set;

    set.insert("banana");
    set.insert("apple");
    set.insert("cherry");

    std::vector<std::string> values;
    for (const auto& s : set) {
        values.push_back(s);
    }

    CHECK_EQ(values, std::vector<std::string>{"apple", "banana", "cherry"});
}

TEST_CASE("skiplist_set::large_scale") {
    skiplist_set<int> set;
    constexpr int N = 10000;

    for (int i = 0; i < N; ++i) {
        set.insert(i);
    }
    CHECK_EQ(set.size(), N);

    // Verify ordering
    int expected = 0;
    for (int val : set) {
        CHECK_EQ(val, expected);
        ++expected;
    }

    // Random lookups
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> dist(0, N - 1);
    for (int i = 0; i < 1000; ++i) {
        int key = dist(rng);
        CHECK(set.contains(key));
    }
}

TEST_CASE("skiplist_set::compare_with_std_set") {
    skiplist_set<int> sset;
    std::set<int> stdset;

    std::mt19937 rng(123);
    std::uniform_int_distribution<int> dist(0, 999);

    for (int i = 0; i < 500; ++i) {
        int val = dist(rng);
        sset.insert(val);
        stdset.insert(val);
    }

    CHECK_EQ(sset.size(), stdset.size());

    auto sit = sset.begin();
    auto mit = stdset.begin();
    while (sit != sset.end()) {
        CHECK_EQ(*sit, *mit);
        ++sit;
        ++mit;
    }
}

// ============================================================================
// PMR (Polymorphic Memory Resource) Tests
// ============================================================================

TEST_CASE("skiplist_map::pmr") {
    SUBCASE("basic operations with monotonic_buffer_resource") {
        std::array<std::byte, 8192> buffer;
        std::pmr::monotonic_buffer_resource resource(buffer.data(), buffer.size(),
                                                     std::pmr::null_memory_resource());

        stdb::pmr::skiplist_map<int, std::string> map(&resource);

        map[1] = "one";
        map[2] = "two";
        map[3] = "three";

        CHECK_EQ(map.size(), 3);
        CHECK_EQ(map[1], "one");
        CHECK_EQ(map[2], "two");
        CHECK_EQ(map[3], "three");

        map.erase(2);
        CHECK_EQ(map.size(), 2);
        CHECK(map.find(2) == map.end());
    }

    SUBCASE("allocator-only constructor") {
        std::pmr::monotonic_buffer_resource resource;
        stdb::pmr::skiplist_map<int, int> map(&resource);

        CHECK(map.empty());
        CHECK_EQ(map.size(), 0);

        map[42] = 100;
        CHECK_EQ(map.size(), 1);
        CHECK_EQ(map[42], 100);
    }

    SUBCASE("allocator-extended copy constructor") {
        std::pmr::monotonic_buffer_resource resource1;
        std::pmr::monotonic_buffer_resource resource2;

        stdb::pmr::skiplist_map<int, int> map1(&resource1);
        map1[1] = 10;
        map1[2] = 20;

        stdb::pmr::skiplist_map<int, int> map2(map1, &resource2);

        CHECK_EQ(map2.size(), 2);
        CHECK_EQ(map2[1], 10);
        CHECK_EQ(map2[2], 20);
        CHECK_EQ(map2.get_allocator().resource(), &resource2);
    }

    SUBCASE("allocator-extended move constructor - same resource") {
        std::pmr::monotonic_buffer_resource resource;

        stdb::pmr::skiplist_map<int, int> map1(&resource);
        map1[1] = 10;
        map1[2] = 20;

        stdb::pmr::skiplist_map<int, int> map2(std::move(map1), &resource);

        CHECK_EQ(map2.size(), 2);
        CHECK_EQ(map2[1], 10);
        CHECK_EQ(map2[2], 20);
        CHECK(map1.empty());  // Resources were stolen
    }

    SUBCASE("allocator-extended move constructor - different resource") {
        std::pmr::monotonic_buffer_resource resource1;
        std::pmr::monotonic_buffer_resource resource2;

        stdb::pmr::skiplist_map<int, int> map1(&resource1);
        map1[1] = 10;
        map1[2] = 20;

        stdb::pmr::skiplist_map<int, int> map2(std::move(map1), &resource2);

        CHECK_EQ(map2.size(), 2);
        CHECK_EQ(map2[1], 10);
        CHECK_EQ(map2[2], 20);
        CHECK_EQ(map2.get_allocator().resource(), &resource2);
    }

    SUBCASE("copy assignment - keeps allocator") {
        std::pmr::monotonic_buffer_resource resource1;
        std::pmr::monotonic_buffer_resource resource2;

        stdb::pmr::skiplist_map<int, int> map1(&resource1);
        map1[1] = 10;
        map1[2] = 20;

        stdb::pmr::skiplist_map<int, int> map2(&resource2);
        map2[3] = 30;

        map2 = map1;

        CHECK_EQ(map2.size(), 2);
        CHECK_EQ(map2[1], 10);
        CHECK_EQ(map2[2], 20);
        CHECK_EQ(map2.get_allocator().resource(), &resource2);  // Kept original allocator
    }

    SUBCASE("move assignment - same resource") {
        std::pmr::monotonic_buffer_resource resource;

        stdb::pmr::skiplist_map<int, int> map1(&resource);
        map1[1] = 10;
        map1[2] = 20;

        stdb::pmr::skiplist_map<int, int> map2(&resource);
        map2[3] = 30;

        map2 = std::move(map1);

        CHECK_EQ(map2.size(), 2);
        CHECK_EQ(map2[1], 10);
        CHECK_EQ(map2[2], 20);
        CHECK(map1.empty());  // Resources were stolen
    }

    SUBCASE("move assignment - different resource") {
        std::pmr::monotonic_buffer_resource resource1;
        std::pmr::monotonic_buffer_resource resource2;

        stdb::pmr::skiplist_map<int, int> map1(&resource1);
        map1[1] = 10;
        map1[2] = 20;

        stdb::pmr::skiplist_map<int, int> map2(&resource2);
        map2[3] = 30;

        map2 = std::move(map1);

        CHECK_EQ(map2.size(), 2);
        CHECK_EQ(map2[1], 10);
        CHECK_EQ(map2[2], 20);
        CHECK_EQ(map2.get_allocator().resource(), &resource2);  // Kept original allocator
    }

    SUBCASE("string keys with PMR") {
        std::pmr::monotonic_buffer_resource resource;
        stdb::pmr::skiplist_map<std::string, int> map(&resource);

        map["hello"] = 1;
        map["world"] = 2;
        map["test"] = 3;

        CHECK_EQ(map.size(), 3);
        CHECK_EQ(map["hello"], 1);
        CHECK_EQ(map["world"], 2);
        CHECK_EQ(map["test"], 3);

        int count = 0;
        for (const auto& [key, value] : map) {
            (void)key;
            (void)value;
            ++count;
        }
        CHECK_EQ(count, 3);
    }

    SUBCASE("skiplist_set with PMR") {
        std::pmr::monotonic_buffer_resource resource;
        stdb::pmr::skiplist_set<int> set(&resource);

        set.insert(1);
        set.insert(2);
        set.insert(3);

        CHECK_EQ(set.size(), 3);
        CHECK(set.contains(1));
        CHECK(set.contains(2));
        CHECK(set.contains(3));
        CHECK(!set.contains(4));
    }
}

}  // namespace stdb::container

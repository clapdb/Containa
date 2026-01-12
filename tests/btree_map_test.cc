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

#include "container/btree_map.hpp"

#include <atomic>
#include <map>
#include <random>
#include <set>
#include <string>
#include <vector>

#include "doctest/doctest/doctest.h"

// Counting allocator for testing allocator support
namespace test_alloc {
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
}  // namespace test_alloc

namespace stdb::container {

TEST_CASE("btree_map::basic") {
    SUBCASE("default constructor") {
        btree_map<int, int> map;
        CHECK(map.empty());
        CHECK_EQ(map.size(), 0);
    }

    SUBCASE("initializer list") {
        btree_map<int, std::string> map{{1, "one"}, {2, "two"}, {3, "three"}};
        CHECK_EQ(map.size(), 3);
        CHECK_EQ(map.at(1), "one");
        CHECK_EQ(map.at(2), "two");
        CHECK_EQ(map.at(3), "three");
    }

    SUBCASE("range constructor from vector") {
        std::vector<std::pair<int, std::string>> vec{{1, "one"}, {2, "two"}, {3, "three"}};
        btree_map<int, std::string> map(vec.begin(), vec.end());
        CHECK_EQ(map.size(), 3);
        CHECK_EQ(map.at(1), "one");
        CHECK_EQ(map.at(2), "two");
        CHECK_EQ(map.at(3), "three");
    }

    SUBCASE("range constructor from std::map") {
        std::map<int, int> stdmap{{1, 10}, {2, 20}, {3, 30}};
        btree_map<int, int> map(stdmap.begin(), stdmap.end());
        CHECK_EQ(map.size(), 3);
        CHECK_EQ(map.at(1), 10);
        CHECK_EQ(map.at(2), 20);
        CHECK_EQ(map.at(3), 30);
    }

    SUBCASE("range constructor with custom comparator") {
        std::vector<std::pair<int, int>> vec{{1, 10}, {2, 20}, {3, 30}};
        btree_map<int, int, std::greater<int>> map(vec.begin(), vec.end(), std::greater<int>{});
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
        btree_map<int, int> map1{{1, 10}, {2, 20}, {3, 30}};
        btree_map<int, int> map2(map1);
        CHECK_EQ(map2.size(), 3);
        CHECK_EQ(map2.at(1), 10);
        CHECK_EQ(map2.at(2), 20);
        CHECK_EQ(map2.at(3), 30);
    }

    SUBCASE("move constructor") {
        btree_map<int, int> map1{{1, 10}, {2, 20}};
        btree_map<int, int> map2(std::move(map1));
        CHECK_EQ(map2.size(), 2);
        CHECK(map1.empty());
    }

    SUBCASE("copy assignment") {
        btree_map<int, int> map1{{1, 10}, {2, 20}};
        btree_map<int, int> map2;
        map2 = map1;
        CHECK_EQ(map2.size(), 2);
        CHECK_EQ(map2.at(1), 10);
    }

    SUBCASE("move assignment") {
        btree_map<int, int> map1{{1, 10}, {2, 20}};
        btree_map<int, int> map2;
        map2 = std::move(map1);
        CHECK_EQ(map2.size(), 2);
        CHECK(map1.empty());
    }
}

TEST_CASE("btree_map::insert") {
    SUBCASE("insert single element") {
        btree_map<int, int> map;
        auto [it, inserted] = map.insert(1, 100);
        CHECK(inserted);
        CHECK_EQ(it->first, 1);
        CHECK_EQ(it->second, 100);
        CHECK_EQ(map.size(), 1);
    }

    SUBCASE("insert duplicate key") {
        btree_map<int, int> map;
        map.insert(1, 100);
        auto [it, inserted] = map.insert(1, 200);
        CHECK_FALSE(inserted);
        CHECK_EQ(it->second, 100);  // Original value preserved
        CHECK_EQ(map.size(), 1);
    }

    SUBCASE("insert multiple elements") {
        btree_map<int, int> map;
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
        btree_map<int, int> map;
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
        btree_map<int, int> map;
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

    SUBCASE("insert initializer_list") {
        btree_map<int, std::string> map;
        map.insert({{1, "one"}, {2, "two"}, {3, "three"}});
        CHECK_EQ(map.size(), 3);
        CHECK_EQ(map[1], "one");
        CHECK_EQ(map[2], "two");
        CHECK_EQ(map[3], "three");

        // Insert with duplicates - should ignore duplicates
        map.insert({{2, "TWO"}, {4, "four"}, {5, "five"}});
        CHECK_EQ(map.size(), 5);
        CHECK_EQ(map[2], "two");  // Original preserved
        CHECK_EQ(map[4], "four");
        CHECK_EQ(map[5], "five");
    }

    SUBCASE("insert iterator range") {
        std::vector<std::pair<int, int>> vec = {{10, 100}, {20, 200}, {30, 300}};
        btree_map<int, int> map;
        map.insert(vec.begin(), vec.end());
        CHECK_EQ(map.size(), 3);
        CHECK_EQ(map[10], 100);
        CHECK_EQ(map[20], 200);
        CHECK_EQ(map[30], 300);
    }
}

TEST_CASE("btree_map::find") {
    btree_map<int, std::string> map;
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
}

TEST_CASE("btree_map::contains_count") {
    btree_map<int, int> map{{1, 10}, {2, 20}, {3, 30}};

    SUBCASE("contains") {
        CHECK(map.contains(1));
        CHECK(map.contains(2));
        CHECK(map.contains(3));
        CHECK_FALSE(map.contains(0));
        CHECK_FALSE(map.contains(4));
    }

    SUBCASE("count") {
        CHECK_EQ(map.count(1), 1);
        CHECK_EQ(map.count(2), 1);
        CHECK_EQ(map.count(0), 0);
        CHECK_EQ(map.count(100), 0);
    }
}

TEST_CASE("btree_map::operator[]") {
    btree_map<int, int> map;

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
    }
}

TEST_CASE("btree_map::at") {
    btree_map<int, std::string> map{{1, "one"}, {2, "two"}};

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
}

TEST_CASE("btree_map::erase") {
    btree_map<int, int> map;
    for (int i = 0; i < 100; ++i) {
        map.insert(i, i * 10);
    }

    SUBCASE("erase existing key") {
        size_t erased = map.erase(50);
        CHECK_EQ(erased, 1);
        CHECK_EQ(map.size(), 99);
        CHECK_FALSE(map.contains(50));
    }

    SUBCASE("erase non-existing key") {
        size_t erased = map.erase(1000);
        CHECK_EQ(erased, 0);
        CHECK_EQ(map.size(), 100);
    }

    SUBCASE("erase multiple keys") {
        map.erase(10);
        map.erase(20);
        map.erase(30);
        CHECK_EQ(map.size(), 97);
        CHECK_FALSE(map.contains(10));
        CHECK_FALSE(map.contains(20));
        CHECK_FALSE(map.contains(30));
    }

    SUBCASE("erase preserves order") {
        map.erase(50);
        int expected = 0;
        for (const auto& [k, v] : map) {
            if (expected == 50) ++expected;  // Skip erased
            CHECK_EQ(k, expected);
            ++expected;
        }
    }
}

TEST_CASE("btree_map::clear") {
    btree_map<int, int> map;
    for (int i = 0; i < 100; ++i) {
        map.insert(i, i);
    }

    CHECK_EQ(map.size(), 100);
    map.clear();
    CHECK(map.empty());
    CHECK_EQ(map.size(), 0);

    // Can insert after clear
    map.insert(1, 100);
    CHECK_EQ(map.size(), 1);
    CHECK_EQ(map.at(1), 100);
}

TEST_CASE("btree_map::iteration") {
    btree_map<int, int> map;
    for (int i = 0; i < 100; ++i) {
        map.insert(i, i * 10);
    }

    SUBCASE("forward iteration in order") {
        int expected = 0;
        for (const auto& [k, v] : map) {
            CHECK_EQ(k, expected);
            CHECK_EQ(v, expected * 10);
            ++expected;
        }
        CHECK_EQ(expected, 100);
    }

    SUBCASE("const iteration") {
        const auto& cmap = map;
        int count = 0;
        for (const auto& [k, v] : cmap) {
            CHECK_EQ(v, k * 10);
            ++count;
        }
        CHECK_EQ(count, 100);
    }

    SUBCASE("begin/end on empty map") {
        btree_map<int, int> empty_map;
        CHECK(empty_map.begin() == empty_map.end());
    }
}

TEST_CASE("btree_map::lower_upper_bound") {
    btree_map<int, int> map;
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
}

TEST_CASE("btree_map::comparison") {
    SUBCASE("equal maps") {
        btree_map<int, int> map1{{1, 10}, {2, 20}, {3, 30}};
        btree_map<int, int> map2{{1, 10}, {2, 20}, {3, 30}};
        CHECK(map1 == map2);
        CHECK_FALSE(map1 != map2);
    }

    SUBCASE("different size") {
        btree_map<int, int> map1{{1, 10}, {2, 20}};
        btree_map<int, int> map2{{1, 10}, {2, 20}, {3, 30}};
        CHECK(map1 != map2);
    }

    SUBCASE("different values") {
        btree_map<int, int> map1{{1, 10}, {2, 20}};
        btree_map<int, int> map2{{1, 10}, {2, 999}};
        CHECK(map1 != map2);
    }
}

TEST_CASE("btree_map::string_keys") {
    btree_map<std::string, int> map;

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

    SUBCASE("iteration order") {
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
}

TEST_CASE("btree_map::heterogeneous_lookup") {
    // Use std::less<> for transparent comparison
    btree_map<std::string, int, std::less<>> map;

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

TEST_CASE("btree_map::string_string") {
    btree_map<std::string, std::string> map;

    SUBCASE("insert and find") {
        map["hello"] = "world";
        map["foo"] = "bar";
        map["key"] = "value";

        CHECK_EQ(map.size(), 3);
        CHECK_EQ(map.at("hello"), "world");
        CHECK_EQ(map.at("foo"), "bar");
        CHECK_EQ(map.at("key"), "value");
    }

    SUBCASE("large strings") {
        std::string long_key(1000, 'k');
        std::string long_value(1000, 'v');

        map[long_key] = long_value;
        CHECK_EQ(map.size(), 1);
        CHECK_EQ(map.at(long_key), long_value);
    }

    SUBCASE("many string entries") {
        for (int i = 0; i < 1000; ++i) {
            std::string key = "key_" + std::to_string(i);
            std::string value = "value_" + std::to_string(i * 2);
            map[key] = value;
        }
        CHECK_EQ(map.size(), 1000);

        for (int i = 0; i < 1000; ++i) {
            std::string key = "key_" + std::to_string(i);
            std::string expected = "value_" + std::to_string(i * 2);
            CHECK_EQ(map.at(key), expected);
        }
    }

    SUBCASE("erase string keys") {
        map["a"] = "1";
        map["b"] = "2";
        map["c"] = "3";

        CHECK_EQ(map.erase("b"), 1);
        CHECK_EQ(map.size(), 2);
        CHECK(map.find("b") == map.end());
        CHECK_EQ(map.at("a"), "1");
        CHECK_EQ(map.at("c"), "3");
    }

    SUBCASE("iteration with strings") {
        map["zebra"] = "z";
        map["apple"] = "a";
        map["mango"] = "m";
        map["banana"] = "b";

        std::vector<std::pair<std::string, std::string>> items;
        for (const auto& [k, v] : map) {
            items.emplace_back(k, v);
        }

        CHECK_EQ(items.size(), 4);
        CHECK_EQ(items[0].first, "apple");
        CHECK_EQ(items[1].first, "banana");
        CHECK_EQ(items[2].first, "mango");
        CHECK_EQ(items[3].first, "zebra");
    }
}

TEST_CASE("btree_map::complex_types") {
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
        btree_map<Point, std::string> map;
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
        btree_map<int, Point> map;
        map[1] = Point{10, 20};
        map[2] = Point{30, 40};

        CHECK_EQ(map.at(1).x, 10);
        CHECK_EQ(map.at(1).y, 20);
        CHECK_EQ(map.at(2).x, 30);
        CHECK_EQ(map.at(2).y, 40);
    }
}

TEST_CASE("btree_map::node_handle") {
    btree_map<int, std::string> map;
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

        btree_map<int, std::string> map2;
        map2[10] = "ten";

        auto result = map2.insert(std::move(nh));
        CHECK(result.inserted);
        CHECK_EQ(result.position->first, 2);
        CHECK_EQ(result.position->second, "two");
        CHECK(result.node.empty());
        CHECK_EQ(map2.size(), 2);
    }

    SUBCASE("insert duplicate key") {
        btree_map<int, std::string> map2;
        map2[2] = "TWO";  // Duplicate key

        auto nh = map.extract(2);
        auto result = map2.insert(std::move(nh));

        CHECK_FALSE(result.inserted);
        CHECK_EQ(result.position->second, "TWO");  // Original value preserved
        CHECK_FALSE(result.node.empty());  // Node returned back
        CHECK_EQ(result.node.mapped(), "two");
    }

    SUBCASE("extract_and_get_next") {
        auto it = map.find(2);
        auto [nh, next] = map.extract_and_get_next(it);

        CHECK_FALSE(nh.empty());
        CHECK_EQ(nh.key(), 2);
        CHECK(next != map.end());
        CHECK_EQ(next->first, 3);
        CHECK_EQ(map.size(), 2);
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
        btree_map<int, std::string>::node_type nh;
        CHECK(nh.empty());

        auto result = map.insert(std::move(nh));
        CHECK_FALSE(result.inserted);
        CHECK(result.position == map.end());
        CHECK_EQ(map.size(), 3);
    }
}

TEST_CASE("btree_map::allocator") {
    SUBCASE("default allocator") {
        btree_map<int, int> map;
        auto alloc = map.get_allocator();
        CHECK((std::is_same_v<decltype(alloc), std::allocator<std::pair<const int, int>>>));
    }

    SUBCASE("allocator_type alias") {
        using map_type = btree_map<std::string, int>;
        CHECK((std::is_same_v<map_type::allocator_type, std::allocator<std::pair<const std::string, int>>>));
    }

    SUBCASE("custom allocator template parameter") {
        // Just test that it compiles with a custom allocator
        using custom_alloc = std::allocator<std::pair<const int, std::string>>;
        btree_map<int, std::string, std::less<int>, custom_alloc> map;
        map[1] = "one";
        map[2] = "two";
        CHECK_EQ(map.size(), 2);
        CHECK_EQ(map.at(1), "one");
    }

    SUBCASE("constructor with allocator") {
        std::allocator<std::pair<const int, int>> alloc;
        btree_map<int, int> map(alloc);
        map[1] = 10;
        CHECK_EQ(map.size(), 1);
        CHECK_EQ(map.at(1), 10);
    }

    SUBCASE("constructor with comparator and allocator") {
        std::allocator<std::pair<const int, int>> alloc;
        btree_map<int, int, std::greater<int>> map(std::greater<int>{}, alloc);
        map[1] = 10;
        map[2] = 20;
        CHECK_EQ(map.size(), 2);
        // Check reverse order
        auto it = map.begin();
        CHECK_EQ(it->first, 2);
    }

    SUBCASE("initializer list with allocator") {
        std::allocator<std::pair<const int, std::string>> alloc;
        btree_map<int, std::string> map({{1, "one"}, {2, "two"}}, alloc);
        CHECK_EQ(map.size(), 2);
        CHECK_EQ(map.at(1), "one");
    }

    SUBCASE("copy constructor with allocator") {
        btree_map<int, int> map1;
        map1[1] = 10;
        map1[2] = 20;

        std::allocator<std::pair<const int, int>> alloc;
        btree_map<int, int> map2(map1, alloc);
        CHECK_EQ(map2.size(), 2);
        CHECK_EQ(map2.at(1), 10);
        CHECK_EQ(map2.at(2), 20);
    }

    SUBCASE("move constructor with allocator") {
        btree_map<int, int> map1;
        map1[1] = 10;
        map1[2] = 20;

        std::allocator<std::pair<const int, int>> alloc;
        btree_map<int, int> map2(std::move(map1), alloc);
        CHECK_EQ(map2.size(), 2);
        CHECK_EQ(map2.at(1), 10);
    }

    SUBCASE("range constructor with allocator") {
        std::vector<std::pair<int, std::string>> vec{{1, "one"}, {2, "two"}};
        std::allocator<std::pair<const int, std::string>> alloc;
        btree_map<int, std::string> map(vec.begin(), vec.end(), alloc);
        CHECK_EQ(map.size(), 2);
        CHECK_EQ(map.at(1), "one");
    }

    SUBCASE("custom allocator is actually used") {
        // Reset counters
        test_alloc::alloc_count = 0;
        test_alloc::dealloc_count = 0;

        using Alloc = test_alloc::CountingAllocator<std::pair<const int, int>>;
        {
            btree_map<int, int, std::less<int>, Alloc> map;

            // Insert enough elements to trigger multiple node allocations/splits
            for (int i = 0; i < 500; ++i) {
                map[i] = i;
            }
            CHECK(test_alloc::alloc_count > 0);
            CHECK_EQ(map.size(), 500);

            // Erase elements to trigger node merging/rebalancing and deallocations
            for (int i = 0; i < 250; ++i) {
                map.erase(i);
            }
            CHECK(test_alloc::dealloc_count > 0);
            CHECK_EQ(map.size(), 250);

            // Clear should deallocate remaining nodes
            map.clear();
            CHECK(map.empty());
        }

        // After destructor, all allocations should be matched by deallocations
        CHECK_EQ(test_alloc::alloc_count.load(), test_alloc::dealloc_count.load());
    }
}

TEST_CASE("btree_map::move_semantics") {
    SUBCASE("move construct map") {
        btree_map<std::string, std::string> map1;
        map1["a"] = "1";
        map1["b"] = "2";

        btree_map<std::string, std::string> map2(std::move(map1));
        CHECK_EQ(map2.size(), 2);
        CHECK_EQ(map2.at("a"), "1");
        CHECK_EQ(map2.at("b"), "2");
        CHECK_EQ(map1.size(), 0);  // NOLINT: testing moved-from state
    }

    SUBCASE("move assign map") {
        btree_map<std::string, std::string> map1;
        map1["x"] = "10";

        btree_map<std::string, std::string> map2;
        map2["y"] = "20";

        map2 = std::move(map1);
        CHECK_EQ(map2.size(), 1);
        CHECK_EQ(map2.at("x"), "10");
    }
}

TEST_CASE("btree_map::large_scale") {
    btree_map<int, int> map;
    constexpr int N = 10000;

    SUBCASE("insert many elements") {
        for (int i = 0; i < N; ++i) {
            map.insert(i, i * 2);
        }
        CHECK_EQ(map.size(), N);
    }

    SUBCASE("random access after large insert") {
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

TEST_CASE("btree_map::node_info") {
    // Just verify the compile-time slot calculations
    using Map = btree_map<int, int>;
    CHECK_GT(Map::leaf_slots(), 4);
    CHECK_GT(Map::internal_slots(), 4);

    MESSAGE("Leaf slots: ", Map::leaf_slots());
    MESSAGE("Internal slots: ", Map::internal_slots());
}

TEST_CASE("btree_map::compare_with_std_map") {
    // Verify behavior matches std::map
    btree_map<int, std::string> bmap;
    std::map<int, std::string> smap;

    std::mt19937 rng(789);
    std::uniform_int_distribution<int> key_dist(0, 999);

    // Insert same elements
    for (int i = 0; i < 500; ++i) {
        int key = key_dist(rng);
        std::string value = "value_" + std::to_string(key);
        bmap.insert(key, value);
        smap.insert({key, value});
    }

    CHECK_EQ(bmap.size(), smap.size());

    // Compare contents
    auto bit = bmap.begin();
    auto sit = smap.begin();
    while (bit != bmap.end() && sit != smap.end()) {
        CHECK_EQ(bit->first, sit->first);
        CHECK_EQ(bit->second, sit->second);
        ++bit;
        ++sit;
    }
    CHECK(bit == bmap.end());
    CHECK(sit == smap.end());
}

// ============================================================================
// Iterator Tests (absl-style)
// ============================================================================

TEST_CASE("btree_map::iterator_validity") {
    btree_map<int, int> map;
    for (int i = 0; i < 100; ++i) {
        map[i] = i * 10;
    }

    SUBCASE("iterator equality") {
        auto it1 = map.begin();
        auto it2 = map.begin();
        CHECK(it1 == it2);

        ++it1;
        CHECK(it1 != it2);

        ++it2;
        CHECK(it1 == it2);
    }

    SUBCASE("const_iterator from iterator") {
        btree_map<int, int>::iterator it = map.begin();
        btree_map<int, int>::const_iterator cit = it;
        CHECK(cit == map.cbegin());
    }

    SUBCASE("iterator to end and back") {
        auto it = map.begin();
        size_t count = 0;
        while (it != map.end()) {
            ++it;
            ++count;
        }
        CHECK_EQ(count, map.size());

        // Go backwards using reverse iterator
        auto rit = map.rbegin();
        count = 0;
        while (rit != map.rend()) {
            ++rit;
            ++count;
        }
        CHECK_EQ(count, map.size());
    }

    SUBCASE("iterator increment/decrement") {
        auto it = map.find(50);
        CHECK(it != map.end());
        CHECK_EQ(it->first, 50);

        ++it;
        CHECK_EQ(it->first, 51);

        --it;
        CHECK_EQ(it->first, 50);

        auto it2 = it++;
        CHECK_EQ(it2->first, 50);
        CHECK_EQ(it->first, 51);

        auto it3 = it--;
        CHECK_EQ(it3->first, 51);
        CHECK_EQ(it->first, 50);
    }

    SUBCASE("reverse iterator base") {
        auto rit = map.rbegin();
        ++rit;  // Now points to second-to-last element
        auto it = rit.base();
        CHECK((it == map.end() || it->first == 99));
    }
}

TEST_CASE("btree_map::iterator_edge_cases") {
    SUBCASE("empty map iterators") {
        btree_map<int, int> map;
        CHECK(map.begin() == map.end());
        CHECK(map.cbegin() == map.cend());
        CHECK(map.rbegin() == map.rend());
        CHECK(map.crbegin() == map.crend());
    }

    SUBCASE("single element iteration") {
        btree_map<int, int> map;
        map[42] = 100;

        auto it = map.begin();
        CHECK_EQ(it->first, 42);
        CHECK_EQ(it->second, 100);
        ++it;
        CHECK(it == map.end());

        auto rit = map.rbegin();
        CHECK_EQ(rit->first, 42);
        ++rit;
        CHECK(rit == map.rend());
    }

    SUBCASE("find returns correct iterator") {
        btree_map<int, int> map;
        for (int i = 0; i < 50; ++i) {
            map[i * 2] = i;  // Even numbers only
        }

        // Find existing key
        auto it = map.find(10);
        CHECK(it != map.end());
        CHECK_EQ(it->first, 10);

        // Find non-existing key
        auto it2 = map.find(11);  // Odd number
        CHECK(it2 == map.end());
    }
}

// ============================================================================
// Edge Cases and Stress Tests
// ============================================================================

TEST_CASE("btree_map::stress_random_operations") {
    btree_map<int, int> map;
    std::map<int, int> ref;  // Reference implementation
    std::mt19937 rng(12345);
    std::uniform_int_distribution<int> key_dist(0, 1000);
    std::uniform_int_distribution<int> op_dist(0, 2);  // 0: insert, 1: erase, 2: find

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

        // Periodically verify size
        if (i % 1000 == 0) {
            CHECK_EQ(map.size(), ref.size());
        }
    }

    // Final verification
    CHECK_EQ(map.size(), ref.size());
    auto bit = map.begin();
    auto rit = ref.begin();
    while (bit != map.end()) {
        CHECK_EQ(bit->first, rit->first);
        CHECK_EQ(bit->second, rit->second);
        ++bit;
        ++rit;
    }
}

TEST_CASE("btree_map::stress_sequential_operations") {
    SUBCASE("sequential insert then erase all") {
        btree_map<int, int> map;
        constexpr int N = 5000;

        // Insert
        for (int i = 0; i < N; ++i) {
            map[i] = i * 2;
        }
        CHECK_EQ(map.size(), N);

        // Verify order
        int expected = 0;
        for (auto& [k, v] : map) {
            CHECK_EQ(k, expected);
            CHECK_EQ(v, expected * 2);
            ++expected;
        }

        // Erase all
        for (int i = 0; i < N; ++i) {
            CHECK_EQ(map.erase(i), 1);
        }
        CHECK(map.empty());
    }

    SUBCASE("reverse sequential insert") {
        btree_map<int, int> map;
        constexpr int N = 5000;

        // Insert in reverse
        for (int i = N - 1; i >= 0; --i) {
            map[i] = i;
        }
        CHECK_EQ(map.size(), N);

        // Verify order (should still be ascending)
        int expected = 0;
        for (auto& [k, v] : map) {
            CHECK_EQ(k, expected);
            ++expected;
        }
    }

    SUBCASE("interleaved insert and erase") {
        btree_map<int, int> map;

        for (int round = 0; round < 10; ++round) {
            // Insert 100 elements
            for (int i = round * 100; i < (round + 1) * 100; ++i) {
                map[i] = i;
            }
            // Erase half
            for (int i = round * 100; i < round * 100 + 50; ++i) {
                map.erase(i);
            }
        }

        CHECK_EQ(map.size(), 500);  // 50 remaining per round * 10 rounds
    }
}

// ============================================================================
// Special Type Tests
// ============================================================================

// Note: Types without default constructors are not currently supported as values
// because leaf_node uses a fixed array that requires default construction.
// This is a known limitation compared to absl::btree_map.

// Type with expensive copy (to test move optimization)
struct ExpensiveCopy {
    std::string data;
    static int copy_count;
    static int move_count;

    ExpensiveCopy() : data("default") {}
    ExpensiveCopy(const std::string& s) : data(s) {}
    ExpensiveCopy(const ExpensiveCopy& other) : data(other.data) { ++copy_count; }
    ExpensiveCopy(ExpensiveCopy&& other) noexcept : data(std::move(other.data)) { ++move_count; }
    ExpensiveCopy& operator=(const ExpensiveCopy& other) {
        data = other.data;
        ++copy_count;
        return *this;
    }
    ExpensiveCopy& operator=(ExpensiveCopy&& other) noexcept {
        data = std::move(other.data);
        ++move_count;
        return *this;
    }
};

int ExpensiveCopy::copy_count = 0;
int ExpensiveCopy::move_count = 0;

TEST_CASE("btree_map::expensive_copy_value") {
    ExpensiveCopy::copy_count = 0;
    ExpensiveCopy::move_count = 0;

    btree_map<int, ExpensiveCopy> map;

    // emplace should minimize copies
    map.emplace(1, ExpensiveCopy("one"));
    map.emplace(2, ExpensiveCopy("two"));

    CHECK_EQ(map.size(), 2);
    CHECK_EQ(map.at(1).data, "one");
    CHECK_EQ(map.at(2).data, "two");

    // Check that moves are preferred over copies
    CHECK_GT(ExpensiveCopy::move_count, 0);
}

TEST_CASE("btree_map::string_key_optimizations") {
    btree_map<std::string, int> map;

    // Insert many strings to trigger tree splits
    for (int i = 0; i < 1000; ++i) {
        map["key_" + std::to_string(i)] = i;
    }
    CHECK_EQ(map.size(), 1000);

    // Verify ordering
    std::string prev;
    for (auto& [k, v] : map) {
        CHECK(prev < k);
        prev = k;
    }

    // Find operations
    CHECK(map.contains("key_500"));
    CHECK_FALSE(map.contains("key_9999"));

    // Erase and verify
    map.erase("key_500");
    CHECK_FALSE(map.contains("key_500"));
    CHECK_EQ(map.size(), 999);
}

// ============================================================================
// Merge and Swap Tests
// ============================================================================

TEST_CASE("btree_map::merge_operations") {
    SUBCASE("merge non-overlapping") {
        btree_map<int, int> map1, map2;
        for (int i = 0; i < 50; ++i) {
            map1[i] = i;
            map2[i + 100] = i + 100;
        }

        size_t orig_size = map1.size() + map2.size();
        map1.merge(map2);

        CHECK_EQ(map1.size(), orig_size);
        CHECK(map2.empty());
    }

    SUBCASE("merge with duplicates") {
        btree_map<int, int> map1, map2;
        for (int i = 0; i < 50; ++i) {
            map1[i] = i;
            map2[i + 25] = i + 1000;  // Keys 25-74, overlaps 25-49
        }

        map1.merge(map2);

        // map1 should have 75 elements (0-74)
        CHECK_EQ(map1.size(), 75);
        // map2 should have 25 elements left (25-49 duplicates)
        CHECK_EQ(map2.size(), 25);

        // Verify original values preserved for duplicates
        CHECK_EQ(map1[30], 30);  // Original, not 1005
    }

    SUBCASE("merge with different comparator") {
        btree_map<int, int, std::less<int>> map1;
        btree_map<int, int, std::greater<int>> map2;

        for (int i = 0; i < 20; ++i) {
            map1[i] = i;
            map2[i + 20] = i + 20;
        }

        map1.merge(map2);
        CHECK_EQ(map1.size(), 40);
        CHECK(map2.empty());

        // Verify ordering in map1 (ascending)
        int prev = -1;
        for (auto& [k, v] : map1) {
            CHECK_GT(k, prev);
            prev = k;
        }
    }
}

TEST_CASE("btree_map::swap_operations") {
    btree_map<int, std::string> map1, map2;

    map1[1] = "one";
    map1[2] = "two";
    map2[10] = "ten";

    swap(map1, map2);

    CHECK_EQ(map1.size(), 1);
    CHECK_EQ(map2.size(), 2);
    CHECK_EQ(map1.at(10), "ten");
    CHECK_EQ(map2.at(1), "one");

    // Member swap
    map1.swap(map2);
    CHECK_EQ(map1.size(), 2);
    CHECK_EQ(map2.size(), 1);
}

// ============================================================================
// Emplace and Try_emplace Tests
// ============================================================================

TEST_CASE("btree_map::emplace_variations") {
    btree_map<std::string, std::string> map;

    SUBCASE("emplace new key") {
        auto [it, inserted] = map.emplace("key", "value");
        CHECK(inserted);
        CHECK_EQ(it->first, "key");
        CHECK_EQ(it->second, "value");
    }

    SUBCASE("emplace existing key") {
        map["key"] = "original";
        auto [it, inserted] = map.emplace("key", "new_value");
        CHECK_FALSE(inserted);
        CHECK_EQ(it->second, "original");
    }

    SUBCASE("emplace_hint at correct position") {
        for (int i = 0; i < 100; ++i) {
            map.emplace(std::to_string(i), std::to_string(i));
        }

        auto hint = map.find("050");
        auto it = map.emplace_hint(hint, "051", "inserted");
        CHECK_EQ(it->first, "051");
    }
}

TEST_CASE("btree_map::try_emplace_variations") {
    SUBCASE("try_emplace with piecewise construct") {
        btree_map<int, std::string> map;

        auto [it1, ins1] = map.try_emplace(1, "hello");
        CHECK(ins1);
        CHECK_EQ(it1->second, "hello");

        auto [it2, ins2] = map.try_emplace(1, "world");
        CHECK_FALSE(ins2);
        CHECK_EQ(it2->second, "hello");  // Not modified
    }

    SUBCASE("try_emplace with move key") {
        btree_map<std::string, int> map;

        std::string key = "moveable";
        auto [it, inserted] = map.try_emplace(std::move(key), 42);
        CHECK(inserted);
        // Key may or may not be moved depending on implementation
        CHECK((key.empty() || key == "moveable"));
    }
}

// ============================================================================
// Insert_or_assign Tests
// ============================================================================

TEST_CASE("btree_map::insert_or_assign") {
    btree_map<int, std::string> map;

    SUBCASE("insert new") {
        auto [it, inserted] = map.insert_or_assign(1, "one");
        CHECK(inserted);
        CHECK_EQ(it->second, "one");
    }

    SUBCASE("assign existing") {
        map[1] = "original";
        auto [it, inserted] = map.insert_or_assign(1, "updated");
        CHECK_FALSE(inserted);
        CHECK_EQ(it->second, "updated");
    }

    SUBCASE("with hint") {
        for (int i = 0; i < 10; ++i) {
            map[i] = std::to_string(i);
        }

        auto hint = map.find(5);
        auto it = map.insert_or_assign(hint, 5, "five_updated");
        CHECK_EQ(it->second, "five_updated");
    }
}

// ============================================================================
// Equal_range and Bounds Tests
// ============================================================================

TEST_CASE("btree_map::bounds_operations") {
    btree_map<int, int> map;
    for (int i = 0; i < 100; i += 2) {  // Even numbers 0, 2, 4, ...
        map[i] = i;
    }

    SUBCASE("lower_bound") {
        auto it = map.lower_bound(50);
        CHECK_EQ(it->first, 50);

        auto it2 = map.lower_bound(51);  // Odd, should get next even
        CHECK_EQ(it2->first, 52);

        auto it3 = map.lower_bound(100);  // Past last
        CHECK(it3 == map.end());
    }

    SUBCASE("upper_bound") {
        auto it = map.upper_bound(50);
        CHECK_EQ(it->first, 52);

        auto it2 = map.upper_bound(98);  // Last element
        CHECK(it2 == map.end());
    }

    SUBCASE("equal_range existing") {
        auto [lb, ub] = map.equal_range(50);
        CHECK_EQ(lb->first, 50);
        CHECK_EQ(ub->first, 52);
    }

    SUBCASE("equal_range non-existing") {
        auto [lb, ub] = map.equal_range(51);  // Doesn't exist
        CHECK(lb == ub);
        CHECK_EQ(lb->first, 52);
    }
}

// ============================================================================
// Comparison Operators Tests
// ============================================================================

TEST_CASE("btree_map::comparison_operators") {
    btree_map<int, int> map1, map2, map3;

    for (int i = 0; i < 10; ++i) {
        map1[i] = i;
        map2[i] = i;
        map3[i] = i + 1;  // Different values
    }

    SUBCASE("equality") {
        CHECK(map1 == map2);
        CHECK_FALSE(map1 == map3);
        CHECK(map1 != map3);
    }

    SUBCASE("less than") {
        btree_map<int, int> smaller, larger;
        smaller[1] = 1;
        larger[1] = 2;

        CHECK(smaller < larger);
        CHECK_FALSE(larger < smaller);
        CHECK(smaller <= larger);
        CHECK(larger >= smaller);
    }

    SUBCASE("lexicographic ordering") {
        btree_map<int, int> a, b;
        a[1] = 1;
        a[2] = 2;
        b[1] = 1;
        b[2] = 3;

        CHECK(a < b);  // Same prefix, but a[2] < b[2]
    }

    SUBCASE("size difference") {
        btree_map<int, int> shorter, longer;
        shorter[1] = 1;
        longer[1] = 1;
        longer[2] = 2;

        CHECK(shorter < longer);  // Shorter is less when prefix matches
    }
}

// ============================================================================
// Erase_if Tests
// ============================================================================

TEST_CASE("btree_map::erase_if") {
    btree_map<int, int> map;
    for (int i = 0; i < 100; ++i) {
        map[i] = i;
    }

    SUBCASE("erase even keys") {
        auto erased = erase_if(map, [](const auto& kv) { return kv.first % 2 == 0; });
        CHECK_EQ(erased, 50);
        CHECK_EQ(map.size(), 50);

        for (auto& [k, v] : map) {
            CHECK(k % 2 == 1);
        }
    }

    SUBCASE("erase nothing") {
        auto erased = erase_if(map, [](const auto&) { return false; });
        CHECK_EQ(erased, 0);
        CHECK_EQ(map.size(), 100);
    }

    SUBCASE("erase everything") {
        auto erased = erase_if(map, [](const auto&) { return true; });
        CHECK_EQ(erased, 100);
        CHECK(map.empty());
    }
}

// ============================================================================
// Fuzz Tests - Random operations with verification
// ============================================================================

TEST_CASE("btree_map::fuzz_random_operations") {
    std::mt19937 rng(12345);

    SUBCASE("random int operations - small") {
        btree_map<int, int> map;
        std::map<int, int> reference;

        for (int iter = 0; iter < 10000; ++iter) {
            int op = rng() % 10;
            int key = rng() % 100;
            int value = rng() % 1000;

            switch (op) {
                case 0:
                case 1:
                case 2:
                case 3:  // Insert (40%)
                    map[key] = value;
                    reference[key] = value;
                    break;
                case 4:
                case 5:  // Erase (20%)
                    map.erase(key);
                    reference.erase(key);
                    break;
                case 6:
                case 7: {  // Find (20%)
                    auto it1 = map.find(key);
                    auto it2 = reference.find(key);
                    CHECK_EQ((it1 != map.end()), (it2 != reference.end()));
                    if (it1 != map.end()) {
                        CHECK_EQ(it1->second, it2->second);
                    }
                    break;
                }
                case 8: {  // Lower bound (10%)
                    auto it1 = map.lower_bound(key);
                    auto it2 = reference.lower_bound(key);
                    CHECK_EQ((it1 != map.end()), (it2 != reference.end()));
                    if (it1 != map.end()) {
                        CHECK_EQ(it1->first, it2->first);
                    }
                    break;
                }
                case 9: {  // Upper bound (10%)
                    auto it1 = map.upper_bound(key);
                    auto it2 = reference.upper_bound(key);
                    CHECK_EQ((it1 != map.end()), (it2 != reference.end()));
                    if (it1 != map.end()) {
                        CHECK_EQ(it1->first, it2->first);
                    }
                    break;
                }
            }
        }

        // Verify final state
        CHECK_EQ(map.size(), reference.size());
        auto it1 = map.begin();
        auto it2 = reference.begin();
        while (it1 != map.end()) {
            CHECK_EQ(it1->first, it2->first);
            CHECK_EQ(it1->second, it2->second);
            ++it1;
            ++it2;
        }
    }

    SUBCASE("random int operations - large keys") {
        btree_map<int, int> map;
        std::map<int, int> reference;

        for (int iter = 0; iter < 50000; ++iter) {
            int op = rng() % 10;
            int key = rng() % 100000;
            int value = rng() % 1000000;

            switch (op) {
                case 0:
                case 1:
                case 2:
                case 3:
                    map[key] = value;
                    reference[key] = value;
                    break;
                case 4:
                case 5:
                    map.erase(key);
                    reference.erase(key);
                    break;
                case 6:
                case 7: {
                    auto it1 = map.find(key);
                    auto it2 = reference.find(key);
                    CHECK_EQ((it1 != map.end()), (it2 != reference.end()));
                    break;
                }
                default:
                    break;
            }
        }
        CHECK_EQ(map.size(), reference.size());
    }

    SUBCASE("random string operations") {
        btree_map<std::string, int> map;
        std::map<std::string, int> reference;

        for (int iter = 0; iter < 10000; ++iter) {
            int op = rng() % 10;
            std::string key = "key_" + std::to_string(rng() % 500);
            int value = rng() % 1000;

            switch (op) {
                case 0:
                case 1:
                case 2:
                case 3:
                    map[key] = value;
                    reference[key] = value;
                    break;
                case 4:
                case 5:
                    map.erase(key);
                    reference.erase(key);
                    break;
                case 6:
                case 7: {
                    auto it1 = map.find(key);
                    auto it2 = reference.find(key);
                    CHECK_EQ((it1 != map.end()), (it2 != reference.end()));
                    if (it1 != map.end()) {
                        CHECK_EQ(it1->second, it2->second);
                    }
                    break;
                }
                default:
                    break;
            }
        }

        CHECK_EQ(map.size(), reference.size());
        auto it1 = map.begin();
        auto it2 = reference.begin();
        while (it1 != map.end()) {
            CHECK_EQ(it1->first, it2->first);
            CHECK_EQ(it1->second, it2->second);
            ++it1;
            ++it2;
        }
    }
}

TEST_CASE("btree_map::fuzz_iterator_stability") {
    std::mt19937 rng(54321);

    SUBCASE("iteration during modification") {
        btree_map<int, int> map;
        for (int i = 0; i < 1000; ++i) {
            map[i] = i;
        }

        // Iterate and collect keys to erase
        std::vector<int> to_erase;
        for (auto& [k, v] : map) {
            if (k % 3 == 0) to_erase.push_back(k);
        }

        // Erase collected keys
        for (int k : to_erase) {
            map.erase(k);
        }

        // Verify remaining
        for (auto& [k, v] : map) {
            CHECK(k % 3 != 0);
            CHECK_EQ(k, v);
        }
    }

    SUBCASE("erase via iterator") {
        btree_map<int, int> map;
        for (int i = 0; i < 500; ++i) {
            map[i] = i;
        }

        // Erase every other element using iterator
        for (auto it = map.begin(); it != map.end();) {
            if (it->first % 2 == 0) {
                it = map.erase(it);
            } else {
                ++it;
            }
        }

        CHECK_EQ(map.size(), 250);
        for (auto& [k, v] : map) {
            CHECK(k % 2 == 1);
        }
    }
}

TEST_CASE("btree_map::fuzz_edge_cases") {
    std::mt19937 rng(99999);

    SUBCASE("insert and erase same key repeatedly") {
        btree_map<int, int> map;
        for (int iter = 0; iter < 10000; ++iter) {
            int key = rng() % 10;  // Very small key range
            int value = rng();
            if (rng() % 2) {
                map[key] = value;
            } else {
                map.erase(key);
            }
        }
        // Just verify no crash and valid state
        size_t count = 0;
        int prev = -1;
        for (auto& [k, v] : map) {
            CHECK(k > prev);
            prev = k;
            ++count;
        }
        CHECK_EQ(count, map.size());
    }

    SUBCASE("sequential insert then random erase") {
        btree_map<int, int> map;
        std::set<int> remaining;

        // Sequential insert
        for (int i = 0; i < 10000; ++i) {
            map[i] = i;
            remaining.insert(i);
        }

        // Random erase
        std::vector<int> keys(remaining.begin(), remaining.end());
        std::shuffle(keys.begin(), keys.end(), rng);

        for (int i = 0; i < 5000; ++i) {
            map.erase(keys[i]);
            remaining.erase(keys[i]);
        }

        // Verify
        CHECK_EQ(map.size(), remaining.size());
        for (int k : remaining) {
            CHECK(map.contains(k));
            CHECK_EQ(map[k], k);
        }
    }

    SUBCASE("alternating insert/erase pattern") {
        btree_map<int, int> map;
        std::map<int, int> reference;

        for (int round = 0; round < 100; ++round) {
            // Insert phase
            for (int i = 0; i < 100; ++i) {
                int key = rng() % 1000;
                int value = rng();
                map[key] = value;
                reference[key] = value;
            }

            // Erase phase
            for (int i = 0; i < 50; ++i) {
                int key = rng() % 1000;
                map.erase(key);
                reference.erase(key);
            }
        }

        CHECK_EQ(map.size(), reference.size());
    }

    SUBCASE("clear and refill") {
        btree_map<int, int> map;

        for (int round = 0; round < 100; ++round) {
            // Fill
            for (int i = 0; i < 100; ++i) {
                map[rng() % 500] = rng();
            }

            // Clear
            map.clear();
            CHECK(map.empty());
            CHECK_EQ(map.size(), 0);
            CHECK(map.begin() == map.end());
        }
    }
}

TEST_CASE("btree_map::fuzz_merge_and_swap") {
    std::mt19937 rng(11111);

    SUBCASE("random merge operations") {
        for (int trial = 0; trial < 10; ++trial) {
            btree_map<int, int> map1, map2;
            std::map<int, int> ref1, ref2;

            // Fill both maps with distinct ranges to simplify verification
            for (int i = 0; i < 200; ++i) {
                int k1 = rng() % 500;        // Keys 0-499
                int k2 = 500 + rng() % 500;  // Keys 500-999 (no overlap)
                map1[k1] = k1;
                map2[k2] = k2;
                ref1[k1] = k1;
                ref2[k2] = k2;
            }

            size_t expected_size = ref1.size() + ref2.size();

            map1.merge(map2);

            // With no overlap, map1 should have all elements and map2 should be empty
            CHECK_EQ(map1.size(), expected_size);
            CHECK(map2.empty());
        }
    }

    SUBCASE("swap preserves data") {
        btree_map<int, int> map1, map2;

        for (int i = 0; i < 100; ++i) map1[i] = i;
        for (int i = 100; i < 200; ++i) map2[i] = i;

        size_t size1 = map1.size();
        size_t size2 = map2.size();

        map1.swap(map2);

        CHECK_EQ(map1.size(), size2);
        CHECK_EQ(map2.size(), size1);

        // Verify contents swapped
        CHECK(map1.contains(150));
        CHECK(!map1.contains(50));
        CHECK(map2.contains(50));
        CHECK(!map2.contains(150));
    }
}

// =============================================================================
// PMR (Polymorphic Memory Resource) Tests
// =============================================================================
#ifdef BTREE_HAS_PMR

TEST_CASE("btree_map::pmr") {
    SUBCASE("basic operations with monotonic_buffer_resource") {
        std::array<std::byte, 4096> buffer;
        std::pmr::monotonic_buffer_resource pool{buffer.data(), buffer.size()};

        stdb::pmr::btree_map<int, int> map{&pool};

        // Insert
        map[1] = 10;
        map[2] = 20;
        map[3] = 30;

        CHECK_EQ(map.size(), 3);
        CHECK_EQ(map.at(1), 10);
        CHECK_EQ(map.at(2), 20);
        CHECK_EQ(map.at(3), 30);

        // Find
        auto it = map.find(2);
        CHECK(it != map.end());
        CHECK_EQ(it->second, 20);

        // Erase
        map.erase(2);
        CHECK_EQ(map.size(), 2);
        CHECK(!map.contains(2));
    }

    SUBCASE("with unsynchronized_pool_resource") {
        std::pmr::unsynchronized_pool_resource pool;

        stdb::pmr::btree_map<int, std::pmr::string> map{&pool};

        map[1] = "one";
        map[2] = "two";
        map[3] = "three";

        CHECK_EQ(map.size(), 3);
        CHECK_EQ(map.at(1), "one");
        CHECK_EQ(map.at(2), "two");
        CHECK_EQ(map.at(3), "three");
    }

    SUBCASE("copy and move with pmr") {
        std::pmr::unsynchronized_pool_resource pool1;
        std::pmr::unsynchronized_pool_resource pool2;

        stdb::pmr::btree_map<int, int> map1{&pool1};
        for (int i = 0; i < 100; ++i) {
            map1[i] = i * 10;
        }

        // Copy (uses same allocator)
        stdb::pmr::btree_map<int, int> map2{map1};
        CHECK_EQ(map2.size(), 100);
        CHECK_EQ(map2.at(50), 500);

        // Move
        stdb::pmr::btree_map<int, int> map3{std::move(map1)};
        CHECK_EQ(map3.size(), 100);
        CHECK_EQ(map3.at(50), 500);
    }

    SUBCASE("initializer list with pmr") {
        std::pmr::unsynchronized_pool_resource pool;

        stdb::pmr::btree_map<int, int> map{{1, 10}, {2, 20}, {3, 30}};

        CHECK_EQ(map.size(), 3);
        CHECK_EQ(map.at(1), 10);
    }

    SUBCASE("pmr btree_map_compact") {
        std::array<std::byte, 8192> buffer;
        std::pmr::monotonic_buffer_resource pool{buffer.data(), buffer.size()};

        stdb::pmr::btree_map_compact<int, int> map{&pool};

        for (int i = 0; i < 50; ++i) {
            map[i] = i;
        }

        CHECK_EQ(map.size(), 50);
        for (int i = 0; i < 50; ++i) {
            CHECK_EQ(map.at(i), i);
        }
    }
}

TEST_CASE("btree_set::pmr") {
    SUBCASE("basic operations with monotonic_buffer_resource") {
        std::array<std::byte, 4096> buffer;
        std::pmr::monotonic_buffer_resource pool{buffer.data(), buffer.size()};

        stdb::pmr::btree_set<int> set{&pool};

        // Insert
        set.insert(1);
        set.insert(2);
        set.insert(3);

        CHECK_EQ(set.size(), 3);
        CHECK(set.contains(1));
        CHECK(set.contains(2));
        CHECK(set.contains(3));

        // Find
        auto it = set.find(2);
        CHECK(it != set.end());
        CHECK_EQ(*it, 2);

        // Erase
        set.erase(2);
        CHECK_EQ(set.size(), 2);
        CHECK(!set.contains(2));
    }

    SUBCASE("with unsynchronized_pool_resource") {
        std::pmr::unsynchronized_pool_resource pool;

        stdb::pmr::btree_set<std::pmr::string> set{&pool};

        set.insert("one");
        set.insert("two");
        set.insert("three");

        CHECK_EQ(set.size(), 3);
        CHECK(set.contains("one"));
        CHECK(set.contains("two"));
        CHECK(set.contains("three"));
    }

    SUBCASE("pmr btree_set_compact") {
        std::array<std::byte, 8192> buffer;
        std::pmr::monotonic_buffer_resource pool{buffer.data(), buffer.size()};

        stdb::pmr::btree_set_compact<int> set{&pool};

        for (int i = 0; i < 50; ++i) {
            set.insert(i);
        }

        CHECK_EQ(set.size(), 50);
        for (int i = 0; i < 50; ++i) {
            CHECK(set.contains(i));
        }
    }
}

#endif  // BTREE_HAS_PMR

// ============================================================================
// Bug Fix Tests - Covering fixes for line 4886 and lines 3741/3753
// ============================================================================

TEST_CASE("btree_map::node_handle_insert_with_hint") {
    btree_map<int, std::string> map;

    // Populate map with some initial data
    for (int i = 0; i < 100; i += 10) {
        map[i] = "value_" + std::to_string(i);
    }

    SUBCASE("insert node handle with hint - correct position") {
        // Extract a node
        auto nh = map.extract(50);
        CHECK_FALSE(nh.empty());
        CHECK_EQ(nh.key(), 50);
        CHECK_EQ(map.size(), 9);

        // Create another map and insert with hint
        btree_map<int, std::string> map2;
        for (int i = 0; i < 10; ++i) {
            map2[i * 5] = "other_" + std::to_string(i * 5);
        }

        // Insert with hint (using hint parameter that was previously unused)
        auto hint = map2.find(45);  // Hint near where 50 should go
        auto it = map2.insert(hint, std::move(nh));

        CHECK(nh.empty());  // Node handle should be empty after successful insertion
        CHECK_EQ(it->first, 50);
        CHECK_EQ(it->second, "value_50");
        CHECK_EQ(map2.size(), 11);
        CHECK(map2.contains(50));
    }

    SUBCASE("insert node handle with hint - duplicate key") {
        auto nh = map.extract(40);
        CHECK_FALSE(nh.empty());

        btree_map<int, std::string> map2;
        map2[40] = "duplicate";  // Same key
        map2[50] = "other";

        auto hint = map2.find(50);
        auto it = map2.insert(hint, std::move(nh));

        // Should return iterator to existing element
        // Note: with hint version, node handle is consumed even if insertion fails
        CHECK_EQ(it->first, 40);
        CHECK_EQ(it->second, "duplicate");  // Original value preserved
        CHECK(nh.empty());  // Node handle is consumed
        CHECK_EQ(map2.size(), 2);  // Size unchanged
    }

    SUBCASE("insert empty node handle with hint") {
        btree_map<int, std::string>::node_type nh;  // Empty node handle
        CHECK(nh.empty());

        auto hint = map.find(50);
        auto it = map.insert(hint, std::move(nh));

        CHECK(it == map.end());  // Should return end() for empty node handle
        CHECK_EQ(map.size(), 10);  // Size unchanged
    }

    SUBCASE("insert node handle with hint - end hint") {
        auto nh = map.extract(90);

        btree_map<int, std::string> map2;
        for (int i = 0; i < 5; ++i) {
            map2[i] = "val_" + std::to_string(i);
        }

        // Use end() as hint
        auto it = map2.insert(map2.end(), std::move(nh));

        CHECK(nh.empty());
        CHECK_EQ(it->first, 90);
        CHECK_EQ(it->second, "value_90");
        CHECK_EQ(map2.size(), 6);
    }
}

TEST_CASE("btree_map::copy_constructor_comprehensive") {
    SUBCASE("copy constructor with large map - verifies member init order") {
        btree_map<int, std::string> original;

        // Insert enough elements to trigger multiple node allocations and splits
        // This ensures that _size and _comp are both used during copy construction
        for (int i = 0; i < 1000; ++i) {
            original[i] = "value_" + std::to_string(i * 2);
        }

        CHECK_EQ(original.size(), 1000);

        // Copy construct - this tests the fixed initialization order (line 3741)
        btree_map<int, std::string> copy(original);

        // Verify size is correct
        CHECK_EQ(copy.size(), 1000);
        CHECK_EQ(copy.size(), original.size());

        // Verify all elements copied correctly
        for (int i = 0; i < 1000; ++i) {
            CHECK(copy.contains(i));
            CHECK_EQ(copy.at(i), "value_" + std::to_string(i * 2));
            CHECK_EQ(copy.at(i), original.at(i));
        }

        // Verify ordering is preserved
        int prev = -1;
        for (const auto& [k, v] : copy) {
            CHECK_GT(k, prev);
            prev = k;
        }

        // Modify copy and ensure original is unchanged
        copy[500] = "modified";
        copy[1001] = "new_element";
        copy.erase(100);

        CHECK_EQ(original.at(500), "value_1000");  // Unchanged
        CHECK_FALSE(original.contains(1001));
        CHECK(original.contains(100));
        CHECK_EQ(original.size(), 1000);
        CHECK_EQ(copy.size(), 1000);  // 1000 - 1 (erased) + 1 (added) = 1000
    }

    SUBCASE("copy constructor with custom comparator - verifies _comp initialization") {
        btree_map<int, int, std::greater<int>> original(std::greater<int>{});

        // Insert in ascending order, but map should store in descending order
        for (int i = 0; i < 500; ++i) {
            original[i] = i * 10;
        }

        // Copy construct - ensures comparator is copied correctly (line 3741)
        btree_map<int, int, std::greater<int>> copy(original);

        CHECK_EQ(copy.size(), 500);

        // Verify ordering is descending (comparator working correctly)
        int prev = 1000;  // Start with large number
        for (const auto& [k, v] : copy) {
            CHECK_LT(k, prev);  // Should be descending
            prev = k;
        }

        // Verify first element is largest
        auto it = copy.begin();
        CHECK_EQ(it->first, 499);
    }

    SUBCASE("copy constructor with allocator - verifies line 3753 fix") {
        btree_map<int, int> original;

        for (int i = 0; i < 200; ++i) {
            original[i] = i * 3;
        }

        std::allocator<std::pair<const int, int>> alloc;

        // Copy construct with allocator - tests line 3753 initialization order
        btree_map<int, int> copy(original, alloc);

        CHECK_EQ(copy.size(), 200);
        CHECK_EQ(copy.size(), original.size());

        // Verify all elements
        for (int i = 0; i < 200; ++i) {
            CHECK_EQ(copy.at(i), i * 3);
        }
    }

    SUBCASE("copy empty map - edge case") {
        btree_map<int, int> original;

        btree_map<int, int> copy(original);

        CHECK(copy.empty());
        CHECK_EQ(copy.size(), 0);
        CHECK(copy.begin() == copy.end());
    }

    SUBCASE("copy single element map - edge case") {
        btree_map<int, std::string> original;
        original[42] = "answer";

        btree_map<int, std::string> copy(original);

        CHECK_EQ(copy.size(), 1);
        CHECK(copy.contains(42));
        CHECK_EQ(copy.at(42), "answer");

        // Modify copy
        copy[42] = "modified";
        CHECK_EQ(original.at(42), "answer");  // Original unchanged
    }
}

// ============================================================================
// PMR (Polymorphic Memory Resource) Tests
// ============================================================================

#ifdef BTREE_HAS_PMR

TEST_CASE("btree_map::pmr") {
    SUBCASE("basic operations with monotonic_buffer_resource") {
        std::array<std::byte, 8192> buffer;
        std::pmr::monotonic_buffer_resource resource(buffer.data(), buffer.size(),
                                                     std::pmr::null_memory_resource());

        stdb::pmr::btree_map<int, std::string> map(&resource);

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
        stdb::pmr::btree_map<int, int> map(&resource);

        CHECK(map.empty());
        CHECK_EQ(map.size(), 0);

        map[42] = 100;
        CHECK_EQ(map.size(), 1);
        CHECK_EQ(map[42], 100);
    }

    SUBCASE("allocator-extended copy constructor") {
        std::pmr::monotonic_buffer_resource resource1;
        std::pmr::monotonic_buffer_resource resource2;

        stdb::pmr::btree_map<int, int> map1(&resource1);
        map1[1] = 10;
        map1[2] = 20;

        stdb::pmr::btree_map<int, int> map2(map1, &resource2);

        CHECK_EQ(map2.size(), 2);
        CHECK_EQ(map2[1], 10);
        CHECK_EQ(map2[2], 20);
        CHECK_EQ(map2.get_allocator().resource(), &resource2);
    }

    SUBCASE("allocator-extended move constructor - same resource") {
        std::pmr::monotonic_buffer_resource resource;

        stdb::pmr::btree_map<int, int> map1(&resource);
        map1[1] = 10;
        map1[2] = 20;

        stdb::pmr::btree_map<int, int> map2(std::move(map1), &resource);

        CHECK_EQ(map2.size(), 2);
        CHECK_EQ(map2[1], 10);
        CHECK_EQ(map2[2], 20);
        CHECK(map1.empty());  // Resources were stolen
    }

    SUBCASE("allocator-extended move constructor - different resource") {
        std::pmr::monotonic_buffer_resource resource1;
        std::pmr::monotonic_buffer_resource resource2;

        stdb::pmr::btree_map<int, int> map1(&resource1);
        map1[1] = 10;
        map1[2] = 20;

        stdb::pmr::btree_map<int, int> map2(std::move(map1), &resource2);

        CHECK_EQ(map2.size(), 2);
        CHECK_EQ(map2[1], 10);
        CHECK_EQ(map2[2], 20);
        CHECK_EQ(map2.get_allocator().resource(), &resource2);
    }

    SUBCASE("copy assignment - keeps allocator") {
        std::pmr::monotonic_buffer_resource resource1;
        std::pmr::monotonic_buffer_resource resource2;

        stdb::pmr::btree_map<int, int> map1(&resource1);
        map1[1] = 10;
        map1[2] = 20;

        stdb::pmr::btree_map<int, int> map2(&resource2);
        map2[3] = 30;

        map2 = map1;

        CHECK_EQ(map2.size(), 2);
        CHECK_EQ(map2[1], 10);
        CHECK_EQ(map2[2], 20);
        CHECK_EQ(map2.get_allocator().resource(), &resource2);  // Kept original allocator
    }

    SUBCASE("move assignment - same resource") {
        std::pmr::monotonic_buffer_resource resource;

        stdb::pmr::btree_map<int, int> map1(&resource);
        map1[1] = 10;
        map1[2] = 20;

        stdb::pmr::btree_map<int, int> map2(&resource);
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

        stdb::pmr::btree_map<int, int> map1(&resource1);
        map1[1] = 10;
        map1[2] = 20;

        stdb::pmr::btree_map<int, int> map2(&resource2);
        map2[3] = 30;

        map2 = std::move(map1);

        CHECK_EQ(map2.size(), 2);
        CHECK_EQ(map2[1], 10);
        CHECK_EQ(map2[2], 20);
        CHECK_EQ(map2.get_allocator().resource(), &resource2);  // Kept original allocator
    }

    SUBCASE("string keys with PMR") {
        std::pmr::monotonic_buffer_resource resource;
        stdb::pmr::btree_map<std::string, int> map(&resource);

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

    SUBCASE("btree_set with PMR") {
        std::pmr::monotonic_buffer_resource resource;
        stdb::pmr::btree_set<int> set(&resource);

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

#endif  // BTREE_HAS_PMR

}  // namespace stdb::container

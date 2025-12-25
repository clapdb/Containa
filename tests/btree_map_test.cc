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

#include <map>
#include <random>
#include <string>
#include <vector>

#include "doctest/doctest/doctest.h"

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

}  // namespace stdb::container

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

#include <map>
#include <random>
#include <set>
#include <string>
#include <vector>

#include "doctest/doctest/doctest.h"

namespace stdb::container {

// ============================================================================
// skiplist_map tests
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
    }

    SUBCASE("move constructor") {
        skiplist_map<int, int> map1{{1, 10}, {2, 20}};
        skiplist_map<int, int> map2(std::move(map1));
        CHECK_EQ(map2.size(), 2);
        CHECK(map1.empty());
    }

    SUBCASE("copy assignment") {
        skiplist_map<int, int> map1{{1, 10}, {2, 20}};
        skiplist_map<int, int> map2;
        map2 = map1;
        CHECK_EQ(map2.size(), 2);
        CHECK_EQ(map2.at(1), 10);
    }

    SUBCASE("move assignment") {
        skiplist_map<int, int> map1{{1, 10}, {2, 20}};
        skiplist_map<int, int> map2;
        map2 = std::move(map1);
        CHECK_EQ(map2.size(), 2);
        CHECK(map1.empty());
    }
}

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
}

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
}

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
}

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
}

TEST_CASE("skiplist_map::find") {
    skiplist_map<int, int> map{{1, 10}, {2, 20}, {3, 30}};

    SUBCASE("find existing key") {
        auto it = map.find(2);
        CHECK(it != map.end());
        CHECK_EQ(it->first, 2);
        CHECK_EQ(it->second, 20);
    }

    SUBCASE("find non-existing key") {
        auto it = map.find(42);
        CHECK(it == map.end());
    }

    SUBCASE("const find") {
        const auto& cmap = map;
        auto it = cmap.find(2);
        CHECK(it != cmap.end());
        CHECK_EQ(it->second, 20);
    }
}

TEST_CASE("skiplist_map::contains_count") {
    skiplist_map<int, int> map{{1, 10}, {2, 20}, {3, 30}};

    SUBCASE("contains existing key") {
        CHECK(map.contains(2));
    }

    SUBCASE("contains non-existing key") {
        CHECK_FALSE(map.contains(42));
    }

    SUBCASE("count existing key") {
        CHECK_EQ(map.count(2), 1);
    }

    SUBCASE("count non-existing key") {
        CHECK_EQ(map.count(42), 0);
    }
}

TEST_CASE("skiplist_map::operator[]") {
    SUBCASE("access existing element") {
        skiplist_map<int, int> map{{1, 10}};
        CHECK_EQ(map[1], 10);
    }

    SUBCASE("insert via operator[]") {
        skiplist_map<int, int> map;
        map[1] = 10;
        CHECK_EQ(map.size(), 1);
        CHECK_EQ(map[1], 10);
    }

    SUBCASE("modify via operator[]") {
        skiplist_map<int, int> map{{1, 10}};
        map[1] = 20;
        CHECK_EQ(map[1], 20);
    }
}

TEST_CASE("skiplist_map::at") {
    skiplist_map<int, int> map{{1, 10}, {2, 20}};

    SUBCASE("at existing key") {
        CHECK_EQ(map.at(1), 10);
    }

    SUBCASE("at non-existing key throws") {
        CHECK_THROWS_AS(map.at(42), std::out_of_range);
    }

    SUBCASE("const at") {
        const auto& cmap = map;
        CHECK_EQ(cmap.at(1), 10);
    }
}

TEST_CASE("skiplist_map::erase") {
    SUBCASE("erase by key") {
        skiplist_map<int, int> map{{1, 10}, {2, 20}, {3, 30}};
        auto count = map.erase(2);
        CHECK_EQ(count, 1);
        CHECK_EQ(map.size(), 2);
        CHECK_FALSE(map.contains(2));
    }

    SUBCASE("erase non-existing key") {
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
}

TEST_CASE("skiplist_map::clear") {
    skiplist_map<int, int> map{{1, 10}, {2, 20}, {3, 30}};
    map.clear();
    CHECK(map.empty());
    CHECK_EQ(map.size(), 0);

    // Can insert after clear
    map.insert(4, 40);
    CHECK_EQ(map.size(), 1);
}

TEST_CASE("skiplist_map::bounds") {
    skiplist_map<int, int> map{{10, 100}, {20, 200}, {30, 300}, {40, 400}};

    SUBCASE("lower_bound") {
        auto it = map.lower_bound(20);
        CHECK_EQ(it->first, 20);

        it = map.lower_bound(25);
        CHECK_EQ(it->first, 30);

        it = map.lower_bound(5);
        CHECK_EQ(it->first, 10);

        it = map.lower_bound(50);
        CHECK(it == map.end());
    }

    SUBCASE("upper_bound") {
        auto it = map.upper_bound(20);
        CHECK_EQ(it->first, 30);

        it = map.upper_bound(25);
        CHECK_EQ(it->first, 30);

        it = map.upper_bound(40);
        CHECK(it == map.end());
    }

    SUBCASE("equal_range") {
        auto [first, second] = map.equal_range(20);
        CHECK_EQ(first->first, 20);
        CHECK_EQ(second->first, 30);

        auto [first2, second2] = map.equal_range(25);
        CHECK(first2 == second2);
        CHECK_EQ(first2->first, 30);
    }
}

TEST_CASE("skiplist_map::iteration") {
    skiplist_map<int, int> map{{3, 30}, {1, 10}, {2, 20}};

    SUBCASE("forward iteration") {
        std::vector<int> keys;
        for (const auto& [k, v] : map) {
            keys.push_back(k);
        }
        CHECK_EQ(keys, std::vector<int>{1, 2, 3});
    }

    SUBCASE("iterator modification") {
        for (auto& [k, v] : map) {
            v *= 2;
        }
        CHECK_EQ(map.at(1), 20);
        CHECK_EQ(map.at(2), 40);
        CHECK_EQ(map.at(3), 60);
    }
}

TEST_CASE("skiplist_map::swap") {
    skiplist_map<int, int> map1{{1, 10}, {2, 20}};
    skiplist_map<int, int> map2{{3, 30}, {4, 40}, {5, 50}};

    map1.swap(map2);

    CHECK_EQ(map1.size(), 3);
    CHECK_EQ(map2.size(), 2);
    CHECK(map1.contains(3));
    CHECK(map2.contains(1));
}

TEST_CASE("skiplist_map::comparison") {
    SUBCASE("equal maps") {
        skiplist_map<int, int> map1{{1, 10}, {2, 20}};
        skiplist_map<int, int> map2{{1, 10}, {2, 20}};
        CHECK(map1 == map2);
        CHECK_FALSE(map1 != map2);
    }

    SUBCASE("unequal maps - different size") {
        skiplist_map<int, int> map1{{1, 10}, {2, 20}};
        skiplist_map<int, int> map2{{1, 10}};
        CHECK(map1 != map2);
    }

    SUBCASE("unequal maps - different values") {
        skiplist_map<int, int> map1{{1, 10}, {2, 20}};
        skiplist_map<int, int> map2{{1, 10}, {2, 30}};
        CHECK(map1 != map2);
    }

    SUBCASE("lexicographical ordering") {
        skiplist_map<int, int> map1{{1, 10}, {2, 20}};
        skiplist_map<int, int> map2{{1, 10}, {3, 30}};
        CHECK(map1 < map2);
        CHECK(map1 <= map2);
        CHECK(map2 > map1);
        CHECK(map2 >= map1);
    }
}

TEST_CASE("skiplist_map::extract") {
    skiplist_map<int, int> map{{1, 10}, {2, 20}, {3, 30}};

    SUBCASE("extract by key") {
        auto nh = map.extract(2);
        CHECK(!nh.empty());
        CHECK_EQ(nh.key(), 2);
        CHECK_EQ(nh.mapped(), 20);
        CHECK_EQ(map.size(), 2);
        CHECK_FALSE(map.contains(2));
    }

    SUBCASE("extract non-existing key") {
        auto nh = map.extract(42);
        CHECK(nh.empty());
        CHECK_EQ(map.size(), 3);
    }

    SUBCASE("insert extracted node") {
        auto nh = map.extract(2);
        skiplist_map<int, int> map2;
        auto result = map2.insert(std::move(nh));
        CHECK(result.inserted);
        CHECK_EQ(map2.at(2), 20);
    }
}

TEST_CASE("skiplist_map::merge") {
    SUBCASE("merge non-overlapping") {
        skiplist_map<int, int> map1{{1, 10}, {2, 20}};
        skiplist_map<int, int> map2{{3, 30}, {4, 40}};
        map1.merge(map2);
        CHECK_EQ(map1.size(), 4);
        CHECK(map2.empty());
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
}

TEST_CASE("skiplist_map::large_dataset") {
    const int N = 10000;
    skiplist_map<int, int> map;

    SUBCASE("insert many elements") {
        for (int i = 0; i < N; ++i) {
            map.insert(i, i * 2);
        }
        CHECK_EQ(map.size(), N);

        // Verify all elements
        for (int i = 0; i < N; ++i) {
            CHECK_EQ(map.at(i), i * 2);
        }
    }

    SUBCASE("random operations") {
        std::mt19937 rng(42);
        std::uniform_int_distribution<int> dist(0, N - 1);

        // Insert
        for (int i = 0; i < N; ++i) {
            map.insert(i, i);
        }

        // Random lookups
        for (int i = 0; i < N; ++i) {
            int key = dist(rng);
            CHECK(map.contains(key));
        }

        // Random erases
        std::set<int> erased;
        for (int i = 0; i < N / 2; ++i) {
            int key = dist(rng);
            if (erased.find(key) == erased.end()) {
                map.erase(key);
                erased.insert(key);
            }
        }

        // Verify
        for (int i = 0; i < N; ++i) {
            if (erased.find(i) != erased.end()) {
                CHECK_FALSE(map.contains(i));
            } else {
                CHECK(map.contains(i));
            }
        }
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
        std::vector<int> vec{3, 1, 2};
        skiplist_set<int> set(vec.begin(), vec.end());
        CHECK_EQ(set.size(), 3);
    }

    SUBCASE("copy constructor") {
        skiplist_set<int> set1{1, 2, 3};
        skiplist_set<int> set2(set1);
        CHECK_EQ(set2.size(), 3);
        CHECK(set2.contains(2));
    }

    SUBCASE("move constructor") {
        skiplist_set<int> set1{1, 2, 3};
        skiplist_set<int> set2(std::move(set1));
        CHECK_EQ(set2.size(), 3);
        CHECK(set1.empty());
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

        // Check ordering
        int expected = 0;
        for (int val : set) {
            CHECK_EQ(val, expected);
            ++expected;
        }
    }
}

TEST_CASE("skiplist_set::find") {
    skiplist_set<int> set{1, 2, 3, 4, 5};

    SUBCASE("find existing") {
        auto it = set.find(3);
        CHECK(it != set.end());
        CHECK_EQ(*it, 3);
    }

    SUBCASE("find non-existing") {
        auto it = set.find(42);
        CHECK(it == set.end());
    }
}

TEST_CASE("skiplist_set::erase") {
    SUBCASE("erase by value") {
        skiplist_set<int> set{1, 2, 3};
        auto count = set.erase(2);
        CHECK_EQ(count, 1);
        CHECK_EQ(set.size(), 2);
        CHECK_FALSE(set.contains(2));
    }

    SUBCASE("erase by iterator") {
        skiplist_set<int> set{1, 2, 3};
        auto it = set.find(2);
        auto next = set.erase(it);
        CHECK_EQ(set.size(), 2);
        CHECK_EQ(*next, 3);
    }
}

TEST_CASE("skiplist_set::bounds") {
    skiplist_set<int> set{10, 20, 30, 40};

    SUBCASE("lower_bound") {
        auto it = set.lower_bound(20);
        CHECK_EQ(*it, 20);

        it = set.lower_bound(25);
        CHECK_EQ(*it, 30);
    }

    SUBCASE("upper_bound") {
        auto it = set.upper_bound(20);
        CHECK_EQ(*it, 30);
    }

    SUBCASE("equal_range") {
        auto [first, second] = set.equal_range(20);
        CHECK_EQ(*first, 20);
        CHECK_EQ(*second, 30);
    }
}

TEST_CASE("skiplist_set::comparison") {
    SUBCASE("equal sets") {
        skiplist_set<int> set1{1, 2, 3};
        skiplist_set<int> set2{1, 2, 3};
        CHECK(set1 == set2);
    }

    SUBCASE("unequal sets") {
        skiplist_set<int> set1{1, 2, 3};
        skiplist_set<int> set2{1, 2, 4};
        CHECK(set1 != set2);
        CHECK(set1 < set2);
    }
}

TEST_CASE("skiplist_set::merge") {
    skiplist_set<int> set1{1, 2};
    skiplist_set<int> set2{2, 3};

    set1.merge(set2);
    CHECK_EQ(set1.size(), 3);
    CHECK(set1.contains(1));
    CHECK(set1.contains(2));
    CHECK(set1.contains(3));
    CHECK_EQ(set2.size(), 1);  // Duplicate not moved
    CHECK(set2.contains(2));
}

TEST_CASE("skiplist_set::extract") {
    skiplist_set<int> set{1, 2, 3};

    auto nh = set.extract(2);
    CHECK(!nh.empty());
    CHECK_EQ(nh.value(), 2);
    CHECK_EQ(set.size(), 2);
    CHECK_FALSE(set.contains(2));
}

// ============================================================================
// Iterator stability tests
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

}  // namespace stdb::container

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

#include "container/dense_map.hpp"

#include <random>
#include <string>
#include <unordered_map>
#include <vector>

#include "doctest/doctest/doctest.h"

namespace stdb::container {

TEST_CASE("dense_map::basic") {
    SUBCASE("default constructor") {
        dense_map<int, int> map;
        CHECK(map.empty());
        CHECK_EQ(map.size(), 0);
    }

    SUBCASE("initializer list") {
        dense_map<int, std::string> map{{1, "one"}, {2, "two"}, {3, "three"}};
        CHECK_EQ(map.size(), 3);
        CHECK_EQ(map.at(1), "one");
        CHECK_EQ(map.at(2), "two");
        CHECK_EQ(map.at(3), "three");
    }

    SUBCASE("capacity constructor") {
        dense_map<int, int> map(100);
        CHECK(map.empty());
        CHECK_GE(map.capacity(), 100);
    }
}

TEST_CASE("dense_map::insert") {
    SUBCASE("insert single value") {
        dense_map<int, int> map;
        auto [it, inserted] = map.insert({1, 10});
        CHECK(inserted);
        CHECK_EQ(it->first, 1);
        CHECK_EQ(it->second, 10);
        CHECK_EQ(map.size(), 1);

        // Insert duplicate
        auto [it2, inserted2] = map.insert({1, 20});
        CHECK_FALSE(inserted2);
        CHECK_EQ(it2->second, 10);  // Original value unchanged
        CHECK_EQ(map.size(), 1);
    }

    SUBCASE("insert move") {
        dense_map<int, std::string> map;
        std::string value = "hello";
        auto [it, inserted] = map.insert({1, std::move(value)});
        CHECK(inserted);
        CHECK_EQ(it->second, "hello");
    }

    SUBCASE("insert_or_assign") {
        dense_map<int, int> map;
        auto [it1, inserted1] = map.insert_or_assign(1, 10);
        CHECK(inserted1);
        CHECK_EQ(it1->second, 10);

        auto [it2, inserted2] = map.insert_or_assign(1, 20);
        CHECK_FALSE(inserted2);
        CHECK_EQ(it2->second, 20);  // Value updated
    }

    SUBCASE("insert range") {
        std::vector<std::pair<int, int>> vec{{1, 10}, {2, 20}, {3, 30}};
        dense_map<int, int> map;
        map.insert(vec.begin(), vec.end());
        CHECK_EQ(map.size(), 3);
        CHECK_EQ(map.at(1), 10);
        CHECK_EQ(map.at(2), 20);
        CHECK_EQ(map.at(3), 30);
    }
}

TEST_CASE("dense_map::emplace") {
    SUBCASE("emplace") {
        dense_map<int, std::string> map;
        auto [it, inserted] = map.emplace(1, "hello");
        CHECK(inserted);
        CHECK_EQ(it->first, 1);
        CHECK_EQ(it->second, "hello");

        // Emplace duplicate
        auto [it2, inserted2] = map.emplace(1, "world");
        CHECK_FALSE(inserted2);
        CHECK_EQ(it2->second, "hello");
    }

    SUBCASE("try_emplace") {
        dense_map<int, std::string> map;
        auto [it1, inserted1] = map.try_emplace(1, "hello");
        CHECK(inserted1);
        CHECK_EQ(it1->second, "hello");

        // try_emplace on existing key - should NOT update
        auto [it2, inserted2] = map.try_emplace(1, "world");
        CHECK_FALSE(inserted2);
        CHECK_EQ(it2->second, "hello");  // Original value preserved
    }
}

TEST_CASE("dense_map::erase") {
    SUBCASE("erase by key") {
        dense_map<int, int> map{{1, 10}, {2, 20}, {3, 30}};
        auto erased = map.erase(2);
        CHECK_EQ(erased, 1);
        CHECK_EQ(map.size(), 2);
        CHECK_FALSE(map.contains(2));
        CHECK(map.contains(1));
        CHECK(map.contains(3));

        // Erase non-existent
        erased = map.erase(100);
        CHECK_EQ(erased, 0);
    }

    SUBCASE("erase by iterator") {
        dense_map<int, int> map{{1, 10}, {2, 20}, {3, 30}};
        auto it = map.find(2);
        REQUIRE(it != map.end());
        map.erase(it);
        CHECK_EQ(map.size(), 2);
        CHECK_FALSE(map.contains(2));
    }

    SUBCASE("clear") {
        dense_map<int, int> map{{1, 10}, {2, 20}, {3, 30}};
        map.clear();
        CHECK(map.empty());
        CHECK_EQ(map.size(), 0);
    }
}

TEST_CASE("dense_map::lookup") {
    dense_map<int, std::string> map{{1, "one"}, {2, "two"}, {3, "three"}};

    SUBCASE("find existing") {
        auto it = map.find(2);
        REQUIRE(it != map.end());
        CHECK_EQ(it->first, 2);
        CHECK_EQ(it->second, "two");
    }

    SUBCASE("find non-existing") {
        auto it = map.find(100);
        CHECK(it == map.end());
    }

    SUBCASE("contains") {
        CHECK(map.contains(1));
        CHECK(map.contains(2));
        CHECK(map.contains(3));
        CHECK_FALSE(map.contains(100));
    }

    SUBCASE("at") {
        CHECK_EQ(map.at(1), "one");
        CHECK_THROWS_AS(map.at(100), std::out_of_range);
    }

    SUBCASE("operator[]") {
        CHECK_EQ(map[1], "one");
        // operator[] inserts default value for non-existent key
        map[100] = "hundred";
        CHECK(map.contains(100));
        CHECK_EQ(map[100], "hundred");
    }

    SUBCASE("count") {
        CHECK_EQ(map.count(1), 1);
        CHECK_EQ(map.count(100), 0);
    }

    SUBCASE("equal_range") {
        auto [first, last] = map.equal_range(2);
        REQUIRE(first != last);
        CHECK_EQ(first->first, 2);
        ++first;
        CHECK(first == last);

        auto [f2, l2] = map.equal_range(100);
        CHECK(f2 == l2);
    }
}

TEST_CASE("dense_map::iterator") {
    SUBCASE("iteration") {
        dense_map<int, int> map{{1, 10}, {2, 20}, {3, 30}};
        std::unordered_map<int, int> found;
        for (const auto& [k, v] : map) {
            found[k] = v;
        }
        CHECK_EQ(found.size(), 3);
        CHECK_EQ(found[1], 10);
        CHECK_EQ(found[2], 20);
        CHECK_EQ(found[3], 30);
    }

    SUBCASE("empty iteration") {
        dense_map<int, int> map;
        int count = 0;
        for ([[maybe_unused]] const auto& kv : map) {
            ++count;
        }
        CHECK_EQ(count, 0);
    }

    SUBCASE("const iteration") {
        const dense_map<int, int> map{{1, 10}, {2, 20}};
        std::unordered_map<int, int> found;
        for (const auto& [k, v] : map) {
            found[k] = v;
        }
        CHECK_EQ(found.size(), 2);
    }
}

TEST_CASE("dense_map::copy_and_move") {
    SUBCASE("copy constructor") {
        dense_map<int, std::string> original{{1, "one"}, {2, "two"}};
        dense_map<int, std::string> copy(original);
        CHECK_EQ(copy.size(), 2);
        CHECK_EQ(copy.at(1), "one");
        CHECK_EQ(copy.at(2), "two");

        // Modify original, copy unchanged
        original[1] = "ONE";
        CHECK_EQ(copy.at(1), "one");
    }

    SUBCASE("copy assignment") {
        dense_map<int, int> original{{1, 10}, {2, 20}};
        dense_map<int, int> copy;
        copy = original;
        CHECK_EQ(copy.size(), 2);
        CHECK_EQ(copy.at(1), 10);
    }

    SUBCASE("move constructor") {
        dense_map<int, std::string> original{{1, "one"}, {2, "two"}};
        dense_map<int, std::string> moved(std::move(original));
        CHECK_EQ(moved.size(), 2);
        CHECK_EQ(moved.at(1), "one");
        CHECK(original.empty());  // NOLINT
    }

    SUBCASE("move assignment") {
        dense_map<int, int> original{{1, 10}, {2, 20}};
        dense_map<int, int> moved;
        moved = std::move(original);
        CHECK_EQ(moved.size(), 2);
        CHECK(original.empty());  // NOLINT
    }

    SUBCASE("swap") {
        dense_map<int, int> a{{1, 10}};
        dense_map<int, int> b{{2, 20}, {3, 30}};
        a.swap(b);
        CHECK_EQ(a.size(), 2);
        CHECK_EQ(b.size(), 1);
        CHECK(a.contains(2));
        CHECK(b.contains(1));
    }
}

TEST_CASE("dense_map::hash_policy") {
    SUBCASE("load_factor") {
        dense_map<int, int> map;
        CHECK_EQ(map.load_factor(), 0.0f);

        map[1] = 10;
        CHECK_GT(map.load_factor(), 0.0f);
        CHECK_LE(map.load_factor(), map.max_load_factor());
    }

    SUBCASE("reserve") {
        dense_map<int, int> map;
        map.reserve(1000);
        CHECK_GE(map.capacity(), 1000);
        size_t cap = map.capacity();

        // Inserting up to reserved amount shouldn't reallocate
        for (int i = 0; i < 800; ++i) {
            map[i] = i * 10;
        }
        CHECK_EQ(map.capacity(), cap);
    }

    SUBCASE("rehash") {
        dense_map<int, int> map;
        for (int i = 0; i < 100; ++i) {
            map[i] = i;
        }
        size_t old_cap = map.capacity();

        map.rehash(old_cap * 2);
        CHECK_GE(map.capacity(), old_cap * 2);

        // All elements should still be accessible
        for (int i = 0; i < 100; ++i) {
            CHECK_EQ(map.at(i), i);
        }
    }
}

TEST_CASE("dense_map::stress_test") {
    SUBCASE("large insert and lookup") {
        constexpr int N = 10000;
        dense_map<int, int> map;
        map.reserve(N);

        for (int i = 0; i < N; ++i) {
            map[i] = i * 2;
        }
        CHECK_EQ(map.size(), N);

        for (int i = 0; i < N; ++i) {
            CHECK_EQ(map.at(i), i * 2);
        }
    }

    SUBCASE("random operations") {
        std::mt19937 gen(42);
        std::uniform_int_distribution<int> dist(0, 10000);

        dense_map<int, int> map;
        std::unordered_map<int, int> reference;

        for (int i = 0; i < 5000; ++i) {
            int key = dist(gen);
            int value = dist(gen);

            if (dist(gen) % 3 == 0 && !reference.empty()) {
                // Erase
                auto it = reference.begin();
                map.erase(it->first);
                reference.erase(it);
            } else {
                // Insert
                map[key] = value;
                reference[key] = value;
            }
        }

        CHECK_EQ(map.size(), reference.size());
        for (const auto& [k, v] : reference) {
            CHECK_EQ(map.at(k), v);
        }
    }

    SUBCASE("insert and erase interleaved") {
        dense_map<int, int> map;

        // Insert 1000 elements
        for (int i = 0; i < 1000; ++i) {
            map[i] = i;
        }

        // Erase even numbers
        for (int i = 0; i < 1000; i += 2) {
            map.erase(i);
        }
        CHECK_EQ(map.size(), 500);

        // Verify odd numbers remain
        for (int i = 1; i < 1000; i += 2) {
            CHECK_EQ(map.at(i), i);
        }

        // Reinsert evens
        for (int i = 0; i < 1000; i += 2) {
            map[i] = i * 10;
        }
        CHECK_EQ(map.size(), 1000);

        // Verify all
        for (int i = 0; i < 1000; ++i) {
            if (i % 2 == 0) {
                CHECK_EQ(map.at(i), i * 10);
            } else {
                CHECK_EQ(map.at(i), i);
            }
        }
    }
}

TEST_CASE("dense_map::string_keys") {
    SUBCASE("basic string operations") {
        dense_map<std::string, int> map;
        map["hello"] = 1;
        map["world"] = 2;
        map["test"] = 3;

        CHECK_EQ(map.size(), 3);
        CHECK_EQ(map.at("hello"), 1);
        CHECK_EQ(map.at("world"), 2);
        CHECK(map.contains("test"));
        CHECK_FALSE(map.contains("missing"));
    }

    SUBCASE("string stress test") {
        dense_map<std::string, int> map;
        for (int i = 0; i < 1000; ++i) {
            map["key_" + std::to_string(i)] = i;
        }
        CHECK_EQ(map.size(), 1000);

        for (int i = 0; i < 1000; ++i) {
            CHECK_EQ(map.at("key_" + std::to_string(i)), i);
        }
    }
}

TEST_CASE("dense_map::transparent_lookup") {
    SUBCASE("string_hash transparent lookup") {
        dense_map<std::string, int, string_hash, std::equal_to<>> map;
        map["hello"] = 1;
        map["world"] = 2;

        // Lookup with string_view
        std::string_view sv = "hello";
        CHECK(map.contains(sv));
        CHECK_EQ(map.find(sv)->second, 1);

        // Lookup with const char*
        CHECK(map.contains("world"));
    }
}

TEST_CASE("dense_map::equality") {
    SUBCASE("equal maps") {
        dense_map<int, int> a{{1, 10}, {2, 20}, {3, 30}};
        dense_map<int, int> b{{3, 30}, {1, 10}, {2, 20}};  // Different order
        CHECK(a == b);
        CHECK_FALSE(a != b);
    }

    SUBCASE("different sizes") {
        dense_map<int, int> a{{1, 10}, {2, 20}};
        dense_map<int, int> b{{1, 10}};
        CHECK(a != b);
        CHECK_FALSE(a == b);
    }

    SUBCASE("different values") {
        dense_map<int, int> a{{1, 10}};
        dense_map<int, int> b{{1, 20}};
        CHECK(a != b);
    }

    SUBCASE("different keys") {
        dense_map<int, int> a{{1, 10}};
        dense_map<int, int> b{{2, 10}};
        CHECK(a != b);
    }
}

TEST_CASE("dense_map::hash_quality") {
    // Test that hash distribution is reasonable
    SUBCASE("sequential keys") {
        dense_map<int, int> map;
        for (int i = 0; i < 10000; ++i) {
            map[i] = i;
        }
        CHECK_EQ(map.size(), 10000);
        CHECK_LE(map.load_factor(), map.max_load_factor());
    }

    SUBCASE("sparse keys") {
        dense_map<int, int> map;
        for (int i = 0; i < 1000; ++i) {
            map[i * 1000] = i;
        }
        CHECK_EQ(map.size(), 1000);
    }
}

// Additional comprehensive tests based on tsl/ankerl patterns

TEST_CASE("dense_map::edge_cases") {
    SUBCASE("operations on empty map") {
        dense_map<int, int> map;
        CHECK(map.find(1) == map.end());
        CHECK_EQ(map.erase(1), 0);
        CHECK_EQ(map.count(1), 0);
        CHECK_FALSE(map.contains(1));
        map.clear();  // Should not crash
        CHECK(map.empty());
    }

    SUBCASE("single element") {
        dense_map<int, int> map;
        map[42] = 100;
        CHECK_EQ(map.size(), 1);
        CHECK_EQ(map.at(42), 100);

        // Erase and re-insert
        map.erase(42);
        CHECK(map.empty());
        map[42] = 200;
        CHECK_EQ(map.at(42), 200);
    }

    SUBCASE("erase all elements") {
        dense_map<int, int> map;
        for (int i = 0; i < 100; ++i) {
            map[i] = i;
        }

        // Erase all one by one
        for (int i = 0; i < 100; ++i) {
            CHECK_EQ(map.erase(i), 1);
        }
        CHECK(map.empty());
        CHECK_EQ(map.size(), 0);

        // Insert again
        map[0] = 0;
        CHECK_EQ(map.size(), 1);
    }

    SUBCASE("rehash to smaller") {
        dense_map<int, int> map;
        map.reserve(1000);
        for (int i = 0; i < 10; ++i) {
            map[i] = i;
        }
        map.rehash(16);
        CHECK_EQ(map.size(), 10);
        for (int i = 0; i < 10; ++i) {
            CHECK_EQ(map.at(i), i);
        }
    }
}

TEST_CASE("dense_map::various_key_types") {
    SUBCASE("int8 keys") {
        dense_map<int8_t, int> map;
        for (int8_t i = -100; i < 100; ++i) {
            map[i] = i * 2;
        }
        CHECK_EQ(map.size(), 200);
        for (int8_t i = -100; i < 100; ++i) {
            CHECK_EQ(map.at(i), i * 2);
        }
    }

    SUBCASE("int64 keys") {
        dense_map<int64_t, int> map;
        for (int64_t i = 0; i < 1000; ++i) {
            map[i * 1000000000LL] = static_cast<int>(i);
        }
        CHECK_EQ(map.size(), 1000);
        for (int64_t i = 0; i < 1000; ++i) {
            CHECK_EQ(map.at(i * 1000000000LL), static_cast<int>(i));
        }
    }

    SUBCASE("pointer keys") {
        std::vector<int> values(100);
        dense_map<int*, int> map;
        for (int i = 0; i < 100; ++i) {
            values[i] = i;
            map[&values[i]] = i * 10;
        }
        CHECK_EQ(map.size(), 100);
        for (int i = 0; i < 100; ++i) {
            CHECK_EQ(map.at(&values[i]), i * 10);
        }
    }

    SUBCASE("size_t keys") {
        dense_map<size_t, int> map;
        for (size_t i = 0; i < 1000; ++i) {
            map[i] = static_cast<int>(i);
        }
        CHECK_EQ(map.size(), 1000);
    }
}

TEST_CASE("dense_map::collision_handling") {
    // Custom hash that forces collisions
    struct BadHash {
        size_t operator()(int key) const noexcept {
            return static_cast<size_t>(key % 8);  // Only 8 different hash values
        }
    };

    SUBCASE("many collisions insert") {
        dense_map<int, int, BadHash> map;
        for (int i = 0; i < 1000; ++i) {
            map[i] = i * 2;
        }
        CHECK_EQ(map.size(), 1000);
        for (int i = 0; i < 1000; ++i) {
            CHECK_EQ(map.at(i), i * 2);
        }
    }

    SUBCASE("many collisions erase") {
        dense_map<int, int, BadHash> map;
        for (int i = 0; i < 1000; ++i) {
            map[i] = i;
        }

        // Erase every other element
        for (int i = 0; i < 1000; i += 2) {
            map.erase(i);
        }
        CHECK_EQ(map.size(), 500);

        // Check remaining
        for (int i = 1; i < 1000; i += 2) {
            CHECK_EQ(map.at(i), i);
        }
    }
}

TEST_CASE("dense_map::complex_values") {
    SUBCASE("vector value") {
        dense_map<int, std::vector<int>> map;
        map[1] = {1, 2, 3};
        map[2] = {4, 5, 6, 7, 8};

        CHECK_EQ(map.at(1).size(), 3);
        CHECK_EQ(map.at(2).size(), 5);
        CHECK_EQ(map.at(1)[0], 1);
        CHECK_EQ(map.at(2)[4], 8);
    }

    SUBCASE("map value") {
        dense_map<int, std::unordered_map<std::string, int>> map;
        map[1]["a"] = 1;
        map[1]["b"] = 2;
        map[2]["x"] = 10;

        CHECK_EQ(map.at(1).size(), 2);
        CHECK_EQ(map.at(2).at("x"), 10);
    }

    SUBCASE("pair value") {
        dense_map<int, std::pair<std::string, double>> map;
        map[1] = {"hello", 3.14};
        map[2] = {"world", 2.71};

        CHECK_EQ(map.at(1).first, "hello");
        CHECK_EQ(map.at(1).second, doctest::Approx(3.14));
    }
}

TEST_CASE("dense_map::move_only_values") {
    SUBCASE("unique_ptr value") {
        dense_map<int, std::unique_ptr<int>> map;
        map.emplace(1, std::make_unique<int>(100));
        map.emplace(2, std::make_unique<int>(200));

        CHECK_EQ(map.size(), 2);
        CHECK_EQ(*map.at(1), 100);
        CHECK_EQ(*map.at(2), 200);

        // Move out
        auto ptr = std::move(map[1]);
        CHECK_EQ(*ptr, 100);
        CHECK(map[1] == nullptr);
    }
}

TEST_CASE("dense_map::iterator_validity") {
    SUBCASE("erase maintains iteration") {
        dense_map<int, int> map;
        for (int i = 0; i < 100; ++i) {
            map[i] = i;
        }

        // Count elements during iteration
        int count = 0;
        for (auto it = map.begin(); it != map.end(); ++it) {
            ++count;
        }
        CHECK_EQ(count, 100);
    }

    SUBCASE("modification during iteration via reference") {
        dense_map<int, int> map;
        for (int i = 0; i < 100; ++i) {
            map[i] = i;
        }

        for (auto& [k, v] : map) {
            v *= 2;
        }

        for (int i = 0; i < 100; ++i) {
            CHECK_EQ(map.at(i), i * 2);
        }
    }
}

TEST_CASE("dense_map::high_load_factor") {
    SUBCASE("insert at high load") {
        dense_map<int, int> map;
        map.reserve(16);  // Small initial capacity

        // Insert up to near max load
        for (int i = 0; i < 13; ++i) {  // ~81% load
            map[i] = i;
        }
        CHECK_EQ(map.size(), 13);

        // Insert more to trigger rehash
        for (int i = 13; i < 30; ++i) {
            map[i] = i;
        }
        CHECK_EQ(map.size(), 30);

        // Verify all
        for (int i = 0; i < 30; ++i) {
            CHECK_EQ(map.at(i), i);
        }
    }
}

TEST_CASE("dense_map::negative_keys") {
    SUBCASE("negative int keys") {
        dense_map<int, int> map;
        for (int i = -1000; i < 1000; ++i) {
            map[i] = i * 2;
        }
        CHECK_EQ(map.size(), 2000);

        for (int i = -1000; i < 1000; ++i) {
            CHECK_EQ(map.at(i), i * 2);
        }
    }
}

TEST_CASE("dense_map::erase_patterns") {
    SUBCASE("erase from beginning") {
        dense_map<int, int> map;
        for (int i = 0; i < 100; ++i) {
            map[i] = i;
        }

        for (int i = 0; i < 50; ++i) {
            map.erase(i);
        }
        CHECK_EQ(map.size(), 50);

        for (int i = 50; i < 100; ++i) {
            CHECK_EQ(map.at(i), i);
        }
    }

    SUBCASE("erase from end") {
        dense_map<int, int> map;
        for (int i = 0; i < 100; ++i) {
            map[i] = i;
        }

        for (int i = 99; i >= 50; --i) {
            map.erase(i);
        }
        CHECK_EQ(map.size(), 50);

        for (int i = 0; i < 50; ++i) {
            CHECK_EQ(map.at(i), i);
        }
    }

    SUBCASE("erase random order") {
        dense_map<int, int> map;
        std::vector<int> keys;
        for (int i = 0; i < 100; ++i) {
            map[i] = i;
            keys.push_back(i);
        }

        std::mt19937 gen(42);
        std::shuffle(keys.begin(), keys.end(), gen);

        // Erase half in random order
        for (int i = 0; i < 50; ++i) {
            map.erase(keys[i]);
        }
        CHECK_EQ(map.size(), 50);

        // Verify remaining
        for (int i = 50; i < 100; ++i) {
            CHECK(map.contains(keys[i]));
        }
    }
}

TEST_CASE("dense_map::large_scale") {
    SUBCASE("100K elements") {
        constexpr int N = 100000;
        dense_map<int64_t, int64_t> map;

        for (int64_t i = 0; i < N; ++i) {
            map[i] = i * 2;
        }
        CHECK_EQ(map.size(), N);

        for (int64_t i = 0; i < N; ++i) {
            CHECK_EQ(map.at(i), i * 2);
        }

        // Erase half
        for (int64_t i = 0; i < N; i += 2) {
            map.erase(i);
        }
        CHECK_EQ(map.size(), N / 2);
    }
}

TEST_CASE("dense_map::duplicate_insert_behavior") {
    SUBCASE("insert does not overwrite") {
        dense_map<int, int> map;
        map.insert({1, 100});
        auto [it, inserted] = map.insert({1, 200});

        CHECK_FALSE(inserted);
        CHECK_EQ(it->second, 100);  // Original value
    }

    SUBCASE("insert_or_assign does overwrite") {
        dense_map<int, int> map;
        map.insert({1, 100});
        auto [it, inserted] = map.insert_or_assign(1, 200);

        CHECK_FALSE(inserted);
        CHECK_EQ(it->second, 200);  // New value
    }

    SUBCASE("try_emplace does not construct on duplicate") {
        static int construct_count = 0;
        struct Counter {
            Counter() { ++construct_count; }
            Counter(int) { ++construct_count; }
            Counter(const Counter&) { ++construct_count; }
            Counter(Counter&&) noexcept { ++construct_count; }
        };

        construct_count = 0;
        dense_map<int, Counter> map;
        map.try_emplace(1, 100);
        int after_first = construct_count;

        map.try_emplace(1, 200);  // Should not construct
        CHECK_EQ(construct_count, after_first);
    }
}

TEST_CASE("dense_map::find_patterns") {
    dense_map<int, int> map;
    for (int i = 0; i < 1000; ++i) {
        map[i * 2] = i;  // Only even keys
    }

    SUBCASE("find existing keys") {
        for (int i = 0; i < 1000; ++i) {
            auto it = map.find(i * 2);
            REQUIRE(it != map.end());
            CHECK_EQ(it->second, i);
        }
    }

    SUBCASE("find non-existing keys") {
        for (int i = 0; i < 1000; ++i) {
            auto it = map.find(i * 2 + 1);  // Odd keys don't exist
            CHECK(it == map.end());
        }
    }
}

}  // namespace stdb::container

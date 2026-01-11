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

#include "container/dense_set.hpp"

#include <random>
#include <string>
#include <unordered_set>
#include <vector>

#include "doctest/doctest/doctest.h"

namespace stdb::container {

TEST_CASE("dense_set::basic") {
    SUBCASE("default constructor") {
        dense_set<int> set;
        CHECK(set.empty());
        CHECK_EQ(set.size(), 0);
    }

    SUBCASE("initializer list") {
        dense_set<int> set{1, 2, 3, 4, 5};
        CHECK_EQ(set.size(), 5);
        CHECK(set.contains(1));
        CHECK(set.contains(3));
        CHECK(set.contains(5));
        CHECK_FALSE(set.contains(0));
        CHECK_FALSE(set.contains(6));
    }

    SUBCASE("capacity constructor") {
        dense_set<int> set(100);
        CHECK(set.empty());
        CHECK_GE(set.bucket_count(), 100);
    }
}

TEST_CASE("dense_set::insert") {
    SUBCASE("insert single value") {
        dense_set<int> set;
        auto [it, inserted] = set.insert(42);
        CHECK(inserted);
        CHECK_EQ(*it, 42);
        CHECK_EQ(set.size(), 1);

        // Insert duplicate
        auto [it2, inserted2] = set.insert(42);
        CHECK_FALSE(inserted2);
        CHECK_EQ(*it2, 42);
        CHECK_EQ(set.size(), 1);
    }

    SUBCASE("insert range") {
        std::vector<int> vec{1, 2, 3, 4, 5};
        dense_set<int> set;
        set.insert(vec.begin(), vec.end());
        CHECK_EQ(set.size(), 5);
        for (int i = 1; i <= 5; ++i) {
            CHECK(set.contains(i));
        }
    }

    SUBCASE("insert move") {
        dense_set<std::string> set;
        std::string value = "hello";
        auto [it, inserted] = set.insert(std::move(value));
        CHECK(inserted);
        CHECK_EQ(*it, "hello");
    }
}

TEST_CASE("dense_set::erase") {
    SUBCASE("erase by key") {
        dense_set<int> set{1, 2, 3, 4, 5};
        CHECK_EQ(set.erase(3), 1);
        CHECK_EQ(set.size(), 4);
        CHECK_FALSE(set.contains(3));

        CHECK_EQ(set.erase(100), 0);  // Non-existent
        CHECK_EQ(set.size(), 4);
    }

    SUBCASE("erase multiple") {
        dense_set<int> set;
        for (int i = 0; i < 100; ++i) {
            set.insert(i);
        }

        for (int i = 0; i < 50; ++i) {
            set.erase(i * 2);  // Erase even numbers
        }

        CHECK_EQ(set.size(), 50);
        for (int i = 0; i < 100; ++i) {
            if (i % 2 == 0) {
                CHECK_FALSE(set.contains(i));
            } else {
                CHECK(set.contains(i));
            }
        }
    }
}

TEST_CASE("dense_set::find") {
    dense_set<int> set{10, 20, 30, 40, 50};

    SUBCASE("find existing") {
        auto it = set.find(30);
        REQUIRE(it != set.end());
        CHECK_EQ(*it, 30);
    }

    SUBCASE("find non-existing") {
        auto it = set.find(25);
        CHECK(it == set.end());
    }
}

TEST_CASE("dense_set::iteration") {
    dense_set<int> set;
    for (int i = 0; i < 100; ++i) {
        set.insert(i);
    }

    SUBCASE("count elements") {
        int count = 0;
        for (const auto& x : set) {
            ++count;
            (void)x;
        }
        CHECK_EQ(count, 100);
    }

    SUBCASE("sum elements") {
        int sum = 0;
        for (const auto& x : set) {
            sum += x;
        }
        CHECK_EQ(sum, 99 * 100 / 2);  // Sum 0..99
    }
}

TEST_CASE("dense_set::string_keys") {
    SUBCASE("basic string operations") {
        dense_set<std::string> set;
        set.insert("hello");
        set.insert("world");
        set.insert("test");

        CHECK_EQ(set.size(), 3);
        CHECK(set.contains("hello"));
        CHECK(set.contains("world"));
        CHECK(set.contains("test"));
        CHECK_FALSE(set.contains("missing"));
    }

    SUBCASE("string stress test") {
        std::mt19937_64 rng(42);
        dense_set<std::string> set;
        std::vector<std::string> keys;

        auto random_string = [&rng](int len) {
            static const char chars[] = "abcdefghijklmnopqrstuvwxyz";
            std::string s;
            for (int i = 0; i < len; i++) s += chars[rng() % 26];
            return s;
        };

        for (int i = 0; i < 1000; ++i) {
            auto key = random_string(16);
            keys.push_back(key);
            set.insert(key);
        }

        CHECK_EQ(set.size(), 1000);

        for (const auto& k : keys) {
            CHECK(set.contains(k));
        }
    }

    SUBCASE("long strings") {
        dense_set<std::string> set;
        std::string long_prefix(500, 'x');

        for (int i = 0; i < 100; ++i) {
            set.insert(long_prefix + std::to_string(i));
        }

        CHECK_EQ(set.size(), 100);

        for (int i = 0; i < 100; ++i) {
            CHECK(set.contains(long_prefix + std::to_string(i)));
        }
    }
}

TEST_CASE("dense_set::equality") {
    SUBCASE("equal sets") {
        dense_set<int> a{1, 2, 3, 4, 5};
        dense_set<int> b{5, 4, 3, 2, 1};  // Different order
        CHECK(a == b);
        CHECK_FALSE(a != b);
    }

    SUBCASE("different sizes") {
        dense_set<int> a{1, 2, 3};
        dense_set<int> b{1, 2};
        CHECK(a != b);
        CHECK_FALSE(a == b);
    }

    SUBCASE("different elements") {
        dense_set<int> a{1, 2, 3};
        dense_set<int> b{1, 2, 4};
        CHECK(a != b);
    }
}

TEST_CASE("dense_set::clear") {
    dense_set<int> set{1, 2, 3, 4, 5};
    CHECK_EQ(set.size(), 5);

    set.clear();
    CHECK(set.empty());
    CHECK_EQ(set.size(), 0);

    // Can insert again after clear
    set.insert(10);
    CHECK_EQ(set.size(), 1);
    CHECK(set.contains(10));
}

TEST_CASE("dense_set::copy_move") {
    SUBCASE("copy constructor") {
        dense_set<int> a{1, 2, 3, 4, 5};
        dense_set<int> b(a);
        CHECK_EQ(a.size(), b.size());
        CHECK(a == b);
    }

    SUBCASE("move constructor") {
        dense_set<int> a{1, 2, 3, 4, 5};
        dense_set<int> b(std::move(a));
        CHECK_EQ(b.size(), 5);
        for (int i = 1; i <= 5; ++i) {
            CHECK(b.contains(i));
        }
    }

    SUBCASE("copy assignment") {
        dense_set<int> a{1, 2, 3};
        dense_set<int> b{10, 20};
        b = a;
        CHECK(a == b);
    }

    SUBCASE("move assignment") {
        dense_set<int> a{1, 2, 3, 4, 5};
        dense_set<int> b;
        b = std::move(a);
        CHECK_EQ(b.size(), 5);
    }
}

TEST_CASE("dense_set::large_scale") {
    SUBCASE("insert 100K elements") {
        dense_set<int> set;
        for (int i = 0; i < 100000; ++i) {
            set.insert(i);
        }
        CHECK_EQ(set.size(), 100000);

        for (int i = 0; i < 100000; ++i) {
            CHECK(set.contains(i));
        }
    }

    SUBCASE("random operations") {
        std::mt19937_64 rng(123);
        dense_set<int> set;
        std::unordered_set<int> reference;

        for (int i = 0; i < 10000; ++i) {
            int op = rng() % 3;
            int val = rng() % 5000;

            if (op == 0) {
                set.insert(val);
                reference.insert(val);
            } else if (op == 1) {
                set.erase(val);
                reference.erase(val);
            } else {
                CHECK_EQ(set.contains(val), reference.count(val) > 0);
            }
        }

        CHECK_EQ(set.size(), reference.size());
    }
}

TEST_CASE("dense_set::rehash") {
    dense_set<int> set;
    set.reserve(16);

    for (int i = 0; i < 100; ++i) {
        set.insert(i);

        // Verify all elements after each insert
        for (int j = 0; j <= i; ++j) {
            if (!set.contains(j)) {
                FAIL("Element " << j << " not found after inserting " << i);
            }
        }
    }

    CHECK_EQ(set.size(), 100);
}

}  // namespace stdb::container

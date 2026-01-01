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

#include "container/boolean_vector.hpp"

#include <algorithm>
#include <numeric>
#include <random>
#include <vector>

#include "doctest/doctest/doctest.h"

using namespace stdb::container;

TEST_SUITE("boolean_vector") {
    TEST_CASE("construction") {
        SUBCASE("default constructor") {
            boolean_vector v;
            CHECK(v.size() == 0);
            CHECK(v.empty());
            CHECK(v.capacity() >= 0);  // SBO may have initial capacity
        }

        SUBCASE("size constructor with default value") {
            boolean_vector v(100);
            CHECK(v.size() == 100);
            CHECK_FALSE(v.empty());
            for (size_t i = 0; i < 100; ++i) {
                CHECK(v[i] == false);
            }
        }

        SUBCASE("size constructor with value true") {
            boolean_vector v(100, true);
            CHECK(v.size() == 100);
            for (size_t i = 0; i < 100; ++i) {
                CHECK(v[i] == true);
            }
        }

        SUBCASE("initializer_list constructor") {
            boolean_vector v{true, false, true, true, false};
            CHECK(v.size() == 5);
            CHECK(v[0] == true);
            CHECK(v[1] == false);
            CHECK(v[2] == true);
            CHECK(v[3] == true);
            CHECK(v[4] == false);
        }

        SUBCASE("copy constructor") {
            boolean_vector v1(100, true);
            v1[50] = false;

            boolean_vector v2(v1);
            CHECK(v2.size() == 100);
            CHECK(v2[49] == true);
            CHECK(v2[50] == false);
            CHECK(v2[51] == true);
        }

        SUBCASE("move constructor") {
            boolean_vector v1(100, true);
            v1[50] = false;

            boolean_vector v2(std::move(v1));
            CHECK(v2.size() == 100);
            CHECK(v2[50] == false);
            CHECK(v1.size() == 0);  // NOLINT(bugprone-use-after-move)
            CHECK(v1.empty());
        }

        SUBCASE("iterator constructor") {
            std::vector<bool> src{true, false, true, false, true};
            boolean_vector v(src.begin(), src.end());
            CHECK(v.size() == 5);
            for (size_t i = 0; i < 5; ++i) {
                CHECK(v[i] == src[i]);
            }
        }
    }

    TEST_CASE("assignment") {
        SUBCASE("copy assignment") {
            boolean_vector v1(50, true);
            boolean_vector v2;
            v2 = v1;
            CHECK(v2.size() == 50);
            for (size_t i = 0; i < 50; ++i) {
                CHECK(v2[i] == true);
            }
        }

        SUBCASE("move assignment") {
            boolean_vector v1(50, true);
            boolean_vector v2;
            v2 = std::move(v1);
            CHECK(v2.size() == 50);
            CHECK(v1.empty());  // NOLINT(bugprone-use-after-move)
        }

        SUBCASE("initializer_list assignment") {
            boolean_vector v;
            v = {true, false, true};
            CHECK(v.size() == 3);
            CHECK(v[0] == true);
            CHECK(v[1] == false);
            CHECK(v[2] == true);
        }

        SUBCASE("assign count and value") {
            boolean_vector v;
            v.assign(100, true);
            CHECK(v.size() == 100);
            for (size_t i = 0; i < 100; ++i) {
                CHECK(v[i] == true);
            }
        }
    }

    TEST_CASE("element access") {
        boolean_vector v(100);
        v[0] = true;
        v[50] = true;
        v[99] = true;

        SUBCASE("operator[]") {
            CHECK(v[0] == true);
            CHECK(v[1] == false);
            CHECK(v[50] == true);
            CHECK(v[99] == true);
        }

        SUBCASE("at") {
            CHECK(v.at(0) == true);
            CHECK(v.at(50) == true);
            CHECK_THROWS_AS(v.at(100), std::out_of_range);
        }

        SUBCASE("front and back") {
            CHECK(v.front() == true);
            CHECK(v.back() == true);
        }

        SUBCASE("reference modification") {
            v[10] = true;
            CHECK(v[10] == true);
            v[10] = false;
            CHECK(v[10] == false);

            bool& ref = v[20];
            ref = true;
            CHECK(v[20] == true);
            ref = !ref;
            CHECK(v[20] == false);
        }
    }

    TEST_CASE("iterators") {
        boolean_vector v{true, false, true, false, true};

        SUBCASE("forward iteration") {
            std::vector<bool> result;
            for (auto it = v.begin(); it != v.end(); ++it) {
                result.push_back(*it);
            }
            CHECK(result.size() == 5);
            CHECK(result[0] == true);
            CHECK(result[1] == false);
            CHECK(result[2] == true);
        }

        SUBCASE("range-based for") {
            size_t count = 0;
            size_t true_count = 0;
            for (bool b : v) {
                ++count;
                if (b) ++true_count;
            }
            CHECK(count == 5);
            CHECK(true_count == 3);
        }

        SUBCASE("reverse iteration") {
            std::vector<bool> result;
            for (auto it = v.rbegin(); it != v.rend(); ++it) {
                result.push_back(*it);
            }
            CHECK(result.size() == 5);
            CHECK(result[0] == true);   // v[4]
            CHECK(result[1] == false);  // v[3]
            CHECK(result[2] == true);   // v[2]
        }

        SUBCASE("iterator arithmetic") {
            auto it = v.begin();
            CHECK(*(it + 2) == true);
            CHECK(it[3] == false);

            auto it2 = v.begin() + 3;
            CHECK(it2 - it == 3);
        }

        SUBCASE("iterator modification") {
            auto it = v.begin() + 1;
            *it = true;
            CHECK(v[1] == true);
        }

        SUBCASE("const iterator") {
            const boolean_vector& cv = v;
            size_t count = 0;
            for (auto it = cv.cbegin(); it != cv.cend(); ++it) {
                ++count;
            }
            CHECK(count == 5);
        }
    }

    TEST_CASE("capacity") {
        SUBCASE("empty and size") {
            boolean_vector v;
            CHECK(v.empty());
            CHECK(v.size() == 0);

            v.push_back(true);
            CHECK_FALSE(v.empty());
            CHECK(v.size() == 1);
        }

        SUBCASE("reserve") {
            boolean_vector v;
            v.reserve(1000);
            CHECK(v.capacity() >= 1000);
            CHECK(v.size() == 0);
        }

        SUBCASE("shrink_to_fit") {
            boolean_vector v(1000, true);
            v.resize(10);
            CHECK(v.size() == 10);
            v.shrink_to_fit();
            CHECK(v.capacity() >= 10);
            CHECK(v.capacity() < 1000);
        }
    }

    TEST_CASE("modifiers") {
        SUBCASE("clear") {
            boolean_vector v(100, true);
            v.clear();
            CHECK(v.empty());
            CHECK(v.size() == 0);
        }

        SUBCASE("push_back") {
            boolean_vector v;
            for (int i = 0; i < 100; ++i) {
                v.push_back(i % 2 == 0);
            }
            CHECK(v.size() == 100);
            for (size_t i = 0; i < 100; ++i) {
                CHECK(v[i] == (i % 2 == 0));
            }
        }

        SUBCASE("push_back across word boundaries") {
            boolean_vector v;
            for (int i = 0; i < 200; ++i) {
                v.push_back(true);
            }
            CHECK(v.size() == 200);
            for (size_t i = 0; i < 200; ++i) {
                CHECK(v[i] == true);
            }
        }

        SUBCASE("pop_back") {
            boolean_vector v{true, false, true};
            v.pop_back();
            CHECK(v.size() == 2);
            CHECK(v.back() == false);
            v.pop_back();
            CHECK(v.size() == 1);
            CHECK(v.back() == true);
        }

        SUBCASE("resize expand with false") {
            boolean_vector v(10, true);
            v.resize(20);
            CHECK(v.size() == 20);
            for (size_t i = 0; i < 10; ++i) {
                CHECK(v[i] == true);
            }
            for (size_t i = 10; i < 20; ++i) {
                CHECK(v[i] == false);
            }
        }

        SUBCASE("resize expand with true") {
            boolean_vector v(10, false);
            v.resize(20, true);
            CHECK(v.size() == 20);
            for (size_t i = 0; i < 10; ++i) {
                CHECK(v[i] == false);
            }
            for (size_t i = 10; i < 20; ++i) {
                CHECK(v[i] == true);
            }
        }

        SUBCASE("resize shrink") {
            boolean_vector v(100, true);
            v.resize(10);
            CHECK(v.size() == 10);
            for (size_t i = 0; i < 10; ++i) {
                CHECK(v[i] == true);
            }
        }

        SUBCASE("swap") {
            boolean_vector v1(10, true);
            boolean_vector v2(20, false);
            v1.swap(v2);
            CHECK(v1.size() == 20);
            CHECK(v2.size() == 10);
            CHECK(v1[0] == false);
            CHECK(v2[0] == true);
        }

        SUBCASE("insert single") {
            boolean_vector v{true, true, true};
            auto it = v.insert(v.begin() + 1, false);
            CHECK(v.size() == 4);
            CHECK(*it == false);
            CHECK(v[0] == true);
            CHECK(v[1] == false);
            CHECK(v[2] == true);
            CHECK(v[3] == true);
        }

        SUBCASE("insert at beginning") {
            boolean_vector v{true, true};
            v.insert(v.begin(), false);
            CHECK(v.size() == 3);
            CHECK(v[0] == false);
            CHECK(v[1] == true);
            CHECK(v[2] == true);
        }

        SUBCASE("insert at end") {
            boolean_vector v{true, true};
            v.insert(v.end(), false);
            CHECK(v.size() == 3);
            CHECK(v[0] == true);
            CHECK(v[1] == true);
            CHECK(v[2] == false);
        }

        SUBCASE("erase single") {
            boolean_vector v{true, false, true, false};
            auto it = v.erase(v.begin() + 1);
            CHECK(v.size() == 3);
            CHECK(*it == true);
            CHECK(v[0] == true);
            CHECK(v[1] == true);
            CHECK(v[2] == false);
        }

        SUBCASE("erase range") {
            boolean_vector v{true, false, false, true, true};
            v.erase(v.begin() + 1, v.begin() + 3);
            CHECK(v.size() == 3);
            CHECK(v[0] == true);
            CHECK(v[1] == true);
            CHECK(v[2] == true);
        }

        SUBCASE("emplace_back") {
            boolean_vector v;
            v.emplace_back(true);
            v.emplace_back(false);
            CHECK(v.size() == 2);
            CHECK(v[0] == true);
            CHECK(v[1] == false);
        }
    }

    TEST_CASE("flip") {
        SUBCASE("flip entire vector") {
            boolean_vector v{true, false, true, false, true};
            v.flip();
            CHECK(v[0] == false);
            CHECK(v[1] == true);
            CHECK(v[2] == false);
            CHECK(v[3] == true);
            CHECK(v[4] == false);
        }

        SUBCASE("flip large vector") {
            boolean_vector v(1000, true);
            v.flip();
            for (size_t i = 0; i < 1000; ++i) {
                CHECK(v[i] == false);
            }
        }
    }

    TEST_CASE("comparison") {
        SUBCASE("equality") {
            boolean_vector v1{true, false, true};
            boolean_vector v2{true, false, true};
            boolean_vector v3{true, true, true};
            boolean_vector v4{true, false};

            CHECK(v1 == v2);
            CHECK_FALSE(v1 == v3);
            CHECK_FALSE(v1 == v4);
        }

        SUBCASE("inequality") {
            boolean_vector v1{true, false, true};
            boolean_vector v2{true, true, true};
            CHECK(v1 != v2);
        }

        SUBCASE("less than") {
            boolean_vector v1{false, true};
            boolean_vector v2{true, false};
            boolean_vector v3{false, true, false};

            CHECK(v1 < v2);  // false < true at index 0
            CHECK(v1 < v3);  // same prefix, but v1 is shorter
        }
    }

    TEST_CASE("edge cases") {
        SUBCASE("single element") {
            boolean_vector v(1, true);
            CHECK(v.size() == 1);
            CHECK(v[0] == true);
            v[0] = false;
            CHECK(v[0] == false);
        }

        SUBCASE("word boundary operations") {
            boolean_vector v(64, true);
            v.push_back(false);
            CHECK(v.size() == 65);
            CHECK(v[63] == true);
            CHECK(v[64] == false);
        }

        SUBCASE("operations at word boundaries") {
            boolean_vector v(128);
            for (size_t i = 0; i < 128; ++i) {
                v[i] = (i % 2 == 0);
            }
            for (size_t i = 0; i < 128; ++i) {
                CHECK(v[i] == (i % 2 == 0));
            }
        }

        SUBCASE("large vector") {
            boolean_vector v(10000);
            for (size_t i = 0; i < 10000; ++i) {
                v[i] = (i % 3 == 0);
            }
            for (size_t i = 0; i < 10000; ++i) {
                CHECK(v[i] == (i % 3 == 0));
            }
        }
    }

    TEST_CASE("std algorithm compatibility") {
        SUBCASE("std::count") {
            boolean_vector v{true, false, true, true, false};
            auto count = std::count(v.begin(), v.end(), true);
            CHECK(count == 3);
        }

        SUBCASE("std::find") {
            boolean_vector v{false, false, true, false};
            auto it = std::find(v.begin(), v.end(), true);
            CHECK(it != v.end());
            CHECK(it - v.begin() == 2);
        }

        SUBCASE("std::fill") {
            boolean_vector v(100, false);
            std::fill(v.begin(), v.end(), true);
            for (size_t i = 0; i < 100; ++i) {
                CHECK(v[i] == true);
            }
        }

        SUBCASE("std::copy") {
            boolean_vector src{true, false, true};
            boolean_vector dst(3);
            std::copy(src.begin(), src.end(), dst.begin());
            CHECK(dst[0] == true);
            CHECK(dst[1] == false);
            CHECK(dst[2] == true);
        }

        SUBCASE("manual reverse") {
            boolean_vector v{true, false, false, true, true};
            // std::reverse doesn't work with proxy iterators, test manual reverse
            size_t n = v.size();
            for (size_t i = 0; i < n / 2; ++i) {
                bool tmp = v[i];
                v[i] = v[n - 1 - i];
                v[n - 1 - i] = tmp;
            }
            CHECK(v[0] == true);
            CHECK(v[1] == true);
            CHECK(v[2] == false);
            CHECK(v[3] == false);
            CHECK(v[4] == true);
        }
    }

    TEST_CASE("SBO (Small Buffer Optimization)") {
        SUBCASE("within SBO capacity (<=64)") {
            boolean_vector v(64, true);
            CHECK(v.size() == 64);
            CHECK(v.capacity() == 64);
            for (size_t i = 0; i < 64; ++i) {
                CHECK(v[i] == true);
            }
            // Modify and verify
            v[0] = false;
            v[63] = false;
            CHECK(v[0] == false);
            CHECK(v[63] == false);
        }

        SUBCASE("exactly at SBO boundary") {
            boolean_vector v;
            for (int i = 0; i < 64; ++i) {
                v.push_back(i % 2 == 0);
            }
            CHECK(v.size() == 64);
            for (size_t i = 0; i < 64; ++i) {
                CHECK(v[i] == (i % 2 == 0));
            }
        }

        SUBCASE("crossing SBO boundary") {
            boolean_vector v(64, true);
            CHECK(v.size() == 64);

            // Push one more to cross SBO boundary
            v.push_back(false);
            CHECK(v.size() == 65);
            CHECK(v.capacity() > 64);

            // Verify all data is preserved
            for (size_t i = 0; i < 64; ++i) {
                CHECK(v[i] == true);
            }
            CHECK(v[64] == false);
        }

        SUBCASE("above SBO capacity") {
            boolean_vector v(100, true);
            CHECK(v.size() == 100);
            CHECK(v.capacity() >= 100);
            for (size_t i = 0; i < 100; ++i) {
                CHECK(v[i] == true);
            }
        }

        SUBCASE("shrink back to SBO") {
            boolean_vector v(1000, true);
            CHECK(v.capacity() >= 1000);

            v.resize(10);
            v.shrink_to_fit();
            CHECK(v.size() == 10);
            CHECK(v.capacity() <= 64);  // Should be back to SBO

            for (size_t i = 0; i < 10; ++i) {
                CHECK(v[i] == true);
            }
        }

        SUBCASE("shrink to empty then back to SBO") {
            boolean_vector v(1000, true);
            v.clear();
            v.shrink_to_fit();
            CHECK(v.capacity() == 64);
            CHECK(v.empty());

            // Can still use it
            v.push_back(true);
            CHECK(v.size() == 1);
            CHECK(v[0] == true);
        }
    }

    TEST_CASE("data pointer validity") {
        SUBCASE("data() returns valid pointer") {
            boolean_vector v(100, true);
            bool* ptr = v.data();
            CHECK(ptr != nullptr);
            for (size_t i = 0; i < 100; ++i) {
                CHECK(ptr[i] == true);
            }
        }

        SUBCASE("data() for SBO") {
            boolean_vector v(32, true);
            bool* ptr = v.data();
            CHECK(ptr != nullptr);
            ptr[0] = false;
            CHECK(v[0] == false);
        }

        SUBCASE("data() for heap") {
            boolean_vector v(1000, true);
            bool* ptr = v.data();
            CHECK(ptr != nullptr);
            ptr[500] = false;
            CHECK(v[500] == false);
        }

        SUBCASE("data pointer after reserve") {
            boolean_vector v(10, true);
            v.reserve(1000);
            bool* ptr = v.data();
            CHECK(ptr != nullptr);
            for (size_t i = 0; i < 10; ++i) {
                CHECK(ptr[i] == true);
            }
        }

        SUBCASE("iterator validity with data pointer") {
            boolean_vector v(100, true);
            auto begin = v.begin();
            auto end = v.end();
            CHECK(begin == v.data());
            CHECK(end == v.data() + v.size());
        }
    }

    TEST_CASE("count/any/all/none") {
        SUBCASE("count basic") {
            boolean_vector v{true, false, true, true, false};
            CHECK(v.count() == 3);
        }

        SUBCASE("count all true") {
            boolean_vector v(100, true);
            CHECK(v.count() == 100);
        }

        SUBCASE("count all false") {
            boolean_vector v(100, false);
            CHECK(v.count() == 0);
        }

        SUBCASE("count empty") {
            boolean_vector v;
            CHECK(v.count() == 0);
        }

        SUBCASE("count large vector") {
            boolean_vector v(10000);
            for (size_t i = 0; i < 10000; ++i) {
                v[i] = (i % 3 == 0);
            }
            size_t expected = (10000 + 2) / 3;  // ceil division
            CHECK(v.count() == expected);
        }

        SUBCASE("any basic") {
            boolean_vector v1{false, false, true, false};
            CHECK(v1.any() == true);

            boolean_vector v2{false, false, false};
            CHECK(v2.any() == false);
        }

        SUBCASE("any empty") {
            boolean_vector v;
            CHECK(v.any() == false);
        }

        SUBCASE("any large - last element true") {
            boolean_vector v(10000, false);
            v[9999] = true;
            CHECK(v.any() == true);
        }

        SUBCASE("all basic") {
            boolean_vector v1{true, true, true};
            CHECK(v1.all() == true);

            boolean_vector v2{true, false, true};
            CHECK(v2.all() == false);
        }

        SUBCASE("all empty") {
            boolean_vector v;
            CHECK(v.all() == true);  // vacuously true
        }

        SUBCASE("all large - last element false") {
            boolean_vector v(10000, true);
            v[9999] = false;
            CHECK(v.all() == false);
        }

        SUBCASE("none basic") {
            boolean_vector v1{false, false, false};
            CHECK(v1.none() == true);

            boolean_vector v2{false, true, false};
            CHECK(v2.none() == false);
        }

        SUBCASE("none empty") {
            boolean_vector v;
            CHECK(v.none() == true);
        }

        SUBCASE("test method") {
            boolean_vector v{true, false, true};
            CHECK(v.test(0) == true);
            CHECK(v.test(1) == false);
            CHECK(v.test(2) == true);
        }
    }

    TEST_CASE("copy and move with SBO/heap transitions") {
        SUBCASE("copy SBO to SBO") {
            boolean_vector v1(32, true);
            v1[0] = false;
            boolean_vector v2(v1);
            CHECK(v2.size() == 32);
            CHECK(v2[0] == false);
            CHECK(v2[1] == true);

            // Modify original, copy should be independent
            v1[1] = false;
            CHECK(v2[1] == true);
        }

        SUBCASE("copy heap to heap") {
            boolean_vector v1(1000, true);
            v1[500] = false;
            boolean_vector v2(v1);
            CHECK(v2.size() == 1000);
            CHECK(v2[500] == false);

            v1[501] = false;
            CHECK(v2[501] == true);
        }

        SUBCASE("move SBO") {
            boolean_vector v1(32, true);
            v1[0] = false;
            boolean_vector v2(std::move(v1));
            CHECK(v2.size() == 32);
            CHECK(v2[0] == false);
            CHECK(v1.empty());  // NOLINT
        }

        SUBCASE("move heap") {
            boolean_vector v1(1000, true);
            v1[500] = false;
            boolean_vector v2(std::move(v1));
            CHECK(v2.size() == 1000);
            CHECK(v2[500] == false);
            CHECK(v1.empty());  // NOLINT
        }

        SUBCASE("assign SBO to heap") {
            boolean_vector v1(32, true);
            boolean_vector v2(1000, false);
            v2 = v1;
            CHECK(v2.size() == 32);
            for (size_t i = 0; i < 32; ++i) {
                CHECK(v2[i] == true);
            }
        }

        SUBCASE("assign heap to SBO") {
            boolean_vector v1(1000, true);
            boolean_vector v2(32, false);
            v2 = v1;
            CHECK(v2.size() == 1000);
            for (size_t i = 0; i < 1000; ++i) {
                CHECK(v2[i] == true);
            }
        }

        SUBCASE("move assign SBO to heap") {
            boolean_vector v1(32, true);
            boolean_vector v2(1000, false);
            v2 = std::move(v1);
            CHECK(v2.size() == 32);
            CHECK(v1.empty());  // NOLINT
        }

        SUBCASE("move assign heap to SBO") {
            boolean_vector v1(1000, true);
            boolean_vector v2(32, false);
            v2 = std::move(v1);
            CHECK(v2.size() == 1000);
            CHECK(v1.empty());  // NOLINT
        }
    }

    TEST_CASE("swap with SBO/heap combinations") {
        SUBCASE("swap SBO with SBO") {
            boolean_vector v1(32, true);
            boolean_vector v2(48, false);
            v1.swap(v2);
            CHECK(v1.size() == 48);
            CHECK(v2.size() == 32);
            CHECK(v1[0] == false);
            CHECK(v2[0] == true);
        }

        SUBCASE("swap heap with heap") {
            boolean_vector v1(1000, true);
            boolean_vector v2(2000, false);
            v1.swap(v2);
            CHECK(v1.size() == 2000);
            CHECK(v2.size() == 1000);
            CHECK(v1[0] == false);
            CHECK(v2[0] == true);
        }

        SUBCASE("swap SBO with heap") {
            boolean_vector v1(32, true);
            boolean_vector v2(1000, false);
            v1.swap(v2);
            CHECK(v1.size() == 1000);
            CHECK(v2.size() == 32);
            CHECK(v1[0] == false);
            CHECK(v2[0] == true);
        }

        SUBCASE("swap heap with SBO") {
            boolean_vector v1(1000, true);
            boolean_vector v2(32, false);
            v1.swap(v2);
            CHECK(v1.size() == 32);
            CHECK(v2.size() == 1000);
            CHECK(v1[0] == false);
            CHECK(v2[0] == true);
        }
    }

    TEST_CASE("stress tests") {
        SUBCASE("many push_back operations") {
            boolean_vector v;
            for (int i = 0; i < 100000; ++i) {
                v.push_back(i % 2 == 0);
            }
            CHECK(v.size() == 100000);
            for (size_t i = 0; i < 100000; ++i) {
                CHECK(v[i] == (i % 2 == 0));
            }
        }

        SUBCASE("many resize operations") {
            boolean_vector v;
            for (int i = 0; i < 100; ++i) {
                v.resize(i * 100, true);
                CHECK(v.size() == static_cast<size_t>(i * 100));
            }
            v.resize(50);
            CHECK(v.size() == 50);
        }

        SUBCASE("alternating grow and shrink") {
            boolean_vector v;
            for (int i = 0; i < 50; ++i) {
                v.resize(1000);
                for (size_t j = 0; j < 1000; ++j) {
                    v[j] = (j % 3 == 0);
                }
                v.resize(10);
                CHECK(v.size() == 10);
                for (size_t j = 0; j < 10; ++j) {
                    CHECK(v[j] == (j % 3 == 0));
                }
            }
        }

        SUBCASE("random access pattern") {
            boolean_vector v(10000, false);
            std::mt19937 rng(42);

            // Random writes
            for (int i = 0; i < 5000; ++i) {
                size_t idx = rng() % 10000;
                v[idx] = true;
            }

            // Verify via count
            size_t count = 0;
            for (size_t i = 0; i < 10000; ++i) {
                if (v[i]) ++count;
            }
            CHECK(count == v.count());
        }
    }

    TEST_CASE("self-assignment") {
        SUBCASE("copy self-assignment SBO") {
            boolean_vector v(32, true);
            v[0] = false;
            v = v;  // NOLINT(clang-diagnostic-self-assign-overloaded)
            CHECK(v.size() == 32);
            CHECK(v[0] == false);
            CHECK(v[1] == true);
        }

        SUBCASE("copy self-assignment heap") {
            boolean_vector v(1000, true);
            v[500] = false;
            v = v;  // NOLINT(clang-diagnostic-self-assign-overloaded)
            CHECK(v.size() == 1000);
            CHECK(v[500] == false);
        }

        SUBCASE("move self-assignment") {
            boolean_vector v(100, true);
            v = std::move(v);  // NOLINT(clang-diagnostic-self-move)
            // After self-move, the object should be in a valid state
            // (behavior is implementation-defined, but should not crash)
        }
    }

    TEST_CASE("insert and erase edge cases") {
        SUBCASE("insert at empty vector") {
            boolean_vector v;
            v.insert(v.begin(), true);
            CHECK(v.size() == 1);
            CHECK(v[0] == true);
        }

        SUBCASE("insert multiple at beginning") {
            boolean_vector v{true, true};
            v.insert(v.begin(), 3, false);
            CHECK(v.size() == 5);
            CHECK(v[0] == false);
            CHECK(v[1] == false);
            CHECK(v[2] == false);
            CHECK(v[3] == true);
            CHECK(v[4] == true);
        }

        SUBCASE("insert crossing SBO boundary") {
            boolean_vector v(60, true);
            v.insert(v.begin() + 30, 10, false);
            CHECK(v.size() == 70);
            for (size_t i = 0; i < 30; ++i) {
                CHECK(v[i] == true);
            }
            for (size_t i = 30; i < 40; ++i) {
                CHECK(v[i] == false);
            }
            for (size_t i = 40; i < 70; ++i) {
                CHECK(v[i] == true);
            }
        }

        SUBCASE("erase all elements") {
            boolean_vector v{true, false, true};
            v.erase(v.begin(), v.end());
            CHECK(v.empty());
        }

        SUBCASE("erase first element repeatedly") {
            boolean_vector v{true, false, true, false, true};
            while (!v.empty()) {
                v.erase(v.begin());
            }
            CHECK(v.empty());
        }

        SUBCASE("erase last element repeatedly") {
            boolean_vector v{true, false, true, false, true};
            while (!v.empty()) {
                v.erase(v.end() - 1);
            }
            CHECK(v.empty());
        }
    }

    TEST_CASE("const correctness") {
        SUBCASE("const operator[]") {
            const boolean_vector v(100, true);
            CHECK(v[0] == true);
            CHECK(v[99] == true);
        }

        SUBCASE("const at") {
            const boolean_vector v(100, true);
            CHECK(v.at(0) == true);
            CHECK_THROWS_AS(v.at(100), std::out_of_range);
        }

        SUBCASE("const front/back") {
            const boolean_vector v{true, false, true};
            CHECK(v.front() == true);
            CHECK(v.back() == true);
        }

        SUBCASE("const data") {
            const boolean_vector v(100, true);
            const bool* ptr = v.data();
            CHECK(ptr != nullptr);
            CHECK(ptr[0] == true);
        }

        SUBCASE("const iterators") {
            const boolean_vector v{true, false, true};
            size_t count = 0;
            for (auto it = v.begin(); it != v.end(); ++it) {
                ++count;
            }
            CHECK(count == 3);
        }

        SUBCASE("const count/any/all/none/test") {
            const boolean_vector v{true, false, true};
            CHECK(v.count() == 2);
            CHECK(v.any() == true);
            CHECK(v.all() == false);
            CHECK(v.none() == false);
            CHECK(v.test(0) == true);
        }
    }
}

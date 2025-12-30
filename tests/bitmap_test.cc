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

#include "container/bitmap.hpp"

#include <algorithm>
#include <random>
#include <vector>

#include "doctest/doctest/doctest.h"

using namespace stdb::container;

// =============================================================================
// Dynamic bitmap tests
// =============================================================================

TEST_SUITE("bitmap") {
    TEST_CASE("construction") {
        SUBCASE("default constructor") {
            bitmap b;
            CHECK(b.size() == 0);
            CHECK(b.empty());
        }

        SUBCASE("size constructor") {
            bitmap b(100);
            CHECK(b.size() == 100);
            CHECK_FALSE(b.empty());
            CHECK(b.popcount() == 0);
        }

        SUBCASE("copy constructor") {
            bitmap b1(100);
            b1.set(10);
            b1.set(50);

            bitmap b2(b1);
            CHECK(b2.size() == 100);
            CHECK(b2.test(10));
            CHECK(b2.test(50));
            CHECK(b2.popcount() == 2);
        }

        SUBCASE("move constructor") {
            bitmap b1(100);
            b1.set(10);

            bitmap b2(std::move(b1));
            CHECK(b2.size() == 100);
            CHECK(b2.test(10));
            CHECK(b1.size() == 0);  // NOLINT(bugprone-use-after-move)
        }
    }

    TEST_CASE("single bit operations") {
        bitmap b(200);

        SUBCASE("set and test") {
            CHECK_FALSE(b.test(0));
            b.set(0);
            CHECK(b.test(0));

            CHECK_FALSE(b.test(63));
            b.set(63);
            CHECK(b.test(63));

            CHECK_FALSE(b.test(64));
            b.set(64);
            CHECK(b.test(64));

            CHECK_FALSE(b.test(127));
            b.set(127);
            CHECK(b.test(127));

            CHECK_FALSE(b.test(199));
            b.set(199);
            CHECK(b.test(199));
        }

        SUBCASE("clear_bit") {
            b.set(50);
            CHECK(b.test(50));
            b.clear_bit(50);
            CHECK_FALSE(b.test(50));
        }

        SUBCASE("flip") {
            CHECK_FALSE(b.test(75));
            b.flip(75);
            CHECK(b.test(75));
            b.flip(75);
            CHECK_FALSE(b.test(75));
        }

        SUBCASE("operator[]") {
            b.set(42);
            CHECK(b[42]);
            CHECK_FALSE(b[41]);
        }
    }

    TEST_CASE("popcount") {
        SUBCASE("empty bitmap") {
            bitmap b(1000);
            CHECK(b.popcount() == 0);
        }

        SUBCASE("single bit") {
            bitmap b(1000);
            b.set(500);
            CHECK(b.popcount() == 1);
        }

        SUBCASE("multiple bits") {
            bitmap b(1000);
            for (uint32_t i = 0; i < 100; ++i) {
                b.set(i * 10);
            }
            CHECK(b.popcount() == 100);
        }

        SUBCASE("all bits set") {
            bitmap b(128);
            for (uint32_t i = 0; i < 128; ++i) {
                b.set(i);
            }
            CHECK(b.popcount() == 128);
        }

        SUBCASE("large bitmap") {
            bitmap b(100000);
            for (uint32_t i = 0; i < 100000; i += 2) {
                b.set(i);
            }
            CHECK(b.popcount() == 50000);
        }
    }

    TEST_CASE("find_first and find_next") {
        SUBCASE("empty bitmap") {
            bitmap b(100);
            CHECK(b.find_first() == -1);
        }

        SUBCASE("single bit at start") {
            bitmap b(100);
            b.set(0);
            CHECK(b.find_first() == 0);
            CHECK(b.find_next(0) == -1);
        }

        SUBCASE("single bit in middle") {
            bitmap b(100);
            b.set(50);
            CHECK(b.find_first() == 50);
            CHECK(b.find_next(50) == -1);
        }

        SUBCASE("multiple bits") {
            bitmap b(200);
            b.set(10);
            b.set(64);
            b.set(100);
            b.set(150);

            CHECK(b.find_first() == 10);
            CHECK(b.find_next(10) == 64);
            CHECK(b.find_next(64) == 100);
            CHECK(b.find_next(100) == 150);
            CHECK(b.find_next(150) == -1);
        }

        SUBCASE("word boundary") {
            bitmap b(200);
            b.set(63);
            b.set(64);
            CHECK(b.find_first() == 63);
            CHECK(b.find_next(63) == 64);
        }
    }

    TEST_CASE("iterator") {
        bitmap b(200);
        b.set(5);
        b.set(64);
        b.set(127);
        b.set(128);

        std::vector<uint32_t> bits;
        for (uint32_t bit : b) {
            bits.push_back(bit);
        }

        CHECK(bits.size() == 4);
        CHECK(bits[0] == 5);
        CHECK(bits[1] == 64);
        CHECK(bits[2] == 127);
        CHECK(bits[3] == 128);
    }

    TEST_CASE("bitwise operations") {
        bitmap a(128);
        bitmap b(128);

        // a = {0, 1, 2, 64, 65}
        a.set(0);
        a.set(1);
        a.set(2);
        a.set(64);
        a.set(65);

        // b = {1, 2, 3, 65, 66}
        b.set(1);
        b.set(2);
        b.set(3);
        b.set(65);
        b.set(66);

        SUBCASE("and_") {
            bitmap c = a;
            c.and_(b);
            // Expected: {1, 2, 65}
            CHECK(c.popcount() == 3);
            CHECK(c.test(1));
            CHECK(c.test(2));
            CHECK(c.test(65));
            CHECK_FALSE(c.test(0));
            CHECK_FALSE(c.test(3));
        }

        SUBCASE("or_") {
            bitmap c = a;
            c.or_(b);
            // Expected: {0, 1, 2, 3, 64, 65, 66}
            CHECK(c.popcount() == 7);
            CHECK(c.test(0));
            CHECK(c.test(1));
            CHECK(c.test(2));
            CHECK(c.test(3));
            CHECK(c.test(64));
            CHECK(c.test(65));
            CHECK(c.test(66));
        }

        SUBCASE("xor_") {
            bitmap c = a;
            c.xor_(b);
            // Expected: {0, 3, 64, 66}
            CHECK(c.popcount() == 4);
            CHECK(c.test(0));
            CHECK(c.test(3));
            CHECK(c.test(64));
            CHECK(c.test(66));
        }

        SUBCASE("andnot_") {
            bitmap c = a;
            c.andnot_(b);
            // Expected: {0, 64} (a - b)
            CHECK(c.popcount() == 2);
            CHECK(c.test(0));
            CHECK(c.test(64));
        }

        SUBCASE("operators") {
            bitmap c = a;
            c &= b;
            CHECK(c.popcount() == 3);

            c = a;
            c |= b;
            CHECK(c.popcount() == 7);

            c = a;
            c ^= b;
            CHECK(c.popcount() == 4);
        }
    }

    TEST_CASE("large bitwise operations") {
        constexpr size_t size = 100000;
        bitmap a(size);
        bitmap b(size);

        std::mt19937 rng(42);
        for (size_t i = 0; i < 10000; ++i) {
            a.set(rng() % size);
            b.set(rng() % size);
        }

        size_t a_pop = a.popcount();
        size_t b_pop = b.popcount();

        SUBCASE("and_ consistency") {
            bitmap c = a;
            c.and_(b);
            // Verify result is subset of both
            for (uint32_t bit : c) {
                CHECK(a.test(bit));
                CHECK(b.test(bit));
            }
        }

        SUBCASE("or_ consistency") {
            bitmap c = a;
            c.or_(b);
            // Verify all original bits are present
            for (uint32_t bit : a) {
                CHECK(c.test(bit));
            }
            for (uint32_t bit : b) {
                CHECK(c.test(bit));
            }
        }
    }

    TEST_CASE("bulk IN operations") {
        bitmap b(1000);
        b.set(10);
        b.set(50);
        b.set(100);
        b.set(500);
        b.set(999);

        SUBCASE("in with array") {
            uint32_t ids[] = {5, 10, 20, 50, 100, 200, 500, 998, 999};
            uint32_t result[9];
            size_t count = b.in(ids, 9, result);

            CHECK(count == 5);
            CHECK(result[0] == 10);
            CHECK(result[1] == 50);
            CHECK(result[2] == 100);
            CHECK(result[3] == 500);
            CHECK(result[4] == 999);
        }

        SUBCASE("in with out of range ids") {
            uint32_t ids[] = {10, 1000, 2000};  // 1000 and 2000 are out of range
            uint32_t result[3];
            size_t count = b.in(ids, 3, result);

            CHECK(count == 1);
            CHECK(result[0] == 10);
        }

        SUBCASE("in with bitmap result") {
            uint32_t ids[] = {5, 10, 20, 50, 100};
            bitmap result;
            b.in(ids, 5, result);

            CHECK(result.size() == 5);
            CHECK_FALSE(result.test(0));  // id=5 not in b
            CHECK(result.test(1));         // id=10 in b
            CHECK_FALSE(result.test(2));  // id=20 not in b
            CHECK(result.test(3));         // id=50 in b
            CHECK(result.test(4));         // id=100 in b
        }
    }

    TEST_CASE("resize") {
        SUBCASE("grow") {
            bitmap b(100);
            b.set(50);
            b.resize(200);

            CHECK(b.size() == 200);
            CHECK(b.test(50));
            CHECK_FALSE(b.test(150));

            b.set(150);
            CHECK(b.test(150));
        }

        SUBCASE("shrink") {
            bitmap b(200);
            b.set(50);
            b.set(150);
            b.resize(100);

            CHECK(b.size() == 100);
            CHECK(b.test(50));
            // bit 150 should be gone (out of range)
        }
    }

    TEST_CASE("clear") {
        bitmap b(1000);
        for (uint32_t i = 0; i < 1000; ++i) {
            b.set(i);
        }
        CHECK(b.popcount() == 1000);

        b.clear();
        CHECK(b.popcount() == 0);
        CHECK(b.size() == 1000);
    }
}

// =============================================================================
// Static bitmap tests
// =============================================================================

TEST_SUITE("static_bitmap") {
    TEST_CASE("construction") {
        static_bitmap<100> b;
        CHECK(b.size() == 100);
        CHECK(b.popcount() == 0);
    }

    TEST_CASE("single bit operations") {
        static_bitmap<200> b;

        b.set(0);
        CHECK(b.test(0));

        b.set(63);
        CHECK(b.test(63));

        b.set(64);
        CHECK(b.test(64));

        b.set(199);
        CHECK(b.test(199));

        b.clear_bit(64);
        CHECK_FALSE(b.test(64));

        b.flip(100);
        CHECK(b.test(100));
        b.flip(100);
        CHECK_FALSE(b.test(100));
    }

    TEST_CASE("popcount") {
        static_bitmap<1000> b;
        for (uint32_t i = 0; i < 100; ++i) {
            b.set(i * 10);
        }
        CHECK(b.popcount() == 100);
    }

    TEST_CASE("find_first and find_next") {
        static_bitmap<200> b;
        b.set(10);
        b.set(64);
        b.set(100);

        CHECK(b.find_first() == 10);
        CHECK(b.find_next(10) == 64);
        CHECK(b.find_next(64) == 100);
        CHECK(b.find_next(100) == -1);
    }

    TEST_CASE("iterator") {
        static_bitmap<200> b;
        b.set(5);
        b.set(64);
        b.set(127);

        std::vector<uint32_t> bits;
        for (uint32_t bit : b) {
            bits.push_back(bit);
        }

        CHECK(bits.size() == 3);
        CHECK(bits[0] == 5);
        CHECK(bits[1] == 64);
        CHECK(bits[2] == 127);
    }

    TEST_CASE("bitwise operations") {
        static_bitmap<128> a;
        static_bitmap<128> b;

        a.set(0);
        a.set(1);
        a.set(64);

        b.set(1);
        b.set(2);
        b.set(64);

        SUBCASE("and_") {
            static_bitmap<128> c = a;
            c.and_(b);
            CHECK(c.popcount() == 2);  // {1, 64}
            CHECK(c.test(1));
            CHECK(c.test(64));
        }

        SUBCASE("or_") {
            static_bitmap<128> c = a;
            c.or_(b);
            CHECK(c.popcount() == 4);  // {0, 1, 2, 64}
        }

        SUBCASE("xor_") {
            static_bitmap<128> c = a;
            c.xor_(b);
            CHECK(c.popcount() == 2);  // {0, 2}
        }
    }

    TEST_CASE("fill and clear") {
        static_bitmap<100> b;

        b.fill();
        CHECK(b.popcount() == 100);
        CHECK(b.test(0));
        CHECK(b.test(99));

        b.clear();
        CHECK(b.popcount() == 0);
    }

    TEST_CASE("bulk IN operations") {
        static_bitmap<1000> b;
        b.set(10);
        b.set(50);
        b.set(100);

        uint32_t ids[] = {5, 10, 50, 100, 200};
        uint32_t result[5];
        size_t count = b.in(ids, 5, result);

        CHECK(count == 3);
        CHECK(result[0] == 10);
        CHECK(result[1] == 50);
        CHECK(result[2] == 100);
    }
}

// =============================================================================
// Edge case tests
// =============================================================================

TEST_SUITE("bitmap edge cases") {
    TEST_CASE("very small bitmap") {
        bitmap b(1);
        CHECK(b.size() == 1);
        b.set(0);
        CHECK(b.test(0));
        CHECK(b.popcount() == 1);
    }

    TEST_CASE("word-aligned sizes") {
        for (size_t size : {64, 128, 192, 256, 512}) {
            bitmap b(size);
            b.set(0);
            b.set(static_cast<uint32_t>(size - 1));
            CHECK(b.popcount() == 2);
        }
    }

    TEST_CASE("non-word-aligned sizes") {
        for (size_t size : {65, 127, 129, 255, 257}) {
            bitmap b(size);
            b.set(0);
            b.set(static_cast<uint32_t>(size - 1));
            CHECK(b.popcount() == 2);
            CHECK(b.test(static_cast<uint32_t>(size - 1)));
        }
    }
}

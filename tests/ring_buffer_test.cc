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

#include "container/ring_buffer.hpp"

#include <doctest/doctest.h>

#include <string>
#include <vector>

namespace stdb::container {
// NOLINTBEGIN

TEST_CASE("ring_buffer::basic") {
    SUBCASE("default constructor") {
        ring_buffer<int, 8> buf;
        CHECK_EQ(buf.empty(), true);
        CHECK_EQ(buf.size(), 0);
        CHECK_EQ(buf.capacity(), 8);
        CHECK_EQ(buf.full(), false);
        CHECK_EQ(buf.available(), 8);
    }

    SUBCASE("static_capacity matches template parameter") {
        CHECK_EQ(ring_buffer<int, 4>::static_capacity, 4);
        CHECK_EQ(ring_buffer<int, 16>::static_capacity, 16);
    }

    SUBCASE("push_back increases size") {
        ring_buffer<int, 4> buf;
        buf.push_back(1);
        CHECK_EQ(buf.size(), 1);
        buf.push_back(2);
        CHECK_EQ(buf.size(), 2);
        buf.push_back(3);
        buf.push_back(4);
        CHECK_EQ(buf.size(), 4);
        CHECK_EQ(buf.full(), true);
    }

    SUBCASE("overflow overwrites oldest") {
        ring_buffer<int, 4> buf;
        buf.push_back(1);
        buf.push_back(2);
        buf.push_back(3);
        buf.push_back(4);
        CHECK_EQ(buf.front(), 1);
        CHECK_EQ(buf.back(), 4);

        buf.push_back(5);          // Overwrites 1
        CHECK_EQ(buf.size(), 4);   // Still 4
        CHECK_EQ(buf.front(), 2);  // Now 2 is oldest
        CHECK_EQ(buf.back(), 5);

        buf.push_back(6);  // Overwrites 2
        CHECK_EQ(buf.front(), 3);
        CHECK_EQ(buf.back(), 6);
    }
}

TEST_CASE("ring_buffer::constructors") {
    SUBCASE("initializer list") {
        ring_buffer<int, 8> buf{1, 2, 3, 4, 5};
        CHECK_EQ(buf.size(), 5);
        CHECK_EQ(buf[0], 1);
        CHECK_EQ(buf[4], 5);
    }

    SUBCASE("initializer list overflow") {
        ring_buffer<int, 4> buf{1, 2, 3, 4, 5, 6};
        CHECK_EQ(buf.size(), 4);
        CHECK_EQ(buf[0], 3);  // 1, 2 were overwritten
        CHECK_EQ(buf[3], 6);
    }

    SUBCASE("copy constructor") {
        ring_buffer<int, 8> buf1{1, 2, 3, 4};
        ring_buffer<int, 8> buf2(buf1);
        CHECK_EQ(buf2.size(), 4);
        CHECK_EQ(buf1, buf2);
    }

    SUBCASE("move constructor") {
        ring_buffer<int, 8> buf1{1, 2, 3, 4};
        ring_buffer<int, 8> buf2(std::move(buf1));
        CHECK_EQ(buf2.size(), 4);
        CHECK_EQ(buf2[0], 1);
        CHECK_EQ(buf2[3], 4);
        CHECK_EQ(buf1.size(), 0);
    }

    SUBCASE("copy assignment") {
        ring_buffer<int, 8> buf1{1, 2, 3};
        ring_buffer<int, 8> buf2{10, 20, 30, 40, 50};
        buf2 = buf1;
        CHECK_EQ(buf2.size(), 3);
        CHECK_EQ(buf2, buf1);
    }

    SUBCASE("move assignment") {
        ring_buffer<int, 8> buf1{1, 2, 3, 4};
        ring_buffer<int, 8> buf2{10, 20};
        buf2 = std::move(buf1);
        CHECK_EQ(buf2.size(), 4);
        CHECK_EQ(buf2[0], 1);
        CHECK_EQ(buf1.size(), 0);
    }
}

TEST_CASE("ring_buffer::element_access") {
    ring_buffer<int, 8> buf{10, 20, 30, 40, 50};

    SUBCASE("operator[]") {
        CHECK_EQ(buf[0], 10);
        CHECK_EQ(buf[4], 50);
        buf[2] = 300;
        CHECK_EQ(buf[2], 300);
    }

    SUBCASE("at() valid") {
        CHECK_EQ(buf.at(0), 10);
        CHECK_EQ(buf.at(4), 50);
    }

    SUBCASE("at() throws on out of range") {
        CHECK_THROWS_AS(buf.at(5), std::out_of_range);
        CHECK_THROWS_AS(buf.at(100), std::out_of_range);
    }

    SUBCASE("front() and back()") {
        CHECK_EQ(buf.front(), 10);
        CHECK_EQ(buf.back(), 50);
        buf.front() = 100;
        buf.back() = 500;
        CHECK_EQ(buf.front(), 100);
        CHECK_EQ(buf.back(), 500);
    }
}

TEST_CASE("ring_buffer::modifiers") {
    SUBCASE("push_back lvalue") {
        ring_buffer<int, 8> buf;
        for (int i = 0; i < 5; ++i) {
            int val = i * 10;
            buf.push_back(val);
        }
        CHECK_EQ(buf.size(), 5);
        for (int i = 0; i < 5; ++i) {
            CHECK_EQ(buf[i], i * 10);
        }
    }

    SUBCASE("push_back rvalue") {
        ring_buffer<std::string, 4> buf;
        buf.push_back("hello");
        buf.push_back("world");
        CHECK_EQ(buf.size(), 2);
        CHECK_EQ(buf[0], "hello");
        CHECK_EQ(buf[1], "world");
    }

    SUBCASE("emplace_back") {
        ring_buffer<std::pair<int, std::string>, 4> buf;
        buf.emplace_back(1, "one");
        buf.emplace_back(2, "two");
        CHECK_EQ(buf.size(), 2);
        CHECK_EQ(buf[0].first, 1);
        CHECK_EQ(buf[0].second, "one");
    }

    SUBCASE("emplace_back with overflow") {
        ring_buffer<std::pair<int, std::string>, 2> buf;
        buf.emplace_back(1, "one");
        buf.emplace_back(2, "two");
        buf.emplace_back(3, "three");  // Overwrites (1, "one")
        CHECK_EQ(buf.size(), 2);
        CHECK_EQ(buf[0].first, 2);
        CHECK_EQ(buf[1].first, 3);
    }

    SUBCASE("try_push_back") {
        ring_buffer<int, 4> buf;
        CHECK_EQ(buf.try_push_back(1), true);
        CHECK_EQ(buf.try_push_back(2), true);
        CHECK_EQ(buf.try_push_back(3), true);
        CHECK_EQ(buf.try_push_back(4), true);
        CHECK_EQ(buf.try_push_back(5), false);  // Full, doesn't overwrite
        CHECK_EQ(buf.size(), 4);
        CHECK_EQ(buf.back(), 4);
    }

    SUBCASE("pop_front") {
        ring_buffer<int, 8> buf{1, 2, 3, 4, 5};
        buf.pop_front();
        CHECK_EQ(buf.size(), 4);
        CHECK_EQ(buf.front(), 2);
    }

    SUBCASE("pop_back") {
        ring_buffer<int, 8> buf{1, 2, 3, 4, 5};
        buf.pop_back();
        CHECK_EQ(buf.size(), 4);
        CHECK_EQ(buf.back(), 4);
    }

    SUBCASE("try_pop_front") {
        ring_buffer<int, 4> buf{1, 2, 3};
        auto val = buf.try_pop_front();
        CHECK_EQ(val.has_value(), true);
        CHECK_EQ(val.value(), 1);
        CHECK_EQ(buf.size(), 2);

        buf.try_pop_front();
        buf.try_pop_front();
        val = buf.try_pop_front();
        CHECK_EQ(val.has_value(), false);
    }

    SUBCASE("clear") {
        ring_buffer<int, 8> buf{1, 2, 3, 4, 5};
        buf.clear();
        CHECK_EQ(buf.empty(), true);
        CHECK_EQ(buf.size(), 0);
    }
}

TEST_CASE("ring_buffer::wrap_around") {
    SUBCASE("wrap around access") {
        ring_buffer<int, 4> buf;
        // Fill buffer
        buf.push_back(1);
        buf.push_back(2);
        buf.push_back(3);
        buf.push_back(4);

        // Pop two from front
        buf.pop_front();
        buf.pop_front();
        CHECK_EQ(buf.front(), 3);

        // Push two more (will wrap around)
        buf.push_back(5);
        buf.push_back(6);
        CHECK_EQ(buf.size(), 4);
        CHECK_EQ(buf[0], 3);
        CHECK_EQ(buf[1], 4);
        CHECK_EQ(buf[2], 5);
        CHECK_EQ(buf[3], 6);
    }

    SUBCASE("iteration after wrap around") {
        ring_buffer<int, 4> buf;
        buf.push_back(1);
        buf.push_back(2);
        buf.push_back(3);
        buf.push_back(4);
        buf.pop_front();
        buf.pop_front();
        buf.push_back(5);
        buf.push_back(6);

        std::vector<int> result;
        for (int val : buf) {
            result.push_back(val);
        }
        CHECK_EQ(result, std::vector<int>{3, 4, 5, 6});
    }
}

TEST_CASE("ring_buffer::iterators") {
    ring_buffer<int, 8> buf{1, 2, 3, 4, 5};

    SUBCASE("begin/end") {
        int expected = 1;
        for (auto it = buf.begin(); it != buf.end(); ++it) {
            CHECK_EQ(*it, expected++);
        }
    }

    SUBCASE("range-based for") {
        int sum = 0;
        for (int val : buf) {
            sum += val;
        }
        CHECK_EQ(sum, 15);
    }

    SUBCASE("reverse iterators") {
        int expected = 5;
        for (auto it = buf.rbegin(); it != buf.rend(); ++it) {
            CHECK_EQ(*it, expected--);
        }
    }

    SUBCASE("const iterators") {
        const ring_buffer<int, 8>& cbuf = buf;
        int expected = 1;
        for (auto it = cbuf.cbegin(); it != cbuf.cend(); ++it) {
            CHECK_EQ(*it, expected++);
        }
    }

    SUBCASE("iterator arithmetic") {
        auto it = buf.begin();
        CHECK_EQ(*(it + 2), 3);
        CHECK_EQ(it[3], 4);
        auto it2 = buf.end();
        CHECK_EQ(it2 - it, 5);
    }

    SUBCASE("iterator comparison") {
        auto it1 = buf.begin();
        auto it2 = buf.begin() + 2;
        CHECK_EQ(it1 < it2, true);
        CHECK_EQ(it1 <= it2, true);
        CHECK_EQ(it2 > it1, true);
        CHECK_EQ(it2 >= it1, true);
    }
}

TEST_CASE("ring_buffer::comparison") {
    SUBCASE("equality") {
        ring_buffer<int, 8> buf1{1, 2, 3};
        ring_buffer<int, 8> buf2{1, 2, 3};
        ring_buffer<int, 8> buf3{1, 2, 4};
        CHECK_EQ(buf1 == buf2, true);
        CHECK_EQ(buf1 == buf3, false);
        CHECK_EQ(buf1 != buf3, true);
    }

    SUBCASE("equality after wrap around") {
        ring_buffer<int, 4> buf1{1, 2, 3, 4};
        buf1.pop_front();
        buf1.push_back(5);  // Contains [2,3,4,5]

        ring_buffer<int, 4> buf2{2, 3, 4, 5};
        CHECK_EQ(buf1 == buf2, true);
    }
}

TEST_CASE("ring_buffer::complex_types") {
    SUBCASE("with std::string") {
        ring_buffer<std::string, 4> buf;
        buf.push_back("hello");
        buf.push_back("world");
        buf.emplace_back("test");

        CHECK_EQ(buf.size(), 3);
        CHECK_EQ(buf[0], "hello");
        CHECK_EQ(buf[1], "world");
        CHECK_EQ(buf[2], "test");

        // Overflow
        buf.push_back("a");
        buf.push_back("b");  // Overwrites "hello"
        CHECK_EQ(buf.front(), "world");
        CHECK_EQ(buf.back(), "b");
    }

    SUBCASE("move-only type") {
        struct MoveOnly
        {
            int value;
            MoveOnly(int v) : value(v) {}
            MoveOnly(const MoveOnly&) = delete;
            MoveOnly& operator=(const MoveOnly&) = delete;
            MoveOnly(MoveOnly&& other) noexcept : value(other.value) { other.value = -1; }
            MoveOnly& operator=(MoveOnly&& other) noexcept {
                value = other.value;
                other.value = -1;
                return *this;
            }
        };

        ring_buffer<MoveOnly, 4> buf;
        buf.emplace_back(1);
        buf.emplace_back(2);
        buf.push_back(MoveOnly(3));

        CHECK_EQ(buf.size(), 3);
        CHECK_EQ(buf[0].value, 1);
        CHECK_EQ(buf[1].value, 2);
        CHECK_EQ(buf[2].value, 3);
    }
}

TEST_CASE("ring_buffer::utility") {
    SUBCASE("contains") {
        ring_buffer<int, 8> buf{1, 2, 3, 4, 5};
        CHECK_EQ(buf.contains(3), true);
        CHECK_EQ(buf.contains(10), false);
    }

    SUBCASE("copy_to") {
        ring_buffer<int, 4> buf{1, 2, 3, 4};
        buf.pop_front();
        buf.push_back(5);  // [2, 3, 4, 5]

        std::vector<int> result;
        buf.copy_to(std::back_inserter(result));
        CHECK_EQ(result, std::vector<int>{2, 3, 4, 5});
    }

    SUBCASE("swap") {
        ring_buffer<int, 8> buf1{1, 2, 3};
        ring_buffer<int, 8> buf2{10, 20};
        buf1.swap(buf2);
        CHECK_EQ(buf1.size(), 2);
        CHECK_EQ(buf2.size(), 3);
        CHECK_EQ(buf1[0], 10);
        CHECK_EQ(buf2[0], 1);
    }

    SUBCASE("std::swap") {
        ring_buffer<int, 8> buf1{1, 2, 3};
        ring_buffer<int, 8> buf2{10, 20};
        std::swap(buf1, buf2);
        CHECK_EQ(buf1.size(), 2);
        CHECK_EQ(buf2.size(), 3);
    }
}

TEST_CASE("ring_buffer::fifo_usage") {
    SUBCASE("producer-consumer pattern") {
        ring_buffer<int, 4> buf;

        // Producer adds items
        buf.push_back(1);
        buf.push_back(2);

        // Consumer takes item
        CHECK_EQ(buf.front(), 1);
        buf.pop_front();

        // Producer adds more
        buf.push_back(3);
        buf.push_back(4);

        // Consumer processes all
        std::vector<int> consumed;
        while (!buf.empty()) {
            consumed.push_back(buf.front());
            buf.pop_front();
        }
        CHECK_EQ(consumed, std::vector<int>{2, 3, 4});
    }

    SUBCASE("sliding window") {
        ring_buffer<int, 3> window;

        // Simulate sliding window sum
        std::vector<int> data{1, 2, 3, 4, 5, 6};
        std::vector<int> window_sums;

        for (int val : data) {
            window.push_back(val);  // Automatically maintains window of 3
            if (window.size() == 3) {
                int sum = 0;
                for (int w : window) sum += w;
                window_sums.push_back(sum);
            }
        }

        // Windows: [1,2,3]=6, [2,3,4]=9, [3,4,5]=12, [4,5,6]=15
        CHECK_EQ(window_sums, std::vector<int>{6, 9, 12, 15});
    }

    SUBCASE("logging buffer - keep last N entries") {
        ring_buffer<std::string, 3> log;

        log.push_back("event1");
        log.push_back("event2");
        log.push_back("event3");
        log.push_back("event4");  // Overwrites event1
        log.push_back("event5");  // Overwrites event2

        std::vector<std::string> recent_logs;
        log.copy_to(std::back_inserter(recent_logs));
        CHECK_EQ(recent_logs, std::vector<std::string>{"event3", "event4", "event5"});
    }
}

TEST_CASE("ring_buffer::edge_cases") {
    SUBCASE("size 1 buffer") {
        ring_buffer<int, 1> buf;
        buf.push_back(1);
        CHECK_EQ(buf.front(), 1);
        CHECK_EQ(buf.back(), 1);
        CHECK_EQ(buf.full(), true);

        buf.push_back(2);  // Overwrites 1
        CHECK_EQ(buf.front(), 2);
        CHECK_EQ(buf.size(), 1);
    }

    SUBCASE("alternating push/pop") {
        ring_buffer<int, 4> buf;
        for (int i = 0; i < 100; ++i) {
            buf.push_back(i);
            if (buf.size() > 2) {
                buf.pop_front();
            }
        }
        // Should have last 2 elements
        CHECK_EQ(buf.size(), 2);
        CHECK_EQ(buf[0], 98);
        CHECK_EQ(buf[1], 99);
    }

    SUBCASE("empty buffer operations") {
        ring_buffer<int, 4> buf;
        auto val = buf.try_pop_front();
        CHECK_EQ(val.has_value(), false);
        CHECK_EQ(buf.empty(), true);
    }
}

// NOLINTEND
}  // namespace stdb::container

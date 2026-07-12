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

#include "container/devectra.hpp"

#include <doctest/doctest.h>

#include <array>  // std::array, used by the pmr buffer cases below
#include <string>
#include <vector>

namespace stdb::container {
// NOLINTBEGIN

TEST_CASE("devectra::basic") {
    SUBCASE("default constructor") {
        devectra<int> dv;
        CHECK_EQ(dv.empty(), true);
        CHECK_EQ(dv.size(), 0);
        CHECK_EQ(dv.capacity(), 0);
    }

    SUBCASE("push_back increases size") {
        devectra<int> dv;
        dv.push_back(1);
        CHECK_EQ(dv.size(), 1);
        dv.push_back(2);
        CHECK_EQ(dv.size(), 2);
        CHECK_EQ(dv[0], 1);
        CHECK_EQ(dv[1], 2);
    }

    SUBCASE("push_front increases size") {
        devectra<int> dv;
        dv.push_front(1);
        CHECK_EQ(dv.size(), 1);
        dv.push_front(2);
        CHECK_EQ(dv.size(), 2);
        CHECK_EQ(dv[0], 2);  // 2 was pushed to front
        CHECK_EQ(dv[1], 1);
    }

    SUBCASE("mixed push_front and push_back") {
        devectra<int> dv;
        dv.push_back(3);
        dv.push_front(2);
        dv.push_back(4);
        dv.push_front(1);
        // Should be [1, 2, 3, 4]
        CHECK_EQ(dv.size(), 4);
        CHECK_EQ(dv[0], 1);
        CHECK_EQ(dv[1], 2);
        CHECK_EQ(dv[2], 3);
        CHECK_EQ(dv[3], 4);
    }
}

TEST_CASE("devectra::constructors") {
    SUBCASE("size constructor") {
        devectra<int> dv(5);
        CHECK_EQ(dv.size(), 5);
        for (int i = 0; i < 5; ++i) {
            CHECK_EQ(dv[i], 0);
        }
    }

    SUBCASE("size and value constructor") {
        devectra<int> dv(5, 42);
        CHECK_EQ(dv.size(), 5);
        for (int i = 0; i < 5; ++i) {
            CHECK_EQ(dv[i], 42);
        }
    }

    SUBCASE("initializer list") {
        devectra<int> dv{1, 2, 3, 4, 5};
        CHECK_EQ(dv.size(), 5);
        CHECK_EQ(dv[0], 1);
        CHECK_EQ(dv[4], 5);
    }

    SUBCASE("iterator constructor") {
        std::vector<int> source{1, 2, 3, 4, 5};
        devectra<int> dv(source.begin(), source.end());
        CHECK_EQ(dv.size(), 5);
        for (size_t i = 0; i < source.size(); ++i) {
            CHECK_EQ(dv[i], source[i]);
        }
    }

    SUBCASE("copy constructor") {
        devectra<int> dv1{1, 2, 3, 4};
        devectra<int> dv2(dv1);
        CHECK_EQ(dv2.size(), 4);
        CHECK_EQ(dv1, dv2);
    }

    SUBCASE("move constructor") {
        devectra<int> dv1{1, 2, 3, 4};
        auto* old_data = dv1.data();
        devectra<int> dv2(std::move(dv1));
        CHECK_EQ(dv2.size(), 4);
        CHECK_EQ(dv2.data(), old_data);
        CHECK_EQ(dv2[0], 1);
        CHECK_EQ(dv1.size(), 0);
    }
}

TEST_CASE("devectra::assignment") {
    SUBCASE("copy assignment") {
        devectra<int> dv1{1, 2, 3};
        devectra<int> dv2{10, 20, 30, 40, 50};
        dv2 = dv1;
        CHECK_EQ(dv2.size(), 3);
        CHECK_EQ(dv2, dv1);
    }

    SUBCASE("move assignment") {
        devectra<int> dv1{1, 2, 3, 4};
        devectra<int> dv2{10, 20};
        auto* old_data = dv1.data();
        dv2 = std::move(dv1);
        CHECK_EQ(dv2.size(), 4);
        CHECK_EQ(dv2.data(), old_data);
        CHECK_EQ(dv1.size(), 0);
    }

    SUBCASE("self assignment") {
        devectra<int> dv{1, 2, 3, 4};
        auto* ptr = &dv;
        dv = *ptr;
        CHECK_EQ(dv.size(), 4);
        CHECK_EQ(dv[0], 1);
    }
}

TEST_CASE("devectra::element_access") {
    devectra<int> dv{10, 20, 30, 40, 50};

    SUBCASE("operator[]") {
        CHECK_EQ(dv[0], 10);
        CHECK_EQ(dv[4], 50);
        dv[2] = 300;
        CHECK_EQ(dv[2], 300);
    }

    SUBCASE("at() valid") {
        CHECK_EQ(dv.at(0), 10);
        CHECK_EQ(dv.at(4), 50);
    }

    SUBCASE("at() throws on out of range") {
        CHECK_THROWS_AS(dv.at(5), std::out_of_range);
        CHECK_THROWS_AS(dv.at(100), std::out_of_range);
    }

    SUBCASE("front() and back()") {
        CHECK_EQ(dv.front(), 10);
        CHECK_EQ(dv.back(), 50);
        dv.front() = 100;
        dv.back() = 500;
        CHECK_EQ(dv.front(), 100);
        CHECK_EQ(dv.back(), 500);
    }

    SUBCASE("data()") {
        int* ptr = dv.data();
        CHECK_NE(ptr, nullptr);
        CHECK_EQ(ptr[0], 10);
        CHECK_EQ(ptr[4], 50);
    }
}

TEST_CASE("devectra::front_operations") {
    SUBCASE("push_front multiple") {
        devectra<int> dv;
        for (int i = 5; i >= 1; --i) {
            dv.push_front(i);
        }
        CHECK_EQ(dv.size(), 5);
        for (int i = 0; i < 5; ++i) {
            CHECK_EQ(dv[i], i + 1);
        }
    }

    SUBCASE("emplace_front") {
        devectra<std::pair<int, std::string>> dv;
        dv.emplace_front(2, "two");
        dv.emplace_front(1, "one");
        CHECK_EQ(dv.size(), 2);
        CHECK_EQ(dv[0].first, 1);
        CHECK_EQ(dv[0].second, "one");
        CHECK_EQ(dv[1].first, 2);
    }

    SUBCASE("pop_front") {
        devectra<int> dv{1, 2, 3, 4, 5};
        dv.pop_front();
        CHECK_EQ(dv.size(), 4);
        CHECK_EQ(dv.front(), 2);
        dv.pop_front();
        CHECK_EQ(dv.front(), 3);
    }

    SUBCASE("alternating push_front and pop_front") {
        devectra<int> dv;
        for (int i = 0; i < 100; ++i) {
            dv.push_front(i);
            if (i % 2 == 1) {
                dv.pop_front();
            }
        }
        CHECK_EQ(dv.size(), 50);
    }
}

TEST_CASE("devectra::back_operations") {
    SUBCASE("push_back multiple") {
        devectra<int> dv;
        for (int i = 1; i <= 5; ++i) {
            dv.push_back(i);
        }
        CHECK_EQ(dv.size(), 5);
        for (int i = 0; i < 5; ++i) {
            CHECK_EQ(dv[i], i + 1);
        }
    }

    SUBCASE("emplace_back") {
        devectra<std::pair<int, std::string>> dv;
        dv.emplace_back(1, "one");
        dv.emplace_back(2, "two");
        CHECK_EQ(dv.size(), 2);
        CHECK_EQ(dv[0].first, 1);
        CHECK_EQ(dv[1].second, "two");
    }

    SUBCASE("pop_back") {
        devectra<int> dv{1, 2, 3, 4, 5};
        dv.pop_back();
        CHECK_EQ(dv.size(), 4);
        CHECK_EQ(dv.back(), 4);
        dv.pop_back();
        CHECK_EQ(dv.back(), 3);
    }
}

TEST_CASE("devectra::capacity") {
    SUBCASE("reserve") {
        devectra<int> dv;
        dv.reserve(100);
        CHECK_GE(dv.capacity(), 100);
        CHECK_EQ(dv.size(), 0);
    }

    SUBCASE("reserve_front") {
        devectra<int> dv{1, 2, 3};
        dv.reserve_front(10);
        CHECK_GE(dv.front_capacity(), 10);
    }

    SUBCASE("reserve_back") {
        devectra<int> dv{1, 2, 3};
        dv.reserve_back(10);
        CHECK_GE(dv.back_capacity(), 10);
    }

    SUBCASE("shrink_to_fit") {
        devectra<int> dv;
        dv.reserve(100);
        dv.push_back(1);
        dv.push_back(2);
        dv.push_back(3);
        dv.shrink_to_fit();
        CHECK_EQ(dv.capacity(), 3);
        CHECK_EQ(dv.size(), 3);
    }

    SUBCASE("shrink_to_fit empty") {
        devectra<int> dv;
        dv.reserve(100);
        dv.shrink_to_fit();
        CHECK_EQ(dv.capacity(), 0);
    }
}

TEST_CASE("devectra::modifiers") {
    SUBCASE("clear") {
        devectra<int> dv{1, 2, 3, 4, 5};
        dv.clear();
        CHECK_EQ(dv.empty(), true);
        // After clear, should still be able to push
        dv.push_front(10);
        dv.push_back(20);
        CHECK_EQ(dv.size(), 2);
    }

    SUBCASE("resize grow") {
        devectra<int> dv{1, 2, 3};
        dv.resize(6);
        CHECK_EQ(dv.size(), 6);
        CHECK_EQ(dv[0], 1);
        CHECK_EQ(dv[3], 0);
    }

    SUBCASE("resize shrink") {
        devectra<int> dv{1, 2, 3, 4, 5};
        dv.resize(2);
        CHECK_EQ(dv.size(), 2);
        CHECK_EQ(dv[0], 1);
        CHECK_EQ(dv[1], 2);
    }

    SUBCASE("resize with value") {
        devectra<int> dv{1, 2, 3};
        dv.resize(6, 42);
        CHECK_EQ(dv.size(), 6);
        CHECK_EQ(dv[0], 1);
        CHECK_EQ(dv[3], 42);
    }

    SUBCASE("assign count and value") {
        devectra<int> dv{1, 2, 3};
        dv.assign(5, 42);
        CHECK_EQ(dv.size(), 5);
        for (int i = 0; i < 5; ++i) {
            CHECK_EQ(dv[i], 42);
        }
    }

    SUBCASE("assign from iterators") {
        std::vector<int> source{10, 20, 30, 40, 50};
        devectra<int> dv{1, 2, 3};
        dv.assign(source.begin(), source.end());
        CHECK_EQ(dv.size(), 5);
        for (size_t i = 0; i < source.size(); ++i) {
            CHECK_EQ(dv[i], source[i]);
        }
    }

    SUBCASE("assign from initializer list") {
        devectra<int> dv{1, 2, 3};
        dv.assign({10, 20, 30, 40});
        CHECK_EQ(dv.size(), 4);
        CHECK_EQ(dv[0], 10);
        CHECK_EQ(dv[3], 40);
    }
}

TEST_CASE("devectra::iterators") {
    devectra<int> dv{1, 2, 3, 4, 5};

    SUBCASE("begin/end") {
        int expected = 1;
        for (auto it = dv.begin(); it != dv.end(); ++it) {
            CHECK_EQ(*it, expected++);
        }
    }

    SUBCASE("range-based for") {
        int sum = 0;
        for (int val : dv) {
            sum += val;
        }
        CHECK_EQ(sum, 15);
    }

    SUBCASE("reverse iterators") {
        int expected = 5;
        for (auto it = dv.rbegin(); it != dv.rend(); ++it) {
            CHECK_EQ(*it, expected--);
        }
    }

    SUBCASE("const iterators") {
        const devectra<int>& cdv = dv;
        int expected = 1;
        for (auto it = cdv.cbegin(); it != cdv.cend(); ++it) {
            CHECK_EQ(*it, expected++);
        }
    }
}

TEST_CASE("devectra::erase") {
    SUBCASE("erase single element") {
        devectra<int> dv{1, 2, 3, 4, 5};
        auto it = dv.erase(dv.begin() + 2);
        CHECK_EQ(dv.size(), 4);
        CHECK_EQ(*it, 4);
        CHECK_EQ(dv[0], 1);
        CHECK_EQ(dv[1], 2);
        CHECK_EQ(dv[2], 4);
        CHECK_EQ(dv[3], 5);
    }

    SUBCASE("erase range") {
        devectra<int> dv{1, 2, 3, 4, 5};
        auto it = dv.erase(dv.begin() + 1, dv.begin() + 4);
        CHECK_EQ(dv.size(), 2);
        CHECK_EQ(*it, 5);
        CHECK_EQ(dv[0], 1);
        CHECK_EQ(dv[1], 5);
    }

    SUBCASE("erase first element") {
        devectra<int> dv{1, 2, 3};
        dv.erase(dv.begin());
        CHECK_EQ(dv.size(), 2);
        CHECK_EQ(dv[0], 2);
    }

    SUBCASE("erase last element") {
        devectra<int> dv{1, 2, 3};
        dv.erase(dv.end() - 1);
        CHECK_EQ(dv.size(), 2);
        CHECK_EQ(dv[1], 2);
    }
}

TEST_CASE("devectra::insert") {
    SUBCASE("insert at beginning") {
        devectra<int> dv{2, 3, 4};
        dv.insert(dv.begin(), 1);
        CHECK_EQ(dv.size(), 4);
        CHECK_EQ(dv[0], 1);
        CHECK_EQ(dv[1], 2);
    }

    SUBCASE("insert in middle") {
        devectra<int> dv{1, 2, 4, 5};
        dv.insert(dv.begin() + 2, 3);
        CHECK_EQ(dv.size(), 5);
        CHECK_EQ(dv[2], 3);
    }

    SUBCASE("insert at end") {
        devectra<int> dv{1, 2, 3};
        dv.insert(dv.end(), 4);
        CHECK_EQ(dv.size(), 4);
        CHECK_EQ(dv[3], 4);
    }

    SUBCASE("insert rvalue") {
        devectra<std::string> dv{"a", "c"};
        dv.insert(dv.begin() + 1, "b");
        CHECK_EQ(dv.size(), 3);
        CHECK_EQ(dv[1], "b");
    }
}

TEST_CASE("devectra::swap") {
    SUBCASE("swap") {
        devectra<int> dv1{1, 2, 3};
        devectra<int> dv2{10, 20};
        dv1.swap(dv2);
        CHECK_EQ(dv1.size(), 2);
        CHECK_EQ(dv2.size(), 3);
        CHECK_EQ(dv1[0], 10);
        CHECK_EQ(dv2[0], 1);
    }

    SUBCASE("std::swap") {
        devectra<int> dv1{1, 2, 3};
        devectra<int> dv2{10, 20};
        std::swap(dv1, dv2);
        CHECK_EQ(dv1.size(), 2);
        CHECK_EQ(dv2.size(), 3);
    }
}

TEST_CASE("devectra::comparison") {
    SUBCASE("equality") {
        devectra<int> dv1{1, 2, 3};
        devectra<int> dv2{1, 2, 3};
        devectra<int> dv3{1, 2, 4};
        CHECK_EQ(dv1 == dv2, true);
        CHECK_EQ(dv1 == dv3, false);
        CHECK_EQ(dv1 != dv3, true);
    }

    SUBCASE("ordering") {
        devectra<int> dv1{1, 2, 3};
        devectra<int> dv2{1, 2, 4};
        devectra<int> dv3{1, 2};

        CHECK_EQ(dv1 < dv2, true);
        CHECK_EQ(dv2 > dv1, true);
        CHECK_EQ(dv3 < dv1, true);
        CHECK_EQ(dv1 <=> dv2, std::strong_ordering::less);
    }
}

TEST_CASE("devectra::complex_types") {
    SUBCASE("with std::string") {
        devectra<std::string> dv;
        dv.push_front("world");
        dv.push_front("hello");
        dv.push_back("!");

        CHECK_EQ(dv.size(), 3);
        CHECK_EQ(dv[0], "hello");
        CHECK_EQ(dv[1], "world");
        CHECK_EQ(dv[2], "!");
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

        devectra<MoveOnly> dv;
        dv.emplace_back(1);
        dv.emplace_front(0);
        dv.emplace_back(2);

        CHECK_EQ(dv.size(), 3);
        CHECK_EQ(dv[0].value, 0);
        CHECK_EQ(dv[1].value, 1);
        CHECK_EQ(dv[2].value, 2);
    }
}

TEST_CASE("devectra::std_erase") {
    SUBCASE("std::erase") {
        devectra<int> dv{1, 2, 3, 2, 4, 2, 5};
        auto count = std::erase(dv, 2);
        CHECK_EQ(count, 3);
        CHECK_EQ(dv.size(), 4);
        CHECK_EQ(dv[0], 1);
        CHECK_EQ(dv[1], 3);
        CHECK_EQ(dv[2], 4);
        CHECK_EQ(dv[3], 5);
    }

    SUBCASE("std::erase_if") {
        devectra<int> dv{1, 2, 3, 4, 5, 6};
        auto count = std::erase_if(dv, [](int x) { return x % 2 == 0; });
        CHECK_EQ(count, 3);
        CHECK_EQ(dv.size(), 3);
        CHECK_EQ(dv[0], 1);
        CHECK_EQ(dv[1], 3);
        CHECK_EQ(dv[2], 5);
    }
}

TEST_CASE("devectra::deque_like_usage") {
    SUBCASE("as double-ended queue") {
        devectra<int> dq;

        // Enqueue at back
        dq.push_back(1);
        dq.push_back(2);
        dq.push_back(3);

        // Dequeue from front
        CHECK_EQ(dq.front(), 1);
        dq.pop_front();
        CHECK_EQ(dq.front(), 2);
        dq.pop_front();

        // More enqueues
        dq.push_back(4);
        dq.push_back(5);

        // Dequeue all
        std::vector<int> result;
        while (!dq.empty()) {
            result.push_back(dq.front());
            dq.pop_front();
        }
        CHECK_EQ(result, std::vector<int>{3, 4, 5});
    }

    SUBCASE("undo/redo stack pattern") {
        devectra<std::string> history;

        // Actions
        history.push_back("action1");
        history.push_back("action2");
        history.push_back("action3");

        // Undo (pop from back)
        history.pop_back();
        CHECK_EQ(history.back(), "action2");

        // More actions
        history.push_back("action4");

        // Limit history size by removing oldest
        while (history.size() > 2) {
            history.pop_front();
        }

        CHECK_EQ(history.size(), 2);
        CHECK_EQ(history[0], "action2");
        CHECK_EQ(history[1], "action4");
    }
}

TEST_CASE("devectra::pmr") {
    SUBCASE("pmr type alias exists") {
        std::array<std::byte, 1024> buffer;
        std::pmr::monotonic_buffer_resource mbr(buffer.data(), buffer.size());
        stdb::pmr::devectra<int> dv(&mbr);
        dv.push_back(1);
        dv.push_back(2);
        dv.push_front(0);
        CHECK_EQ(dv.size(), 3);
        CHECK_EQ(dv[0], 0);
        CHECK_EQ(dv[1], 1);
        CHECK_EQ(dv[2], 2);
    }

    SUBCASE("allocator-only constructor") {
        std::array<std::byte, 1024> buffer;
        std::pmr::monotonic_buffer_resource mbr(buffer.data(), buffer.size());
        std::pmr::polymorphic_allocator<int> alloc(&mbr);
        devectra<int, std::pmr::polymorphic_allocator<int>> dv(alloc);
        CHECK(dv.empty());
        CHECK(dv.get_allocator() == alloc);
    }

    SUBCASE("constructor with size and allocator") {
        std::array<std::byte, 1024> buffer;
        std::pmr::monotonic_buffer_resource mbr(buffer.data(), buffer.size());
        std::pmr::polymorphic_allocator<int> alloc(&mbr);
        devectra<int, std::pmr::polymorphic_allocator<int>> dv(5, alloc);
        CHECK_EQ(dv.size(), 5);
        CHECK(dv.get_allocator() == alloc);
    }

    SUBCASE("constructor with size, value and allocator") {
        std::array<std::byte, 1024> buffer;
        std::pmr::monotonic_buffer_resource mbr(buffer.data(), buffer.size());
        std::pmr::polymorphic_allocator<int> alloc(&mbr);
        devectra<int, std::pmr::polymorphic_allocator<int>> dv(5, 42, alloc);
        CHECK_EQ(dv.size(), 5);
        for (int i = 0; i < 5; ++i) {
            CHECK_EQ(dv[i], 42);
        }
        CHECK(dv.get_allocator() == alloc);
    }

    SUBCASE("copy constructor uses select_on_container_copy_construction") {
        std::array<std::byte, 1024> buffer1;
        std::pmr::monotonic_buffer_resource mbr1(buffer1.data(), buffer1.size());
        std::pmr::polymorphic_allocator<int> alloc1(&mbr1);

        devectra<int, std::pmr::polymorphic_allocator<int>> dv1(alloc1);
        dv1.push_back(1);
        dv1.push_back(2);
        dv1.push_back(3);

        // Copy constructor - PMR uses default resource for copies
        devectra<int, std::pmr::polymorphic_allocator<int>> dv2(dv1);
        CHECK_EQ(dv2.size(), 3);
        CHECK_EQ(dv2[0], 1);
        // PMR polymorphic_allocator selects default resource for copies
        CHECK(dv2.get_allocator().resource() == std::pmr::get_default_resource());
    }

    SUBCASE("allocator-extended copy constructor") {
        std::array<std::byte, 1024> buffer1, buffer2;
        std::pmr::monotonic_buffer_resource mbr1(buffer1.data(), buffer1.size());
        std::pmr::monotonic_buffer_resource mbr2(buffer2.data(), buffer2.size());
        std::pmr::polymorphic_allocator<int> alloc1(&mbr1);
        std::pmr::polymorphic_allocator<int> alloc2(&mbr2);

        devectra<int, std::pmr::polymorphic_allocator<int>> dv1(alloc1);
        dv1.push_back(1);
        dv1.push_back(2);

        devectra<int, std::pmr::polymorphic_allocator<int>> dv2(dv1, alloc2);
        CHECK_EQ(dv2.size(), 2);
        CHECK_EQ(dv2[0], 1);
        CHECK_EQ(dv2[1], 2);
        CHECK(dv2.get_allocator() == alloc2);
    }

    SUBCASE("allocator-extended move constructor with same allocator") {
        std::array<std::byte, 2048> buffer;
        std::pmr::monotonic_buffer_resource mbr(buffer.data(), buffer.size());
        std::pmr::polymorphic_allocator<int> alloc(&mbr);

        devectra<int, std::pmr::polymorphic_allocator<int>> dv1(alloc);
        dv1.push_back(1);
        dv1.push_back(2);
        auto* old_data = dv1.data();

        devectra<int, std::pmr::polymorphic_allocator<int>> dv2(std::move(dv1), alloc);
        CHECK_EQ(dv2.size(), 2);
        CHECK_EQ(dv2.data(), old_data);  // Resources stolen
        CHECK(dv1.empty());
    }

    SUBCASE("allocator-extended move constructor with different allocator") {
        std::array<std::byte, 1024> buffer1, buffer2;
        std::pmr::monotonic_buffer_resource mbr1(buffer1.data(), buffer1.size());
        std::pmr::monotonic_buffer_resource mbr2(buffer2.data(), buffer2.size());
        std::pmr::polymorphic_allocator<int> alloc1(&mbr1);
        std::pmr::polymorphic_allocator<int> alloc2(&mbr2);

        devectra<int, std::pmr::polymorphic_allocator<int>> dv1(alloc1);
        dv1.push_back(1);
        dv1.push_back(2);
        auto* old_data = dv1.data();

        devectra<int, std::pmr::polymorphic_allocator<int>> dv2(std::move(dv1), alloc2);
        CHECK_EQ(dv2.size(), 2);
        CHECK_NE(dv2.data(), old_data);  // Cannot steal, must copy
        CHECK_EQ(dv2[0], 1);
        CHECK_EQ(dv2[1], 2);
        CHECK(dv2.get_allocator() == alloc2);
    }

    SUBCASE("copy assignment does not propagate allocator for PMR") {
        std::array<std::byte, 1024> buffer1, buffer2;
        std::pmr::monotonic_buffer_resource mbr1(buffer1.data(), buffer1.size());
        std::pmr::monotonic_buffer_resource mbr2(buffer2.data(), buffer2.size());
        std::pmr::polymorphic_allocator<int> alloc1(&mbr1);
        std::pmr::polymorphic_allocator<int> alloc2(&mbr2);

        devectra<int, std::pmr::polymorphic_allocator<int>> dv1(alloc1);
        dv1.push_back(1);
        dv1.push_back(2);

        devectra<int, std::pmr::polymorphic_allocator<int>> dv2(alloc2);
        dv2.push_back(10);

        dv2 = dv1;
        CHECK_EQ(dv2.size(), 2);
        CHECK_EQ(dv2[0], 1);
        // Allocator not propagated for PMR
        CHECK(dv2.get_allocator() == alloc2);
    }

    SUBCASE("move assignment with same allocator steals resources") {
        std::array<std::byte, 2048> buffer;
        std::pmr::monotonic_buffer_resource mbr(buffer.data(), buffer.size());
        std::pmr::polymorphic_allocator<int> alloc(&mbr);

        devectra<int, std::pmr::polymorphic_allocator<int>> dv1(alloc);
        dv1.push_back(1);
        dv1.push_back(2);
        auto* old_data = dv1.data();

        devectra<int, std::pmr::polymorphic_allocator<int>> dv2(alloc);
        dv2.push_back(10);

        dv2 = std::move(dv1);
        CHECK_EQ(dv2.size(), 2);
        CHECK_EQ(dv2.data(), old_data);  // Resources stolen
        CHECK(dv1.empty());
    }

    SUBCASE("move assignment with different allocator moves elements") {
        std::array<std::byte, 1024> buffer1, buffer2;
        std::pmr::monotonic_buffer_resource mbr1(buffer1.data(), buffer1.size());
        std::pmr::monotonic_buffer_resource mbr2(buffer2.data(), buffer2.size());
        std::pmr::polymorphic_allocator<int> alloc1(&mbr1);
        std::pmr::polymorphic_allocator<int> alloc2(&mbr2);

        devectra<int, std::pmr::polymorphic_allocator<int>> dv1(alloc1);
        dv1.push_back(1);
        dv1.push_back(2);
        auto* old_data = dv1.data();

        devectra<int, std::pmr::polymorphic_allocator<int>> dv2(alloc2);
        dv2.push_back(10);

        dv2 = std::move(dv1);
        CHECK_EQ(dv2.size(), 2);
        CHECK_NE(dv2.data(), old_data);  // Cannot steal, must move elements
        CHECK_EQ(dv2[0], 1);
        CHECK_EQ(dv2[1], 2);
        CHECK(dv2.get_allocator() == alloc2);  // Allocator not propagated
    }

    SUBCASE("swap with same allocator") {
        std::array<std::byte, 2048> buffer;
        std::pmr::monotonic_buffer_resource mbr(buffer.data(), buffer.size());
        std::pmr::polymorphic_allocator<int> alloc(&mbr);

        devectra<int, std::pmr::polymorphic_allocator<int>> dv1(alloc);
        dv1.push_back(1);
        dv1.push_back(2);
        auto* data1 = dv1.data();

        devectra<int, std::pmr::polymorphic_allocator<int>> dv2(alloc);
        dv2.push_back(10);
        dv2.push_back(20);
        dv2.push_back(30);
        auto* data2 = dv2.data();

        dv1.swap(dv2);
        CHECK_EQ(dv1.size(), 3);
        CHECK_EQ(dv2.size(), 2);
        CHECK_EQ(dv1.data(), data2);
        CHECK_EQ(dv2.data(), data1);
    }
}

// NOLINTEND
}  // namespace stdb::container

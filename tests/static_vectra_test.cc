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

#include "container/static_vectra.hpp"

#include <doctest/doctest.h>

#include <string>
#include <vector>

namespace stdb::container {
// NOLINTBEGIN

TEST_CASE("static_vectra::basic") {
    SUBCASE("default constructor") {
        static_vectra<int, 8> vec;
        CHECK_EQ(vec.empty(), true);
        CHECK_EQ(vec.size(), 0);
        CHECK_EQ(vec.capacity(), 8);
        CHECK_EQ(vec.max_size(), 8);
        CHECK_EQ(vec.full(), false);
        CHECK_EQ(vec.available(), 8);
        CHECK_NE(vec.data(), nullptr);
    }

    SUBCASE("static_capacity matches template parameter") {
        CHECK_EQ(static_vectra<int, 4>::static_capacity, 4);
        CHECK_EQ(static_vectra<int, 16>::static_capacity, 16);
        CHECK_EQ(static_vectra<double, 8>::static_capacity, 8);
    }

    SUBCASE("capacity is constexpr") {
        constexpr auto cap = static_vectra<int, 10>::capacity();
        CHECK_EQ(cap, 10);
    }

    SUBCASE("fills to capacity") {
        static_vectra<int, 4> vec;
        for (int i = 0; i < 4; ++i) {
            vec.push_back(i);
            CHECK_EQ(vec.available(), 3 - i);
        }
        CHECK_EQ(vec.size(), 4);
        CHECK_EQ(vec.full(), true);
        CHECK_EQ(vec.available(), 0);
    }

    SUBCASE("throws on overflow") {
        static_vectra<int, 4> vec{1, 2, 3, 4};
        CHECK_EQ(vec.full(), true);
        CHECK_THROWS_AS(vec.push_back(5), std::length_error);
    }
}

TEST_CASE("static_vectra::constructors") {
    SUBCASE("size constructor") {
        static_vectra<int, 8> vec(5);
        CHECK_EQ(vec.size(), 5);
        for (int i = 0; i < 5; ++i) {
            CHECK_EQ(vec[i], 0);
        }
    }

    SUBCASE("size constructor - at capacity") {
        static_vectra<int, 4> vec(4);
        CHECK_EQ(vec.size(), 4);
        CHECK_EQ(vec.full(), true);
    }

    SUBCASE("size constructor - exceeds capacity throws") {
        CHECK_THROWS_AS((static_vectra<int, 4>(5)), std::length_error);
    }

    SUBCASE("size and value constructor") {
        static_vectra<int, 8> vec(5, 42);
        CHECK_EQ(vec.size(), 5);
        for (int i = 0; i < 5; ++i) {
            CHECK_EQ(vec[i], 42);
        }
    }

    SUBCASE("initializer list") {
        static_vectra<int, 8> vec{1, 2, 3, 4, 5};
        CHECK_EQ(vec.size(), 5);
        CHECK_EQ(vec[0], 1);
        CHECK_EQ(vec[4], 5);
    }

    SUBCASE("initializer list - exceeds capacity throws") {
        CHECK_THROWS_AS((static_vectra<int, 4>{1, 2, 3, 4, 5}), std::length_error);
    }

    SUBCASE("iterator constructor") {
        std::vector<int> source{1, 2, 3, 4, 5};
        static_vectra<int, 8> vec(source.begin(), source.end());
        CHECK_EQ(vec.size(), 5);
        for (size_t i = 0; i < source.size(); ++i) {
            CHECK_EQ(vec[i], source[i]);
        }
    }
}

TEST_CASE("static_vectra::copy_and_move") {
    SUBCASE("copy constructor") {
        static_vectra<int, 8> vec1{1, 2, 3, 4};
        static_vectra<int, 8> vec2(vec1);
        CHECK_EQ(vec2.size(), 4);
        CHECK_EQ(vec1, vec2);
    }

    SUBCASE("move constructor") {
        static_vectra<int, 8> vec1{1, 2, 3, 4};
        static_vectra<int, 8> vec2(std::move(vec1));
        CHECK_EQ(vec2.size(), 4);
        CHECK_EQ(vec2[0], 1);
        CHECK_EQ(vec2[3], 4);
        CHECK_EQ(vec1.size(), 0);  // Source should be empty
    }

    SUBCASE("copy assignment") {
        static_vectra<int, 8> vec1{1, 2, 3};
        static_vectra<int, 8> vec2{10, 20, 30, 40, 50};
        vec2 = vec1;
        CHECK_EQ(vec2.size(), 3);
        CHECK_EQ(vec2, vec1);
    }

    SUBCASE("move assignment") {
        static_vectra<int, 8> vec1{1, 2, 3, 4};
        static_vectra<int, 8> vec2{10, 20};
        vec2 = std::move(vec1);
        CHECK_EQ(vec2.size(), 4);
        CHECK_EQ(vec2[0], 1);
        CHECK_EQ(vec1.size(), 0);
    }

    SUBCASE("self assignment") {
        static_vectra<int, 8> vec{1, 2, 3, 4};
        auto* ptr = &vec;
        vec = *ptr;
        CHECK_EQ(vec.size(), 4);
        CHECK_EQ(vec[0], 1);
    }
}

TEST_CASE("static_vectra::modifiers") {
    SUBCASE("push_back lvalue") {
        static_vectra<int, 8> vec;
        for (int i = 0; i < 5; ++i) {
            int val = i * 10;
            vec.push_back(val);
        }
        CHECK_EQ(vec.size(), 5);
        for (int i = 0; i < 5; ++i) {
            CHECK_EQ(vec[i], i * 10);
        }
    }

    SUBCASE("push_back rvalue") {
        static_vectra<std::string, 4> vec;
        vec.push_back("hello");
        vec.push_back("world");
        CHECK_EQ(vec.size(), 2);
        CHECK_EQ(vec[0], "hello");
        CHECK_EQ(vec[1], "world");
    }

    SUBCASE("emplace_back") {
        static_vectra<std::pair<int, std::string>, 4> vec;
        vec.emplace_back(1, "one");
        vec.emplace_back(2, "two");
        CHECK_EQ(vec.size(), 2);
        CHECK_EQ(vec[0].first, 1);
        CHECK_EQ(vec[0].second, "one");
    }

    SUBCASE("pop_back") {
        static_vectra<int, 8> vec{1, 2, 3, 4, 5};
        vec.pop_back();
        CHECK_EQ(vec.size(), 4);
        CHECK_EQ(vec.back(), 4);
    }

    SUBCASE("clear") {
        static_vectra<int, 8> vec{1, 2, 3, 4, 5};
        vec.clear();
        CHECK_EQ(vec.empty(), true);
        CHECK_EQ(vec.capacity(), 8);
    }

    SUBCASE("resize - grow") {
        static_vectra<int, 8> vec{1, 2, 3};
        vec.resize(6);
        CHECK_EQ(vec.size(), 6);
        CHECK_EQ(vec[0], 1);
        CHECK_EQ(vec[3], 0);
    }

    SUBCASE("resize - shrink") {
        static_vectra<int, 8> vec{1, 2, 3, 4, 5};
        vec.resize(2);
        CHECK_EQ(vec.size(), 2);
        CHECK_EQ(vec[0], 1);
        CHECK_EQ(vec[1], 2);
    }

    SUBCASE("resize - exceeds capacity throws") {
        static_vectra<int, 4> vec{1, 2};
        CHECK_THROWS_AS(vec.resize(5), std::length_error);
    }

    SUBCASE("resize with value") {
        static_vectra<int, 8> vec{1, 2, 3};
        vec.resize(6, 42);
        CHECK_EQ(vec.size(), 6);
        CHECK_EQ(vec[0], 1);
        CHECK_EQ(vec[3], 42);
        CHECK_EQ(vec[5], 42);
    }
}

TEST_CASE("static_vectra::reserve_noop") {
    SUBCASE("reserve within capacity is noop") {
        static_vectra<int, 8> vec{1, 2, 3};
        vec.reserve(6);  // Should not throw
        CHECK_EQ(vec.size(), 3);
        CHECK_EQ(vec.capacity(), 8);
    }

    SUBCASE("reserve exceeds capacity throws") {
        static_vectra<int, 4> vec{1, 2};
        CHECK_THROWS_AS(vec.reserve(10), std::length_error);
    }

    SUBCASE("shrink_to_fit is noop") {
        static_vectra<int, 8> vec{1, 2, 3};
        vec.shrink_to_fit();
        CHECK_EQ(vec.capacity(), 8);  // Unchanged
    }
}

TEST_CASE("static_vectra::element_access") {
    static_vectra<int, 8> vec{10, 20, 30, 40, 50};

    SUBCASE("operator[]") {
        CHECK_EQ(vec[0], 10);
        CHECK_EQ(vec[4], 50);
        vec[2] = 300;
        CHECK_EQ(vec[2], 300);
    }

    SUBCASE("at() valid") {
        CHECK_EQ(vec.at(0), 10);
        CHECK_EQ(vec.at(4), 50);
    }

    SUBCASE("at() throws on out of range") {
        CHECK_THROWS_AS(vec.at(5), std::out_of_range);
        CHECK_THROWS_AS(vec.at(100), std::out_of_range);
    }

    SUBCASE("front() and back()") {
        CHECK_EQ(vec.front(), 10);
        CHECK_EQ(vec.back(), 50);
        vec.front() = 100;
        vec.back() = 500;
        CHECK_EQ(vec.front(), 100);
        CHECK_EQ(vec.back(), 500);
    }

    SUBCASE("data()") {
        int* ptr = vec.data();
        CHECK_NE(ptr, nullptr);
        CHECK_EQ(ptr[0], 10);
        CHECK_EQ(ptr[4], 50);
    }
}

TEST_CASE("static_vectra::iterators") {
    static_vectra<int, 8> vec{1, 2, 3, 4, 5};

    SUBCASE("begin/end") {
        int expected = 1;
        for (auto it = vec.begin(); it != vec.end(); ++it) {
            CHECK_EQ(*it, expected++);
        }
    }

    SUBCASE("range-based for") {
        int sum = 0;
        for (int val : vec) {
            sum += val;
        }
        CHECK_EQ(sum, 15);
    }

    SUBCASE("reverse iterators") {
        int expected = 5;
        for (auto it = vec.rbegin(); it != vec.rend(); ++it) {
            CHECK_EQ(*it, expected--);
        }
    }

    SUBCASE("const iterators") {
        const static_vectra<int, 8>& cvec = vec;
        int expected = 1;
        for (auto it = cvec.cbegin(); it != cvec.cend(); ++it) {
            CHECK_EQ(*it, expected++);
        }
    }
}

TEST_CASE("static_vectra::erase") {
    SUBCASE("erase single element") {
        static_vectra<int, 8> vec{1, 2, 3, 4, 5};
        auto it = vec.erase(vec.begin() + 2);
        CHECK_EQ(vec.size(), 4);
        CHECK_EQ(*it, 4);
        CHECK_EQ(vec[0], 1);
        CHECK_EQ(vec[1], 2);
        CHECK_EQ(vec[2], 4);
        CHECK_EQ(vec[3], 5);
    }

    SUBCASE("erase range") {
        static_vectra<int, 8> vec{1, 2, 3, 4, 5};
        auto it = vec.erase(vec.begin() + 1, vec.begin() + 4);
        CHECK_EQ(vec.size(), 2);
        CHECK_EQ(*it, 5);
        CHECK_EQ(vec[0], 1);
        CHECK_EQ(vec[1], 5);
    }

    SUBCASE("erase first element") {
        static_vectra<int, 8> vec{1, 2, 3};
        vec.erase(vec.begin());
        CHECK_EQ(vec.size(), 2);
        CHECK_EQ(vec[0], 2);
        CHECK_EQ(vec[1], 3);
    }

    SUBCASE("erase last element") {
        static_vectra<int, 8> vec{1, 2, 3};
        vec.erase(vec.end() - 1);
        CHECK_EQ(vec.size(), 2);
        CHECK_EQ(vec[0], 1);
        CHECK_EQ(vec[1], 2);
    }
}

TEST_CASE("static_vectra::insert") {
    SUBCASE("insert at beginning") {
        static_vectra<int, 8> vec{2, 3, 4};
        vec.insert(vec.begin(), 1);
        CHECK_EQ(vec.size(), 4);
        CHECK_EQ(vec[0], 1);
        CHECK_EQ(vec[1], 2);
    }

    SUBCASE("insert in middle") {
        static_vectra<int, 8> vec{1, 2, 4, 5};
        vec.insert(vec.begin() + 2, 3);
        CHECK_EQ(vec.size(), 5);
        CHECK_EQ(vec[2], 3);
    }

    SUBCASE("insert at end") {
        static_vectra<int, 8> vec{1, 2, 3};
        vec.insert(vec.end(), 4);
        CHECK_EQ(vec.size(), 4);
        CHECK_EQ(vec[3], 4);
    }

    SUBCASE("insert on full container throws") {
        static_vectra<int, 4> vec{1, 2, 3, 4};
        CHECK_THROWS_AS(vec.insert(vec.begin(), 0), std::length_error);
    }

    SUBCASE("insert count and value") {
        static_vectra<int, 8> vec{1, 5};
        vec.insert(vec.begin() + 1, 3, 3);
        CHECK_EQ(vec.size(), 5);
        CHECK_EQ(vec[0], 1);
        CHECK_EQ(vec[1], 3);
        CHECK_EQ(vec[2], 3);
        CHECK_EQ(vec[3], 3);
        CHECK_EQ(vec[4], 5);
    }
}

TEST_CASE("static_vectra::assign") {
    SUBCASE("assign count and value") {
        static_vectra<int, 8> vec{1, 2, 3};
        vec.assign(5, 42);
        CHECK_EQ(vec.size(), 5);
        for (int i = 0; i < 5; ++i) {
            CHECK_EQ(vec[i], 42);
        }
    }

    SUBCASE("assign exceeds capacity throws") {
        static_vectra<int, 4> vec{1, 2, 3};
        CHECK_THROWS_AS(vec.assign(10, 42), std::length_error);
    }

    SUBCASE("assign from iterators") {
        std::vector<int> source{10, 20, 30, 40, 50};
        static_vectra<int, 8> vec{1, 2, 3};
        vec.assign(source.begin(), source.end());
        CHECK_EQ(vec.size(), 5);
        for (size_t i = 0; i < source.size(); ++i) {
            CHECK_EQ(vec[i], source[i]);
        }
    }

    SUBCASE("assign from initializer list") {
        static_vectra<int, 8> vec{1, 2, 3};
        vec.assign({10, 20, 30, 40});
        CHECK_EQ(vec.size(), 4);
        CHECK_EQ(vec[0], 10);
        CHECK_EQ(vec[3], 40);
    }
}

TEST_CASE("static_vectra::swap") {
    SUBCASE("swap same size") {
        static_vectra<int, 8> vec1{1, 2, 3};
        static_vectra<int, 8> vec2{10, 20, 30};
        vec1.swap(vec2);
        CHECK_EQ(vec1[0], 10);
        CHECK_EQ(vec2[0], 1);
    }

    SUBCASE("swap different sizes") {
        static_vectra<int, 8> vec1{1, 2, 3, 4, 5};
        static_vectra<int, 8> vec2{10, 20};
        vec1.swap(vec2);
        CHECK_EQ(vec1.size(), 2);
        CHECK_EQ(vec2.size(), 5);
        CHECK_EQ(vec1[0], 10);
        CHECK_EQ(vec2[0], 1);
        CHECK_EQ(vec2[4], 5);
    }

    SUBCASE("std::swap") {
        static_vectra<int, 8> vec1{1, 2, 3};
        static_vectra<int, 8> vec2{10, 20};
        std::swap(vec1, vec2);
        CHECK_EQ(vec1.size(), 2);
        CHECK_EQ(vec2.size(), 3);
    }
}

TEST_CASE("static_vectra::comparison") {
    SUBCASE("equality - same type") {
        static_vectra<int, 8> vec1{1, 2, 3};
        static_vectra<int, 8> vec2{1, 2, 3};
        static_vectra<int, 8> vec3{1, 2, 4};
        CHECK_EQ(vec1 == vec2, true);
        CHECK_EQ(vec1 == vec3, false);
        CHECK_EQ(vec1 != vec3, true);
    }

    SUBCASE("equality - different capacity") {
        static_vectra<int, 4> vec1{1, 2, 3};
        static_vectra<int, 8> vec2{1, 2, 3};
        CHECK_EQ(vec1 == vec2, true);
    }

    SUBCASE("ordering") {
        static_vectra<int, 8> vec1{1, 2, 3};
        static_vectra<int, 8> vec2{1, 2, 4};
        static_vectra<int, 8> vec3{1, 2};

        CHECK_EQ(vec1 < vec2, true);
        CHECK_EQ(vec2 > vec1, true);
        CHECK_EQ(vec3 < vec1, true);
        CHECK_EQ(vec1 <=> vec2, std::strong_ordering::less);
    }
}

TEST_CASE("static_vectra::complex_types") {
    SUBCASE("with std::string") {
        static_vectra<std::string, 4> vec;
        vec.push_back("hello");
        vec.push_back("world");
        vec.emplace_back("test");

        CHECK_EQ(vec.size(), 3);
        CHECK_EQ(vec[0], "hello");
        CHECK_EQ(vec[1], "world");
        CHECK_EQ(vec[2], "test");
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

        static_vectra<MoveOnly, 4> vec;
        vec.emplace_back(1);
        vec.emplace_back(2);
        vec.push_back(MoveOnly(3));

        CHECK_EQ(vec.size(), 3);
        CHECK_EQ(vec[0].value, 1);
        CHECK_EQ(vec[1].value, 2);
        CHECK_EQ(vec[2].value, 3);

        // Move to another vector
        static_vectra<MoveOnly, 4> vec2(std::move(vec));
        CHECK_EQ(vec2.size(), 3);
        CHECK_EQ(vec2[0].value, 1);
        CHECK_EQ(vec.size(), 0);
    }
}

TEST_CASE("static_vectra::unsafe_mode") {
    SUBCASE("unsafe push_back") {
        static_vectra<int, 8> vec;
        for (int i = 0; i < 5; ++i) {
            vec.push_back<Safety::Unsafe>(i);
        }
        CHECK_EQ(vec.size(), 5);
        for (int i = 0; i < 5; ++i) {
            CHECK_EQ(vec[i], i);
        }
    }

    SUBCASE("unsafe emplace_back") {
        static_vectra<std::pair<int, int>, 8> vec;
        vec.emplace_back<Safety::Unsafe>(1, 2);
        vec.emplace_back<Safety::Unsafe>(3, 4);
        CHECK_EQ(vec.size(), 2);
        CHECK_EQ(vec[0].first, 1);
        CHECK_EQ(vec[1].second, 4);
    }

    SUBCASE("unsafe insert") {
        static_vectra<int, 8> vec{1, 2, 4};
        vec.insert<Safety::Unsafe>(vec.begin() + 2, 3);
        CHECK_EQ(vec.size(), 4);
        CHECK_EQ(vec[2], 3);
    }
}

TEST_CASE("static_vectra::std_erase") {
    SUBCASE("std::erase") {
        static_vectra<int, 8> vec{1, 2, 3, 2, 4, 2, 5};
        auto count = std::erase(vec, 2);
        CHECK_EQ(count, 3);
        CHECK_EQ(vec.size(), 4);
        CHECK_EQ(vec[0], 1);
        CHECK_EQ(vec[1], 3);
        CHECK_EQ(vec[2], 4);
        CHECK_EQ(vec[3], 5);
    }

    SUBCASE("std::erase_if") {
        static_vectra<int, 8> vec{1, 2, 3, 4, 5, 6};
        auto count = std::erase_if(vec, [](int x) { return x % 2 == 0; });
        CHECK_EQ(count, 3);
        CHECK_EQ(vec.size(), 3);
        CHECK_EQ(vec[0], 1);
        CHECK_EQ(vec[1], 3);
        CHECK_EQ(vec[2], 5);
    }
}

TEST_CASE("static_vectra::no_heap_allocation") {
    // This test verifies the design - static_vectra should not have any heap allocation
    // by checking sizeof and ensuring data is stored inline

    SUBCASE("sizeof includes storage") {
        // sizeof(static_vectra<T,N>) should be approximately N*sizeof(T) + sizeof(size_type)
        // plus alignment padding
        constexpr size_t expected_min = 8 * sizeof(int) + sizeof(size_t);
        CHECK_GE(sizeof(static_vectra<int, 8>), expected_min);

        // Should be much smaller than a heap-allocated vector would need
        CHECK_LE(sizeof(static_vectra<int, 8>), expected_min + 16);  // Allow some padding
    }

    SUBCASE("data pointer is within object") {
        static_vectra<int, 8> vec{1, 2, 3};
        auto* obj_start = reinterpret_cast<const char*>(&vec);
        auto* obj_end = obj_start + sizeof(vec);
        auto* data_ptr = reinterpret_cast<const char*>(vec.data());

        // Data should be stored within the object itself
        CHECK_GE(data_ptr, obj_start);
        CHECK_LT(data_ptr, obj_end);
    }
}

// NOLINTEND
}  // namespace stdb::container

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

#include "container/small_vectra.hpp"

#include <doctest/doctest.h>

#include <iostream>
#include <string>
#include <vector>

namespace stdb::container {
// NOLINTBEGIN

TEST_CASE("small_vectra::basic") {
    SUBCASE("default constructor uses inline storage") {
        small_vectra<int, 8> vec;
        CHECK_EQ(vec.empty(), true);
        CHECK_EQ(vec.size(), 0);
        CHECK_EQ(vec.capacity(), 8);
        CHECK_EQ(vec.is_small(), true);
        CHECK_NE(vec.data(), nullptr);
    }

    SUBCASE("inline_capacity matches template parameter") {
        CHECK_EQ(small_vectra<int, 4>::inline_capacity, 4);
        CHECK_EQ(small_vectra<int, 16>::inline_capacity, 16);
        CHECK_EQ(small_vectra<double, 8>::inline_capacity, 8);
    }

    SUBCASE("stays inline when size <= N") {
        small_vectra<int, 8> vec;
        for (int i = 0; i < 8; ++i) {
            vec.push_back(i);
            CHECK_EQ(vec.is_small(), true);
        }
        CHECK_EQ(vec.size(), 8);
        CHECK_EQ(vec.capacity(), 8);
    }

    SUBCASE("transitions to heap when size > N") {
        small_vectra<int, 4> vec;
        for (int i = 0; i < 4; ++i) {
            vec.push_back(i);
        }
        CHECK_EQ(vec.is_small(), true);

        vec.push_back(4);  // This should trigger heap allocation
        CHECK_EQ(vec.is_small(), false);
        CHECK_EQ(vec.size(), 5);
        CHECK_GT(vec.capacity(), 4);

        // Verify data integrity
        for (int i = 0; i < 5; ++i) {
            CHECK_EQ(vec[i], i);
        }
    }
}

TEST_CASE("small_vectra::constructors") {
    SUBCASE("size constructor - inline") {
        small_vectra<int, 8> vec(5);
        CHECK_EQ(vec.size(), 5);
        CHECK_EQ(vec.is_small(), true);
        for (int i = 0; i < 5; ++i) {
            CHECK_EQ(vec[i], 0);
        }
    }

    SUBCASE("size constructor - heap") {
        small_vectra<int, 4> vec(10);
        CHECK_EQ(vec.size(), 10);
        CHECK_EQ(vec.is_small(), false);
        for (int i = 0; i < 10; ++i) {
            CHECK_EQ(vec[i], 0);
        }
    }

    SUBCASE("size and value constructor - inline") {
        small_vectra<int, 8> vec(5, 42);
        CHECK_EQ(vec.size(), 5);
        CHECK_EQ(vec.is_small(), true);
        for (int i = 0; i < 5; ++i) {
            CHECK_EQ(vec[i], 42);
        }
    }

    SUBCASE("size and value constructor - heap") {
        small_vectra<int, 4> vec(10, 42);
        CHECK_EQ(vec.size(), 10);
        CHECK_EQ(vec.is_small(), false);
        for (int i = 0; i < 10; ++i) {
            CHECK_EQ(vec[i], 42);
        }
    }

    SUBCASE("initializer list - inline") {
        small_vectra<int, 8> vec{1, 2, 3, 4, 5};
        CHECK_EQ(vec.size(), 5);
        CHECK_EQ(vec.is_small(), true);
        CHECK_EQ(vec[0], 1);
        CHECK_EQ(vec[4], 5);
    }

    SUBCASE("initializer list - heap") {
        small_vectra<int, 4> vec{1, 2, 3, 4, 5, 6, 7, 8};
        CHECK_EQ(vec.size(), 8);
        CHECK_EQ(vec.is_small(), false);
        CHECK_EQ(vec[0], 1);
        CHECK_EQ(vec[7], 8);
    }

    SUBCASE("iterator constructor") {
        std::vector<int> source{1, 2, 3, 4, 5};
        small_vectra<int, 8> vec(source.begin(), source.end());
        CHECK_EQ(vec.size(), 5);
        CHECK_EQ(vec.is_small(), true);
        for (size_t i = 0; i < source.size(); ++i) {
            CHECK_EQ(vec[i], source[i]);
        }
    }
}

TEST_CASE("small_vectra::copy_and_move") {
    SUBCASE("copy constructor - inline to inline") {
        small_vectra<int, 8> vec1{1, 2, 3, 4};
        small_vectra<int, 8> vec2(vec1);
        CHECK_EQ(vec2.size(), 4);
        CHECK_EQ(vec2.is_small(), true);
        CHECK_EQ(vec1, vec2);
    }

    SUBCASE("copy constructor - heap to heap") {
        small_vectra<int, 4> vec1{1, 2, 3, 4, 5, 6, 7, 8};
        small_vectra<int, 4> vec2(vec1);
        CHECK_EQ(vec2.size(), 8);
        CHECK_EQ(vec2.is_small(), false);
        CHECK_EQ(vec1, vec2);
    }

    SUBCASE("move constructor - inline") {
        small_vectra<int, 8> vec1{1, 2, 3, 4};
        auto* original_data = vec1.data();
        small_vectra<int, 8> vec2(std::move(vec1));

        CHECK_EQ(vec2.size(), 4);
        CHECK_EQ(vec2.is_small(), true);
        // Inline storage means different addresses
        CHECK_NE(vec2.data(), original_data);
        CHECK_EQ(vec2[0], 1);
        CHECK_EQ(vec2[3], 4);
        CHECK_EQ(vec1.size(), 0);  // Source should be empty
    }

    SUBCASE("move constructor - heap") {
        small_vectra<int, 4> vec1{1, 2, 3, 4, 5, 6, 7, 8};
        auto* original_data = vec1.data();
        small_vectra<int, 4> vec2(std::move(vec1));

        CHECK_EQ(vec2.size(), 8);
        CHECK_EQ(vec2.is_small(), false);
        // Heap storage means pointer is stolen
        CHECK_EQ(vec2.data(), original_data);
        CHECK_EQ(vec2[0], 1);
        CHECK_EQ(vec2[7], 8);
        CHECK_EQ(vec1.is_small(), true);  // Source should be reset to inline
    }

    SUBCASE("copy assignment - smaller to larger capacity") {
        small_vectra<int, 8> vec1{1, 2, 3};
        small_vectra<int, 8> vec2{10, 20, 30, 40, 50};
        vec2 = vec1;
        CHECK_EQ(vec2.size(), 3);
        CHECK_EQ(vec2, vec1);
    }

    SUBCASE("copy assignment - larger to smaller capacity") {
        small_vectra<int, 8> vec1{1, 2, 3, 4, 5, 6, 7};
        small_vectra<int, 8> vec2{10, 20};
        vec2 = vec1;
        CHECK_EQ(vec2.size(), 7);
        CHECK_EQ(vec2, vec1);
    }

    SUBCASE("move assignment - inline to inline") {
        small_vectra<int, 8> vec1{1, 2, 3, 4};
        small_vectra<int, 8> vec2{10, 20};
        vec2 = std::move(vec1);
        CHECK_EQ(vec2.size(), 4);
        CHECK_EQ(vec2.is_small(), true);
        CHECK_EQ(vec2[0], 1);
    }

    SUBCASE("move assignment - heap to heap") {
        small_vectra<int, 4> vec1{1, 2, 3, 4, 5, 6, 7, 8};
        small_vectra<int, 4> vec2{10, 20, 30, 40, 50, 60};
        auto* original_data = vec1.data();
        vec2 = std::move(vec1);
        CHECK_EQ(vec2.size(), 8);
        CHECK_EQ(vec2.is_small(), false);
        CHECK_EQ(vec2.data(), original_data);
    }

    SUBCASE("self assignment") {
        small_vectra<int, 8> vec{1, 2, 3, 4};
        auto* ptr = &vec;
        vec = *ptr;
        CHECK_EQ(vec.size(), 4);
        CHECK_EQ(vec[0], 1);
    }
}

TEST_CASE("small_vectra::modifiers") {
    SUBCASE("push_back lvalue") {
        small_vectra<int, 4> vec;
        for (int i = 0; i < 10; ++i) {
            int val = i * 10;
            vec.push_back(val);
        }
        CHECK_EQ(vec.size(), 10);
        CHECK_EQ(vec.is_small(), false);
        for (int i = 0; i < 10; ++i) {
            CHECK_EQ(vec[i], i * 10);
        }
    }

    SUBCASE("push_back rvalue") {
        small_vectra<std::string, 4> vec;
        vec.push_back("hello");
        vec.push_back("world");
        CHECK_EQ(vec.size(), 2);
        CHECK_EQ(vec[0], "hello");
        CHECK_EQ(vec[1], "world");
    }

    SUBCASE("emplace_back") {
        small_vectra<std::pair<int, std::string>, 4> vec;
        vec.emplace_back(1, "one");
        vec.emplace_back(2, "two");
        CHECK_EQ(vec.size(), 2);
        CHECK_EQ(vec[0].first, 1);
        CHECK_EQ(vec[0].second, "one");
    }

    SUBCASE("pop_back") {
        small_vectra<int, 8> vec{1, 2, 3, 4, 5};
        vec.pop_back();
        CHECK_EQ(vec.size(), 4);
        CHECK_EQ(vec.back(), 4);
    }

    SUBCASE("clear") {
        small_vectra<int, 8> vec{1, 2, 3, 4, 5};
        vec.clear();
        CHECK_EQ(vec.empty(), true);
        CHECK_EQ(vec.is_small(), true);  // Should still be inline
        CHECK_EQ(vec.capacity(), 8);
    }

    SUBCASE("resize - grow inline") {
        small_vectra<int, 8> vec{1, 2, 3};
        vec.resize(6);
        CHECK_EQ(vec.size(), 6);
        CHECK_EQ(vec.is_small(), true);
        CHECK_EQ(vec[0], 1);
        CHECK_EQ(vec[3], 0);
    }

    SUBCASE("resize - grow to heap") {
        small_vectra<int, 4> vec{1, 2, 3};
        vec.resize(10);
        CHECK_EQ(vec.size(), 10);
        CHECK_EQ(vec.is_small(), false);
        CHECK_EQ(vec[0], 1);
        CHECK_EQ(vec[5], 0);
    }

    SUBCASE("resize - shrink") {
        small_vectra<int, 8> vec{1, 2, 3, 4, 5};
        vec.resize(2);
        CHECK_EQ(vec.size(), 2);
        CHECK_EQ(vec[0], 1);
        CHECK_EQ(vec[1], 2);
    }

    SUBCASE("resize with value") {
        small_vectra<int, 8> vec{1, 2, 3};
        vec.resize(6, 42);
        CHECK_EQ(vec.size(), 6);
        CHECK_EQ(vec[0], 1);
        CHECK_EQ(vec[3], 42);
        CHECK_EQ(vec[5], 42);
    }
}

TEST_CASE("small_vectra::reserve_and_shrink") {
    SUBCASE("reserve - stay inline") {
        small_vectra<int, 8> vec{1, 2, 3};
        vec.reserve(6);
        CHECK_EQ(vec.is_small(), true);
        CHECK_EQ(vec.capacity(), 8);
        CHECK_EQ(vec.size(), 3);
    }

    SUBCASE("reserve - transition to heap") {
        small_vectra<int, 4> vec{1, 2, 3};
        vec.reserve(10);
        CHECK_EQ(vec.is_small(), false);
        CHECK_GE(vec.capacity(), 10);
        CHECK_EQ(vec.size(), 3);
        CHECK_EQ(vec[0], 1);
    }

    SUBCASE("shrink_to_fit - heap to inline") {
        small_vectra<int, 8> vec;
        vec.reserve(100);  // Force heap allocation
        vec.push_back(1);
        vec.push_back(2);
        CHECK_EQ(vec.is_small(), false);

        vec.shrink_to_fit();
        CHECK_EQ(vec.is_small(), true);
        CHECK_EQ(vec.size(), 2);
        CHECK_EQ(vec[0], 1);
        CHECK_EQ(vec[1], 2);
    }

    SUBCASE("shrink_to_fit - heap stays heap") {
        small_vectra<int, 4> vec;
        vec.reserve(100);
        for (int i = 0; i < 10; ++i) {
            vec.push_back(i);
        }
        CHECK_EQ(vec.is_small(), false);

        vec.shrink_to_fit();
        CHECK_EQ(vec.is_small(), false);
        CHECK_EQ(vec.capacity(), 10);
        CHECK_EQ(vec.size(), 10);
    }
}

TEST_CASE("small_vectra::element_access") {
    small_vectra<int, 8> vec{10, 20, 30, 40, 50};

    SUBCASE("operator[]") {
        CHECK_EQ(vec[0], 10);
        CHECK_EQ(vec[4], 50);
        vec[2] = 300;
        CHECK_EQ(vec[2], 300);
    }

    SUBCASE("at()") {
        CHECK_EQ(vec.at(0), 10);
        CHECK_EQ(vec.at(4), 50);
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

TEST_CASE("small_vectra::iterators") {
    small_vectra<int, 8> vec{1, 2, 3, 4, 5};

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
        const small_vectra<int, 8>& cvec = vec;
        int expected = 1;
        for (auto it = cvec.cbegin(); it != cvec.cend(); ++it) {
            CHECK_EQ(*it, expected++);
        }
    }
}

TEST_CASE("small_vectra::erase") {
    SUBCASE("erase single element") {
        small_vectra<int, 8> vec{1, 2, 3, 4, 5};
        auto it = vec.erase(vec.begin() + 2);
        CHECK_EQ(vec.size(), 4);
        CHECK_EQ(*it, 4);
        CHECK_EQ(vec[0], 1);
        CHECK_EQ(vec[1], 2);
        CHECK_EQ(vec[2], 4);
        CHECK_EQ(vec[3], 5);
    }

    SUBCASE("erase range") {
        small_vectra<int, 8> vec{1, 2, 3, 4, 5};
        auto it = vec.erase(vec.begin() + 1, vec.begin() + 4);
        CHECK_EQ(vec.size(), 2);
        CHECK_EQ(*it, 5);
        CHECK_EQ(vec[0], 1);
        CHECK_EQ(vec[1], 5);
    }

    SUBCASE("erase first element") {
        small_vectra<int, 8> vec{1, 2, 3};
        vec.erase(vec.begin());
        CHECK_EQ(vec.size(), 2);
        CHECK_EQ(vec[0], 2);
        CHECK_EQ(vec[1], 3);
    }

    SUBCASE("erase last element") {
        small_vectra<int, 8> vec{1, 2, 3};
        vec.erase(vec.end() - 1);
        CHECK_EQ(vec.size(), 2);
        CHECK_EQ(vec[0], 1);
        CHECK_EQ(vec[1], 2);
    }
}

TEST_CASE("small_vectra::insert") {
    SUBCASE("insert at beginning") {
        small_vectra<int, 8> vec{2, 3, 4};
        vec.insert(vec.begin(), 1);
        CHECK_EQ(vec.size(), 4);
        CHECK_EQ(vec[0], 1);
        CHECK_EQ(vec[1], 2);
    }

    SUBCASE("insert in middle") {
        small_vectra<int, 8> vec{1, 2, 4, 5};
        vec.insert(vec.begin() + 2, 3);
        CHECK_EQ(vec.size(), 5);
        CHECK_EQ(vec[2], 3);
    }

    SUBCASE("insert at end") {
        small_vectra<int, 8> vec{1, 2, 3};
        vec.insert(vec.end(), 4);
        CHECK_EQ(vec.size(), 4);
        CHECK_EQ(vec[3], 4);
    }

    SUBCASE("insert triggers reallocation") {
        small_vectra<int, 4> vec{1, 2, 3, 4};
        CHECK_EQ(vec.is_small(), true);
        vec.insert(vec.begin() + 2, 100);
        CHECK_EQ(vec.is_small(), false);
        CHECK_EQ(vec.size(), 5);
        CHECK_EQ(vec[0], 1);
        CHECK_EQ(vec[1], 2);
        CHECK_EQ(vec[2], 100);
        CHECK_EQ(vec[3], 3);
        CHECK_EQ(vec[4], 4);
    }
}

TEST_CASE("small_vectra::assign") {
    SUBCASE("assign count and value - inline") {
        small_vectra<int, 8> vec{1, 2, 3};
        vec.assign(5, 42);
        CHECK_EQ(vec.size(), 5);
        CHECK_EQ(vec.is_small(), true);
        for (int i = 0; i < 5; ++i) {
            CHECK_EQ(vec[i], 42);
        }
    }

    SUBCASE("assign count and value - to heap") {
        small_vectra<int, 4> vec{1, 2, 3};
        vec.assign(10, 42);
        CHECK_EQ(vec.size(), 10);
        CHECK_EQ(vec.is_small(), false);
        for (int i = 0; i < 10; ++i) {
            CHECK_EQ(vec[i], 42);
        }
    }

    SUBCASE("assign from iterators") {
        std::vector<int> source{10, 20, 30, 40, 50};
        small_vectra<int, 8> vec{1, 2, 3};
        vec.assign(source.begin(), source.end());
        CHECK_EQ(vec.size(), 5);
        for (size_t i = 0; i < source.size(); ++i) {
            CHECK_EQ(vec[i], source[i]);
        }
    }

    SUBCASE("assign from initializer list") {
        small_vectra<int, 8> vec{1, 2, 3};
        vec.assign({10, 20, 30, 40});
        CHECK_EQ(vec.size(), 4);
        CHECK_EQ(vec[0], 10);
        CHECK_EQ(vec[3], 40);
    }
}

TEST_CASE("small_vectra::swap") {
    SUBCASE("swap inline with inline") {
        small_vectra<int, 8> vec1{1, 2, 3};
        small_vectra<int, 8> vec2{10, 20, 30, 40};
        vec1.swap(vec2);
        CHECK_EQ(vec1.size(), 4);
        CHECK_EQ(vec2.size(), 3);
        CHECK_EQ(vec1[0], 10);
        CHECK_EQ(vec2[0], 1);
    }

    SUBCASE("swap heap with heap") {
        small_vectra<int, 4> vec1{1, 2, 3, 4, 5, 6};
        small_vectra<int, 4> vec2{10, 20, 30, 40, 50, 60, 70, 80};
        auto* ptr1 = vec1.data();
        auto* ptr2 = vec2.data();
        vec1.swap(vec2);
        CHECK_EQ(vec1.data(), ptr2);
        CHECK_EQ(vec2.data(), ptr1);
        CHECK_EQ(vec1.size(), 8);
        CHECK_EQ(vec2.size(), 6);
    }

    SUBCASE("swap inline with heap") {
        small_vectra<int, 4> vec1{1, 2, 3};
        small_vectra<int, 4> vec2{10, 20, 30, 40, 50, 60};
        CHECK_EQ(vec1.is_small(), true);
        CHECK_EQ(vec2.is_small(), false);
        vec1.swap(vec2);
        CHECK_EQ(vec1.is_small(), false);
        CHECK_EQ(vec2.is_small(), true);
        CHECK_EQ(vec1.size(), 6);
        CHECK_EQ(vec2.size(), 3);
        CHECK_EQ(vec1[0], 10);
        CHECK_EQ(vec2[0], 1);
    }

    SUBCASE("std::swap") {
        small_vectra<int, 8> vec1{1, 2, 3};
        small_vectra<int, 8> vec2{10, 20};
        std::swap(vec1, vec2);
        CHECK_EQ(vec1.size(), 2);
        CHECK_EQ(vec2.size(), 3);
    }
}

TEST_CASE("small_vectra::comparison") {
    SUBCASE("equality") {
        small_vectra<int, 8> vec1{1, 2, 3};
        small_vectra<int, 8> vec2{1, 2, 3};
        small_vectra<int, 8> vec3{1, 2, 4};
        CHECK_EQ(vec1 == vec2, true);
        CHECK_EQ(vec1 == vec3, false);
        CHECK_EQ(vec1 != vec3, true);
    }

    SUBCASE("ordering") {
        small_vectra<int, 8> vec1{1, 2, 3};
        small_vectra<int, 8> vec2{1, 2, 4};
        small_vectra<int, 8> vec3{1, 2};

        CHECK_EQ(vec1 < vec2, true);
        CHECK_EQ(vec2 > vec1, true);
        CHECK_EQ(vec3 < vec1, true);
        CHECK_EQ(vec1 <=> vec2, std::strong_ordering::less);
    }
}

TEST_CASE("small_vectra::complex_types") {
    SUBCASE("with std::string") {
        small_vectra<std::string, 4> vec;
        vec.push_back("hello");
        vec.push_back("world");
        vec.emplace_back("test");

        CHECK_EQ(vec.size(), 3);
        CHECK_EQ(vec[0], "hello");
        CHECK_EQ(vec[1], "world");
        CHECK_EQ(vec[2], "test");

        // Test move to heap
        vec.push_back("a");
        vec.push_back("b");
        CHECK_EQ(vec.is_small(), false);
        CHECK_EQ(vec[0], "hello");
        CHECK_EQ(vec[4], "b");
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

        small_vectra<MoveOnly, 4> vec;
        vec.emplace_back(1);
        vec.emplace_back(2);
        vec.push_back(MoveOnly(3));

        CHECK_EQ(vec.size(), 3);
        CHECK_EQ(vec[0].value, 1);
        CHECK_EQ(vec[1].value, 2);
        CHECK_EQ(vec[2].value, 3);

        // Move to another vector
        small_vectra<MoveOnly, 4> vec2(std::move(vec));
        CHECK_EQ(vec2.size(), 3);
        CHECK_EQ(vec2[0].value, 1);
    }
}

TEST_CASE("small_vectra::unsafe_mode") {
    SUBCASE("unsafe push_back") {
        small_vectra<int, 8> vec;
        vec.reserve(5);
        for (int i = 0; i < 5; ++i) {
            vec.push_back<Safety::Unsafe>(i);
        }
        CHECK_EQ(vec.size(), 5);
        for (int i = 0; i < 5; ++i) {
            CHECK_EQ(vec[i], i);
        }
    }

    SUBCASE("unsafe emplace_back") {
        small_vectra<std::pair<int, int>, 8> vec;
        vec.reserve(3);
        vec.emplace_back<Safety::Unsafe>(1, 2);
        vec.emplace_back<Safety::Unsafe>(3, 4);
        CHECK_EQ(vec.size(), 2);
        CHECK_EQ(vec[0].first, 1);
        CHECK_EQ(vec[1].second, 4);
    }
}

TEST_CASE("small_vectra::std_erase") {
    SUBCASE("std::erase") {
        small_vectra<int, 8> vec{1, 2, 3, 2, 4, 2, 5};
        auto count = std::erase(vec, 2);
        CHECK_EQ(count, 3);
        CHECK_EQ(vec.size(), 4);
        CHECK_EQ(vec[0], 1);
        CHECK_EQ(vec[1], 3);
        CHECK_EQ(vec[2], 4);
        CHECK_EQ(vec[3], 5);
    }

    SUBCASE("std::erase_if") {
        small_vectra<int, 8> vec{1, 2, 3, 4, 5, 6};
        auto count = std::erase_if(vec, [](int x) { return x % 2 == 0; });
        CHECK_EQ(count, 3);
        CHECK_EQ(vec.size(), 3);
        CHECK_EQ(vec[0], 1);
        CHECK_EQ(vec[1], 3);
        CHECK_EQ(vec[2], 5);
    }
}

// ============================================================================
// PMR (Polymorphic Memory Resource) Tests
// ============================================================================

TEST_CASE("small_vectra::pmr") {
    SUBCASE("basic operations with monotonic_buffer_resource - inline storage") {
        std::array<std::byte, 4096> buffer;
        std::pmr::monotonic_buffer_resource resource(buffer.data(), buffer.size(),
                                                     std::pmr::null_memory_resource());

        stdb::pmr::small_vectra<int, 8> vec(&resource);

        // Use inline storage (< 8 elements)
        vec.push_back(1);
        vec.push_back(2);
        vec.push_back(3);

        CHECK_EQ(vec.size(), 3);
        CHECK(vec.is_small());  // Should be in inline storage
        CHECK_EQ(vec[0], 1);
        CHECK_EQ(vec[1], 2);
        CHECK_EQ(vec[2], 3);
    }

    SUBCASE("basic operations with monotonic_buffer_resource - heap storage") {
        std::array<std::byte, 4096> buffer;
        std::pmr::monotonic_buffer_resource resource(buffer.data(), buffer.size(),
                                                     std::pmr::null_memory_resource());

        stdb::pmr::small_vectra<int, 4> vec(&resource);

        // Force heap allocation (> 4 elements)
        for (int i = 0; i < 10; ++i) {
            vec.push_back(i);
        }

        CHECK_EQ(vec.size(), 10);
        CHECK(!vec.is_small());  // Should be on heap
        for (int i = 0; i < 10; ++i) {
            CHECK_EQ(vec[i], i);
        }
    }

    SUBCASE("allocator-only constructor") {
        std::pmr::monotonic_buffer_resource resource;
        stdb::pmr::small_vectra<int, 8> vec(&resource);

        CHECK(vec.empty());
        CHECK_EQ(vec.size(), 0);
        CHECK(vec.is_small());

        vec.push_back(42);
        CHECK_EQ(vec.size(), 1);
        CHECK_EQ(vec[0], 42);
    }

    SUBCASE("allocator-extended copy constructor") {
        std::pmr::monotonic_buffer_resource resource1;
        std::pmr::monotonic_buffer_resource resource2;

        stdb::pmr::small_vectra<int, 8> vec1(&resource1);
        vec1.push_back(10);
        vec1.push_back(20);
        vec1.push_back(30);

        stdb::pmr::small_vectra<int, 8> vec2(vec1, &resource2);

        CHECK_EQ(vec2.size(), 3);
        CHECK_EQ(vec2[0], 10);
        CHECK_EQ(vec2[1], 20);
        CHECK_EQ(vec2[2], 30);
        CHECK_EQ(vec2.get_allocator().resource(), &resource2);
    }

    SUBCASE("allocator-extended move constructor - same resource") {
        std::pmr::monotonic_buffer_resource resource;

        stdb::pmr::small_vectra<int, 4> vec1(&resource);
        // Force heap allocation
        for (int i = 0; i < 10; ++i) {
            vec1.push_back(i);
        }

        stdb::pmr::small_vectra<int, 4> vec2(std::move(vec1), &resource);

        CHECK_EQ(vec2.size(), 10);
        for (int i = 0; i < 10; ++i) {
            CHECK_EQ(vec2[i], i);
        }
        CHECK(vec1.empty());  // Resources were stolen
    }

    SUBCASE("allocator-extended move constructor - different resource") {
        std::pmr::monotonic_buffer_resource resource1;
        std::pmr::monotonic_buffer_resource resource2;

        stdb::pmr::small_vectra<int, 4> vec1(&resource1);
        for (int i = 0; i < 10; ++i) {
            vec1.push_back(i);
        }

        stdb::pmr::small_vectra<int, 4> vec2(std::move(vec1), &resource2);

        CHECK_EQ(vec2.size(), 10);
        for (int i = 0; i < 10; ++i) {
            CHECK_EQ(vec2[i], i);
        }
        CHECK_EQ(vec2.get_allocator().resource(), &resource2);
    }

    SUBCASE("copy assignment - keeps allocator") {
        std::pmr::monotonic_buffer_resource resource1;
        std::pmr::monotonic_buffer_resource resource2;

        stdb::pmr::small_vectra<int, 8> vec1(&resource1);
        vec1.push_back(10);
        vec1.push_back(20);

        stdb::pmr::small_vectra<int, 8> vec2(&resource2);
        vec2.push_back(30);

        vec2 = vec1;

        CHECK_EQ(vec2.size(), 2);
        CHECK_EQ(vec2[0], 10);
        CHECK_EQ(vec2[1], 20);
        CHECK_EQ(vec2.get_allocator().resource(), &resource2);  // Kept original allocator
    }

    SUBCASE("move assignment - same resource") {
        std::pmr::monotonic_buffer_resource resource;

        stdb::pmr::small_vectra<int, 4> vec1(&resource);
        for (int i = 0; i < 10; ++i) {
            vec1.push_back(i);
        }

        stdb::pmr::small_vectra<int, 4> vec2(&resource);
        vec2.push_back(100);

        vec2 = std::move(vec1);

        CHECK_EQ(vec2.size(), 10);
        for (int i = 0; i < 10; ++i) {
            CHECK_EQ(vec2[i], i);
        }
        CHECK(vec1.empty());  // Resources were stolen
    }

    SUBCASE("move assignment - different resource") {
        std::pmr::monotonic_buffer_resource resource1;
        std::pmr::monotonic_buffer_resource resource2;

        stdb::pmr::small_vectra<int, 4> vec1(&resource1);
        for (int i = 0; i < 10; ++i) {
            vec1.push_back(i);
        }

        stdb::pmr::small_vectra<int, 4> vec2(&resource2);
        vec2.push_back(100);

        vec2 = std::move(vec1);

        CHECK_EQ(vec2.size(), 10);
        for (int i = 0; i < 10; ++i) {
            CHECK_EQ(vec2[i], i);
        }
        CHECK_EQ(vec2.get_allocator().resource(), &resource2);  // Kept original allocator
    }

    SUBCASE("string elements with PMR") {
        std::pmr::monotonic_buffer_resource resource;
        stdb::pmr::small_vectra<std::string, 4> vec(&resource);

        vec.push_back("hello");
        vec.push_back("world");
        vec.push_back("test");

        CHECK_EQ(vec.size(), 3);
        CHECK_EQ(vec[0], "hello");
        CHECK_EQ(vec[1], "world");
        CHECK_EQ(vec[2], "test");
    }

    SUBCASE("transition from inline to heap with PMR") {
        std::pmr::monotonic_buffer_resource resource;
        stdb::pmr::small_vectra<int, 4> vec(&resource);

        // Start inline
        vec.push_back(1);
        vec.push_back(2);
        CHECK(vec.is_small());

        // Force transition to heap
        for (int i = 3; i <= 10; ++i) {
            vec.push_back(i);
        }
        CHECK(!vec.is_small());
        CHECK_EQ(vec.size(), 10);

        // Verify all elements
        for (int i = 0; i < 10; ++i) {
            CHECK_EQ(vec[i], i + 1);
        }
    }
}

// NOLINTEND
}  // namespace stdb::container

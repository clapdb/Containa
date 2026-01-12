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

#include "container/concurrent_skiplist.hpp"

#include <doctest/doctest.h>

#include <string>
#include <thread>
#include <vector>

namespace stdb::container {
// NOLINTBEGIN

TEST_CASE("concurrent_skiplist::basic") {
    SUBCASE("default constructor") {
        concurrent_skiplist<int, std::string> sl;
        CHECK(sl.empty());
        CHECK_EQ(sl.size(), 0);
    }

    SUBCASE("insert and find") {
        concurrent_skiplist<int, std::string> sl;
        CHECK(sl.insert(1, "one"));
        CHECK(sl.insert(2, "two"));
        CHECK(sl.insert(3, "three"));

        CHECK_EQ(sl.size(), 3);

        auto v1 = sl.find(1);
        CHECK(v1.has_value());
        CHECK_EQ(*v1, "one");

        auto v2 = sl.find(2);
        CHECK(v2.has_value());
        CHECK_EQ(*v2, "two");

        auto v3 = sl.find(3);
        CHECK(v3.has_value());
        CHECK_EQ(*v3, "three");

        auto v4 = sl.find(4);
        CHECK_FALSE(v4.has_value());
    }

    SUBCASE("contains") {
        concurrent_skiplist<int, int> sl;
        sl.insert(10, 100);
        sl.insert(20, 200);

        CHECK(sl.contains(10));
        CHECK(sl.contains(20));
        CHECK_FALSE(sl.contains(30));
    }

    SUBCASE("duplicate insert returns false") {
        concurrent_skiplist<int, std::string> sl;
        CHECK(sl.insert(1, "one"));
        CHECK_FALSE(sl.insert(1, "uno"));  // duplicate key

        auto v = sl.find(1);
        CHECK(v.has_value());
        CHECK_EQ(*v, "one");  // original value preserved
    }

    SUBCASE("erase") {
        concurrent_skiplist<int, std::string> sl;
        sl.insert(1, "one");
        sl.insert(2, "two");
        sl.insert(3, "three");

        CHECK(sl.erase(2));
        CHECK_FALSE(sl.contains(2));
        CHECK_EQ(sl.size(), 2);

        CHECK_FALSE(sl.erase(2));  // already erased
        CHECK_FALSE(sl.erase(100));  // never existed
    }

    SUBCASE("for_each") {
        concurrent_skiplist<int, int> sl;
        sl.insert(1, 10);
        sl.insert(2, 20);
        sl.insert(3, 30);

        int sum_keys = 0;
        int sum_values = 0;
        sl.for_each([&](const int& k, const int& v) {
            sum_keys += k;
            sum_values += v;
        });

        CHECK_EQ(sum_keys, 6);
        CHECK_EQ(sum_values, 60);
    }

    SUBCASE("move insert") {
        concurrent_skiplist<std::string, std::string> sl;
        std::string key = "key";
        std::string value = "value";

        CHECK(sl.insert(std::move(key), std::move(value)));
        CHECK(key.empty());  // moved from
        CHECK(value.empty());

        auto v = sl.find("key");
        CHECK(v.has_value());
        CHECK_EQ(*v, "value");
    }
}

TEST_CASE("concurrent_skiplist::pmr") {
    SUBCASE("pmr type alias exists") {
        // Use synchronized_pool_resource for thread-safety
        std::pmr::synchronized_pool_resource pool;
        stdb::pmr::concurrent_skiplist<int, std::string> sl(&pool);

        CHECK(sl.insert(1, "one"));
        CHECK(sl.insert(2, "two"));

        CHECK_EQ(sl.size(), 2);
        CHECK(sl.contains(1));
        CHECK(sl.contains(2));

        auto v = sl.find(1);
        CHECK(v.has_value());
        CHECK_EQ(*v, "one");
    }

    SUBCASE("allocator constructor") {
        std::pmr::synchronized_pool_resource pool;
        std::pmr::polymorphic_allocator<std::pair<const int, int>> alloc(&pool);

        concurrent_skiplist<int, int, std::less<int>,
                           std::pmr::polymorphic_allocator<std::pair<const int, int>>> sl(alloc);

        sl.insert(10, 100);
        sl.insert(20, 200);

        CHECK_EQ(sl.size(), 2);
        CHECK(sl.get_allocator().resource() == &pool);
    }

    SUBCASE("get_allocator returns correct allocator") {
        std::pmr::synchronized_pool_resource pool;
        stdb::pmr::concurrent_skiplist<int, int> sl(&pool);

        auto alloc = sl.get_allocator();
        CHECK(alloc.resource() == &pool);
    }

    SUBCASE("pmr erase works correctly") {
        std::pmr::synchronized_pool_resource pool;
        stdb::pmr::concurrent_skiplist<int, std::string> sl(&pool);

        sl.insert(1, "one");
        sl.insert(2, "two");
        sl.insert(3, "three");

        CHECK(sl.erase(2));
        CHECK_FALSE(sl.contains(2));
        CHECK_EQ(sl.size(), 2);
    }

    SUBCASE("pmr concurrent_skiplist_map alias") {
        std::pmr::synchronized_pool_resource pool;
        stdb::pmr::concurrent_skiplist_map<std::string, int> sl(&pool);

        sl.insert("one", 1);
        sl.insert("two", 2);

        CHECK_EQ(sl.size(), 2);
        CHECK(sl.contains("one"));

        auto v = sl.find("two");
        CHECK(v.has_value());
        CHECK_EQ(*v, 2);
    }

    SUBCASE("pmr with many insertions") {
        std::pmr::synchronized_pool_resource pool;
        stdb::pmr::concurrent_skiplist<int, int> sl(&pool);

        constexpr int N = 1000;
        for (int i = 0; i < N; ++i) {
            CHECK(sl.insert(i, i * 10));
        }

        CHECK_EQ(sl.size(), N);

        for (int i = 0; i < N; ++i) {
            auto v = sl.find(i);
            CHECK(v.has_value());
            CHECK_EQ(*v, i * 10);
        }
    }
}

TEST_CASE("concurrent_skiplist::concurrent") {
    SUBCASE("concurrent inserts") {
        concurrent_skiplist<int, int> sl;
        constexpr int num_threads = 4;
        constexpr int ops_per_thread = 1000;

        std::vector<std::thread> threads;
        for (int t = 0; t < num_threads; ++t) {
            threads.emplace_back([&sl, t]() {
                for (int i = 0; i < ops_per_thread; ++i) {
                    int key = t * ops_per_thread + i;
                    sl.insert(key, key * 10);
                }
            });
        }

        for (auto& th : threads) {
            th.join();
        }

        CHECK_EQ(sl.size(), num_threads * ops_per_thread);
    }

    SUBCASE("concurrent inserts and finds") {
        concurrent_skiplist<int, int> sl;

        // Pre-populate
        for (int i = 0; i < 100; ++i) {
            sl.insert(i, i);
        }

        std::atomic<bool> done{false};
        std::atomic<int> found_count{0};

        // Reader thread
        std::thread reader([&]() {
            while (!done.load(std::memory_order_relaxed)) {
                for (int i = 0; i < 100; ++i) {
                    if (sl.contains(i)) {
                        found_count.fetch_add(1, std::memory_order_relaxed);
                    }
                }
            }
        });

        // Writer thread
        std::thread writer([&]() {
            for (int i = 100; i < 200; ++i) {
                sl.insert(i, i);
            }
            done.store(true, std::memory_order_relaxed);
        });

        reader.join();
        writer.join();

        CHECK_EQ(sl.size(), 200);
        CHECK_GT(found_count.load(), 0);
    }

    SUBCASE("concurrent with pmr") {
        std::pmr::synchronized_pool_resource pool;
        stdb::pmr::concurrent_skiplist<int, int> sl(&pool);

        constexpr int num_threads = 4;
        constexpr int ops_per_thread = 500;

        std::vector<std::thread> threads;
        for (int t = 0; t < num_threads; ++t) {
            threads.emplace_back([&sl, t]() {
                for (int i = 0; i < ops_per_thread; ++i) {
                    int key = t * ops_per_thread + i;
                    sl.insert(key, key);
                }
            });
        }

        for (auto& th : threads) {
            th.join();
        }

        CHECK_EQ(sl.size(), num_threads * ops_per_thread);

        // Verify all values
        for (int i = 0; i < num_threads * ops_per_thread; ++i) {
            auto v = sl.find(i);
            CHECK(v.has_value());
            CHECK_EQ(*v, i);
        }
    }
}

// NOLINTEND
}  // namespace stdb::container

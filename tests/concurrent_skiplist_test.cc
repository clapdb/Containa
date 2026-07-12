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

#include <atomic>
#include <set>
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

// erase() used to leave the victim wired into the level-0 chain on purpose, so the destructor could
// still find it. find_insert_position() then reported the successor with that corpse skipped, while
// pred->next[0] still physically pointed at it -- so insert()'s CAS compared a logical successor
// against a physical link, could never match, and its retry loop never terminated.
//
// The whole trigger is: erase anything, then insert anything. Three operations hang the container.
// Nothing raced; the loop condition was simply unsatisfiable. These cases hang forever on master, so
// they were verified there under an external timeout rather than by watching them fail.
TEST_CASE("concurrent_skiplist::insert after erase") {
    SUBCASE("re-insert the same key") {
        concurrent_skiplist<int, std::string> sl;
        CHECK(sl.insert(1, "a"));
        CHECK(sl.erase(1));
        CHECK_FALSE(sl.find(1).has_value());

        CHECK(sl.insert(1, "b"));  // used to hang forever
        auto v = sl.find(1);
        REQUIRE(v.has_value());
        CHECK_EQ(v.value(), "b");
        CHECK_EQ(sl.size(), 1);
    }

    SUBCASE("insert a different key after an erase") {
        concurrent_skiplist<int, std::string> sl;
        CHECK(sl.insert(1, "a"));
        CHECK(sl.erase(1));

        CHECK(sl.insert(5, "b"));  // used to hang forever: any key, not just the erased one
        CHECK(sl.find(5).has_value());
        CHECK_FALSE(sl.find(1).has_value());
        CHECK_EQ(sl.size(), 1);
    }

    SUBCASE("churn: repeated insert/erase cycles stay consistent") {
        concurrent_skiplist<int, int> sl;
        for (int round = 0; round < 50; ++round) {
            for (int k = 0; k < 32; ++k) {
                CHECK(sl.insert(k, round * 100 + k));
            }
            CHECK_EQ(sl.size(), 32);
            for (int k = 0; k < 32; ++k) {
                CHECK(sl.erase(k));
            }
            CHECK_EQ(sl.size(), 0);
            for (int k = 0; k < 32; ++k) {
                CHECK_FALSE(sl.find(k).has_value());
            }
        }
    }

    SUBCASE("erased keys stay erased and live keys stay findable") {
        concurrent_skiplist<int, int> sl;
        for (int k = 0; k < 200; ++k) {
            sl.insert(k, k);
        }
        for (int k = 0; k < 200; k += 2) {
            CHECK(sl.erase(k));  // corpses now sit between every surviving pair
        }
        for (int k = 200; k < 400; ++k) {
            CHECK(sl.insert(k, k));  // each of these used to hang
        }
        CHECK_EQ(sl.size(), 300);
        for (int k = 0; k < 400; ++k) {
            bool want = (k >= 200) || (k % 2 == 1);
            CHECK_EQ(sl.find(k).has_value(), want);
        }
    }
}

// Erased nodes are handed to a retired list now, so exactly one thread must claim each node -- push it
// twice and the destructor frees it twice. The marking loop cannot tell you who won, because it exits
// on "already marked", which is also true for the thread that lost the CAS. On master both threads
// return true from erase() and both decrement _size.
TEST_CASE("concurrent_skiplist::concurrent erase of the same key claims it exactly once") {
    constexpr int kKeys = 400;
    constexpr int kThreads = 8;

    // Enough rounds to actually land the interleaving: on master this reliably over-counts within the
    // first handful, but five rounds is not enough to be sure of catching it.
    for (int round = 0; round < 40; ++round) {
        concurrent_skiplist<int, int> sl;
        for (int k = 0; k < kKeys; ++k) {
            sl.insert(k, k);
        }

        std::atomic<int> claimed{0};
        std::vector<std::thread> threads;
        threads.reserve(kThreads);
        for (int t = 0; t < kThreads; ++t) {
            threads.emplace_back([&] {
                int n = 0;
                for (int k = 0; k < kKeys; ++k) {
                    if (sl.erase(k)) {
                        ++n;
                    }
                }
                claimed += n;
            });
        }
        for (auto& t : threads) {
            t.join();
        }

        CHECK_EQ(claimed.load(), kKeys);  // not 422
        CHECK_EQ(sl.size(), 0);
    }
}


// A mark on a pointer belongs to the node the pointer came *out of*, not to the node it points *at*.
// Two places in the helping path got that backwards, and both quietly deleted live nodes:
//
//   * The branch was entered on `succ.marked() || curr.marked()` and then spliced pred->next in either
//     case. But curr.marked() is the mark on pred->next, so it says *pred* is being erased -- it says
//     nothing about curr, which may be a node someone inserted after pred a moment ago. Rewriting a
//     dying predecessor's next pointer splices that live node straight out.
//   * The walk that skipped the run of marked nodes went one node too far: it started from succ (already
//     marked, because curr is deleted), stepped to n->next, found it unmarked because n is *alive*, and
//     stopped holding n's successor rather than n. The CAS then spliced pred past the live n.
//
// Either way the node is still counted in _size, no longer reachable from level 0, and not on the
// retired list either -- so the destructor cannot free it. It shows up as size() disagreeing with what
// the list actually contains, which is the only way to see it: nothing else in the API reports it.
TEST_CASE("concurrent_skiplist::concurrent insert and erase never lose a live node") {
    constexpr int kN = 2000;
    constexpr int kThreads = 4;

    for (int round = 0; round < 25; ++round) {
        concurrent_skiplist<int, int> sl;

        std::vector<std::thread> threads;
        threads.reserve(kThreads * 2);
        for (int t = 0; t < kThreads; ++t) {
            // inserters cover every key; erasers only ever remove odd ones, so every even key that goes
            // in must still be there at the end -- and must be reachable, not merely counted.
            threads.emplace_back([&, t] {
                for (int i = t; i < kN; i += kThreads) {
                    sl.insert(i, i);
                }
            });
            threads.emplace_back([&, t] {
                for (int i = t; i < kN; i += kThreads) {
                    if (i % 2 == 1) {
                        sl.erase(i);
                    }
                }
            });
        }
        for (auto& t : threads) {
            t.join();
        }

        std::set<int> reachable;
        sl.for_each([&](int k, int) { reachable.insert(k); });

        INFO("round=", round);
        for (int i = 0; i < kN; i += 2) {
            INFO("even key=", i);
            CHECK(reachable.count(i) == 1);       // never erased -- must still be in the list
            CHECK(sl.find(i).has_value());        // and must be findable through the index
        }

        // size() must agree with what is actually there. A spliced-out node keeps its slot in the count.
        CHECK_EQ(sl.size(), reachable.size());
    }
}

}  // namespace stdb::container

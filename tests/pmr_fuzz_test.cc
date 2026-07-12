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

/*
 * PMR Fuzz Testing
 *
 * This file contains fuzz tests to verify PMR implementations work correctly
 * under heavy load with random operations. Tests cover:
 * - Large data volumes (10K-100K+ operations)
 * - Random operation sequences (insert, erase, find, copy, move)
 * - Memory resource switching
 * - Allocator propagation semantics
 * - Edge cases (empty, single element, etc.)
 */

#include "container/btree_map.hpp"
#include "container/concurrent_skiplist.hpp"
#include "container/dense_map.hpp"
#include "container/devectra.hpp"
#include "container/skiplist_map.hpp"
#include "container/small_vectra.hpp"

#include <atomic>
#include <memory_resource>
#include <doctest/doctest.h>

#include <algorithm>
#include <deque>
#include <map>
#include <random>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace stdb::container {
// NOLINTBEGIN

// ============================================================================
// Fuzz Test Utilities
// ============================================================================

class FuzzRng {
   public:
    explicit FuzzRng(uint64_t seed = 42) : _rng(seed) {}

    int randint(int min, int max) {
        std::uniform_int_distribution<int> dist(min, max);
        return dist(_rng);
    }

    size_t randsize(size_t min, size_t max) {
        std::uniform_int_distribution<size_t> dist(min, max);
        return dist(_rng);
    }

    double randdouble(double min, double max) {
        std::uniform_real_distribution<double> dist(min, max);
        return dist(_rng);
    }

    std::string randstring(size_t len) {
        static const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
        std::string result;
        result.reserve(len);
        for (size_t i = 0; i < len; ++i) {
            result += charset[randint(0, sizeof(charset) - 2)];
        }
        return result;
    }

    template <typename Container>
    typename Container::value_type& pick(Container& c) {
        auto it = c.begin();
        std::advance(it, randsize(0, c.size() - 1));
        return *it;
    }

   private:
    std::mt19937_64 _rng;
};

// Track allocations to verify no leaks
class TrackingMemoryResource : public std::pmr::memory_resource {
   public:
    TrackingMemoryResource() = default;

    size_t allocation_count() const { return _alloc_count; }
    size_t deallocation_count() const { return _dealloc_count; }
    size_t bytes_allocated() const { return _bytes_allocated; }
    size_t bytes_deallocated() const { return _bytes_deallocated; }
    size_t outstanding_bytes() const { return _bytes_allocated - _bytes_deallocated; }

    bool balanced() const {
        return _alloc_count == _dealloc_count && _bytes_allocated == _bytes_deallocated;
    }

   protected:
    void* do_allocate(size_t bytes, size_t alignment) override {
        ++_alloc_count;
        _bytes_allocated += bytes;
        return std::pmr::get_default_resource()->allocate(bytes, alignment);
    }

    void do_deallocate(void* p, size_t bytes, size_t alignment) override {
        ++_dealloc_count;
        _bytes_deallocated += bytes;
        std::pmr::get_default_resource()->deallocate(p, bytes, alignment);
    }

    bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override {
        return this == &other;
    }

   private:
    size_t _alloc_count = 0;
    size_t _dealloc_count = 0;
    size_t _bytes_allocated = 0;
    size_t _bytes_deallocated = 0;
};

// ============================================================================
// Map Container Fuzz Tests (dense_map, btree_map, skiplist_map)
// ============================================================================

// dense_map fuzz test (hash map - different template signature)
void fuzz_dense_map_operations(size_t num_ops, uint64_t seed) {
    TrackingMemoryResource resource;
    using Alloc = std::pmr::polymorphic_allocator<std::pair<int, std::string>>;
    Alloc alloc(&resource);

    {
        dense_map<int, std::string, dense_hash<int>, std::equal_to<int>, Alloc> map(alloc);
        std::unordered_map<int, std::string> reference;

        FuzzRng rng(seed);

        for (size_t i = 0; i < num_ops; ++i) {
            int op = rng.randint(0, 99);
            int key = rng.randint(0, 10000);
            std::string value = rng.randstring(rng.randsize(1, 50));

            if (op < 50) {
                auto [it, inserted] = map.insert({key, value});
                auto [ref_it, ref_inserted] = reference.insert({key, value});
                CHECK_EQ(inserted, ref_inserted);
            } else if (op < 70) {
                auto it = map.find(key);
                auto ref_it = reference.find(key);
                CHECK_EQ((it != map.end()), (ref_it != reference.end()));
                if (it != map.end() && ref_it != reference.end()) {
                    CHECK_EQ(it->second, ref_it->second);
                }
            } else if (op < 85) {
                size_t erased = map.erase(key);
                size_t ref_erased = reference.erase(key);
                CHECK_EQ(erased, ref_erased);
            } else if (op < 90) {
                map[key] = value;
                reference[key] = value;
            } else if (op < 95) {
                CHECK_EQ(map.size(), reference.size());
            } else {
                if (rng.randint(0, 9) == 0) {
                    map.clear();
                    reference.clear();
                }
            }
        }

        CHECK_EQ(map.size(), reference.size());
        for (const auto& [k, v] : reference) {
            auto it = map.find(k);
            CHECK(it != map.end());
            if (it != map.end()) {
                CHECK_EQ(it->second, v);
            }
        }
    }

    CHECK_MESSAGE(resource.balanced(),
                  "Memory leak detected: allocated=" << resource.bytes_allocated()
                                                     << " deallocated=" << resource.bytes_deallocated());
}

// btree_map fuzz test (ordered map)
void fuzz_btree_map_operations(size_t num_ops, uint64_t seed) {
    TrackingMemoryResource resource;
    using Alloc = std::pmr::polymorphic_allocator<std::pair<const int, std::string>>;
    Alloc alloc(&resource);

    {
        btree_map<int, std::string, std::less<int>, Alloc> map(alloc);
        std::map<int, std::string> reference;

        FuzzRng rng(seed);

        for (size_t i = 0; i < num_ops; ++i) {
            int op = rng.randint(0, 99);
            int key = rng.randint(0, 10000);
            std::string value = rng.randstring(rng.randsize(1, 50));

            if (op < 50) {
                auto [it, inserted] = map.insert({key, value});
                auto [ref_it, ref_inserted] = reference.insert({key, value});
                CHECK_EQ(inserted, ref_inserted);
            } else if (op < 70) {
                auto it = map.find(key);
                auto ref_it = reference.find(key);
                CHECK_EQ((it != map.end()), (ref_it != reference.end()));
                if (it != map.end() && ref_it != reference.end()) {
                    CHECK_EQ(it->second, ref_it->second);
                }
            } else if (op < 85) {
                size_t erased = map.erase(key);
                size_t ref_erased = reference.erase(key);
                CHECK_EQ(erased, ref_erased);
            } else if (op < 90) {
                map[key] = value;
                reference[key] = value;
            } else if (op < 95) {
                CHECK_EQ(map.size(), reference.size());
            } else {
                if (rng.randint(0, 9) == 0) {
                    map.clear();
                    reference.clear();
                }
            }
        }

        CHECK_EQ(map.size(), reference.size());
        for (const auto& [k, v] : reference) {
            auto it = map.find(k);
            CHECK(it != map.end());
            if (it != map.end()) {
                CHECK_EQ(it->second, v);
            }
        }
    }

    CHECK_MESSAGE(resource.balanced(),
                  "Memory leak detected: allocated=" << resource.bytes_allocated()
                                                     << " deallocated=" << resource.bytes_deallocated());
}

// skiplist_map fuzz test
void fuzz_skiplist_map_operations(size_t num_ops, uint64_t seed) {
    TrackingMemoryResource resource;
    using Alloc = std::pmr::polymorphic_allocator<std::pair<const int, std::string>>;
    Alloc alloc(&resource);

    {
        skiplist_map<int, std::string, std::less<int>, Alloc> map(alloc);
        std::map<int, std::string> reference;

        FuzzRng rng(seed);

        for (size_t i = 0; i < num_ops; ++i) {
            int op = rng.randint(0, 99);
            int key = rng.randint(0, 10000);
            std::string value = rng.randstring(rng.randsize(1, 50));

            if (op < 50) {
                auto [it, inserted] = map.insert({key, value});
                auto [ref_it, ref_inserted] = reference.insert({key, value});
                CHECK_EQ(inserted, ref_inserted);
            } else if (op < 70) {
                auto it = map.find(key);
                auto ref_it = reference.find(key);
                CHECK_EQ((it != map.end()), (ref_it != reference.end()));
                if (it != map.end() && ref_it != reference.end()) {
                    CHECK_EQ(it->second, ref_it->second);
                }
            } else if (op < 85) {
                size_t erased = map.erase(key);
                size_t ref_erased = reference.erase(key);
                CHECK_EQ(erased, ref_erased);
            } else if (op < 95) {
                CHECK_EQ(map.size(), reference.size());
            } else {
                if (rng.randint(0, 9) == 0) {
                    map.clear();
                    reference.clear();
                }
            }
        }

        CHECK_EQ(map.size(), reference.size());
        for (const auto& [k, v] : reference) {
            auto it = map.find(k);
            CHECK(it != map.end());
            if (it != map.end()) {
                CHECK_EQ(it->second, v);
            }
        }
    }

    CHECK_MESSAGE(resource.balanced(),
                  "Memory leak detected: allocated=" << resource.bytes_allocated()
                                                     << " deallocated=" << resource.bytes_deallocated());
}

TEST_CASE("pmr_fuzz::dense_map") {
    SUBCASE("1K operations") { fuzz_dense_map_operations(1000, 12345); }
    SUBCASE("5K operations") { fuzz_dense_map_operations(5000, 67890); }
    SUBCASE("different seed 1") { fuzz_dense_map_operations(2000, 11111); }
    SUBCASE("different seed 2") { fuzz_dense_map_operations(2000, 22222); }
}

TEST_CASE("pmr_fuzz::btree_map") {
    SUBCASE("1K operations") { fuzz_btree_map_operations(1000, 12345); }
    SUBCASE("5K operations") { fuzz_btree_map_operations(5000, 67890); }
    SUBCASE("different seed 1") { fuzz_btree_map_operations(2000, 33333); }
    SUBCASE("different seed 2") { fuzz_btree_map_operations(2000, 44444); }
}

TEST_CASE("pmr_fuzz::skiplist_map") {
    SUBCASE("1K operations") { fuzz_skiplist_map_operations(1000, 12345); }
    SUBCASE("5K operations") { fuzz_skiplist_map_operations(5000, 67890); }
    SUBCASE("different seed 1") { fuzz_skiplist_map_operations(2000, 55555); }
    SUBCASE("different seed 2") { fuzz_skiplist_map_operations(2000, 66666); }
}

// ============================================================================
// Vector Container Fuzz Tests (small_vectra, devectra)
// ============================================================================

void fuzz_small_vectra_operations(size_t num_ops, uint64_t seed) {
    TrackingMemoryResource resource;
    using Alloc = std::pmr::polymorphic_allocator<std::string>;
    Alloc alloc(&resource);

    {
        small_vectra<std::string, 4, Alloc> vec(alloc);
        std::vector<std::string> reference;

        FuzzRng rng(seed);

        for (size_t i = 0; i < num_ops; ++i) {
            int op = rng.randint(0, 99);
            std::string value = rng.randstring(rng.randsize(1, 100));

            if (op < 40) {
                // push_back
                vec.push_back(value);
                reference.push_back(value);
            } else if (op < 55) {
                // pop_back
                if (!vec.empty()) {
                    vec.pop_back();
                    reference.pop_back();
                }
            } else if (op < 65) {
                // Random access
                if (!vec.empty()) {
                    size_t idx = rng.randsize(0, vec.size() - 1);
                    CHECK_EQ(vec[idx], reference[idx]);
                }
            } else if (op < 75) {
                // Erase at position
                if (!vec.empty()) {
                    size_t idx = rng.randsize(0, vec.size() - 1);
                    vec.erase(vec.begin() + idx);
                    reference.erase(reference.begin() + idx);
                }
            } else if (op < 85) {
                // Insert at position
                if (vec.size() < 10000) {  // Limit size to avoid OOM
                    size_t idx = vec.empty() ? 0 : rng.randsize(0, vec.size());
                    vec.insert(vec.begin() + idx, value);
                    reference.insert(reference.begin() + idx, value);
                }
            } else if (op < 90) {
                // Size check
                CHECK_EQ(vec.size(), reference.size());
            } else if (op < 95) {
                // Reserve
                size_t cap = rng.randsize(vec.size(), vec.size() + 100);
                vec.reserve(cap);
                reference.reserve(cap);
            } else {
                // Clear occasionally
                if (rng.randint(0, 19) == 0) {
                    vec.clear();
                    reference.clear();
                }
            }
        }

        // Final consistency check
        CHECK_EQ(vec.size(), reference.size());
        for (size_t i = 0; i < vec.size(); ++i) {
            CHECK_EQ(vec[i], reference[i]);
        }
    }

    CHECK_MESSAGE(resource.balanced(),
                  "Memory leak detected: allocated=" << resource.bytes_allocated()
                                                     << " deallocated=" << resource.bytes_deallocated());
}

void fuzz_devectra_operations(size_t num_ops, uint64_t seed) {
    TrackingMemoryResource resource;
    std::pmr::polymorphic_allocator<std::string> alloc(&resource);

    {
        devectra<std::string, decltype(alloc)> vec(alloc);
        std::deque<std::string> reference;  // Use deque as reference for devectra

        FuzzRng rng(seed);

        for (size_t i = 0; i < num_ops; ++i) {
            int op = rng.randint(0, 99);
            std::string value = rng.randstring(rng.randsize(1, 100));

            if (op < 25) {
                // push_back
                vec.push_back(value);
                reference.push_back(value);
            } else if (op < 50) {
                // push_front
                vec.push_front(value);
                reference.push_front(value);
            } else if (op < 60) {
                // pop_back
                if (!vec.empty()) {
                    vec.pop_back();
                    reference.pop_back();
                }
            } else if (op < 70) {
                // pop_front
                if (!vec.empty()) {
                    vec.pop_front();
                    reference.pop_front();
                }
            } else if (op < 80) {
                // Random access
                if (!vec.empty()) {
                    size_t idx = rng.randsize(0, vec.size() - 1);
                    CHECK_EQ(vec[idx], reference[idx]);
                }
            } else if (op < 90) {
                // Size check
                CHECK_EQ(vec.size(), reference.size());
            } else {
                // Clear occasionally
                if (rng.randint(0, 19) == 0) {
                    vec.clear();
                    reference.clear();
                }
            }
        }

        // Final consistency check
        CHECK_EQ(vec.size(), reference.size());
        for (size_t i = 0; i < vec.size(); ++i) {
            CHECK_EQ(vec[i], reference[i]);
        }
    }

    CHECK_MESSAGE(resource.balanced(),
                  "Memory leak detected: allocated=" << resource.bytes_allocated()
                                                     << " deallocated=" << resource.bytes_deallocated());
}

TEST_CASE("pmr_fuzz::small_vectra") {
    SUBCASE("1K operations") { fuzz_small_vectra_operations(1000, 12345); }
    SUBCASE("5K operations") { fuzz_small_vectra_operations(5000, 67890); }
    SUBCASE("different seed 1") { fuzz_small_vectra_operations(2000, 77777); }
    SUBCASE("different seed 2") { fuzz_small_vectra_operations(2000, 88888); }
}

TEST_CASE("pmr_fuzz::devectra") {
    SUBCASE("1K operations") { fuzz_devectra_operations(1000, 12345); }
    SUBCASE("5K operations") { fuzz_devectra_operations(5000, 67890); }
    SUBCASE("different seed 1") { fuzz_devectra_operations(2000, 99999); }
    SUBCASE("different seed 2") { fuzz_devectra_operations(2000, 10101); }
}

// ============================================================================
// Copy/Move Semantics Fuzz Tests
// ============================================================================

TEST_CASE("pmr_fuzz::copy_move_semantics") {
    SUBCASE("dense_map copy/move stress") {
        TrackingMemoryResource res1, res2;
        using Alloc = std::pmr::polymorphic_allocator<std::pair<int, std::string>>;
        Alloc alloc1(&res1);
        Alloc alloc2(&res2);

        FuzzRng rng(54321);

        for (int trial = 0; trial < 100; ++trial) {
            dense_map<int, std::string, dense_hash<int>, std::equal_to<int>, Alloc> map1(alloc1);
            dense_map<int, std::string, dense_hash<int>, std::equal_to<int>, Alloc> map2(alloc2);

            // Fill map1
            size_t n = rng.randsize(10, 500);
            for (size_t i = 0; i < n; ++i) {
                map1[rng.randint(0, 1000)] = rng.randstring(20);
            }

            // Copy assignment (different allocators - should not propagate for PMR)
            map2 = map1;
            CHECK_EQ(map2.size(), map1.size());
            CHECK(map2.get_allocator().resource() == &res2);  // Allocator not propagated

            // Move assignment (different allocators - should move elements, not steal)
            dense_map<int, std::string, dense_hash<int>, std::equal_to<int>, Alloc> map3(alloc1);
            for (size_t i = 0; i < n / 2; ++i) {
                map3[rng.randint(0, 1000)] = rng.randstring(20);
            }
            size_t map3_size = map3.size();
            map2 = std::move(map3);
            CHECK_EQ(map2.size(), map3_size);
        }

        // Both resources should be balanced after all maps destroyed
        CHECK(res1.balanced());
        CHECK(res2.balanced());
    }

    SUBCASE("small_vectra copy/move stress") {
        TrackingMemoryResource res1, res2;
        std::pmr::polymorphic_allocator<std::string> alloc1(&res1);
        std::pmr::polymorphic_allocator<std::string> alloc2(&res2);

        FuzzRng rng(65432);

        for (int trial = 0; trial < 100; ++trial) {
            small_vectra<std::string, 4, decltype(alloc1)> vec1(alloc1);
            small_vectra<std::string, 4, decltype(alloc2)> vec2(alloc2);

            // Fill vec1
            size_t n = rng.randsize(1, 100);
            for (size_t i = 0; i < n; ++i) {
                vec1.push_back(rng.randstring(30));
            }

            // Copy assignment
            vec2 = vec1;
            CHECK_EQ(vec2.size(), vec1.size());

            // Move assignment
            small_vectra<std::string, 4, decltype(alloc1)> vec3(alloc1);
            for (size_t i = 0; i < n / 2; ++i) {
                vec3.push_back(rng.randstring(30));
            }
            size_t vec3_size = vec3.size();
            vec2 = std::move(vec3);
            CHECK_EQ(vec2.size(), vec3_size);
        }

        CHECK(res1.balanced());
        CHECK(res2.balanced());
    }

    SUBCASE("devectra copy/move stress") {
        TrackingMemoryResource res1, res2;
        std::pmr::polymorphic_allocator<std::string> alloc1(&res1);
        std::pmr::polymorphic_allocator<std::string> alloc2(&res2);

        FuzzRng rng(76543);

        for (int trial = 0; trial < 100; ++trial) {
            devectra<std::string, decltype(alloc1)> vec1(alloc1);
            devectra<std::string, decltype(alloc2)> vec2(alloc2);

            // Fill vec1 with mixed front/back operations
            size_t n = rng.randsize(1, 100);
            for (size_t i = 0; i < n; ++i) {
                if (rng.randint(0, 1)) {
                    vec1.push_back(rng.randstring(30));
                } else {
                    vec1.push_front(rng.randstring(30));
                }
            }

            // Copy assignment
            vec2 = vec1;
            CHECK_EQ(vec2.size(), vec1.size());

            // Move assignment
            devectra<std::string, decltype(alloc1)> vec3(alloc1);
            for (size_t i = 0; i < n / 2; ++i) {
                vec3.push_back(rng.randstring(30));
            }
            size_t vec3_size = vec3.size();
            vec2 = std::move(vec3);
            CHECK_EQ(vec2.size(), vec3_size);
        }

        CHECK(res1.balanced());
        CHECK(res2.balanced());
    }
}

// ============================================================================
// Concurrent Skiplist Fuzz Tests
// ============================================================================

TEST_CASE("pmr_fuzz::concurrent_skiplist") {
    SUBCASE("single thread 1K ops") {
        std::pmr::synchronized_pool_resource pool;
        stdb::pmr::concurrent_skiplist<int, std::string> sl(&pool);
        std::unordered_map<int, std::string> reference;

        FuzzRng rng(11111);
        constexpr size_t num_ops = 1000;

        for (size_t i = 0; i < num_ops; ++i) {
            int op = rng.randint(0, 99);
            int key = rng.randint(0, 5000);
            std::string value = rng.randstring(rng.randsize(1, 30));

            if (op < 60) {
                // Insert
                bool inserted = sl.insert(key, value);
                auto [it, ref_inserted] = reference.insert({key, value});
                CHECK_EQ(inserted, ref_inserted);
            } else if (op < 80) {
                // Find
                auto result = sl.find(key);
                auto ref_it = reference.find(key);
                CHECK_EQ(result.has_value(), (ref_it != reference.end()));
            } else {
                // Erase
                bool erased = sl.erase(key);
                bool ref_erased = reference.erase(key) > 0;
                CHECK_EQ(erased, ref_erased);
            }
        }

        CHECK_EQ(sl.size(), reference.size());
    }

    SUBCASE("multi-thread stress test") {
        std::pmr::synchronized_pool_resource pool;
        stdb::pmr::concurrent_skiplist<int, int> sl(&pool);

        constexpr int num_threads = 4;
        constexpr int ops_per_thread = 500;

        std::vector<std::thread> threads;
        std::atomic<int> successful_inserts{0};

        for (int t = 0; t < num_threads; ++t) {
            threads.emplace_back([&sl, &successful_inserts, t]() {
                FuzzRng rng(t * 1000 + 12345);
                for (int i = 0; i < ops_per_thread; ++i) {
                    int op = rng.randint(0, 99);
                    int key = rng.randint(0, 10000);

                    if (op < 50) {
                        if (sl.insert(key, key * 10)) {
                            successful_inserts.fetch_add(1, std::memory_order_relaxed);
                        }
                    } else if (op < 80) {
                        sl.contains(key);
                    } else {
                        sl.erase(key);
                    }
                }
            });
        }

        for (auto& th : threads) {
            th.join();
        }

        // Verify data integrity
        sl.for_each([](const int& k, const int& v) { CHECK_EQ(v, k * 10); });
    }

    SUBCASE("high contention scenario") {
        std::pmr::synchronized_pool_resource pool;
        stdb::pmr::concurrent_skiplist<int, int> sl(&pool);

        constexpr int num_threads = 4;
        constexpr int key_range = 100;  // Small range = high contention
        constexpr int ops_per_thread = 500;

        std::vector<std::thread> threads;

        for (int t = 0; t < num_threads; ++t) {
            threads.emplace_back([&sl, t]() {
                FuzzRng rng(t * 7777);
                for (int i = 0; i < ops_per_thread; ++i) {
                    int op = rng.randint(0, 99);
                    int key = rng.randint(0, key_range);

                    if (op < 40) {
                        sl.insert(key, key);
                    } else if (op < 70) {
                        sl.find(key);
                    } else {
                        sl.erase(key);
                    }
                }
            });
        }

        for (auto& th : threads) {
            th.join();
        }

        // Just verify no crashes and data is consistent
        sl.for_each([](const int& k, const int& v) { CHECK_EQ(v, k); });
    }
}

// ============================================================================
// Large Scale Tests
// ============================================================================

TEST_CASE("pmr_fuzz::large_scale") {
    SUBCASE("dense_map 10K elements") {
        TrackingMemoryResource resource;
        using Alloc = std::pmr::polymorphic_allocator<std::pair<int, int>>;
        Alloc alloc(&resource);

        {
            dense_map<int, int, dense_hash<int>, std::equal_to<int>, Alloc> map(alloc);

            constexpr int N = 10000;
            for (int i = 0; i < N; ++i) {
                map[i] = i * 2;
            }

            CHECK_EQ(map.size(), N);

            // Verify all elements
            for (int i = 0; i < N; ++i) {
                auto it = map.find(i);
                CHECK(it != map.end());
                CHECK_EQ(it->second, i * 2);
            }

            // Erase half
            for (int i = 0; i < N; i += 2) {
                map.erase(i);
            }

            CHECK_EQ(map.size(), N / 2);
        }

        CHECK(resource.balanced());
    }

    SUBCASE("btree_map 10K elements") {
        TrackingMemoryResource resource;
        using Alloc = std::pmr::polymorphic_allocator<std::pair<const int, int>>;
        Alloc alloc(&resource);

        {
            btree_map<int, int, std::less<int>, Alloc> map(alloc);

            constexpr int N = 10000;
            for (int i = 0; i < N; ++i) {
                map[i] = i * 3;
            }

            CHECK_EQ(map.size(), N);

            // Verify in order (btree maintains order)
            int expected = 0;
            for (const auto& [k, v] : map) {
                CHECK_EQ(k, expected);
                CHECK_EQ(v, expected * 3);
                ++expected;
            }
        }

        CHECK(resource.balanced());
    }

    SUBCASE("small_vectra large strings") {
        TrackingMemoryResource resource;
        std::pmr::polymorphic_allocator<std::string> alloc(&resource);

        {
            small_vectra<std::string, 2, decltype(alloc)> vec(alloc);

            constexpr int N = 1000;
            for (int i = 0; i < N; ++i) {
                // Large strings to stress memory allocation
                vec.push_back(std::string(1000, 'a' + (i % 26)));
            }

            CHECK_EQ(vec.size(), N);

            // Verify
            for (int i = 0; i < N; ++i) {
                CHECK_EQ(vec[i].size(), 1000);
                CHECK_EQ(vec[i][0], 'a' + (i % 26));
            }
        }

        CHECK(resource.balanced());
    }

    SUBCASE("devectra alternating push") {
        TrackingMemoryResource resource;
        std::pmr::polymorphic_allocator<int> alloc(&resource);

        {
            devectra<int, decltype(alloc)> vec(alloc);

            constexpr int N = 5000;
            for (int i = 0; i < N; ++i) {
                if (i % 2 == 0) {
                    vec.push_back(i);
                } else {
                    vec.push_front(-i);
                }
            }

            CHECK_EQ(vec.size(), N);

            // Verify front elements are negative, back elements are positive
            CHECK_LT(vec.front(), 0);
            CHECK_GE(vec.back(), 0);
        }

        CHECK(resource.balanced());
    }
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST_CASE("pmr_fuzz::edge_cases") {
    SUBCASE("empty container operations") {
        TrackingMemoryResource resource;
        using MapAlloc = std::pmr::polymorphic_allocator<std::pair<int, int>>;
        using VecAlloc = std::pmr::polymorphic_allocator<int>;
        MapAlloc map_alloc(&resource);
        VecAlloc vec_alloc(&resource);

        {
            dense_map<int, int, dense_hash<int>, std::equal_to<int>, MapAlloc> map(map_alloc);
            small_vectra<int, 4, VecAlloc> vec(vec_alloc);
            devectra<int, VecAlloc> dvec(vec_alloc);

            // Operations on empty containers
            CHECK(map.empty());
            CHECK(vec.empty());
            CHECK(dvec.empty());

            CHECK(map.find(42) == map.end());
            CHECK_EQ(map.erase(42), 0);

            // Copy empty
            auto map2 = map;
            auto vec2 = vec;
            auto dvec2 = dvec;

            CHECK(map2.empty());
            CHECK(vec2.empty());
            CHECK(dvec2.empty());

            // Move empty
            auto map3 = std::move(map2);
            auto vec3 = std::move(vec2);
            auto dvec3 = std::move(dvec2);

            CHECK(map3.empty());
            CHECK(vec3.empty());
            CHECK(dvec3.empty());
        }

        CHECK(resource.balanced());
    }

    SUBCASE("single element") {
        TrackingMemoryResource resource;
        using MapAlloc = std::pmr::polymorphic_allocator<std::pair<int, int>>;
        using VecAlloc = std::pmr::polymorphic_allocator<int>;
        MapAlloc map_alloc(&resource);
        VecAlloc vec_alloc(&resource);

        {
            dense_map<int, int, dense_hash<int>, std::equal_to<int>, MapAlloc> map(map_alloc);
            small_vectra<int, 4, VecAlloc> vec(vec_alloc);
            devectra<int, VecAlloc> dvec(vec_alloc);

            map[1] = 100;
            vec.push_back(200);
            dvec.push_front(300);

            CHECK_EQ(map.size(), 1);
            CHECK_EQ(vec.size(), 1);
            CHECK_EQ(dvec.size(), 1);

            // Copy single element
            auto map2 = map;
            auto vec2 = vec;
            auto dvec2 = dvec;

            CHECK_EQ(map2[1], 100);
            CHECK_EQ(vec2[0], 200);
            CHECK_EQ(dvec2[0], 300);

            // Erase single element
            map.erase(1);
            vec.pop_back();
            dvec.pop_front();

            CHECK(map.empty());
            CHECK(vec.empty());
            CHECK(dvec.empty());
        }

        CHECK(resource.balanced());
    }

    SUBCASE("repeated insert/erase same key") {
        TrackingMemoryResource resource;
        using Alloc = std::pmr::polymorphic_allocator<std::pair<int, std::string>>;
        Alloc alloc(&resource);

        {
            dense_map<int, std::string, dense_hash<int>, std::equal_to<int>, Alloc> map(alloc);

            constexpr int iterations = 1000;
            for (int i = 0; i < iterations; ++i) {
                map[42] = "value" + std::to_string(i);
                CHECK_EQ(map.size(), 1);
                map.erase(42);
                CHECK(map.empty());
            }
        }

        CHECK(resource.balanced());
    }
}

// NOLINTEND
}  // namespace stdb::container

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

#include "container/dense_map.hpp"
#include "container/dense_set.hpp"

#include <memory_resource>
#include <array>
#include <random>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "doctest/doctest/doctest.h"

namespace stdb::container {

namespace {

struct CountingIntHash {
    size_t* calls{nullptr};

    auto operator()(int value) const noexcept -> size_t {
        ++*calls;
        return dense_hash<int>{}(value);
    }
};

}  // namespace

TEST_CASE("dense_map::basic") {
    SUBCASE("default constructor") {
        dense_map<int, int> map;
        CHECK(map.empty());
        CHECK_EQ(map.size(), 0);
    }

    SUBCASE("initializer list") {
        dense_map<int, std::string> map{{1, "one"}, {2, "two"}, {3, "three"}};
        CHECK_EQ(map.size(), 3);
        CHECK_EQ(map.at(1), "one");
        CHECK_EQ(map.at(2), "two");
        CHECK_EQ(map.at(3), "three");
    }

    SUBCASE("capacity constructor") {
        dense_map<int, int> map(100);
        CHECK(map.empty());
        CHECK_GE(map.capacity(), 100);
    }
}

TEST_CASE("dense_map::insert") {
    SUBCASE("insert single value") {
        dense_map<int, int> map;
        auto [it, inserted] = map.insert({1, 10});
        CHECK(inserted);
        CHECK_EQ(it->first, 1);
        CHECK_EQ(it->second, 10);
        CHECK_EQ(map.size(), 1);

        // Insert duplicate
        auto [it2, inserted2] = map.insert({1, 20});
        CHECK_FALSE(inserted2);
        CHECK_EQ(it2->second, 10);  // Original value unchanged
        CHECK_EQ(map.size(), 1);
    }

    SUBCASE("insert move") {
        dense_map<int, std::string> map;
        std::string value = "hello";
        auto [it, inserted] = map.insert({1, std::move(value)});
        CHECK(inserted);
        CHECK_EQ(it->second, "hello");
    }

    SUBCASE("insert_or_assign") {
        dense_map<int, int> map;
        auto [it1, inserted1] = map.insert_or_assign(1, 10);
        CHECK(inserted1);
        CHECK_EQ(it1->second, 10);

        auto [it2, inserted2] = map.insert_or_assign(1, 20);
        CHECK_FALSE(inserted2);
        CHECK_EQ(it2->second, 20);  // Value updated
    }

    SUBCASE("insert range") {
        std::vector<std::pair<int, int>> vec{{1, 10}, {2, 20}, {3, 30}};
        dense_map<int, int> map;
        map.insert(vec.begin(), vec.end());
        CHECK_EQ(map.size(), 3);
        CHECK_EQ(map.at(1), 10);
        CHECK_EQ(map.at(2), 20);
        CHECK_EQ(map.at(3), 30);
    }
}

TEST_CASE("dense_map::emplace") {
    SUBCASE("emplace") {
        dense_map<int, std::string> map;
        auto [it, inserted] = map.emplace(1, "hello");
        CHECK(inserted);
        CHECK_EQ(it->first, 1);
        CHECK_EQ(it->second, "hello");

        // Emplace duplicate
        auto [it2, inserted2] = map.emplace(1, "world");
        CHECK_FALSE(inserted2);
        CHECK_EQ(it2->second, "hello");
    }

    SUBCASE("try_emplace") {
        dense_map<int, std::string> map;
        auto [it1, inserted1] = map.try_emplace(1, "hello");
        CHECK(inserted1);
        CHECK_EQ(it1->second, "hello");

        // try_emplace on existing key - should NOT update
        auto [it2, inserted2] = map.try_emplace(1, "world");
        CHECK_FALSE(inserted2);
        CHECK_EQ(it2->second, "hello");  // Original value preserved
    }

    SUBCASE("try_emplace_prehashed") {
        auto check_policy = []<typename Policy>() {
            size_t hash_calls = 0;
            using Value = std::pair<int, std::string>;
            dense_map<int, std::string, CountingIntHash, std::equal_to<int>, std::allocator<Value>, Policy> map(
                64, CountingIntHash{&hash_calls});

            for (int key = 0; key < 32; ++key) {
                const auto hash = dense_hash<int>{}(key);
                auto [it, inserted] = key % 2 == 0
                                        ? map.try_emplace_prehashed(hash, key, std::to_string(key))
                                        : map.try_emplace_prehashed(hash, int{key}, std::to_string(key));
                CHECK(inserted);
                CHECK_EQ(it->second, std::to_string(key));
            }
            CHECK_EQ(hash_calls, 0);

            int duplicate = 7;
            auto [it, inserted] =
                map.try_emplace_prehashed(dense_hash<int>{}(duplicate), duplicate, "replacement");
            CHECK_FALSE(inserted);
            CHECK_EQ(it->second, "7");
            CHECK_EQ(hash_calls, 0);
        };

        check_policy.template operator()<force_inline_policy>();
        check_policy.template operator()<force_flat_policy>();
        check_policy.template operator()<force_indirect_policy>();
    }
}

TEST_CASE("dense_map::erase") {
    SUBCASE("erase by key") {
        dense_map<int, int> map{{1, 10}, {2, 20}, {3, 30}};
        auto erased = map.erase(2);
        CHECK_EQ(erased, 1);
        CHECK_EQ(map.size(), 2);
        CHECK_FALSE(map.contains(2));
        CHECK(map.contains(1));
        CHECK(map.contains(3));

        // Erase non-existent
        erased = map.erase(100);
        CHECK_EQ(erased, 0);
    }

    SUBCASE("erase by iterator") {
        dense_map<int, int> map{{1, 10}, {2, 20}, {3, 30}};
        auto it = map.find(2);
        REQUIRE(it != map.end());
        map.erase(it);
        CHECK_EQ(map.size(), 2);
        CHECK_FALSE(map.contains(2));
    }

    SUBCASE("clear") {
        dense_map<int, int> map{{1, 10}, {2, 20}, {3, 30}};
        map.clear();
        CHECK(map.empty());
        CHECK_EQ(map.size(), 0);
    }
}

TEST_CASE("dense_map::lookup") {
    dense_map<int, std::string> map{{1, "one"}, {2, "two"}, {3, "three"}};

    SUBCASE("find existing") {
        auto it = map.find(2);
        REQUIRE(it != map.end());
        CHECK_EQ(it->first, 2);
        CHECK_EQ(it->second, "two");
    }

    SUBCASE("find non-existing") {
        auto it = map.find(100);
        CHECK(it == map.end());
    }

    SUBCASE("contains") {
        CHECK(map.contains(1));
        CHECK(map.contains(2));
        CHECK(map.contains(3));
        CHECK_FALSE(map.contains(100));
    }

    SUBCASE("at") {
        CHECK_EQ(map.at(1), "one");
        CHECK_THROWS_AS(map.at(100), std::out_of_range);
    }

    SUBCASE("operator[]") {
        CHECK_EQ(map[1], "one");
        // operator[] inserts default value for non-existent key
        map[100] = "hundred";
        CHECK(map.contains(100));
        CHECK_EQ(map[100], "hundred");
    }

    SUBCASE("count") {
        CHECK_EQ(map.count(1), 1);
        CHECK_EQ(map.count(100), 0);
    }

    SUBCASE("equal_range") {
        auto [first, last] = map.equal_range(2);
        REQUIRE(first != last);
        CHECK_EQ(first->first, 2);
        ++first;
        CHECK(first == last);

        auto [f2, l2] = map.equal_range(100);
        CHECK(f2 == l2);
    }
}

TEST_CASE("dense_map::iterator") {
    SUBCASE("iteration") {
        dense_map<int, int> map{{1, 10}, {2, 20}, {3, 30}};
        std::unordered_map<int, int> found;
        for (const auto& [k, v] : map) {
            found[k] = v;
        }
        CHECK_EQ(found.size(), 3);
        CHECK_EQ(found[1], 10);
        CHECK_EQ(found[2], 20);
        CHECK_EQ(found[3], 30);
    }

    SUBCASE("empty iteration") {
        dense_map<int, int> map;
        int count = 0;
        for ([[maybe_unused]] const auto& kv : map) {
            ++count;
        }
        CHECK_EQ(count, 0);
    }

    SUBCASE("const iteration") {
        const dense_map<int, int> map{{1, 10}, {2, 20}};
        std::unordered_map<int, int> found;
        for (const auto& [k, v] : map) {
            found[k] = v;
        }
        CHECK_EQ(found.size(), 2);
    }
}

TEST_CASE("dense_map::copy_and_move") {
    SUBCASE("copy constructor") {
        dense_map<int, std::string> original{{1, "one"}, {2, "two"}};
        dense_map<int, std::string> copy(original);
        CHECK_EQ(copy.size(), 2);
        CHECK_EQ(copy.at(1), "one");
        CHECK_EQ(copy.at(2), "two");

        // Modify original, copy unchanged
        original[1] = "ONE";
        CHECK_EQ(copy.at(1), "one");
    }

    SUBCASE("copy assignment") {
        dense_map<int, int> original{{1, 10}, {2, 20}};
        dense_map<int, int> copy;
        copy = original;
        CHECK_EQ(copy.size(), 2);
        CHECK_EQ(copy.at(1), 10);
    }

    SUBCASE("move constructor") {
        dense_map<int, std::string> original{{1, "one"}, {2, "two"}};
        dense_map<int, std::string> moved(std::move(original));
        CHECK_EQ(moved.size(), 2);
        CHECK_EQ(moved.at(1), "one");
        CHECK(original.empty());  // NOLINT
    }

    SUBCASE("move assignment") {
        dense_map<int, int> original{{1, 10}, {2, 20}};
        dense_map<int, int> moved;
        moved = std::move(original);
        CHECK_EQ(moved.size(), 2);
        CHECK(original.empty());  // NOLINT
    }

    SUBCASE("swap") {
        dense_map<int, int> a{{1, 10}};
        dense_map<int, int> b{{2, 20}, {3, 30}};
        a.swap(b);
        CHECK_EQ(a.size(), 2);
        CHECK_EQ(b.size(), 1);
        CHECK(a.contains(2));
        CHECK(b.contains(1));
    }
}

TEST_CASE("dense_map::hash_policy") {
    SUBCASE("load_factor") {
        dense_map<int, int> map;
        CHECK_EQ(map.load_factor(), 0.0f);

        map[1] = 10;
        CHECK_GT(map.load_factor(), 0.0f);
        CHECK_LE(map.load_factor(), map.max_load_factor());
    }

    SUBCASE("reserve") {
        dense_map<int, int> map;
        map.reserve(1000);
        CHECK_GE(map.capacity(), 1000);
        size_t cap = map.capacity();

        // Inserting up to reserved amount shouldn't reallocate
        for (int i = 0; i < 800; ++i) {
            map[i] = i * 10;
        }
        CHECK_EQ(map.capacity(), cap);
    }

    SUBCASE("rehash") {
        dense_map<int, int> map;
        for (int i = 0; i < 100; ++i) {
            map[i] = i;
        }
        size_t old_cap = map.capacity();

        map.rehash(old_cap * 2);
        CHECK_GE(map.capacity(), old_cap * 2);

        // All elements should still be accessible
        for (int i = 0; i < 100; ++i) {
            CHECK_EQ(map.at(i), i);
        }
    }
}

TEST_CASE("dense_map::reserve preallocates contiguous string values") {
    dense_map<std::string, uint64_t> map;
    constexpr uint64_t kCount = 256;
    map.reserve(kCount);
    map.reserve_values(kCount);

    auto [first, inserted] = map.try_emplace("key-0", 0);
    REQUIRE(inserted);
    const auto* first_value = &*first;
    for (uint64_t i = 1; i < kCount; ++i) {
        auto [_, added] = map.try_emplace("key-" + std::to_string(i), i);
        REQUIRE(added);
    }

    CHECK_EQ(&*map.find("key-0"), first_value);
}

TEST_CASE("dense_map::direct memory footprint supports every storage policy") {
    constexpr size_t kReservedEntries = 32;

    auto check_policy = [=]<typename Policy>() {
        using Value = std::pair<int, int>;
        using Map = dense_map<int, int, dense_hash<int>, std::equal_to<>, std::allocator<Value>, Policy>;

        Map map;
        auto empty = map.direct_memory_footprint();
        REQUIRE(empty.has_value());
        CHECK_EQ(empty->bucket_capacity, 0);
        CHECK_EQ(empty->entry_capacity, 0);
        CHECK_EQ(empty->value_capacity, 0);
        CHECK_EQ(empty->direct_allocation_bytes, 0);
        auto zero_projection = map.projected_direct_memory_footprint(0);
        REQUIRE(zero_projection.has_value());
        CHECK(*zero_projection == *empty);

        auto projected = map.projected_direct_memory_footprint(kReservedEntries);
        REQUIRE(projected.has_value());
        CHECK_GE(projected->entry_capacity, kReservedEntries);
        CHECK_GE(projected->value_capacity, kReservedEntries);
        CHECK_EQ(map.capacity(), 0);
        CHECK_EQ(map.size(), 0);

        map.reserve(kReservedEntries);
        map.reserve_values(kReservedEntries);
        auto current = map.direct_memory_footprint();
        REQUIRE(current.has_value());
        CHECK(*current == *projected);
        CHECK_EQ(map.entry_capacity(), current->entry_capacity);
        CHECK_EQ(map.value_capacity(), current->value_capacity);

        size_t expected_bytes = 0;
        if constexpr (std::is_same_v<Policy, force_inline_policy>) {
            expected_bytes = (map.capacity() + detail::kGroupWidth + 1) * sizeof(int8_t) +
                             map.capacity() * sizeof(Value);
        } else if constexpr (std::is_same_v<Policy, force_flat_policy>) {
            expected_bytes = map.capacity() * sizeof(detail::Bucket) + map.value_capacity() * sizeof(Value);
        } else {
            expected_bytes = (map.capacity() + detail::kGroupWidth + 1) * sizeof(int8_t) +
                             map.capacity() * sizeof(uint32_t) + map.value_capacity() * sizeof(Value);
        }
        CHECK_EQ(current->direct_allocation_bytes, expected_bytes);

        const auto bucket_capacity = map.capacity();
        const auto value_capacity = map.value_capacity();
        for (size_t i = 0; i < kReservedEntries; ++i) {
            map.emplace(static_cast<int>(i), static_cast<int>(i));
        }
        CHECK_EQ(map.capacity(), bucket_capacity);
        CHECK_EQ(map.value_capacity(), value_capacity);
        auto after_insert = map.direct_memory_footprint();
        REQUIRE(after_insert.has_value());
        CHECK(*after_insert == *current);

        auto no_growth = map.projected_direct_memory_footprint(kReservedEntries);
        REQUIRE(no_growth.has_value());
        CHECK(*no_growth == *current);

        const auto second_growth_count = current->entry_capacity + 1;
        auto second_projection = map.projected_direct_memory_footprint(second_growth_count);
        REQUIRE(second_projection.has_value());
        map.reserve(second_growth_count);
        map.reserve_values(second_growth_count);
        auto after_second_growth = map.direct_memory_footprint();
        REQUIRE(after_second_growth.has_value());
        CHECK(*after_second_growth == *second_projection);
    };

    check_policy.template operator()<force_inline_policy>();
    check_policy.template operator()<force_flat_policy>();
    check_policy.template operator()<force_indirect_policy>();
}

TEST_CASE("dense_map::memory footprint rejects overflowing projections") {
    using Value = std::pair<int, int>;
    using FlatMap = dense_map<int, int, dense_hash<int>, std::equal_to<>, std::allocator<Value>, force_flat_policy>;
    using IndirectMap =
        dense_map<int, int, dense_hash<int>, std::equal_to<>, std::allocator<Value>, force_indirect_policy>;

    if constexpr (std::numeric_limits<size_t>::digits > std::numeric_limits<uint32_t>::digits) {
        const auto max_entries = static_cast<size_t>(std::numeric_limits<uint32_t>::max());
        const auto too_many = max_entries + 1;
        auto check_policy = [=]<typename Policy>() {
            using Map = dense_map<int, int, dense_hash<int>, std::equal_to<>, std::allocator<Value>, Policy>;
            Map map;
            CHECK(map.projected_direct_memory_footprint(max_entries).has_value());
            CHECK_FALSE(map.projected_direct_memory_footprint(too_many).has_value());
            CHECK_THROWS_AS(map.reserve(too_many), std::length_error);
            CHECK_THROWS_AS(map.reserve_values(too_many), std::length_error);
            CHECK_EQ(map.capacity(), 0);
            CHECK_EQ(map.size(), 0);
        };
        check_policy.template operator()<force_flat_policy>();
        check_policy.template operator()<force_indirect_policy>();
    }

    CHECK_FALSE(FlatMap{}.projected_direct_memory_footprint(std::numeric_limits<size_t>::max()).has_value());
    CHECK_FALSE(IndirectMap{}.projected_direct_memory_footprint(std::numeric_limits<size_t>::max()).has_value());
}

TEST_CASE("dense_map::reserve_values before reserve preserves the value allocation") {
    constexpr size_t kReservedEntries = 32;
    auto check_policy = [=]<typename Policy>() {
        using Value = std::pair<int, int>;
        using Map = dense_map<int, int, dense_hash<int>, std::equal_to<>, std::allocator<Value>, Policy>;

        Map map;
        map.reserve_values(8);
        auto values_only = map.direct_memory_footprint();
        REQUIRE(values_only.has_value());
        auto zero_projection = map.projected_direct_memory_footprint(0);
        REQUIRE(zero_projection.has_value());
        CHECK(*zero_projection == *values_only);
        if constexpr (std::is_same_v<Policy, force_inline_policy>) {
            CHECK_NE(values_only->bucket_capacity, 0);
        } else {
            CHECK_EQ(values_only->bucket_capacity, 0);
            CHECK_EQ(values_only->value_capacity, 8);
            CHECK_GT(values_only->direct_allocation_bytes, 0);
        }

        auto projected = map.projected_direct_memory_footprint(kReservedEntries);
        REQUIRE(projected.has_value());
        map.reserve(kReservedEntries);
        map.reserve_values(kReservedEntries);
        auto current = map.direct_memory_footprint();
        REQUIRE(current.has_value());
        CHECK(*current == *projected);

        map.emplace(1, 1);
        auto after_insert = map.direct_memory_footprint();
        REQUIRE(after_insert.has_value());
        CHECK(*after_insert == *current);
    };

    check_policy.template operator()<force_inline_policy>();
    check_policy.template operator()<force_flat_policy>();
    check_policy.template operator()<force_indirect_policy>();
}

TEST_CASE("dense_map::reserve_values grows values when buckets already suffice") {
    constexpr size_t kReservedEntries = 32;
    auto check_policy = [=]<typename Policy>() {
        using Value = std::pair<int, int>;
        using Map = dense_map<int, int, dense_hash<int>, std::equal_to<>, std::allocator<Value>, Policy>;

        Map map(64);
        const auto bucket_capacity = map.capacity();
        const auto before = map.direct_memory_footprint();
        REQUIRE(before.has_value());
        auto projected = map.projected_direct_memory_footprint(kReservedEntries);
        REQUIRE(projected.has_value());
        map.reserve_values(kReservedEntries);
        auto after = map.direct_memory_footprint();
        REQUIRE(after.has_value());
        CHECK_EQ(map.capacity(), bucket_capacity);
        CHECK(*after == *projected);
        CHECK_GT(after->value_capacity, before->value_capacity);
    };

    check_policy.template operator()<force_flat_policy>();
    check_policy.template operator()<force_indirect_policy>();
}

TEST_CASE("dense_map::stress_test") {
    SUBCASE("large insert and lookup") {
        constexpr int N = 10000;
        dense_map<int, int> map;
        map.reserve(N);

        for (int i = 0; i < N; ++i) {
            map[i] = i * 2;
        }
        CHECK_EQ(map.size(), N);

        for (int i = 0; i < N; ++i) {
            CHECK_EQ(map.at(i), i * 2);
        }
    }

    SUBCASE("random operations") {
        std::mt19937 gen(42);
        std::uniform_int_distribution<int> dist(0, 10000);

        dense_map<int, int> map;
        std::unordered_map<int, int> reference;

        for (int i = 0; i < 5000; ++i) {
            int key = dist(gen);
            int value = dist(gen);

            if (dist(gen) % 3 == 0 && !reference.empty()) {
                // Erase
                auto it = reference.begin();
                map.erase(it->first);
                reference.erase(it);
            } else {
                // Insert
                map[key] = value;
                reference[key] = value;
            }
        }

        CHECK_EQ(map.size(), reference.size());
        for (const auto& [k, v] : reference) {
            CHECK_EQ(map.at(k), v);
        }
    }

    SUBCASE("insert and erase interleaved") {
        dense_map<int, int> map;

        // Insert 1000 elements
        for (int i = 0; i < 1000; ++i) {
            map[i] = i;
        }

        // Erase even numbers
        for (int i = 0; i < 1000; i += 2) {
            map.erase(i);
        }
        CHECK_EQ(map.size(), 500);

        // Verify odd numbers remain
        for (int i = 1; i < 1000; i += 2) {
            CHECK_EQ(map.at(i), i);
        }

        // Reinsert evens
        for (int i = 0; i < 1000; i += 2) {
            map[i] = i * 10;
        }
        CHECK_EQ(map.size(), 1000);

        // Verify all
        for (int i = 0; i < 1000; ++i) {
            if (i % 2 == 0) {
                CHECK_EQ(map.at(i), i * 10);
            } else {
                CHECK_EQ(map.at(i), i);
            }
        }
    }
}

TEST_CASE("dense_map::string_keys") {
    SUBCASE("basic string operations") {
        dense_map<std::string, int> map;
        map["hello"] = 1;
        map["world"] = 2;
        map["test"] = 3;

        CHECK_EQ(map.size(), 3);
        CHECK_EQ(map.at("hello"), 1);
        CHECK_EQ(map.at("world"), 2);
        CHECK(map.contains("test"));
        CHECK_FALSE(map.contains("missing"));
    }

    SUBCASE("string stress test") {
        dense_map<std::string, int> map;
        for (int i = 0; i < 1000; ++i) {
            map["key_" + std::to_string(i)] = i;
        }
        CHECK_EQ(map.size(), 1000);

        for (int i = 0; i < 1000; ++i) {
            CHECK_EQ(map.at("key_" + std::to_string(i)), i);
        }
    }
}

TEST_CASE("dense_map::transparent_lookup") {
    SUBCASE("default string policies support transparent lookup") {
        dense_map<std::string, int> map;
        map["hello"] = 1;
        map["world"] = 2;

        // Lookup with string_view
        std::string_view sv = "hello";
        CHECK(map.contains(sv));
        CHECK_EQ(map.find(sv)->second, 1);

        // Lookup with const char*
        CHECK(map.contains("world"));
    }
}

TEST_CASE("dense_map::equality") {
    SUBCASE("equal maps") {
        dense_map<int, int> a{{1, 10}, {2, 20}, {3, 30}};
        dense_map<int, int> b{{3, 30}, {1, 10}, {2, 20}};  // Different order
        CHECK(a == b);
        CHECK_FALSE(a != b);
    }

    SUBCASE("different sizes") {
        dense_map<int, int> a{{1, 10}, {2, 20}};
        dense_map<int, int> b{{1, 10}};
        CHECK(a != b);
        CHECK_FALSE(a == b);
    }

    SUBCASE("different values") {
        dense_map<int, int> a{{1, 10}};
        dense_map<int, int> b{{1, 20}};
        CHECK(a != b);
    }

    SUBCASE("different keys") {
        dense_map<int, int> a{{1, 10}};
        dense_map<int, int> b{{2, 10}};
        CHECK(a != b);
    }
}

TEST_CASE("dense_map::hash_quality") {
    // Test that hash distribution is reasonable
    SUBCASE("sequential keys") {
        dense_map<int, int> map;
        for (int i = 0; i < 10000; ++i) {
            map[i] = i;
        }
        CHECK_EQ(map.size(), 10000);
        CHECK_LE(map.load_factor(), map.max_load_factor());
    }

    SUBCASE("sparse keys") {
        dense_map<int, int> map;
        for (int i = 0; i < 1000; ++i) {
            map[i * 1000] = i;
        }
        CHECK_EQ(map.size(), 1000);
    }
}

// Additional comprehensive tests based on tsl/ankerl patterns

TEST_CASE("dense_map::edge_cases") {
    SUBCASE("operations on empty map") {
        dense_map<int, int> map;
        CHECK(map.find(1) == map.end());
        CHECK_EQ(map.erase(1), 0);
        CHECK_EQ(map.count(1), 0);
        CHECK_FALSE(map.contains(1));
        map.clear();  // Should not crash
        CHECK(map.empty());
    }

    SUBCASE("single element") {
        dense_map<int, int> map;
        map[42] = 100;
        CHECK_EQ(map.size(), 1);
        CHECK_EQ(map.at(42), 100);

        // Erase and re-insert
        map.erase(42);
        CHECK(map.empty());
        map[42] = 200;
        CHECK_EQ(map.at(42), 200);
    }

    SUBCASE("erase all elements") {
        dense_map<int, int> map;
        for (int i = 0; i < 100; ++i) {
            map[i] = i;
        }

        // Erase all one by one
        for (int i = 0; i < 100; ++i) {
            CHECK_EQ(map.erase(i), 1);
        }
        CHECK(map.empty());
        CHECK_EQ(map.size(), 0);

        // Insert again
        map[0] = 0;
        CHECK_EQ(map.size(), 1);
    }

    SUBCASE("rehash to smaller") {
        dense_map<int, int> map;
        map.reserve(1000);
        for (int i = 0; i < 10; ++i) {
            map[i] = i;
        }
        map.rehash(16);
        CHECK_EQ(map.size(), 10);
        for (int i = 0; i < 10; ++i) {
            CHECK_EQ(map.at(i), i);
        }
    }
}

TEST_CASE("dense_map::various_key_types") {
    SUBCASE("int8 keys") {
        dense_map<int8_t, int> map;
        for (int8_t i = -100; i < 100; ++i) {
            map[i] = i * 2;
        }
        CHECK_EQ(map.size(), 200);
        for (int8_t i = -100; i < 100; ++i) {
            CHECK_EQ(map.at(i), i * 2);
        }
    }

    SUBCASE("int64 keys") {
        dense_map<int64_t, int> map;
        for (int64_t i = 0; i < 1000; ++i) {
            map[i * 1000000000LL] = static_cast<int>(i);
        }
        CHECK_EQ(map.size(), 1000);
        for (int64_t i = 0; i < 1000; ++i) {
            CHECK_EQ(map.at(i * 1000000000LL), static_cast<int>(i));
        }
    }

    SUBCASE("pointer keys") {
        std::vector<int> values(100);
        dense_map<int*, int> map;
        for (int i = 0; i < 100; ++i) {
            values[i] = i;
            map[&values[i]] = i * 10;
        }
        CHECK_EQ(map.size(), 100);
        for (int i = 0; i < 100; ++i) {
            CHECK_EQ(map.at(&values[i]), i * 10);
        }
    }

    SUBCASE("size_t keys") {
        dense_map<size_t, int> map;
        for (size_t i = 0; i < 1000; ++i) {
            map[i] = static_cast<int>(i);
        }
        CHECK_EQ(map.size(), 1000);
    }
}

TEST_CASE("dense_map::collision_handling") {
    // Custom hash that forces collisions
    struct BadHash {
        size_t operator()(int key) const noexcept {
            return static_cast<size_t>(key % 8);  // Only 8 different hash values
        }
    };

    SUBCASE("many collisions insert") {
        dense_map<int, int, BadHash> map;
        for (int i = 0; i < 1000; ++i) {
            map[i] = i * 2;
        }
        CHECK_EQ(map.size(), 1000);
        for (int i = 0; i < 1000; ++i) {
            CHECK_EQ(map.at(i), i * 2);
        }
    }

    SUBCASE("many collisions erase") {
        dense_map<int, int, BadHash> map;
        for (int i = 0; i < 1000; ++i) {
            map[i] = i;
        }

        // Erase every other element
        for (int i = 0; i < 1000; i += 2) {
            map.erase(i);
        }
        CHECK_EQ(map.size(), 500);

        // Check remaining
        for (int i = 1; i < 1000; i += 2) {
            CHECK_EQ(map.at(i), i);
        }
    }
}

TEST_CASE("dense_map::complex_values") {
    SUBCASE("vector value") {
        dense_map<int, std::vector<int>> map;
        map[1] = {1, 2, 3};
        map[2] = {4, 5, 6, 7, 8};

        CHECK_EQ(map.at(1).size(), 3);
        CHECK_EQ(map.at(2).size(), 5);
        CHECK_EQ(map.at(1)[0], 1);
        CHECK_EQ(map.at(2)[4], 8);
    }

    SUBCASE("map value") {
        dense_map<int, std::unordered_map<std::string, int>> map;
        map[1]["a"] = 1;
        map[1]["b"] = 2;
        map[2]["x"] = 10;

        CHECK_EQ(map.at(1).size(), 2);
        CHECK_EQ(map.at(2).at("x"), 10);
    }

    SUBCASE("pair value") {
        dense_map<int, std::pair<std::string, double>> map;
        map[1] = {"hello", 3.14};
        map[2] = {"world", 2.71};

        CHECK_EQ(map.at(1).first, "hello");
        CHECK_EQ(map.at(1).second, doctest::Approx(3.14));
    }
}

TEST_CASE("dense_map::move_only_values") {
    SUBCASE("unique_ptr value") {
        dense_map<int, std::unique_ptr<int>> map;
        map.emplace(1, std::make_unique<int>(100));
        map.emplace(2, std::make_unique<int>(200));

        CHECK_EQ(map.size(), 2);
        CHECK_EQ(*map.at(1), 100);
        CHECK_EQ(*map.at(2), 200);

        // Move out
        auto ptr = std::move(map[1]);
        CHECK_EQ(*ptr, 100);
        CHECK(map[1] == nullptr);
    }
}

TEST_CASE("dense_map::iterator_validity") {
    SUBCASE("erase maintains iteration") {
        dense_map<int, int> map;
        for (int i = 0; i < 100; ++i) {
            map[i] = i;
        }

        // Count elements during iteration
        int count = 0;
        for (auto it = map.begin(); it != map.end(); ++it) {
            ++count;
        }
        CHECK_EQ(count, 100);
    }

    SUBCASE("modification during iteration via reference") {
        dense_map<int, int> map;
        for (int i = 0; i < 100; ++i) {
            map[i] = i;
        }

        for (auto& [k, v] : map) {
            v *= 2;
        }

        for (int i = 0; i < 100; ++i) {
            CHECK_EQ(map.at(i), i * 2);
        }
    }
}

TEST_CASE("dense_map::high_load_factor") {
    SUBCASE("insert at high load") {
        dense_map<int, int> map;
        map.reserve(16);  // Small initial capacity

        // Insert up to near max load
        for (int i = 0; i < 13; ++i) {  // ~81% load
            map[i] = i;
        }
        CHECK_EQ(map.size(), 13);

        // Insert more to trigger rehash
        for (int i = 13; i < 30; ++i) {
            map[i] = i;
        }
        CHECK_EQ(map.size(), 30);

        // Verify all
        for (int i = 0; i < 30; ++i) {
            CHECK_EQ(map.at(i), i);
        }
    }
}

TEST_CASE("dense_map::negative_keys") {
    SUBCASE("negative int keys") {
        dense_map<int, int> map;
        for (int i = -1000; i < 1000; ++i) {
            map[i] = i * 2;
        }
        CHECK_EQ(map.size(), 2000);

        for (int i = -1000; i < 1000; ++i) {
            CHECK_EQ(map.at(i), i * 2);
        }
    }
}

TEST_CASE("dense_map::erase_patterns") {
    SUBCASE("erase from beginning") {
        dense_map<int, int> map;
        for (int i = 0; i < 100; ++i) {
            map[i] = i;
        }

        for (int i = 0; i < 50; ++i) {
            map.erase(i);
        }
        CHECK_EQ(map.size(), 50);

        for (int i = 50; i < 100; ++i) {
            CHECK_EQ(map.at(i), i);
        }
    }

    SUBCASE("erase from end") {
        dense_map<int, int> map;
        for (int i = 0; i < 100; ++i) {
            map[i] = i;
        }

        for (int i = 99; i >= 50; --i) {
            map.erase(i);
        }
        CHECK_EQ(map.size(), 50);

        for (int i = 0; i < 50; ++i) {
            CHECK_EQ(map.at(i), i);
        }
    }

    SUBCASE("erase random order") {
        dense_map<int, int> map;
        std::vector<int> keys;
        for (int i = 0; i < 100; ++i) {
            map[i] = i;
            keys.push_back(i);
        }

        std::mt19937 gen(42);
        std::shuffle(keys.begin(), keys.end(), gen);

        // Erase half in random order
        for (int i = 0; i < 50; ++i) {
            map.erase(keys[i]);
        }
        CHECK_EQ(map.size(), 50);

        // Verify remaining
        for (int i = 50; i < 100; ++i) {
            CHECK(map.contains(keys[i]));
        }
    }
}

TEST_CASE("dense_map::large_scale") {
    SUBCASE("100K elements") {
        constexpr int N = 100000;
        dense_map<int64_t, int64_t> map;

        for (int64_t i = 0; i < N; ++i) {
            map[i] = i * 2;
        }
        CHECK_EQ(map.size(), N);

        for (int64_t i = 0; i < N; ++i) {
            CHECK_EQ(map.at(i), i * 2);
        }

        // Erase half
        for (int64_t i = 0; i < N; i += 2) {
            map.erase(i);
        }
        CHECK_EQ(map.size(), N / 2);
    }
}

TEST_CASE("dense_map::duplicate_insert_behavior") {
    SUBCASE("insert does not overwrite") {
        dense_map<int, int> map;
        map.insert({1, 100});
        auto [it, inserted] = map.insert({1, 200});

        CHECK_FALSE(inserted);
        CHECK_EQ(it->second, 100);  // Original value
    }

    SUBCASE("insert_or_assign does overwrite") {
        dense_map<int, int> map;
        map.insert({1, 100});
        auto [it, inserted] = map.insert_or_assign(1, 200);

        CHECK_FALSE(inserted);
        CHECK_EQ(it->second, 200);  // New value
    }

    SUBCASE("try_emplace does not construct on duplicate") {
        static int construct_count = 0;
        struct Counter {
            Counter() { ++construct_count; }
            Counter(int) { ++construct_count; }
            Counter(const Counter&) { ++construct_count; }
            Counter(Counter&&) noexcept { ++construct_count; }
        };

        construct_count = 0;
        dense_map<int, Counter> map;
        map.try_emplace(1, 100);
        int after_first = construct_count;

        map.try_emplace(1, 200);  // Should not construct
        CHECK_EQ(construct_count, after_first);
    }
}

TEST_CASE("dense_map::find_patterns") {
    dense_map<int, int> map;
    for (int i = 0; i < 1000; ++i) {
        map[i * 2] = i;  // Only even keys
    }

    SUBCASE("find existing keys") {
        for (int i = 0; i < 1000; ++i) {
            auto it = map.find(i * 2);
            REQUIRE(it != map.end());
            CHECK_EQ(it->second, i);
        }
    }

    SUBCASE("find non-existing keys") {
        for (int i = 0; i < 1000; ++i) {
            auto it = map.find(i * 2 + 1);  // Odd keys don't exist
            CHECK(it == map.end());
        }
    }
}

// ============================================================================
// Comprehensive string key tests (Swiss Table indirect storage)
// These tests specifically target the SIMD bitmask conversion and probing logic
// ============================================================================

TEST_CASE("dense_map::string_indirect_storage") {
    SUBCASE("insert and find with random strings") {
        // This test caught the magic multiply overflow bug
        std::mt19937_64 rng(42);
        dense_map<std::string, int64_t> map;
        std::vector<std::string> keys;

        auto random_string = [&rng](int len) {
            static const char chars[] = "abcdefghijklmnopqrstuvwxyz";
            std::string s;
            for (int i = 0; i < len; i++) s += chars[rng() % 26];
            return s;
        };

        // Insert keys one by one and verify find after each
        for (int i = 0; i < 100; ++i) {
            auto key = random_string(16);
            keys.push_back(key);
            map[key] = i;

            // Verify ALL keys are still findable
            for (size_t j = 0; j <= static_cast<size_t>(i); ++j) {
                auto it = map.find(keys[j]);
                REQUIRE(it != map.end());
                CHECK_EQ(it->second, static_cast<int64_t>(j));
            }
        }
    }

    SUBCASE("multiple rehashes with string keys") {
        std::mt19937_64 rng(123);
        dense_map<std::string, int> map;
        std::vector<std::string> keys;

        auto random_string = [&rng](int len) {
            static const char chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
            std::string s;
            for (int i = 0; i < len; i++) s += chars[rng() % 62];
            return s;
        };

        // Insert enough to trigger multiple rehashes
        for (int i = 0; i < 10000; ++i) {
            auto key = random_string(20);
            keys.push_back(key);
            map[key] = i;
        }

        CHECK_EQ(map.size(), 10000);

        // Verify all keys
        for (size_t i = 0; i < keys.size(); ++i) {
            auto it = map.find(keys[i]);
            REQUIRE(it != map.end());
            CHECK_EQ(it->second, static_cast<int>(i));
        }
    }

    SUBCASE("collision handling with similar hash values") {
        dense_map<std::string, int> map;

        // Insert strings that might have similar hash patterns
        std::vector<std::string> keys;
        for (int i = 0; i < 1000; ++i) {
            keys.push_back("prefix_" + std::to_string(i) + "_suffix");
        }

        for (size_t i = 0; i < keys.size(); ++i) {
            map[keys[i]] = static_cast<int>(i);
        }

        CHECK_EQ(map.size(), keys.size());

        // Verify all
        for (size_t i = 0; i < keys.size(); ++i) {
            CHECK_EQ(map.at(keys[i]), static_cast<int>(i));
        }
    }

    SUBCASE("empty string key") {
        dense_map<std::string, int> map;
        map[""] = 42;
        CHECK_EQ(map.at(""), 42);
        CHECK(map.contains(""));
        CHECK_EQ(map.size(), 1);

        map["non_empty"] = 100;
        CHECK_EQ(map.at(""), 42);
        CHECK_EQ(map.at("non_empty"), 100);
    }

    SUBCASE("very long string keys") {
        dense_map<std::string, int> map;
        std::string long_key(1000, 'x');

        for (int i = 0; i < 100; ++i) {
            std::string key = long_key + std::to_string(i);
            map[key] = i;
        }

        CHECK_EQ(map.size(), 100);

        for (int i = 0; i < 100; ++i) {
            std::string key = long_key + std::to_string(i);
            CHECK_EQ(map.at(key), i);
        }
    }

    SUBCASE("erase and reinsert string keys") {
        std::mt19937_64 rng(456);
        dense_map<std::string, int> map;
        std::vector<std::string> keys;

        auto random_string = [&rng](int len) {
            static const char chars[] = "abcdefghijklmnopqrstuvwxyz";
            std::string s;
            for (int i = 0; i < len; i++) s += chars[rng() % 26];
            return s;
        };

        // Insert keys
        for (int i = 0; i < 500; ++i) {
            auto key = random_string(12);
            keys.push_back(key);
            map[key] = i;
        }

        // Erase half
        for (int i = 0; i < 250; ++i) {
            map.erase(keys[i]);
        }
        CHECK_EQ(map.size(), 250);

        // Verify remaining
        for (int i = 250; i < 500; ++i) {
            REQUIRE(map.contains(keys[i]));
            CHECK_EQ(map.at(keys[i]), i);
        }

        // Verify erased are gone
        for (int i = 0; i < 250; ++i) {
            CHECK_FALSE(map.contains(keys[i]));
        }

        // Reinsert erased keys with new values
        for (int i = 0; i < 250; ++i) {
            map[keys[i]] = i + 1000;
        }
        CHECK_EQ(map.size(), 500);

        // Verify all
        for (int i = 0; i < 250; ++i) {
            CHECK_EQ(map.at(keys[i]), i + 1000);
        }
        for (int i = 250; i < 500; ++i) {
            CHECK_EQ(map.at(keys[i]), i);
        }
    }

    SUBCASE("iteration correctness with string keys") {
        dense_map<std::string, int> map;
        std::unordered_map<std::string, int> reference;

        for (int i = 0; i < 1000; ++i) {
            std::string key = "iter_key_" + std::to_string(i);
            map[key] = i;
            reference[key] = i;
        }

        // Verify via iteration
        int count = 0;
        for (const auto& [k, v] : map) {
            auto it = reference.find(k);
            REQUIRE(it != reference.end());
            CHECK_EQ(v, it->second);
            ++count;
        }
        CHECK_EQ(count, 1000);
    }

    SUBCASE("mixed insert and find operations") {
        std::mt19937_64 rng(789);
        dense_map<std::string, int> map;
        std::unordered_map<std::string, int> reference;

        auto random_string = [&rng](int len) {
            static const char chars[] = "abcdefghijklmnopqrstuvwxyz0123456789";
            std::string s;
            for (int i = 0; i < len; i++) s += chars[rng() % 36];
            return s;
        };

        // Mixed operations
        for (int i = 0; i < 5000; ++i) {
            if (rng() % 3 != 0) {
                // Insert
                auto key = random_string(8);
                map[key] = i;
                reference[key] = i;
            } else if (!reference.empty()) {
                // Find random existing key
                auto it = reference.begin();
                std::advance(it, rng() % reference.size());
                auto map_it = map.find(it->first);
                REQUIRE(map_it != map.end());
                CHECK_EQ(map_it->second, it->second);
            }
        }

        CHECK_EQ(map.size(), reference.size());
    }

    SUBCASE("small capacity transitions") {
        // Test behavior around capacity boundaries (8, 16, 32, ...)
        dense_map<std::string, int> map;

        for (int i = 0; i < 50; ++i) {
            std::string key = "key" + std::to_string(i);
            map[key] = i;

            // Verify all after each insert
            for (int j = 0; j <= i; ++j) {
                std::string check_key = "key" + std::to_string(j);
                auto it = map.find(check_key);
                REQUIRE(it != map.end());
                CHECK_EQ(it->second, j);
            }
        }
    }
}

TEST_CASE("dense_map::string_key_edge_cases") {
    SUBCASE("keys with null bytes") {
        dense_map<std::string, int> map;
        // Use string constructor with explicit length to include null bytes
        std::string key1("hello\0world", 11);
        std::string key2("hello\0other", 11);

        map[key1] = 1;
        map[key2] = 2;

        CHECK_EQ(map.size(), 2);
        CHECK_EQ(map.at(key1), 1);
        CHECK_EQ(map.at(key2), 2);
    }

    SUBCASE("keys with special characters") {
        dense_map<std::string, int> map;
        std::vector<std::string> keys = {
            "hello world",
            "tab\there",
            "newline\nhere",
            "unicode: \xc3\xa9\xc3\xa0",  // UTF-8 éà
            "emoji: \xf0\x9f\x98\x80",    // UTF-8 emoji
        };

        for (size_t i = 0; i < keys.size(); ++i) {
            map[keys[i]] = static_cast<int>(i);
        }

        CHECK_EQ(map.size(), keys.size());
        for (size_t i = 0; i < keys.size(); ++i) {
            CHECK_EQ(map.at(keys[i]), static_cast<int>(i));
        }
    }

    SUBCASE("duplicate inserts") {
        dense_map<std::string, int> map;
        std::string key = "duplicate_key";

        map[key] = 1;
        CHECK_EQ(map.at(key), 1);

        map[key] = 2;
        CHECK_EQ(map.at(key), 2);
        CHECK_EQ(map.size(), 1);

        auto [it, inserted] = map.insert({key, 3});
        CHECK_FALSE(inserted);
        CHECK_EQ(map.at(key), 2);  // Not changed by insert
    }

    SUBCASE("clear and reuse") {
        dense_map<std::string, int> map;

        for (int round = 0; round < 3; ++round) {
            for (int i = 0; i < 100; ++i) {
                map["key_" + std::to_string(i)] = round * 100 + i;
            }
            CHECK_EQ(map.size(), 100);

            for (int i = 0; i < 100; ++i) {
                CHECK_EQ(map.at("key_" + std::to_string(i)), round * 100 + i);
            }

            map.clear();
            CHECK_EQ(map.size(), 0);
        }
    }
}

TEST_CASE("dense_map::large_key_value") {
    // Test with large keys (128-byte strings) and large values (256-byte struct)
    struct LargeValue {
        std::array<uint64_t, 32> data;  // 256 bytes

        LargeValue() { data.fill(0); }
        explicit LargeValue(uint64_t seed) {
            for (size_t i = 0; i < data.size(); i++) {
                data[i] = seed + i;
            }
        }

        bool operator==(const LargeValue& other) const {
            return data == other.data;
        }
    };

    auto random_string = [](std::mt19937_64& rng, int len) {
        static const char chars[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
        std::string s;
        s.reserve(len);
        for (int i = 0; i < len; i++) {
            s += chars[rng() % (sizeof(chars) - 1)];
        }
        return s;
    };

    SUBCASE("large keys 128 bytes") {
        std::mt19937_64 rng(42);
        dense_map<std::string, int> map;
        std::vector<std::string> keys;

        for (int i = 0; i < 1000; ++i) {
            auto key = random_string(rng, 128);
            keys.push_back(key);
            map[key] = i;
        }

        CHECK_EQ(map.size(), 1000);

        for (size_t i = 0; i < keys.size(); ++i) {
            auto it = map.find(keys[i]);
            REQUIRE(it != map.end());
            CHECK_EQ(it->second, static_cast<int>(i));
        }
    }

    SUBCASE("large values 256 bytes") {
        dense_map<int, LargeValue> map;

        for (int i = 0; i < 1000; ++i) {
            map[i] = LargeValue(i);
        }

        CHECK_EQ(map.size(), 1000);

        for (int i = 0; i < 1000; ++i) {
            auto it = map.find(i);
            REQUIRE(it != map.end());
            CHECK(it->second == LargeValue(i));
        }
    }

    SUBCASE("large keys and values together") {
        std::mt19937_64 rng(123);
        dense_map<std::string, LargeValue> map;
        std::vector<std::string> keys;

        for (int i = 0; i < 5000; ++i) {
            auto key = random_string(rng, 128);
            keys.push_back(key);
            map[key] = LargeValue(i);
        }

        CHECK_EQ(map.size(), 5000);

        // Verify all keys and values
        for (size_t i = 0; i < keys.size(); ++i) {
            auto it = map.find(keys[i]);
            REQUIRE(it != map.end());
            CHECK(it->second == LargeValue(i));
        }

        // Verify iteration
        size_t count = 0;
        for (const auto& [k, v] : map) {
            ++count;
        }
        CHECK_EQ(count, 5000);
    }

    SUBCASE("large keys with rehash") {
        std::mt19937_64 rng(456);
        dense_map<std::string, LargeValue> map;
        std::vector<std::string> keys;

        // Insert enough to trigger multiple rehashes
        for (int i = 0; i < 10000; ++i) {
            auto key = random_string(rng, 128);
            keys.push_back(key);
            map[key] = LargeValue(i);

            // Verify all keys after every 1000 inserts
            if ((i + 1) % 1000 == 0) {
                for (size_t j = 0; j <= static_cast<size_t>(i); ++j) {
                    auto it = map.find(keys[j]);
                    if (it == map.end()) {
                        FAIL("Key " << j << " not found after inserting " << (i + 1) << " keys");
                    }
                }
            }
        }

        CHECK_EQ(map.size(), 10000);
    }

    SUBCASE("large keys erase and reinsert") {
        std::mt19937_64 rng(789);
        dense_map<std::string, LargeValue> map;
        std::vector<std::string> keys;

        // Insert
        for (int i = 0; i < 1000; ++i) {
            auto key = random_string(rng, 128);
            keys.push_back(key);
            map[key] = LargeValue(i);
        }

        // Erase half
        for (int i = 0; i < 500; ++i) {
            map.erase(keys[i]);
        }
        CHECK_EQ(map.size(), 500);

        // Verify remaining
        for (int i = 500; i < 1000; ++i) {
            auto it = map.find(keys[i]);
            REQUIRE(it != map.end());
            CHECK(it->second == LargeValue(i));
        }

        // Reinsert with different values
        for (int i = 0; i < 500; ++i) {
            map[keys[i]] = LargeValue(i + 10000);
        }
        CHECK_EQ(map.size(), 1000);

        // Verify all
        for (int i = 0; i < 500; ++i) {
            CHECK(map.at(keys[i]) == LargeValue(i + 10000));
        }
        for (int i = 500; i < 1000; ++i) {
            CHECK(map.at(keys[i]) == LargeValue(i));
        }
    }

    SUBCASE("very large keys 1KB") {
        std::mt19937_64 rng(999);
        dense_map<std::string, int> map;
        std::vector<std::string> keys;

        for (int i = 0; i < 500; ++i) {
            auto key = random_string(rng, 1024);  // 1KB keys
            keys.push_back(key);
            map[key] = i;
        }

        CHECK_EQ(map.size(), 500);

        for (size_t i = 0; i < keys.size(); ++i) {
            auto it = map.find(keys[i]);
            REQUIRE(it != map.end());
            CHECK_EQ(it->second, static_cast<int>(i));
        }
    }
}

TEST_CASE("dense_map::simd_bitmask_correctness") {
    // These tests specifically target the SIMD bitmask logic
    // that was buggy with the magic multiply approach

    SUBCASE("many collisions at same probe position") {
        dense_map<std::string, int> map;
        // Create keys that will have various h2 values
        for (int i = 0; i < 256; ++i) {
            std::string key = "collision_test_" + std::to_string(i);
            map[key] = i;
        }

        CHECK_EQ(map.size(), 256);

        for (int i = 0; i < 256; ++i) {
            std::string key = "collision_test_" + std::to_string(i);
            auto it = map.find(key);
            REQUIRE(it != map.end());
            CHECK_EQ(it->second, i);
        }
    }

    SUBCASE("interleaved insert-find pattern") {
        // This pattern caught the bug where keys became unfindable
        // after subsequent inserts
        std::mt19937_64 rng(999);
        dense_map<std::string, int> map;
        std::vector<std::pair<std::string, int>> inserted;

        auto random_string = [&rng](int len) {
            static const char chars[] = "abcdefghijklmnopqrstuvwxyz";
            std::string s;
            for (int i = 0; i < len; i++) s += chars[rng() % 26];
            return s;
        };

        for (int i = 0; i < 200; ++i) {
            // Insert new key
            auto key = random_string(16);
            map[key] = i;
            inserted.emplace_back(key, i);

            // Immediately verify ALL previously inserted keys
            for (const auto& [k, v] : inserted) {
                auto it = map.find(k);
                if (it == map.end()) {
                    // Detailed failure message
                    FAIL("Key '" << k << "' (value=" << v
                         << ") not found after inserting key " << i
                         << " (map size=" << map.size() << ")");
                }
                CHECK_EQ(it->second, v);
            }
        }
    }

    SUBCASE("probe sequence exhaustive test") {
        // Force many probe sequence iterations
        dense_map<std::string, int> map;
        map.reserve(16);  // Small capacity

        // Insert keys to fill most slots
        std::vector<std::string> keys;
        for (int i = 0; i < 12; ++i) {  // High load factor
            std::string key = "probe_" + std::to_string(i);
            keys.push_back(key);
            map[key] = i;
        }

        // Verify all finds work
        for (int i = 0; i < 12; ++i) {
            auto it = map.find(keys[i]);
            REQUIRE(it != map.end());
            CHECK_EQ(it->second, i);
        }

        // Check non-existent keys
        for (int i = 100; i < 120; ++i) {
            auto it = map.find("probe_" + std::to_string(i));
            CHECK(it == map.end());
        }
    }
}

// ============================================================================
// PMR (Polymorphic Memory Resource) Tests
// ============================================================================

TEST_CASE("dense_map::pmr") {
    SUBCASE("basic operations with monotonic_buffer_resource") {
        // Create a buffer and monotonic resource
        std::array<std::byte, 4096> buffer;
        std::pmr::monotonic_buffer_resource resource(buffer.data(), buffer.size(),
                                                     std::pmr::null_memory_resource());

        // Create a PMR dense_map
        pmr::dense_map<int, std::string> map(&resource);

        // Basic insert and find
        map[1] = "one";
        map[2] = "two";
        map[3] = "three";

        CHECK_EQ(map.size(), 3);
        CHECK_EQ(map[1], "one");
        CHECK_EQ(map[2], "two");
        CHECK_EQ(map[3], "three");

        // Erase
        map.erase(2);
        CHECK_EQ(map.size(), 2);
        CHECK(map.find(2) == map.end());
    }

    SUBCASE("allocator-only constructor") {
        std::pmr::monotonic_buffer_resource resource;
        pmr::dense_map<int, int> map(&resource);

        CHECK(map.empty());
        CHECK_EQ(map.size(), 0);

        map[42] = 100;
        CHECK_EQ(map.size(), 1);
        CHECK_EQ(map[42], 100);
    }

    SUBCASE("copy constructor uses select_on_container_copy_construction") {
        std::pmr::monotonic_buffer_resource resource1;
        std::pmr::monotonic_buffer_resource resource2;

        pmr::dense_map<int, int> map1(&resource1);
        map1[1] = 10;
        map1[2] = 20;

        // Copy constructor - for PMR, uses default memory resource (not resource1)
        pmr::dense_map<int, int> map2(map1);

        CHECK_EQ(map2.size(), 2);
        CHECK_EQ(map2[1], 10);
        CHECK_EQ(map2[2], 20);
    }

    SUBCASE("allocator-extended copy constructor") {
        std::pmr::monotonic_buffer_resource resource1;
        std::pmr::monotonic_buffer_resource resource2;

        pmr::dense_map<int, int> map1(&resource1);
        map1[1] = 10;
        map1[2] = 20;

        // Copy with explicit allocator
        pmr::dense_map<int, int> map2(map1, &resource2);

        CHECK_EQ(map2.size(), 2);
        CHECK_EQ(map2[1], 10);
        CHECK_EQ(map2[2], 20);
        CHECK_EQ(map2.get_allocator().resource(), &resource2);
    }

    SUBCASE("allocator-extended move constructor - same resource") {
        std::pmr::monotonic_buffer_resource resource;

        pmr::dense_map<int, int> map1(&resource);
        map1[1] = 10;
        map1[2] = 20;

        // Move with same allocator - should steal resources
        pmr::dense_map<int, int> map2(std::move(map1), &resource);

        CHECK_EQ(map2.size(), 2);
        CHECK_EQ(map2[1], 10);
        CHECK_EQ(map2[2], 20);
        CHECK(map1.empty());  // Resources were stolen
    }

    SUBCASE("allocator-extended move constructor - different resource") {
        std::pmr::monotonic_buffer_resource resource1;
        std::pmr::monotonic_buffer_resource resource2;

        pmr::dense_map<int, int> map1(&resource1);
        map1[1] = 10;
        map1[2] = 20;

        // Move with different allocator - must copy elements
        pmr::dense_map<int, int> map2(std::move(map1), &resource2);

        CHECK_EQ(map2.size(), 2);
        CHECK_EQ(map2[1], 10);
        CHECK_EQ(map2[2], 20);
        CHECK_EQ(map2.get_allocator().resource(), &resource2);
    }

    SUBCASE("copy assignment - keeps allocator") {
        std::pmr::monotonic_buffer_resource resource1;
        std::pmr::monotonic_buffer_resource resource2;

        pmr::dense_map<int, int> map1(&resource1);
        map1[1] = 10;
        map1[2] = 20;

        pmr::dense_map<int, int> map2(&resource2);
        map2[3] = 30;

        // Copy assignment - PMR does not propagate allocator
        map2 = map1;

        CHECK_EQ(map2.size(), 2);
        CHECK_EQ(map2[1], 10);
        CHECK_EQ(map2[2], 20);
        CHECK_EQ(map2.get_allocator().resource(), &resource2);  // Kept original allocator
    }

    SUBCASE("move assignment - same resource") {
        std::pmr::monotonic_buffer_resource resource;

        pmr::dense_map<int, int> map1(&resource);
        map1[1] = 10;
        map1[2] = 20;

        pmr::dense_map<int, int> map2(&resource);
        map2[3] = 30;

        // Move assignment - same resource, should steal resources
        map2 = std::move(map1);

        CHECK_EQ(map2.size(), 2);
        CHECK_EQ(map2[1], 10);
        CHECK_EQ(map2[2], 20);
        CHECK(map1.empty());  // Resources were stolen
    }

    SUBCASE("move assignment - different resource") {
        std::pmr::monotonic_buffer_resource resource1;
        std::pmr::monotonic_buffer_resource resource2;

        pmr::dense_map<int, int> map1(&resource1);
        map1[1] = 10;
        map1[2] = 20;

        pmr::dense_map<int, int> map2(&resource2);
        map2[3] = 30;

        // Move assignment - different resource, must move elements
        map2 = std::move(map1);

        CHECK_EQ(map2.size(), 2);
        CHECK_EQ(map2[1], 10);
        CHECK_EQ(map2[2], 20);
        CHECK_EQ(map2.get_allocator().resource(), &resource2);  // Kept original allocator
    }

    SUBCASE("string keys with PMR") {
        std::pmr::monotonic_buffer_resource resource;
        pmr::dense_map<std::string, int> map(&resource);

        map["hello"] = 1;
        map["world"] = 2;
        map["test"] = 3;

        CHECK_EQ(map.size(), 3);
        CHECK_EQ(map["hello"], 1);
        CHECK_EQ(map["world"], 2);
        CHECK_EQ(map["test"], 3);

        // Test iteration
        int count = 0;
        for (const auto& [key, value] : map) {
            (void)key;
            (void)value;
            ++count;
        }
        CHECK_EQ(count, 3);
    }

    SUBCASE("pmr::fast_map alias") {
        std::pmr::monotonic_buffer_resource resource;
        pmr::fast_map<int, std::string> map(&resource);

        map[1] = "one";
        map[2] = "two";

        CHECK_EQ(map.size(), 2);
        CHECK_EQ(map[1], "one");
    }
}

// ============================================================================
// Tests for string_view keys
// This tests the scenario that crashes in CI with -O1 optimization without -march=native
// The crash occurs in dense_map<string_view>::find() during aggregation operations
// ============================================================================

TEST_CASE("dense_map::string_view_keys") {
    SUBCASE("basic insert and find") {
        // Create stable storage for strings
        std::vector<std::string> strings = {"hello", "world", "test", "foo", "bar"};
        dense_map<std::string_view, int> map;

        for (size_t i = 0; i < strings.size(); ++i) {
            map[strings[i]] = static_cast<int>(i);
        }

        CHECK_EQ(map.size(), 5);

        for (size_t i = 0; i < strings.size(); ++i) {
            auto it = map.find(strings[i]);
            REQUIRE(it != map.end());
            CHECK_EQ(it->second, static_cast<int>(i));
        }
    }

    SUBCASE("heterogeneous lookup with string_view") {
        std::vector<std::string> strings;
        dense_map<std::string_view, int> map;
        // The map keys are string_views into these strings, so the strings must not move. A
        // vector<string> reallocates on growth and the keys here are short enough for SSO, so the
        // character data lives inside the string object and moves with it -- every view handed to
        // the map before a reallocation is left dangling. Reserve up front so the storage is stable,
        // which is what the first subcase in this test case already does deliberately.
        strings.reserve(1000);

        // Insert many strings to trigger rehashing
        for (int i = 0; i < 1000; ++i) {
            strings.push_back("key_" + std::to_string(i));
            map[strings.back()] = i;
        }

        CHECK_EQ(map.size(), 1000);

        // Lookup using const char*
        for (int i = 0; i < 1000; ++i) {
            std::string key = "key_" + std::to_string(i);
            auto it = map.find(std::string_view(key));
            REQUIRE(it != map.end());
            CHECK_EQ(it->second, i);
        }
    }

    SUBCASE("insert from span of string_view") {
        // This mimics the pattern in aggregate_on_datum.cc Parallel<>::distinct()
        std::vector<std::string> source_strings = {"alpha", "beta", "gamma", "delta", "epsilon"};
        std::vector<std::string_view> views;
        for (const auto& s : source_strings) {
            views.push_back(s);
        }

        dense_set<std::string_view> set;
        for (const auto& v : views) {
            set.insert(v);
        }

        CHECK_EQ(set.size(), 5);

        // Verify all elements are findable
        for (const auto& v : views) {
            CHECK(set.contains(v));
        }
    }

    SUBCASE("rehash with string_view keys") {
        std::vector<std::string> strings;
        dense_map<std::string_view, int> map;

        // The map keys are string_views into these strings, so the strings must not move. A
        // vector<string> reallocates on growth and the keys here are short enough for SSO, so the
        // character data lives inside the string object and moves with it -- every view handed to
        // the map before a reallocation is left dangling. Reserve up front so the storage is stable,
        // which is what the first subcase in this test case already does deliberately.
        strings.reserve(100);

        // Small initial capacity to force rehashing
        map.reserve(4);

        for (int i = 0; i < 100; ++i) {
            strings.push_back("rehash_test_key_" + std::to_string(i));
            map[strings.back()] = i;

            // Verify all previous insertions are still accessible after potential rehash
            for (int j = 0; j <= i; ++j) {
                auto it = map.find(strings[j]);
                REQUIRE_MESSAGE(it != map.end(), "Key not found after rehash: " << strings[j]);
                CHECK_EQ(it->second, j);
            }
        }
    }

    SUBCASE("erase and find with string_view") {
        std::vector<std::string> strings;
        dense_map<std::string_view, int> map;
        // The map keys are string_views into these strings, so the strings must not move. A
        // vector<string> reallocates on growth and the keys here are short enough for SSO, so the
        // character data lives inside the string object and moves with it -- every view handed to
        // the map before a reallocation is left dangling. Reserve up front so the storage is stable,
        // which is what the first subcase in this test case already does deliberately.
        strings.reserve(50);

        for (int i = 0; i < 50; ++i) {
            strings.push_back("erase_key_" + std::to_string(i));
            map[strings.back()] = i;
        }

        // Erase even-indexed keys
        for (int i = 0; i < 50; i += 2) {
            map.erase(strings[i]);
        }

        CHECK_EQ(map.size(), 25);

        // Verify odd-indexed keys are still accessible
        for (int i = 1; i < 50; i += 2) {
            auto it = map.find(strings[i]);
            REQUIRE(it != map.end());
            CHECK_EQ(it->second, i);
        }

        // Verify even-indexed keys are gone
        for (int i = 0; i < 50; i += 2) {
            CHECK_FALSE(map.contains(strings[i]));
        }
    }

    SUBCASE("constructor from range of string_view") {
        std::vector<std::string> source = {"a", "b", "c", "d", "e"};
        std::vector<std::string_view> views;
        for (const auto& s : source) {
            views.push_back(s);
        }

        // Construct set directly from range - this is the pattern used in aggregation
        dense_set<std::string_view> set(views.begin(), views.end());

        CHECK_EQ(set.size(), 5);
        for (const auto& v : views) {
            CHECK(set.contains(v));
        }
    }

    SUBCASE("stress test with many string_view keys") {
        std::vector<std::string> strings;
        dense_map<std::string_view, int> map;
        // The map keys are string_views into these strings, so the strings must not move. A
        // vector<string> reallocates on growth and the keys here are short enough for SSO, so the
        // character data lives inside the string object and moves with it -- every view handed to
        // the map before a reallocation is left dangling. Reserve up front so the storage is stable,
        // which is what the first subcase in this test case already does deliberately.
        strings.reserve(5000);
        std::mt19937 gen(12345);

        auto random_string = [&gen](int len) {
            static const char chars[] = "abcdefghijklmnopqrstuvwxyz";
            std::string s;
            for (int i = 0; i < len; i++) s += chars[gen() % 26];
            return s;
        };

        // Insert many random strings
        for (int i = 0; i < 5000; ++i) {
            strings.push_back(random_string(16));
            map[strings.back()] = i;
        }

        // Note: duplicates may reduce actual size
        REQUIRE(map.size() <= 5000);

        // Verify all inserted strings can be found
        for (size_t i = 0; i < strings.size(); ++i) {
            CHECK(map.contains(strings[i]));
        }

        // Mixed find and erase operations
        for (int i = 0; i < 1000; ++i) {
            int idx = gen() % strings.size();
            if (map.contains(strings[idx])) {
                map.erase(strings[idx]);
            }
        }

        // Verify consistency
        for (const auto& s : strings) {
            auto it = map.find(s);
            if (it != map.end()) {
                // If found, value should be valid
                CHECK_GE(it->second, 0);
            }
        }
    }

    SUBCASE("concurrent-like access pattern with string_view") {
        // Simulates the pattern in Parallel<>::distinct() where multiple "threads"
        // access the same data structure (single-threaded simulation)
        std::vector<std::string> source_strings;
        for (int i = 0; i < 1000; ++i) {
            source_strings.push_back("concurrent_key_" + std::to_string(i));
        }

        std::vector<std::string_view> views;
        for (const auto& s : source_strings) {
            views.push_back(s);
        }

        // Simulate multiple "producers" inserting into the same set
        dense_set<std::string_view> set;
        size_t chunk_size = views.size() / 4;

        for (size_t producer = 0; producer < 4; ++producer) {
            size_t start = producer * chunk_size;
            size_t end = (producer == 3) ? views.size() : start + chunk_size;

            for (size_t i = start; i < end; ++i) {
                set.insert(views[i]);
            }

            // Verify all inserted elements are findable
            for (size_t i = 0; i <= end - 1 && i < views.size(); ++i) {
                CHECK_MESSAGE(set.contains(views[i]), "Missing key at index " << i);
            }
        }

        CHECK_EQ(set.size(), views.size());
    }
}

// ============================================================================
// Regression tests for specific bugs
// ============================================================================

TEST_CASE("dense_map::regression::growth_left_on_erase") {
    // Regression test for bug: growth_left_ was incorrectly incremented when
    // marking a slot as kDeleted during erase. This allowed size_ to exceed
    // capacity_ after repeated insert/erase cycles.
    //
    // The bug manifested as: after many insert/erase cycles, the map would
    // have size_ > capacity_, causing erase_value_at() to fail to find the
    // slot for the last element (assertion "slot not found for last_idx").

    SUBCASE("repeated insert-erase cycles maintain size <= capacity") {
        dense_map<int, int> map;

        // Perform many insert-erase cycles
        for (int cycle = 0; cycle < 100; ++cycle) {
            // Insert elements
            for (int i = 0; i < 20; ++i) {
                map[cycle * 100 + i] = i;
            }

            // Erase half of them
            for (int i = 0; i < 10; ++i) {
                map.erase(cycle * 100 + i);
            }

            // Verify size is reasonable
            CHECK_LE(map.size(), map.capacity());
        }
    }

    SUBCASE("insert-erase-insert pattern with same keys") {
        dense_map<int, int> map;

        for (int round = 0; round < 50; ++round) {
            // Insert 10 elements
            for (int i = 0; i < 10; ++i) {
                map[i] = round * 10 + i;
            }
            CHECK_EQ(map.size(), 10);

            // Erase all elements
            for (int i = 0; i < 10; ++i) {
                map.erase(i);
            }
            CHECK_EQ(map.size(), 0);
        }

        // Map should still be usable
        map[999] = 999;
        CHECK_EQ(map.at(999), 999);
    }

    SUBCASE("high churn with string keys") {
        // String keys use indirect storage mode where the bug was present
        dense_map<std::string, int> map;
        std::vector<std::string> keys;

        // Create keys
        for (int i = 0; i < 100; ++i) {
            keys.push_back("key_" + std::to_string(i));
        }

        // High churn: repeatedly insert and erase
        for (int cycle = 0; cycle < 20; ++cycle) {
            // Insert all keys
            for (int i = 0; i < 100; ++i) {
                map[keys[i]] = cycle * 100 + i;
            }
            CHECK_EQ(map.size(), 100);

            // Erase all keys
            for (int i = 0; i < 100; ++i) {
                map.erase(keys[i]);
            }
            CHECK_EQ(map.size(), 0);
            CHECK_LE(map.size(), map.capacity());
        }
    }

    SUBCASE("alternating insert-erase on same key") {
        dense_map<std::string, int> map;
        std::string key = "alternating_key";

        for (int i = 0; i < 1000; ++i) {
            map[key] = i;
            CHECK_EQ(map.size(), 1);
            map.erase(key);
            CHECK_EQ(map.size(), 0);
        }

        // Map should still work
        map["final_key"] = 42;
        CHECK_EQ(map.at("final_key"), 42);
    }
}

TEST_CASE("dense_map::regression::match_empty_vs_deleted") {
    // Regression test for bug: match_empty() SIMD function was matching both
    // kEmpty (0x80) and kDeleted (0xFE) because it only checked for the high
    // bit being set (< 0 in signed comparison).
    //
    // The fix changed match_empty() to use exact comparison with kEmpty.
    //
    // This bug caused erase_value_at() to incorrectly detect "empty" slots
    // when they were actually deleted, breaking the probe chain search.

    SUBCASE("find after erase with probe chain") {
        // Use a bad hash to force collisions and long probe chains
        struct BadHash {
            size_t operator()(int key) const noexcept {
                return static_cast<size_t>(key % 4);  // Only 4 hash buckets
            }
        };

        dense_map<int, int, BadHash> map;

        // Insert keys that will all collide
        for (int i = 0; i < 20; ++i) {
            map[i] = i * 10;
        }

        // Erase some keys (creates deleted slots)
        for (int i = 0; i < 10; ++i) {
            map.erase(i);
        }

        // Verify remaining keys are still findable
        // This would fail if match_empty() matched deleted slots
        for (int i = 10; i < 20; ++i) {
            auto it = map.find(i);
            REQUIRE_MESSAGE(it != map.end(), "Key " << i << " not found after erasing earlier keys");
            CHECK_EQ(it->second, i * 10);
        }
    }

    SUBCASE("find traverses deleted slots correctly") {
        dense_map<std::string, int> map;

        // Insert keys that may have overlapping probe sequences
        std::vector<std::string> keys;
        for (int i = 0; i < 100; ++i) {
            keys.push_back("probe_chain_key_" + std::to_string(i));
            map[keys.back()] = i;
        }

        // Erase every other key to create deleted slots in probe chains
        for (int i = 0; i < 100; i += 2) {
            map.erase(keys[i]);
        }

        // Verify remaining keys are still findable
        for (int i = 1; i < 100; i += 2) {
            auto it = map.find(keys[i]);
            REQUIRE_MESSAGE(it != map.end(), "Key at index " << i << " not found");
            CHECK_EQ(it->second, i);
        }
    }

    SUBCASE("insert into deleted slots works correctly") {
        dense_map<int, int> map;

        // Insert and erase to create deleted slots
        for (int i = 0; i < 50; ++i) {
            map[i] = i;
        }
        for (int i = 0; i < 50; ++i) {
            map.erase(i);
        }

        // Insert new keys - should reuse deleted slots
        for (int i = 100; i < 150; ++i) {
            map[i] = i;
        }

        CHECK_EQ(map.size(), 50);

        // Verify all new keys are findable
        for (int i = 100; i < 150; ++i) {
            CHECK_EQ(map.at(i), i);
        }
    }
}

TEST_CASE("dense_map::regression::erase_swap_and_pop") {
    // Test the swap-and-pop erase strategy used in indirect storage mode.
    // When erasing an element that is not the last in the values array,
    // we swap it with the last element and update the slot mapping.

    SUBCASE("erase middle element updates slot correctly") {
        dense_map<std::string, int> map;

        // Insert several elements
        map["first"] = 1;
        map["middle"] = 2;
        map["last"] = 3;

        // Erase middle element
        map.erase("middle");

        // Both remaining elements should be findable
        CHECK_EQ(map.at("first"), 1);
        CHECK_EQ(map.at("last"), 3);
        CHECK_FALSE(map.contains("middle"));
    }

    SUBCASE("erase in reverse order") {
        dense_map<std::string, int> map;

        for (int i = 0; i < 100; ++i) {
            map["key_" + std::to_string(i)] = i;
        }

        // Erase in reverse order
        for (int i = 99; i >= 0; --i) {
            map.erase("key_" + std::to_string(i));
            CHECK_EQ(map.size(), static_cast<size_t>(i));

            // Verify remaining keys
            for (int j = 0; j < i; ++j) {
                CHECK_MESSAGE(map.contains("key_" + std::to_string(j)),
                              "Key " << j << " missing after erasing " << i);
            }
        }
    }

    SUBCASE("erase random order with verification") {
        std::mt19937 gen(42);
        dense_map<int, int> map;
        std::vector<int> keys;

        // Insert 100 elements
        for (int i = 0; i < 100; ++i) {
            keys.push_back(i);
            map[i] = i * 10;
        }

        // Shuffle keys
        std::shuffle(keys.begin(), keys.end(), gen);

        // Erase one by one in random order
        for (size_t i = 0; i < keys.size(); ++i) {
            int key_to_erase = keys[i];
            map.erase(key_to_erase);

            // Verify all remaining keys
            for (size_t j = i + 1; j < keys.size(); ++j) {
                int remaining_key = keys[j];
                auto it = map.find(remaining_key);
                REQUIRE_MESSAGE(it != map.end(),
                                "Key " << remaining_key << " not found after erasing " << key_to_erase);
                CHECK_EQ(it->second, remaining_key * 10);
            }
        }

        CHECK(map.empty());
    }
}

}  // namespace stdb::container

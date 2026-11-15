#include "container/small_vectra.hpp"

#include <doctest/doctest.h>
#include <memory>
#include <string>
#include <vector>

template <typename T>
static void CheckAliasedInsert(const T& first, const T& second) {
    for (std::size_t initial : {1, 2, 4, 8}) {
        for (bool reserve : {false, true}) {
            for (std::size_t source = 0; source < initial; ++source) {
                for (std::size_t pos = 0; pos <= initial; ++pos) {
                    stdb::container::small_vectra<T, 4> values(initial, first);
                    values[source] = second;
                    std::vector<T> expected(values.begin(), values.end());
                    const T saved = expected[source];
                    expected.insert(expected.begin() + pos, saved);
                    if (reserve) values.reserve(32);
                    auto result = values.insert(values.begin() + pos, values[source]);
                    REQUIRE(result == values.begin() + pos);
                    REQUIRE(values.size() == expected.size());
                    for (std::size_t i = 0; i < expected.size(); ++i) CHECK(values[i] == expected[i]);
                }
            }
        }
    }
}

TEST_CASE("small_vectra::insert preserves aliased lvalues") {
    CheckAliasedInsert(11, 42);
    CheckAliasedInsert(std::string(100, 'a'), std::string(100, 'b'));
}

TEST_CASE("small_vectra::insert preserves aliased move-only rvalues") {
    for (std::size_t initial : {1, 2, 4, 8}) {
        for (bool reserve : {false, true}) {
            for (std::size_t source = 0; source < initial; ++source) {
                for (std::size_t pos = 0; pos <= initial; ++pos) {
                    stdb::container::small_vectra<std::unique_ptr<int>, 4> values;
                    for (std::size_t i = 0; i < initial; ++i) values.emplace_back(new int(i + 10));
                    if (reserve) values.reserve(32);
                    auto result = values.insert(values.begin() + pos, std::move(values[source]));
                    REQUIRE(result == values.begin() + pos);
                    REQUIRE(values.size() == initial + 1);
                    REQUIRE(values[pos] != nullptr);
                    CHECK(*values[pos] == source + 10);
                    for (std::size_t i = 0; i < initial; ++i) {
                        const auto shifted = i + (i >= pos);
                        if (i == source) CHECK(values[shifted] == nullptr);
                        else {
                            REQUIRE(values[shifted] != nullptr);
                            CHECK(*values[shifted] == i + 10);
                        }
                    }
                }
            }
        }
    }
}

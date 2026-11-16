#include "container/small_vectra.hpp"

#include <doctest/doctest.h>
#include <string>

template <typename T>
static void CheckAliasedAssign(const T& first, const T& fill) {
    for (std::size_t initial : {1, 4, 8}) {
        for (bool reserve : {false, true}) {
            for (std::size_t source : {std::size_t(0), initial - 1}) {
                for (std::size_t count : {0, 1, 4, 8, 100}) {
                    stdb::container::small_vectra<T, 4> values(initial, first);
                    values[source] = fill;
                    if (reserve) values.reserve(128);
                    values.assign(count, values[source]);
                    REQUIRE(values.size() == count);
                    for (const auto& value : values) CHECK(value == fill);
                }
            }
        }
    }
}

TEST_CASE("small_vectra::assign preserves aliased fill values") {
    CheckAliasedAssign(11, 42);
    CheckAliasedAssign(std::string(100, 'a'), std::string(100, 'x'));
}

TEST_CASE("small_vectra::assign accepts external fill values") {
    const std::string fill(100, 'x');
    for (std::size_t count : {0, 1, 4, 100}) {
        stdb::container::small_vectra<std::string, 4> values(2, "old");
        values.assign(count, fill);
        REQUIRE(values.size() == count);
        for (const auto& value : values) CHECK(value == fill);
        CHECK(fill == std::string(100, 'x'));
    }
}

#include "container/small_vectra.hpp"

#include <doctest/doctest.h>
#include <string>

TEST_CASE("small_vectra::resize preserves aliased fill values") {
    for (std::size_t initial : {1, 4, 8}) {
        for (bool reserve : {false, true}) {
            for (std::size_t index : {std::size_t(0), initial - 1}) {
                stdb::container::small_vectra<int, 4> ints(initial, 42);
                if (reserve) ints.reserve(100);
                ints.resize(100, ints[index]);
                REQUIRE(ints.size() == 100);
                for (int value : ints) CHECK(value == 42);

                // Moving destroys the old string's value even for inline storage.
                const std::string expected(100, 'x');
                stdb::container::small_vectra<std::string, 4> strings(initial, expected);
                if (reserve) strings.reserve(100);
                strings.resize(100, strings[index]);
                REQUIRE(strings.size() == 100);
                for (const auto& value : strings) CHECK(value == expected);
                strings.resize(100, strings[0]);
                strings.resize(2, strings.back());
                REQUIRE(strings.size() == 2);
                CHECK(strings[0] == expected);
                CHECK(strings[1] == expected);
            }
        }
    }
}

TEST_CASE("small_vectra::resize accepts external fill values") {
    stdb::container::small_vectra<std::string, 4> strings;
    const std::string value(100, 'y');
    strings.resize(100, value);
    REQUIRE(strings.size() == 100);
    for (const auto& item : strings) CHECK(item == value);
    CHECK(value == std::string(100, 'y'));
}

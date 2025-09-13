
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

#define ANKERL_NANOBENCH_IMPLEMENT
#include <iostream>
#include <vector>

#include "../nanobench/src/include/nanobench.h"
#include "container/vectra.hpp"

namespace stdb::container {

// NOLINTBEGIN
constexpr size_t times = 1024 * 64;

template <typename T>
void push_back() {
    std::vector<T> vec;
    vec.reserve(times);
    for (size_t i = 0; i < times; ++i) {
        vec.push_back(i);
    }
    ankerl::nanobench::doNotOptimizeAway(vec);
}

template <typename T>
void push_back_vectra() {
    stdb::container::vectra<T> vec;
    vec.reserve(times);
    for (size_t i = 0; i < times; ++i) {
        vec.push_back(i);
    }
    ankerl::nanobench::doNotOptimizeAway(vec);
}

template <typename T>
void init_vector() {
    std::vector<T> vec(times, std::numeric_limits<T>::max());
    ankerl::nanobench::doNotOptimizeAway(vec);
}

template <typename T>
void init_vectra() {
    stdb::container::vectra<T> vec(times, std::numeric_limits<T>::max());
    ankerl::nanobench::doNotOptimizeAway(vec);
}

template <typename T>
void assign() {
    std::vector<T> vec;
    vec.assign(times, std::numeric_limits<T>::max());
    ankerl::nanobench::doNotOptimizeAway(vec);
}

template <typename T>
void assign_vectra() {
    stdb::container::vectra<T> vec;
    vec.assign(times, std::numeric_limits<T>::max());
    ankerl::nanobench::doNotOptimizeAway(vec);
}

template <typename T>
void push_back_unsafe() {
    vectra<T> vec;
    vec.reserve(times);
    for (size_t i = 0; i < times; ++i) {
        vec.template push_back<Safety::Unsafe>(i);
    }
    ankerl::nanobench::doNotOptimizeAway(vec);
}

template <typename T>
void for_loop(const std::vector<T>& vec) {
    T sum = 0;
    for (T i : vec) {
        sum += i;
    }
    ankerl::nanobench::doNotOptimizeAway(sum);
}

template <typename T>
void for_loop(const stdb::container::vectra<T>& vec) {
    T sum = 0;
    for (T i : vec) {
        sum += i;
    }
    ankerl::nanobench::doNotOptimizeAway(sum);
}

class just_move
{
   public:
    int value;
    void* buf = nullptr;
    just_move(int v) : value(v), buf(malloc(1024)) { memset(buf, value, 1024); }
    just_move(just_move&& other) noexcept : value(other.value), buf(other.buf) {
        other.buf = nullptr;
        value = 0;
    }
    just_move& operator=(just_move&& other) noexcept {
        buf = std::exchange(other.buf, nullptr);
        value = std::exchange(other.value, 0);
        return *this;
    }
    just_move(const just_move&) = delete;
    ~just_move() { free(buf); }
};

class just_copy
{
   public:
    int value;
    void* buf = nullptr;
    just_copy(int v) : value(v), buf(malloc(1024)) { memset(buf, value, 1024); }
    just_copy(const just_copy& other) noexcept : value(other.value), buf(malloc(1024)) { memcpy(buf, other.buf, 1024); }
    just_copy& operator=(const just_copy& other) noexcept {
        value = other.value;
        memcpy(buf, other.buf, 1024);
        return *this;
    }
    // Provide move constructor that behaves like copy for libc++ compatibility
    just_copy(just_copy&& other) noexcept : value(other.value), buf(malloc(1024)) { memcpy(buf, other.buf, 1024); }
    just_copy& operator=(just_copy&&) = delete;
    ~just_copy() { free(buf); }
};

struct trivially_copyable
{
    int x;
    double y;
    int z;
    void* ptr;
};

struct non_trivially_copyable
{
    int x;
    double y;
    int z;
    void* ptr;
    ~non_trivially_copyable() {}
};

static_assert(IsRelocatable<trivially_copyable>, "trivially_copyable is not relocatable");
static_assert(!IsRelocatable<non_trivially_copyable>, "non_trivially_copyable is relocatable");

template <typename T>
auto generate_t() -> T {
    if constexpr (std::is_pointer_v<T>) {
        return T(-1);
    } else {
        return T{1};
    }
}

auto filler(size_t* buffer) -> size_t {
    if (buffer) {
        for (size_t i = 0; i < times; ++i) {
            buffer[i] = i;
        }
    }
    return times;
}

int main() {
    ankerl::nanobench::Bench bench;
    bench.title("vectra vs std::vector Benchmarks").unit("op").warmup(100).epochs(1000);

    // === PUSH_BACK COMPARISON ===
    std::cout << "\n=== push_back() Performance ===\n";

    // size_t comparison
    bench.run("push_back std::vector<size_t>", []() { push_back<size_t>(); });
    bench.run("push_back vectra<size_t>", []() { push_back_vectra<size_t>(); });
    bench.run("push_back vectra<size_t> unsafe", []() { push_back_unsafe<size_t>(); });

    // int32_t comparison
    bench.run("push_back std::vector<int32_t>", []() { push_back<int32_t>(); });
    bench.run("push_back vectra<int32_t>", []() { push_back_vectra<int32_t>(); });
    bench.run("push_back vectra<int32_t> unsafe", []() { push_back_unsafe<int32_t>(); });

    // int16_t comparison
    bench.run("push_back std::vector<int16_t>", []() { push_back<int16_t>(); });
    bench.run("push_back vectra<int16_t>", []() { push_back_vectra<int16_t>(); });
    bench.run("push_back vectra<int16_t> unsafe", []() { push_back_unsafe<int16_t>(); });

    // int8_t comparison
    bench.run("push_back std::vector<int8_t>", []() { push_back<int8_t>(); });
    bench.run("push_back vectra<int8_t>", []() { push_back_vectra<int8_t>(); });
    bench.run("push_back vectra<int8_t> unsafe", []() { push_back_unsafe<int8_t>(); });

    // === INITIALIZATION COMPARISON ===
    std::cout << "\n=== Constructor Initialization Performance ===\n";

    // int64_t comparison
    bench.run("init std::vector<int64_t>", []() { init_vector<int64_t>(); });
    bench.run("init vectra<int64_t>", []() { init_vectra<int64_t>(); });

    // int32_t comparison
    bench.run("init std::vector<int32_t>", []() { init_vector<int32_t>(); });
    bench.run("init vectra<int32_t>", []() { init_vectra<int32_t>(); });

    // int16_t comparison
    bench.run("init std::vector<int16_t>", []() { init_vector<int16_t>(); });
    bench.run("init vectra<int16_t>", []() { init_vectra<int16_t>(); });

    // int8_t comparison
    bench.run("init std::vector<int8_t>", []() { init_vector<int8_t>(); });
    bench.run("init vectra<int8_t>", []() { init_vectra<int8_t>(); });

    // === ASSIGN COMPARISON ===
    std::cout << "\n=== assign() Performance ===\n";

    // int64_t comparison
    bench.run("assign std::vector<int64_t>", []() { assign<int64_t>(); });
    bench.run("assign vectra<int64_t>", []() { assign_vectra<int64_t>(); });

    // int32_t comparison
    bench.run("assign std::vector<int32_t>", []() { assign<int32_t>(); });
    bench.run("assign vectra<int32_t>", []() { assign_vectra<int32_t>(); });

    // int16_t comparison
    bench.run("assign std::vector<int16_t>", []() { assign<int16_t>(); });
    bench.run("assign vectra<int16_t>", []() { assign_vectra<int16_t>(); });

    // int8_t comparison
    bench.run("assign std::vector<int8_t>", []() { assign<int8_t>(); });
    bench.run("assign vectra<int8_t>", []() { assign_vectra<int8_t>(); });

    // === ITERATION COMPARISON ===
    std::cout << "\n=== Range-based for loop Performance ===\n";

    // int64_t iteration comparison
    bench.run("for_loop std::vector<int64_t>", []() {
        std::vector<int64_t> data;
        data.reserve(times);
        for (size_t i = 0; i < times; ++i) {
            data.push_back((int64_t)i);
        }
        for_loop(data);
    });
    bench.run("for_loop vectra<int64_t>", []() {
        stdb::container::vectra<int64_t> data;
        data.reserve(times);
        for (size_t i = 0; i < times; ++i) {
            data.push_back((int64_t)i);
        }
        for_loop(data);
    });

    // int32_t iteration comparison
    bench.run("for_loop std::vector<int32_t>", []() {
        std::vector<int32_t> data;
        data.reserve(times);
        for (size_t i = 0; i < times; ++i) {
            data.push_back((int32_t)i);
        }
        for_loop(data);
    });
    bench.run("for_loop vectra<int32_t>", []() {
        stdb::container::vectra<int32_t> data;
        data.reserve(times);
        for (size_t i = 0; i < times; ++i) {
            data.push_back((int32_t)i);
        }
        for_loop(data);
    });

    // === MOVE SEMANTICS COMPARISON ===
    std::cout << "\n=== Move-only Type Performance ===\n";

    bench.run("push_back std::vector<just_move>", []() {
        std::vector<just_move> vec;
        vec.reserve(times);
        for (size_t i = 0; i < times; ++i) {
            just_move m(i);
            vec.push_back(std::move(m));
        }
        ankerl::nanobench::doNotOptimizeAway(vec);
    });
    bench.run("push_back vectra<just_move>", []() {
        stdb::container::vectra<just_move> vec;
        vec.reserve(times);
        for (size_t i = 0; i < times; ++i) {
            just_move m(i);
            vec.push_back(std::move(m));
        }
        ankerl::nanobench::doNotOptimizeAway(vec);
    });
    bench.run("push_back vectra<just_move> unsafe", []() {
        stdb::container::vectra<just_move> vec;
        vec.reserve(times);
        for (size_t i = 0; i < times; ++i) {
            just_move m(i);
            vec.push_back<Safety::Unsafe>(std::move(m));
        }
        ankerl::nanobench::doNotOptimizeAway(vec);
    });

    // === COPY SEMANTICS COMPARISON ===
    std::cout << "\n=== Copy-only Type Performance ===\n";

    bench.run("push_back std::vector<just_copy>", []() {
        std::vector<just_copy> vec;
        vec.reserve(times);
        for (size_t i = 0; i < times; ++i) {
            stdb::container::just_copy m(i);
            vec.push_back(m);
        }
        ankerl::nanobench::doNotOptimizeAway(vec);
    });
    bench.run("push_back vectra<just_copy>", []() {
        stdb::container::vectra<just_copy> vec;
        vec.reserve(times);
        for (size_t i = 0; i < times; ++i) {
            stdb::container::just_copy m(i);
            vec.push_back(m);
        }
        ankerl::nanobench::doNotOptimizeAway(vec);
    });
    bench.run("push_back vectra<just_copy> unsafe", []() {
        stdb::container::vectra<just_copy> vec;
        vec.reserve(times);
        for (size_t i = 0; i < times; ++i) {
            stdb::container::just_copy m(i);
            vec.push_back<Safety::Unsafe>(m);
        }
        ankerl::nanobench::doNotOptimizeAway(vec);
    });

    // === RESERVE COMPARISON ===
    std::cout << "\n=== reserve() Performance ===\n";

    bench.run("reserve std::vector<trivially_copyable>", []() {
        std::vector<trivially_copyable> vec;
        vec.reserve(times * 2);
        vec.push_back(trivially_copyable());
        ankerl::nanobench::doNotOptimizeAway(vec);
    });
    bench.run("reserve vectra<trivially_copyable>", []() {
        stdb::container::vectra<trivially_copyable> vec;
        vec.reserve(times * 2);
        vec.push_back(trivially_copyable());
        ankerl::nanobench::doNotOptimizeAway(vec);
    });

    bench.run("reserve std::vector<non_trivially_copyable>", []() {
        std::vector<non_trivially_copyable> vec;
        vec.reserve(times * 2);
        vec.push_back(non_trivially_copyable());
        ankerl::nanobench::doNotOptimizeAway(vec);
    });
    bench.run("reserve vectra<non_trivially_copyable>", []() {
        stdb::container::vectra<non_trivially_copyable> vec;
        vec.reserve(times * 2);
        vec.push_back(non_trivially_copyable());
        ankerl::nanobench::doNotOptimizeAway(vec);
    });

    // === STACK-LIKE OPERATIONS COMPARISON ===
    std::cout << "\n=== Stack-like Operations Performance ===\n";

    bench.run("stack_like std::vector<char*>", []() {
        std::vector<char*> vec;
        vec.reserve(16);
        vec.push_back(nullptr);
        if (vec.back() == nullptr) {
            vec.pop_back();
        }
        for (uint64_t i = 0; i < 8; ++i) {
            vec.push_back(generate_t<char*>());
        }
        while (not vec.empty()) {
            vec.pop_back();
        }
        ankerl::nanobench::doNotOptimizeAway(vec);
    });
    bench.run("stack_like vectra<char*>", []() {
        vectra<char*> vec;
        vec.reserve(16);
        vec.push_back(nullptr);
        if (vec.back() == nullptr) {
            vec.pop_back();
        }
        for (uint64_t i = 0; i < 8; ++i) {
            vec.push_back(generate_t<char*>());
        }
        while (not vec.empty()) {
            vec.pop_back();
        }
        ankerl::nanobench::doNotOptimizeAway(vec);
    });

    // === VECTRA UNIQUE FEATURES ===
    std::cout << "\n=== vectra Unique Features ===\n";

    bench.run("vectra resize unsafe", []() {
        stdb::container::vectra<size_t> vec;
        vec.resize<Safety::Unsafe>(times);
        for (size_t i = 0; i < times; ++i) {
            vec[i] = i;
        }
        ankerl::nanobench::doNotOptimizeAway(vec);
    });

    bench.run("vectra resize safe", []() {
        stdb::container::vectra<size_t> vec;
        vec.resize<Safety::Safe>(times);
        for (size_t i = 0; i < times; ++i) {
            vec[i] = i;
        }
        ankerl::nanobench::doNotOptimizeAway(vec);
    });

    bench.run("vectra get_writebuffer", []() {
        stdb::container::vectra<size_t> vec;
        vec.reserve(times);
        auto buffer = vec.get_writebuffer(times);
        for (size_t i = 0; i < times; ++i) {
            buffer[i] = i;
        }
        ankerl::nanobench::doNotOptimizeAway(vec);
    });

    bench.run("vectra get_writebuffer unsafe", []() {
        stdb::container::vectra<size_t> vec;
        vec.reserve(times);
        auto buffer = vec.get_writebuffer<Safety::Unsafe>(times);
        for (size_t i = 0; i < times; ++i) {
            buffer[i] = i;
        }
        ankerl::nanobench::doNotOptimizeAway(vec);
    });

    bench.run("vectra fill", []() {
        stdb::container::vectra<size_t> vec;
        vec.reserve(times);
        vec.fill(&filler);
        ankerl::nanobench::doNotOptimizeAway(vec);
    });

    bench.run("vectra fill unsafe", []() {
        stdb::container::vectra<size_t> vec;
        vec.reserve(times);
        vec.fill<Safety::Unsafe>(&filler);
        ankerl::nanobench::doNotOptimizeAway(vec);
    });

    return 0;
}

// NOLINTEND

}  // namespace stdb::container

int main() { return stdb::container::main(); }
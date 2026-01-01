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
#include "../nanobench/src/include/nanobench.h"

#include <algorithm>
#include <deque>
#include <iostream>
#include <random>
#include <vector>

#include "container/small_vectra.hpp"
#include "container/devectra.hpp"
#include "container/ring_buffer.hpp"

using namespace stdb::container;

// =============================================================================
// small_vectra benchmarks
// =============================================================================

template <size_t N>
void run_small_vectra_push_back_benchmark() {
    ankerl::nanobench::Bench bench;
    bench.warmup(3).epochs(5).relative(true);

    std::cout << "\n=== small_vectra vs std::vector push_back: " << N << " elements ===\n";

    // std::vector baseline
    bench.run("std::vector", [&]() {
        std::vector<int> vec;
        for (size_t i = 0; i < N; ++i) {
            vec.push_back(static_cast<int>(i));
        }
        ankerl::nanobench::doNotOptimizeAway(vec.data());
    });

    // small_vectra
    bench.run("small_vectra", [&]() {
        small_vectra<int> vec;
        for (size_t i = 0; i < N; ++i) {
            vec.push_back(static_cast<int>(i));
        }
        ankerl::nanobench::doNotOptimizeAway(vec.data());
    });
}

template <size_t N>
void run_small_vectra_reserved_benchmark() {
    ankerl::nanobench::Bench bench;
    bench.warmup(3).epochs(5).relative(true);

    std::cout << "\n=== small_vectra vs std::vector push_back (reserved): " << N << " elements ===\n";

    // std::vector baseline
    bench.run("std::vector", [&]() {
        std::vector<int> vec;
        vec.reserve(N);
        for (size_t i = 0; i < N; ++i) {
            vec.push_back(static_cast<int>(i));
        }
        ankerl::nanobench::doNotOptimizeAway(vec.data());
    });

    // small_vectra
    bench.run("small_vectra", [&]() {
        small_vectra<int> vec;
        vec.reserve(N);
        for (size_t i = 0; i < N; ++i) {
            vec.push_back(static_cast<int>(i));
        }
        ankerl::nanobench::doNotOptimizeAway(vec.data());
    });
}

// =============================================================================
// devectra benchmarks
// =============================================================================

template <size_t N>
void run_devectra_push_back_benchmark() {
    ankerl::nanobench::Bench bench;
    bench.warmup(3).epochs(5).relative(true);

    std::cout << "\n=== devectra vs std::deque push_back: " << N << " elements ===\n";

    // std::deque baseline
    bench.run("std::deque", [&]() {
        std::deque<int> dq;
        for (size_t i = 0; i < N; ++i) {
            dq.push_back(static_cast<int>(i));
        }
        ankerl::nanobench::doNotOptimizeAway(&dq);
    });

    // std::vector for comparison
    bench.run("std::vector", [&]() {
        std::vector<int> vec;
        for (size_t i = 0; i < N; ++i) {
            vec.push_back(static_cast<int>(i));
        }
        ankerl::nanobench::doNotOptimizeAway(vec.data());
    });

    // devectra
    bench.run("devectra", [&]() {
        devectra<int> dv;
        for (size_t i = 0; i < N; ++i) {
            dv.push_back(static_cast<int>(i));
        }
        ankerl::nanobench::doNotOptimizeAway(dv.data());
    });
}

template <size_t N>
void run_devectra_push_front_benchmark() {
    ankerl::nanobench::Bench bench;
    bench.warmup(3).epochs(5).relative(true);

    std::cout << "\n=== devectra vs std::deque push_front: " << N << " elements ===\n";

    // std::deque baseline
    bench.run("std::deque", [&]() {
        std::deque<int> dq;
        for (size_t i = 0; i < N; ++i) {
            dq.push_front(static_cast<int>(i));
        }
        ankerl::nanobench::doNotOptimizeAway(&dq);
    });

    // devectra
    bench.run("devectra", [&]() {
        devectra<int> dv;
        for (size_t i = 0; i < N; ++i) {
            dv.push_front(static_cast<int>(i));
        }
        ankerl::nanobench::doNotOptimizeAway(dv.data());
    });
}

template <size_t N>
void run_devectra_alternating_benchmark() {
    ankerl::nanobench::Bench bench;
    bench.warmup(3).epochs(5).relative(true);

    std::cout << "\n=== devectra vs std::deque alternating push_front/push_back: " << N << " elements ===\n";

    // std::deque baseline
    bench.run("std::deque", [&]() {
        std::deque<int> dq;
        for (size_t i = 0; i < N; ++i) {
            if (i % 2 == 0) {
                dq.push_back(static_cast<int>(i));
            } else {
                dq.push_front(static_cast<int>(i));
            }
        }
        ankerl::nanobench::doNotOptimizeAway(&dq);
    });

    // devectra
    bench.run("devectra", [&]() {
        devectra<int> dv;
        for (size_t i = 0; i < N; ++i) {
            if (i % 2 == 0) {
                dv.push_back(static_cast<int>(i));
            } else {
                dv.push_front(static_cast<int>(i));
            }
        }
        ankerl::nanobench::doNotOptimizeAway(dv.data());
    });
}

template <size_t N>
void run_devectra_random_access_benchmark() {
    std::deque<int> dq(N);
    std::vector<int> vec(N);
    devectra<int> dv(N);

    for (size_t i = 0; i < N; ++i) {
        dq[i] = static_cast<int>(i);
        vec[i] = static_cast<int>(i);
        dv[i] = static_cast<int>(i);
    }

    // Generate random indices
    std::vector<size_t> indices(N);
    for (size_t i = 0; i < N; ++i) indices[i] = i;
    std::mt19937 rng(42);
    std::shuffle(indices.begin(), indices.end(), rng);

    ankerl::nanobench::Bench bench;
    bench.warmup(3).epochs(5).relative(true);

    std::cout << "\n=== devectra vs std::deque random access: " << N << " elements ===\n";

    // std::deque
    bench.run("std::deque", [&]() {
        int sum = 0;
        for (size_t i = 0; i < N; ++i) {
            sum += dq[indices[i]];
        }
        ankerl::nanobench::doNotOptimizeAway(sum);
    });

    // std::vector
    bench.run("std::vector", [&]() {
        int sum = 0;
        for (size_t i = 0; i < N; ++i) {
            sum += vec[indices[i]];
        }
        ankerl::nanobench::doNotOptimizeAway(sum);
    });

    // devectra
    bench.run("devectra", [&]() {
        int sum = 0;
        for (size_t i = 0; i < N; ++i) {
            sum += dv[indices[i]];
        }
        ankerl::nanobench::doNotOptimizeAway(sum);
    });
}

// =============================================================================
// ring_buffer benchmarks
// =============================================================================

template <size_t BufSize, size_t N>
void run_ring_buffer_benchmark() {
    ankerl::nanobench::Bench bench;
    bench.warmup(3).epochs(5).relative(true);

    std::cout << "\n=== ring_buffer<int, " << BufSize << "> push_back: " << N << " operations ===\n";

    // Simulate with std::deque (pop_front + push_back when full)
    bench.run("std::deque (simulated)", [&]() {
        std::deque<int> dq;
        for (size_t i = 0; i < N; ++i) {
            if (dq.size() >= BufSize) {
                dq.pop_front();
            }
            dq.push_back(static_cast<int>(i));
        }
        ankerl::nanobench::doNotOptimizeAway(&dq);
    });

    // ring_buffer
    bench.run("ring_buffer", [&]() {
        ring_buffer<int, BufSize> rb;
        for (size_t i = 0; i < N; ++i) {
            rb.push_back(static_cast<int>(i));
        }
        ankerl::nanobench::doNotOptimizeAway(&rb);
    });
}

template <size_t BufSize>
void run_ring_buffer_iteration_benchmark() {
    ring_buffer<int, BufSize> rb;
    std::deque<int> dq;

    // Fill both
    for (size_t i = 0; i < BufSize; ++i) {
        rb.push_back(static_cast<int>(i));
        dq.push_back(static_cast<int>(i));
    }

    ankerl::nanobench::Bench bench;
    bench.warmup(3).epochs(5).relative(true);

    std::cout << "\n=== ring_buffer<int, " << BufSize << "> iteration ===\n";

    // std::deque iteration
    bench.run("std::deque", [&]() {
        int sum = 0;
        for (const auto& v : dq) {
            sum += v;
        }
        ankerl::nanobench::doNotOptimizeAway(sum);
    });

    // ring_buffer iteration
    bench.run("ring_buffer", [&]() {
        int sum = 0;
        for (const auto& v : rb) {
            sum += v;
        }
        ankerl::nanobench::doNotOptimizeAway(sum);
    });
}

// Power-of-2 vs non-power-of-2 comparison
void run_ring_buffer_power_of_2_comparison() {
    constexpr size_t N = 100000;

    ankerl::nanobench::Bench bench;
    bench.warmup(3).epochs(5).relative(true);

    std::cout << "\n=== ring_buffer power-of-2 vs non-power-of-2: " << N << " push_back ===\n";

    // Non-power-of-2 (uses modulo)
    bench.run("ring_buffer<int, 100>", [&]() {
        ring_buffer<int, 100> rb;
        for (size_t i = 0; i < N; ++i) {
            rb.push_back(static_cast<int>(i));
        }
        ankerl::nanobench::doNotOptimizeAway(&rb);
    });

    // Power-of-2 (uses bitwise AND)
    bench.run("ring_buffer<int, 128>", [&]() {
        ring_buffer<int, 128> rb;
        for (size_t i = 0; i < N; ++i) {
            rb.push_back(static_cast<int>(i));
        }
        ankerl::nanobench::doNotOptimizeAway(&rb);
    });

    // Larger non-power-of-2
    bench.run("ring_buffer<int, 1000>", [&]() {
        ring_buffer<int, 1000> rb;
        for (size_t i = 0; i < N; ++i) {
            rb.push_back(static_cast<int>(i));
        }
        ankerl::nanobench::doNotOptimizeAway(&rb);
    });

    // Larger power-of-2
    bench.run("ring_buffer<int, 1024>", [&]() {
        ring_buffer<int, 1024> rb;
        for (size_t i = 0; i < N; ++i) {
            rb.push_back(static_cast<int>(i));
        }
        ankerl::nanobench::doNotOptimizeAway(&rb);
    });
}

// =============================================================================
// Main
// =============================================================================

int main() {
    std::cout << "============================================================\n";
    std::cout << "Container Performance Benchmark\n";
    std::cout << "============================================================\n";
    std::cout << "Comparing: small_vectra, devectra, ring_buffer\n";
    std::cout << "============================================================\n";

    // small_vectra benchmarks
    std::cout << "\n### small_vectra Benchmarks ###\n";
    run_small_vectra_push_back_benchmark<1000>();
    run_small_vectra_push_back_benchmark<10000>();
    run_small_vectra_push_back_benchmark<100000>();
    run_small_vectra_reserved_benchmark<10000>();

    // devectra benchmarks
    std::cout << "\n### devectra Benchmarks ###\n";
    run_devectra_push_back_benchmark<1000>();
    run_devectra_push_back_benchmark<10000>();
    run_devectra_push_front_benchmark<1000>();
    run_devectra_push_front_benchmark<10000>();
    run_devectra_alternating_benchmark<1000>();
    run_devectra_alternating_benchmark<10000>();
    run_devectra_random_access_benchmark<10000>();

    // ring_buffer benchmarks
    std::cout << "\n### ring_buffer Benchmarks ###\n";
    run_ring_buffer_benchmark<64, 10000>();
    run_ring_buffer_benchmark<1024, 100000>();
    run_ring_buffer_iteration_benchmark<1024>();
    run_ring_buffer_power_of_2_comparison();

    std::cout << "\n============================================================\n";
    std::cout << "Benchmark completed.\n";
    std::cout << "============================================================\n";

    return 0;
}

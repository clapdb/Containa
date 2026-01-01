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
#include <string>
#include <vector>

#include "container/small_vectra.hpp"
#include "container/devectra.hpp"
#include "container/ring_buffer.hpp"
#include "container/static_vectra.hpp"

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

// Small size benchmarks - where small_vectra shines (no heap allocation)
template <size_t N, size_t InlineCapacity = 64>
void run_small_vectra_small_size_benchmark() {
    ankerl::nanobench::Bench bench;
    bench.warmup(10).epochs(10).relative(true);

    std::cout << "\n=== small_vectra<int, " << InlineCapacity << "> vs std::vector: " << N << " elements (inline) ===\n";

    // std::vector baseline
    bench.run("std::vector", [&]() {
        std::vector<int> vec;
        for (size_t i = 0; i < N; ++i) {
            vec.push_back(static_cast<int>(i));
        }
        ankerl::nanobench::doNotOptimizeAway(vec.data());
    });

    // std::vector with reserve
    bench.run("std::vector (reserved)", [&]() {
        std::vector<int> vec;
        vec.reserve(N);
        for (size_t i = 0; i < N; ++i) {
            vec.push_back(static_cast<int>(i));
        }
        ankerl::nanobench::doNotOptimizeAway(vec.data());
    });

    // small_vectra (uses inline storage, no heap allocation)
    bench.run("small_vectra (inline)", [&]() {
        small_vectra<int, InlineCapacity> vec;
        for (size_t i = 0; i < N; ++i) {
            vec.push_back(static_cast<int>(i));
        }
        ankerl::nanobench::doNotOptimizeAway(vec.data());
    });
}

// Batch create/destroy - simulates temporary vectors in hot loops
template <size_t N, size_t Iterations = 10000>
void run_small_vectra_batch_benchmark() {
    ankerl::nanobench::Bench bench;
    bench.warmup(5).epochs(5).relative(true);

    std::cout << "\n=== Batch create/destroy " << Iterations << "x vectors of " << N << " elements ===\n";

    bench.run("std::vector", [&]() {
        for (size_t iter = 0; iter < Iterations; ++iter) {
            std::vector<int> vec;
            for (size_t i = 0; i < N; ++i) {
                vec.push_back(static_cast<int>(i));
            }
            ankerl::nanobench::doNotOptimizeAway(vec.data());
        }
    });

    bench.run("std::vector (reserved)", [&]() {
        for (size_t iter = 0; iter < Iterations; ++iter) {
            std::vector<int> vec;
            vec.reserve(N);
            for (size_t i = 0; i < N; ++i) {
                vec.push_back(static_cast<int>(i));
            }
            ankerl::nanobench::doNotOptimizeAway(vec.data());
        }
    });

    bench.run("small_vectra", [&]() {
        for (size_t iter = 0; iter < Iterations; ++iter) {
            small_vectra<int, 64> vec;
            for (size_t i = 0; i < N; ++i) {
                vec.push_back(static_cast<int>(i));
            }
            ankerl::nanobench::doNotOptimizeAway(vec.data());
        }
    });
}

// String small size benchmark
template <size_t N>
void run_small_vectra_small_string_benchmark() {
    ankerl::nanobench::Bench bench;
    bench.warmup(5).epochs(5).relative(true);

    std::cout << "\n=== small_vectra<string> vs std::vector<string>: " << N << " short strings ===\n";

    bench.run("std::vector<string>", [&]() {
        std::vector<std::string> vec;
        for (size_t i = 0; i < N; ++i) {
            vec.push_back("str");  // short string (SSO)
        }
        ankerl::nanobench::doNotOptimizeAway(vec.data());
    });

    bench.run("small_vectra<string>", [&]() {
        small_vectra<std::string, 8> vec;
        for (size_t i = 0; i < N; ++i) {
            vec.push_back("str");
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

template <size_t N>
void run_small_vectra_emplace_back_benchmark() {
    ankerl::nanobench::Bench bench;
    bench.warmup(3).epochs(5).relative(true);

    std::cout << "\n=== small_vectra vs std::vector emplace_back: " << N << " elements ===\n";

    bench.run("std::vector", [&]() {
        std::vector<int> vec;
        for (size_t i = 0; i < N; ++i) {
            vec.emplace_back(static_cast<int>(i));
        }
        ankerl::nanobench::doNotOptimizeAway(vec.data());
    });

    bench.run("small_vectra", [&]() {
        small_vectra<int> vec;
        for (size_t i = 0; i < N; ++i) {
            vec.emplace_back(static_cast<int>(i));
        }
        ankerl::nanobench::doNotOptimizeAway(vec.data());
    });
}

template <size_t N>
void run_small_vectra_iteration_benchmark() {
    std::vector<int> std_vec(N);
    small_vectra<int> sv;
    sv.resize(N);
    for (size_t i = 0; i < N; ++i) {
        std_vec[i] = static_cast<int>(i);
        sv[i] = static_cast<int>(i);
    }

    ankerl::nanobench::Bench bench;
    bench.warmup(3).epochs(5).relative(true);

    std::cout << "\n=== small_vectra vs std::vector iteration: " << N << " elements ===\n";

    bench.run("std::vector", [&]() {
        int sum = 0;
        for (const auto& v : std_vec) {
            sum += v;
        }
        ankerl::nanobench::doNotOptimizeAway(sum);
    });

    bench.run("small_vectra", [&]() {
        int sum = 0;
        for (const auto& v : sv) {
            sum += v;
        }
        ankerl::nanobench::doNotOptimizeAway(sum);
    });
}

template <size_t N>
void run_small_vectra_copy_benchmark() {
    std::vector<int> std_vec(N);
    small_vectra<int> sv;
    sv.resize(N);
    for (size_t i = 0; i < N; ++i) {
        std_vec[i] = static_cast<int>(i);
        sv[i] = static_cast<int>(i);
    }

    ankerl::nanobench::Bench bench;
    bench.warmup(3).epochs(5).relative(true);

    std::cout << "\n=== small_vectra vs std::vector copy: " << N << " elements ===\n";

    bench.run("std::vector", [&]() {
        std::vector<int> copy = std_vec;
        ankerl::nanobench::doNotOptimizeAway(copy.data());
    });

    bench.run("small_vectra", [&]() {
        small_vectra<int> copy = sv;
        ankerl::nanobench::doNotOptimizeAway(copy.data());
    });
}

template <size_t N>
void run_small_vectra_insert_benchmark() {
    ankerl::nanobench::Bench bench;
    bench.warmup(3).epochs(5).relative(true);

    std::cout << "\n=== small_vectra vs std::vector insert at middle: " << N << " inserts ===\n";

    bench.run("std::vector", [&]() {
        std::vector<int> vec;
        vec.reserve(N);
        for (size_t i = 0; i < N; ++i) {
            vec.insert(vec.begin() + static_cast<long>(vec.size() / 2), static_cast<int>(i));
        }
        ankerl::nanobench::doNotOptimizeAway(vec.data());
    });

    bench.run("small_vectra", [&]() {
        small_vectra<int> vec;
        vec.reserve(N);
        for (size_t i = 0; i < N; ++i) {
            vec.insert(vec.begin() + static_cast<long>(vec.size() / 2), static_cast<int>(i));
        }
        ankerl::nanobench::doNotOptimizeAway(vec.data());
    });
}

template <size_t N>
void run_small_vectra_erase_benchmark() {
    ankerl::nanobench::Bench bench;
    bench.warmup(3).epochs(5).relative(true);

    std::cout << "\n=== small_vectra vs std::vector erase from middle: " << N << " erases ===\n";

    bench.run("std::vector", [&]() {
        std::vector<int> vec(N);
        for (size_t i = 0; i < N; ++i) vec[i] = static_cast<int>(i);
        while (!vec.empty()) {
            vec.erase(vec.begin() + static_cast<long>(vec.size() / 2));
        }
        ankerl::nanobench::doNotOptimizeAway(vec.data());
    });

    bench.run("small_vectra", [&]() {
        small_vectra<int> vec;
        vec.resize(N);
        for (size_t i = 0; i < N; ++i) vec[i] = static_cast<int>(i);
        while (!vec.empty()) {
            vec.erase(vec.begin() + static_cast<long>(vec.size() / 2));
        }
        ankerl::nanobench::doNotOptimizeAway(vec.data());
    });
}

template <size_t N>
void run_small_vectra_pop_back_benchmark() {
    ankerl::nanobench::Bench bench;
    bench.warmup(3).epochs(5).relative(true);

    std::cout << "\n=== small_vectra vs std::vector pop_back: " << N << " elements ===\n";

    bench.run("std::vector", [&]() {
        std::vector<int> vec(N);
        while (!vec.empty()) {
            vec.pop_back();
        }
        ankerl::nanobench::doNotOptimizeAway(vec.data());
    });

    bench.run("small_vectra", [&]() {
        small_vectra<int> vec;
        vec.resize(N);
        while (!vec.empty()) {
            vec.pop_back();
        }
        ankerl::nanobench::doNotOptimizeAway(vec.data());
    });
}

template <size_t N>
void run_small_vectra_clear_repopulate_benchmark() {
    ankerl::nanobench::Bench bench;
    bench.warmup(3).epochs(5).relative(true);

    std::cout << "\n=== small_vectra vs std::vector clear+repopulate: " << N << " elements x5 cycles ===\n";

    bench.run("std::vector", [&]() {
        std::vector<int> vec;
        for (int cycle = 0; cycle < 5; ++cycle) {
            for (size_t i = 0; i < N; ++i) {
                vec.push_back(static_cast<int>(i));
            }
            vec.clear();
        }
        ankerl::nanobench::doNotOptimizeAway(vec.data());
    });

    bench.run("small_vectra", [&]() {
        small_vectra<int> vec;
        for (int cycle = 0; cycle < 5; ++cycle) {
            for (size_t i = 0; i < N; ++i) {
                vec.push_back(static_cast<int>(i));
            }
            vec.clear();
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

template <size_t N>
void run_devectra_pop_front_benchmark() {
    ankerl::nanobench::Bench bench;
    bench.warmup(3).epochs(5).relative(true);

    std::cout << "\n=== devectra vs std::deque pop_front: " << N << " elements ===\n";

    bench.run("std::deque", [&]() {
        std::deque<int> dq(N);
        while (!dq.empty()) {
            dq.pop_front();
        }
        ankerl::nanobench::doNotOptimizeAway(&dq);
    });

    bench.run("devectra", [&]() {
        devectra<int> dv(N);
        while (!dv.empty()) {
            dv.pop_front();
        }
        ankerl::nanobench::doNotOptimizeAway(dv.data());
    });
}

template <size_t N>
void run_devectra_pop_back_benchmark() {
    ankerl::nanobench::Bench bench;
    bench.warmup(3).epochs(5).relative(true);

    std::cout << "\n=== devectra vs std::deque pop_back: " << N << " elements ===\n";

    bench.run("std::deque", [&]() {
        std::deque<int> dq(N);
        while (!dq.empty()) {
            dq.pop_back();
        }
        ankerl::nanobench::doNotOptimizeAway(&dq);
    });

    bench.run("devectra", [&]() {
        devectra<int> dv(N);
        while (!dv.empty()) {
            dv.pop_back();
        }
        ankerl::nanobench::doNotOptimizeAway(dv.data());
    });
}

template <size_t N>
void run_devectra_iteration_benchmark() {
    std::deque<int> dq(N);
    devectra<int> dv(N);
    for (size_t i = 0; i < N; ++i) {
        dq[i] = static_cast<int>(i);
        dv[i] = static_cast<int>(i);
    }

    ankerl::nanobench::Bench bench;
    bench.warmup(3).epochs(5).relative(true);

    std::cout << "\n=== devectra vs std::deque iteration: " << N << " elements ===\n";

    bench.run("std::deque", [&]() {
        int sum = 0;
        for (const auto& v : dq) {
            sum += v;
        }
        ankerl::nanobench::doNotOptimizeAway(sum);
    });

    bench.run("devectra", [&]() {
        int sum = 0;
        for (const auto& v : dv) {
            sum += v;
        }
        ankerl::nanobench::doNotOptimizeAway(sum);
    });
}

template <size_t N>
void run_devectra_copy_benchmark() {
    std::deque<int> dq(N);
    devectra<int> dv(N);
    for (size_t i = 0; i < N; ++i) {
        dq[i] = static_cast<int>(i);
        dv[i] = static_cast<int>(i);
    }

    ankerl::nanobench::Bench bench;
    bench.warmup(3).epochs(5).relative(true);

    std::cout << "\n=== devectra vs std::deque copy: " << N << " elements ===\n";

    bench.run("std::deque", [&]() {
        std::deque<int> copy = dq;
        ankerl::nanobench::doNotOptimizeAway(&copy);
    });

    bench.run("devectra", [&]() {
        devectra<int> copy = dv;
        ankerl::nanobench::doNotOptimizeAway(copy.data());
    });
}

template <size_t N>
void run_devectra_fifo_benchmark() {
    ankerl::nanobench::Bench bench;
    bench.warmup(3).epochs(5).relative(true);

    std::cout << "\n=== devectra vs std::deque FIFO pattern: " << N << " push+pop cycles ===\n";

    bench.run("std::deque", [&]() {
        std::deque<int> dq;
        for (size_t i = 0; i < N; ++i) {
            dq.push_back(static_cast<int>(i));
            if (dq.size() > 100) {
                dq.pop_front();
            }
        }
        ankerl::nanobench::doNotOptimizeAway(&dq);
    });

    bench.run("devectra", [&]() {
        devectra<int> dv;
        for (size_t i = 0; i < N; ++i) {
            dv.push_back(static_cast<int>(i));
            if (dv.size() > 100) {
                dv.pop_front();
            }
        }
        ankerl::nanobench::doNotOptimizeAway(dv.data());
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

template <size_t BufSize, size_t N>
void run_ring_buffer_pop_front_benchmark() {
    ankerl::nanobench::Bench bench;
    bench.warmup(3).epochs(5).relative(true);

    std::cout << "\n=== ring_buffer<int, " << BufSize << "> pop_front: " << N << " operations ===\n";

    bench.run("std::deque (simulated)", [&]() {
        std::deque<int> dq;
        for (size_t i = 0; i < BufSize; ++i) dq.push_back(static_cast<int>(i));
        for (size_t i = 0; i < N; ++i) {
            int val = dq.front();
            dq.pop_front();
            dq.push_back(val + 1);
        }
        ankerl::nanobench::doNotOptimizeAway(&dq);
    });

    bench.run("ring_buffer", [&]() {
        ring_buffer<int, BufSize> rb;
        for (size_t i = 0; i < BufSize; ++i) rb.push_back(static_cast<int>(i));
        for (size_t i = 0; i < N; ++i) {
            int val = rb.front();
            rb.pop_front();
            rb.push_back(val + 1);
        }
        ankerl::nanobench::doNotOptimizeAway(&rb);
    });
}

template <size_t BufSize>
void run_ring_buffer_copy_benchmark() {
    ring_buffer<int, BufSize> rb;
    for (size_t i = 0; i < BufSize; ++i) {
        rb.push_back(static_cast<int>(i));
    }

    ankerl::nanobench::Bench bench;
    bench.warmup(3).epochs(5).relative(true);

    std::cout << "\n=== ring_buffer<int, " << BufSize << "> copy ===\n";

    bench.run("ring_buffer copy", [&]() {
        ring_buffer<int, BufSize> copy = rb;
        ankerl::nanobench::doNotOptimizeAway(&copy);
    });
}

// =============================================================================
// static_vectra benchmarks
// =============================================================================

template <size_t N>
void run_static_vectra_push_back_benchmark() {
    ankerl::nanobench::Bench bench;
    bench.warmup(3).epochs(5).relative(true);

    std::cout << "\n=== static_vectra vs std::array-based push_back: " << N << " elements ===\n";

    bench.run("std::vector (reserved)", [&]() {
        std::vector<int> vec;
        vec.reserve(N);
        for (size_t i = 0; i < N; ++i) {
            vec.push_back(static_cast<int>(i));
        }
        ankerl::nanobench::doNotOptimizeAway(vec.data());
    });

    bench.run("static_vectra", [&]() {
        static_vectra<int, N> vec;
        for (size_t i = 0; i < N; ++i) {
            vec.push_back(static_cast<int>(i));
        }
        ankerl::nanobench::doNotOptimizeAway(vec.data());
    });
}

template <size_t N>
void run_static_vectra_iteration_benchmark() {
    std::vector<int> std_vec(N);
    static_vectra<int, N> sv;
    for (size_t i = 0; i < N; ++i) {
        std_vec[i] = static_cast<int>(i);
        sv.push_back(static_cast<int>(i));
    }

    ankerl::nanobench::Bench bench;
    bench.warmup(3).epochs(5).relative(true);

    std::cout << "\n=== static_vectra vs std::vector iteration: " << N << " elements ===\n";

    bench.run("std::vector", [&]() {
        int sum = 0;
        for (const auto& v : std_vec) {
            sum += v;
        }
        ankerl::nanobench::doNotOptimizeAway(sum);
    });

    bench.run("static_vectra", [&]() {
        int sum = 0;
        for (const auto& v : sv) {
            sum += v;
        }
        ankerl::nanobench::doNotOptimizeAway(sum);
    });
}

template <size_t N>
void run_static_vectra_insert_benchmark() {
    ankerl::nanobench::Bench bench;
    bench.warmup(3).epochs(5).relative(true);

    std::cout << "\n=== static_vectra insert at middle: " << N << " inserts ===\n";

    bench.run("std::vector (reserved)", [&]() {
        std::vector<int> vec;
        vec.reserve(N);
        for (size_t i = 0; i < N; ++i) {
            vec.insert(vec.begin() + static_cast<long>(vec.size() / 2), static_cast<int>(i));
        }
        ankerl::nanobench::doNotOptimizeAway(vec.data());
    });

    bench.run("static_vectra", [&]() {
        static_vectra<int, N> vec;
        for (size_t i = 0; i < N; ++i) {
            vec.insert(vec.begin() + static_cast<long>(vec.size() / 2), static_cast<int>(i));
        }
        ankerl::nanobench::doNotOptimizeAway(vec.data());
    });
}

template <size_t N>
void run_static_vectra_erase_benchmark() {
    ankerl::nanobench::Bench bench;
    bench.warmup(3).epochs(5).relative(true);

    std::cout << "\n=== static_vectra erase from middle: " << N << " erases ===\n";

    bench.run("std::vector", [&]() {
        std::vector<int> vec(N);
        for (size_t i = 0; i < N; ++i) vec[i] = static_cast<int>(i);
        while (!vec.empty()) {
            vec.erase(vec.begin() + static_cast<long>(vec.size() / 2));
        }
        ankerl::nanobench::doNotOptimizeAway(vec.data());
    });

    bench.run("static_vectra", [&]() {
        static_vectra<int, N> vec;
        for (size_t i = 0; i < N; ++i) vec.push_back(static_cast<int>(i));
        while (!vec.empty()) {
            vec.erase(vec.begin() + static_cast<long>(vec.size() / 2));
        }
        ankerl::nanobench::doNotOptimizeAway(vec.data());
    });
}

// =============================================================================
// String type benchmarks (non-trivial)
// =============================================================================

template <size_t N>
void run_string_push_back_benchmark() {
    ankerl::nanobench::Bench bench;
    bench.warmup(3).epochs(5).relative(true);

    std::cout << "\n=== small_vectra<string> vs std::vector<string> push_back: " << N << " elements ===\n";

    bench.run("std::vector<string>", [&]() {
        std::vector<std::string> vec;
        for (size_t i = 0; i < N; ++i) {
            vec.push_back("test_string_" + std::to_string(i));
        }
        ankerl::nanobench::doNotOptimizeAway(vec.data());
    });

    bench.run("small_vectra<string>", [&]() {
        small_vectra<std::string> vec;
        for (size_t i = 0; i < N; ++i) {
            vec.push_back("test_string_" + std::to_string(i));
        }
        ankerl::nanobench::doNotOptimizeAway(vec.data());
    });
}

template <size_t N>
void run_string_copy_benchmark() {
    std::vector<std::string> std_vec;
    small_vectra<std::string> sv;
    for (size_t i = 0; i < N; ++i) {
        std::string s = "test_string_" + std::to_string(i);
        std_vec.push_back(s);
        sv.push_back(s);
    }

    ankerl::nanobench::Bench bench;
    bench.warmup(3).epochs(5).relative(true);

    std::cout << "\n=== small_vectra<string> vs std::vector<string> copy: " << N << " elements ===\n";

    bench.run("std::vector<string>", [&]() {
        std::vector<std::string> copy = std_vec;
        ankerl::nanobench::doNotOptimizeAway(copy.data());
    });

    bench.run("small_vectra<string>", [&]() {
        small_vectra<std::string> copy = sv;
        ankerl::nanobench::doNotOptimizeAway(copy.data());
    });
}

template <size_t N>
void run_string_move_benchmark() {
    ankerl::nanobench::Bench bench;
    bench.warmup(3).epochs(5).relative(true);

    std::cout << "\n=== small_vectra<string> vs std::vector<string> move: " << N << " elements ===\n";

    bench.run("std::vector<string>", [&]() {
        std::vector<std::string> vec;
        for (size_t i = 0; i < N; ++i) {
            vec.push_back("test_string_" + std::to_string(i));
        }
        std::vector<std::string> moved = std::move(vec);
        ankerl::nanobench::doNotOptimizeAway(moved.data());
    });

    bench.run("small_vectra<string>", [&]() {
        small_vectra<std::string> vec;
        for (size_t i = 0; i < N; ++i) {
            vec.push_back("test_string_" + std::to_string(i));
        }
        small_vectra<std::string> moved = std::move(vec);
        ankerl::nanobench::doNotOptimizeAway(moved.data());
    });
}

template <size_t N>
void run_devectra_string_benchmark() {
    ankerl::nanobench::Bench bench;
    bench.warmup(3).epochs(5).relative(true);

    std::cout << "\n=== devectra<string> vs std::deque<string> push_back: " << N << " elements ===\n";

    bench.run("std::deque<string>", [&]() {
        std::deque<std::string> dq;
        for (size_t i = 0; i < N; ++i) {
            dq.push_back("test_string_" + std::to_string(i));
        }
        ankerl::nanobench::doNotOptimizeAway(&dq);
    });

    bench.run("devectra<string>", [&]() {
        devectra<std::string> dv;
        for (size_t i = 0; i < N; ++i) {
            dv.push_back("test_string_" + std::to_string(i));
        }
        ankerl::nanobench::doNotOptimizeAway(dv.data());
    });
}

template <size_t BufSize, size_t N>
void run_ring_buffer_string_benchmark() {
    ankerl::nanobench::Bench bench;
    bench.warmup(3).epochs(5).relative(true);

    std::cout << "\n=== ring_buffer<string, " << BufSize << "> push_back: " << N << " operations ===\n";

    bench.run("std::deque<string> (simulated)", [&]() {
        std::deque<std::string> dq;
        for (size_t i = 0; i < N; ++i) {
            if (dq.size() >= BufSize) {
                dq.pop_front();
            }
            dq.push_back("log_entry_" + std::to_string(i));
        }
        ankerl::nanobench::doNotOptimizeAway(&dq);
    });

    bench.run("ring_buffer<string>", [&]() {
        ring_buffer<std::string, BufSize> rb;
        for (size_t i = 0; i < N; ++i) {
            rb.push_back("log_entry_" + std::to_string(i));
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
    std::cout << "Comparing: small_vectra, devectra, ring_buffer, static_vectra\n";
    std::cout << "============================================================\n";

    // small_vectra benchmarks
    std::cout << "\n### small_vectra Benchmarks ###\n";
    run_small_vectra_push_back_benchmark<1000>();
    run_small_vectra_push_back_benchmark<10000>();
    run_small_vectra_reserved_benchmark<10000>();
    run_small_vectra_emplace_back_benchmark<10000>();
    run_small_vectra_iteration_benchmark<10000>();
    run_small_vectra_copy_benchmark<10000>();
    run_small_vectra_insert_benchmark<1000>();
    run_small_vectra_erase_benchmark<1000>();
    run_small_vectra_pop_back_benchmark<10000>();
    run_small_vectra_clear_repopulate_benchmark<1000>();

    // Small size benchmarks - where small_vectra shines (inline storage, no heap)
    std::cout << "\n### small_vectra Small Size Benchmarks ###\n";
    run_small_vectra_small_size_benchmark<4>();
    run_small_vectra_small_size_benchmark<8>();
    run_small_vectra_small_size_benchmark<16>();
    run_small_vectra_batch_benchmark<8>();
    run_small_vectra_batch_benchmark<16>();
    run_small_vectra_small_string_benchmark<4>();
    run_small_vectra_small_string_benchmark<8>();

    // devectra benchmarks
    std::cout << "\n### devectra Benchmarks ###\n";
    run_devectra_push_back_benchmark<1000>();
    run_devectra_push_back_benchmark<10000>();
    run_devectra_push_front_benchmark<1000>();
    run_devectra_push_front_benchmark<10000>();
    run_devectra_alternating_benchmark<10000>();
    run_devectra_random_access_benchmark<10000>();
    run_devectra_pop_front_benchmark<10000>();
    run_devectra_pop_back_benchmark<10000>();
    run_devectra_iteration_benchmark<10000>();
    run_devectra_copy_benchmark<10000>();
    run_devectra_fifo_benchmark<10000>();

    // ring_buffer benchmarks
    std::cout << "\n### ring_buffer Benchmarks ###\n";
    run_ring_buffer_benchmark<64, 10000>();
    run_ring_buffer_benchmark<1024, 100000>();
    run_ring_buffer_iteration_benchmark<1024>();
    run_ring_buffer_power_of_2_comparison();
    run_ring_buffer_pop_front_benchmark<64, 10000>();
    run_ring_buffer_copy_benchmark<1024>();

    // static_vectra benchmarks
    std::cout << "\n### static_vectra Benchmarks ###\n";
    run_static_vectra_push_back_benchmark<1000>();
    run_static_vectra_iteration_benchmark<1000>();
    run_static_vectra_insert_benchmark<500>();
    run_static_vectra_erase_benchmark<500>();

    // String type benchmarks
    std::cout << "\n### String Type Benchmarks ###\n";
    run_string_push_back_benchmark<1000>();
    run_string_copy_benchmark<1000>();
    run_string_move_benchmark<1000>();
    run_devectra_string_benchmark<1000>();
    run_ring_buffer_string_benchmark<100, 10000>();

    std::cout << "\n============================================================\n";
    std::cout << "Benchmark completed.\n";
    std::cout << "============================================================\n";

    return 0;
}

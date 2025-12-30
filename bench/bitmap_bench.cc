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

#include <algorithm>
#include <bitset>
#include <iostream>
#include <random>
#include <vector>

#include "../nanobench/src/include/nanobench.h"
#include "container/bitmap.hpp"

using namespace stdb::container;

// NOLINTBEGIN

// Bitmap sizes for testing
constexpr size_t SMALL_SIZE = 1024;            // 1K bits
constexpr size_t MEDIUM_SIZE = 64 * 1024;      // 64K bits
constexpr size_t LARGE_SIZE = 1024 * 1024;     // 1M bits

// =============================================================================
// Helper: fill vector<bool>
// =============================================================================

void fill_random_vector_bool(std::vector<bool>& b, double density, std::mt19937& rng) {
    std::uniform_real_distribution<double> dist(0.0, 1.0);
    for (size_t i = 0; i < b.size(); ++i) {
        if (dist(rng) < density) {
            b[i] = true;
        }
    }
}

// =============================================================================
// Helper: fill with random bits
// =============================================================================

template <size_t Size>
void fill_random_bitmap(bitmap& b, double density, std::mt19937& rng) {
    std::uniform_real_distribution<double> dist(0.0, 1.0);
    for (size_t i = 0; i < Size; ++i) {
        if (dist(rng) < density) {
            b.set(static_cast<uint32_t>(i));
        }
    }
}

template <size_t Size>
void fill_random_bitset(std::bitset<Size>& b, double density, std::mt19937& rng) {
    std::uniform_real_distribution<double> dist(0.0, 1.0);
    for (size_t i = 0; i < Size; ++i) {
        if (dist(rng) < density) {
            b.set(i);
        }
    }
}

// =============================================================================
// Comparison benchmarks: bitmap vs std::bitset
// =============================================================================

template <size_t Size>
void bench_set_comparison(ankerl::nanobench::Bench& bench, const std::string& size_label) {
    std::vector<uint32_t> positions(1000);
    std::mt19937 rng(42);
    for (size_t i = 0; i < 1000; ++i) {
        positions[i] = rng() % Size;
    }

    bench.run(size_label + " bitmap::set", [&]() {
        bitmap b(Size);
        for (uint32_t pos : positions) {
            b.set(pos);
        }
        ankerl::nanobench::doNotOptimizeAway(b);
    });

    bench.run(size_label + " std::bitset::set", [&]() {
        std::bitset<Size> b;
        for (uint32_t pos : positions) {
            b.set(pos);
        }
        ankerl::nanobench::doNotOptimizeAway(b);
    });
}

template <size_t Size>
void bench_test_comparison(ankerl::nanobench::Bench& bench, const std::string& size_label) {
    std::mt19937 rng(42);

    bitmap bm(Size);
    fill_random_bitmap<Size>(bm, 0.5, rng);

    rng.seed(42);
    std::bitset<Size> bs;
    fill_random_bitset<Size>(bs, 0.5, rng);

    std::vector<uint32_t> queries(1000);
    for (size_t i = 0; i < 1000; ++i) {
        queries[i] = rng() % Size;
    }

    bench.run(size_label + " bitmap::test", [&]() {
        size_t count = 0;
        for (uint32_t q : queries) {
            count += bm.test(q);
        }
        ankerl::nanobench::doNotOptimizeAway(count);
    });

    bench.run(size_label + " std::bitset::test", [&]() {
        size_t count = 0;
        for (uint32_t q : queries) {
            count += bs.test(q);
        }
        ankerl::nanobench::doNotOptimizeAway(count);
    });
}

template <size_t Size>
void bench_popcount_comparison(ankerl::nanobench::Bench& bench, const std::string& size_label) {
    std::mt19937 rng(42);

    bitmap bm(Size);
    fill_random_bitmap<Size>(bm, 0.5, rng);

    rng.seed(42);
    std::bitset<Size> bs;
    fill_random_bitset<Size>(bs, 0.5, rng);

    bench.run(size_label + " bitmap::popcount", [&]() {
        size_t count = bm.popcount();
        ankerl::nanobench::doNotOptimizeAway(count);
    });

    bench.run(size_label + " std::bitset::count", [&]() {
        size_t count = bs.count();
        ankerl::nanobench::doNotOptimizeAway(count);
    });
}

template <size_t Size>
void bench_and_comparison(ankerl::nanobench::Bench& bench, const std::string& size_label) {
    std::mt19937 rng(42);

    bitmap bm_a(Size), bm_b(Size);
    fill_random_bitmap<Size>(bm_a, 0.5, rng);
    fill_random_bitmap<Size>(bm_b, 0.5, rng);

    rng.seed(42);
    std::bitset<Size> bs_a, bs_b;
    fill_random_bitset<Size>(bs_a, 0.5, rng);
    fill_random_bitset<Size>(bs_b, 0.5, rng);

    bench.run(size_label + " bitmap AND", [&]() {
        bitmap c = bm_a;
        c.and_(bm_b);
        ankerl::nanobench::doNotOptimizeAway(c);
    });

    bench.run(size_label + " std::bitset AND", [&]() {
        std::bitset<Size> c = bs_a & bs_b;
        ankerl::nanobench::doNotOptimizeAway(c);
    });
}

template <size_t Size>
void bench_or_comparison(ankerl::nanobench::Bench& bench, const std::string& size_label) {
    std::mt19937 rng(42);

    bitmap bm_a(Size), bm_b(Size);
    fill_random_bitmap<Size>(bm_a, 0.5, rng);
    fill_random_bitmap<Size>(bm_b, 0.5, rng);

    rng.seed(42);
    std::bitset<Size> bs_a, bs_b;
    fill_random_bitset<Size>(bs_a, 0.5, rng);
    fill_random_bitset<Size>(bs_b, 0.5, rng);

    bench.run(size_label + " bitmap OR", [&]() {
        bitmap c = bm_a;
        c.or_(bm_b);
        ankerl::nanobench::doNotOptimizeAway(c);
    });

    bench.run(size_label + " std::bitset OR", [&]() {
        std::bitset<Size> c = bs_a | bs_b;
        ankerl::nanobench::doNotOptimizeAway(c);
    });
}

template <size_t Size>
void bench_xor_comparison(ankerl::nanobench::Bench& bench, const std::string& size_label) {
    std::mt19937 rng(42);

    bitmap bm_a(Size), bm_b(Size);
    fill_random_bitmap<Size>(bm_a, 0.5, rng);
    fill_random_bitmap<Size>(bm_b, 0.5, rng);

    rng.seed(42);
    std::bitset<Size> bs_a, bs_b;
    fill_random_bitset<Size>(bs_a, 0.5, rng);
    fill_random_bitset<Size>(bs_b, 0.5, rng);

    bench.run(size_label + " bitmap XOR", [&]() {
        bitmap c = bm_a;
        c.xor_(bm_b);
        ankerl::nanobench::doNotOptimizeAway(c);
    });

    bench.run(size_label + " std::bitset XOR", [&]() {
        std::bitset<Size> c = bs_a ^ bs_b;
        ankerl::nanobench::doNotOptimizeAway(c);
    });
}

template <size_t Size>
void bench_flip_comparison(ankerl::nanobench::Bench& bench, const std::string& size_label) {
    std::vector<uint32_t> positions(1000);
    std::mt19937 rng(42);
    for (size_t i = 0; i < 1000; ++i) {
        positions[i] = rng() % Size;
    }

    bitmap bm(Size);
    std::bitset<Size> bs;

    bench.run(size_label + " bitmap::flip", [&]() {
        for (uint32_t pos : positions) {
            bm.flip(pos);
        }
        ankerl::nanobench::doNotOptimizeAway(bm);
    });

    bench.run(size_label + " std::bitset::flip", [&]() {
        for (uint32_t pos : positions) {
            bs.flip(pos);
        }
        ankerl::nanobench::doNotOptimizeAway(bs);
    });
}

template <size_t Size>
void bench_any_none_comparison(ankerl::nanobench::Bench& bench, const std::string& size_label) {
    std::mt19937 rng(42);

    bitmap bm(Size);
    fill_random_bitmap<Size>(bm, 0.5, rng);

    rng.seed(42);
    std::bitset<Size> bs;
    fill_random_bitset<Size>(bs, 0.5, rng);

    // bitmap uses find_first() != -1 for "any"
    bench.run(size_label + " bitmap any()", [&]() {
        bool has_any = bm.find_first() != -1;
        ankerl::nanobench::doNotOptimizeAway(has_any);
    });

    bench.run(size_label + " std::bitset::any", [&]() {
        bool has_any = bs.any();
        ankerl::nanobench::doNotOptimizeAway(has_any);
    });

    // Test with empty bitmap
    bitmap bm_empty(Size);
    std::bitset<Size> bs_empty;

    bench.run(size_label + " bitmap any() [empty]", [&]() {
        bool has_any = bm_empty.find_first() != -1;
        ankerl::nanobench::doNotOptimizeAway(has_any);
    });

    bench.run(size_label + " std::bitset::any [empty]", [&]() {
        bool has_any = bs_empty.any();
        ankerl::nanobench::doNotOptimizeAway(has_any);
    });
}

// =============================================================================
// vector<bool> comparison
// =============================================================================

template <size_t Size>
void bench_vector_bool_set(ankerl::nanobench::Bench& bench, const std::string& size_label) {
    std::vector<uint32_t> positions(1000);
    std::mt19937 rng(42);
    for (size_t i = 0; i < 1000; ++i) {
        positions[i] = rng() % Size;
    }

    bench.run(size_label + " bitmap::set", [&]() {
        bitmap b(Size);
        for (uint32_t pos : positions) {
            b.set(pos);
        }
        ankerl::nanobench::doNotOptimizeAway(b);
    });

    bench.run(size_label + " vector<bool>::set", [&]() {
        std::vector<bool> b(Size, false);
        for (uint32_t pos : positions) {
            b[pos] = true;
        }
        ankerl::nanobench::doNotOptimizeAway(b);
    });
}

template <size_t Size>
void bench_vector_bool_test(ankerl::nanobench::Bench& bench, const std::string& size_label) {
    std::mt19937 rng(42);

    bitmap bm(Size);
    fill_random_bitmap<Size>(bm, 0.5, rng);

    rng.seed(42);
    std::vector<bool> vb(Size, false);
    fill_random_vector_bool(vb, 0.5, rng);

    std::vector<uint32_t> queries(1000);
    for (size_t i = 0; i < 1000; ++i) {
        queries[i] = rng() % Size;
    }

    bench.run(size_label + " bitmap::test", [&]() {
        size_t count = 0;
        for (uint32_t q : queries) {
            count += bm.test(q);
        }
        ankerl::nanobench::doNotOptimizeAway(count);
    });

    bench.run(size_label + " vector<bool>::test", [&]() {
        size_t count = 0;
        for (uint32_t q : queries) {
            count += vb[q];
        }
        ankerl::nanobench::doNotOptimizeAway(count);
    });
}

template <size_t Size>
void bench_vector_bool_popcount(ankerl::nanobench::Bench& bench, const std::string& size_label) {
    std::mt19937 rng(42);

    bitmap bm(Size);
    fill_random_bitmap<Size>(bm, 0.5, rng);

    rng.seed(42);
    std::vector<bool> vb(Size, false);
    fill_random_vector_bool(vb, 0.5, rng);

    bench.run(size_label + " bitmap::popcount", [&]() {
        size_t count = bm.popcount();
        ankerl::nanobench::doNotOptimizeAway(count);
    });

    bench.run(size_label + " vector<bool> count", [&]() {
        size_t count = std::count(vb.begin(), vb.end(), true);
        ankerl::nanobench::doNotOptimizeAway(count);
    });
}

template <size_t Size>
void bench_vector_bool_iterate(ankerl::nanobench::Bench& bench, const std::string& size_label) {
    std::mt19937 rng(42);

    bitmap bm(Size);
    fill_random_bitmap<Size>(bm, 0.01, rng);  // 1% density

    rng.seed(42);
    std::vector<bool> vb(Size, false);
    fill_random_vector_bool(vb, 0.01, rng);

    bench.run(size_label + " bitmap iterate", [&]() {
        size_t sum = 0;
        for (uint32_t bit : bm) {
            sum += bit;
        }
        ankerl::nanobench::doNotOptimizeAway(sum);
    });

    bench.run(size_label + " vector<bool> iterate", [&]() {
        size_t sum = 0;
        for (size_t i = 0; i < vb.size(); ++i) {
            if (vb[i]) sum += i;
        }
        ankerl::nanobench::doNotOptimizeAway(sum);
    });
}

// =============================================================================
// bitmap-only benchmarks (features not in std::bitset)
// =============================================================================

template <size_t Size>
void bench_find_first(ankerl::nanobench::Bench& bench, const std::string& size_label) {
    std::mt19937 rng(42);
    bitmap b(Size);
    fill_random_bitmap<Size>(b, 0.01, rng);  // Sparse

    bench.run(size_label + " bitmap::find_first", [&]() {
        int64_t pos = b.find_first();
        ankerl::nanobench::doNotOptimizeAway(pos);
    });
}

template <size_t Size>
void bench_iterate(ankerl::nanobench::Bench& bench, const std::string& size_label) {
    std::mt19937 rng(42);
    bitmap b(Size);
    fill_random_bitmap<Size>(b, 0.01, rng);  // ~1% density

    bench.run(size_label + " bitmap iterate", [&]() {
        size_t sum = 0;
        for (uint32_t bit : b) {
            sum += bit;
        }
        ankerl::nanobench::doNotOptimizeAway(sum);
    });
}

template <size_t Size>
void bench_in_operation(ankerl::nanobench::Bench& bench, const std::string& size_label) {
    std::mt19937 rng(42);
    bitmap b(Size);
    fill_random_bitmap<Size>(b, 0.5, rng);

    std::vector<uint32_t> ids(10000);
    for (size_t i = 0; i < ids.size(); ++i) {
        ids[i] = rng() % Size;
    }
    std::vector<uint32_t> result(ids.size());

    bench.run(size_label + " bitmap::in 10K", [&]() {
        size_t count = b.in(ids.data(), ids.size(), result.data());
        ankerl::nanobench::doNotOptimizeAway(count);
    });
}

int main() {
    ankerl::nanobench::Bench bench;
    bench.title("bitmap vs std::bitset").unit("op").warmup(100).epochs(500).minEpochIterations(100);

    std::cout << "\n";
    std::cout << "================================================================================\n";
    std::cout << "                        bitmap vs std::bitset Comparison\n";
    std::cout << "================================================================================\n";

    // 1K bits
    std::cout << "\n=== 1K bits ===\n";
    bench_set_comparison<SMALL_SIZE>(bench, "1K");
    bench_test_comparison<SMALL_SIZE>(bench, "1K");
    bench_popcount_comparison<SMALL_SIZE>(bench, "1K");
    bench_and_comparison<SMALL_SIZE>(bench, "1K");
    bench_or_comparison<SMALL_SIZE>(bench, "1K");
    bench_xor_comparison<SMALL_SIZE>(bench, "1K");

    // 64K bits
    std::cout << "\n=== 64K bits ===\n";
    bench_set_comparison<MEDIUM_SIZE>(bench, "64K");
    bench_test_comparison<MEDIUM_SIZE>(bench, "64K");
    bench_popcount_comparison<MEDIUM_SIZE>(bench, "64K");
    bench_and_comparison<MEDIUM_SIZE>(bench, "64K");
    bench_or_comparison<MEDIUM_SIZE>(bench, "64K");
    bench_xor_comparison<MEDIUM_SIZE>(bench, "64K");
    bench_flip_comparison<MEDIUM_SIZE>(bench, "64K");
    bench_any_none_comparison<MEDIUM_SIZE>(bench, "64K");

    // 1M bits
    std::cout << "\n=== 1M bits ===\n";
    bench_set_comparison<LARGE_SIZE>(bench, "1M");
    bench_test_comparison<LARGE_SIZE>(bench, "1M");
    bench_popcount_comparison<LARGE_SIZE>(bench, "1M");
    bench_and_comparison<LARGE_SIZE>(bench, "1M");
    bench_or_comparison<LARGE_SIZE>(bench, "1M");
    bench_xor_comparison<LARGE_SIZE>(bench, "1M");

    // bitmap-only features
    std::cout << "\n=== bitmap-only features ===\n";
    bench_find_first<LARGE_SIZE>(bench, "1M");
    bench_iterate<LARGE_SIZE>(bench, "1M");
    bench_in_operation<MEDIUM_SIZE>(bench, "64K");
    bench_in_operation<LARGE_SIZE>(bench, "1M");

    std::cout << "\n";
    std::cout << "================================================================================\n";
    std::cout << "                        bitmap vs vector<bool> Comparison\n";
    std::cout << "================================================================================\n";

    std::cout << "\n=== 64K bits ===\n";
    bench_vector_bool_set<MEDIUM_SIZE>(bench, "64K");
    bench_vector_bool_test<MEDIUM_SIZE>(bench, "64K");
    bench_vector_bool_popcount<MEDIUM_SIZE>(bench, "64K");

    std::cout << "\n=== 1M bits ===\n";
    bench_vector_bool_set<LARGE_SIZE>(bench, "1M");
    bench_vector_bool_test<LARGE_SIZE>(bench, "1M");
    bench_vector_bool_popcount<LARGE_SIZE>(bench, "1M");
    bench_vector_bool_iterate<LARGE_SIZE>(bench, "1M");

    return 0;
}

// NOLINTEND

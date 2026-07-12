/*
 * Differential tests: every hand-written SIMD kernel against the scalar reference that sits
 * next to it in the same file.
 *
 * Two SIMD bugs shipped in this library, and both were of the same shape -- a kernel that was
 * right on the common path and wrong at the edge, with nothing comparing it to anything:
 *
 *   * boolean_vector::count() accumulated 0/1 bytes into 8-bit lanes and only widened after the
 *     loop, so a lane wrapped on the 256th iteration -- 4096 elements on SSE, 8192 on AVX2.
 *   * bitmap's popcount_avx2() folded its byte accumulator every 32 iterations where its own
 *     comment said 31, so on a fully set bitmap every lane wrapped together and popcount()
 *     returned *zero* from 128 words up.
 *
 * Neither was exotic. Both needed nothing but a dense input and a scalar to compare against, and
 * the scalar was already there. So compare against it, on every kernel, at every width, at the
 * densities and sizes where a byte lane can actually saturate -- and do it for the whole family,
 * not just the two that were caught, because and/or/xor/andnot have never been checked against
 * anything either.
 *
 * These tests are density- and size-driven on purpose. Which kernel runs is a compile-time choice
 * (BITMAP_HAS_AVX2, BOOLVEC_HAS_SSE2, ...), so a single -march only ever exercises one of them --
 * which is why CI builds this at -march=x86-64-v2 (no AVX2: bitmap falls through to scalar) *and*
 * -march=x86-64-v3 (AVX2). One march is structurally blind to half of these bugs.
 */
#include <doctest/doctest.h>

#include <cstdint>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>

#include "container/bitmap.hpp"
#include "container/boolean_vector.hpp"

namespace stdb::container {

namespace {

// Which kernel did this build actually select? Reported so a CI failure says so out loud.
const char* simd_path() {
#if defined(BITMAP_HAS_AVX512_VPOPCNT)
    return "AVX512-VPOPCNT";
#elif defined(BITMAP_HAS_AVX2)
    return "AVX2";
#elif defined(BITMAP_HAS_NEON)
    return "NEON";
#else
    return "scalar";
#endif
}

// Word counts that straddle every vector width and every fold boundary in the file: the AVX2
// popcount folds every 31 iterations of 4 words (124), the SSE kernels step 2 words, and the
// tails are what a "+= 4 <= n" loop leaves behind.
const std::vector<size_t> kWordCounts = {0,  1,   2,   3,   4,   5,   7,   8,   15,  16,   17,  31,
                                         32, 33,  63,  64,  65,  123, 124, 125, 127, 128,  129, 255,
                                         256, 257, 511, 512, 1023, 1024, 4096};

// Aligned buffer: the AVX2 popcount uses _mm256_load_si256, which requires 32-byte alignment.
struct AlignedWords {
    uint64_t* data = nullptr;
    size_t count = 0;

    explicit AlignedWords(size_t n) : count(n) {
        if (n == 0) {
            return;
        }
        size_t bytes = ((n * sizeof(uint64_t) + 63) / 64) * 64;
        data = static_cast<uint64_t*>(std::aligned_alloc(64, bytes));
        REQUIRE(data != nullptr);
    }
    ~AlignedWords() { std::free(data); }
    AlignedWords(const AlignedWords&) = delete;
    AlignedWords& operator=(const AlignedWords&) = delete;
};

// The densities that matter. all-ones is the one that saturates a byte lane fastest and the one
// nothing ever tested; "sparse" is what every existing test used, and is exactly why the bugs hid.
enum class Fill { Zeros, Ones, Random, Alternating, Sparse };

void fill_words(uint64_t* p, size_t n, Fill f, std::mt19937_64& rng) {
    for (size_t i = 0; i < n; ++i) {
        switch (f) {
            case Fill::Zeros:
                p[i] = 0;
                break;
            case Fill::Ones:
                p[i] = ~uint64_t(0);
                break;
            case Fill::Random:
                p[i] = rng();
                break;
            case Fill::Alternating:
                p[i] = 0xAAAAAAAAAAAAAAAAULL;
                break;
            case Fill::Sparse:
                p[i] = (i % 8 == 0) ? 1ULL : 0ULL;
                break;
        }
    }
}

const char* fill_name(Fill f) {
    switch (f) {
        case Fill::Zeros:
            return "zeros";
        case Fill::Ones:
            return "ones";
        case Fill::Random:
            return "random";
        case Fill::Alternating:
            return "alternating";
        case Fill::Sparse:
            return "sparse";
    }
    return "?";
}

}  // namespace

TEST_SUITE("simd_differential") {

    TEST_CASE("bitmap::popcount agrees with the scalar reference") {
        INFO("SIMD path selected by this build: ", simd_path());
        std::mt19937_64 rng(20260713);

        for (Fill f : {Fill::Zeros, Fill::Ones, Fill::Random, Fill::Alternating, Fill::Sparse}) {
            for (size_t n : kWordCounts) {
                AlignedWords buf(n);
                fill_words(buf.data, n, f, rng);

                size_t got = bitmap_detail::bitmap_popcount(buf.data, n);
                size_t want = bitmap_detail::popcount_scalar(buf.data, n);

                INFO("fill=", fill_name(f), " words=", n);
                CHECK_EQ(got, want);
            }
        }
    }

    // and/or/xor/andnot have scalar references too, and have never been compared to them.
    TEST_CASE("bitmap bitwise ops agree with the scalar reference") {
        INFO("SIMD path selected by this build: ", simd_path());
        std::mt19937_64 rng(918273645);

        for (Fill f : {Fill::Zeros, Fill::Ones, Fill::Random, Fill::Alternating, Fill::Sparse}) {
            for (size_t n : kWordCounts) {
                AlignedWords src(n);
                AlignedWords a(n);
                AlignedWords b(n);
                fill_words(src.data, n, f, rng);

                auto run = [&](const char* op, void (*simd)(uint64_t*, const uint64_t*, size_t),
                               void (*ref)(uint64_t*, const uint64_t*, size_t)) {
                    fill_words(a.data, n, Fill::Random, rng);
                    for (size_t i = 0; i < n; ++i) {
                        b.data[i] = a.data[i];  // same starting dst for both
                    }
                    simd(a.data, src.data, n);
                    ref(b.data, src.data, n);
                    for (size_t i = 0; i < n; ++i) {
                        INFO("op=", op, " fill=", fill_name(f), " words=", n, " word=", i);
                        CHECK_EQ(a.data[i], b.data[i]);
                    }
                };

                run("and", bitmap_detail::bitmap_and, bitmap_detail::and_scalar);
                run("or", bitmap_detail::bitmap_or, bitmap_detail::or_scalar);
                run("xor", bitmap_detail::bitmap_xor, bitmap_detail::xor_scalar);
                run("andnot", bitmap_detail::bitmap_andnot, bitmap_detail::andnot_scalar);
            }
        }
    }

    // boolean_vector's kernels have no in-file scalar twin, so the reference is a naive loop.
    // Sizes straddle both wrap boundaries: 256 * 16 = 4096 (SSE) and 256 * 32 = 8192 (AVX2).
    TEST_CASE("boolean_vector count/any/all/none agree with a naive loop") {
        std::mt19937 rng(13572468);

        const std::vector<size_t> sizes = {0,    1,    15,   16,   17,   31,   32,   33,   4095, 4096,
                                           4097, 8191, 8192, 8193, 10000, 20000, 65536, 100000};

        // density 1.0 is the case that wraps a byte lane fastest, and the case nothing covered.
        for (double density : {0.0, 1.0, 0.5, 0.99, 0.01}) {
            for (size_t n : sizes) {
                boolean_vector v(n, false);
                std::bernoulli_distribution d(density);

                size_t want_count = 0;
                for (size_t i = 0; i < n; ++i) {
                    bool bit = (density == 1.0) ? true : (density == 0.0 ? false : d(rng));
                    v[i] = bit;
                    want_count += bit ? 1 : 0;
                }

                INFO("density=", density, " n=", n);
                CHECK_EQ(v.count(), want_count);
                CHECK_EQ(v.any(), want_count > 0);
                // all() on an empty vector is vacuously true, as std::all_of is on an empty range --
                // want_count == n gives that for free at n == 0.
                CHECK_EQ(v.all(), want_count == n);
                CHECK_EQ(v.none(), want_count == 0);
            }
        }
    }

    TEST_CASE("boolean_vector::flip agrees with a naive loop") {
        std::mt19937 rng(24681357);

        for (size_t n : {0u, 1u, 15u, 16u, 17u, 31u, 32u, 33u, 4096u, 8192u, 10000u}) {
            boolean_vector v(n, false);
            std::vector<bool> ref(n);
            std::bernoulli_distribution d(0.5);
            for (size_t i = 0; i < n; ++i) {
                bool bit = d(rng);
                v[i] = bit;
                ref[i] = bit;
            }

            v.flip();
            for (size_t i = 0; i < n; ++i) {
                INFO("n=", n, " i=", i);
                CHECK_EQ(v[i], !ref[i]);
            }
            // and count() must still agree afterwards
            size_t want = 0;
            for (size_t i = 0; i < n; ++i) {
                want += ref[i] ? 0 : 1;
            }
            CHECK_EQ(v.count(), want);
        }
    }
}

}  // namespace stdb::container

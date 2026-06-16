# Use nanobench from git submodule
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../nanobench ${CMAKE_CURRENT_BINARY_DIR}/nanobench)

# Helper function to create benchmark targets
function(add_bench_target name source)
    add_executable(${name} ${source})
    target_compile_definitions(${name} PRIVATE NDEBUG)
    if(ENABLE_ASAN OR ENABLE_UBSAN)
        target_compile_options(${name} PRIVATE ${SANITIZER_FLAGS})
        target_link_options(${name} PRIVATE ${SANITIZER_LINK_FLAGS})
    endif()
    target_link_libraries(${name} nanobench)
endfunction()

# Skiplist benchmarks
add_bench_target(skiplist_bench skiplist_bench.cc)

# Bitmap benchmarks
add_bench_target(bitmap_bench bitmap_bench.cc)

# Boolean vector benchmarks
add_bench_target(boolean_vector_bench boolean_vector_bench.cc)

# Container benchmarks (small_vectra, devectra, ring_buffer)
add_bench_target(container_bench container_bench.cc)

# immutable_ordered_multimap vs containa btree_multimap
add_bench_target(immutable_ordered_multimap_bench immutable_ordered_multimap_bench.cc)

# Dense map benchmarks
add_executable(dense_map_bench dense_map_bench.cc)
target_compile_definitions(dense_map_bench PRIVATE NDEBUG)
target_link_libraries(dense_map_bench nanobench)
if(ENABLE_ABSL_BENCH)
    target_compile_definitions(dense_map_bench PRIVATE ENABLE_ABSL)
    target_link_libraries(dense_map_bench absl::flat_hash_map absl::hash)
endif()

# Comprehensive dense_map benchmark (vs ankerl vs tsl)
add_executable(dense_map_comprehensive_bench dense_map_comprehensive_bench.cc)
target_compile_definitions(dense_map_comprehensive_bench PRIVATE NDEBUG)
target_link_libraries(dense_map_comprehensive_bench nanobench)

# Concurrent benchmark (skiplist vs btree)
add_executable(skiplist_concurrent_bench skiplist_concurrent_bench.cc)
target_compile_definitions(skiplist_concurrent_bench PRIVATE NDEBUG)
target_link_libraries(skiplist_concurrent_bench pthread)

# B-tree benchmark (with optional Abseil comparison)
add_executable(btree_bench btree_bench.cc)
target_compile_definitions(btree_bench PRIVATE NDEBUG)
target_link_libraries(btree_bench nanobench)

if(ENABLE_ABSL_BENCH)
    # Add Abseil support to btree_bench
    target_compile_definitions(btree_bench PRIVATE ENABLE_ABSL)
    target_link_libraries(btree_bench absl::btree)

    # Also build the comprehensive absl comparison benchmark
    add_executable(absl_btree_bench absl_btree_bench.cc)
    target_compile_definitions(absl_btree_bench PRIVATE NDEBUG)
    target_link_libraries(absl_btree_bench
        nanobench
        absl::btree
        absl::flat_hash_map
        absl::hash
    )
    message(STATUS "Building Abseil btree comparison benchmarks")
endif()

# PMR allocator benchmark
option(ENABLE_PMR_BENCH "Enable PMR allocator benchmark" ON)
if(ENABLE_PMR_BENCH)
    add_executable(pmr_bench pmr_bench.cc)
    target_compile_definitions(pmr_bench PRIVATE NDEBUG)
    target_link_libraries(pmr_bench nanobench)
    if(ENABLE_ASAN OR ENABLE_UBSAN)
        target_compile_options(pmr_bench PRIVATE ${SANITIZER_FLAGS})
        target_link_options(pmr_bench PRIVATE ${SANITIZER_LINK_FLAGS})
    endif()

    # Optional Arena allocator support (user provides ARENA_DIR)
    if(DEFINED ARENA_DIR)
        include(FetchContent)
        FetchContent_Declare(
            fmt
            GIT_REPOSITORY https://github.com/fmtlib/fmt.git
            GIT_TAG 11.1.4
            GIT_SHALLOW TRUE
        )
        FetchContent_MakeAvailable(fmt)

        add_library(arena_lib STATIC ${ARENA_DIR}/src/arena.cc)
        target_include_directories(arena_lib PUBLIC ${ARENA_DIR}/include)
        target_compile_options(arena_lib PRIVATE -frtti)
        target_link_libraries(arena_lib fmt::fmt)

        target_compile_definitions(pmr_bench PRIVATE ENABLE_ARENA_BENCH)
        target_include_directories(pmr_bench PRIVATE ${ARENA_DIR}/include)
        target_link_libraries(pmr_bench arena_lib fmt::fmt)
        message(STATUS "Building PMR benchmark with Arena support (ARENA_DIR=${ARENA_DIR})")
    else()
        message(STATUS "Building PMR benchmark without Arena (set ARENA_DIR to enable)")
    endif()
endif()

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

# Vector benchmarks
add_bench_target(vb vector_bench.cc)

# Skiplist benchmarks
add_bench_target(skiplist_bench skiplist_bench.cc)

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

# PMR + Arena allocator benchmark
option(ENABLE_PMR_BENCH "Enable PMR/Arena allocator benchmark" ON)
if(ENABLE_PMR_BENCH)
    # Fetch fmt for arena dependency
    include(FetchContent)
    FetchContent_Declare(
        fmt
        GIT_REPOSITORY https://github.com/fmtlib/fmt.git
        GIT_TAG 11.1.4
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(fmt)

    # Build arena library
    set(ARENA_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../arena/src)
    add_library(arena_lib STATIC ${ARENA_SRC_DIR}/arena.cc)
    target_include_directories(arena_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../arena/include)
    target_compile_options(arena_lib PRIVATE -frtti)
    target_link_libraries(arena_lib fmt::fmt)

    add_executable(pmr_bench pmr_bench.cc)
    target_compile_definitions(pmr_bench PRIVATE NDEBUG)
    target_include_directories(pmr_bench PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../arena/include
    )
    target_link_libraries(pmr_bench nanobench arena_lib fmt::fmt)
    if(ENABLE_ASAN OR ENABLE_UBSAN)
        target_compile_options(pmr_bench PRIVATE ${SANITIZER_FLAGS})
        target_link_options(pmr_bench PRIVATE ${SANITIZER_LINK_FLAGS})
    endif()
    message(STATUS "Building PMR/Arena allocator benchmark")
endif()

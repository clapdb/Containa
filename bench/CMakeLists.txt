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

# Bitmap benchmarks
add_bench_target(bitmap_bench bitmap_bench.cc)

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

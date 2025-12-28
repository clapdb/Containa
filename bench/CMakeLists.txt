# Use nanobench from git submodule
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../nanobench ${CMAKE_CURRENT_BINARY_DIR}/nanobench)

#FILE(GLOB_RECURSE BENCH_SRC *.cc)
LIST(APPEND BENCH_SRC vector_bench.cc)
add_executable(vb ${BENCH_SRC})

# Force NDEBUG for benchmarks (disable assertions for accurate timing)
target_compile_definitions(vb PRIVATE NDEBUG)

# Apply sanitizer flags if enabled
if(ENABLE_ASAN OR ENABLE_UBSAN)
    target_compile_options(vb PRIVATE ${SANITIZER_FLAGS})
    target_link_options(vb PRIVATE ${SANITIZER_LINK_FLAGS})
endif()

# Link with nanobench (header-only, so just need include)
target_link_libraries(vb nanobench)


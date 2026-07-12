cmake_minimum_required(VERSION 3.10)

aux_source_directory(. TEST_SRC)

add_executable(containa_test EXCLUDE_FROM_ALL ${TEST_SRC})


## test flags
list(APPEND TEST_FLAGS
  #-DBOOST_STACKTRACE_USE_BACKTRACE
  -DDOCTEST_CONFIG_SUPER_FAST_ASSERTS
  -Wno-deprecated
)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-deprecated-declarations -Wno-error=cpp")
### include arena src
#target_include_directories(memory_test PRIVATE ${MEMORY_SOURCE_DIR}/)

target_compile_options(containa_test PRIVATE ${TEST_FLAGS})

# Apply sanitizer flags if enabled
if(ENABLE_ASAN OR ENABLE_UBSAN)
    target_compile_options(containa_test PRIVATE ${SANITIZER_FLAGS})
    target_link_options(containa_test PRIVATE ${SANITIZER_LINK_FLAGS})
endif()

target_link_libraries(containa_test dl doctest)

# Register every doctest TEST_CASE as its own ctest test.
#
# Three things this buys, each of which hid a real bug:
#
#   1. ctest runs something at all. There was no add_test() anywhere, so `ctest` reported
#      "No tests were found!!!" and exited 0, and CI called that a pass.
#   2. A crash no longer hides the rest. Run as one binary, a SIGSEGV in btree_multimap aborted the
#      whole run and took 186 test cases down as "skipped" -- among them the ones that would have
#      caught a dense_map use-after-free and a concurrent_skiplist infinite loop. One test case per
#      ctest test means one crash fails one test.
#   3. A hang fails instead of wedging the machine. concurrent_skiplist spun forever on
#      insert-after-erase; doctest has no per-test timeout, ctest does.
include(${CMAKE_CURRENT_SOURCE_DIR}/../doctest/scripts/cmake/doctest.cmake)
doctest_discover_tests(containa_test
    TEST_PREFIX "containa::"
    PROPERTIES TIMEOUT 300
)

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
target_link_libraries(containa_test dl doctest)

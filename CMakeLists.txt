cmake_minimum_required(VERSION 3.20)

project(Containa
    VERSION 1.0.0
    DESCRIPTION "High-performance C++ container library with vectra"
    LANGUAGES CXX
)

# Set C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler flags
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -DDEBUG")

# Default build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/)

# Header-only library target
add_library(vectra INTERFACE)
target_include_directories(vectra INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>
)
target_compile_features(vectra INTERFACE cxx_std_20)

# Add compiler warnings
if(MSVC)
    target_compile_options(vectra INTERFACE /W4)
else()
    target_compile_options(vectra INTERFACE 
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow
    )
endif()


# Add submodules
add_subdirectory(doctest)
add_subdirectory(tests)
add_subdirectory(bench)

# Custom targets
add_custom_target(test_run
    COMMAND ${CMAKE_CTEST_COMMAND} --verbose
    DEPENDS vectra_test
    COMMENT "Running tests"
)

add_custom_target(benchmark_run
    COMMAND vector_bench
    DEPENDS vector_bench
    COMMENT "Running benchmarks"
)

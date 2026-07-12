cmake_minimum_required(VERSION 3.20)

project(Containa
    VERSION 1.0.0
    DESCRIPTION "High-performance C++ container library"
    LANGUAGES CXX
)

# project() only sets PROJECT_IS_TOP_LEVEL from CMake 3.21, and we still claim 3.20. On 3.20 the
# variable is simply empty, which is not an error and not a warning -- it silently reads as false, so
# every `if(PROJECT_IS_TOP_LEVEL)` below is skipped and the two options that default to it come out
# OFF. A 3.20 user configuring this project directly would get no tests and no benchmarks, and the
# `containa_test` target the README tells them to build would not exist.
#
# So compute it. Same definition project() uses.
if(NOT DEFINED PROJECT_IS_TOP_LEVEL)
    if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
        set(PROJECT_IS_TOP_LEVEL ON)
    else()
        set(PROJECT_IS_TOP_LEVEL OFF)
    endif()
endif()

if(PROJECT_IS_TOP_LEVEL)
    # Set C++ standard (C++23 for std::flat_map)
    set(CMAKE_CXX_STANDARD 23)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
endif()

# Default to Clang compiler
if(NOT DEFINED CMAKE_C_COMPILER AND NOT DEFINED CMAKE_CXX_COMPILER)
    find_program(CLANG_C_COMPILER clang)
    find_program(CLANG_CXX_COMPILER clang++)
    if(CLANG_C_COMPILER AND CLANG_CXX_COMPILER)
        set(CMAKE_C_COMPILER ${CLANG_C_COMPILER} CACHE STRING "C compiler" FORCE)
        set(CMAKE_CXX_COMPILER ${CLANG_CXX_COMPILER} CACHE STRING "C++ compiler" FORCE)
    endif()
endif()

# Compiler flags
# Use -march=native to enable AVX2/AVX512 SIMD optimizations
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG -march=native")
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -DDEBUG")

if(PROJECT_IS_TOP_LEVEL)
    # Use libc++ with Clang for std::flat_map support (requires libc++ installed)
    option(USE_LIBCXX_DEFAULT "Use libc++ by default with Clang" ON)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND USE_LIBCXX_DEFAULT)
        add_compile_options(-stdlib=libc++)
        add_link_options(-stdlib=libc++)
    endif()
endif()

# Default build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

if(PROJECT_IS_TOP_LEVEL)
    # libc++ option (for Clang)
    option(USE_LIBCXX "Use libc++ instead of libstdc++" OFF)
    if(USE_LIBCXX)
        if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
            add_compile_options(-stdlib=libc++)
            add_link_options(-stdlib=libc++ -lc++abi)
            message(STATUS "Using libc++ standard library")
        else()
            message(WARNING "USE_LIBCXX is only supported with Clang compiler")
        endif()
    endif()
endif()

# Sanitizer options
option(ENABLE_ASAN "Enable AddressSanitizer for memory leak and error detection" OFF)
option(ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)

# Abseil benchmark option
option(ENABLE_ABSL_BENCH "Enable Abseil btree benchmark comparison" OFF)

# Configure sanitizer flags
if(ENABLE_ASAN)
    list(APPEND SANITIZER_FLAGS -fsanitize=address -fno-omit-frame-pointer)
    list(APPEND SANITIZER_LINK_FLAGS -fsanitize=address)
    message(STATUS "AddressSanitizer enabled")
endif()

if(ENABLE_UBSAN)
    list(APPEND SANITIZER_FLAGS -fsanitize=undefined -fno-omit-frame-pointer)
    list(APPEND SANITIZER_LINK_FLAGS -fsanitize=undefined)
    message(STATUS "UndefinedBehaviorSanitizer enabled")
endif()

# Abseil configuration (optional, for benchmarks)
if(ENABLE_ABSL_BENCH)
    include(FetchContent)

    # Set Abseil build options
    set(ABSL_PROPAGATE_CXX_STD ON)
    set(ABSL_BUILD_TESTING OFF)
    set(BUILD_TESTING OFF)

    FetchContent_Declare(
        abseil-cpp
        GIT_REPOSITORY https://github.com/abseil/abseil-cpp.git
        GIT_TAG        20240722.0
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(abseil-cpp)
    message(STATUS "Abseil enabled for benchmark comparison")
endif()

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/)

option(CONTAINA_BUILD_TESTS "Build Containa tests" ${PROJECT_IS_TOP_LEVEL})
option(CONTAINA_BUILD_BENCHMARKS "Build Containa benchmarks" ${PROJECT_IS_TOP_LEVEL})

# Header-only library target
add_library(containa INTERFACE)

target_include_directories(containa INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>
)
target_compile_features(containa INTERFACE cxx_std_20)

if(PROJECT_IS_TOP_LEVEL)
    if(MSVC)
        target_compile_options(containa INTERFACE /W4 /WX)
    else()
        target_compile_options(containa INTERFACE
            -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
        )
    endif()
endif()


if(CONTAINA_BUILD_TESTS OR CONTAINA_BUILD_BENCHMARKS)
    add_subdirectory(doctest)
endif()
if(CONTAINA_BUILD_TESTS)
    add_subdirectory(tests)
endif()
if(CONTAINA_BUILD_BENCHMARKS)
    add_subdirectory(bench)
endif()

if(CONTAINA_BUILD_TESTS)
    add_custom_target(test_run
        COMMAND ${CMAKE_CTEST_COMMAND} --verbose
        DEPENDS containa_test
        COMMENT "Running tests"
    )
endif()

if(CONTAINA_BUILD_BENCHMARKS)
    add_custom_target(benchmark_run
        COMMAND btree_bench
        DEPENDS btree_bench
        COMMENT "Running benchmarks"
    )
endif()

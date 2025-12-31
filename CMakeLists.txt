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
# Use -march=native to enable AVX2/AVX512 SIMD optimizations
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG -march=native")
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -DDEBUG")

# Default build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

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

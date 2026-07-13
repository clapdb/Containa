cmake_minimum_required(VERSION 3.20)

project(Containa
    VERSION 1.0.0
    DESCRIPTION "High-performance C++ container library"
    LANGUAGES CXX
)

# project() only sets PROJECT_IS_TOP_LEVEL from CMake 3.21, and we still claim 3.20. On 3.20 the
# variable is simply empty, which is not an error and not a warning -- it silently reads as false, so
# every top-level-only block below is skipped and the two options that default to it come out OFF. A
# 3.20 user configuring this project directly would get no tests and no benchmarks, and the
# `containa_test` target the README tells them to build would not exist.
#
# So compute it -- but into our *own* variable, and unconditionally.
#
# Not `if(NOT DEFINED PROJECT_IS_TOP_LEVEL)`: on 3.20, where the polyfill is the whole point, project()
# defines nothing and add_subdirectory() inherits the parent's variables. A parent that carries its own
# 3.20 polyfill therefore hands us its own PROJECT_IS_TOP_LEVEL=ON, the guard sees it as DEFINED, skips
# the computation, and vendored containa concludes it is the top-level project -- re-enabling its tests
# and benchmarks and overwriting the parent's flags and C++ standard. That is precisely the class of bug
# the rest of this file exists to prevent, reintroduced by the guard meant to prevent it.
#
# And into CONTAINA_IS_TOP_LEVEL rather than PROJECT_IS_TOP_LEVEL, so the value cannot leak the other way
# either: our subdirectories (doctest, tests, bench) get their own from project(), or from their own
# polyfill, and never ours.
if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    set(CONTAINA_IS_TOP_LEVEL ON)
else()
    set(CONTAINA_IS_TOP_LEVEL OFF)
endif()

if(CONTAINA_IS_TOP_LEVEL)
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

# Compiler flags. Two separate things had to be fixed here, and they are orthogonal -- keep both.
#
# 1. Top-level only. A parent that vendors us with add_subdirectory() gets these applied to our directory
#    scope while its own targets keep its flags, so we would quietly compile a piece of their build for
#    whatever CPU the build machine happens to have. That is a portability bug on its own (a binary built
#    on an AVX-512 machine will not run without it), and with CONTAINA_BUILD_MODULES it is fatal: a BMI
#    records the target features it was compiled with, and clang refuses to load one into a TU that lacks
#    them --
#
#      error: precompiled file '...bmi' was compiled with the target feature '+adx'
#             but the current translation unit is not
#
#    so `import containa;` fails for a consumer that did nothing wrong.
#
# 2. An option, not a decree. Even at top level, a plain `set()` overwrites whatever the build asked for:
#    a -DCMAKE_CXX_FLAGS_RELEASE=... on the command line is silently discarded, and a -march passed
#    through CMAKE_CXX_FLAGS loses too, because CMake appends the per-config flags *after* the generic
#    ones and clang takes the last -march. That is what made the -march matrix in our own CI a no-op --
#    both entries compiled -march=native, and the job tested one path twice while claiming to test two.
if(CONTAINA_IS_TOP_LEVEL)
    option(CONTAINA_MARCH_NATIVE "Add -march=native to Release builds" ON)
    if(CONTAINA_MARCH_NATIVE)
        set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG -march=native")
    else()
        set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")
    endif()
    set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -DDEBUG")
endif()

if(CONTAINA_IS_TOP_LEVEL)
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

if(CONTAINA_IS_TOP_LEVEL)
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

option(CONTAINA_BUILD_MODULES "Build Containa C++ module interface" OFF)
option(CONTAINA_BUILD_TESTS "Build Containa tests" ${CONTAINA_IS_TOP_LEVEL})
option(CONTAINA_BUILD_BENCHMARKS "Build Containa benchmarks" ${CONTAINA_IS_TOP_LEVEL})

if(CONTAINA_BUILD_MODULES)
    # CXX_MODULES file sets landed in CMake 3.28. The project as a whole still supports 3.20, so say so
    # here rather than letting a 3.20-3.27 user hit an opaque configure error the moment they turn
    # modules on.
    if(CMAKE_VERSION VERSION_LESS 3.28)
        message(FATAL_ERROR "CONTAINA_BUILD_MODULES requires CMake 3.28 or newer (CXX_MODULES file sets); found ${CMAKE_VERSION}")
    endif()
    add_library(containa STATIC)
    target_sources(containa
        PUBLIC
            FILE_SET CXX_MODULES
            BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/container
            FILES container/containa.cppm
    )
else()
    add_library(containa INTERFACE)
endif()

target_include_directories(containa INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>
)
# A BMI can only be loaded by a TU compiled at the same standard as the BMI. Not "at least" -- the same:
#
#   error: language dialect differs 'C++20', expected 'C++23'
#   error: failed to read compiled module: Bad file data
#
# So for the module build, the standard is not a preference of ours that consumers may exceed. It is a
# term of the contract, and it has to be *one* number that the module target and every importer share.
#
# CONTAINA_CXX_STANDARD is that number. It defaults to the parent's global choice -- the common case, and
# why none of this was visible for so long -- and a parent that configures the standard per-target can
# pin us to match. Two things then have to happen with it, and getting only one of them is how both
# halves of this bug were written:
#
#   set_target_properties(CXX_STANDARD)  compiles containa.cppm, and its BMI, at that standard.
#   target_compile_features(PUBLIC)      raises every consumer that links us to that standard.
#
# With only the first, a parent asking for 23 gets a gnu++23 BMI and a gnu++20 app: 'C++23', expected
# 'C++20'. With only the second, a parent whose *own* target is C++23 while the global is unset gets a
# gnu++20 BMI and a gnu++23 app: the mirror image. Both were reproduced with a minimal Ninja parent; both
# are the same failure. The feature must name the standard we actually selected, never a hard-coded floor.
#
# PUBLIC and not INTERFACE, because there is a real target to compile: INTERFACE alone would leave
# containa.cppm itself at whatever the parent picked, and under a parent at CMAKE_CXX_STANDARD=17 that
# fails generation outright -- "target 'containa' has module sources but only cxx_std_17".
#
# The header-only path keeps a plain cxx_std_20 floor, and that is not an oversight. An INTERFACE library
# has no objects and no BMI, so there is nothing for a consumer to be out of step *with*; 20 is a genuine
# minimum (the README and CLAUDE.md promise a C++20 compiler) and a C++23 consumer is free to exceed it.
# The pinning exists only because modules make the standard observable at link time.
if(DEFINED CMAKE_CXX_STANDARD)
    set(_containa_standard_default ${CMAKE_CXX_STANDARD})
else()
    set(_containa_standard_default 20)
endif()
set(CONTAINA_CXX_STANDARD "${_containa_standard_default}" CACHE STRING
    "C++ standard to compile containa (and its module) with; importers are raised to match")

# Raising importers is only half of it. `target_compile_features` publishes a *minimum*, and CMake never
# lowers a target that already asks for more: a parent that leaves CMAKE_CXX_STANDARD unset but puts
# CXX_STANDARD 23 on the target that imports us gets a C++20 BMI and a C++23 importing TU, which is the
# same hard failure in the other direction. CONTAINA_CXX_STANDARD *can* fix that -- but only if the
# consumer already knows to set it, and a consumer who knew would not have hit the bug.
#
# CMake has no way to propagate an exact dialect, so we check instead. This runs deferred to the end of
# the top-level directory, by which point every target exists, and compares the effective standard of
# each target that links us against the one the BMI was built with. A module consumer must link its
# provider directly (that is how it gets the BMI into its modmap at all), so LINK_LIBRARIES is the
# complete set -- there is no transitive importer to miss.
#
# It fails the configure rather than compiling something that cannot link. The one thing it must not do
# is guess: it never edits the parent's targets, only reports what has to change.
function(_containa_collect_targets dir out_var)
    get_property(_targets DIRECTORY "${dir}" PROPERTY BUILDSYSTEM_TARGETS)
    get_property(_children DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)
    foreach(_child IN LISTS _children)
        _containa_collect_targets("${_child}" _grandchildren)
        list(APPEND _targets ${_grandchildren})
    endforeach()
    set(${out_var} "${_targets}" PARENT_SCOPE)
endfunction()

# The standard `target` will actually compile at: the CXX_STANDARD property, or the highest cxx_std_NN
# among its compile features, whichever is greater. Empty if the target picks neither and just inherits
# the floor we publish -- which is by definition a match.
function(_containa_effective_standard target out_var)
    set(_std "")
    get_target_property(_prop ${target} CXX_STANDARD)
    if(_prop)
        set(_std ${_prop})
    endif()
    foreach(_prop_name COMPILE_FEATURES INTERFACE_COMPILE_FEATURES)
        get_target_property(_features ${target} ${_prop_name})
        if(_features)
            foreach(_feature IN LISTS _features)
                if(_feature MATCHES "^cxx_std_([0-9]+)$")
                    if("${_std}" STREQUAL "" OR CMAKE_MATCH_1 GREATER _std)
                        set(_std ${CMAKE_MATCH_1})
                    endif()
                endif()
            endforeach()
        endif()
    endforeach()
    set(${out_var} "${_std}" PARENT_SCOPE)
endfunction()

function(_containa_assert_importers_match_dialect)
    _containa_collect_targets("${CMAKE_SOURCE_DIR}" _all_targets)

    set(_mismatched "")
    foreach(_target IN LISTS _all_targets)
        if(_target STREQUAL "containa")
            continue()
        endif()
        get_target_property(_type ${_target} TYPE)
        if(_type STREQUAL "INTERFACE_LIBRARY" OR _type STREQUAL "UTILITY")
            continue()   # no translation units of its own, so no BMI to be out of step with
        endif()
        get_target_property(_libs ${_target} LINK_LIBRARIES)
        if(NOT _libs OR NOT "containa" IN_LIST _libs)
            continue()
        endif()

        _containa_effective_standard(${_target} _std)
        if(NOT "${_std}" STREQUAL "" AND NOT _std EQUAL CONTAINA_CXX_STANDARD)
            list(APPEND _mismatched "${_target} (C++${_std})")
        endif()
    endforeach()

    if(_mismatched)
        list(JOIN _mismatched "\n    " _report)
        message(FATAL_ERROR
            "containa's module was built at C++${CONTAINA_CXX_STANDARD}, but these targets import it at a "
            "different standard:\n\n"
            "    ${_report}\n\n"
            "A BMI can only be loaded by a translation unit compiled at the *same* standard -- not merely "
            "at least it -- so this would fail as \"language dialect differs\" at build time.\n\n"
            "Build the module at the standard they use:\n\n"
            "    -DCONTAINA_CXX_STANDARD=<their standard>\n\n"
            "or set CMAKE_CXX_STANDARD globally, which containa follows by default.")
    endif()
endfunction()

if(CONTAINA_BUILD_MODULES)
    if(CONTAINA_CXX_STANDARD LESS 20)
        message(FATAL_ERROR
            "CONTAINA_CXX_STANDARD is ${CONTAINA_CXX_STANDARD}; containa requires C++20 or later.")
    endif()
    set_target_properties(containa PROPERTIES CXX_STANDARD ${CONTAINA_CXX_STANDARD})
    target_compile_features(containa PUBLIC cxx_std_${CONTAINA_CXX_STANDARD})
    cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL _containa_assert_importers_match_dialect)
else()
    target_compile_features(containa INTERFACE cxx_std_20)
endif()
# A BMI records the language options it was compiled with, so the module and everyone who imports it must
# agree on whether GNU extensions are on. CMake gives us no way to discover what our importers will pick,
# so the best we can do is be *deterministic* and let the parent pin it.
#
# The old `if(DEFINED CMAKE_CXX_EXTENSIONS)` guard was not enough. It exists because writing
# `set_target_properties(containa PROPERTIES CXX_EXTENSIONS ${CMAKE_CXX_EXTENSIONS})` with the variable
# unset expands to a property with no value and CMake aborts outright -- so that part must stay. But a
# parent can set CXX_EXTENSIONS on its *own* target without ever defining the global variable, and then
# the guard silently does nothing: containa.cppm compiles -std=gnu++20 (CMake's default) while the
# importer compiles -std=c++20. Reproduced exactly that.
#
# clang 22 happens to accept the mismatched BMI today, so this is not a live break -- but it is an
# unintended divergence in the one thing a BMI is most sensitive to, and "the current compiler tolerates
# it" is not a property worth depending on. CONTAINA_CXX_EXTENSIONS makes it explicit: it follows the
# parent's global choice when there is one, falls back to CMake's own default when there is not, and a
# parent that configures extensions per-target can pin us to match.
if(DEFINED CMAKE_CXX_EXTENSIONS)
    set(_containa_extensions_default ${CMAKE_CXX_EXTENSIONS})
else()
    set(_containa_extensions_default ON)   # CMake's default, i.e. what an unconfigured importer gets
endif()
option(CONTAINA_CXX_EXTENSIONS "Compile containa (and its module) with GNU extensions; must match importers"
       ${_containa_extensions_default})
set_target_properties(containa PROPERTIES CXX_EXTENSIONS ${CONTAINA_CXX_EXTENSIONS})

if(CONTAINA_IS_TOP_LEVEL)
    if(MSVC)
        target_compile_options(containa INTERFACE /W4 /WX)
    else()
        target_compile_options(containa INTERFACE
            -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
        )
    endif()
endif()


# ctest registers nothing without enable_testing(), and `ctest` with nothing registered exits 0 -- which
# is how CI stayed green for years while never running a single test. See tests/CMakeLists.txt.
#
# Only when we are the project being tested, though: a parent that vendors us wants our headers, not our
# test suite, and CONTAINA_BUILD_TESTS defaults to CONTAINA_IS_TOP_LEVEL.
if(CONTAINA_BUILD_TESTS)
    enable_testing()
endif()

# Tests only. The benchmarks link nanobench (and pthread, and optionally abseil/arena) and not one bench
# source includes doctest, so pulling it in for CONTAINA_BUILD_BENCHMARKS=ON only made a benchmark-only
# configure depend on a test framework it never uses -- and fail outright when the doctest submodule is
# absent, which is exactly the configuration the new split options exist to make possible.
if(CONTAINA_BUILD_TESTS)
    add_subdirectory(doctest)
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

# ============================================================
# CMakeLists.txt — Shrtn server
# ============================================================
#
# Top-level build file for server/. Pulls in the three untouched
# upstream submodules (falconhttp/, minidb/, ratethrottle/) via
# add_subdirectory, then builds our own executable against them.
#
# NOTE: falconhttp/, minidb/, and ratethrottle/ each vendor their own
# independent copies of shared submodules (VectorPro, HashMapPro,
# ArenaPro, CachePro, ...) and compile any non-header-only ones
# directly into their own static library. If linking below fails with
# "multiple definition of ...", that's why — see the project notes on
# this. Try building first; only chase that fix if the linker actually
# complains.
# ============================================================

cmake_minimum_required(VERSION 3.20)

project(
    Shrtn
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# ============================================================================
# Submodules (untouched, upstream — see .gitmodules)
# ============================================================================

# All three submodules' examples/tests/benchmarks/regression suites
# default ON in their own CMakeLists.txt (ratethrottle/'s included --
# same option names). Turn them off before add_subdirectory() picks up
# their defaults; we don't build any of that here.
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(BUILD_REGRESSION OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

add_subdirectory(third_party/falconhttp)
add_subdirectory(third_party/minidb)
add_subdirectory(third_party/ratethrottle)

# ============================================================================
# Shrtn server executable
# ============================================================================

add_executable(
    ${CMAKE_PROJECT_NAME}_server
    src/main.cpp
)

target_include_directories(
    ${CMAKE_PROJECT_NAME}_server
    PRIVATE
        include
        src
)

# Target names are the bare project names (FalconHTTP, MiniDB,
# ThrottlePro) — all three submodules' CMakeLists.txt only namespace-
# alias under NAMESPACE:: for their *installed* package export; in-tree
# via add_subdirectory, the plain names apply. Note ratethrottle/'s repo
# name and its CMake project()/target name differ (ThrottlePro, not
# RateThrottle) — same JsonPro-vs-JsonParser mismatch its own
# CMakeLists.txt comments warn about.
target_link_libraries(
    ${CMAKE_PROJECT_NAME}_server
    PRIVATE
        FalconHTTP
        MiniDB
        ThrottlePro
)

# ============================================================================
# Tests
# ============================================================================

# Named SHRTN_BUILD_TESTS, not BUILD_TESTS: the latter is force-set OFF
# above for the submodules.
option(SHRTN_BUILD_TESTS "Build the Shrtn test suite" ON)

if(SHRTN_BUILD_TESTS)
    file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp"
    )

    add_executable(shrtn_tests ${TEST_SOURCES})

    target_include_directories(
        shrtn_tests
        PRIVATE
            include
            src
            ${CMAKE_CURRENT_SOURCE_DIR}/tests
    )

    target_link_libraries(
        shrtn_tests
        PRIVATE
            FalconHTTP
            MiniDB
    )
endif()

# ============================================================================
# Fuzz harnesses (built by ClusterFuzzLite; see ../.clusterfuzzlite/)
# ============================================================================

# OFF by default: the harnesses need a fuzzing engine to link against, which
# ClusterFuzzLite provides through $LIB_FUZZING_ENGINE. Every
# fuzz/fuzz_<name>.cpp becomes its own executable, no edit needed here.
option(SHRTN_BUILD_FUZZERS "Build the libFuzzer harnesses" OFF)

if(SHRTN_BUILD_FUZZERS)
    file(GLOB FUZZ_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/fuzz/fuzz_*.cpp"
    )

    foreach(fuzz_source ${FUZZ_SOURCES})
        get_filename_component(fuzz_name ${fuzz_source} NAME_WE)

        add_executable(${fuzz_name} ${fuzz_source})

        target_include_directories(${fuzz_name} PRIVATE include src)

        target_link_libraries(
            ${fuzz_name}
            PRIVATE
                FalconHTTP
                MiniDB
                $ENV{LIB_FUZZING_ENGINE}
        )
    endforeach()
endif()

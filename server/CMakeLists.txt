# ============================================================
# CMakeLists.txt — Shrtn server
# ============================================================
#
# Top-level build file for server/. Pulls in the two untouched
# upstream submodules (falconhttp/, minidb/) via add_subdirectory,
# then builds our own executable against them.
#
# NOTE: both falconhttp/ and minidb/ vendor their own independent
# copies of shared submodules (VectorPro, HashMapPro, ArenaPro, ...)
# and compile any non-header-only ones directly into their own static
# library. If linking below fails with "multiple definition of ...",
# that's why — see the project notes on this. Try building first;
# only chase that fix if the linker actually complains.
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

# Both examples/tests/benchmarks/regression suites default ON in each
# submodule's own CMakeLists.txt. We don't need any of that here, so
# turn them off before add_subdirectory() picks up their defaults.
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(BUILD_REGRESSION OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

add_subdirectory(third_party/falconhttp)
add_subdirectory(third_party/minidb)

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

# Target names are the bare project names (FalconHTTP, MiniDB) — both
# submodules' CMakeLists.txt only namespace-alias under NAMESPACE::
# for their *installed* package export; in-tree via add_subdirectory,
# the plain names apply.
target_link_libraries(
    ${CMAKE_PROJECT_NAME}_server
    PRIVATE
        FalconHTTP
        MiniDB
)

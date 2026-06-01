cmake_minimum_required(VERSION 3.22)
project(ECS_System VERSION 2.0.0 LANGUAGES CXX)

# ── Standards ──────────────────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Build type default ─────────────────────────────────────────────────────────
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

# ── Options ────────────────────────────────────────────────────────────────────
option(ECS_BUILD_TESTS      "Build test suite"   ON)
option(ECS_BUILD_BENCHMARKS "Build benchmarks"   ON)
option(ECS_BUILD_EXAMPLES   "Build examples"     ON)
option(ECS_ENABLE_ASAN      "Enable AddressSanitizer" OFF)
option(ECS_ENABLE_UBSAN     "Enable UBSanitizer"      OFF)

# ── Warning flags ──────────────────────────────────────────────────────────────
add_library(ecs_warnings INTERFACE)
if(MSVC)
  target_compile_options(ecs_warnings INTERFACE /W4 /WX)
else()
  target_compile_options(ecs_warnings INTERFACE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow
    -Wno-unused-parameter
  )
endif()

# ── Sanitizer flags ────────────────────────────────────────────────────────────
add_library(ecs_sanitizers INTERFACE)
if(ECS_ENABLE_ASAN)
  target_compile_options(ecs_sanitizers INTERFACE -fsanitize=address -fno-omit-frame-pointer)
  target_link_options   (ecs_sanitizers INTERFACE -fsanitize=address)
endif()
if(ECS_ENABLE_UBSAN)
  target_compile_options(ecs_sanitizers INTERFACE -fsanitize=undefined)
  target_link_options   (ecs_sanitizers INTERFACE -fsanitize=undefined)
endif()

# ── Header-only library target ─────────────────────────────────────────────────
add_library(ecs INTERFACE)
target_include_directories(ecs INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
target_link_libraries(ecs INTERFACE ecs_warnings ecs_sanitizers)
add_library(ECS::ecs ALIAS ecs)

# ── Tests ──────────────────────────────────────────────────────────────────────
if(ECS_BUILD_TESTS)
  enable_testing()

  add_executable(test_ecs tests/test_ecs.cpp)
  target_link_libraries(test_ecs PRIVATE ECS::ecs)
  target_include_directories(test_ecs PRIVATE tests)

  add_test(NAME ecs_unit_tests COMMAND test_ecs)
  set_tests_properties(ecs_unit_tests PROPERTIES
    TIMEOUT 30
    PASS_REGULAR_EXPRESSION "\\[doctest\\] test cases: .* \\| 0 failed"
  )
endif()

# ── Benchmarks ─────────────────────────────────────────────────────────────────
if(ECS_BUILD_BENCHMARKS)
  add_executable(bench_ecs benchmarks/bench_ecs.cpp)
  target_link_libraries(bench_ecs PRIVATE ECS::ecs)
  target_compile_options(bench_ecs PRIVATE -O3 -march=native)
endif()

# ── Examples ───────────────────────────────────────────────────────────────────
if(ECS_BUILD_EXAMPLES)
  add_executable(example_game src/main.cpp)
  target_link_libraries(example_game PRIVATE ECS::ecs)
endif()

# ── Install rules ──────────────────────────────────────────────────────────────
include(GNUInstallDirs)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(TARGETS ecs EXPORT ECSTargets)
install(EXPORT ECSTargets
  FILE      ECSTargets.cmake
  NAMESPACE ECS::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ECS
)

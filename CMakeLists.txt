# This file is part of tbc-arena-lab.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License along
# with this program; if not, write to the Free Software Foundation, Inc.,
# 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

cmake_minimum_required(VERSION 3.20)
project(tbc_arena_lab CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Default build: -O2 with debug info (this is also the bench config).
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

add_compile_options(-Wall -Wextra -Werror)

# Embedded commit id for trace headers; stale until reconfigure, see docs/m0_status.md.
execute_process(
  COMMAND git describe --always --dirty
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  OUTPUT_VARIABLE SIM_COMMIT_ID
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET)
if(NOT SIM_COMMIT_ID)
  set(SIM_COMMIT_ID "unknown")
endif()
add_compile_definitions(SIM_COMMIT="${SIM_COMMIT_ID}")

add_library(arena_core STATIC
  sim/core/scenario.cpp
  sim/core/trace.cpp)
target_include_directories(arena_core PUBLIC ${CMAKE_SOURCE_DIR})

add_library(arena_mechanics STATIC
  sim/mechanics/attack_table.cpp
  sim/mechanics/ruleset.cpp
  sim/mechanics/dist.cpp
  sim/mechanics/replay.cpp)
target_link_libraries(arena_mechanics PUBLIC arena_core)

add_executable(arena_run tools/arena_run.cpp)
add_executable(arena_replay tools/arena_replay.cpp)
add_executable(arena_dist tools/arena_dist.cpp)
add_executable(arena_bench tools/arena_bench.cpp)
foreach(tool arena_run arena_replay arena_dist arena_bench)
  target_link_libraries(${tool} PRIVATE arena_mechanics)
endforeach()

enable_testing()
add_executable(arena_tests
  sim/tests/test_main.cpp
  sim/tests/test_rng.cpp
  sim/tests/test_event_order.cpp
  sim/tests/test_attack_table.cpp
  sim/tests/test_damage_rage.cpp
  sim/tests/test_determinism.cpp
  sim/tests/test_replay_golden.cpp
  sim/tests/test_dist_self.cpp)
target_link_libraries(arena_tests PRIVATE arena_mechanics)
target_include_directories(arena_tests PRIVATE ${CMAKE_SOURCE_DIR}/third_party)
target_compile_definitions(arena_tests PRIVATE REPO_ROOT="${CMAKE_SOURCE_DIR}")
add_test(NAME arena_tests COMMAND arena_tests)

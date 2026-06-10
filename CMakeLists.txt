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
# Static archives are linked into the arena_env shared library.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

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
  sim/mechanics/abilities.cpp
  sim/mechanics/ruleset.cpp
  sim/mechanics/dist.cpp
  sim/mechanics/replay.cpp)
target_link_libraries(arena_mechanics PUBLIC arena_core)

# Differential-harness oracle model (cited port of cmangos-tbc math, floats
# allowed: diagnostic-only, never linked into sim/core or sim/mechanics).
add_library(arena_oracle STATIC
  sim/oracle/cmangos_model.cpp)
target_link_libraries(arena_oracle PUBLIC arena_mechanics)

add_executable(arena_run tools/arena_run.cpp)
add_executable(arena_replay tools/arena_replay.cpp)
add_executable(arena_dist tools/arena_dist.cpp)
add_executable(arena_bench tools/arena_bench.cpp)
foreach(tool arena_run arena_replay arena_dist arena_bench)
  target_link_libraries(${tool} PRIVATE arena_mechanics)
endforeach()
add_executable(arena_diff tools/arena_diff.cpp)
target_link_libraries(arena_diff PRIVATE arena_oracle)

# C ABI for external drivers (Python ctypes wrapper in python/tbc_arena).
add_library(arena_env SHARED sim/env/arena_c_api.cpp)
target_link_libraries(arena_env PRIVATE arena_mechanics)

enable_testing()
add_executable(arena_tests
  sim/tests/test_main.cpp
  sim/tests/test_rng.cpp
  sim/tests/test_event_order.cpp
  sim/tests/test_attack_table.cpp
  sim/tests/test_damage_rage.cpp
  sim/tests/test_determinism.cpp
  sim/tests/test_replay_golden.cpp
  sim/tests/test_dist_self.cpp
  sim/tests/test_oracle_model.cpp
  sim/tests/test_parry_haste.cpp
  sim/tests/test_abilities.cpp
  sim/tests/test_observation.cpp
  sim/tests/test_engine.cpp)
target_link_libraries(arena_tests PRIVATE arena_oracle)
target_include_directories(arena_tests PRIVATE ${CMAKE_SOURCE_DIR}/third_party)
target_compile_definitions(arena_tests PRIVATE REPO_ROOT="${CMAKE_SOURCE_DIR}")
add_test(NAME arena_tests COMMAND arena_tests)

find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
  add_test(NAME python_env
           COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/python/tests/test_env.py)
  set_tests_properties(python_env PROPERTIES
    ENVIRONMENT "TBC_ARENA_LIB=$<TARGET_FILE:arena_env>;TBC_ARENA_ROOT=${CMAKE_SOURCE_DIR}")
  # M5 training harness verification (numpy-only; skipped if numpy missing).
  execute_process(COMMAND ${Python3_EXECUTABLE} -c "import numpy"
                  RESULT_VARIABLE HAVE_NUMPY OUTPUT_QUIET ERROR_QUIET)
  if(HAVE_NUMPY EQUAL 0)
    add_test(NAME python_train
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/python/train/test_train.py)
    set_tests_properties(python_train PROPERTIES
      ENVIRONMENT "TBC_ARENA_LIB=$<TARGET_FILE:arena_env>;TBC_ARENA_ROOT=${CMAKE_SOURCE_DIR}")
  endif()
endif()

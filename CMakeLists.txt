cmake_minimum_required(VERSION 3.16)
project(mundo_x_native LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(mundo_x native/src/main.cpp)
target_include_directories(mundo_x PRIVATE native/include)

add_executable(mundo_x_phase native/src/phase_runner.cpp)
target_include_directories(mundo_x_phase PRIVATE native/include)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(mundo_x PRIVATE -O3 -Wall -Wextra -Wpedantic)
  target_compile_options(mundo_x_phase PRIVATE -O3 -Wall -Wextra -Wpedantic)
endif()

include(CTest)
if (BUILD_TESTING)
  add_executable(test_phase_core tests/test_phase_core.cpp)
  target_include_directories(test_phase_core PRIVATE native/include)
  add_test(NAME phase_core COMMAND test_phase_core)
  add_executable(test_state_transfer tests/test_state_transfer.cpp)
  target_include_directories(test_state_transfer PRIVATE native/include)
  add_test(NAME state_transfer COMMAND test_state_transfer)
  add_test(NAME existing_hello COMMAND mundo_x hello)
  add_test(NAME existing_teleport COMMAND mundo_x teleport)
  add_test(NAME existing_magic COMMAND mundo_x magic-witness)
endif()

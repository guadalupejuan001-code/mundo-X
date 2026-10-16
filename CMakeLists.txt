cmake_minimum_required(VERSION 3.16)
project(mundo_x_native LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(mundo_x native/src/main.cpp)
target_include_directories(mundo_x PRIVATE native/include)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(mundo_x PRIVATE -O3 -Wall -Wextra -Wpedantic)
endif()

include(CTest)
if (BUILD_TESTING)
  add_executable(mundo_x_native_tests tests/native_tests.cpp)
  target_include_directories(mundo_x_native_tests PRIVATE native/include)
  if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(mundo_x_native_tests PRIVATE -Wall -Wextra -Wpedantic)
  endif()
  add_test(NAME native_core_and_xasm COMMAND mundo_x_native_tests)

  foreach(example hello bell ghz4 magic_t clifford_t)
    add_test(NAME xasm_${example}
      COMMAND mundo_x run "${CMAKE_CURRENT_SOURCE_DIR}/examples/${example}.x")
  endforeach()

  file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/xasm_reduced_magic.x"
    "backend reduced\nqubits 2\nh 0\nt 0\ncx 0 1\ncz 0 1\ns 1\nh 1\nassert fidelity\nassert norm\nshow\nbytes\n")
  add_test(NAME xasm_reduced_magic
    COMMAND mundo_x run "${CMAKE_CURRENT_BINARY_DIR}/xasm_reduced_magic.x")

  file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/xasm_failed_assert.x"
    "qubits 1\nassert one\n")
  add_test(NAME xasm_failed_assert_exit
    COMMAND mundo_x run "${CMAKE_CURRENT_BINARY_DIR}/xasm_failed_assert.x")
  set_tests_properties(xasm_failed_assert_exit PROPERTIES WILL_FAIL TRUE)

  file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/xasm_invalid_program.x"
    "qubits 1\nh -1\n")
  add_test(NAME xasm_invalid_program_exit
    COMMAND mundo_x run "${CMAKE_CURRENT_BINARY_DIR}/xasm_invalid_program.x")
  set_tests_properties(xasm_invalid_program_exit PROPERTIES WILL_FAIL TRUE)
endif()

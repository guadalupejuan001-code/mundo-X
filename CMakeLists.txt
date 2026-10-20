cmake_minimum_required(VERSION 3.16)
cmake_policy(SET CMP0091 NEW)
project(mundo_x_native LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)

# The repository-owned C bridge and EngineSession can be linked by hosts and
# shared libraries without keeping another copy of the quantum core source.
add_library(mundo_x_graphics_core STATIC
  native/src/graphics_core_bridge.cpp native/src/infinite_bridge.cpp)
target_include_directories(mundo_x_graphics_core PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/native/include")
target_compile_features(mundo_x_graphics_core PUBLIC cxx_std_17)
target_link_libraries(mundo_x_graphics_core PUBLIC Threads::Threads)
set_target_properties(mundo_x_graphics_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(mundo_x_graphics_core PRIVATE -O3 -Wall -Wextra -Wpedantic)
endif()

add_executable(mundo_x native/src/main.cpp)
target_include_directories(mundo_x PRIVATE native/include)
if (MSVC)
  # Keep the downloadable Windows CLI independent of a separate VC runtime install.
  set_property(TARGET mundo_x PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

add_executable(mundo_x_phase native/src/phase_runner.cpp)
target_include_directories(mundo_x_phase PRIVATE native/include)
if (MSVC)
  set_property(TARGET mundo_x_phase PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

add_executable(mundo_x_vm native/src/basis_vm_main.cpp)
target_include_directories(mundo_x_vm PRIVATE
  native/include android-experiments/native/include)
if (MSVC)
  set_property(TARGET mundo_x_vm PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# Rules-based infinite domain; shared live sessions also serve PC and graphics.
# Existing finite runners retain their own register contracts.
add_executable(mundo_x_infinite native/src/infinite_runner.cpp)
target_include_directories(mundo_x_infinite PRIVATE native/include)
if (MSVC)
  set_property(TARGET mundo_x_infinite PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(mundo_x PRIVATE -O3 -Wall -Wextra -Wpedantic)
  target_compile_options(mundo_x_phase PRIVATE -O3 -Wall -Wextra -Wpedantic)
  target_compile_options(mundo_x_vm PRIVATE -O3 -Wall -Wextra -Wpedantic)
  target_compile_options(mundo_x_infinite PRIVATE -O3 -Wall -Wextra -Wpedantic)
endif()

include(CTest)
add_subdirectory(pc-android)
if (BUILD_TESTING)
  add_test(NAME basis_vm_cli_selftest COMMAND mundo_x_vm --self-test)
  set_tests_properties(basis_vm_cli_selftest PROPERTIES TIMEOUT 120)
  add_executable(test_phase_core tests/test_phase_core.cpp)
  target_include_directories(test_phase_core PRIVATE native/include)
  if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(test_phase_core PRIVATE -O3 -Wall -Wextra -Wpedantic)
  endif()
  add_test(NAME phase_core COMMAND test_phase_core)
  add_executable(test_state_transfer tests/test_state_transfer.cpp)
  target_include_directories(test_state_transfer PRIVATE native/include)
  if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(test_state_transfer PRIVATE -O3 -Wall -Wextra -Wpedantic)
  endif()
  add_test(NAME state_transfer COMMAND test_state_transfer)
  add_test(NAME existing_hello COMMAND mundo_x hello)
  add_test(NAME existing_teleport COMMAND mundo_x teleport)
  add_test(NAME existing_magic COMMAND mundo_x magic-witness)
  set(native_tests physics_world reduced_optimization boundaries dense_precision factorized_core factorized_storage factorized_structure micro_lab compact_measurement xasm_measurement visual_scene xasm_visual integrated_runtime engine_session lab_circuit lab_memory lab_protocols core_performance core_60 core_1000 xasm_60 infinite_address infinite_core infinite_tensor infinite_session infinite_bridge ether_core ether_frame ether_fusion ether_rules quantum_algorithms)
  foreach(test_name IN LISTS native_tests)
    add_executable(test_${test_name} native/tests/${test_name}.cpp)
    target_include_directories(test_${test_name} PRIVATE native/include)
    if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
      target_compile_options(test_${test_name} PRIVATE -O3 -Wall -Wextra -Wpedantic)
    endif()
    if (MSVC AND test_name STREQUAL "reduced_optimization")
      # MSVC 19.51 Release fails this reference comparison with inlining and
      # passes unchanged with /Ob0. Keep the workaround on the test executable;
      # mundo_x retains its normal optimization and all tolerances stay intact.
      target_compile_options(test_${test_name} PRIVATE /Ob0)
    endif()
    add_test(NAME ${test_name} COMMAND test_${test_name})
    set_tests_properties(${test_name} PROPERTIES TIMEOUT 30)
  endforeach()
  target_link_libraries(test_engine_session PRIVATE mundo_x_graphics_core)
  target_link_libraries(test_infinite_session PRIVATE Threads::Threads)
  target_link_libraries(test_ether_fusion PRIVATE Threads::Threads)
  add_executable(test_ether_batch native/tests/ether_batch.cpp)
  target_include_directories(test_ether_batch PRIVATE native/include)
  target_link_libraries(test_ether_batch PRIVATE Threads::Threads)
  add_test(NAME ether_batch COMMAND test_ether_batch)
  set_tests_properties(ether_batch PROPERTIES TIMEOUT 60)
  add_executable(ether_stress native/tests/ether_stress.cpp)
  target_include_directories(ether_stress PRIVATE native/include)
  target_link_libraries(ether_stress PRIVATE Threads::Threads)
  target_link_libraries(test_infinite_bridge PRIVATE mundo_x_graphics_core)
  target_link_libraries(test_core_60 PRIVATE mundo_x_graphics_core)
  target_link_libraries(test_core_1000 PRIVATE mundo_x_graphics_core)
  add_test(NAME ether_example
    COMMAND mundo_x_infinite "${CMAKE_CURRENT_SOURCE_DIR}/examples/ether_fusion.xi")
  set_tests_properties(ether_example PROPERTIES TIMEOUT 30)
  set_tests_properties(core_60 core_1000 xasm_60 PROPERTIES TIMEOUT 120)
  add_executable(core_1000_benchmark native/tests/core_1000_benchmark.cpp)
  target_include_directories(core_1000_benchmark PRIVATE native/include)
  target_link_libraries(core_1000_benchmark PRIVATE mundo_x_graphics_core)
  if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(core_1000_benchmark PRIVATE -O3 -Wall -Wextra -Wpedantic)
  endif()
  target_link_libraries(test_lab_memory PRIVATE Threads::Threads)
  add_executable(infinite_scale native/tests/infinite_scale.cpp)
  target_include_directories(infinite_scale PRIVATE native/include)
  if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(infinite_scale PRIVATE -O3 -Wall -Wextra -Wpedantic)
  endif()
  foreach(example zero bell)
    add_test(NAME infinite_example_${example}
      COMMAND mundo_x_infinite "${CMAKE_CURRENT_SOURCE_DIR}/examples/infinite_${example}.xi")
    set_tests_properties(infinite_example_${example} PROPERTIES TIMEOUT 30)
  endforeach()
  find_package(Python3 QUIET COMPONENTS Interpreter)
  if (Python3_Interpreter_FOUND)
    add_test(NAME infinite_cli_contract
      COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/validate_infinite_cli.py"
        --binary "$<TARGET_FILE:mundo_x_infinite>")
    set_tests_properties(infinite_cli_contract PROPERTIES TIMEOUT 60)
  endif()
  # Original Android VM/Ecos experiments restored into the unified APK.
  foreach(experiment_test basis_vm quantum_echo sparse_observable)
    add_executable(test_${experiment_test}
      android-experiments/native/tests/${experiment_test}_test.cpp)
    target_include_directories(test_${experiment_test} PRIVATE
      native/include android-experiments/native/include)
    if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
      target_compile_options(test_${experiment_test} PRIVATE -O3 -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME ${experiment_test} COMMAND test_${experiment_test})
    set_tests_properties(${experiment_test} PROPERTIES TIMEOUT 120)
  endforeach()
  if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(native_exams native_original_exams factorized_diversity)
    foreach(exam_name IN LISTS native_exams)
      add_executable(${exam_name} native/tests/${exam_name}.cpp)
      target_include_directories(${exam_name} PRIVATE native/include)
      if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${exam_name} PRIVATE -O3 -Wall -Wextra -Wpedantic)
      endif()
      add_test(NAME ${exam_name} COMMAND ${exam_name})
      set_tests_properties(${exam_name} PROPERTIES TIMEOUT 30)
    endforeach()
    add_executable(factorized_stream_benchmark native/tests/factorized_stream_benchmark.cpp)
    target_include_directories(factorized_stream_benchmark PRIVATE native/include)
    if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
      target_compile_options(factorized_stream_benchmark PRIVATE -O3 -Wall -Wextra -Wpedantic)
    endif()
  endif()
  add_executable(mundo_x_native_tests tests/native_tests.cpp)
  target_include_directories(mundo_x_native_tests PRIVATE native/include)
  if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(mundo_x_native_tests PRIVATE -Wall -Wextra -Wpedantic)
  endif()
  add_test(NAME native_core_and_xasm COMMAND mundo_x_native_tests)

  foreach(example hello bell ghz4 magic_t clifford_t ghz1000 t60)
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

  add_executable(mundo_x_mxlang_tests tests/mxlang_tests.cpp)
  target_include_directories(mundo_x_mxlang_tests PRIVATE native/include)
  if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(mundo_x_mxlang_tests PRIVATE -Wall -Wextra -Wpedantic)
  endif()
  add_test(NAME mxlang_compiler COMMAND mundo_x_mxlang_tests)
  foreach(example mx_bell mx_magic mx_layers)
    add_test(NAME ${example}
      COMMAND mundo_x run-mx "${CMAKE_CURRENT_SOURCE_DIR}/examples/${example}.mx")
  endforeach()

  file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/mx_failed_assert.mx"
    "qubits 1;\nassert one;\n")
  file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/mx_invalid_program.mx"
    "qubits 1;\nh(0);\n")
  add_test(NAME mxlang_cli_contract COMMAND "${CMAKE_COMMAND}"
    "-DPROGRAM=$<TARGET_FILE:mundo_x>"
    "-DASSERT_SOURCE=${CMAKE_CURRENT_BINARY_DIR}/mx_failed_assert.mx"
    "-DINVALID_SOURCE=${CMAKE_CURRENT_BINARY_DIR}/mx_invalid_program.mx"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_mx_cli.cmake")
  set_tests_properties(mxlang_compiler mxlang_cli_contract PROPERTIES TIMEOUT 20)
endif()

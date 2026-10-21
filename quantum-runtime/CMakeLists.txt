cmake_minimum_required(VERSION 3.16)
project(mundo_x_quantum_only LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)
enable_testing()

function(mx_quantum_target name source)
  add_executable(\${name} "\${CMAKE_CURRENT_LIST_DIR}/../\${source}")
  target_include_directories(\${name} PRIVATE
    "\${CMAKE_CURRENT_LIST_DIR}/../native/include")
  target_compile_features(\${name} PRIVATE cxx_std_17)
  if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(\${name} PRIVATE -O3 -Wall -Wextra -Wpedantic)
  endif()
endfunction()

# Runtime: only Ether/InfiniteCore/CompactCore + Inverse Memory + XI parser.
# This CMake project deliberately does not add pc-android, Graphics, Solar,
# physics, Android/JNI or renderer targets.
mx_quantum_target(mundo_x_quantum native/src/quantum_runner.cpp)

mx_quantum_target(test_inverse_memory native/tests/inverse_memory.cpp)
mx_quantum_target(test_quantum_runtime native/tests/quantum_runtime.cpp)
mx_quantum_target(test_infinite_extreme native/tests/infinite_extreme.cpp)
mx_quantum_target(test_quantum_algorithms native/tests/quantum_algorithms.cpp)
mx_quantum_target(test_ether_rules native/tests/ether_rules.cpp)
mx_quantum_target(test_infinite_session native/tests/infinite_session.cpp)
target_link_libraries(test_infinite_session PRIVATE Threads::Threads)

add_test(NAME inverse_memory COMMAND test_inverse_memory)
add_test(NAME quantum_runtime COMMAND test_quantum_runtime)
add_test(NAME infinite_extreme COMMAND test_infinite_extreme)
add_test(NAME quantum_algorithms COMMAND test_quantum_algorithms)
add_test(NAME ether_rules COMMAND test_ether_rules)
add_test(NAME infinite_session COMMAND test_infinite_session)

set_tests_properties(ether_rules PROPERTIES TIMEOUT 180)
set_tests_properties(infinite_extreme quantum_algorithms PROPERTIES TIMEOUT 120)

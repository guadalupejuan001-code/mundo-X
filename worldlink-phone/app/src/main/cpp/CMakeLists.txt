cmake_minimum_required(VERSION 3.22.1)
project(worldlink_mil_ether_bridge LANGUAGES CXX)

# Only canonical Mundo X C++ headers from this same repository revision.
get_filename_component(MUNDO_X_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../../.." ABSOLUTE)
add_library(worldlink_quantum SHARED worldlink_quantum_jni.cpp)
target_include_directories(worldlink_quantum PRIVATE "${MUNDO_X_ROOT}/native/include")
target_compile_features(worldlink_quantum PRIVATE cxx_std_17)
target_compile_options(worldlink_quantum PRIVATE -O2 -Wall -Wextra)
target_link_options(worldlink_quantum PRIVATE "-Wl,-z,max-page-size=16384")

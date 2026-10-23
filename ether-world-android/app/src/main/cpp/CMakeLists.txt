cmake_minimum_required(VERSION 3.22.1)
project(ether_world_mundo_x LANGUAGES CXX)

get_filename_component(MUNDO_X_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../../.." ABSOLUTE)
add_library(ether_world SHARED ether_world_jni.cpp)
target_include_directories(ether_world PRIVATE "${MUNDO_X_ROOT}/native/include")
target_compile_features(ether_world PRIVATE cxx_std_17)
target_compile_options(ether_world PRIVATE -O3 -Wall -Wextra -Wpedantic)
target_link_options(ether_world PRIVATE "-Wl,-z,max-page-size=16384")

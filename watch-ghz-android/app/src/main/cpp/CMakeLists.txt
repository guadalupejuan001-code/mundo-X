cmake_minimum_required(VERSION 3.22.1)
project(mundox_ghz LANGUAGES CXX)
add_library(mundox_ghz SHARED ghz_jni.cpp)
target_include_directories(mundox_ghz PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../../../../../native/include"
    "${CMAKE_CURRENT_LIST_DIR}/../../../../native")
target_compile_features(mundox_ghz PRIVATE cxx_std_17)
target_link_options(mundox_ghz PRIVATE "-Wl,-z,max-page-size=16384")

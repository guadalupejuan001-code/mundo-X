cmake_minimum_required(VERSION 3.22.1)
project(mundox_android LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

get_filename_component(MUNDO_X_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../.." ABSOLUTE)

add_library(mundox SHARED native-lib.cpp lab-jni.cpp)

target_include_directories(
    mundox
    PRIVATE
    "${MUNDO_X_ROOT}/native/include"
)

target_compile_options(mundox PRIVATE -O3 -Wall -Wextra -Wpedantic)

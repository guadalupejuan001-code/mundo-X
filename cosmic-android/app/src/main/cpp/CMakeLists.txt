cmake_minimum_required(VERSION 3.22.1)
project(cosmic_android LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
get_filename_component(MUNDO_X_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../../.." ABSOLUTE)

# Compile the existing original sources directly. No PC/x86/Windows source belongs here.
add_library(mx_graphics SHARED
    "${MUNDO_X_ROOT}/native/src/graphics_core_bridge.cpp"
    "${MUNDO_X_ROOT}/native/src/infinite_bridge.cpp"
    "${MUNDO_X_ROOT}/graphics-android/app/src/main/cpp/graphics_jni.cpp")
add_library(mundox SHARED
    "${MUNDO_X_ROOT}/app/src/main/cpp/native-lib.cpp"
    "${MUNDO_X_ROOT}/app/src/main/cpp/lab-jni.cpp"
    "${MUNDO_X_ROOT}/app/src/main/cpp/micro-jni.cpp")
foreach(cosmic_target mx_graphics mundox)
    target_include_directories(${cosmic_target} PRIVATE "${MUNDO_X_ROOT}/native/include")
    target_compile_options(${cosmic_target} PRIVATE -O3 -Wall -Wextra -Wpedantic)
    target_link_options(${cosmic_target} PRIVATE "-Wl,-z,max-page-size=16384")
endforeach()
add_subdirectory("${MUNDO_X_ROOT}/android-experiments" "${CMAKE_CURRENT_BINARY_DIR}/experiments")

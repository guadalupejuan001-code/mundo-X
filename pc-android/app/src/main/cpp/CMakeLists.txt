cmake_minimum_required(VERSION 3.22.1)
project(cosmic_pc LANGUAGES CXX)
get_filename_component(MUNDO_X_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../../.." ABSOLUTE)
# One shared bridge owns both the PC device session handle and the canonical
# GLES frontend. Do not compile another bridge into cosmic_pc.
add_library(mx_graphics SHARED
    "${MUNDO_X_ROOT}/native/src/graphics_core_bridge.cpp"
    "${MUNDO_X_ROOT}/native/src/infinite_bridge.cpp"
    "${MUNDO_X_ROOT}/graphics-android/app/src/main/cpp/graphics_jni.cpp")
target_include_directories(mx_graphics PRIVATE "${MUNDO_X_ROOT}/native/include")
target_compile_features(mx_graphics PRIVATE cxx_std_17)
target_compile_options(mx_graphics PRIVATE -O3 -Wall -Wextra)
target_link_options(mx_graphics PRIVATE -Wl,-z,max-page-size=16384)
add_library(cosmic_pc SHARED pc_jni.cpp)
target_compile_features(cosmic_pc PRIVATE cxx_std_17)
target_include_directories(cosmic_pc PRIVATE ../../../../native ../../../../../native/include)
target_compile_options(cosmic_pc PRIVATE -Wall -Wextra -Wpedantic)
target_link_options(cosmic_pc PRIVATE -Wl,-z,max-page-size=16384)
target_link_libraries(cosmic_pc PRIVATE mx_graphics)

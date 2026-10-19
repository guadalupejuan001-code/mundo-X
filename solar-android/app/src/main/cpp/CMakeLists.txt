cmake_minimum_required(VERSION 3.22.1)
project(cosmic_x_solar LANGUAGES CXX)

get_filename_component(MUNDO_X_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../../.." ABSOLUTE)

# The exact existing COSMIC X bridge is built into the new APK. No external engine.
add_library(mx_graphics SHARED
    "${MUNDO_X_ROOT}/native/src/graphics_core_bridge.cpp"
    "${MUNDO_X_ROOT}/native/src/infinite_bridge.cpp"
    "${MUNDO_X_ROOT}/graphics-android/app/src/main/cpp/graphics_jni.cpp")
target_include_directories(mx_graphics PRIVATE "${MUNDO_X_ROOT}/native/include")
target_compile_features(mx_graphics PRIVATE cxx_std_17)
target_compile_options(mx_graphics PRIVATE -O3 -Wall -Wextra)
target_link_options(mx_graphics PRIVATE "-Wl,-z,max-page-size=16384")

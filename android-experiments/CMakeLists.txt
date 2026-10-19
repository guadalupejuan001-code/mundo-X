# Restored Android experiments compile against the one canonical Mundo X core.
get_filename_component(MUNDO_X_EXPERIMENT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
add_library(mundox_basis SHARED src/main/cpp/basis-vm-jni.cpp)
add_library(mundox_echoes SHARED src/main/cpp/native-echo.cpp)
foreach(experiment_target mundox_basis mundox_echoes)
    target_compile_features(${experiment_target} PRIVATE cxx_std_17)
    target_include_directories(${experiment_target} PRIVATE
        "${MUNDO_X_EXPERIMENT_ROOT}/native/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/native/include")
    target_compile_options(${experiment_target} PRIVATE -O3 -Wall -Wextra -Wpedantic)
    target_link_options(${experiment_target} PRIVATE
        "-Wl,-z,max-page-size=16384" "-Wl,-z,common-page-size=16384")
endforeach()

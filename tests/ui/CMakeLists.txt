cmake_minimum_required(VERSION 3.20)
project(sightline_ui_tests LANGUAGES C CXX)

# Build separately from the board targets, using the same LVGL source revision
# with software rendering and no display, GPIO or radio dependencies.
set(SIGHTLINE_LVGL_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../.toolchains/lvgl-src"
    CACHE PATH "Path to the device-matching LVGL source tree")
if(NOT EXISTS "${SIGHTLINE_LVGL_SOURCE_DIR}/lvgl.h")
  message(FATAL_ERROR "Set SIGHTLINE_LVGL_SOURCE_DIR to the device-matching LVGL sources")
endif()
set(LV_BUILD_CONF_PATH "${CMAKE_CURRENT_LIST_DIR}/lv_conf.h" CACHE PATH "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory("${SIGHTLINE_LVGL_SOURCE_DIR}" lvgl)
get_filename_component(SIGHTLINE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/include")
file(CREATE_LINK "${SIGHTLINE_LVGL_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/include/lvgl" SYMBOLIC)
add_executable(sightline_ui_tests ui_shell_tests.cpp
  "${SIGHTLINE_ROOT}/src/ui_shell.cpp" "${SIGHTLINE_ROOT}/assets/zeta_k230_logo.c")
target_include_directories(sightline_ui_tests PRIVATE
  "${CMAKE_CURRENT_BINARY_DIR}/include" "${SIGHTLINE_ROOT}"
  "${SIGHTLINE_ROOT}/shared/identity/include")
target_link_libraries(sightline_ui_tests PRIVATE lvgl m)
target_compile_features(sightline_ui_tests PRIVATE cxx_std_17)
# Assertions are the test checks, including in Release configurations.
target_compile_options(sightline_ui_tests PRIVATE -UNDEBUG)
enable_testing()
add_test(NAME sightline_ui_tests COMMAND sightline_ui_tests)

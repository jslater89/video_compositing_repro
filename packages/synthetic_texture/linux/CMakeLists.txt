cmake_minimum_required(VERSION 3.10)
set(PROJECT_NAME "synthetic_texture")
project(${PROJECT_NAME} LANGUAGES CXX)

set(PLUGIN_NAME "synthetic_texture_plugin")

add_library(${PLUGIN_NAME} SHARED
  "synthetic_texture_plugin.cc"
  "yuv_pattern_texture.cc"
)
apply_standard_settings(${PLUGIN_NAME})
set_target_properties(${PLUGIN_NAME} PROPERTIES
  CXX_VISIBILITY_PRESET hidden)
target_compile_definitions(${PLUGIN_NAME} PRIVATE FLUTTER_PLUGIN_IMPL)
target_include_directories(${PLUGIN_NAME} INTERFACE
  "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(${PLUGIN_NAME} PRIVATE flutter)
target_link_libraries(${PLUGIN_NAME} PRIVATE PkgConfig::GTK)

find_package(PkgConfig REQUIRED)
pkg_check_modules(EPOXY REQUIRED epoxy)
target_include_directories(${PLUGIN_NAME} PRIVATE ${EPOXY_INCLUDE_DIRS})
target_link_libraries(${PLUGIN_NAME} PRIVATE ${EPOXY_LIBRARIES})

set(synthetic_texture_bundled_libraries
  ""
  PARENT_SCOPE
)

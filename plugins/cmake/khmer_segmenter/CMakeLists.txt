cmake_minimum_required(VERSION 3.16)

project(xsql_khmer_segmenter_plugin LANGUAGES C CXX)

if (NOT DEFINED XSQL_ROOT)
  message(FATAL_ERROR "XSQL_ROOT must be set to the XSQL source root.")
endif()
if (NOT DEFINED PLUGIN_SOURCE)
  message(FATAL_ERROR "PLUGIN_SOURCE must point to the khmer_segmenter repository.")
endif()

set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

get_filename_component(PLUGIN_SOURCE_ABS "${PLUGIN_SOURCE}" ABSOLUTE)

set(KHMER_PORT_SRC "${PLUGIN_SOURCE_ABS}/port/c/src")
set(KHMER_PORT_INCLUDE "${PLUGIN_SOURCE_ABS}/port/c/include")
set(KHMER_COMMON_INCLUDE "${PLUGIN_SOURCE_ABS}/port/common")

set(SEGMENTER_SOURCES
  "${KHMER_PORT_SRC}/khmer_segmenter.c"
  "${KHMER_PORT_SRC}/khmer_rule_engine.c"
  "${KHMER_PORT_SRC}/khmer_normalization.c"
)

foreach(source_file IN LISTS SEGMENTER_SOURCES)
  if (NOT EXISTS "${source_file}")
    message(FATAL_ERROR "Missing khmer_segmenter source: ${source_file}")
  endif()
endforeach()

add_library(khmer_segmenter_plugin SHARED
  ${SEGMENTER_SOURCES}
  xsql_khmer_plugin.cpp
)

target_include_directories(khmer_segmenter_plugin PRIVATE
  "${KHMER_PORT_INCLUDE}"
  "${KHMER_COMMON_INCLUDE}"
  "${XSQL_ROOT}/core/include"
)

target_compile_definitions(khmer_segmenter_plugin PRIVATE
  XSQL_KHMER_PLUGIN_SOURCE="${PLUGIN_SOURCE_ABS}"
)

set_target_properties(khmer_segmenter_plugin PROPERTIES
  OUTPUT_NAME "khmer_segmenter"
)

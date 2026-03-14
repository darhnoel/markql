cmake_minimum_required(VERSION 3.16)

project(markql_number_to_khmer_plugin LANGUAGES CXX)

if (NOT DEFINED MARKQL_ROOT)
  message(FATAL_ERROR "MARKQL_ROOT must be set to the MARKQL source root.")
endif()
if (NOT DEFINED PLUGIN_SOURCE)
  message(FATAL_ERROR "PLUGIN_SOURCE must point to the MARKQL source root.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

get_filename_component(PLUGIN_SOURCE_ABS "${PLUGIN_SOURCE}" ABSOLUTE)

set(KHMER_NUMBER_SOURCES
  "${PLUGIN_SOURCE_ABS}/core/src/khmer_number.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/markql_number_to_khmer_plugin.cpp"
)

foreach(source_file IN LISTS KHMER_NUMBER_SOURCES)
  if (NOT EXISTS "${source_file}")
    message(FATAL_ERROR "Missing Khmer number source: ${source_file}")
  endif()
endforeach()

add_library(number_to_khmer_plugin SHARED
  ${KHMER_NUMBER_SOURCES}
)

target_include_directories(number_to_khmer_plugin PRIVATE
  "${MARKQL_ROOT}/core/include"
)

set_target_properties(number_to_khmer_plugin PROPERTIES
  OUTPUT_NAME "number_to_khmer"
)

add_executable(xsql-agent
  src/main.cpp
  src/sha256.cpp
)
set_target_properties(xsql-agent PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

target_include_directories(xsql-agent
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Resolve cpp-httplib header path (vendored first, then FetchContent fallback).
set(_xsql_agent_httplib_hints
  ${CMAKE_SOURCE_DIR}/third_party/cpp-httplib
  ${CMAKE_SOURCE_DIR}/third_party/cpp-httplib/include
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/cpp-httplib
)
find_path(XSQL_AGENT_HTTPLIB_INCLUDE_DIR
  NAMES httplib.h
  PATHS ${_xsql_agent_httplib_hints}
)

# Resolve nlohmann/json (package, vendored include path, then FetchContent).
find_package(nlohmann_json CONFIG QUIET)

set(_xsql_agent_needs_fetch FALSE)
if (NOT XSQL_AGENT_HTTPLIB_INCLUDE_DIR)
  set(_xsql_agent_needs_fetch TRUE)
endif()
if (NOT nlohmann_json_FOUND)
  if (EXISTS "${CMAKE_SOURCE_DIR}/third_party/nlohmann_json/single_include/nlohmann/json.hpp")
    add_library(xsql_agent_nlohmann_json INTERFACE)
    target_include_directories(xsql_agent_nlohmann_json
      INTERFACE ${CMAKE_SOURCE_DIR}/third_party/nlohmann_json/single_include
    )
    add_library(nlohmann_json::nlohmann_json ALIAS xsql_agent_nlohmann_json)
  else()
    set(_xsql_agent_needs_fetch TRUE)
  endif()
endif()

if (_xsql_agent_needs_fetch)
  if (NOT XSQL_AGENT_FETCH_DEPS)
    message(FATAL_ERROR "xsql-agent dependencies missing. Set XSQL_AGENT_FETCH_DEPS=ON or vendor third_party/cpp-httplib and third_party/nlohmann_json.")
  endif()

  include(FetchContent)

  if (NOT XSQL_AGENT_HTTPLIB_INCLUDE_DIR)
    FetchContent_Declare(cpp_httplib
      URL https://github.com/yhirose/cpp-httplib/archive/refs/tags/v0.16.3.tar.gz
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(cpp_httplib)
    set(XSQL_AGENT_HTTPLIB_INCLUDE_DIR "${cpp_httplib_SOURCE_DIR}")
  endif()

  if (NOT TARGET nlohmann_json::nlohmann_json)
    FetchContent_Declare(nlohmann_json
      URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(nlohmann_json)
  endif()
endif()

if (NOT XSQL_AGENT_HTTPLIB_INCLUDE_DIR)
  message(FATAL_ERROR "Could not locate httplib.h for xsql-agent")
endif()

find_package(Threads REQUIRED)

target_include_directories(xsql-agent
  PRIVATE
    ${XSQL_AGENT_HTTPLIB_INCLUDE_DIR}
)

target_link_libraries(xsql-agent
  PRIVATE
    xsql_core
    nlohmann_json::nlohmann_json
    Threads::Threads
)

target_compile_features(xsql-agent PRIVATE cxx_std_20)

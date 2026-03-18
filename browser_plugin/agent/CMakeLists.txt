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

find_package(httplib CONFIG QUIET)
find_package(nlohmann_json CONFIG QUIET)

# Resolve cpp-httplib (package first, then vendored/system include path, then FetchContent).
if (TARGET httplib::httplib)
  set(XSQL_AGENT_HTTPLIB_TARGET httplib::httplib)
else()
  set(_xsql_agent_httplib_hints
    ${CMAKE_SOURCE_DIR}/third_party/cpp-httplib
    ${CMAKE_SOURCE_DIR}/third_party/cpp-httplib/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/cpp-httplib
  )
  find_path(XSQL_AGENT_HTTPLIB_INCLUDE_DIR
    NAMES httplib.h
    PATHS ${_xsql_agent_httplib_hints}
  )
  if (XSQL_AGENT_HTTPLIB_INCLUDE_DIR)
    add_library(xsql_agent_httplib INTERFACE)
    target_include_directories(xsql_agent_httplib
      INTERFACE ${XSQL_AGENT_HTTPLIB_INCLUDE_DIR}
    )
    add_library(httplib::httplib ALIAS xsql_agent_httplib)
    set(XSQL_AGENT_HTTPLIB_TARGET httplib::httplib)
  endif()
endif()

# Resolve nlohmann/json (package first, then vendored/system include path, then FetchContent).
set(_xsql_agent_needs_fetch FALSE)
if (NOT XSQL_AGENT_HTTPLIB_TARGET)
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
    find_path(XSQL_AGENT_NLOHMANN_JSON_INCLUDE_DIR
      NAMES nlohmann/json.hpp
    )
    if (XSQL_AGENT_NLOHMANN_JSON_INCLUDE_DIR)
      add_library(xsql_agent_nlohmann_json INTERFACE)
      target_include_directories(xsql_agent_nlohmann_json
        INTERFACE ${XSQL_AGENT_NLOHMANN_JSON_INCLUDE_DIR}
      )
      add_library(nlohmann_json::nlohmann_json ALIAS xsql_agent_nlohmann_json)
    else()
      set(_xsql_agent_needs_fetch TRUE)
    endif()
  endif()
endif()

if (_xsql_agent_needs_fetch)
  if (NOT MARKQL_AGENT_FETCH_DEPS)
    message(FATAL_ERROR "xsql-agent dependencies missing. Set MARKQL_AGENT_FETCH_DEPS=ON or provide httplib and nlohmann_json via vcpkg, system packages, or third_party/.")
  else()
    include(FetchContent)

    if (NOT XSQL_AGENT_HTTPLIB_TARGET)
      FetchContent_Declare(cpp_httplib
        GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
        GIT_TAG v0.16.3
      )
      FetchContent_MakeAvailable(cpp_httplib)
      add_library(xsql_agent_httplib INTERFACE)
      target_include_directories(xsql_agent_httplib
        INTERFACE ${cpp_httplib_SOURCE_DIR}
      )
      add_library(httplib::httplib ALIAS xsql_agent_httplib)
      set(XSQL_AGENT_HTTPLIB_TARGET httplib::httplib)
    endif()

    if (NOT TARGET nlohmann_json::nlohmann_json)
      FetchContent_Declare(nlohmann_json
        URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
      )
      FetchContent_MakeAvailable(nlohmann_json)
    endif()
  endif()
endif()

if (NOT XSQL_AGENT_HTTPLIB_TARGET)
  message(FATAL_ERROR "Could not locate cpp-httplib for xsql-agent")
endif()

find_package(Threads REQUIRED)

target_link_libraries(xsql-agent
  PRIVATE
    markql_core
    ${XSQL_AGENT_HTTPLIB_TARGET}
    nlohmann_json::nlohmann_json
    Threads::Threads
)

target_compile_features(xsql-agent PRIVATE cxx_std_20)

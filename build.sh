#!/bin/bash

set -euo pipefail
VCPKG_ROOT=~/vcpkg
: "${XSQL_BUILD_AGENT:=ON}"
: "${XSQL_AGENT_FETCH_DEPS:=ON}"

cmake_args=(
  -S .
  -B build
  -DXSQL_WITH_LIBXML2=ON
  -DXSQL_WITH_CURL=ON
  -DXSQL_WITH_ARROW=ON
  "-DXSQL_BUILD_AGENT=${XSQL_BUILD_AGENT}"
  "-DXSQL_AGENT_FETCH_DEPS=${XSQL_AGENT_FETCH_DEPS}"
)

if [[ -z "${VCPKG_ROOT:-}" && -d "${HOME}/vcpkg" ]]; then
  VCPKG_ROOT="${HOME}/vcpkg"
fi

if [[ -n "${VCPKG_ROOT:-}" ]]; then
  cmake_args+=("-DCMAKE_TOOLCHAIN_FILE=${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
  cmake_args+=("-DVCPKG_TARGET_TRIPLET=x64-linux")
  cmake_args+=("-DCMAKE_PREFIX_PATH=${VCPKG_ROOT}/installed/x64-linux")
  cmake_args+=("-DCURL_DIR=${VCPKG_ROOT}/installed/x64-linux/share/curl")
  cmake_args+=("-DOPENSSL_ROOT_DIR=${VCPKG_ROOT}/installed/x64-linux")
  cmake_args+=("-DArrow_DIR=${VCPKG_ROOT}/installed/x64-linux/share/arrow")
  cmake_args+=("-DParquet_DIR=${VCPKG_ROOT}/installed/x64-linux/share/parquet")
fi

cmake "${cmake_args[@]}"
cmake --build build

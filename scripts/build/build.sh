#!/usr/bin/env bash

set -euo pipefail

if [[ -n "${XSQL_BUILD_AGENT:-}" && -z "${MARKQL_BUILD_AGENT:-}" ]]; then
  MARKQL_BUILD_AGENT="${XSQL_BUILD_AGENT}"
fi
if [[ -n "${XSQL_AGENT_FETCH_DEPS:-}" && -z "${MARKQL_AGENT_FETCH_DEPS:-}" ]]; then
  MARKQL_AGENT_FETCH_DEPS="${XSQL_AGENT_FETCH_DEPS}"
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

cd "${REPO_ROOT}"

: "${MARKQL_BUILD_AGENT:=ON}"
: "${MARKQL_AGENT_FETCH_DEPS:=ON}"
VCPKG_ROOT="${VCPKG_ROOT:-$HOME/vcpkg}"
VCPKG_LOCAL_INSTALLED_DIR="${REPO_ROOT}/vcpkg_installed"
VCPKG_LOCAL_BUILDTREES_DIR="${REPO_ROOT}/.vcpkg/buildtrees"
VCPKG_LOCAL_PACKAGES_DIR="${REPO_ROOT}/.vcpkg/packages"
VCPKG_DOWNLOADS_DIR="${HOME}/vcpkg/downloads"

detect_vcpkg_triplet() {
  local os_name arch_name
  os_name="$(uname -s)"
  arch_name="$(uname -m)"

  case "${arch_name}" in
    x86_64|amd64) arch_name="x64" ;;
    aarch64|arm64) arch_name="arm64" ;;
    *)
      echo "Unsupported architecture for automatic vcpkg triplet detection: ${arch_name}" >&2
      echo "Set VCPKG_TARGET_TRIPLET and VCPKG_HOST_TRIPLET explicitly." >&2
      return 1
      ;;
  esac

  case "${os_name}" in
    Linux) echo "${arch_name}-linux" ;;
    Darwin) echo "${arch_name}-osx" ;;
    MINGW*|MSYS*|CYGWIN*|Windows_NT) echo "${arch_name}-windows" ;;
    *)
      echo "Unsupported operating system for automatic vcpkg triplet detection: ${os_name}" >&2
      echo "Set VCPKG_TARGET_TRIPLET and VCPKG_HOST_TRIPLET explicitly." >&2
      return 1
      ;;
  esac
}

detect_parallel_jobs() {
  if command -v nproc >/dev/null 2>&1; then
    nproc
    return
  fi
  if command -v sysctl >/dev/null 2>&1; then
    sysctl -n hw.logicalcpu
    return
  fi
  if command -v getconf >/dev/null 2>&1; then
    getconf _NPROCESSORS_ONLN
    return
  fi
  echo 1
}

reset_stale_cmake_cache_for_vcpkg_manifest() {
  local cache_file manifest_dir_line manifest_mode_line manifest_install_line
  cache_file="${REPO_ROOT}/build/CMakeCache.txt"

  if [[ ! -f "${cache_file}" ]]; then
    return
  fi

  manifest_dir_line="$(grep '^VCPKG_MANIFEST_DIR:PATH=' "${cache_file}" || true)"
  manifest_mode_line="$(grep '^VCPKG_MANIFEST_MODE:BOOL=' "${cache_file}" || true)"
  manifest_install_line="$(grep '^VCPKG_MANIFEST_INSTALL:BOOL=' "${cache_file}" || true)"

  if [[ "${manifest_mode_line}" != 'VCPKG_MANIFEST_MODE:BOOL=ON' ]] || \
     [[ "${manifest_install_line}" != 'VCPKG_MANIFEST_INSTALL:BOOL=ON' ]] || \
     [[ "${manifest_dir_line}" != "VCPKG_MANIFEST_DIR:PATH=${REPO_ROOT}" ]]; then
    echo "Resetting stale CMake cache to reconfigure with the repo vcpkg manifest..."
    rm -rf "${REPO_ROOT}/build/CMakeCache.txt" "${REPO_ROOT}/build/CMakeFiles"
  fi
}

VCPKG_TARGET_TRIPLET="${VCPKG_TARGET_TRIPLET:-$(detect_vcpkg_triplet)}"
VCPKG_HOST_TRIPLET="${VCPKG_HOST_TRIPLET:-$VCPKG_TARGET_TRIPLET}"
VCPKG_TARGET_INSTALLED_DIR="${VCPKG_LOCAL_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}"
VCPKG_FALLBACK_INSTALLED_DIR="${VCPKG_ROOT}/installed/${VCPKG_TARGET_TRIPLET}"
BUILD_JOBS="${BUILD_JOBS:-$(detect_parallel_jobs)}"
CMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE:-MinSizeRel}"
MARKQL_OPTIMIZE_FOR_SIZE="${MARKQL_OPTIMIZE_FOR_SIZE:-ON}"
MARKQL_STRIP_BINARIES="${MARKQL_STRIP_BINARIES:-ON}"

cmake_args=(
  -S "${REPO_ROOT}"
  -B "${REPO_ROOT}/build"
  "-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}"
  -DMARKQL_WITH_LIBXML2=ON
  -DMARKQL_WITH_CURL=ON
  -DMARKQL_WITH_ARROW=ON
  "-DMARKQL_BUILD_AGENT=${MARKQL_BUILD_AGENT}"
  "-DMARKQL_AGENT_FETCH_DEPS=${MARKQL_AGENT_FETCH_DEPS}"
  "-DMARKQL_OPTIMIZE_FOR_SIZE=${MARKQL_OPTIMIZE_FOR_SIZE}"
  "-DMARKQL_STRIP_BINARIES=${MARKQL_STRIP_BINARIES}"
)

if [[ -n "${VCPKG_ROOT:-}" ]]; then
  cmake_args+=("-DCMAKE_TOOLCHAIN_FILE=${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
  cmake_args+=("-DVCPKG_TARGET_TRIPLET=${VCPKG_TARGET_TRIPLET}")
  cmake_args+=("-DVCPKG_HOST_TRIPLET=${VCPKG_HOST_TRIPLET}")
  cmake_args+=("-DVCPKG_MANIFEST_MODE=ON")
  cmake_args+=("-DVCPKG_MANIFEST_DIR=${REPO_ROOT}")
  cmake_args+=("-DVCPKG_INSTALLED_DIR=${VCPKG_LOCAL_INSTALLED_DIR}")
  cmake_args+=("-DCMAKE_PREFIX_PATH=${VCPKG_TARGET_INSTALLED_DIR};${VCPKG_FALLBACK_INSTALLED_DIR}")
  if [[ -d "${VCPKG_FALLBACK_INSTALLED_DIR}/share/curl" ]]; then
    cmake_args+=("-DCURL_DIR=${VCPKG_FALLBACK_INSTALLED_DIR}/share/curl")
  fi
  if [[ -d "${VCPKG_FALLBACK_INSTALLED_DIR}" ]]; then
    cmake_args+=("-DOPENSSL_ROOT_DIR=${VCPKG_FALLBACK_INSTALLED_DIR}")
  fi
  if [[ -d "${VCPKG_FALLBACK_INSTALLED_DIR}/share/arrow" ]]; then
    cmake_args+=("-DArrow_DIR=${VCPKG_FALLBACK_INSTALLED_DIR}/share/arrow")
  fi
  if [[ -d "${VCPKG_FALLBACK_INSTALLED_DIR}/share/parquet" ]]; then
    cmake_args+=("-DParquet_DIR=${VCPKG_FALLBACK_INSTALLED_DIR}/share/parquet")
  fi
fi

if [[ -n "${VCPKG_ROOT:-}" && -f "${REPO_ROOT}/vcpkg.json" ]]; then
  mkdir -p "${VCPKG_LOCAL_BUILDTREES_DIR}" "${VCPKG_LOCAL_PACKAGES_DIR}" "${VCPKG_LOCAL_INSTALLED_DIR}"
  "${VCPKG_ROOT}/vcpkg" install \
    --triplet "${VCPKG_TARGET_TRIPLET}" \
    --host-triplet "${VCPKG_HOST_TRIPLET}" \
    --x-buildtrees-root="${VCPKG_LOCAL_BUILDTREES_DIR}" \
    --x-packages-root="${VCPKG_LOCAL_PACKAGES_DIR}" \
    --x-install-root="${VCPKG_LOCAL_INSTALLED_DIR}" \
    --downloads-root="${VCPKG_DOWNLOADS_DIR}" \
    --binarysource=clear \
    --no-print-usage
  reset_stale_cmake_cache_for_vcpkg_manifest
fi

cmake "${cmake_args[@]}"
cmake --build "${REPO_ROOT}/build" --parallel "${BUILD_JOBS}"

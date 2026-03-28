#!/usr/bin/env bash
set -euo pipefail

usage() {
  local script_name
  script_name="$(basename "$0")"
  cat <<EOF
Usage: ${script_name} [--build-dir DIR] [--output-dir DIR] [--build-type TYPE] [--with-tests]

Builds a local Debian package (.deb) for MarkQL.

Options:
  --build-dir DIR    CMake build directory (default: build)
  --output-dir DIR   Where to place generated .deb (default: dist/deb)
  --build-type TYPE  CMAKE_BUILD_TYPE (default: Release)
  --with-tests       Build tests too (default: OFF for faster packaging)
  -h, --help         Show this help message
EOF
}

BUILD_DIR="build"
OUTPUT_DIR="dist/deb"
BUILD_TYPE="Release"
WITH_TESTS="OFF"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --build-dir)
      BUILD_DIR="${2:-}"
      shift 2
      ;;
    --output-dir)
      OUTPUT_DIR="${2:-}"
      shift 2
      ;;
    --build-type)
      BUILD_TYPE="${2:-}"
      shift 2
      ;;
    --with-tests)
      WITH_TESTS="ON"
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown argument: $1" >&2
      usage
      exit 2
      ;;
  esac
done

if ! command -v cmake >/dev/null 2>&1; then
  echo "Missing required tool: cmake" >&2
  exit 2
fi
if ! command -v cpack >/dev/null 2>&1; then
  echo "Missing required tool: cpack" >&2
  exit 2
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
BUILD_DIR_ABS="${REPO_ROOT}/${BUILD_DIR}"
OUTPUT_DIR_ABS="${REPO_ROOT}/${OUTPUT_DIR}"

if [[ ! -f "${REPO_ROOT}/CMakeLists.txt" ]]; then
  echo "Run this from inside the MarkQL repository." >&2
  exit 2
fi

if command -v nproc >/dev/null 2>&1; then
  JOBS="$(nproc)"
else
  JOBS="4"
fi

echo "[1/3] Configure project (${BUILD_TYPE})"
cmake -S "${REPO_ROOT}" -B "${BUILD_DIR_ABS}" \
  -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
  -DMARKQL_BUILD_CLI=ON \
  -DMARKQL_BUILD_AGENT=ON \
  -DMARKQL_BUILD_TESTS="${WITH_TESTS}" \
  -DMARKQL_BUILD_PYTHON=OFF \
  -DMARKQL_WITH_ARROW=OFF \
  -DMARKQL_OPTIMIZE_FOR_SIZE=ON \
  -DMARKQL_STRIP_BINARIES=ON

echo "[2/3] Build CLI and agent targets"
cmake --build "${BUILD_DIR_ABS}" --target markql markql-agent -j"${JOBS}"

if [[ ! -f "${BUILD_DIR_ABS}/CPackConfig.cmake" ]]; then
  echo "CPack config not found: ${BUILD_DIR_ABS}/CPackConfig.cmake" >&2
  echo "Ensure include(CPack) exists in CMakeLists.txt." >&2
  exit 1
fi

echo "[3/3] Create .deb package"
mkdir -p "${OUTPUT_DIR_ABS}"
cpack --config "${BUILD_DIR_ABS}/CPackConfig.cmake" -G DEB -B "${OUTPUT_DIR_ABS}"

echo
echo "Generated package(s):"
ls -1 "${OUTPUT_DIR_ABS}"/*.deb

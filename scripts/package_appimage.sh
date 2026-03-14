#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage: scripts/package_appimage.sh [--build-dir DIR] [--dist-dir DIR] [--appdir DIR]

Builds MarkQL and packages it as an AppImage for local testing.

Options:
  --build-dir DIR   CMake build directory (default: build)
  --dist-dir DIR    Output directory for AppImage (default: dist)
  --appdir DIR      Staging AppDir path (default: <build-dir>/AppDir)
  -h, --help        Show this help message

Tooling:
  Preferred: linuxdeploy + appimagetool (bundles runtime deps)
  Fallback : appimagetool only (no auto dependency bundling)
EOF
}

BUILD_DIR="build"
DIST_DIR="dist"
APPDIR=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --build-dir)
      BUILD_DIR="${2:-}"
      shift 2
      ;;
    --dist-dir)
      DIST_DIR="${2:-}"
      shift 2
      ;;
    --appdir)
      APPDIR="${2:-}"
      shift 2
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

if [[ -z "${APPDIR}" ]]; then
  APPDIR="${BUILD_DIR}/AppDir"
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR_ABS="${REPO_ROOT}/${BUILD_DIR}"
APPDIR_ABS="${REPO_ROOT}/${APPDIR}"
DIST_DIR_ABS="${REPO_ROOT}/${DIST_DIR}"

if [[ ! -f "${REPO_ROOT}/CMakeLists.txt" ]]; then
  echo "Run this script from the MarkQL repository." >&2
  exit 2
fi

if ! command -v cmake >/dev/null 2>&1; then
  echo "Missing required tool: cmake" >&2
  exit 2
fi

VERSION="$(sed -n 's/^__version__ = "\(.*\)"/\1/p' "${REPO_ROOT}/python/markql/_meta.py" | head -n 1)"
if [[ -z "${VERSION}" ]]; then
  VERSION="0.0.0"
fi

echo "[1/5] Configure + build markql"
cmake -S "${REPO_ROOT}" -B "${BUILD_DIR_ABS}" -DCMAKE_BUILD_TYPE=Release
cmake --build "${BUILD_DIR_ABS}" -j"$(nproc)"

echo "[2/5] Stage AppDir"
rm -rf "${APPDIR_ABS}"
mkdir -p "${APPDIR_ABS}"
cmake --install "${BUILD_DIR_ABS}" --prefix /usr --config Release --strip DESTDIR="${APPDIR_ABS}"

BIN_PATH="${APPDIR_ABS}/usr/bin/markql"
if [[ ! -x "${BIN_PATH}" ]]; then
  echo "Expected binary not found at ${BIN_PATH}" >&2
  exit 1
fi

echo "[3/5] Add desktop entry and icon"
mkdir -p "${APPDIR_ABS}/usr/share/applications"
mkdir -p "${APPDIR_ABS}/usr/share/icons/hicolor/256x256/apps"

DESKTOP_FILE="${APPDIR_ABS}/usr/share/applications/markql.desktop"
ICON_FILE="${APPDIR_ABS}/usr/share/icons/hicolor/256x256/apps/markql.png"

cat >"${DESKTOP_FILE}" <<'EOF'
[Desktop Entry]
Type=Application
Name=MarkQL
Exec=markql
Icon=markql
Terminal=true
Categories=Utility;Development;
EOF

# Minimal placeholder PNG for AppImage metadata; replace with project icon if available.
base64 -d > "${ICON_FILE}" <<'EOF'
iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVQIHWP4////fwAJ+wP+S5uV3wAAAABJRU5ErkJggg==
EOF

ln -sf "usr/share/icons/hicolor/256x256/apps/markql.png" "${APPDIR_ABS}/.DirIcon"

mkdir -p "${DIST_DIR_ABS}"
ARCH_NAME="$(uname -m)"
OUT_APPIMAGE="${DIST_DIR_ABS}/markql-${VERSION}-${ARCH_NAME}.AppImage"

echo "[4/5] Build AppImage"
if command -v linuxdeploy >/dev/null 2>&1; then
  linuxdeploy \
    --appdir "${APPDIR_ABS}" \
    --desktop-file "${DESKTOP_FILE}" \
    --icon-file "${ICON_FILE}" \
    --executable "${BIN_PATH}" \
    --output appimage

  # linuxdeploy writes to current working directory.
  GENERATED="$(find "${REPO_ROOT}" -maxdepth 1 -type f -name "*.AppImage" -printf "%f\n" | sort | tail -n 1 || true)"
  if [[ -n "${GENERATED}" ]]; then
    mv -f "${REPO_ROOT}/${GENERATED}" "${OUT_APPIMAGE}"
  fi
elif command -v appimagetool >/dev/null 2>&1; then
  echo "linuxdeploy not found: using appimagetool only (no auto dependency bundling)."
  ARCH="${ARCH_NAME}" appimagetool "${APPDIR_ABS}" "${OUT_APPIMAGE}"
else
  echo "Missing AppImage toolchain." >&2
  echo "Install linuxdeploy + appimagetool (recommended), or at least appimagetool." >&2
  exit 2
fi

if [[ ! -f "${OUT_APPIMAGE}" ]]; then
  echo "AppImage generation did not produce expected file: ${OUT_APPIMAGE}" >&2
  exit 1
fi

chmod +x "${OUT_APPIMAGE}"

echo "[5/5] Done"
echo "AppImage: ${OUT_APPIMAGE}"

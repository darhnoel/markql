#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage: scripts/uninstall_markql.sh [--appimage] [--deb] [--purge] [--yes]

Uninstalls local MarkQL packaging artifacts.

Options:
  --appimage   Remove local AppImage artifacts (dist/markql-*.AppImage, ./markql-*.AppImage)
  --deb        Uninstall Debian package "markql" using apt
  --purge      With --deb, purge package config files (apt purge)
  --yes        Non-interactive apt mode (-y)
  -h, --help   Show this help message

If no options are provided, defaults to --appimage only.
EOF
}

DO_APPIMAGE=0
DO_DEB=0
DO_PURGE=0
ASSUME_YES=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    --appimage)
      DO_APPIMAGE=1
      shift
      ;;
    --deb)
      DO_DEB=1
      shift
      ;;
    --purge)
      DO_PURGE=1
      shift
      ;;
    --yes)
      ASSUME_YES=1
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

if [[ ${DO_APPIMAGE} -eq 0 && ${DO_DEB} -eq 0 ]]; then
  DO_APPIMAGE=1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

remove_appimages() {
  local removed=0
  shopt -s nullglob
  local candidates=(
    "${REPO_ROOT}/dist/markql-"*.AppImage
    "${REPO_ROOT}/markql-"*.AppImage
  )
  for file in "${candidates[@]}"; do
    rm -f -- "${file}"
    echo "Removed: ${file}"
    removed=1
  done
  shopt -u nullglob

  if [[ ${removed} -eq 0 ]]; then
    echo "No AppImage artifacts found."
  fi
}

remove_deb_package() {
  if ! command -v dpkg >/dev/null 2>&1 || ! command -v apt >/dev/null 2>&1; then
    echo "dpkg/apt not found. --deb works on Debian/Ubuntu systems only." >&2
    exit 2
  fi

  if ! dpkg -s markql >/dev/null 2>&1; then
    echo "Package 'markql' is not installed."
    return
  fi

  local apt_args=()
  if [[ ${ASSUME_YES} -eq 1 ]]; then
    apt_args+=("-y")
  fi

  if [[ ${DO_PURGE} -eq 1 ]]; then
    echo "Purging package: markql"
    sudo apt "${apt_args[@]}" purge markql
  else
    echo "Removing package: markql"
    sudo apt "${apt_args[@]}" remove markql
  fi
}

if [[ ${DO_APPIMAGE} -eq 1 ]]; then
  remove_appimages
fi

if [[ ${DO_DEB} -eq 1 ]]; then
  remove_deb_package
fi

echo "Done."

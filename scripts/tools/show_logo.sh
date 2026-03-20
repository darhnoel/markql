#!/usr/bin/env bash
set -euo pipefail

usage() {
  local script_name
  script_name="$(basename "$0")"
  cat <<EOF
Usage: ${script_name} [--logo FILE] [--width N]

Render the MarkQL SVG logo in your terminal.

Options:
  --logo FILE   Path to an SVG logo file (default: docs/assets/logo/markql_logo_light.svg)
  --width N     Output width in terminal columns (default: auto small, clamped to 18..32)
  -h, --help    Show this help message

Renderer priority:
  1) chafa (preferred)
  2) kitty icat / viu / img2sixel (requires SVG -> PNG conversion)
EOF
}

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
DEFAULT_LOGO="${REPO_ROOT}/docs/assets/logo/markql_logo_light.svg"
LOGO_FILE="${DEFAULT_LOGO}"

TERM_COLS="${COLUMNS:-80}"
WIDTH=$((TERM_COLS / 3))
if [[ "${WIDTH}" -lt 18 ]]; then
  WIDTH=18
fi
if [[ "${WIDTH}" -gt 32 ]]; then
  WIDTH=32
fi
HEIGHT=$(((WIDTH * 651 + 1199) / 1200))

while [[ $# -gt 0 ]]; do
  case "$1" in
    --logo)
      LOGO_FILE="${2:-}"
      shift 2
      ;;
    --width)
      WIDTH="${2:-}"
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

if [[ ! -f "${LOGO_FILE}" ]]; then
  echo "Logo file not found: ${LOGO_FILE}" >&2
  exit 2
fi

if ! [[ "${WIDTH}" =~ ^[0-9]+$ ]] || [[ "${WIDTH}" -le 0 ]]; then
  echo "--width must be a positive integer: ${WIDTH}" >&2
  exit 2
fi

HEIGHT=$(((WIDTH * 651 + 1199) / 1200))
if [[ "${HEIGHT}" -le 0 ]]; then
  HEIGHT=1
fi

TMP_PNG=""
cleanup() {
  if [[ -n "${TMP_PNG}" && -f "${TMP_PNG}" ]]; then
    rm -f "${TMP_PNG}"
  fi
}
trap cleanup EXIT

svg_to_png() {
  TMP_PNG="$(mktemp /tmp/markql-logo.XXXXXX.png)"
  if command -v rsvg-convert >/dev/null 2>&1; then
    rsvg-convert -w "$((WIDTH * 8))" "${LOGO_FILE}" -o "${TMP_PNG}" >/dev/null 2>&1
    return 0
  fi
  if command -v magick >/dev/null 2>&1; then
    magick "${LOGO_FILE}" -resize "$((WIDTH * 8))x" "${TMP_PNG}" >/dev/null 2>&1
    return 0
  fi
  if command -v convert >/dev/null 2>&1; then
    convert "${LOGO_FILE}" -resize "$((WIDTH * 8))x" "${TMP_PNG}" >/dev/null 2>&1
    return 0
  fi
  return 1
}

render_with_chafa() {
  chafa --format symbols --size "${WIDTH}x${HEIGHT}" "${1}"
}

if command -v chafa >/dev/null 2>&1; then
  if render_with_chafa "${LOGO_FILE}" 2>/dev/null; then
    exit 0
  fi
fi

if ! svg_to_png; then
  echo "Unable to render SVG logo in terminal." >&2
  echo "Install one of: chafa, librsvg2-bin (rsvg-convert), imagemagick." >&2
  exit 1
fi

if command -v chafa >/dev/null 2>&1; then
  if render_with_chafa "${TMP_PNG}"; then
    exit 0
  fi
fi

if command -v kitten >/dev/null 2>&1; then
  kitten icat --align center "${TMP_PNG}"
  exit 0
fi

if command -v viu >/dev/null 2>&1; then
  viu -w "${WIDTH}" "${TMP_PNG}"
  exit 0
fi

if command -v img2sixel >/dev/null 2>&1; then
  img2sixel -w "${WIDTH}" "${TMP_PNG}"
  exit 0
fi

echo "Rendered PNG fallback is ready, but no terminal image renderer was found." >&2
echo "Install one of: chafa, kitty (kitten icat), viu, img2sixel." >&2
exit 1

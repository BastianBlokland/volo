#!/usr/bin/env bash
set -e -o pipefail

# ------------------------------------------------------------------------------
# Utility script to generate Wayland protocol headers and code using
# wayland-scanner. Output is written to libs/gap/src/wayland/.
# ------------------------------------------------------------------------------

info() {
  echo "${1}"
}

error() {
  echo "ERROR: ${1}" >&2
}

fail() {
  error "${1}"
  exit 1
}

hasCommand() {
  [ -x "$(command -v "${1}")" ]
}

getSourceDirectory() {
  realpath "$(dirname -- "${0}")"
}

hasCommand wayland-scanner || fail "'wayland-scanner' not found on path"

test -d "${VOLO_WAYLAND_DATADIR}" || fail "'VOLO_WAYLAND_DATADIR' environment variable required"
test -d "${VOLO_WAYLAND_PROTOCOLS_DATADIR}" || fail "'VOLO_WAYLAND_PROTOCOLS_DATADIR' environment variable required"

OUT_DIR="$(getSourceDirectory)/../libs/gap/src/wayland"
mkdir -p "${OUT_DIR}"

scan_header() {
  local xml="${1}"
  local out="${OUT_DIR}/$(basename "${xml%.xml}").h"
  test -f "${xml}" || fail "Protocol xml not found: '${xml}'"
  info "Generating $(basename "${out}")"
  wayland-scanner client-header "${xml}" "${out}"
}

scan_code() {
  local xml="${1}"
  local out="${OUT_DIR}/$(basename "${xml%.xml}").c"
  test -f "${xml}" || fail "Protocol xml not found: '${xml}'"
  info "Generating $(basename "${out}")"
  wayland-scanner private-code "${xml}" "${out}"
}

scan_header "${VOLO_WAYLAND_DATADIR}/wayland.xml"
scan_header "${VOLO_WAYLAND_PROTOCOLS_DATADIR}/stable/xdg-shell/xdg-shell.xml"
scan_code   "${VOLO_WAYLAND_PROTOCOLS_DATADIR}/stable/xdg-shell/xdg-shell.xml"
info "Done"

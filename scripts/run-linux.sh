#!/usr/bin/env bash
set -e -o pipefail

# --------------------------------------------------------------------------------------------------
# Utility script to configure and invoke a CMake target.
# --------------------------------------------------------------------------------------------------

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

getScriptDirectory() {
  realpath "$(dirname -- "${0}")"
}

verifyBuildSystemOption() {
  case "${1}" in
    make|ninja)
      ;;
    *)
      fail "Unsupported build-system: '${1}'"
      ;;
  esac
}

verifyBoolOption() {
  case "${1}" in
    On|Off)
      ;;
    *)
      fail "Unsupported bool value: '${1}'"
      ;;
  esac
}

getGeneratorName() {
  case "${1}" in
    make)
      echo "Unix Makefiles"
      ;;
    ninja)
      echo "Ninja"
      ;;
  esac
}

build() {
  local buildDir="${1}"
  local buildTarget="${2}"
  local buildSystem="${3}"
  local fastMode="${4}"
  local sanitizeMode="${5}"

  verifyBuildSystemOption "${buildSystem}"
  verifyBoolOption "${fastMode}"
  verifyBoolOption "${sanitizeMode}"

  local sourceDir
  sourceDir="$(getScriptDirectory)/.."

  # Create target directory if it doesn't exist yet.
  test -d "${buildDir}" || mkdir -p "${buildDir}"

  # Verify that cmake is present on path.
  hasCommand cmake || fail "'cmake' not found on path, it is required"

  info "Configuring build directory '${buildDir}'"

  # Configure.
  ( cd "$sourceDir"; cmake -B "${buildDir}" \
    -G "$(getGeneratorName "${buildSystem}")" \
    -DFAST="${fastMode}" \
    -DSANITIZE="${sanitizeMode}" )

  info "Building target '${buildTarget}' using '${buildSystem}'"

  # Build.
  ( cd "$sourceDir"; cmake --build "${buildDir}" --target "${buildTarget}" )
}

# Defaults.
buildDir="build"
buildTarget="run.sandbox"
buildSystem="ninja"
fastMode="Off"
sanitizeMode="Off"

printUsage() {
  echo "Options:"
  echo "-h,--help     Print this usage information"
  echo "-d,--dir      Build directory, default: '${buildDir}'"
  echo "-t,--target   Build target, default: '${buildTarget}'"
  echo "-s,--system   Build system, default: '${buildSystem}'"
  echo "--fast        Fast mode, disables various runtime validations, default: '${fastMode}'"
  echo "--sanitize    Santiser instrumentation, default: '${sanitizeMode}'"
}

# Parse options.
while [[ $# -gt 0 ]]
do
  case "${1}" in
    -h|--help)
      echo "Volo -- Build utility"
      printUsage
      exit 0
      ;;
    -d|--dir)
      buildDir="${2}"
      shift 2
      ;;
    -t|--target)
      buildTarget="${2}"
      shift 2
      ;;
    -s|--system)
      buildSystem="${2}"
      shift 2
      ;;
    --fast)
      fastMode="On"
      shift 1
      ;;
    --sanitize)
      sanitizeMode="On"
      shift 1
      ;;
    *)
      error "Unknown option '${1}'"
      printUsage
      exit 1
      ;;
  esac
done

# Execute.
build \
  "${buildDir}" \
  "${buildTarget}" \
  "${buildSystem}" \
  "${fastMode}" \
  "${sanitizeMode}"
exit 0

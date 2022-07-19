#!/usr/bin/env bash
set -e -o pipefail

# --------------------------------------------------------------------------------------------------
# Utility script to configure and invoke a CMake target.
# --------------------------------------------------------------------------------------------------

info()
{
  echo "${1}"
}

error()
{
  echo "ERROR: ${1}" >&2
}

fail()
{
  error "${1}"
  exit 1
}

hasCommand()
{
  [ -x "$(command -v "${1}")" ]
}

verifyBuildSystemOption()
{
  case "${1}" in
    make|ninja)
      ;;
    *)
      fail "Unsupported build-system: '${1}'"
      ;;
  esac
}

verifyBoolOption()
{
  case "${1}" in
    On|Off)
      ;;
    *)
      fail "Unsupported bool value: '${1}'"
      ;;
  esac
}

getGeneratorName()
{
  case "${1}" in
    make)
      echo "Unix Makefiles"
      ;;
    ninja)
      echo "Ninja"
      ;;
  esac
}

build()
{
  local buildDir="${1}"
  local buildTarget="${2}"
  local buildSystem="${3}"
  local sanitizeMode="${4}"

  verifyBuildSystemOption "${buildSystem}"
  verifyBoolOption "${sanitizeMode}"

  # Create target directory if it doesn't exist yet.
  test -d "${buildDir}" || mkdir -p "${buildDir}"

  # Verify that cmake is present on path.
  hasCommand cmake || fail "'cmake' not found on path, it is required"

  info "Configuring build directory '${buildDir}'"

  # Configure.
  cmake -B "${buildDir}" \
    -G "$(getGeneratorName "${buildSystem}")" \
    -DSANITIZE="${sanitizeMode}"

  info "Building target '${buildTarget}' using '${buildSystem}'"

  # Build.
  cmake --build "${buildDir}" --target "${buildTarget}"
}

# Defaults.
buildDir="build"
buildTarget="run.sandbox"
buildSystem="ninja"
sanitizeMode="Off"

printUsage()
{
  echo "Options:"
  echo "-h,--help     Print this usage information"
  echo "-d,--dir      Build directory, default: '${buildDir}'"
  echo "-t,--target   Build target, default: '${buildTarget}'"
  echo "-s,--system   Build system, default: '${buildSystem}'"
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
  "${sanitizeMode}"
exit 0

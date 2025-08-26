<#
.SYNOPSIS
  Utility script to configure and invoke a CMake target.
.PARAMETER BuildDirectory
  Default: build
  Directory to build into (will be created if it doesn't exist).
.PARAMETER BuildTarget
  Default: run.volo
  CMake build-target to invoke.
.PARAMETER BuildSystem
  Default: nmake
  Build system to use.
.PARAMETER BuildType
  Default: Release
  Build type.
.PARAMETER NoTrace
  Default: Off
  Disables runtime performance tracing.
.PARAMETER Lto
  Default: Off
  Link time optimization.
.PARAMETER Sanitize
  Default: Off
  Should sanitizer instrumentation be enabled.
#>
[cmdletbinding()]
param(
  [string]$BuildDirectory = "build",
  [string]$BuildTarget = "run.volo",
  [ValidateSet("ninja", "nmake", "mingw", "vs2019", "vs2022")] [string]$BuildSystem = "nmake",
  [ValidateSet("Debug", "Release")] [string]$BuildType = "Release",
  [switch]$NoTrace,
  [switch]$Lto,
  [switch]$Sanitize
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Info([string] $message) {
  Write-Output $message
}

function Verbose([string] $message) {
  Write-Verbose $message
}

function Fail([string] $message) {
  Write-Error $message
  exit 1
}

function GetSourceDirectory() {
  "$PSScriptRoot" | Resolve-Path
}

function GetGeneratorName([string] $buildSystem) {
  switch -Exact ($buildSystem) {
    "nmake" { "NMake Makefiles" }
    "ninja" { "Ninja" }
    "mingw" { "MinGW Makefiles" }
    "vs2019" { "Visual Studio 16 2019" }
    "vs2022" { "Visual Studio 17 2022" }
    default { Fail "Unsupported build-system: '$BuildSystem'" }
  }
}

function SetupEnvironment() {
  if (!(Test-Path "$PSScriptRoot\env-win32.ps1")) {
    Fail "Environment setup script missing"
  }
  & "$PSScriptRoot\env-win32.ps1"
  if ($LASTEXITCODE -ne 0) {
    Fail "Environment setup failed"
  }
}

function ExecuteGenerator(
    [string] $buildDirectory,
    [string] $buildSystem,
    [string] $buildType,
    [bool] $noTrace,
    [bool] $lto,
    [bool] $sanitize) {
  if (!(Get-Command "cmake.exe" -ErrorAction SilentlyContinue)) {
    Fail "'cmake.exe' not found on path, please install the CMake build-system generator"
  }
  $sourceDir = "$(GetSourceDirectory)\..\"

  # Create target directory if it doesn't exist yet.
  if (!(Test-Path "$sourceDir\$buildDirectory")) {
    New-Item -ItemType Directory -Path "$sourceDir\$buildDirectory" | Out-Null
  }

  Push-Location $sourceDir

  & cmake.exe -B $buildDirectory `
    -G "$(GetGeneratorName $buildSystem)" `
    -DCMAKE_BUILD_TYPE="$buildType" `
    -DVOLO_TRACE="$(if($noTrace) { "Off" } else { "On" })" `
    -DVOLO_LTO="$(if($lto) { "On" } else { "Off" })" `
    -DVOLO_SANITIZE="$(if($sanitize) { "On" } else { "Off" })"

  $result = $LASTEXITCODE
  Pop-Location

  if ($result -ne 0) {
    Fail "Generator failed"
  }
}

function ExecuteBuild([string] $buildDirectory, [string]$buildTarget, [string] $buildType) {
  if (!(Get-Command "cmake.exe" -ErrorAction SilentlyContinue)) {
    Fail "'cmake.exe' not found on path"
  }
  $sourceDir = "$(GetSourceDirectory)\..\"
  $jobs = $(Get-CimInstance Win32_ComputerSystem).NumberOfLogicalProcessors

  Verbose "Maximum number of jobs: $jobs"

  Push-Location $sourceDir

  & cmake.exe --build $buildDirectory --target $buildTarget --config $buildType --parallel $jobs

  $result = $LASTEXITCODE
  Pop-Location

  if ($result -ne 0) {
    Fail "Build failed"
  }
}

function Execute() {
  Info "Setting up environment"
  SetupEnvironment

  Info "Configuring build directory '$BuildDirectory'"
  ExecuteGenerator $BuildDirectory $BuildSystem $BuildType $NoTrace $Lto $Sanitize

  Info "Building target '$BuildTarget' using '$BuildSystem' ($BuildType)"
  ExecuteBuild $BuildDirectory $BuildTarget $BuildType
}

Execute
exit 0

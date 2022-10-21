<#
.SYNOPSIS
  Utility script to configure and invoke a CMake target.
.PARAMETER BuildDirectory
  Default: build
  Directory to build into (will be created if it doesn't exist).
.PARAMETER BuildTarget
  Default: run.sandbox
  CMake build-target to invoke.
.PARAMETER BuildSystem
  Default: nmake
  Build system to use.
.PARAMETER Fast
  Default: Off
  Fast mode, disables various runtime validations.
.PARAMETER Sanitize
  Default: Off
  Should santizer instrumentation be enabled.
#>
[cmdletbinding()]
param(
  [string]$BuildDirectory = "build",
  [string]$BuildTarget = "run.sandbox",
  [ValidateSet("ninja", "nmake", "mingw", "vs2019", "vs2022")] [string]$BuildSystem = "nmake",
  [switch]$Fast,
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

function GetScriptDirectory() {
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
    [bool] $fast,
    [bool] $sanitize) {
  if (!(Get-Command "cmake.exe" -ErrorAction SilentlyContinue)) {
    Fail "'cmake.exe' not found on path, please install the CMake build-system generator"
  }
  $sourceDir = "$(GetScriptDirectory)\..\"

  # Create target directory if it doesn't exist yet.
  if (!(Test-Path "$sourceDir\$buildDirectory")) {
    New-Item -ItemType Directory -Path "$sourceDir\$buildDirectory" | Out-Null
  }

  Push-Location $sourceDir

  & cmake.exe -B $buildDirectory `
    -G "$(GetGeneratorName $buildSystem)" `
    -DFAST="$(if($fast) { "On" } else { "Off" })" `
    -DSANITIZE="$(if($sanitize) { "On" } else { "Off" })"

  $result = $LASTEXITCODE
  Pop-Location

  if ($result -ne 0) {
    Fail "Generator failed"
  }
}

function ExecuteBuild([string] $buildDirectory, [string]$buildTarget) {
  if (!(Get-Command "cmake.exe" -ErrorAction SilentlyContinue)) {
    Fail "'cmake.exe' not found on path"
  }
  $sourceDir = "$(GetScriptDirectory)\..\"
  $jobs = $(Get-CimInstance Win32_ComputerSystem).NumberOfLogicalProcessors

  Verbose "Maximum number of jobs: $jobs"

  Push-Location $sourceDir

  & cmake.exe --build $buildDirectory --target $buildTarget --parallel $jobs

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
  ExecuteGenerator $BuildDirectory $BuildSystem $Fast $Sanitize

  Info "Building target '$BuildTarget' using '$BuildSystem'"
  ExecuteBuild $BuildDirectory $BuildTarget
}

Execute
exit 0

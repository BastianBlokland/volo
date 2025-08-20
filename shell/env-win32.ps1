<#
.SYNOPSIS
  Script to setup environment variables required by the MSVC (Microsoft Visual C) toolchain.
.DESCRIPTION
  Sets up the current shell to be able to invoke the MSVC 'cl' compiler to compile Windows apps.
  Call this once per powershell session before configuring or invoking the build system.
  Dependencies:
  - VCTools (Microsoft Visual C toolchain).
  - Windows10SDK
  Both can be installed through the 'Visual Studio Installer' by choosing the 'Desktop development
  with C++' workload and including the recommended components.
  Or alternatively using winget:
  winget install Microsoft.VisualStudio.2022.BuildTools --override "--add Microsoft.VisualStudio.Workload.VCTools --add Microsoft.VisualStudio.Component.VC.Tools.x86.x64 --add Microsoft.VisualStudio.Component.Windows10SDK.19041 --focusedUi"
.PARAMETER Arch
  Default: x64
  Machine architecture.
#>
[cmdletbinding()]
param(
  [ValidateSet("x64")] [string]$Arch = "x64"
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

function EnvAdd([string] $key, [string] $value) {
  $valueOld   = [Environment]::GetEnvironmentVariable($key)
  $entriesOld = $valueOld -split [IO.Path]::PathSeparator
  if ($entriesOld -notcontains $value) {
    [Environment]::SetEnvironmentVariable($key, "$valueOld;$value")
  }
}

function GetVsWherePath() {
  # Attempt to find it on the path.
  $vswhere = Get-Command "vswhere.exe" -ErrorAction Ignore
  if ($vswhere) {
    return $vswhere.Source
  }
  # Fall back to the location where Visual Studio installs it by default.
  return "${Env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
}

function GetVcToolsInstallPath([string] $vswherePath) {
  & "$vswherePath" `
    -latest `
    -products * `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -property installationPath
}

function GetVcToolsVersion([string] $vctoolsInstallPath) {
  Get-Content -Path "${vctoolsInstallPath}\VC\Auxiliary\Build\Microsoft.VCToolsVersion.default.txt"
}

function SetToolchainEnv([string] $arch) {
  $vswherePath = "$(GetVsWherePath)"
  if (!(Test-Path ${vswherePath})) {
    Fail "'vswhere.exe' not found, please install the 'Build Tools for Visual Studio'"
  }
  Verbose "VSWhere path: '${vswherePath}'"

  $vctoolsInstallPath = "$(GetVcToolsInstallPath ${vswherePath})"
  if ([string]::IsNullOrEmpty($vctoolsInstallPath)) {
    Fail "MSVC (Microsoft Visual C) toolchain not found, please install the 'VCTools' workload"
  }
  Verbose "VCTools install-path: '${vctoolsInstallPath}'"

  $vctoolsVersion = "$(GetVcToolsVersion ${vctoolsInstallPath})"
  if ([string]::IsNullOrEmpty($vctoolsVersion)) {
    Fail "MSVC (Microsoft Visual C) toolchain version missing"
  }
  Info "VCTools version: ${vctoolsVersion}"

  $vctoolsPath = "${vctoolsInstallPath}\VC\Tools\MSVC\${vctoolsVersion}"
  if (-not(Test-Path -path ${vctoolsPath})) {
    Fail "VCTools installation missing (path: '${vctoolsPath}')"
  }
  Info "VCTools path: '${vctoolsPath}'"

  EnvAdd "PATH" "$vctoolsPath\bin\Host$($arch.ToUpper())\$arch"
  EnvAdd "LIB" "$vctoolsPath\lib\$arch"
  EnvAdd "INCLUDE" "$vctoolsPath\include"

  if ([string]::IsNullOrEmpty([Environment]::GetEnvironmentVariable("CC"))) {
    [Environment]::SetEnvironmentVariable("CC", "cl.exe")
  }
}

function SetWindowsSDKEnv([string] $arch) {
  $sdkRegPath  = "HKLM\SOFTWARE\WOW6432Node\Microsoft\Microsoft SDKs\Windows"
  $sdkRegEntry = Get-ItemProperty -ErrorAction Ignore -Path "Registry::${sdkRegPath}\v10.0"
  if ($sdkRegEntry -eq $null) {
    Fail "Windows SDK not found, please install the 'Windows10SDK' component"
  }
  $sdkVersion = $sdkRegEntry.ProductVersion
  Info "WindowsSDK version: ${sdkVersion}"

  $sdkPath = $sdkRegEntry.InstallationFolder
  if (-not(Test-Path -path ${sdkPath})) {
    Fail "WindowsSDK installation missing (path: '${sdkPath}')"
  }
  Info "WindowsSDK path: '${sdkPath}'"

  EnvAdd "PATH" "${sdkPath}bin\${sdkVersion}.0\$arch"
  EnvAdd "LIB" "${sdkPath}lib\${sdkVersion}.0\ucrt\$arch"
  EnvAdd "LIB" "${sdkPath}lib\${sdkVersion}.0\um\$arch"
  EnvAdd "INCLUDE" "${sdkPath}include\${sdkVersion}.0\ucrt"
  EnvAdd "INCLUDE" "${sdkPath}include\${sdkVersion}.0\shared"
  EnvAdd "INCLUDE" "${sdkPath}include\${sdkVersion}.0\um"
}

SetToolchainEnv $Arch
SetWindowsSDKEnv $Arch
Info "Win32 environment setup (arch: $Arch)"

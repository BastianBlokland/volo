<#
.SYNOPSIS
  Script to setup environment variables required for the MSVC (Microsoft Visual C) compiler.
.DESCRIPTION
  Sets up the current shell to be able to invoke the MSVC 'cl' compiler.
  Call this once per powershell session before configuring or invoking the build system.
  Dependencies:
  - MSVC (Microsoft Visual C) toolchain. Can be installed through the 'Visual Studio Installer' by
    choosing the 'Desktop development with C++' workload.
    Or alternativly using winget:
      winget install Microsoft.VisualStudio.2022.BuildTools --override "--add Microsoft.VisualStudio.Workload.VCTools"
.PARAMETER Arch
  Default: x64
  Target machine architecture.
.PARAMETER HostArch
  Default: x64
  Host machine architecture.
#>
[cmdletbinding()]
param(
  [ValidateSet("x64")] [string]$Arch = "x64",
  [ValidateSet("x64")] [string]$HostArch = "x64"
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

function GetVsWherePath() {
  # Attempt to find it on the path.
  $vswhere = Get-Command "vswhere.exe" -ErrorAction SilentlyContinue
  if ($vswhere) {
    return $vswhere.Source
  }
  # Attempt the location where Visual Studio installs it by default.
  $defaultPath = "${Env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
  if (!(Test-Path $defaultPath)) {
    Fail "'vswhere.exe' not found, please install the 'Build Tools for Visual Studio'"
  }
  return $defaultPath
}

function GetVCToolsInstallPath() {
  $vswherePath = "$(GetVsWherePath)"
  Verbose "vswhere path: $vswherePath"

  & "$vswherePath" `
    -latest `
    -products * `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -property installationPath
}

function GetVSDevEnvBatPath() {
  $vctoolsInstallPath = "$(GetVCToolsInstallPath)"
  if ([string]::IsNullOrEmpty($vctoolsInstallPath)) {
    Fail "MSVC (Microsoft Visual C) toolchain not found, please install the 'VCTools' workload"
  }
  Verbose "vctools path: $vctoolsInstallPath"

  join-path "$vctoolsInstallPath" 'Common7\Tools\vsdevcmd.bat'
}

function SetVsDevEnvVars() {
  if (!(Get-Command "cmd.exe" -ErrorAction SilentlyContinue)) {
    Fail "'cmd.exe' not found on path"
  }
  $argString = "/s /c " + `
    "set VSCMD_SKIP_SENDTELEMETRY=1 &&" + `
    "`"$(GetVSDevEnvBatPath)`" -arch=$Arch -host_arch=$HostArch -no_logo &&" + `
    "set"
  $outputFile = "$env:windir\temp\env-vars"
  Start-Process `
    -NoNewWindow `
    -Wait `
    -RedirectStandardOutput "$outputFile" `
    -FilePath "cmd.exe" `
    -ArgumentList $argString

  Get-Content -Path $outputFile | Foreach-Object {
    $name, $value = $_ -split '=', 2
    Verbose "$name = $value"
    [Environment]::SetEnvironmentVariable($name, $value)
  }
}

SetVsDevEnvVars
Info "win32 environment setup (arch: $Arch, host_arg: $HostArch)"

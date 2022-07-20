<#
.SYNOPSIS
  Script to setup environment variables required for the MSVC (Microsoft Visual C) compiler.
.DESCRIPTION
  Sets up the current shell to be able to invoke the MSVC 'cl' compiler.
  Call this once per powershell session before configuring or invoking the build system.
  Dependencies:
  - MSVC (Microsoft Visual C) toolchain, which is included in the 'Windows SDK' which can either be
    installed as part of the VisualStudio IDE or installed seperatly
    (https://developer.microsoft.com/en-US/windows/downloads/windows-10-sdk/).
  - VsWhere (https://github.com/Microsoft/vswhere)
    Install it using chocolatey (choco install vswhere) or alternativly you can manually download it
    (https://github.com/Microsoft/vswhere/releases).
    Note: vswhere.exe needs to be visible on PATH.
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

function GetVCToolsInstallPath() {
  if (!(Get-Command "vswhere.exe" -ErrorAction SilentlyContinue)) {
    Fail "'vswhere.exe' not found on path, please install it and add its bin dir to path"
  }
  & vswhere `
    -latest `
    -products * `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -property installationPath
}

function GetVSDevEnvBatPath() {
  join-path $(GetVCToolsInstallPath) 'Common7\Tools\vsdevcmd.bat'
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
    -UseNewEnvironment `
    -NoNewWindow `
    -Wait `
    -RedirectStandardOutput "$outputFile" `
    -FilePath "cmd.exe" `
    -ArgumentList $argString

  Get-Content -Path $outputFile | Foreach-Object {
    $name, $value = $_ -split '=', 2
    Verbose "$name = $value"
    set-content env:\$name $value
  }
}

SetVsDevEnvVars
Info "win32 environment setup (arch: $Arch, host_arg: $HostArch)"

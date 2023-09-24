:: Convenience wrapper around around the run-win32.ps1 powershell script.
@echo off
setlocal
cd /d %~dp0
powershell.exe -ExecutionPolicy Bypass -NoLogo -NoProfile .\run-win32.ps1 -Fast -Lto %*
pause

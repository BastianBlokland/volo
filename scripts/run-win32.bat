@ECHO OFF
setlocal
cd /d %~dp0
powershell.exe -ExecutionPolicy Bypass -NoLogo -NoProfile .\run-win32.ps1
PAUSE

@echo off
setlocal
set "HAMMER_GMOD_RUNTIME_DIR=%~dp0.."
set "PATH=%HAMMER_GMOD_RUNTIME_DIR%;%PATH%"
if /I "%~1"=="--selftest" goto selftest
start "" /B "%~dp0hammer_qt.exe" %*
exit /b 0
:selftest
"%~dp0hammer_qt.exe" %*
exit /b %ERRORLEVEL%

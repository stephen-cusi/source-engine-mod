@echo off
setlocal
rem Build the Servo C API library.
rem Usage: build_servo_capi.cmd [servo_root] [target] [jobs]
rem Full cargo output goes to D:\tmp\servo_capi_build.log; on failure the
rem error lines are echoed here so the log does not have to be opened.

set "SERVO_ROOT=%~1"
if "%SERVO_ROOT%"=="" set "SERVO_ROOT=D:\project\servo-integration"
set "TARGET=%~2"
if "%TARGET%"=="" set "TARGET=x86_64-pc-windows-msvc"
set "JOBS=%~3"
if "%JOBS%"=="" set "JOBS=12"
set "LOG=D:\tmp\servo_capi_build.log"
set "ARTIFACT=%SERVO_ROOT%\target\%TARGET%\release\servo_capi.dll"

if not exist "%SERVO_ROOT%\ffi\capi\Cargo.toml" (
    echo Servo C API sources are missing from the checkout.
    exit /b 1
)

rem Toolchain environment the build needs; only set what is not already present.
if not defined LIBCLANG_PATH set "LIBCLANG_PATH=D:\Program Files\LLVM\bin"
if not defined CARGO_HOME set "CARGO_HOME=D:\project\servo-cargo-cache"
if not defined RUSTUP_TOOLCHAIN set "RUSTUP_TOOLCHAIN=stable-x86_64-pc-windows-msvc"
set "PYTHONUTF8=1"

echo Servo build: %SERVO_ROOT% ^(target %TARGET%, jobs %JOBS%^)
pushd "%SERVO_ROOT%"
cargo build --locked -j %JOBS% --target %TARGET% --release -p servo-capi > "%LOG%" 2>&1
set CARGO_EXIT=%ERRORLEVEL%
popd

if "%CARGO_EXIT%"=="0" (
    echo Servo build finished. Full output: %LOG%
    exit /b 0
)

echo Servo build FAILED with exit code %CARGO_EXIT%. Full output: %LOG%
for /f "usebackq delims=" %%L in (`findstr /r "^error" "%LOG%"`) do @echo %%L
exit /b 1

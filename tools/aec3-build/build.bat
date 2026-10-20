@echo off
rem Builds thirdparty\AEC3\lib\AEC3.lib (Release) or AEC3_debug.lib (Debug).
rem Usage: build.bat Release|Debug <webrtc checkout> <abseil-cpp checkout> <chromium third_party parent>
rem Needs Visual Studio 2026 (MSVC v145) with its bundled CMake and Ninja.
setlocal enabledelayedexpansion

set CONFIG=%~1
set WEBRTC_SRC=%~2
set ABSEIL_SRC=%~3
set CHROMIUM_SRC=%~4
if "%CONFIG%"=="" goto usage
if "%WEBRTC_SRC%"=="" goto usage
if "%ABSEIL_SRC%"=="" goto usage
if "%CHROMIUM_SRC%"=="" goto usage
if /i "%CONFIG%"=="Release" (set LIBNAME=AEC3) else if /i "%CONFIG%"=="Debug" (set LIBNAME=AEC3_debug) else goto usage

set VS=C:\Program Files\Microsoft Visual Studio\18\Community
call "%VS%\VC\Auxiliary\Build\vcvarsall.bat" amd64 > nul 2>&1
set CMAKE="%VS%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
set NINJA="%VS%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"

set HERE=%~dp0
set BUILD=%HERE%build_%CONFIG%

echo === Configure (%CONFIG%) ===
%CMAKE% -G Ninja -DCMAKE_MAKE_PROGRAM=%NINJA% -DCMAKE_BUILD_TYPE=%CONFIG% "-DWEBRTC_SRC=%WEBRTC_SRC%" "-DABSEIL_SRC=%ABSEIL_SRC%" "-DCHROMIUM_THIRD_PARTY_SRC=%CHROMIUM_SRC%" -S "%HERE%." -B "%BUILD%"
if !ERRORLEVEL! neq 0 exit /b 1

echo === Build ===
%CMAKE% --build "%BUILD%" -- -j%NUMBER_OF_PROCESSORS%
if !ERRORLEVEL! neq 0 exit /b 1

echo === Merge AEC3 + Abseil into one %LIBNAME%.lib ===
set A=%BUILD%\abseil-cpp\absl
lib.exe /NOLOGO /OUT:"%BUILD%\lib\%LIBNAME%_merged.lib" ^
  "%BUILD%\lib\%LIBNAME%.lib" ^
  "%A%\base\absl_base.lib" ^
  "%A%\base\absl_log_severity.lib" ^
  "%A%\base\absl_raw_logging_internal.lib" ^
  "%A%\base\absl_spinlock_wait.lib" ^
  "%A%\base\absl_throw_delegate.lib" ^
  "%A%\numeric\absl_int128.lib" ^
  "%A%\strings\absl_strings.lib" ^
  "%A%\strings\absl_strings_internal.lib" ^
  "%A%\strings\absl_str_format_internal.lib"
if !ERRORLEVEL! neq 0 exit /b 1

echo === Done: %BUILD%\lib\%LIBNAME%_merged.lib ===
echo Copy it to thirdparty\AEC3\lib\%LIBNAME%.lib to use it.
exit /b 0

:usage
echo Usage: build.bat Release^|Debug ^<webrtc checkout^> ^<abseil-cpp checkout^> ^<chromium third_party parent^>
exit /b 1

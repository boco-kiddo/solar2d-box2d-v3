@echo off
rem Builds the Release DLL and packs it as the Solar2D "win32-sim" plugin archive (also used by Win32 desktop builds):
rem   plugins\<publisherId>\plugin.box2dv3\win32-sim\data.tgz
rem Usage: package.bat [publisherId]   (default com.box2dv3)
setlocal
set PUBLISHER=%~1
if "%PUBLISHER%"=="" set PUBLISHER=com.box2dv3

call "%~dp0build.bat" Release
if errorlevel 1 exit /b 1

set OUT=%~dp0..\plugins\%PUBLISHER%\plugin.box2dv3\win32-sim
if not exist "%OUT%" mkdir "%OUT%"
rem tar.exe ships with Windows 10 1803+
tar -czf "%OUT%\data.tgz" -C "%~dp0Release" plugin_box2dv3.dll
if errorlevel 1 (
  echo ERROR: tar failed ^(Windows 10 1803 or newer is needed for tar.exe^)
  exit /b 1
)
echo OK: %OUT%\data.tgz

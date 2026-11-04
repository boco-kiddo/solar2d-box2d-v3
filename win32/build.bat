@echo off
rem Builds plugin_box2dv3.dll without opening Visual Studio and copies it to the Simulator plugins folder.
rem Usage: build.bat [Debug|Release]   (default Debug)
setlocal
set CONFIG=%~1
if "%CONFIG%"=="" set CONFIG=Debug

if "%CORONA_ROOT%"=="" (
  echo ERROR: CORONA_ROOT is not set. It must point to the Solar2D "Native" folder,
  echo        e.g. setx CORONA_ROOT "C:\Program Files (x86)\Corona Labs\Corona\Native"
  exit /b 1
)

set VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe`) do set MSBUILD=%%i
if not exist "%MSBUILD%" (
  echo ERROR: MSBuild not found. Install Visual Studio 2019 or newer with "Desktop development with C++".
  exit /b 1
)

"%MSBUILD%" "%~dp0Plugin.sln" /m /nologo /v:minimal /p:Configuration=%CONFIG% /p:Platform=Win32
if errorlevel 1 (
  echo.
  echo BUILD FAILED
  exit /b 1
)
echo.
echo OK: %~dp0%CONFIG%\plugin_box2dv3.dll copied to "%APPDATA%\Corona Labs\Corona Simulator\Plugins"

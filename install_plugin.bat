@echo off
rem Installs the packed plugin archives (plugins\<publisherId>\plugin.box2dv3\<platform>\data.tgz) into the local
rem Solar2D plugin folder, where the Simulator and the Android / Win32 builders look for self-hosted plugins:
rem   %HOME%\Solar2DPlugins (if HOME is set, e.g. by Git for Windows) else %APPDATA%\Solar2DPlugins
rem Usage: install_plugin.bat [publisherId]   (default com.box2dv3)
setlocal
set PUBLISHER=%~1
if "%PUBLISHER%"=="" set PUBLISHER=com.box2dv3
set SRC=%~dp0plugins\%PUBLISHER%\plugin.box2dv3
if not exist "%SRC%" (
  echo ERROR: %SRC% not found
  exit /b 1
)
if defined HOME (set STORE=%HOME%\Solar2DPlugins) else (set STORE=%APPDATA%\Solar2DPlugins)
set DST=%STORE%\%PUBLISHER%\plugin.box2dv3
for %%P in (win32-sim android) do (
  if exist "%SRC%\%%P\data.tgz" (
    if not exist "%DST%\%%P" mkdir "%DST%\%%P"
    copy /Y "%SRC%\%%P\data.tgz" "%DST%\%%P\data.tgz" >nul
    echo installed %%P -^> %DST%\%%P\data.tgz
  ) else (
    echo skipped %%P: no %SRC%\%%P\data.tgz
  )
)
echo.
echo build.settings:
echo   plugins = { ["plugin.box2dv3"] = { publisherId = "%PUBLISHER%" } },

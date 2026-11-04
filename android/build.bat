@echo off
rem Builds libplugin.box2dv3.so with the Android NDK (ndk-build) and packs android\build\data.tgz.
rem Needs: ANDROID_NDK (NDK folder, r23 or newer recommended) and CORONA_ROOT (Solar2D "Native" folder).
rem The prebuilt plugins\com.box2dv3\plugin.box2dv3\android\data.tgz was made with build_zig.sh instead.
setlocal
if "%ANDROID_NDK%"=="" (
  echo ERROR: set ANDROID_NDK, e.g. set ANDROID_NDK=D:\Android\Sdk\ndk\27.2.12479018
  exit /b 1
)
if "%CORONA_ROOT%"=="" (
  echo ERROR: CORONA_ROOT is not set ^(Solar2D "Native" folder^)
  exit /b 1
)
set HERE=%~dp0
set OUT=%HERE%build
if exist "%OUT%" rmdir /s /q "%OUT%"
mkdir "%OUT%\corona-libs"
rem liblua.so of each ABI from Corona.aar (tar.exe reads zip files)
tar -xf "%CORONA_ROOT%\Corona\android\lib\gradle\Corona.aar" -C "%OUT%\corona-libs" jni/armeabi-v7a/liblua.so jni/arm64-v8a/liblua.so jni/x86_64/liblua.so
if errorlevel 1 (
  echo ERROR: could not read liblua.so from Corona.aar
  exit /b 1
)
call "%ANDROID_NDK%\ndk-build.cmd" NDK_PROJECT_PATH="%HERE%." APP_BUILD_SCRIPT="%HERE%jni\Android.mk" NDK_APPLICATION_MK="%HERE%jni\Application.mk" NDK_OUT="%OUT%\obj" NDK_LIBS_OUT="%OUT%\jniLibs" CORONA_LIBS="%OUT%\corona-libs" CORONA_ROOT="%CORONA_ROOT%" -j8
if errorlevel 1 (
  echo BUILD FAILED
  exit /b 1
)
for /r "%OUT%\jniLibs" %%f in (liblua.so) do if exist "%%f" del "%%f"
copy /Y "%HERE%metadata.lua" "%OUT%\metadata.lua" >nul
tar -czf "%OUT%\data.tgz" -C "%OUT%" metadata.lua jniLibs
set DST=%HERE%..\plugins\com.box2dv3\plugin.box2dv3\android
if not exist "%DST%" mkdir "%DST%"
copy /Y "%OUT%\data.tgz" "%DST%\data.tgz" >nul
echo OK: %DST%\data.tgz

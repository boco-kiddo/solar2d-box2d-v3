# plugin.box2dv3 — cài đặt và build

Hỗ trợ: **Solar2D Simulator trên Windows**, **bản build Win32 (desktop Windows)**, **Android**
(armeabi-v7a, arm64-v8a, x86_64; Android 5.0 / API 21 trở lên; căn trang 16 KB cho Android 15+).
Chưa có: iOS, macOS, HTML5.

```lua
local physics = require( "plugin.box2dv3" )
physics.start()
```

Danh sách API và khác biệt so với `physics` (v2): xem `ROADMAP.md`.

## Cấu trúc

| Thư mục | Nội dung |
|---|---|
| `shared/src`, `shared/include` | Box2D 3.2.0 (không sửa) |
| `shared/plugin` | binding Lua của plugin |
| `win32/` | project Visual Studio (DLL Win32), `build.bat`, `package.bat` |
| `android/` | `build_zig.sh` + `make_sysroot.sh` (build không cần NDK), `build.bat` + `jni/` (build bằng NDK) |
| `plugins/com.box2dv3/plugin.box2dv3/<platform>/data.tgz` | gói plugin đã build, dạng Solar2D cần |
| `install_plugin.bat` | chép các gói vào thư mục plugin cục bộ của Solar2D |
| `Corona/` | app test |
| `test/` | test chạy trên Linux không cần Solar2D |

## Dùng trong một project

1. Tạo gói Windows (cần Visual Studio 2019+ và biến `CORONA_ROOT` như khi build bình thường):
   `win32\package.bat` → `plugins\com.box2dv3\plugin.box2dv3\win32-sim\data.tgz`.
   Gói Android đã có sẵn trong `plugins\...\android\data.tgz`.
2. Chạy `install_plugin.bat`. Gói được chép vào `%APPDATA%\Solar2DPlugins\com.box2dv3\plugin.box2dv3\<platform>\`
   (hoặc `%HOME%\Solar2DPlugins\...` nếu máy có biến `HOME`, ví dụ do Git for Windows đặt — Solar2D ưu tiên `HOME`).
3. Thêm vào `build.settings`:

   ```lua
   settings = {
       plugins = {
           ["plugin.box2dv3"] = { publisherId = "com.box2dv3" },
       },
   }
   ```

   Simulator, bản build Android và Win32 sẽ lấy plugin từ thư mục trên. Bản build Win32 dùng gói `win32-sim`
   (Solar2D tự lấy `win32-sim` khi không có `win32`).

   Muốn đổi publisherId: `win32\package.bat com.ten.cua.ban` rồi `install_plugin.bat com.ten.cua.ban`, và đổi
   thư mục `plugins\com.box2dv3` của gói Android cho khớp.

Khi đang phát triển plugin: `win32\build.bat` chép DLL Debug thẳng vào thư mục plugin của Simulator, nên app test
(`Corona/`) chỉ khai báo plugin cho Android / Win32 (`supportedPlatforms = { android = true, win32 = true }`).

## Build lại cho Windows

`win32\build.bat [Debug|Release]`. Bản Release liên kết tĩnh CRT (`/MT`) để app desktop không cần cài thêm
Visual C++ Redistributable. `win32\package.bat` build Release rồi đóng gói.

## Build lại cho Android

Gói có sẵn được build bằng **Zig** trên Linux, không dùng NDK:

```sh
pip install ziglang            # trình biên dịch (có sẵn libc++)
android/make_sysroot.sh        # header bionic + thư viện stub từ mirror AOSP trên GitHub (cần git, clang, lld)
ANDROID_SYSROOT=android/sysroot \
CORONA_LIBS=<thư mục chứa jni/<abi>/liblua.so, giải nén từ Corona.aar> \
CORONA_INC=<Native>/Corona/shared/include/Corona \
LUA_INC=<Native>/Corona/shared/include/lua \
android/build_zig.sh           # -> android/build/data.tgz
```

`Corona.aar` nằm ở `<Native>\Corona\android\lib\gradle\Corona.aar` (Native = thư mục `CORONA_ROOT`). Chạy được trong
WSL trên Windows. Có thể dùng `toolchains/llvm/prebuilt/<host>/sysroot` của một NDK làm `ANDROID_SYSROOT`.

Hoặc dùng **NDK** trên Windows: đặt `ANDROID_NDK` (khuyên dùng r23 trở lên) và `CORONA_ROOT`, chạy
`android\build.bat` → `plugins\com.box2dv3\plugin.box2dv3\android\data.tgz`. Sau đó chạy lại `install_plugin.bat`.

Gói Android gồm `metadata.lua` (`format = "sharedLibrary"`) và `jniLibs/<abi>/libplugin.box2dv3.so`; thư viện chỉ
phụ thuộc `liblua.so` của Solar2D và libc/libm/libdl của hệ thống (libc++ liên kết tĩnh).

## Ghi chú

- `physics.setThreadCount()` hoạt động trên cả Windows và Android (luồng hệ thống).
- armeabi-v7a: Box2D dùng phép tính vô hướng (không SIMD) trên ARM 32-bit, như upstream. arm64 dùng NEON, x86_64
  dùng SSE2 / AVX2 (tự chọn lúc chạy).

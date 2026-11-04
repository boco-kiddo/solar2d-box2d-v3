#!/bin/bash
# Builds libplugin.box2dv3.so for Android (armeabi-v7a, arm64-v8a, x86_64) with Zig, without the NDK, and packs
# android/build/data.tgz (metadata.lua + jniLibs/<abi>/libplugin.box2dv3.so).
#
# Needs (Linux / macOS / WSL):
#   zig 0.14+            (pip install ziglang   -> python3 -m ziglang)
#   ANDROID_SYSROOT      NDK-style sysroot: usr/include (+ usr/include/<triple>/asm) and usr/lib/<triple>/21/
#                        {libc,libm,libdl}.so, crtbegin_so.o, crtend_so.o. An NDK's
#                        toolchains/llvm/prebuilt/<host>/sysroot works as is.
#   CORONA_LIBS          folder with jni/<abi>/liblua.so (unzip Solar2D's Native/Corona/android/lib/gradle/Corona.aar)
#   CORONA_INC           folder with CoronaLua.h / CoronaMacros.h   (Native/Corona/shared/include/Corona)
#   LUA_INC              folder with lua.h / lauxlib.h               (Native/Corona/shared/include/lua)
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT=${OUT:-$ROOT/android/build}
API=21
ZIG=${ZIG:-"python3 -m ziglang"}
: "${ANDROID_SYSROOT:?set ANDROID_SYSROOT}" "${CORONA_LIBS:?set CORONA_LIBS}" "${CORONA_INC:?set CORONA_INC}" "${LUA_INC:?set LUA_INC}"

ABIS="armeabi-v7a:arm-linux-androideabi:arm-linux-androideabi arm64-v8a:aarch64-linux-android:aarch64-linux-android x86_64:x86_64-linux-android:x86_64-linux-android"
rm -rf "$OUT"; mkdir -p "$OUT"
for entry in $ABIS; do
	IFS=: read -r abi zigt triple <<< "$entry"
	echo "== $abi"
	obj=$OUT/obj/$abi; mkdir -p "$obj" "$OUT/jniLibs/$abi"
	libc=$OUT/libc-$abi.txt
	cat > "$libc" <<EOT
include_dir=$ANDROID_SYSROOT/usr/include
sys_include_dir=$ANDROID_SYSROOT/usr/include/$triple
crt_dir=$ANDROID_SYSROOT/usr/lib/$triple/$API
msvc_lib_dir=
kernel32_lib_dir=
cc_dir=$ANDROID_SYSROOT/usr/lib/$triple/$API
darwin_sdk_dir=
EOT
	export ZIG_LIBC=$libc
	target=$zigt.$API
	flags="-target $target -O2 -fPIC -fno-sanitize=all -ffunction-sections -fdata-sections -fvisibility=hidden -DNDEBUG -DRtt_ANDROID_ENV"
	[ "$abi" = armeabi-v7a ] && flags="$flags -mcpu=generic+v7a+neon+vfp3 -mfloat-abi=softfp"
	pids=""
	for f in "$ROOT"/shared/src/*.c; do
		$ZIG cc $flags -std=c17 -I"$ROOT/shared/include" -c "$f" -o "$obj/$(basename "$f" .c).o" & pids="$pids $!"
	done
	for f in "$ROOT"/shared/plugin/*.cpp; do
		$ZIG c++ $flags -std=c++17 -fno-exceptions -fno-rtti -I"$ROOT/shared/include" -I"$ROOT/shared/plugin" -I"$CORONA_INC" -I"$LUA_INC" \
			-c "$f" -o "$obj/$(basename "$f" .cpp).o" & pids="$pids $!"
	done
	for p in $pids; do wait "$p"; done
	$ZIG c++ -target $target -shared -O2 -fno-sanitize=all -Wl,--gc-sections -Wl,-z,max-page-size=16384 -Wl,-soname,libplugin.box2dv3.so \
		-s -o "$OUT/jniLibs/$abi/libplugin.box2dv3.so" "$obj"/*.o "$CORONA_LIBS/jni/$abi/liblua.so" 2>/dev/null \
	|| $ZIG c++ -target $target -shared -O2 -fno-sanitize=all -Wl,--gc-sections -Wl,-z,max-page-size=16384 -Wl,-soname,libplugin.box2dv3.so \
		-s -o "$OUT/jniLibs/$abi/libplugin.box2dv3.so" "$obj"/*.o "$CORONA_LIBS/jni/$abi/liblua.so"
done
cp "$ROOT/android/metadata.lua" "$OUT/metadata.lua"
( cd "$OUT" && tar --owner=0 --group=0 --numeric-owner -czf data.tgz metadata.lua jniLibs )
echo "OK: $OUT/data.tgz"

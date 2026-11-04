#!/bin/bash
# Creates a minimal NDK-style sysroot for android/build_zig.sh without downloading the NDK:
# bionic + kernel headers from the AOSP mirror on GitHub, stub libc/libm/libdl.so generated from bionic's symbol
# maps (link-time only, like the NDK's stubs), crtbegin_so.o / crtend_so.o. API level 21.
# usage: android/make_sysroot.sh [OUT_DIR]   (default android/sysroot)
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${1:-$HERE/sysroot}
WORK=$OUT.work
CLANG=${CLANG:-clang}
API=21
mkdir -p "$WORK"
if [ ! -d "$WORK/bionic" ]; then
	git clone --depth 1 --filter=blob:none --sparse https://github.com/aosp-mirror/platform_bionic "$WORK/bionic"
	git -C "$WORK/bionic" sparse-checkout set --no-cone /libc/include/ /libc/kernel/ /libc/libc.map.txt \
		/libm/libm.map.txt /libdl/libdl.map.txt /libc/arch-common/
fi
B=$WORK/bionic/libc
INC=$OUT/usr/include
rm -rf "$OUT/usr"; mkdir -p "$INC"
cp -r "$B/include/." "$INC/"
for d in "$B"/kernel/uapi/*; do
	case "$(basename "$d")" in asm-generic) cp -r "$d" "$INC/" ;; asm-*) ;; *) cp -r "$d" "$INC/" ;; esac
done
cp -r "$B/kernel/android/uapi/." "$INC/"
for t in aarch64-linux-android:arm64 arm-linux-androideabi:arm x86_64-linux-android:x86 i686-linux-android:x86; do
	mkdir -p "$INC/${t%%:*}"; cp -r "$B/kernel/uapi/asm-${t##*:}/asm" "$INC/${t%%:*}/asm"
done
for l in libc:libc/libc.map.txt libm:libm/libm.map.txt libdl:libdl/libdl.map.txt; do
	python3 "$HERE/genstub.py" "$WORK/bionic/${l##*:}" "$WORK/${l%%:*}.c" "$WORK/${l%%:*}.map"
done
for t in aarch64-linux-android:aarch64-linux-android armv7a-linux-androideabi:arm-linux-androideabi x86_64-linux-android:x86_64-linux-android; do
	tgt=${t%%:*}$API; tr=${t##*:}; lib=$OUT/usr/lib/$tr/$API; mkdir -p "$lib"
	for n in libc libm libdl; do
		$CLANG --target=$tgt -fPIC -shared -nostdlib -fno-builtin -w -fuse-ld=lld -Wl,--version-script="$WORK/$n.map" \
			-Wl,--undefined-version -Wl,-soname,$n.so "$WORK/$n.c" -o "$lib/$n.so" 2>/dev/null
	done
	$CLANG --target=$tgt --sysroot="$OUT" -isystem "$INC/$tr" -fPIC -O2 -I"$B/arch-common/bionic" \
		-c "$B/arch-common/bionic/crtbegin_so.c" -o "$lib/crtbegin_so.o"
	$CLANG --target=$tgt -c "$HERE/crtend_so.S" -o "$lib/crtend_so.o"
done
echo "OK: $OUT"

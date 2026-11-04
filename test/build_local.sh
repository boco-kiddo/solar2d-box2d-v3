#!/bin/sh
# Builds plugin/box2d.so for the local Lua 5.1 test harness (Linux host, not shipped).
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT=$ROOT/test/build; mkdir -p $OUT $ROOT/test/plugin
# CORONA_INC: folder with CoronaLua.h / CoronaMacros.h ; LUA_INC: Lua 5.1 headers (lua.h ...)
CORONA=${CORONA_INC:-/home/claude/ref/corona/librtt/Corona}
LUA=${LUA_INC:-/home/claude/env/lua}
for f in $ROOT/shared/src/*.c; do
  gcc -std=c17 -O2 -g -fPIC -I$ROOT/shared/include -c $f -o $OUT/$(basename $f .c).o &
done; wait
g++ -std=c++17 -O1 -g -fPIC -Wall -Wextra -Wno-unused-parameter -I$ROOT/shared/include -I$ROOT/shared/plugin -I$CORONA -I$LUA \
  -shared -o $ROOT/test/plugin/box2dv3.so $ROOT/shared/plugin/*.cpp $OUT/*.o
echo built $ROOT/test/plugin/box2dv3.so

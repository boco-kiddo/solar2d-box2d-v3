# plugin.box2dv3

Box2D **3.2** physics for [Solar2D](https://solar2d.com), with the same syntax as the built-in `physics`
library (Box2D 2.x). Existing code mostly works by changing one line:

```lua
local physics = require( "plugin.box2dv3" )   -- instead of require( "physics" )
```

On top of the v2 API it adds the new Box2D v3 features: capsules, rounded polygons, rolling resistance,
conveyor belts, kinematic target transforms, explosions, impact (`"hit"`) events, shape casts, a character
mover, motor and breakable joints, world snapshots and multithreading.

- **Platforms:** Solar2D Simulator on Windows, Win32 desktop builds, Android (armeabi-v7a, arm64-v8a, x86_64,
  Android 5.0 / API 21+, 16 KB page aligned).
- **Not yet:** iOS, macOS, HTML5.
- **Full API reference:** [docs/API.md](docs/API.md)

## Contents

1. [Quick start](#quick-start)
2. [Installing the plugin](#installing-the-plugin)
3. [Moving from the built-in physics library](#moving-from-the-built-in-physics-library)
4. [Using the v3 features](#using-the-v3-features)
5. [Performance tips](#performance-tips)
6. [Building the plugin](#building-the-plugin)
7. [Project layout](#project-layout)
8. [Testing](#testing)

## Quick start

```lua
local physics = require( "plugin.box2dv3" )
physics.start()

local ground = display.newRect( display.contentCenterX, 300, 480, 20 )
physics.addBody( ground, "static", { friction = 0.6 } )

local crate = display.newRect( 240, 50, 40, 40 )
physics.addBody( crate, { density = 1, bounce = 0.2, cornerRadius = 4 } )

crate:addEventListener( "collision", function( event )
    if event.phase == "began" then print( "hit", event.other ) end
end )
```

All values use pixels, degrees, pixels/second, degrees/second, Newtons and kg — see
[Units](docs/API.md#units).

## Installing the plugin

The plugin is self-hosted: Solar2D finds it in a local plugin folder.

1. **Get the packages.** The Android package is prebuilt in
   `plugins/com.box2dv3/plugin.box2dv3/android/data.tgz`. Make the Windows package with
   `win32\package.bat` (needs Visual Studio 2019+ and `CORONA_ROOT`, see
   [Building](#building-the-plugin)) → `plugins/.../win32-sim/data.tgz`.
2. **Install them.** Run `install_plugin.bat`. It copies the packages to
   `%APPDATA%\Solar2DPlugins\com.box2dv3\plugin.box2dv3\<platform>\data.tgz`
   (or `%HOME%\Solar2DPlugins\…` when a `HOME` variable is set, e.g. by Git for Windows — Solar2D uses `HOME`
   first).
3. **Declare it in `build.settings`:**

   ```lua
   settings = {
       plugins = {
           ["plugin.box2dv3"] = { publisherId = "com.box2dv3" },
       },
   }
   ```

The Simulator, Android builds and Win32 builds then use the plugin. Win32 builds use the `win32-sim` package.

To use another publisher id, pass it to both scripts (`win32\package.bat com.example`,
`install_plugin.bat com.example`) and rename the `plugins\com.box2dv3` folder of the Android package to match.

## Moving from the built-in physics library

Most code runs unchanged. Check these points (the full list is in
[docs/API.md](docs/API.md#differences-from-the-built-in-physics-library)):

| Built-in `physics` | `plugin.box2dv3` |
|---|---|
| `setVelocityIterations`, `setPositionIterations` | no effect — use `physics.setSubSteps( n )` (default 4) |
| default material differs between cases | always density 1, friction 0.3, bounce 0.2 |
| collision `x, y` local to object1 | content coordinates (`setReportCollisionsInContentCoordinates( false )` for the old behaviour) |
| changing the world in `collision` needs `timer.performWithDelay` | allowed directly: events are dispatched after the step |
| `pulley`, `gear` joints | not available in Box2D v3 |
| sensors detect sensors | v3 sensors ignore other sensors |
| `applyAngularImpulse`, `getWorldVector`, `getInertia`, `getLinearVelocityFrom*Point`, wheel `motorSpeed`, rope `maxLength` | fixed to use consistent units |

`display.newLine`, `display.newPolygon` and `graphics.newOutline` objects can get bodies as before, but
`require( "plugin.box2dv3" )` must run **before** they are created (the plugin records their vertices).

## Using the v3 features

### Shapes

```lua
physics.addBody( pill, { capsule = true } )                       -- capsule filling the object
physics.addBody( rock, { shape = { -20,-10, 20,-15, 25,10, -15,15 }, cornerRadius = 3 } )
physics.addBody( wheel, { radius = 12, rollingResistance = 0.1 } )
physics.addBody( belt, "static", { tangentSpeed = 100 } )          -- conveyor belt, px/s
belt.tangentSpeed = -100                                           -- reverse it at run time
```

### Bodies

```lua
elevator.isFixedX = true                                           -- move only vertically
paddle:setTargetTransform( x, y, angle )                           -- kinematic: pushes instead of teleporting
ball:applyLinearImpulseToCenter( 0, -3 )
```

### Impacts and explosions

```lua
physics.setHitEventThreshold( 200 )                                -- px/s
Runtime:addEventListener( "hit", function( e ) playThud( e.approachSpeed ) end )
physics.explode( x, y, 80, 2 )                                     -- radius px, N·s per meter
```

### Queries

```lua
local hits = physics.circleCast( x1, y1, x2, y2, 10 )               -- where would a ball of radius 10 hit?
local near = physics.queryCircle( x, y, 50 )                        -- objects within 50 px
```

### Character mover

```lua
local hero = display.newRect( 60, 200, 16, 32 )
local mover = physics.newMover( hero, { maxSpeed = 140, jumpHeight = 70 } )
mover.throttle = 1          -- run right
mover:jump()
```

### Joints

```lua
local m = physics.newJoint( "motor", anchor, platform )            -- drives platform relative to anchor
m:setLinearOffset( 0, 80 )

local link = physics.newJoint( "pivot", a, b, x, y )
link.breakForce = 300                                              -- N; "jointBreak" event, then removed
```

### Snapshots

```lua
local checkpoint = physics.saveState()
for _, o in ipairs( physics.restoreState( checkpoint ) ) do display.remove( o ) end
```

## Performance tips

- Rendering usually costs more than physics. Compare `physics.getStats().stepTime` with the frame time.
- Tall stacks or large mass ratios sink a little with soft v3 contacts. Raise the stiffness:
  `physics.setSubSteps( 8 ); physics.setContactTuning( 60 )`.
- Large scenes (hundreds of awake bodies) benefit from `physics.setThreadCount( 4 )`. A `preCollision`
  listener forces a single thread while it exists.
- Pre-solve, post-solve and hit events only cost time when someone listens to them.

## Building the plugin

### Windows (Simulator and Win32)

Needs Visual Studio 2019 or newer (toolset v142+) and the `CORONA_ROOT` environment variable pointing to
Solar2D's `Native` folder.

```bat
win32\build.bat            :: Debug, copies the DLL to the Simulator plugin folder
win32\build.bat Release
win32\package.bat          :: Release + plugins\com.box2dv3\plugin.box2dv3\win32-sim\data.tgz
```

The Release DLL links the C runtime statically (`/MT`), so desktop apps need no Visual C++ redistributable.
The project compiles Box2D's C11 sources with MSVC through a small compatibility header
(`shared/plugin/msvc/`).

### Android

The prebuilt package was made with Zig on Linux, without the NDK:

```sh
pip install ziglang
android/make_sysroot.sh          # bionic headers + link stubs from the AOSP mirror on GitHub (needs git, clang, lld)
ANDROID_SYSROOT=android/sysroot \
CORONA_LIBS=<folder with jni/<abi>/liblua.so from Corona.aar> \
CORONA_INC=<Native>/Corona/shared/include/Corona \
LUA_INC=<Native>/Corona/shared/include/lua \
android/build_zig.sh             # -> android/build/data.tgz
```

`Corona.aar` is `<Native>\Corona\android\lib\gradle\Corona.aar` (it is a zip file). This works in WSL. An NDK's
`toolchains/llvm/prebuilt/<host>/sysroot` can be used as `ANDROID_SYSROOT`.

With the NDK on Windows instead: set `ANDROID_NDK` (r23+ recommended) and `CORONA_ROOT`, then run
`android\build.bat`.

The package contains `metadata.lua` and `jniLibs/<abi>/libplugin.box2dv3.so`; the library depends only on
Solar2D's `liblua.so` and the system libc / libm / libdl (libc++ is linked statically). After rebuilding,
run `install_plugin.bat` again.

## Project layout

| Path | Contents |
|---|---|
| `shared/src`, `shared/include` | Box2D 3.2.0, unmodified |
| `shared/plugin` | the Lua binding (`b2plugin_*.cpp`) |
| `win32/` | Visual Studio project, `build.bat`, `package.bat` |
| `android/` | `build_zig.sh`, `make_sysroot.sh`, NDK build (`build.bat`, `jni/`), `metadata.lua` |
| `plugins/<publisherId>/plugin.box2dv3/<platform>/data.tgz` | packaged plugin |
| `install_plugin.bat` | installs the packages into the local Solar2D plugin folder |
| `Corona/` | test app: sandbox, benchmarks (v2 vs v3), one scene and self-test per feature group |
| `test/` | tests that run on Linux with a mock of Solar2D's display objects |
| `docs/API.md` | API reference |
| `docs/ROADMAP.md`, `docs/HUONG_DAN.md` | development notes (Vietnamese) |

## Testing

**In Solar2D:** open `Corona/` in the Simulator. The buttons open the scenes of each feature group and run
their self-tests (results in the console). `bench v2` / `bench v3` compare the built-in library with the plugin.

**Without Solar2D** (Linux / macOS):

```sh
cd test
CORONA_INC=<Corona headers> LUA_INC=<Lua 5.1 headers> sh build_local.sh
for t in test_step*.lua; do lua5.1 $t; done
```

## Credits

Box2D © Erin Catto, MIT license (`shared/src`, `shared/include`). The character mover is a port of Box2D's
`samples/geometric_mover.cpp`.

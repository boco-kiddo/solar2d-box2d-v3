-- Step 1 tests: run with  lua51 test_step1.lua  from the test directory.
package.cpath = "./?.so;" .. package.cpath
dofile( "mock_solar2d.lua" )

local physics = require( "plugin.box2dv3" )
local passed, failed = 0, 0
local function check( cond, msg )
	if cond then passed = passed + 1 else failed = failed + 1; print( "FAIL: " .. msg ) end
end
local function near( a, b, eps ) return math.abs( a - b ) <= ( eps or 0.5 ) end

print( physics.engineVersion, physics.version )

-- addBody before start must fail like v2
check( not pcall( physics.addBody, display.newRect( 0, 0, 10, 10 ), "dynamic" ), "addBody before start errors" )

physics.start()
check( Runtime.listenerCount( "enterFrame" ) == 1, "enterFrame listener registered" )
local gx, gy = physics.getGravity()
check( gx == 0 and near( gy, 9.8, 1e-4 ), "default gravity 0, 9.8" )

-- 1. free fall: after 1 s, y ≈ 0.5*g*t^2 meters (semi-implicit Euler is a bit ahead)
local box = display.newRect( 100, 0, 20, 20 )
check( physics.addBody( box, "dynamic" ) == true, "addBody returns true" )
check( isPhysicsMt( box ), "object got physics metatable" )
check( box.bodyType == "dynamic", "bodyType getter" )
advanceFrames( 60 )
local expected = 0.5 * 9.8 * 1 * 30
check( near( box.y, expected, 6 ), "free fall y=" .. box.y .. " expected≈" .. expected )
check( near( box.x, 100, 1e-3 ), "x unchanged during fall" )
local vx, vy = box:getLinearVelocity()
check( near( vy, 9.8 * 30, 3 ), "velocity ≈ g*t (px/s) got " .. vy )

-- 2. ground stops a falling box
local ground = display.newRect( 160, 400, 320, 20 )
physics.addBody( ground, "static", { friction = 0.5, bounce = 0 } )
local crate = display.newRect( 160, 300, 40, 40 )
physics.addBody( crate, { density = 1, friction = 0.3, bounce = 0 } )
advanceFrames( 180 )
check( near( crate.y, 400 - 10 - 20, 1.5 ), "crate rests on ground y=" .. crate.y )
check( crate.isAwake == false, "crate fell asleep" )

-- 3. teleport through obj.x / obj.y like v2
crate.x = 50
check( crate.isAwake == true, "teleport wakes body" )
advanceFrames( 1 )
check( near( crate.x, 50, 0.5 ), "crate.x teleported, x=" .. crate.x )
crate:translate( 10, 0 )
advanceFrames( 1 )
check( near( crate.x, 60, 0.5 ), "translate moves body, x=" .. crate.x )

-- 4. rotation is synced and cumulative
local wheel = display.newCircle( 400, 100, 10 )
physics.addBody( wheel, "kinematic", { radius = 10 } )
wheel.angularVelocity = 360 -- deg/s
check( near( wheel.angularVelocity, 360, 1e-2 ), "angularVelocity getter" )
advanceFrames( 120 )
check( near( wheel.rotation, 720, 2 ), "cumulative rotation ≈ 720, got " .. wheel.rotation )
wheel.rotation = 0
advanceFrames( 1 )
check( near( wheel.rotation, 6, 0.5 ), "rotation teleport then continues, got " .. wheel.rotation )

-- 5. setLinearVelocity on kinematic
local k = display.newRect( 0, 0, 10, 10 )
physics.addBody( k, "kinematic" )
k:setLinearVelocity( 60, 0 )
advanceFrames( 60 )
check( near( k.x, 60, 0.5 ), "kinematic moves 60px in 1s, x=" .. k.x )

-- 6. bodyType switch
k.bodyType = "dynamic"
check( k.bodyType == "dynamic", "bodyType setter" )

-- 7. removeSelf removes the body automatically
local before = physics.getStats().bodyCount
box:removeSelf()
check( physics.getStats().bodyCount == before - 1, "removeSelf destroys body" )
advanceFrames( 2 ) -- must not touch the removed object

-- 8. removeBody restores the object
check( physics.removeBody( k ) == true, "removeBody returns true" )
check( not isPhysicsMt( k ), "metatable restored after removeBody" )
check( k.bodyType == nil, "physics props gone after removeBody" )
local kx = k.x
advanceFrames( 5 )
check( k.x == kx, "object no longer simulated" )
check( physics.removeBody( k ) == false, "second removeBody returns false" )

-- 9. anchor offsets: top-left anchored box resting on ground
local tl = display.newRect( 200, 300, 40, 40 )
tl.anchorX, tl.anchorY = 0, 0
physics.addBody( tl, { density = 1, bounce = 0 } )
advanceFrames( 180 )
check( near( tl.y, 390 - 40, 1.5 ), "top-left anchored box rests with top at 350, got " .. tl.y )

-- 10. multi element + shape + box params, invalid shape errors
local multi = display.newRect( 300, 0, 10, 10 )
check( pcall( physics.addBody, multi, "dynamic",
	{ shape = { -10, -10, 10, -10, 10, 10, -10, 10 } },
	{ box = { halfWidth = 5, halfHeight = 20, x = 20, y = 0, angle = 45 } },
	{ circle = { radius = 5, x = -20, y = 0 } } ), "multi element body" )
check( physics.getStats().shapeCount >= 3, "shapes created" )
check( not pcall( physics.addBody, display.newRect( 0, 0, 1, 1 ), { shape = { 0, 0, 1, 1, 2, 2 } } ), "degenerate shape rejected" )
check( not pcall( physics.addBody, display.newRect( 0, 0, 1, 1 ), { shape = { 0, 0, 1, 0 } } ), "too few vertices rejected" )
check( not pcall( physics.addBody, {}, "dynamic" ), "non display object rejected" )

-- 11. group removal removes children bodies
local g = display.newGroup()
local c1 = display.newRect( 0, 0, 10, 10 ); g:insert( c1 )
physics.addBody( c1 )
before = physics.getStats().bodyCount
g:removeSelf()
advanceFrames( 1 ) -- children are finalized when Solar2D collects the orphaned group
check( physics.getStats().bodyCount == before - 1, "removing parent group removes child body" )

-- 12. pause / start / stop
physics.pause()
local cy = crate.y
crate.y = 0
advanceFrames( 10 )
check( crate.y == 0, "paused world does not step" )
physics.start()
advanceFrames( 10 )
check( crate.y > 0, "restarted world steps" )
check( physics.stop() == true, "stop returns true" )
check( Runtime.listenerCount( "enterFrame" ) == 0, "enterFrame removed on stop" )
check( not isPhysicsMt( crate ), "stop restores objects" )
check( physics.getStats().bodyCount == 0, "no bodies after stop" )
check( physics.stop() == false, "second stop returns false" )

-- 13. restart works after stop
physics.start()
local r = display.newRect( 0, 0, 10, 10 )
physics.addBody( r )
advanceFrames( 10 )
check( r.y > 0, "world works after restart" )
physics.stop()

print( string.format( "\n%d passed, %d failed", passed, failed ) )
os.exit( failed == 0 and 0 or 1 )

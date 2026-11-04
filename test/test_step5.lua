-- Step 5 tests: rayCast, reflectRay, queryRegion, queryPoint, setDrawMode.
-- Run with  lua51 test_step5.lua  from the test directory.
package.cpath = "./?.so;" .. package.cpath
dofile( "mock_solar2d.lua" )

local physics = require( "plugin.box2dv3" )
local passed, failed = 0, 0
local function check( cond, msg )
	if cond then passed = passed + 1 else failed = failed + 1; print( "FAIL: " .. msg ) end
end
local function near( a, b, eps ) return a ~= nil and b ~= nil and math.abs( a - b ) <= ( eps or 0.5 ) end

physics.start()
physics.setGravity( 0, 0 )

-- three boxes in a row along y = 100, a sensor and a circle
local b1 = display.newRect( 100, 100, 20, 20 ); physics.addBody( b1, "static" )
local b2 = display.newRect( 200, 100, 20, 20 ); physics.addBody( b2, "static" )
local b3 = display.newRect( 300, 100, 20, 20 ); physics.addBody( b3, "static" )
local zone = display.newRect( 150, 100, 20, 60 ); physics.addBody( zone, "static", { isSensor = true } )
local multi = display.newRect( 100, 300, 10, 10 )
physics.addBody( multi, "static", { box = { halfWidth = 10, halfHeight = 10, x = -15 } }, { box = { halfWidth = 10, halfHeight = 10, x = 15 } } )
local masked = display.newRect( 250, 100, 10, 10 )
physics.addBody( masked, "static", { filter = { categoryBits = 4, maskBits = 0xFFFF } } )

-- ---------------------------------------------------------------- rayCast
local hits = physics.rayCast( 0, 100, 400, 100 )
check( hits and #hits == 1, "closest returns one hit" )
if hits then
	local h = hits[1]
	check( h.object == b1, "closest hit is the first box" )
	check( near( h.position.x, 90, 0.01 ) and near( h.position.y, 100, 0.01 ), "hit position in pixels (" .. h.position.x .. ")" )
	check( near( h.normal.x, -1, 1e-4 ) and near( h.normal.y, 0, 1e-4 ), "hit normal" )
	check( near( h.fraction, 90 / 400, 1e-4 ), "hit fraction" )
	check( h.element == 1, "hit element index (new)" )
end

local sorted = physics.rayCast( 0, 100, 400, 100, "sorted" )
check( sorted and #sorted == 5, "sorted returns all hits incl. sensor (" .. ( sorted and #sorted or 0 ) .. ")" )
if sorted then
	local order = {}
	for i, h in ipairs( sorted ) do order[i] = h.object end
	check( order[1] == b1 and order[2] == zone and order[3] == b2 and order[4] == masked and order[5] == b3, "sorted by distance" )
	local ok = true
	for i = 2, #sorted do ok = ok and sorted[i - 1].fraction <= sorted[i].fraction end
	check( ok, "fractions increasing" )
end
local unsorted = physics.rayCast( 0, 100, 400, 100, "unsorted" )
check( unsorted and #unsorted == 5, "unsorted returns all hits" )
local any = physics.rayCast( 0, 100, 400, 100, "any" )
check( any and #any == 1, "any returns one hit" )
check( physics.rayCast( 0, 0, 400, 0 ) == nil, "no hit returns nil" )
local noSensor = physics.rayCast( 0, 100, 400, 100, "sorted", { ignoreSensors = true } )
check( noSensor and #noSensor == 4, "ignoreSensors option" )
local onlyMasked = physics.rayCast( 0, 100, 400, 100, "sorted", { maskBits = 4 } )
check( onlyMasked and #onlyMasked == 1 and onlyMasked[1].object == masked, "maskBits option" )
local closestNoSensor = physics.rayCast( 120, 100, 400, 100, { ignoreSensors = true } )
check( closestNoSensor and closestNoSensor[1].object == b2, "options as 5th argument (closest)" )

-- ---------------------------------------------------------------- reflectRay
local h = hits[1]
local rx, ry = physics.reflectRay( 0, 100, h )
check( near( rx, -1, 1e-4 ) and near( ry, 0, 1e-4 ), "reflectRay head-on" )
local diag = physics.rayCast( 40, 50, 140, 150 )
local dx, dy = physics.reflectRay( 40, 50, diag[1] )
check( near( dx, -math.sqrt( 0.5 ), 1e-3 ) and near( dy, math.sqrt( 0.5 ), 1e-3 ), "reflectRay at 45 degrees (" .. dx .. ", " .. dy .. ")" )

-- ---------------------------------------------------------------- queryRegion / queryPoint
local function set( list ) local s = {}; for _, o in ipairs( list or {} ) do s[o] = true end; return s end
local r = physics.queryRegion( 80, 80, 220, 120 )
local rs = set( r )
check( r and #r == 3 and rs[b1] and rs[b2] and rs[zone], "queryRegion finds overlapping objects" )
check( physics.queryRegion( 111, 80, 139, 120 ) == nil, "exact test: gap between shapes is empty (v2 used bounding boxes)" )
local reversed = physics.queryRegion( 220, 120, 80, 80 )
check( reversed and #reversed == 3, "corner order does not matter" )
local m = physics.queryRegion( 70, 290, 130, 310 )
check( m and #m == 1 and m[1] == multi, "multi-element body reported once" )
check( #( physics.queryRegion( 80, 80, 220, 120, { ignoreSensors = true } ) or {} ) == 2, "queryRegion ignoreSensors" )
local p = physics.queryPoint( 200, 100 )
check( p and #p == 1 and p[1] == b2, "queryPoint inside a box" )
check( physics.queryPoint( 200, 120 ) == nil, "queryPoint outside" )

-- circle exactness
local ball = display.newCircle( 400, 300, 20 )
physics.addBody( ball, "static", { radius = 20 } )
check( physics.queryPoint( 400 + 15, 300 + 15 ) == nil, "queryPoint respects the circle (corner of its box)" )
check( physics.queryPoint( 410, 305 ) ~= nil, "queryPoint inside the circle" )

-- removed objects are not reported
display.remove( b2 )
check( physics.queryPoint( 200, 100 ) == nil, "removed object not reported" )
local afterRemove = physics.rayCast( 0, 100, 400, 100, "sorted" )
check( #afterRemove == 4, "rayCast skips removed object" )

-- ---------------------------------------------------------------- debug draw
physics.setDrawMode( "hybrid" )
check( physics.getDrawMode() == "hybrid", "getDrawMode" )
advanceFrames( 1 )
physics.setDrawMode( "debug", true )
advanceFrames( 2 )
physics.pause()
advanceFrames( 2 )
physics.start()
check( not pcall( physics.setDrawMode, "wireframe" ), "invalid draw mode errors" )
physics.setDrawMode( "normal" )
check( physics.getDrawMode() == "normal", "back to normal" )

physics.stop()
check( not pcall( physics.rayCast, 0, 0, 1, 1 ), "rayCast needs a world" )
print( string.format( "\n%d passed, %d failed", passed, failed ) )
os.exit( failed == 0 and 0 or 1 )

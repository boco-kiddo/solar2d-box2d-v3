-- Step 2 tests: body properties / methods, chain, line, polygon, outline, MKS.
-- Run with  lua51 test_step2.lua  from the test directory.
package.cpath = "./?.so;" .. package.cpath
dofile( "mock_solar2d.lua" )

local physics = require( "plugin.box2dv3" )
local passed, failed = 0, 0
local function check( cond, msg )
	if cond then passed = passed + 1 else failed = failed + 1; print( "FAIL: " .. msg ) end
end
local function near( a, b, eps ) return math.abs( a - b ) <= ( eps or 0.5 ) end
local function fresh()
	physics.stop()
	physics.start()
	physics.setGravity( 0, 9.8 )
end

-- ---------------------------------------------------------------- properties
fresh()
local b = display.newRect( 0, 0, 30, 30 ) -- 1 m x 1 m at scale 30
physics.addBody( b, { density = 2 } )
check( near( b.mass, 2, 1e-4 ), "mass = density * area (" .. b.mass .. ")" )
b.isBullet = true;           check( b.isBullet == true, "isBullet" )
b.isSleepingAllowed = false; check( b.isSleepingAllowed == false, "isSleepingAllowed" )
b.linearDamping = 0.5;       check( near( b.linearDamping, 0.5, 1e-6 ), "linearDamping" )
b.angularDamping = 0.25;     check( near( b.angularDamping, 0.25, 1e-6 ), "angularDamping" )
b.gravityScale = 0;          check( b.gravityScale == 0, "gravityScale get" )
check( b.isSensor == false, "isSensor getter false" )
b.linearDamping = 0
advanceFrames( 30 )
check( near( b.y, 0, 1e-3 ), "gravityScale 0: no fall (y=" .. b.y .. ")" )
b.gravityScale = 1
advanceFrames( 30 )
check( b.y > 10, "gravityScale 1: falls again" )

-- fixed rotation
local fr = display.newRect( 100, 0, 30, 30 )
physics.addBody( fr, { density = 1 } )
fr.gravityScale = 0
fr.isFixedRotation = true
check( fr.isFixedRotation == true, "isFixedRotation get" )
fr:applyTorque( 50 )
advanceFrames( 30 )
check( near( fr.rotation, 0, 1e-3 ), "fixed rotation ignores torque" )
fr.isFixedRotation = false
fr:applyTorque( 50 )
advanceFrames( 10 )
check( math.abs( fr.rotation ) > 1, "rotation unlocked" )

-- ---------------------------------------------------------------- methods
fresh()
physics.setGravity( 0, 0 )
local m = display.newRect( 0, 0, 30, 30 )
physics.addBody( m, { density = 1 } ) -- mass 1 kg
local cx, cy = m:getMassWorldCenter()
check( near( cx, 0, 1e-3 ) and near( cy, 0, 1e-3 ), "getMassWorldCenter" )
m:applyLinearImpulse( 2, 0, m.x, m.y ) -- dv = J / m = 2 m/s = 60 px/s
local vx, vy = m:getLinearVelocity()
check( near( vx, 60, 1e-2 ) and near( vy, 0, 1e-3 ), "applyLinearImpulse -> 60 px/s (" .. vx .. ")" )
m:setLinearVelocity( 0, 0 )
for _ = 1, 60 do m:applyForce( 1, 0, m.x, m.y ); advanceFrames( 1 ) end -- a = 1 m/s^2 for 1 s
vx = m:getLinearVelocity()
check( near( vx, 30, 1 ), "applyForce 1 N for 1 s -> ~30 px/s (" .. vx .. ")" )
m:setLinearVelocity( 0, 0 )
m.angularVelocity = 0
check( near( m:getInertia(), 1 / 6, 1e-4 ), "getInertia = m (w^2 + h^2) / 12 = " .. m:getInertia() )
m:applyAngularImpulse( 1 / 6 * math.pi ) -- dw = J / I = pi rad/s = 180 deg/s
check( near( m.angularVelocity, 180, 0.01 ), "applyAngularImpulse is a real impulse (" .. m.angularVelocity .. ")" )
m.angularVelocity = 0
m:applyTorque( 1 )
check( m.angularVelocity == 0, "torque only acts during the step" )
advanceFrames( 1 )
check( m.angularVelocity ~= 0, "torque spins body" )
m.angularVelocity = 0
m.rotation = 90
local wx, wy = m:getWorldVector( 30, 0 )
check( near( wx, 0, 1e-3 ) and near( wy, 30, 1e-3 ), "getWorldVector rotates, same units (" .. wx .. ", " .. wy .. ")" )
m.rotation = 0
m:setLinearVelocity( 30, 0 )
local pvx = m:getLinearVelocityFromWorldPoint( m.x, m.y )
check( near( pvx, 30, 1e-3 ), "getLinearVelocityFromWorldPoint in px/s (" .. pvx .. ")" )
local lvx = m:getLinearVelocityFromLocalPoint( 0, 0 )
check( near( lvx, 30, 1e-3 ), "getLinearVelocityFromLocalPoint in px/s" )
m:setLinearVelocity( 0, 0 )

local an = display.newRect( 200, 200, 30, 30 )
an.anchorX, an.anchorY = 0, 0
physics.addBody( an, { density = 1 } )
local lx, ly = an:getMassLocalCenter()
check( near( lx, 15, 1e-3 ) and near( ly, 15, 1e-3 ), "getMassLocalCenter with anchor 0,0" )
an:resetMassData()
check( near( an.mass, 1, 1e-4 ), "resetMassData keeps mass" )

-- ---------------------------------------------------------------- material defaults
fresh()
local def1 = display.newRect( 0, 0, 30, 30 )
physics.addBody( def1 )
local def2 = display.newRect( 50, 0, 30, 30 )
physics.addBody( def2, { bounce = 0.5 } )
check( near( def1.mass, 1, 1e-4 ) and near( def2.mass, 1, 1e-4 ), "default density 1 with or without element table" )

-- ---------------------------------------------------------------- isSensor toggle
fresh()
local ground = display.newRect( 160, 400, 320, 20 )
physics.addBody( ground, "static", { friction = 0.5, bounce = 0 } )
local s = display.newRect( 160, 300, 30, 30 )
physics.addBody( s, { density = 1, bounce = 0 } )
advanceFrames( 120 )
check( near( s.y, 375, 1.5 ), "box rests on ground before sensor (y=" .. s.y .. ")" )
local massBefore = s.mass
s.isSensor = true
check( s.isSensor == true, "isSensor getter true" )
check( near( s.mass, massBefore, 1e-4 ), "mass kept after isSensor rebuild" )
s.isAwake = true
advanceFrames( 30 )
check( s.y > 400, "sensor body falls through ground (y=" .. s.y .. ")" )
s.isSensor = false
check( s.isSensor == false, "isSensor back to false" )

-- ---------------------------------------------------------------- chain (two-sided)
fresh()
local chainObj = display.newRect( 0, 0, 1, 1 )
physics.addBody( chainObj, "static", { chain = { 0, 300, 160, 320, 320, 300 }, friction = 0.5, bounce = 0 } )
local top = display.newRect( 80, 200, 20, 20 )
physics.addBody( top, { density = 1, bounce = 0, friction = 1 } )
advanceFrames( 120 )
check( top.y > 290 and top.y < 305, "box lands on chain from above (y=" .. top.y .. ")" )
-- from below: gravity up
local below = display.newRect( 240, 400, 20, 20 )
physics.addBody( below, { density = 1, bounce = 0, friction = 1 } )
below.gravityScale = -1
advanceFrames( 120 )
check( below.y > 310 and below.y < 335, "box hits chain from below: two-sided (y=" .. below.y .. ")" )
check( physics.getStats().shapeCount >= 6, "chain built with both windings" )

-- loop chain container: ball inside stays inside
local ring = display.newRect( 0, 0, 1, 1 )
physics.addBody( ring, "static", { chain = { 500, 100, 700, 100, 700, 300, 500, 300 }, connectFirstAndLastChainVertex = true, bounce = 0 } )
local ball = display.newCircle( 600, 150, 10 )
physics.addBody( ball, { radius = 10, density = 1, bounce = 0 } )
advanceFrames( 120 )
check( ball.y > 270 and ball.y < 300, "ball rests inside loop chain (y=" .. ball.y .. ")" )
check( not pcall( physics.addBody, display.newRect( 0, 0, 1, 1 ), "static", { chain = { 0, 0 } } ), "chain needs 2 points" )
check( not pcall( physics.addBody, display.newRect( 0, 0, 1, 1 ), "static", { chain = { 0, 0, 10, 0 }, connectFirstAndLastChainVertex = true } ), "loop needs 3 points" )

-- ---------------------------------------------------------------- display.newLine + append
fresh()
local line = display.newLine( 0, 300, 100, 300 )
line:append( 200, 280, 300, 300 )
physics.addBody( line, { friction = 0.5, bounce = 0 } ) -- forced static like v2
check( line.bodyType == "static", "line body forced static" )
local onLine = display.newRect( 50, 200, 20, 20 )
physics.addBody( onLine, { density = 1, bounce = 0, friction = 1 } )
local onAppend = display.newRect( 260, 200, 10, 10 )
physics.addBody( onAppend, { density = 1, bounce = 0, friction = 1 } )
advanceFrames( 120 )
check( near( onLine.y, 290, 1.5 ), "box rests on line (y=" .. onLine.y .. ")" )
check( onAppend.y < 300 and onAppend.y > 270, "box rests on appended segment (y=" .. onAppend.y .. ")" )

-- ---------------------------------------------------------------- static display.newPolygon
fresh()
local poly = display.newPolygon( 160, 300, { -100, 0, 0, -50, 100, 0, 0, 50 } ) -- diamond
physics.addBody( poly, "static", { bounce = 0, friction = 1 } )
local onPoly = display.newRect( 160, 100, 10, 10 )
physics.addBody( onPoly, { density = 1, bounce = 0 } )
advanceFrames( 90 )
check( onPoly.y < 250 and onPoly.y > 235, "box stops on top of static polygon (y=" .. onPoly.y .. ")" )

-- ---------------------------------------------------------------- outline (concave -> convex pieces)
fresh()
local gnd = display.newRect( 160, 400, 320, 20 )
physics.addBody( gnd, "static", { bounce = 0 } )
local img = display.newImage( "L.png" ) -- 64x32 in the mock
img.x, img.y = 160, 200
local outline = graphics.newOutline( 2, "L.png" )
check( outline._b2texW == 64 and outline._b2texH == 32, "newOutline hook recorded texture size" )
local shapesBefore = physics.getStats().shapeCount
physics.addBody( img, { outline = outline, density = 1, bounce = 0 } )
local pieces = physics.getStats().shapeCount - shapesBefore
check( pieces >= 2, "concave outline split into " .. pieces .. " convex pieces" )
local area = 64 * 16 + 32 * 16
check( near( img.mass, area / 900, 1e-3 ), "outline mass matches L area (" .. img.mass .. ")" )
advanceFrames( 150 )
check( near( img.y, 390 - 16, 2 ), "outline body rests on ground (y=" .. img.y .. ")" )

-- decomposition of a 12 point star
local star, n = {}, 12
for i = 0, n - 1 do
	local r = ( i % 2 == 0 ) and 30 or 12
	local a = i * 2 * math.pi / n
	star[#star + 1] = 32 + r * math.cos( a )
	star[#star + 1] = 16 + r * math.sin( a )
end
local starObj = display.newImage( "star.png" )
shapesBefore = physics.getStats().shapeCount
check( pcall( physics.addBody, starObj, { outline = star } ), "star outline accepted" )
check( physics.getStats().shapeCount - shapesBefore >= 6, "star split into convex pieces" )
check( not pcall( physics.addBody, display.newImage( "x.png" ), { outline = { 0, 0, 10, 10, 10, 0, 0, 10 } } ), "self-intersecting outline rejected" )

-- ---------------------------------------------------------------- MKS
check( near( physics.toMKS( "length", 30 ), 1, 1e-6 ), "toMKS length" )
check( near( physics.fromMKS( "velocity", 2 ), 60, 1e-6 ), "fromMKS velocity" )
check( near( physics.toMKS( "angularVelocity", 180 ), math.pi, 1e-6 ), "toMKS angularVelocity" )
check( not pcall( physics.toMKS, "mass", 1 ), "toMKS unknown unit errors" )
check( physics.setMKS( "velocityThreshold", 2 ) == true and near( physics.getMKS( "velocityThreshold" ), 2, 1e-6 ), "setMKS velocityThreshold" )
check( physics.setMKS( "linearSleepTolerance", 0.1 ) == true and near( physics.getMKS( "linearSleepTolerance" ), 0.1, 1e-6 ), "setMKS linearSleepTolerance" )
check( physics.setMKS( "contactHertz", 45 ) == true and physics.getMKS( "contactHertz" ) == 45, "setMKS contactHertz (v3)" )
check( physics.setMKS( "timeToSleep", 1 ) == false, "setMKS unsupported v2 key returns false" )

-- a "shape" with no area is skipped (warning), the other elements keep their index
do
	local g = display.newRect( 100, 300, 10, 10 )
	local okAdd = pcall( physics.addBody, g, "static",
		{ shape = { 0, 0, 10, 0, 20, 0 } },                 -- collinear: no area
		{ shape = { -10, -10, 10, -10, 0, 10 } } )
	check( okAdd and g.bodyType == "static", "zero-area shape skipped, body created" )
	local hits = physics.rayCast( 100, 250, 100, 350, "closest" )
	check( hits and hits[1].element == 2, "remaining element keeps index 2" )
	display.remove( g )
	-- a long display.newLine( parent, unpack( points ) ) must not overflow the Lua stack in the hook
	local pts = {}
	for i = 1, 60 do pts[#pts + 1] = i * 5; pts[#pts + 1] = 200 + ( i % 2 ) * 3 end
	local line = display.newLine( unpack( pts ) )
	check( #rawget( line, "_b2line" ) == 120, "newLine with 120 arguments" )
	display.remove( line )
end

physics.stop()
print( string.format( "\n%d passed, %d failed", passed, failed ) )
os.exit( failed == 0 and 0 or 1 )

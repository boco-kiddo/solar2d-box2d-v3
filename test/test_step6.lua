-- Step 6 tests: Box2D v3 features (capsule, cornerRadius, rolling resistance, tangent speed, motion
-- locks, sleepThreshold, *ToCenter, setTargetTransform, explode, "hit" events, circleCast,
-- queryCircle, setThreadCount).
-- Run with  lua51 test_step6.lua  from the test directory.
package.cpath = "./?.so;" .. package.cpath
dofile( "mock_solar2d.lua" )

local physics = require( "plugin.box2dv3" )
local passed, failed = 0, 0
local function check( cond, msg )
	if cond then passed = passed + 1 else failed = failed + 1; print( "FAIL: " .. msg ) end
end
local function near( a, b, eps ) return a ~= nil and b ~= nil and math.abs( a - b ) <= ( eps or 0.5 ) end
local function fresh( gx, gy )
	physics.stop()
	Runtime._functionListeners, Runtime._tableListeners = nil, nil
	physics.start()
	physics.setGravity( gx or 0, gy or 9.8 )
end
local function ground( y, opts )
	local g = display.newRect( 240, y or 400, 480, 20 )
	physics.addBody( g, "static", opts or { friction = 0.5, bounce = 0 } )
	return g
end

physics.start()
check( physics.version >= "0.6.0", "version >= 0.6.0" )

-- ---------------------------------------------------------------- capsule
do
	fresh( 0, 0 )
	local c = display.newRect( 100, 100, 60, 20 )
	physics.addBody( c, "static", { capsule = true } ) -- fitted to the object: horizontal, r = 10
	check( physics.queryPoint( 70.5, 100 ) ~= nil and physics.queryPoint( 69.5, 100 ) == nil, "fitted capsule reaches the left end" )
	check( physics.queryPoint( 72, 91 ) == nil, "fitted capsule has round ends (corner empty)" )
	check( physics.queryPoint( 100, 91 ) ~= nil, "fitted capsule middle is full height" )

	local v = display.newRect( 200, 100, 20, 60 )
	physics.addBody( v, "static", { capsule = {} } )
	check( physics.queryPoint( 200, 70.5 ) ~= nil and physics.queryPoint( 209, 72 ) == nil, "fitted vertical capsule" )

	local e = display.newRect( 300, 100, 10, 10 )
	physics.addBody( e, "static", { capsule = { x1 = -20, y1 = 0, x2 = 20, y2 = 0, radius = 5 } } )
	check( physics.queryPoint( 324, 100 ) ~= nil and physics.queryPoint( 326, 100 ) == nil, "explicit capsule" )
	check( not pcall( physics.addBody, display.newRect( 0, 0, 10, 10 ), { capsule = { radius = 0 } } ), "capsule radius 0 rejected" )

	-- a dynamic capsule falls and rests on the ground with its round end
	fresh()
	ground( 400 )
	local pill = display.newRect( 240, 300, 40, 16 )
	physics.addBody( pill, { capsule = true, bounce = 0 } )
	advanceFrames( 120 )
	check( near( pill.y, 390 - 8, 1.5 ), "capsule rests on the ground (" .. pill.y .. ")" )
	check( pill.mass > 0, "capsule has mass" )
end

-- ---------------------------------------------------------------- cornerRadius
do
	fresh( 0, 0 )
	local r = display.newRect( 100, 100, 40, 40 )
	physics.addBody( r, "static", { cornerRadius = 10 } )
	check( physics.queryPoint( 119.5, 100 ) ~= nil, "rounded rect keeps its outer size" )
	check( physics.queryPoint( 119, 119 ) == nil, "rounded rect corner is cut" )
	check( physics.queryPoint( 110, 110 ) ~= nil, "rounded rect inner corner area is solid" )

	local b = display.newRect( 200, 100, 10, 10 )
	physics.addBody( b, "static", { box = { halfWidth = 20, halfHeight = 10, x = 0, y = 0 }, cornerRadius = 50 } )
	check( physics.queryPoint( 219.5, 100 ) ~= nil, "too large cornerRadius clamped (box keeps size)" )

	local s = display.newRect( 300, 100, 10, 10 )
	physics.addBody( s, "static", { shape = { -20, -20, 20, -20, 20, 20, -20, 20 }, cornerRadius = 6 } )
	check( physics.queryPoint( 319.5, 300 - 200 ) ~= nil, "rounded shape keeps its outer edge" )
	check( physics.queryPoint( 319, 119 ) == nil, "rounded shape corner is cut" )

	-- rounded box rolls/slides off a slope more easily than a sharp one? just check it is valid
	local t = display.newRect( 400, 100, 30, 30 )
	physics.addBody( t, { cornerRadius = 4 } )
	check( t.mass > 0, "rounded dynamic box has mass" )
end

-- ---------------------------------------------------------------- rolling resistance
do
	local function roll( rr )
		fresh()
		ground( 400, { friction = 1, bounce = 0, rollingResistance = rr } )
		local ball = display.newCircle( 100, 380, 10 )
		physics.addBody( ball, { radius = 10, friction = 1, bounce = 0, rollingResistance = rr } )
		advanceFrames( 20 )
		ball:setLinearVelocity( 200, 0 )
		advanceFrames( 120 )
		return ball.x, ball
	end
	local free = roll( 0 )
	local slowed, ball = roll( 0.2 )
	check( free > slowed + 50, "rolling resistance slows a rolling ball (" .. free .. " vs " .. slowed .. ")" )
	check( near( ball.rollingResistance, 0.2, 1e-5 ), "obj.rollingResistance reads the element value" )
	ball.rollingResistance = 0
	check( ball.rollingResistance == 0, "obj.rollingResistance settable" )
end

-- ---------------------------------------------------------------- tangent speed (conveyor belt)
do
	fresh()
	local belt = ground( 400, { friction = 1, bounce = 0, tangentSpeed = 100 } )
	check( near( belt.tangentSpeed, 100, 1e-3 ), "tangentSpeed in px/s" )
	local box = display.newRect( 240, 380, 20, 20 )
	physics.addBody( box, { friction = 1, bounce = 0 } )
	advanceFrames( 90 )
	local vx = box:getLinearVelocity()
	check( math.abs( vx ) > 80 and math.abs( vx ) < 120, "box carried by the belt (" .. vx .. " px/s)" )
	local x0 = box.x
	belt.tangentSpeed = 0
	advanceFrames( 60 )
	local x1 = box.x
	advanceFrames( 30 )
	check( math.abs( box.x - x1 ) < 2, "belt stopped at runtime (" .. ( box.x - x1 ) .. ")" )
	check( x1 ~= x0, "box moved before the stop" )
	-- belt reverses: box sleeping on a stopped belt must be woken
	advanceFrames( 200 )
	belt.tangentSpeed = -100
	advanceFrames( 60 )
	local vx2 = box:getLinearVelocity()
	check( vx2 * vx < 0, "reversed belt wakes and carries the box (" .. vx2 .. ")" )
end

-- ---------------------------------------------------------------- motion locks / sleepThreshold
do
	fresh()
	local box = display.newRect( 100, 100, 20, 20 )
	physics.addBody( box )
	box.isFixedY = true
	check( box.isFixedY == true and box.isFixedX == false, "isFixedX / isFixedY read back" )
	box:setLinearVelocity( 50, 0 )
	advanceFrames( 30 )
	check( near( box.y, 100, 0.01 ), "isFixedY blocks gravity (" .. box.y .. ")" )
	check( box.x > 120, "x still free" )
	box.isFixedY = false
	box.isFixedX = true
	local x = box.x
	advanceFrames( 30 )
	check( near( box.x, x, 0.01 ) and box.y > 110, "isFixedX locks x, y falls" )

	box.sleepThreshold = 15
	check( near( box.sleepThreshold, 15, 1e-3 ), "sleepThreshold in px/s" )
end

-- ---------------------------------------------------------------- ToCenter
do
	fresh( 0, 0 )
	local box = display.newRect( 100, 100, 20, 20 )
	physics.addBody( box )
	box:applyLinearImpulseToCenter( box.mass * 2, 0 ) -- 2 m/s = 60 px/s
	local vx, vy = box:getLinearVelocity()
	check( near( vx, 60, 1e-2 ) and near( vy, 0, 1e-4 ), "applyLinearImpulseToCenter (" .. vx .. ")" )
	check( near( box.angularVelocity, 0, 1e-4 ), "no spin from a central impulse" )
	box:setLinearVelocity( 0, 0 )
	box:applyForceToCenter( 0, box.mass * 60 ) -- a = 60 m/s^2 for one step
	advanceFrames( 1 )
	local _, vy2 = box:getLinearVelocity()
	check( near( vy2, 60 * 30 / 60, 1 ), "applyForceToCenter (" .. vy2 .. " px/s)" )
end

-- ---------------------------------------------------------------- setTargetTransform
do
	fresh()
	ground( 400 )
	local paddle = display.newRect( 100, 300, 60, 10 )
	physics.addBody( paddle, "kinematic" )
	paddle:setTargetTransform( 130, 300, 0, 0.5 )
	local vx = paddle:getLinearVelocity()
	check( near( vx, 60, 0.5 ), "target velocity = distance / time (" .. vx .. ")" )
	advanceFrames( 30 )
	check( near( paddle.x, 130, 0.5 ), "kinematic reaches the target (" .. paddle.x .. ")" )
	paddle:setTargetTransform( paddle.x, paddle.y, 30 ) -- one frame (Box2D limits rotation to 45 degrees per step)
	advanceFrames( 1 )
	check( near( paddle.rotation, 30, 0.5 ), "target rotation in one frame (" .. paddle.rotation .. ")" )
	-- a kinematic moved by setTargetTransform pushes a dynamic box instead of tunnelling through it
	local box = display.newRect( 200, 380, 20, 20 )
	physics.addBody( box, { friction = 0.2 } )
	local pusher = display.newRect( 150, 380, 10, 20 )
	physics.addBody( pusher, "kinematic" )
	advanceFrames( 10 )
	for i = 1, 30 do
		pusher:setTargetTransform( pusher.x + 4, 380 )
		advanceFrames( 1 )
	end
	check( box.x > 200 + 40, "box pushed by the kinematic (" .. box.x .. ")" )
end

-- ---------------------------------------------------------------- explode
do
	fresh( 0, 0 )
	local boxes = {}
	for i, p in ipairs{ { 140, 100 }, { 60, 100 }, { 100, 140 }, { 300, 100 } } do
		boxes[i] = display.newRect( p[1], p[2], 10, 10 )
		physics.addBody( boxes[i] )
	end
	physics.explode( 100, 100, 60, 2 )
	advanceFrames( 1 )
	local v1x = boxes[1]:getLinearVelocity()
	local v2x = boxes[2]:getLinearVelocity()
	local _, v3y = boxes[3]:getLinearVelocity()
	local v4x, v4y = boxes[4]:getLinearVelocity()
	check( v1x > 1 and v2x < -1 and v3y > 1, "explosion pushes outwards" )
	check( v4x == 0 and v4y == 0, "out of range body unaffected" )
	physics.explode( 100, 100, 60, -2 ) -- implosion
	advanceFrames( 1 )
	check( boxes[1]:getLinearVelocity() < v1x, "negative impulse implodes" )
	check( not pcall( physics.explode, 0, 0 ), "explode requires radius and impulse" )
	-- maskBits
	local masked = display.newRect( 100, 60, 10, 10 )
	physics.addBody( masked, { filter = { categoryBits = 2 } } )
	physics.explode( 100, 100, 60, 2, 0, 1 )
	advanceFrames( 1 )
	local mx, my = masked:getLinearVelocity()
	check( mx == 0 and my == 0, "explosion maskBits" )
end

-- ---------------------------------------------------------------- hit events
do
	fresh()
	ground( 400 )
	local hits, localHits = {}, {}
	Runtime:addEventListener( "hit", function( e ) hits[#hits + 1] = e end )
	local ball = display.newCircle( 240, 200, 10 )
	physics.addBody( ball, { radius = 10, bounce = 0 } )
	advanceFrames( 90 )
	check( #hits >= 1, "Runtime hit event (" .. #hits .. ")" )
	local h = hits[1]
	if h then
		check( h.name == "hit" and h.object1 and h.object2, "hit event fields" )
		check( ( h.object1 == ball or h.object2 == ball ), "hit objects" )
		check( h.approachSpeed > 200 and h.approachSpeed < 400, "approachSpeed in px/s (" .. h.approachSpeed .. ")" )
		check( near( h.x, 240, 1 ) and near( h.y, 390, 2 ), "hit position (" .. h.x .. ", " .. h.y .. ")" )
		check( near( math.abs( h.normalY ), 1, 1e-3 ), "hit normal" )
	end
	local n = #hits
	advanceFrames( 60 )
	check( #hits == n, "no hit while resting" )

	-- local listener and threshold
	fresh()
	ground( 400 )
	local ball2 = display.newCircle( 240, 360, 10 )
	physics.addBody( ball2, { radius = 10, bounce = 0 } )
	ball2:addEventListener( "hit", function( e ) localHits[#localHits + 1] = e end )
	physics.setHitEventThreshold( 1000 )
	check( near( physics.getHitEventThreshold(), 1000, 1e-2 ), "hit threshold in px/s" )
	advanceFrames( 60 )
	check( #localHits == 0, "slow impact below threshold" )
	physics.setHitEventThreshold( 30 )
	ball2.y = 200
	advanceFrames( 90 )
	check( #localHits >= 1 and localHits[1].target == ball2 and localHits[1].other ~= nil, "local hit event" )
end

-- ---------------------------------------------------------------- circleCast / queryCircle
do
	fresh( 0, 0 )
	local wall = display.newRect( 200, 100, 20, 200 )
	physics.addBody( wall, "static" )
	local hits = physics.circleCast( 0, 100, 400, 100, 10 )
	check( hits and #hits == 1 and hits[1].object == wall, "circleCast hits the wall" )
	if hits then
		check( near( hits[1].center.x, 180, 0.3 ), "circleCast center at impact (" .. hits[1].center.x .. ")" )
		check( near( hits[1].position.x, 190, 0.1 ), "circleCast contact point" )
	end
	-- a ray along y = 205 passes under the wall, a fat circle does not
	check( physics.rayCast( 0, 205, 400, 205 ) == nil, "ray misses below the wall" )
	check( physics.circleCast( 0, 205, 400, 205, 10 ) ~= nil, "circle catches the corner" )
	check( physics.circleCast( 0, 100, 400, 100, 10, "any", { maskBits = 2 } ) == nil, "circleCast options" )

	local ball = display.newCircle( 300, 300, 10 )
	physics.addBody( ball, "static", { radius = 10 } )
	check( physics.queryCircle( 300, 325, 16 ) ~= nil, "queryCircle overlaps" )
	check( physics.queryCircle( 300, 325, 14 ) == nil, "queryCircle exact" )
	check( not pcall( physics.circleCast, 0, 0, 1, 1, 0 ), "circleCast radius 0 rejected" )
end

-- ---------------------------------------------------------------- threads
do
	physics.setThreadCount( 4 )
	fresh()
	ground( 400 )
	local boxes = {}
	for i = 1, 200 do
		local b = display.newRect( 40 + ( i % 20 ) * 20, 380 - math.floor( i / 20 ) * 21, 18, 18 )
		physics.addBody( b, { bounce = 0 } )
		boxes[i] = b
	end
	advanceFrames( 5 )
	local cfg, used = physics.getThreadCount()
	check( cfg == 4 and used == 4, "4 threads in use (" .. cfg .. ", " .. used .. ")" )
	check( physics.getStats().threadCount == 4, "getStats().threadCount" )

	local count = 0
	Runtime:addEventListener( "collision", function() count = count + 1 end )
	advanceFrames( 60 )
	check( count > 0, "collision events with threads" )

	-- preCollision forces a single thread
	local pre = 0
	local function onPre( e ) pre = pre + 1 end
	Runtime:addEventListener( "preCollision", onPre )
	boxes[1]:applyLinearImpulse( 0, -0.5, boxes[1].x, boxes[1].y )
	advanceFrames( 5 )
	local _, used2 = physics.getThreadCount()
	check( used2 == 1 and pre > 0, "preCollision runs single threaded (" .. used2 .. ", " .. pre .. ")" )
	Runtime:removeEventListener( "preCollision", onPre )
	advanceFrames( 2 )
	local _, used3 = physics.getThreadCount()
	check( used3 == 4, "threads resume after the preCollision listener is removed" )

	physics.setThreadCount( 2 )
	advanceFrames( 30 )
	local _, used4 = physics.getThreadCount()
	check( used4 == 2, "thread count changed at runtime" )
	physics.setThreadCount( 1 )
	advanceFrames( 30 )
	check( select( 2, physics.getThreadCount() ) == 1, "back to one thread" )
	local settled = true
	for i = 1, #boxes do settled = settled and boxes[i].y < 400 end
	check( settled, "stack stable across thread changes" )
end

physics.stop()
collectOrphans()
print( string.format( "%d passed, %d failed", passed, failed ) )

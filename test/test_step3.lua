-- Step 3 tests: collision / preCollision / postCollision events (v2 model).
-- Run with  lua51 test_step3.lua  from the test directory.
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
	Runtime._functionListeners, Runtime._tableListeners = nil, nil
	Runtime:addEventListener( "enterFrame", nil ) -- no-op guard
end
local function clearRuntime( name )
	for _, f in ipairs{ "_functionListeners", "_tableListeners" } do
		if Runtime[f] then Runtime[f][name] = nil end
	end
end
local function ground( y )
	local g = display.newRect( 160, y or 400, 320, 20 )
	physics.addBody( g, "static", { friction = 0.5, bounce = 0 } )
	return g
end

physics.start()

-- ---------------------------------------------------------------- Runtime collision began / ended
do
	physics.stop(); physics.start()
	local events = {}
	local function onCollision( e ) events[#events + 1] = e end
	Runtime:addEventListener( "collision", onCollision )
	local g = ground()
	local box = display.newRect( 160, 300, 20, 20 )
	physics.addBody( box, { density = 1, bounce = 0 } )
	advanceFrames( 90 )
	local began
	for _, e in ipairs( events ) do if e.phase == "began" then began = e; break end end
	check( began ~= nil, "Runtime got collision began" )
	if began then
		check( began.name == "collision", "event.name" )
		check( ( began.object1 == box and began.object2 == g ) or ( began.object1 == g and began.object2 == box ), "object1/object2" )
		check( began.element1 == 1 and began.element2 == 1, "element1/element2" )
		check( began.other == nil and began.target == nil, "Runtime event has no other/target (v2)" )
		check( began.contact ~= nil, "event.contact present" )
		check( began.contact.isTouching == nil, "contact invalid after dispatch (v2)" )
	end
	local n = #events
	box.y = 100 -- teleport away: contact ends
	advanceFrames( 2 )
	local ended = events[#events]
	check( #events > n and ended.phase == "ended", "collision ended after separation" )
	Runtime:removeEventListener( "collision", onCollision )
end

-- ---------------------------------------------------------------- local listeners + propagation
do
	physics.stop(); physics.start()
	local g = ground()
	local box = display.newRect( 160, 300, 20, 20 )
	physics.addBody( box, { density = 1, bounce = 0 } )
	local localEvent, runtimeCount = nil, 0
	function box:collision( e )
		if e.phase == "began" then localEvent = e end
		return true -- stop propagation
	end
	box:addEventListener( "collision" )
	local function rt( e ) runtimeCount = runtimeCount + 1 end
	Runtime:addEventListener( "collision", rt )
	advanceFrames( 90 )
	check( localEvent ~= nil, "table listener on object got collision" )
	if localEvent then
		check( localEvent.target == box and localEvent.other == g, "event.target / event.other" )
		check( localEvent.selfElement == 1 and localEvent.otherElement == 1, "selfElement / otherElement" )
		check( localEvent.object1 == nil, "local event has no object1 (v2)" )
	end
	check( runtimeCount == 0, "returning true stops propagation to Runtime" )
	box:removeEventListener( "collision" )
	Runtime:removeEventListener( "collision", rt )

	-- function listener on the ground sees the box as other; not handled -> Runtime too
	local gotGround, gotRuntime = false, false
	local function gl( e ) if e.phase == "began" then gotGround = ( e.target == g and e.other ~= nil ) end end
	g:addEventListener( "collision", gl )
	Runtime:addEventListener( "collision", function( e ) gotRuntime = true end )
	local b2 = display.newRect( 100, 300, 20, 20 )
	physics.addBody( b2, { density = 1, bounce = 0 } )
	advanceFrames( 90 )
	check( gotGround, "function listener on second object" )
	check( gotRuntime, "unhandled event reaches Runtime" )
	clearRuntime( "collision" )
end

-- ---------------------------------------------------------------- multi-element indices
do
	physics.stop(); physics.start()
	local g = ground()
	local obj = display.newRect( 160, 300, 10, 10 )
	physics.addBody( obj, { box = { halfWidth = 30, halfHeight = 5, y = -30 } }, { box = { halfWidth = 10, halfHeight = 10, y = 0 }, density = 1, bounce = 0 } )
	local elem
	function obj:collision( e ) if e.phase == "began" and e.other == g then elem = e.selfElement end end
	obj:addEventListener( "collision" )
	advanceFrames( 90 )
	check( elem == 2, "selfElement identifies the lower element (got " .. tostring( elem ) .. ")" )
end

-- ---------------------------------------------------------------- sensors
do
	physics.stop(); physics.start()
	local zone = display.newRect( 160, 250, 200, 40 )
	physics.addBody( zone, "static", { isSensor = true } )
	local phases = {}
	function zone:collision( e ) phases[#phases + 1] = e.phase end
	zone:addEventListener( "collision" )
	local ball = display.newCircle( 160, 150, 8 )
	physics.addBody( ball, { radius = 8, density = 1 } )
	advanceFrames( 90 )
	check( phases[1] == "began" and phases[2] == "ended", "sensor began then ended (" .. table.concat( phases, "," ) .. ")" )
	check( ball.y > 300, "ball passed through sensor" )
end

-- ---------------------------------------------------------------- preCollision: one-way platform
do
	physics.stop(); physics.start()
	local platform = display.newRect( 160, 300, 200, 10 )
	physics.addBody( platform, "static", { bounce = 0, friction = 1 } )
	local hero = display.newRect( 160, 360, 20, 20 )
	physics.addBody( hero, { density = 1, bounce = 0, friction = 1 } )
	hero.isFixedRotation = true
	local preCount = 0
	function hero:preCollision( e )
		preCount = preCount + 1
		-- pass through while coming from below
		if self.y > e.other.y - 10 then e.contact.isEnabled = false end
	end
	hero:addEventListener( "preCollision" )
	hero:setLinearVelocity( 0, -400 ) -- jump up through the platform
	advanceFrames( 240 )
	check( preCount > 0, "preCollision dispatched (" .. preCount .. ")" )
	check( near( hero.y, 300 - 5 - 10, 1.5 ), "one-way platform: jumped through, landed on top (y=" .. hero.y .. ")" )
end

-- ---------------------------------------------------------------- postCollision
do
	physics.stop(); physics.start()
	local g = ground()
	local maxForce, count = 0, 0
	local function post( e )
		count = count + 1
		if e.force > maxForce then maxForce = e.force end
	end
	Runtime:addEventListener( "postCollision", post )
	local box = display.newRect( 160, 200, 20, 20 )
	physics.addBody( box, { density = 1, bounce = 0 } )
	advanceFrames( 120 )
	check( count > 0 and maxForce > 0, "postCollision with force (" .. count .. " events, max " .. maxForce .. ")" )
	clearRuntime( "postCollision" )
	local before = count
	advanceFrames( 10 )
	check( count == before, "no postCollision after listener removed" )
end

-- ---------------------------------------------------------------- removing objects inside listeners
do
	physics.stop(); physics.start()
	local g = ground()
	local victim = display.newRect( 160, 300, 20, 20 )
	physics.addBody( victim, { density = 1 } )
	function victim:preCollision( e ) display.remove( self ) end -- inside the step: deferred
	victim:addEventListener( "preCollision" )
	local before = physics.getStats().bodyCount
	check( pcall( advanceFrames, 90 ), "display.remove inside preCollision does not crash" )
	check( physics.getStats().bodyCount == before - 1, "body destroyed after the step" )

	local v2 = display.newRect( 100, 300, 20, 20 )
	physics.addBody( v2, { density = 1 } )
	local other = display.newRect( 104, 260, 20, 20 )
	physics.addBody( other, { density = 1 } )
	function v2:collision( e ) if e.phase == "began" then display.remove( self ) end end
	v2:addEventListener( "collision" )
	check( pcall( advanceFrames, 90 ), "display.remove inside collision does not crash" )
	check( v2.x == nil, "object removed" )
end

-- ---------------------------------------------------------------- errors and physics.stop in listeners
do
	physics.stop(); physics.start()
	local g = ground()
	local box = display.newRect( 160, 300, 20, 20 )
	physics.addBody( box, { density = 1, bounce = 0 } )
	function box:collision( e ) error( "boom" ) end
	box:addEventListener( "collision" )
	local ok, err = pcall( advanceFrames, 90 )
	check( not ok and tostring( err ):find( "boom" ), "listener error surfaces as a Lua error" )
	box:removeEventListener( "collision" )
	check( pcall( advanceFrames, 5 ), "world keeps running after the error" )

	local b = display.newRect( 60, 300, 20, 20 )
	physics.addBody( b, { density = 1 } )
	function b:collision( e ) if e.phase == "began" then physics.stop() end end
	b:addEventListener( "collision" )
	check( pcall( advanceFrames, 90 ), "physics.stop() inside collision does not crash" )
	check( physics.getStats().bodyCount == 0, "world stopped" )
end

-- ---------------------------------------------------------------- collision position
do
	physics.stop(); physics.start()
	check( physics.getReportCollisionsInContentCoordinates() == true, "collision positions in content coordinates by default" )
	local g = ground()
	local box = display.newRect( 200, 300, 20, 20 )
	physics.addBody( box, { density = 1, bounce = 0 } )
	local ex, ey
	function box:collision( e ) if e.phase == "began" then ex, ey = e.x, e.y end end
	box:addEventListener( "collision" )
	advanceFrames( 90 )
	check( ex and near( ey, 390, 2 ) and ex > 185 and ex < 215, "content-space contact point (" .. tostring( ex ) .. ", " .. tostring( ey ) .. ")" )
	physics.setReportCollisionsInContentCoordinates( false )
end

physics.stop()
print( string.format( "\n%d passed, %d failed", passed, failed ) )
os.exit( failed == 0 and 0 or 1 )

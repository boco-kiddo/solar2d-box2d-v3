-- Step 6b tests: "motor" joint, breakable joints ("jointBreak"), character mover, saveState / restoreState.
-- Run with  lua51 test_step6b.lua  from the test directory.
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
local function ground( y, w )
	local g = display.newRect( 240, y or 400, w or 480, 20 )
	physics.addBody( g, "static", { friction = 0.5, bounce = 0 } )
	return g
end

physics.start()
check( physics.version >= "0.7.0", "version >= 0.7.0" )

-- ---------------------------------------------------------------- motor joint
do
	fresh( 0, 0 )
	local base = display.newRect( 100, 100, 20, 20 ); physics.addBody( base, "static" )
	local plat = display.newRect( 150, 100, 40, 10 ); physics.addBody( plat )
	local m = physics.newJoint( "motor", base, plat )
	check( m.jointType == "motor", "motor joint created" )
	local ox, oy = m:getLinearOffset()
	check( near( ox, 50, 0.01 ) and near( oy, 0, 0.01 ), "initial offset = current pose (" .. ox .. ")" )
	advanceFrames( 30 )
	check( near( plat.x, 150, 0.1 ), "motor holds the pose" )
	m:setLinearOffset( 50, 60 )
	m.angularOffset = 45
	advanceFrames( 120 )
	check( near( plat.x, 150, 1 ) and near( plat.y, 160, 1 ), "moves to the new offset (" .. plat.x .. ", " .. plat.y .. ")" )
	check( near( plat.rotation, 45, 1 ), "angular offset (" .. plat.rotation .. ")" )
	check( near( m.angularOffset, 45, 1e-3 ), "angularOffset reads back" )
	m.linearFrequency = 0; m.angularFrequency = 0
	m.maxForce = 1000; m.maxTorque = 1000
	m:setLinearVelocity( 30, 0 ); m.angularVelocity = 0
	local vx = select( 1, m:getLinearVelocity() )
	check( near( vx, 30, 1e-3 ), "linear velocity property" )
	advanceFrames( 60 )
	local pvx = plat:getLinearVelocity()
	check( near( pvx, 30, 1 ), "velocity drive (" .. pvx .. ")" )
	check( m.maxSpringForce > 0 and m.linearDampingRatio > 0, "spring properties" )
end

-- ---------------------------------------------------------------- breakable joints
do
	fresh()
	local hook = display.newRect( 240, 50, 10, 10 ); physics.addBody( hook, "static" )
	local weight = display.newCircle( 240, 100, 10 ); physics.addBody( weight, { radius = 10, density = 1 } )
	local j = physics.newJoint( "distance", hook, weight, 240, 50, 240, 100 )
	check( j.breakForce == math.huge, "unbreakable by default" )
	local events, localEvents = {}, {}
	Runtime:addEventListener( "jointBreak", function( e ) events[#events + 1] = e end )
	weight:addEventListener( "jointBreak", function( e ) localEvents[#localEvents + 1] = e end )
	j.breakForce = 1000
	advanceFrames( 60 )
	check( #events == 0 and #localEvents == 0 and j.length ~= nil, "holds below the threshold" )
	weight:applyLinearImpulse( 0, weight.mass * 200, weight.x, weight.y ) -- yank it
	advanceFrames( 2 )
	check( #localEvents == 1 and localEvents[1].joint == j, "local jointBreak event" )
	check( #events == 1, "propagates to Runtime when the local listener does not return true" )
	if localEvents[1] then
		check( localEvents[1].target == weight and localEvents[1].other == hook, "event target / other" )
		check( localEvents[1].force > 0, "event force (" .. localEvents[1].force .. ")" )
	end
	check( j.length == nil and j.jointType == "distance", "joint removed after the event" )
	advanceFrames( 60 )
	check( weight.y > 150, "weight falls free (" .. weight.y .. ")" )

	-- Runtime only, torque threshold on a pivot, return true stops propagation
	local a = display.newRect( 100, 200, 10, 10 ); physics.addBody( a, "static" )
	local arm = display.newRect( 130, 200, 60, 6 ); physics.addBody( arm )
	local p = physics.newJoint( "pivot", a, arm, 100, 200 )
	p.isMotorEnabled = true; p.maxMotorTorque = 1e6; p.motorSpeed = 0
	p.breakTorque = 5
	check( near( p.breakTorque, 5, 1e-6 ), "breakTorque reads back" )
	local before = #events
	advanceFrames( 10 )
	check( #events == before + 1 and events[#events].object2 == arm and events[#events].object1 == a, "Runtime jointBreak on torque" )
	check( p.jointAngle == nil, "pivot removed" )
	p:removeSelf() -- harmless on a removed joint

	-- breakForce = nil makes it unbreakable again
	local b2 = display.newRect( 300, 200, 10, 10 ); physics.addBody( b2, "static" )
	local w2 = display.newCircle( 300, 250, 10 ); physics.addBody( w2, { radius = 10 } )
	local j2 = physics.newJoint( "distance", b2, w2, 300, 200, 300, 250 )
	j2.breakForce = 0.0001
	j2.breakForce = nil
	advanceFrames( 30 )
	check( j2.length ~= nil, "breakForce = nil disables breaking" )
end

-- ---------------------------------------------------------------- character mover
do
	fresh()
	ground( 400 )
	local hero = display.newRect( 100, 300, 20, 40 )
	check( not pcall( physics.newMover, ground( 100, 10 ) ), "object with a body rejected" )
	local mover = physics.newMover( hero )
	check( tostring( mover ) == "Mover", "mover created" )
	advanceFrames( 90 )
	check( mover.isOnGround, "lands on the ground" )
	check( near( hero.y + 20, 390, 2 ), "feet on the ground (" .. hero.y .. ")" )
	check( mover.groundObject ~= nil, "groundObject" )
	mover.throttle = 1
	advanceFrames( 60 )
	local vx = mover:getVelocity()
	check( near( vx, mover.maxSpeed, 2 ) and hero.x > 200, "runs at maxSpeed (" .. vx .. ", x " .. hero.x .. ")" )
	mover.throttle = 0
	advanceFrames( 30 )
	check( near( ( mover:getVelocity() ), 0, 0.5 ), "friction stops it" )

	-- jump
	local y0 = hero.y
	check( mover:jump() == true, "jump from the ground" )
	check( mover:jump() == false, "no double jump" )
	local apex = y0
	for i = 1, 90 do advanceFrames( 1 ); apex = math.min( apex, hero.y ) end
	check( near( y0 - apex, mover.jumpHeight, 6 ), "jump height (" .. ( y0 - apex ) .. " vs " .. mover.jumpHeight .. ")" )
	check( mover.isOnGround and near( hero.y, y0, 2 ), "lands again" )

	-- walls and steps
	local wall = display.newRect( 400, 350, 20, 80 ); physics.addBody( wall, "static" )
	local step = display.newRect( 300, 385, 40, 10 ); physics.addBody( step, "static" ) -- 10 px step
	mover.throttle = 1
	advanceFrames( 120 )
	check( hero.x < 400 - 10 - 10 + 1 and hero.x > 360, "stopped by the wall (" .. hero.x .. ")" )
	mover.throttle = 0

	-- teleport through obj.x
	hero.x, hero.y = 150, 200
	advanceFrames( 1 )
	check( near( hero.x, 150, 1 ), "obj.x teleports the mover" )
	mover.x = 120
	check( hero.x == 120, "mover.x teleports the object" )
	advanceFrames( 90 )

	-- pushes dynamic bodies
	local crate = display.newRect( 200, 375, 30, 30 ); physics.addBody( crate, { density = 0.5, friction = 0.2 } )
	advanceFrames( 30 )
	mover.throttle = 1
	advanceFrames( 60 )
	check( crate.x > 230, "pushes a crate (" .. crate.x .. ")" )
	mover.throttle = 0

	-- options
	local small = display.newRect( 50, 100, 10, 20 )
	local m2 = physics.newMover( small, { maxSpeed = 50, jumpHeight = 10, stepHeight = 2, radius = 4 } )
	check( near( m2.maxSpeed, 50, 1e-3 ) and near( m2.jumpHeight, 10, 1e-2 ) and near( m2.radius, 4, 1e-3 ), "options" )
	m2.gravity = 0
	check( m2.gravity == 0, "gravity property" )
	-- removing the object removes the mover
	display.remove( small )
	advanceFrames( 2 )
	check( m2.isOnGround == nil, "mover gone with its object" )
	mover:removeSelf()
	check( mover.throttle == nil, "mover:removeSelf()" )
end

-- ---------------------------------------------------------------- saveState / restoreState
do
	fresh()
	ground( 400 )
	local box = display.newRect( 100, 100, 20, 20 ); physics.addBody( box, { bounce = 0 } )
	local ball = display.newCircle( 200, 100, 10 ); physics.addBody( ball, { radius = 10, bounce = 0.5 } )
	local anchor = display.newRect( 300, 100, 10, 10 ); physics.addBody( anchor, "static" )
	local bob = display.newCircle( 350, 100, 8 ); physics.addBody( bob, { radius = 8 } )
	local pend = physics.newJoint( "pivot", anchor, bob, 300, 100 )
	local hero = display.newRect( 400, 300, 20, 40 )
	local mover = physics.newMover( hero )
	advanceFrames( 20 )
	box:setLinearVelocity( 30, 0 )

	local state = physics.saveState()
	check( state and state.size > 0 and state.bodyCount == 5, "saveState (" .. tostring( state ) .. ")" )
	local bx, by, bvx = box.x, box.y, box:getLinearVelocity()
	local ballY, bobX, heroY = ball.y, bob.x, hero.y

	-- run on, then change things
	advanceFrames( 60 )
	local after = box.y
	local extra = display.newRect( 240, 50, 10, 10 ); physics.addBody( extra )
	local extraJoint = physics.newJoint( "weld", box, ball, 150, 100 )
	display.remove( ball ) -- removed after the save
	pend:removeSelf()
	box.isSensor = true -- rebuilds the shapes
	advanceFrames( 5 )

	local removed = physics.restoreState( state )
	check( after ~= by, "the world moved before the restore" )
	check( near( box.x, bx, 1e-3 ) and near( box.y, by, 1e-3 ), "positions restored (" .. box.y .. " vs " .. by .. ")" )
	check( near( box:getLinearVelocity(), bvx, 1e-3 ), "velocity restored" )
	check( box.isSensor == false, "element records restored" )
	check( #removed == 1 and removed[1] == extra and extra.bodyType == nil, "body created after the save removed and returned" )
	check( extraJoint.jointType == "weld" and extraJoint.length == nil and not pcall( extraJoint.getAnchorA, extraJoint ), "joint created after the save removed" )
	check( pend.jointAngle == nil, "joint removed after the save stays removed" )
	check( physics.getStats().bodyCount == 4, "removed display object stays removed (" .. physics.getStats().bodyCount .. ")" )
	check( near( hero.y, heroY, 1e-3 ), "mover restored" )
	check( near( bob.x, bobX, 1e-3 ), "bob restored" )

	-- events still work after a restore (shape user data restored)
	local hits = 0
	box:addEventListener( "collision", function( e ) if e.phase == "began" and e.selfElement == 1 then hits = hits + 1 end end )
	advanceFrames( 120 )
	check( hits >= 1, "collision events after restore" )
	check( box.y > 370, "box falls and lands after restore (" .. box.y .. ")" )

	-- restore twice (determinism)
	physics.restoreState( state )
	advanceFrames( 30 ); local y1 = box.y
	physics.restoreState( state )
	advanceFrames( 30 ); local y2 = box.y
	check( y1 == y2, "deterministic replay (" .. y1 .. " vs " .. y2 .. ")" )

	physics.stop(); physics.start()
	check( not pcall( physics.restoreState, state ), "state from another world rejected" )
	check( not pcall( physics.restoreState, {} ), "bad argument rejected" )
end

physics.stop()
collectOrphans()
print( string.format( "%d passed, %d failed", passed, failed ) )

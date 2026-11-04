-- Step 4 tests: physics.newJoint() (v2 joint types and properties).
-- Run with  lua51 test_step4.lua  from the test directory.
package.cpath = "./?.so;" .. package.cpath
dofile( "mock_solar2d.lua" )

local physics = require( "plugin.box2dv3" )
local passed, failed = 0, 0
local function check( cond, msg )
	if cond then passed = passed + 1 else failed = failed + 1; print( "FAIL: " .. msg ) end
end
local function near( a, b, eps ) return a ~= nil and b ~= nil and math.abs( a - b ) <= ( eps or 0.5 ) end
local function dist( ax, ay, bx, by ) return math.sqrt( ( ax - bx ) ^ 2 + ( ay - by ) ^ 2 ) end
local function fresh( gx, gy )
	physics.stop(); physics.start()
	physics.setGravity( gx or 0, gy or 9.8 )
end
local function anchorBody( x, y )
	local o = display.newRect( x, y, 10, 10 )
	physics.addBody( o, "static" )
	return o
end
local function box( x, y, w, h, t )
	local o = display.newRect( x, y, w or 20, h or 20 )
	physics.addBody( o, t or "dynamic", { density = 1, friction = 0.3, bounce = 0 } )
	return o
end

-- ---------------------------------------------------------------- pivot (revolute)
fresh()
local pivotBase = anchorBody( 160, 100 )
local arm = box( 220, 100, 100, 10 )
local pivot = physics.newJoint( "pivot", pivotBase, arm, 160, 100 )
check( pivot ~= nil and pivot.jointType == "pivot", "pivot created" )
local ax, ay = pivot:getAnchorA()
check( near( ax, 160, 1e-3 ) and near( ay, 100, 1e-3 ), "getAnchorA in pixels" )
local lx, ly = pivot:getLocalAnchorB()
check( near( lx, -60, 1e-3 ) and near( ly, 0, 1e-3 ), "getLocalAnchorB in pixels" )
advanceFrames( 30 )
check( pivot.jointAngle > 10, "pendulum swings (angle " .. pivot.jointAngle .. ")" )
check( near( dist( arm.x, arm.y, 160, 100 ), 60, 0.5 ), "arm stays attached at the pivot" )
check( pivot.referenceAngle == 0, "referenceAngle" )
pivot.isLimitEnabled = true
pivot:setRotationLimits( -20, 20 )
local lo, hi = pivot:getRotationLimits()
check( near( lo, -20, 1e-3 ) and near( hi, 20, 1e-3 ), "get/setRotationLimits in degrees" )
advanceFrames( 120 )
check( pivot.jointAngle <= 21 and pivot.jointAngle >= -21, "limit respected (" .. pivot.jointAngle .. ")" )
pivot.isLimitEnabled = false
physics.setGravity( 0, 0 )
arm.angularVelocity = 0
pivot.isMotorEnabled = true
pivot.maxMotorTorque = 10000
pivot.motorSpeed = 90
check( pivot.isMotorEnabled == true and near( pivot.motorSpeed, 90, 1e-3 ), "motor properties" )
advanceFrames( 30 )
check( near( pivot.jointSpeed, 90, 1 ), "motor drives 90 deg/s (" .. pivot.jointSpeed .. ")" )
check( pivot.motorTorque ~= nil, "motorTorque readable" )
local fx, fy = pivot:getReactionForce()
check( fx ~= nil and fy ~= nil and pivot.reactionTorque ~= nil, "reaction force / torque" )

-- ---------------------------------------------------------------- distance
fresh()
local d1 = anchorBody( 160, 100 )
local d2 = box( 160, 200 )
local dj = physics.newJoint( "distance", d1, d2, 160, 100, 160, 200 )
check( near( dj.length, 100, 1e-3 ), "distance length in pixels" )
d2:applyLinearImpulse( 2, 0, d2.x, d2.y )
advanceFrames( 60 )
check( near( dist( d2.x, d2.y, 160, 100 ), 100, 0.5 ), "rigid distance kept (" .. dist( d2.x, d2.y, 160, 100 ) .. ")" )
dj.length = 50
advanceFrames( 60 )
check( near( dist( d2.x, d2.y, 160, 100 ), 50, 1 ), "length setter" )
dj.frequency = 2; dj.dampingRatio = 0.1
check( near( dj.frequency, 2, 1e-4 ) and near( dj.dampingRatio, 0.1, 1e-4 ), "frequency / dampingRatio" )
advanceFrames( 20 )
check( dist( d2.x, d2.y, 160, 100 ) > 51, "spring stretches under gravity" )

-- ---------------------------------------------------------------- piston (prismatic)
fresh()
local rail = anchorBody( 160, 200 )
local slider = box( 160, 200 )
local pj = physics.newJoint( "piston", rail, slider, 160, 200, 1, 0 )
slider:applyLinearImpulse( 1, 0, slider.x, slider.y )
advanceFrames( 60 )
check( near( slider.y, 200, 0.05 ) and slider.x > 170, "slides along the axis only (x " .. slider.x .. ", y " .. slider.y .. ")" )
check( near( pj.jointTranslation, slider.x - 160, 0.1 ), "jointTranslation in pixels" )
local axx, axy = pj:getLocalAxisA()
check( near( axx, 1, 1e-4 ) and near( axy, 0, 1e-4 ), "getLocalAxisA" )
pj.isLimitEnabled = true
pj:setLimits( -20, 20 )
local l1, l2 = pj:getLimits()
check( near( l1, -20, 1e-3 ) and near( l2, 20, 1e-3 ), "get/setLimits in pixels" )
advanceFrames( 60 )
check( pj.jointTranslation <= 21, "translation limited (" .. pj.jointTranslation .. ")" )
pj.isLimitEnabled = false
pj.isMotorEnabled = true; pj.maxMotorForce = 1000; pj.motorSpeed = -60
advanceFrames( 30 )
check( near( pj.jointSpeed, -60, 1 ), "piston motor in px/s (" .. pj.jointSpeed .. ")" )

-- ---------------------------------------------------------------- wheel
fresh()
local chassis = box( 160, 200, 100, 20 )
chassis.isFixedRotation = true
local wheel = display.newCircle( 160, 230, 10 )
physics.addBody( wheel, { radius = 10, density = 1, friction = 1 } )
local gnd = display.newRect( 160, 300, 400, 20 )
physics.addBody( gnd, "static", { friction = 1 } )
local wj = physics.newJoint( "wheel", chassis, wheel, 160, 230, 0, 1 )
check( near( wj.springFrequency, 2, 1e-4 ) and near( wj.springDampingRatio, 0.7, 1e-4 ), "wheel spring defaults like v2" )
advanceFrames( 120 )
check( near( wheel.y, 280, 1.5 ), "wheel rests on ground (y " .. wheel.y .. ")" )
check( wj.jointTranslation ~= nil and wj.jointSpeed ~= nil, "wheel translation / speed" )
wj.isMotorEnabled = true; wj.maxMotorTorque = 100; wj.motorSpeed = 360 -- degrees / second
advanceFrames( 60 )
check( near( wheel.angularVelocity, 360, 5 ), "wheel spins at motorSpeed deg/s (" .. wheel.angularVelocity .. ")" )
check( chassis.x > 170, "wheel motor drives the car (x " .. chassis.x .. ")" )
check( near( wj.motorSpeed, 360, 1e-2 ), "wheel motorSpeed roundtrip (deg/s)" )
check( wj.motorTorque ~= nil and wj.motorTorque ~= wj.maxMotorTorque, "motorTorque is the current torque" )

-- ---------------------------------------------------------------- weld
fresh()
local w1 = box( 100, 100 )
local w2 = box( 120, 100 )
w2.rotation = 30
local weld = physics.newJoint( "weld", w1, w2, 110, 100 )
check( near( weld.referenceAngle, 30, 1e-3 ), "weld referenceAngle (" .. tostring( weld.referenceAngle ) .. ")" )
w1:applyAngularImpulse( 0.5 )
advanceFrames( 60 )
check( near( dist( w1.x, w1.y, w2.x, w2.y ), 20, 0.3 ), "welded bodies keep their distance" )
check( near( ( w2.rotation - w1.rotation ) % 360, 30, 0.5 ), "welded bodies keep relative angle" )

-- ---------------------------------------------------------------- friction (top-down)
fresh( 0, 0 )
local table_ = anchorBody( 0, 0 )
local puck = box( 100, 100 )
local fj = physics.newJoint( "friction", table_, puck, 100, 100 )
fj.maxForce = 5
fj.maxTorque = 1
check( near( fj.maxForce, 5, 1e-4 ) and near( fj.maxTorque, 1, 1e-4 ), "friction maxForce / maxTorque" )
puck:setLinearVelocity( 90, 0 )
advanceFrames( 60 )
local pvx = puck:getLinearVelocity()
check( near( pvx, 0, 0.5 ), "friction joint stops the puck (vx " .. pvx .. ")" )

-- ---------------------------------------------------------------- rope
fresh()
local hook = anchorBody( 160, 50 )
local weight = box( 160, 150 )
local rope = physics.newJoint( "rope", hook, weight, 0, 0, 0, 0 )
check( near( rope.maxLength, 100, 1e-2 ), "rope maxLength = distance between anchors" )
weight:applyLinearImpulse( 3, 0, weight.x, weight.y )
local maxD = 0
for _ = 1, 120 do advanceFrames( 1 ); maxD = math.max( maxD, dist( weight.x, weight.y, 160, 50 ) ) end
check( maxD <= 101, "rope never stretches past maxLength (" .. maxD .. ")" )
weight.y = 100
advanceFrames( 1 )
check( dist( weight.x, weight.y, 160, 50 ) < 99, "rope allows slack" )
check( rope.limitState == "inactive" or rope.limitState == "upper", "limitState" )
rope.maxLength = 60
check( near( rope.maxLength, 60, 1e-2 ), "rope maxLength setter in pixels" )
local hook2 = anchorBody( 260, 50 )
local w2b = box( 260, 150 )
local rope2 = physics.newJoint( "rope", hook2, w2b, 0, 5, 0, -10, 70 )
check( near( rope2.maxLength, 70, 1e-2 ), "rope with explicit maxLength" )
local rope3 = physics.newJoint( "rope", hook2, box( 300, 150 ), 0, 0, 0, -10 )
check( near( rope3.maxLength, math.sqrt( 40 ^ 2 + 90 ^ 2 ), 1e-2 ), "rope default length measured between anchors" )

-- ---------------------------------------------------------------- touch
fresh()
local dragged = box( 100, 100 )
local touch = physics.newJoint( "touch", dragged, 100, 100 )
check( near( touch.frequency, 5, 1e-4 ) and near( touch.dampingRatio, 0.7, 1e-4 ), "touch defaults like v2" )
check( near( touch.maxForce, 1000 * dragged.mass, 1e-2 ), "touch maxForce = 1000 * mass" )
touch:setTarget( 200, 120 )
local tx, ty = touch:getTarget()
check( near( tx, 200, 1e-3 ) and near( ty, 120, 1e-3 ), "getTarget" )
advanceFrames( 120 )
check( near( dragged.x, 200, 3 ) and near( dragged.y, 120, 5 ), "body follows the touch target (" .. dragged.x .. ", " .. dragged.y .. ")" )
local worldBodies = physics.getStats().worldBodyCount
touch:removeSelf()
check( touch.maxForce == nil, "removed joint properties read nil" )
check( physics.getStats().worldBodyCount == worldBodies - 1, "touch helper body destroyed" )
check( not pcall( function() touch:setTarget( 0, 0 ) end ), "methods of removed joint error" )

local t2 = physics.newJoint( "touch", dragged, dragged.x, dragged.y )
worldBodies = physics.getStats().worldBodyCount
display.remove( dragged )
advanceFrames( 1 )
check( t2.maxForce == nil, "joint invalid after body removed" )
check( physics.getStats().worldBodyCount == worldBodies - 2, "helper cleaned up with the body" )

-- ---------------------------------------------------------------- lifetime and errors
fresh()
local a = box( 0, 0, 10, 10, "static" )
local b = box( 50, 0 )
local j = physics.newJoint( "pivot", a, b, 0, 0 )
physics.stop()
check( j.motorSpeed == nil, "joints invalid after physics.stop()" )
physics.start()
local a2 = box( 0, 0, 10, 10, "static" )
local b2 = box( 50, 0 )
local j2 = physics.newJoint( "pivot", a2, b2, 0, 0 )
check( j.motorSpeed == nil and j2.motorSpeed ~= nil, "stale joint does not alias a joint of the new world" )
check( not pcall( physics.newJoint, "pulley", a2, b2 ), "pulley not supported" )
check( not pcall( physics.newJoint, "gear", a2, b2 ), "gear not supported" )
check( not pcall( physics.newJoint, "spring", a2, b2 ), "unknown joint type errors" )
check( not pcall( physics.newJoint, "pivot", a2, a2, 0, 0 ), "same object errors" )
check( not pcall( physics.newJoint, "pivot", a2, display.newRect( 0, 0, 1, 1 ), 0, 0 ), "object without body errors" )
check( tostring( j2 ):find( "pivot" ) ~= nil, "tostring" )
j2:removeSelf(); j2:removeSelf()
check( true, "double removeSelf is safe" )

physics.stop()
print( string.format( "\n%d passed, %d failed", passed, failed ) )
os.exit( failed == 0 and 0 or 1 )

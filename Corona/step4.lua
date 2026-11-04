-- plugin.box2dv3 — step 4 test scene: physics.newJoint().
--
-- Drag any dynamic body      : "touch" joint (setTarget while dragging, removeSelf on release)
-- Bridge                     : planks linked with "pivot" joints
-- Car                        : "wheel" joints with motor; tap the car to reverse
-- Elevator                   : "piston" joint with limits and a motor that reverses
-- Hanging weight             : "rope" joint;  spring ball: "distance" joint with frequency
-- Welded L shape             : "weld" joint
-- "self-test 4"              : automated checks, results in the console
local M = {}

local W, H = display.contentWidth, display.contentHeight
local CX = display.contentCenterX

local joints = {}
local timers = {}

local function color( o, r, g, b ) if o.setFillColor then o:setFillColor( r, g, b ) end end

-- touch joint dragging, attached to every dynamic body of the scene
local function onDrag( event )
	local o = event.target
	local physics = M.physics
	if event.phase == "began" then
		display.getCurrentStage():setFocus( o )
		o.dragJoint = physics.newJoint( "touch", o, event.x, event.y )
	elseif o.dragJoint then
		if event.phase == "moved" then
			o.dragJoint:setTarget( event.x, event.y )
		else
			o.dragJoint:removeSelf()
			o.dragJoint = nil
			display.getCurrentStage():setFocus( nil )
		end
	end
	return true
end

local function draggable( o )
	o:addEventListener( "touch", onDrag )
	o:addEventListener( "tap", function() return true end )
	return o
end

function M.build( physics, world )
	M.physics = physics
	joints = {}

	local ground = display.newRect( world, CX, H - 10, W, 20 )
	ground:setFillColor( 0.35, 0.45, 0.35 )
	physics.addBody( ground, "static", { friction = 1, bounce = 0 } )
	local wl = display.newRect( world, 3, H / 2, 6, H * 2 )
	local wr = display.newRect( world, W - 3, H / 2, 6, H * 2 )
	physics.addBody( wl, "static" ); physics.addBody( wr, "static" )

	-- bridge: pivot joints
	local leftPost = display.newRect( world, 20, 170, 16, 16 )
	local rightPost = display.newRect( world, 200, 170, 16, 16 )
	physics.addBody( leftPost, "static" ); physics.addBody( rightPost, "static" )
	local prev, n = leftPost, 8
	local plankW = ( 200 - 20 ) / n
	for i = 1, n do
		local plank = display.newRect( world, 20 + ( i - 0.5 ) * plankW, 170, plankW - 2, 6 )
		color( plank, 0.7, 0.5, 0.3 )
		physics.addBody( plank, { density = 2, friction = 0.8, bounce = 0 } )
		joints[#joints + 1] = physics.newJoint( "pivot", prev, plank, 20 + ( i - 1 ) * plankW, 170 )
		draggable( plank )
		prev = plank
	end
	joints[#joints + 1] = physics.newJoint( "pivot", prev, rightPost, 200, 170 )

	-- car: wheel joints with motor
	local car = display.newRect( world, 300, H - 50, 70, 14 )
	color( car, 0.9, 0.3, 0.3 )
	physics.addBody( car, { density = 2, friction = 0.5, bounce = 0 } )
	draggable( car )
	local wheels = {}
	for i, dx in ipairs{ -24, 24 } do
		local w = display.newCircle( world, 300 + dx, H - 35, 9 )
		color( w, 0.2, 0.2, 0.2 )
		physics.addBody( w, { radius = 9, density = 1, friction = 1.5, bounce = 0 } )
		local wj = physics.newJoint( "wheel", car, w, w.x, w.y, 0, 1 )
		wj.springFrequency = 4
		wj.isMotorEnabled = true
		wj.maxMotorTorque = 40
		wj.motorSpeed = 720 -- degrees / second
		wheels[i] = wj
		joints[#joints + 1] = wj
		draggable( w )
	end
	car:addEventListener( "tap", function()
		for _, wj in ipairs( wheels ) do wj.motorSpeed = -wj.motorSpeed end
		return true
	end )

	-- elevator: piston with limits and a reversing motor
	local shaft = display.newRect( world, W - 40, H - 120, 6, 200 )
	color( shaft, 0.5, 0.5, 0.5 )
	physics.addBody( shaft, "static", { isSensor = true } )
	local cabin = display.newRect( world, W - 40, H - 40, 50, 10 )
	color( cabin, 0.3, 0.7, 1 )
	physics.addBody( cabin, { density = 5, friction = 1, bounce = 0 } )
	local piston = physics.newJoint( "piston", shaft, cabin, cabin.x, cabin.y, 0, 1 )
	piston.isLimitEnabled = true
	piston:setLimits( -150, 0 )
	piston.isMotorEnabled = true
	piston.maxMotorForce = 5000
	piston.motorSpeed = -60
	joints[#joints + 1] = piston
	timers[#timers + 1] = timer.performWithDelay( 3000, function()
		if piston.motorSpeed then piston.motorSpeed = -piston.motorSpeed end
	end, 0 )

	-- rope with a hanging weight
	local hook = display.newRect( world, 250, 60, 8, 8 )
	physics.addBody( hook, "static" )
	local weight = display.newCircle( world, 320, 60, 12 )
	color( weight, 0.9, 0.8, 0.2 )
	physics.addBody( weight, { radius = 12, density = 3, bounce = 0.1 } )
	joints[#joints + 1] = physics.newJoint( "rope", hook, weight, 0, 0, 0, 0 )
	draggable( weight )
	M.rope = { hook = hook, weight = weight }

	-- spring: distance joint with frequency
	local springTop = display.newRect( world, 130, 60, 8, 8 )
	physics.addBody( springTop, "static" )
	local springBall = display.newCircle( world, 130, 110, 10 )
	color( springBall, 0.6, 1, 0.6 )
	physics.addBody( springBall, { radius = 10, density = 1 } )
	local spring = physics.newJoint( "distance", springTop, springBall, 130, 60, 130, 110 )
	spring.frequency = 1.5
	spring.dampingRatio = 0.1
	joints[#joints + 1] = spring
	draggable( springBall )
	M.springLine = { top = springTop, ball = springBall }

	-- welded L shape
	local l1 = display.newRect( world, 380, 80, 50, 12 )
	local l2 = display.newRect( world, 361, 105, 12, 38 )
	color( l1, 0.8, 0.4, 1 ); color( l2, 0.8, 0.4, 1 )
	physics.addBody( l1, { density = 1, friction = 0.5 } )
	physics.addBody( l2, { density = 1, friction = 0.5 } )
	joints[#joints + 1] = physics.newJoint( "weld", l1, l2, 361, 86 )
	draggable( l1 ); draggable( l2 )

	-- lines showing the rope and the spring
	M.lines = display.newGroup(); world:insert( M.lines )
end

function M.update()
	if not M.lines or not M.lines.numChildren then return end
	for i = M.lines.numChildren, 1, -1 do M.lines[i]:removeSelf() end
	for _, pair in ipairs{ { M.rope and M.rope.hook, M.rope and M.rope.weight }, { M.springLine and M.springLine.top, M.springLine and M.springLine.ball } } do
		local a, b = pair[1], pair[2]
		if a and b and a.x and b.x then
			local l = display.newLine( M.lines, a.x, a.y, b.x, b.y )
			l.strokeWidth = 1
		end
	end
end

function M.teardown()
	for _, t in ipairs( timers ) do timer.cancel( t ) end
	timers = {}
	joints = {}
	M.lines = nil
end

function M.status()
	local live = 0
	for _, j in ipairs( joints ) do if j.jointType and j.isCollideConnected ~= nil then live = live + 1 end end
	return string.format( "[step 4] joints %d live — drag bodies (touch joint), tap the car to reverse", live )
end

-- --------------------------------------------------------------------------
function M.selfTest( physics, world, done )
	local ok, fail = 0, 0
	local function check( c, m ) if c then ok = ok + 1 else fail = fail + 1; print( "FAIL: " .. m ) end end
	local function near( a, b, e ) return a ~= nil and math.abs( a - b ) <= ( e or 1e-3 ) end

	local base = display.newRect( world, CX, 30, 8, 8 )
	physics.addBody( base, "static" )
	local arm = display.newRect( world, CX + 40, 30, 60, 6 )
	physics.addBody( arm, { density = 1 } )
	local p = physics.newJoint( "pivot", base, arm, CX, 30 )
	check( p.jointType == "pivot", "pivot created" )
	local ax, ay = p:getAnchorA()
	check( near( ax, CX ) and near( ay, 30 ), "getAnchorA" )
	p.isMotorEnabled = true; p.motorSpeed = 45; p.maxMotorTorque = 5
	check( p.isMotorEnabled and near( p.motorSpeed, 45 ) and near( p.maxMotorTorque, 5 ), "pivot motor properties" )
	p.isLimitEnabled = true; p:setRotationLimits( -30, 30 )
	local lo, hi = p:getRotationLimits()
	check( near( lo, -30 ) and near( hi, 30 ), "rotation limits" )

	local puck = display.newRect( world, CX, -100, 16, 16 )
	physics.addBody( puck, { density = 1 } )
	local t = physics.newJoint( "touch", puck, puck.x, puck.y )
	t:setTarget( CX + 60, 40 )

	timer.performWithDelay( 1200, function()
		check( math.abs( p.jointAngle ) <= 31, "limit respected (" .. p.jointAngle .. ")" )
		check( puck.x and math.abs( puck.x - ( CX + 60 ) ) < 5 and math.abs( puck.y - 40 ) < 8, "touch joint drags body to target" )
		t:removeSelf()
		check( t.maxForce == nil, "removed joint reads nil" )
		display.remove( arm )
		check( p.jointAngle == nil, "joint invalid after its body is removed" )
		check( not pcall( physics.newJoint, "gear", base, puck ), "gear reported as unsupported" )
		display.remove( base ); display.remove( puck )
		print( string.format( "[box2d step-4 self-test] %d passed, %d failed", ok, fail ) )
		if done then done( ok, fail ) end
	end )
end

return M

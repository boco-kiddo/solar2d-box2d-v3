-- plugin.box2dv3 — test app
--
-- Step 1 scene (default) — world, addBody, sync, teleport, removal:
-- Tap empty space   : spawn a random body (box / circle / polygon / multi-element)
-- Drag a body       : teleports it through obj.x / obj.y (v2 behaviour)
-- Double tap a body : removes it (display.remove -> body destroyed automatically)
-- Buttons (row 1)   : pause / resume, restart (back to step 1), toggle gravity, step-1 self-test,
--                     "step 2" scene (see step2.lua), step-2 self-test
-- Buttons (row 2)   : ... "step 3" scene (collision events, see step3.lua), step-3 self-test
-- Buttons (row 3)   : "step 5" scene (queries, see step5.lua), step-5 self-test, draw mode cycle,
--                     "step 4" scene (joints, see step4.lua), step-4 self-test
-- Buttons (row 4)   : "step 6" scene (v3 features, see step6.lua), step-6 self-test, thread count,
--                     "step 6b" scene (mover, motor joint, breakable joints, save / restore), step-6b self-test
-- Buttons (row 2)   : benchmark / stress scene, run it on plugin.box2dv3 (v3), on built-in physics (v2),
--                     or on v3 with stiffer contacts (v3+: 8 sub-steps, 60 Hz)
display.setStatusBar( display.HiddenStatusBar )

local physics = require( "plugin.box2dv3" )
print( "Loaded plugin.box2dv3 " .. physics.version .. " / " .. physics.engineVersion )

local W, H = display.contentWidth, display.contentHeight
local CX = display.contentCenterX
local world = display.newGroup()
local ui = display.newGroup()

local info = display.newText{ parent = ui, text = "", x = 8, y = 8, font = native.systemFont, fontSize = 13 }
info.anchorX, info.anchorY = 0, 0

local function rnd( a, b ) return a + math.random() * ( b - a ) end

-- --------------------------------------------------------------------------
local function buildStatic()
	local ground = display.newRect( world, CX, H - 10, W, 20 )
	ground:setFillColor( 0.35, 0.45, 0.35 )
	physics.addBody( ground, "static", { friction = 0.6, bounce = 0 } )

	local left = display.newRect( world, 5, H / 2, 10, H )
	local right = display.newRect( world, W - 5, H / 2, 10, H )
	physics.addBody( left, "static", { friction = 0.3 } )
	physics.addBody( right, "static", { friction = 0.3 } )

	-- a tilted static ramp using box params (x, y, angle are relative to the object)
	local ramp = display.newRect( world, CX - 110, H * 0.6, 10, 10 )
	ramp.isVisible = false
	physics.addBody( ramp, "static", { box = { halfWidth = 90, halfHeight = 6, angle = 15 }, friction = 0.2 } )
	local rampGfx = display.newRect( world, CX - 110, H * 0.6, 180, 12 )
	rampGfx.rotation = 15
	rampGfx:setFillColor( 0.5 )

	-- a kinematic spinner (angularVelocity in degrees / second)
	local spinner = display.newRect( world, CX + 120, H * 0.5, 120, 10 )
	spinner:setFillColor( 0.9, 0.6, 0.2 )
	physics.addBody( spinner, "kinematic", { friction = 0.5 } )
	spinner.angularVelocity = 90
end

local onBodyTouch

local function spawn( x, y )
	local kind = math.random( 4 )
	local obj
	if kind == 1 then
		obj = display.newRect( world, x, y, rnd( 20, 50 ), rnd( 20, 50 ) )
		physics.addBody( obj, { density = 1, friction = 0.4, bounce = 0.1 } )
	elseif kind == 2 then
		local r = rnd( 10, 25 )
		obj = display.newCircle( world, x, y, r )
		physics.addBody( obj, { density = 1, friction = 0.3, bounce = 0.5, radius = r } )
	elseif kind == 3 then
		local s = rnd( 15, 30 )
		local verts = { 0, -s, s, s * 0.8, -s, s * 0.8 }
		obj = display.newPolygon( world, x, y, verts )
		-- display.newPolygon recentres its vertices on their bounding box; offset the physics shape the same way
		local cy = ( -s + s * 0.8 ) / 2
		physics.addBody( obj, { density = 1, friction = 0.3, bounce = 0.2,
			shape = { 0, -s - cy, s, s * 0.8 - cy, -s, s * 0.8 - cy } } )
	else
		-- multi-element body: a "dumbbell" (box + two circles)
		obj = display.newGroup()
		world:insert( obj )
		display.newRect( obj, 0, 0, 40, 8 )
		display.newCircle( obj, -24, 0, 10 )
		display.newCircle( obj, 24, 0, 10 )
		obj.x, obj.y = x, y
		physics.addBody( obj, "dynamic",
			{ density = 1, friction = 0.3, box = { halfWidth = 20, halfHeight = 4 } },
			{ density = 2, friction = 0.3, bounce = 0.3, circle = { radius = 10, x = -24, y = 0 } },
			{ density = 2, friction = 0.3, bounce = 0.3, circle = { radius = 10, x = 24, y = 0 } } )
	end
	if obj.setFillColor then obj:setFillColor( rnd( 0.3, 1 ), rnd( 0.3, 1 ), rnd( 0.3, 1 ) ) end
	obj.rotation = rnd( -30, 30 )
	obj:addEventListener( "touch", onBodyTouch )
	obj:addEventListener( "tap", function() return true end ) -- don't spawn when tapping a body
	return obj
end

-- drag = teleport via obj.x / obj.y ; double tap = remove
local lastTap = {}
function onBodyTouch( event )
	local obj = event.target
	if event.phase == "began" then
		display.getCurrentStage():setFocus( obj )
		obj.isFocus = true
		obj.markX, obj.markY = obj.x - event.x, obj.y - event.y
		local now = system.getTimer()
		if lastTap[obj] and now - lastTap[obj] < 300 then
			display.getCurrentStage():setFocus( nil )
			display.remove( obj ) -- the body is destroyed automatically ("finalize")
			return true
		end
		lastTap[obj] = now
	elseif obj.isFocus then
		if event.phase == "moved" then
			obj.x, obj.y = obj.markX + event.x, obj.markY + event.y
			obj:setLinearVelocity( 0, 0 )
		else
			display.getCurrentStage():setFocus( nil )
			obj.isFocus = false
		end
	end
	return true
end

local benchRunning = false
local step2 = require( "step2" )
local step3 = require( "step3" )
local step4 = require( "step4" )
local step5 = require( "step5" )
local step6 = require( "step6" )
local step6b = require( "step6b" )
local currentScene = "step1"
Runtime:addEventListener( "tap", function( event )
	if benchRunning or event.y <= 150 then return end
	if currentScene == "step2" then
		step2.spawn( physics, world, event.x, event.y )
	elseif currentScene == "step3" then
		step3.spawn( physics, world, event.x, event.y )
	elseif currentScene == "step4" or currentScene == "step5" or currentScene == "step6" or currentScene == "step6b" then
		return -- these scenes handle touches themselves
	else
		spawn( event.x, event.y )
	end
end )

-- --------------------------------------------------------------------------
-- buttons
local function button( label, x, onTap, y, color )
	y = y or 44
	local g = display.newGroup(); ui:insert( g )
	local bg = display.newRoundedRect( g, x, y, 74, 24, 5 )
	bg:setFillColor( unpack( color or { 0.2, 0.3, 0.5 } ) )
	g.label = display.newText{ parent = g, text = label, x = x, y = y, font = native.systemFont, fontSize = 12 }
	bg:addEventListener( "tap", function() onTap( g ); return true end )
	return g
end

local paused = false
local function startScene()
	physics.start()
	physics.setGravity( 0, 9.8 )
	buildStatic()
	for i = 1, 12 do spawn( rnd( 40, W - 40 ), rnd( 70, H * 0.35 ) ) end
end

button( "pause", 45, function()
	if benchRunning or resultBox then return end
	paused = not paused
	if paused then physics.pause() else physics.start() end
end )
local resultBox -- benchmark result overlay

local function clearWorld()
	step3.teardown()
	step4.teardown()
	step5.teardown()
	step6.teardown()
	step6b.teardown()
	for i = world.numChildren, 1, -1 do world[i]:removeSelf() end
end

button( "restart", 125, function()
	if benchRunning then return end
	physics.stop()
	clearWorld()
	display.remove( resultBox ); resultBox = nil
	paused = false
	currentScene = "step1"
	startScene()
end )
local flipped = false
button( "gravity", 205, function()
	flipped = not flipped
	physics.setGravity( 0, flipped and -9.8 or 9.8 )
end )

-- --------------------------------------------------------------------------
-- automated self-test of step 1 behaviour, results in the console
local function selfTest()
	if benchRunning or resultBox then return end
	local ok, fail = 0, 0
	local function check( c, m ) if c then ok = ok + 1 else fail = fail + 1; print( "FAIL: " .. m ) end end
	local o = display.newRect( world, CX, 100, 20, 20 )
	physics.addBody( o, "kinematic" )
	check( o.bodyType == "kinematic", "bodyType get" )
	o.bodyType = "dynamic"
	check( o.bodyType == "dynamic", "bodyType set" )
	o:setLinearVelocity( 30, -10 )
	local vx, vy = o:getLinearVelocity()
	check( math.abs( vx - 30 ) < 1e-3 and math.abs( vy + 10 ) < 1e-3, "linear velocity roundtrip" )
	o.angularVelocity = 45
	check( math.abs( o.angularVelocity - 45 ) < 1e-3, "angularVelocity roundtrip" )
	o.x = 123
	check( o.x == 123, "teleport x" )
	check( physics.removeBody( o ), "removeBody" )
	check( o.bodyType == nil, "props removed after removeBody" )
	display.remove( o )
	local before = physics.getStats().bodyCount
	local t = display.newRect( world, CX, 100, 20, 20 )
	physics.addBody( t )
	display.remove( t )
	check( physics.getStats().bodyCount == before, "display.remove destroys body" )
	check( not pcall( physics.addBody, {}, "dynamic" ), "non display object rejected" )
	print( string.format( "[box2d self-test] %d passed, %d failed", ok, fail ) )
end
button( "self-test", 285, selfTest )

-- --------------------------------------------------------------------------
-- step 2 scene: body properties / methods, chain, line, polygon, outline
button( "step 2", 365, function()
	if benchRunning then return end
	physics.stop()
	clearWorld()
	display.remove( resultBox ); resultBox = nil
	paused = false
	currentScene = "step2"
	physics.start()
	physics.setGravity( 0, 9.8 )
	step2.build( physics, world )
end, nil, { 0.4, 0.25, 0.5 } )
button( "self-test 2", 445, function()
	if benchRunning or resultBox then return end
	if currentScene ~= "step2" then print( "open the step 2 scene first" ); return end
	step2.selfTest( physics, world )
end, nil, { 0.4, 0.25, 0.5 } )

-- --------------------------------------------------------------------------
-- step 3 scene: collision / preCollision / postCollision
button( "step 3", 365, function()
	if benchRunning then return end
	physics.stop()
	clearWorld()
	display.remove( resultBox ); resultBox = nil
	paused = false
	currentScene = "step3"
	physics.start()
	physics.setGravity( 0, 9.8 )
	step3.build( physics, world )
end, 74, { 0.2, 0.35, 0.6 } )
button( "self-test 3", 445, function()
	if benchRunning or resultBox then return end
	if currentScene ~= "step3" then print( "open the step 3 scene first" ); return end
	step3.selfTest( physics, world )
end, 74, { 0.2, 0.35, 0.6 } )

-- --------------------------------------------------------------------------
-- step 5 scene: queries and debug draw
button( "step 5", 45, function()
	if benchRunning then return end
	physics.stop()
	clearWorld()
	display.remove( resultBox ); resultBox = nil
	paused = false
	currentScene = "step5"
	physics.start()
	step5.build( physics, world )
end, 104, { 0.15, 0.45, 0.45 } )
button( "self-test 5", 125, function()
	if benchRunning or resultBox then return end
	if currentScene ~= "step5" then print( "open the step 5 scene first" ); return end
	step5.selfTest( physics, world )
end, 104, { 0.15, 0.45, 0.45 } )
local drawModes = { "normal", "hybrid", "debug" }
local drawIndex = 1
button( "draw", 205, function( b )
	drawIndex = drawIndex % #drawModes + 1
	physics.setDrawMode( drawModes[drawIndex] )
	b.label.text = "draw: " .. drawModes[drawIndex]
end, 104, { 0.15, 0.45, 0.45 } )

-- --------------------------------------------------------------------------
-- step 4 scene: joints
button( "step 4", 365, function()
	if benchRunning then return end
	physics.stop()
	clearWorld()
	display.remove( resultBox ); resultBox = nil
	paused = false
	currentScene = "step4"
	physics.start()
	physics.setGravity( 0, 9.8 )
	step4.build( physics, world )
end, 104, { 0.5, 0.4, 0.15 } )
button( "self-test 4", 445, function()
	if benchRunning or resultBox then return end
	if currentScene ~= "step4" then print( "open the step 4 scene first" ); return end
	step4.selfTest( physics, world )
end, 104, { 0.5, 0.4, 0.15 } )

-- --------------------------------------------------------------------------
-- step 6 scene: Box2D v3 features
button( "step 6", 45, function()
	if benchRunning then return end
	physics.stop()
	clearWorld()
	display.remove( resultBox ); resultBox = nil
	paused = false
	currentScene = "step6"
	physics.start()
	step6.build( physics, world )
end, 134, { 0.45, 0.2, 0.35 } )
button( "self-test 6", 125, function()
	if benchRunning or resultBox then return end
	if currentScene ~= "step6" then print( "open the step 6 scene first" ); return end
	step6.selfTest( physics, world )
end, 134, { 0.45, 0.2, 0.35 } )
local threadChoices = { 1, 2, 4 }
local threadIndex = 1
button( "threads: 1", 205, function( b )
	threadIndex = threadIndex % #threadChoices + 1
	physics.setThreadCount( threadChoices[threadIndex] )
	b.label.text = "threads: " .. threadChoices[threadIndex]
end, 134, { 0.45, 0.2, 0.35 } )

button( "step 6b", 285, function()
	if benchRunning then return end
	physics.stop()
	clearWorld()
	display.remove( resultBox ); resultBox = nil
	paused = false
	currentScene = "step6b"
	physics.start()
	step6b.build( physics, world )
end, 134, { 0.2, 0.4, 0.3 } )
button( "self-test 6b", 365, function()
	if benchRunning or resultBox then return end
	if currentScene ~= "step6b" then print( "open the step 6b scene first" ); return end
	step6b.selfTest( physics, world )
end, 134, { 0.2, 0.4, 0.3 } )

-- --------------------------------------------------------------------------
-- benchmark: same scene code on plugin.box2dv3 and on Solar2D's built-in physics
local bench = require( "bench" )
local sceneOrder = bench.order
local sceneIndex = 1
local okV2, physicsV2 = pcall( require, "physics" )

local function showResult( text )
	display.remove( resultBox )
	resultBox = display.newGroup(); ui:insert( resultBox )
	local t = display.newText{ parent = resultBox, text = text, x = CX, y = 0, width = W - 40,
		font = native.systemFont, fontSize = 11 }
	t.anchorY = 0
	t.y = 100
	local bg = display.newRect( resultBox, CX, 94 + ( t.height + 12 ) / 2, W - 30, t.height + 12 )
	bg:setFillColor( 0, 0, 0, 0.75 )
	bg:toBack()
end

local function runBench( engine, name, preset )
	if benchRunning then return end
	benchRunning = true
	physics.stop() -- free the step-1 scene (plugin world)
	clearWorld()
	display.remove( resultBox ); resultBox = nil
	paused = false
	if preset then preset() end
	bench.run{
		engine = engine, name = name, scene = sceneOrder[sceneIndex], parent = world,
		onProgress = function( txt ) info.text = txt end,
		onDone = function( r )
			engine.stop() -- freeze the final state on screen
			if preset then -- back to defaults
				physics.setSubSteps( 4 )
				physics.setContactTuning( 30 )
			end
			local text = bench.format( r )
			print( text )
			showResult( text .. "\n\n(tap restart to go back)" )
			benchRunning = false
		end,
	}
end

button( "pyramid", 45, function( b )
	if benchRunning then return end
	sceneIndex = sceneIndex % #sceneOrder + 1
	b.label.text = sceneOrder[sceneIndex]
end, 74, { 0.35, 0.3, 0.15 } )
button( "bench v3", 125, function() runBench( physics, "box2dv3 (Box2D " .. physics.engineVersion:match( "[%d%.]+" ) .. ")" ) end,
	74, { 0.15, 0.45, 0.25 } )
-- v3 with stiffer contacts: 8 sub-steps allow 60 Hz contacts (Box2D caps hertz at 1/8 of the sub-step rate)
button( "bench v3+", 285, function()
	runBench( physics, "box2dv3 stiff (8 substeps, 60 Hz contacts)", function()
		physics.setSubSteps( 8 )
		physics.setContactTuning( 60 )
	end )
end, 74, { 0.1, 0.55, 0.35 } )
button( "bench v2", 205, function()
	if not okV2 then print( "built-in physics not available" ); return end
	runBench( physicsV2, "built-in physics (Box2D 2.x)" )
end, 74, { 0.45, 0.25, 0.15 } )

-- --------------------------------------------------------------------------
startScene()

Runtime:addEventListener( "enterFrame", function()
	if benchRunning or resultBox then return end
	if currentScene == "step2" then step2.update() end
	if currentScene == "step4" then step4.update() end
	if currentScene == "step5" then step5.update( physics ) end
	if currentScene == "step6" then step6.update( physics ) end
	if currentScene == "step6b" then step6b.update( physics ) end
	local s = physics.getStats()
	info.text = string.format( "%s%s | bodies %d  awake %d  contacts %d  step %.2f ms%s",
		currentScene == "step2" and "[step 2: tap = spawn, tap purple = isSensor] " or
		currentScene == "step3" and ( step3.status() .. " " ) or
		currentScene == "step4" and ( step4.status() .. " " ) or
		currentScene == "step5" and ( step5.status( physics ) .. " " ) or
		currentScene == "step6" and ( step6.status( physics ) .. " " ) or
		currentScene == "step6b" and ( step6b.status( physics ) .. " " ) or "",
		physics.engineVersion, s.bodyCount, s.awakeBodyCount or 0, s.contactCount or 0, s.stepTime or 0,
		paused and "  [paused]" or "" )
end )

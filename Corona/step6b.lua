-- plugin.box2dv3 — step 6b test scene: character mover, "motor" joint, breakable joints, save / restore.
--
-- Hero (green)        : physics.newMover() — on-screen ◀ ▶ jump buttons, or arrow keys / space
--                       walks up the steps and the slope, pushes the crates
-- Moving platform     : "motor" joint, its target offset follows a path (joint:setLinearOffset)
-- Bridge              : "pivot" joints with joint.breakForce; tap above the bridge to drop a heavy ball,
--                       broken joints flash ("jointBreak" event)
-- save / load         : physics.saveState() / physics.restoreState() (balls dropped after the save vanish)
-- "self-test 6b"      : automated checks, results in the console
local M = {}

local W, H = display.contentWidth, display.contentHeight
local CX = display.contentCenterX

local hero, mover, platformJoint, keyListener, breakListener, worldGroup, saved
local input = { left = false, right = false }
local t, breaks = 0, 0
local balls = {}

local function color( o, r, g, b, a ) if o.setFillColor then o:setFillColor( r, g, b, a or 1 ) end end

local function uiButton( parent, label, x, y, onPress, onRelease )
	local b = display.newRoundedRect( parent, x, y, 44, 22, 4 )
	color( b, 0.25, 0.25, 0.3, 0.9 )
	local tx = display.newText{ parent = parent, text = label, x = x, y = y, font = native.systemFont, fontSize = 11 }
	b:addEventListener( "touch", function( e )
		if e.phase == "began" then
			display.getCurrentStage():setFocus( b )
			if onPress then onPress() end
		elseif e.phase == "ended" or e.phase == "cancelled" then
			display.getCurrentStage():setFocus( nil )
			if onRelease then onRelease() end
		end
		return true
	end )
	b:addEventListener( "tap", function() return true end )
	return b, tx
end

local function updateThrottle()
	if mover and mover.throttle then
		mover.throttle = ( input.right and 1 or 0 ) - ( input.left and 1 or 0 )
	end
end

local function jump() if mover and mover.isOnGround ~= nil then mover:jump() end end

function M.build( physics, world )
	worldGroup = world
	balls, saved, breaks, t = {}, nil, 0, 0
	input.left, input.right = false, false
	physics.setGravity( 0, 9.8 )

	local function static( x, y, w, h, opts )
		local o = display.newRect( world, x, y, w, h )
		color( o, 0.35, 0.45, 0.35 )
		physics.addBody( o, "static", opts or { friction = 0.6, bounce = 0 } )
		return o
	end
	static( CX, H - 6, W, 12 )
	static( 3, H / 2, 6, H * 2 ); static( W - 3, H / 2, 6, H * 2 )
	-- steps
	for i = 1, 3 do static( 120 + i * 18, H - 12 - i * 5, 18, i * 10 ) end
	-- slope
	local slope = display.newPolygon( world, 230, H - 22, { -40, 10, 40, 10, 40, -10 } )
	color( slope, 0.35, 0.45, 0.35 )
	physics.addBody( slope, "static", { shape = { -40, 10, 40, 10, 40, -10 }, friction = 0.6 } )
	static( 300, H - 22, 60, 20 )

	-- crates
	for i = 1, 3 do
		local c = display.newRect( world, 360 + i * 22, H - 22, 20, 20 )
		color( c, 0.75, 0.55, 0.3 )
		physics.addBody( c, { density = 0.5, friction = 0.4, bounce = 0 } )
	end

	-- bridge with breakable pivots
	local lx, rx, by = 250, 420, 190
	local l = static( lx, by, 10, 10 ); local r = static( rx, by, 10, 10 )
	local n = 8
	local w = ( rx - lx ) / n
	local prev = l
	breakListener = function( e )
		breaks = breaks + 1
		local fx = display.newCircle( world, e.joint:getAnchorA() or 0, by, 8 )
		color( fx, 1, 0.4, 0.2, 0.8 )
		transition.to( fx, { time = 300, alpha = 0, xScale = 2, yScale = 2, onComplete = function() display.remove( fx ) end } )
	end
	Runtime:addEventListener( "jointBreak", breakListener )
	for i = 1, n do
		local p = display.newRect( world, lx + ( i - 0.5 ) * w, by, w - 2, 5 )
		color( p, 0.7, 0.5, 0.3 )
		physics.addBody( p, { density = 1, friction = 0.8, bounce = 0 } )
		local j = physics.newJoint( "pivot", prev, p, lx + ( i - 1 ) * w, by )
		j.breakForce = 150
		prev = p
	end
	physics.newJoint( "pivot", prev, r, rx, by ).breakForce = 150

	-- motor-driven platform
	local anchor = display.newRect( world, 110, 150, 6, 6 ); anchor.isVisible = false
	physics.addBody( anchor, "static", { isSensor = true } )
	local plat = display.newRect( world, 110, 200, 60, 8 )
	color( plat, 0.4, 0.6, 1 )
	physics.addBody( plat, { density = 2, friction = 1, bounce = 0 } )
	platformJoint = physics.newJoint( "motor", anchor, plat )
	platformJoint.maxSpringForce = 2000

	-- hero
	hero = display.newRect( world, 40, H - 60, 16, 32 )
	color( hero, 0.3, 1, 0.4 )
	mover = physics.newMover( hero, { maxSpeed = 140, jumpHeight = 70 } )

	-- drop heavy balls above the bridge
	local catcher = display.newRect( world, CX, ( 150 + H ) / 2, W, H - 150 )
	catcher.isVisible = false
	catcher.isHitTestable = true
	catcher:toBack()
	catcher:addEventListener( "tap", function( e )
		local b = display.newCircle( world, e.x, math.min( e.y, 160 ), 10 )
		color( b, 0.9, 0.9, 0.9 )
		physics.addBody( b, { radius = 10, density = 8, bounce = 0.1 } )
		balls[#balls + 1] = b
		return true
	end )

	-- controls
	local ui = display.newGroup(); world:insert( ui )
	uiButton( ui, "◀", 30, 165, function() input.left = true; updateThrottle() end, function() input.left = false; updateThrottle() end )
	uiButton( ui, "▶", 80, 165, function() input.right = true; updateThrottle() end, function() input.right = false; updateThrottle() end )
	uiButton( ui, "jump", 130, 165, jump )
	uiButton( ui, "save", W - 80, 165, function() saved = physics.saveState(); print( "[step 6b] saved", saved ) end )
	uiButton( ui, "load", W - 30, 165, function()
		if not saved then print( "[step 6b] nothing saved yet" ); return end
		for _, o in ipairs( physics.restoreState( saved ) ) do display.remove( o ) end
	end )
	keyListener = function( e )
		local down = e.phase == "down"
		if e.keyName == "left" or e.keyName == "a" then input.left = down; updateThrottle()
		elseif e.keyName == "right" or e.keyName == "d" then input.right = down; updateThrottle()
		elseif ( e.keyName == "space" or e.keyName == "up" or e.keyName == "w" ) and down then jump() end
		return false
	end
	Runtime:addEventListener( "key", keyListener )
end

function M.update( physics )
	if not platformJoint or not platformJoint.jointType then return end
	t = t + 1 / display.fps
	if platformJoint.maxSpringForce then
		platformJoint:setLinearOffset( 40 * math.sin( t * 0.8 ), 50 + 20 * math.sin( t * 1.6 ) )
	end
end

function M.teardown()
	if keyListener then Runtime:removeEventListener( "key", keyListener ); keyListener = nil end
	if breakListener then Runtime:removeEventListener( "jointBreak", breakListener ); breakListener = nil end
	hero, mover, platformJoint, saved = nil, nil, nil, nil
	balls = {}
end

function M.status( physics )
	return string.format( "[step 6b] %s, joints broken %d%s — ◀ ▶ jump / arrows, tap = heavy ball",
		mover and mover.isOnGround and "on ground" or "in air", breaks, saved and ", state saved" or "" )
end

-- --------------------------------------------------------------------------
function M.selfTest( physics, world, done )
	local ok, fail = 0, 0
	local function check( c, m ) if c then ok = ok + 1 else fail = fail + 1; print( "FAIL: " .. m ) end end
	local function near( a, b, e ) return a ~= nil and b ~= nil and math.abs( a - b ) <= ( e or 1e-2 ) end
	local Y = 60

	local floor = display.newRect( world, CX, Y + 30, 200, 10 ); physics.addBody( floor, "static" )
	local guy = display.newRect( world, CX - 60, Y, 10, 20 )
	local m = physics.newMover( guy )
	local base = display.newRect( world, CX + 80, Y - 40, 6, 6 ); physics.addBody( base, "static" )
	local box = display.newRect( world, CX + 80, Y - 20, 10, 10 ); physics.addBody( box, { density = 1 } )
	local mj = physics.newJoint( "motor", base, box )
	mj:setLinearOffset( 0, 10 )
	local hookA = display.newRect( world, CX - 90, Y - 40, 6, 6 ); physics.addBody( hookA, "static" )
	local w = display.newCircle( world, CX - 90, Y - 10, 5 ); physics.addBody( w, { radius = 5, density = 1 } )
	local dj = physics.newJoint( "distance", hookA, w, hookA.x, hookA.y, w.x, w.y )
	dj.breakForce = 1e-3
	local broke = false
	w:addEventListener( "jointBreak", function( e ) broke = e.joint == dj end )
	local state = physics.saveState()
	check( state.bodyCount >= 5, "saveState" )

	timer.performWithDelay( 700, function()
		check( m.isOnGround, "mover on the ground" )
		check( near( guy.y + 10, Y + 25, 2 ), "mover feet on the floor (" .. guy.y .. ")" )
		check( near( box.y, Y - 30, 2 ), "motor joint offset (" .. box.y .. ")" )
		check( broke and dj.length == nil, "joint broke with an event" )
		local extra = display.newRect( world, CX, Y - 50, 8, 8 ); physics.addBody( extra )
		local removed = physics.restoreState( state )
		check( #removed == 1 and removed[1] == extra, "restoreState returns the newer bodies" )
		check( near( box.y, Y - 20, 1e-2 ) and near( guy.y, Y, 1e-2 ), "positions restored" )
		display.remove( extra )
		m:removeSelf()
		for _, o in ipairs{ floor, guy, base, box, hookA, w } do display.remove( o ) end
		print( string.format( "[box2d step-6b self-test] %d passed, %d failed", ok, fail ) )
		if done then done( ok, fail ) end
	end )
end

return M

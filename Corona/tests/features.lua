-- plugin.box2dv3 — test "v3 shapes & forces": Box2D v3 features.
--
-- Conveyor belts      : static bodies with "tangentSpeed" (px/s); tap a belt to reverse it
-- Falling objects     : "capsule" elements and rounded boxes ("cornerRadius")
-- Two balls           : same push, the right one has "rollingResistance" and stops sooner
-- Paddle              : kinematic body driven by obj:setTargetTransform() (pushes instead of teleporting)
-- Scanner             : physics.circleCast() — the ghost circle shows where a radius-8 ball would touch
-- Tap empty space     : physics.explode()
-- Hard impacts flash  : "hit" events (approachSpeed above physics.setHitEventThreshold())
-- "threads" button    : physics.setThreadCount( 1 / 2 / 4 )
-- "self-test"       : automated checks, results in the console
local M = { name = "features", title = "v3 shapes & forces", help = "tap a belt to reverse · tap empty space = explode" }

local W, H = display.contentWidth, display.contentHeight
local CX = display.contentCenterX

local timers, objects = {}, {}
local belts = {}
local paddle, scanGroup, hitListener, worldGroup
local hitCount, paddleT = 0, 0

local function color( o, r, g, b, a ) if o.setFillColor then o:setFillColor( r, g, b, a or 1 ) end end

local function flash( o )
	if not o or not o.x or not o.baseColor then return end
	color( o, 1, 1, 1 )
	timer.performWithDelay( 80, function() if o.x then color( o, unpack( o.baseColor ) ) end end )
end

local function spawnPiece( physics )
	local x = 70 + math.random( -15, 15 )
	local o
	if math.random() < 0.5 then
		o = display.newRect( worldGroup, x, 130, 30, 12 ) -- drawn as a rect, simulated as a capsule
		o.baseColor = { 0.95, 0.6, 0.2 }
		physics.addBody( o, { capsule = true, density = 1, friction = 0.6, bounce = 0.1 } )
	else
		o = display.newRoundedRect( worldGroup, x, 130, 18, 18, 5 )
		o.baseColor = { 0.4, 0.8, 1 }
		physics.addBody( o, { cornerRadius = 5, density = 1, friction = 0.6, bounce = 0.1 } )
	end
	color( o, unpack( o.baseColor ) )
	objects[#objects + 1] = o
	-- keep the scene small
	while #objects > 40 do display.remove( table.remove( objects, 1 ) ) end
end

function M.build( physics, world )
	worldGroup = world
	timers, objects, belts = {}, {}, {}
	hitCount, paddleT = 0, 0
	physics.setGravity( 0, 9.8 )
	physics.setHitEventThreshold( 150 ) -- px/s

	local floor = display.newRect( world, CX, H - 6, W, 12 )
	color( floor, 0.35, 0.45, 0.35 )
	physics.addBody( floor, "static", { friction = 0.8, bounce = 0 } )
	local wl = display.newRect( world, 3, H / 2, 6, H * 2 )
	local wr = display.newRect( world, W - 3, H / 2, 6, H * 2 )
	physics.addBody( wl, "static" ); physics.addBody( wr, "static" )

	-- conveyor belts (rounded ends)
	local specs = { { 140, 175, 220, 120 }, { 300, 230, 220, -120 } }
	for i, s in ipairs( specs ) do
		local b = display.newRoundedRect( world, s[1], s[2], s[3], 12, 6 )
		b.baseColor = { 0.5, 0.5, 0.55 }
		color( b, unpack( b.baseColor ) )
		physics.addBody( b, "static", { cornerRadius = 6, friction = 1, bounce = 0, tangentSpeed = s[4] } )
		b:addEventListener( "tap", function()
			b.tangentSpeed = -b.tangentSpeed
			return true
		end )
		belts[i] = b
	end

	-- rolling resistance: two balls pushed the same way
	for i, rr in ipairs{ 0, 0.3 } do
		local ball = display.newCircle( world, 40 + ( i - 1 ) * 22, H - 22, 10 )
		ball.baseColor = rr > 0 and { 0.9, 0.3, 0.3 } or { 0.3, 0.9, 0.3 }
		color( ball, unpack( ball.baseColor ) )
		physics.addBody( ball, { radius = 10, density = 1, friction = 1, bounce = 0, rollingResistance = rr } )
		timers[#timers + 1] = timer.performWithDelay( 4000, function()
			if ball.x then
				ball.x, ball.y = 40 + ( i - 1 ) * 22, H - 22
				ball:setLinearVelocity( 0, 0 )
				ball.angularVelocity = 0
				ball:applyLinearImpulseToCenter( ball.mass * 6, 0 ) -- 6 m/s
			end
		end, 0 )
	end

	-- kinematic paddle following a path with setTargetTransform
	paddle = display.newRect( world, W - 90, H - 60, 70, 8 )
	color( paddle, 0.8, 0.8, 0.3 )
	physics.addBody( paddle, "kinematic", { friction = 0.5 } )

	-- spawner
	timers[#timers + 1] = timer.performWithDelay( 450, function() spawnPiece( physics ) end, 0 )

	-- hit events
	hitListener = function( e )
		hitCount = hitCount + 1
		if e.approachSpeed > 250 then flash( e.object1 ); flash( e.object2 ) end
	end
	Runtime:addEventListener( "hit", hitListener )

	scanGroup = display.newGroup(); world:insert( scanGroup )

	-- explosions on empty space
	local catcher = display.newRect( world, CX, ( 30 + H ) / 2, W, H - 30 ) -- everything below the top bar
	catcher.isVisible = false
	catcher.isHitTestable = true
	catcher:toBack()
	catcher:addEventListener( "tap", function( event )
		physics.explode( event.x, event.y, 70, 1.5, 30 )
		local ring = display.newCircle( world, event.x, event.y, 70 )
		ring:setFillColor( 1, 0.6, 0.2, 0.25 ); ring.strokeWidth = 1
		transition.to( ring, { time = 250, alpha = 0, xScale = 1.4, yScale = 1.4, onComplete = function() display.remove( ring ) end } )
		return true
	end )
end

function M.update( physics )
	if not paddle or not paddle.x then return end
	-- paddle path: ellipse, one revolution every 4 s, rotating with the path
	paddleT = paddleT + 1 / display.fps
	local a = paddleT * math.pi * 0.5
	paddle:setTargetTransform( W - 90 + 50 * math.cos( a ), H - 70 + 20 * math.sin( a ), 20 * math.sin( a ) )

	-- scanner: a circle cast from the left at the height of the upper belt
	if scanGroup and scanGroup.numChildren then
		for i = scanGroup.numChildren, 1, -1 do scanGroup[i]:removeSelf() end
		local y = 150 + 15 * math.sin( paddleT * 2 )
		local hits = physics.circleCast( 10, y, W - 10, y, 8, "closest" )
		local ex = hits and hits[1].center.x or W - 10
		local l = display.newLine( scanGroup, 10, y, ex, y )
		l:setStrokeColor( 0.6, 0.9, 1, 0.6 )
		if hits then
			local c = display.newCircle( scanGroup, hits[1].center.x, hits[1].center.y, 8 )
			c:setFillColor( 0.6, 0.9, 1, 0.25 ); c.strokeWidth = 1
			local p = display.newCircle( scanGroup, hits[1].position.x, hits[1].position.y, 2 )
			p:setFillColor( 1, 0.3, 0.3 )
		end
	end
end

function M.teardown()
	for _, t in ipairs( timers ) do timer.cancel( t ) end
	timers, objects, belts = {}, {}, {}
	if hitListener then Runtime:removeEventListener( "hit", hitListener ); hitListener = nil end
	paddle, scanGroup = nil, nil
end

function M.status( physics )
	local cfg, used = physics.getThreadCount()
	return string.format( "hits %d, threads %d (step used %d)", hitCount, cfg, used )
end

-- --------------------------------------------------------------------------
function M.selfTest( physics, world, done )
	local ok, fail = 0, 0
	local function check( c, m ) if c then ok = ok + 1 else fail = fail + 1; print( "FAIL: " .. m ) end end
	local function near( a, b, e ) return a ~= nil and b ~= nil and math.abs( a - b ) <= ( e or 1e-2 ) end
	local Y = 40 -- above the scene

	local cap = display.newRect( world, 60, Y, 60, 20 )
	physics.addBody( cap, "static", { capsule = true } )
	check( physics.queryPoint( 31, Y ) ~= nil and physics.queryPoint( 32, Y - 9 ) == nil, "capsule fitted to the object" )

	local rr = display.newRect( world, 140, Y, 40, 40 )
	physics.addBody( rr, "static", { cornerRadius = 10 } )
	check( physics.queryPoint( 159.5, Y ) ~= nil and physics.queryPoint( 159, Y + 19 ) == nil, "rounded corners" )

	local belt = display.newRect( world, 220, Y, 40, 10 )
	physics.addBody( belt, "static", { tangentSpeed = 50 } )
	check( near( belt.tangentSpeed, 50, 1e-3 ), "tangentSpeed px/s" )

	check( near( ( physics.circleCast( 12, Y, 400, Y, 5 ) or { { center = {} } } )[1].center.x, 25, 0.3 ), "circleCast center" )
	check( physics.queryCircle( 140, Y + 30, 11 ) ~= nil and physics.queryCircle( 140, Y + 30, 9 ) == nil, "queryCircle" )

	local box = display.newRect( world, 300, Y, 10, 10 )
	physics.addBody( box, { density = 1 } )
	box.isFixedY = true
	box:applyLinearImpulseToCenter( box.mass, 0 )
	local vx = box:getLinearVelocity()
	check( near( vx, physics.getScale(), 1e-2 ), "applyLinearImpulseToCenter" )

	local cfg = physics.getThreadCount()
	physics.setThreadCount( 2 )
	check( physics.getThreadCount() == 2, "setThreadCount" )

	local hits = 0
	local drop = display.newCircle( world, 380, Y - 20, 5 )
	physics.addBody( drop, { radius = 5 } )
	local pad = display.newRect( world, 380, Y + 40, 30, 6 )
	physics.addBody( pad, "static" )
	drop:addEventListener( "hit", function() hits = hits + 1 end )

	timer.performWithDelay( 800, function()
		check( near( box.y, Y, 0.01 ), "isFixedY keeps y" )
		check( hits >= 1, "hit event on impact" )
		local _, used = physics.getThreadCount()
		check( used == 2, "step used 2 threads (" .. used .. ")" )
		physics.setThreadCount( cfg )
		for _, o in ipairs{ cap, rr, belt, box, drop, pad } do display.remove( o ) end
		print( string.format( "[self-test features] %d passed, %d failed", ok, fail ) )
		if done then done( ok, fail ) end
	end )
end

return M

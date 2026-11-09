-- plugin.box2dv3 — test "Bodies & shapes": addBody, element types, body properties / methods.
--
-- Tap empty space : spawn the next object (star outline, boot outline, fixed-rotation box, bullet,
--                   random box / circle / polygon / multi-element body)
-- Drag a body     : teleports it through obj.x / obj.y;  double tap: removes it (body destroyed with it)
-- Tap the purple  : toggles isSensor (it drops through everything, tap again to make it solid)
-- Terrain         : "chain"; ring: closed chain; platform: display.newLine + append; diamond: static newPolygon
local M = { name = "bodies", title = "Bodies & shapes", help = "tap = spawn · drag = teleport · double tap = remove · tap purple = isSensor" }

local W, H = display.contentWidth, display.contentHeight
local CX = display.contentCenterX

local function rnd( a, b ) return a + math.random() * ( b - a ) end

local spawnIndex = 0
local lastTap = {}

-- drag = teleport via obj.x / obj.y ; double tap = remove
local function onBodyTouch( event )
	local obj = event.target
	if event.phase == "began" then
		display.getCurrentStage():setFocus( obj )
		obj.isFocus = true
		obj.markX, obj.markY = obj.x - event.x, obj.y - event.y
		local now = system.getTimer()
		if lastTap[obj] and now - lastTap[obj] < 300 then
			display.getCurrentStage():setFocus( nil )
			display.remove( obj )
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

-- random box / circle / triangle / multi-element dumbbell
local function spawnRandom( physics, world, x, y )
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
		obj = display.newPolygon( world, x, y, { 0, -s, s, s * 0.8, -s, s * 0.8 } )
		-- display.newPolygon recentres its vertices on their bounding box; offset the shape the same way
		local cy = ( -s + s * 0.8 ) / 2
		physics.addBody( obj, { density = 1, friction = 0.3, bounce = 0.2,
			shape = { 0, -s - cy, s, s * 0.8 - cy, -s, s * 0.8 - cy } } )
	else
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
	obj:addEventListener( "tap", function() return true end )
	return obj
end

-- walls, a tilted "box" ramp, a kinematic spinner and a few random bodies
local function buildSandbox( physics, world )
	local left = display.newRect( world, 3, H / 2, 6, H * 2 )
	local right = display.newRect( world, W - 3, H / 2, 6, H * 2 )
	physics.addBody( left, "static" ); physics.addBody( right, "static" )
	local ramp = display.newRect( world, CX + 40, H * 0.45, 10, 10 )
	ramp.isVisible = false
	physics.addBody( ramp, "static", { box = { halfWidth = 60, halfHeight = 5, angle = -15 }, friction = 0.2 } )
	local rampGfx = display.newRect( world, CX + 40, H * 0.45, 120, 10 )
	rampGfx.rotation = -15
	rampGfx:setFillColor( 0.5 )
	local spinner = display.newRect( world, CX + 150, H * 0.62, 80, 8 )
	spinner:setFillColor( 0.9, 0.6, 0.2 )
	physics.addBody( spinner, "kinematic", { friction = 0.5 } )
	spinner.angularVelocity = 90 -- degrees / second
	for i = 1, 6 do spawnRandom( physics, world, rnd( 140, W - 140 ), rnd( 60, 110 ) ) end
end

function M.build( physics, world )
	lastTap = {}
	buildSandbox( physics, world )
	-- hilly terrain: a "chain" element (two-sided, smooth), drawn with a line
	local pts = {}
	for i = 0, 24 do
		local x = i * W / 24
		local y = H - 40 - 18 * math.sin( i * 0.7 ) - ( ( i == 0 or i == 24 ) and 120 or 0 )
		pts[#pts + 1] = x; pts[#pts + 1] = y
	end
	local terrain = display.newRect( world, 0, 0, 1, 1 )
	terrain.isVisible = false
	physics.addBody( terrain, "static", { chain = pts, friction = 0.6, bounce = 0 } )
	local terrainGfx = display.newLine( world, unpack( pts ) )
	terrainGfx.strokeWidth = 3
	terrainGfx:setStrokeColor( 0.4, 0.8, 0.4 )

	-- a closed chain ("connectFirstAndLastChainVertex") holding a ball inside: needs two-sided chains
	local ringPts = { -60, -40, 60, -40, 60, 40, -60, 40 }
	local ring = display.newRect( world, W - 80, 95, 1, 1 )
	ring.isVisible = false
	physics.addBody( ring, "static", { chain = ringPts, connectFirstAndLastChainVertex = true, bounce = 0.6 } )
	local ringGfx = display.newRect( world, W - 80, 95, 120, 80 )
	ringGfx:setFillColor( 0, 0, 0, 0 ); ringGfx.strokeWidth = 2; ringGfx:setStrokeColor( 0.8, 0.8, 0.3 )
	local ball = display.newCircle( world, W - 80, 95, 8 )
	ball:setFillColor( 1, 0.4, 0.4 )
	physics.addBody( ball, { radius = 8, density = 1, bounce = 0.6, friction = 0.2 } )
	ball:setLinearVelocity( 140, -60 )
	ball.isSleepingAllowed = false

	-- display.newLine platform with :append() (v2: line objects become static chains)
	local platform = display.newLine( world, 20, 150, 120, 150 )
	platform:append( 160, 170, 200, 170 )
	platform.strokeWidth = 3
	platform:setStrokeColor( 0.6, 0.7, 1 )
	physics.addBody( platform, { friction = 0.8, bounce = 0 } )

	-- static display.newPolygon (v2: becomes a closed chain along its outline)
	local diamond = display.newPolygon( world, CX - 10, 175, { -45, 0, 0, -28, 45, 0, 0, 28 } )
	diamond:setFillColor( 0.5, 0.5, 0.6 )
	physics.addBody( diamond, "static", { friction = 0.4, bounce = 0.2 } )

	-- sensor toggle
	local ghost = display.newRect( world, 70, 100, 34, 34 )
	ghost:setFillColor( 0.7, 0.3, 0.9 )
	physics.addBody( ghost, { density = 1, friction = 0.8, bounce = 0 } )
	ghost:addEventListener( "tap", function()
		ghost.isSensor = not ghost.isSensor
		ghost.alpha = ghost.isSensor and 0.4 or 1
		ghost.isAwake = true
		return true
	end )
	-- keep it on screen: when it fell below, bring it back above the platform
	M.ghost = ghost

	spawnIndex = 0
end

-- Spawns the next demo object at x, y.
function M.spawn( physics, world, x, y )
	spawnIndex = spawnIndex % 6 + 1
	if spawnIndex >= 5 then return spawnRandom( physics, world, x, y ) end
	local o
	if spawnIndex == 1 then
		o = display.newImage( world, "assets/star.png", x, y )
		physics.addBody( o, { outline = graphics.newOutline( 2, "assets/star.png" ), density = 1, friction = 0.5, bounce = 0.2 } )
	elseif spawnIndex == 2 then
		o = display.newImage( world, "assets/boot.png", x, y )
		physics.addBody( o, { outline = graphics.newOutline( 2, "assets/boot.png" ), density = 1, friction = 0.5, bounce = 0.1 } )
	elseif spawnIndex == 3 then
		o = display.newRect( world, x, y, 26, 26 )
		o:setFillColor( 0.9, 0.6, 0.2 )
		physics.addBody( o, { density = 1, friction = 0.6, bounce = 0 } )
		o.isFixedRotation = true -- never tips over
	else
		o = display.newCircle( world, x, y, 5 )
		o:setFillColor( 1, 1, 1 )
		physics.addBody( o, { radius = 5, density = 4, friction = 0.2, bounce = 0.4 } )
		o.isBullet = true
		o:applyLinearImpulse( ( x < CX and 1 or -1 ) * 2, -0.5, o.x, o.y ) -- fast shot
	end
	o:addEventListener( "tap", function() return true end )
	return o
end

function M.update()
	local g = M.ghost
	if g and g.y and g.y > H + 50 then
		g.isSensor = false
		g.alpha = 1
		g.x, g.y = 70, 100
		g:setLinearVelocity( 0, 0 )
	end
end

-- --------------------------------------------------------------------------
-- automated checks on real Solar2D display objects

function M.selfTest( physics, world, done )
	local ok, fail = 0, 0
	local function check( c, m ) if c then ok = ok + 1 else fail = fail + 1; print( "FAIL: " .. m ) end end
	local function near( a, b, e ) return math.abs( a - b ) <= ( e or 1e-3 ) end

	-- body types, velocities, teleport, removal
	local o = display.newRect( world, CX, 100, 20, 20 )
	physics.addBody( o, "kinematic" )
	check( o.bodyType == "kinematic", "bodyType get" )
	o.bodyType = "dynamic"
	check( o.bodyType == "dynamic", "bodyType set" )
	o:setLinearVelocity( 30, -10 )
	local lvx, lvy = o:getLinearVelocity()
	check( near( lvx, 30 ) and near( lvy, -10 ), "linear velocity roundtrip" )
	o.angularVelocity = 45
	check( near( o.angularVelocity, 45 ), "angularVelocity roundtrip" )
	o.x = 123
	check( o.x == 123, "teleport x" )
	check( physics.removeBody( o ), "removeBody" )
	check( o.bodyType == nil, "props removed after removeBody" )
	display.remove( o )
	local count = physics.getStats().bodyCount
	local t = display.newRect( world, CX, 100, 20, 20 )
	physics.addBody( t )
	display.remove( t )
	check( physics.getStats().bodyCount == count, "display.remove destroys body" )
	check( not pcall( physics.addBody, {}, "dynamic" ), "non display object rejected" )

	local b = display.newRect( world, CX, 40, 30, 30 ) -- 1 m x 1 m at scale 30
	physics.addBody( b, { density = 2 } )
	check( near( b.mass, 2 ), "mass" )
	for _, p in ipairs{ { "isBullet", true }, { "isSleepingAllowed", false }, { "isFixedRotation", true },
		{ "linearDamping", 0.5 }, { "angularDamping", 0.25 }, { "gravityScale", 0 } } do
		b[p[1]] = p[2]
		local v = b[p[1]]
		check( type( v ) == "number" and near( v, p[2] ) or v == p[2], p[1] .. " roundtrip" )
	end
	b.isFixedRotation = false
	b.linearDamping = 0
	b:setLinearVelocity( 0, 0 )
	b:applyLinearImpulse( 2, 0, b.x, b.y ) -- dv = J/m = 1 m/s = 30 px/s
	local vx = b:getLinearVelocity()
	check( near( vx, 30, 1e-2 ), "applyLinearImpulse (" .. vx .. ")" )
	local cx, cy = b:getMassWorldCenter()
	check( near( cx, b.x ) and near( cy, b.y ), "getMassWorldCenter" )
	check( near( physics.toMKS( "length", physics.getScale() ), 1 ), "toMKS" )
	b.isSensor = true
	check( b.isSensor == true and near( b.mass, 2 ), "isSensor rebuild keeps mass" )
	display.remove( b )

	local line = display.newLine( world, 0, 0, 10, 0 )
	line:append( 20, 5 )
	local lp = rawget( line, "_b2line" )
	check( lp and #lp == 6 and lp[5] == 20 and lp[6] == 5, "newLine / append hook recorded points" )
	physics.addBody( line, { friction = 1 } )
	check( line.bodyType == "static", "line forced static" )
	display.remove( line )

	local poly = display.newPolygon( world, 0, 0, { 0, 0, 40, 0, 20, 30 } )
	check( rawget( poly, "_b2polygon" ) ~= nil, "newPolygon hook" )
	display.remove( poly )

	local outline = graphics.newOutline( 2, "assets/star.png" )
	check( outline and outline._b2texW == 96 and outline._b2texH == 96, "newOutline hook texture size" )
	local s = display.newImage( world, "assets/star.png", CX, -200 )
	local before = physics.getStats().shapeCount
	physics.addBody( s, { outline = outline, density = 1 } )
	check( physics.getStats().shapeCount - before >= 2, "concave star split into convex pieces" )
	display.remove( s )

	check( physics.setMKS( "velocityThreshold", 1 ) and near( physics.getMKS( "velocityThreshold" ), 1 ), "setMKS / getMKS" )

	print( string.format( "[self-test bodies] %d passed, %d failed", ok, fail ) )
	if done then done( ok, fail ) end
	return ok, fail
end

return M

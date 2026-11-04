-- plugin.box2dv3 — step 5 test scene: rayCast, reflectRay, queryRegion, queryPoint, debug draw.
--
-- A laser sweeps from the left and bounces off shapes (rayCast "closest" + reflectRay).
-- Touch and drag on empty space : selection rectangle, objects inside light up (queryRegion).
-- Tap an object                 : it flashes (queryPoint) — shapes are tested exactly.
-- "draw" button                 : cycles physics.setDrawMode( "normal" / "hybrid" / "debug" ).
-- "self-test 5"                 : automated checks, results in the console.
local M = {}

local W, H = display.contentWidth, display.contentHeight
local CX = display.contentCenterX

local laserGroup, selection, objects
local angle = 0

local function tint( o, r, g, b ) if o.setFillColor then o:setFillColor( r, g, b ) end end

function M.build( physics, world )
	objects = {}
	physics.setGravity( 0, 0 )
	local function add( o, r, g, b )
		o.baseColor = { r, g, b }
		tint( o, r, g, b )
		objects[#objects + 1] = o
		return o
	end
	-- mirrors and obstacles
	local walls = {
		{ CX, 140, W - 40, 6 }, { CX, H - 20, W - 40, 6 }, { W - 22, ( 140 + H - 20 ) / 2, 6, H - 160 },
	}
	for _, w in ipairs( walls ) do
		local o = add( display.newRect( world, w[1], w[2], w[3], w[4] ), 0.5, 0.5, 0.6 )
		physics.addBody( o, "static" )
	end
	for i = 1, 6 do
		local x, y = 150 + ( i % 3 ) * 100, 180 + math.floor( ( i - 1 ) / 3 ) * 70
		local o
		if i % 2 == 0 then
			o = add( display.newCircle( world, x, y, 16 ), 0.3, 0.6, 0.9 )
			physics.addBody( o, "static", { radius = 16 } )
		else
			o = add( display.newRect( world, x, y, 40, 14 ), 0.9, 0.6, 0.3 )
			o.rotation = 25 * i
			physics.addBody( o, "static" )
		end
	end
	local star = add( display.newImage( world, "assets/star.png", 120, H - 60 ), 1, 1, 1 )
	physics.addBody( star, "static", { outline = graphics.newOutline( 2, "assets/star.png" ) } )

	laserGroup = display.newGroup(); world:insert( laserGroup )

	-- selection rectangle on a full-screen touch catcher (behind the objects)
	local catcher = display.newRect( world, CX, ( 140 + H ) / 2, W, H - 140 )
	catcher.isVisible = false
	catcher.isHitTestable = true
	catcher:toBack()
	catcher:addEventListener( "touch", function( event )
		if event.phase == "began" then
			display.getCurrentStage():setFocus( catcher )
			catcher.sx, catcher.sy = event.x, event.y
		elseif catcher.sx then
			display.remove( selection )
			local x1, y1, x2, y2 = catcher.sx, catcher.sy, event.x, event.y
			selection = display.newRect( world, ( x1 + x2 ) / 2, ( y1 + y2 ) / 2, math.abs( x2 - x1 ), math.abs( y2 - y1 ) )
			selection:setFillColor( 1, 1, 1, 0.1 ); selection.strokeWidth = 1
			for _, o in ipairs( objects ) do if o.x then tint( o, unpack( o.baseColor ) ) end end
			for _, o in ipairs( physics.queryRegion( x1, y1, x2, y2 ) or {} ) do tint( o, 0.3, 1, 0.3 ) end
			if event.phase ~= "moved" then
				display.getCurrentStage():setFocus( nil )
				catcher.sx = nil
			end
		end
		return true
	end )
	catcher:addEventListener( "tap", function( event )
		for _, o in ipairs( physics.queryPoint( event.x, event.y ) or {} ) do
			tint( o, 1, 1, 1 )
			timer.performWithDelay( 150, function() if o.x then tint( o, unpack( o.baseColor ) ) end end )
		end
		return true
	end )
end

-- laser with up to 8 bounces
function M.update( physics )
	if not laserGroup or not laserGroup.numChildren then return end
	for i = laserGroup.numChildren, 1, -1 do laserGroup[i]:removeSelf() end
	angle = angle + 0.4
	local a = math.rad( 10 + 25 * math.sin( math.rad( angle ) ) )
	local x, y = 30, 160
	local dx, dy = math.cos( a ), math.sin( a )
	for bounce = 1, 8 do
		local tx, ty = x + dx * 1000, y + dy * 1000
		local hits = physics.rayCast( x, y, tx, ty, "closest" )
		local hx, hy = tx, ty
		if hits then hx, hy = hits[1].position.x, hits[1].position.y end
		local l = display.newLine( laserGroup, x, y, hx, hy )
		l:setStrokeColor( 1, 0.2, 0.2, 1 - bounce * 0.1 ); l.strokeWidth = 2
		if not hits then break end
		dx, dy = physics.reflectRay( x, y, hits[1] )
		x, y = hx + dx * 0.01, hy + dy * 0.01 -- step off the surface
	end
end

function M.teardown()
	laserGroup, selection, objects = nil, nil, nil
end

function M.status( physics )
	return string.format( "[step 5] draw mode: %s — drag = queryRegion, tap = queryPoint", physics.getDrawMode() )
end

function M.selfTest( physics, world )
	local ok, fail = 0, 0
	local function check( c, m ) if c then ok = ok + 1 else fail = fail + 1; print( "FAIL: " .. m ) end end
	local function near( a, b, e ) return a ~= nil and math.abs( a - b ) <= ( e or 1e-2 ) end

	local a = display.newRect( world, 100, 60, 20, 20 ); physics.addBody( a, "static" )
	local b = display.newCircle( world, 200, 60, 10 ); physics.addBody( b, "static", { radius = 10 } )
	local hits = physics.rayCast( 0, 60, 400, 60, "sorted" )
	check( hits and #hits >= 2 and hits[1].object == a and hits[2].object == b, "sorted rayCast" )
	if hits then
		check( near( hits[1].position.x, 90 ) and near( hits[1].normal.x, -1 ), "hit position / normal" )
		check( hits[1].element == 1, "hit.element" )
		local rx = physics.reflectRay( 0, 60, hits[1] )
		check( near( rx, -1 ), "reflectRay" )
	end
	local r = physics.queryRegion( 85, 45, 215, 75 )
	check( r and #r == 2, "queryRegion" )
	check( physics.queryRegion( 111, 45, 189, 75 ) == nil, "queryRegion exact (gap is empty)" )
	check( physics.queryPoint( 207, 67 ) ~= nil and physics.queryPoint( 208.5, 68.5 ) == nil, "queryPoint exact on the circle" )
	display.remove( a )
	check( physics.queryPoint( 100, 60 ) == nil, "removed object not reported" )
	display.remove( b )
	print( string.format( "[box2d step-5 self-test] %d passed, %d failed", ok, fail ) )
	return ok, fail
end

return M

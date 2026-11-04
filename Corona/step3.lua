-- plugin.box2dv3 — step 3 test scene: collision / preCollision / postCollision events.
--
-- Tap empty space : drop a crate (flashes on "collision" began, shows impact force from "postCollision")
-- Blue platform   : one-way platform made with "preCollision" + event.contact.isEnabled = false
-- Yellow zone     : sensor; turns green while something is inside ("collision" began / ended)
-- Red button-pad  : removes whatever touches it (display.remove inside a collision listener)
-- "self-test 3"   : automated checks, results in the console
local M = {}

local W, H = display.contentWidth, display.contentHeight
local CX = display.contentCenterX

local counters = { began = 0, ended = 0, pre = 0, post = 0, maxForce = 0 }
M.counters = counters

local onRuntimeCollision, onRuntimePost

function M.build( physics, world )
	for k in pairs( counters ) do counters[k] = 0 end

	local ground = display.newRect( world, CX, H - 10, W, 20 )
	ground:setFillColor( 0.35, 0.45, 0.35 )
	physics.addBody( ground, "static", { friction = 0.6, bounce = 0 } )
	local l = display.newRect( world, 3, H / 2, 6, H * 2 )
	local r = display.newRect( world, W - 3, H / 2, 6, H * 2 )
	physics.addBody( l, "static", { friction = 0.2 } )
	physics.addBody( r, "static", { friction = 0.2 } )

	-- one-way platform: contacts are disabled while the other body is below the platform top
	local platform = display.newRect( world, CX - 110, H - 110, 150, 10 )
	platform:setFillColor( 0.3, 0.6, 1 )
	physics.addBody( platform, "static", { friction = 0.8, bounce = 0 } )
	function platform:preCollision( event )
		counters.pre = counters.pre + 1
		local other = event.other
		if other.y + ( other.height or 0 ) * 0.5 > self.y - 5 + 2 then
			event.contact.isEnabled = false -- pass through from below
		end
	end
	platform:addEventListener( "preCollision" )

	-- launch pad below the platform so crates jump through it
	local pad = display.newRect( world, CX - 110, H - 26, 60, 12 )
	pad:setFillColor( 0.9, 0.5, 0.1 )
	physics.addBody( pad, "static", { friction = 0.5, bounce = 0 } )
	function pad:collision( event )
		if event.phase == "began" then
			local o = event.other
			timer.performWithDelay( 1, function()
				if o.setLinearVelocity then o:setLinearVelocity( 0, -420 ) end
			end )
		end
	end
	pad:addEventListener( "collision" )

	-- sensor zone
	local zone = display.newRect( world, CX + 40, H - 120, 90, 90 )
	zone:setFillColor( 1, 0.9, 0.2, 0.25 )
	physics.addBody( zone, "static", { isSensor = true } )
	zone.inside = 0
	function zone:collision( event )
		self.inside = self.inside + ( event.phase == "began" and 1 or -1 )
		if self.inside > 0 then self:setFillColor( 0.3, 1, 0.3, 0.35 ) else self:setFillColor( 1, 0.9, 0.2, 0.25 ) end
	end
	zone:addEventListener( "collision" )

	-- destroyer pad
	local killer = display.newRect( world, W - 60, H - 26, 70, 12 )
	killer:setFillColor( 0.9, 0.2, 0.2 )
	physics.addBody( killer, "static" )
	function killer:collision( event )
		if event.phase == "began" then display.remove( event.other ) end
	end
	killer:addEventListener( "collision" )

	-- global listeners (count everything)
	onRuntimeCollision = function( event )
		if event.phase == "began" then counters.began = counters.began + 1 else counters.ended = counters.ended + 1 end
	end
	onRuntimePost = function( event )
		counters.post = counters.post + 1
		if event.force > counters.maxForce then counters.maxForce = event.force end
	end
	Runtime:addEventListener( "collision", onRuntimeCollision )
	Runtime:addEventListener( "postCollision", onRuntimePost )
end

function M.teardown()
	if onRuntimeCollision then Runtime:removeEventListener( "collision", onRuntimeCollision ) end
	if onRuntimePost then Runtime:removeEventListener( "postCollision", onRuntimePost ) end
	onRuntimeCollision, onRuntimePost = nil, nil
end

local function flash( obj )
	if obj.setFillColor then
		obj:setFillColor( 1, 1, 1 )
		timer.performWithDelay( 80, function() if obj.setFillColor then obj:setFillColor( obj.r, obj.g, obj.b ) end end )
	end
end

function M.spawn( physics, world, x, y )
	local s = math.random( 18, 34 )
	local crate = display.newRect( world, x, y, s, s )
	crate.r, crate.g, crate.b = 0.6 + math.random() * 0.4, 0.4, 0.2
	crate:setFillColor( crate.r, crate.g, crate.b )
	physics.addBody( crate, { density = 1, friction = 0.5, bounce = 0.1 } )
	function crate:collision( event )
		if event.phase == "began" then flash( self ) end
	end
	crate:addEventListener( "collision" )
	function crate:postCollision( event )
		if event.force > 2 and not self.label then
			self.label = display.newText{ parent = self.parent, text = string.format( "%.1f", event.force ),
				x = self.x, y = self.y - s, font = native.systemFont, fontSize = 10 }
			transition.to( self.label, { time = 500, y = self.y - s - 20, alpha = 0, onComplete = function( t )
				display.remove( t ); self.label = nil
			end } )
		end
	end
	crate:addEventListener( "postCollision" )
	crate:addEventListener( "tap", function() return true end )
	return crate
end

function M.status()
	return string.format( "[step 3] began %d ended %d pre %d post %d maxForce %.1f",
		counters.began, counters.ended, counters.pre, counters.post, counters.maxForce )
end

-- --------------------------------------------------------------------------
-- automated checks: runs a few frames, results printed when done
function M.selfTest( physics, world, done )
	local ok, fail = 0, 0
	local function check( c, m ) if c then ok = ok + 1 else fail = fail + 1; print( "FAIL: " .. m ) end end

	local floor = display.newRect( world, CX, 60, 120, 10 )
	physics.addBody( floor, "static", { bounce = 0 } )
	local box = display.newRect( world, CX, 20, 16, 16 )
	physics.addBody( box, { density = 1, bounce = 0 }, { radius = 4, density = 1 } )
	local sensor = display.newRect( world, CX + 80, 40, 40, 40 )
	physics.addBody( sensor, "static", { isSensor = true } )
	local ball = display.newCircle( world, CX + 80, 0, 5 )
	physics.addBody( ball, { radius = 5, density = 1 } )

	local localEvent, runtimeEvent, sensorPhases, preSeen = nil, nil, {}, false
	function box:collision( e ) if e.phase == "began" and e.other == floor then localEvent = e end end
	box:addEventListener( "collision" )
	local function rt( e ) if e.phase == "began" and ( e.object1 == box or e.object2 == box ) then runtimeEvent = e end end
	Runtime:addEventListener( "collision", rt )
	function sensor:collision( e ) if e.other == ball then sensorPhases[#sensorPhases + 1] = e.phase end end
	sensor:addEventListener( "collision" )
	function floor:preCollision( e ) preSeen = true end
	floor:addEventListener( "preCollision" )

	timer.performWithDelay( 1500, function()
		check( localEvent ~= nil, "local collision began" )
		if localEvent then
			check( localEvent.target == box and localEvent.other == floor, "target / other" )
			check( localEvent.selfElement >= 1 and localEvent.otherElement == 1, "selfElement / otherElement" )
			check( localEvent.contact ~= nil, "event.contact" )
		end
		check( runtimeEvent ~= nil, "Runtime collision began (not handled locally)" )
		if runtimeEvent then check( runtimeEvent.element1 ~= nil and runtimeEvent.object1 ~= nil, "object1 / element1" ) end
		check( sensorPhases[1] == "began" and sensorPhases[2] == "ended", "sensor began / ended (" .. table.concat( sensorPhases, "," ) .. ")" )
		check( preSeen, "preCollision dispatched" )
		Runtime:removeEventListener( "collision", rt )
		display.remove( floor ); display.remove( box ); display.remove( sensor ); display.remove( ball )
		print( string.format( "[box2d step-3 self-test] %d passed, %d failed", ok, fail ) )
		if done then done( ok, fail ) end
	end )
end

return M

-- Smoke-runs Corona/main.lua against the mock (catches Lua errors in the demo scene).
package.cpath = "./?.so;" .. package.cpath
package.path = "../Corona/?.lua;" .. package.path
dofile( "mock_solar2d.lua" )
local newRect, newGroup = display.newRect, display.newGroup
local function addMethods( o )
  rawset( o, "setFillColor", function() end ); rawset( o, "toBack", function() end ); rawset( o, "setStrokeColor", function() end ); return o
end
local function parentInsert( parent, o ) if parent and parent.insert then parent:insert( o ) end; return addMethods( o ) end
display.contentWidth, display.contentHeight, display.contentCenterX = 480, 320, 240
display.HiddenStatusBar = 0
function display.setStatusBar() end
function display.newRect( p, x, y, w, h ) if type( p ) ~= "table" then return addMethods( newRect( p, x, y, w ) ) end return parentInsert( p, newRect( x, y, w, h ) ) end
function display.newRoundedRect( p, x, y, w, h ) return parentInsert( p, newRect( x, y, w, h ) ) end
function display.newCircle( p, x, y, r ) return parentInsert( p, newRect( x, y, 2 * r, 2 * r ) ) end
function display.newPolygon( p, x, y, v ) return parentInsert( p, newRect( x, y, 30, 30 ) ) end
function display.newText( o ) local t = newRect( o.x, o.y, 10, 10 ); rawset( t, "text", "" ); return parentInsert( o.parent, t ) end
function display.newGroup() local g = newGroup(); return g end
function display.getCurrentStage() return { setFocus = function() end } end
native = { systemFont = "x" }
system = { getTimer = function() return os.clock() * 1000 end }
dofile( "../Corona/main.lua" )
advanceFrames( 120 )
Runtime:dispatchEvent{ name = "tap", x = 100, y = 200 }
advanceFrames( 60 )
print( "smoke ok, bodies:", require( "plugin.box2dv3" ).getStats().bodyCount )

-- step 2 scene on the plugin
do
	local physics = require( "plugin.box2dv3" )
	local step2 = require( "step2" )
	local nl, np, ni = display.newLine, display.newPolygon, display.newImage
	local function fix( o ) rawset( o, "setFillColor", function() end ); rawset( o, "setStrokeColor", function() end ); return o end
	display.newLine = function( ... ) return fix( nl( ... ) ) end
	display.newPolygon = function( ... ) return fix( np( ... ) ) end
	display.newImage = function( p, f, x, y ) local o = fix( ni( f ) ); o.x, o.y = x or 0, y or 0; return o end
	physics.stop(); physics.start()
	local g = display.newGroup()
	step2.build( physics, g )
	for i = 1, 8 do step2.spawn( physics, g, 40 + i * 40, 60 ); advanceFrames( 10 ); step2.update() end
	advanceFrames( 120 )
	local ok, fail = step2.selfTest( physics, g )
	print( "step2 scene ok, bodies:", physics.getStats().bodyCount, "selftest", ok, fail )
	physics.stop(); g:removeSelf()
	display.newLine, display.newPolygon, display.newImage = nl, np, ni
end

-- step 3 scene on the plugin
do
	local pending = {}
	timer = { performWithDelay = function( ms, fn ) pending[#pending + 1] = { t = ms, fn = fn } end }
	transition = { to = function( o, p ) if p.onComplete then pending[#pending + 1] = { t = p.time or 0, fn = function() p.onComplete( o ) end } end end }
	local function tick()
		for i = #pending, 1, -1 do
			local p = pending[i]
			p.t = p.t - 1000 / 60
			if p.t <= 0 then table.remove( pending, i ); p.fn() end
		end
	end
	Runtime:addEventListener( "enterFrame", tick )
	local physics = require( "plugin.box2dv3" )
	local step3 = require( "step3" )
	physics.stop(); physics.start()
	local g = display.newGroup()
	step3.build( physics, g )
	for i = 1, 10 do step3.spawn( physics, g, 30 + i * 40, 60 ); advanceFrames( 15 ) end
	local result
	step3.selfTest( physics, g, function( ok, fail ) result = { ok, fail } end )
	advanceFrames( 200 )
	print( "step3 scene ok:", step3.status(), "selftest", result and result[1], result and result[2] )
	step3.teardown()
	physics.stop(); g:removeSelf()

	-- step 4 scene
	timer.cancel = function() end
	local step4 = require( "step4" )
	physics.start()
	local g4 = display.newGroup()
	step4.build( physics, g4 )
	for _ = 1, 120 do advanceFrames( 1 ); step4.update() end
	local r4
	step4.selfTest( physics, g4, function( ok, fail ) r4 = { ok, fail } end )
	advanceFrames( 120 )
	print( "step4 scene ok:", step4.status(), "selftest", r4 and r4[1], r4 and r4[2] )
	step4.teardown()
	physics.stop(); g4:removeSelf()

	-- step 5 scene (+ debug draw)
	local step5 = require( "step5" )
	physics.start()
	local g5 = display.newGroup()
	step5.build( physics, g5 )
	physics.setDrawMode( "hybrid" )
	for _ = 1, 30 do advanceFrames( 1 ); step5.update( physics ) end
	physics.setDrawMode( "debug" )
	advanceFrames( 5 )
	physics.setDrawMode( "normal" )
	local ok5, fail5 = step5.selfTest( physics, g5 )
	print( "step5 scene ok:", step5.status( physics ), "selftest", ok5, fail5 )
	step5.teardown()
	physics.stop(); g5:removeSelf()

	-- step 6 scene (v3 features)
	local step6 = require( "step6" )
	physics.start()
	local g6 = display.newGroup()
	step6.build( physics, g6 )
	physics.setThreadCount( 2 )
	for _ = 1, 240 do advanceFrames( 1 ); step6.update( physics ) end
	local r6
	step6.selfTest( physics, g6, function( ok, fail ) r6 = { ok, fail } end )
	for _ = 1, 90 do advanceFrames( 1 ); step6.update( physics ) end
	print( "step6 scene ok:", step6.status( physics ), "selftest", r6 and r6[1], r6 and r6[2] )
	physics.setThreadCount( 1 )
	step6.teardown()
	physics.stop(); g6:removeSelf()

	-- step 6b scene (mover, motor, breakable joints, save / restore)
	local step6b = require( "step6b" )
	physics.start()
	local g7 = display.newGroup()
	step6b.build( physics, g7 )
	for _ = 1, 120 do advanceFrames( 1 ); step6b.update( physics ) end
	local r7
	step6b.selfTest( physics, g7, function( ok, fail ) r7 = { ok, fail } end )
	for _ = 1, 60 do advanceFrames( 1 ); step6b.update( physics ) end
	print( "step6b scene ok:", step6b.status( physics ), "selftest", r7 and r7[1], r7 and r7[2] )
	step6b.teardown()
	physics.stop(); g7:removeSelf()
	Runtime:removeEventListener( "enterFrame", tick )
end

-- benchmark module on the plugin (short runs)
local bench = require( "bench" )
local physics = require( "plugin.box2dv3" )
for _, name in ipairs( bench.order ) do
	physics.stop()
	if name ~= "limit" then bench.scenes[name].frames = 240 end
	local g = display.newGroup()
	local done
	bench.run{ engine = physics, name = "v3", scene = name, parent = g, onDone = function( r ) done = r end }
	local n = 0
	while not done and n < 8000 do advanceFrames( 1 ); n = n + 1 end
	assert( done, "bench " .. name .. " finished" )
	print( bench.format( done ) )
	physics.stop()
	g:removeSelf()
end

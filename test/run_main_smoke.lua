-- Smoke-runs the test app (Corona/main.lua + Corona/tests/*) against the mock: opens every test, taps,
-- runs "run all self-tests" and prints the report.
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
function display.newText( o ) local t = newRect( o.x, o.y, 10, 10 ); rawset( t, "text", "" ); return parentInsert( o.parent, t ) end
function display.newGroup() return newGroup() end
function display.getCurrentStage() return { setFocus = function() end } end
native = { systemFont = "x" }
system = { getTimer = function() return os.clock() * 1000 end }
local nl, np, ni = display.newLine, display.newPolygon, display.newImage
local function fix( o ) rawset( o, "setFillColor", function() end ); rawset( o, "setStrokeColor", function() end ); return o end
display.newLine = function( ... ) return fix( nl( ... ) ) end
display.newPolygon = function( p, x, y, v ) if type( p ) == "table" and p.insert then local o = fix( np( x, y, v ) ); p:insert( o ); return o end return fix( np( p, x, y ) ) end
display.newImage = function( p, f, x, y ) local o = fix( ni( f ) ); if type( p ) == "table" and p.insert then p:insert( o ) end; o.x, o.y = x or 0, y or 0; return o end

local pending = {}
timer = { performWithDelay = function( ms, fn ) pending[#pending + 1] = { t = ms, fn = fn }; return pending[#pending] end,
	cancel = function( h ) for i = #pending, 1, -1 do if pending[i] == h then table.remove( pending, i ) end end end }
transition = { to = function( o, p ) if p.onComplete then pending[#pending + 1] = { t = p.time or 0, fn = function() p.onComplete( o ) end } end end }
Runtime:addEventListener( "enterFrame", function()
	for i = #pending, 1, -1 do
		local p = pending[i]
		if p then
			p.t = p.t - 1000 / 60
			if p.t <= 0 then table.remove( pending, i ); p.fn() end
		end
	end
end )

local app = dofile( "../Corona/main.lua" )
local physics = require( "plugin.box2dv3" )
for _, t in ipairs( app.tests ) do
	if t.name ~= "benchmark" then
		app.open( t )
		advanceFrames( 30 )
		for i = 1, 4 do Runtime:dispatchEvent{ name = "tap", x = 60 + i * 80, y = 120 }; advanceFrames( 15 ) end
		advanceFrames( 60 )
		print( string.format( "opened %-28s bodies %d", t.title, physics.getStats().bodyCount ) )
	end
end
-- the printed report of "run all tests" is the result
app.runAll()
advanceFrames( 1500 )
physics.stop()
collectOrphans()

-- plugin.box2dv3 — test app
--
-- Top bar      : "☰ tests" opens the menu, then the current test's self-test, pause and debug draw mode.
-- Menu         : one test per feature group, "run all self-tests", gravity, thread count.
-- Each test is a module in tests/ with build / update / teardown / status / selfTest (and spawn: tap on
-- empty space). See the header of each file for what the scene shows.
display.setStatusBar( display.HiddenStatusBar )

local physics = require( "plugin.box2dv3" )
local widgets = require( "tests.widgets" )
print( "Loaded plugin.box2dv3 " .. physics.version .. " / " .. physics.engineVersion )

local W, H = display.contentWidth, display.contentHeight
local CX = display.contentCenterX
local BAR = 30 -- height of the top bar; taps below it go to the scene

local tests = {
	require( "tests.bodies" ),
	require( "tests.collisions" ),
	require( "tests.joints" ),
	require( "tests.queries" ),
	require( "tests.features" ),
	require( "tests.mover" ),
	require( "tests.benchmark" ),
}

local world = display.newGroup()
local ui = display.newGroup()
local current, paused, menu, report, pauseButton

-- --------------------------------------------------------------------------
-- scene switching

local function clearWorld()
	if current and current.teardown then current.teardown() end
	for i = world.numChildren, 1, -1 do world[i]:removeSelf() end
	display.getCurrentStage():setFocus( nil )
end

local function ground()
	local g = display.newRect( world, CX, H - 6, W, 12 )
	g:setFillColor( 0.35, 0.45, 0.35 )
	physics.addBody( g, "static", { friction = 0.6, bounce = 0 } )
end

local titleText, statusText

local function open( test )
	physics.stop()
	clearWorld()
	paused = false
	if pauseButton then pauseButton:setLabel( "pause" ) end
	current = test
	physics.start()
	physics.setGravity( 0, 9.8 )
	if test.name == "bodies" then ground() end
	test.build( physics, world )
	titleText.text = test.title .. "  ·  " .. test.help
end

-- --------------------------------------------------------------------------
-- top bar

local bar = display.newGroup(); ui:insert( bar )
local barBg = display.newRect( bar, CX, BAR / 2, W, BAR )
barBg:setFillColor( 0, 0, 0, 0.55 )
barBg:addEventListener( "touch", function() return true end ) -- the bar never reaches the scene
barBg:addEventListener( "tap", function() return true end )

local x = 4
local function barButton( label, w, fn, color )
	local b = widgets.button( bar, label, x + w / 2, BAR / 2, w, fn, { color = color } )
	x = x + w + 4
	return b
end

local function closeMenu() display.remove( menu ); menu = nil end
local openMenu
barButton( "☰ tests", 62, function() if menu then closeMenu() else openMenu() end end, { 0.3, 0.3, 0.45 } )
barButton( "self-test", 62, function()
	if current.noSelfTest then print( "no self-test for " .. current.title ); return end
	current.selfTest( physics, world, function( ok, fail )
		print( string.format( "[%s] %d passed, %d failed", current.title, ok, fail ) )
	end )
end, { 0.2, 0.4, 0.3 } )
pauseButton = barButton( "pause", 52, function( b )
	paused = not paused
	if paused then physics.pause() else physics.start() end
	b:setLabel( paused and "resume" or "pause" )
end )
local drawModes, drawIndex = { "normal", "hybrid", "debug" }, 1
barButton( "draw: normal", 82, function( b )
	drawIndex = drawIndex % #drawModes + 1
	physics.setDrawMode( drawModes[drawIndex] )
	b:setLabel( "draw: " .. drawModes[drawIndex] )
end )

titleText = display.newText{ parent = bar, text = "", x = x + 2, y = 9, width = W - x - 6,
	font = native.systemFont, fontSize = 9 }
titleText.anchorX = 0
statusText = display.newText{ parent = bar, text = "", x = x + 2, y = 21, width = W - x - 6,
	font = native.systemFont, fontSize = 9 }
statusText.anchorX = 0
statusText:setFillColor( 0.75, 0.9, 1 )

-- --------------------------------------------------------------------------
-- run all self-tests, one test after the other

local function showReport( lines )
	display.remove( report )
	report = display.newGroup(); ui:insert( report )
	local t = display.newText{ parent = report, text = table.concat( lines, "\n" ), x = CX, y = 0, width = W - 80,
		font = native.systemFont, fontSize = 11 }
	local bg = display.newRoundedRect( report, CX, 0, W - 60, t.height + 24, 6 )
	bg:setFillColor( 0, 0, 0, 0.85 )
	bg:toBack()
	report.y = H / 2
	bg:addEventListener( "touch", function( e )
		if e.phase == "ended" then display.remove( report ); report = nil end
		return true
	end )
end

local function runAll()
	closeMenu()
	local list = {}
	for _, t in ipairs( tests ) do if not t.noSelfTest then list[#list + 1] = t end end
	local lines, total, failed, i = { "Self-tests (tap to close)" }, 0, 0, 0
	local function nextTest()
		i = i + 1
		local t = list[i]
		if not t then
			lines[#lines + 1] = string.format( "TOTAL: %d passed, %d failed", total, failed )
			print( table.concat( lines, "\n" ) )
			showReport( lines )
			return
		end
		open( t )
		timer.performWithDelay( 500, function()
			if current ~= t then return end -- the user opened another test meanwhile
			t.selfTest( physics, world, function( ok, fail )
				total, failed = total + ok, failed + fail
				lines[#lines + 1] = string.format( "%s %-26s %d passed, %d failed", fail == 0 and "✓" or "✗", t.title, ok, fail )
				timer.performWithDelay( 100, nextTest )
			end )
		end )
	end
	nextTest()
end

-- --------------------------------------------------------------------------
-- menu

local threadChoices, threadIndex = { 1, 2, 4 }, 1
local flipped = false

function openMenu()
	closeMenu()
	menu = display.newGroup(); ui:insert( menu )
	local rows = #tests
	local panelH = 18 + rows * 26
	local bg = display.newRoundedRect( menu, 4 + 150, BAR + 4 + panelH / 2, 300, panelH, 6 )
	bg:setFillColor( 0.08, 0.08, 0.12, 0.95 )
	bg:addEventListener( "touch", function() return true end )
	bg:addEventListener( "tap", function() return true end )
	local y = BAR + 4 + 10 + 11
	for _, t in ipairs( tests ) do
		widgets.button( menu, t.title, 4 + 8 + 85, y, 170, function()
			closeMenu()
			open( t )
		end, { color = t == current and { 0.3, 0.45, 0.7 } or { 0.2, 0.22, 0.3 } } )
		y = y + 26
	end
	local ox, oy = 4 + 8 + 170 + 8 + 50, BAR + 4 + 10 + 11
	widgets.button( menu, "run all tests", ox, oy, 100, runAll, { color = { 0.2, 0.45, 0.3 } } )
	widgets.button( menu, flipped and "gravity: up" or "gravity: down", ox, oy + 26, 100, function( b )
		flipped = not flipped
		physics.setGravity( 0, flipped and -9.8 or 9.8 )
		b:setLabel( flipped and "gravity: up" or "gravity: down" )
	end )
	widgets.button( menu, "threads: " .. threadChoices[threadIndex], ox, oy + 52, 100, function( b )
		threadIndex = threadIndex % #threadChoices + 1
		physics.setThreadCount( threadChoices[threadIndex] )
		b:setLabel( "threads: " .. threadChoices[threadIndex] )
	end )
	widgets.button( menu, "restart test", ox, oy + 78, 100, function() closeMenu(); open( current ) end )
	widgets.button( menu, "close", ox, oy + 104, 100, closeMenu, { color = { 0.35, 0.2, 0.2 } } )
end

-- --------------------------------------------------------------------------
-- taps on empty space go to the scene (spawn)

Runtime:addEventListener( "tap", function( event )
	if menu or report or event.y <= BAR then return end
	if current and current.spawn then current.spawn( physics, world, event.x, event.y ) end
end )

Runtime:addEventListener( "enterFrame", function()
	ui:toFront() -- anything a scene puts on the stage stays below the bar and the menu
	if not current then return end
	if current.isBusy and current.isBusy() then return end
	if current.update and not paused then current.update( physics ) end
	local s = physics.getStats()
	local extra = current.status and current.status( physics ) or ""
	statusText.text = string.format( "%s%sbodies %d · awake %d · contacts %d · step %.2f ms%s", extra,
		extra ~= "" and "  |  " or "", s.bodyCount, s.awakeBodyCount or 0, s.contactCount or 0,
		s.stepTime or 0, paused and "  [paused]" or "" )
end )

open( tests[1] )

return { open = open, tests = tests, runAll = runAll } -- used by test/run_main_smoke.lua (ignored by Solar2D)

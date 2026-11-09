-- plugin.box2dv3 — test "Benchmark": the same stress scenes on plugin.box2dv3 and on Solar2D's built-in physics.
--
-- "scene" : pyramid / rain / big / limit / tower / mass / pile (see bench_core.lua)
-- "v3"    : plugin.box2dv3 with default settings
-- "v3+"   : plugin.box2dv3 with stiffer contacts (8 sub-steps, 60 Hz contacts)
-- "v2"    : Solar2D's built-in physics (Box2D 2.x)
local widgets = require( "tests.widgets" )
local bench = require( "tests.bench_core" )

local M = { name = "benchmark", title = "Benchmark v2 / v3", help = "pick a scene, then run it on v3, v3+ or v2", noSelfTest = true }

local W, H = display.contentWidth, display.contentHeight
local CX = display.contentCenterX

local okV2, physicsV2 = pcall( require, "physics" )
local sceneIndex = 1
local running, area, resultText, progress
local plugin

local function clearArea()
	if area and area.numChildren then
		for i = area.numChildren, 1, -1 do area[i]:removeSelf() end
	end
	display.remove( resultText ); resultText = nil
end

local function stopRun()
	if running then
		running.handle.cancel()
		running.engine.stop()
		if running.preset then plugin.setSubSteps( 4 ); plugin.setContactTuning( 30 ) end
		running = nil
	end
end

local function run( world, engine, name, preset )
	stopRun()
	plugin.stop() -- free the scene of the plugin world before any engine starts
	clearArea()
	if preset then preset() end
	local r = { engine = engine, preset = preset }
	r.handle = bench.run{
		engine = engine, name = name, scene = bench.order[sceneIndex], parent = area,
		onProgress = function( txt ) if progress then progress.text = txt end end,
		onDone = function( result )
			engine.stop() -- freeze the final state on screen
			if preset then plugin.setSubSteps( 4 ); plugin.setContactTuning( 30 ) end
			running = nil
			local text = bench.format( result )
			print( text )
			resultText = display.newText{ parent = world, text = text, x = CX, y = 100, width = W - 40,
				font = native.systemFont, fontSize = 10 }
			resultText.anchorY = 0
		end,
	}
	running = r
end

function M.build( physics, world )
	plugin = physics
	area = display.newGroup(); world:insert( area )
	local bar = display.newGroup(); world:insert( bar )
	local y = 62
	widgets.button( bar, "scene: " .. bench.order[sceneIndex], 60, y, 104, function( b )
		sceneIndex = sceneIndex % #bench.order + 1
		b:setLabel( "scene: " .. bench.order[sceneIndex] )
	end )
	widgets.button( bar, "run v3", 160, y, 80, function()
		run( world, physics, "box2dv3 (Box2D " .. physics.engineVersion:match( "[%d%.]+" ) .. ")" )
	end, { color = { 0.15, 0.45, 0.25 } } )
	widgets.button( bar, "run v3+", 250, y, 80, function()
		run( world, physics, "box2dv3 stiff (8 substeps, 60 Hz contacts)", function()
			physics.setSubSteps( 8 )
			physics.setContactTuning( 60 )
		end )
	end, { color = { 0.1, 0.55, 0.35 } } )
	widgets.button( bar, "run v2", 340, y, 80, function()
		if not okV2 then print( "built-in physics not available" ); return end
		run( world, physicsV2, "built-in physics (Box2D 2.x)" )
	end, { color = { 0.45, 0.25, 0.15 } } )
	progress = display.newText{ parent = bar, text = "", x = 8, y = 80, font = native.systemFont, fontSize = 10 }
	progress.anchorX, progress.anchorY = 0, 0
end

function M.status()
	return running and "running…" or ""
end

function M.teardown()
	stopRun()
	clearArea()
	area, progress = nil, nil
end

-- true while a benchmark owns the screen (the app does not draw its stats line then)
function M.isBusy() return running ~= nil end

return M

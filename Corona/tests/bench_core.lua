-- Benchmark and stress scenes for plugin.box2dv3, written against the v2 physics API so the very
-- same code runs on Solar2D's built-in physics (Box2D 2.x) for comparison.
--
--   local bench = require( "tests.bench_core" )
--   bench.run{ engine = physics, name = "v3", scene = "tower", parent = group, onDone = fn, onProgress = fn }
--
-- Every scene reports frame timing (event.time deltas). When the engine provides physics.getStats()
-- (plugin.box2dv3) the time spent in Box2D and in syncing display objects is reported too.
-- Scene specific metrics only use v2 API (x, y, rotation, isAwake, getLinearVelocity) so both
-- engines are measured the same way.
local M = {}

local W, H = display.contentWidth, display.contentHeight
local CX = display.contentCenterX
local GROUND_TOP = H - 12

-- --------------------------------------------------------------------------
-- helpers

local function avg( t )
	if #t == 0 then return 0 end
	local s = 0
	for i = 1, #t do s = s + t[i] end
	return s / #t
end

local function maxOf( t )
	local m = 0
	for i = 1, #t do if t[i] > m then m = t[i] end end
	return m
end

local function percentile( t, p )
	if #t == 0 then return 0 end
	local c = {}
	for i = 1, #t do c[i] = t[i] end
	table.sort( c )
	return c[math.max( 1, math.ceil( #c * p ) )]
end

local function addContainer( engine, parent, halfWidth )
	halfWidth = halfWidth or W / 2
	local ground = display.newRect( parent, CX, H - 6, W * 2, 12 )
	ground:setFillColor( 0.35, 0.45, 0.35 )
	engine.addBody( ground, "static", { friction = 0.6, bounce = 0 } )
	local l = display.newRect( parent, CX - halfWidth + 2, H / 2 - H, 4, H * 4 )
	local r = display.newRect( parent, CX + halfWidth - 2, H / 2 - H, 4, H * 4 )
	l:setFillColor( 0.5 ); r:setFillColor( 0.5 )
	engine.addBody( l, "static", { friction = 0.2, bounce = 0 } )
	engine.addBody( r, "static", { friction = 0.2, bounce = 0 } )
	return 3
end

local function box( engine, parent, x, y, w, h, density, friction )
	local b = display.newRect( parent, x, y, w, h )
	engine.addBody( b, "dynamic", { density = density or 1, friction = friction or 0.6, bounce = 0 } )
	return b
end

local function buildPyramid( engine, parent, rows, size )
	local list = {}
	local baseY = GROUND_TOP - size / 2
	for row = 0, rows - 1 do
		local count = rows - row
		local y = baseY - row * size
		local startX = CX - ( count - 1 ) * size / 2
		for i = 0, count - 1 do
			local b = box( engine, parent, startX + i * size, y, size, size )
			b:setFillColor( 0.55 + 0.4 * row / rows, 0.5, 0.9 - 0.5 * row / rows )
			list[#list + 1] = { obj = b, x0 = b.x, y0 = b.y }
		end
	end
	return list
end

local function spawnRandom( engine, parent, x, y, minSize, maxSize )
	local o
	if math.random() < 0.5 then
		local r = math.random( minSize, maxSize ) / 2
		o = display.newCircle( parent, x, y, r )
		engine.addBody( o, { density = 1, friction = 0.3, bounce = 0.2, radius = r } )
	else
		local s = math.random( minSize, maxSize )
		o = display.newRect( parent, x, y, s, s )
		engine.addBody( o, { density = 1, friction = 0.4, bounce = 0.1 } )
	end
	o:setFillColor( math.random(), math.random(), math.random() )
	return o
end

local function driftReport( tracked, lines )
	local drift, maxDrift, tilted = 0, 0, 0
	for _, t in ipairs( tracked ) do
		local dx, dy = t.obj.x - t.x0, t.obj.y - t.y0
		local d = math.sqrt( dx * dx + dy * dy )
		drift = drift + d
		if d > maxDrift then maxDrift = d end
		local r = math.abs( t.obj.rotation ) % 90
		if r > 10 and r < 80 then tilted = tilted + 1 end
	end
	if #tracked > 0 then drift = drift / #tracked end
	lines[#lines + 1] = string.format( "stability: drift avg %.2f px, max %.1f px, tilted boxes %d / %d",
		drift, maxDrift, tilted, #tracked )
end

local function awakeReport( objs, lines )
	local awake, speed = 0, 0
	for _, o in ipairs( objs ) do
		if o.isAwake then awake = awake + 1 end
		local vx, vy = o:getLinearVelocity()
		speed = speed + math.sqrt( vx * vx + vy * vy )
	end
	lines[#lines + 1] = string.format( "settling: awake %d / %d, avg speed %.2f px/s (jitter)",
		awake, #objs, #objs > 0 and speed / #objs or 0 )
end

-- --------------------------------------------------------------------------
-- scenes: setup( ctx ) -> bodyCount ; update( ctx, frame, fps ) -> stop? ; report( ctx, lines )

M.order = { "pyramid", "rain", "big", "limit", "tower", "mass", "pile" }

M.scenes = {
	pyramid = {
		label = "pyramid 24 rows (300 boxes)", frames = 600,
		setup = function( c )
			c.tracked = buildPyramid( c.engine, c.parent, 24, 11 )
			return #c.tracked
		end,
		report = function( c, lines ) driftReport( c.tracked, lines ) end,
	},

	big = {
		label = "big pyramid 40 rows (820 boxes)", frames = 600,
		setup = function( c )
			c.tracked = buildPyramid( c.engine, c.parent, 40, 7 )
			return #c.tracked
		end,
		report = function( c, lines ) driftReport( c.tracked, lines ) end,
	},

	rain = {
		label = "rain 1200 bodies", frames = 900,
		setup = function( c ) c.left = 1200; return 0 end,
		update = function( c )
			for _ = 1, math.min( 12, c.left ) do
				spawnRandom( c.engine, c.parent, math.random( 20, W - 20 ), -math.random( 10, 120 ), 6, 10 )
				c.left = c.left - 1
				c.bodies = c.bodies + 1
			end
		end,
	},

	-- STRESS: keep adding bodies until the frame rate drops; same display cost on both engines,
	-- so the difference is physics + sync cost.
	limit = {
		label = "limit: add bodies until fps < 55", frames = 7200,
		setup = function( c ) c.below = 0; c.limitAt = nil; return 0 end,
		update = function( c, frame, rollingFps )
			if c.limitAt then return true end
			if frame > 30 and rollingFps < 55 then
				c.below = c.below + 1
				if c.below >= 45 then c.limitAt = c.bodies; return true end -- 0.75 s below target
			else
				c.below = 0
				for _ = 1, 15 do
					spawnRandom( c.engine, c.parent, math.random( 20, W - 20 ), -math.random( 10, 200 ), 4, 7 )
					c.bodies = c.bodies + 1
				end
			end
			if c.bodies >= 8000 then c.limitAt = c.bodies; return true end
		end,
		report = function( c, lines )
			lines[#lines + 1] = string.format( "LIMIT: %d bodies before fps dropped below 55", c.limitAt or c.bodies )
		end,
	},

	-- STRESS: tall thin tower, classic Box2D v2 weakness (jitter, slow collapse)
	tower = {
		label = "tower: 3 columns x 40 boxes", frames = 900,
		setup = function( c )
			c.tracked, c.tops = {}, {}
			local size = 7
			for col = -1, 1 do
				local x = CX + col * 120
				for i = 0, 39 do
					local b = box( c.engine, c.parent, x, GROUND_TOP - size / 2 - i * size, size, size, 1, 0.6 )
					b:setFillColor( 0.9, 0.7 - 0.4 * i / 40, 0.3 )
					c.tracked[#c.tracked + 1] = { obj = b, x0 = b.x, y0 = b.y }
					if i == 39 then c.tops[#c.tops + 1] = { obj = b, x0 = b.x, y0 = b.y } end
				end
			end
			return #c.tracked
		end,
		report = function( c, lines )
			driftReport( c.tracked, lines )
			for i, t in ipairs( c.tops ) do
				lines[#lines + 1] = string.format( "tower %d top: dx %.1f px, dy %.1f px%s", i,
					t.obj.x - t.x0, t.obj.y - t.y0, math.abs( t.obj.x - t.x0 ) > 7 and "  -> COLLAPSED" or "" )
			end
			local objs = {}
			for _, t in ipairs( c.tracked ) do objs[#objs + 1] = t.obj end
			awakeReport( objs, lines )
		end,
	},

	-- STRESS: large mass ratios (heavy on light). v2 sinks / explodes, v3 soft step copes much better.
	mass = {
		label = "mass ratio: 1 : 100 : 10000", frames = 600,
		setup = function( c )
			c.cases = {}
			local n = 0
			for k, ratio in ipairs{ 10, 100, 10000 } do
				local x = CX + ( k - 2 ) * 150
				local l1 = box( c.engine, c.parent, x - 25, GROUND_TOP - 10, 20, 20, 1 )
				local l2 = box( c.engine, c.parent, x + 25, GROUND_TOP - 10, 20, 20, 1 )
				local plankY = GROUND_TOP - 20 - 6
				local plank = box( c.engine, c.parent, x, plankY, 90, 12, ratio )
				local heavyY = plankY - 6 - 20
				local heavy = box( c.engine, c.parent, x, heavyY, 40, 40, ratio )
				l1:setFillColor( 0.4, 0.8, 1 ); l2:setFillColor( 0.4, 0.8, 1 )
				plank:setFillColor( 0.8, 0.6, 0.3 ); heavy:setFillColor( 0.9, 0.3, 0.3 )
				c.cases[k] = { ratio = ratio, lights = { l1, l2 }, plank = plank, plankY = plankY, heavy = heavy, heavyY = heavyY }
				n = n + 4
			end
			return n
		end,
		report = function( c, lines )
			for _, cs in ipairs( c.cases ) do
				lines[#lines + 1] = string.format( "ratio 1:%-5d plank sink %.2f px, heavy sink %.2f px, plank tilt %.1f deg%s",
					cs.ratio, cs.plank.y - cs.plankY, cs.heavy.y - cs.heavyY, cs.plank.rotation,
					( cs.plank.y - cs.plankY > 15 ) and "  -> CRUSHED" or "" )
			end
		end,
	},

	-- STRESS: 1500 small bodies piled in a narrow bin: contact count, settling and jitter
	pile = {
		label = "pile: 1500 bodies in a narrow bin", frames = 1200,
		container = 110,
		setup = function( c ) c.left = 1500; c.objs = {}; return 0 end,
		update = function( c )
			for _ = 1, math.min( 10, c.left ) do
				local o = spawnRandom( c.engine, c.parent, CX + math.random( -95, 95 ), -math.random( 10, 300 ), 5, 7 )
				c.objs[#c.objs + 1] = o
				c.left = c.left - 1
				c.bodies = c.bodies + 1
			end
		end,
		report = function( c, lines )
			local escaped, top = 0, H
			for _, o in ipairs( c.objs ) do
				if math.abs( o.x - CX ) > 112 or o.y > H then escaped = escaped + 1 end
				if o.y < top then top = o.y end
			end
			awakeReport( c.objs, lines )
			lines[#lines + 1] = string.format( "pile top at y=%.0f, bodies escaped through walls/ground: %d", top, escaped )
		end,
	},
}

-- --------------------------------------------------------------------------

-- opts: engine, name, scene, parent, onDone(result), onProgress(text)
function M.run( opts )
	local engine, parent = opts.engine, opts.parent
	local sceneName = opts.scene or "pyramid"
	local scene = assert( M.scenes[sceneName], "unknown scene " .. tostring( sceneName ) )
	local hasStats = type( engine.getStats ) == "function"

	engine.start()
	engine.setGravity( 0, 9.8 )

	local c = { engine = engine, parent = parent, bodies = 0 }
	c.bodies = addContainer( engine, parent, scene.container ) + scene.setup( c )

	local frameMs, stepMs, syncMs, physMs = {}, {}, {}, {}
	local frame, lastTime = 0, nil
	local startWall = system.getTimer()

	local listener
	listener = function( event )
		frame = frame + 1
		if lastTime then frameMs[#frameMs + 1] = event.time - lastTime end
		lastTime = event.time

		-- fps over the last 30 frames
		local n, sum = 0, 0
		for i = #frameMs, math.max( 1, #frameMs - 29 ), -1 do n = n + 1; sum = sum + frameMs[i] end
		local rollingFps = n > 0 and 1000 * n / sum or 60

		if hasStats then
			local s = engine.getStats()
			stepMs[#stepMs + 1] = s.stepTime or 0
			syncMs[#syncMs + 1] = s.syncTime or 0
			physMs[#physMs + 1] = s.frameTime or 0
		end

		local stop = scene.update and scene.update( c, frame, rollingFps )

		if opts.onProgress and frame % 15 == 0 then
			local txt = string.format( "[%s] %s  frame %d  bodies %d  fps %.1f", opts.name, scene.label,
				frame, c.bodies, rollingFps )
			if hasStats then
				txt = txt .. string.format( "  physics %.2f ms (step %.2f, sync %.2f)",
					physMs[#physMs], stepMs[#stepMs], syncMs[#syncMs] )
			end
			opts.onProgress( txt )
		end

		if stop or frame >= scene.frames then
			Runtime:removeEventListener( "enterFrame", listener )
			local lines = {}
			if scene.report then scene.report( c, lines ) end
			local result = {
				engine = opts.name, scene = sceneName, label = scene.label, bodies = c.bodies, frames = frame,
				wallSeconds = ( system.getTimer() - startWall ) / 1000,
				avgFps = 1000 / math.max( avg( frameMs ), 1e-6 ), avgFrameMs = avg( frameMs ),
				p95FrameMs = percentile( frameMs, 0.95 ), maxFrameMs = maxOf( frameMs ),
				avgStepMs = hasStats and avg( stepMs ) or nil, maxStepMs = hasStats and maxOf( stepMs ) or nil,
				avgSyncMs = hasStats and avg( syncMs ) or nil, avgPhysicsMs = hasStats and avg( physMs ) or nil,
				p95PhysicsMs = hasStats and percentile( physMs, 0.95 ) or nil,
				lines = lines,
			}
			if opts.onDone then opts.onDone( result ) end
		end
	end
	Runtime:addEventListener( "enterFrame", listener )
	return { cancel = function() Runtime:removeEventListener( "enterFrame", listener ) end }
end

function M.format( r )
	local out = {
		string.format( "== %s : %s ==", r.engine, r.label ),
		string.format( "bodies %d, frames %d, wall %.1f s", r.bodies, r.frames, r.wallSeconds ),
		string.format( "fps avg %.1f | frame ms avg %.2f  p95 %.2f  max %.2f", r.avgFps, r.avgFrameMs, r.p95FrameMs, r.maxFrameMs ),
	}
	if r.avgStepMs then
		out[#out + 1] = string.format( "physics ms avg %.2f  p95 %.2f | box2d step avg %.2f  max %.2f | sync avg %.2f",
			r.avgPhysicsMs, r.p95PhysicsMs, r.avgStepMs, r.maxStepMs, r.avgSyncMs )
	end
	for _, l in ipairs( r.lines or {} ) do out[#out + 1] = l end
	return table.concat( out, "\n" )
end

return M

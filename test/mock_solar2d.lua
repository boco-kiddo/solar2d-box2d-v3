-- Minimal imitation of Solar2D's display object proxy model, enough to test plugin.box2dv3 on a
-- desktop Lua 5.1. Display objects are tables with a "_proxy" userdata and the shared metatable
-- Runtime._proxy whose __index/__newindex are functions (like Rtt_LuaProxy.cpp). removeSelf()
-- dispatches "finalize", clears _proxy and sets the metatable to nil (LuaProxy::RestoreTable).

local native = setmetatable( {}, { __mode = "k" } ) -- obj -> native property table

-- EventDispatcher like Solar2D's init.lua: listeners live in the object's own _functionListeners /
-- _tableListeners fields; dispatchEvent returns true if a listener returned true; it does not set
-- event.target.
local EventDispatcher = {}
function EventDispatcher.addEventListener( self, name, listener )
	local field = type( listener ) == "function" and "_functionListeners" or "_tableListeners"
	local dict = self[field]
	if not dict then dict = {}; self[field] = dict end
	dict[name] = dict[name] or {}
	table.insert( dict[name], listener )
	return true
end
function EventDispatcher.removeEventListener( self, name, listener )
	for _, field in ipairs{ "_functionListeners", "_tableListeners" } do
		local dict = self[field]
		local list = dict and dict[name]
		if list then
			for i = #list, 1, -1 do if list[i] == listener then table.remove( list, i ) end end
		end
	end
end
function EventDispatcher.dispatchEvent( self, event )
	local result = false
	local f = self._functionListeners and self._functionListeners[event.name]
	if f then for _, fn in ipairs{ unpack( f ) } do result = fn( event ) or result end end
	local t = self._tableListeners and self._tableListeners[event.name]
	if t then
		for _, o in ipairs{ unpack( t ) } do
			local m = o[event.name]
			if type( m ) == "function" then result = m( o, event ) or result end
		end
	end
	return result
end

local methods = {}
local proxyMt = {}

proxyMt.__index = function( t, k )
	local n = native[t]
	if not n then
		print( "WARNING: Attempting to get property (" .. tostring( k ) .. ") on a removed DisplayObject" )
		return nil
	end
	if type( k ) == "number" then return n.children[k] end
	if k == "parent" then return n.parent end
	if n.props[k] ~= nil then return n.props[k] end
	if methods[k] then return methods[k] end
	return nil
end

proxyMt.__newindex = function( t, k, v )
	local n = native[t]
	if not n then
		print( "WARNING: Attempting to set property (" .. tostring( k ) .. ") on a removed DisplayObject" )
		return
	end
	if n.props[k] ~= nil then
		n.props[k] = v
	else
		rawset( t, k, v )
	end
end

function methods.addEventListener( self, name, listener )
	return EventDispatcher.addEventListener( self, name, listener or self )
end
function methods.removeEventListener( self, name, listener )
	return EventDispatcher.removeEventListener( self, name, listener or self )
end
methods.dispatchEvent = EventDispatcher.dispatchEvent

function methods.translate( self, dx, dy )
	local p = native[self].props
	p.x, p.y = p.x + dx, p.y + dy
end

function methods.rotate( self, d )
	local p = native[self].props
	p.rotation = p.rotation + d
end

-- Like Solar2D: removeSelf() only orphans the object (and its children); "finalize" is dispatched and
-- the table is stripped later, when the display collects unreachable objects (end of the frame).
local orphanage = {}
local function finalizeNow( self )
	local n = native[self]
	if not n then return end
	if n.props.numChildren then
		for i = #n.children, 1, -1 do finalizeNow( n.children[i] ) end
	end
	methods.dispatchEvent( self, { name = "finalize", target = self } )
	native[self] = nil
	rawset( self, "_proxy", nil )
	setmetatable( self, nil )
end
function methods.removeSelf( self )
	local n = native[self]
	if not n or n.orphan then return end
	n.orphan = true
	if n.parent and native[n.parent] then
		local siblings = native[n.parent].children
		for i = #siblings, 1, -1 do if siblings[i] == self then table.remove( siblings, i ) end end
		native[n.parent].props.numChildren = #siblings
	end
	orphanage[#orphanage + 1] = self
end
-- groups: content -> local through x / y / xScale / yScale of the parent chain (no rotation)
function methods.contentToLocal( self, x, y )
	local chain, o = {}, self
	while o do chain[#chain + 1] = o; o = native[o] and native[o].parent end
	for i = #chain, 1, -1 do
		local p = native[chain[i]].props
		x = ( x - ( p.x or 0 ) ) / ( p.xScale or 1 )
		y = ( y - ( p.y or 0 ) ) / ( p.yScale or 1 )
	end
	return x, y
end
function methods.setFillColor() end
function methods.setStrokeColor() end
function methods.toFront( self )
	local n = native[self]
	if n.parent and native[n.parent] then
		local siblings = native[n.parent].children
		for i = #siblings, 1, -1 do if siblings[i] == self then table.remove( siblings, i ) end end
		table.insert( siblings, self )
	end
end
function collectOrphans()
	local list = orphanage
	orphanage = {}
	for _, o in ipairs( list ) do finalizeNow( o ) end
end

function methods.insert( self, child )
	local old = native[child].parent
	if old and native[old] then
		local siblings = native[old].children
		for i = #siblings, 1, -1 do if siblings[i] == child then table.remove( siblings, i ) end end
		native[old].props.numChildren = #siblings
	end
	table.insert( native[self].children, child )
	native[self].props.numChildren = #native[self].children
	native[child].parent = self
end

local function newObject( props, isGroup )
	local t = { _proxy = newproxy( true ) }
	native[t] = {
		props = {
			x = props.x or 0, y = props.y or 0, rotation = 0,
			width = props.width or 0, height = props.height or 0,
			anchorX = 0.5, anchorY = 0.5, xScale = 1, yScale = 1,
			numChildren = isGroup and 0 or nil, anchorChildren = isGroup and false or nil,
		},
		listeners = {},
		children = {},
	}
	return setmetatable( t, proxyMt )
end

display = { fps = 60, contentCenterX = 160, contentCenterY = 240 }
local function withParent( fn )
	return function( a, ... )
		if type( a ) == "table" then
			local o = fn( ... )
			a:insert( o )
			return o
		end
		return fn( a, ... )
	end
end
display.newRect = withParent( function( x, y, w, h ) return newObject{ x = x, y = y, width = w, height = h } end )
display.newCircle = withParent( function( x, y, r ) return newObject{ x = x, y = y, width = 2 * r, height = 2 * r } end )
function display.newGroup() return newObject( {}, true ) end

-- lines: object at the first point, like Solar2D
local function bounds( pts )
	local minX, maxX, minY, maxY = math.huge, -math.huge, math.huge, -math.huge
	for i = 1, #pts, 2 do
		minX, maxX = math.min( minX, pts[i] ), math.max( maxX, pts[i] )
		minY, maxY = math.min( minY, pts[i + 1] ), math.max( maxY, pts[i + 1] )
	end
	return minX, maxX, minY, maxY
end
function display.newLine( ... )
	local args = { ... }
	local parent
	if type( args[1] ) == "table" then parent = table.remove( args, 1 ) end
	local minX, maxX, minY, maxY = bounds( args )
	local o = newObject{ x = args[1], y = args[2], width = maxX - minX, height = maxY - minY }
	native[o].linePoints = args
	if parent then parent:insert( o ) end
	return o
end
function methods.append( self, ... )
	for _, v in ipairs{ ... } do table.insert( native[self].linePoints, v ) end
end
display.newPolygon = withParent( function( x, y, verts )
	local minX, maxX, minY, maxY = bounds( verts )
	return newObject{ x = x, y = y, width = maxX - minX, height = maxY - minY }
end )
function display.newImage( file )
	local o = newObject{ x = 0, y = 0, width = 64, height = 32 } -- pretend every image is 64x32
	return o
end
graphics = {}
function graphics.newOutline( coarseness, file )
	-- an L shaped (concave) outline in texel coordinates of a 64x32 image
	return { 0, 0, 64, 0, 64, 16, 32, 16, 32, 32, 0, 32 }
end
function display.remove( o ) if o and o.removeSelf then o:removeSelf() end end

Runtime = { _proxy = proxyMt }
Runtime.addEventListener = EventDispatcher.addEventListener
Runtime.removeEventListener = EventDispatcher.removeEventListener
Runtime.dispatchEvent = EventDispatcher.dispatchEvent
function Runtime.listenerCount( name )
	local n = 0
	for _, field in ipairs{ "_functionListeners", "_tableListeners" } do
		local d = Runtime[field]
		n = n + #( d and d[name] or {} )
	end
	return n
end

local frameTime = 0
-- Simulates n frames at display.fps
function advanceFrames( n )
	for _ = 1, n do
		frameTime = frameTime + 1000 / display.fps
		Runtime:dispatchEvent{ name = "enterFrame", time = frameTime }
		collectOrphans()
	end
end

function isPhysicsMt( o ) local mt = getmetatable( o ); return mt ~= nil and rawget( mt, "__box2d" ) == true end

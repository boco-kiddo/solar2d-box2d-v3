// plugin.box2dv3 — physics.setDrawMode( "normal" | "hybrid" | "debug" ).
//
// Box2D's debug draw output (b2World_Draw) is collected into plain arrays first, then turned into
// Solar2D display objects in a group kept on top of the stage. "hybrid" draws translucent shapes
// over the scene; "debug" draws them over an opaque background so only the physics is visible.
// The group is rebuilt every frame while a debug mode is on (debugging aid, not for release).
#include "b2plugin.h"

#include <string.h>
#include <vector>

namespace
{
struct DrawPolygon
{
	std::vector<b2Vec2> points; // meters
	b2HexColor color;
};

struct DrawCircle
{
	b2Vec2 center;
	float radius;
	b2Vec2 axis;
	b2HexColor color;
	bool solid;
};

struct DrawLine
{
	b2Vec2 a, b;
	b2HexColor color;
};

struct DrawList
{
	std::vector<DrawPolygon> polygons;
	std::vector<DrawCircle> circles;
	std::vector<DrawLine> lines;
};

b2Vec2 ToVec( b2Pos p )
{
	return b2Vec2{ (float)p.x, (float)p.y };
}

b2Vec2 Apply( b2WorldTransform t, b2Vec2 v )
{
	b2Vec2 r = b2RotateVector( t.q, v );
	return b2Vec2{ (float)t.p.x + r.x, (float)t.p.y + r.y };
}

void DrawSolidPolygon( b2WorldTransform t, const b2Vec2* v, int n, float radius, b2HexColor color, void* ctx )
{
	(void)radius;
	DrawList* d = (DrawList*)ctx;
	DrawPolygon p;
	p.color = color;
	p.points.reserve( (size_t)n );
	for ( int i = 0; i < n; ++i )
	{
		p.points.push_back( Apply( t, v[i] ) );
	}
	d->polygons.push_back( p );
}

void DrawPolygonOutline( b2WorldTransform t, const b2Vec2* v, int n, b2HexColor color, void* ctx )
{
	DrawList* d = (DrawList*)ctx;
	for ( int i = 0; i < n; ++i )
	{
		d->lines.push_back( DrawLine{ Apply( t, v[i] ), Apply( t, v[( i + 1 ) % n] ), color } );
	}
}

void DrawSolidCircle( b2WorldTransform t, b2Vec2 center, float radius, b2HexColor color, void* ctx )
{
	DrawList* d = (DrawList*)ctx;
	d->circles.push_back( DrawCircle{ Apply( t, center ), radius, b2Rot_GetXAxis( t.q ), color, true } );
}

void DrawCircleOutline( b2Pos center, float radius, b2HexColor color, void* ctx )
{
	DrawList* d = (DrawList*)ctx;
	d->circles.push_back( DrawCircle{ ToVec( center ), radius, b2Vec2{ 1.0f, 0.0f }, color, false } );
}

void DrawSolidCapsule( b2Pos p1, b2Pos p2, float radius, b2HexColor color, void* ctx )
{
	DrawList* d = (DrawList*)ctx;
	b2Vec2 a = ToVec( p1 ), b = ToVec( p2 );
	b2Vec2 axis = b2Normalize( b2Sub( b, a ) );
	b2Vec2 n = b2MulSV( radius, b2LeftPerp( axis ) );
	DrawPolygon body;
	body.color = color;
	body.points = { b2Add( a, n ), b2Sub( a, n ), b2Sub( b, n ), b2Add( b, n ) };
	d->polygons.push_back( body );
	d->circles.push_back( DrawCircle{ a, radius, axis, color, true } );
	d->circles.push_back( DrawCircle{ b, radius, axis, color, true } );
}

void DrawSegment( b2Pos p1, b2Pos p2, b2HexColor color, void* ctx )
{
	DrawList* d = (DrawList*)ctx;
	d->lines.push_back( DrawLine{ ToVec( p1 ), ToVec( p2 ), color } );
}

void DrawTransformNoop( b2WorldTransform, void* )
{
}

void DrawPointNoop( b2Pos, float, b2HexColor, void* )
{
}

void DrawStringNoop( b2Pos, const char*, b2HexColor, void* )
{
}

void DrawBoundsNoop( b2AABB, b2HexColor, void* )
{
}

// ---- Lua side -----------------------------------------------------------------------

DrawList* sList = nullptr;
PluginState* sState = nullptr;

void SetColor( lua_State* L, int obj, const char* method, b2HexColor c, float alpha )
{
	lua_getfield( L, obj, method );
	lua_pushvalue( L, obj );
	lua_pushnumber( L, ( ( c >> 16 ) & 0xFF ) / 255.0 );
	lua_pushnumber( L, ( ( c >> 8 ) & 0xFF ) / 255.0 );
	lua_pushnumber( L, ( c & 0xFF ) / 255.0 );
	lua_pushnumber( L, alpha );
	lua_call( L, 5, 0 );
}

void SetNumber( lua_State* L, int obj, const char* key, double v )
{
	lua_pushnumber( L, v );
	lua_setfield( L, obj, key );
}

// Called through lua_pcall; upvalue-free, reads sList / sState. Stack: 1 = group, 2 = display
int RenderList( lua_State* L )
{
	PluginState* st = sState;
	DrawList* d = sList;
	float s = st->scale;
	bool debug = st->drawMode == 2;
	float fillAlpha = debug ? 0.9f : 0.35f;
	int group = 1, display = 2;

	// clear previous frame
	int n = (int)lua_objlen( L, group );
	lua_getfield( L, group, "numChildren" );
	if ( lua_isnumber( L, -1 ) )
	{
		n = (int)lua_tointeger( L, -1 );
	}
	lua_pop( L, 1 );
	for ( int i = n; i >= 1; --i )
	{
		lua_rawgeti( L, group, i ); // Solar2D groups also answer numeric __index; rawget first
		if ( lua_isnil( L, -1 ) )
		{
			lua_pop( L, 1 );
			lua_pushinteger( L, i );
			lua_gettable( L, group );
		}
		if ( lua_istable( L, -1 ) )
		{
			lua_getfield( L, -1, "removeSelf" );
			lua_pushvalue( L, -2 );
			lua_call( L, 1, 0 );
		}
		lua_pop( L, 1 );
	}

	if ( debug )
	{
		// opaque background hiding the normal display. The draw group lives in the physics world's group,
		// so centre the rect on the screen centre expressed in that group's coordinates.
		lua_getfield( L, display, "contentCenterX" );
		lua_getfield( L, display, "contentCenterY" );
		int cx = lua_gettop( L ) - 1;
		lua_getfield( L, group, "contentToLocal" );
		if ( lua_isfunction( L, -1 ) )
		{
			lua_pushvalue( L, group );
			lua_pushvalue( L, cx );
			lua_pushvalue( L, cx + 1 );
			lua_call( L, 3, 2 );
			lua_replace( L, cx + 1 );
			lua_replace( L, cx );
		}
		else
		{
			lua_pop( L, 1 );
		}
		lua_getfield( L, display, "newRect" );
		lua_pushvalue( L, group );
		lua_pushvalue( L, cx );
		lua_pushvalue( L, cx + 1 );
		lua_pushnumber( L, 1.0e6 );
		lua_pushnumber( L, 1.0e6 );
		lua_remove( L, cx + 1 );
		lua_remove( L, cx );
		lua_call( L, 5, 1 );
		int bg = lua_gettop( L );
		SetColor( L, bg, "setFillColor", (b2HexColor)0x101018, 1.0f );
		lua_pop( L, 1 );
	}

	for ( const DrawPolygon& p : d->polygons )
	{
		if ( p.points.size() < 3 )
		{
			continue;
		}
		float minX = p.points[0].x, maxX = minX, minY = p.points[0].y, maxY = minY;
		for ( const b2Vec2& v : p.points )
		{
			minX = v.x < minX ? v.x : minX;
			maxX = v.x > maxX ? v.x : maxX;
			minY = v.y < minY ? v.y : minY;
			maxY = v.y > maxY ? v.y : maxY;
		}
		lua_getfield( L, display, "newPolygon" );
		lua_pushvalue( L, group );
		lua_pushnumber( L, 0.5f * ( minX + maxX ) * s );
		lua_pushnumber( L, 0.5f * ( minY + maxY ) * s );
		lua_createtable( L, (int)p.points.size() * 2, 0 );
		int k = 0;
		for ( const b2Vec2& v : p.points )
		{
			lua_pushnumber( L, v.x * s );
			lua_rawseti( L, -2, ++k );
			lua_pushnumber( L, v.y * s );
			lua_rawseti( L, -2, ++k );
		}
		lua_call( L, 4, 1 );
		int o = lua_gettop( L );
		if ( lua_istable( L, o ) )
		{
			SetColor( L, o, "setFillColor", p.color, fillAlpha );
			SetColor( L, o, "setStrokeColor", p.color, 1.0f );
			SetNumber( L, o, "strokeWidth", 1 );
		}
		lua_pop( L, 1 );
	}

	for ( const DrawCircle& c : d->circles )
	{
		lua_getfield( L, display, "newCircle" );
		lua_pushvalue( L, group );
		lua_pushnumber( L, c.center.x * s );
		lua_pushnumber( L, c.center.y * s );
		lua_pushnumber( L, c.radius * s );
		lua_call( L, 4, 1 );
		int o = lua_gettop( L );
		if ( lua_istable( L, o ) )
		{
			SetColor( L, o, "setFillColor", c.color, c.solid ? fillAlpha : 0.0f );
			SetColor( L, o, "setStrokeColor", c.color, 1.0f );
			SetNumber( L, o, "strokeWidth", 1 );
		}
		lua_pop( L, 1 );
		if ( c.solid )
		{
			// radius line showing the rotation
			b2Vec2 e = b2MulAdd( c.center, c.radius, c.axis );
			lua_getfield( L, display, "newLine" );
			lua_pushvalue( L, group );
			lua_pushnumber( L, c.center.x * s );
			lua_pushnumber( L, c.center.y * s );
			lua_pushnumber( L, e.x * s );
			lua_pushnumber( L, e.y * s );
			lua_call( L, 5, 1 );
			int l = lua_gettop( L );
			if ( lua_istable( L, l ) )
			{
				SetColor( L, l, "setStrokeColor", c.color, 1.0f );
				SetNumber( L, l, "strokeWidth", 1 );
			}
			lua_pop( L, 1 );
		}
	}

	for ( const DrawLine& ln : d->lines )
	{
		if ( b2DistanceSquared( ln.a, ln.b ) < 1e-10f )
		{
			continue;
		}
		lua_getfield( L, display, "newLine" );
		lua_pushvalue( L, group );
		lua_pushnumber( L, ln.a.x * s );
		lua_pushnumber( L, ln.a.y * s );
		lua_pushnumber( L, ln.b.x * s );
		lua_pushnumber( L, ln.b.y * s );
		lua_call( L, 5, 1 );
		int l = lua_gettop( L );
		if ( lua_istable( L, l ) )
		{
			SetColor( L, l, "setStrokeColor", ln.color, 1.0f );
			SetNumber( L, l, "strokeWidth", 1 );
		}
		lua_pop( L, 1 );
	}

	// keep the debug group on top of its parent (the physics world's group)
	lua_getfield( L, group, "toFront" );
	if ( lua_isfunction( L, -1 ) )
	{
		lua_pushvalue( L, group );
		lua_call( L, 1, 0 );
	}
	else
	{
		lua_pop( L, 1 );
	}
	return 0;
}

void RemoveGroup( lua_State* L, PluginState* st )
{
	if ( st->drawGroupRef == LUA_NOREF )
	{
		return;
	}
	lua_rawgeti( L, LUA_REGISTRYINDEX, st->drawGroupRef );
	if ( lua_istable( L, -1 ) )
	{
		lua_getfield( L, -1, "removeSelf" );
		if ( lua_isfunction( L, -1 ) )
		{
			lua_pushvalue( L, -2 );
			lua_pcall( L, 1, 0, 0 ) == 0 ? (void)0 : lua_pop( L, 1 );
		}
		else
		{
			lua_pop( L, 1 );
		}
	}
	lua_pop( L, 1 );
	luaL_unref( L, LUA_REGISTRYINDEX, st->drawGroupRef );
	st->drawGroupRef = LUA_NOREF;
}

// Pushes the draw group, creating it on the stage if needed. Returns false on failure.
bool PushGroup( lua_State* L, PluginState* st )
{
	if ( st->drawGroupRef != LUA_NOREF )
	{
		lua_rawgeti( L, LUA_REGISTRYINDEX, st->drawGroupRef );
		if ( lua_istable( L, -1 ) && lua_getmetatable( L, -1 ) )
		{
			lua_pop( L, 1 );
			return true; // still a live display group
		}
		lua_pop( L, 1 );
		luaL_unref( L, LUA_REGISTRYINDEX, st->drawGroupRef );
		st->drawGroupRef = LUA_NOREF;
	}
	lua_getglobal( L, "display" );
	lua_getfield( L, -1, "newGroup" );
	lua_remove( L, -2 );
	if ( !lua_isfunction( L, -1 ) || lua_pcall( L, 0, 1, 0 ) != 0 || !lua_istable( L, -1 ) )
	{
		lua_pop( L, 1 );
		return false;
	}
	lua_pushvalue( L, -1 );
	st->drawGroupRef = luaL_ref( L, LUA_REGISTRYINDEX );
	return true;
}

static bool IsLiveObject( lua_State* L, int index )
{
	if ( !lua_istable( L, index ) || !lua_getmetatable( L, index ) )
	{
		return false;
	}
	lua_pop( L, 1 );
	return true;
}

// Pushes the group the shapes must be drawn in: physics.setDrawGroup(), else the parent of a body's
// display object (physics coordinates are the coordinates of that parent), else nil (stage).
static void PushDrawParent( lua_State* L, PluginState* st )
{
	if ( st->drawParentRef != LUA_NOREF )
	{
		lua_rawgeti( L, LUA_REGISTRYINDEX, st->drawParentRef );
		if ( IsLiveObject( L, -1 ) )
		{
			return;
		}
		lua_pop( L, 1 );
	}
	for ( Body* b : st->bodies )
	{
		if ( b->destroyed || b->pendingDestroy )
		{
			continue;
		}
		lua_rawgeti( L, LUA_REGISTRYINDEX, b->objRef );
		if ( IsLiveObject( L, -1 ) )
		{
			lua_getfield( L, -1, "parent" );
			lua_remove( L, -2 );
			if ( IsLiveObject( L, -1 ) )
			{
				return;
			}
		}
		lua_pop( L, 1 );
		break; // a removed object: try again next frame
	}
	lua_pushnil( L );
}

// Moves the draw group (at index) into the right parent so it follows the world group's transform
// (position, scale, rotation of a camera group). Cheap: a few field reads per frame.
static void AttachGroup( lua_State* L, PluginState* st, int group )
{
	PushDrawParent( L, st );
	int parent = lua_gettop( L );
	if ( lua_isnil( L, parent ) )
	{
		lua_pop( L, 1 );
		return;
	}
	lua_getfield( L, group, "parent" );
	bool same = lua_rawequal( L, -1, parent ) != 0;
	lua_pop( L, 1 );
	if ( !same )
	{
		lua_getfield( L, parent, "insert" );
		if ( lua_isfunction( L, -1 ) )
		{
			lua_pushvalue( L, parent );
			lua_pushvalue( L, group );
			if ( lua_pcall( L, 2, 0, 0 ) != 0 )
			{
				lua_pop( L, 1 );
			}
		}
		else
		{
			lua_pop( L, 1 );
		}
	}
	lua_pop( L, 1 );
}
} // namespace

void b2p_renderDebugDraw( lua_State* L, PluginState* st )
{
	if ( st->drawMode == 0 || B2_IS_NULL( st->world ) )
	{
		return;
	}
	int top = lua_gettop( L );

	DrawList list;
	b2DebugDraw dd = b2DefaultDebugDraw();
	dd.DrawPolygonFcn = DrawPolygonOutline;
	dd.DrawSolidPolygonFcn = DrawSolidPolygon;
	dd.DrawCircleFcn = DrawCircleOutline;
	dd.DrawSolidCircleFcn = DrawSolidCircle;
	dd.DrawSolidCapsuleFcn = DrawSolidCapsule;
	dd.DrawLineFcn = DrawSegment;
	dd.DrawTransformFcn = DrawTransformNoop;
	dd.DrawPointFcn = DrawPointNoop;
	dd.DrawStringFcn = DrawStringNoop;
	dd.DrawBoundsFcn = DrawBoundsNoop;
	dd.drawingBounds.lowerBound = b2Vec2{ -1.0e5f, -1.0e5f };
	dd.drawingBounds.upperBound = b2Vec2{ 1.0e5f, 1.0e5f };
	dd.drawShapes = true;
	dd.drawJoints = true;
	dd.drawJointExtras = false;
	dd.drawChainNormals = false;
	dd.drawContacts = st->drawContacts;
	dd.drawContactNormals = st->drawContacts;
	dd.context = &list;
	b2World_Draw( st->world, &dd );

	if ( !PushGroup( L, st ) )
	{
		lua_settop( L, top );
		return;
	}
	int group = lua_gettop( L );
	AttachGroup( L, st, group );
	lua_pushcfunction( L, RenderList );
	lua_pushvalue( L, group );
	lua_getglobal( L, "display" );
	sList = &list;
	sState = st;
	if ( lua_pcall( L, 2, 0, 0 ) != 0 )
	{
		b2p_warn( L, "debug draw failed (%s); switching back to \"normal\"", lua_tostring( L, -1 ) );
		st->drawMode = 0;
	}
	sList = nullptr;
	lua_settop( L, top );
	if ( st->drawMode == 0 )
	{
		RemoveGroup( L, st );
	}
}

void b2p_removeDebugDraw( lua_State* L, PluginState* st )
{
	RemoveGroup( L, st );
}

// physics.setDrawMode( "normal" | "hybrid" | "debug" )
static int setDrawMode( lua_State* L )
{
	PluginState* st = b2p_state( L );
	const char* mode = luaL_checkstring( L, 1 );
	if ( strcmp( mode, "normal" ) == 0 )
	{
		st->drawMode = 0;
		RemoveGroup( L, st );
	}
	else if ( strcmp( mode, "hybrid" ) == 0 )
	{
		st->drawMode = 1;
	}
	else if ( strcmp( mode, "debug" ) == 0 )
	{
		st->drawMode = 2;
	}
	else
	{
		return luaL_error( L, "physics.setDrawMode() parameter must be one of 'normal', 'hybrid' or 'debug'" );
	}
	if ( st->drawMode != 0 && lua_isboolean( L, 2 ) )
	{
		st->drawContacts = lua_toboolean( L, 2 ) != 0; // new: also draw contact points / normals
	}
	b2p_renderDebugDraw( L, st ); // show it right away, even when paused
	return 0;
}

// physics.setDrawGroup( [group] )   (new) group whose coordinates the physics world uses; the debug draw
// is drawn inside it so it follows its position / scale / rotation. nil: the parent of the bodies' objects.
static int setDrawGroup( lua_State* L )
{
	PluginState* st = b2p_state( L );
	if ( st->drawParentRef != LUA_NOREF )
	{
		luaL_unref( L, LUA_REGISTRYINDEX, st->drawParentRef );
		st->drawParentRef = LUA_NOREF;
	}
	if ( lua_istable( L, 1 ) )
	{
		lua_pushvalue( L, 1 );
		st->drawParentRef = luaL_ref( L, LUA_REGISTRYINDEX );
	}
	else if ( !lua_isnoneornil( L, 1 ) )
	{
		return luaL_argerror( L, 1, "display group or nil expected" );
	}
	b2p_renderDebugDraw( L, st );
	return 0;
}

// physics.getDrawMode() -> string   (new)
static int getDrawMode( lua_State* L )
{
	static const char* const names[] = { "normal", "hybrid", "debug" };
	lua_pushstring( L, names[b2p_state( L )->drawMode] );
	return 1;
}

void b2p_registerDraw( lua_State* L, int libIndex )
{
	lua_pushcfunction( L, setDrawMode );
	lua_setfield( L, libIndex, "setDrawMode" );
	lua_pushcfunction( L, getDrawMode );
	lua_setfield( L, libIndex, "getDrawMode" );
	lua_pushcfunction( L, setDrawGroup );
	lua_setfield( L, libIndex, "setDrawGroup" );
}

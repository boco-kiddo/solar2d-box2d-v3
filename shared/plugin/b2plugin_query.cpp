// plugin.box2dv3 — world queries: physics.rayCast, physics.reflectRay, physics.queryRegion and
// the new physics.queryPoint.
//
//   physics.rayCast( x1, y1, x2, y2, [behavior], [options] ) -> hits | nil
//       behavior: "closest" (default), "any", "unsorted", "sorted"
//       hit = { object, element, position = { x, y }, normal = { x, y }, fraction }
//   physics.reflectRay( x1, y1, hit ) -> dx, dy   (normalized reflected direction)
//   physics.queryRegion( x1, y1, x2, y2, [options] ) -> objects | nil
//   physics.queryPoint( x, y, [options] ) -> objects | nil
//   physics.circleCast( x1, y1, x2, y2, radius, [behavior], [options] ) -> hits | nil   (v3 shape cast)
//       hit also has center = { x, y }: where the circle's centre is when it touches
//   physics.queryCircle( x, y, radius, [options] ) -> objects | nil                     (v3)
//
//   options = { categoryBits = n, maskBits = n, ignoreSensors = bool }   (all optional)
//
// Differences from v2: queryRegion / queryPoint test the real shapes (v2 tested bounding boxes),
// each object is reported once (v2 repeated multi-element bodies), hits carry the element index.
#include "b2plugin.h"

#include <algorithm>
#include <math.h>
#include <string.h>
#include <vector>

struct QueryOptions
{
	b2QueryFilter filter;
	bool ignoreSensors;
};

static QueryOptions ReadOptions( lua_State* L, int index )
{
	QueryOptions o;
	o.filter.categoryBits = UINT64_MAX; // match every shape by default, like v2 (which had no filter)
	o.filter.maskBits = UINT64_MAX;
	o.ignoreSensors = false;
	if ( index > 0 && lua_istable( L, index ) )
	{
		lua_getfield( L, index, "categoryBits" );
		if ( lua_isnumber( L, -1 ) )
			o.filter.categoryBits = (uint64_t)lua_tonumber( L, -1 );
		lua_pop( L, 1 );
		lua_getfield( L, index, "maskBits" );
		if ( lua_isnumber( L, -1 ) )
			o.filter.maskBits = (uint64_t)lua_tonumber( L, -1 );
		lua_pop( L, 1 );
		o.ignoreSensors = b2p_getBoolField( L, index, "ignoreSensors", false );
	}
	return o;
}

// Body record of a shape that belongs to a live display object, or nullptr.
static Body* QueryBody( b2ShapeId shape )
{
	Body* b = (Body*)b2Body_GetUserData( b2Shape_GetBody( shape ) );
	if ( b == nullptr || b->destroyed || b->pendingDestroy )
	{
		return nullptr;
	}
	return b;
}

// ---------------------------------------------------------------------------
// rayCast

struct RayHit
{
	b2ShapeId shape;
	b2Vec2 point;
	b2Vec2 normal;
	float fraction;
};

enum RayMode
{
	kRayClosest,
	kRayAny,
	kRayAll,
};

struct RayContext
{
	int mode;
	bool ignoreSensors;
	std::vector<RayHit>* hits;
};

static float RayCallback( b2ShapeId shape, b2Pos point, b2Vec2 normal, float fraction, void* context )
{
	RayContext* rc = (RayContext*)context;
	if ( QueryBody( shape ) == nullptr || ( rc->ignoreSensors && b2Shape_IsSensor( shape ) ) )
	{
		return -1.0f; // skip, continue
	}
	RayHit h = { shape, b2Vec2{ (float)point.x, (float)point.y }, normal, fraction };
	if ( rc->mode == kRayClosest )
	{
		rc->hits->clear();
		rc->hits->push_back( h );
		return fraction; // clip: only closer hits are reported from now on
	}
	rc->hits->push_back( h );
	return rc->mode == kRayAny ? 0.0f : 1.0f;
}

static void PushHit( lua_State* L, const PluginState* st, const RayHit& h )
{
	Body* b = QueryBody( h.shape );
	lua_createtable( L, 0, 5 );
	lua_rawgeti( L, LUA_REGISTRYINDEX, b->objRef );
	lua_setfield( L, -2, "object" );
	lua_pushinteger( L, (lua_Integer)(intptr_t)b2Shape_GetUserData( h.shape ) );
	lua_setfield( L, -2, "element" );
	lua_createtable( L, 0, 2 );
	lua_pushnumber( L, b2p_toPixels( st, h.point.x ) );
	lua_setfield( L, -2, "x" );
	lua_pushnumber( L, b2p_toPixels( st, h.point.y ) );
	lua_setfield( L, -2, "y" );
	lua_setfield( L, -2, "position" );
	lua_createtable( L, 0, 2 );
	lua_pushnumber( L, h.normal.x );
	lua_setfield( L, -2, "x" );
	lua_pushnumber( L, h.normal.y );
	lua_setfield( L, -2, "y" );
	lua_setfield( L, -2, "normal" );
	lua_pushnumber( L, h.fraction );
	lua_setfield( L, -2, "fraction" );
}

// Shared by rayCast and circleCast: radius < 0 casts a ray.
static int CastCommon( lua_State* L, const char* fname, float radiusPx, int behaviorIndex )
{
	PluginState* st = b2p_requireWorld( L, fname );
	if ( st->inStep )
	{
		return luaL_error( L, "%s cannot be called while the world is stepping.", fname );
	}
	b2Vec2 p1 = { b2p_toMeters( st, (float)luaL_checknumber( L, 1 ) ), b2p_toMeters( st, (float)luaL_checknumber( L, 2 ) ) };
	b2Vec2 p2 = { b2p_toMeters( st, (float)luaL_checknumber( L, 3 ) ), b2p_toMeters( st, (float)luaL_checknumber( L, 4 ) ) };
	const char* behavior = lua_isstring( L, behaviorIndex ) ? lua_tostring( L, behaviorIndex ) : "closest";
	QueryOptions opt = ReadOptions( L, lua_istable( L, behaviorIndex ) ? behaviorIndex : behaviorIndex + 1 );

	int mode = kRayClosest;
	bool sorted = false;
	if ( strcmp( behavior, "any" ) == 0 )
		mode = kRayAny;
	else if ( strcmp( behavior, "unsorted" ) == 0 )
		mode = kRayAll;
	else if ( strcmp( behavior, "sorted" ) == 0 )
	{
		mode = kRayAll;
		sorted = true;
	}

	b2Vec2 translation = b2Sub( p2, p1 );
	if ( b2LengthSquared( translation ) <= 0.0f )
	{
		return 0;
	}

	std::vector<RayHit> hits;
	RayContext rc = { mode, opt.ignoreSensors, &hits };
	if ( radiusPx < 0.0f )
	{
		b2World_CastRay( st->world, b2Pos{ p1.x, p1.y }, translation, opt.filter, RayCallback, &rc );
	}
	else
	{
		b2Vec2 center = { 0.0f, 0.0f };
		b2ShapeProxy proxy = b2MakeProxy( &center, 1, b2p_toMeters( st, radiusPx ) );
		b2World_CastShape( st->world, b2Pos{ p1.x, p1.y }, &proxy, translation, opt.filter, RayCallback, &rc );
	}
	if ( sorted )
	{
		std::sort( hits.begin(), hits.end(), []( const RayHit& a, const RayHit& b ) { return a.fraction < b.fraction; } );
	}

	// Build the Lua result only after Box2D returned (no Lua errors inside the query).
	size_t n = hits.size();
	if ( n == 0 )
	{
		return 0;
	}
	std::vector<RayHit> local;
	local.swap( hits ); // keep the hits alive in a vector that is destroyed before any Lua error can unwind
	lua_createtable( L, (int)n, 0 );
	for ( size_t i = 0; i < n; ++i )
	{
		PushHit( L, st, local[i] );
		if ( radiusPx >= 0.0f )
		{
			b2Vec2 c = b2MulAdd( p1, local[i].fraction, translation );
			lua_createtable( L, 0, 2 );
			lua_pushnumber( L, b2p_toPixels( st, c.x ) );
			lua_setfield( L, -2, "x" );
			lua_pushnumber( L, b2p_toPixels( st, c.y ) );
			lua_setfield( L, -2, "y" );
			lua_setfield( L, -2, "center" );
		}
		lua_rawseti( L, -2, (int)i + 1 );
	}
	return 1;
}

static int rayCast( lua_State* L )
{
	return CastCommon( L, "physics.rayCast()", -1.0f, 5 );
}

static int circleCast( lua_State* L )
{
	float r = (float)luaL_checknumber( L, 5 );
	if ( r <= 0.0f )
	{
		return luaL_argerror( L, 5, "radius must be > 0" );
	}
	return CastCommon( L, "physics.circleCast()", r, 6 );
}

// physics.reflectRay( x1, y1, hit ) -> dx, dy
static int reflectRay( lua_State* L )
{
	float ax = (float)luaL_checknumber( L, 1 );
	float ay = (float)luaL_checknumber( L, 2 );
	luaL_checktype( L, 3, LUA_TTABLE );
	lua_getfield( L, 3, "position" );
	if ( !lua_istable( L, -1 ) )
	{
		return luaL_error( L, "physics.reflectRay(): argument #3 must be a hit returned by physics.rayCast()" );
	}
	float bx = b2p_getNumberField( L, -1, "x", 0.0f );
	float by = b2p_getNumberField( L, -1, "y", 0.0f );
	lua_pop( L, 1 );
	lua_getfield( L, 3, "normal" );
	if ( !lua_istable( L, -1 ) )
	{
		return luaL_error( L, "physics.reflectRay(): argument #3 must be a hit returned by physics.rayCast()" );
	}
	b2Vec2 n = { b2p_getNumberField( L, -1, "x", 0.0f ), b2p_getNumberField( L, -1, "y", 0.0f ) };
	lua_pop( L, 1 );

	b2Vec2 incoming = { bx - ax, by - ay };
	b2Vec2 r = b2Sub( incoming, b2MulSV( 2.0f * b2Dot( incoming, n ), n ) ); // I - 2 (I.N) N
	r = b2Normalize( r );
	lua_pushnumber( L, r.x );
	lua_pushnumber( L, r.y );
	return 2;
}

// ---------------------------------------------------------------------------
// queryRegion / queryPoint (exact shape overlap)

struct OverlapContext
{
	bool ignoreSensors;
	std::vector<Body*>* bodies;
};

static bool OverlapCallback( b2ShapeId shape, void* context )
{
	OverlapContext* oc = (OverlapContext*)context;
	if ( oc->ignoreSensors && b2Shape_IsSensor( shape ) )
	{
		return true;
	}
	Body* b = QueryBody( shape );
	if ( b != nullptr )
	{
		oc->bodies->push_back( b );
	}
	return true;
}

static int PushUniqueObjects( lua_State* L, std::vector<Body*>& bodies )
{
	std::sort( bodies.begin(), bodies.end() );
	bodies.erase( std::unique( bodies.begin(), bodies.end() ), bodies.end() );
	if ( bodies.empty() )
	{
		return 0;
	}
	std::vector<int> refs;
	refs.reserve( bodies.size() );
	for ( Body* b : bodies )
	{
		refs.push_back( b->objRef );
	}
	std::vector<Body*>().swap( bodies );
	int n = (int)refs.size();
	lua_createtable( L, n, 0 );
	for ( int i = 0; i < n; ++i )
	{
		lua_rawgeti( L, LUA_REGISTRYINDEX, refs[(size_t)i] );
		lua_rawseti( L, -2, i + 1 );
	}
	return 1;
}

static int RunOverlap( lua_State* L, PluginState* st, const b2Vec2* points, int count, const QueryOptions& opt,
					   float radius = 0.0f )
{
	int result;
	{
		std::vector<Body*> bodies;
		OverlapContext oc = { opt.ignoreSensors, &bodies };
		b2ShapeProxy proxy = b2MakeProxy( points, count, radius );
		b2World_OverlapShape( st->world, b2Pos{ 0.0f, 0.0f }, &proxy, opt.filter, OverlapCallback, &oc );
		result = PushUniqueObjects( L, bodies );
	}
	return result;
}

static int queryRegion( lua_State* L )
{
	PluginState* st = b2p_requireWorld( L, "physics.queryRegion()" );
	if ( st->inStep )
	{
		return luaL_error( L, "physics.queryRegion() cannot be called while the world is stepping." );
	}
	float x1 = b2p_toMeters( st, (float)luaL_checknumber( L, 1 ) );
	float y1 = b2p_toMeters( st, (float)luaL_checknumber( L, 2 ) );
	float x2 = b2p_toMeters( st, (float)luaL_checknumber( L, 3 ) );
	float y2 = b2p_toMeters( st, (float)luaL_checknumber( L, 4 ) );
	QueryOptions opt = ReadOptions( L, 5 );
	float lx = x1 < x2 ? x1 : x2, ux = x1 < x2 ? x2 : x1;
	float ly = y1 < y2 ? y1 : y2, uy = y1 < y2 ? y2 : y1;
	b2Vec2 box[4] = { { lx, ly }, { ux, ly }, { ux, uy }, { lx, uy } };
	int count = 4;
	if ( ux - lx < 1e-6f && uy - ly < 1e-6f )
	{
		count = 1; // degenerate region: point query
	}
	return RunOverlap( L, st, box, count, opt );
}

static int queryPoint( lua_State* L )
{
	PluginState* st = b2p_requireWorld( L, "physics.queryPoint()" );
	if ( st->inStep )
	{
		return luaL_error( L, "physics.queryPoint() cannot be called while the world is stepping." );
	}
	b2Vec2 p = { b2p_toMeters( st, (float)luaL_checknumber( L, 1 ) ), b2p_toMeters( st, (float)luaL_checknumber( L, 2 ) ) };
	QueryOptions opt = ReadOptions( L, 3 );
	return RunOverlap( L, st, &p, 1, opt );
}

static int queryCircle( lua_State* L )
{
	PluginState* st = b2p_requireWorld( L, "physics.queryCircle()" );
	if ( st->inStep )
	{
		return luaL_error( L, "physics.queryCircle() cannot be called while the world is stepping." );
	}
	b2Vec2 p = { b2p_toMeters( st, (float)luaL_checknumber( L, 1 ) ), b2p_toMeters( st, (float)luaL_checknumber( L, 2 ) ) };
	float r = (float)luaL_checknumber( L, 3 );
	QueryOptions opt = ReadOptions( L, 4 );
	return RunOverlap( L, st, &p, 1, opt, b2p_toMeters( st, r > 0.0f ? r : 0.0f ) );
}

void b2p_registerQueries( lua_State* L, int libIndex )
{
	const luaL_Reg fns[] = {
		{ "rayCast", rayCast },
		{ "reflectRay", reflectRay },
		{ "queryRegion", queryRegion },
		{ "queryPoint", queryPoint },
		{ "circleCast", circleCast },
		{ "queryCircle", queryCircle },
		{ nullptr, nullptr },
	};
	for ( const luaL_Reg* f = fns; f->name; ++f )
	{
		lua_pushcfunction( L, f->func );
		lua_setfield( L, libIndex, f->name );
	}
}

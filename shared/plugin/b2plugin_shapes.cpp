// plugin.box2dv3 — body elements: parsing addBody() parameters (v2 rules), creating Box2D shapes,
// concave outline decomposition and the display.* hooks that remember line / polygon vertices.
#include "b2plugin.h"

#include <map>
#include <math.h>
#include <stdint.h>
#include <string.h>
#include <utility>

static const float kMinPointDistance = 0.5f * B2_LINEAR_SLOP; // meters; closer points are merged

// ---------------------------------------------------------------------------
// geometry helpers

ObjectGeometry b2p_readGeometry( lua_State* L, int obj )
{
	ObjectGeometry g;
	g.width = b2p_getNumberField( L, obj, "width", 0.0f );
	g.height = b2p_getNumberField( L, obj, "height", 0.0f );
	g.anchorOffset = b2Vec2{ 0.0f, 0.0f };

	lua_getfield( L, obj, "numChildren" );
	bool isGroup = !lua_isnil( L, -1 );
	lua_pop( L, 1 );

	lua_pushliteral( L, "_b2line" );
	lua_rawget( L, obj );
	bool isLine = !lua_isnil( L, -1 );
	lua_pop( L, 1 );

	bool offset = !isLine && ( !isGroup || b2p_getBoolField( L, obj, "anchorChildren", false ) );
	if ( offset )
	{
		float ax = b2p_getNumberField( L, obj, "anchorX", 0.5f );
		float ay = b2p_getNumberField( L, obj, "anchorY", 0.5f );
		g.anchorOffset.x = ( 0.5f - ax ) * g.width;
		g.anchorOffset.y = ( 0.5f - ay ) * g.height;
	}
	return g;
}

static float Cross( b2Vec2 o, b2Vec2 a, b2Vec2 b )
{
	return ( a.x - o.x ) * ( b.y - o.y ) - ( a.y - o.y ) * ( b.x - o.x );
}

static float SignedArea( const std::vector<b2Vec2>& p )
{
	float a = 0.0f;
	size_t n = p.size();
	for ( size_t i = 0; i < n; ++i )
	{
		const b2Vec2& u = p[i];
		const b2Vec2& v = p[( i + 1 ) % n];
		a += u.x * v.y - v.x * u.y;
	}
	return 0.5f * a;
}

// Removes consecutive points closer than kMinPointDistance (and the closing duplicate for loops).
static void WeldPoints( std::vector<b2Vec2>& p, bool loop )
{
	std::vector<b2Vec2> out;
	out.reserve( p.size() );
	for ( const b2Vec2& v : p )
	{
		if ( out.empty() || b2Distance( out.back(), v ) > kMinPointDistance )
		{
			out.push_back( v );
		}
	}
	if ( loop )
	{
		while ( out.size() > 1 && b2Distance( out.front(), out.back() ) <= kMinPointDistance )
		{
			out.pop_back();
		}
	}
	p.swap( out );
}

// ---------------------------------------------------------------------------
// concave polygon decomposition: ear clipping + Hertel-Mehlhorn merge (convex, <= 8 vertices)

static bool PointInTriangle( b2Vec2 p, b2Vec2 a, b2Vec2 b, b2Vec2 c )
{
	const float eps = -1e-9f;
	return Cross( a, b, p ) >= eps && Cross( b, c, p ) >= eps && Cross( c, a, p ) >= eps;
}

static bool IsConvexRing( const std::vector<b2Vec2>& pts, const std::vector<int>& ring )
{
	size_t n = ring.size();
	for ( size_t i = 0; i < n; ++i )
	{
		b2Vec2 a = pts[ring[i]], b = pts[ring[( i + 1 ) % n]], c = pts[ring[( i + 2 ) % n]];
		if ( Cross( a, b, c ) < -1e-7f )
		{
			return false;
		}
	}
	return true;
}

static bool SegmentsCross( b2Vec2 a, b2Vec2 b, b2Vec2 c, b2Vec2 d )
{
	float d1 = Cross( c, d, a ), d2 = Cross( c, d, b );
	float d3 = Cross( a, b, c ), d4 = Cross( a, b, d );
	return ( ( d1 > 0.0f && d2 < 0.0f ) || ( d1 < 0.0f && d2 > 0.0f ) ) &&
		   ( ( d3 > 0.0f && d4 < 0.0f ) || ( d3 < 0.0f && d4 > 0.0f ) );
}

static bool IsSimplePolygon( const std::vector<b2Vec2>& p )
{
	size_t n = p.size();
	for ( size_t i = 0; i < n; ++i )
	{
		for ( size_t j = i + 2; j < n; ++j )
		{
			if ( i == 0 && j == n - 1 )
			{
				continue; // adjacent through the closing edge
			}
			if ( SegmentsCross( p[i], p[( i + 1 ) % n], p[j], p[( j + 1 ) % n] ) )
			{
				return false;
			}
		}
	}
	return true;
}

bool b2p_decomposePolygon( const std::vector<b2Vec2>& input, std::vector<std::vector<b2Vec2>>& out )
{
	std::vector<b2Vec2> pts = input;
	WeldPoints( pts, true );

	// drop collinear points
	for ( bool changed = true; changed && pts.size() > 3; )
	{
		changed = false;
		for ( size_t i = 0; i < pts.size() && pts.size() > 3; ++i )
		{
			size_t n = pts.size();
			b2Vec2 a = pts[( i + n - 1 ) % n], b = pts[i], c = pts[( i + 1 ) % n];
			if ( fabsf( Cross( a, b, c ) ) < 1e-9f )
			{
				pts.erase( pts.begin() + (long)i );
				changed = true;
			}
		}
	}
	if ( pts.size() < 3 || !IsSimplePolygon( pts ) )
	{
		return false;
	}
	if ( SignedArea( pts ) < 0.0f )
	{
		std::vector<b2Vec2> rev( pts.rbegin(), pts.rend() );
		pts.swap( rev );
	}

	// ear clipping
	std::vector<int> idx( pts.size() );
	for ( size_t i = 0; i < idx.size(); ++i )
	{
		idx[i] = (int)i;
	}
	std::vector<std::vector<int>> polys;
	size_t guard = 0;
	while ( idx.size() > 3 )
	{
		size_t n = idx.size();
		bool clipped = false;
		for ( size_t i = 0; i < n; ++i )
		{
			int ia = idx[( i + n - 1 ) % n], ib = idx[i], ic = idx[( i + 1 ) % n];
			b2Vec2 a = pts[ia], b = pts[ib], c = pts[ic];
			if ( Cross( a, b, c ) <= 1e-9f )
			{
				continue; // reflex
			}
			bool ear = true;
			for ( size_t k = 0; k < n && ear; ++k )
			{
				int ik = idx[k];
				if ( ik == ia || ik == ib || ik == ic )
				{
					continue;
				}
				if ( PointInTriangle( pts[ik], a, b, c ) )
				{
					ear = false;
				}
			}
			if ( ear )
			{
				polys.push_back( { ia, ib, ic } );
				idx.erase( idx.begin() + (long)i );
				clipped = true;
				break;
			}
		}
		if ( !clipped || ++guard > 100000 )
		{
			return false; // self intersecting
		}
	}
	polys.push_back( idx );

	// Hertel-Mehlhorn: merge neighbours while the result stays convex and small enough
	std::vector<bool> alive( polys.size(), true );
	std::map<std::pair<int, int>, int> owner; // directed edge -> polygon
	auto registerEdges = [&]( int p ) {
		const std::vector<int>& r = polys[p];
		for ( size_t i = 0; i < r.size(); ++i )
		{
			owner[std::make_pair( r[i], r[( i + 1 ) % r.size()] )] = p;
		}
	};
	for ( size_t p = 0; p < polys.size(); ++p )
	{
		registerEdges( (int)p );
	}

	for ( size_t p = 0; p < polys.size(); ++p )
	{
		bool merged = true;
		while ( alive[p] && merged )
		{
			merged = false;
			std::vector<int>& rp = polys[p];
			for ( size_t e = 0; e < rp.size() && !merged; ++e )
			{
				int u = rp[e], v = rp[( e + 1 ) % rp.size()];
				auto it = owner.find( std::make_pair( v, u ) );
				if ( it == owner.end() )
				{
					continue;
				}
				int q = it->second;
				if ( q == (int)p || !alive[q] )
				{
					continue;
				}
				const std::vector<int>& rq = polys[q];
				if ( rp.size() + rq.size() - 2 > (size_t)B2_MAX_POLYGON_VERTICES )
				{
					continue;
				}
				// p rotated to start at v and end at u; q rotated to start at u and end at v
				std::vector<int> ring;
				size_t np = rp.size();
				for ( size_t k = 0; k < np; ++k )
				{
					ring.push_back( rp[( e + 1 + k ) % np] ); // v ... u
				}
				size_t nq = rq.size(), start = 0;
				for ( size_t k = 0; k < nq; ++k )
				{
					if ( rq[k] == u )
					{
						start = k;
					}
				}
				for ( size_t k = 1; k + 1 < nq; ++k )
				{
					ring.push_back( rq[( start + k ) % nq] ); // interior of u ... v
				}
				if ( !IsConvexRing( pts, ring ) )
				{
					continue;
				}
				alive[q] = false;
				polys[p] = ring;
				registerEdges( (int)p );
				merged = true;
			}
		}
	}

	for ( size_t p = 0; p < polys.size(); ++p )
	{
		if ( !alive[p] )
		{
			continue;
		}
		std::vector<b2Vec2> piece;
		for ( int i : polys[p] )
		{
			piece.push_back( pts[i] );
		}
		out.push_back( piece );
	}
	return !out.empty();
}

// ---------------------------------------------------------------------------
// element parsing (v2 rules, see Rtt_LuaLibPhysics.cpp)

// Material defaults: density 1, friction 0.3, bounce 0.2 (the values Solar2D documents; its code used
// other, inconsistent defaults). Missing fields keep these defaults whether or not a table is given.
static const float kDefaultDensity = 1.0f;
static const float kDefaultFriction = 0.3f;
static const float kDefaultBounce = 0.2f;

static void ParseMaterial( lua_State* L, const PluginState* st, int t, ElementRecord* e )
{
	e->density = kDefaultDensity;
	e->friction = kDefaultFriction;
	e->bounce = kDefaultBounce;
	e->rollingResistance = 0.0f;
	e->tangentSpeed = 0.0f;
	e->capsule = b2Capsule{};
	e->isSensor = false;
	e->filter = b2DefaultFilter(); // category 1, collides with everything (64 bit masks)
	e->loop = false;
	e->chainA = b2_nullChainId;
	e->chainB = b2_nullChainId;
	e->circle = b2Circle{};
	e->polygon = b2Polygon{};

	if ( t == 0 )
	{
		return;
	}

	float density = b2p_getNumberField( L, t, "density", kDefaultDensity );
	e->density = density >= 0.0f ? density : kDefaultDensity;
	float friction = b2p_getNumberField( L, t, "friction", kDefaultFriction );
	e->friction = friction >= 0.0f ? friction : kDefaultFriction;
	float bounce = b2p_getNumberField( L, t, "bounce", kDefaultBounce );
	e->bounce = bounce >= 0.0f ? bounce : kDefaultBounce;
	e->isSensor = b2p_getBoolField( L, t, "isSensor", false );
	float rolling = b2p_getNumberField( L, t, "rollingResistance", 0.0f );
	e->rollingResistance = rolling > 0.0f ? rolling : 0.0f;
	e->tangentSpeed = b2p_toMeters( st, b2p_getNumberField( L, t, "tangentSpeed", 0.0f ) );

	lua_getfield( L, t, "filter" );
	if ( lua_istable( L, -1 ) )
	{
		int f = lua_gettop( L );
		lua_getfield( L, f, "categoryBits" );
		if ( lua_isnumber( L, -1 ) )
		{
			e->filter.categoryBits = (uint64_t)lua_tonumber( L, -1 );
		}
		lua_pop( L, 1 );
		lua_getfield( L, f, "maskBits" );
		if ( lua_isnumber( L, -1 ) )
		{
			e->filter.maskBits = (uint64_t)lua_tonumber( L, -1 );
		}
		lua_pop( L, 1 );
		lua_getfield( L, f, "groupIndex" );
		if ( lua_isnumber( L, -1 ) )
		{
			e->filter.groupIndex = (int)lua_tointeger( L, -1 );
		}
		lua_pop( L, 1 );
	}
	lua_pop( L, 1 );
}

static void MakeBox( const PluginState* st, ElementRecord* e, float halfW, float halfH, b2Vec2 centerPx, float angleDeg,
					 float cornerRadiusPx = 0.0f )
{
	float hw = b2p_toMeters( st, fabsf( halfW ) );
	float hh = b2p_toMeters( st, fabsf( halfH ) );
	float r = b2p_toMeters( st, cornerRadiusPx );
	if ( r > 0.0f )
	{
		// shrink the core so the rounded box keeps the requested outer size
		float maxR = ( hw < hh ? hw : hh ) - B2_LINEAR_SLOP;
		r = r < maxR ? r : ( maxR > 0.0f ? maxR : 0.0f );
		hw -= r;
		hh -= r;
	}
	else
	{
		r = 0.0f;
	}
	const float minExtent = B2_LINEAR_SLOP; // Box2D rejects degenerate boxes
	if ( hw < minExtent )
	{
		hw = minExtent;
	}
	if ( hh < minExtent )
	{
		hh = minExtent;
	}
	b2Vec2 c = { b2p_toMeters( st, centerPx.x ), b2p_toMeters( st, centerPx.y ) };
	e->kind = kElementPolygon;
	e->polygon = b2MakeOffsetRoundedBox( hw, hh, c, b2p_makeRot( angleDeg * kDegToRad ), r );
}

static void MakeCircle( const PluginState* st, ElementRecord* e, float radiusPx, b2Vec2 centerPx )
{
	float r = b2p_toMeters( st, radiusPx );
	if ( r <= 0.0f )
	{
		r = 1.0f / 16.0f; // v2 fallback
	}
	e->kind = kElementCircle;
	e->circle.center = b2Vec2{ b2p_toMeters( st, centerPx.x ), b2p_toMeters( st, centerPx.y ) };
	e->circle.radius = r;
}

// Reads a flat { x1, y1, x2, y2, ... } table at index t into meters: (v + offsetPx) * scale / ppm.
static void ReadFlatPoints( lua_State* L, const PluginState* st, int t, b2Vec2 offsetPx, b2Vec2 scale,
							std::vector<b2Vec2>& out )
{
	int n = (int)lua_objlen( L, t ) / 2;
	out.reserve( (size_t)n );
	for ( int i = 0; i < n; ++i )
	{
		lua_rawgeti( L, t, 2 * i + 1 );
		lua_rawgeti( L, t, 2 * i + 2 );
		float x = ( (float)lua_tonumber( L, -2 ) + offsetPx.x ) * scale.x;
		float y = ( (float)lua_tonumber( L, -1 ) + offsetPx.y ) * scale.y;
		lua_pop( L, 2 );
		out.push_back( b2Vec2{ b2p_toMeters( st, x ), b2p_toMeters( st, y ) } );
	}
}

// Rounded convex polygon whose outer boundary stays on the given hull: the hull is moved inwards by
// the radius, then inflated back by Box2D. The radius is reduced if the polygon is too thin.
static b2Polygon MakeRoundedPolygon( const b2Hull& hull, float radius, bool* reduced )
{
	*reduced = false;
	for ( int attempt = 0; attempt < 12 && radius > 0.0f; ++attempt )
	{
		int n = hull.count;
		b2Vec2 inset[B2_MAX_POLYGON_VERTICES];
		bool ok = true;
		for ( int i = 0; i < n && ok; ++i )
		{
			// vertex i lies on edges (i-1, i) and (i, i+1); hull is counter-clockwise
			b2Vec2 p0 = hull.points[( i + n - 1 ) % n], p1 = hull.points[i], p2 = hull.points[( i + 1 ) % n];
			b2Vec2 n1 = b2Normalize( b2Vec2{ p1.y - p0.y, p0.x - p1.x } );
			b2Vec2 n2 = b2Normalize( b2Vec2{ p2.y - p1.y, p1.x - p2.x } );
			float d1 = b2Dot( n1, p1 ) - radius, d2 = b2Dot( n2, p1 ) - radius;
			float det = n1.x * n2.y - n1.y * n2.x;
			if ( fabsf( det ) < 1e-6f )
			{
				ok = false;
				break;
			}
			inset[i] = b2Vec2{ ( d1 * n2.y - d2 * n1.y ) / det, ( n1.x * d2 - n2.x * d1 ) / det };
		}
		if ( ok )
		{
			b2Hull h = b2ComputeHull( inset, n );
			// the inset must keep every vertex and the same orientation (no edge collapsed)
			bool same = h.count == n;
			for ( int i = 0; same && i < n; ++i )
			{
				b2Vec2 e0 = b2Sub( hull.points[( i + 1 ) % n], hull.points[i] );
				b2Vec2 e1 = b2Sub( inset[( i + 1 ) % n], inset[i] );
				same = b2Dot( e0, e1 ) > 0.0f;
			}
			if ( same )
			{
				return b2MakePolygon( &h, radius );
			}
		}
		radius *= 0.5f;
		*reduced = true;
	}
	return b2MakePolygon( &hull, 0.0f );
}

static int NextIndex( const std::vector<ElementRecord>& out )
{
	return out.empty() ? 1 : out.back().index + 1;
}

void b2p_parseElement( lua_State* L, const PluginState* st, int obj, int t, b2BodyType type, const ObjectGeometry& g,
					   std::vector<ElementRecord>& out, bool* forceStatic )
{
	ElementRecord e;
	ParseMaterial( L, st, t, &e );
	// v3: rounded corners for "box", "shape" and the default rectangle. The outer size is kept.
	float cornerRadius = t != 0 ? b2p_getNumberField( L, t, "cornerRadius", 0.0f ) : 0.0f;
	e.index = NextIndex( out );

	if ( t != 0 )
	{
		// The order of the checks follows v2: circle, shape, box, chain, radius, outline,
		// static polygon object, line object, rectangle.

		lua_getfield( L, t, "circle" );
		if ( lua_istable( L, -1 ) )
		{
			int c = lua_gettop( L );
			float radius = b2p_getNumberField( L, c, "radius", 0.0f );
			b2Vec2 center = { b2p_getNumberField( L, c, "x", 0.0f ), b2p_getNumberField( L, c, "y", 0.0f ) };
			lua_pop( L, 1 );
			MakeCircle( st, &e, radius, center );
			out.push_back( e );
			return;
		}
		lua_pop( L, 1 );

		// v3: capsule = { x1, y1, x2, y2, radius } in pixels, relative to the object (like "circle").
		// Missing fields: the capsule fills the object along its longer side.
		lua_getfield( L, t, "capsule" );
		if ( lua_istable( L, -1 ) || lua_isboolean( L, -1 ) )
		{
			int c = lua_istable( L, -1 ) ? lua_gettop( L ) : 0;
			bool horizontal = g.width >= g.height;
			float r = 0.5f * ( horizontal ? g.height : g.width );
			float half = 0.5f * ( horizontal ? g.width : g.height ) - r;
			b2Vec2 a = g.anchorOffset, b = g.anchorOffset;
			if ( horizontal ) { a.x -= half; b.x += half; } else { a.y -= half; b.y += half; }
			if ( c != 0 )
			{
				r = b2p_getNumberField( L, c, "radius", r );
				a.x = b2p_getNumberField( L, c, "x1", a.x );
				a.y = b2p_getNumberField( L, c, "y1", a.y );
				b.x = b2p_getNumberField( L, c, "x2", b.x );
				b.y = b2p_getNumberField( L, c, "y2", b.y );
			}
			lua_pop( L, 1 );
			if ( r <= 0.0f )
			{
				luaL_error( L, "physics.addBody() \"capsule\" requires a radius > 0." );
			}
			float rm = b2p_toMeters( st, r );
			b2Vec2 am = { b2p_toMeters( st, a.x ), b2p_toMeters( st, a.y ) };
			b2Vec2 bm = { b2p_toMeters( st, b.x ), b2p_toMeters( st, b.y ) };
			if ( b2Distance( am, bm ) <= B2_LINEAR_SLOP )
			{
				MakeCircle( st, &e, r, b2Vec2{ 0.5f * ( a.x + b.x ), 0.5f * ( a.y + b.y ) } ); // degenerate capsule
			}
			else
			{
				e.kind = kElementCapsule;
				e.capsule.center1 = am;
				e.capsule.center2 = bm;
				e.capsule.radius = rm;
			}
			out.push_back( e );
			return;
		}
		lua_pop( L, 1 );

		lua_getfield( L, t, "shape" );
		if ( lua_istable( L, -1 ) )
		{
			int s = lua_gettop( L );
			int n = (int)lua_objlen( L, s ) / 2;
			if ( n < 3 )
			{
				luaL_error( L, "physics.addBody() with a \"shape\" requires at least 3 vertices." );
			}
			if ( n > B2_MAX_POLYGON_VERTICES )
			{
				luaL_error( L, "physics.addBody() \"shape\" has %d vertices; the maximum is %d. "
							   "Split it into several elements or use \"outline\".",
							n, B2_MAX_POLYGON_VERTICES );
			}
			b2Vec2 points[B2_MAX_POLYGON_VERTICES];
			for ( int i = 0; i < n; ++i )
			{
				lua_rawgeti( L, s, 2 * i + 1 );
				lua_rawgeti( L, s, 2 * i + 2 );
				float x = (float)lua_tonumber( L, -2 ) + g.anchorOffset.x;
				float y = (float)lua_tonumber( L, -1 ) + g.anchorOffset.y;
				lua_pop( L, 2 );
				points[i] = b2Vec2{ b2p_toMeters( st, x ), b2p_toMeters( st, y ) };
			}
			lua_pop( L, 1 );

			b2Hull hull = b2ComputeHull( points, n );
			if ( hull.count == 0 )
			{
				// No area (or nearly none, e.g. a sliver triangle of a triangulated ground): leave this
				// element out instead of failing the whole addBody. It keeps its index so selfElement /
				// otherElement still match the order of the elements passed to addBody.
				b2p_warn( L, "addBody: element %d is a \"shape\" with no area (or nearly none); it was skipped.", e.index );
				e.kind = kElementSkipped;
				out.push_back( e );
				return;
			}
			if ( hull.count < n )
			{
				b2p_warn( L, "addBody: \"shape\" was not convex (or had collinear points); its convex hull was used." );
			}
			e.kind = kElementPolygon;
			if ( cornerRadius > 0.0f )
			{
				bool reduced;
				e.polygon = MakeRoundedPolygon( hull, b2p_toMeters( st, cornerRadius ), &reduced );
				if ( reduced )
				{
					b2p_warn( L, "addBody: \"cornerRadius\" was too large for the \"shape\" and has been reduced." );
				}
			}
			else
			{
				e.polygon = b2MakePolygon( &hull, 0.0f );
			}
			out.push_back( e );
			return;
		}
		lua_pop( L, 1 );

		lua_getfield( L, t, "box" );
		if ( lua_istable( L, -1 ) )
		{
			int b = lua_gettop( L );
			float hw = b2p_getNumberField( L, b, "halfWidth", 0.0f );
			float hh = b2p_getNumberField( L, b, "halfHeight", 0.0f );
			lua_getfield( L, b, "x" );
			lua_getfield( L, b, "y" );
			bool hasCenter = lua_type( L, -2 ) == LUA_TNUMBER && lua_type( L, -1 ) == LUA_TNUMBER;
			b2Vec2 center = hasCenter ? b2Vec2{ (float)lua_tonumber( L, -2 ), (float)lua_tonumber( L, -1 ) } : g.anchorOffset;
			lua_pop( L, 2 );
			float angle = b2p_getNumberField( L, b, "angle", 0.0f );
			lua_pop( L, 1 );
			MakeBox( st, &e, hw, hh, center, angle, cornerRadius );
			out.push_back( e );
			return;
		}
		lua_pop( L, 1 );

		lua_getfield( L, t, "chain" );
		if ( lua_istable( L, -1 ) )
		{
			int c = lua_gettop( L );
			e.loop = b2p_getBoolField( L, t, "connectFirstAndLastChainVertex", false );
			e.kind = kElementChain;
			ReadFlatPoints( L, st, c, g.anchorOffset, b2Vec2{ 1.0f, 1.0f }, e.points );
			lua_pop( L, 1 );
			WeldPoints( e.points, e.loop );
			if ( e.points.size() < ( e.loop ? 3u : 2u ) )
			{
				std::vector<b2Vec2>().swap( e.points ); // free before the error unwinds
				luaL_error( L, "physics.addBody() with a \"chain\" requires at least %d distinct vertices.", e.loop ? 3 : 2 );
			}
			out.push_back( e );
			return;
		}
		lua_pop( L, 1 );

		lua_getfield( L, t, "radius" );
		if ( lua_isnumber( L, -1 ) )
		{
			float radius = (float)lua_tonumber( L, -1 );
			lua_pop( L, 1 );
			MakeCircle( st, &e, radius, g.anchorOffset );
			out.push_back( e );
			return;
		}
		lua_pop( L, 1 );

		lua_getfield( L, t, "outline" );
		if ( lua_istable( L, -1 ) )
		{
			int o = lua_gettop( L );
			// graphics.newOutline() returns texel coordinates of the image (origin top left). v2 centres
			// them on the image and scales by display size / texture size. The texture size is recorded
			// by our graphics.newOutline hook; otherwise the object's size is used.
			float texW = b2p_getNumberField( L, o, "_b2texW", g.width );
			float texH = b2p_getNumberField( L, o, "_b2texH", g.height );
			if ( texW <= 0.0f ) texW = 1.0f;
			if ( texH <= 0.0f ) texH = 1.0f;
			b2Vec2 offset = { -0.5f * texW + g.anchorOffset.x, -0.5f * texH + g.anchorOffset.y };
			b2Vec2 scale = { g.width > 0.0f ? g.width / texW : 1.0f, g.height > 0.0f ? g.height / texH : 1.0f };

			std::vector<std::vector<b2Vec2>>* pieces = new std::vector<std::vector<b2Vec2>>();
			bool ok;
			{
				std::vector<b2Vec2> pts;
				ReadFlatPoints( L, st, o, offset, scale, pts );
				ok = pts.size() >= 3 && b2p_decomposePolygon( pts, *pieces );
			}
			lua_pop( L, 1 );

			int added = 0;
			if ( ok )
			{
				for ( const std::vector<b2Vec2>& piece : *pieces )
				{
					b2Hull hull = b2ComputeHull( piece.data(), (int)piece.size() );
					if ( hull.count == 0 )
					{
						continue; // sliver
					}
					ElementRecord pe = e;
					pe.index = NextIndex( out ); // v2: every convex piece is its own element
					pe.kind = kElementPolygon;
					pe.polygon = b2MakePolygon( &hull, 0.0f );
					out.push_back( pe );
					++added;
				}
			}
			delete pieces;
			if ( !ok || added == 0 )
			{
				luaL_error( L, "physics.addBody() : invalid \"outline\"" );
			}
			return;
		}
		lua_pop( L, 1 );
	}

	// static display.newPolygon: loop chain along its contour (v2 ArbitraryPolygonalShape)
	if ( type == b2_staticBody )
	{
		lua_pushliteral( L, "_b2polygon" );
		lua_rawget( L, obj );
		if ( lua_istable( L, -1 ) )
		{
			e.kind = kElementChain;
			e.loop = true;
			ReadFlatPoints( L, st, lua_gettop( L ), g.anchorOffset, b2Vec2{ 1.0f, 1.0f }, e.points );
			lua_pop( L, 1 );
			WeldPoints( e.points, true );
			if ( e.points.size() >= 3 )
			{
				out.push_back( e );
				return;
			}
		}
		else
		{
			lua_pop( L, 1 );
		}
	}

	// display.newLine: open chain, always static (v2 StaticLine)
	lua_pushliteral( L, "_b2line" );
	lua_rawget( L, obj );
	if ( lua_istable( L, -1 ) )
	{
		e.kind = kElementChain;
		e.loop = false;
		ReadFlatPoints( L, st, lua_gettop( L ), g.anchorOffset, b2Vec2{ 1.0f, 1.0f }, e.points );
		lua_pop( L, 1 );
		WeldPoints( e.points, false );
		if ( e.points.size() < 2 )
		{
			std::vector<b2Vec2>().swap( e.points );
			luaL_error( L, "physics.addBody() with a LineObject requires at least 2 vertices." );
		}
		*forceStatic = true;
		out.push_back( e );
		return;
	}
	lua_pop( L, 1 );

	// default: rectangle matching the display object
	MakeBox( st, &e, g.width * 0.5f, g.height * 0.5f, g.anchorOffset, 0.0f, cornerRadius );
	out.push_back( e );
}

// ---------------------------------------------------------------------------
// shape creation

static b2ShapeDef MakeShapeDef( const ElementRecord& rec )
{
	b2ShapeDef sd = b2DefaultShapeDef();
	sd.density = rec.density;
	sd.material.friction = rec.friction;
	sd.material.restitution = rec.bounce;
	sd.material.rollingResistance = rec.rollingResistance;
	sd.material.tangentSpeed = rec.tangentSpeed;
	sd.isSensor = rec.isSensor;
	sd.enableHitEvents = !rec.isSensor; // "hit" events are only dispatched when someone listens
	sd.filter = rec.filter;
	sd.enableSensorEvents = true;
	sd.enableContactEvents = true;
	sd.enablePreSolveEvents = true; // filtered in the callback (see b2p_applyPreSolveFlag)
	sd.updateBodyMass = false;
	sd.userData = (void*)(intptr_t)rec.index;
	return sd;
}

static b2ChainId CreateChain( b2BodyId bodyId, const ElementRecord& rec, const std::vector<b2Vec2>& pts )
{
	b2SurfaceMaterial material = b2DefaultSurfaceMaterial();
	material.friction = rec.friction;
	material.restitution = rec.bounce;
	material.rollingResistance = rec.rollingResistance;
	material.tangentSpeed = rec.tangentSpeed;

	b2ChainDef cd = b2DefaultChainDef();
	cd.points = pts.data();
	cd.pointCount = (int)pts.size();
	cd.isLoop = rec.loop;
	cd.materials = &material;
	cd.materialCount = 1;
	cd.filter = rec.filter;
	cd.enableSensorEvents = true;
	cd.userData = (void*)(intptr_t)rec.index;
	if ( !rec.loop )
	{
		size_t n = pts.size();
		cd.ghost1 = b2Sub( b2MulSV( 2.0f, pts[0] ), pts[1] );
		cd.ghost2 = b2Sub( b2MulSV( 2.0f, pts[n - 1] ), pts[n - 2] );
	}
	b2ChainId id = b2CreateChain( bodyId, &cd );

	// Box2D disables contact events on chain segments; v2 reported collisions with chains.
	int count = b2Chain_GetSegmentCount( id );
	if ( count > 0 )
	{
		std::vector<b2ShapeId> segments( (size_t)count );
		count = b2Chain_GetSegments( id, segments.data(), count );
		for ( int i = 0; i < count; ++i )
		{
			b2Shape_EnableContactEvents( segments[(size_t)i], true );
			b2Shape_EnableHitEvents( segments[(size_t)i], true );
			b2Shape_EnablePreSolveEvents( segments[(size_t)i], true );
		}
	}
	return id;
}

void b2p_createElementShapes( b2BodyId bodyId, ElementRecord& rec )
{
	rec.chainA = b2_nullChainId;
	rec.chainB = b2_nullChainId;

	if ( rec.kind == kElementSkipped )
	{
		return;
	}
	if ( rec.kind == kElementCircle )
	{
		b2ShapeDef sd = MakeShapeDef( rec );
		b2CreateCircleShape( bodyId, &sd, &rec.circle );
		return;
	}
	if ( rec.kind == kElementPolygon )
	{
		b2ShapeDef sd = MakeShapeDef( rec );
		b2CreatePolygonShape( bodyId, &sd, &rec.polygon );
		return;
	}
	if ( rec.kind == kElementCapsule )
	{
		b2ShapeDef sd = MakeShapeDef( rec );
		b2CreateCapsuleShape( bodyId, &sd, &rec.capsule );
		return;
	}

	// chain
	if ( rec.isSensor )
	{
		// Box2D chains cannot be sensors: use plain two-sided segments instead.
		b2ShapeDef sd = MakeShapeDef( rec );
		size_t n = rec.points.size();
		size_t segs = rec.loop ? n : n - 1;
		for ( size_t i = 0; i < segs; ++i )
		{
			b2Segment s = { rec.points[i], rec.points[( i + 1 ) % n] };
			b2CreateSegmentShape( bodyId, &sd, &s );
		}
		return;
	}

	// Box2D v3 chains are one-sided; v2 chains collide on both sides. Build the chain with both
	// windings so either side is solid while keeping the smooth (no ghost bump) collision.
	rec.chainA = CreateChain( bodyId, rec, rec.points );
	std::vector<b2Vec2> reversed( rec.points.rbegin(), rec.points.rend() );
	rec.chainB = CreateChain( bodyId, rec, reversed );
}

void b2p_destroyElementShapes( b2BodyId bodyId, std::vector<ElementRecord>& elements )
{
	for ( ElementRecord& rec : elements )
	{
		if ( B2_IS_NON_NULL( rec.chainA ) && b2Chain_IsValid( rec.chainA ) )
		{
			b2DestroyChain( rec.chainA );
		}
		if ( B2_IS_NON_NULL( rec.chainB ) && b2Chain_IsValid( rec.chainB ) )
		{
			b2DestroyChain( rec.chainB );
		}
		rec.chainA = b2_nullChainId;
		rec.chainB = b2_nullChainId;
	}
	int count = b2Body_GetShapeCount( bodyId );
	if ( count > 0 )
	{
		std::vector<b2ShapeId> shapes( (size_t)count );
		count = b2Body_GetShapes( bodyId, shapes.data(), count );
		for ( int i = 0; i < count; ++i )
		{
			b2DestroyShape( shapes[(size_t)i], false );
		}
	}
}

void b2p_applyElementMaterials( b2BodyId bodyId, const std::vector<ElementRecord>& elements )
{
	int count = b2Body_GetShapeCount( bodyId );
	if ( count <= 0 )
	{
		return;
	}
	std::vector<b2ShapeId> shapes( (size_t)count );
	count = b2Body_GetShapes( bodyId, shapes.data(), count );
	for ( int i = 0; i < count; ++i )
	{
		int index = (int)(intptr_t)b2Shape_GetUserData( shapes[(size_t)i] );
		for ( const ElementRecord& rec : elements )
		{
			if ( rec.index == index )
			{
				b2SurfaceMaterial m = b2Shape_GetSurfaceMaterial( shapes[(size_t)i] );
				m.friction = rec.friction;
				m.restitution = rec.bounce;
				m.rollingResistance = rec.rollingResistance;
				m.tangentSpeed = rec.tangentSpeed;
				b2Shape_SetSurfaceMaterial( shapes[(size_t)i], &m );
				break;
			}
		}
	}
}

// ---------------------------------------------------------------------------
// display hooks: Solar2D does not expose the vertices of line and polygon objects, so we record
// them when the objects are created (v2 read them natively for addBody).

static void PushFlat( lua_State* L, const std::vector<float>& v )
{
	lua_createtable( L, (int)v.size(), 2 );
	for ( size_t i = 0; i < v.size(); ++i )
	{
		lua_pushnumber( L, v[i] );
		lua_rawseti( L, -2, (int)i + 1 );
	}
}

// line:append( x, y, ... ) wrapper. upvalue 1: nothing; original append fetched from the metatable.
static int LineAppend( lua_State* L )
{
	int n = lua_gettop( L );
	if ( !lua_istable( L, 1 ) || !lua_getmetatable( L, 1 ) )
	{
		return luaL_error( L, "append: invalid line object" );
	}
	lua_getfield( L, -1, "__index" );
	lua_remove( L, -2 );
	lua_pushvalue( L, 1 );
	lua_pushliteral( L, "append" );
	lua_call( L, 2, 1 ); // original append (rawget field is ours, __index gives Solar2D's)
	luaL_checkstack( L, n + 8, "too many arguments" ); // line:append( unpack( points ) )
	for ( int i = 1; i <= n; ++i )
	{
		lua_pushvalue( L, i );
	}
	lua_call( L, n, 0 );

	lua_pushliteral( L, "_b2line" );
	lua_rawget( L, 1 );
	if ( lua_istable( L, -1 ) )
	{
		int t = lua_gettop( L );
		float x0 = b2p_getNumberField( L, t, "x0", 0.0f );
		float y0 = b2p_getNumberField( L, t, "y0", 0.0f );
		int len = (int)lua_objlen( L, t );
		for ( int i = 2; i + 1 <= n; i += 2 )
		{
			lua_pushnumber( L, lua_tonumber( L, i ) - x0 );
			lua_rawseti( L, t, ++len );
			lua_pushnumber( L, lua_tonumber( L, i + 1 ) - y0 );
			lua_rawseti( L, t, ++len );
		}
	}
	lua_pop( L, 1 );
	return 0;
}

// display.newLine( [parent,] x1, y1, x2, y2, ... ) wrapper; upvalue 1 = original
static int NewLineHook( lua_State* L )
{
	int n = lua_gettop( L );
	// the arguments are pushed again for the original function: display.newLine( parent, unpack( points ) )
	// easily passes more than the LUA_MINSTACK (20) free slots a C function is guaranteed
	luaL_checkstack( L, n + 8, "too many arguments" );
	lua_pushvalue( L, lua_upvalueindex( 1 ) );
	for ( int i = 1; i <= n; ++i )
	{
		lua_pushvalue( L, i );
	}
	lua_call( L, n, 1 );
	int obj = lua_gettop( L );
	if ( !lua_istable( L, obj ) )
	{
		return 1;
	}

	int s = lua_istable( L, 1 ) ? 2 : 1;
	std::vector<float> v;
	for ( int i = s; i <= n; ++i )
	{
		if ( lua_isnumber( L, i ) )
		{
			v.push_back( (float)lua_tonumber( L, i ) );
		}
	}
	if ( v.size() >= 4 )
	{
		float x0 = v[0], y0 = v[1];
		for ( size_t i = 0; i + 1 < v.size(); i += 2 )
		{
			v[i] -= x0;
			v[i + 1] -= y0;
		}
		PushFlat( L, v );
		lua_pushnumber( L, x0 );
		lua_setfield( L, -2, "x0" );
		lua_pushnumber( L, y0 );
		lua_setfield( L, -2, "y0" );
		lua_pushliteral( L, "_b2line" );
		lua_insert( L, -2 );
		lua_rawset( L, obj );

		lua_pushliteral( L, "append" );
		lua_pushcfunction( L, LineAppend );
		lua_rawset( L, obj );
	}
	lua_settop( L, obj );
	return 1;
}

// display.newPolygon( [parent,] x, y, vertices ) wrapper; upvalue 1 = original
static int NewPolygonHook( lua_State* L )
{
	int n = lua_gettop( L );
	// the arguments are pushed again for the original function: display.newLine( parent, unpack( points ) )
	// easily passes more than the LUA_MINSTACK (20) free slots a C function is guaranteed
	luaL_checkstack( L, n + 8, "too many arguments" );
	lua_pushvalue( L, lua_upvalueindex( 1 ) );
	for ( int i = 1; i <= n; ++i )
	{
		lua_pushvalue( L, i );
	}
	lua_call( L, n, 1 );
	int obj = lua_gettop( L );
	if ( !lua_istable( L, obj ) )
	{
		return 1;
	}

	int vi = n; // ( [parent,] x, y, vertices ): the vertices are the last argument
	if ( vi >= 3 && lua_istable( L, vi ) )
	{
		std::vector<float> v;
		int len = (int)lua_objlen( L, vi );
		for ( int i = 1; i <= len; ++i )
		{
			lua_rawgeti( L, vi, i );
			v.push_back( (float)lua_tonumber( L, -1 ) );
			lua_pop( L, 1 );
		}
		if ( v.size() >= 6 )
		{
			// Solar2D re-centres the vertices on their bounding box
			float minX = v[0], maxX = v[0], minY = v[1], maxY = v[1];
			for ( size_t i = 0; i + 1 < v.size(); i += 2 )
			{
				minX = v[i] < minX ? v[i] : minX;
				maxX = v[i] > maxX ? v[i] : maxX;
				minY = v[i + 1] < minY ? v[i + 1] : minY;
				maxY = v[i + 1] > maxY ? v[i + 1] : maxY;
			}
			float cx = 0.5f * ( minX + maxX ), cy = 0.5f * ( minY + maxY );
			for ( size_t i = 0; i + 1 < v.size(); i += 2 )
			{
				v[i] -= cx;
				v[i + 1] -= cy;
			}
			lua_pushliteral( L, "_b2polygon" );
			PushFlat( L, v );
			lua_rawset( L, obj );
		}
	}
	lua_settop( L, obj );
	return 1;
}

// graphics.newOutline( coarseness, file|sheet, [baseDir|frame] ) wrapper; upvalue 1 = original.
// Records the texture size so addBody can scale the outline like v2 did.
static int NewOutlineHook( lua_State* L )
{
	int n = lua_gettop( L );
	// the arguments are pushed again for the original function: display.newLine( parent, unpack( points ) )
	// easily passes more than the LUA_MINSTACK (20) free slots a C function is guaranteed
	luaL_checkstack( L, n + 8, "too many arguments" );
	lua_pushvalue( L, lua_upvalueindex( 1 ) );
	for ( int i = 1; i <= n; ++i )
	{
		lua_pushvalue( L, i );
	}
	lua_call( L, n, 1 );
	int outline = lua_gettop( L );
	if ( !lua_istable( L, outline ) || n < 2 )
	{
		return 1;
	}

	// temporary display.newImage( file|sheet, [baseDir|frame] ) to read the natural size
	lua_getglobal( L, "display" );
	if ( lua_istable( L, -1 ) )
	{
		lua_getfield( L, -1, "newImage" );
		if ( lua_isfunction( L, -1 ) )
		{
			int args = 0;
			for ( int i = 2; i <= n; ++i )
			{
				lua_pushvalue( L, i );
				++args;
			}
			if ( lua_pcall( L, args, 1, 0 ) == 0 && lua_istable( L, -1 ) )
			{
				int img = lua_gettop( L );
				float w = b2p_getNumberField( L, img, "width", 0.0f );
				float h = b2p_getNumberField( L, img, "height", 0.0f );
				if ( w > 0.0f && h > 0.0f )
				{
					lua_pushnumber( L, w );
					lua_setfield( L, outline, "_b2texW" );
					lua_pushnumber( L, h );
					lua_setfield( L, outline, "_b2texH" );
				}
				lua_getfield( L, img, "removeSelf" );
				if ( lua_isfunction( L, -1 ) )
				{
					lua_pushvalue( L, img );
					lua_pcall( L, 1, 0, 0 );
				}
				else
				{
					lua_pop( L, 1 );
				}
			}
		}
	}
	lua_settop( L, outline );
	return 1;
}

static void Hook( lua_State* L, const char* lib, const char* fn, lua_CFunction hook )
{
	lua_getglobal( L, lib );
	if ( lua_istable( L, -1 ) )
	{
		lua_getfield( L, -1, fn );
		if ( lua_isfunction( L, -1 ) )
		{
			lua_getfield( L, -2, "_b2hooked" );
			bool hooked = false;
			if ( lua_istable( L, -1 ) )
			{
				lua_getfield( L, -1, fn );
				hooked = lua_toboolean( L, -1 ) != 0;
				lua_pop( L, 1 );
			}
			lua_pop( L, 1 );
			if ( !hooked )
			{
				lua_pushcclosure( L, hook, 1 ); // consumes the original
				lua_setfield( L, -2, fn );

				lua_getfield( L, -1, "_b2hooked" );
				if ( !lua_istable( L, -1 ) )
				{
					lua_pop( L, 1 );
					lua_newtable( L );
					lua_pushvalue( L, -1 );
					lua_setfield( L, -3, "_b2hooked" );
				}
				lua_pushboolean( L, 1 );
				lua_setfield( L, -2, fn );
				lua_pop( L, 1 );
			}
			else
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

void b2p_installDisplayHooks( lua_State* L )
{
	Hook( L, "display", "newLine", NewLineHook );
	Hook( L, "display", "newPolygon", NewPolygonHook );
	Hook( L, "graphics", "newOutline", NewOutlineHook );
}

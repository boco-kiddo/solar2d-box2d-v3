// plugin.box2dv3 — collision events with the v2 ("physics") model:
//
//   "collision"     phase "began" / "ended"   (contact begin/end events and sensor events of Box2D v3)
//   "preCollision"  every step while touching  (Box2D v3 pre-solve callback, contact.isEnabled works)
//   "postCollision" every step while touching  (contact impulses read after the step)
//
// Propagation is the same as Solar2D's: object1 first (event.other = object2), then object2 unless a
// listener returned true, then Runtime (event.object1 / object2 / element1 / element2) unless handled.
//
// Box2D v3 reports begin/end events after the step, so "collision" listeners run with the world
// unlocked (v2 ran them inside the step). "preCollision" runs inside the step like v2: removing a
// display object there is deferred until the step ends.
#include "b2plugin.h"

#include <math.h>
#include <stdint.h>
#include <string.h>
#include <unordered_set>

static char kContactMt = 0;

// ---------------------------------------------------------------------------
// event.contact

struct ContactUD
{
	bool valid;		  // only during the dispatch of its event
	bool enabled;	  // contact.isEnabled
	bool canDisable;  // true for preCollision (pre-solve), where disabling has an effect
	bool touching;
	b2Vec2 normal;
	float friction;
	float bounce;
	float tangentSpeed;
};

static bool sWarnedContactWrite = false;

static int ContactIndex( lua_State* L )
{
	ContactUD* c = (ContactUD*)lua_touserdata( L, 1 );
	const char* key = lua_tostring( L, 2 );
	if ( c == nullptr || !c->valid || key == nullptr )
	{
		lua_pushnil( L );
		return 1;
	}
	if ( strcmp( key, "isEnabled" ) == 0 )
		lua_pushboolean( L, c->enabled );
	else if ( strcmp( key, "isTouching" ) == 0 )
		lua_pushboolean( L, c->touching );
	else if ( strcmp( key, "friction" ) == 0 )
		lua_pushnumber( L, c->friction );
	else if ( strcmp( key, "bounce" ) == 0 )
		lua_pushnumber( L, c->bounce );
	else if ( strcmp( key, "tangentSpeed" ) == 0 )
		lua_pushnumber( L, c->tangentSpeed );
	else if ( strcmp( key, "normalX" ) == 0 ) // new: contact normal, from object1 to object2
		lua_pushnumber( L, c->normal.x );
	else if ( strcmp( key, "normalY" ) == 0 )
		lua_pushnumber( L, c->normal.y );
	else
		lua_pushnil( L );
	return 1;
}

static int ContactNewindex( lua_State* L )
{
	ContactUD* c = (ContactUD*)lua_touserdata( L, 1 );
	const char* key = lua_tostring( L, 2 );
	if ( c == nullptr || !c->valid || key == nullptr )
	{
		return 0;
	}
	if ( strcmp( key, "isEnabled" ) == 0 )
	{
		c->enabled = lua_toboolean( L, 3 ) != 0;
		if ( !c->canDisable && !sWarnedContactWrite )
		{
			sWarnedContactWrite = true;
			b2p_warn( L, "event.contact.isEnabled only has an effect in \"preCollision\" listeners with Box2D v3." );
		}
	}
	else if ( strcmp( key, "friction" ) == 0 || strcmp( key, "bounce" ) == 0 || strcmp( key, "tangentSpeed" ) == 0 )
	{
		if ( !sWarnedContactWrite )
		{
			sWarnedContactWrite = true;
			b2p_warn( L, "event.contact.%s cannot be changed per contact in Box2D v3 and is ignored "
						 "(set friction / bounce on the body element instead).",
					  key );
		}
	}
	return 0;
}

static ContactUD* PushContact( lua_State* L, b2ShapeId a, b2ShapeId b, b2Vec2 normal, bool touching, bool canDisable )
{
	ContactUD* c = (ContactUD*)lua_newuserdata( L, sizeof( ContactUD ) );
	c->valid = true;
	c->enabled = true;
	c->canDisable = canDisable;
	c->touching = touching;
	c->normal = normal;
	b2SurfaceMaterial ma = b2Shape_GetSurfaceMaterial( a );
	b2SurfaceMaterial mb = b2Shape_GetSurfaceMaterial( b );
	c->friction = sqrtf( ma.friction * mb.friction ); // Box2D's default mixing
	c->bounce = ma.restitution > mb.restitution ? ma.restitution : mb.restitution;
	c->tangentSpeed = ma.tangentSpeed + mb.tangentSpeed;
	lua_pushlightuserdata( L, (void*)&kContactMt );
	lua_rawget( L, LUA_REGISTRYINDEX );
	lua_setmetatable( L, -2 );
	return c;
}

void b2p_initEvents( lua_State* L )
{
	lua_pushlightuserdata( L, (void*)&kContactMt );
	lua_newtable( L );
	lua_pushcfunction( L, ContactIndex );
	lua_setfield( L, -2, "__index" );
	lua_pushcfunction( L, ContactNewindex );
	lua_setfield( L, -2, "__newindex" );
	lua_rawset( L, LUA_REGISTRYINDEX );
	sWarnedContactWrite = false;
}

// ---------------------------------------------------------------------------
// listener detection (Solar2D's EventDispatcher keeps listeners in these two table fields)

static bool RespondsTo( lua_State* L, int obj, const char* name )
{
	static const char* const fields[] = { "_functionListeners", "_tableListeners" };
	for ( const char* f : fields )
	{
		lua_pushstring( L, f );
		lua_rawget( L, obj );
		if ( lua_istable( L, -1 ) )
		{
			lua_getfield( L, -1, name );
			bool has = lua_istable( L, -1 ) && lua_objlen( L, -1 ) > 0;
			lua_pop( L, 2 );
			if ( has )
			{
				return true;
			}
		}
		else
		{
			lua_pop( L, 1 );
		}
	}
	return false;
}

static bool IsAlive( lua_State* L, int obj )
{
	if ( !lua_getmetatable( L, obj ) )
	{
		return false; // Solar2D removed the display object
	}
	lua_pop( L, 1 );
	return true;
}

// Every shape is created with pre-solve events on (Box2D only reads the shape flag when a contact is
// created, so a listener added later would miss existing contacts). The callback filters on this flag.
void b2p_applyPreSolveFlag( Body* body, bool enabled )
{
	body->preSolveEnabled = enabled;
}

void b2p_scanListeners( lua_State* L, PluginState* st )
{
	int top = lua_gettop( L );
	lua_getglobal( L, "Runtime" );
	int rt = lua_gettop( L );
	st->runtimePre = lua_istable( L, rt ) && RespondsTo( L, rt, "preCollision" );
	st->runtimePost = lua_istable( L, rt ) && RespondsTo( L, rt, "postCollision" );
	st->anyPost = st->runtimePost;
	st->anyHit = lua_istable( L, rt ) && RespondsTo( L, rt, "hit" );
	st->anyPreSolve = st->runtimePre;

	for ( Body* b : st->bodies )
	{
		if ( b->destroyed || b->pendingDestroy )
		{
			continue;
		}
		lua_rawgeti( L, LUA_REGISTRYINDEX, b->objRef );
		int obj = lua_gettop( L );
		bool pre = st->runtimePre || RespondsTo( L, obj, "preCollision" );
		b->wantPost = RespondsTo( L, obj, "postCollision" );
		st->anyPost = st->anyPost || b->wantPost;
		st->anyHit = st->anyHit || RespondsTo( L, obj, "hit" );
		st->anyPreSolve = st->anyPreSolve || pre;
		lua_settop( L, rt );
		if ( pre != b->preSolveEnabled )
		{
			b2p_applyPreSolveFlag( b, pre );
		}
	}
	lua_settop( L, top );
}

// ---------------------------------------------------------------------------
// dispatch

struct CollisionInfo
{
	const char* name;
	const char* phase; // nullptr for pre/postCollision
	int obj1, obj2;	   // stack indices of the display objects
	int element1, element2;
	float x, y;
	int contact; // stack index of the contact userdata or 0
	bool hasForce;
	float force, friction;
	bool hasHit; // "hit" event: approachSpeed, normalX, normalY
	float approachSpeed, normalX, normalY;
};

static void SetNumber( lua_State* L, const char* k, double v )
{
	lua_pushnumber( L, v );
	lua_setfield( L, -2, k );
}

// Calls target:dispatchEvent( event ) protected; returns whether a listener returned true.
static bool CallDispatch( lua_State* L, PluginState* st, int target, int errfunc )
{
	lua_getfield( L, target, "dispatchEvent" );
	if ( !lua_isfunction( L, -1 ) )
	{
		lua_pop( L, 2 ); // function, event
		return false;
	}
	lua_pushvalue( L, target );
	lua_pushvalue( L, -3 ); // event
	bool handled = false;
	if ( lua_pcall( L, 2, 1, errfunc ) != 0 )
	{
		if ( st->pendingError.empty() )
		{
			const char* msg = lua_tostring( L, -1 );
			st->pendingError = msg ? msg : "error in collision listener";
		}
	}
	else
	{
		handled = lua_toboolean( L, -1 ) != 0;
	}
	lua_pop( L, 2 ); // result / error, event
	return handled;
}

static void PushLocalEvent( lua_State* L, const CollisionInfo& c, int self, int other, int selfElement, int otherElement )
{
	lua_newtable( L );
	lua_pushstring( L, c.name );
	lua_setfield( L, -2, "name" );
	if ( c.phase )
	{
		lua_pushstring( L, c.phase );
		lua_setfield( L, -2, "phase" );
	}
	lua_pushvalue( L, self );
	lua_setfield( L, -2, "target" );
	lua_pushvalue( L, other );
	lua_setfield( L, -2, "other" );
	SetNumber( L, "selfElement", selfElement );
	SetNumber( L, "otherElement", otherElement );
	SetNumber( L, "x", c.x );
	SetNumber( L, "y", c.y );
	if ( c.contact )
	{
		lua_pushvalue( L, c.contact );
		lua_setfield( L, -2, "contact" );
	}
	if ( c.hasForce )
	{
		SetNumber( L, "force", c.force );
		SetNumber( L, "friction", c.friction );
	}
	if ( c.hasHit )
	{
		SetNumber( L, "approachSpeed", c.approachSpeed );
		SetNumber( L, "normalX", c.normalX );
		SetNumber( L, "normalY", c.normalY );
	}
}

static void DispatchCollision( lua_State* L, PluginState* st, const CollisionInfo& c, int runtime, int errfunc )
{
	bool handled = false;
	if ( IsAlive( L, c.obj1 ) && RespondsTo( L, c.obj1, c.name ) )
	{
		PushLocalEvent( L, c, c.obj1, c.obj2, c.element1, c.element2 );
		handled = CallDispatch( L, st, c.obj1, errfunc );
	}
	if ( !handled && IsAlive( L, c.obj2 ) && RespondsTo( L, c.obj2, c.name ) )
	{
		PushLocalEvent( L, c, c.obj2, c.obj1, c.element2, c.element1 );
		handled = CallDispatch( L, st, c.obj2, errfunc );
	}
	if ( !handled && runtime != 0 && RespondsTo( L, runtime, c.name ) )
	{
		lua_newtable( L );
		lua_pushstring( L, c.name );
		lua_setfield( L, -2, "name" );
		if ( c.phase )
		{
			lua_pushstring( L, c.phase );
			lua_setfield( L, -2, "phase" );
		}
		lua_pushvalue( L, c.obj1 );
		lua_setfield( L, -2, "object1" );
		lua_pushvalue( L, c.obj2 );
		lua_setfield( L, -2, "object2" );
		SetNumber( L, "element1", c.element1 );
		SetNumber( L, "element2", c.element2 );
		SetNumber( L, "x", c.x );
		SetNumber( L, "y", c.y );
		if ( c.contact )
		{
			lua_pushvalue( L, c.contact );
			lua_setfield( L, -2, "contact" );
		}
		if ( c.hasForce )
		{
			SetNumber( L, "force", c.force );
			SetNumber( L, "friction", c.friction );
		}
		if ( c.hasHit )
		{
			SetNumber( L, "approachSpeed", c.approachSpeed );
			SetNumber( L, "normalX", c.normalX );
			SetNumber( L, "normalY", c.normalY );
		}
		CallDispatch( L, st, runtime, errfunc );
	}
}

static Body* BodyOfShape( b2ShapeId shape )
{
	if ( !b2Shape_IsValid( shape ) )
	{
		return nullptr;
	}
	Body* b = (Body*)b2Body_GetUserData( b2Shape_GetBody( shape ) );
	if ( b == nullptr || b->destroyed || b->pendingDestroy )
	{
		return nullptr;
	}
	return b;
}

static int ElementOf( b2ShapeId shape )
{
	return (int)(intptr_t)b2Shape_GetUserData( shape );
}

// Event position: the first (or averaged) contact point, in content coordinates by default, or in
// object1's local coordinates after physics.setReportCollisionsInContentCoordinates( false ).
static void EventPoint( const PluginState* st, b2BodyId bodyA, const b2Manifold* m, float* x, float* y )
{
	*x = 0.0f;
	*y = 0.0f;
	if ( m == nullptr || m->pointCount == 0 )
	{
		return;
	}
	int n = st->averagePositions ? m->pointCount : 1;
	b2Pos center = b2Body_GetWorldCenter( bodyA );
	double sx = 0.0, sy = 0.0;
	for ( int i = 0; i < n; ++i )
	{
		sx += center.x + m->points[i].anchorA.x;
		sy += center.y + m->points[i].anchorA.y;
	}
	b2Pos world = { (float)( sx / n ), (float)( sy / n ) };
	if ( st->reportContentCoords )
	{
		*x = b2p_toPixels( st, (float)world.x );
		*y = b2p_toPixels( st, (float)world.y );
	}
	else
	{
		b2Vec2 local = b2Body_GetLocalPoint( bodyA, world );
		*x = b2p_toPixels( st, local.x );
		*y = b2p_toPixels( st, local.y );
	}
}

static int PushErrFunc( lua_State* L )
{
	lua_getglobal( L, "debug" );
	if ( lua_istable( L, -1 ) )
	{
		lua_getfield( L, -1, "traceback" );
		lua_remove( L, -2 );
		if ( lua_isfunction( L, -1 ) )
		{
			return lua_gettop( L );
		}
	}
	lua_pop( L, 1 );
	lua_pushnil( L ); // keep the stack shape; 0 = no handler
	return 0;
}

// Dispatches one pair. Returns false if the world went away (physics.stop() in a listener).
static bool DispatchPair( lua_State* L, PluginState* st, const char* name, const char* phase, b2ShapeId a, b2ShapeId b,
						  const b2Manifold* manifold, b2Vec2 normal, bool touching, bool hasForce, float force,
						  float friction, int runtime, int errfunc )
{
	Body* ba = BodyOfShape( a );
	Body* bb = BodyOfShape( b );
	if ( ba == nullptr || bb == nullptr )
	{
		return true; // v2 skipped events of removed objects
	}
	int top = lua_gettop( L );
	lua_rawgeti( L, LUA_REGISTRYINDEX, ba->objRef );
	lua_rawgeti( L, LUA_REGISTRYINDEX, bb->objRef );

	CollisionInfo c;
	c.name = name;
	c.phase = phase;
	c.obj1 = top + 1;
	c.obj2 = top + 2;
	c.element1 = ElementOf( a );
	c.element2 = ElementOf( b );
	EventPoint( st, ba->id, manifold, &c.x, &c.y );
	ContactUD* ud = PushContact( L, a, b, normal, touching, false );
	c.contact = top + 3;
	c.hasForce = hasForce;
	c.force = force;
	c.friction = friction;
	c.hasHit = false;
	c.approachSpeed = c.normalX = c.normalY = 0.0f;

	DispatchCollision( L, st, c, runtime, errfunc );
	ud->valid = false;
	lua_settop( L, top );
	return B2_IS_NON_NULL( st->world );
}

// v3 "hit" event: a contact began with an approach speed above physics.setHitEventThreshold().
static bool DispatchHit( lua_State* L, PluginState* st, const b2ContactHitEvent& e, int runtime, int errfunc )
{
	Body* ba = BodyOfShape( e.shapeIdA );
	Body* bb = BodyOfShape( e.shapeIdB );
	if ( ba == nullptr || bb == nullptr )
	{
		return true;
	}
	int top = lua_gettop( L );
	lua_rawgeti( L, LUA_REGISTRYINDEX, ba->objRef );
	lua_rawgeti( L, LUA_REGISTRYINDEX, bb->objRef );

	CollisionInfo c;
	c.name = "hit";
	c.phase = nullptr;
	c.obj1 = top + 1;
	c.obj2 = top + 2;
	c.element1 = ElementOf( e.shapeIdA );
	c.element2 = ElementOf( e.shapeIdB );
	if ( st->reportContentCoords )
	{
		c.x = b2p_toPixels( st, (float)e.point.x );
		c.y = b2p_toPixels( st, (float)e.point.y );
	}
	else
	{
		b2Vec2 local = b2Body_GetLocalPoint( ba->id, e.point );
		c.x = b2p_toPixels( st, local.x );
		c.y = b2p_toPixels( st, local.y );
	}
	c.contact = 0;
	c.hasForce = false;
	c.force = c.friction = 0.0f;
	c.hasHit = true;
	c.approachSpeed = b2p_toPixels( st, e.approachSpeed );
	c.normalX = e.normal.x;
	c.normalY = e.normal.y;

	DispatchCollision( L, st, c, runtime, errfunc );
	lua_settop( L, top );
	return B2_IS_NON_NULL( st->world );
}

void b2p_dispatchStepEvents( lua_State* L, PluginState* st )
{
	b2ContactEvents ce = b2World_GetContactEvents( st->world );
	b2SensorEvents se = b2World_GetSensorEvents( st->world );
	bool needPost = st->anyPost;
	int hitCount = st->anyHit ? ce.hitCount : 0;
	if ( ce.beginCount == 0 && ce.endCount == 0 && se.beginCount == 0 && se.endCount == 0 && hitCount == 0 && !needPost )
	{
		return;
	}

	int top = lua_gettop( L );
	int errfunc = PushErrFunc( L );
	lua_getglobal( L, "Runtime" );
	int runtime = lua_istable( L, -1 ) ? lua_gettop( L ) : 0;
	const b2Vec2 zero = { 0.0f, 0.0f };

	for ( int i = 0; i < ce.beginCount; ++i )
	{
		const b2ContactBeginTouchEvent& e = ce.beginEvents[i];
		b2ContactData data;
		bool hasData = b2Contact_IsValid( e.contactId );
		if ( hasData )
		{
			data = b2Contact_GetData( e.contactId );
		}
		if ( !DispatchPair( L, st, "collision", "began", e.shapeIdA, e.shapeIdB, hasData ? &data.manifold : nullptr,
							hasData ? data.manifold.normal : zero, true, false, 0, 0, runtime, errfunc ) )
		{
			goto done;
		}
	}
	for ( int i = 0; i < se.beginCount; ++i )
	{
		const b2SensorBeginTouchEvent& e = se.beginEvents[i];
		if ( !DispatchPair( L, st, "collision", "began", e.sensorShapeId, e.visitorShapeId, nullptr, zero, true, false, 0,
							0, runtime, errfunc ) )
		{
			goto done;
		}
	}
	for ( int i = 0; i < hitCount; ++i )
	{
		if ( !DispatchHit( L, st, ce.hitEvents[i], runtime, errfunc ) )
		{
			goto done;
		}
	}
	for ( int i = 0; i < ce.endCount; ++i )
	{
		const b2ContactEndTouchEvent& e = ce.endEvents[i];
		if ( !DispatchPair( L, st, "collision", "ended", e.shapeIdA, e.shapeIdB, nullptr, zero, false, false, 0, 0,
							runtime, errfunc ) )
		{
			goto done;
		}
	}
	for ( int i = 0; i < se.endCount; ++i )
	{
		const b2SensorEndTouchEvent& e = se.endEvents[i];
		if ( !DispatchPair( L, st, "collision", "ended", e.sensorShapeId, e.visitorShapeId, nullptr, zero, false, false,
							0, 0, runtime, errfunc ) )
		{
			goto done;
		}
	}

	// postCollision: impulses of every touching contact of awake bodies that someone listens to
	if ( needPost )
	{
		std::unordered_set<uint64_t> seen;
		std::vector<b2ContactData> contacts;
		// snapshot ids, not pointers: listeners may remove display objects (and free their Body)
		std::vector<b2BodyId> ids;
		ids.reserve( st->bodies.size() );
		for ( Body* b : st->bodies )
		{
			ids.push_back( b->id );
		}
		for ( b2BodyId id : ids )
		{
			if ( B2_IS_NULL( st->world ) )
			{
				break;
			}
			if ( !b2Body_IsValid( id ) || !b2Body_IsAwake( id ) )
			{
				continue;
			}
			Body* b = (Body*)b2Body_GetUserData( id );
			if ( b == nullptr || b->destroyed || b->pendingDestroy )
			{
				continue;
			}
			int cap = b2Body_GetContactCapacity( b->id );
			if ( cap <= 0 )
			{
				continue;
			}
			contacts.resize( (size_t)cap );
			int n = b2Body_GetContactData( b->id, contacts.data(), cap );
			for ( int k = 0; k < n; ++k )
			{
				const b2ContactData& d = contacts[(size_t)k];
				if ( d.manifold.pointCount == 0 )
				{
					continue;
				}
				uint64_t key = ( (uint64_t)(uint32_t)d.contactId.index1 << 32 ) | d.contactId.generation;
				if ( !seen.insert( key ).second )
				{
					continue;
				}
				Body* ba = BodyOfShape( d.shapeIdA );
				Body* bb = BodyOfShape( d.shapeIdB );
				if ( ba == nullptr || bb == nullptr || !( st->runtimePost || ba->wantPost || bb->wantPost ) )
				{
					continue;
				}
				float force = 0.0f, friction = 0.0f;
				for ( int p = 0; p < d.manifold.pointCount; ++p )
				{
					force = d.manifold.points[p].normalImpulse > force ? d.manifold.points[p].normalImpulse : force;
					float t = fabsf( d.manifold.points[p].tangentImpulse );
					friction = t > friction ? t : friction;
				}
				if ( !DispatchPair( L, st, "postCollision", nullptr, d.shapeIdA, d.shapeIdB, &d.manifold, d.manifold.normal,
									true, true, force, friction, runtime, errfunc ) )
				{
					break;
				}
			}
		}
	}

done:
	lua_settop( L, top );
}

// ---------------------------------------------------------------------------
// preCollision: Box2D pre-solve callback (inside the step, world locked)

static void PreSolve( b2ShapeId a, b2ShapeId b, b2Manifold* manifold, void* context )
{
	PluginState* st = (PluginState*)context;
	// Without preCollision listeners this may run on a Box2D worker thread: touch nothing but this flag.
	if ( !st->anyPreSolve )
	{
		return;
	}
	lua_State* L = st->L;
	if ( !st->pendingError.empty() )
	{
		return;
	}
	Body* ba = BodyOfShape( a );
	Body* bb = BodyOfShape( b );
	if ( ba == nullptr || bb == nullptr || !( ba->preSolveEnabled || bb->preSolveEnabled ) )
	{
		return;
	}
	if ( !lua_checkstack( L, 16 ) )
	{
		return;
	}

	int top = lua_gettop( L );
	int errfunc = PushErrFunc( L );
	lua_getglobal( L, "Runtime" );
	int runtime = ( st->runtimePre && lua_istable( L, -1 ) ) ? lua_gettop( L ) : 0;
	int obj1 = lua_gettop( L ) + 1;
	lua_rawgeti( L, LUA_REGISTRYINDEX, ba->objRef );
	lua_rawgeti( L, LUA_REGISTRYINDEX, bb->objRef );

	CollisionInfo c;
	c.name = "preCollision";
	c.phase = nullptr;
	c.obj1 = obj1;
	c.obj2 = obj1 + 1;
	c.element1 = ElementOf( a );
	c.element2 = ElementOf( b );
	EventPoint( st, ba->id, manifold, &c.x, &c.y );
	ContactUD* ud = PushContact( L, a, b, manifold->normal, manifold->pointCount > 0, true );
	c.contact = lua_gettop( L );
	c.hasForce = false;
	c.force = c.friction = 0.0f;
	c.hasHit = false;
	c.approachSpeed = c.normalX = c.normalY = 0.0f;

	DispatchCollision( L, st, c, runtime, errfunc );

	if ( !ud->enabled )
	{
		manifold->pointCount = 0; // disables the contact for this step, like b2Contact::SetEnabled( false )
	}
	ud->valid = false;
	lua_settop( L, top );
}

void b2p_attachWorldCallbacks( PluginState* st )
{
	b2World_SetPreSolveCallback( st->world, PreSolve, nullptr, st );
}

// ---------------------------------------------------------------------------

void b2p_processPendingDestroys( lua_State* L, PluginState* st )
{
	if ( st->pendingDestroy.empty() )
	{
		return;
	}
	std::vector<Body*> list;
	list.swap( st->pendingDestroy );
	for ( Body* b : list )
	{
		b->pendingDestroy = false;
		b2p_destroyBody( L, b, false );
	}
}

void b2p_raisePendingError( lua_State* L, PluginState* st )
{
	if ( st->pendingError.empty() )
	{
		return;
	}
	lua_pushstring( L, st->pendingError.c_str() );
	st->pendingError.clear();
	lua_error( L );
}

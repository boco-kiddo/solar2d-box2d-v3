// plugin.box2dv3 — character mover (Box2D v3 "geometric mover"), new API:
//
//   local mover = physics.newMover( obj, [options] )
//
// The display object is moved by a capsule that is cast through the world and pushed out of other
// shapes (b2World_CollideMover / b2SolvePlanes / b2World_CastMover), with a "pogo" spring that keeps
// it above the ground so it walks up steps and slopes. The game stays in control of the motion
// (Quake style ground / air movement), unlike a dynamic body. The mover is not a body: other bodies do
// not collide with it, but it pushes dynamic bodies it walks into and presses on what it stands on.
//
// The object's box is: capsule on top, legs of height `stepHeight` below. At rest the bottom of the
// object touches the ground.
//
// options (pixels, pixels / second, seconds):
//   radius        capsule radius               (default: half the object width)
//   height        total height incl. legs      (default: object height)
//   stepHeight    leg length, highest step     (default: height / 4)
//   maxSpeed      run speed                    (default: 180 px/s at the default scale)
//   jumpHeight    jump apex height             (default: 1.5 x height)
//   gravity       px/s^2 (down)                (default: physics world gravity)
//   acceleration  ground acceleration factor   (default 20: reaches maxSpeed in 1/20 s)
//   airControl    fraction of acceleration in the air (default 0.2)
//   friction      ground friction, 1/s         (default 8)
//   stopSpeed     px/s, below it friction stops the mover quickly (default 90 at the default scale)
//   pogoFrequency, pogoDampingRatio            (default 5 Hz, 0.8)
//   pushBodies    push dynamic bodies          (default true)
//   groundForce   N pressed on what it stands on (default 50)
//   filter        { categoryBits, maskBits }   shapes it collides with
//
// mover.throttle (-1 .. 1, horizontal input, kept until changed), mover:jump() -> bool,
// mover.isOnGround, mover.groundObject, mover:getVelocity() / mover:setVelocity( vx, vy ),
// mover.x / mover.y (assigning teleports, as does assigning obj.x / obj.y), the options above as
// read / write properties, mover:removeSelf(). Removing the display object removes the mover.
#include "b2plugin.h"

#include <float.h>
#include <string.h>

static char kMoverMt = 0;
static const int kPlaneCapacity = 8;

struct Mover
{
	uint64_t serial;
	int objRef;
	bool dead;

	b2Capsule capsule; // meters, relative to the position (center of the object)
	float radius, height, stepHeight; // meters
	b2QueryFilter filter;

	float maxSpeed, minSpeed, stopSpeed; // m/s
	float accelerate, airSteer, friction;
	float gravity;	 // m/s^2, positive = down (screen y)
	float jumpSpeed; // m/s
	float pogoRestLength, pogoHertz, pogoDampingRatio;
	float groundForce;
	float minGroundNormalY;
	bool pushBodies;

	b2Pos position;
	b2Vec2 velocity; // m/s, screen axes
	float pogoVelocity;
	bool onGround;
	float throttle;
	b2BodyId groundBody;

	b2CollisionPlane planes[kPlaneCapacity];
	b2Pos planePoints[kPlaneCapacity];
	b2ShapeId planeShapes[kPlaneCapacity];
	int planeCount;

	float lastX, lastY; // pixels last written to the display object
};

struct MoverUD
{
	uint64_t serial;
};

static Mover* FindMover( PluginState* st, uint64_t serial )
{
	for ( Mover* m : st->movers )
	{
		if ( m->serial == serial && !m->dead )
		{
			return m;
		}
	}
	return nullptr;
}

static Mover* ToMover( lua_State* L, int index )
{
	MoverUD* ud = (MoverUD*)lua_touserdata( L, index );
	PluginState* st = b2p_state( L );
	return ( ud && st ) ? FindMover( st, ud->serial ) : nullptr;
}

// Builds the capsule from radius / height / stepHeight. y grows downwards.
static void UpdateGeometry( Mover* m )
{
	float r = m->radius;
	float body = m->height - m->stepHeight; // capsule length
	if ( body < 2.0f * r )
	{
		body = 2.0f * r;
	}
	float top = -0.5f * m->height;
	m->capsule.center1 = b2Vec2{ 0.0f, top + r };		 // top sphere
	m->capsule.center2 = b2Vec2{ 0.0f, top + body - r }; // bottom sphere
	m->capsule.radius = r;
	// the pogo spring holds the bottom sphere centre this far above the ground
	m->pogoRestLength = 0.5f * m->height - m->capsule.center2.y;
	if ( m->pogoRestLength < r )
	{
		m->pogoRestLength = r;
	}
}

// ---------------------------------------------------------------------------
// simulation (port of Box2D's samples/geometric_mover.cpp, with screen y pointing down)

struct CastResult
{
	b2Pos point;
	b2Vec2 normal;
	b2BodyId bodyId;
	float fraction;
	bool hit;
};

static float CastCallback( b2ShapeId shapeId, b2Pos point, b2Vec2 normal, float fraction, void* context )
{
	CastResult* r = (CastResult*)context;
	if ( b2Shape_IsSensor( shapeId ) )
	{
		return -1.0f;
	}
	r->point = point;
	r->normal = normal;
	r->bodyId = b2Shape_GetBody( shapeId );
	r->fraction = fraction;
	r->hit = true;
	return fraction;
}

static bool PlaneCallback( b2ShapeId shapeId, const b2PlaneResult* result, void* context )
{
	Mover* m = (Mover*)context;
	if ( !result->hit || b2Shape_IsSensor( shapeId ) )
	{
		return true;
	}
	if ( m->planeCount < kPlaneCapacity )
	{
		m->planes[m->planeCount] = b2CollisionPlane{ result->plane, FLT_MAX, 0.0f, true };
		m->planePoints[m->planeCount] = b2OffsetPos( m->position, result->point );
		m->planeShapes[m->planeCount] = shapeId;
		m->planeCount += 1;
	}
	return true;
}

// A normal velocity constraint per touched dynamic body; the mover has infinite mass.
static void PushBodies( Mover* m )
{
	for ( int i = 0; i < m->planeCount; ++i )
	{
		b2BodyId bodyId = b2Shape_GetBody( m->planeShapes[i] );
		if ( b2Body_GetType( bodyId ) != b2_dynamicBody )
		{
			continue;
		}
		b2Pos point = m->planePoints[i];
		b2Vec2 normal = b2Neg( m->planes[i].plane.normal ); // plane normals point at the mover

		float mass = b2Body_GetMass( bodyId );
		float invMass = mass > 0.0f ? 1.0f / mass : 0.0f;
		float inertia = b2Body_GetRotationalInertia( bodyId );
		float invI = inertia > 0.0f ? 1.0f / inertia : 0.0f;
		b2Pos center = b2Body_GetWorldCenter( bodyId );
		b2Vec2 rB = b2SubPos( point, center );
		float rn = b2Cross( rB, normal );
		float k = invMass + invI * rn * rn;
		float normalMass = k > 0.0f ? 1.0f / k : 0.0f;

		b2Vec2 vB = b2Add( b2Body_GetLinearVelocity( bodyId ), b2CrossSV( b2Body_GetAngularVelocity( bodyId ), rB ) );
		float vn = b2Dot( b2Sub( vB, m->velocity ), normal );
		float impulse = b2MaxFloat( -normalMass * vn, 0.0f ); // push only
		if ( impulse > 0.0f )
		{
			b2Body_ApplyLinearImpulse( bodyId, b2MulSV( impulse, normal ), point, true );
		}
	}
}

static void UpdateMover( PluginState* st, Mover* m, float dt )
{
	// friction
	float speed = b2Length( m->velocity );
	if ( speed < m->minSpeed )
	{
		m->velocity = b2Vec2{ 0.0f, 0.0f };
	}
	else if ( m->onGround )
	{
		float control = speed < m->stopSpeed ? m->stopSpeed : speed;
		float drop = control * m->friction * dt;
		float newSpeed = b2MaxFloat( 0.0f, speed - drop );
		m->velocity = b2MulSV( newSpeed / speed, m->velocity );
	}

	// accelerate towards throttle * maxSpeed
	float throttle = m->throttle < -1.0f ? -1.0f : ( m->throttle > 1.0f ? 1.0f : m->throttle );
	b2Vec2 desired = { m->maxSpeed * throttle, 0.0f };
	float desiredSpeed;
	b2Vec2 dir = b2GetLengthAndNormalize( &desiredSpeed, desired );
	float currentSpeed = b2Dot( m->velocity, dir );
	float addSpeed = desiredSpeed - currentSpeed;
	if ( addSpeed > 0.0f )
	{
		float steer = m->onGround ? 1.0f : m->airSteer;
		float accelSpeed = steer * m->accelerate * m->maxSpeed * dt;
		m->velocity = b2MulAdd( m->velocity, accelSpeed < addSpeed ? accelSpeed : addSpeed, dir );
	}

	// pogo probe below the bottom sphere
	float rayLength = m->onGround ? 2.0f * m->pogoRestLength : m->pogoRestLength;
	b2Vec2 translation = { 0.0f, rayLength };
	b2Pos origin = b2OffsetPos( m->position, m->capsule.center2 );
	CastResult cast = {};
	b2World_CastRay( st->world, origin, translation, m->filter, CastCallback, &cast );

	bool haveGround = cast.hit && cast.normal.y < -m->minGroundNormalY;
	m->onGround = m->onGround ? haveGround : ( haveGround && m->velocity.y >= -0.01f );
	m->groundBody = m->onGround ? cast.bodyId : b2_nullBodyId;

	if ( !m->onGround )
	{
		m->velocity.y += m->gravity * dt;
		m->pogoVelocity = 0.0f;
	}
	else
	{
		m->velocity.y = 0.0f;
		float offset = cast.fraction * rayLength - m->pogoRestLength;
		m->pogoVelocity = b2SpringDamper( m->pogoHertz, m->pogoDampingRatio, offset, m->pogoVelocity, dt );
		if ( m->groundForce > 0.0f && b2Body_GetType( cast.bodyId ) == b2_dynamicBody )
		{
			b2Body_ApplyForce( cast.bodyId, b2Vec2{ 0.0f, m->groundForce }, cast.point, true );
		}
	}

	// pogo velocity shortens the leg: positive moves the mover up (screen -y)
	b2Vec2 step = b2MulSV( dt, m->velocity );
	step.y -= dt * m->pogoVelocity;
	b2Pos target = b2OffsetPos( m->position, step );

	const float tolerance = 0.01f;
	for ( int iteration = 0; iteration < 5; ++iteration )
	{
		m->planeCount = 0;
		b2World_CollideMover( st->world, m->position, &m->capsule, m->filter, PlaneCallback, m );
		b2PlaneSolverResult result = b2SolvePlanes( b2SubPos( target, m->position ), m->planes, m->planeCount );
		float fraction = b2World_CastMover( st->world, m->position, &m->capsule, result.delta, m->filter );
		b2Vec2 delta = b2MulSV( fraction, result.delta );
		m->position = b2OffsetPos( m->position, delta );
		if ( b2LengthSquared( delta ) < tolerance * tolerance )
		{
			break;
		}
	}

	if ( m->pushBodies )
	{
		PushBodies( m );
	}
	m->velocity = b2ClipVector( m->velocity, m->planes, m->planeCount );
}

// Reads obj.x / obj.y; returns false if the object is gone.
static bool ReadObject( lua_State* L, Mover* m, float* x, float* y )
{
	lua_rawgeti( L, LUA_REGISTRYINDEX, m->objRef );
	int obj = lua_gettop( L );
	if ( !lua_istable( L, obj ) || !lua_getmetatable( L, obj ) )
	{
		lua_pop( L, 1 );
		return false;
	}
	lua_pop( L, 1 );
	lua_getfield( L, obj, "x" );
	lua_getfield( L, obj, "y" );
	*x = (float)lua_tonumber( L, -2 );
	*y = (float)lua_tonumber( L, -1 );
	lua_pop( L, 3 );
	return true;
}

static void WriteObject( lua_State* L, PluginState* st, Mover* m )
{
	m->lastX = b2p_toPixels( st, (float)m->position.x );
	m->lastY = b2p_toPixels( st, (float)m->position.y );
	lua_rawgeti( L, LUA_REGISTRYINDEX, m->objRef );
	lua_pushnumber( L, m->lastX );
	lua_setfield( L, -2, "x" );
	lua_pushnumber( L, m->lastY );
	lua_setfield( L, -2, "y" );
	lua_pop( L, 1 );
}

static void FreeMover( lua_State* L, Mover* m )
{
	if ( L != nullptr && m->objRef != LUA_NOREF )
	{
		luaL_unref( L, LUA_REGISTRYINDEX, m->objRef );
	}
	delete m;
}

void b2p_updateMovers( lua_State* L, PluginState* st, float dt )
{
	if ( st->movers.empty() || dt <= 0.0f )
	{
		return;
	}
	for ( size_t i = 0; i < st->movers.size(); )
	{
		Mover* m = st->movers[i];
		float x, y;
		if ( m->dead || !ReadObject( L, m, &x, &y ) )
		{
			FreeMover( L, m );
			st->movers[i] = st->movers.back();
			st->movers.pop_back();
			continue;
		}
		if ( x != m->lastX || y != m->lastY )
		{
			m->position = b2Pos{ b2p_toMeters( st, x ), b2p_toMeters( st, y ) }; // moved by the app: teleport
		}
		UpdateMover( st, m, dt );
		WriteObject( L, st, m );
		++i;
	}
}

void b2p_destroyAllMovers( lua_State* L, PluginState* st )
{
	for ( Mover* m : st->movers )
	{
		FreeMover( L, m );
	}
	st->movers.clear();
}

// ---- snapshot support
struct MoverStateSave
{
	uint64_t serial;
	b2Pos position;
	b2Vec2 velocity;
	float pogoVelocity;
	bool onGround;
};

void* b2p_saveMovers( PluginState* st, size_t* count )
{
	*count = st->movers.size();
	MoverStateSave* out = new MoverStateSave[st->movers.size() + 1];
	for ( size_t i = 0; i < st->movers.size(); ++i )
	{
		Mover* m = st->movers[i];
		out[i] = MoverStateSave{ m->serial, m->position, m->velocity, m->pogoVelocity, m->onGround };
	}
	return out;
}

void b2p_restoreMovers( lua_State* L, PluginState* st, const void* data, size_t count )
{
	const MoverStateSave* saved = (const MoverStateSave*)data;
	for ( size_t i = 0; i < count; ++i )
	{
		Mover* m = FindMover( st, saved[i].serial );
		if ( m == nullptr )
		{
			continue;
		}
		m->position = saved[i].position;
		m->velocity = saved[i].velocity;
		m->pogoVelocity = saved[i].pogoVelocity;
		m->onGround = saved[i].onGround;
		m->groundBody = b2_nullBodyId;
		m->planeCount = 0;
		float x, y;
		if ( ReadObject( L, m, &x, &y ) )
		{
			WriteObject( L, st, m );
		}
	}
}

void b2p_freeMoverSave( void* data )
{
	delete[] (MoverStateSave*)data;
}

// ---------------------------------------------------------------------------
// Lua API

static Mover* CheckMover( lua_State* L, const char* fname )
{
	Mover* m = ToMover( L, 1 );
	if ( m == nullptr )
	{
		luaL_error( L, "%s: mover is no longer valid (use ':' to call mover methods).", fname );
	}
	return m;
}

static int m_jump( lua_State* L )
{
	Mover* m = CheckMover( L, "mover:jump()" );
	if ( !m->onGround )
	{
		lua_pushboolean( L, 0 );
		return 1;
	}
	m->velocity.y = -m->jumpSpeed;
	m->onGround = false;
	m->pogoVelocity = 0.0f;
	lua_pushboolean( L, 1 );
	return 1;
}

static int m_getVelocity( lua_State* L )
{
	Mover* m = CheckMover( L, "mover:getVelocity()" );
	PluginState* st = b2p_state( L );
	lua_pushnumber( L, b2p_toPixels( st, m->velocity.x ) );
	lua_pushnumber( L, b2p_toPixels( st, m->velocity.y ) );
	return 2;
}

static int m_setVelocity( lua_State* L )
{
	Mover* m = CheckMover( L, "mover:setVelocity()" );
	PluginState* st = b2p_state( L );
	m->velocity = b2Vec2{ b2p_toMeters( st, (float)luaL_checknumber( L, 2 ) ), b2p_toMeters( st, (float)luaL_checknumber( L, 3 ) ) };
	if ( m->velocity.y < 0.0f )
	{
		m->onGround = false; // launched upwards
	}
	return 0;
}

static int m_removeSelf( lua_State* L )
{
	Mover* m = ToMover( L, 1 );
	if ( m != nullptr )
	{
		m->dead = true; // freed before the next step
	}
	return 0;
}

static float JumpSpeedFor( float gravity, float height )
{
	return ( gravity > 0.0f && height > 0.0f ) ? sqrtf( 2.0f * gravity * height ) : 0.0f;
}

enum MoverProp
{
	kMpThrottle,
	kMpMaxSpeed,
	kMpJumpHeight,
	kMpJumpSpeed,
	kMpGravity,
	kMpAcceleration,
	kMpAirControl,
	kMpFriction,
	kMpStopSpeed,
	kMpPogoFrequency,
	kMpPogoDampingRatio,
	kMpPushBodies,
	kMpGroundForce,
	kMpRadius,
	kMpHeight,
	kMpStepHeight,
	kMpIsOnGround,
	kMpGroundObject,
	kMpX,
	kMpY,
	kMpCount
};

static const char* const kMoverProps[] = { "throttle", "maxSpeed", "jumpHeight", "jumpSpeed", "gravity", "acceleration",
										   "airControl", "friction", "stopSpeed", "pogoFrequency", "pogoDampingRatio",
										   "pushBodies", "groundForce", "radius", "height", "stepHeight", "isOnGround",
										   "groundObject", "x", "y" };

static int FindProp( const char* key )
{
	for ( int i = 0; i < kMpCount; ++i )
	{
		if ( strcmp( key, kMoverProps[i] ) == 0 )
		{
			return i;
		}
	}
	return -1;
}

static int MoverIndex( lua_State* L )
{
	const char* key = lua_tostring( L, 2 );
	if ( key == nullptr )
	{
		return 0;
	}
	static const luaL_Reg methods[] = {
		{ "jump", m_jump },			  { "getVelocity", m_getVelocity }, { "setVelocity", m_setVelocity },
		{ "removeSelf", m_removeSelf }, { nullptr, nullptr },
	};
	for ( const luaL_Reg* r = methods; r->name; ++r )
	{
		if ( strcmp( key, r->name ) == 0 )
		{
			lua_pushcfunction( L, r->func );
			return 1;
		}
	}
	Mover* m = ToMover( L, 1 );
	PluginState* st = b2p_state( L );
	if ( m == nullptr )
	{
		return 0; // removed mover: properties read nil
	}
	switch ( FindProp( key ) )
	{
		case kMpThrottle: lua_pushnumber( L, m->throttle ); break;
		case kMpMaxSpeed: lua_pushnumber( L, b2p_toPixels( st, m->maxSpeed ) ); break;
		case kMpJumpHeight:
			lua_pushnumber( L, m->gravity > 0.0f ? b2p_toPixels( st, m->jumpSpeed * m->jumpSpeed / ( 2.0f * m->gravity ) ) : 0.0 );
			break;
		case kMpJumpSpeed: lua_pushnumber( L, b2p_toPixels( st, m->jumpSpeed ) ); break;
		case kMpGravity: lua_pushnumber( L, b2p_toPixels( st, m->gravity ) ); break;
		case kMpAcceleration: lua_pushnumber( L, m->accelerate ); break;
		case kMpAirControl: lua_pushnumber( L, m->airSteer ); break;
		case kMpFriction: lua_pushnumber( L, m->friction ); break;
		case kMpStopSpeed: lua_pushnumber( L, b2p_toPixels( st, m->stopSpeed ) ); break;
		case kMpPogoFrequency: lua_pushnumber( L, m->pogoHertz ); break;
		case kMpPogoDampingRatio: lua_pushnumber( L, m->pogoDampingRatio ); break;
		case kMpPushBodies: lua_pushboolean( L, m->pushBodies ); break;
		case kMpGroundForce: lua_pushnumber( L, m->groundForce ); break;
		case kMpRadius: lua_pushnumber( L, b2p_toPixels( st, m->radius ) ); break;
		case kMpHeight: lua_pushnumber( L, b2p_toPixels( st, m->height ) ); break;
		case kMpStepHeight: lua_pushnumber( L, b2p_toPixels( st, m->stepHeight ) ); break;
		case kMpIsOnGround: lua_pushboolean( L, m->onGround ); break;
		case kMpGroundObject:
		{
			Body* b = ( B2_IS_NON_NULL( m->groundBody ) && b2Body_IsValid( m->groundBody ) )
						  ? (Body*)b2Body_GetUserData( m->groundBody )
						  : nullptr;
			if ( b != nullptr && !b->destroyed && !b->pendingDestroy )
				lua_rawgeti( L, LUA_REGISTRYINDEX, b->objRef );
			else
				lua_pushnil( L );
			break;
		}
		case kMpX: lua_pushnumber( L, b2p_toPixels( st, (float)m->position.x ) ); break;
		case kMpY: lua_pushnumber( L, b2p_toPixels( st, (float)m->position.y ) ); break;
		default: lua_pushnil( L ); break;
	}
	return 1;
}

static void SetProp( lua_State* L, PluginState* st, Mover* m, int prop, int v )
{
	float n = (float)lua_tonumber( L, v );
	float px = b2p_toMeters( st, n );
	switch ( prop )
	{
		case kMpThrottle: m->throttle = n; break;
		case kMpMaxSpeed: m->maxSpeed = px > 0.0f ? px : 0.0f; break;
		case kMpJumpHeight: m->jumpSpeed = JumpSpeedFor( m->gravity, px ); break;
		case kMpJumpSpeed: m->jumpSpeed = px > 0.0f ? px : 0.0f; break;
		case kMpGravity: m->gravity = px; break;
		case kMpAcceleration: m->accelerate = n > 0.0f ? n : 0.0f; break;
		case kMpAirControl: m->airSteer = n > 0.0f ? n : 0.0f; break;
		case kMpFriction: m->friction = n > 0.0f ? n : 0.0f; break;
		case kMpStopSpeed: m->stopSpeed = px > 0.0f ? px : 0.0f; break;
		case kMpPogoFrequency: m->pogoHertz = n > 0.0f ? n : 0.0f; break;
		case kMpPogoDampingRatio: m->pogoDampingRatio = n > 0.0f ? n : 0.0f; break;
		case kMpPushBodies: m->pushBodies = lua_toboolean( L, v ) != 0; break;
		case kMpGroundForce: m->groundForce = n > 0.0f ? n : 0.0f; break;
		case kMpRadius:
			m->radius = px > B2_LINEAR_SLOP ? px : B2_LINEAR_SLOP;
			UpdateGeometry( m );
			break;
		case kMpHeight:
			m->height = px > 2.0f * m->radius ? px : 2.0f * m->radius;
			UpdateGeometry( m );
			break;
		case kMpStepHeight:
			m->stepHeight = px > 0.0f ? px : 0.0f;
			UpdateGeometry( m );
			break;
		case kMpX:
		case kMpY:
		{
			if ( prop == kMpX )
				m->position.x = px;
			else
				m->position.y = px;
			float x, y;
			if ( ReadObject( L, m, &x, &y ) )
			{
				WriteObject( L, st, m );
			}
			break;
		}
		default:
			break; // read-only
	}
}

static int MoverNewindex( lua_State* L )
{
	const char* key = lua_tostring( L, 2 );
	Mover* m = ToMover( L, 1 );
	if ( key == nullptr || m == nullptr )
	{
		return 0;
	}
	int prop = FindProp( key );
	if ( prop < 0 )
	{
		return luaL_error( L, "mover: unknown property '%s'", key );
	}
	SetProp( L, b2p_state( L ), m, prop, 3 );
	return 0;
}

static int MoverToString( lua_State* L )
{
	lua_pushstring( L, ToMover( L, 1 ) ? "Mover" : "Mover [removed]" );
	return 1;
}

static int MoverEq( lua_State* L )
{
	MoverUD* a = (MoverUD*)lua_touserdata( L, 1 );
	MoverUD* b = (MoverUD*)lua_touserdata( L, 2 );
	lua_pushboolean( L, a && b && a->serial == b->serial );
	return 1;
}

// physics.newMover( obj, [options] ) -> mover
static int newMover( lua_State* L )
{
	PluginState* st = b2p_requireWorld( L, "physics.newMover()" );
	luaL_checktype( L, 1, LUA_TTABLE );
	if ( b2p_toBody( L, 1 ) != nullptr )
	{
		return luaL_error( L, "physics.newMover(): the object already has a physics body (a mover replaces the body)." );
	}
	int opt = lua_istable( L, 2 ) ? 2 : 0;
	ObjectGeometry g = b2p_readGeometry( L, 1 );
	float x = b2p_getNumberField( L, 1, "x", 0.0f );
	float y = b2p_getNumberField( L, 1, "y", 0.0f );

	auto num = [&]( const char* key, float def ) { return opt ? b2p_getNumberField( L, opt, key, def ) : def; };

	Mover* m = new Mover();
	m->serial = ++st->nextSerial;
	m->dead = false;
	float heightPx = num( "height", g.height > 0.0f ? g.height : 32.0f );
	float radiusPx = num( "radius", g.width > 0.0f ? 0.5f * g.width : 8.0f );
	m->radius = b2p_toMeters( st, radiusPx > 0.5f ? radiusPx : 0.5f );
	m->height = b2p_toMeters( st, heightPx );
	if ( m->height < 2.0f * m->radius )
	{
		m->height = 2.0f * m->radius;
	}
	m->stepHeight = b2p_toMeters( st, num( "stepHeight", 0.25f * b2p_toPixels( st, m->height ) ) );
	if ( m->stepHeight < 0.0f )
	{
		m->stepHeight = 0.0f;
	}
	UpdateGeometry( m );

	m->filter = b2DefaultQueryFilter();
	if ( opt )
	{
		lua_getfield( L, opt, "filter" );
		if ( lua_istable( L, -1 ) )
		{
			int f = lua_gettop( L );
			lua_getfield( L, f, "categoryBits" );
			if ( lua_isnumber( L, -1 ) )
				m->filter.categoryBits = (uint64_t)lua_tonumber( L, -1 );
			lua_pop( L, 1 );
			lua_getfield( L, f, "maskBits" );
			if ( lua_isnumber( L, -1 ) )
				m->filter.maskBits = (uint64_t)lua_tonumber( L, -1 );
			lua_pop( L, 1 );
		}
		lua_pop( L, 1 );
	}

	float worldGravity = sqrtf( st->gravity.x * st->gravity.x + st->gravity.y * st->gravity.y );
	m->maxSpeed = b2p_toMeters( st, num( "maxSpeed", 6.0f * st->scale ) );
	m->minSpeed = 0.1f;
	m->stopSpeed = b2p_toMeters( st, num( "stopSpeed", 3.0f * st->scale ) );
	m->accelerate = num( "acceleration", 20.0f );
	m->airSteer = num( "airControl", 0.2f );
	m->friction = num( "friction", 8.0f );
	m->gravity = b2p_toMeters( st, num( "gravity", b2p_toPixels( st, worldGravity > 0.0f ? worldGravity : 9.8f ) ) );
	m->jumpSpeed = JumpSpeedFor( m->gravity, b2p_toMeters( st, num( "jumpHeight", 1.5f * heightPx ) ) );
	m->pogoHertz = num( "pogoFrequency", 5.0f );
	m->pogoDampingRatio = num( "pogoDampingRatio", 0.8f );
	m->groundForce = num( "groundForce", 50.0f );
	m->pushBodies = opt ? b2p_getBoolField( L, opt, "pushBodies", true ) : true;
	m->minGroundNormalY = 0.7f;

	m->position = b2Pos{ b2p_toMeters( st, x ), b2p_toMeters( st, y ) };
	m->velocity = b2Vec2{ 0.0f, 0.0f };
	m->pogoVelocity = 0.0f;
	m->onGround = false;
	m->throttle = 0.0f;
	m->groundBody = b2_nullBodyId;
	m->planeCount = 0;
	m->lastX = x;
	m->lastY = y;

	lua_pushvalue( L, 1 );
	m->objRef = luaL_ref( L, LUA_REGISTRYINDEX );
	st->movers.push_back( m );

	MoverUD* ud = (MoverUD*)lua_newuserdata( L, sizeof( MoverUD ) );
	ud->serial = m->serial;
	lua_pushlightuserdata( L, (void*)&kMoverMt );
	lua_rawget( L, LUA_REGISTRYINDEX );
	lua_setmetatable( L, -2 );
	return 1;
}

void b2p_registerMover( lua_State* L, int libIndex )
{
	lua_pushlightuserdata( L, (void*)&kMoverMt );
	lua_newtable( L );
	lua_pushcfunction( L, MoverIndex );
	lua_setfield( L, -2, "__index" );
	lua_pushcfunction( L, MoverNewindex );
	lua_setfield( L, -2, "__newindex" );
	lua_pushcfunction( L, MoverToString );
	lua_setfield( L, -2, "__tostring" );
	lua_pushcfunction( L, MoverEq );
	lua_setfield( L, -2, "__eq" );
	lua_rawset( L, LUA_REGISTRYINDEX );

	lua_pushcfunction( L, newMover );
	lua_setfield( L, libIndex, "newMover" );
}

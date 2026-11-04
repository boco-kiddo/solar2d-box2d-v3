// plugin.box2dv3 — physics.addBody / removeBody, body <-> display object binding.
//
// Binding strategy
// ----------------
// Solar2D display objects are Lua tables whose shared metatable (Runtime._proxy) has C
// __index / __newindex functions that find the native object through the table's "_proxy"
// field, not through the metatable. When a body is added we give the object its own physics
// metatable: a copy of the original whose __index / __newindex first handle physics keys
// (obj.bodyType, obj.isAwake, obj:applyForce ...) and then delegate to the original functions.
// Writes to x / y / rotation are forwarded to Solar2D and then teleport the body, exactly like
// the built-in physics did. When Solar2D removes the object it resets the metatable to nil,
// which also removes ours.
#include "b2plugin.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

// registry keys (addresses are unique)
static char kBodiesKey = 0;   // obj -> lightuserdata(Body*)
static char kMtCacheKey = 0; // original metatable -> physics metatable (weak keys)
static char kPropsKey = 0;   // key -> property id (number) or method (function)

enum PropId
{
	kPropX = 1,
	kPropY,
	kPropRotation,
	kPropBodyType,
	kPropIsAwake,
	kPropAngularVelocity,
	kPropIsBodyActive,
	kPropIsBullet,
	kPropIsSleepingAllowed,
	kPropIsFixedRotation,
	kPropLinearDamping,
	kPropAngularDamping,
	kPropIsSensor,
	kPropMass,
	kPropGravityScale,
	// v3 additions (step 6)
	kPropIsFixedX,
	kPropIsFixedY,
	kPropSleepThreshold,
	kPropTangentSpeed,
	kPropRollingResistance,
};

static char kRecordsMt = 0; // metatable of the GC-owned element list used while parsing

static const char kStatic[] = "static";
static const char kDynamic[] = "dynamic";
static const char kKinematic[] = "kinematic";

// ---------------------------------------------------------------------------
// small helpers

static void PushRegistryTable( lua_State* L, char* key )
{
	lua_pushlightuserdata( L, (void*)key );
	lua_rawget( L, LUA_REGISTRYINDEX );
}

static bool IsDisplayObject( lua_State* L, int index )
{
	if ( !lua_istable( L, index ) )
	{
		return false;
	}
	lua_pushliteral( L, "_proxy" );
	lua_rawget( L, index );
	bool result = lua_isuserdata( L, -1 ) != 0;
	lua_pop( L, 1 );
	return result;
}

Body* b2p_toBody( lua_State* L, int objIndex )
{
	if ( !lua_istable( L, objIndex ) )
	{
		return nullptr;
	}
	objIndex = objIndex < 0 ? lua_gettop( L ) + objIndex + 1 : objIndex;
	PushRegistryTable( L, &kBodiesKey );
	lua_pushvalue( L, objIndex );
	lua_rawget( L, -2 );
	Body* body = (Body*)lua_touserdata( L, -1 );
	lua_pop( L, 2 );
	return ( body != nullptr && !body->destroyed ) ? body : nullptr;
}

static Body* CheckBody( lua_State* L, int index, const char* fname )
{
	Body* body = b2p_toBody( L, index );
	if ( body == nullptr )
	{
		luaL_error( L, "%s: object has no physics body (use ':' to call methods).", fname );
	}
	return body;
}

static void CheckUnlocked( lua_State* L, const PluginState* st, const char* what )
{
	if ( st->inStep )
	{
		luaL_error( L, "%s cannot be changed while the world is stepping.", what );
	}
}

static const char* BodyTypeName( b2BodyType t )
{
	switch ( t )
	{
		case b2_staticBody:
			return kStatic;
		case b2_kinematicBody:
			return kKinematic;
		default:
			return kDynamic;
	}
}

static b2BodyType ParseBodyType( const char* s )
{
	if ( s != nullptr )
	{
		if ( strcmp( s, kStatic ) == 0 )
		{
			return b2_staticBody;
		}
		if ( strcmp( s, kKinematic ) == 0 )
		{
			return b2_kinematicBody;
		}
	}
	return b2_dynamicBody;
}

// Pushes the value of the original __index for obj[key].
static void OrigGet( lua_State* L, Body* body, int objIndex, const char* key )
{
	lua_rawgeti( L, LUA_REGISTRYINDEX, body->origMtRef );
	lua_getfield( L, -1, "__index" );
	lua_remove( L, -2 );
	if ( lua_isfunction( L, -1 ) )
	{
		lua_pushvalue( L, objIndex );
		lua_pushstring( L, key );
		lua_call( L, 2, 1 );
	}
	else if ( lua_istable( L, -1 ) )
	{
		lua_getfield( L, -1, key );
		lua_remove( L, -2 );
	}
	else
	{
		lua_pop( L, 1 );
		lua_pushnil( L );
	}
}

// obj[key] = number through the original __newindex (bypasses teleport handling).
static void OrigSetNumber( lua_State* L, Body* body, int objIndex, const char* key, float value )
{
	lua_rawgeti( L, LUA_REGISTRYINDEX, body->origNewindexRef );
	lua_pushvalue( L, objIndex );
	lua_pushstring( L, key );
	lua_pushnumber( L, value );
	lua_call( L, 3, 0 );
}

// Moves the body to the display object's current x / y / rotation.
static void TeleportFromObject( lua_State* L, Body* body, int objIndex )
{
	PluginState* st = body->state;
	OrigGet( L, body, objIndex, "x" );
	OrigGet( L, body, objIndex, "y" );
	OrigGet( L, body, objIndex, "rotation" );
	float x = (float)lua_tonumber( L, -3 );
	float y = (float)lua_tonumber( L, -2 );
	float r = (float)lua_tonumber( L, -1 );
	lua_pop( L, 3 );

	b2Pos p = { b2p_toMeters( st, x ), b2p_toMeters( st, y ) };
	b2Body_SetTransform( body->id, p, b2p_makeRot( r * kDegToRad ) );
	body->lastRotation = r;
	if ( b2Body_GetType( body->id ) != b2_staticBody )
	{
		b2Body_SetAwake( body->id, true );
	}
}

// After a material change: wakes the bodies touching this one (a static conveyor belt changing speed
// must wake what rests on it) and drops their cached contact manifolds. Box2D reuses the manifold of a
// contact that did not move ("contact recycling") and only reads the shape materials when it is
// rebuilt; recomputing the mass of a dynamic body invalidates its cached contacts (same mass).
static void RefreshTouching( Body* body )
{
	if ( b2Body_GetType( body->id ) == b2_dynamicBody )
	{
		b2Body_UpdateMassFromShapes( body->id );
	}
	int cap = b2Body_GetContactCapacity( body->id );
	if ( cap <= 0 )
	{
		return;
	}
	std::vector<b2ContactData> contacts( (size_t)cap );
	int n = b2Body_GetContactData( body->id, contacts.data(), cap );
	for ( int i = 0; i < n; ++i )
	{
		b2BodyId a = b2Shape_GetBody( contacts[(size_t)i].shapeIdA );
		b2BodyId b = b2Shape_GetBody( contacts[(size_t)i].shapeIdB );
		b2BodyId pair[2] = { a, b };
		for ( b2BodyId other : pair )
		{
			if ( B2_ID_EQUALS( other, body->id ) || b2Body_GetType( other ) == b2_staticBody )
			{
				continue;
			}
			b2Body_SetAwake( other, true );
			if ( b2Body_GetType( other ) == b2_dynamicBody )
			{
				b2Body_UpdateMassFromShapes( other );
			}
		}
	}
}

// ---------------------------------------------------------------------------
// properties

// Pushes the property value; returns false if the key is not readable here.
static bool GetProp( lua_State* L, Body* body, int id )
{
	PluginState* st = body->state;
	b2BodyId bid = body->id;
	switch ( id )
	{
		case kPropBodyType:
			lua_pushstring( L, BodyTypeName( b2Body_GetType( bid ) ) );
			return true;
		case kPropIsAwake:
			lua_pushboolean( L, b2Body_IsAwake( bid ) );
			return true;
		case kPropAngularVelocity:
			lua_pushnumber( L, b2Body_GetAngularVelocity( bid ) * kRadToDeg );
			return true;
		case kPropIsBodyActive:
			lua_pushboolean( L, b2Body_IsEnabled( bid ) );
			return true;
		case kPropIsBullet:
			lua_pushboolean( L, b2Body_IsBullet( bid ) );
			return true;
		case kPropIsSleepingAllowed:
			lua_pushboolean( L, b2Body_IsSleepingEnabled( bid ) );
			return true;
		case kPropIsFixedRotation:
			lua_pushboolean( L, b2Body_GetMotionLocks( bid ).angularZ );
			return true;
		case kPropLinearDamping:
			lua_pushnumber( L, b2Body_GetLinearDamping( bid ) );
			return true;
		case kPropAngularDamping:
			lua_pushnumber( L, b2Body_GetAngularDamping( bid ) );
			return true;
		case kPropIsSensor:
		{
			// v2 returned nil here; we return whether every element is a sensor
			bool all = !body->elements.empty();
			for ( const ElementRecord& e : body->elements )
			{
				all = all && e.isSensor;
			}
			lua_pushboolean( L, all );
			return true;
		}
		case kPropMass:
			lua_pushnumber( L, b2Body_GetMass( bid ) );
			return true;
		case kPropGravityScale:
			lua_pushnumber( L, b2Body_GetGravityScale( bid ) );
			return true;
		case kPropIsFixedX:
			lua_pushboolean( L, b2Body_GetMotionLocks( bid ).linearX );
			return true;
		case kPropIsFixedY:
			lua_pushboolean( L, b2Body_GetMotionLocks( bid ).linearY );
			return true;
		case kPropSleepThreshold:
			lua_pushnumber( L, b2p_toPixels( st, b2Body_GetSleepThreshold( bid ) ) );
			return true;
		case kPropTangentSpeed:
			lua_pushnumber( L, body->elements.empty() ? 0.0 : b2p_toPixels( st, body->elements[0].tangentSpeed ) );
			return true;
		case kPropRollingResistance:
			lua_pushnumber( L, body->elements.empty() ? 0.0 : body->elements[0].rollingResistance );
			return true;
		default:
			return false;
	}
}

// Rebuilds every shape of the body from its element records (Box2D v3 cannot toggle sensors).
static void RebuildShapes( Body* body )
{
	b2p_destroyElementShapes( body->id, body->elements );
	for ( ElementRecord& rec : body->elements )
	{
		b2p_createElementShapes( body->id, rec );
	}
	b2Body_UpdateMassFromShapes( body->id );
	if ( body->preSolveEnabled )
	{
		b2p_applyPreSolveFlag( body, true );
	}
}

// Applies obj[key] = value; returns false if the key is not a physics property.
static bool SetProp( lua_State* L, Body* body, int id, int valueIndex )
{
	PluginState* st = body->state;
	b2BodyId bid = body->id;
	switch ( id )
	{
		case kPropBodyType:
			CheckUnlocked( L, st, "bodyType" );
			b2Body_SetType( bid, ParseBodyType( luaL_checkstring( L, valueIndex ) ) );
			return true;
		case kPropIsAwake:
			b2Body_SetAwake( bid, lua_toboolean( L, valueIndex ) != 0 );
			return true;
		case kPropAngularVelocity:
			b2Body_SetAngularVelocity( bid, (float)luaL_checknumber( L, valueIndex ) * kDegToRad );
			return true;
		case kPropIsBodyActive:
			CheckUnlocked( L, st, "isBodyActive" );
			if ( lua_toboolean( L, valueIndex ) )
			{
				b2Body_Enable( bid );
			}
			else
			{
				b2Body_Disable( bid );
			}
			return true;
		case kPropIsBullet:
			b2Body_SetBullet( bid, lua_toboolean( L, valueIndex ) != 0 );
			return true;
		case kPropIsSleepingAllowed:
			b2Body_EnableSleeping( bid, lua_toboolean( L, valueIndex ) != 0 );
			return true;
		case kPropIsFixedRotation:
		{
			b2MotionLocks locks = b2Body_GetMotionLocks( bid );
			locks.angularZ = lua_toboolean( L, valueIndex ) != 0;
			b2Body_SetMotionLocks( bid, locks );
			if ( locks.angularZ )
			{
				b2Body_SetAngularVelocity( bid, 0.0f );
			}
			return true;
		}
		case kPropLinearDamping:
			b2Body_SetLinearDamping( bid, (float)lua_tonumber( L, valueIndex ) );
			return true;
		case kPropAngularDamping:
			b2Body_SetAngularDamping( bid, (float)lua_tonumber( L, valueIndex ) );
			return true;
		case kPropIsSensor:
		{
			CheckUnlocked( L, st, "isSensor" );
			bool sensor = lua_toboolean( L, valueIndex ) != 0;
			bool changed = false;
			for ( ElementRecord& e : body->elements )
			{
				changed = changed || e.isSensor != sensor;
				e.isSensor = sensor;
			}
			if ( changed )
			{
				RebuildShapes( body );
			}
			return true;
		}
		case kPropGravityScale:
			b2Body_SetGravityScale( bid, (float)lua_tonumber( L, valueIndex ) );
			if ( b2Body_GetType( bid ) == b2_dynamicBody )
			{
				b2Body_SetAwake( bid, true );
			}
			return true;
		case kPropIsFixedX:
		case kPropIsFixedY:
		{
			b2MotionLocks locks = b2Body_GetMotionLocks( bid );
			bool on = lua_toboolean( L, valueIndex ) != 0;
			b2Vec2 v = b2Body_GetLinearVelocity( bid );
			if ( id == kPropIsFixedX )
			{
				locks.linearX = on;
				v.x = on ? 0.0f : v.x;
			}
			else
			{
				locks.linearY = on;
				v.y = on ? 0.0f : v.y;
			}
			b2Body_SetMotionLocks( bid, locks );
			b2Body_SetLinearVelocity( bid, v );
			return true;
		}
		case kPropSleepThreshold:
			b2Body_SetSleepThreshold( bid, b2p_toMeters( st, (float)luaL_checknumber( L, valueIndex ) ) );
			return true;
		case kPropTangentSpeed:
		case kPropRollingResistance:
		{
			CheckUnlocked( L, st, id == kPropTangentSpeed ? "tangentSpeed" : "rollingResistance" );
			float v = (float)luaL_checknumber( L, valueIndex );
			for ( ElementRecord& e : body->elements )
			{
				if ( id == kPropTangentSpeed )
				{
					e.tangentSpeed = b2p_toMeters( st, v );
				}
				else
				{
					e.rollingResistance = v < 0.0f ? 0.0f : v;
				}
			}
			b2p_applyElementMaterials( body->id, body->elements );
			if ( b2Body_GetType( bid ) != b2_staticBody )
			{
				b2Body_SetAwake( bid, true );
			}
			RefreshTouching( body );
			return true;
		}
		default:
			return false; // read-only (mass) or unknown: Solar2D stores it in the table like v2
	}
}

// ---------------------------------------------------------------------------
// methods

// obj:setLinearVelocity( vx, vy )   pixels / second
static int m_setLinearVelocity( lua_State* L )
{
	Body* body = CheckBody( L, 1, "setLinearVelocity" );
	PluginState* st = body->state;
	b2Vec2 v = { b2p_toMeters( st, (float)luaL_checknumber( L, 2 ) ),
				 b2p_toMeters( st, (float)luaL_checknumber( L, 3 ) ) };
	b2Body_SetLinearVelocity( body->id, v );
	return 0;
}

// obj:getLinearVelocity() -> vx, vy   pixels / second
static int m_getLinearVelocity( lua_State* L )
{
	Body* body = CheckBody( L, 1, "getLinearVelocity" );
	PluginState* st = body->state;
	b2Vec2 v = b2Body_GetLinearVelocity( body->id );
	lua_pushnumber( L, b2p_toPixels( st, v.x ) );
	lua_pushnumber( L, b2p_toPixels( st, v.y ) );
	return 2;
}

static b2Pos PixelsToPos( const PluginState* st, float x, float y )
{
	return b2Pos{ b2p_toMeters( st, x ), b2p_toMeters( st, y ) };
}

// obj:applyForce( fx, fy, px, py )   force in Newtons, point in content pixels (v2)
static int m_applyForce( lua_State* L )
{
	Body* body = CheckBody( L, 1, "applyForce" );
	b2Vec2 force = { (float)lua_tonumber( L, 2 ), (float)lua_tonumber( L, 3 ) };
	b2Body_ApplyForce( body->id, force, PixelsToPos( body->state, (float)lua_tonumber( L, 4 ), (float)lua_tonumber( L, 5 ) ), true );
	return 0;
}

// obj:applyLinearImpulse( ix, iy, px, py )   impulse in N*s, point in content pixels (v2)
static int m_applyLinearImpulse( lua_State* L )
{
	Body* body = CheckBody( L, 1, "applyLinearImpulse" );
	b2Vec2 impulse = { (float)lua_tonumber( L, 2 ), (float)lua_tonumber( L, 3 ) };
	b2Body_ApplyLinearImpulse( body->id, impulse,
							   PixelsToPos( body->state, (float)lua_tonumber( L, 4 ), (float)lua_tonumber( L, 5 ) ), true );
	return 0;
}

// obj:applyTorque( t )   N*m
static int m_applyTorque( lua_State* L )
{
	Body* body = CheckBody( L, 1, "applyTorque" );
	b2Body_ApplyTorque( body->id, (float)lua_tonumber( L, 2 ), true );
	return 0;
}

// obj:applyAngularImpulse( i )   N*m*s (a real impulse; Solar2D's v2 applied a torque here)
static int m_applyAngularImpulse( lua_State* L )
{
	Body* body = CheckBody( L, 1, "applyAngularImpulse" );
	b2Body_ApplyAngularImpulse( body->id, (float)lua_tonumber( L, 2 ), true );
	return 0;
}

// obj:applyForceToCenter( fx, fy )   Newtons, at the center of mass (v3)
static int m_applyForceToCenter( lua_State* L )
{
	Body* body = CheckBody( L, 1, "applyForceToCenter" );
	b2Vec2 force = { (float)luaL_checknumber( L, 2 ), (float)luaL_checknumber( L, 3 ) };
	b2Body_ApplyForceToCenter( body->id, force, true );
	return 0;
}

// obj:applyLinearImpulseToCenter( ix, iy )   N*s, at the center of mass (v3)
static int m_applyLinearImpulseToCenter( lua_State* L )
{
	Body* body = CheckBody( L, 1, "applyLinearImpulseToCenter" );
	b2Vec2 impulse = { (float)luaL_checknumber( L, 2 ), (float)luaL_checknumber( L, 3 ) };
	b2Body_ApplyLinearImpulseToCenter( body->id, impulse, true );
	return 0;
}

// obj:setTargetTransform( x, y, [rotation], [time] )
// Kinematic bodies: sets the velocity that moves the body to (x, y, rotation) in `time` seconds
// (default: one frame), so it pushes dynamic bodies properly instead of teleporting through them.
static int m_setTargetTransform( lua_State* L )
{
	Body* body = CheckBody( L, 1, "setTargetTransform" );
	PluginState* st = body->state;
	float x = (float)luaL_checknumber( L, 2 );
	float y = (float)luaL_checknumber( L, 3 );
	float rot = lua_isnumber( L, 4 ) ? (float)lua_tonumber( L, 4 ) : body->lastRotation;
	float dt = lua_isnumber( L, 5 ) ? (float)lua_tonumber( L, 5 ) : st->frameInterval;
	if ( dt <= 0.0f )
	{
		return luaL_argerror( L, 5, "time must be > 0" );
	}
	b2WorldTransform target;
	target.p = PixelsToPos( st, x, y );
	target.q = b2p_makeRot( rot * kDegToRad );
	b2Body_SetTargetTransform( body->id, target, dt, true );
	return 0;
}

// obj:resetMassData()
static int m_resetMassData( lua_State* L )
{
	Body* body = CheckBody( L, 1, "resetMassData" );
	CheckUnlocked( L, body->state, "resetMassData" );
	b2Body_UpdateMassFromShapes( body->id );
	return 0;
}

// obj:getMassWorldCenter() -> x, y   pixels
static int m_getMassWorldCenter( lua_State* L )
{
	Body* body = CheckBody( L, 1, "getMassWorldCenter" );
	b2Pos c = b2Body_GetWorldCenter( body->id );
	lua_pushnumber( L, b2p_toPixels( body->state, (float)c.x ) );
	lua_pushnumber( L, b2p_toPixels( body->state, (float)c.y ) );
	return 2;
}

// obj:getMassLocalCenter() -> x, y   pixels
static int m_getMassLocalCenter( lua_State* L )
{
	Body* body = CheckBody( L, 1, "getMassLocalCenter" );
	b2Vec2 c = b2Body_GetLocalCenter( body->id );
	lua_pushnumber( L, b2p_toPixels( body->state, c.x ) );
	lua_pushnumber( L, b2p_toPixels( body->state, c.y ) );
	return 2;
}

// obj:getWorldVector( lx, ly ) -> wx, wy   rotates a local vector into content space (same units in and out)
static int m_getWorldVector( lua_State* L )
{
	Body* body = CheckBody( L, 1, "getWorldVector" );
	b2Vec2 lv = { (float)lua_tonumber( L, 2 ), (float)lua_tonumber( L, 3 ) };
	b2Vec2 wv = b2Body_GetWorldVector( body->id, lv );
	lua_pushnumber( L, wv.x );
	lua_pushnumber( L, wv.y );
	return 2;
}

// obj:getInertia()   rotational inertia about the center of mass, kg*m^2
static int m_getInertia( lua_State* L )
{
	Body* body = CheckBody( L, 1, "getInertia" );
	lua_pushnumber( L, b2Body_GetRotationalInertia( body->id ) );
	return 1;
}

// obj:getLinearVelocityFromWorldPoint( x, y ) -> vx, vy   point in pixels, velocity in pixels / second
static int m_getLinearVelocityFromWorldPoint( lua_State* L )
{
	Body* body = CheckBody( L, 1, "getLinearVelocityFromWorldPoint" );
	PluginState* st = body->state;
	b2Vec2 v = b2Body_GetWorldPointVelocity( body->id, PixelsToPos( st, (float)lua_tonumber( L, 2 ), (float)lua_tonumber( L, 3 ) ) );
	lua_pushnumber( L, b2p_toPixels( st, v.x ) );
	lua_pushnumber( L, b2p_toPixels( st, v.y ) );
	return 2;
}

// obj:getLinearVelocityFromLocalPoint( x, y ) -> vx, vy   same units as above
static int m_getLinearVelocityFromLocalPoint( lua_State* L )
{
	Body* body = CheckBody( L, 1, "getLinearVelocityFromLocalPoint" );
	PluginState* st = body->state;
	b2Vec2 lp = { b2p_toMeters( st, (float)lua_tonumber( L, 2 ) ), b2p_toMeters( st, (float)lua_tonumber( L, 3 ) ) };
	b2Vec2 v = b2Body_GetLocalPointVelocity( body->id, lp );
	lua_pushnumber( L, b2p_toPixels( st, v.x ) );
	lua_pushnumber( L, b2p_toPixels( st, v.y ) );
	return 2;
}

// obj:translate( dx, dy ) / obj:rotate( deg ): call Solar2D then move the body along.
static int CallOrigMethodThenTeleport( lua_State* L, const char* name )
{
	Body* body = b2p_toBody( L, 1 );
	if ( body == nullptr )
	{
		return luaL_error( L, "%s: invalid object", name );
	}
	CheckUnlocked( L, body->state, name );
	int nargs = lua_gettop( L );
	OrigGet( L, body, 1, name );
	for ( int i = 1; i <= nargs; ++i )
	{
		lua_pushvalue( L, i );
	}
	lua_call( L, nargs, 0 );
	TeleportFromObject( L, body, 1 );
	return 0;
}

// obj:removeSelf() / display.remove( obj )
// Solar2D only orphans the object here and dispatches "finalize" later, when it collects
// unreachable objects. Destroy the body right away so joints, queries and collisions see the
// removal immediately (v2 destroyed it at the end of the next step at the latest).
static int m_removeSelf( lua_State* L )
{
	if ( !lua_istable( L, 1 ) || !lua_getmetatable( L, 1 ) )
	{
		return 0;
	}
	lua_pushliteral( L, "__b2orig" );
	lua_rawget( L, -2 );
	lua_getfield( L, -1, "__index" );
	if ( lua_isfunction( L, -1 ) )
	{
		lua_pushvalue( L, 1 );
		lua_pushliteral( L, "removeSelf" );
		lua_call( L, 2, 1 ); // Solar2D's removeSelf
	}
	else
	{
		lua_pushnil( L );
	}
	int orig = lua_gettop( L );

	Body* body = b2p_toBody( L, 1 );
	if ( body != nullptr )
	{
		b2p_destroyBody( L, body, false ); // deferred if the world is stepping
	}

	if ( lua_isfunction( L, orig ) )
	{
		int nargs = lua_gettop( L ) >= 1 ? 1 : 0;
		lua_pushvalue( L, orig );
		lua_pushvalue( L, 1 );
		lua_call( L, nargs, 0 );
	}
	return 0;
}

static int m_translate( lua_State* L )
{
	return CallOrigMethodThenTeleport( L, "translate" );
}

static int m_rotate( lua_State* L )
{
	return CallOrigMethodThenTeleport( L, "rotate" );
}

// ---------------------------------------------------------------------------
// physics metatable

// upvalues: 1 original __index, 2 bodies table, 3 props table
static int ProxyIndex( lua_State* L )
{
	if ( lua_type( L, 2 ) == LUA_TSTRING )
	{
		lua_pushvalue( L, 2 );
		lua_rawget( L, lua_upvalueindex( 3 ) );
		int t = lua_type( L, -1 );
		if ( t == LUA_TFUNCTION )
		{
			return 1; // physics method
		}
		if ( t == LUA_TNUMBER )
		{
			int id = (int)lua_tointeger( L, -1 );
			lua_pop( L, 1 );
			lua_pushvalue( L, 1 );
			lua_rawget( L, lua_upvalueindex( 2 ) );
			Body* body = (Body*)lua_touserdata( L, -1 );
			lua_pop( L, 1 );
			if ( body != nullptr && !body->destroyed && GetProp( L, body, id ) )
			{
				return 1;
			}
		}
		else
		{
			lua_pop( L, 1 );
		}
	}

	// delegate to the original __index
	lua_pushvalue( L, lua_upvalueindex( 1 ) );
	if ( lua_isfunction( L, -1 ) )
	{
		lua_pushvalue( L, 1 );
		lua_pushvalue( L, 2 );
		lua_call( L, 2, 1 );
		return 1;
	}
	if ( lua_istable( L, -1 ) )
	{
		lua_pushvalue( L, 2 );
		lua_gettable( L, -2 );
		return 1;
	}
	lua_pushnil( L );
	return 1;
}

// upvalues: 1 original __newindex, 2 bodies table, 3 props table
static int ProxyNewindex( lua_State* L )
{
	int id = 0;
	Body* body = nullptr;
	if ( lua_type( L, 2 ) == LUA_TSTRING )
	{
		lua_pushvalue( L, 2 );
		lua_rawget( L, lua_upvalueindex( 3 ) );
		if ( lua_type( L, -1 ) == LUA_TNUMBER )
		{
			id = (int)lua_tointeger( L, -1 );
			lua_pushvalue( L, 1 );
			lua_rawget( L, lua_upvalueindex( 2 ) );
			body = (Body*)lua_touserdata( L, -1 );
			lua_pop( L, 1 );
			if ( body != nullptr && body->destroyed )
			{
				body = nullptr;
			}
		}
		lua_pop( L, 1 );
	}

	if ( body != nullptr && id >= kPropBodyType )
	{
		if ( SetProp( L, body, id, 3 ) )
		{
			return 0;
		}
	}

	// delegate to the original __newindex
	lua_pushvalue( L, lua_upvalueindex( 1 ) );
	if ( lua_isfunction( L, -1 ) )
	{
		lua_pushvalue( L, 1 );
		lua_pushvalue( L, 2 );
		lua_pushvalue( L, 3 );
		lua_call( L, 3, 0 );
	}
	else
	{
		lua_pop( L, 1 );
		lua_pushvalue( L, 2 );
		lua_pushvalue( L, 3 );
		lua_rawset( L, 1 );
	}

	// x / y / rotation written by the user: move the body (v2 behaviour)
	if ( body != nullptr && ( id == kPropX || id == kPropY || id == kPropRotation ) )
	{
		CheckUnlocked( L, body->state, "position" );
		TeleportFromObject( L, body, 1 );
	}
	return 0;
}

// Pushes the physics metatable for the metatable at index mtIndex (cached).
static void PushPhysicsMetatable( lua_State* L, int mtIndex )
{
	PushRegistryTable( L, &kMtCacheKey ); // cache
	lua_pushvalue( L, mtIndex );
	lua_rawget( L, -2 );
	if ( lua_istable( L, -1 ) )
	{
		lua_remove( L, -2 );
		return;
	}
	lua_pop( L, 1 );

	lua_newtable( L ); // cache, pmt
	int pmt = lua_gettop( L );

	// copy every field of the original metatable
	lua_pushnil( L );
	while ( lua_next( L, mtIndex ) != 0 )
	{
		lua_pushvalue( L, -2 );
		lua_insert( L, -2 );
		lua_rawset( L, pmt );
	}

	lua_getfield( L, mtIndex, "__index" );
	PushRegistryTable( L, &kBodiesKey );
	PushRegistryTable( L, &kPropsKey );
	lua_pushcclosure( L, ProxyIndex, 3 );
	lua_setfield( L, pmt, "__index" );

	lua_getfield( L, mtIndex, "__newindex" );
	PushRegistryTable( L, &kBodiesKey );
	PushRegistryTable( L, &kPropsKey );
	lua_pushcclosure( L, ProxyNewindex, 3 );
	lua_setfield( L, pmt, "__newindex" );

	lua_pushboolean( L, 1 );
	lua_setfield( L, pmt, "__box2d" );
	lua_pushvalue( L, mtIndex );
	lua_setfield( L, pmt, "__b2orig" );

	// cache[mt] = pmt
	lua_pushvalue( L, mtIndex );
	lua_pushvalue( L, pmt );
	lua_rawset( L, -4 );

	lua_remove( L, -2 ); // remove cache
}

// obj:addEventListener / removeEventListener( "finalize", finalizeListener ) via original __index
static void FinalizeListener( lua_State* L, Body* body, int objIndex, const char* method )
{
	OrigGet( L, body, objIndex, method );
	if ( lua_isfunction( L, -1 ) )
	{
		lua_pushvalue( L, objIndex );
		lua_pushliteral( L, "finalize" );
		lua_rawgeti( L, LUA_REGISTRYINDEX, body->state->finalizeRef );
		lua_call( L, 3, 0 );
	}
	else
	{
		lua_pop( L, 1 );
	}
}

// Display object was removed (display.remove / removeSelf / parent removed).
static int OnFinalize( lua_State* L )
{
	if ( !lua_istable( L, 1 ) )
	{
		return 0;
	}
	lua_getfield( L, 1, "target" );
	Body* body = b2p_toBody( L, -1 );
	lua_pop( L, 1 );
	if ( body != nullptr )
	{
		b2p_destroyBody( L, body, false );
	}
	return 0;
}

// ---------------------------------------------------------------------------
// destruction

static void FreeBody( lua_State* L, Body* body, bool luaAlive )
{
	PluginState* st = body->state;

	// swap-remove from the body list
	int last = (int)st->bodies.size() - 1;
	if ( body->index >= 0 && body->index <= last )
	{
		Body* moved = st->bodies[last];
		st->bodies[body->index] = moved;
		moved->index = body->index;
		st->bodies.pop_back();
	}

	if ( luaAlive )
	{
		luaL_unref( L, LUA_REGISTRYINDEX, body->objRef );
		luaL_unref( L, LUA_REGISTRYINDEX, body->origMtRef );
		luaL_unref( L, LUA_REGISTRYINDEX, body->origNewindexRef );
	}
	delete body;
}

void b2p_destroyBody( lua_State* L, Body* body, bool restoreObject )
{
	if ( body->destroyed )
	{
		return;
	}
	if ( body->state->inStep )
	{
		// e.g. display.remove() inside a "preCollision" listener: Box2D is locked, finish after the step
		if ( !body->pendingDestroy )
		{
			body->pendingDestroy = true;
			body->state->pendingDestroy.push_back( body );
		}
		return;
	}
	body->destroyed = true;

	lua_rawgeti( L, LUA_REGISTRYINDEX, body->objRef ); // obj
	int obj = lua_gettop( L );

	// bodies[obj] = nil
	PushRegistryTable( L, &kBodiesKey );
	lua_pushvalue( L, obj );
	lua_pushnil( L );
	lua_rawset( L, -3 );
	lua_pop( L, 1 );

	if ( restoreObject && lua_getmetatable( L, obj ) )
	{
		lua_pop( L, 1 );
		// Object still alive: give it back its original metatable and drop our listener.
		FinalizeListener( L, body, obj, "removeEventListener" );
		lua_rawgeti( L, LUA_REGISTRYINDEX, body->origMtRef );
		lua_setmetatable( L, obj );
	}
	lua_pop( L, 1 ); // obj

	if ( b2Body_IsValid( body->id ) )
	{
		b2DestroyBody( body->id );
	}
	FreeBody( L, body, true );
}

void b2p_destroyAllBodies( lua_State* L, bool luaAlive )
{
	PluginState* st = b2p_state( L );
	st->pendingDestroy.clear();
	while ( !st->bodies.empty() )
	{
		Body* body = st->bodies.back();
		if ( luaAlive )
		{
			b2p_destroyBody( L, body, true );
		}
		else
		{
			body->destroyed = true;
			FreeBody( L, body, false ); // the world (and its bodies) is destroyed by the caller
		}
	}
}

// ---------------------------------------------------------------------------
// per step synchronisation: body transform -> display object

void b2p_writeTransform( lua_State* L, Body* body )
{
	PluginState* st = body->state;
	if ( body->destroyed || !b2Body_IsValid( body->id ) )
	{
		return;
	}
	int top = lua_gettop( L );
	lua_rawgeti( L, LUA_REGISTRYINDEX, body->objRef );
	int obj = lua_gettop( L );
	if ( lua_getmetatable( L, obj ) )
	{
		lua_pop( L, 1 );
		b2WorldTransform t = b2Body_GetTransform( body->id );
		float angle = b2p_rotAngle( t.q ) * kRadToDeg;
		float delta = fmodf( angle - body->lastRotation, 360.0f );
		if ( delta > 180.0f )
			delta -= 360.0f;
		else if ( delta < -180.0f )
			delta += 360.0f;
		body->lastRotation += delta;
		OrigSetNumber( L, body, obj, "x", b2p_toPixels( st, (float)t.p.x ) );
		OrigSetNumber( L, body, obj, "y", b2p_toPixels( st, (float)t.p.y ) );
		OrigSetNumber( L, body, obj, "rotation", body->lastRotation );
	}
	lua_settop( L, top );
}

void b2p_syncBodies( lua_State* L, PluginState* st )
{
	b2BodyEvents events = b2World_GetBodyEvents( st->world );
	if ( events.moveCount == 0 )
	{
		return;
	}

	std::vector<Body*> removed;
	int top = lua_gettop( L );
	for ( int i = 0; i < events.moveCount; ++i )
	{
		const b2BodyMoveEvent& e = events.moveEvents[i];
		Body* body = (Body*)e.userData;
		if ( body == nullptr || body->destroyed || body->pendingDestroy )
		{
			continue;
		}

		lua_rawgeti( L, LUA_REGISTRYINDEX, body->objRef );
		int obj = lua_gettop( L );
		if ( !lua_getmetatable( L, obj ) )
		{
			// Solar2D removed the object without a finalize event reaching us.
			removed.push_back( body );
			lua_settop( L, top );
			continue;
		}
		lua_pop( L, 1 );

		float angle = b2p_rotAngle( e.transform.q ) * kRadToDeg;
		float delta = fmodf( angle - body->lastRotation, 360.0f );
		if ( delta > 180.0f )
		{
			delta -= 360.0f;
		}
		else if ( delta < -180.0f )
		{
			delta += 360.0f;
		}
		body->lastRotation += delta; // keep rotation cumulative like Box2D v2 did

		OrigSetNumber( L, body, obj, "x", b2p_toPixels( st, (float)e.transform.p.x ) );
		OrigSetNumber( L, body, obj, "y", b2p_toPixels( st, (float)e.transform.p.y ) );
		OrigSetNumber( L, body, obj, "rotation", body->lastRotation );
		lua_settop( L, top );
	}

	for ( Body* body : removed )
	{
		b2p_destroyBody( L, body, false );
	}
}

// ---------------------------------------------------------------------------
// physics.addBody

// GC-owned holder for the element list while parsing: Lua errors raised by the parser never leak it.
struct RecordsHolder
{
	std::vector<ElementRecord>* records;
};

static int RecordsHolderGC( lua_State* L )
{
	RecordsHolder* h = (RecordsHolder*)lua_touserdata( L, 1 );
	delete h->records;
	h->records = nullptr;
	return 0;
}

// physics.addBody( obj, [bodyType], [element1, element2, ...] ) -> true
int b2p_addBody( lua_State* L )
{
	PluginState* st = b2p_requireWorld( L, "physics.addBody()" );
	CheckUnlocked( L, st, "physics.addBody()" );

	if ( !IsDisplayObject( L, 1 ) )
	{
		luaL_error( L, "physics.addBody() requires a display object as the first argument." );
	}
	if ( b2p_toBody( L, 1 ) != nullptr )
	{
		b2p_warn( L, "physics.addBody(): object already has a body." );
		lua_pushboolean( L, 0 );
		return 1;
	}
	if ( !lua_getmetatable( L, 1 ) )
	{
		luaL_error( L, "physics.addBody(): display object has no metatable." );
	}
	int origMt = lua_gettop( L );

	int nargs = origMt - 1;
	int first = 2;
	b2BodyType type = b2_dynamicBody;
	if ( lua_type( L, 2 ) == LUA_TSTRING )
	{
		type = ParseBodyType( lua_tostring( L, 2 ) );
		first = 3;
	}

	int tableCount = 0;
	for ( int i = first; i <= nargs; ++i )
	{
		if ( lua_istable( L, i ) )
		{
			++tableCount;
		}
		else if ( !lua_isnil( L, i ) )
		{
			luaL_error( L, "physics.addBody(): argument #%d must be a table of body element parameters.", i );
		}
	}

	// Parse everything first (may raise errors); nothing is created in the world yet.
	RecordsHolder* holder = (RecordsHolder*)lua_newuserdata( L, sizeof( RecordsHolder ) );
	holder->records = new std::vector<ElementRecord>();
	lua_pushlightuserdata( L, (void*)&kRecordsMt );
	lua_rawget( L, LUA_REGISTRYINDEX );
	lua_setmetatable( L, -2 );
	int holderIndex = lua_gettop( L );

	ObjectGeometry geom = b2p_readGeometry( L, 1 );
	bool forceStatic = false;
	if ( tableCount == 0 )
	{
		b2p_parseElement( L, st, 1, 0, type, geom, *holder->records, &forceStatic );
	}
	else
	{
		for ( int i = first; i <= nargs; ++i )
		{
			if ( lua_istable( L, i ) )
			{
				b2p_parseElement( L, st, 1, i, type, geom, *holder->records, &forceStatic );
			}
		}
	}
	if ( forceStatic )
	{
		type = b2_staticBody; // v2: line objects are always static
	}

	float x = b2p_getNumberField( L, 1, "x", 0.0f );
	float y = b2p_getNumberField( L, 1, "y", 0.0f );
	float rotation = b2p_getNumberField( L, 1, "rotation", 0.0f );

	// ---- no Lua errors past this point until the body is registered

	Body* body = new Body();
	body->state = st;
	body->serial = ++st->nextSerial;
	body->destroyed = false;
	body->pendingDestroy = false;
	body->preSolveEnabled = false;
	body->wantPost = false;
	body->lastRotation = rotation;
	body->elements.swap( *holder->records );

	b2BodyDef bd = b2DefaultBodyDef();
	bd.type = type;
	bd.position = b2Pos{ b2p_toMeters( st, x ), b2p_toMeters( st, y ) };
	bd.rotation = b2p_makeRot( rotation * kDegToRad );
	bd.sleepThreshold = st->sleepThreshold;
	bd.userData = body;
	body->id = b2CreateBody( st->world, &bd );

	for ( ElementRecord& rec : body->elements )
	{
		b2p_createElementShapes( body->id, rec );
	}
	b2Body_UpdateMassFromShapes( body->id );
	if ( st->runtimePre )
	{
		b2p_applyPreSolveFlag( body, true );
	}
	lua_remove( L, holderIndex );

	// register: refs, bodies[obj] = body, physics metatable, finalize listener
	lua_pushvalue( L, 1 );
	body->objRef = luaL_ref( L, LUA_REGISTRYINDEX );
	lua_pushvalue( L, origMt );
	body->origMtRef = luaL_ref( L, LUA_REGISTRYINDEX );
	lua_getfield( L, origMt, "__newindex" );
	body->origNewindexRef = luaL_ref( L, LUA_REGISTRYINDEX );

	body->index = (int)st->bodies.size();
	st->bodies.push_back( body );

	PushRegistryTable( L, &kBodiesKey );
	lua_pushvalue( L, 1 );
	lua_pushlightuserdata( L, body );
	lua_rawset( L, -3 );
	lua_pop( L, 1 );

	PushPhysicsMetatable( L, origMt );
	lua_setmetatable( L, 1 );

	FinalizeListener( L, body, 1, "addEventListener" );

	lua_pushboolean( L, 1 );
	return 1;
}

// physics.removeBody( obj ) -> boolean
int b2p_removeBody( lua_State* L )
{
	PluginState* st = b2p_requireWorld( L, "physics.removeBody()" );
	CheckUnlocked( L, st, "physics.removeBody()" );
	Body* body = b2p_toBody( L, 1 );
	if ( body == nullptr )
	{
		lua_pushboolean( L, 0 );
		return 1;
	}
	b2p_destroyBody( L, body, true );
	lua_pushboolean( L, 1 );
	return 1;
}

// ---------------------------------------------------------------------------

void b2p_initBodyModule( lua_State* L )
{
	PluginState* st = b2p_state( L );

	// bodies: obj -> Body*
	lua_pushlightuserdata( L, (void*)&kBodiesKey );
	lua_newtable( L );
	lua_rawset( L, LUA_REGISTRYINDEX );

	// metatable cache with weak keys
	lua_pushlightuserdata( L, (void*)&kMtCacheKey );
	lua_newtable( L );
	lua_newtable( L );
	lua_pushliteral( L, "k" );
	lua_setfield( L, -2, "__mode" );
	lua_setmetatable( L, -2 );
	lua_rawset( L, LUA_REGISTRYINDEX );

	// props: name -> id / method
	lua_pushlightuserdata( L, (void*)&kPropsKey );
	lua_newtable( L );
	struct
	{
		const char* name;
		int id;
	} props[] = {
		{ "x", kPropX },
		{ "y", kPropY },
		{ "rotation", kPropRotation },
		{ "bodyType", kPropBodyType },
		{ "isAwake", kPropIsAwake },
		{ "angularVelocity", kPropAngularVelocity },
		{ "isBodyActive", kPropIsBodyActive },
		{ "isBullet", kPropIsBullet },
		{ "isSleepingAllowed", kPropIsSleepingAllowed },
		{ "isFixedRotation", kPropIsFixedRotation },
		{ "linearDamping", kPropLinearDamping },
		{ "angularDamping", kPropAngularDamping },
		{ "isSensor", kPropIsSensor },
		{ "mass", kPropMass },
		{ "gravityScale", kPropGravityScale },
		{ "isFixedX", kPropIsFixedX },
		{ "isFixedY", kPropIsFixedY },
		{ "sleepThreshold", kPropSleepThreshold },
		{ "tangentSpeed", kPropTangentSpeed },
		{ "rollingResistance", kPropRollingResistance },
	};
	for ( auto& p : props )
	{
		lua_pushinteger( L, p.id );
		lua_setfield( L, -2, p.name );
	}
	const luaL_Reg methods[] = {
		{ "setLinearVelocity", m_setLinearVelocity },
		{ "getLinearVelocity", m_getLinearVelocity },
		{ "applyForce", m_applyForce },
		{ "applyTorque", m_applyTorque },
		{ "applyLinearImpulse", m_applyLinearImpulse },
		{ "applyAngularImpulse", m_applyAngularImpulse },
		{ "applyForceToCenter", m_applyForceToCenter },
		{ "applyLinearImpulseToCenter", m_applyLinearImpulseToCenter },
		{ "setTargetTransform", m_setTargetTransform },
		{ "resetMassData", m_resetMassData },
		{ "getMassWorldCenter", m_getMassWorldCenter },
		{ "getMassLocalCenter", m_getMassLocalCenter },
		{ "getWorldVector", m_getWorldVector },
		{ "getInertia", m_getInertia },
		{ "getLinearVelocityFromWorldPoint", m_getLinearVelocityFromWorldPoint },
		{ "getLinearVelocityFromLocalPoint", m_getLinearVelocityFromLocalPoint },
		{ "translate", m_translate },
		{ "removeSelf", m_removeSelf },
		{ "rotate", m_rotate },
		{ nullptr, nullptr },
	};
	for ( const luaL_Reg* m = methods; m->name; ++m )
	{
		lua_pushcfunction( L, m->func );
		lua_setfield( L, -2, m->name );
	}
	lua_rawset( L, LUA_REGISTRYINDEX );

	lua_pushcfunction( L, OnFinalize );
	st->finalizeRef = luaL_ref( L, LUA_REGISTRYINDEX );

	lua_pushlightuserdata( L, (void*)&kRecordsMt );
	lua_newtable( L );
	lua_pushcfunction( L, RecordsHolderGC );
	lua_setfield( L, -2, "__gc" );
	lua_rawset( L, LUA_REGISTRYINDEX );

	b2p_installDisplayHooks( L );
}

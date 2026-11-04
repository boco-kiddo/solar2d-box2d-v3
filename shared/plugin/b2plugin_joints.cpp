// plugin.box2dv3 — physics.newJoint() with the v2 joint types and properties, built on Box2D v3:
//
//   v2 type    Box2D v3
//   "pivot"    revolute joint
//   "distance" distance joint (rigid; spring when joint.frequency > 0)
//   "piston"   prismatic joint
//   "wheel"    wheel joint
//   "weld"     weld joint
//   "friction" motor joint with zero target velocity (its velocity constraint acts as friction)
//   "rope"     distance joint with only a maximum length
//   "touch"    motor joint to a hidden kinematic body that follows joint:setTarget()
//   "motor"    (new) motor joint: drives object2 to an offset / velocity relative to object1
//   "pulley", "gear": not available in Box2D v3 (error)
//
// Every joint (new): joint.breakForce (N) / joint.breakTorque (N*m). When the joint's constraint force
// exceeds them, a "jointBreak" event is dispatched (object1 -> object2 -> Runtime, like "collision")
// after the step, then the joint is removed.
//
// Units: pixels for lengths, degrees for angles, pixels / second and degrees / second for speeds,
// Newtons and N*m for forces and torques.
#include "b2plugin.h"

#include <float.h>
#include <math.h>
#include <string.h>
#include <unordered_set>

static char kJointMt = 0;
static char kJointsKey = 0;	   // weak-keyed set of every joint userdata (enumeration after a restore)
static char kBreakableKey = 0; // strong set of joints with a break threshold (kept alive for the event)

enum JointKind
{
	kJointPivot,
	kJointDistance,
	kJointPiston,
	kJointWheel,
	kJointWeld,
	kJointFriction,
	kJointRope,
	kJointTouch,
	kJointMotor,
};

static const char* const kJointNames[] = { "pivot", "distance", "piston", "wheel", "weld", "friction", "rope", "touch",
										   "motor" };

static uint64_t JointKey( b2JointId id )
{
	return ( (uint64_t)(uint32_t)id.index1 << 32 ) | ( (uint64_t)id.world0 << 16 ) | id.generation;
}

struct JointUD
{
	b2JointId id;
	int kind;
	uint32_t epoch;
	b2BodyId helper;	  // touch: kinematic body at the target
	b2Vec2 target;		  // touch: meters
	float referenceAngle; // degrees (v2 reports the angle between the bodies at creation)
	float ropeMaxLength;  // meters
};

// ---------------------------------------------------------------------------
// helpers

static PluginState* State( lua_State* L )
{
	return b2p_state( L );
}

static bool IsLive( const PluginState* st, const JointUD* j )
{
	return st != nullptr && B2_IS_NON_NULL( st->world ) && j->epoch == st->worldEpoch && b2Joint_IsValid( j->id );
}

static float Px( const PluginState* st, float m )
{
	return m * st->scale;
}

static float M( const PluginState* st, float px )
{
	return px / st->scale;
}

static float AngleDeg( b2BodyId b )
{
	return b2p_rotAngle( b2Body_GetRotation( b ) ) * kRadToDeg;
}

static b2Vec2 WorldAnchor( b2JointId id, bool a )
{
	b2Transform frame = a ? b2Joint_GetLocalFrameA( id ) : b2Joint_GetLocalFrameB( id );
	b2BodyId body = a ? b2Joint_GetBodyA( id ) : b2Joint_GetBodyB( id );
	b2Pos p = b2Body_GetWorldPoint( body, frame.p );
	return b2Vec2{ (float)p.x, (float)p.y };
}

// world axis of joints whose axis is frame A's x axis (prismatic, wheel)
static b2Vec2 WorldAxis( b2JointId id )
{
	b2Transform frame = b2Joint_GetLocalFrameA( id );
	b2Rot q = b2MulRot( b2Body_GetRotation( b2Joint_GetBodyA( id ) ), frame.q );
	return b2Rot_GetXAxis( q );
}

static float WheelTranslation( b2JointId id )
{
	b2Vec2 d = b2Sub( WorldAnchor( id, false ), WorldAnchor( id, true ) );
	return b2Dot( d, WorldAxis( id ) );
}

static float WheelSpeed( b2JointId id )
{
	b2Vec2 pa = WorldAnchor( id, true ), pb = WorldAnchor( id, false );
	b2Pos posA = { pa.x, pa.y }, posB = { pb.x, pb.y };
	b2Vec2 va = b2Body_GetWorldPointVelocity( b2Joint_GetBodyA( id ), posA );
	b2Vec2 vb = b2Body_GetWorldPointVelocity( b2Joint_GetBodyB( id ), posB );
	return b2Dot( b2Sub( vb, va ), WorldAxis( id ) );
}

// ---------------------------------------------------------------------------
// methods (looked up through __index)

static JointUD* CheckLive( lua_State* L, const char* fname )
{
	JointUD* j = (JointUD*)lua_touserdata( L, 1 );
	if ( j == nullptr || !IsLive( State( L ), j ) )
	{
		luaL_error( L, "%s: joint is no longer valid (use ':' to call joint methods).", fname );
	}
	return j;
}

static int j_getAnchorA( lua_State* L )
{
	JointUD* j = CheckLive( L, "getAnchorA" );
	b2Vec2 p = WorldAnchor( j->id, true );
	lua_pushnumber( L, Px( State( L ), p.x ) );
	lua_pushnumber( L, Px( State( L ), p.y ) );
	return 2;
}

static int j_getAnchorB( lua_State* L )
{
	JointUD* j = CheckLive( L, "getAnchorB" );
	b2Vec2 p = WorldAnchor( j->id, false );
	lua_pushnumber( L, Px( State( L ), p.x ) );
	lua_pushnumber( L, Px( State( L ), p.y ) );
	return 2;
}

static int j_getLocalAnchorA( lua_State* L )
{
	JointUD* j = CheckLive( L, "getLocalAnchorA" );
	b2Vec2 p = b2Joint_GetLocalFrameA( j->id ).p;
	lua_pushnumber( L, Px( State( L ), p.x ) );
	lua_pushnumber( L, Px( State( L ), p.y ) );
	return 2;
}

static int j_getLocalAnchorB( lua_State* L )
{
	JointUD* j = CheckLive( L, "getLocalAnchorB" );
	b2Vec2 p = b2Joint_GetLocalFrameB( j->id ).p;
	lua_pushnumber( L, Px( State( L ), p.x ) );
	lua_pushnumber( L, Px( State( L ), p.y ) );
	return 2;
}

// joint:getLocalAxisA() -> x, y   unit axis in body A's frame
// (v2 returned the world anchor B here by mistake; we return the axis the name promises)
static int j_getLocalAxisA( lua_State* L )
{
	JointUD* j = CheckLive( L, "getLocalAxisA" );
	b2Vec2 axis = b2Rot_GetXAxis( b2Joint_GetLocalFrameA( j->id ).q );
	lua_pushnumber( L, axis.x );
	lua_pushnumber( L, axis.y );
	return 2;
}

static int j_getReactionForce( lua_State* L )
{
	JointUD* j = CheckLive( L, "getReactionForce" );
	b2Vec2 f = b2Joint_GetConstraintForce( j->id );
	lua_pushnumber( L, f.x );
	lua_pushnumber( L, f.y );
	return 2;
}

// Adds / removes the joint userdata at index from the strong breakable set.
static void SetBreakable( lua_State* L, int index, bool on )
{
	index = index < 0 ? lua_gettop( L ) + index + 1 : index;
	lua_pushlightuserdata( L, (void*)&kBreakableKey );
	lua_rawget( L, LUA_REGISTRYINDEX );
	lua_pushvalue( L, index );
	if ( on )
		lua_pushboolean( L, 1 );
	else
		lua_pushnil( L );
	lua_rawset( L, -3 );
	lua_pop( L, 1 );
}

static void PushThreshold( lua_State* L, float v )
{
	lua_pushnumber( L, v >= FLT_MAX ? HUGE_VAL : (double)v );
}

static int j_removeSelf( lua_State* L )
{
	JointUD* j = (JointUD*)lua_touserdata( L, 1 );
	PluginState* st = State( L );
	if ( j == nullptr )
	{
		return 0;
	}
	if ( st->inStep )
	{
		return luaL_error( L, "joint:removeSelf() cannot be called while the world is stepping." );
	}
	if ( IsLive( st, j ) )
	{
		st->jointKeys.erase( JointKey( j->id ) );
		b2Joint_WakeBodies( j->id );
		b2DestroyJoint( j->id );
	}
	SetBreakable( L, 1, false );
	if ( B2_IS_NON_NULL( j->helper ) && j->epoch == st->worldEpoch && b2Body_IsValid( j->helper ) )
	{
		b2DestroyBody( j->helper );
	}
	j->helper = b2_nullBodyId;
	j->id = b2_nullJointId;
	b2p_cleanupTouchHelpers( st );
	return 0;
}

// pivot
static int j_setRotationLimits( lua_State* L )
{
	JointUD* j = CheckLive( L, "setRotationLimits" );
	b2RevoluteJoint_SetLimits( j->id, (float)lua_tonumber( L, 2 ) * kDegToRad, (float)lua_tonumber( L, 3 ) * kDegToRad );
	return 0;
}

static int j_getRotationLimits( lua_State* L )
{
	JointUD* j = CheckLive( L, "getRotationLimits" );
	lua_pushnumber( L, b2RevoluteJoint_GetLowerLimit( j->id ) * kRadToDeg );
	lua_pushnumber( L, b2RevoluteJoint_GetUpperLimit( j->id ) * kRadToDeg );
	return 2;
}

// piston / wheel
static int j_setLimits( lua_State* L )
{
	JointUD* j = CheckLive( L, "setLimits" );
	PluginState* st = State( L );
	float lo = M( st, (float)lua_tonumber( L, 2 ) ), hi = M( st, (float)lua_tonumber( L, 3 ) );
	if ( j->kind == kJointWheel )
		b2WheelJoint_SetLimits( j->id, lo, hi );
	else
		b2PrismaticJoint_SetLimits( j->id, lo, hi );
	return 0;
}

static int j_getLimits( lua_State* L )
{
	JointUD* j = CheckLive( L, "getLimits" );
	PluginState* st = State( L );
	bool wheel = j->kind == kJointWheel;
	lua_pushnumber( L, Px( st, wheel ? b2WheelJoint_GetLowerLimit( j->id ) : b2PrismaticJoint_GetLowerLimit( j->id ) ) );
	lua_pushnumber( L, Px( st, wheel ? b2WheelJoint_GetUpperLimit( j->id ) : b2PrismaticJoint_GetUpperLimit( j->id ) ) );
	return 2;
}

// touch
static int j_setTarget( lua_State* L )
{
	JointUD* j = CheckLive( L, "setTarget" );
	PluginState* st = State( L );
	j->target = b2Vec2{ M( st, (float)lua_tonumber( L, 2 ) ), M( st, (float)lua_tonumber( L, 3 ) ) };
	if ( b2Body_IsValid( j->helper ) )
	{
		b2Body_SetTransform( j->helper, b2Pos{ j->target.x, j->target.y }, b2Rot_identity );
	}
	b2Joint_WakeBodies( j->id );
	return 0;
}

static int j_getTarget( lua_State* L )
{
	JointUD* j = CheckLive( L, "getTarget" );
	PluginState* st = State( L );
	lua_pushnumber( L, Px( st, j->target.x ) );
	lua_pushnumber( L, Px( st, j->target.y ) );
	return 2;
}

// motor
static int j_setLinearOffset( lua_State* L )
{
	JointUD* j = CheckLive( L, "setLinearOffset" );
	PluginState* st = State( L );
	b2Transform f = b2Joint_GetLocalFrameA( j->id );
	f.p = b2Vec2{ M( st, (float)luaL_checknumber( L, 2 ) ), M( st, (float)luaL_checknumber( L, 3 ) ) };
	b2Joint_SetLocalFrameA( j->id, f );
	b2Joint_WakeBodies( j->id );
	return 0;
}

static int j_getLinearOffset( lua_State* L )
{
	JointUD* j = CheckLive( L, "getLinearOffset" );
	PluginState* st = State( L );
	b2Transform f = b2Joint_GetLocalFrameA( j->id );
	lua_pushnumber( L, Px( st, f.p.x ) );
	lua_pushnumber( L, Px( st, f.p.y ) );
	return 2;
}

static int j_setLinearVelocity( lua_State* L )
{
	JointUD* j = CheckLive( L, "setLinearVelocity" );
	PluginState* st = State( L );
	b2MotorJoint_SetLinearVelocity( j->id, b2Vec2{ M( st, (float)luaL_checknumber( L, 2 ) ), M( st, (float)luaL_checknumber( L, 3 ) ) } );
	b2Joint_WakeBodies( j->id );
	return 0;
}

static int j_getLinearVelocity( lua_State* L )
{
	JointUD* j = CheckLive( L, "getLinearVelocity" );
	PluginState* st = State( L );
	b2Vec2 v = b2MotorJoint_GetLinearVelocity( j->id );
	lua_pushnumber( L, Px( st, v.x ) );
	lua_pushnumber( L, Px( st, v.y ) );
	return 2;
}

// ---------------------------------------------------------------------------
// properties

static bool PushCommon( lua_State* L, JointUD* j, const char* key )
{
	static const luaL_Reg methods[] = {
		{ "getAnchorA", j_getAnchorA },
		{ "getAnchorB", j_getAnchorB },
		{ "getReactionForce", j_getReactionForce },
		{ "removeSelf", j_removeSelf },
		{ nullptr, nullptr },
	};
	for ( const luaL_Reg* m = methods; m->name; ++m )
	{
		if ( strcmp( key, m->name ) == 0 )
		{
			lua_pushcfunction( L, m->func );
			return true;
		}
	}
	if ( strcmp( key, "jointType" ) == 0 ) // new: the v2 type name
	{
		lua_pushstring( L, kJointNames[j->kind] );
		return true;
	}
	if ( !IsLive( State( L ), j ) )
	{
		return false;
	}
	if ( strcmp( key, "reactionTorque" ) == 0 )
	{
		lua_pushnumber( L, b2Joint_GetConstraintTorque( j->id ) );
		return true;
	}
	if ( strcmp( key, "isActive" ) == 0 )
	{
		lua_pushboolean( L, b2Body_IsEnabled( b2Joint_GetBodyA( j->id ) ) && b2Body_IsEnabled( b2Joint_GetBodyB( j->id ) ) );
		return true;
	}
	if ( strcmp( key, "isCollideConnected" ) == 0 )
	{
		lua_pushboolean( L, b2Joint_GetCollideConnected( j->id ) );
		return true;
	}
	if ( strcmp( key, "breakForce" ) == 0 ) // new
	{
		PushThreshold( L, b2Joint_GetForceThreshold( j->id ) );
		return true;
	}
	if ( strcmp( key, "breakTorque" ) == 0 )
	{
		PushThreshold( L, b2Joint_GetTorqueThreshold( j->id ) );
		return true;
	}
	if ( j->kind != kJointTouch )
	{
		if ( strcmp( key, "getLocalAnchorA" ) == 0 )
		{
			lua_pushcfunction( L, j_getLocalAnchorA );
			return true;
		}
		if ( strcmp( key, "getLocalAnchorB" ) == 0 )
		{
			lua_pushcfunction( L, j_getLocalAnchorB );
			return true;
		}
	}
	return false;
}

static int JointIndex( lua_State* L )
{
	JointUD* j = (JointUD*)lua_touserdata( L, 1 );
	const char* key = lua_tostring( L, 2 );
	PluginState* st = State( L );
	if ( j == nullptr || key == nullptr )
	{
		lua_pushnil( L );
		return 1;
	}
	if ( PushCommon( L, j, key ) )
	{
		return 1;
	}
	if ( !IsLive( st, j ) )
	{
		lua_pushnil( L ); // v2: properties of a destroyed joint read as nil
		return 1;
	}
	b2JointId id = j->id;

	switch ( j->kind )
	{
		case kJointDistance:
			if ( strcmp( key, "length" ) == 0 )
				lua_pushnumber( L, Px( st, b2DistanceJoint_GetRestLength( id ) ) );
			else if ( strcmp( key, "frequency" ) == 0 )
				lua_pushnumber( L, b2DistanceJoint_IsSpringEnabled( id ) ? b2DistanceJoint_GetSpringHertz( id ) : 0.0f );
			else if ( strcmp( key, "dampingRatio" ) == 0 )
				lua_pushnumber( L, b2DistanceJoint_GetSpringDampingRatio( id ) );
			else
				lua_pushnil( L );
			return 1;

		case kJointPivot:
			if ( strcmp( key, "isMotorEnabled" ) == 0 )
				lua_pushboolean( L, b2RevoluteJoint_IsMotorEnabled( id ) );
			else if ( strcmp( key, "motorSpeed" ) == 0 )
				lua_pushnumber( L, b2RevoluteJoint_GetTargetMotorSpeed( id ) * kRadToDeg );
			else if ( strcmp( key, "motorTorque" ) == 0 )
				lua_pushnumber( L, b2RevoluteJoint_GetCurrentMotorTorque( id ) );
			else if ( strcmp( key, "maxMotorTorque" ) == 0 )
				lua_pushnumber( L, b2RevoluteJoint_GetMaxMotorTorque( id ) );
			else if ( strcmp( key, "referenceAngle" ) == 0 )
				lua_pushnumber( L, j->referenceAngle );
			else if ( strcmp( key, "jointAngle" ) == 0 )
				lua_pushnumber( L, b2RevoluteJoint_GetCurrentAngle( id ) * kRadToDeg );
			else if ( strcmp( key, "jointSpeed" ) == 0 )
				lua_pushnumber( L, b2RevoluteJoint_GetCurrentSpeed( id ) * kRadToDeg );
			else if ( strcmp( key, "isLimitEnabled" ) == 0 )
				lua_pushboolean( L, b2RevoluteJoint_IsLimitEnabled( id ) );
			else if ( strcmp( key, "setRotationLimits" ) == 0 )
				lua_pushcfunction( L, j_setRotationLimits );
			else if ( strcmp( key, "getRotationLimits" ) == 0 )
				lua_pushcfunction( L, j_getRotationLimits );
			else if ( strcmp( key, "springFrequency" ) == 0 ) // new in v3
				lua_pushnumber( L, b2RevoluteJoint_IsSpringEnabled( id ) ? b2RevoluteJoint_GetSpringHertz( id ) : 0.0f );
			else if ( strcmp( key, "springDampingRatio" ) == 0 )
				lua_pushnumber( L, b2RevoluteJoint_GetSpringDampingRatio( id ) );
			else
				lua_pushnil( L );
			return 1;

		case kJointPiston:
			if ( strcmp( key, "isMotorEnabled" ) == 0 )
				lua_pushboolean( L, b2PrismaticJoint_IsMotorEnabled( id ) );
			else if ( strcmp( key, "motorSpeed" ) == 0 )
				lua_pushnumber( L, Px( st, b2PrismaticJoint_GetTargetMotorSpeed( id ) ) );
			else if ( strcmp( key, "motorForce" ) == 0 )
				lua_pushnumber( L, b2PrismaticJoint_GetCurrentMotorForce( id ) );
			else if ( strcmp( key, "maxMotorForce" ) == 0 )
				lua_pushnumber( L, b2PrismaticJoint_GetMaxMotorForce( id ) );
			else if ( strcmp( key, "getLocalAxisA" ) == 0 )
				lua_pushcfunction( L, j_getLocalAxisA );
			else if ( strcmp( key, "referenceAngle" ) == 0 )
				lua_pushnumber( L, j->referenceAngle );
			else if ( strcmp( key, "jointTranslation" ) == 0 )
				lua_pushnumber( L, Px( st, b2PrismaticJoint_GetCurrentTranslation( id ) ) );
			else if ( strcmp( key, "jointSpeed" ) == 0 )
				lua_pushnumber( L, Px( st, b2PrismaticJoint_GetCurrentSpeed( id ) ) );
			else if ( strcmp( key, "isLimitEnabled" ) == 0 )
				lua_pushboolean( L, b2PrismaticJoint_IsLimitEnabled( id ) );
			else if ( strcmp( key, "setLimits" ) == 0 )
				lua_pushcfunction( L, j_setLimits );
			else if ( strcmp( key, "getLimits" ) == 0 )
				lua_pushcfunction( L, j_getLimits );
			else
				lua_pushnil( L );
			return 1;

		case kJointFriction:
			if ( strcmp( key, "maxForce" ) == 0 )
				lua_pushnumber( L, b2MotorJoint_GetMaxVelocityForce( id ) );
			else if ( strcmp( key, "maxTorque" ) == 0 )
				lua_pushnumber( L, b2MotorJoint_GetMaxVelocityTorque( id ) );
			else
				lua_pushnil( L );
			return 1;

		case kJointWheel:
			if ( strcmp( key, "isMotorEnabled" ) == 0 )
				lua_pushboolean( L, b2WheelJoint_IsMotorEnabled( id ) );
			else if ( strcmp( key, "motorSpeed" ) == 0 ) // wheel spin, degrees / second (like pivot)
				lua_pushnumber( L, b2WheelJoint_GetTargetMotorSpeed( id ) * kRadToDeg );
			else if ( strcmp( key, "motorTorque" ) == 0 )
				lua_pushnumber( L, b2WheelJoint_GetCurrentMotorTorque( id ) );
			else if ( strcmp( key, "maxMotorTorque" ) == 0 )
				lua_pushnumber( L, b2WheelJoint_GetMaxMotorTorque( id ) );
			else if ( strcmp( key, "getLocalAxisA" ) == 0 )
				lua_pushcfunction( L, j_getLocalAxisA );
			else if ( strcmp( key, "jointTranslation" ) == 0 )
				lua_pushnumber( L, Px( st, WheelTranslation( id ) ) );
			else if ( strcmp( key, "jointSpeed" ) == 0 )
				lua_pushnumber( L, Px( st, WheelSpeed( id ) ) );
			else if ( strcmp( key, "springFrequency" ) == 0 )
				lua_pushnumber( L, b2WheelJoint_IsSpringEnabled( id ) ? b2WheelJoint_GetSpringHertz( id ) : 0.0f );
			else if ( strcmp( key, "springDampingRatio" ) == 0 )
				lua_pushnumber( L, b2WheelJoint_GetSpringDampingRatio( id ) );
			else if ( strcmp( key, "isLimitEnabled" ) == 0 ) // new in v3
				lua_pushboolean( L, b2WheelJoint_IsLimitEnabled( id ) );
			else if ( strcmp( key, "setLimits" ) == 0 )
				lua_pushcfunction( L, j_setLimits );
			else if ( strcmp( key, "getLimits" ) == 0 )
				lua_pushcfunction( L, j_getLimits );
			else
				lua_pushnil( L );
			return 1;

		case kJointWeld:
			if ( strcmp( key, "referenceAngle" ) == 0 )
				lua_pushnumber( L, j->referenceAngle );
			else if ( strcmp( key, "frequency" ) == 0 )
				lua_pushnumber( L, b2WeldJoint_GetAngularHertz( id ) );
			else if ( strcmp( key, "dampingRatio" ) == 0 )
				lua_pushnumber( L, b2WeldJoint_GetAngularDampingRatio( id ) );
			else
				lua_pushnil( L );
			return 1;

		case kJointTouch:
			if ( strcmp( key, "maxForce" ) == 0 )
				lua_pushnumber( L, b2MotorJoint_GetMaxSpringForce( id ) );
			else if ( strcmp( key, "frequency" ) == 0 )
				lua_pushnumber( L, b2MotorJoint_GetLinearHertz( id ) );
			else if ( strcmp( key, "dampingRatio" ) == 0 )
				lua_pushnumber( L, b2MotorJoint_GetLinearDampingRatio( id ) );
			else if ( strcmp( key, "setTarget" ) == 0 )
				lua_pushcfunction( L, j_setTarget );
			else if ( strcmp( key, "getTarget" ) == 0 )
				lua_pushcfunction( L, j_getTarget );
			else
				lua_pushnil( L );
			return 1;

		case kJointMotor:
			if ( strcmp( key, "setLinearOffset" ) == 0 )
				lua_pushcfunction( L, j_setLinearOffset );
			else if ( strcmp( key, "getLinearOffset" ) == 0 )
				lua_pushcfunction( L, j_getLinearOffset );
			else if ( strcmp( key, "angularOffset" ) == 0 )
				lua_pushnumber( L, b2p_rotAngle( b2Joint_GetLocalFrameA( id ).q ) * kRadToDeg );
			else if ( strcmp( key, "setLinearVelocity" ) == 0 )
				lua_pushcfunction( L, j_setLinearVelocity );
			else if ( strcmp( key, "getLinearVelocity" ) == 0 )
				lua_pushcfunction( L, j_getLinearVelocity );
			else if ( strcmp( key, "angularVelocity" ) == 0 )
				lua_pushnumber( L, b2MotorJoint_GetAngularVelocity( id ) * kRadToDeg );
			else if ( strcmp( key, "maxForce" ) == 0 )
				lua_pushnumber( L, b2MotorJoint_GetMaxVelocityForce( id ) );
			else if ( strcmp( key, "maxTorque" ) == 0 )
				lua_pushnumber( L, b2MotorJoint_GetMaxVelocityTorque( id ) );
			else if ( strcmp( key, "linearFrequency" ) == 0 )
				lua_pushnumber( L, b2MotorJoint_GetLinearHertz( id ) );
			else if ( strcmp( key, "linearDampingRatio" ) == 0 )
				lua_pushnumber( L, b2MotorJoint_GetLinearDampingRatio( id ) );
			else if ( strcmp( key, "angularFrequency" ) == 0 )
				lua_pushnumber( L, b2MotorJoint_GetAngularHertz( id ) );
			else if ( strcmp( key, "angularDampingRatio" ) == 0 )
				lua_pushnumber( L, b2MotorJoint_GetAngularDampingRatio( id ) );
			else if ( strcmp( key, "maxSpringForce" ) == 0 )
				lua_pushnumber( L, b2MotorJoint_GetMaxSpringForce( id ) );
			else if ( strcmp( key, "maxSpringTorque" ) == 0 )
				lua_pushnumber( L, b2MotorJoint_GetMaxSpringTorque( id ) );
			else
				lua_pushnil( L );
			return 1;

		case kJointRope:
			if ( strcmp( key, "maxLength" ) == 0 )
				lua_pushnumber( L, Px( st, b2DistanceJoint_GetUpperLimit( id ) ) );
			else if ( strcmp( key, "limitState" ) == 0 )
			{
				float len = b2DistanceJoint_GetCurrentLength( id );
				lua_pushstring( L, len >= b2DistanceJoint_GetUpperLimit( id ) - B2_LINEAR_SLOP ? "upper" : "inactive" );
			}
			else
				lua_pushnil( L );
			return 1;
	}
	lua_pushnil( L );
	return 1;
}

static int JointNewindex( lua_State* L )
{
	JointUD* j = (JointUD*)lua_touserdata( L, 1 );
	const char* key = lua_tostring( L, 2 );
	PluginState* st = State( L );
	if ( j == nullptr || key == nullptr || !IsLive( st, j ) )
	{
		return 0;
	}
	b2JointId id = j->id;
	float n = (float)lua_tonumber( L, 3 );
	bool b = lua_toboolean( L, 3 ) != 0;

	if ( strcmp( key, "isCollideConnected" ) == 0 ) // read-only in v2, settable in v3
	{
		b2Joint_SetCollideConnected( id, b );
		return 0;
	}
	if ( strcmp( key, "breakForce" ) == 0 || strcmp( key, "breakTorque" ) == 0 )
	{
		float v = lua_isnumber( L, 3 ) ? n : FLT_MAX; // nil = unbreakable
		v = v < 0.0f ? 0.0f : ( v > FLT_MAX ? FLT_MAX : v );
		if ( key[5] == 'F' )
			b2Joint_SetForceThreshold( id, v );
		else
			b2Joint_SetTorqueThreshold( id, v );
		SetBreakable( L, 1, b2Joint_GetForceThreshold( id ) < FLT_MAX || b2Joint_GetTorqueThreshold( id ) < FLT_MAX );
		return 0;
	}

	switch ( j->kind )
	{
		case kJointDistance:
			if ( strcmp( key, "length" ) == 0 )
				b2DistanceJoint_SetRestLength( id, M( st, n ) );
			else if ( strcmp( key, "frequency" ) == 0 )
			{
				b2DistanceJoint_EnableSpring( id, n > 0.0f ); // v2: frequency 0 = rigid
				b2DistanceJoint_SetSpringHertz( id, n );
			}
			else if ( strcmp( key, "dampingRatio" ) == 0 )
				b2DistanceJoint_SetSpringDampingRatio( id, n );
			break;

		case kJointPivot:
			if ( strcmp( key, "isMotorEnabled" ) == 0 )
				b2RevoluteJoint_EnableMotor( id, b );
			else if ( strcmp( key, "motorSpeed" ) == 0 )
				b2RevoluteJoint_SetTargetMotorSpeed( id, n * kDegToRad );
			else if ( strcmp( key, "maxMotorTorque" ) == 0 )
				b2RevoluteJoint_SetMaxMotorTorque( id, n );
			else if ( strcmp( key, "isLimitEnabled" ) == 0 )
				b2RevoluteJoint_EnableLimit( id, b );
			else if ( strcmp( key, "springFrequency" ) == 0 )
			{
				b2RevoluteJoint_EnableSpring( id, n > 0.0f );
				b2RevoluteJoint_SetSpringHertz( id, n );
			}
			else if ( strcmp( key, "springDampingRatio" ) == 0 )
				b2RevoluteJoint_SetSpringDampingRatio( id, n );
			break;

		case kJointPiston:
			if ( strcmp( key, "isMotorEnabled" ) == 0 )
				b2PrismaticJoint_EnableMotor( id, b );
			else if ( strcmp( key, "motorSpeed" ) == 0 )
				b2PrismaticJoint_SetTargetMotorSpeed( id, M( st, n ) );
			else if ( strcmp( key, "maxMotorForce" ) == 0 )
				b2PrismaticJoint_SetMaxMotorForce( id, n );
			else if ( strcmp( key, "isLimitEnabled" ) == 0 )
				b2PrismaticJoint_EnableLimit( id, b );
			break;

		case kJointFriction:
			if ( strcmp( key, "maxForce" ) == 0 )
				b2MotorJoint_SetMaxVelocityForce( id, n );
			else if ( strcmp( key, "maxTorque" ) == 0 )
				b2MotorJoint_SetMaxVelocityTorque( id, n );
			break;

		case kJointWheel:
			if ( strcmp( key, "isMotorEnabled" ) == 0 )
				b2WheelJoint_EnableMotor( id, b );
			else if ( strcmp( key, "motorSpeed" ) == 0 )
				b2WheelJoint_SetTargetMotorSpeed( id, n * kDegToRad );
			else if ( strcmp( key, "maxMotorTorque" ) == 0 )
				b2WheelJoint_SetMaxMotorTorque( id, n );
			else if ( strcmp( key, "springFrequency" ) == 0 )
			{
				b2WheelJoint_EnableSpring( id, n > 0.0f );
				b2WheelJoint_SetSpringHertz( id, n );
			}
			else if ( strcmp( key, "springDampingRatio" ) == 0 )
				b2WheelJoint_SetSpringDampingRatio( id, n );
			else if ( strcmp( key, "isLimitEnabled" ) == 0 )
				b2WheelJoint_EnableLimit( id, b );
			break;

		case kJointWeld:
			if ( strcmp( key, "frequency" ) == 0 )
				b2WeldJoint_SetAngularHertz( id, n );
			else if ( strcmp( key, "dampingRatio" ) == 0 )
				b2WeldJoint_SetAngularDampingRatio( id, n );
			break;

		case kJointTouch:
			if ( strcmp( key, "maxForce" ) == 0 )
				b2MotorJoint_SetMaxSpringForce( id, n );
			else if ( strcmp( key, "frequency" ) == 0 )
				b2MotorJoint_SetLinearHertz( id, n );
			else if ( strcmp( key, "dampingRatio" ) == 0 )
				b2MotorJoint_SetLinearDampingRatio( id, n );
			break;

		case kJointMotor:
			if ( strcmp( key, "angularOffset" ) == 0 )
			{
				b2Transform f = b2Joint_GetLocalFrameA( id );
				f.q = b2p_makeRot( n * kDegToRad );
				b2Joint_SetLocalFrameA( id, f );
			}
			else if ( strcmp( key, "angularVelocity" ) == 0 )
				b2MotorJoint_SetAngularVelocity( id, n * kDegToRad );
			else if ( strcmp( key, "maxForce" ) == 0 )
				b2MotorJoint_SetMaxVelocityForce( id, n );
			else if ( strcmp( key, "maxTorque" ) == 0 )
				b2MotorJoint_SetMaxVelocityTorque( id, n );
			else if ( strcmp( key, "linearFrequency" ) == 0 )
				b2MotorJoint_SetLinearHertz( id, n );
			else if ( strcmp( key, "linearDampingRatio" ) == 0 )
				b2MotorJoint_SetLinearDampingRatio( id, n );
			else if ( strcmp( key, "angularFrequency" ) == 0 )
				b2MotorJoint_SetAngularHertz( id, n );
			else if ( strcmp( key, "angularDampingRatio" ) == 0 )
				b2MotorJoint_SetAngularDampingRatio( id, n );
			else if ( strcmp( key, "maxSpringForce" ) == 0 )
				b2MotorJoint_SetMaxSpringForce( id, n );
			else if ( strcmp( key, "maxSpringTorque" ) == 0 )
				b2MotorJoint_SetMaxSpringTorque( id, n );
			break;

		case kJointRope:
			if ( strcmp( key, "maxLength" ) == 0 ) // pixels
			{
				float len = M( st, n ) > B2_LINEAR_SLOP ? M( st, n ) : B2_LINEAR_SLOP;
				b2DistanceJoint_SetLimits( id, 0.0f, len );
				b2DistanceJoint_SetRestLength( id, len );
			}
			break;
	}
	b2Joint_WakeBodies( id );
	return 0;
}

static int JointToString( lua_State* L )
{
	JointUD* j = (JointUD*)lua_touserdata( L, 1 );
	lua_pushfstring( L, "Joint(%s)%s", j ? kJointNames[j->kind] : "?", ( j && IsLive( State( L ), j ) ) ? "" : " [removed]" );
	return 1;
}

static int JointEq( lua_State* L )
{
	JointUD* a = (JointUD*)lua_touserdata( L, 1 );
	JointUD* b = (JointUD*)lua_touserdata( L, 2 );
	lua_pushboolean( L, a && b && a->epoch == b->epoch && B2_ID_EQUALS( a->id, b->id ) );
	return 1;
}

void b2p_initJoints( lua_State* L )
{
	lua_pushlightuserdata( L, (void*)&kJointMt );
	lua_newtable( L );
	lua_pushcfunction( L, JointIndex );
	lua_setfield( L, -2, "__index" );
	lua_pushcfunction( L, JointNewindex );
	lua_setfield( L, -2, "__newindex" );
	lua_pushcfunction( L, JointToString );
	lua_setfield( L, -2, "__tostring" );
	lua_pushcfunction( L, JointEq );
	lua_setfield( L, -2, "__eq" );
	lua_rawset( L, LUA_REGISTRYINDEX );

	lua_pushlightuserdata( L, (void*)&kJointsKey );
	lua_newtable( L );
	lua_newtable( L );
	lua_pushliteral( L, "k" );
	lua_setfield( L, -2, "__mode" );
	lua_setmetatable( L, -2 );
	lua_rawset( L, LUA_REGISTRYINDEX );

	lua_pushlightuserdata( L, (void*)&kBreakableKey );
	lua_newtable( L );
	lua_rawset( L, LUA_REGISTRYINDEX );
}

// physics.stop(): forget the breakable joints of the old world.
void b2p_resetJoints( lua_State* L, PluginState* st )
{
	st->jointKeys.clear();
	lua_pushlightuserdata( L, (void*)&kBreakableKey );
	lua_newtable( L );
	lua_rawset( L, LUA_REGISTRYINDEX );
}

// ---------------------------------------------------------------------------
// physics.newJoint

static Body* JointBody( lua_State* L, int index, const char* type )
{
	Body* b = b2p_toBody( L, index );
	if ( b == nullptr )
	{
		luaL_error( L, "physics.newJoint( \"%s\" ) requires argument %d to have a physics body attached to the display object",
					type, index );
	}
	return b;
}

static b2Vec2 ArgPoint( lua_State* L, const PluginState* st, int index )
{
	return b2Vec2{ M( st, (float)lua_tonumber( L, index ) ), M( st, (float)lua_tonumber( L, index + 1 ) ) };
}

static b2Vec2 LocalPoint( b2BodyId b, b2Vec2 world )
{
	return b2Body_GetLocalPoint( b, b2Pos{ world.x, world.y } );
}

// frames for joints whose reference angle is the current relative rotation
static void AnchorFrames( b2JointDef* base, b2BodyId a, b2BodyId b, b2Vec2 anchor, b2Rot frameWorld )
{
	base->bodyIdA = a;
	base->bodyIdB = b;
	base->localFrameA.p = LocalPoint( a, anchor );
	base->localFrameB.p = LocalPoint( b, anchor );
	base->localFrameA.q = b2InvMulRot( b2Body_GetRotation( a ), frameWorld );
	base->localFrameB.q = b2InvMulRot( b2Body_GetRotation( b ), frameWorld );
}

static JointUD* PushJoint( lua_State* L, PluginState* st, b2JointId id, int kind )
{
	JointUD* j = (JointUD*)lua_newuserdata( L, sizeof( JointUD ) );
	j->id = id;
	j->kind = kind;
	j->epoch = st->worldEpoch;
	j->helper = b2_nullBodyId;
	j->target = b2Vec2{ 0.0f, 0.0f };
	j->referenceAngle = 0.0f;
	j->ropeMaxLength = 0.0f;
	lua_pushlightuserdata( L, (void*)&kJointMt );
	lua_rawget( L, LUA_REGISTRYINDEX );
	lua_setmetatable( L, -2 );
	// joints[ud] = true (weak keys)
	lua_pushlightuserdata( L, (void*)&kJointsKey );
	lua_rawget( L, LUA_REGISTRYINDEX );
	lua_pushvalue( L, -2 );
	lua_pushboolean( L, 1 );
	lua_rawset( L, -3 );
	lua_pop( L, 1 );
	st->jointKeys.insert( JointKey( id ) );
	return j;
}

// physics.newJoint( type, objA, objB, ... ) -> joint
int b2p_newJoint( lua_State* L )
{
	PluginState* st = b2p_requireWorld( L, "physics.newJoint()" );
	if ( st->inStep )
	{
		return luaL_error( L, "physics.newJoint() cannot be called while the world is stepping." );
	}
	const char* type = luaL_checkstring( L, 1 );

	if ( strcmp( type, "pulley" ) == 0 || strcmp( type, "gear" ) == 0 )
	{
		return luaL_error( L, "physics.newJoint(): the \"%s\" joint does not exist in Box2D v3.", type );
	}

	Body* ba = JointBody( L, 2, type );

	if ( strcmp( type, "touch" ) == 0 )
	{
		b2Vec2 target = ArgPoint( L, st, 3 );
		b2BodyDef hd = b2DefaultBodyDef();
		hd.type = b2_kinematicBody;
		hd.position = b2Pos{ target.x, target.y };
		hd.enableSleep = false;
		b2BodyId helper = b2CreateBody( st->world, &hd );

		float mass = b2Body_GetMass( ba->id );
		b2MotorJointDef d = b2DefaultMotorJointDef();
		d.base.bodyIdA = helper;
		d.base.bodyIdB = ba->id;
		d.base.localFrameA = b2Transform_identity;
		d.base.localFrameB.p = LocalPoint( ba->id, target );
		d.base.localFrameB.q = b2InvMulRot( b2Body_GetRotation( ba->id ), b2Rot_identity );
		d.linearHertz = 5.0f; // b2MouseJointDef defaults in v2
		d.linearDampingRatio = 0.7f;
		d.maxSpringForce = 1000.0f * ( mass > 0.0f ? mass : 1.0f );
		d.angularHertz = 0.0f;
		d.maxSpringTorque = 0.0f;
		d.maxVelocityForce = 0.0f;
		d.maxVelocityTorque = 0.0f;
		b2JointId id = b2CreateMotorJoint( st->world, &d );
		b2Body_SetAwake( ba->id, true );

		JointUD* j = PushJoint( L, st, id, kJointTouch );
		j->helper = helper;
		j->target = target;
		st->touchHelpers.push_back( PluginState::TouchHelper{ id, helper } );
		return 1;
	}

	Body* bb = JointBody( L, 3, type );
	if ( ba == bb )
	{
		return luaL_error( L, "physics.newJoint() object1 and object2 cannot be the same object" );
	}
	b2BodyId a = ba->id, b = bb->id;
	float refAngle = AngleDeg( b ) - AngleDeg( a );
	b2Rot qA = b2Body_GetRotation( a );

	if ( strcmp( type, "pivot" ) == 0 )
	{
		b2RevoluteJointDef d = b2DefaultRevoluteJointDef();
		AnchorFrames( &d.base, a, b, ArgPoint( L, st, 4 ), qA );
		JointUD* j = PushJoint( L, st, b2CreateRevoluteJoint( st->world, &d ), kJointPivot );
		j->referenceAngle = refAngle;
		return 1;
	}
	if ( strcmp( type, "weld" ) == 0 )
	{
		b2WeldJointDef d = b2DefaultWeldJointDef();
		AnchorFrames( &d.base, a, b, ArgPoint( L, st, 4 ), qA );
		JointUD* j = PushJoint( L, st, b2CreateWeldJoint( st->world, &d ), kJointWeld );
		j->referenceAngle = refAngle;
		return 1;
	}
	if ( strcmp( type, "friction" ) == 0 )
	{
		b2MotorJointDef d = b2DefaultMotorJointDef();
		AnchorFrames( &d.base, a, b, ArgPoint( L, st, 4 ), qA );
		d.linearVelocity = b2Vec2{ 0.0f, 0.0f };
		d.angularVelocity = 0.0f;
		d.maxVelocityForce = 0.0f; // v2 b2FrictionJointDef defaults
		d.maxVelocityTorque = 0.0f;
		d.linearHertz = 0.0f;
		d.angularHertz = 0.0f;
		d.maxSpringForce = 0.0f;
		d.maxSpringTorque = 0.0f;
		d.base.collideConnected = false;
		PushJoint( L, st, b2CreateMotorJoint( st->world, &d ), kJointFriction );
		return 1;
	}
	if ( strcmp( type, "motor" ) == 0 )
	{
		// physics.newJoint( "motor", objA, objB ): holds objB at its current offset / angle relative to
		// objA. Change the target with joint:setLinearOffset() / joint.angularOffset, or drive it with
		// joint:setLinearVelocity() / joint.angularVelocity (needs maxForce / maxTorque > 0).
		b2MotorJointDef d = b2DefaultMotorJointDef();
		d.base.bodyIdA = a;
		d.base.bodyIdB = b;
		b2Pos pb = b2Body_GetPosition( b );
		d.base.localFrameA.p = b2Body_GetLocalPoint( a, pb );
		d.base.localFrameA.q = b2InvMulRot( qA, b2Body_GetRotation( b ) );
		d.base.localFrameB = b2Transform_identity;
		float massB = b2Body_GetMass( b );
		float inertiaB = b2Body_GetRotationalInertia( b );
		d.linearHertz = 4.0f;
		d.linearDampingRatio = 0.7f;
		d.angularHertz = 4.0f;
		d.angularDampingRatio = 0.7f;
		d.maxSpringForce = 1000.0f * ( massB > 0.0f ? massB : 1.0f );
		d.maxSpringTorque = 1000.0f * ( inertiaB > 0.0f ? inertiaB : 1.0f );
		d.maxVelocityForce = 0.0f;
		d.maxVelocityTorque = 0.0f;
		PushJoint( L, st, b2CreateMotorJoint( st->world, &d ), kJointMotor );
		return 1;
	}
	if ( strcmp( type, "piston" ) == 0 || strcmp( type, "wheel" ) == 0 )
	{
		bool wheel = type[0] == 'w';
		b2Vec2 anchor = ArgPoint( L, st, 4 );
		b2Vec2 axis = { (float)lua_tonumber( L, 6 ), (float)lua_tonumber( L, 7 ) };
		if ( b2Length( axis ) < 1e-6f )
		{
			return luaL_error( L, "physics.newJoint( \"%s\" ) requires a non-zero axis.", type );
		}
		axis = b2Normalize( axis );
		b2Rot frame = b2MakeRotFromUnitVector( axis );
		JointUD* j;
		if ( wheel )
		{
			b2WheelJointDef d = b2DefaultWheelJointDef();
			AnchorFrames( &d.base, a, b, anchor, frame );
			d.enableSpring = true; // v2 b2WheelJointDef defaults
			d.hertz = 2.0f;
			d.dampingRatio = 0.7f;
			j = PushJoint( L, st, b2CreateWheelJoint( st->world, &d ), kJointWheel );
		}
		else
		{
			b2PrismaticJointDef d = b2DefaultPrismaticJointDef();
			AnchorFrames( &d.base, a, b, anchor, frame );
			j = PushJoint( L, st, b2CreatePrismaticJoint( st->world, &d ), kJointPiston );
		}
		j->referenceAngle = refAngle;
		return 1;
	}
	if ( strcmp( type, "distance" ) == 0 )
	{
		b2Vec2 p1 = ArgPoint( L, st, 4 ), p2 = ArgPoint( L, st, 6 );
		b2DistanceJointDef d = b2DefaultDistanceJointDef();
		d.base.bodyIdA = a;
		d.base.bodyIdB = b;
		d.base.localFrameA.p = LocalPoint( a, p1 );
		d.base.localFrameB.p = LocalPoint( b, p2 );
		float len = b2Distance( p1, p2 );
		d.length = len > B2_LINEAR_SLOP ? len : B2_LINEAR_SLOP;
		d.enableSpring = false; // v2: rigid until frequency is set
		d.enableLimit = false;
		d.dampingRatio = 0.0f;
		PushJoint( L, st, b2CreateDistanceJoint( st->world, &d ), kJointDistance );
		return 1;
	}
	if ( strcmp( type, "rope" ) == 0 )
	{
		// physics.newJoint( "rope", objA, objB, [offsetAx, offsetAy, offsetBx, offsetBy, [maxLength]] )
		// anchors are offsets from each body's origin (as in v2); the maximum length defaults to the
		// current distance between the two anchors (v2 measured between the body origins).
		b2DistanceJointDef d = b2DefaultDistanceJointDef();
		d.base.bodyIdA = a;
		d.base.bodyIdB = b;
		d.base.localFrameA.p = ArgPoint( L, st, 4 );
		d.base.localFrameB.p = ArgPoint( L, st, 6 );
		b2Pos pa = b2Body_GetWorldPoint( a, d.base.localFrameA.p ), pb = b2Body_GetWorldPoint( b, d.base.localFrameB.p );
		float maxLen = lua_isnumber( L, 8 ) ? M( st, (float)lua_tonumber( L, 8 ) )
											 : b2Distance( b2Vec2{ (float)pa.x, (float)pa.y }, b2Vec2{ (float)pb.x, (float)pb.y } );
		if ( maxLen < B2_LINEAR_SLOP )
		{
			maxLen = B2_LINEAR_SLOP;
		}
		d.length = maxLen;
		d.enableSpring = true; // spring on with 0 Hz = slack below the limit
		d.hertz = 0.0f;
		d.enableLimit = true;
		d.minLength = 0.0f;
		d.maxLength = maxLen;
		JointUD* j = PushJoint( L, st, b2CreateDistanceJoint( st->world, &d ), kJointRope );
		j->ropeMaxLength = maxLen;
		return 1;
	}

	return luaL_error( L, "physics.newJoint() unknown joint type '%s'", type );
}

void b2p_cleanupTouchHelpers( PluginState* st )
{
	std::vector<PluginState::TouchHelper>& list = st->touchHelpers;
	for ( size_t i = 0; i < list.size(); )
	{
		if ( B2_IS_NULL( st->world ) || !b2Joint_IsValid( list[i].joint ) )
		{
			if ( B2_IS_NON_NULL( st->world ) && b2Body_IsValid( list[i].helper ) )
			{
				b2DestroyBody( list[i].helper );
			}
			list[i] = list.back();
			list.pop_back();
		}
		else
		{
			++i;
		}
	}
}

// ---------------------------------------------------------------------------
// "jointBreak": Box2D reports joints whose constraint force / torque exceeds their threshold.

static void PushObjectOf( lua_State* L, b2BodyId body )
{
	Body* b = b2Body_IsValid( body ) ? (Body*)b2Body_GetUserData( body ) : nullptr;
	if ( b != nullptr && !b->destroyed && !b->pendingDestroy )
		lua_rawgeti( L, LUA_REGISTRYINDEX, b->objRef );
	else
		lua_pushnil( L );
}

static bool Alive( lua_State* L, int obj )
{
	if ( !lua_istable( L, obj ) || !lua_getmetatable( L, obj ) )
	{
		return false;
	}
	lua_pop( L, 1 );
	return true;
}

// target:dispatchEvent( event ) protected; event at the top is consumed
static bool Dispatch( lua_State* L, PluginState* st, int target, int errfunc )
{
	int ev = lua_gettop( L );
	lua_getfield( L, target, "dispatchEvent" );
	if ( !lua_isfunction( L, -1 ) )
	{
		lua_settop( L, ev - 1 );
		return false;
	}
	lua_pushvalue( L, target );
	lua_pushvalue( L, ev );
	bool handled = false;
	if ( lua_pcall( L, 2, 1, errfunc ) != 0 )
	{
		if ( st->pendingError.empty() )
		{
			const char* msg = lua_tostring( L, -1 );
			st->pendingError = msg ? msg : "error in jointBreak listener";
		}
	}
	else
	{
		handled = lua_toboolean( L, -1 ) != 0;
	}
	lua_settop( L, ev - 1 );
	return handled;
}

void b2p_processJointEvents( lua_State* L, PluginState* st )
{
	b2JointEvents events = b2World_GetJointEvents( st->world );
	if ( events.count == 0 )
	{
		return;
	}
	std::vector<b2JointId> ids;
	ids.reserve( (size_t)events.count );
	for ( int i = 0; i < events.count; ++i )
	{
		ids.push_back( events.jointEvents[i].jointId );
	}

	int top = lua_gettop( L );
	lua_getglobal( L, "debug" );
	lua_getfield( L, -1, "traceback" );
	lua_remove( L, -2 );
	int errfunc = lua_isfunction( L, -1 ) ? lua_gettop( L ) : 0;
	lua_pushlightuserdata( L, (void*)&kBreakableKey );
	lua_rawget( L, LUA_REGISTRYINDEX );
	int breakable = lua_gettop( L );
	lua_getglobal( L, "Runtime" );
	int runtime = lua_istable( L, -1 ) ? lua_gettop( L ) : 0;

	for ( b2JointId id : ids )
	{
		if ( B2_IS_NULL( st->world ) || !b2Joint_IsValid( id ) )
		{
			continue;
		}
		// find the userdata of this joint among the breakable ones
		int ud = 0;
		lua_pushnil( L );
		while ( lua_next( L, breakable ) != 0 )
		{
			lua_pop( L, 1 );
			JointUD* j = (JointUD*)lua_touserdata( L, -1 );
			if ( j != nullptr && j->epoch == st->worldEpoch && B2_ID_EQUALS( j->id, id ) )
			{
				ud = lua_gettop( L ); // keep the key on the stack
				break;
			}
		}
		if ( ud == 0 )
		{
			continue; // threshold not set through the plugin
		}

		b2Vec2 f = b2Joint_GetConstraintForce( id );
		float force = b2Length( f );
		float torque = fabsf( b2Joint_GetConstraintTorque( id ) );
		PushObjectOf( L, b2Joint_GetBodyA( id ) );
		int obj1 = lua_gettop( L );
		PushObjectOf( L, b2Joint_GetBodyB( id ) );
		int obj2 = lua_gettop( L );

		bool handled = false;
		for ( int pass = 0; pass < 3 && !handled; ++pass )
		{
			int target = pass == 0 ? obj1 : ( pass == 1 ? obj2 : runtime );
			if ( target == 0 || ( pass < 2 && !Alive( L, target ) ) )
			{
				continue;
			}
			lua_newtable( L );
			lua_pushliteral( L, "jointBreak" );
			lua_setfield( L, -2, "name" );
			lua_pushvalue( L, ud );
			lua_setfield( L, -2, "joint" );
			if ( pass < 2 )
			{
				lua_pushvalue( L, target );
				lua_setfield( L, -2, "target" );
				lua_pushvalue( L, pass == 0 ? obj2 : obj1 );
				lua_setfield( L, -2, "other" );
			}
			else
			{
				lua_pushvalue( L, obj1 );
				lua_setfield( L, -2, "object1" );
				lua_pushvalue( L, obj2 );
				lua_setfield( L, -2, "object2" );
			}
			lua_pushnumber( L, force );
			lua_setfield( L, -2, "force" );
			lua_pushnumber( L, torque );
			lua_setfield( L, -2, "torque" );
			handled = Dispatch( L, st, target, errfunc );
		}

		// remove the joint (unless a listener already did, or physics.stop() was called)
		JointUD* j = (JointUD*)lua_touserdata( L, ud );
		if ( B2_IS_NON_NULL( st->world ) && IsLive( st, j ) )
		{
			st->jointKeys.erase( JointKey( j->id ) );
			b2Joint_WakeBodies( j->id );
			b2DestroyJoint( j->id );
			j->id = b2_nullJointId;
		}
		SetBreakable( L, ud, false );
		lua_settop( L, runtime != 0 ? runtime : breakable );
	}
	lua_settop( L, top );
	if ( !st->touchHelpers.empty() )
	{
		b2p_cleanupTouchHelpers( st );
	}
}

// ---------------------------------------------------------------------------
// snapshot support

// Every joint of the world that the plugin created and that still exists.
void b2p_saveJoints( PluginState* st, std::vector<b2JointId>& out )
{
	for ( auto it = st->jointKeys.begin(); it != st->jointKeys.end(); )
	{
		b2JointId id;
		id.index1 = (int32_t)( *it >> 32 );
		id.world0 = (uint16_t)( ( *it >> 16 ) & 0xFFFF );
		id.generation = (uint16_t)( *it & 0xFFFF );
		if ( b2Joint_IsValid( id ) )
		{
			out.push_back( id );
			++it;
		}
		else
		{
			it = st->jointKeys.erase( it ); // destroyed with one of its bodies
		}
	}
}

// After b2World_RestoreFromSnapshot: joints created since the snapshot are gone (their userdata now
// reads as removed); joints removed since the snapshot came back and are destroyed again.
void b2p_jointsAfterRestore( lua_State* L, PluginState* st, const std::vector<b2JointId>& saved,
							 const std::vector<PluginState::TouchHelper>& savedHelpers )
{
	std::unordered_set<uint64_t> savedKeys;
	for ( b2JointId id : saved )
	{
		savedKeys.insert( JointKey( id ) );
	}

	// userdata of joints that did not exist at the snapshot
	lua_pushlightuserdata( L, (void*)&kJointsKey );
	lua_rawget( L, LUA_REGISTRYINDEX );
	int all = lua_gettop( L );
	lua_pushnil( L );
	while ( lua_next( L, all ) != 0 )
	{
		lua_pop( L, 1 );
		JointUD* j = (JointUD*)lua_touserdata( L, -1 );
		if ( j != nullptr && j->epoch == st->worldEpoch && B2_IS_NON_NULL( j->id ) )
		{
			if ( savedKeys.count( JointKey( j->id ) ) == 0 )
			{
				j->id = b2_nullJointId;
				j->helper = b2_nullBodyId;
			}
			else if ( j->kind == kJointTouch && b2Body_IsValid( j->helper ) )
			{
				b2Pos p = b2Body_GetPosition( j->helper );
				j->target = b2Vec2{ (float)p.x, (float)p.y };
			}
		}
	}
	lua_pop( L, 1 );

	// joints removed after the snapshot (joint:removeSelf, broken)
	std::unordered_set<uint64_t> live = st->jointKeys;
	st->jointKeys.clear();
	for ( b2JointId id : saved )
	{
		uint64_t key = JointKey( id );
		if ( !b2Joint_IsValid( id ) )
		{
			continue;
		}
		if ( live.count( key ) == 0 )
		{
			b2DestroyJoint( id );
		}
		else
		{
			st->jointKeys.insert( key );
		}
	}

	// touch helpers: the restored world has the helpers of the snapshot
	st->touchHelpers.clear();
	for ( const PluginState::TouchHelper& h : savedHelpers )
	{
		if ( b2Joint_IsValid( h.joint ) )
		{
			st->touchHelpers.push_back( h );
		}
		else if ( b2Body_IsValid( h.helper ) )
		{
			b2DestroyBody( h.helper );
		}
	}
}

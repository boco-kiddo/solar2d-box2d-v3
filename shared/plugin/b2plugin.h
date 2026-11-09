// plugin.box2dv3 — Box2D v3 physics for Solar2D with a physics (v2) compatible API.
// Shared declarations between the plugin translation units.
#pragma once

#include "CoronaLua.h"
#include "box2d/box2d.h"

#include <math.h>
#include <string>
#include <unordered_set>
#include <vector>

#define B2PLUGIN_NAME "plugin.box2dv3"
#define B2PLUGIN_VERSION "0.7.1"

struct PluginState;
struct Mover;

enum ElementKind
{
	kElementCircle,
	kElementPolygon,
	kElementChain, // "chain", display.newLine, static display.newPolygon
	kElementCapsule,
	kElementSkipped, // degenerate "shape" left out (keeps the element numbering of addBody)
};

// One body element (v2 "fixture"). Kept so shapes can be rebuilt (e.g. obj.isSensor = true).
struct ElementRecord
{
	int kind;
	int index; // v2 element index (1-based), stored in the shape user data
	b2Circle circle;
	b2Polygon polygon; // polygon.radius > 0: rounded corners ("cornerRadius")
	b2Capsule capsule;
	std::vector<b2Vec2> points; // chain points (meters, body space)
	bool loop;
	float density;
	float friction;
	float bounce;
	float rollingResistance; // v3, [0, 1]
	float tangentSpeed;		 // v3, m/s (conveyor belts)
	bool isSensor;
	b2Filter filter;
	b2ChainId chainA; // a chain is built twice (both windings) so it is two-sided like v2
	b2ChainId chainB;
};

// One physics body attached to one display object.
struct Body
{
	b2BodyId id;
	PluginState* state;
	uint64_t serial;	   // unique per body (snapshot matching)
	int objRef;			   // registry ref to the display object table
	int origMtRef;		   // registry ref to the object's original metatable
	int origNewindexRef;   // registry ref to the original __newindex (used to write x/y/rotation)
	int index;			   // position in PluginState::bodies
	float lastRotation;	   // last rotation written to the display object (degrees, cumulative)
	std::vector<ElementRecord> elements;
	bool destroyed;
	bool pendingDestroy;  // display object removed while the world was locked
	bool preSolveEnabled; // shapes have pre-solve events on (someone listens to "preCollision")
	bool wantPost;		  // the object listens to "postCollision"
};

struct FrameStats
{
	float stepTime;	 // ms in b2World_Step during the last enterFrame
	float syncTime;	 // ms writing transforms to display objects
	float eventTime; // ms dispatching collision events
	float frameTime; // ms for the whole physics enterFrame
	int stepCount;
};

struct PluginState
{
	lua_State* L;
	b2WorldId world;
	bool running;		 // physics.start() called and not paused
	float scale;		 // pixels per meter (v2 default 30)
	b2Vec2 gravity;		 // m/s^2
	float timeStep;		 // >0 fixed, <0 frame interval, 0 time based (v2 semantics)
	float frameInterval; // 1 / display.fps
	float timeScale;
	int subSteps;
	float contactHertz;		  // contact stiffness (Hz), capped by Box2D at 1/8 of the sub-step rate
	float contactDampingRatio;
	float contactPushSpeed;	  // m/s
	float restitutionThreshold; // m/s (v2 setMKS "velocityThreshold")
	float sleepThreshold;		// m/s (v2 setMKS "linearSleepTolerance")
	float maxLinearSpeed;		// m/s
	double timePrevious; // seconds, for time based stepping
	float timeRemainder;
	bool inStep;		 // true while the world is locked
	int enterFrameRef;	 // registry ref of the enterFrame listener
	int finalizeRef;	 // registry ref of the finalize listener given to every body object
	FrameStats frameStats;
	std::vector<Body*> bodies;
	std::vector<Body*> pendingDestroy; // destroyed after the current step
	bool reportContentCoords;		   // physics.setReportCollisionsInContentCoordinates
	bool averagePositions;			   // physics.setAverageCollisionPositions
	bool debugErrors;				   // physics.setDebugErrorsEnabled (stored only)
	bool runtimePre;				   // Runtime listens to "preCollision"
	bool runtimePost;				   // Runtime listens to "postCollision"
	bool anyPost;
	bool anyHit;	   // someone listens to "hit" (v3)
	bool anyPreSolve;  // some body has pre-solve on: the step must run on the Lua thread only
	int threadCount;   // physics.setThreadCount (1 = single threaded)
	void* scheduler;   // Box2D's built-in task scheduler when threadCount > 1
	bool serialTasks;  // run Box2D tasks inline on the calling thread
	float hitThreshold; // m/s
	std::string pendingError;		   // first Lua error raised by a listener during the step
	uint32_t worldEpoch;			   // incremented for every new world (stale joint userdata detection)
	struct TouchHelper
	{
		b2JointId joint;
		b2BodyId helper;
	};
	std::vector<TouchHelper> touchHelpers; // kinematic bodies driving "touch" joints
	uint64_t nextSerial;				   // body / mover serial numbers
	std::vector<Mover*> movers;			   // physics.newMover
	std::unordered_set<uint64_t> jointKeys; // joints created by the plugin (snapshot restore)
	int drawMode;	   // 0 normal, 1 hybrid, 2 debug
	int drawGroupRef;  // registry ref of the debug draw group
	int drawParentRef; // physics.setDrawGroup(): group the debug draw is inserted in (LUA_NOREF = auto)
	bool drawContacts; // debug draw also shows contacts
};

// Global state (Solar2D runs one Lua state per app; relaunch re-opens the plugin).
PluginState* b2p_state( lua_State* L );
PluginState* b2p_requireWorld( lua_State* L, const char* fname );

// ---- body module (b2plugin_body.cpp)
int b2p_addBody( lua_State* L );
int b2p_removeBody( lua_State* L );
void b2p_destroyBody( lua_State* L, Body* body, bool restoreObject );
void b2p_destroyAllBodies( lua_State* L, bool luaAlive );
void b2p_syncBodies( lua_State* L, PluginState* st );
Body* b2p_toBody( lua_State* L, int objIndex );
void b2p_writeTransform( lua_State* L, Body* body ); // body transform -> display object now
void b2p_initBodyModule( lua_State* L );

// ---- shapes module (b2plugin_shapes.cpp)
struct ObjectGeometry
{
	float width, height;
	b2Vec2 anchorOffset; // pixels, from the anchor point to the object's centre
};
ObjectGeometry b2p_readGeometry( lua_State* L, int obj );
// Parses one element table (t == 0: no table) and appends one or more records. May raise Lua errors.
// *forceStatic is set when v2 forces the body static (display.newLine).
void b2p_parseElement( lua_State* L, const PluginState* st, int obj, int t, b2BodyType type,
					   const ObjectGeometry& g, std::vector<ElementRecord>& out, bool* forceStatic );
void b2p_createElementShapes( b2BodyId bodyId, ElementRecord& rec );
void b2p_destroyElementShapes( b2BodyId bodyId, std::vector<ElementRecord>& elements );
// Pushes friction / bounce / rollingResistance / tangentSpeed of the records to the existing shapes.
void b2p_applyElementMaterials( b2BodyId bodyId, const std::vector<ElementRecord>& elements );
void b2p_installDisplayHooks( lua_State* L );
// Splits a simple polygon (any winding) into convex pieces of at most B2_MAX_POLYGON_VERTICES.
bool b2p_decomposePolygon( const std::vector<b2Vec2>& in, std::vector<std::vector<b2Vec2>>& out );

// ---- events module (b2plugin_events.cpp)
void b2p_initEvents( lua_State* L );
void b2p_attachWorldCallbacks( PluginState* st );
void b2p_scanListeners( lua_State* L, PluginState* st );	   // before a step
void b2p_dispatchStepEvents( lua_State* L, PluginState* st ); // after a step
void b2p_applyPreSolveFlag( Body* body, bool enabled );
void b2p_processPendingDestroys( lua_State* L, PluginState* st );
void b2p_raisePendingError( lua_State* L, PluginState* st );

// ---- joints module (b2plugin_joints.cpp)
int b2p_newJoint( lua_State* L );
void b2p_initJoints( lua_State* L );
void b2p_cleanupTouchHelpers( PluginState* st ); // destroys helpers of joints that went away
void b2p_processJointEvents( lua_State* L, PluginState* st ); // "jointBreak", after a step
void b2p_resetJoints( lua_State* L, PluginState* st );
void b2p_saveJoints( PluginState* st, std::vector<b2JointId>& out );
void b2p_jointsAfterRestore( lua_State* L, PluginState* st, const std::vector<b2JointId>& saved,
							 const std::vector<PluginState::TouchHelper>& savedHelpers );

// ---- character mover (b2plugin_mover.cpp)
void b2p_registerMover( lua_State* L, int libIndex );
void b2p_updateMovers( lua_State* L, PluginState* st, float dt ); // before each step
void b2p_destroyAllMovers( lua_State* L, PluginState* st );
void* b2p_saveMovers( PluginState* st, size_t* count );
void b2p_restoreMovers( lua_State* L, PluginState* st, const void* data, size_t count );
void b2p_freeMoverSave( void* data );

// ---- snapshot (b2plugin_snapshot.cpp)
void b2p_registerSnapshot( lua_State* L, int libIndex );
void b2p_applyWorldSettings( PluginState* st ); // plugin settings -> world (after a restore)

// ---- queries (b2plugin_query.cpp) and debug draw (b2plugin_draw.cpp)
void b2p_registerQueries( lua_State* L, int libIndex );
void b2p_registerDraw( lua_State* L, int libIndex );
void b2p_renderDebugDraw( lua_State* L, PluginState* st );
void b2p_removeDebugDraw( lua_State* L, PluginState* st );

// ---- helpers
inline float b2p_toMeters( const PluginState* st, float pixels )
{
	return pixels / st->scale;
}
inline float b2p_toPixels( const PluginState* st, float meters )
{
	return meters * st->scale;
}

const float kRadToDeg = 57.295779513082320876f;
const float kDegToRad = 0.017453292519943295769f;

// Exact rotation from radians. b2MakeRot uses a fast approximation (about 0.03 degree error) that
// would make obj.rotation = 30 read back as 30.03.
inline b2Rot b2p_makeRot( float radians )
{
	b2Rot q = { cosf( radians ), sinf( radians ) };
	return q;
}

// Exact angle of a rotation (b2Rot_GetAngle uses a fast atan2 approximation).
inline float b2p_rotAngle( b2Rot q )
{
	return atan2f( q.s, q.c );
}

// Reads t[key] as a number; returns def when absent / not a number.
float b2p_getNumberField( lua_State* L, int t, const char* key, float def );
bool b2p_getBoolField( lua_State* L, int t, const char* key, bool def );
void b2p_warn( lua_State* L, const char* fmt, ... );

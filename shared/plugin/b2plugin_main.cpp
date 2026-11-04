// plugin.box2dv3 — module entry, world lifecycle and stepping.
#include "b2plugin.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static PluginState* gState = nullptr;
static char kSentinelKey = 0; // address used as registry key

// ---------------------------------------------------------------------------
// helpers

PluginState* b2p_state( lua_State* L )
{
	(void)L;
	return gState;
}

PluginState* b2p_requireWorld( lua_State* L, const char* fname )
{
	PluginState* st = gState;
	if ( st == nullptr || B2_IS_NULL( st->world ) )
	{
		luaL_error( L, "%s: physics.start() has not been called.", fname );
	}
	return st;
}

float b2p_getNumberField( lua_State* L, int t, const char* key, float def )
{
	lua_getfield( L, t, key );
	float v = lua_isnumber( L, -1 ) ? (float)lua_tonumber( L, -1 ) : def;
	lua_pop( L, 1 );
	return v;
}

bool b2p_getBoolField( lua_State* L, int t, const char* key, bool def )
{
	lua_getfield( L, t, key );
	bool v = lua_isnil( L, -1 ) ? def : ( lua_toboolean( L, -1 ) != 0 );
	lua_pop( L, 1 );
	return v;
}

void b2p_warn( lua_State* L, const char* fmt, ... )
{
	char buffer[512];
	va_list args;
	va_start( args, fmt );
	vsnprintf( buffer, sizeof( buffer ), fmt, args );
	va_end( args );

	lua_getglobal( L, "print" );
	if ( lua_isfunction( L, -1 ) )
	{
		lua_pushfstring( L, "WARNING: " B2PLUGIN_NAME ": %s", buffer );
		if ( lua_pcall( L, 1, 0, 0 ) != 0 )
		{
			lua_pop( L, 1 );
		}
	}
	else
	{
		lua_pop( L, 1 );
	}
}

static int AssertHandler( const char* condition, const char* fileName, int lineNumber )
{
	fprintf( stderr, "[" B2PLUGIN_NAME "] Box2D assert failed: %s (%s:%d)\n", condition, fileName, lineNumber );
	return 0; // do not break into the debugger / crash the Simulator
}

// Runtime:addEventListener / removeEventListener( "enterFrame", fn )
static void RuntimeListener( lua_State* L, const char* method, const char* eventName, int fnRef )
{
	lua_getglobal( L, "Runtime" );
	if ( !lua_istable( L, -1 ) )
	{
		lua_pop( L, 1 );
		return;
	}
	lua_getfield( L, -1, method );
	lua_pushvalue( L, -2 );
	lua_pushstring( L, eventName );
	lua_rawgeti( L, LUA_REGISTRYINDEX, fnRef );
	lua_call( L, 3, 0 );
	lua_pop( L, 1 );
}

// ---------------------------------------------------------------------------
// multithreading (v3): Box2D's built-in scheduler behind our own task callbacks, so a step can be
// forced onto the Lua thread when preCollision listeners exist (pre-solve runs inside the step).

extern "C" {
typedef struct b2Scheduler b2Scheduler;
b2Scheduler* b2CreateScheduler( int workerCount );
void b2DestroyScheduler( b2Scheduler* scheduler );
void b2ResetScheduler( b2Scheduler* scheduler );
void* b2SchedulerEnqueueTask( b2TaskCallbackFcn* task, void* taskContext, void* userContext );
void b2SchedulerFinishTask( void* userTask, void* userContext );
}

static void* EnqueueTask( b2TaskCallbackFcn* task, void* taskContext, void* userContext )
{
	PluginState* st = (PluginState*)userContext;
	if ( st->serialTasks || st->scheduler == nullptr )
	{
		task( taskContext );
		return nullptr; // executed inline
	}
	return b2SchedulerEnqueueTask( task, taskContext, st->scheduler );
}

static void FinishTask( void* userTask, void* userContext )
{
	PluginState* st = (PluginState*)userContext;
	if ( userTask != nullptr && st->scheduler != nullptr )
	{
		b2SchedulerFinishTask( userTask, st->scheduler );
	}
}

static void DestroyScheduler( PluginState* st )
{
	if ( st->scheduler != nullptr )
	{
		b2DestroyScheduler( (b2Scheduler*)st->scheduler );
		st->scheduler = nullptr;
	}
}

// Creates / resizes the scheduler for st->threadCount. Only called while no step is running.
static void UpdateScheduler( PluginState* st )
{
	DestroyScheduler( st );
	if ( st->threadCount > 1 )
	{
		st->scheduler = b2CreateScheduler( st->threadCount );
	}
}

// Before each step: choose how many Box2D workers this step uses.
static void PrepareWorkers( PluginState* st )
{
	int wanted = ( st->anyPreSolve || st->scheduler == nullptr ) ? 1 : st->threadCount;
	st->serialTasks = wanted == 1;
	if ( b2World_GetWorkerCount( st->world ) != wanted )
	{
		b2World_SetWorkerCount( st->world, wanted );
	}
	if ( st->scheduler != nullptr )
	{
		b2ResetScheduler( (b2Scheduler*)st->scheduler );
	}
}

// ---------------------------------------------------------------------------
// stepping

static void StepOnce( lua_State* L, PluginState* st, float dt )
{
	b2p_scanListeners( L, st );
	if ( !st->touchHelpers.empty() )
	{
		b2p_cleanupTouchHelpers( st );
	}

	b2p_updateMovers( L, st, dt * st->timeScale ); // character movers query / push the world before it steps
	PrepareWorkers( st );

	uint64_t ticks = b2GetTicks();
	st->inStep = true;
	b2World_Step( st->world, dt * st->timeScale, st->subSteps ); // preCollision listeners run in here
	st->inStep = false;
	st->frameStats.stepTime += b2GetMillisecondsAndReset( &ticks );

	// Move events point at our Body records: sync before destroying bodies removed during the step.
	b2p_syncBodies( L, st );
	b2p_processPendingDestroys( L, st );
	st->frameStats.syncTime += b2GetMillisecondsAndReset( &ticks );

	if ( B2_IS_NON_NULL( st->world ) )
	{
		b2p_dispatchStepEvents( L, st ); // collision / postCollision listeners
		b2p_processPendingDestroys( L, st );
	}
	if ( B2_IS_NON_NULL( st->world ) )
	{
		b2p_processJointEvents( L, st ); // "jointBreak"
		b2p_processPendingDestroys( L, st );
	}
	st->frameStats.eventTime += b2GetMilliseconds( ticks );
	st->frameStats.stepCount += 1;
}

static void EnterFrameSteps( lua_State* L, PluginState* st );

// enterFrame listener
static int OnEnterFrame( lua_State* L )
{
	PluginState* st = gState;
	if ( st == nullptr || B2_IS_NULL( st->world ) )
	{
		return 0;
	}
	if ( !st->running )
	{
		b2p_renderDebugDraw( L, st ); // keep showing the debug draw while paused
		return 0;
	}

	uint64_t ticks = b2GetTicks();
	st->frameStats = FrameStats{};
	EnterFrameSteps( L, st );
	st->frameStats.frameTime = b2GetMilliseconds( ticks );
	b2p_renderDebugDraw( L, st );
	b2p_raisePendingError( L, st ); // a listener failed: report it like a normal Lua error
	return 0;
}

static void EnterFrameSteps( lua_State* L, PluginState* st )
{
	if ( st->timeStep > 0.0f )
	{
		StepOnce( L, st, st->timeStep );
	}
	else if ( st->timeStep < 0.0f )
	{
		StepOnce( L, st, st->frameInterval );
	}
	else
	{
		// Time based: fixed steps of frameInterval that track real elapsed time (v2 behaviour).
		double now = 0.0;
		if ( lua_istable( L, 1 ) )
		{
			lua_getfield( L, 1, "time" );
			now = lua_tonumber( L, -1 ) * 0.001; // keep double precision for long sessions
			lua_pop( L, 1 );
		}
		float dt = st->frameInterval;
		double previous = st->timePrevious > 0.0 ? st->timePrevious : now - dt;
		float elapsed = (float)( now - previous ) + st->timeRemainder;

		// Avoid a spiral of death after a long hitch (e.g. app resumed).
		const int maxSteps = 8;
		int steps = 0;
		while ( elapsed >= dt && steps < maxSteps )
		{
			StepOnce( L, st, dt );
			elapsed -= dt;
			++steps;
			if ( B2_IS_NULL( st->world ) || !st->running )
			{
				return; // physics.stop() from inside a listener
			}
		}
		if ( steps == maxSteps )
		{
			elapsed = 0.0f;
		}
		st->timePrevious = now;
		st->timeRemainder = elapsed;
	}
}

// ---------------------------------------------------------------------------
// world lifecycle

static void DestroyWorld( lua_State* L, PluginState* st, bool luaAlive )
{
	if ( B2_IS_NULL( st->world ) )
	{
		return;
	}
	b2p_destroyAllBodies( L, luaAlive );
	b2p_destroyAllMovers( luaAlive ? L : nullptr, st );
	if ( luaAlive )
	{
		b2p_resetJoints( L, st );
	}
	else
	{
		st->jointKeys.clear();
	}
	st->touchHelpers.clear();
	if ( luaAlive )
	{
		b2p_removeDebugDraw( L, st );
	}
	b2DestroyWorld( st->world );
	st->world = b2_nullWorldId;
	st->running = false;
	if ( luaAlive && st->enterFrameRef != LUA_NOREF )
	{
		RuntimeListener( L, "removeEventListener", "enterFrame", st->enterFrameRef );
	}
}

// physics.start( [noSleep] )
static int start( lua_State* L )
{
	PluginState* st = gState;

	if ( B2_IS_NON_NULL( st->world ) )
	{
		st->running = true;
		return 0;
	}

	bool noSleep = lua_toboolean( L, 1 ) != 0;

	b2WorldDef def = b2DefaultWorldDef();
	def.gravity = st->gravity;
	def.enableSleep = !noSleep;
	def.workerCount = st->threadCount > 1 ? st->threadCount : 1;
	def.enqueueTask = EnqueueTask;
	def.finishTask = FinishTask;
	def.userTaskContext = st;
	def.hitEventThreshold = st->hitThreshold;
	def.contactHertz = st->contactHertz;
	def.contactDampingRatio = st->contactDampingRatio;
	def.contactSpeed = st->contactPushSpeed;
	def.restitutionThreshold = st->restitutionThreshold;
	def.maximumLinearSpeed = st->maxLinearSpeed;
	if ( st->threadCount > 1 && st->scheduler == nullptr )
	{
		UpdateScheduler( st );
	}
	st->world = b2CreateWorld( &def );
	st->worldEpoch += 1;
	b2p_attachWorldCallbacks( st );

	// v2: default time step equals the frame interval
	lua_getglobal( L, "display" );
	float fps = lua_istable( L, -1 ) ? b2p_getNumberField( L, -1, "fps", 30.0f ) : 30.0f;
	lua_pop( L, 1 );
	st->frameInterval = 1.0f / ( fps > 0.0f ? fps : 30.0f );
	st->timePrevious = -1.0;
	st->timeRemainder = 0.0f;
	st->running = true;

	RuntimeListener( L, "addEventListener", "enterFrame", st->enterFrameRef );
	return 0;
}

// physics.pause()
static int pause( lua_State* L )
{
	b2p_requireWorld( L, "physics.pause()" );
	gState->running = false;
	return 0;
}

// physics.stop() -> boolean
static int stop( lua_State* L )
{
	PluginState* st = gState;
	if ( B2_IS_NULL( st->world ) )
	{
		lua_pushboolean( L, 0 );
		return 1;
	}
	if ( st->inStep )
	{
		luaL_error( L, "physics.stop() cannot be called during a world step (e.g. inside a collision listener)." );
	}
	DestroyWorld( L, st, true );
	lua_pushboolean( L, 1 );
	return 1;
}

// physics.setGravity( gx, gy )
static int setGravity( lua_State* L )
{
	PluginState* st = gState;
	st->gravity.x = (float)luaL_checknumber( L, 1 );
	st->gravity.y = (float)luaL_checknumber( L, 2 );
	if ( B2_IS_NON_NULL( st->world ) )
	{
		b2World_SetGravity( st->world, st->gravity );
		// v2 behaviour: changing gravity affects sleeping bodies too
		for ( Body* b : st->bodies )
		{
			if ( !b->destroyed && b2Body_GetType( b->id ) == b2_dynamicBody )
			{
				b2Body_SetAwake( b->id, true );
			}
		}
	}
	return 0;
}

// physics.getGravity() -> gx, gy
static int getGravity( lua_State* L )
{
	PluginState* st = gState;
	lua_pushnumber( L, st->gravity.x );
	lua_pushnumber( L, st->gravity.y );
	return 2;
}

// physics.setScale( pixelsPerMeter )
static int setScale( lua_State* L )
{
	PluginState* st = gState;
	float s = (float)luaL_checknumber( L, 1 );
	if ( s <= 0.0f )
	{
		luaL_error( L, "physics.setScale() requires a positive number." );
	}
	if ( !st->bodies.empty() )
	{
		b2p_warn( L, "physics.setScale() called after bodies were added; existing bodies keep their size in meters." );
	}
	st->scale = s;
	return 0;
}

// physics.getScale() -> pixelsPerMeter   (new)
static int getScale( lua_State* L )
{
	lua_pushnumber( L, gState->scale );
	return 1;
}

// physics.setTimeStep( dt )   dt > 0 fixed, dt < 0 frame interval (default), dt == 0 time based
static int setTimeStep( lua_State* L )
{
	PluginState* st = gState;
	st->timeStep = (float)luaL_checknumber( L, 1 );
	st->timePrevious = -1.0;
	st->timeRemainder = 0.0f;
	return 0;
}

static int setTimeScale( lua_State* L )
{
	float s = (float)luaL_checknumber( L, 1 );
	gState->timeScale = s < 0.0f ? 0.0f : s;
	return 0;
}

static int getTimeScale( lua_State* L )
{
	lua_pushnumber( L, gState->timeScale );
	return 1;
}

// physics.setSubSteps( n )   (new in v3, replaces velocity/position iterations, default 4)
static int setSubSteps( lua_State* L )
{
	int n = (int)luaL_checkinteger( L, 1 );
	gState->subSteps = n < 1 ? 1 : n;
	return 0;
}

static int getSubSteps( lua_State* L )
{
	lua_pushinteger( L, gState->subSteps );
	return 1;
}

// physics.setContactTuning( hertz, [dampingRatio], [pushSpeed] )   (new in v3)
// Contact stiffness. Box2D caps hertz at 1/8 of the sub-step rate (60 fps, 4 sub-steps -> 30 Hz), so
// raise physics.setSubSteps() too when raising hertz. Stiffer contacts sink less under heavy stacks
// or large mass ratios.
static int setContactTuning( lua_State* L )
{
	PluginState* st = gState;
	st->contactHertz = (float)luaL_checknumber( L, 1 );
	st->contactDampingRatio = (float)luaL_optnumber( L, 2, st->contactDampingRatio );
	st->contactPushSpeed = (float)luaL_optnumber( L, 3, st->contactPushSpeed );
	if ( st->contactHertz < 0.0f ) st->contactHertz = 0.0f;
	if ( st->contactDampingRatio < 0.0f ) st->contactDampingRatio = 0.0f;
	if ( st->contactPushSpeed < 0.0f ) st->contactPushSpeed = 0.0f;
	if ( B2_IS_NON_NULL( st->world ) )
	{
		b2World_SetContactTuning( st->world, st->contactHertz, st->contactDampingRatio, st->contactPushSpeed );
	}
	return 0;
}

// physics.getContactTuning() -> hertz, dampingRatio, pushSpeed, effectiveHertz
static int getContactTuning( lua_State* L )
{
	PluginState* st = gState;
	float inv_h = (float)st->subSteps / ( st->timeStep > 0.0f ? st->timeStep : st->frameInterval );
	float effective = st->contactHertz < 0.125f * inv_h ? st->contactHertz : 0.125f * inv_h;
	lua_pushnumber( L, st->contactHertz );
	lua_pushnumber( L, st->contactDampingRatio );
	lua_pushnumber( L, st->contactPushSpeed );
	lua_pushnumber( L, effective );
	return 4;
}

// ---------------------------------------------------------------------------
// MKS helpers (v2)

// physics.toMKS( unit, value ) / physics.fromMKS( unit, value ): "length", "velocity", "angularVelocity"
static int ConvertMKS( lua_State* L, bool toMKS, const char* fname )
{
	PluginState* st = gState;
	const char* unit = luaL_checkstring( L, 1 );
	double v = luaL_checknumber( L, 2 );
	const double pi = 3.14159265358979323846;
	if ( strcmp( unit, "length" ) == 0 || strcmp( unit, "velocity" ) == 0 )
	{
		v = toMKS ? v / st->scale : v * st->scale;
	}
	else if ( strcmp( unit, "angularVelocity" ) == 0 )
	{
		v = toMKS ? v * pi / 180.0 : v * 180.0 / pi;
	}
	else
	{
		return luaL_error( L, "%s unit type '%s' not recognized", fname, unit );
	}
	lua_pushnumber( L, v );
	return 1;
}

static int toMKS( lua_State* L )
{
	return ConvertMKS( L, true, "physics.toMKS()" );
}

static int fromMKS( lua_State* L )
{
	return ConvertMKS( L, false, "physics.fromMKS()" );
}

void b2p_applyWorldSettings( PluginState* st )
{
	if ( B2_IS_NULL( st->world ) )
	{
		return;
	}
	b2World_SetGravity( st->world, st->gravity );
	b2World_SetContactTuning( st->world, st->contactHertz, st->contactDampingRatio, st->contactPushSpeed );
	b2World_SetRestitutionThreshold( st->world, st->restitutionThreshold );
	b2World_SetMaxLinearSpeed( st->world, st->maxLinearSpeed );
	b2World_SetHitEventThreshold( st->world, st->hitThreshold );
}

static void ApplyContactTuning( PluginState* st )
{
	if ( B2_IS_NON_NULL( st->world ) )
	{
		b2World_SetContactTuning( st->world, st->contactHertz, st->contactDampingRatio, st->contactPushSpeed );
	}
}

// physics.setMKS( key, value ) -> boolean
// v2 keys: velocityThreshold, linearSleepTolerance (supported); timeToSleep, maxSubSteps,
// angularSleepTolerance (no Box2D v3 equivalent: warning, returns false).
// v3 keys: contactHertz, contactDampingRatio, contactPushSpeed, maxLinearSpeed.
static int setMKS( lua_State* L )
{
	PluginState* st = gState;
	if ( st->inStep )
	{
		return luaL_error( L, "physics.setMKS() cannot be called while the world is stepping." );
	}
	const char* key = luaL_checkstring( L, 1 );
	float v = (float)luaL_checknumber( L, 2 );
	bool ok = true;
	if ( strcmp( key, "velocityThreshold" ) == 0 )
	{
		st->restitutionThreshold = v;
		if ( B2_IS_NON_NULL( st->world ) )
		{
			b2World_SetRestitutionThreshold( st->world, v );
		}
	}
	else if ( strcmp( key, "linearSleepTolerance" ) == 0 )
	{
		st->sleepThreshold = v;
		for ( Body* b : st->bodies )
		{
			if ( !b->destroyed )
			{
				b2Body_SetSleepThreshold( b->id, v );
			}
		}
	}
	else if ( strcmp( key, "contactHertz" ) == 0 )
	{
		st->contactHertz = v;
		ApplyContactTuning( st );
	}
	else if ( strcmp( key, "contactDampingRatio" ) == 0 )
	{
		st->contactDampingRatio = v;
		ApplyContactTuning( st );
	}
	else if ( strcmp( key, "contactPushSpeed" ) == 0 )
	{
		st->contactPushSpeed = v;
		ApplyContactTuning( st );
	}
	else if ( strcmp( key, "maxLinearSpeed" ) == 0 )
	{
		st->maxLinearSpeed = v;
		if ( B2_IS_NON_NULL( st->world ) )
		{
			b2World_SetMaxLinearSpeed( st->world, v );
		}
	}
	else if ( strcmp( key, "timeToSleep" ) == 0 || strcmp( key, "maxSubSteps" ) == 0 ||
			  strcmp( key, "angularSleepTolerance" ) == 0 )
	{
		b2p_warn( L, "physics.setMKS(): \"%s\" has no equivalent in Box2D v3 and is ignored.", key );
		ok = false;
	}
	else
	{
		b2p_warn( L, "physics.setMKS() does not support the %s property", key );
		ok = false;
	}
	lua_pushboolean( L, ok );
	return 1;
}

// physics.getMKS( key ) -> number
static int getMKS( lua_State* L )
{
	PluginState* st = gState;
	const char* key = luaL_checkstring( L, 1 );
	float v = 0.0f;
	if ( strcmp( key, "velocityThreshold" ) == 0 )
	{
		v = st->restitutionThreshold;
	}
	else if ( strcmp( key, "linearSleepTolerance" ) == 0 )
	{
		v = st->sleepThreshold;
	}
	else if ( strcmp( key, "contactHertz" ) == 0 )
	{
		v = st->contactHertz;
	}
	else if ( strcmp( key, "contactDampingRatio" ) == 0 )
	{
		v = st->contactDampingRatio;
	}
	else if ( strcmp( key, "contactPushSpeed" ) == 0 )
	{
		v = st->contactPushSpeed;
	}
	else if ( strcmp( key, "maxLinearSpeed" ) == 0 )
	{
		v = st->maxLinearSpeed;
	}
	else if ( strcmp( key, "timeToSleep" ) == 0 )
	{
		v = B2_TIME_TO_SLEEP;
	}
	else
	{
		b2p_warn( L, "physics.getMKS() does not support the %s property", key );
	}
	lua_pushnumber( L, v );
	return 1;
}

// physics.setReportCollisionsInContentCoordinates( bool ) etc. (v2)
static int setReportCollisionsInContentCoordinates( lua_State* L )
{
	gState->reportContentCoords = lua_toboolean( L, 1 ) != 0;
	return 0;
}
static int getReportCollisionsInContentCoordinates( lua_State* L )
{
	lua_pushboolean( L, gState->reportContentCoords );
	return 1;
}
static int setAverageCollisionPositions( lua_State* L )
{
	gState->averagePositions = lua_toboolean( L, 1 ) != 0;
	return 0;
}
static int getAverageCollisionPositions( lua_State* L )
{
	lua_pushboolean( L, gState->averagePositions );
	return 1;
}
static int setDebugErrorsEnabled( lua_State* L )
{
	gState->debugErrors = lua_toboolean( L, 1 ) != 0;
	return 0;
}
static int getDebugErrorsEnabled( lua_State* L )
{
	lua_pushboolean( L, gState->debugErrors );
	return 1;
}

// physics.setVelocityIterations / setPositionIterations: accepted for compatibility.
// Box2D v3 uses sub-stepping instead; see physics.setSubSteps().
static int setIterationsNoop( lua_State* L )
{
	luaL_checknumber( L, 1 );
	return 0;
}

// physics.setContinuous( enabled )
static int setContinuous( lua_State* L )
{
	PluginState* st = b2p_requireWorld( L, "physics.setContinuous()" );
	b2World_EnableContinuous( st->world, lua_toboolean( L, 1 ) != 0 );
	return 0;
}

// physics.setThreadCount( n )   (v3) number of threads Box2D uses, including the main thread.
// preCollision listeners force single threaded steps (Lua runs inside pre-solve).
static int setThreadCount( lua_State* L )
{
	PluginState* st = gState;
	if ( st->inStep )
	{
		return luaL_error( L, "physics.setThreadCount() cannot be called during a collision event." );
	}
	int n = (int)luaL_checkinteger( L, 1 );
	n = n < 1 ? 1 : ( n > B2_MAX_WORKERS ? B2_MAX_WORKERS : n );
	if ( n != st->threadCount )
	{
		st->threadCount = n;
		UpdateScheduler( st );
		if ( B2_IS_NON_NULL( st->world ) )
		{
			b2World_SetWorkerCount( st->world, 1 ); // PrepareWorkers sets the real count before the next step
		}
	}
	return 0;
}

// physics.getThreadCount() -> configured count, count used by the last step
static int getThreadCount( lua_State* L )
{
	PluginState* st = gState;
	lua_pushinteger( L, st->threadCount );
	lua_pushinteger( L, B2_IS_NON_NULL( st->world ) ? b2World_GetWorkerCount( st->world ) : 1 );
	return 2;
}

// physics.setHitEventThreshold( speed )   pixels / second; "hit" events need a faster approach
static int setHitEventThreshold( lua_State* L )
{
	PluginState* st = gState;
	float v = (float)luaL_checknumber( L, 1 );
	st->hitThreshold = b2p_toMeters( st, v > 0.0f ? v : 0.0f );
	if ( B2_IS_NON_NULL( st->world ) )
	{
		b2World_SetHitEventThreshold( st->world, st->hitThreshold );
	}
	return 0;
}

static int getHitEventThreshold( lua_State* L )
{
	PluginState* st = gState;
	lua_pushnumber( L, b2p_toPixels( st, st->hitThreshold ) );
	return 1;
}

// physics.explode( x, y, radius, impulsePerLength, [falloff], [maskBits] )   (v3)
// Position, radius and falloff in pixels. impulsePerLength in N*s per meter of the shape outline that
// faces the explosion (negative: implosion). Applies to circles, capsules and polygons.
static int explode( lua_State* L )
{
	PluginState* st = b2p_requireWorld( L, "physics.explode()" );
	if ( st->inStep )
	{
		return luaL_error( L, "physics.explode() cannot be called during preCollision." );
	}
	b2ExplosionDef def = b2DefaultExplosionDef();
	def.position = b2Pos{ b2p_toMeters( st, (float)luaL_checknumber( L, 1 ) ), b2p_toMeters( st, (float)luaL_checknumber( L, 2 ) ) };
	float radius = (float)luaL_checknumber( L, 3 );
	def.radius = b2p_toMeters( st, radius > 0.0f ? radius : 0.0f );
	def.impulsePerLength = (float)luaL_checknumber( L, 4 );
	float falloff = (float)luaL_optnumber( L, 5, 0.0 );
	def.falloff = b2p_toMeters( st, falloff > 0.0f ? falloff : 0.0f );
	if ( lua_isnumber( L, 6 ) )
	{
		def.maskBits = (uint64_t)lua_tonumber( L, 6 );
	}
	b2World_Explode( st->world, &def );
	return 0;
}

// physics.getStats() -> table   (new)
static int getStats( lua_State* L )
{
	PluginState* st = gState;
	lua_newtable( L );
	lua_pushinteger( L, (lua_Integer)st->bodies.size() );
	lua_setfield( L, -2, "bodyCount" );
	if ( B2_IS_NON_NULL( st->world ) )
	{
		b2Counters c = b2World_GetCounters( st->world );
		b2Profile p = b2World_GetProfile( st->world );
		lua_pushinteger( L, c.shapeCount );
		lua_setfield( L, -2, "shapeCount" );
		lua_pushinteger( L, c.bodyCount );
		lua_setfield( L, -2, "worldBodyCount" ); // includes internal bodies (touch joint helpers)
		lua_pushinteger( L, c.contactCount );
		lua_setfield( L, -2, "contactCount" );
		lua_pushinteger( L, c.jointCount );
		lua_setfield( L, -2, "jointCount" );
		lua_pushinteger( L, c.islandCount );
		lua_setfield( L, -2, "islandCount" );
		lua_pushinteger( L, b2World_GetAwakeBodyCount( st->world ) );
		lua_setfield( L, -2, "awakeBodyCount" );
		lua_pushinteger( L, b2World_GetWorkerCount( st->world ) );
		lua_setfield( L, -2, "threadCount" ); // threads used by the last step
		(void)p;
	}
	// timings of the last enterFrame, in milliseconds
	lua_pushnumber( L, st->frameStats.stepTime );
	lua_setfield( L, -2, "stepTime" ); // Box2D b2World_Step (all steps of the frame)
	lua_pushnumber( L, st->frameStats.syncTime );
	lua_setfield( L, -2, "syncTime" ); // writing x / y / rotation to display objects
	lua_pushnumber( L, st->frameStats.eventTime );
	lua_setfield( L, -2, "eventTime" ); // collision / postCollision dispatch
	lua_pushnumber( L, st->frameStats.frameTime );
	lua_setfield( L, -2, "frameTime" ); // whole physics enterFrame
	lua_pushinteger( L, st->frameStats.stepCount );
	lua_setfield( L, -2, "stepCount" );
	lua_pushboolean( L, st->running );
	lua_setfield( L, -2, "running" );
	return 1;
}

// ---------------------------------------------------------------------------
// lifetime

static int SentinelGC( lua_State* L )
{
	PluginState* st = gState;
	if ( st != nullptr )
	{
		DestroyWorld( L, st, false );
		DestroyScheduler( st );
		delete st;
		gState = nullptr;
	}
	return 0;
}

// CORONA_PUBLIC_SUFFIX: default visibility on Android, where the plugin is built with -fvisibility=hidden
CORONA_EXPORT int luaopen_plugin_box2dv3( lua_State* L ) CORONA_PUBLIC_SUFFIX;
#if defined( __GNUC__ ) && !defined( _WIN32 )
__attribute__( ( visibility( "default" ) ) )
#endif
CORONA_EXPORT int luaopen_plugin_box2dv3( lua_State* L )
{
	if ( gState != nullptr )
	{
		// Stale state from a previous Lua state that was not collected (should not happen).
		delete gState;
		gState = nullptr;
	}

	b2SetAssertCallback( AssertHandler );

	PluginState* st = new PluginState();
	st->L = L;
	st->world = b2_nullWorldId;
	st->running = false;
	st->scale = 30.0f;
	st->gravity = b2Vec2{ 0.0f, 9.8f };
	st->timeStep = -1.0f;
	st->frameInterval = 1.0f / 30.0f;
	st->timeScale = 1.0f;
	st->subSteps = 4;
	{
		b2WorldDef d = b2DefaultWorldDef();
		st->contactHertz = d.contactHertz;
		st->contactDampingRatio = d.contactDampingRatio;
		st->contactPushSpeed = d.contactSpeed;
		st->restitutionThreshold = d.restitutionThreshold;
		st->maxLinearSpeed = d.maximumLinearSpeed;
		st->sleepThreshold = b2DefaultBodyDef().sleepThreshold;
	}
	st->timePrevious = -1.0;
	st->timeRemainder = 0.0f;
	st->inStep = false;
	st->frameStats = FrameStats{};
	st->reportContentCoords = true; // collision x / y in content coordinates (v2 default was body-local)
	st->averagePositions = false;
	st->debugErrors = false;
	st->runtimePre = false;
	st->runtimePost = false;
	st->anyPost = false;
	st->anyHit = false;
	st->anyPreSolve = false;
	st->threadCount = 1;
	st->scheduler = nullptr;
	st->serialTasks = true;
	st->hitThreshold = b2DefaultWorldDef().hitEventThreshold;
	st->worldEpoch = 0;
	st->nextSerial = 0;
	st->drawMode = 0;
	st->drawGroupRef = LUA_NOREF;
	st->drawContacts = false;
	gState = st;

	lua_pushcfunction( L, OnEnterFrame );
	st->enterFrameRef = luaL_ref( L, LUA_REGISTRYINDEX );

	// Destroy the world when this Lua state closes (Simulator relaunch).
	lua_pushlightuserdata( L, (void*)&kSentinelKey );
	lua_newuserdata( L, 1 );
	lua_newtable( L );
	lua_pushcfunction( L, SentinelGC );
	lua_setfield( L, -2, "__gc" );
	lua_setmetatable( L, -2 );
	lua_rawset( L, LUA_REGISTRYINDEX );

	b2p_initBodyModule( L );
	b2p_initEvents( L );
	b2p_initJoints( L );

	const luaL_Reg functions[] = {
		{ "start", start },
		{ "pause", pause },
		{ "stop", stop },
		{ "setGravity", setGravity },
		{ "getGravity", getGravity },
		{ "setScale", setScale },
		{ "getScale", getScale },
		{ "setTimeStep", setTimeStep },
		{ "setTimeScale", setTimeScale },
		{ "getTimeScale", getTimeScale },
		{ "setSubSteps", setSubSteps },
		{ "getSubSteps", getSubSteps },
		{ "setContactTuning", setContactTuning },
		{ "setReportCollisionsInContentCoordinates", setReportCollisionsInContentCoordinates },
		{ "getReportCollisionsInContentCoordinates", getReportCollisionsInContentCoordinates },
		{ "setAverageCollisionPositions", setAverageCollisionPositions },
		{ "getAverageCollisionPositions", getAverageCollisionPositions },
		{ "setDebugErrorsEnabled", setDebugErrorsEnabled },
		{ "getDebugErrorsEnabled", getDebugErrorsEnabled },
		{ "toMKS", toMKS },
		{ "fromMKS", fromMKS },
		{ "setMKS", setMKS },
		{ "getMKS", getMKS },
		{ "getContactTuning", getContactTuning },
		{ "setVelocityIterations", setIterationsNoop },
		{ "setPositionIterations", setIterationsNoop },
		{ "setContinuous", setContinuous },
		{ "addBody", b2p_addBody },
		{ "removeBody", b2p_removeBody },
		{ "newJoint", b2p_newJoint },
		{ "getStats", getStats },
		{ "setThreadCount", setThreadCount },
		{ "getThreadCount", getThreadCount },
		{ "setHitEventThreshold", setHitEventThreshold },
		{ "getHitEventThreshold", getHitEventThreshold },
		{ "explode", explode },
		{ nullptr, nullptr },
	};

	lua_newtable( L );
	luaL_register( L, nullptr, functions );
	b2p_registerQueries( L, lua_gettop( L ) );
	b2p_registerDraw( L, lua_gettop( L ) );
	b2p_registerMover( L, lua_gettop( L ) );
	b2p_registerSnapshot( L, lua_gettop( L ) );

	b2Version v = b2GetVersion();
	lua_pushfstring( L, "Box2D %d.%d.%d", v.major, v.minor, v.revision );
	lua_setfield( L, -2, "engineVersion" );
	lua_pushstring( L, B2PLUGIN_VERSION );
	lua_setfield( L, -2, "version" );

	return 1;
}

// plugin.box2dv3 — world snapshots (Box2D v3 b2World_GetSnapshot / b2World_RestoreFromSnapshot), new API:
//
//   local state = physics.saveState()           -- between frames, not inside preCollision
//   local removed = physics.restoreState( state ) -- back to that instant
//
// Restores every body (position, rotation, velocity, sleep state, body type...), contact, joint and
// character mover to the saved instant. Use it for undo, rollback or "retry from checkpoint".
// The plugin settings (gravity, scale, contact tuning, hit threshold...) are kept as they are now.
//
// Display objects are not created or removed:
//   - bodies added after the save are removed (like physics.removeBody); their display objects are
//     returned in `removed` so the app can delete them;
//   - bodies whose display object was removed since the save stay removed;
//   - joints created after the save are removed (their userdata reads as removed), joints removed
//     since the save are not brought back.
// A state belongs to the world it was saved from (physics.stop() discards it).
#include "b2plugin.h"

#include <string.h>
#include <unordered_map>

static char kStateMt = 0;

struct SavedBody
{
	uint64_t serial;
	b2BodyId id;
	float lastRotation;
	std::vector<ElementRecord> elements;
	std::vector<std::pair<b2ShapeId, int>> shapes; // shape -> element index (Box2D drops user data)
};

struct Snapshot
{
	uint32_t epoch;
	std::vector<uint8_t> image;
	std::vector<SavedBody> bodies;
	std::vector<b2JointId> joints;
	std::vector<PluginState::TouchHelper> helpers;
	void* movers;
	size_t moverCount;
};

struct StateUD
{
	Snapshot* snap;
};

static int StateGC( lua_State* L )
{
	StateUD* ud = (StateUD*)lua_touserdata( L, 1 );
	if ( ud != nullptr && ud->snap != nullptr )
	{
		b2p_freeMoverSave( ud->snap->movers );
		delete ud->snap;
		ud->snap = nullptr;
	}
	return 0;
}

static int StateIndex( lua_State* L )
{
	StateUD* ud = (StateUD*)lua_touserdata( L, 1 );
	const char* key = lua_tostring( L, 2 );
	if ( ud == nullptr || ud->snap == nullptr || key == nullptr )
	{
		return 0;
	}
	if ( strcmp( key, "size" ) == 0 ) // bytes of the Box2D image
	{
		lua_pushinteger( L, (lua_Integer)ud->snap->image.size() );
		return 1;
	}
	if ( strcmp( key, "bodyCount" ) == 0 )
	{
		lua_pushinteger( L, (lua_Integer)ud->snap->bodies.size() );
		return 1;
	}
	return 0;
}

static int StateToString( lua_State* L )
{
	StateUD* ud = (StateUD*)lua_touserdata( L, 1 );
	lua_pushfstring( L, "PhysicsState(%d bodies, %d bytes)", ud && ud->snap ? (int)ud->snap->bodies.size() : 0,
					 ud && ud->snap ? (int)ud->snap->image.size() : 0 );
	return 1;
}

// physics.saveState() -> state
static int saveState( lua_State* L )
{
	PluginState* st = b2p_requireWorld( L, "physics.saveState()" );
	if ( st->inStep )
	{
		return luaL_error( L, "physics.saveState() cannot be called while the world is stepping." );
	}
	b2p_processPendingDestroys( L, st );

	StateUD* ud = (StateUD*)lua_newuserdata( L, sizeof( StateUD ) );
	ud->snap = nullptr;
	lua_pushlightuserdata( L, (void*)&kStateMt );
	lua_rawget( L, LUA_REGISTRYINDEX );
	lua_setmetatable( L, -2 );

	Snapshot* s = new Snapshot();
	s->movers = nullptr;
	s->moverCount = 0;
	ud->snap = s; // owned by the userdata from here on (no leak on a Lua error)
	s->epoch = st->worldEpoch;

	int size = b2World_GetSnapshot( st->world, nullptr, 0 );
	if ( size <= 0 )
	{
		return luaL_error( L, "physics.saveState(): Box2D could not take a snapshot." );
	}
	s->image.resize( (size_t)size );
	b2World_GetSnapshot( st->world, s->image.data(), size );

	s->bodies.reserve( st->bodies.size() );
	std::vector<b2ShapeId> shapes;
	for ( Body* b : st->bodies )
	{
		if ( b->destroyed || b->pendingDestroy || !b2Body_IsValid( b->id ) )
		{
			continue;
		}
		SavedBody sb;
		sb.serial = b->serial;
		sb.id = b->id;
		sb.lastRotation = b->lastRotation;
		sb.elements = b->elements;
		int n = b2Body_GetShapeCount( b->id );
		shapes.resize( (size_t)( n > 0 ? n : 0 ) );
		n = n > 0 ? b2Body_GetShapes( b->id, shapes.data(), n ) : 0;
		for ( int i = 0; i < n; ++i )
		{
			sb.shapes.emplace_back( shapes[(size_t)i], (int)(intptr_t)b2Shape_GetUserData( shapes[(size_t)i] ) );
		}
		s->bodies.push_back( std::move( sb ) );
	}
	b2p_saveJoints( st, s->joints );
	s->helpers = st->touchHelpers;
	s->movers = b2p_saveMovers( st, &s->moverCount );
	return 1;
}

// physics.restoreState( state ) -> removed objects (table)
static int restoreState( lua_State* L )
{
	PluginState* st = b2p_requireWorld( L, "physics.restoreState()" );
	if ( st->inStep )
	{
		return luaL_error( L, "physics.restoreState() cannot be called while the world is stepping." );
	}
	StateUD* ud = (StateUD*)luaL_checkudata( L, 1, "b2plugin.state" );
	Snapshot* s = ud->snap;
	if ( s == nullptr || s->epoch != st->worldEpoch )
	{
		return luaL_error( L, "physics.restoreState(): this state was saved from another world (physics.stop() discards states)." );
	}
	b2p_processPendingDestroys( L, st );

	if ( !b2World_RestoreFromSnapshot( st->world, s->image.data(), (int)s->image.size() ) )
	{
		return luaL_error( L, "physics.restoreState(): Box2D rejected the state." );
	}

	std::unordered_map<uint64_t, const SavedBody*> bySerial;
	for ( const SavedBody& sb : s->bodies )
	{
		bySerial[sb.serial] = &sb;
	}

	// current bodies: restore the records of the saved ones, collect the newer ones
	std::vector<Body*> newer;
	std::unordered_map<uint64_t, bool> kept;
	for ( Body* b : st->bodies )
	{
		if ( b->destroyed )
		{
			continue;
		}
		auto it = bySerial.find( b->serial );
		if ( it == bySerial.end() || !b2Body_IsValid( it->second->id ) )
		{
			newer.push_back( b );
			continue;
		}
		const SavedBody& sb = *it->second;
		kept[sb.serial] = true;
		b->id = sb.id;
		b->elements = sb.elements;
		b->lastRotation = sb.lastRotation;
		b2Body_SetUserData( b->id, b );
		for ( const auto& sh : sb.shapes )
		{
			if ( b2Shape_IsValid( sh.first ) )
			{
				b2Shape_SetUserData( sh.first, (void*)(intptr_t)sh.second );
			}
		}
	}

	// saved bodies whose display object is gone: destroy them again
	for ( const SavedBody& sb : s->bodies )
	{
		if ( kept.count( sb.serial ) == 0 && b2Body_IsValid( sb.id ) )
		{
			b2DestroyBody( sb.id );
		}
	}

	b2p_jointsAfterRestore( L, st, s->joints, s->helpers );
	b2p_applyWorldSettings( st );

	// bodies added after the save: removed like physics.removeBody(); returned to the app
	lua_createtable( L, (int)newer.size(), 0 );
	int result = lua_gettop( L );
	int n = 0;
	for ( Body* b : newer )
	{
		lua_rawgeti( L, LUA_REGISTRYINDEX, b->objRef );
		lua_rawseti( L, result, ++n );
		b->id = b2_nullBodyId; // its Box2D body no longer exists
		b2p_destroyBody( L, b, true );
	}

	for ( Body* b : st->bodies )
	{
		b2p_writeTransform( L, b );
	}
	b2p_restoreMovers( L, st, s->movers, s->moverCount );
	return 1;
}

void b2p_registerSnapshot( lua_State* L, int libIndex )
{
	luaL_newmetatable( L, "b2plugin.state" );
	lua_pushcfunction( L, StateGC );
	lua_setfield( L, -2, "__gc" );
	lua_pushcfunction( L, StateIndex );
	lua_setfield( L, -2, "__index" );
	lua_pushcfunction( L, StateToString );
	lua_setfield( L, -2, "__tostring" );
	lua_pushlightuserdata( L, (void*)&kStateMt );
	lua_pushvalue( L, -2 );
	lua_rawset( L, LUA_REGISTRYINDEX );
	lua_pop( L, 1 );

	lua_pushcfunction( L, saveState );
	lua_setfield( L, libIndex, "saveState" );
	lua_pushcfunction( L, restoreState );
	lua_setfield( L, libIndex, "restoreState" );
}

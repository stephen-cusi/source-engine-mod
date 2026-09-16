#include "cbase.h"
#include "luamanager.h"
#include "luasrclib.h"
#include "lbaseentity_shared.h"
#include "basescripted.h"
#include "mathlib/lvector.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

LUA_REGISTRATION_INIT( Entities );

#define MAX_ENTITYARRAY 1024

LUA_BINDING_BEGIN( Entities, Find, "library", "Finds the entity by its entity index" )
{
    int iEntity = LUA_BINDING_ARGUMENT( luaL_checknumber, 1, "entityIndex" );
    CBaseEntity *ent = CBaseEntity::Instance( iEntity );

    if ( !ent )
    {
        CBaseEntity::PushLuaInstanceSafe( L, NULL );
        return 1;
    }

    CBaseEntity::PushLuaInstanceSafe( L, ent );
    return 1;
}
LUA_BINDING_END( "Entity", "The found entity or NULL entity" )

#ifndef CLIENT_DLL
LUA_BINDING_BEGIN( Entities, CreateByName, "library", "Creates an entity by the given class name", "server" )
{
    const char *pszClassName = LUA_BINDING_ARGUMENT( luaL_checkstring, 1, "className" );

    LUA_EXPECTED_SCRIPTED_LIBRARY_BEGIN( LUA_SCRIPTEDENTITIESLIBNAME );
    CBaseEntity *pEntity = CreateEntityByName( pszClassName );
    LUA_EXPECTED_SCRIPTED_LIBRARY_END( LUA_SCRIPTEDENTITIESLIBNAME );

    if ( dynamic_cast< CBaseScripted * >( pEntity ) != NULL )
        DispatchSpawn( pEntity );

    CBaseEntity::PushLuaInstanceSafe( L, pEntity );
    return 1;
}
LUA_BINDING_END( "Entity", "The created entity" )
#endif

LUA_BINDING_BEGIN( Entities, CreatePredictedEntityByName, "class", "Create predicted entity by name." )
{
    const char *pszClassName = LUA_BINDING_ARGUMENT( luaL_checkstring, 1, "className" );
    const char *pszModule = LUA_BINDING_ARGUMENT( luaL_checkstring, 2, "module" );
    int nLine = LUA_BINDING_ARGUMENT( luaL_checknumber, 3, "line" );
    bool bPersists = LUA_BINDING_ARGUMENT_WITH_DEFAULT( luaL_optboolean, 4, false, "shouldPersist" );

    CBaseEntity::PushLuaInstanceSafe( L, CBaseEntity::CreatePredictedEntityByName( pszClassName, pszModule, nLine, bPersists ) );
    return 1;
}
LUA_BINDING_END( "Entity", "The predicted entity." )

LUA_BINDING_BEGIN( Entities, GetAlongRay, "library", "Finds all entities along the given ray." )
{
    CBaseEntity *pList[MAX_ENTITYARRAY];

    Vector start = LUA_BINDING_ARGUMENT( luaL_checkvector, 1, "start" );
    Vector end = LUA_BINDING_ARGUMENT( luaL_checkvector, 2, "end" );
    Vector mins = LUA_BINDING_ARGUMENT( luaL_checkvector, 3, "mins" );
    Vector maxs = LUA_BINDING_ARGUMENT( luaL_checkvector, 4, "maxs" );

    Ray_t ray;
    ray.Init( start, end, mins, maxs );

    int count = UTIL_EntitiesAlongRay( pList, MAX_ENTITYARRAY, ray, UINT_MAX );

    lua_newtable( L );

    for ( int i = 0; i < count; i++ )
    {
        lua_pushinteger( L, i );
        CBaseEntity::PushLuaInstanceSafe( L, pList[i] );
        lua_settable( L, -3 );
    }

    lua_pushinteger( L, count );
    return 2;
}
LUA_BINDING_END( "table", "A table of entities found.", "integer", "The number of entities found." )

LUA_BINDING_BEGIN( Entities, GetInBox, "library", "Finds all entities in the given box. Note that clientPartitionMask is only available on the client." )
{
    CBaseEntity *pList[MAX_ENTITYARRAY];

    Vector mins = LUA_BINDING_ARGUMENT( luaL_checkvector, 1, "mins" );
    Vector maxs = LUA_BINDING_ARGUMENT( luaL_checkvector, 2, "maxs" );
    int flagMask = LUA_BINDING_ARGUMENT_WITH_DEFAULT( luaL_optinteger, 3, PARTITION_CLIENT_NON_STATIC_EDICTS, "flagMask" );

#ifdef CLIENT_DLL
    int partitionMask = LUA_BINDING_ARGUMENT_WITH_DEFAULT( luaL_optinteger, 4, PARTITION_CLIENT_NON_STATIC_EDICTS, "clientPartitionMask" );
    int count = UTIL_EntitiesInBox( pList, MAX_ENTITYARRAY, mins, maxs, flagMask, partitionMask );
#else
    int count = UTIL_EntitiesInBox( pList, MAX_ENTITYARRAY, mins, maxs, flagMask );
#endif

    lua_newtable( L );

    for ( int i = 0; i < count; i++ )
    {
        lua_pushinteger( L, i );
        CBaseEntity::PushLuaInstanceSafe( L, pList[i] );
        lua_settable( L, -3 );
    }

    lua_pushinteger( L, count );
    return 2;
}
LUA_BINDING_END( "table", "A table of entities found.", "integer", "The number of entities found." )

LUA_BINDING_BEGIN( Entities, GetInSphere, "library", "Finds all entities in the given sphere. Note that clientPartitionMask is only available on the client." )
{
    CBaseEntity *pList[MAX_ENTITYARRAY];

    Vector position = LUA_BINDING_ARGUMENT( luaL_checkvector, 1, "position" );
    float radius = LUA_BINDING_ARGUMENT( luaL_checknumber, 2, "radius" );

    // HL2SB: 0 means "no flag filter" -- the server reads a non-zero mask as a
    // requirement (util.cpp:317) and this defaulted to
    // PARTITION_CLIENT_NON_STATIC_EDICTS = 1<<7 = FL_ATCONTROLS, so on the server
    // every call came back empty.  See the note on FindInSphere below.
    int flagMask = LUA_BINDING_ARGUMENT_WITH_DEFAULT( luaL_optinteger, 3, 0, "flagMask" );

#ifdef CLIENT_DLL
    int partitionMask = LUA_BINDING_ARGUMENT_WITH_DEFAULT( luaL_optinteger, 4, PARTITION_CLIENT_NON_STATIC_EDICTS, "partitionMask" );
    int count = UTIL_EntitiesInSphere( pList, MAX_ENTITYARRAY, position, radius, flagMask, partitionMask );
#else
    int count = UTIL_EntitiesInSphere( pList, MAX_ENTITYARRAY, position, radius, flagMask );
#endif

    lua_newtable( L );

    for ( int i = 0; i < count; i++ )
    {
        lua_pushinteger( L, i );
        CBaseEntity::PushLuaInstanceSafe( L, pList[i] );
        lua_settable( L, -3 );
    }

    lua_pushinteger( L, count );
    return 2;
}
LUA_BINDING_END( "table", "A table of entities found.", "integer", "The number of entities found." )

#ifdef GAME_DLL

LUA_BINDING_BEGIN( Entities, GetInPvs, "library", "Goes through the entities and find the ones that are in the PVS of the provided vector.", "server" )
{
    Vector vecViewOrigin = LUA_BINDING_ARGUMENT( luaL_checkvector, 1, "viewOrigin" );

    static byte pvs[MAX_MAP_CLUSTERS / 8];
    int clusterIndex = engine->GetClusterForOrigin( vecViewOrigin );

    if ( clusterIndex < 0 )
    {
        lua_newtable( L );
        lua_pushinteger( L, 0 );
        return 2;
    }

    engine->GetPVSForCluster( clusterIndex, sizeof( pvs ), pvs );

    int count = 0;

    lua_newtable( L );

    for ( CBaseEntity *pEntity = gEntList.NextEnt( NULL ); pEntity; pEntity = gEntList.NextEnt( pEntity ) )
    {
        // Only return attached ents.
        if ( !pEntity->edict() )
            continue;

        CBaseEntity *pParent = pEntity->GetRootMoveParent();

        Vector vecSurroundMins, vecSurroundMaxs;
        pParent->CollisionProp()->WorldSpaceSurroundingBounds( &vecSurroundMins, &vecSurroundMaxs );
        if ( !engine->CheckBoxInPVS( vecSurroundMins, vecSurroundMaxs, pvs, sizeof( pvs ) ) )
            continue;

        lua_pushinteger( L, count );
        CBaseEntity::PushLuaInstanceSafe( L, pEntity );
        lua_settable( L, -3 );
        count++;

        if ( count >= MAX_ENTITYARRAY )
        {
            DevWarning( "UTIL_EntitiesInPVS: Reached max (%d) entities in PVS (skipping the rest)\n", MAX_ENTITYARRAY );
            break;
        }
    }

    lua_pushinteger( L, count );

    return 2;
}
LUA_BINDING_END( "table", "A table of entities found.", "integer", "The number of entities found." )

// LUA_BINDING_BEGIN( Entities, AddPostClientMessageEntity, "library", "Adds an entity to the post client message list.", "server" )
//{
//     CBaseEntity *entity = LUA_BINDING_ARGUMENT( luaL_checkentity, 1, "entity" );
//
//     gEntList.AddPostClientMessageEntity( entity );
//     return 0;
// }
// LUA_BINDING_END()

LUA_BINDING_BEGIN( Entities, CleanDeleteList, "library", "Cleans up the delete list.", "server" )
{
    gEntList.CleanupDeleteList();
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( Entities, Clear, "library", "Clears the entity list.", "server" )
{
    gEntList.Clear();
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( Entities, FindByClass, "library", "Finds an entity by its class name", "server" )
{
    const char *className = LUA_BINDING_ARGUMENT( luaL_checkstring, 1, "className" );
    CBaseEntity *startEntity = LUA_BINDING_ARGUMENT_WITH_DEFAULT( luaL_optentity, 2, NULL, "startEntity" );

    CBaseEntity::PushLuaInstanceSafe( L, gEntList.FindEntityByClassname( startEntity, className ) );
    return 1;
}
LUA_BINDING_END( "Entity", "The entity found, or NULL if not found." )

LUA_BINDING_BEGIN( Entities, FindByClassNearest, "library", "Finds the nearest entity by its class name", "server" )
{
    const char *className = LUA_BINDING_ARGUMENT( luaL_checkstring, 1, "className" );
    Vector position = LUA_BINDING_ARGUMENT( luaL_checkvector, 2, "position" );
    float maxDistance = LUA_BINDING_ARGUMENT( luaL_checknumber, 3, "maxDistance" );

    CBaseEntity::PushLuaInstanceSafe( L, gEntList.FindEntityByClassnameNearest( className, position, maxDistance ) );
    return 1;
}
LUA_BINDING_END( "Entity", "The entity found, or NULL if not found." )

LUA_BINDING_BEGIN( Entities, FindByClassWithin, "library", "Finds an entity by its class name within a radius", "server" )
{
    const char *className = LUA_BINDING_ARGUMENT( luaL_checkstring, 1, "className" );
    Vector position = LUA_BINDING_ARGUMENT( luaL_checkvector, 2, "position" );
    float maxDistance = LUA_BINDING_ARGUMENT( luaL_checknumber, 3, "maxDistance" );
    CBaseEntity *startEntity = LUA_BINDING_ARGUMENT_WITH_DEFAULT( luaL_optentity, 4, NULL, "startEntity" );

    CBaseEntity::PushLuaInstanceSafe( L, gEntList.FindEntityByClassnameWithin( startEntity, className, position, maxDistance ) );
    return 1;
}
LUA_BINDING_END( "Entity", "The entity found, or NULL if not found." )

LUA_BINDING_BEGIN( Entities, FindByModel, "library", "Finds an entity by its model name", "server" )
{
    const char *modelName = LUA_BINDING_ARGUMENT( luaL_checkstring, 1, "modelName" );
    CBaseEntity *startEntity = LUA_BINDING_ARGUMENT_WITH_DEFAULT( luaL_optentity, 2, NULL, "startEntity" );

    CBaseEntity::PushLuaInstanceSafe( L, gEntList.FindEntityByModel( startEntity, modelName ) );
    return 1;
}
LUA_BINDING_END( "Entity", "The entity found, or NULL if not found." )

LUA_BINDING_BEGIN( Entities, FindByName, "library", "Finds an entity by its name", "server" )
{
    const char *name = LUA_BINDING_ARGUMENT( luaL_checkstring, 1, "name" );
    CBaseEntity *startEntity = LUA_BINDING_ARGUMENT_WITH_DEFAULT( luaL_optentity, 2, NULL, "startEntity" );

    CBaseEntity::PushLuaInstanceSafe( L, gEntList.FindEntityByName( startEntity, name ) );
    return 1;
}
LUA_BINDING_END( "Entity", "The entity found, or NULL if not found." )

LUA_BINDING_BEGIN( Entities, FindByNameNearest, "library", "Finds the nearest entity by its name", "server" )
{
    const char *name = LUA_BINDING_ARGUMENT( luaL_checkstring, 1, "name" );
    Vector position = LUA_BINDING_ARGUMENT( luaL_checkvector, 2, "position" );
    float maxDistance = LUA_BINDING_ARGUMENT( luaL_checknumber, 3, "maxDistance" );

    CBaseEntity::PushLuaInstanceSafe( L, gEntList.FindEntityByNameNearest( name, position, maxDistance ) );
    return 1;
}
LUA_BINDING_END( "Entity", "The entity found, or NULL if not found." )

LUA_BINDING_BEGIN( Entities, FindByNameWithin, "library", "Finds an entity by its name within a radius", "server" )
{
    const char *name = LUA_BINDING_ARGUMENT( luaL_checkstring, 1, "name" );
    Vector position = LUA_BINDING_ARGUMENT( luaL_checkvector, 2, "position" );
    float maxDistance = LUA_BINDING_ARGUMENT( luaL_checknumber, 3, "maxDistance" );
    CBaseEntity *startEntity = LUA_BINDING_ARGUMENT_WITH_DEFAULT( luaL_optentity, 4, NULL, "startEntity" );

    CBaseEntity::PushLuaInstanceSafe( L, gEntList.FindEntityByNameWithin( startEntity, name, position, maxDistance ) );
    return 1;
}
LUA_BINDING_END( "Entity", "The entity found, or NULL if not found." )

LUA_BINDING_BEGIN( Entities, FindByTarget, "library", "Finds an entity by its target name", "server" )
{
    const char *target = LUA_BINDING_ARGUMENT( luaL_checkstring, 1, "target" );
    CBaseEntity *startEntity = LUA_BINDING_ARGUMENT_WITH_DEFAULT( luaL_optentity, 2, NULL, "startEntity" );

    CBaseEntity::PushLuaInstanceSafe( L, gEntList.FindEntityByTarget( startEntity, target ) );
    return 1;
}
LUA_BINDING_END( "Entity", "The entity found, or NULL if not found." )

LUA_BINDING_BEGIN( Entities, FindClassNearestFacing, "library", "Finds the nearest entity of a class facing a direction", "server" )
{
    Vector position = LUA_BINDING_ARGUMENT( luaL_checkvector, 1, "position" );
    Vector direction = LUA_BINDING_ARGUMENT( luaL_checkvector, 2, "direction" );
    float maxDistance = LUA_BINDING_ARGUMENT( luaL_checknumber, 3, "maxDistance" );
    const char *className = LUA_BINDING_ARGUMENT( luaL_checkstring, 4, "className" );

    CBaseEntity::PushLuaInstanceSafe( L, gEntList.FindEntityClassNearestFacing( position, direction, maxDistance, strdup( className ) ) );
    return 1;
}
LUA_BINDING_END( "Entity", "The entity found, or NULL if not found." )

LUA_BINDING_BEGIN( Entities, FindGeneric, "library", "Finds an entity by its generic name", "server" )
{
    const char *generic = LUA_BINDING_ARGUMENT( luaL_checkstring, 1, "generic" );
    CBaseEntity *startEntity = LUA_BINDING_ARGUMENT_WITH_DEFAULT( luaL_optentity, 2, NULL, "startEntity" );

    CBaseEntity::PushLuaInstanceSafe( L, gEntList.FindEntityGeneric( startEntity, generic ) );
    return 1;
}
LUA_BINDING_END( "Entity", "The entity found, or NULL if not found." )

LUA_BINDING_BEGIN( Entities, FindGenericNearest, "library", "Finds the nearest entity by its generic name", "server" )
{
    const char *generic = LUA_BINDING_ARGUMENT( luaL_checkstring, 1, "generic" );
    Vector position = LUA_BINDING_ARGUMENT( luaL_checkvector, 2, "position" );
    float maxDistance = LUA_BINDING_ARGUMENT( luaL_checknumber, 3, "maxDistance" );

    CBaseEntity::PushLuaInstanceSafe( L, gEntList.FindEntityGenericNearest( generic, position, maxDistance ) );
    return 1;
}
LUA_BINDING_END( "Entity", "The entity found, or NULL if not found." )

LUA_BINDING_BEGIN( Entities, FindGenericWithin, "library", "Finds an entity by its generic name within a radius", "server" )
{
    const char *generic = LUA_BINDING_ARGUMENT( luaL_checkstring, 1, "generic" );
    Vector position = LUA_BINDING_ARGUMENT( luaL_checkvector, 2, "position" );
    float maxDistance = LUA_BINDING_ARGUMENT( luaL_checknumber, 3, "maxDistance" );
    CBaseEntity *startEntity = LUA_BINDING_ARGUMENT_WITH_DEFAULT( luaL_optentity, 4, NULL, "startEntity" );

    CBaseEntity::PushLuaInstanceSafe( L, gEntList.FindEntityGenericWithin( startEntity, generic, position, maxDistance ) );
    return 1;
}
LUA_BINDING_END( "Entity", "The entity found, or NULL if not found." )

// HL2SB: GMod's ents.FindInSphere() answers a TABLE of every entity in the
// sphere; this fork's version answered the single one gEntList happened to find
// first.  That is not a difference a script can survive:
//
//     for k, v in pairs( ents.FindInSphere( self:GetPos(), 100 ) ) do
//
// SCP-096's FindEnemy() opens with exactly that line (init.lua:201-202) and the
// userdata it got instead raised
//
//     init.lua:202: bad argument #1 to 'for iterator' (table expected, got CBaseAnimating)
//
// on the behaviour coroutine's FIRST frame -- which base_nextbot's BehaveUpdate
// answers by dropping the thread, so the bot never ran a line of its AI again and
// stood still with a clean log.  The GMod-compatible table implementation already
// existed right above as Entities.GetInSphere() (and sh_init.lua:139 aliased one
// to the other, but that file is not loaded); it is used here now.
LUA_BINDING_BEGIN( Entities, FindInSphere, "library", "Finds all entities within a radius", "server" )
{
    CBaseEntity *pList[MAX_ENTITYARRAY];

    Vector position = LUA_BINDING_ARGUMENT( luaL_checkvector, 1, "position" );
    float radius = LUA_BINDING_ARGUMENT( luaL_checknumber, 2, "radius" );

    // HL2SB: the flag mask must default to 0 ("do not filter").  The server's
    // UTIL_EntitiesInSphere() reads a non-zero mask as a requirement --
    //     util.cpp:317  if ( m_flagMask && !( pEntity->GetFlags() & m_flagMask ) ) continue;
    // -- and every engine caller passes 0.  This used to default to
    // PARTITION_CLIENT_NON_STATIC_EDICTS, which is (1 << 7) = FL_ATCONTROLS on the
    // server: the table came back EMPTY for every query, and SCP-096's attack
    // loop, SeeMe, SeeMe2, its prop/door scans and playernear() all iterate it --
    // an empty table is silent, which is how "it never looks at me" and "it never
    // hits me" survived every other fix.
    int flagMask = LUA_BINDING_ARGUMENT_WITH_DEFAULT( luaL_optinteger, 3, 0, "flagMask" );

    int count = UTIL_EntitiesInSphere( pList, MAX_ENTITYARRAY, position, radius, flagMask );

    lua_newtable( L );

    for ( int i = 0; i < count; i++ )
    {
        lua_pushinteger( L, i );
        CBaseEntity::PushLuaInstanceSafe( L, pList[i] );
        lua_settable( L, -3 );
    }

    lua_pushinteger( L, count );
    return 2;
}
LUA_BINDING_END( "table", "A table of every entity found, plus the count." )

LUA_BINDING_BEGIN( Entities, FindNearestFacing, "library", "Finds the nearest entity facing a direction", "server" )
{
    Vector position = LUA_BINDING_ARGUMENT( luaL_checkvector, 1, "position" );
    Vector direction = LUA_BINDING_ARGUMENT( luaL_checkvector, 2, "direction" );
    float maxDistance = LUA_BINDING_ARGUMENT( luaL_checknumber, 3, "maxDistance" );

    CBaseEntity::PushLuaInstanceSafe( L, gEntList.FindEntityNearestFacing( position, direction, maxDistance ) );
    return 1;
}
LUA_BINDING_END( "Entity", "The entity found, or NULL if not found." )

LUA_BINDING_BEGIN( Entities, FindProcedural, "library", "Finds an entity by its procedural name", "server" )
{
    const char *procedural = LUA_BINDING_ARGUMENT( luaL_checkstring, 1, "procedural" );
    CBaseEntity *startEntity = LUA_BINDING_ARGUMENT_WITH_DEFAULT( luaL_optentity, 2, NULL, "startEntity" );

    CBaseEntity::PushLuaInstanceSafe( L, gEntList.FindEntityProcedural( procedural, startEntity ) );
    return 1;
}
LUA_BINDING_END( "Entity", "The entity found, or NULL if not found." )

LUA_BINDING_BEGIN( Entities, IsClearing, "library", "Checks if the entity list is being cleared", "server" )
{
    lua_pushboolean( L, gEntList.IsClearingEntities() );
    return 1;
}
LUA_BINDING_END( "boolean", "True if the entity list is being cleared, false otherwise." )

LUA_BINDING_BEGIN( Entities, NotifyCreate, "library", "Notifies the entity list that an entity was created", "server" )
{
    CBaseEntity *entity = LUA_BINDING_ARGUMENT( luaL_checkentity, 1, "entity" );

    gEntList.NotifyCreateEntity( entity );
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( Entities, NotifySpawn, "library", "Notifies the entity list that an entity was spawned", "server" )
{
    CBaseEntity *entity = LUA_BINDING_ARGUMENT( luaL_checkentity, 1, "entity" );

    gEntList.NotifySpawn( entity );
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( Entities, GetEdictCount, "library", "Gets the number of edicts", "server" )
{
    lua_pushinteger( L, gEntList.NumberOfEdicts() );
    return 1;
}
LUA_BINDING_END( "integer", "The number of edicts." )

LUA_BINDING_BEGIN( Entities, ReportFlagsChanged, "library", "Reports that an entity's flags have changed", "server" )
{
    CBaseEntity *entity = LUA_BINDING_ARGUMENT( luaL_checkentity, 1, "entity" );
    unsigned int flagsOld = LUA_BINDING_ARGUMENT( luaL_checknumber, 2, "flagsOld" );
    unsigned int flagsNow = LUA_BINDING_ARGUMENT( luaL_checknumber, 3, "flagsNow" );

    gEntList.ReportEntityFlagsChanged( entity, flagsOld, flagsNow );
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( Entities, ResetDeleteList, "library", "Resets the delete list", "server" )
{
    lua_pushinteger( L, gEntList.ResetDeleteList() );
    return 1;
}
LUA_BINDING_END( "integer", "The number of entities in the delete list." )

LUA_BINDING_BEGIN( Entities, CanCreateEntityClass, "library", "Checks if an entity class can be created", "server" )
{
    lua_pushboolean( L, CanCreateEntityClass( LUA_BINDING_ARGUMENT( luaL_checkstring, 1, "className" ) ) );
    return 1;
}
LUA_BINDING_END( "boolean", "True if the entity class can be created, false otherwise." )

LUA_BINDING_BEGIN( Entities, FindByEdictNumber, "library", "Gets an entity by its edict number", "server" )
{
    CBaseEntity *pEntity = CBaseEntity::Instance( INDEXENT( LUA_BINDING_ARGUMENT( luaL_checknumber, 1, "edictNumber" ) ) );
    CBaseEntity::PushLuaInstanceSafe( L, pEntity );
    return 1;
}
LUA_BINDING_END( "Entity", "The entity found, or NULL if not found." )

LUA_BINDING_BEGIN( Entities, FNullEnt, "library", "Checks if an entity is NULL", "server" )
{
    lua_pushboolean( L, FNullEnt( LUA_BINDING_ARGUMENT( luaL_checkentity, 1, "entity" )->edict() ) );
    return 1;
}
LUA_BINDING_END( "boolean", "True if the entity is NULL, false otherwise." )

LUA_BINDING_BEGIN( Entities, FindByIndex, "library", "Gets an entity by its index. Might be the same as FindByEdictNumber?", "server" )
{
    CBaseEntity::PushLuaInstanceSafe( L, UTIL_EntityByIndex( LUA_BINDING_ARGUMENT( luaL_checknumber, 1, "index" ) ) );
    return 1;
}
LUA_BINDING_END( "Entity", "The entity found, or NULL if not found." )

LUA_BINDING_BEGIN( Entities, GetClientPvsIsExpanded, "library", "Checks if the client PVS is expanded", "server" )
{
    lua_pushboolean( L, UTIL_ClientPVSIsExpanded() );
    return 1;
}
LUA_BINDING_END( "boolean", "True if the client PVS is expanded, false otherwise." )

LUA_BINDING_BEGIN( Entities, FindClientInPvs, "library", "Finds a client in the PVS", "server" )
{
    CBaseEntity::PushLuaInstanceSafe( L,
                                    UTIL_FindClientInPVS(
                                        LUA_BINDING_ARGUMENT( luaL_checkvector, 1, "minimumVector" ),
                                        LUA_BINDING_ARGUMENT( luaL_checkvector, 2, "maximumVector" ) ) );
    return 1;
}
LUA_BINDING_END( "Entity", "The entity found, or NULL if not found." )

LUA_BINDING_BEGIN( Entities, Remove, "library", "Removes an entity", "server" )
{
    UTIL_Remove( LUA_BINDING_ARGUMENT( luaL_checkentity, 1, "entity" ) );
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( Entities, DisableRemoveImmediate, "library", "Disables immediate removal of entities", "server" )
{
    UTIL_DisableRemoveImmediate();
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( Entities, EnableRemoveImmediate, "library", "Enables immediate removal of entities", "server" )
{
    UTIL_EnableRemoveImmediate();
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( Entities, RemoveImmediate, "library", "Removes an entity immediately", "server" )
{
    UTIL_RemoveImmediate( LUA_BINDING_ARGUMENT( luaL_checkentity, 1, "entity" ) );
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( Entities, IsValidEdict, "library", "Checks if an entity is valid by checking if it's edict is valid and not free", "server" )
{
    lua_pushboolean( L, UTIL_IsValidEntity( LUA_BINDING_ARGUMENT( luaL_checkentity, 1, "entity" ) ) );
    return 1;
}
LUA_BINDING_END( "boolean", "True if the entity is valid, false otherwise." )

#endif  // GAME_DLL

LUA_BINDING_BEGIN( Entities, FirstInList, "library", "Gets the first entity in the list" )
{
#ifdef CLIENT_DLL
    CBaseEntity::PushLuaInstanceSafe( L, ClientEntityList().FirstBaseEntity() );
#else
    CBaseEntity::PushLuaInstanceSafe( L, gEntList.FirstEnt() );
#endif

    return 1;
}
LUA_BINDING_END( "Entity", "The first entity in the entity list." )

LUA_BINDING_BEGIN( Entities, GetAll, "library", "Gets all entities in the list" )
{
    lua_newtable( L );

    CBaseEntity *pEnt = NULL;
    int i = 0;
#ifdef CLIENT_DLL
    while ( ( pEnt = ClientEntityList().NextBaseEntity( pEnt ) ) != NULL )
#else
    while ( ( pEnt = gEntList.NextEnt( pEnt ) ) != NULL )
#endif
    {
        lua_pushinteger( L, ++i );  // 1-based index for Lua
        CBaseEntity::PushLuaInstanceSafe( L, pEnt );
        lua_settable( L, -3 );
    }

    return 1;
}
LUA_BINDING_END( "table", "A table of all entities in the entity list." )

LUA_BINDING_BEGIN( Entities, GetByClass, "library", "Gets all entities in the list by their class name" )
{
    const char *className = LUA_BINDING_ARGUMENT( luaL_checkstring, 1, "className" );

    lua_newtable( L );

    CBaseEntity *pEnt = NULL;
    int i = 0;
#ifdef CLIENT_DLL
    while ( ( pEnt = ClientEntityList().NextBaseEntity( pEnt ) ) != NULL )
#else
    while ( ( pEnt = gEntList.NextEnt( pEnt ) ) != NULL )
#endif
    {
        if ( !Q_strcmp( pEnt->GetClassname(), className ) )
        {
            lua_pushinteger( L, ++i );  // 1-based index for Lua
            CBaseEntity::PushLuaInstanceSafe( L, pEnt );
            lua_settable( L, -3 );
        }
    }

    return 1;
}
LUA_BINDING_END( "table", "A table of all entities in the entity list with the given class name." )

LUA_BINDING_BEGIN( Entities, NextInList, "library", "Gets the next entity in the list" )
{
    CBaseEntity *pCurrent = LUA_BINDING_ARGUMENT_WITH_DEFAULT( luaL_optentity, 1, NULL, "startingEntity" );

#ifdef CLIENT_DLL
    CBaseEntity::PushLuaInstanceSafe( L, ClientEntityList().NextBaseEntity( pCurrent ) );
#else
    CBaseEntity::PushLuaInstanceSafe( L, gEntList.NextEnt( pCurrent ) );
#endif

    return 1;
}
LUA_BINDING_END( "Entity", "The next entity in the entity list." )

LUA_BINDING_BEGIN( Entities, GetCount, "library", "Gets the number of entities in the list" )
{
#ifdef CLIENT_DLL
    lua_pushinteger( L, ClientEntityList().NumberOfEntities() );
#else
    lua_pushinteger( L, gEntList.NumberOfEntities() );
#endif

    return 1;
}
LUA_BINDING_END( "integer", "The number of entities in the entity list." )

/*
** Open gEntList library.
**
** Experiment: Source calls this library `Entities`; GMod calls the same thing
** `ents` (ents.FindByClass, ents.GetAll, ents.FindInSphere, ...).  Both names are
** published onto one table, which the CBaseFlex bindings (Entities.CreateClientEntity)
** merge into as well -- see luaL_register_entity_library.
*/
LUALIB_API int luaopen_Entities( lua_State *L )
{
    luaL_register_entity_library( L, Entities_luaRegistry );
    return 1;
}

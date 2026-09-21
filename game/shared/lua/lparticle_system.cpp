#include "cbase.h"
#include "luamanager.h"
#include "luasrclib.h"
#include "mathlib/lvector.h"	// luaL_checkvector / luaL_checkangle
#include "lbaseentity_shared.h"	// luaL_checkentity
#include "particle_parse.h"
#include "particles/particles.h"
#ifdef CLIENT_DLL
#include "lnewparticle.h"	// HL2SB_ClientCreateParticleEffect / HL2SB_ClientAttachParticleEffect
#endif

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// Declared server-side in enginecallback.h and client-side in
// cdll_client_int.h -- neither header is shared, so restate the (identical)
// prototype here.  Compatible with both existing declarations.
void PrecacheParticleSystem( const char *pParticleSystemName );

// From particle_property.h, restated: particle_property.h drags in the
// client-only particles_new.h, so a shared file cannot include it.  Argh:
// server considers -1 to be an invalid attachment, whereas the client uses 0.
#ifdef CLIENT_DLL
#define HL2SB_INVALID_PARTICLE_ATTACHMENT	0
#else
#define HL2SB_INVALID_PARTICLE_ATTACHMENT	-1
#endif

LUA_REGISTRATION_INIT( ParticleSystems )

LUA_BINDING_BEGIN( ParticleSystems, Precache, "library", "Precache a particle system." )
{
    const char *systemName = LUA_BINDING_ARGUMENT( luaL_checkstring, 1, "systemName" );

    // HL2SB: GMod addons precache their particle systems from Initialize, which
    // runs again for every re-created entity / every map reload.  Asking the
    // engine once per name per session keeps the (synchronous, disk-touching)
    // PrecacheParticleSystem() off the live throw, not just off the second one.
    if ( !HL2SB_PrecacheOnce( systemName ) )
        return 0;

    PrecacheParticleSystem( systemName );
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( ParticleSystems, ReadConfigFile, "library", "Read a particle system config file, add it to the list of particle configs." )
{
    const char *filePath = LUA_BINDING_ARGUMENT( luaL_checkstring, 1, "filePath" );

    if ( !g_pParticleSystemMgr->ReadParticleConfigFile( filePath, true, true ) )
    {
        // ReadParticleConfigFile already prints a warning message itself
        // Warning("Error reading particle config file: %s\n", filePath);
    }

    return 0;
}
LUA_BINDING_END()

/*
** HL2SB (2026-09-21): GMod's shared globals around .pcf particle systems.
**
** GMod realm notes (from the wiki pages):
**   PrecacheParticleSystem -- Shared.  On the server it also networks the name
**     (the string table IS the 4096-entry budget the wiki warns about); the
**     client call additionally asks the particle manager to precache.
**   ParticleEffect / ParticleEffectAttach -- Shared.  On the server these
**     dispatch the "ParticleEffect" TE (c_particle_system.cpp's
**     ParticleEffectCallback does the creation on every client); on the client
**     they create locally with no TE involved.
*/

// ---------------------------------------------------------------------------
// Global PrecacheParticleSystem( particleSystemName )
// ---------------------------------------------------------------------------
static int LuaGlobal_PrecacheParticleSystem( lua_State *Lstate )
{
	const char *pszSystemName = luaL_checkstring( Lstate, 1 );

	// Same once-per-name-per-session guard as game.Precache above: addons
	// precache from Initialize, which runs again for every re-created entity /
	// every map reload, and the underlying call is a synchronous string-table
	// insert.
	if ( !HL2SB_PrecacheOnce( pszSystemName ) )
		return 0;

	PrecacheParticleSystem( pszSystemName );
	return 0;
}

// ---------------------------------------------------------------------------
// Global ParticleEffect( particleName, position, angles, parent = NULL )
// ---------------------------------------------------------------------------
static int LuaGlobal_ParticleEffect( lua_State *Lstate )
{
	const char *pszParticleName = luaL_checkstring( Lstate, 1 );
	Vector vecPos = luaL_checkvector( Lstate, 2 );
	QAngle angOrient = luaL_checkangle( Lstate, 3 );
	CBaseEntity *pParent = lua_isnoneornil( Lstate, 4 ) ? NULL : luaL_checkentity( Lstate, 4 );

#ifdef CLIENT_DLL
	HL2SB_ClientCreateParticleEffect( pszParticleName, vecPos, angOrient, pParent );
#else
	DispatchParticleEffect( pszParticleName, vecPos, angOrient, pParent );
#endif
	return 0;
}

// ---------------------------------------------------------------------------
// Global ParticleEffectAttach( particleName, attachType, entity, attachmentID )
// ---------------------------------------------------------------------------
static int LuaGlobal_ParticleEffectAttach( lua_State *Lstate )
{
	const char *pszParticleName = luaL_checkstring( Lstate, 1 );
	int iAttachType = luaL_checkint( Lstate, 2 );
	CBaseEntity *pEntity = luaL_checkentity( Lstate, 3 );
	int iAttachmentID = luaL_optint( Lstate, 4, HL2SB_INVALID_PARTICLE_ATTACHMENT );

#ifdef CLIENT_DLL
	HL2SB_ClientAttachParticleEffect( pszParticleName, iAttachType, pEntity, iAttachmentID );
#else
	DispatchParticleEffect( pszParticleName, (ParticleAttachment_t)iAttachType, pEntity, iAttachmentID, false );
#endif
	return 0;
}

/*
** Open ParticleSystem library
*/
LUALIB_API int luaopen_ParticleSystem( lua_State *L )
{
    LUA_REGISTRATION_COMMIT_LIBRARY( ParticleSystems );

	// GMod exposes these as plain globals, not game.* members.
	lua_pushcfunction( L, LuaGlobal_PrecacheParticleSystem ); lua_setglobal( L, "PrecacheParticleSystem" );
	lua_pushcfunction( L, LuaGlobal_ParticleEffect );         lua_setglobal( L, "ParticleEffect" );
	lua_pushcfunction( L, LuaGlobal_ParticleEffectAttach );   lua_setglobal( L, "ParticleEffectAttach" );

	// Enums/PATTACH -- values match game/shared/particle_parse.h's
	// ParticleAttachment_t (the enum the whole TE/client pipeline consumes).
	lua_pushinteger( L, PATTACH_ABSORIGIN );        lua_setglobal( L, "PATTACH_ABSORIGIN" );
	lua_pushinteger( L, PATTACH_ABSORIGIN_FOLLOW ); lua_setglobal( L, "PATTACH_ABSORIGIN_FOLLOW" );
	lua_pushinteger( L, PATTACH_CUSTOMORIGIN );     lua_setglobal( L, "PATTACH_CUSTOMORIGIN" );
	lua_pushinteger( L, PATTACH_POINT );            lua_setglobal( L, "PATTACH_POINT" );
	lua_pushinteger( L, PATTACH_POINT_FOLLOW );     lua_setglobal( L, "PATTACH_POINT_FOLLOW" );
	lua_pushinteger( L, PATTACH_WORLDORIGIN );      lua_setglobal( L, "PATTACH_WORLDORIGIN" );

    return 1;
}

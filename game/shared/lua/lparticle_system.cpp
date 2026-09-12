#include "cbase.h"
#include "luamanager.h"
#include "luasrclib.h"
#include "particle_parse.h"
#include "particles/particles.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

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
** Open ParticleSystem library
*/
LUALIB_API int luaopen_ParticleSystem( lua_State *L )
{
    LUA_REGISTRATION_COMMIT_LIBRARY( ParticleSystems );

    return 1;
}

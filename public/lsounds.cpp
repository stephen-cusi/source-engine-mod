//=============================================================================//
//
// Purpose: The Sounds library, ported from Experiment: Source
//          (src/public/lsounds.cpp).
//
//          Kept verbatim from upstream except for the adaptations listed below;
//          GMod's sound.Add / sound.Play are built on this (its `sound` table is
//          aliased onto Sounds by the Lua content).
//
//          Dropped from the port:
//            * the AudioChannel userdata and every Sounds.PlayUrl / Sounds.PlayFile
//              binding -- all of them go through Experiment's util/bassmanager,
//              which wraps the BASS audio library for URL/file streaming.  HL2SB
//              does not ship BASS, and nothing else in the library depends on it.
//            * _E.PLAY_SOUND_FLAG, which only existed to describe BASS flags.
//
//          Adapted:
//            * EmitSound() is CBaseEntity::EmitSound() here; upstream has a free
//              function of that name in SoundEmitterSystem.cpp.
//            * the recipient-filter optional argument is tested with a local
//              helper instead of lua_isrecipientfilter() (not ported).
//            * Sounds.Add read Volume twice (the first read's value was popped
//              again); only the second, which also handles the table form, is kept.
//            * Sounds.Play returned 0 after pushing the duration, so callers never
//              saw it; it returns the duration now.
//
//=============================================================================//

#include "cbase.h"
#include "luamanager.h"
#include "luasrclib.h"
#include "lsounds.h"
#include "engine/IEngineSound.h"
#include "mathlib/lvector.h"
#ifdef CLIENT_DLL
#include "c_recipientfilter.h"
#include "lc_recipientfilter.h"
#else
#include "recipientfilter.h"
#include "lrecipientfilter.h"
#endif

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

extern ISoundEmitterSystemBase *soundemitterbase;

// HL2SB: the "precache each raw wave once" helper from game/shared/lua/lutil_shared.cpp.
// sound.Play needs it for the same reason Entity:EmitSound does (see below).
extern bool HL2SB_PrecacheOnce( const char *pszName );

// The recipient-filter argument of Sounds.Play is optional: accept either filter
// type, and anything else means "build a PAS attenuation filter around origin".
static bool lua_issoundsrecipientfilter (lua_State *L, int idx) {
  return luaL_testudata(L, idx, "CRecipientFilter") != NULL ||
         luaL_testudata(L, idx, "CPASFilter") != NULL;
}

LUA_REGISTRATION_INIT( Sounds )

LUA_BINDING_BEGIN( Sounds, Add, "library", "Creates a sound script." )
{
    if ( !LUA_BINDING_ARGUMENT( lua_istable, 1, "soundData" ) )
    {
        luaL_argerror( L, 1, "expected table" );
        return 0;
    }

    CSoundParametersInternal parameters;

    // For gmod compat we check both lowecase and UpperCamelCase
    GET_FIELD_WITH_COMPATIBILITY_OR_ERROR( L, 1, "Name", "name", lua_isstring );
    const char *name = luaL_checkstring( L, -1 );
    lua_pop( L, 1 );  // pop the name value

    GET_FIELD_WITH_COMPATIBILITY_OR_ERROR( L, 1, "Channel", "channel", lua_isnumber );
    parameters.SetChannel( luaL_checknumber( L, -1 ) );
    lua_pop( L, 1 );  // pop the channel value

    GET_FIELD_WITH_COMPATIBILITY( L, 1, "Level", "level" );
    if ( lua_isnumber( L, -1 ) )
        parameters.SetSoundLevel( luaL_checknumber( L, -1 ) );
    else
        parameters.SetSoundLevel( SNDLVL_NORM );
    lua_pop( L, 1 );  // pop the level value

    GET_FIELD_WITH_COMPATIBILITY( L, 1, "Volume", "volume" );
    if ( lua_istable( L, -1 ) )
    {
        lua_rawgeti( L, -1, 1 );
        lua_rawgeti( L, -2, 2 );
        parameters.SetVolume( luaL_checknumber( L, -2 ), luaL_checknumber( L, -1 ) );
        lua_pop( L, 2 );  // pop the volume values
    }
    else if ( lua_isnumber( L, -1 ) )
    {
        parameters.SetVolume( luaL_checknumber( L, -1 ), 0 );
    }
    else
    {
        parameters.SetVolume( 1.0f, 0 );
    }
    lua_pop( L, 1 );  // pop the volume value

    GET_FIELD_WITH_COMPATIBILITY( L, 1, "Pitch", "pitch" );
    if ( lua_istable( L, -1 ) )
    {
        lua_rawgeti( L, -1, 1 );
        lua_rawgeti( L, -2, 2 );
        parameters.SetPitch( luaL_checknumber( L, -2 ), luaL_checknumber( L, -1 ) );
        lua_pop( L, 2 );  // pop the pitch values
    }
    else if ( lua_isnumber( L, -1 ) )
    {
        parameters.SetPitch( luaL_checknumber( L, -1 ), 0 );
    }
    else
    {
        parameters.SetPitch( 100, 0 );
    }
    lua_pop( L, 1 );  // pop the pitch value

    GET_FIELD_WITH_COMPATIBILITY( L, 1, "Sound", "sound" );
    if ( lua_istable( L, -1 ) )
    {
        // Loop through the table and add each sound file
        lua_pushnil( L );

        while ( lua_next( L, -2 ) != 0 )
        {
            if ( lua_isstring( L, -1 ) )
            {
                CUtlSymbol soundSymbol = soundemitterbase->AddWaveName( luaL_checkstring( L, -1 ) );
                SoundFile soundFile;
                soundFile.symbol = soundSymbol;
                soundFile.gender = GENDER_NONE;
                parameters.AddSoundName( soundFile );
            }
            lua_pop( L, 1 );
        }
    }
    else if ( lua_isstring( L, -1 ) )
    {
        CUtlSymbol soundSymbol = soundemitterbase->AddWaveName( luaL_checkstring( L, -1 ) );
        SoundFile soundFile;
        soundFile.symbol = soundSymbol;
        soundFile.gender = GENDER_NONE;
        parameters.AddSoundName( soundFile );
    }
    else
    {
        luaL_argerror( L, 1, "expected field 'sound' to be a string or table of strings" );
        return 0;
    }
    lua_pop( L, 1 );  // pop the sound value

    // TODO: Check if the file needs to exist, or if we can just create a sound script without a file
    soundemitterbase->AddSound( name, "scripts/sounds/lua_procedural.txt", parameters );

    return 0;
}
LUA_BINDING_END()

// lua_run_cl Sounds.Play("ambient/levels/labs/teleport_alarm_loop1.wav", Vectors.Create(0, 0, 0))
//
// HL2SB GMod compat: the argument list is GMOD's, not Experiment's.
//
// This binding was ported with Experiment: Source's order --
//     ( sound, origin, entity, channel, volume, soundLevel, soundFlags,
//       pitchPercent, dsp, filter )
// but GMod's sound.Play is
//     sound.Play( sound, pos, level, pitch, volume, channel )
// so a stock GMod script calling sound.Play( BounceSound, pos, 75, pitch, vol )
// (which is what sent_ball's ENT:PhysicsCollide does) had 75 read as an
// ENTITY INDEX and the pitch read as a CHANNEL, and the sound either went to the
// wrong place or was dropped.  GMod's order is implemented here; nothing inside
// this tree called Sounds.Play at all before, so nothing regresses.
LUA_BINDING_BEGIN( Sounds, Play, "library", "Plays a sound emitting from a place in the world. GMod order: ( sound, pos, level, pitch, volume, channel )." )
{
    const char *pszSoundName = LUA_BINDING_ARGUMENT( luaL_checkstring, 1, "soundName" );  // doc: sound script name or sound file name relative to sound/ folder
    const Vector vecOrigin = LUA_BINDING_ARGUMENT( luaL_checkvector, 2, "origin" );       // doc: position of the sound
    soundlevel_t soundLevel = LUA_BINDING_ARGUMENT_ENUM_WITH_DEFAULT( soundlevel_t, 3, SNDLVL_NORM, "soundLevel" );
    float flPitchPercent = LUA_BINDING_ARGUMENT_WITH_DEFAULT( luaL_optnumber, 4, 100, "pitchPercent" );
    float flVolume = LUA_BINDING_ARGUMENT_WITH_DEFAULT( luaL_optnumber, 5, 1, "volume" );
    SOUND_CHANNEL channel = ( SOUND_CHANNEL )LUA_BINDING_ARGUMENT_WITH_DEFAULT( luaL_optnumber, 6, CHAN_AUTO, "channel" );

    // Not part of GMod's signature, but a few call sites in this tree and in
    // Experiment-era scripts pass a recipient filter last; keep accepting it.
    int entityIndex = SOUND_FROM_WORLD;
    lua_CRecipientFilter filter;

    if ( lua_issoundsrecipientfilter( L, 7 ) )
    {
        filter = LUA_BINDING_ARGUMENT_NILLABLE( luaL_checkrecipientfilter, 7, "filter" );
    }
    else
    {
        filter = CPASAttenuationFilter( vecOrigin, soundLevel );
    }

    float duration = 0;

#ifndef CLIENT_DLL
    // HL2SB GMod compat: a GMod script plays a raw wave by name without ever
    // precaching it, and SV_StartSound then drops it ("SV_StartSound: <wave> not
    // precached (0)") with no Lua-visible error -- exactly the trap Entity:EmitSound
    // already works around (see lbaseentity_shared.cpp).  sent_ball plays
    // sound/garrysmod/balloon_pop_cute.wav this way, so register it once here.
    if ( pszSoundName[0] != '!' && pszSoundName[0] != '?' && HL2SB_PrecacheOnce( pszSoundName ) )
    {
        CBaseEntity::PrecacheScriptSound( pszSoundName );
        CBaseEntity::PrecacheSound( pszSoundName );
        enginesound->PrecacheSound( pszSoundName );
    }
#endif

    EmitSound_t params;
    params.m_pSoundName = pszSoundName;
    params.m_pOrigin = &vecOrigin;
    params.m_flVolume = flVolume;
    params.m_SoundLevel = soundLevel;
    params.m_nPitch = flPitchPercent;
    params.m_nSpecialDSP = 0;
    params.m_flSoundTime = 0;
    params.m_pflSoundDuration = &duration;
    params.m_bWarnOnDirectWaveReference = false;
    params.m_nChannel = channel;
    params.m_nFlags = 0;

    CBaseEntity::EmitSound( filter, entityIndex, params );

    lua_pushnumber( L, duration );

    return 1;
}
LUA_BINDING_END()


/*
** Open Sounds library
*/
LUALIB_API int luaopen_Sounds( lua_State *L )
{
    LUA_REGISTRATION_COMMIT_LIBRARY( Sounds );

    return 1;
}

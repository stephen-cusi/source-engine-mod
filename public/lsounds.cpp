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

static bool HL2SB_SoundRange( lua_State *L, int index, float &first, float &last )
{
  index = lua_absindex( L, index );
  if ( lua_type( L, index ) == LUA_TNUMBER ) {
    first = last = (float)lua_tonumber( L, index );
  }
  else if ( lua_istable( L, index ) ) {
    lua_geti( L, index, 1 );
    lua_geti( L, index, 2 );
    first = lua_type( L, -2 ) == LUA_TNUMBER ? (float)lua_tonumber( L, -2 ) : -1.0f;
    last = lua_type( L, -1 ) == LUA_TNUMBER ? (float)lua_tonumber( L, -1 ) : first;
    lua_pop( L, 2 );
  }
  else return false;
  return first > 0.0f && last > 0.0f;
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

    // HL2SB GMod compat: GMod's sound.Add() table has channel/level/volume/pitch
    // all OPTIONAL (only `name` and `sound` are required), and nextbot addons
    // write the short form:
    //
    //     sound.Add( { name = "windgrinbot.hit", sound = "windgrinbot/hit.mp3" } )
    //
    // Requiring Channel turned that into "bad argument #1 to 'Add' (expected field
    // 'Channel')" on the first call -- the windgrin_npc nextbot's whole load.  A
    // missing channel is CHAN_AUTO, which is what GMod does.
    lua_getfield( L, 1, "channel" );
    if ( lua_isnumber( L, -1 ) )
        parameters.SetChannel( luaL_checknumber( L, -1 ) );
    else
        parameters.SetChannel( CHAN_AUTO );
    lua_pop( L, 1 );  // pop the channel value

    lua_getfield( L, 1, "level" );
    if ( lua_isnumber( L, -1 ) )
        parameters.SetSoundLevel( luaL_checknumber( L, -1 ) );
    else
        parameters.SetSoundLevel( SNDLVL_NORM );
    lua_pop( L, 1 );  // pop the level value

    lua_getfield( L, 1, "volume" );
    float first, last;
    if ( HL2SB_SoundRange( L, -1, first, last ) )
        parameters.SetVolume( first, last - first );
    lua_pop( L, 1 );  // pop the volume value

    lua_getfield( L, 1, "pitchstart" );
    lua_getfield( L, 1, "pitchend" );
    first = lua_type( L, -2 ) == LUA_TNUMBER ? (float)lua_tonumber( L, -2 ) : -1.0f;
    last = lua_type( L, -1 ) == LUA_TNUMBER ? (float)lua_tonumber( L, -1 ) : -1.0f;
    lua_pop( L, 2 );
    if ( first <= 0.0f || last <= 0.0f ) {
        lua_getfield( L, 1, "pitch" );
        bool valid = HL2SB_SoundRange( L, -1, first, last );
        lua_pop( L, 1 );
        if ( !valid ) first = last = -1.0f;
    }
    if ( first > 0.0f && last > 0.0f ) parameters.SetPitch( first, last - first );

    lua_getfield( L, 1, "sound" );
    if ( lua_istable( L, -1 ) )
    {
        for ( int i = 1; ; ++i ) {
            lua_geti( L, -1, i );
            if ( lua_type( L, -1 ) != LUA_TSTRING ) { lua_pop( L, 1 ); break; }
            soundemitterbase->ExpandSoundNameMacros( parameters, lua_tostring( L, -1 ) );
            lua_pop( L, 1 );
        }
    }
    else if ( lua_type( L, -1 ) == LUA_TSTRING )
    {
        soundemitterbase->ExpandSoundNameMacros( parameters, lua_tostring( L, -1 ) );
    }
    else
    {
        lua_pop( L, 1 );
        luasrc_LuaErrorMsgF( "sound.Add called without anything in the 'sound' key! (or wrong type?)\n" );
        return 0;
    }
    lua_pop( L, 1 );  // pop the sound value

    lua_getfield( L, 1, "name" );
    if ( lua_type( L, -1 ) != LUA_TSTRING ) {
        lua_pop( L, 1 );
        luasrc_LuaErrorMsgF( "sound.Add called without anything in the 'name' key! (or wrong type?)\n" );
        return 0;
    }
    const char *name = lua_tostring( L, -1 );
    if ( soundemitterbase->FindSoundScript( "scripts/sounds/lua.txt" ) != soundemitterbase->InvalidIndex() )
        soundemitterbase->AddSound( name, "scripts/sounds/lua.txt", parameters );
    else
        soundemitterbase->AddSound( name, "scripts/sounds/lua_procedural.txt", parameters );
    lua_pop( L, 1 );

    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( Sounds, GetProperties, "library", "Returns a sound script's properties." )
{
    const char *name = luaL_checkstring( L, 1 );
    int index = soundemitterbase->GetSoundIndex( name );
    CSoundParametersInternal *params = index == -1 ? NULL : soundemitterbase->InternalGetParametersForSound( index );
    if ( !params || params->NumSoundNames() == 0 ) return 0;
    lua_newtable( L );
    lua_pushstring( L, soundemitterbase->GetSoundName( index ) ); lua_setfield( L, -2, "name" );
    lua_pushinteger( L, params->GetChannel() ); lua_setfield( L, -2, "channel" );
    lua_pushinteger( L, params->GetSoundLevel().start ); lua_setfield( L, -2, "level" );
    float volume = params->GetVolume().start;
    float volumeRange = params->GetVolume().range;
    if ( volumeRange == 0 ) lua_pushnumber( L, volume );
    else { lua_createtable( L, 2, 0 ); lua_pushnumber( L, volume ); lua_rawseti( L, -2, 1 ); lua_pushnumber( L, volume + volumeRange ); lua_rawseti( L, -2, 2 ); }
    lua_setfield( L, -2, "volume" );
    int pitch = params->GetPitch().start, pitchRange = params->GetPitch().range;
    if ( pitchRange == 0 ) lua_pushinteger( L, pitch );
    else { lua_createtable( L, 2, 0 ); lua_pushinteger( L, pitch ); lua_rawseti( L, -2, 1 ); lua_pushinteger( L, pitch + pitchRange ); lua_rawseti( L, -2, 2 ); }
    lua_setfield( L, -2, "pitch" );
    SoundFile *files = params->GetSoundNames();
    if ( params->NumSoundNames() == 1 ) lua_pushstring( L, soundemitterbase->GetWaveName( files[0].symbol ) );
    else {
        lua_createtable( L, params->NumSoundNames(), 0 );
        for ( int i = 0; i < params->NumSoundNames(); ++i ) { lua_pushstring( L, soundemitterbase->GetWaveName( files[i].symbol ) ); lua_rawseti( L, -2, i + 1 ); }
    }
    lua_setfield( L, -2, "sound" );
    return 1;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( Sounds, GetTable, "library", "Returns registered sound script names." )
{
    lua_newtable( L );
    int output = 1;
    for ( int i = soundemitterbase->First(); i != soundemitterbase->InvalidIndex(); i = soundemitterbase->Next( i ) ) {
        if ( soundemitterbase->IsValidIndex( i ) ) { lua_pushstring( L, soundemitterbase->GetSoundName( i ) ); lua_rawseti( L, -2, output++ ); }
    }
    return 1;
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
//     sound.Play( sound, pos, level, pitch, volume, dsp )
// (wiki: level 75 = normal, pitch 100 = normal, volume 1 = full, dsp 0 = the
// DSP preset for the sound), and a stock GMod script calling
// sound.Play( BounceSound, pos, 75, pitch, vol ) -- which is what sent_ball's
// ENT:PhysicsCollide does -- had 75 read as an ENTITY INDEX and the pitch read
// as a CHANNEL, and the sound either went to the wrong place or was dropped.
// The reference returns nothing, so the sound duration is no longer handed
// back to the caller.
LUA_BINDING_BEGIN( Sounds, Play, "library", "Plays a sound emitting from a place in the world. GMod order: ( sound, pos, level, pitch, volume, dsp )." )
{
    const char *pszSoundName = LUA_BINDING_ARGUMENT( luaL_checkstring, 1, "soundName" );  // doc: sound script name or sound file name relative to sound/ folder
    const Vector vecOrigin = LUA_BINDING_ARGUMENT( luaL_checkvector, 2, "origin" );       // doc: position of the sound
    soundlevel_t soundLevel = LUA_BINDING_ARGUMENT_ENUM_WITH_DEFAULT( soundlevel_t, 3, SNDLVL_NORM, "soundLevel" );
    float flPitchPercent = LUA_BINDING_ARGUMENT_WITH_DEFAULT( luaL_optnumber, 4, 100, "pitchPercent" );
    float flVolume = LUA_BINDING_ARGUMENT_WITH_DEFAULT( luaL_optnumber, 5, 1, "volume" );
    int nDSP = ( int )LUA_BINDING_ARGUMENT_WITH_DEFAULT( luaL_optnumber, 6, 0, "dsp" );

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
    params.m_nSpecialDSP = nDSP;
    params.m_flSoundTime = 0;
    params.m_pflSoundDuration = &duration;
    params.m_bWarnOnDirectWaveReference = false;
    params.m_nChannel = CHAN_AUTO;
    params.m_nFlags = 0;

    CBaseEntity::EmitSound( filter, entityIndex, params );

    return 0;
}
LUA_BINDING_END()

static int luasrc_GlobalEmitSound( lua_State *L )
{
    const char *name = luaL_checkstring( L, 1 );
    const Vector origin = luaL_checkvector( L, 2 );
    int entity = (int)luaL_optnumber( L, 3, 0 );
    CSoundParameters soundParameters;
    CBaseEntity::GetParametersForSound( name, soundParameters, NULL );
    EmitSound_t params;
    params.m_pSoundName = name;
    params.m_pOrigin = &origin;
    params.m_nChannel = (int)luaL_optnumber( L, 4, soundParameters.channel );
    params.m_flVolume = (float)luaL_optnumber( L, 5, soundParameters.volume );
    params.m_SoundLevel = (soundlevel_t)(int)luaL_optnumber( L, 6, soundParameters.soundlevel );
    params.m_nFlags = (int)luaL_optnumber( L, 7, 0 );
    params.m_nPitch = (int)luaL_optnumber( L, 8, soundParameters.pitch );
    params.m_nSpecialDSP = (int)luaL_optnumber( L, 9, 0 );
    params.m_bWarnOnDirectWaveReference = false;
#ifndef CLIENT_DLL
    if ( entity < 0 ) entity = 0;
    if ( name[0] != '!' && name[0] != '?' && HL2SB_PrecacheOnce( name ) )
    {
        CBaseEntity::PrecacheScriptSound( name );
        enginesound->PrecacheSound( name );
    }
#endif
    lua_CRecipientFilter filter;
    if ( lua_issoundsrecipientfilter( L, 10 ) )
        filter = luaL_checkrecipientfilter( L, 10 );
    else
        filter = CPASAttenuationFilter( origin, params.m_SoundLevel );
    CBaseEntity::EmitSound( filter, entity, params );
    return 0;
}


#ifdef CLIENT_DLL
// HL2SB: GMod's sound.PlayFile (CLIENT only per the reference contract).  The
// implementation lives in game/client/lua/lgmodaudio.cpp; this entry only
// registers it on the Sounds library so the Lua-facing table is `sound`.
// Same local-include rule as lsrcinit's local prototypes: nothing else needs
// this header, so luasrclib.h stays untouched.
#include "../../game/client/lua/lgmodaudio.h"
LUA_BINDING_BEGIN( Sounds, PlayFile, "library", "GMod contract: streams a game file into an IGModAudioChannel (CLIENT only)." )
{
    return HL2SB_AudioPlayFile( L );
}
LUA_BINDING_END()
#endif

/*
** Open Sounds library
*/
LUALIB_API int luaopen_Sounds( lua_State *L )
{
    LUA_REGISTRATION_COMMIT_LIBRARY( Sounds );

    lua_pushcfunction( L, luasrc_GlobalEmitSound );
    lua_setglobal( L, "EmitSound" );

    return 1;
}

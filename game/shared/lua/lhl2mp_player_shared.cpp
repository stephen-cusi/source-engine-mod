//========= Copyright © 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//
//=============================================================================//
#define lhl2mp_player_shared_cpp

#include "cbase.h"

#include "luamanager.h"
#include "lhl2mp_player_shared.h"
#include "lbaseentity_shared.h"
#include "Multiplayer/multiplayer_animstate.h"
#ifdef CLIENT_DLL
#include "lc_baseanimating.h"
#include "voice_status.h"	// HL2SB: Player:IsSpeaking / VoiceVolume
#else
#include "lbaseanimating.h"
#endif
#include "mathlib/lvector.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#ifndef CLIENT_DLL
// HL2SB (2026-10-02): GMod gesture broadcast sender (defined in
// game/server/hl2mp/hl2mp_player.cpp) - declared here because the
// DoAnimationEvent default branch below fires it.
void TE_PlayerAnimEvent( CBasePlayer *pPlayer, PlayerAnimEvent_t event, int nData );
#endif

/*
** access functions (stack -> C)
*/


LUA_API lua_CHL2MP_Player *lua_tohl2mpplayer (lua_State *L, int idx) {
  CBaseHandle *hPlayer = dynamic_cast<CBaseHandle *>((CBaseHandle *)lua_touserdata(L, idx));
  if (hPlayer == NULL)
    return NULL;
  return dynamic_cast<lua_CHL2MP_Player *>(hPlayer->Get());
}



/*
** push functions (C -> stack)
*/


LUA_API void lua_pushhl2mpplayer (lua_State *L, CHL2MP_Player *pPlayer) {
  CBaseHandle *hPlayer = (CBaseHandle *)lua_newuserdata(L, sizeof(CBaseHandle));
  hPlayer->Set(pPlayer);
  luaL_getmetatable(L, "CHL2MP_Player");
  lua_setmetatable(L, -2);
}


LUALIB_API lua_CHL2MP_Player *luaL_checkhl2mpplayer (lua_State *L, int narg) {
  lua_CHL2MP_Player *d = lua_tohl2mpplayer(L, narg);
  if (d == NULL)  /* avoid extra test when d is not 0 */
    luaL_argerror(L, narg, "CHL2MP_Player expected, got NULL entity");
  return d;
}


LUALIB_API lua_CHL2MP_Player *luaL_opthl2mpplayer (lua_State *L, int narg,
                                                                 CHL2MP_Player *def) {
  return luaL_opt(L, luaL_checkhl2mpplayer, narg, def);
}


static int CHL2MP_Player_BecomeRagdollOnClient (lua_State *L) {
#ifdef CLIENT_DLL
  lua_pushanimating(L, luaL_checkhl2mpplayer(L, 1)->BecomeRagdollOnClient());
#else
  lua_pushboolean(L, luaL_checkhl2mpplayer(L, 1)->BecomeRagdollOnClient(luaL_checkvector(L, 2)));
#endif
  return 1;
}

static int CHL2MP_Player_CalculateIKLocks (lua_State *L) {
  luaL_checkhl2mpplayer(L, 1)->CalculateIKLocks(luaL_checknumber(L, 2));
  return 0;
}

static int CHL2MP_Player_CalcView (lua_State *L) {
  Vector eyeOrigin;
  QAngle eyeAngles;
  float zNear, zFar, fov;
  luaL_checkhl2mpplayer(L, 1)->CalcView(eyeOrigin, eyeAngles, zNear, zFar, fov);
  lua_pushvector(L, eyeOrigin);
  lua_pushangle(L, eyeAngles);
  lua_pushnumber(L, zNear);
  lua_pushnumber(L, zFar);
  lua_pushnumber(L, fov);
  return 5;
}

static int CHL2MP_Player_CanSprint (lua_State *L) {
  lua_pushboolean(L, luaL_checkhl2mpplayer(L, 1)->CanSprint());
  return 1;
}

// ===========================================================================
// HL2SB (2026-09-27): GMod Player animation/movement bindings.
//
// reference contract (client.dll Player meta, 102 methods) + base gamemode
// animations.lua usage.  The gesture-slot functions run their layer work on
// the SERVER (CBaseAnimatingOverlay layers replicate through
// DT_BaseAnimatingOverlay; the fork learned in hl2mp_player.cpp:135 that
// client-written layers are stomped by the networked decode) and are no-ops
// on the client.
// ===========================================================================

static int CHL2MP_Player_AnimRestartMainSequence (lua_State *L) {
  CHL2MP_Player *pPlayer = luaL_checkhl2mpplayer(L, 1);
  pPlayer->m_flAnimTime = gpGlobals->curtime;
  pPlayer->SetCycle( 0 );
  return 0;
}

static int CHL2MP_Player_AnimRestartGesture (lua_State *L) {
#ifndef CLIENT_DLL
  luaL_checkhl2mpplayer(L, 1)->HL2SB_AnimRestartGestureLocal(
      luaL_checkint(L, 2), (Activity)luaL_checkint(L, 3), luaL_optboolean(L, 4, false) );
#else
  // HL2SB (2026-10-02): GMod architecture - the binding is LIVE on the client
  // too (GMod's client table has it; we reference ).  Shared
  // animations.lua code calling ply:AnimRestartGesture now works in both
  // realms; the client builds the layer locally.
  luaL_checkhl2mpplayer(L, 1)->HL2SB_ClientRestartGesture(
      luaL_checkint(L, 2), (Activity)luaL_checkint(L, 3), luaL_optboolean(L, 4, false) );
#endif
  return 0;
}

static int CHL2MP_Player_AnimResetGestureSlot (lua_State *L) {
#ifndef CLIENT_DLL
  luaL_checkhl2mpplayer(L, 1)->HL2SB_AnimResetGestureSlot( luaL_checkint(L, 2) );
#else
  luaL_checkhl2mpplayer(L, 1)->HL2SB_ClientResetGestureSlot( luaL_checkint(L, 2) );
#endif
  return 0;
}

static int CHL2MP_Player_AnimSetGestureWeight (lua_State *L) {
#ifndef CLIENT_DLL
  luaL_checkhl2mpplayer(L, 1)->HL2SB_AnimSetGestureWeight(
      luaL_checkint(L, 2), luaL_checknumber(L, 3) );
#else
  // Client-local layers own their weight (Lua fades, e.g. the chat gesture).
  CHL2MP_Player *pPlayer = luaL_checkhl2mpplayer(L, 1);
  int iSlot = luaL_checkint(L, 2);
  if ( iSlot >= 0 && iSlot < pPlayer->GetNumAnimOverlays() )
    pPlayer->GetAnimOverlay( iSlot )->m_flWeight = clamp( (float)luaL_checknumber(L, 3), 0.0f, 1.0f );
#endif
  return 0;
}

static int CHL2MP_Player_AnimSetGestureSequence (lua_State *L) {
#ifndef CLIENT_DLL
  luaL_checkhl2mpplayer(L, 1)->HL2SB_AnimSetGestureSequence(
      luaL_checkint(L, 2), luaL_checkint(L, 3) );
#else
  CHL2MP_Player *pPlayer = luaL_checkhl2mpplayer(L, 1);
  int iSlot = luaL_checkint(L, 2);
  int iSequence = luaL_checkint(L, 3);
  if ( iSlot >= 0 && iSlot < pPlayer->GetNumAnimOverlays() && iSequence > 0 )
  {
    pPlayer->GetAnimOverlay( iSlot )->m_nSequence = iSequence;
    pPlayer->m_flOverlayPrevEventCycle[iSlot] = -1.0f;
  }
#endif
  return 0;
}

static int CHL2MP_Player_AddVCDSequenceToGestureSlot (lua_State *L) {
  CHL2MP_Player *pPlayer = luaL_checkhl2mpplayer(L, 1);
  int iSlot = luaL_checkint(L, 2);
  if ( iSlot < 0 ) return 0;
  int iSequence = luaL_checkint(L, 3);
  if ( iSequence < 0 ) return 0;
  float flCycle = luaL_checknumber(L, 4);
  bool bAutoKill = luaL_optboolean(L, 5, false);
  if ( iSlot >= GESTURE_SLOT_COUNT )
    return 0;
#ifndef CLIENT_DLL
  pPlayer->HL2SB_AnimResetGestureSlot( iSlot );
  int iLayer = pPlayer->AddGestureSequence( iSequence, bAutoKill );
  if ( iLayer >= 0 )
  {
    pPlayer->SetLayerCycle( iLayer, flCycle, flCycle );
    pPlayer->SetLayerWeight( iLayer, 1.0f );
    pPlayer->m_iHL2SBSlotLayer[iSlot] = iLayer;
    pPlayer->m_iHL2SBSlotActivity[iSlot] = ACT_INVALID;
  }
#else
  pPlayer->HL2SB_ClientResetGestureSlot( iSlot );
  pPlayer->HL2SB_ClientSetLayerSequence( iSlot, iSequence, ACT_MP_VCD, bAutoKill, 1.0f );
  if ( iSlot < pPlayer->GetNumAnimOverlays() )
    pPlayer->GetAnimOverlay( iSlot )->m_flCycle = flCycle;
#endif
  return 0;
}

static int CHL2MP_Player_IsPlayingTaunt (lua_State *L) {
  // HL2SB (2026-09-27): both realms now carry the replicated taunt clock, so
  // the real answer replaces the old client-side `false` stub (the Lua taunt
  // camera polls IsPlayingTaunt every frame on the client).
  lua_pushboolean(L, luaL_checkhl2mpplayer(L, 1)->HL2SB_IsPlayingTaunt());
  return 1;
}

// GMod: Player:DoAnimationEvent( event, data = 0 ) - the programmatic trigger
// (SWEPs use it server-side).  The GM:DoAnimationEvent hook gets its chance
// first, then the stock MP-animstate event mapping runs.
static int CHL2MP_Player_DoAnimationEvent (lua_State *L) {
#ifndef CLIENT_DLL
  CHL2MP_Player *pPlayer = luaL_checkhl2mpplayer(L, 1);
  PlayerAnimEvent_t event = (PlayerAnimEvent_t)luaL_checkint(L, 2);
  int nData = luaL_optinteger(L, 3, 0);

  if ( pPlayer->HL2SB_DoAnimationEventLua( event, nData ) )
    return 0;

  bool bDucking = ( pPlayer->GetFlags() & FL_DUCKING ) != 0;
  switch ( event )
  {
  case PLAYERANIMEVENT_ATTACK_PRIMARY:
    pPlayer->HL2SB_AnimRestartGesture( GESTURE_SLOT_ATTACK_AND_RELOAD,
        bDucking ? ACT_MP_ATTACK_CROUCH_PRIMARYFIRE : ACT_MP_ATTACK_STAND_PRIMARYFIRE, true );
    break;
  case PLAYERANIMEVENT_ATTACK_SECONDARY:
    pPlayer->HL2SB_AnimRestartGesture( GESTURE_SLOT_ATTACK_AND_RELOAD,
        ACT_MP_ATTACK_STAND_SECONDARYFIRE, true );
    break;
  case PLAYERANIMEVENT_RELOAD:
    pPlayer->HL2SB_AnimRestartGesture( GESTURE_SLOT_ATTACK_AND_RELOAD,
        bDucking ? ACT_MP_RELOAD_CROUCH : ACT_MP_RELOAD_STAND, true );
    break;
  case PLAYERANIMEVENT_CANCEL_RELOAD:
    pPlayer->HL2SB_AnimResetGestureSlot( GESTURE_SLOT_ATTACK_AND_RELOAD );
    break;
  case PLAYERANIMEVENT_CUSTOM_GESTURE_SEQUENCE:
  case PLAYERANIMEVENT_CUSTOM_GESTURE:
  case PLAYERANIMEVENT_CUSTOM_SEQUENCE:
    pPlayer->HL2SB_AnimRestartGesture( GESTURE_SLOT_CUSTOM, (Activity)nData, true );
    break;
  default:
    // HL2SB (2026-10-02): anything the state machine above does not map to a
    // server gesture goes out over the GMod gesture broadcast verbatim
    // (FLINCH/JUMP/SWIM/... - the client's dispatch decides what to do with
    // it; unhandled events log under hl2sb_anim_debug).
    TE_PlayerAnimEvent( pPlayer, event, nData );
    break;
  }
#else
  // HL2SB (2026-10-02): live on the client as well (GMod's client table has
  // it) - shared SWEP bases calling ply:DoAnimationEvent on the client realm
  // drive the local layer directly.
  luaL_checkhl2mpplayer(L, 1)->HL2SB_ClientDoAnimationEvent(
      luaL_checkint(L, 2), luaL_optinteger(L, 3, 0) );
#endif
  return 0;
}

#ifndef CLIENT_DLL
// HL2SB (2026-10-02): GMod's raw event broadcasts - the exact server Lua
// bindings we reference from server.dll (table at ..):
// DoAttackEvent / DoSecondaryAttack / DoReloadEvent / DoCustomAnimEvent.
// Each sends CTEPlayerAnimEvent directly; relay-style extra arguments are
// accepted and ignored (our CPVSFilter is always anchored on the player).
void TE_PlayerAnimEvent( CBasePlayer *pPlayer, PlayerAnimEvent_t event, int nData );

static int CHL2MP_Player_DoAttackEvent (lua_State *L) {
  (void)luaL_optboolean(L, 2, true);	// GMod's relay flag - see above
  TE_PlayerAnimEvent( luaL_checkhl2mpplayer(L, 1), PLAYERANIMEVENT_ATTACK_PRIMARY, 0 );
  return 0;
}

static int CHL2MP_Player_DoSecondaryAttack (lua_State *L) {
  (void)luaL_optboolean(L, 2, true);
  TE_PlayerAnimEvent( luaL_checkhl2mpplayer(L, 1), PLAYERANIMEVENT_ATTACK_SECONDARY, 0 );
  return 0;
}

static int CHL2MP_Player_DoReloadEvent (lua_State *L) {
  (void)luaL_optboolean(L, 2, true);
  TE_PlayerAnimEvent( luaL_checkhl2mpplayer(L, 1), PLAYERANIMEVENT_RELOAD, 0 );
  return 0;
}
#endif	// !CLIENT_DLL

// HL2SB (2026-10-02): GMod's DoCustomAnimEvent exists on BOTH realms (client
// binding reference at ): server -> raw TE broadcast, client ->
// the local gesture dispatch.
static int CHL2MP_Player_DoCustomAnimEvent (lua_State *L) {
  int iEvent = luaL_checkint(L, 2);
  int nData = luaL_optinteger(L, 3, 0);
  // GMod clamps the event id to 0..39 in this binding.
  if ( iEvent < 0 ) iEvent = 0;
  if ( iEvent > 39 ) iEvent = 39;
#ifndef CLIENT_DLL
  (void)luaL_optboolean(L, 4, true);
  TE_PlayerAnimEvent( luaL_checkhl2mpplayer(L, 1), (PlayerAnimEvent_t)iEvent, nData );
#else
  luaL_checkhl2mpplayer(L, 1)->HL2SB_ClientDoAnimationEvent( iEvent, nData );
#endif
  return 0;
}

// GMod: Player:TranslateWeaponActivity( act ) - the active weapon's acttable.
// The active weapon supplies the same activity translation on both realms.
static int CHL2MP_Player_TranslateWeaponActivity (lua_State *L) {
  CHL2MP_Player *pPlayer = luaL_checkhl2mpplayer(L, 1);
  Activity act = (Activity)luaL_checkint(L, 2);
  CBaseCombatWeapon *pWeapon = pPlayer->GetActiveWeapon();
  lua_pushinteger( L, pWeapon ? pWeapon->ActivityOverride( act, NULL ) : act );
  return 1;
}

// ---------------------------------------------------------------------------
// GMod movement speeds.  Walk / Run / JumpPower feed the movement engine
// (GetPlayerMaxSpeed / CheckJumpButton); the rest are stored per player.
// ---------------------------------------------------------------------------
static int CHL2MP_Player_SetWalkSpeed (lua_State *L) {
  luaL_checkhl2mpplayer(L, 1)->m_flHL2SBWalkSpeed = luaL_checknumber(L, 2); return 0;
}
static int CHL2MP_Player_GetWalkSpeed (lua_State *L) {
  lua_pushnumber(L, luaL_checkhl2mpplayer(L, 1)->m_flHL2SBWalkSpeed); return 1;
}
static int CHL2MP_Player_SetRunSpeed (lua_State *L) {
  luaL_checkhl2mpplayer(L, 1)->m_flHL2SBRunSpeed = luaL_checknumber(L, 2); return 0;
}
static int CHL2MP_Player_GetRunSpeed (lua_State *L) {
  lua_pushnumber(L, luaL_checkhl2mpplayer(L, 1)->m_flHL2SBRunSpeed); return 1;
}
static int CHL2MP_Player_SetSlowWalkSpeed (lua_State *L) {
  luaL_checkhl2mpplayer(L, 1)->m_flHL2SBSlowWalkSpeed = luaL_checknumber(L, 2); return 0;
}
static int CHL2MP_Player_GetSlowWalkSpeed (lua_State *L) {
  lua_pushnumber(L, luaL_checkhl2mpplayer(L, 1)->m_flHL2SBSlowWalkSpeed); return 1;
}
static int CHL2MP_Player_SetCrouchedWalkSpeed (lua_State *L) {
  luaL_checkhl2mpplayer(L, 1)->m_flHL2SBCrouchedWalkSpeed = luaL_checknumber(L, 2); return 0;
}
static int CHL2MP_Player_GetCrouchedWalkSpeed (lua_State *L) {
  lua_pushnumber(L, luaL_checkhl2mpplayer(L, 1)->m_flHL2SBCrouchedWalkSpeed); return 1;
}
static int CHL2MP_Player_SetDuckSpeed (lua_State *L) {
  CHL2MP_Player *pPlayer = luaL_checkhl2mpplayer(L, 1);
  pPlayer->m_flHL2SBDuckSpeed = luaL_checknumber(L, 2);
#ifndef CLIENT_DLL
  pPlayer->NetworkStateChanged();
#endif
  return 0;
}
static int CHL2MP_Player_GetDuckSpeed (lua_State *L) {
  lua_pushnumber(L, luaL_checkhl2mpplayer(L, 1)->m_flHL2SBDuckSpeed); return 1;
}
static int CHL2MP_Player_SetUnDuckSpeed (lua_State *L) {
  CHL2MP_Player *pPlayer = luaL_checkhl2mpplayer(L, 1);
  pPlayer->m_flHL2SBUnDuckSpeed = luaL_checknumber(L, 2);
#ifndef CLIENT_DLL
  pPlayer->NetworkStateChanged();
#endif
  return 0;
}
static int CHL2MP_Player_GetUnDuckSpeed (lua_State *L) {
  lua_pushnumber(L, luaL_checkhl2mpplayer(L, 1)->m_flHL2SBUnDuckSpeed); return 1;
}
static int CHL2MP_Player_SetLadderClimbSpeed (lua_State *L) {
  luaL_checkhl2mpplayer(L, 1)->m_flHL2SBLadderClimbSpeed = luaL_checknumber(L, 2); return 0;
}
static int CHL2MP_Player_GetLadderClimbSpeed (lua_State *L) {
  lua_pushnumber(L, luaL_checkhl2mpplayer(L, 1)->m_flHL2SBLadderClimbSpeed); return 1;
}
static int CHL2MP_Player_SetJumpPower (lua_State *L) {
  luaL_checkhl2mpplayer(L, 1)->m_flHL2SBJumpPower = luaL_checknumber(L, 2); return 0;
}
static int CHL2MP_Player_GetJumpPower (lua_State *L) {
  lua_pushnumber(L, luaL_checkhl2mpplayer(L, 1)->m_flHL2SBJumpPower); return 1;
}
static int CHL2MP_Player_SetStepSize (lua_State *L) {
  luaL_checkhl2mpplayer(L, 1)->m_flHL2SBStepSize = luaL_checknumber(L, 2); return 0;
}
static int CHL2MP_Player_GetStepSize (lua_State *L) {
  lua_pushnumber(L, luaL_checkhl2mpplayer(L, 1)->m_flHL2SBStepSize); return 1;
}
static int CHL2MP_Player_SetAllowWeaponsInVehicle (lua_State *L) {
  luaL_checkhl2mpplayer(L, 1)->m_bHL2SBAllowWeaponsInVehicle = luaL_checkboolean(L, 2); return 0;
}
static int CHL2MP_Player_GetAllowWeaponsInVehicle (lua_State *L) {
  lua_pushboolean(L, luaL_checkhl2mpplayer(L, 1)->m_bHL2SBAllowWeaponsInVehicle); return 1;
}

// ---------------------------------------------------------------------------
// Chat / voice state.  v1: IsTyping has no engine plumbing in this fork's chat
// yet (GMod keeps it in its C++ chat panel), so it answers false.  The voice
// side reads the CVoiceStatus state -- speaking bits plus the loudness
// samples the engine voice path pushes -- which is what animations.lua's
// MouthMoveAnimation and the base voice HUD consume.
// ---------------------------------------------------------------------------
static int CHL2MP_Player_IsTyping (lua_State *L) {
  lua_pushboolean(L, false);
  return 1;
}
#ifdef CLIENT_DLL
// Reference behavior: Player:IsSpeaking answers the local recorder state for
// the local player and the voice-manager per-index speaking bit for everyone
// else, so remote speakers report too (the base animation file's mouth
// movement reads it on other players).
static bool HL2SB_PlayerSpeaking( CHL2MP_Player *pPlayer )
{
  CVoiceStatus *pVoiceMgr = GetClientVoiceMgr();
  if ( pVoiceMgr == NULL )
    return false;
  if ( pPlayer->IsLocalPlayer() )
    return pVoiceMgr->IsLocalPlayerSpeaking();
  int entindex = pPlayer->entindex();
  if ( entindex < 1 || entindex > VOICE_MAX_PLAYERS )
    return false;
  return pVoiceMgr->IsPlayerSpeaking( entindex );
}

// Reference behavior: Player:VoiceVolume reads the per-player loudness table
// fed by the engine voice path.  While no engine sample has arrived yet for
// a speaking player (no mixer, Steam voice path, before the first pull) the
// answer keeps the documented while-speaking / silent endpoints (1 / 0).
static float HL2SB_PlayerVoiceVolume( CHL2MP_Player *pPlayer )
{
  if ( !HL2SB_PlayerSpeaking( pPlayer ) )
    return 0.0f;

  bool bValid = false;
  float flLoudness = GetClientVoiceMgr()->GetPlayerLoudness( pPlayer->entindex(), &bValid );
  return bValid ? flLoudness : 1.0f;
}

// ---------------------------------------------------------------------------
// Per-steamid voice volume scale (GMod Player:Get/SetVoiceVolumeScale --
// client-realm Player methods; the reference stores the scale per steam id,
// answers 1.0 for unknown players and clamps Set to 0..1).  The fork keys
// the store on the player_info_t guid -- the same stable identity the
// engine reports for the player across reconnects.
// ---------------------------------------------------------------------------
struct HL2SBVoiceVolumeScaleEntry
{
  char  guid[SIGNED_GUID_LEN + 1];
  float flScale;
};
static CUtlVector<HL2SBVoiceVolumeScaleEntry> s_VoiceVolumeScales;

static bool HL2SB_GetPlayerGuid( CHL2MP_Player *pPlayer, char *pszGuid, int nSize )
{
  player_info_t pi;
  if ( !engine->GetPlayerInfo( pPlayer->entindex(), &pi ) )
    return false;
  if ( pi.guid[0] == '\0' )
    return false;
  Q_strncpy( pszGuid, pi.guid, nSize );
  return true;
}

static float HL2SB_FindVoiceVolumeScale( const char *pszGuid )
{
  FOR_EACH_VEC( s_VoiceVolumeScales, i )
  {
    if ( FStrEq( s_VoiceVolumeScales[i].guid, pszGuid ) )
      return s_VoiceVolumeScales[i].flScale;
  }
  return 1.0f;
}

static void HL2SB_StoreVoiceVolumeScale( const char *pszGuid, float flScale )
{
  FOR_EACH_VEC( s_VoiceVolumeScales, i )
  {
    if ( FStrEq( s_VoiceVolumeScales[i].guid, pszGuid ) )
    {
      s_VoiceVolumeScales[i].flScale = flScale;
      return;
    }
  }

  int iSlot = s_VoiceVolumeScales.AddToTail();
  Q_strncpy( s_VoiceVolumeScales[iSlot].guid, pszGuid, sizeof( s_VoiceVolumeScales[iSlot].guid ) );
  s_VoiceVolumeScales[iSlot].flScale = flScale;
}
#endif
static int CHL2MP_Player_IsSpeaking (lua_State *L) {
#ifdef CLIENT_DLL
  lua_pushboolean(L, HL2SB_PlayerSpeaking( luaL_checkhl2mpplayer(L, 1) ));
#else
  lua_pushboolean(L, false);
#endif
  return 1;
}
static int CHL2MP_Player_VoiceVolume (lua_State *L) {
#ifdef CLIENT_DLL
  lua_pushnumber(L, HL2SB_PlayerVoiceVolume( luaL_checkhl2mpplayer(L, 1) ));
#else
  lua_pushnumber(L, 0.0f);
#endif
  return 1;
}
static int CHL2MP_Player_GetVoiceVolumeScale (lua_State *L) {
#ifdef CLIENT_DLL
  char szGuid[SIGNED_GUID_LEN + 1];
  if ( HL2SB_GetPlayerGuid( luaL_checkhl2mpplayer(L, 1), szGuid, sizeof(szGuid) ) )
    lua_pushnumber(L, HL2SB_FindVoiceVolumeScale( szGuid ));
  else
    lua_pushnumber(L, 1.0f);	// reference: unknown players answer 1.0
#else
  lua_pushnumber(L, 1.0f);
#endif
  return 1;
}
static int CHL2MP_Player_SetVoiceVolumeScale (lua_State *L) {
#ifdef CLIENT_DLL
  float flScale = (float)luaL_checknumber(L, 2);
  if ( flScale < 0.0f ) flScale = 0.0f;		// reference clamps 0..1
  else if ( flScale > 1.0f ) flScale = 1.0f;
  char szGuid[SIGNED_GUID_LEN + 1];
  if ( HL2SB_GetPlayerGuid( luaL_checkhl2mpplayer(L, 1), szGuid, sizeof(szGuid) ) )
    HL2SB_StoreVoiceVolumeScale( szGuid, flScale );
#endif
  return 0;
}
#ifdef CLIENT_DLL
static int CHL2MP_Player_ShowProfile (lua_State *L) {
  luaL_checkhl2mpplayer(L, 1);
  // Steam profile overlays are intentionally unavailable in this client.
  return 0;
}
#endif

// Reference: Player:IsMuted answers the client's mute store for that player
// (the same store the scoreboard squelch drives).
static int CHL2MP_Player_IsMuted (lua_State *L) {
#ifdef CLIENT_DLL
  int entindex = luaL_checkhl2mpplayer(L, 1)->entindex();
  CVoiceStatus *pVoiceMgr = GetClientVoiceMgr();
  lua_pushboolean(L, pVoiceMgr != NULL && entindex >= 1 && entindex <= VOICE_MAX_PLAYERS &&
                     pVoiceMgr->IsPlayerBlocked( entindex ));
#else
  lua_pushboolean(L, false);
#endif
  return 1;
}
// Reference: Player:SetMuted stores the requested state (set, not toggle) and
// resyncs the engine-side voice ban mask.
static int CHL2MP_Player_SetMuted (lua_State *L) {
#ifdef CLIENT_DLL
  int entindex = luaL_checkhl2mpplayer(L, 1)->entindex();
  bool bMuted = luaL_checkboolean(L, 2) != 0;
  CVoiceStatus *pVoiceMgr = GetClientVoiceMgr();
  if ( pVoiceMgr != NULL && entindex >= 1 && entindex <= VOICE_MAX_PLAYERS )
    pVoiceMgr->SetPlayerBlocked( entindex, bMuted );
#endif
  return 0;
}

// GMod: Entity:SetIK( bool ) - stored only in this fork (no engine plumbing
// for the player IK chain yet); noclip keeps its C++-managed pose layer.
static int CHL2MP_Player_SetIK (lua_State *L) {
  luaL_checkhl2mpplayer(L, 1);
  return 0;
}

// HL2SB (2026-09-27): real GetHands/SetHands, GMod's reference semantics.
// GetHands resolves the replicated m_hHands handle (serial-checked on GMod's
// side; EHANDLE::Get does the same here) and pushes the entity, nil when
// empty/stale.  SetHands takes an entity (nil clears); on the server the
// CNetworkHandle assignment marks the prop dirty exactly like GMod's
// change-guarded write.
static int CHL2MP_Player_GetHands (lua_State *L) {
  CBaseEntity *pHands = luaL_checkhl2mpplayer(L, 1)->HL2SB_GetHandsEntity();
  if ( pHands != NULL )
    lua_pushentity( L, pHands );
  else
    lua_pushnil( L );
  return 1;
}
static int CHL2MP_Player_SetHands (lua_State *L) {
  CHL2MP_Player *pPlayer = luaL_checkhl2mpplayer(L, 1);
  if ( lua_isnoneornil( L, 2 ) )
    pPlayer->HL2SB_SetHandsEntity( NULL );
  else
    pPlayer->HL2SB_SetHandsEntity( lua_toentity( L, 2 ) );
  return 0;
}

static int CHL2MP_Player___index (lua_State *L) {
  CHL2MP_Player *pPlayer = lua_tohl2mpplayer(L, 1);
  if (pPlayer == NULL) {  /* avoid extra test when d is not 0 */
    /* HL2SB: GMod's NULL sentinel answers reads instead of raising -- same
    ** contract as CBaseEntity___index / CBasePlayer___index. */
    HL2SB_PushNullEntityIndex( L, lua_tostring( L, 2 ) );
    return 1;
  }
  const char *field = luaL_checkstring(L, 2);
#ifdef CLIENT_DLL
  if (Q_strcmp(field, "m_fNextThinkPushAway") == 0)
  {
    // HL2SB (2026-09-25): leftover debug Msg removed -- any addon writing
    // this field printed "QQWEE" to the console on every write.
  }
  else {
#endif
    if (lua_isrefvalid(L, pPlayer->m_nTableReference)) {
      lua_getref(L, pPlayer->m_nTableReference);
      lua_getfield(L, -1, field);
      if (lua_isnil(L, -1)) {
        lua_pop(L, 2);
        lua_getmetatable(L, 1);
        lua_getfield(L, -1, field);
        if (lua_isnil(L, -1)) {
          lua_pop(L, 2);
          luaL_getmetatable(L, "CBasePlayer");
          lua_getfield(L, -1, field);
          if (lua_isnil(L, -1)) {
            lua_pop(L, 2);
            luaL_getmetatable(L, "CBaseAnimating");
            lua_getfield(L, -1, field);
            if (lua_isnil(L, -1)) {
              lua_pop(L, 2);
              luaL_getmetatable(L, "CBaseEntity");
              lua_getfield(L, -1, field);
            }
          }
        }
      }
    }
    else {
      lua_getmetatable(L, 1);
      lua_getfield(L, -1, field);
      if (lua_isnil(L, -1)) {
        lua_pop(L, 2);
        luaL_getmetatable(L, "CBasePlayer");
        lua_getfield(L, -1, field);
        if (lua_isnil(L, -1)) {
          lua_pop(L, 2);
          luaL_getmetatable(L, "CBaseAnimating");
          lua_getfield(L, -1, field);
          if (lua_isnil(L, -1)) {
            lua_pop(L, 2);
            luaL_getmetatable(L, "CBaseEntity");
            lua_getfield(L, -1, field);
          }
        }
      }
    }
#ifdef CLIENT_DLL
  }
#endif
  return 1;
}

static int CHL2MP_Player___newindex (lua_State *L) {
  CHL2MP_Player *pPlayer = lua_tohl2mpplayer(L, 1);
  if (pPlayer == NULL) {  /* avoid extra test when d is not 0 */
    /* HL2SB (2026-10-12): keep raising -- this one matches the reference.
    ** The reference gives the PLAYER class its own __newindex whose NULL
    ** path errors ("Player entity is NULL or not a player"), unlike the
    ** entity/weapon classes whose NULL write is silently discarded (see
    ** CBaseEntity___newindex / CBaseCombatWeapon___newindex). */
    lua_Debug ar1;
    lua_getstack(L, 1, &ar1);
    lua_getinfo(L, "fl", &ar1);
    lua_Debug ar2;
    lua_getinfo(L, ">S", &ar2);
	lua_pushfstring(L, "%s:%d: attempt to index a NULL entity", ar2.short_src, ar1.currentline);
	return lua_error(L);
  }
  const char *field = luaL_checkstring(L, 2);
#ifdef CLIENT_DLL
  if (Q_strcmp(field, "m_fNextThinkPushAway") == 0)
  {
    // HL2SB (2026-09-25): leftover debug Msg removed.
  }
  else {
#endif
    // HL2SB: < 0, not == LUA_NOREF -- LUA_REFNIL (-1) is not LUA_NOREF (-2);
    // with the old test lua_getref(-1) pushed nil and lua_setfield dropped the
    // write silently (same root cause as the CBaseEntity___newindex fix).
    if (pPlayer->m_nTableReference < 0) {
      lua_newtable(L);
      pPlayer->m_nTableReference = luaL_ref(L, LUA_REGISTRYINDEX);
    }
    lua_getref(L, pPlayer->m_nTableReference);
    lua_pushvalue(L, 3);
    lua_setfield(L, -2, field);
	lua_pop(L, 1);
#ifdef CLIENT_DLL
  }
#endif
  return 0;
}

static int CHL2MP_Player___eq (lua_State *L) {
  lua_pushboolean(L, lua_tohl2mpplayer(L, 1) == lua_tohl2mpplayer(L, 2));
  return 1;
}

static int CHL2MP_Player___tostring (lua_State *L) {
  CHL2MP_Player *pPlayer = lua_tohl2mpplayer(L, 1);
  if (pPlayer == NULL)
    lua_pushstring(L, "NULL");
  else
    lua_pushfstring(L, "CHL2MP_Player: %d \"%s\"", pPlayer->GetUserID(), pPlayer->GetPlayerName());
  return 1;
}


static const luaL_Reg CHL2MP_Playermeta[] = {
  {"BecomeRagdollOnClient", CHL2MP_Player_BecomeRagdollOnClient},
  {"CalculateIKLocks", CHL2MP_Player_CalculateIKLocks},
  {"CalcView", CHL2MP_Player_CalcView},
  {"CanSprint", CHL2MP_Player_CanSprint},
  {"DoAnimationEvent", CHL2MP_Player_DoAnimationEvent},
#ifndef CLIENT_DLL
  // HL2SB (2026-10-02): GMod raw event broadcasts (server-only; GMod's
  // client table does not carry these three).
  {"DoAttackEvent", CHL2MP_Player_DoAttackEvent},
  {"DoSecondaryAttack", CHL2MP_Player_DoSecondaryAttack},
  {"DoReloadEvent", CHL2MP_Player_DoReloadEvent},
#endif
  {"DoCustomAnimEvent", CHL2MP_Player_DoCustomAnimEvent},
  {"__index", CHL2MP_Player___index},
  {"__newindex", CHL2MP_Player___newindex},
  {"__eq", CHL2MP_Player___eq},
  {"__tostring", CHL2MP_Player___tostring},
  {NULL, NULL}
};

// HL2SB (2026-09-27): the GMod Player surface, registered on BOTH metatables.
// A player reaches Lua through lua_pushplayer(), which installs the
// "CBasePlayer" metatable (LocalPlayer(), Entity(n), player.GetAll(), and this
// fork's animation hook dispatches); the "CHL2MP_Player" metatable only ever
// falls back TO CBasePlayer, never the reverse, so registering these only
// there left ply:AnimRestartGesture()/TranslateWeaponActivity()/IsPlayingTaunt()
// nil on every player Lua actually holds.
#ifndef CLIENT_DLL
static int CHL2MP_Player_CreateRagdoll( lua_State *L )
{
	luaL_checkhl2mpplayer( L, 1 )->CreateRagdollEntity();
	return 0;
}

static int CHL2MP_Player_ShouldDropWeapon( lua_State *L )
{
	CHL2MP_Player *player = luaL_checkhl2mpplayer( L, 1 );
	const bool enabled = lua_toboolean( L, 2 ) != 0;
	lua_getref( L, player->m_nTableReference );
	if ( !lua_istable( L, -1 ) )
	{
		lua_pop( L, 1 );
		if ( player->m_nTableReference >= 0 )
			luaL_unref( L, LUA_REGISTRYINDEX, player->m_nTableReference );
		lua_newtable( L );
		lua_pushvalue( L, -1 );
		player->m_nTableReference = luaL_ref( L, LUA_REGISTRYINDEX );
	}
	lua_pushliteral( L, "__hl2sbDropWeaponOnDeath" );
	lua_pushboolean( L, enabled );
	lua_rawset( L, -3 );
	lua_pop( L, 1 );
	return 0;
}
#endif

static const luaL_Reg CHL2MP_PlayerGModmeta[] = {
#ifndef CLIENT_DLL
  {"CreateRagdoll", CHL2MP_Player_CreateRagdoll},
  {"ShouldDropWeapon", CHL2MP_Player_ShouldDropWeapon},
#endif
  // HL2SB (2026-09-27): GMod Player animation/movement surface.
  {"DoAnimationEvent", CHL2MP_Player_DoAnimationEvent},
#ifndef CLIENT_DLL
  // HL2SB (2026-10-02): GMod raw event broadcasts (server-only).
  {"DoAttackEvent", CHL2MP_Player_DoAttackEvent},
  {"DoSecondaryAttack", CHL2MP_Player_DoSecondaryAttack},
  {"DoReloadEvent", CHL2MP_Player_DoReloadEvent},
#endif
  {"DoCustomAnimEvent", CHL2MP_Player_DoCustomAnimEvent},
  {"AnimRestartMainSequence", CHL2MP_Player_AnimRestartMainSequence},
  {"AnimRestartGesture", CHL2MP_Player_AnimRestartGesture},
  {"AnimResetGestureSlot", CHL2MP_Player_AnimResetGestureSlot},
  {"AnimSetGestureWeight", CHL2MP_Player_AnimSetGestureWeight},
  {"AnimSetGestureSequence", CHL2MP_Player_AnimSetGestureSequence},
  {"AddVCDSequenceToGestureSlot", CHL2MP_Player_AddVCDSequenceToGestureSlot},
  {"IsPlayingTaunt", CHL2MP_Player_IsPlayingTaunt},
  {"TranslateWeaponActivity", CHL2MP_Player_TranslateWeaponActivity},
  {"SetWalkSpeed", CHL2MP_Player_SetWalkSpeed},
  {"GetWalkSpeed", CHL2MP_Player_GetWalkSpeed},
  {"SetRunSpeed", CHL2MP_Player_SetRunSpeed},
  {"GetRunSpeed", CHL2MP_Player_GetRunSpeed},
  {"SetSlowWalkSpeed", CHL2MP_Player_SetSlowWalkSpeed},
  {"GetSlowWalkSpeed", CHL2MP_Player_GetSlowWalkSpeed},
  {"SetCrouchedWalkSpeed", CHL2MP_Player_SetCrouchedWalkSpeed},
  {"GetCrouchedWalkSpeed", CHL2MP_Player_GetCrouchedWalkSpeed},
  {"SetDuckSpeed", CHL2MP_Player_SetDuckSpeed},
  {"GetDuckSpeed", CHL2MP_Player_GetDuckSpeed},
  {"SetUnDuckSpeed", CHL2MP_Player_SetUnDuckSpeed},
  {"GetUnDuckSpeed", CHL2MP_Player_GetUnDuckSpeed},
  {"SetLadderClimbSpeed", CHL2MP_Player_SetLadderClimbSpeed},
  {"GetLadderClimbSpeed", CHL2MP_Player_GetLadderClimbSpeed},
  {"SetJumpPower", CHL2MP_Player_SetJumpPower},
  {"GetJumpPower", CHL2MP_Player_GetJumpPower},
  {"SetStepSize", CHL2MP_Player_SetStepSize},
  {"GetStepSize", CHL2MP_Player_GetStepSize},
  {"SetAllowWeaponsInVehicle", CHL2MP_Player_SetAllowWeaponsInVehicle},
  {"GetAllowWeaponsInVehicle", CHL2MP_Player_GetAllowWeaponsInVehicle},
  {"IsTyping", CHL2MP_Player_IsTyping},
  {"IsSpeaking", CHL2MP_Player_IsSpeaking},
  {"VoiceVolume", CHL2MP_Player_VoiceVolume},
#ifdef CLIENT_DLL
  // HL2SB (2026-10-13): the rest of the GMod voice surface -- client-realm
  // Player methods only (the reference registers them in its client Player
  // table and its server carries no such names).
  {"GetVoiceVolumeScale", CHL2MP_Player_GetVoiceVolumeScale},
  {"SetVoiceVolumeScale", CHL2MP_Player_SetVoiceVolumeScale},
  {"ShowProfile", CHL2MP_Player_ShowProfile},
  {"IsMuted", CHL2MP_Player_IsMuted},
  {"SetMuted", CHL2MP_Player_SetMuted},
#endif
  {"SetIK", CHL2MP_Player_SetIK},
  {"GetHands", CHL2MP_Player_GetHands},
  {"SetHands", CHL2MP_Player_SetHands},
  {NULL, NULL}
};


static int luasrc_ToHL2MPPlayer (lua_State *L) {
  lua_pushhl2mpplayer(L, ToHL2MPPlayer(lua_toentity(L, 1)));
  return 1;
}


static const luaL_Reg CHL2MP_Player_funcs[] = {
  {"ToHL2MPPlayer", luasrc_ToHL2MPPlayer},
  {NULL, NULL}
};


/*
** Open CHL2MP_Player object
*/
LUALIB_API int luaopen_CHL2MP_Player_shared (lua_State *L) {
  luaL_newmetatable(L, "CHL2MP_Player");
  luaL_register(L, NULL, CHL2MP_Playermeta);
  luaL_register(L, NULL, CHL2MP_PlayerGModmeta);
  lua_pushstring(L, "entity");
  lua_setfield(L, -2, "__type");  /* metatable.__type = "entity" */
  lua_pop(L, 1);

  // HL2SB (2026-09-27): the same GMod methods on the metatable every engine push
  // path installs (see CHL2MP_PlayerGModmeta above).  Only the method table goes
  // here - copying CHL2MP_Playermeta wholesale would clobber CBasePlayer's own
  // __index/__newindex.
  luaL_getmetatable(L, "CBasePlayer");
  if (!lua_isnoneornil(L, -1)) {
    luaL_register(L, NULL, CHL2MP_PlayerGModmeta);
    lua_pop(L, 1);
  } else {
    lua_pop(L, 1);
  }

  luaL_register(L, "_G", CHL2MP_Player_funcs);
  lua_pop(L, 1);
  return 1;
}


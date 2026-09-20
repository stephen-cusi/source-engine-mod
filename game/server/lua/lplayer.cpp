//===== Copyright © 1996-2005, Valve Corporation, All rights reserved. ======//
//
// Purpose: Functions dealing with the player.
//
//===========================================================================//

#define lplayer_cpp

#include "cbase.h"
#include "luamanager.h"
#include "lbasecombatweapon_shared.h"
#include "lbaseentity_shared.h"
#include "lbaseplayer_shared.h"
#include "ltakedamageinfo.h"
#include "mathlib/lvector.h"
#include "entitylist.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

//-----------------------------------------------------------------------------
// Purpose: HL2SB - player.GetAll()
//
// Returns a Lua array of every player entity currently on the server, so
// GMod-style scripts (and the undo system) can iterate players without a
// player.Iterator binding.  Built on gEntList.FirstEnt/NextEnt.
//-----------------------------------------------------------------------------
static int CBasePlayer_GetAll (lua_State *L) {
  lua_newtable( L );
  int i = 0;
  for ( CBaseEntity *pEntity = gEntList.FirstEnt(); pEntity != NULL; pEntity = gEntList.NextEnt( pEntity ) )
  {
    if ( !pEntity->IsPlayer() )
      continue;

    lua_pushinteger( L, ++i );
    lua_pushplayer( L, static_cast< CBasePlayer * >( pEntity ) );
    lua_rawset( L, -3 );
  }
  return 1;
}

//-----------------------------------------------------------------------------
// Purpose: HL2SB - player:UniqueID()
//
// Returns a stable per-player key (the engine user ID) so undo stacks and
// other per-player tables can be keyed like GMod's ply:UniqueID().
//-----------------------------------------------------------------------------
static int CBasePlayer_UniqueID (lua_State *L) {
  lua_pushinteger( L, luaL_checkplayer( L, 1 )->GetUserID() );
  return 1;
}

//-----------------------------------------------------------------------------
// HL2SB GMod compat: Player:ChatPrint( message ) -- "Prints a string to the
// chatbox of the client" (wiki).  GMod addons announce things per-player
// through it; scp049's infection broadcast sits on line 232 right before the
// zombie spawn, so without the server binding the whole kill->infect path
// aborted there.  The client realm has the binding (lc_chat.cpp); the server
// answers through the same usermessage GMod's own C++ uses
// (ClientPrint + HUD_PRINTTALK, util.cpp:1281).
//-----------------------------------------------------------------------------
static int CBasePlayer_ChatPrint (lua_State *L) {
  CBasePlayer *pPlayer = luaL_checkplayer( L, 1 );
  const char *pszMessage = luaL_checkstring( L, 2 );

  ClientPrint( pPlayer, HUD_PRINTTALK, pszMessage );
  return 0;
}

static int CBasePlayer_GiveAmmo (lua_State *L) {
  switch(lua_type(L, 3)) {
    case LUA_TNUMBER:
      lua_pushinteger(L, luaL_checkplayer(L, 1)->GiveAmmo(luaL_checkint(L, 2), luaL_checkint(L, 3), luaL_optboolean(L, 4, false)));
      break;
    case LUA_TSTRING:
      lua_pushinteger(L, luaL_checkplayer(L, 1)->GiveAmmo(luaL_checkint(L, 2), luaL_checkstring(L, 3), luaL_optboolean(L, 4, false)));
      break;
    default:
      lua_pushinteger(L, luaL_checkplayer(L, 1)->GiveAmmo(luaL_checkint(L, 2), luaL_checkint(L, 3), luaL_optboolean(L, 4, false)));
      break;
  }
  return 1;
}

static int CBasePlayer_SetBodyPitch (lua_State *L) {
  luaL_checkplayer(L, 1)->SetBodyPitch(luaL_checknumber(L, 2));
  return 0;
}

static int CBasePlayer_CreateViewModel (lua_State *L) {
  luaL_checkplayer(L, 1)->CreateViewModel(luaL_optinteger(L, 2, 0));
  return 0;
}

static int CBasePlayer_HideViewModels (lua_State *L) {
  luaL_checkplayer(L, 1)->HideViewModels();
  return 0;
}

static int CBasePlayer_DestroyViewModels (lua_State *L) {
  luaL_checkplayer(L, 1)->DestroyViewModels();
  return 0;
}

static int CBasePlayer_RequiredEdictIndex (lua_State *L) {
  lua_pushinteger(L, luaL_checkplayer(L, 1)->RequiredEdictIndex());
  return 1;
}

static int CBasePlayer_LockPlayerInPlace (lua_State *L) {
  luaL_checkplayer(L, 1)->LockPlayerInPlace();
  return 0;
}

static int CBasePlayer_UnlockPlayer (lua_State *L) {
  luaL_checkplayer(L, 1)->UnlockPlayer();
  return 0;
}

static int CBasePlayer_DrawDebugGeometryOverlays (lua_State *L) {
  luaL_checkplayer(L, 1)->DrawDebugGeometryOverlays();
  return 0;
}

static int CBasePlayer_UpdateTransmitState (lua_State *L) {
  luaL_checkplayer(L, 1)->UpdateTransmitState();
  return 0;
}

static int CBasePlayer_ForceRespawn (lua_State *L) {
  luaL_checkplayer(L, 1)->ForceRespawn();
  return 0;
}

static int CBasePlayer_InitialSpawn (lua_State *L) {
  luaL_checkplayer(L, 1)->InitialSpawn();
  return 0;
}

static int CBasePlayer_InitHUD (lua_State *L) {
  luaL_checkplayer(L, 1)->InitHUD();
  return 0;
}

static int CBasePlayer_PlayerDeathThink (lua_State *L) {
  luaL_checkplayer(L, 1)->PlayerDeathThink();
  return 0;
}

static int CBasePlayer_Jump (lua_State *L) {
  luaL_checkplayer(L, 1)->Jump();
  return 0;
}

static int CBasePlayer_Duck (lua_State *L) {
  luaL_checkplayer(L, 1)->Duck();
  return 0;
}

static int CBasePlayer_ForceSimulation (lua_State *L) {
  luaL_checkplayer(L, 1)->ForceSimulation();
  return 0;
}

static int CBasePlayer_TakeHealth (lua_State *L) {
  lua_pushinteger(L, luaL_checkplayer(L, 1)->TakeHealth(luaL_checknumber(L, 2), luaL_checkinteger(L, 3)));
  return 1;
}

static int CBasePlayer_DamageEffect (lua_State *L) {
  luaL_checkplayer(L, 1)->DamageEffect(luaL_checknumber(L, 2), luaL_checkinteger(L, 3));
  return 0;
}

static int CBasePlayer_PauseBonusProgress (lua_State *L) {
  luaL_checkplayer(L, 1)->PauseBonusProgress(luaL_optboolean(L, 2, true));
  return 0;
}

static int CBasePlayer_SetBonusProgress (lua_State *L) {
  luaL_checkplayer(L, 1)->SetBonusProgress(luaL_checkinteger(L, 2));
  return 0;
}

static int CBasePlayer_SetBonusChallenge (lua_State *L) {
  luaL_checkplayer(L, 1)->SetBonusChallenge(luaL_checkinteger(L, 2));
  return 0;
}

static int CBasePlayer_GetBonusProgress (lua_State *L) {
  lua_pushinteger(L, luaL_checkplayer(L, 1)->GetBonusProgress());
  return 1;
}

static int CBasePlayer_GetBonusChallenge (lua_State *L) {
  lua_pushinteger(L, luaL_checkplayer(L, 1)->GetBonusChallenge());
  return 1;
}

static int CBasePlayer_SnapEyeAngles (lua_State *L) {
  luaL_checkplayer(L, 1)->SnapEyeAngles(luaL_checkangle(L, 2));
  return 0;
}

static int CBasePlayer_BodyAngles (lua_State *L) {
  lua_pushangle(L, luaL_checkplayer(L, 1)->BodyAngles());
  return 1;
}

static int CBasePlayer_BodyTarget (lua_State *L) {
  lua_pushvector(L, luaL_checkplayer(L, 1)->BodyTarget(luaL_checkvector(L, 2), luaL_checkboolean(L, 3)));
  return 1;
}

static int CBasePlayer_ShouldFadeOnDeath (lua_State *L) {
  lua_pushboolean(L, luaL_checkplayer(L, 1)->ShouldFadeOnDeath());
  return 1;
}

static int CBasePlayer_OnTakeDamage_Alive (lua_State *L) {
  lua_pushinteger(L, luaL_checkplayer(L, 1)->OnTakeDamage_Alive(luaL_checkdamageinfo(L, 2)));
  return 1;
}

static int CBasePlayer_Event_Killed (lua_State *L) {
  luaL_checkplayer(L, 1)->Event_Killed(luaL_checkdamageinfo(L, 2));
  return 0;
}

static int CBasePlayer_Event_KilledOther (lua_State *L) {
  luaL_checkplayer(L, 1)->Event_KilledOther(luaL_checkentity(L, 2), luaL_checkdamageinfo(L, 3));
  return 0;
}

static int CBasePlayer_Event_Dying (lua_State *L) {
//  luaL_checkplayer(L, 1)->Event_Dying();
  return 0;
}

static int CBasePlayer_IsNetClient (lua_State *L) {
  lua_pushboolean(L, luaL_checkplayer(L, 1)->IsNetClient());
  return 1;
}

static int CBasePlayer_IsFakeClient (lua_State *L) {
  lua_pushboolean(L, luaL_checkplayer(L, 1)->IsFakeClient());
  return 1;
}

static int CBasePlayer_GetClientIndex (lua_State *L) {
  lua_pushinteger(L, luaL_checkplayer(L, 1)->GetClientIndex());
  return 1;
}

static int CBasePlayer_SetPlayerName (lua_State *L) {
  luaL_checkplayer(L, 1)->SetPlayerName(luaL_checkstring(L, 2));
  return 0;
}

static int CBasePlayer_GetNetworkIDString (lua_State *L) {
  lua_pushstring(L, luaL_checkplayer(L, 1)->GetNetworkIDString());
  return 1;
}

static int CBasePlayer_ShowViewModel (lua_State *L) {
  luaL_checkplayer(L, 1)->ShowViewModel(luaL_checkboolean(L, 2));
  return 0;
}

static int CBasePlayer_ShowCrosshair (lua_State *L) {
  luaL_checkplayer(L, 1)->ShowCrosshair(luaL_checkboolean(L, 2));
  return 0;
}

static int CBasePlayer_PackDeadPlayerItems (lua_State *L) {
  luaL_checkplayer(L, 1)->PackDeadPlayerItems();
  return 0;
}

static int CBasePlayer_RemoveAllItems (lua_State *L) {
  luaL_checkplayer(L, 1)->RemoveAllItems(luaL_checkboolean(L, 2));
  return 0;
}

static int CBasePlayer_IsDead (lua_State *L) {
  lua_pushboolean(L, luaL_checkplayer(L, 1)->IsDead());
  return 1;
}

static int CBasePlayer_HasPhysicsFlag (lua_State *L) {
  lua_pushboolean(L, luaL_checkplayer(L, 1)->HasPhysicsFlag(luaL_checkinteger(L, 2)));
  return 1;
}

static int CBasePlayer_Weapon_CanUse (lua_State *L) {
  lua_pushboolean(L, luaL_checkplayer(L, 1)->Weapon_CanUse(luaL_checkweapon(L, 2)));
  return 1;
}

static int CBasePlayer_Weapon_Equip (lua_State *L) {
  luaL_checkplayer(L, 1)->Weapon_Equip(luaL_checkweapon(L, 2));
  return 0;
}

static int CBasePlayer_Weapon_Drop (lua_State *L) {
  luaL_checkplayer(L, 1)->Weapon_Drop(luaL_checkweapon(L, 2), &luaL_optvector(L, 3, NULL), &luaL_optvector(L, 4, NULL));
  return 0;
}

static int CBasePlayer_Weapon_DropSlot (lua_State *L) {
  luaL_checkplayer(L, 1)->Weapon_DropSlot(luaL_checkinteger(L, 2));
  return 0;
}

static int CBasePlayer_Weapon_GetLast (lua_State *L) {
  lua_pushweapon(L, luaL_checkplayer(L, 1)->Weapon_GetLast());
  return 1;
}

static int CBasePlayer_HasAnyAmmoOfType (lua_State *L) {
  lua_pushboolean(L, luaL_checkplayer(L, 1)->HasAnyAmmoOfType(luaL_checkinteger(L, 2)));
  return 1;
}

static int CBasePlayer_RumbleEffect (lua_State *L) {
  luaL_checkplayer(L, 1)->RumbleEffect(luaL_checkinteger(L, 2), luaL_checkinteger(L, 3), luaL_checkinteger(L, 4));
  return 0;
}

static int CBasePlayer_IsOnLadder (lua_State *L) {
  lua_pushboolean(L, luaL_checkplayer(L, 1)->IsOnLadder());
  return 1;
}

static int CBasePlayer_SetFlashlightEnabled (lua_State *L) {
  luaL_checkplayer(L, 1)->SetFlashlightEnabled(luaL_checkboolean(L, 2));
  return 0;
}

static int CBasePlayer_FlashlightIsOn (lua_State *L) {
  lua_pushinteger(L, luaL_checkplayer(L, 1)->FlashlightIsOn());
  return 1;
}

static int CBasePlayer_FlashlightTurnOn (lua_State *L) {
  luaL_checkplayer(L, 1)->FlashlightTurnOn();
  return 0;
}

static int CBasePlayer_FlashlightTurnOff (lua_State *L) {
  luaL_checkplayer(L, 1)->FlashlightTurnOff();
  return 0;
}

static int CBasePlayer_IsIlluminatedByFlashlight (lua_State *L) {
  float flDot;
  lua_pushboolean(L, luaL_checkplayer(L, 1)->IsIlluminatedByFlashlight(luaL_checkentity(L, 2), &flDot));
  lua_pushnumber(L, flDot);
  return 2;
}

static int CBasePlayer_UpdatePlayerSound (lua_State *L) {
  luaL_checkplayer(L, 1)->UpdatePlayerSound();
  return 0;
}

static int CBasePlayer_DeathSound (lua_State *L) {
  luaL_checkplayer(L, 1)->DeathSound(luaL_checkdamageinfo(L, 2));
  return 0;
}

static int CBasePlayer_Classify (lua_State *L) {
  lua_pushinteger(L, luaL_checkplayer(L, 1)->Classify());
  return 1;
}

static int CBasePlayer_SetWeaponAnimType (lua_State *L) {
  // Huh?
  // luaL_checkplayer(L, 1)->SetWeaponAnimType(luaL_checkstring(L, 2));
  return 0;
}

static int CBasePlayer_ImpulseCommands (lua_State *L) {
  luaL_checkplayer(L, 1)->ImpulseCommands();
  return 0;
}

static int CBasePlayer_CheatImpulseCommands (lua_State *L) {
  luaL_checkplayer(L, 1)->CheatImpulseCommands(luaL_checkinteger(L, 2));
  return 0;
}

static int CBasePlayer_NotifySinglePlayerGameEnding (lua_State *L) {
  luaL_checkplayer(L, 1)->NotifySinglePlayerGameEnding();
  return 0;
}

static int CBasePlayer_IsSinglePlayerGameEnding (lua_State *L) {
  lua_pushboolean(L, luaL_checkplayer(L, 1)->IsSinglePlayerGameEnding());
  return 1;
}

static int CBasePlayer_StartObserverMode (lua_State *L) {
  lua_pushboolean(L, luaL_checkplayer(L, 1)->StartObserverMode(luaL_checkinteger(L, 2)));
  return 1;
}

static int CBasePlayer_StopObserverMode (lua_State *L) {
  luaL_checkplayer(L, 1)->StopObserverMode();
  return 0;
}

static int CBasePlayer_ModeWantsSpectatorGUI (lua_State *L) {
  lua_pushboolean(L, luaL_checkplayer(L, 1)->ModeWantsSpectatorGUI(luaL_checkinteger(L, 2)));
  return 1;
}

static int CBasePlayer_FindNextObserverTarget (lua_State *L) {
  lua_pushentity(L, luaL_checkplayer(L, 1)->FindNextObserverTarget(luaL_checkboolean(L, 2)));
  return 1;
}

static int CBasePlayer_GetNextObserverSearchStartPoint (lua_State *L) {
  lua_pushinteger(L, luaL_checkplayer(L, 1)->GetNextObserverSearchStartPoint(luaL_checkboolean(L, 2)));
  return 1;
}

static int CBasePlayer_IsValidObserverTarget (lua_State *L) {
  lua_pushboolean(L, luaL_checkplayer(L, 1)->IsValidObserverTarget(luaL_checkentity(L, 2)));
  return 1;
}

static int CBasePlayer_CheckObserverSettings (lua_State *L) {
  luaL_checkplayer(L, 1)->CheckObserverSettings();
  return 0;
}

static int CBasePlayer_JumptoPosition (lua_State *L) {
  luaL_checkplayer(L, 1)->JumptoPosition(luaL_checkvector(L, 2), luaL_checkangle(L, 3));
  return 0;
}

static int CBasePlayer_ForceObserverMode (lua_State *L) {
  luaL_checkplayer(L, 1)->ForceObserverMode(luaL_checkinteger(L, 2));
  return 0;
}

static int CBasePlayer_ValidateCurrentObserverTarget (lua_State *L) {
  luaL_checkplayer(L, 1)->ValidateCurrentObserverTarget();
  return 0;
}

static int CBasePlayer_StartReplayMode (lua_State *L) {
  lua_pushboolean(L, luaL_checkplayer(L, 1)->StartReplayMode(luaL_checknumber(L, 2), luaL_checknumber(L, 3), luaL_checkinteger(L, 4)));
  return 1;
}

static int CBasePlayer_StopReplayMode (lua_State *L) {
  luaL_checkplayer(L, 1)->StopReplayMode();
  return 0;
}

static int CBasePlayer_GetDelayTicks (lua_State *L) {
  lua_pushinteger(L, luaL_checkplayer(L, 1)->GetDelayTicks());
  return 1;
}

static int CBasePlayer_GetReplayEntity (lua_State *L) {
  lua_pushinteger(L, luaL_checkplayer(L, 1)->GetReplayEntity());
  return 1;
}

static int CBasePlayer_CreateCorpse (lua_State *L) {
  luaL_checkplayer(L, 1)->CreateCorpse();
  return 0;
}

static int CBasePlayer_EntSelectSpawnPoint (lua_State *L) {
  lua_pushentity(L, luaL_checkplayer(L, 1)->EntSelectSpawnPoint());
  return 1;
}

static int CBasePlayer_GetVehicleAnalogControlBias (lua_State *L) {
  lua_pushinteger(L, luaL_checkplayer(L, 1)->GetVehicleAnalogControlBias());
  return 1;
}

static int CBasePlayer_SetVehicleAnalogControlBias (lua_State *L) {
  luaL_checkplayer(L, 1)->SetVehicleAnalogControlBias(luaL_checkinteger(L, 2));
  return 0;
}

static int CBasePlayer_OnVehicleStart (lua_State *L) {
  luaL_checkplayer(L, 1)->OnVehicleStart();
  return 0;
}

static int CBasePlayer_GetVehicleEntity (lua_State *L) {
  lua_pushentity(L, luaL_checkplayer(L, 1)->GetVehicleEntity());
  return 1;
}

static int CBasePlayer_BumpWeapon (lua_State *L) {
  lua_pushboolean(L, luaL_checkplayer(L, 1)->BumpWeapon(luaL_checkweapon(L, 2)));
  return 1;
}

static int CBasePlayer_RemovePlayerItem (lua_State *L) {
  lua_pushboolean(L, luaL_checkplayer(L, 1)->RemovePlayerItem(luaL_checkweapon(L, 2)));
  return 1;
}

static int CBasePlayer_HasNamedPlayerItem (lua_State *L) {
  lua_pushentity(L, luaL_checkplayer(L, 1)->HasNamedPlayerItem(luaL_checkstring(L, 2)));
  return 1;
}

static int CBasePlayer_HasWeapons (lua_State *L) {
  lua_pushboolean(L, luaL_checkplayer(L, 1)->HasWeapons());
  return 1;
}

static int CBasePlayer_GiveNamedItem (lua_State *L) {
  lua_pushentity(L, luaL_checkplayer(L, 1)->GiveNamedItem(luaL_checkstring(L, 2), luaL_optinteger(L, 3, 0)));
  return 1;
}

static int CBasePlayer_EnableControl (lua_State *L) {
  luaL_checkplayer(L, 1)->EnableControl(luaL_checkboolean(L, 2));
  return 0;
}

static int CBasePlayer_CheckTrainUpdate (lua_State *L) {
  luaL_checkplayer(L, 1)->CheckTrainUpdate();
  return 0;
}

static int CBasePlayer_EquipSuit (lua_State *L) {
  luaL_checkplayer(L, 1)->EquipSuit(luaL_optboolean(L, 2, true));
  return 0;
}

static int CBasePlayer_RemoveSuit (lua_State *L) {
  luaL_checkplayer(L, 1)->RemoveSuit();
  return 0;
}


// HL2SB GMod compat: GMod's Player:LagCompensation( bool ) - "enable/disable lag
// compensation for this player's traces".  The engine already keeps exactly that
// flag (m_bLagCompensation, game/server/player.h, set from cl_lagcompensation in
// gameinterface.cpp and read by player_lagcompensation.cpp), it just had no Lua
// binding.  weapon_fists' DealDamage() opens with
//     self.Owner:LagCompensation( true )
// and threw "attempt to call a nil value (method 'LagCompensation')" 1204 times
// in one session, which aborted the melee before its TakeDamageInfo().
static int CBasePlayer_LagCompensation (lua_State *L) {
  luaL_checkplayer(L, 1)->m_bLagCompensation = luaL_checkboolean(L, 2);
  return 0;
}

// HL2SB GMod compat: Player:SetViewEntity( ent ) / Player:GetViewEntity().
// GMod wiki (Player:SetViewEntity): "Attaches the players view to the position
// and angles of the specified entity."  CBasePlayer has had the engine half all
// along (SetViewEntity/GetViewEntity, player.cpp); only the binding was missing.
// The Nuke Pack uses it for its missile camera (Owner:SetViewEntity(rocket)).
// A nil entity detaches back to the player's own view, matching GMod.
static int CBasePlayer_SetViewEntity (lua_State *L) {
  CBasePlayer *pPlayer = luaL_checkplayer( L, 1 );
  CBaseEntity *pViewEntity = lua_toentity( L, 2 );   // NULL = detach
  pPlayer->SetViewEntity( pViewEntity );
  return 0;
}

static int CBasePlayer_GetViewEntity (lua_State *L) {
  CBasePlayer *pPlayer = luaL_checkplayer( L, 1 );
  CBaseEntity *pViewEntity = pPlayer->GetViewEntity();
  if ( pViewEntity )
    lua_pushentity( L, pViewEntity );
  else
    lua_pushnil( L );
  return 1;
}

static const luaL_Reg CBasePlayermeta[] = {
  {"GiveAmmo", CBasePlayer_GiveAmmo},
  {"SetBodyPitch", CBasePlayer_SetBodyPitch},
  {"CreateViewModel", CBasePlayer_CreateViewModel},
  {"HideViewModels", CBasePlayer_HideViewModels},
  {"DestroyViewModels", CBasePlayer_DestroyViewModels},
  {"RequiredEdictIndex", CBasePlayer_RequiredEdictIndex},
  {"LockPlayerInPlace", CBasePlayer_LockPlayerInPlace},
  {"UnlockPlayer", CBasePlayer_UnlockPlayer},
  {"DrawDebugGeometryOverlays", CBasePlayer_DrawDebugGeometryOverlays},
  {"UpdateTransmitState", CBasePlayer_UpdateTransmitState},
  {"ForceRespawn", CBasePlayer_ForceRespawn},
{"LagCompensation", CBasePlayer_LagCompensation},
  {"InitialSpawn", CBasePlayer_InitialSpawn},
  {"InitHUD", CBasePlayer_InitHUD},
  {"PlayerDeathThink", CBasePlayer_PlayerDeathThink},
  {"Jump", CBasePlayer_Jump},
  {"Duck", CBasePlayer_Duck},
  {"ForceSimulation", CBasePlayer_ForceSimulation},
  {"TakeHealth", CBasePlayer_TakeHealth},
  {"DamageEffect", CBasePlayer_DamageEffect},
  {"PauseBonusProgress", CBasePlayer_PauseBonusProgress},
  {"SetBonusProgress", CBasePlayer_SetBonusProgress},
  {"SetBonusChallenge", CBasePlayer_SetBonusChallenge},
  {"GetBonusProgress", CBasePlayer_GetBonusProgress},
  {"GetBonusChallenge", CBasePlayer_GetBonusChallenge},
  {"SnapEyeAngles", CBasePlayer_SnapEyeAngles},
  {"BodyAngles", CBasePlayer_BodyAngles},
  {"BodyTarget", CBasePlayer_BodyTarget},
  {"ShouldFadeOnDeath", CBasePlayer_ShouldFadeOnDeath},
  {"OnTakeDamage_Alive", CBasePlayer_OnTakeDamage_Alive},
  {"Event_Killed", CBasePlayer_Event_Killed},
  {"Event_KilledOther", CBasePlayer_Event_KilledOther},
  {"Event_Dying", CBasePlayer_Event_Dying},
  {"IsNetClient", CBasePlayer_IsNetClient},
  {"IsFakeClient", CBasePlayer_IsFakeClient},
  {"GetClientIndex", CBasePlayer_GetClientIndex},
  {"SetPlayerName", CBasePlayer_SetPlayerName},
  {"GetNetworkIDString", CBasePlayer_GetNetworkIDString},
  {"ShowViewModel", CBasePlayer_ShowViewModel},
  {"ShowCrosshair", CBasePlayer_ShowCrosshair},
  {"PackDeadPlayerItems", CBasePlayer_PackDeadPlayerItems},
  {"RemoveAllItems", CBasePlayer_RemoveAllItems},
  {"IsDead", CBasePlayer_IsDead},
  {"HasPhysicsFlag", CBasePlayer_HasPhysicsFlag},
  {"Weapon_CanUse", CBasePlayer_Weapon_CanUse},
  {"Weapon_Equip", CBasePlayer_Weapon_Equip},
  {"Weapon_Drop", CBasePlayer_Weapon_Drop},
  {"Weapon_DropSlot", CBasePlayer_Weapon_DropSlot},
  {"Weapon_GetLast", CBasePlayer_Weapon_GetLast},
  {"HasAnyAmmoOfType", CBasePlayer_HasAnyAmmoOfType},
  {"RumbleEffect", CBasePlayer_RumbleEffect},
  {"IsOnLadder", CBasePlayer_IsOnLadder},
  {"SetFlashlightEnabled", CBasePlayer_SetFlashlightEnabled},
  {"FlashlightIsOn", CBasePlayer_FlashlightIsOn},
  {"FlashlightTurnOn", CBasePlayer_FlashlightTurnOn},
  {"FlashlightTurnOff", CBasePlayer_FlashlightTurnOff},
  {"IsIlluminatedByFlashlight", CBasePlayer_IsIlluminatedByFlashlight},
  {"UpdatePlayerSound", CBasePlayer_UpdatePlayerSound},
  {"DeathSound", CBasePlayer_DeathSound},
  {"Classify", CBasePlayer_Classify},
  {"SetWeaponAnimType", CBasePlayer_SetWeaponAnimType},
  {"ImpulseCommands", CBasePlayer_ImpulseCommands},
  {"CheatImpulseCommands", CBasePlayer_CheatImpulseCommands},
  {"NotifySinglePlayerGameEnding", CBasePlayer_NotifySinglePlayerGameEnding},
  {"IsSinglePlayerGameEnding", CBasePlayer_IsSinglePlayerGameEnding},
  {"StartObserverMode", CBasePlayer_StartObserverMode},
  {"StopObserverMode", CBasePlayer_StopObserverMode},
  {"ModeWantsSpectatorGUI", CBasePlayer_ModeWantsSpectatorGUI},
  {"FindNextObserverTarget", CBasePlayer_FindNextObserverTarget},
  {"GetNextObserverSearchStartPoint", CBasePlayer_GetNextObserverSearchStartPoint},
  {"IsValidObserverTarget", CBasePlayer_IsValidObserverTarget},
  {"CheckObserverSettings", CBasePlayer_CheckObserverSettings},
  {"JumptoPosition", CBasePlayer_JumptoPosition},
  {"ForceObserverMode", CBasePlayer_ForceObserverMode},
  {"ValidateCurrentObserverTarget", CBasePlayer_ValidateCurrentObserverTarget},
  {"StartReplayMode", CBasePlayer_StartReplayMode},
  {"StopReplayMode", CBasePlayer_StopReplayMode},
  {"GetDelayTicks", CBasePlayer_GetDelayTicks},
  {"GetReplayEntity", CBasePlayer_GetReplayEntity},
  {"CreateCorpse", CBasePlayer_CreateCorpse},
  {"EntSelectSpawnPoint", CBasePlayer_EntSelectSpawnPoint},
  {"GetVehicleAnalogControlBias", CBasePlayer_GetVehicleAnalogControlBias},
  {"SetVehicleAnalogControlBias", CBasePlayer_SetVehicleAnalogControlBias},
  {"OnVehicleStart", CBasePlayer_OnVehicleStart},
  {"GetVehicleEntity", CBasePlayer_GetVehicleEntity},
  {"BumpWeapon", CBasePlayer_BumpWeapon},
  {"RemovePlayerItem", CBasePlayer_RemovePlayerItem},
  {"HasNamedPlayerItem", CBasePlayer_HasNamedPlayerItem},
  {"HasWeapons", CBasePlayer_HasWeapons},
  {"GiveNamedItem", CBasePlayer_GiveNamedItem},
  {"EnableControl", CBasePlayer_EnableControl},
  {"CheckTrainUpdate", CBasePlayer_CheckTrainUpdate},
  {"EquipSuit", CBasePlayer_EquipSuit},
  {"RemoveSuit", CBasePlayer_RemoveSuit},
  {"UniqueID", CBasePlayer_UniqueID},
  {"ChatPrint", CBasePlayer_ChatPrint},
  {"SetViewEntity", CBasePlayer_SetViewEntity},
  {"GetViewEntity", CBasePlayer_GetViewEntity},
  {NULL, NULL}
};


/*
** Open CBasePlayer object
*/
LUALIB_API int luaopen_CBasePlayer (lua_State *L) {
  luaL_getmetatable(L, "CBasePlayer");
  if (lua_isnoneornil(L, -1)) {
    lua_pop(L, 1);
    luaL_newmetatable(L, "CBasePlayer");
  }
  luaL_register(L, NULL, CBasePlayermeta);

  // HL2SB: expose a global player.GetAll() so scripts can iterate players.
  lua_getglobal( L, "player" );
  if ( lua_isnil( L, -1 ) )
  {
    lua_pop( L, 1 );
    lua_newtable( L );
  }
  lua_pushcfunction( L, CBasePlayer_GetAll );
  lua_setfield( L, -2, "GetAll" );
  lua_setglobal( L, "player" );

  return 1;
}


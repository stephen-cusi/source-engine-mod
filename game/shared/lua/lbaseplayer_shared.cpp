//========= Copyright © 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: Implements shared baseplayer class functionality
//
// $NoKeywords: $
//=============================================================================//

#define lbaseplayer_shared_cpp

#include "cbase.h"
#include "convar.h"
#include "in_buttons.h"
// HL2SB: Player:SetAmmo resolves GMod's ammo-name string through the shared
// ammo definition (same lookup lua_tofirebulletsinfo uses for bullet.AmmoType).
#include "ammodef.h"
#ifdef CLIENT_DLL
#include "iinput.h"	// HL2SB: input->GetButtonBits (live-command fallback)
// HL2SB: raw per-frame mouse deltas from CInput::MouseMove (in_mouse.cpp)
void HL2SB_GetLastMouseDeltas( int &dx, int &dy );
class CViewSetup;
extern bool HL2SB_GetWorldClickDirection( Vector &direction, const CViewSetup *setup );
#endif
#include "luamanager.h"
#include "luasrclib.h"
#include "lbaseplayer_shared.h"
// HL2SB: Player:SteamID / SteamID64 - CSteamID is not in this file's usual include chain
// (c_baseplayer.h only forward-declares it in its method signature).
#include "steam/steamclientpublic.h"
// HL2SB: usergroup support (settings/users.txt lookup, GMod-style) reads through
// the engine filesystem so the file resolves in the mod dir.
#include "filesystem.h"
#ifdef CLIENT_DLL
#include "lc_baseanimating.h"
// HL2SB: complete IClientVehicle for CBasePlayer_GetVehicleEntity's
// GetVehicleEnt() call.  cbase.h's c_baseplayer.h only uses the type as a
// pointer, so every client file that dereferences it includes this itself.
#include "iclientvehicle.h"
// HL2SB: Player:IsSprinting reads the live C_BaseHLPlayer::m_fIsSprinting state.
#include "c_basehlplayer.h"
#include "hl2mp/c_hl2mp_player.h"
// HL2SB (2026-10-03): Player:ShouldDrawLocalPlayer reads the view entity
// through the engine's render interface.
#include "ivrenderview.h"
// HL2SB: Player:Ping reads the client copy of the player resource
// (the per-player scoreboard data the server networks).
#include "c_playerresource.h"
// HL2SB: Player:IsMuted / SetMuted answer the client's persistent voice ban
// list (voice_status / voice_banmgr), the same state GameUI's mute toggle
// drives through IGameClientExports.
#include "voice_status.h"
#else
#include "lbaseanimating.h"
// HL2SB: Player:IsSprinting reads the live CHL2_Player::m_fIsSprinting state
// (CHL2MP_Player derives from CHL2_Player).
#include "hl2_player.h"
#include "hl2mp_player.h"
// HL2SB: Player:Ping reads the server-side player resource
// (LevelInit creates the player_manager entity, see gameinterface.cpp).
#include "player_resource.h"
#endif
#include "lbasecombatweapon_shared.h"
#include "lbaseentity_shared.h"
#include "lgametrace.h"
#include "SoundEmitterSystem/lisoundemittersystembase.h"
#include "mathlib/lvector.h"
#include "lvphysics_interface.h"
#include "lColor.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

/*
** access functions (stack -> C)
*/
#ifdef CLIENT_DLL
bool HL2SB_LuaPlayerSwitchWeapon( CBasePlayer *pPlayer, CBaseCombatWeapon *pOld, CBaseCombatWeapon *pNew )
{
  if ( L == NULL || pPlayer == NULL )
    return false;

  const int top = lua_gettop( L );
  BEGIN_LUA_CALL_HOOK( "PlayerSwitchWeapon" );
    lua_pushplayer( L, pPlayer );
    if ( pOld ) lua_pushweapon( L, pOld ); else lua_pushnil( L );
    if ( pNew ) lua_pushweapon( L, pNew ); else lua_pushnil( L );
  END_LUA_CALL_HOOK( 3, 1 );
  const bool veto = lua_toboolean( L, -1 ) != 0;
  lua_settop( L, top );
  return veto;
}

void HL2SB_LuaPlayerAmmoChanged( CBasePlayer *pPlayer, int iAmmoType, int iOld, int iNew )
{
  if ( L == NULL || pPlayer == NULL || iOld == iNew )
    return;

  const int top = lua_gettop( L );
  BEGIN_LUA_CALL_HOOK( "PlayerAmmoChanged" );
    lua_pushplayer( L, pPlayer );
    lua_pushinteger( L, iAmmoType );
    lua_pushinteger( L, iOld );
    lua_pushinteger( L, iNew );
  END_LUA_CALL_HOOK( 4, 0 );
  lua_settop( L, top );
}
#endif



LUA_API lua_CBasePlayer *lua_toplayer (lua_State *L, int idx) {
  CBaseHandle *phPlayer = dynamic_cast<CBaseHandle *>((CBaseHandle *)lua_touserdata(L, idx));
  if (phPlayer == NULL)
    return NULL;
  return dynamic_cast<lua_CBasePlayer *>(phPlayer->Get());
}

// HL2SB: per-player hull / desired view-offset state lives in baseplayer_shared.cpp.
extern void HL2SB_GetPlayerHullBounds( CBasePlayer *pPlayer, bool bDucked, Vector &mins, Vector &maxs );
extern void HL2SB_SetPlayerHullBounds( CBasePlayer *pPlayer, bool bDucked, const Vector &mins, const Vector &maxs );
extern void HL2SB_ResetPlayerHullBounds( CBasePlayer *pPlayer );
extern void HL2SB_GetPlayerViewOffsetDesired( CBasePlayer *pPlayer, bool bDucked, Vector &offset );
extern void HL2SB_SetPlayerViewOffsetDesired( CBasePlayer *pPlayer, bool bDucked, const Vector &offset );
extern void HL2SB_ResetPlayerViewOffsets( CBasePlayer *pPlayer );
static void HL2SB_EnsureMoveDataMeta( lua_State *L );



/*
** push functions (C -> stack)
*/


LUA_API void lua_pushplayer (lua_State *L, CBasePlayer *pPlayer) {
  CBaseHandle *phPlayer = (CBaseHandle *)lua_newuserdata(L, sizeof(CBaseHandle));
  phPlayer->Set(pPlayer);
  luaL_getmetatable(L, "CBasePlayer");
  lua_setmetatable(L, -2);
}


LUALIB_API lua_CBasePlayer *luaL_checkplayer (lua_State *L, int narg) {
  lua_CBasePlayer *d = lua_toplayer(L, narg);
  if (d == NULL)  /* avoid extra test when d is not 0 */
    luaL_argerror(L, narg, "CBasePlayer expected, got NULL entity");
  return d;
}


LUALIB_API lua_CBasePlayer *luaL_optplayer (lua_State *L, int narg,
                                                          CBasePlayer *def) {
  return luaL_opt(L, luaL_checkplayer, narg, def);
}


static int CBasePlayer_AbortReload (lua_State *L) {
  luaL_checkplayer(L, 1)->AbortReload();
  return 0;
}

static int CBasePlayer_AddToPlayerSimulationList (lua_State *L) {
  luaL_checkplayer(L, 1)->AddToPlayerSimulationList(luaL_checkentity(L, 2));
  return 0;
}

static int CBasePlayer_CacheVehicleView (lua_State *L) {
  luaL_checkplayer(L, 1)->CacheVehicleView();
  return 0;
}

static int CBasePlayer_ClearPlayerSimulationList (lua_State *L) {
  luaL_checkplayer(L, 1)->ClearPlayerSimulationList();
  return 0;
}

static int CBasePlayer_ClearZoomOwner (lua_State *L) {
  luaL_checkplayer(L, 1)->ClearZoomOwner();
  return 0;
}

static int CBasePlayer_CurrentCommandNumber (lua_State *L) {
  lua_pushinteger(L, luaL_checkplayer(L, 1)->CurrentCommandNumber());
  return 1;
}

//-----------------------------------------------------------------------------
// HL2SB GMod compat: Player:GetCurrentCommand().
//
// gmod_camera's SWEP:Tick() reads the player's live command every frame
// (lua/weapons/gmod_camera/shared.lua:117-122):
//     local cmd = owner:GetCurrentCommand()
//     if ( !cmd:KeyDown( IN_ATTACK2 ) ) then return end
//     ... cmd:GetMouseY() ... cmd:GetMouseX() ...
// and this engine had no binding for it at all, so that Tick() threw every frame
// and the camera could be held but never zoomed or rolled.
//
// The engine only exposes the command as a const pointer (GetCurrentUserCommand,
// valid while that player's command is being processed), so this hands back a
// snapshot table with GMod's field names and the method spellings scripts use.
//-----------------------------------------------------------------------------
static int CBasePlayer_CmdKeyDown (lua_State *L) {
  lua_getfield(L, 1, "buttons");
  const int nButtons = lua_tointeger(L, -1);
  lua_pop(L, 1);

  lua_pushboolean(L, (nButtons & (int)luaL_checknumber(L, 2)) != 0);
  return 1;
}

// HL2SB (2026-10-16) GMod compat: CUserCmd:GetMouseWheel().
//
// Reference behavior (GMod win64): both realms answer the command's
// signed mouse-wheel step.  The drive camera zoom reads it every frame
// (lua/drive/drive_sandbox.lua:61 `cmd:GetMouseWheel() * -0.5`).  This engine's
// CUserCmd carries m_mouseWheel (usercmd.h:159, a short), which the packet
// already ships; the table snapshot just publishes the field here.
static int CBasePlayer_CmdGetMouseWheel (lua_State *L) {
  lua_getfield(L, 1, "mousewheel");
  return 1;
}

static int CBasePlayer_CmdGetMouseX (lua_State *L) { lua_getfield(L, 1, "mousedx"); return 1; }
static int CBasePlayer_CmdGetMouseY (lua_State *L) { lua_getfield(L, 1, "mousedy"); return 1; }
static int CBasePlayer_CmdGetButtons (lua_State *L) { lua_getfield(L, 1, "buttons"); return 1; }
static int CBasePlayer_CmdGetImpulse (lua_State *L) { lua_getfield(L, 1, "impulse"); return 1; }
static int CBasePlayer_CmdGetViewAngles (lua_State *L) {
  lua_getfield(L, 1, "viewangles");
  const QAngle angles = luaL_checkangle(L, -1);
  lua_pop(L, 1);
  lua_pushangle(L, angles);
  return 1;
}
static int CBasePlayer_CmdGetForwardMove (lua_State *L) { lua_getfield(L, 1, "forwardmove"); return 1; }
static int CBasePlayer_CmdGetSideMove (lua_State *L) { lua_getfield(L, 1, "sidemove"); return 1; }
static int CBasePlayer_CmdGetUpMove (lua_State *L) { lua_getfield(L, 1, "upmove"); return 1; }

static int CBasePlayer_CmdSetViewAngles (lua_State *L) {
  const QAngle angles = luaL_checkangle(L, 2);
  lua_pushangle(L, angles);
  lua_setfield(L, 1, "viewangles");
  return 0;
}

// HL2SB (2026-09-27): the write-side GMod methods the taunt camera needs
// (taunt_camera.lua's CAM.CreateMove calls cmd:ClearButtons/ClearMovement) plus
// the symmetric Set* family.
static int CBasePlayer_CmdClearButtons (lua_State *L) {
  lua_pushinteger(L, 0); lua_setfield(L, 1, "buttons");
  return 0;
}

static int CBasePlayer_CmdClearMovement (lua_State *L) {
  lua_pushnumber(L, 0); lua_setfield(L, 1, "forwardmove");
  lua_pushnumber(L, 0); lua_setfield(L, 1, "sidemove");
  lua_pushnumber(L, 0); lua_setfield(L, 1, "upmove");
  return 0;
}

static int CBasePlayer_CmdSetButtons (lua_State *L) {
  lua_setfield(L, 1, "buttons");		// command table, bitfield
  return 0;
}

static int CBasePlayer_CmdSetImpulse (lua_State *L) {
  lua_setfield(L, 1, "impulse");
  return 0;
}

static int CBasePlayer_CmdSetForwardMove (lua_State *L) {
  lua_setfield(L, 1, "forwardmove");
  return 0;
}

static int CBasePlayer_CmdSetSideMove (lua_State *L) {
  lua_setfield(L, 1, "sidemove");
  return 0;
}

static int CBasePlayer_CmdSetUpMove (lua_State *L) {
  lua_setfield(L, 1, "upmove");
  return 0;
}

static int CBasePlayer_CmdAddKey (lua_State *L) {
  lua_getfield(L, 1, "buttons");
  const int nButtons = lua_tointeger(L, -1);
  lua_pop(L, 1);
  lua_pushinteger(L, nButtons | (int)luaL_checknumber(L, 2));
  lua_setfield(L, 1, "buttons");
  return 0;
}

static int CBasePlayer_CmdCommandNumber (lua_State *L) {
  lua_getfield(L, 1, "command_number");
  return 1;
}

static const luaL_Reg HL2SB_UserCmdMeta[] = {
  {"AddKey",             CBasePlayer_CmdAddKey},
  {"CommandNumber",      CBasePlayer_CmdCommandNumber},
  {"KeyDown",            CBasePlayer_CmdKeyDown},
  {"GetMouseX",          CBasePlayer_CmdGetMouseX},
  {"GetMouseY",          CBasePlayer_CmdGetMouseY},
  {"GetMouseWheel",      CBasePlayer_CmdGetMouseWheel},
  {"GetButtons",         CBasePlayer_CmdGetButtons},
  {"GetImpulse",         CBasePlayer_CmdGetImpulse},
  {"GetViewAngles",      CBasePlayer_CmdGetViewAngles},
  {"SetViewAngles",      CBasePlayer_CmdSetViewAngles},
  {"GetForwardMove",     CBasePlayer_CmdGetForwardMove},
  {"GetSideMove",        CBasePlayer_CmdGetSideMove},
  {"GetUpMove",          CBasePlayer_CmdGetUpMove},
  {"ClearButtons",       CBasePlayer_CmdClearButtons},
  {"ClearMovement",      CBasePlayer_CmdClearMovement},
  {"SetButtons",         CBasePlayer_CmdSetButtons},
  {"SetImpulse",         CBasePlayer_CmdSetImpulse},
  {"SetForwardMove",     CBasePlayer_CmdSetForwardMove},
  {"SetSideMove",        CBasePlayer_CmdSetSideMove},
  {"SetUpMove",          CBasePlayer_CmdSetUpMove},
  {NULL, NULL}
};

static void HL2SB_EnsureUserCmdMeta (lua_State *L) {
  luaL_getmetatable(L, "CUserCmd");
  if (lua_isnil(L, -1)) {
    lua_pop(L, 1);
    luaL_newmetatable(L, "CUserCmd");
    luaL_register(L, NULL, HL2SB_UserCmdMeta);
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");
    lua_pushstring(L, "usercmd");
    lua_setfield(L, -2, "__type");
  }
  /* leave the metatable on top: lua_setmetatable() consumes it */
}

// HL2SB (2026-09-27): build the GMod-shaped CUserCmd Lua table.  Shared by
// Player:GetCurrentCommand() and the GM:CreateMove dispatch (in_main.cpp).
// The table is a SNAPSHOT of the command -- callers that hand it to Lua copy
// the writable fields (viewangles/buttons/forwardmove/sidemove/upmove) back
// into the real CUserCmd afterwards; ClearButtons/ClearMovement/Set* only
// mutate the table, so they work through that same copy-back.
void HL2SB_PushUserCmdTable (lua_State *L, const CUserCmd *pCmd) {
  lua_newtable(L);

  lua_pushinteger(L, pCmd->command_number); lua_setfield(L, -2, "command_number");
  lua_pushinteger(L, pCmd->tick_count);     lua_setfield(L, -2, "tick_count");
  lua_pushinteger(L, pCmd->buttons);        lua_setfield(L, -2, "buttons");
  lua_pushinteger(L, pCmd->impulse);        lua_setfield(L, -2, "impulse");
  lua_pushinteger(L, pCmd->weaponselect);   lua_setfield(L, -2, "weaponselect");
  lua_pushinteger(L, pCmd->mousedx);        lua_setfield(L, -2, "mousedx");
  lua_pushinteger(L, pCmd->mousedy);        lua_setfield(L, -2, "mousedy");
  lua_pushinteger(L, pCmd->m_mouseWheel);   lua_setfield(L, -2, "mousewheel");
  lua_pushnumber(L, pCmd->forwardmove);     lua_setfield(L, -2, "forwardmove");
  lua_pushnumber(L, pCmd->sidemove);        lua_setfield(L, -2, "sidemove");
  lua_pushnumber(L, pCmd->upmove);          lua_setfield(L, -2, "upmove");
  lua_pushangle(L, pCmd->viewangles);       lua_setfield(L, -2, "viewangles");

  lua_pushcfunction(L, CBasePlayer_CmdKeyDown);        lua_setfield(L, -2, "KeyDown");
  lua_pushcfunction(L, CBasePlayer_CmdGetMouseX);      lua_setfield(L, -2, "GetMouseX");
  lua_pushcfunction(L, CBasePlayer_CmdGetMouseY);      lua_setfield(L, -2, "GetMouseY");
  lua_pushcfunction(L, CBasePlayer_CmdGetMouseWheel);  lua_setfield(L, -2, "GetMouseWheel");
  lua_pushcfunction(L, CBasePlayer_CmdGetButtons);     lua_setfield(L, -2, "GetButtons");
  lua_pushcfunction(L, CBasePlayer_CmdGetImpulse);     lua_setfield(L, -2, "GetImpulse");
  lua_pushcfunction(L, CBasePlayer_CmdGetViewAngles);  lua_setfield(L, -2, "GetViewAngles");
  lua_pushcfunction(L, CBasePlayer_CmdSetViewAngles);  lua_setfield(L, -2, "SetViewAngles");
  lua_pushcfunction(L, CBasePlayer_CmdGetForwardMove); lua_setfield(L, -2, "GetForwardMove");
  lua_pushcfunction(L, CBasePlayer_CmdGetSideMove);    lua_setfield(L, -2, "GetSideMove");
  lua_pushcfunction(L, CBasePlayer_CmdGetUpMove);      lua_setfield(L, -2, "GetUpMove");
  lua_pushcfunction(L, CBasePlayer_CmdClearButtons);   lua_setfield(L, -2, "ClearButtons");
  lua_pushcfunction(L, CBasePlayer_CmdClearMovement);  lua_setfield(L, -2, "ClearMovement");
  lua_pushcfunction(L, CBasePlayer_CmdSetButtons);     lua_setfield(L, -2, "SetButtons");
  lua_pushcfunction(L, CBasePlayer_CmdSetImpulse);     lua_setfield(L, -2, "SetImpulse");
  lua_pushcfunction(L, CBasePlayer_CmdSetForwardMove); lua_setfield(L, -2, "SetForwardMove");
  lua_pushcfunction(L, CBasePlayer_CmdSetSideMove);    lua_setfield(L, -2, "SetSideMove");
  lua_pushcfunction(L, CBasePlayer_CmdSetUpMove);      lua_setfield(L, -2, "SetUpMove");
  lua_pushcfunction(L, CBasePlayer_CmdAddKey);         lua_setfield(L, -2, "AddKey");
  lua_pushcfunction(L, CBasePlayer_CmdCommandNumber);  lua_setfield(L, -2, "CommandNumber");

  HL2SB_EnsureUserCmdMeta(L);
  lua_setmetatable(L, -2);
}

static int CBasePlayer_GetCurrentCommand (lua_State *L) {
  CBasePlayer *pPlayer = luaL_checkplayer(L, 1);
#ifdef CLIENT_DLL
  const CUserCmd *activeCommand = pPlayer->m_pCurrentCommand;
  if ( activeCommand != NULL )
  {
    HL2SB_PushUserCmdTable(L, activeCommand);
    return 1;
  }
  luasrc_LuaErrorMsgF( "Player:GetCurrentCommand called outside of command/prediction\n" );
  lua_pushnil(L);
  return 1;
#else
  const CUserCmd *pCmd = pPlayer->GetCurrentUserCommand();
  if (pCmd == NULL) {
    luasrc_LuaErrorMsgF( "Player:GetCurrentCommand called outside of command/prediction\n" );
    lua_pushnil(L);
    return 1;
  }
  HL2SB_PushUserCmdTable(L, pCmd);
  return 1;
#endif
}

static int CBasePlayer_DoMuzzleFlash (lua_State *L) {
  luaL_checkplayer(L, 1)->DoMuzzleFlash();
  return 0;
}

// HL2SB GMod SWEP compat: GMod SWEPs call owner:MuzzleFlash().
static int CBasePlayer_MuzzleFlash (lua_State *L) {
  luaL_checkplayer(L, 1)->DoMuzzleFlash();
  return 0;
}

static int CBasePlayer_ExitLadder (lua_State *L) {
  luaL_checkplayer(L, 1)->ExitLadder();
  return 0;
}

static int CBasePlayer_EyeAngles (lua_State *L) {
  QAngle v = luaL_checkplayer(L, 1)->EyeAngles();
  lua_pushangle(L, v);
  return 1;
}

static int CBasePlayer_EyePosition (lua_State *L) {
  Vector v = luaL_checkplayer(L, 1)->EyePosition();
  lua_pushvector(L, v);
  return 1;
}

static int CBasePlayer_EyePositionAndVectors (lua_State *L) {
  luaL_checkplayer(L, 1)->EyePositionAndVectors(&luaL_checkvector(L, 2), &luaL_checkvector(L, 3), &luaL_checkvector(L, 4), &luaL_checkvector(L, 5));
  return 0;
}

static int CBasePlayer_EyeVectors (lua_State *L) {
  luaL_checkplayer(L, 1)->EyeVectors(&luaL_checkvector(L, 2), &luaL_optvector(L, 3, NULL), &luaL_optvector(L, 4, NULL));
  return 0;
}

// HL2SB GMod SWEP compat: selffire bullet origin (GMod Owner:GetShootPos()).
static int CBasePlayer_GetShootPos (lua_State *L) {
  Vector v = luaL_checkplayer(L, 1)->Weapon_ShootPosition();
  lua_pushvector(L, v);
  return 1;
}

// HL2SB GMod SWEP compat: aim direction (GMod Owner:GetAimVector()).
static int CBasePlayer_GetAimVector (lua_State *L) {
  CBasePlayer *player = luaL_checkplayer(L, 1);
  Vector v;
#ifdef CLIENT_DLL
  if (player != C_BasePlayer::GetLocalPlayer() || !HL2SB_GetWorldClickDirection(v, NULL))
#endif
    player->EyeVectors(&v);
  lua_pushvector(L, v);
  return 1;
}

// HL2SB GMod compat: Player:Name() and Player:GetVehicle().
//
// Both are plain GMod spellings of engine calls that already exist
// (GetPlayerName / GetVehicleEntity), and stock addons use them constantly: the
// windgrin_npc nextbot prints "nav_generate requested by ..c:Name()".
// Silent death changes player state directly, including while damage is disabled.
static int CBasePlayer_KillSilent(lua_State *L) {
#ifndef CLIENT_DLL
  luaL_checkplayer(L, 1)->LuaKillSilent();
#endif
  return 0;
}

static int CBasePlayer_Name (lua_State *L) {
  lua_pushstring(L, luaL_checkplayer(L, 1)->GetPlayerName());
  return 1;
}

// The explicit kill method bypasses damage filters and runs ordinary death events.
static int CBasePlayer_Kill(lua_State *L) {
#ifndef CLIENT_DLL
  CBasePlayer *player = luaL_checkplayer(L, 1);
  player->SetHealth(0);
  CTakeDamageInfo info(player, player, 0.0f, DMG_NEVERGIB);
  player->Event_Killed(info);
  player->Event_Dying(info);
#endif
  return 0;
}

static int CBasePlayer_SetTeam (lua_State *L) {
#ifndef CLIENT_DLL
  CBasePlayer *pPlayer = luaL_checkplayer(L, 1);
  const int iTeam = (int)luaL_checknumber(L, 2);
  // GMod raises PlayerChangedTeam before writing the networked team number.
  // Lua teams do not require an engine team-manager entry.
  if ( gpGlobals->maxClients > 1 && ( iTeam < -32768 || iTeam > 32767 ) )
    Warning( "Out of bounds value %i for networking in range %i to %i\n", iTeam, -32768, 32767 );
  if ( pPlayer->GetTeamNumber() == iTeam )
    return 0;
  const int iTop = lua_gettop( L );
  BEGIN_LUA_CALL_HOOK( "PlayerChangedTeam" );
    lua_pushplayer( L, pPlayer );
    lua_pushinteger( L, pPlayer->GetTeamNumber() );
    lua_pushinteger( L, iTeam );
  END_LUA_CALL_HOOK( 3, 0 );
  lua_settop( L, iTop );
  pPlayer->CBaseEntity::ChangeTeam( iTeam );
#else
  (void)L;
#endif

  return 0;
}

static int CBasePlayer_GetVehicle (lua_State *L) {
  // GetVehicleEntity() exists on both realms (server player.h:1318, client
  // c_baseplayer.h:316) and answers the vehicle ENTITY GMod hands back.
  lua_pushentity(L, luaL_checkplayer(L, 1)->GetVehicleEntity());
  return 1;
}

// HL2SB GMod SWEP compat: GMod Owner:GetEyeTrace() -> trace table.
// Mirrors util.TraceLine from the eye along the aim vector a long distance.
// Mask: GMod's Lua GetEyeTrace/GetEyeTraceNoCursor traces through
// util.GetPlayerTrace, whose table carries no "mask" key, so the engine
// default MASK_SOLID applies (reference behaviour, not MASK_SHOT).
static int CBasePlayer_EyeTraceInternal (lua_State *L, bool cursor) {
  CBasePlayer *pPlayer = luaL_checkplayer(L, 1);
  Vector vForward;
  pPlayer->EyeVectors(&vForward, NULL, NULL);
#ifdef CLIENT_DLL
  if (cursor && pPlayer == C_BasePlayer::GetLocalPlayer())
    HL2SB_GetWorldClickDirection(vForward, NULL);
#endif
  Vector vecEye = pPlayer->EyePosition();
  Vector vecEnd = vecEye + vForward * MAX_TRACE_LENGTH;

  trace_t tr;
  UTIL_TraceLine(vecEye, vecEnd, MASK_SOLID, pPlayer, COLLISION_GROUP_NONE, &tr);
  lua_pushtrace(L, tr);
  return 1;
}

static int CBasePlayer_GetEyeTrace (lua_State *L) {
  return CBasePlayer_EyeTraceInternal(L, true);
}

// HL2SB GMod compat: Player:GetEyeTraceNoCursor() (wiki: shared; the cursor is
// a clientside-only concept, so serverside GMod's version traces the same eye
// ray).  scp173 reads victim:GetEyeTraceNoCursor().Normal every think to build
// its vision cone; with the method nil the think coroutine errored each tick
// and the statue froze mid-game.
static int CBasePlayer_GetEyeTraceNoCursor (lua_State *L) {
  return CBasePlayer_EyeTraceInternal( L, false );
}

static int CBasePlayer_FindUseEntity (lua_State *L) {
  CBaseEntity *pUseEntity = luaL_checkplayer(L, 1)->FindUseEntity();
  lua_pushentity(L, pUseEntity);
  return 1;
}

static int CBasePlayer_GetActiveWeapon (lua_State *L) {
  CBaseCombatWeapon *pWeapon = luaL_checkplayer(L, 1)->GetActiveWeapon();
  lua_pushweapon(L, pWeapon);
  return 1;
}

// HL2SB GMod compat (2026-10-08): Player:GetWeapons() -> table (wiki, both
// realms).  GMod answers with the carried weapons as a 1..n sequence -- the
// m_hMyWeapons inventory array in slot order, NULL slots skipped.  hl1sweps'
// GetNextBestWeapon chain walks it with ipairs; the missing binding raised
// "attempt to call a nil value (method 'GetWeapons')" on every switch attempt.
static int CBasePlayer_GetWeapons (lua_State *L) {
  lua_CBasePlayer *pPlayer = luaL_checkplayer(L, 1);
  lua_newtable(L);
  int iOut = 0;
  for ( int i = 0; i < MAX_WEAPONS; i++ ) {
    CBaseCombatWeapon *pWeapon = pPlayer->GetWeapon( i );
    if ( pWeapon == NULL )
      continue;
    lua_pushinteger( L, ++iOut );
    lua_pushweapon( L, pWeapon );
    lua_settable( L, -3 );
  }
  return 1;
}

// FIXME: move to CBaseCombatCharacter
static int CBasePlayer_GetRagdollEntity(lua_State *L) {
  CBasePlayer *player = luaL_checkplayer(L, 1);
#ifdef CLIENT_DLL
  C_HL2MP_Player *mp = dynamic_cast<C_HL2MP_Player *>(player);
  lua_pushentity(L, mp ? mp->GetRagdollEntity() : NULL);
#else
  extern CBaseEntity *HL2SB_GetPlayerRagdollEntity(CBasePlayer *player);
  lua_pushentity(L, HL2SB_GetPlayerRagdollEntity(player));
#endif
  return 1;
}

static int CBasePlayer_GetAmmo(lua_State *L) {
  CBasePlayer *player = luaL_checkplayer(L, 1);
  lua_newtable(L);
  for (int id = 0; id < MAX_AMMO_SLOTS; ++id) {
    const int count = player->GetAmmoCount(id);
    if (count > 0) {
      lua_pushinteger(L, count);
      lua_rawseti(L, -2, id);
    }
  }
  return 1;
}

static int CBasePlayer_GetAmmoCount (lua_State *L) {
  switch(lua_type(L, 2)) {
	case LUA_TNUMBER:
	default:
      lua_pushinteger(L, luaL_checkplayer(L, 1)->GetAmmoCount(luaL_checkint(L, 2)));
	  break;
	case LUA_TSTRING:
      lua_pushinteger(L, luaL_checkplayer(L, 1)->GetAmmoCount((char *)luaL_checkstring(L, 2)));
	  break;
  }
  return 1;
}

static int CBasePlayer_GetAutoaimVector (lua_State *L) {
  Vector v = luaL_checkplayer(L, 1)->GetAutoaimVector(luaL_checknumber(L, 2));
  lua_pushvector(L, v);
  return 1;
}

static int CBasePlayer_GetBonusChallenge (lua_State *L) {
  lua_pushinteger(L, luaL_checkplayer(L, 1)->GetBonusChallenge());
  return 1;
}

static int CBasePlayer_GetBonusProgress (lua_State *L) {
  lua_pushinteger(L, luaL_checkplayer(L, 1)->GetBonusProgress());
  return 1;
}

static int CBasePlayer_GetDeathTime (lua_State *L) {
  lua_pushnumber(L, luaL_checkplayer(L, 1)->GetDeathTime());
  return 1;
}

static int CBasePlayer_GetDefaultFOV (lua_State *L) {
  lua_pushinteger(L, luaL_checkplayer(L, 1)->GetDefaultFOV());
  return 1;
}

  static int CBasePlayer_GetEFNoInterpParity (lua_State *L) {
  //lua_pushinteger(L, luaL_checkplayer(L, 1)->GetEFNoInterpParity());
  return 1;
}

static int CBasePlayer_GetFOV (lua_State *L) {
  lua_pushnumber(L, luaL_checkplayer(L, 1)->GetFOV());
  return 1;
}

static int CBasePlayer_GetFOVDistanceAdjustFactor (lua_State *L) {
  lua_pushnumber(L, luaL_checkplayer(L, 1)->GetFOVDistanceAdjustFactor());
  return 1;
}

static int CBasePlayer_GetFOVTime (lua_State *L) {
  lua_pushnumber(L, luaL_checkplayer(L, 1)->GetFOVTime());
  return 1;
}

static int CBasePlayer_GetHealth (lua_State *L) {
  lua_pushinteger(L, luaL_checkplayer(L, 1)->GetHealth());
  return 1;
}

// HL2SB (2026-10-02) GMod compat: Player:Armor() / GetArmor() / SetArmor() /
// GetMaxArmor() / SetMaxArmor() -- the shipped tarkov_hud addon reads these
// every HUDPaint frame (ply:Armor(), ply:GetMaxArmor()), and the nil methods
// took the whole HUD hook down with them.
// The engine value is CBasePlayer::m_ArmorValue, networked through
// DT_BasePlayer in the same change (server SendProp + client recv member).
// GetMaxArmor answers 100: this fork has no per-player max-armor concept (the
// battery caps at MAX_NORMAL_BATTERY == 100) and 100 is GMod's documented
// default, so the HUD's armor ratio stays sane.  SetMaxArmor exists for
// signature parity only.
static int CBasePlayer_Armor (lua_State *L) {
  lua_pushinteger(L, luaL_checkplayer(L, 1)->ArmorValue());
  return 1;
}

static int CBasePlayer_SetArmor (lua_State *L) {
  luaL_checkplayer(L, 1)->SetArmorValue(luaL_checkint(L, 2));
  return 0;
}

static int CBasePlayer_GetMaxArmor (lua_State *L) {
  luaL_checkplayer(L, 1);
  lua_pushinteger(L, 100);
  return 1;
}

static int CBasePlayer_SetMaxArmor (lua_State *L) {
  luaL_checkplayer(L, 1);
  luaL_checkint(L, 2);
  return 0;
}

static int CBasePlayer_GetImpulse (lua_State *L) {
  lua_pushinteger(L, luaL_checkplayer(L, 1)->GetImpulse());
  return 1;
}

static int CBasePlayer_GetLaggedMovementValue (lua_State *L) {
  lua_pushnumber(L, luaL_checkplayer(L, 1)->GetLaggedMovementValue());
  return 1;
}

static int CBasePlayer_GetLastKnownPlaceName (lua_State *L) {
  lua_pushstring(L, luaL_checkplayer(L, 1)->GetLastKnownPlaceName());
  return 1;
}

static int CBasePlayer_GetNextAttack (lua_State *L) {
  lua_pushnumber(L, luaL_checkplayer(L, 1)->GetNextAttack());
  return 1;
}

static int CBasePlayer_GetObserverMode (lua_State *L) {
  lua_pushinteger(L, luaL_checkplayer(L, 1)->GetObserverMode());
  return 1;
}

// HL2SB (2026-10-16) GMod compat: Player:SetObserverMode( mode ).
//
// Reference behavior (current GMod server build, archived locally): the
// binding is a PURE networked int write -- player-gated, lenient numeric arg,
// modes outside [0, 7) silently dropped, m_iObserverMode written only on
// change.  It does NOT run this engine's CBasePlayer::SetObserverMode cascade
// (no SetFOV/SetViewOffset/SetMoveType(MOVETYPE_OBSERVER)/SetObserverTarget).
// That matters for the drive system: lua/drive/drive_sandbox.lua sets
// OBS_MODE_CHASE EVERY StartMove while driving -- routed through the cascade
// the player becomes MOVETYPE_OBSERVER and the movement code runs down the
// observer path, which wedges the drive exit (the E-key KeyReleased(IN_USE)
// check in StartMove stops seeing usable commands; death and re-drive cannot
// restore either).  Through the reference contract the mode int just rides
// the network to the client view code while the player keeps moving normally.
static int CBasePlayer_SetObserverMode (lua_State *L) {
  CBasePlayer *pPlayer = luaL_checkplayer(L, 1);
  const int nMode = (int)luaL_checknumber(L, 2);

  if (nMode >= OBS_MODE_NONE && nMode < NUM_OBSERVER_MODES) {
#ifndef CLIENT_DLL
    pPlayer->HL2SB_SetObserverModeRaw(nMode);
#else
    pPlayer->SetObserverMode(nMode);
#endif
  }
  return 0;
}

#ifndef CLIENT_DLL
static int CBasePlayer_Spectate(lua_State *L) {
  luaL_checkplayer(L, 1)->LuaSpectate((int)luaL_checknumber(L, 2));
  return 0;
}

static int CBasePlayer_SpectateEntity(lua_State *L) {
  CBasePlayer *player = luaL_checkplayer(L, 1);
  player->LuaSpectateEntity(lua_toentity(L, 2));
  return 0;
}

static int CBasePlayer_UnSpectate(lua_State *L) {
  luaL_checkplayer(L, 1)->LuaUnSpectate();
  return 0;
}

static int CBasePlayer_ExitVehicle(lua_State *L) {
  luaL_checkplayer(L, 1)->ExitVehicle();
  return 0;
}
#endif

static int CBasePlayer_GetObserverTarget (lua_State *L) {
  lua_pushentity(L, luaL_checkplayer(L, 1)->GetObserverTarget());
  return 1;
}

static int CBasePlayer_GetOffset_m_Local (lua_State *L) {
  lua_pushinteger(L, CBasePlayer::GetOffset_m_Local());
  return 1;
}

static int CBasePlayer_GetPlayerLocalData (lua_State *L) {
  CBasePlayer *pPlayer = luaL_checkplayer(L, 1);
  lua_newtable(L);
  lua_pushinteger(L, pPlayer->m_Local.m_iHideHUD);
  lua_setfield(L, -2, "m_iHideHUD");

  lua_pushnumber(L, pPlayer->m_Local.m_flFOVRate);
  lua_setfield(L, -2, "m_flFOVRate");

  lua_pushboolean(L, pPlayer->m_Local.m_bDucked);
  lua_setfield(L, -2, "m_bDucked");
  lua_pushboolean(L, pPlayer->m_Local.m_bDucking);
  lua_setfield(L, -2, "m_bDucking");
  lua_pushboolean(L, pPlayer->m_Local.m_bInDuckJump);
  lua_setfield(L, -2, "m_bInDuckJump");
  lua_pushnumber(L, pPlayer->m_Local.m_flDucktime);
  lua_setfield(L, -2, "m_flDucktime");
  lua_pushnumber(L, pPlayer->m_Local.m_flDuckJumpTime);
  lua_setfield(L, -2, "m_flDuckJumpTime");
  lua_pushnumber(L, pPlayer->m_Local.m_flJumpTime);
  lua_setfield(L, -2, "m_flJumpTime");
  lua_pushinteger(L, pPlayer->m_Local.m_nStepside);
  lua_setfield(L, -2, "m_nStepside");
  lua_pushnumber(L, pPlayer->m_Local.m_flFallVelocity);
  lua_setfield(L, -2, "m_flFallVelocity");
  lua_pushinteger(L, pPlayer->m_Local.m_nOldButtons);
  lua_setfield(L, -2, "m_nOldButtons");

#ifdef CLIENT_DLL
  lua_pushvector(L, pPlayer->m_Local.m_vecClientBaseVelocity);
  lua_setfield(L, -2, "m_vecClientBaseVelocity");
#endif
  QAngle v = pPlayer->m_Local.m_vecPunchAngle;
  lua_pushangle(L, v);
  lua_setfield(L, -2, "m_vecPunchAngle");

  v = pPlayer->m_Local.m_vecPunchAngleVel;
  lua_pushangle(L, v);
  lua_setfield(L, -2, "m_vecPunchAngleVel");
  lua_pushboolean(L, pPlayer->m_Local.m_bDrawViewmodel);
  lua_setfield(L, -2, "m_bDrawViewmodel");
  lua_pushboolean(L, pPlayer->m_Local.m_bWearingSuit);
  lua_setfield(L, -2, "m_bWearingSuit");
  lua_pushboolean(L, pPlayer->m_Local.m_bPoisoned);
  lua_setfield(L, -2, "m_bPoisoned");
  lua_pushnumber(L, pPlayer->m_Local.m_flStepSize);
  lua_setfield(L, -2, "m_flStepSize");
  lua_pushboolean(L, pPlayer->m_Local.m_bAllowAutoMovement);
  lua_setfield(L, -2, "m_bAllowAutoMovement");

  lua_pushboolean(L, pPlayer->m_Local.m_bSlowMovement);
  lua_setfield(L, -2, "m_bSlowMovement");
  return 1;
}

static int CBasePlayer_GetPlayerMaxs (lua_State *L) {
  Vector v = luaL_checkplayer(L, 1)->GetPlayerMaxs();
  lua_pushvector(L, v);
  return 1;
}

static int CBasePlayer_GetPlayerMins (lua_State *L) {
  Vector v = luaL_checkplayer(L, 1)->GetPlayerMins();
  lua_pushvector(L, v);
  return 1;
}

static int CBasePlayer_GetPlayerName (lua_State *L) {
  lua_pushstring(L, luaL_checkplayer(L, 1)->GetPlayerName());
  return 1;
}

static int CBasePlayer_GetPreviouslyPredictedOrigin (lua_State *L) {
  Vector v = luaL_checkplayer(L, 1)->GetPreviouslyPredictedOrigin();
  lua_pushvector(L, v);
  return 1;
}

static int CBasePlayer_GetPunchAngle (lua_State *L) {
  QAngle v = luaL_checkplayer(L, 1)->GetPunchAngle();
  lua_pushangle(L, v);
  return 1;
}

static int CBasePlayer_GetStepSoundCache (lua_State *L) {
  CBasePlayer *pPlayer = luaL_checkplayer(L, 1);
  lua_newtable(L);
  lua_pushinteger(L, 0);
  lua_newtable(L);
  lua_pushsoundparameters(L, pPlayer->m_StepSoundCache[ 0 ].m_SoundParameters);
  lua_setfield(L, -2, "m_SoundParameters");
  lua_pushinteger(L, pPlayer->m_StepSoundCache[ 0 ].m_usSoundNameIndex);
  lua_setfield(L, -2, "m_usSoundNameIndex");
  lua_settable(L, -3);
  lua_pushinteger(L, 1);
  lua_newtable(L);
  lua_pushsoundparameters(L, pPlayer->m_StepSoundCache[ 1 ].m_SoundParameters);
  lua_setfield(L, -2, "m_SoundParameters");
  lua_pushinteger(L, pPlayer->m_StepSoundCache[ 1 ].m_usSoundNameIndex);
  lua_setfield(L, -2, "m_usSoundNameIndex");
  lua_settable(L, -3);
  return 1;
}

static int CBasePlayer_GetStepSoundVelocities (lua_State *L) {
  float velwalk, velrun;
  luaL_checkplayer(L, 1)->GetStepSoundVelocities(&velwalk, &velrun);
  lua_pushnumber(L, velwalk);
  lua_pushnumber(L, velrun);
  return 2;
}

static int CBasePlayer_GetSwimSoundTime (lua_State *L) {
  lua_pushnumber(L, luaL_checkplayer(L, 1)->GetSwimSoundTime());
  return 1;
}

static int CBasePlayer_GetTimeBase (lua_State *L) {
  lua_pushnumber(L, luaL_checkplayer(L, 1)->GetTimeBase());
  return 1;
}

static int CBasePlayer_GetTracerType (lua_State *L) {
  lua_pushstring(L, luaL_checkplayer(L, 1)->GetTracerType());
  return 1;
}

static int CBasePlayer_GetUseEntity (lua_State *L) {
  lua_pushentity(L, luaL_checkplayer(L, 1)->GetUseEntity());
  return 1;
}

static int CBasePlayer_GetUserID (lua_State *L) {
  lua_pushinteger(L, luaL_checkplayer(L, 1)->GetUserID());
  return 1;
}

// FIXME: push CBaseViewModel instead
static int CBasePlayer_GetViewModel (lua_State *L) {
  CBasePlayer *pPlayer = luaL_checkplayer( L, 1 );
  const int nIndex = lua_type( L, 2 ) == LUA_TNUMBER
      ? (int)clamp( lua_tonumber( L, 2 ), (lua_Number)0, (lua_Number)( MAX_VIEWMODELS - 1 ) )
      : 0;
  lua_pushanimating( L, pPlayer->GetViewModel( nIndex ) );
  return 1;
}

static int CBasePlayer_GetWaterJumpTime (lua_State *L) {
  lua_pushnumber(L, luaL_checkplayer(L, 1)->GetWaterJumpTime());
  return 1;
}

static int CBasePlayer_GetWeapon (lua_State *L) {
  CBasePlayer *pPlayer = luaL_checkplayer(L, 1);

  // HL2SB GMod compat (2026-09-25): GetWeapon( className ) -- GMod looks a
  // weapon up by CLASS NAME and returns it (or nil).  This binding historically
  // took a SLOT index; keep that working for numbers, resolve strings through
  // the same class lookup Player:StripWeapon uses.
  if ( lua_type( L, 2 ) == LUA_TSTRING )
  {
    CBaseCombatWeapon *pWeapon = pPlayer->Weapon_OwnsThisType( luaL_checkstring( L, 2 ) );
    if ( pWeapon != NULL )
      lua_pushweapon( L, pWeapon );
    else
      lua_pushnil( L );
    return 1;
  }

  lua_pushweapon(L, pPlayer->GetWeapon(luaL_checkint(L, 2)));
  return 1;
}

static int CBasePlayer_HintMessage (lua_State *L) {
  luaL_checkplayer(L, 1)->HintMessage(luaL_checkstring(L, 2));
  return 0;
}

/*static int CBasePlayer_IncrementEFNoInterpParity (lua_State *L) {
  luaL_checkplayer(L, 1)->IncrementEFNoInterpParity();
  return 0;
}
*/

static int CBasePlayer_IsBot (lua_State *L) {
  lua_pushboolean(L, luaL_checkplayer(L, 1)->IsBot());
  return 1;
}

static int CBasePlayer_IsHLTV (lua_State *L) {
  lua_pushboolean(L, luaL_checkplayer(L, 1)->IsHLTV());
  return 1;
}

static int CBasePlayer_IsInAVehicle (lua_State *L) {
  lua_pushboolean(L, luaL_checkplayer(L, 1)->IsInAVehicle());
  return 1;
}

//-----------------------------------------------------------------------------
// Purpose: HL2SB GMod compat - Player:GetVehicleEntity(), i.e. GMod's
// Player:GetVehicle().
//
// The server half already exists (game/server/lua/lplayer.cpp) and the GMod
// compatibility shim aliases GetVehicle onto it
// (lua/includes/modules/gmod_compatibility/sh_init.lua:904).  The CLIENT half was
// missing entirely, so in the client realm the alias resolved to nil and every
// `ply:GetVehicle()` was nil.  That is not cosmetic: the shipped
// lua/includes/modules/properties.lua:140 does
//
//     local veh = ply:GetVehicle()
//     if ( veh:IsValid() && ... )
//
// so the hovered-entity path raised "attempt to index a nil value" on the client.
//
// The entity is pushed through lua_pushentity(), which now hands a drivable
// vehicle the "Vehicle" metatable (lvehicle_shared.cpp), so this is also the
// client's route into the Vehicle library.
//-----------------------------------------------------------------------------
static int CBasePlayer_GetVehicleEntity (lua_State *L) {
#ifdef CLIENT_DLL
  // C_BasePlayer::GetVehicle() answers the IClientVehicle*, and GetVehicleEnt()
  // the entity behind it (c_baseplayer.h:668).
  IClientVehicle *pVehicle = luaL_checkplayer(L, 1)->GetVehicle();
  lua_pushentity(L, pVehicle != NULL ? pVehicle->GetVehicleEnt() : NULL);
#else
  lua_pushentity(L, luaL_checkplayer(L, 1)->GetVehicleEntity());
#endif
  return 1;
}

static int CBasePlayer_IsObserver (lua_State *L) {
  lua_pushboolean(L, luaL_checkplayer(L, 1)->IsObserver());
  return 1;
}

static int CBasePlayer_IsPlayer (lua_State *L) {
  lua_pushboolean(L, luaL_checkplayer(L, 1)->IsPlayer());
  return 1;
}

static int CBasePlayer_IsPlayerUnderwater (lua_State *L) {
  lua_pushboolean(L, luaL_checkplayer(L, 1)->IsPlayerUnderwater());
  return 1;
}

static int CBasePlayer_IsSuitEquipped (lua_State *L) {
  lua_pushboolean(L, luaL_checkplayer(L, 1)->IsSuitEquipped());
  return 1;
}

static int CBasePlayer_IsUseableEntity (lua_State *L) {
  lua_pushboolean(L, luaL_checkplayer(L, 1)->IsUseableEntity(luaL_checkentity(L, 2), luaL_checkint(L, 3)));
  return 1;
}

static int CBasePlayer_ItemPostFrame (lua_State *L) {
  luaL_checkplayer(L, 1)->ItemPostFrame();
  return 0;
}

static int CBasePlayer_ItemPreFrame (lua_State *L) {
  luaL_checkplayer(L, 1)->ItemPreFrame();
  return 0;
}

static int CBasePlayer_LeaveVehicle (lua_State *L) {
  luaL_checkplayer(L, 1)->LeaveVehicle();
  return 0;
}

static int CBasePlayer_LocalEyeAngles (lua_State *L) {
  QAngle v = luaL_checkplayer(L, 1)->LocalEyeAngles();
  lua_pushangle(L, v);
  return 1;
}

static int CBasePlayer_MaxSpeed (lua_State *L) {
  lua_pushnumber(L, luaL_checkplayer(L, 1)->MaxSpeed());
  return 1;
}

static int CBasePlayer_MyCombatCharacterPointer (lua_State *L) {
  lua_pushplayer(L, (CBasePlayer *)luaL_checkplayer(L, 1)->MyCombatCharacterPointer());
  return 1;
}

static int CBasePlayer_OnRestore (lua_State *L) {
  luaL_checkplayer(L, 1)->OnRestore();
  return 0;
}

static int CBasePlayer_PhysicsSimulate (lua_State *L) {
  luaL_checkplayer(L, 1)->PhysicsSimulate();
  return 0;
}

static int CBasePlayer_PhysicsSolidMaskForEntity (lua_State *L) {
  lua_pushinteger(L, luaL_checkplayer(L, 1)->PhysicsSolidMaskForEntity());
  return 1;
}

static int CBasePlayer_PlayerUse (lua_State *L) {
  luaL_checkplayer(L, 1)->PlayerUse();
  return 0;
}

static int CBasePlayer_PlayStepSound (lua_State *L) {
//  luaL_checkplayer(L, 1)->PlayStepSound(luaL_checkvector(L, 2), lua_tosurfacedata(L, 3), luaL_checknumber(L, 4), luaL_checkboolean(L, 5));
  return 0;
}

static int CBasePlayer_PostThink (lua_State *L) {
  luaL_checkplayer(L, 1)->PostThink();
  return 0;
}

static int CBasePlayer_PreThink (lua_State *L) {
  luaL_checkplayer(L, 1)->PreThink();
  return 0;
}

static int CBasePlayer_RemoveAllAmmo (lua_State *L) {
  luaL_checkplayer(L, 1)->RemoveAllAmmo();
  return 0;
}

static int CBasePlayer_RemoveAmmo (lua_State *L) {
  switch(lua_type(L, 3)) {
	case LUA_TNUMBER:
	default:
	  luaL_checkplayer(L, 1)->RemoveAmmo(luaL_checkint(L, 2), luaL_checkint(L, 3));
	  break;
	case LUA_TSTRING:
      luaL_checkplayer(L, 1)->RemoveAmmo(luaL_checkint(L, 2), luaL_checkstring(L, 3));
	  break;
  }
  return 0;
}

static int CBasePlayer_RemoveFromPlayerSimulationList (lua_State *L) {
  luaL_checkplayer(L, 1)->RemoveFromPlayerSimulationList(luaL_checkentity(L, 2));
  return 0;
}

static int CBasePlayer_ResetAutoaim (lua_State *L) {
  luaL_checkplayer(L, 1)->ResetAutoaim();
  return 0;
}

static int CBasePlayer_ResetObserverMode (lua_State *L) {
  luaL_checkplayer(L, 1)->ResetObserverMode();
  return 0;
}

static int CBasePlayer_SelectItem (lua_State *L) {
  luaL_checkplayer(L, 1)->SelectItem(luaL_checkstring(L, 2), luaL_optint(L, 3, 0));
  return 0;
}

static int CBasePlayer_SelectLastItem (lua_State *L) {
  luaL_checkplayer(L, 1)->SelectLastItem();
  return 0;
}

static int CBasePlayer_SetAmmoCount (lua_State *L) {
  luaL_checkplayer(L, 1)->SetAmmoCount(luaL_checkint(L, 2), luaL_checkint(L, 3));
  return 0;
}

/* HL2SB GMod compat: Player:SetAmmo( count, ammoType ) (GMod wiki: sets the
** player's reserve ammo for the given type).  GMod's ammoType accepts the
** ammo NAME string or a raw ammo index, exactly like RemoveAmmo above; the
** string resolves through the shared ammo definition first. */
static int CBasePlayer_SetAmmo (lua_State *L) {
  CBasePlayer *pPlayer = luaL_checkplayer(L, 1);
  int iCount = luaL_checkint(L, 2);
  if (lua_type(L, 3) == LUA_TSTRING)
  {
    int iAmmo = GetAmmoDef()->Index(luaL_checkstring(L, 3));
    if ( iAmmo >= 0 )
      pPlayer->SetAmmoCount(iCount, iAmmo);
  }
  else
  {
    pPlayer->SetAmmoCount(iCount, luaL_checkint(L, 3));
  }
  return 0;
}

// HL2SB GMod compat: Player:SetCanZoom / GetCanZoom -- the suit-zoom gate
// (GMod wiki: "Sets whether the player can zoom").  hl1sweps' sniper weapons
// toggle it on Deploy/Holster so the +zoom ironsight belongs to the weapon in
// hand.  The engine's suit zoom path (CHL2_Player::CheckSuitZoom) consults
// this flag through HL2SB_PlayerCanZoom below; the fork's historical behavior
// ("anyone with a suit may zoom") is the default when a player never calls it.
// Keyed by entindex in a plain static table; entries die with the level.
#ifndef CLIENT_DLL
static bool s_HL2SBPlayerCanZoom[MAX_PLAYERS];
static bool s_HL2SBPlayerCanZoomSet[MAX_PLAYERS];

bool HL2SB_PlayerCanZoom ( CBasePlayer *pPlayer )
{
  if ( pPlayer == NULL )
    return false;
  int i = pPlayer->entindex();
  if ( i < 0 || i >= MAX_PLAYERS || !s_HL2SBPlayerCanZoomSet[i] )
    return true;
  return s_HL2SBPlayerCanZoom[i];
}
#endif

static int CBasePlayer_SetCanZoom (lua_State *L) {
#ifdef CLIENT_DLL
  luaL_checkplayer(L, 1);
  lua_toboolean(L, 2);
#else
  CBasePlayer *pPlayer = luaL_checkplayer(L, 1);
  int i = pPlayer->entindex();
  if ( i >= 0 && i < MAX_PLAYERS )
  {
    s_HL2SBPlayerCanZoom[i] = lua_toboolean(L, 2) ? true : false;
    s_HL2SBPlayerCanZoomSet[i] = true;
  }
#endif
  return 0;
}

static int CBasePlayer_GetCanZoom (lua_State *L) {
#ifdef CLIENT_DLL
  luaL_checkplayer(L, 1);
  lua_pushboolean(L, true);
#else
  lua_pushboolean(L, HL2SB_PlayerCanZoom(luaL_checkplayer(L, 1)));
#endif
  return 1;
}

static int CBasePlayer_SetAnimation (lua_State *L) {
  luaL_checkplayer(L, 1)->SetAnimation((PLAYER_ANIM)luaL_checkint(L, 2));
  return 0;
}

// HL2SB (2026-09-22): Player:Crouching() -- GMod reads the FL_DUCKING flag
// (wiki: "Returns whether the player is crouching or not (FL_DUCKING flag)").
// cf_beast's weapon base halves the spread for a crouching shooter.
static int CBasePlayer_Crouching (lua_State *L) {
  CBasePlayer *pPlayer = luaL_checkplayer(L, 1);
  lua_pushboolean(L, (pPlayer->GetFlags() & FL_DUCKING) != 0);
  return 1;
}

// HL2SB (2026-10-02) GMod compat: Player:IsSprinting() -- tarkov_hud polls it
// every HUDPaint frame for its movement indicator.
// GMod's contract (wiki): "holding their sprint key and are allowed to
// sprint".  This fork already tracks exactly that state on both realms --
// CHL2_Player (server) / C_BaseHLPlayer (client) flip m_fIsSprinting from
// HandleSpeedChanges on IN_SPEED presses, gated by CanSprint (suit equipped +
// suit power) and drive HL2_SPRINT_SPEED.  Reading that state is the exact
// GMod semantic; a raw IN_SPEED button guess would report "sprinting" while
// merely holding shift against a wall and miss the suit gate.
static int CBasePlayer_IsSprinting (lua_State *L) {
  CBasePlayer *pPlayer = luaL_checkplayer(L, 1);
#ifdef CLIENT_DLL
  C_BaseHLPlayer *pHLEPlayer = static_cast< C_BaseHLPlayer * >( pPlayer );
#else
  CHL2_Player *pHLEPlayer = static_cast< CHL2_Player * >( pPlayer );
#endif
  lua_pushboolean( L, pHLEPlayer != NULL && pHLEPlayer->IsSprinting() );
  return 1;
}

// HL2SB (2026-09-22): Player:DoAnimationEvent( event, data ).  GMod routes the
// event through the gamemode; the SWEP bases this fork must run (cf_beast)
// pass the engine's own PLAYER_ANIM value (PLAYER_ATTACK1) expecting the third
// person model to swing, so play it through the player animation directly.
// The optional second argument is accepted and ignored for signature parity.
static int CBasePlayer_DoAnimationEvent (lua_State *L) {
  CBasePlayer *pPlayer = luaL_checkplayer(L, 1);
  const int iEvent = (int)luaL_checkint(L, 2);
  pPlayer->SetAnimation((PLAYER_ANIM)iEvent);
  return 0;
}

static int CBasePlayer_SetAnimationExtension (lua_State *L) {
  luaL_checkplayer(L, 1)->SetAnimationExtension(luaL_checkstring(L, 2));
  return 0;
}

static int CBasePlayer_SetBloodColor (lua_State *L) {
  luaL_checkplayer(L, 1)->SetBloodColor(luaL_checkint(L, 2));
  return 0;
}

// HL2SB GMod compat: Player:SetFOV( fov, [transitionTime], [requester] ).
//
// Reference behaviour: arg2 (the FOV in degrees) is required, arg3 is the
// transition time in seconds and defaults to 0, arg4 is the entity that owns
// the zoom and defaults to the player itself; the method returns nothing.
// The binding used to expose the raw engine signature
// ( requester, fov, time, speedup ), so every GMod call site - the classic
// scoped bases do ply:SetFOV( 75/ScopeZoom, 0.15 ) from SWEP:Think - raised
// "bad argument #3 to 'SetFOV'" and the caller's whole Think chain aborted
// there (no FOV change, no sights offset, no viewmodel hide).  The engine
// method itself (shared baseplayer_shared.cpp) is unchanged, both realms
// compile this file, and the requester defaults to self so the zoom-owner
// handshake inside SetFOV still sees a valid entity.
static int CBasePlayer_SetFOV (lua_State *L) {
  CBasePlayer *pPlayer = luaL_checkplayer(L, 1);
  int iFOV = luaL_checkint(L, 2);
  float flTime = (float)luaL_optnumber(L, 3, 0);
  CBaseEntity *pRequester = pPlayer;
  if ( !lua_isnoneornil(L, 4) )
    pRequester = luaL_checkentity(L, 4);
  pPlayer->SetFOV(pRequester, iFOV, flTime, 0);
  return 0;
}

static int CBasePlayer_SetLadderNormal (lua_State *L) {
  luaL_checkplayer(L, 1)->SetLadderNormal(luaL_checkvector(L, 2));
  return 0;
}

static int CBasePlayer_SetMaxSpeed (lua_State *L) {
  luaL_checkplayer(L, 1)->SetMaxSpeed(luaL_checknumber(L, 2));
  return 0;
}

// HL2SB GMod compat (2026-10-05): Player:GetMaxSpeed() -- TTT's
// GM:PlayerFootstep gates silent footsteps on it and the ported
// GM:PlayerStepSoundTime cadence method branches on it.
static int CBasePlayer_GetMaxSpeed (lua_State *L) {
  lua_pushnumber(L, luaL_checkplayer(L, 1)->MaxSpeed());
  return 1;
}

static int CBasePlayer_SetNextAttack (lua_State *L) {
  luaL_checkplayer(L, 1)->SetNextAttack(luaL_checknumber(L, 2));
  return 0;
}

static int CBasePlayer_SetPlayerLocalData (lua_State *L) {
  CBasePlayer *pPlayer = luaL_checkplayer(L, 1);
  const char *field = luaL_checkstring(L, 2);
  if (Q_strcmp(field, "m_iHideHUD") == 0)
    pPlayer->m_Local.m_iHideHUD = luaL_checkint(L, 3);

  else if (Q_strcmp(field, "m_flFOVRate") == 0)
    pPlayer->m_Local.m_flFOVRate = luaL_checknumber(L, 3);

  else if (Q_strcmp(field, "m_bDucked") == 0)
    pPlayer->m_Local.m_bDucked = (bool)luaL_checkboolean(L, 3);
  else if (Q_strcmp(field, "m_bDucking") == 0)
    pPlayer->m_Local.m_bDucking = (bool)luaL_checkboolean(L, 3);
  else if (Q_strcmp(field, "m_bInDuckJump") == 0)
    pPlayer->m_Local.m_bInDuckJump = (bool)luaL_checkboolean(L, 3);
  else if (Q_strcmp(field, "m_flDucktime") == 0)
    pPlayer->m_Local.m_flDucktime = luaL_checknumber(L, 3);
  else if (Q_strcmp(field, "m_flDuckJumpTime") == 0)
    pPlayer->m_Local.m_flDuckJumpTime = luaL_checknumber(L, 3);
  else if (Q_strcmp(field, "m_flJumpTime") == 0)
    pPlayer->m_Local.m_flJumpTime = luaL_checknumber(L, 3);
  else if (Q_strcmp(field, "m_nStepside") == 0)
    pPlayer->m_Local.m_nStepside = luaL_checkint(L, 3);
  else if (Q_strcmp(field, "m_flFallVelocity") == 0)
    pPlayer->m_Local.m_flFallVelocity = luaL_checknumber(L, 3);
  else if (Q_strcmp(field, "m_nOldButtons") == 0)
    pPlayer->m_Local.m_nOldButtons = luaL_checkint(L, 3);

#ifdef CLIENT_DLL
  else if (Q_strcmp(field, "m_vecClientBaseVelocity") == 0)
    pPlayer->m_Local.m_vecClientBaseVelocity = luaL_checkvector(L, 3);
#endif
  else if (Q_strcmp(field, "m_vecPunchAngle") == 0)
    pPlayer->m_Local.m_vecPunchAngle = luaL_checkangle(L, 3);

  else if (Q_strcmp(field, "m_vecPunchAngleVel") == 0)
    pPlayer->m_Local.m_vecPunchAngleVel = luaL_checkangle(L, 3);
  else if (Q_strcmp(field, "m_bDrawViewmodel") == 0)
    pPlayer->m_Local.m_bDrawViewmodel = (bool)luaL_checkboolean(L, 3);
  else if (Q_strcmp(field, "m_bWearingSuit") == 0)
    pPlayer->m_Local.m_bWearingSuit = (bool)luaL_checkboolean(L, 3);
  else if (Q_strcmp(field, "m_bPoisoned") == 0)
    pPlayer->m_Local.m_bPoisoned = (bool)luaL_checkboolean(L, 3);
  else if (Q_strcmp(field, "m_flStepSize") == 0)
    pPlayer->m_Local.m_flStepSize = luaL_checknumber(L, 3);
  else if (Q_strcmp(field, "m_bAllowAutoMovement") == 0)
    pPlayer->m_Local.m_bAllowAutoMovement = (bool)luaL_checkboolean(L, 3);

  else if (Q_strcmp(field, "m_bSlowMovement") == 0)
    pPlayer->m_Local.m_bSlowMovement = (bool)luaL_checkboolean(L, 3);
  return 0;
}

static int CBasePlayer_SetPlayerUnderwater (lua_State *L) {
  luaL_checkplayer(L, 1)->SetPlayerUnderwater(luaL_checkboolean(L, 2));
  return 0;
}

static int CBasePlayer_SetPreviouslyPredictedOrigin (lua_State *L) {
  luaL_checkplayer(L, 1)->SetPreviouslyPredictedOrigin(luaL_checkvector(L, 2));
  return 0;
}

static int CBasePlayer_SetPunchAngle (lua_State *L) {
  luaL_checkplayer(L, 1)->SetPunchAngle(luaL_checkangle(L, 2));
  return 0;
}

static int CBasePlayer_SetStepSoundCache (lua_State *L) {
  CBasePlayer *pPlayer = luaL_checkplayer(L, 1);
  int index = luaL_checkint(L, 2);
  const char *field = luaL_checkstring(L, 3);
  if (index == 0) {
    if (Q_strcmp(field, "m_SoundParameters") == 0)
      pPlayer->m_StepSoundCache[ 0 ].m_SoundParameters = lua_tosoundparameters(L, 4);
    else if (Q_strcmp(field, "m_usSoundNameIndex") == 0)
	  pPlayer->m_StepSoundCache[ 0 ].m_usSoundNameIndex = (unsigned short)luaL_checkinteger(L, 4);
  } else if (index == 1) {
    if (Q_strcmp(field, "m_SoundParameters") == 0)
      pPlayer->m_StepSoundCache[ 1 ].m_SoundParameters = lua_tosoundparameters(L, 4);
    else if (Q_strcmp(field, "m_usSoundNameIndex") == 0)
	  pPlayer->m_StepSoundCache[ 1 ].m_usSoundNameIndex = (unsigned short)luaL_checkinteger(L, 4);
  }
  return 1;
}

static int CBasePlayer_SetSuitUpdate (lua_State *L) {
  luaL_checkplayer(L, 1)->SetSuitUpdate((char *)luaL_checkstring(L, 2), luaL_checkint(L, 3), luaL_checkint(L, 4));
  return 0;
}

static int CBasePlayer_SetSwimSoundTime (lua_State *L) {
  luaL_checkplayer(L, 1)->SetSwimSoundTime(luaL_checknumber(L, 2));
  return 0;
}

static int CBasePlayer_SetWaterJumpTime (lua_State *L) {
  luaL_checkplayer(L, 1)->SetWaterJumpTime(luaL_checknumber(L, 2));
  return 0;
}

static int CBasePlayer_SharedSpawn (lua_State *L) {
  luaL_checkplayer(L, 1)->SharedSpawn();
  return 0;
}

static int CBasePlayer_ShouldShowHints (lua_State *L) {
  lua_pushboolean(L, luaL_checkplayer(L, 1)->ShouldShowHints());
  return 1;
}

static int CBasePlayer_SimulatePlayerSimulatedEntities (lua_State *L) {
  luaL_checkplayer(L, 1)->SimulatePlayerSimulatedEntities();
  return 0;
}

static int CBasePlayer_SmoothViewOnStairs (lua_State *L) {
  luaL_checkplayer(L, 1)->SmoothViewOnStairs(luaL_checkvector(L, 2));
  return 0;
}

static int CBasePlayer_Spawn (lua_State *L) {
  luaL_checkplayer(L, 1)->Spawn();
  return 0;
}

static int CBasePlayer_SwitchToNextBestWeapon (lua_State *L) {
  lua_pushboolean(L, luaL_checkplayer(L, 1)->SwitchToNextBestWeapon(luaL_checkweapon(L, 2)));
  return 1;
}

static int CBasePlayer_UpdateClientData (lua_State *L) {
  luaL_checkplayer(L, 1)->UpdateClientData();
  return 0;
}

static int CBasePlayer_UpdateUnderwaterState (lua_State *L) {
  luaL_checkplayer(L, 1)->UpdateUnderwaterState();
  return 0;
}

static int CBasePlayer_UsingStandardWeaponsInVehicle (lua_State *L) {
  lua_pushboolean(L, luaL_checkplayer(L, 1)->UsingStandardWeaponsInVehicle());
  return 1;
}

static int CBasePlayer_ViewPunch (lua_State *L) {
  luaL_checkplayer(L, 1)->ViewPunch(luaL_checkangle(L, 2));
  return 0;
}

static int CBasePlayer_ViewPunchReset (lua_State *L) {
  luaL_checkplayer(L, 1)->ViewPunchReset(luaL_optnumber(L, 2, 0));
  return 0;
}

static int CBasePlayer_Weapon_CanSwitchTo (lua_State *L) {
  lua_pushboolean(L, luaL_checkplayer(L, 1)->Weapon_CanSwitchTo(luaL_checkweapon(L, 2)));
  return 1;
}

// Inventory membership is a boolean and includes every weapon subtype.
static int CBasePlayer_HasWeapon( lua_State *L ) {
  CBasePlayer *player = luaL_checkplayer( L, 1 );
  const char *classname = luaL_checkstring( L, 2 );
  for ( int i = 0; i < MAX_WEAPONS; ++i ) {
    CBaseCombatWeapon *weapon = player->GetWeapon( i );
    if ( weapon != NULL && !Q_strcmp( weapon->GetClassname(), classname ) ) {
      lua_pushboolean( L, true );
      return 1;
    }
  }
  lua_pushboolean( L, false );
  return 1;
}

static int CBasePlayer_Weapon_OwnsThisType (lua_State *L) {
  lua_pushweapon(L, luaL_checkplayer(L, 1)->Weapon_OwnsThisType(luaL_checkstring(L, 2), luaL_optint(L, 3, 0)));
  return 1;
}

static int CBasePlayer_Weapon_SetLast (lua_State *L) {
  luaL_checkplayer(L, 1)->Weapon_SetLast(luaL_checkweapon(L, 2));
  return 0;
}

static int CBasePlayer_Weapon_ShootPosition (lua_State *L) {
  Vector v = luaL_checkplayer(L, 1)->Weapon_ShootPosition();
  lua_pushvector(L, v);
  return 1;
}

static int CBasePlayer_Weapon_ShouldSelectItem (lua_State *L) {
  lua_pushboolean(L, luaL_checkplayer(L, 1)->Weapon_ShouldSelectItem(luaL_checkweapon(L, 2)));
  return 1;
}

static int CBasePlayer_Weapon_ShouldSetLast (lua_State *L) {
  lua_pushboolean(L, luaL_checkplayer(L, 1)->Weapon_ShouldSetLast(luaL_checkweapon(L, 2), luaL_checkweapon(L, 3)));
  return 1;
}

static int CBasePlayer_Weapon_Switch (lua_State *L) {
  lua_pushboolean(L, luaL_checkplayer(L, 1)->Weapon_Switch(luaL_checkweapon(L, 2), luaL_optint(L, 3, 0)));
  return 1;
}

static int CBasePlayer_WeaponCount (lua_State *L) {
  lua_pushinteger(L, luaL_checkplayer(L, 1)->WeaponCount());
  return 1;
}

// HL2SB GMod compat (2026-09-24): Player:Alive()/IsAlive() - the First Person
// Body addon captures PLAYER.Alive; this fork only had Entity:IsAlive and the
// bare PLAYER metatable read does not fall through to it.
static int CBasePlayer_Alive (lua_State *L) {
  CBasePlayer *player = luaL_checkplayer(L, 1);
#ifdef CLIENT_DLL
  if (!g_PR) return 0;
  lua_pushboolean(L, g_PR->IsAlive(player->entindex()));
#else
  lua_pushboolean(L, player->IsAlive());
#endif
  return 1;
}

static int CBasePlayer_GetClassID (lua_State *L) {
#ifdef CLIENT_DLL
  C_HL2MP_Player *pPlayer = dynamic_cast<C_HL2MP_Player *>(luaL_checkplayer(L, 1));
#else
  CHL2MP_Player *pPlayer = dynamic_cast<CHL2MP_Player *>(luaL_checkplayer(L, 1));
#endif
  lua_pushinteger(L, pPlayer ? pPlayer->HL2SB_GetPlayerClassID() : 0);
  return 1;
}

static int CBasePlayer_SetClassID (lua_State *L) {
  int nClassID = clamp(luaL_checkint(L, 2), 0, 0xffff);
#ifdef CLIENT_DLL
  C_HL2MP_Player *pPlayer = dynamic_cast<C_HL2MP_Player *>(luaL_checkplayer(L, 1));
#else
  CHL2MP_Player *pPlayer = dynamic_cast<CHL2MP_Player *>(luaL_checkplayer(L, 1));
#endif
  if (pPlayer) pPlayer->HL2SB_SetPlayerClassID(nClassID);
  return 0;
}

// Spawn hooks receive the base Player metatable as well as the HL2MP one.
// Both must resolve the same replicated hands handle.
static int CBasePlayer_GetHands (lua_State *L) {
#ifdef CLIENT_DLL
  C_HL2MP_Player *pPlayer = dynamic_cast<C_HL2MP_Player *>(luaL_checkplayer(L, 1));
#else
  CHL2MP_Player *pPlayer = dynamic_cast<CHL2MP_Player *>(luaL_checkplayer(L, 1));
#endif
  if (pPlayer && pPlayer->HL2SB_GetHandsEntity())
    lua_pushentity(L, pPlayer->HL2SB_GetHandsEntity());
  else
    lua_pushnil(L);
  return 1;
}

static int CBasePlayer_SetHands (lua_State *L) {
#ifdef CLIENT_DLL
  C_HL2MP_Player *pPlayer = dynamic_cast<C_HL2MP_Player *>(luaL_checkplayer(L, 1));
#else
  CHL2MP_Player *pPlayer = dynamic_cast<CHL2MP_Player *>(luaL_checkplayer(L, 1));
#endif
  if (pPlayer)
    pPlayer->HL2SB_SetHandsEntity(lua_isnoneornil(L, 2) ? NULL : lua_toentity(L, 2));
  return 0;
}

static int CBasePlayer_GetHull (lua_State *L) {
  CBasePlayer *pPlayer = luaL_checkplayer(L, 1);
  Vector mins, maxs;
  HL2SB_GetPlayerHullBounds(pPlayer, false, mins, maxs);
  lua_pushvector(L, mins);
  lua_pushvector(L, maxs);
  return 2;
}

static int CBasePlayer_SetHull (lua_State *L) {
  CBasePlayer *pPlayer = luaL_checkplayer(L, 1);
  HL2SB_SetPlayerHullBounds(pPlayer, false, luaL_checkvector(L, 2), luaL_checkvector(L, 3));
  return 0;
}

static int CBasePlayer_GetHullDuck (lua_State *L) {
  CBasePlayer *pPlayer = luaL_checkplayer(L, 1);
  Vector mins, maxs;
  HL2SB_GetPlayerHullBounds(pPlayer, true, mins, maxs);
  lua_pushvector(L, mins);
  lua_pushvector(L, maxs);
  return 2;
}

static int CBasePlayer_SetHullDuck (lua_State *L) {
  CBasePlayer *pPlayer = luaL_checkplayer(L, 1);
  HL2SB_SetPlayerHullBounds(pPlayer, true, luaL_checkvector(L, 2), luaL_checkvector(L, 3));
  return 0;
}

static int CBasePlayer_ResetHull (lua_State *L) {
  CBasePlayer *pPlayer = luaL_checkplayer(L, 1);
  HL2SB_ResetPlayerHullBounds(pPlayer);
  HL2SB_ResetPlayerViewOffsets(pPlayer);
  return 0;
}

static int CBasePlayer_SetEyeAngles (lua_State *L) {
  CBasePlayer *pPlayer = luaL_checkplayer(L, 1);
  const QAngle angles = luaL_checkangle(L, 2);
#ifdef CLIENT_DLL
  pPlayer->SetLocalAngles(angles);
#else
  pPlayer->SnapEyeAngles(angles);
#endif
  return 0;
}

static int CBasePlayer_GetViewOffset (lua_State *L) {
  Vector offset;
  HL2SB_GetPlayerViewOffsetDesired(luaL_checkplayer(L, 1), false, offset);
  lua_pushvector(L, offset);
  return 1;
}

static int CBasePlayer_SetViewOffset (lua_State *L) {
  HL2SB_SetPlayerViewOffsetDesired(luaL_checkplayer(L, 1), false, luaL_checkvector(L, 2));
  return 0;
}

static int CBasePlayer_GetViewOffsetDucked (lua_State *L) {
  Vector offset;
  HL2SB_GetPlayerViewOffsetDesired(luaL_checkplayer(L, 1), true, offset);
  lua_pushvector(L, offset);
  return 1;
}

static int CBasePlayer_SetViewOffsetDucked (lua_State *L) {
  HL2SB_SetPlayerViewOffsetDesired(luaL_checkplayer(L, 1), true, luaL_checkvector(L, 2));
  return 0;
}

static int CBasePlayer_GetCurrentViewOffset (lua_State *L) {
  lua_pushvector(L, luaL_checkplayer(L, 1)->GetViewOffset());
  return 1;
}

static int CBasePlayer_SetCurrentViewOffset (lua_State *L) {
  luaL_checkplayer(L, 1)->SetViewOffset(luaL_checkvector(L, 2));
  return 0;
}

#ifdef CLIENT_DLL
// Player render angles belong to the animation state, independently of the
// entity's parent transform and eye angles.
// HL2SB GMod compat (2026-10-03): Player:ShouldDrawLocalPlayer() - the
// engine's draw question for this player.  Deliberately WITHOUT the
// ShouldDrawLocalPlayer Lua hook (that hook can itself call this method, and
// the reference binding consults engine state, not the hook) - this mirrors
// the view-entity half of ClientModeShared::ShouldDrawLocalPlayer.
static int CBasePlayer_ShouldDrawLocalPlayer (lua_State *L) {
  C_BasePlayer *pPlayer = lua_toplayer(L, 1);
  bool bShouldDraw = true;
  if ( pPlayer->index == render->GetViewEntity() && !C_BasePlayer::ShouldDrawLocalPlayer() )
    bShouldDraw = false;
  lua_pushboolean( L, bShouldDraw );
  return 1;
}


// HL2SB GMod compat (2026-09-25): Player:GetAllowWeaponsInVehicle() - no
// client-side vehicle flag exists in this fork, answer false (the addon only
// uses it to stand down in vehicles, which its own InVehicle() check already
// covers).
static int CBasePlayer_GetAllowWeaponsInVehicle (lua_State *L) {
  lua_toplayer(L, 1);
  lua_pushboolean(L, 0);
  return 1;
}

#endif

static int CBasePlayer_GetRenderAngles (lua_State *L) {
  CHL2MP_Player *player = dynamic_cast<CHL2MP_Player *>(luaL_checkplayer(L, 1));
  if (player) lua_pushangle(L, player->GetRenderAngles());
  else lua_pushangle(L, vec3_angle);
  return 1;
}

static int CBasePlayer_SetRenderAngles (lua_State *L) {
  CHL2MP_Player *pPlayer = dynamic_cast<CHL2MP_Player *>(luaL_checkplayer(L, 1));
  const QAngle &angles = luaL_checkangle(L, 2);
  if (pPlayer != NULL
#ifdef CLIENT_DLL
      && !pPlayer->IsRagdoll()
#endif
      )
  {
    // The mutable animation state owns this reference. Entity transforms
    // must not change when a render callback temporarily changes the pose.
    const_cast<QAngle &>(pPlayer->GetRenderAngles()) = angles;
  }
  return 0;
}

static int CBasePlayer___index (lua_State *L) {
  CBasePlayer *pPlayer = lua_toplayer(L, 1);
  if (pPlayer == NULL) {  /* avoid extra test when d is not 0 */
    /* HL2SB: GMod's NULL sentinel answers reads instead of raising -- raising
    ** here is what made the global IsValid() (util.lua:318) throw
    ** "attempt to index a NULL entity" from every hook probing a player who
    ** has already left (HUDItemPickedUp).  Same contract as
    ** CBaseEntity___index's NULL branch above lbaseentity_shared.cpp:2695. */
    HL2SB_PushNullEntityIndex( L, lua_tostring( L, 2 ) );
    return 1;
  }
  const char *field = luaL_checkstring(L, 2);
  if (Q_strcmp(field, "m_afButtonLast") == 0)
    lua_pushinteger(L, pPlayer->m_afButtonLast);
  else if (Q_strcmp(field, "m_afButtonPressed") == 0)
    lua_pushinteger(L, pPlayer->m_afButtonPressed);
  else if (Q_strcmp(field, "m_afButtonReleased") == 0)
    lua_pushinteger(L, pPlayer->m_afButtonReleased);
  else if (Q_strcmp(field, "m_flNextAttack") == 0)
    lua_pushnumber(L, pPlayer->m_flNextAttack);
  else if (Q_strcmp(field, "m_fOnTarget") == 0)
    lua_pushboolean(L, pPlayer->m_fOnTarget);
  else if (Q_strcmp(field, "m_nButtons") == 0)
    lua_pushinteger(L, pPlayer->m_nButtons);
  else if (Q_strcmp(field, "m_StuckLast") == 0)
    lua_pushinteger(L, pPlayer->m_StuckLast);
  else if (Q_strcmp(field, "m_szAnimExtension") == 0)
    lua_pushstring(L, pPlayer->m_szAnimExtension);
  else if (lua_isrefvalid(L, pPlayer->m_nTableReference)) {
    lua_getref(L, pPlayer->m_nTableReference);
    lua_getfield(L, -1, field);
    if (lua_isnil(L, -1)) {
      lua_pop(L, 2);
      lua_getmetatable(L, 1);
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
  else {
    lua_getmetatable(L, 1);
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
  return 1;
}

static int CBasePlayer___newindex (lua_State *L) {
  CBasePlayer *pPlayer = lua_toplayer(L, 1);
  if (pPlayer == NULL) {
    /* HL2SB (2026-10-03): field writes on the NULL sentinel are silently
    ** discarded (reference behaviour; see CBaseEntity___newindex). */
    return 0;
  }
  const char *field = luaL_checkstring(L, 2);
  if (Q_strcmp(field, "m_afButtonLast") == 0)
    pPlayer->m_afButtonLast = luaL_checkint(L, 3);
  else if (Q_strcmp(field, "m_afButtonPressed") == 0)
    pPlayer->m_afButtonPressed = luaL_checkint(L, 3);
  else if (Q_strcmp(field, "m_afButtonReleased") == 0)
    pPlayer->m_afButtonReleased = luaL_checkint(L, 3);
  else if (Q_strcmp(field, "m_flNextAttack") == 0)
    pPlayer->m_flNextAttack = luaL_checknumber(L, 3);
#ifdef CLIENT_DLL
  else if (Q_strcmp(field, "m_fOnTarget") == 0)
    pPlayer->m_fOnTarget = luaL_checkboolean(L, 3);
#else
  else if (Q_strcmp(field, "m_fOnTarget") == 0)
    pPlayer->m_fOnTarget.GetForModify() = (bool)luaL_checkboolean(L, 3);
#endif
  else if (Q_strcmp(field, "m_nButtons") == 0)
    pPlayer->m_nButtons = luaL_checkint(L, 3);
  else if (Q_strcmp(field, "m_StuckLast") == 0)
    pPlayer->m_StuckLast = luaL_checkint(L, 3);
  else if (Q_strcmp(field, "m_szAnimExtension") == 0)
    Q_strcpy(pPlayer->m_szAnimExtension, luaL_checkstring(L, 3));
  else {
    // HL2SB: < 0, not == LUA_NOREF.  LUA_REFNIL (-1) is a legal value here
    // (entity.get() had no table when the ref was first taken), and with the
    // old == test the write below went through lua_getref(-1), which pushes
    // nil, so lua_setfield stored the value on itself and the field write was
    // silently dropped.  That kept Owner.C4s nil and broke the cod_c4 addon
    // explosion chain (shared.lua:255 "#Owner.C4s" on a nil field).
    if (pPlayer->m_nTableReference < 0) {
      lua_newtable(L);
      pPlayer->m_nTableReference = luaL_ref(L, LUA_REGISTRYINDEX);
    }
    lua_getref(L, pPlayer->m_nTableReference);
    lua_pushvalue(L, 3);
    lua_setfield(L, -2, field);
	lua_pop(L, 1);
  }
  return 0;
}

static int CBasePlayer___eq (lua_State *L) {
  lua_pushboolean(L, lua_toplayer(L, 1) == lua_toplayer(L, 2));
  return 1;
}

//-----------------------------------------------------------------------------
// HL2SB GMod compat: Player:IsListenServerHost().
//
// The minecraft SWEP routes its menu console command through it (singleplayer
// path).  On a listen server the host is always player index 1.
//-----------------------------------------------------------------------------
static int CBasePlayer_IsListenServerHost (lua_State *L) {
  CBasePlayer *pPlayer = luaL_checkplayer(L, 1);
#ifdef CLIENT_DLL
  // On a listen server the host's client entity index is always 1.
  lua_pushboolean(L, pPlayer->entindex() == 1);
#else
  lua_pushboolean(L, !engine->IsDedicatedServer() && pPlayer->entindex() == 1);
#endif
  return 1;
}

//-----------------------------------------------------------------------------
// HL2SB GMod compat: Player:ConCommand( command ).
//
// The minecraft SWEP drives everything through it (menu open, block-count
// bookkeeping, "remove my blocks"): server side the command must execute on
// that player's console, client side it runs locally.
//-----------------------------------------------------------------------------
static int CBasePlayer_ConCommand (lua_State *L) {
  CBasePlayer *pPlayer = luaL_checkplayer(L, 1);
  const char *pszCommand = luaL_checkstring(L, 2);
#ifndef CLIENT_DLL
  engine->ClientCommand( pPlayer->edict(), "%s", pszCommand );
#else
  (void)pPlayer;
  engine->ClientCmd( pszCommand );
#endif
  return 0;
}

//-----------------------------------------------------------------------------
// HL2SB GMod compat: Player:SelectWeapon( class ).  GMod's gmod_camera
// registers a "gmod_camera" console command whose whole body is
// ply:SelectWeapon( "gmod_camera" ).
//-----------------------------------------------------------------------------
#ifndef CLIENT_DLL
static int CBasePlayer_SelectWeapon (lua_State *L) {
  CBasePlayer *pPlayer = luaL_checkplayer(L, 1);
  const char *pszClass = luaL_checkstring(L, 2);
  pPlayer->SelectItem( pszClass );
  return 0;
}
#endif

static int CBasePlayer___tostring (lua_State *L) {
  CBasePlayer *pPlayer = lua_toplayer(L, 1);
  if (pPlayer == NULL)
    lua_pushstring(L, "NULL");
  else
    lua_pushfstring(L, "CBasePlayer: %d \"%s\"", pPlayer->GetUserID(), pPlayer->GetPlayerName());
  return 1;
}



//-----------------------------------------------------------------------------
// Per-player sleeve colour - GMod player:GetPlayerColor / player:SetPlayerColor.
// The c_arms "PlayerColor" material proxy (client) reads the LOCAL player's
// colour via HL2SB_GetPlayerColor and tints the sleeves. Kept keyed by userid in
// a map so we don't add a member to the shared networked player class.
//-----------------------------------------------------------------------------
static CUtlMap<int, Color> s_PlayerColor;
static bool s_PlayerColorInit = false;

Color HL2SB_GetPlayerColor( int iUserID )
{
	if ( !s_PlayerColorInit ) { s_PlayerColor.SetLessFunc( DefLessFunc( int ) ); s_PlayerColorInit = true; }
	int idx = s_PlayerColor.Find( iUserID );
	if ( idx == s_PlayerColor.InvalidIndex() )
		return Color( 62, 88, 106, 255 );	// GMod teal fallback
	return s_PlayerColor[ idx ];
}

void HL2SB_SetPlayerColor( int iUserID, const Color &clr )
{
	if ( !s_PlayerColorInit ) { s_PlayerColor.SetLessFunc( DefLessFunc( int ) ); s_PlayerColorInit = true; }
	s_PlayerColor.InsertOrReplace( iUserID, clr );
}

// GMod's weapon colour, the other half of the pair (Player:SetWeaponColor /
// GetWeaponColor, sandbox/gamemode/player_class/player_sandbox.lua:111-115).  GMod's
// "PlayerWeaponColor" material proxy reads it; HL2SB's arms already follow the client's
// cl_weaponcolor directly (c_viewmodel_attachment.cpp), so this is storage + API
// compatibility - but GMod's player class calls it on every spawn, so it has to exist.
static CUtlMap<int, Color> s_WeaponColor;
static bool s_WeaponColorInit = false;

Color HL2SB_GetWeaponColor( int iUserID )
{
	if ( !s_WeaponColorInit ) { s_WeaponColor.SetLessFunc( DefLessFunc( int ) ); s_WeaponColorInit = true; }

#ifdef CLIENT_DLL
	// 2026-09-26: the LOCAL player's colour is the live cl_weaponcolor convar
	// -- the map below only fills when Lua calls SetWeaponColor (at spawn), so
	// a weapon colour picked in the menu mid-session left every map reader on
	// the OLD colour while the convar readers showed the new one (the "beam
	// colour never changes" report).  The convar IS the source of truth for
	// the local player; the map keeps serving remote players.
	C_BasePlayer *pLocal = C_BasePlayer::GetLocalPlayer();
	if ( pLocal != NULL && pLocal->GetUserID() == iUserID )
	{
		static ConVarRef s_cl_weaponcolor_live( "cl_weaponcolor" );
		if ( s_cl_weaponcolor_live.IsValid() )
		{
			float r = 0.0f, g = 0.0f, b = 0.0f;
			if ( sscanf( s_cl_weaponcolor_live.GetString(), "%f %f %f", &r, &g, &b ) == 3 )
			{
				return Color(
					clamp( (int)( r * 255.0f ), 0, 255 ),
					clamp( (int)( g * 255.0f ), 0, 255 ),
					clamp( (int)( b * 255.0f ), 0, 255 ), 255 );
			}
		}
	}
#endif

	int idx = s_WeaponColor.Find( iUserID );
	if ( idx != s_WeaponColor.InvalidIndex() )
		return s_WeaponColor[ idx ];

	// 2026-09-24: this used to fall back to the cl_weaponcolor CONVAR for any
	// unknown userid.  The convar is the LOCAL player's value, so on the client
	// every remote player whose entry had not arrived yet was tinted with the
	// local player's colour (cross-player colour bleed on beams, claw sprites
	// and muzzle glows).  The server now broadcasts the whole colour table
	// (hl2sb_setplayercolor) and replays it on ClientActive, so a miss here
	// only happens before that snapshot lands.  GMod's cl_weaponcolor default
	// "0.30 1.80 2.10" clamped.
	return Color( 76, 255, 255, 255 );
}

void HL2SB_SetWeaponColor( int iUserID, const Color &clr )
{
	if ( !s_WeaponColorInit ) { s_WeaponColor.SetLessFunc( DefLessFunc( int ) ); s_WeaponColorInit = true; }
	s_WeaponColor.InsertOrReplace( iUserID, clr );
}

#ifndef CLIENT_DLL
// HL2SB: GMod networks the player colours (m_PlayerColor / m_WeaponColor data
// props on the player data table), so every client natively sees every
// player's colours.  The fork keeps them in per-userid server memory instead,
// so the server pushes the table over the hl2sb_setplayercolor console
// command: every Lua write broadcasts to all connected clients (below) and
// CServerGameClients::ClientActive replays the current table to late joiners.
// pOnlyTo limits the replay to a single freshly activated client.
void HL2SB_SendPlayerColors( CBasePlayer *pOnlyTo )
{
	for ( int iClient = 1; iClient <= gpGlobals->maxClients; iClient++ )
	{
		CBasePlayer *pPlayer = UTIL_PlayerByIndex( iClient );
		if ( !pPlayer || ( pOnlyTo != NULL && pOnlyTo != pPlayer ) )
			continue;
		if ( !pPlayer->IsConnected() || pPlayer->IsFakeClient() )
			continue;

		for ( int idx = s_PlayerColor.FirstInorder(); idx != s_PlayerColor.InvalidIndex(); idx = s_PlayerColor.NextInorder( idx ) )
		{
			const Color &c = s_PlayerColor[ idx ];
			char szCmd[ 64 ];
			Q_snprintf( szCmd, sizeof( szCmd ), "hl2sb_setplayercolor %d 0 %d %d %d %d\n",
				s_PlayerColor.Key( idx ), c.r(), c.g(), c.b(), c.a() );
			engine->ClientCommand( pPlayer->edict(), szCmd );
		}

		for ( int idx = s_WeaponColor.FirstInorder(); idx != s_WeaponColor.InvalidIndex(); idx = s_WeaponColor.NextInorder( idx ) )
		{
			const Color &c = s_WeaponColor[ idx ];
			char szCmd[ 64 ];
			Q_snprintf( szCmd, sizeof( szCmd ), "hl2sb_setplayercolor %d 1 %d %d %d %d\n",
				s_WeaponColor.Key( idx ), c.r(), c.g(), c.b(), c.a() );
			engine->ClientCommand( pPlayer->edict(), szCmd );
		}
	}
}
#endif

// GMod's Player:SetPlayerColor / SetWeaponColor take a NORMALIZED Vector:
//   sandbox/gamemode/player_class/player_sandbox.lua:108-115
//       self.Player:SetPlayerColor( Vector( plyclr ) )      -- plyclr = cl_playercolor
//   terrortown/gamemode/player.lua:270
//       ply:SetPlayerColor( Vector( clr.r/255.0, clr.g/255.0, clr.b/255.0 ) )
// while this fork's original binding took a 0-255 Color table.  Both are accepted now
// (the "r g b" 0-1 string the convars carry works too), so GMod gamemode/addon code
// that passes a Vector works unchanged.  Values are clamped: GMod's default
// cl_weaponcolor is "0.30 1.80 2.10", i.e. deliberately above 1.
static bool HL2SB_LuaColorArg( lua_State *L, int narg, Color &out )
{
  switch ( lua_type( L, narg ) )
  {
    case LUA_TSTRING:
    {
      float r = 0.0f, g = 0.0f, b = 0.0f;
      if ( sscanf( lua_tostring( L, narg ), "%f %f %f", &r, &g, &b ) < 3 )
        return false;

      out = Color( (int)clamp( r * 255.0f, 0.0f, 255.0f ),
                   (int)clamp( g * 255.0f, 0.0f, 255.0f ),
                   (int)clamp( b * 255.0f, 0.0f, 255.0f ), 255 );
      return true;
    }

    case LUA_TTABLE:
    {
      if ( !lua_iscolor( L, narg ) )
        return false;

      out = luaL_checkcolor( L, narg );
      return true;
    }

    case LUA_TUSERDATA:
    {
      Vector v = luaL_checkvector( L, narg );

      out = Color( (int)clamp( v.x * 255.0f, 0.0f, 255.0f ),
                   (int)clamp( v.y * 255.0f, 0.0f, 255.0f ),
                   (int)clamp( v.z * 255.0f, 0.0f, 255.0f ), 255 );
      return true;
    }

    default:
      return false;
  }
}

static int CBasePlayer_GetPlayerColor (lua_State *L) {
  CBasePlayer *pPlayer = luaL_checkplayer(L, 1);
  Color clr = HL2SB_GetPlayerColor( pPlayer->GetUserID() );

  // GMod answers a NORMALIZED Vector, not a Color: lua/matproxy/player_color.lua only
  // accepts a vector ("if ( isvector( col ) ) then mat:SetVector(...)"), and the sandbox
  // / TTT player classes feed the result straight back into SetPlayerColor.
  lua_pushvector(L, Vector( clr.r() / 255.0f, clr.g() / 255.0f, clr.b() / 255.0f ));
  return 1;
}

static int CBasePlayer_SetPlayerColor (lua_State *L) {
  CBasePlayer *pPlayer = luaL_checkplayer(L, 1);
  Color clr;

  if ( !HL2SB_LuaColorArg( L, 2, clr ) )
    return 0;

  HL2SB_SetPlayerColor( pPlayer->GetUserID(), clr );
#ifndef CLIENT_DLL
  // GMod networks the colour to everyone; push the updated table to all
  // clients (this player's own client included) so the PlayerColor proxy
  // renders the tint on every screen, not just the owner's.
  HL2SB_SendPlayerColors( NULL );
#endif
  return 0;
}

// GMod: Player:GetWeaponColor() / SetWeaponColor( Vector ) -- the arm/sleeve colour
// (see HL2SB_GetWeaponColor above).  Same normalized-Vector / Color / string contract
// as SetPlayerColor, and likewise answered as a normalized Vector.
static int CBasePlayer_GetWeaponColor (lua_State *L) {
  CBasePlayer *pPlayer = luaL_checkplayer(L, 1);
  Color clr = HL2SB_GetWeaponColor( pPlayer->GetUserID() );

  lua_pushvector(L, Vector( clr.r() / 255.0f, clr.g() / 255.0f, clr.b() / 255.0f ));
  return 1;
}

static int CBasePlayer_SetWeaponColor (lua_State *L) {
  CBasePlayer *pPlayer = luaL_checkplayer(L, 1);
  Color clr;

  if ( HL2SB_LuaColorArg( L, 2, clr ) )
  {
    HL2SB_SetWeaponColor( pPlayer->GetUserID(), clr );
#ifndef CLIENT_DLL
    // Broadcast like SetPlayerColor - GMod networks m_WeaponColor to everyone.
    HL2SB_SendPlayerColors( NULL );
#endif
  }

  return 0;
}

// GMod: Player:SteamID() / SteamID64().  Both exist on BOTH realms in GMod, and GMod code
// compares the client's answer with the server's: cod_c4's C4 HUD does
//     visible_entity:GetNWString( "OwnerID" ) == LocalPlayerEntity:SteamID()
// (addons/cod_c4/lua/entities/cod-c4/cl_init.lua:58) against the OwnerID the server stored
// from Owner:SteamID() (weapons/seal6-c4/shared.lua:225).
//
// This fork only had SteamID aliased onto GetNetworkIDString, which is bound on the SERVER
// alone (game/server/lua/lplayer.cpp:584) - on the client the method did not exist at all,
// so that HUD line raised "attempt to call a nil value (method 'SteamID')".
//
// CBasePlayer::GetSteamID is available in both realms (server player.cpp:9481, client
// c_baseplayer.cpp:2888).  CSteamID::Render() is NOT used: this SDK prints the newer
// "[U:1:12345]" form, GMod prints the old Steam2 "STEAM_0:1:12345" one that addons parse,
// and Render()'s implementation (common/steamid.cpp) is not linked into the game DLLs
// (LNK2019).  The string is built from the inline accessors with Valve's own arithmetic
// (common/steamid.cpp:508-517: accountID = Low32 * 2 + High32).
static void HL2SB_SteamIDString( CBasePlayer *pPlayer, char *szOut, size_t nOutLen ) {
  CSteamID steamID;

  if ( pPlayer->GetSteamID( &steamID ) && steamID.IsValid() )
  {
    unsigned unUniverse = (unsigned)steamID.GetEUniverse();

    if ( unUniverse >= (unsigned)k_EUniversePublic )
      unUniverse -= (unsigned)k_EUniversePublic;
    else
      unUniverse = 0;

    Q_snprintf( szOut, nOutLen, "STEAM_%u:%u:%u", unUniverse,
                (unsigned)( steamID.GetAccountID() & 1 ), (unsigned)( steamID.GetAccountID() >> 1 ) );
    return;
  }

  // No Steam (this fork also runs standalone): a string both realms derive from the same
  // networked value, so the client/server comparison above still holds.
  Q_snprintf( szOut, nOutLen, "STEAM_0:0:%d", pPlayer->entindex() );
}

static void HL2SB_PushSteamID( lua_State *L, CBasePlayer *pPlayer ) {
  char szID[ 32 ];

  HL2SB_SteamIDString( pPlayer, szID, sizeof( szID ) );
  lua_pushstring( L, szID );
}

static int CBasePlayer_SteamID (lua_State *L) {
  HL2SB_PushSteamID( L, luaL_checkplayer(L, 1) );
  return 1;
}

// Persistent ownership requires a real account ID; entity slots are recycled.
// GMod returns -1 when the player's Steam ID cannot be obtained.
static int CBasePlayer_AccountID (lua_State *L) {
  CBasePlayer *pPlayer = luaL_checkplayer(L, 1);

  CSteamID steamID;
  if ( pPlayer->GetSteamID( &steamID ) )
  {
    lua_pushinteger( L, (lua_Integer)steamID.GetAccountID() );
    return 1;
  }

  lua_pushinteger( L, -1 );
  return 1;
}

/*
** HL2SB GMod compat: the usergroup suite behind Player:IsAdmin().
**
** Wiki semantics (wiki.facepunch.com Player:IsAdmin / IsSuperAdmin /
** IsUserGroup / GetUserGroup):
**   - IsAdmin()      = IsUserGroup("admin") or IsUserGroup("superadmin")
**   - IsSuperAdmin() = IsUserGroup("superadmin")
**   - GetUserGroup() = group string, "user" when none
**   - groups are loaded from garrysmod/settings/users.txt  -> here:
**     settings/users.txt under the GAME search path.
**
** Group resolution order (server): a users.txt entry for the player wins;
** with no entry, the listen-server host is "superadmin" (GMod's own wiki
** example runs Entity(1):IsUserGroup("superadmin") as the listen host),
** everyone else is "user".  This engine is NON-STEAM: GetSteamID never
** yields a real Steam2 id (fallback "STEAM_0:0:<entindex>"), so users.txt
** entries are matched by PLAYER NAME first -- the operative path here --
** and by the id string as a GMod-shaped fallback.  The client realm answers
** from the same host heuristic: this engine does not network the usergroup
** string, so on a dedicated server remote clients always report "user" --
** documented limitation, listen/standalone play (this fork's usage) is
** exact.  Addons gate server functions on this (cod_c4's net receiver at
** shared.lua:27 raised "attempt to call a nil value (method 'IsAdmin')" on
** every convar change before any of this existed).
*/
static void HL2SB_GetUserGroup( CBasePlayer *pPlayer, char *szGroup, size_t nGroupLen ) {
  Q_strncpy( szGroup, "user", nGroupLen );

#ifdef CLIENT_DLL
  // On a listen server the host's client entity index is always 1
  // (same convention as IsListenServerHost above; the client engine
  // interface has no IsDedicatedServer to ask).
  if ( pPlayer->entindex() == 1 )
    Q_strncpy( szGroup, "superadmin", nGroupLen );
#else
  if ( !engine->IsDedicatedServer() && pPlayer->entindex() == 1 )
    Q_strncpy( szGroup, "superadmin", nGroupLen );

  // Explicit users.txt entries beat the implicit host default.
  char szSteamID[ 32 ];
  HL2SB_SteamIDString( pPlayer, szSteamID, sizeof( szSteamID ) );
  const char *pszName = pPlayer->GetPlayerName();

  FileHandle_t hFile = g_pFullFileSystem->Open( "settings/users.txt", "r", "GAME" );
  if ( hFile == FILESYSTEM_INVALID_HANDLE )
    return;

  int nSize = g_pFullFileSystem->Size( hFile );
  if ( nSize <= 0 || nSize > 64 * 1024 )
  {
    g_pFullFileSystem->Close( hFile );
    return;
  }

  char *pszBuf = new char[ nSize + 1 ];
  int nRead = g_pFullFileSystem->Read( pszBuf, nSize, hFile );
  g_pFullFileSystem->Close( hFile );
  pszBuf[ nRead ] = '\0';

  // Tiny walker for GMod's users.txt shape:
  //   "Users" { "superadmin" { "name" "STEAM_0:x:y" ... } "admin" { ... } }
  // with // and /* */ comments.  Depth 1 strings name a group; depth 2 string
  // pairs are (player name, id).  Name matches are the real path on this
  // non-Steam engine; the id match only ever hits the synthetic
  // STEAM_0:0:<entindex> fallbacks.
  int nDepth = 0;
  char szCurGroup[ 64 ] = "";
  char szTopString[ 64 ] = "";
  char szPending[ 64 ] = "";
  bool bHavePending = false;
  const char *p = pszBuf;

  while ( *p && nDepth != -1000 )
  {
    if ( p[ 0 ] == '/' && p[ 1 ] == '/' )
    {
      while ( *p && *p != '\n' )
        p++;
      continue;
    }
    if ( p[ 0 ] == '/' && p[ 1 ] == '*' )
    {
      p += 2;
      while ( *p && !( p[ 0 ] == '*' && p[ 1 ] == '/' ) )
        p++;
      if ( *p )
        p += 2;
      continue;
    }
    if ( *p == '"' )
    {
      p++;
      char szToken[ 64 ];
      size_t n = 0;
      while ( *p && *p != '"' )
      {
        if ( n < sizeof( szToken ) - 1 )
          szToken[ n++ ] = *p;
        p++;
      }
      if ( *p )
        p++;
      szToken[ n ] = '\0';

      if ( nDepth == 1 )
        Q_strncpy( szTopString, szToken, sizeof( szTopString ) );
      else if ( nDepth == 2 && szCurGroup[ 0 ] )
      {
        if ( !bHavePending )
        {
          Q_strncpy( szPending, szToken, sizeof( szPending ) );
          bHavePending = true;
        }
        else
        {
          // The pair is (name, id): a match on either assigns the group.
          if ( ( pszName && pszName[ 0 ] && Q_stricmp( szPending, pszName ) == 0 ) ||
               Q_stricmp( szPending, szSteamID ) == 0 )
          {
            Q_strncpy( szGroup, szCurGroup, nGroupLen );
            nDepth = -1000; // matched, stop walking
          }
          bHavePending = false;
        }
      }
      continue;
    }
    if ( *p == '{' )
    {
      nDepth++;
      if ( nDepth == 2 )
      {
        Q_strncpy( szCurGroup, szTopString, sizeof( szCurGroup ) );
        bHavePending = false;
      }
      p++;
      continue;
    }
    if ( *p == '}' )
    {
      if ( nDepth == 2 )
        szCurGroup[ 0 ] = '\0';
      nDepth--;
      p++;
      continue;
    }
    p++;
  }

  delete[] pszBuf;
#endif
}

static int CBasePlayer_GetUserGroup (lua_State *L) {
  CBasePlayer *pPlayer = luaL_checkplayer(L, 1);

  char szGroup[ 64 ];
  HL2SB_GetUserGroup( pPlayer, szGroup, sizeof( szGroup ) );
  lua_pushstring( L, szGroup );
  return 1;
}

static int CBasePlayer_IsUserGroup (lua_State *L) {
  CBasePlayer *pPlayer = luaL_checkplayer(L, 1);
  const char *pszGroup = luaL_checkstring(L, 2);

  char szGroup[ 64 ];
  HL2SB_GetUserGroup( pPlayer, szGroup, sizeof( szGroup ) );
  lua_pushboolean( L, Q_stricmp( szGroup, pszGroup ) == 0 );
  return 1;
}

static int CBasePlayer_IsSuperAdmin (lua_State *L) {
  CBasePlayer *pPlayer = luaL_checkplayer(L, 1);

  char szGroup[ 64 ];
  HL2SB_GetUserGroup( pPlayer, szGroup, sizeof( szGroup ) );
  lua_pushboolean( L, Q_stricmp( szGroup, "superadmin" ) == 0 );
  return 1;
}

static int CBasePlayer_IsAdmin (lua_State *L) {
  CBasePlayer *pPlayer = luaL_checkplayer(L, 1);

  char szGroup[ 64 ];
  HL2SB_GetUserGroup( pPlayer, szGroup, sizeof( szGroup ) );
  lua_pushboolean( L, Q_stricmp( szGroup, "admin" ) == 0 ||
                       Q_stricmp( szGroup, "superadmin" ) == 0 );
  return 1;
}

static int CBasePlayer_SteamID64 (lua_State *L) {
  CBasePlayer *pPlayer = luaL_checkplayer(L, 1);
  CSteamID steamID;
  char szID[ 32 ];

  if ( pPlayer->GetSteamID( &steamID ) && steamID.IsValid() )
  {
    Q_snprintf( szID, sizeof( szID ), "%llu", (unsigned long long)steamID.ConvertToUint64() );
  }
  else
  {
    // The individual-account base of the 64-bit id space, offset by the entity index.
    Q_snprintf( szID, sizeof( szID ), "%llu",
                76561197960265728ULL + (unsigned long long)pPlayer->entindex() );
  }

  lua_pushstring( L, szID );
  return 1;
}

// HL2SB GMod compat: Player:Ping().  GMod reads the ping its client player
// resource keeps per user id; the same answer comes from this fork's player
// resource on both realms (the server entity networks the smoothed ping, the
// client copy receives it through DT_PlayerResource).  Whole milliseconds,
// pushed as an integer so it formats as "17" instead of "17.0".
static int CBasePlayer_Ping (lua_State *L) {
  CBasePlayer *pPlayer = luaL_checkplayer(L, 1);
#ifdef CLIENT_DLL
  int nPing = g_PR ? g_PR->GetPing( pPlayer->entindex() ) : 0;
#else
  int nPing = g_pPlayerResource ? g_pPlayerResource->GetPing( pPlayer->entindex() ) : 0;
#endif
  lua_pushinteger( L, nPing );
  return 1;
}

// HL2SB GMod compat: Player:Frags() / Player:Deaths() -- the scoreboard's
// kill/death columns.  GMod's bindings read the engine's per-player counters;
// here those live on the server player entity, and reach the client through
// the networked player resource the same way the ping does.
static int CBasePlayer_Frags (lua_State *L) {
  CBasePlayer *pPlayer = luaL_checkplayer(L, 1);
#ifdef CLIENT_DLL
  lua_pushinteger( L, g_PR ? g_PR->GetFrags( pPlayer->entindex() ) : 0 );
#else
  lua_pushinteger( L, pPlayer->FragCount() );
#endif
  return 1;
}

static int CBasePlayer_Deaths (lua_State *L) {
  CBasePlayer *pPlayer = luaL_checkplayer(L, 1);
#ifdef CLIENT_DLL
  lua_pushinteger( L, g_PR ? g_PR->GetDeaths( pPlayer->entindex() ) : 0 );
#else
  lua_pushinteger( L, pPlayer->DeathCount() );
#endif
  return 1;
}

// HL2SB GMod compat: Player:AddFrags( count ) / Player:AddDeaths( count ).
// GMod's base gamemode scores every death through these: the victim always
// adds one death, a player attacker collects a frag and a suicide subtracts
// one.  The counters live on the server entity and network out through the
// player resource, so the client realm accepts the call and drops it.
#ifndef CLIENT_DLL
static int CBasePlayer_AddFrags (lua_State *L) {
  CBasePlayer *pPlayer = luaL_checkplayer(L, 1);
  pPlayer->IncrementFragCount( luaL_checkint(L, 2) );
  return 0;
}

static int CBasePlayer_AddDeaths (lua_State *L) {
  CBasePlayer *pPlayer = luaL_checkplayer(L, 1);
  pPlayer->IncrementDeathCount( luaL_checkint(L, 2) );
  return 0;
}
#endif

static int CBasePlayer_Team (lua_State *L) {
  CBasePlayer *player = luaL_checkplayer(L, 1);
#ifdef CLIENT_DLL
  if (!g_PR) return 0;
  lua_pushnumber(L, g_PR->GetTeam(player->entindex()));
#else
  lua_pushnumber(L, player->GetTeamNumber());
#endif
  return 1;
}

#ifdef CLIENT_DLL
// HL2SB GMod compat: Player:IsMuted() / SetMuted( mute ).  GMod keeps a local
// per-user mute list and pushes it to the engine as a voice ban mask; here the
// same local mute lives in the client's persistent voice ban list (the state
// GameUI's mute toggle drives).  SetPlayerBlockedState flips the stored ban
// and sends the refreshed voice mask, so the squelch also holds server side.
static int CBasePlayer_IsMuted (lua_State *L) {
  CBasePlayer *pPlayer = luaL_checkplayer(L, 1);

  lua_pushboolean( L, GetClientVoiceMgr()->IsPlayerBlocked( pPlayer->entindex() ) ? 1 : 0 );
  return 1;
}

static int CBasePlayer_SetMuted (lua_State *L) {
  CBasePlayer *pPlayer = luaL_checkplayer(L, 1);
  bool bMute = lua_toboolean( L, 2 ) ? true : false;

  if ( GetClientVoiceMgr()->IsPlayerBlocked( pPlayer->entindex() ) != bMute )
  {
    GetClientVoiceMgr()->SetPlayerBlockedState( pPlayer->entindex(), bMute );
  }
  return 0;
}
#endif


//-----------------------------------------------------------------------------
// Purpose: HL2SB - Player:IsValid()
//
// Players have their own metatable, so the Entity one added in
// lbaseentity_shared.cpp does not reach them -- and GMod's global IsValid()
// (lua/includes/util.lua:314) reads `object.IsValid`.  Without this method
// IsValid( <player> ) answered false, which made undo.lua's SetPlayer() and
// Finish() return early ("if ( !IsValid( ply ) ) then return end"), made
// Finish() reject every undo, and made the pickup HUD drop every pickup.
//-----------------------------------------------------------------------------
static int CBasePlayer_IsValid (lua_State *L) {
  lua_pushboolean(L, lua_toplayer(L, 1) != NULL);
  return 1;
}


// HL2SB GMod compat: GMod's Player:LagCompensation( bool ) - "enable/disable lag
// compensation for this player's traces".  On the server that is exactly the flag
// the engine already keeps (m_bLagCompensation, read by player_lagcompensation
// .cpp); C_BasePlayer has no such member, so the client side accepts the call and
// ignores it, which is what GMod's own client does with a stale flag.
//
// weapon_fists' DealDamage() opens with self.Owner:LagCompensation( true ) and
// threw "attempt to call a nil value (method 'LagCompensation')" on BOTH realms
// (1144 times in one session), which aborted the melee before TakeDamageInfo().
// It has to live in the shared library: the shared luaopen_CBasePlayer_shared()
// is what installs the metatable the entity actually uses.
static int CBasePlayer_LagCompensation (lua_State *L) {
  CBasePlayer *pPlayer = luaL_checkplayer(L, 1);

#ifndef CLIENT_DLL
  pPlayer->m_bLagCompensation = luaL_checkboolean(L, 2);
#else
  (void)pPlayer;
  luaL_checkboolean(L, 2);
#endif

  return 0;
}

static int CBasePlayer_SetVelocity (lua_State *L) {
  CBasePlayer *pPlayer = luaL_checkplayer( L, 1 );
  pPlayer->SetBaseVelocity( luaL_checkvector( L, 2 ) );
  return 0;
}

// GMod Player:SetAllowFullRotation / GetAllowFullRotation.
static int CBasePlayer_SetAllowFullRotation (lua_State *L) {
  CBasePlayer *pPlayer = luaL_checkplayer(L, 1);
  const bool bAllow = luaL_checkboolean(L, 2);
  lua_pushboolean( L, bAllow );
  lua_setfield( L, 1, "__hl2sb_allow_full_rotation" );
  return 0;
}

static int CBasePlayer_GetAllowFullRotation (lua_State *L) {
  lua_getfield( L, 1, "__hl2sb_allow_full_rotation" );
  if ( lua_isnil( L, -1 ) )
  {
    lua_pop( L, 1 );
    lua_pushboolean( L, true );
  }
  return 1;
}


/*
** HL2SB GMod compat: Player:KeyDown / KeyPressed / KeyReleased.
**
** weapon_nyangun's Reload() gates on `self.Owner:KeyPressed( IN_RELOAD )` and its
** Think() on KeyReleased( IN_ATTACK ) / KeyDown( IN_ATTACK ), and none of the
** three existed -- each call raised "attempt to call a nil value (method ...)",
** which aborted SWEP:Reload before it ever created the bomb entity.
**
** GMod semantics, straight off the members the engine already maintains on every
** client and server player (CBasePlayer::m_nButtons / m_afButtonPressed /
** m_afButtonReleased, refreshed from the usercmd in
** CBasePlayer::PhysicsSimulate -- baseplayer_shared.cpp:763):
**
**   KeyDown( k )     - the button is held right now
**   KeyPressed( k )  - it went down this frame
**   KeyReleased( k ) - it came up this frame
*/
static int CBasePlayer_KeyDown (lua_State *L) {
  CBasePlayer *pPlayer = luaL_checkplayer(L, 1);
  const int nKey = luaL_checkint(L, 2);
  lua_pushboolean(L, (pPlayer->m_nButtons & nKey) != 0);
  return 1;
}

static int CBasePlayer_KeyPressed (lua_State *L) {
  CBasePlayer *pPlayer = luaL_checkplayer(L, 1);
  const int nKey = luaL_checkint(L, 2);
  lua_pushboolean(L, (pPlayer->m_afButtonPressed & nKey) != 0);
  return 1;
}

static int CBasePlayer_KeyReleased (lua_State *L) {
  CBasePlayer *pPlayer = luaL_checkplayer(L, 1);
  const int nKey = luaL_checkint(L, 2);
  lua_pushboolean(L, (pPlayer->m_afButtonReleased & nKey) != 0);
  return 1;
}

// HL2SB GMod compat (2026-09-24): Player:KeyDownLast( key ) -- was the key down
// last frame.  Mirrors KeyDown/KeyPressed/KeyReleased above; m_afButtonLast is
// the frame-old button mask.
static int CBasePlayer_KeyDownLast (lua_State *L) {
  CBasePlayer *pPlayer = luaL_checkplayer(L, 1);
  const int nKey = luaL_checkint(L, 2);
  lua_pushboolean(L, (pPlayer->m_afButtonLast & nKey) != 0);
  return 1;
}

// HL2SB GMod compat (2026-09-24): Player:StripWeapon( class ) -- wiki
// Player:StripWeapon (SERVER realm): remove the weapon entity of this class
// (OnRemove fires, GetOwner is nil afterwards).  The engine's GetWeapon
// binding takes a SLOT index, not a class, and the sh_init.lua copy of this
// method sits behind the never-loaded gmod_compatibility/ folder pass -- so
// the class lookup happens here in C++ instead (Weapon_OwnsThisType) and the
// weapon is deleted.  C_BaseCombatWeapon has no Delete(), so server-only.
#ifndef CLIENT_DLL
static int CBasePlayer_StripWeapon (lua_State *L) {
  CBasePlayer *pPlayer = luaL_checkplayer(L, 1);
  const char *pszClass = luaL_checkstring(L, 2);
  CBaseCombatWeapon *pWeapon = pPlayer->Weapon_OwnsThisType( pszClass );
  if ( pWeapon != NULL )
  {
    pWeapon->Delete();
  }
  lua_pushboolean( L, pWeapon != NULL );
  return 1;
}
#endif

// Server tool settings belong to the requesting player's userinfo store.
// Client calls read the local console value.
static int CBasePlayer_GetInfo (lua_State *L) {
  CBasePlayer *pPlayer = luaL_checkplayer(L, 1);
  const char *pszName = luaL_checkstring(L, 2);
#ifdef GAME_DLL
  const char *pszValue = engine->GetClientConVarValue( pPlayer->entindex(), pszName );
  if ( pszValue && pszValue[0] )
  {
    lua_pushstring( L, pszValue );
  }
  else
  {
    // Lua-created tool cvars can be registered after the initial userinfo
    // batch. Keep the requesting-player value first, then use the local cvar
    // store as the Source/GMod fallback for that early window.
    ConVarRef ref( pszName, true );
    lua_pushstring( L, ref.IsValid() ? ref.GetString() : "" );
  }
#else
  ConVarRef ref( pszName, true );
  lua_pushstring( L, ref.IsValid() ? ref.GetString() : "" );
#endif
  return 1;
}

static int CBasePlayer_GetInfoNum (lua_State *L) {
  CBasePlayer *pPlayer = luaL_checkplayer(L, 1);
  const char *pszName = luaL_checkstring(L, 2);
  const float flDefault = (float)luaL_optnumber(L, 3, 0.0f);
#ifdef GAME_DLL
  if ( pPlayer->IsFakeClient() ) {
    lua_pushnumber( L, 0 );
    return 1;
  }
  const char *pszValue = engine->GetClientConVarValue( pPlayer->entindex(), pszName );
  if ( pszValue && pszValue[0] )
  {
    lua_pushnumber( L, (float)Q_atof(pszValue) );
  }
  else
  {
    ConVarRef ref( pszName, true );
    lua_pushnumber( L, ref.IsValid() ? ref.GetFloat() : flDefault );
  }
#else
  ConVarRef ref( pszName, true );
  lua_pushnumber( L, ref.IsValid() ? ref.GetFloat() : flDefault );
#endif
  return 1;
}

// ---------------------------------------------------------------------------
// HL2SB GMod compat (2026-09-22 physgun audit): the per-player frozen-object
// list.  GMod's Player:AddFrozenPhysicsObject / Player:PhysgunUnfreeze /
// Player:UnfreezePhysicsObjects and the physgun's R behaviours all run on one
// list per player; the physgun freeze path feeds it and reload drains it.
// SERVER only -- freezing is a server concept.
// ---------------------------------------------------------------------------
#ifndef CLIENT_DLL

struct hl2sb_frozenobject_t
{
	EHANDLE hEnt;                 // owner entity of the frozen body
	IPhysicsObject *pPhys;        // the frozen body
};

static CUtlVector< hl2sb_frozenobject_t > s_aFrozenObjects[ MAX_PLAYERS ];

// The physobj pointer dies with the entity that owns it, so an entry may only
// be touched while the entity is alive AND the body is still in its list
// (ragdolls rebuild/replace bodies).  Anything else is dropped, never touched.
static bool HL2SB_FrozenEntryValid( hl2sb_frozenobject_t &entry )
{
	CBaseEntity *pEnt = entry.hEnt;
	if ( pEnt == NULL || entry.pPhys == NULL )
		return false;

	IPhysicsObject *pList[VPHYSICS_MAX_OBJECT_LIST_COUNT];
	int count = pEnt->VPhysicsGetObjectList( pList, ARRAYSIZE(pList) );
	for ( int i = 0; i < count; ++i )
	{
		if ( pList[i] == entry.pPhys )
			return true;
	}
	return false;
}

static void HL2SB_FrozenUnfreezeEntry( hl2sb_frozenobject_t &entry )
{
	entry.pPhys->EnableMotion( true );
	entry.pPhys->Wake();
}

// One entry per (entity, body) pair.
void HL2SB_PlayerAddFrozenObject( CBasePlayer *pPlayer, CBaseEntity *pEnt, IPhysicsObject *pPhys )
{
	if ( pPlayer == NULL || pEnt == NULL || pPhys == NULL )
		return;

	int iSlot = pPlayer->entindex();
	if ( iSlot < 0 || iSlot >= MAX_PLAYERS )
		return;

	CUtlVector< hl2sb_frozenobject_t > &list = s_aFrozenObjects[ iSlot ];
	for ( int i = 0; i < list.Count(); ++i )
	{
		if ( list[i].hEnt == pEnt && list[i].pPhys == pPhys )
			return;
	}

	hl2sb_frozenobject_t entry;
	entry.hEnt = pEnt;
	entry.pPhys = pPhys;
	list.AddToTail( entry );
}

// Unfreeze every frozen body of the entity under the player's crosshair.
// Returns how many bodies were unfrozen (GMod: Player:PhysgunUnfreeze).
int HL2SB_PlayerUnfreezeAimed( CBasePlayer *pPlayer )
{
	if ( pPlayer == NULL )
		return 0;

	int iSlot = pPlayer->entindex();
	if ( iSlot < 0 || iSlot >= MAX_PLAYERS )
		return 0;

	Vector vForward;
	pPlayer->EyeVectors( &vForward, NULL, NULL );
	Vector vecEye = pPlayer->EyePosition();
	Vector vecEnd = vecEye + vForward * MAX_TRACE_LENGTH;

	trace_t tr;
	UTIL_TraceLine( vecEye, vecEnd, MASK_SHOT, pPlayer, COLLISION_GROUP_NONE, &tr );
	if ( !tr.DidHitNonWorldEntity() || tr.m_pEnt == NULL )
		return 0;

	CBaseEntity *pTarget = tr.m_pEnt;
	CUtlVector< hl2sb_frozenobject_t > &list = s_aFrozenObjects[ iSlot ];

	int nUnfrozen = 0;
	for ( int i = list.Count() - 1; i >= 0; --i )
	{
		if ( list[i].hEnt != pTarget )
			continue;

		if ( HL2SB_FrozenEntryValid( list[i] ) )
		{
			HL2SB_FrozenUnfreezeEntry( list[i] );
			++nUnfrozen;
		}
		list.Remove( i );
	}
	return nUnfrozen;
}

// Unfreeze everything this player froze. Returns the body count
// (GMod: Player:UnfreezePhysicsObjects / double-tap R).
int HL2SB_PlayerUnfreezeAll( CBasePlayer *pPlayer )
{
	if ( pPlayer == NULL )
		return 0;

	int iSlot = pPlayer->entindex();
	if ( iSlot < 0 || iSlot >= MAX_PLAYERS )
		return 0;

	CUtlVector< hl2sb_frozenobject_t > &list = s_aFrozenObjects[ iSlot ];

	int nUnfrozen = 0;
	for ( int i = list.Count() - 1; i >= 0; --i )
	{
		if ( HL2SB_FrozenEntryValid( list[i] ) )
		{
			HL2SB_FrozenUnfreezeEntry( list[i] );
			++nUnfrozen;
		}
		list.Remove( i );
	}
	return nUnfrozen;
}

// Player:AddFrozenPhysicsObject( ent, physobj ) -- records a frozen body.
static int CBasePlayer_AddFrozenPhysicsObject (lua_State *L) {
  CBasePlayer *pPlayer = luaL_checkplayer(L, 1);
  CBaseEntity *pEnt = luaL_checkentity(L, 2);
  IPhysicsObject *pPhys = luaL_checkphysicsobject(L, 3);
  HL2SB_PlayerAddFrozenObject( pPlayer, pEnt, pPhys );
  return 0;
}

// Player:PhysgunUnfreeze() -> number -- unfreezes the frozen bodies of the
// entity under the crosshair, returns the count (the single-R behaviour).
static int CBasePlayer_PhysgunUnfreeze (lua_State *L) {
  CBasePlayer *pPlayer = luaL_checkplayer(L, 1);
  lua_pushinteger( L, HL2SB_PlayerUnfreezeAimed( pPlayer ) );
  return 1;
}

// Player:UnfreezePhysicsObjects() -- unfreezes everything this player froze
// (the double-R behaviour).
static int CBasePlayer_UnfreezePhysicsObjects (lua_State *L) {
  CBasePlayer *pPlayer = luaL_checkplayer(L, 1);
  HL2SB_PlayerUnfreezeAll( pPlayer );
  return 0;
}

// Player:PickupObject( ent ) -- start a +use carry of ent.  The reference Lua
// binding hands the entity straight to the pickup controller, so the walking
// pickup's mass/size limit does not apply here.
extern void PlayerPickupObject( CBasePlayer *pPlayer, CBaseEntity *pObject );
static int CBasePlayer_PickupObject (lua_State *L) {
  CBasePlayer *pPlayer = luaL_checkplayer(L, 1);
  CBaseEntity *pEnt = luaL_checkentity(L, 2);
  PlayerPickupObject( pPlayer, pEnt );
  return 0;
}

// Player:DropObject uses the same deferred drop path as engine callers.
static int CBasePlayer_DropObject (lua_State *L) {
  CBasePlayer *pPlayer = luaL_checkplayer(L, 1);
  CBaseEntity *pEnt = luaL_checkentity(L, 2);
  pPlayer->ForceDropOfCarriedPhysObjects( pEnt );
  return 0;
}

#endif // CLIENT_DLL


// Driving an arbitrary entity is separate from occupying a vehicle seat.
#ifdef CLIENT_DLL
#define CPlayerDriveState C_PlayerDriveState
#endif
class CPlayerDriveState : public CBaseEntity
{
public:
    DECLARE_CLASS(CPlayerDriveState, CBaseEntity);
    DECLARE_NETWORKCLASS();
#ifndef CLIENT_DLL
    DECLARE_DATADESC();
    int UpdateTransmitState() { return SetTransmitState(FL_EDICT_ALWAYS); }
#else
    void OnDataChanged(DataUpdateType_t type);
    CHandle<CBaseEntity> m_lastDriven;
    CHandle<CBaseEntity> m_predictionEntity;
#endif
    CPlayerDriveState();
    ~CPlayerDriveState();
    CNetworkHandle(CBasePlayer, m_hPlayer);
    CNetworkHandle(CBaseEntity, m_hDriven);
    CNetworkHandle(CBaseEntity, m_hHoveredWidget);
    CNetworkHandle(CBaseEntity, m_hPressedWidget);
    CNetworkVar(int, m_iMode);
};

IMPLEMENT_NETWORKCLASS_ALIASED(PlayerDriveState, DT_PlayerDriveState)
BEGIN_NETWORK_TABLE(CPlayerDriveState, DT_PlayerDriveState)
#ifdef CLIENT_DLL
    RecvPropEHandle(RECVINFO(m_hPlayer)),
    RecvPropEHandle(RECVINFO(m_hDriven)),
    RecvPropEHandle(RECVINFO(m_hHoveredWidget)),
    RecvPropEHandle(RECVINFO(m_hPressedWidget)),
    RecvPropInt(RECVINFO(m_iMode)),
#else
    SendPropEHandle(SENDINFO(m_hPlayer)),
    SendPropEHandle(SENDINFO(m_hDriven)),
    SendPropEHandle(SENDINFO(m_hHoveredWidget)),
    SendPropEHandle(SENDINFO(m_hPressedWidget)),
    SendPropInt(SENDINFO(m_iMode), 15),
#endif
END_NETWORK_TABLE()
#ifndef CLIENT_DLL
LINK_ENTITY_TO_CLASS(hl2sb_player_drive_state, CPlayerDriveState);
BEGIN_DATADESC(CPlayerDriveState)
    DEFINE_FIELD(m_hPlayer, FIELD_EHANDLE),
    DEFINE_FIELD(m_hDriven, FIELD_EHANDLE),
    DEFINE_FIELD(m_hHoveredWidget, FIELD_EHANDLE),
    DEFINE_FIELD(m_hPressedWidget, FIELD_EHANDLE),
    DEFINE_FIELD(m_iMode, FIELD_INTEGER),
END_DATADESC()
#endif

static CUtlVector<CPlayerDriveState *> s_PlayerDriveStates;

CPlayerDriveState::CPlayerDriveState()
{
    m_hPlayer = NULL;
    m_hDriven = NULL;
    m_hHoveredWidget = NULL;
    m_hPressedWidget = NULL;
    m_iMode = 0;
    s_PlayerDriveStates.AddToTail(this);
}

CPlayerDriveState::~CPlayerDriveState()
{
#ifdef CLIENT_DLL
    if (m_predictionEntity && m_predictionEntity->GetPredictable())
        m_predictionEntity->ShutdownPredictable();
#endif
    s_PlayerDriveStates.FindAndRemove(this);
}

static void PlayerDriveCallback(const char *name, CBaseEntity *entity, CBasePlayer *player)
{
    if (L == NULL || entity == NULL || player == NULL)
        return;
    BEGIN_LUA_CALL_HOOK(name);
        CBaseEntity::PushLuaInstanceSafe(L, entity);
        lua_pushplayer(L, player);
    END_LUA_CALL_HOOK(2, 0);
}

#ifdef CLIENT_DLL
static void UpdatePlayerWidgetState( CBasePlayer *, CBaseEntity *, CBaseEntity * );
void CPlayerDriveState::OnDataChanged(DataUpdateType_t type)
{
    BaseClass::OnDataChanged(type);
    UpdatePlayerWidgetState( m_hPlayer.Get(), m_hHoveredWidget.Get(), m_hPressedWidget.Get() );
    CBaseEntity *oldEntity = m_lastDriven.Get();
    CBaseEntity *newEntity = m_hDriven.Get();
    CBaseEntity *localEntity = m_hPlayer.Get() == C_BasePlayer::GetLocalPlayer() ? newEntity : NULL;
    if (m_predictionEntity.Get() != localEntity)
    {
        if (m_predictionEntity && m_predictionEntity->GetPredictable())
            m_predictionEntity->ShutdownPredictable();
        m_predictionEntity = NULL;
        if (localEntity && !localEntity->GetPredictable())
        {
            localEntity->InitPredictable();
            m_predictionEntity = localEntity;
        }
    }
    if (oldEntity == newEntity)
        return;
    m_lastDriven = newEntity;
    PlayerDriveCallback("EndEntityDriving", oldEntity, m_hPlayer.Get());
    PlayerDriveCallback("StartEntityDriving", newEntity, m_hPlayer.Get());
}
#endif

static CPlayerDriveState *PlayerDriveState(CBasePlayer *player, bool create = false)
{
    for (int i = 0; i < s_PlayerDriveStates.Count(); ++i)
        if (s_PlayerDriveStates[i]->m_hPlayer.Get() == player)
            return s_PlayerDriveStates[i];
#ifndef CLIENT_DLL
    if (create)
    {
        CPlayerDriveState *state = static_cast<CPlayerDriveState *>(CreateEntityByName("hl2sb_player_drive_state"));
        if (state != NULL)
        {
            state->m_hPlayer = player;
            state->SetOwnerEntity(player);
            state->SetParent(player);
            state->SetLocalOrigin(vec3_origin);
            DispatchSpawn(state);
        }
        return state;
    }
#endif
    return NULL;
}

// HL2SB: force-stop a player's drive session server-side -- the same store
// the binding performs for (nil), fired from the player lifecycle events
// (death/respawn) and wherever the driven entity goes away.  Firing the
// EndEntityDriving callback matters: it is what reaches
// GM:EndEntityDriving -> drive.End, which restores the saved eye angles,
// observer mode, view entity and world model -- without it a dead or
// respawned player keeps the wedged drive view.  The Lua-side
// m_CurrentDriverMethod clears itself on the next IsDrivingEntity check.
void HL2SB_DriveStopPlayer( CBasePlayer *player )
{
#ifdef CLIENT_DLL
    (void)player;
#else
    if ( player == NULL )
        return;

    CPlayerDriveState *state = PlayerDriveState( player, false );
    if ( state == NULL )
        return;

    CBaseEntity *oldEntity = state->m_hDriven.Get();
    if ( oldEntity != NULL )
    {
        PlayerDriveCallback( "EndEntityDriving", oldEntity, player );
        state->m_hDriven = NULL;
    }
    state->m_iMode = 0;
#endif
}

void HL2SB_DriveEntityRemoved(CBaseEntity *entity)
{
#ifndef CLIENT_DLL
    CUtlVector<CHandle<CBasePlayer> > players;
    for (int i = 0; i < s_PlayerDriveStates.Count(); ++i)
    {
        CPlayerDriveState *state = s_PlayerDriveStates[i];
        if (state->m_hDriven.Get() == entity || state->m_hPlayer.Get() == entity)
        {
            CHandle<CBasePlayer> player;
            player = state->m_hPlayer.Get();
            players.AddToTail(player);
        }
    }
    for (int i = 0; i < players.Count(); ++i)
        HL2SB_DriveStopPlayer(players[i].Get());
#endif
}

static int CBasePlayer_SetDrivingEntity(lua_State *L)
{
#ifndef CLIENT_DLL
    CBasePlayer *player = luaL_checkplayer(L, 1);
    CBaseEntity *entity = lua_toentity(L, 2);
    const int mode = entity ? luaL_optint(L, 3, 0) : 0;
    CPlayerDriveState *state = PlayerDriveState(player, entity != NULL);
    if (state == NULL)
        return 0;
    CBaseEntity *oldEntity = state->m_hDriven.Get();
    if (oldEntity != entity)
    {
        PlayerDriveCallback("EndEntityDriving", oldEntity, player);
        state->m_hDriven = entity;
        PlayerDriveCallback("StartEntityDriving", entity, player);
    }
    state->m_iMode = mode;
#endif
    return 0;
}

CBaseEntity *HL2SB_GetPlayerDrivingEntity(CBasePlayer *player)
{
    CPlayerDriveState *state = PlayerDriveState(player);
    return state ? state->m_hDriven.Get() : NULL;
}

static int CBasePlayer_GetDrivingEntity(lua_State *L)
{
    CBaseEntity::PushLuaInstanceSafe(L, HL2SB_GetPlayerDrivingEntity(luaL_checkplayer(L, 1)));
    return 1;
}

static int CBasePlayer_GetDrivingMode(lua_State *L)
{
    CPlayerDriveState *state = PlayerDriveState(luaL_checkplayer(L, 1));
    lua_pushinteger(L, state ? state->m_iMode.Get() : 0);
    return 1;
}

static int CBasePlayer_IsDrivingEntity(lua_State *L)
{
    CPlayerDriveState *state = PlayerDriveState(luaL_checkplayer(L, 1));
    lua_pushboolean(L, state && state->m_hDriven.Get() != NULL);
    return 1;
}

static int CBasePlayer_EnterVehicle(lua_State *L)
{
#ifndef CLIENT_DLL
    CBasePlayer *player = luaL_checkplayer(L, 1);
    CBaseEntity *entity = lua_toentity(L, 2);
    if (entity && entity->GetServerVehicle())
        player->EnterVehicle(entity->GetServerVehicle(), VEHICLE_ROLE_DRIVER);
#endif
    return 0;
}

static int CBasePlayer_DrawWorldModel(lua_State *L)
{
#ifndef CLIENT_DLL
    CBaseCombatWeapon *weapon = luaL_checkplayer(L, 1)->GetActiveWeapon();
    if (weapon != NULL)
    {
        if (lua_toboolean(L, 2)) weapon->RemoveEffects(EF_NODRAW);
        else weapon->AddEffects(EF_NODRAW);
    }
#endif
    return 0;
}

struct PlayerWidgetState
{
  EHANDLE owner;
  EHANDLE hovered;
  EHANDLE pressed;
};

static PlayerWidgetState *GetPlayerWidgetState( CBasePlayer *player, bool create )
{
  static CUtlVector<PlayerWidgetState> states;
  for ( int i = states.Count() - 1; i >= 0; --i )
  {
    if ( states[i].owner.Get() == player )
      return &states[i];
    if ( states[i].owner.Get() == NULL )
      states.FastRemove( i );
  }
  if ( !create )
    return NULL;
  const int index = states.AddToTail();
  states[index].owner = player;
  states[index].hovered = NULL;
  states[index].pressed = NULL;
  return &states[index];
}

static int CBasePlayer_SetHoveredWidget( lua_State *L )
{
  CBasePlayer *player = luaL_checkplayer( L, 1 );
  CBaseEntity *widget = lua_toentity( L, 2 );
  GetPlayerWidgetState( player, true )->hovered = widget;
#ifndef CLIENT_DLL
  CPlayerDriveState *state = PlayerDriveState( player, widget != NULL );
  if ( state ) state->m_hHoveredWidget = widget;
#endif
  return 0;
}

static int CBasePlayer_GetHoveredWidget( lua_State *L )
{
  PlayerWidgetState *state = GetPlayerWidgetState( luaL_checkplayer( L, 1 ), false );
  CBaseEntity::PushLuaInstanceSafe( L, state ? state->hovered.Get() : NULL );
  return 1;
}

static int CBasePlayer_SetPressedWidget( lua_State *L )
{
  CBasePlayer *player = luaL_checkplayer( L, 1 );
  CBaseEntity *widget = lua_toentity( L, 2 );
  GetPlayerWidgetState( player, true )->pressed = widget;
#ifndef CLIENT_DLL
  CPlayerDriveState *state = PlayerDriveState( player, widget != NULL );
  if ( state ) state->m_hPressedWidget = widget;
#endif
  return 0;
}

static int CBasePlayer_GetPressedWidget( lua_State *L )
{
  PlayerWidgetState *state = GetPlayerWidgetState( luaL_checkplayer( L, 1 ), false );
  CBaseEntity::PushLuaInstanceSafe( L, state ? state->pressed.Get() : NULL );
  return 1;
}

bool HL2SB_PlayerCanAttack( CBasePlayer *player )
{
  PlayerWidgetState *state = GetPlayerWidgetState( player, false );
  return state == NULL || state->pressed.Get() == NULL;
}

#ifdef CLIENT_DLL
static void UpdatePlayerWidgetState( CBasePlayer *player, CBaseEntity *hovered, CBaseEntity *pressed )
{
  if ( player == NULL ) return;
  PlayerWidgetState *state = GetPlayerWidgetState( player, true );
  state->hovered = hovered;
  state->pressed = pressed;
}
#endif

static int CBasePlayer_SetAvoidPlayers( lua_State *L ) {
  CHL2MP_Player *player = dynamic_cast<CHL2MP_Player *>( luaL_checkplayer( L, 1 ) );
  if ( !player ) return luaL_argerror( L, 1, "player expected" );
  player->HL2SB_SetAvoidPlayers( lua_toboolean( L, 2 ) != 0 );
  return 0;
}

static int CBasePlayer_GetAvoidPlayers( lua_State *L ) {
  CHL2MP_Player *player = dynamic_cast<CHL2MP_Player *>( luaL_checkplayer( L, 1 ) );
  if ( !player ) return luaL_argerror( L, 1, "player expected" );
  lua_pushboolean( L, player->HL2SB_GetAvoidPlayers() );
  return 1;
}

static int CBasePlayer_SetNoCollideWithTeammates( lua_State *L ) {
  CHL2MP_Player *player = dynamic_cast<CHL2MP_Player *>( luaL_checkplayer( L, 1 ) );
  if ( !player ) return luaL_argerror( L, 1, "player expected" );
  player->HL2SB_SetNoCollideWithTeammates( lua_toboolean( L, 2 ) != 0 );
  return 0;
}

static int CBasePlayer_GetNoCollideWithTeammates( lua_State *L ) {
  CHL2MP_Player *player = dynamic_cast<CHL2MP_Player *>( luaL_checkplayer( L, 1 ) );
  if ( !player ) return luaL_argerror( L, 1, "player expected" );
  lua_pushboolean( L, player->HL2SB_GetNoCollideWithTeammates() );
  return 1;
}

static const luaL_Reg CBasePlayermeta[] = {
  {"SetAvoidPlayers", CBasePlayer_SetAvoidPlayers},
  {"GetAvoidPlayers", CBasePlayer_GetAvoidPlayers},
  {"SetNoCollideWithTeammates", CBasePlayer_SetNoCollideWithTeammates},
  {"GetNoCollideWithTeammates", CBasePlayer_GetNoCollideWithTeammates},
  {"SetHoveredWidget", CBasePlayer_SetHoveredWidget},
  {"GetHoveredWidget", CBasePlayer_GetHoveredWidget},
  {"SetPressedWidget", CBasePlayer_SetPressedWidget},
  {"GetPressedWidget", CBasePlayer_GetPressedWidget},
#ifndef CLIENT_DLL
  {"EnterVehicle", CBasePlayer_EnterVehicle},
#endif
  {"SetDrivingEntity", CBasePlayer_SetDrivingEntity},
  {"GetDrivingEntity", CBasePlayer_GetDrivingEntity},
  {"GetDrivingMode", CBasePlayer_GetDrivingMode},
  {"IsDrivingEntity", CBasePlayer_IsDrivingEntity},
  {"DrawWorldModel", CBasePlayer_DrawWorldModel},
  {"LagCompensation", CBasePlayer_LagCompensation},
  {"SetVelocity", CBasePlayer_SetVelocity},
  {"SetAllowFullRotation", CBasePlayer_SetAllowFullRotation},
  {"GetAllowFullRotation", CBasePlayer_GetAllowFullRotation},
  {"GetInfo", CBasePlayer_GetInfo},
  {"IsListenServerHost", CBasePlayer_IsListenServerHost},
  {"ConCommand", CBasePlayer_ConCommand},
  {"GetInfoNum", CBasePlayer_GetInfoNum},
  {"IsValid", CBasePlayer_IsValid},
  {"AbortReload", CBasePlayer_AbortReload},
  {"AddToPlayerSimulationList", CBasePlayer_AddToPlayerSimulationList},
  {"ClearZoomOwner", CBasePlayer_ClearZoomOwner},
  {"CurrentCommandNumber", CBasePlayer_CurrentCommandNumber},
  {"GetCurrentCommand", CBasePlayer_GetCurrentCommand},
#ifndef CLIENT_DLL
  {"SelectWeapon", CBasePlayer_SelectWeapon},
#endif
  {"DoMuzzleFlash", CBasePlayer_DoMuzzleFlash},
  {"MuzzleFlash", CBasePlayer_MuzzleFlash},
  {"ExitLadder", CBasePlayer_ExitLadder},
  {"EyeAngles", CBasePlayer_EyeAngles},
  {"EyePosition", CBasePlayer_EyePosition},
  // HL2SB: GMod's public spelling (wiki: Entity:EyePos, inherited by Player).
  // Same reason as the entity metatable: Team Sandbox published only the C++
  // name, so ply:EyePos() was nil for every GMod script.
  {"EyePos", CBasePlayer_EyePosition},
  {"EyePositionAndVectors", CBasePlayer_EyePositionAndVectors},
  {"EyeVectors", CBasePlayer_EyeVectors},
  {"FindUseEntity", CBasePlayer_FindUseEntity},
  {"GetActiveWeapon", CBasePlayer_GetActiveWeapon},
  {"GetWeapons", CBasePlayer_GetWeapons},
  {"GetAmmoCount", CBasePlayer_GetAmmoCount},
  {"GetAmmo", CBasePlayer_GetAmmo},
  {"GetRagdollEntity", CBasePlayer_GetRagdollEntity},
#ifndef CLIENT_DLL
  // HL2SB GMod compat (2026-09-22 physgun audit): the per-player frozen list.
  {"AddFrozenPhysicsObject", CBasePlayer_AddFrozenPhysicsObject},
  {"PhysgunUnfreeze", CBasePlayer_PhysgunUnfreeze},
  {"UnfreezePhysicsObjects", CBasePlayer_UnfreezePhysicsObjects},
#endif
  {"GetAutoaimVector", CBasePlayer_GetAutoaimVector},
  {"GetShootPos", CBasePlayer_GetShootPos},
  {"GetAimVector", CBasePlayer_GetAimVector},
  // HL2SB GMod compat: Player:Name() / GetVehicle() / KillSilent()
  // (see the definitions).
  {"Name", CBasePlayer_Name},
  {"GetVehicle", CBasePlayer_GetVehicle},
  {"KillSilent", CBasePlayer_KillSilent},
  {"Kill", CBasePlayer_Kill},
#ifndef CLIENT_DLL
  {"SetTeam", CBasePlayer_SetTeam},
#endif
  {"GetEyeTrace", CBasePlayer_GetEyeTrace},
  {"GetEyeTraceNoCursor", CBasePlayer_GetEyeTraceNoCursor},
  {"GetBonusChallenge", CBasePlayer_GetBonusChallenge},
  {"GetBonusProgress", CBasePlayer_GetBonusProgress},
  {"GetDeathTime", CBasePlayer_GetDeathTime},
  {"GetDefaultFOV", CBasePlayer_GetDefaultFOV},
  {"GetEFNoInterpParity", CBasePlayer_GetEFNoInterpParity},
  {"GetFOV", CBasePlayer_GetFOV},
  {"GetFOVDistanceAdjustFactor", CBasePlayer_GetFOVDistanceAdjustFactor},
  {"GetFOVTime", CBasePlayer_GetFOVTime},
  {"GetHealth", CBasePlayer_GetHealth},
  // HL2SB GMod compat: armor accessors (tarkov_hud) -- GMod's spellings plus
  // the GetArmor alias some addons use.
  {"Armor", CBasePlayer_Armor},
  {"GetArmor", CBasePlayer_Armor},
  {"SetArmor", CBasePlayer_SetArmor},
  {"GetMaxArmor", CBasePlayer_GetMaxArmor},
  {"SetMaxArmor", CBasePlayer_SetMaxArmor},
  {"GetImpulse", CBasePlayer_GetImpulse},
  {"GetLaggedMovementValue", CBasePlayer_GetLaggedMovementValue},
  {"GetLastKnownPlaceName", CBasePlayer_GetLastKnownPlaceName},
  {"GetNextAttack", CBasePlayer_GetNextAttack},
  {"GetObserverMode", CBasePlayer_GetObserverMode},
  {"SetObserverMode", CBasePlayer_SetObserverMode},
  {"GetObserverTarget", CBasePlayer_GetObserverTarget},
#ifndef CLIENT_DLL
  {"Spectate", CBasePlayer_Spectate},
  {"SpectateEntity", CBasePlayer_SpectateEntity},
  {"UnSpectate", CBasePlayer_UnSpectate},
  {"ExitVehicle", CBasePlayer_ExitVehicle},
#endif
  {"GetVehicleEntity", CBasePlayer_GetVehicleEntity},
  {"GetOffset_m_Local", CBasePlayer_GetOffset_m_Local},
  {"GetPlayerLocalData", CBasePlayer_GetPlayerLocalData},
  {"GetPlayerMaxs", CBasePlayer_GetPlayerMaxs},
  {"GetPlayerMins", CBasePlayer_GetPlayerMins},
  {"GetPlayerName", CBasePlayer_GetPlayerName},
  {"GetPreviouslyPredictedOrigin", CBasePlayer_GetPreviouslyPredictedOrigin},
  {"GetPunchAngle", CBasePlayer_GetPunchAngle},
  {"GetStepSoundCache", CBasePlayer_GetStepSoundCache},
  {"GetStepSoundVelocities", CBasePlayer_GetStepSoundVelocities},
  {"GetSwimSoundTime", CBasePlayer_GetSwimSoundTime},
  {"GetTimeBase", CBasePlayer_GetTimeBase},
  {"GetTracerType", CBasePlayer_GetTracerType},
  {"GetUseEntity", CBasePlayer_GetUseEntity},
  {"GetUserID", CBasePlayer_GetUserID},
  {"GetPlayerColor", CBasePlayer_GetPlayerColor},
  {"SetPlayerColor", CBasePlayer_SetPlayerColor},
  {"GetWeaponColor", CBasePlayer_GetWeaponColor},
  {"SetWeaponColor", CBasePlayer_SetWeaponColor},
  {"SteamID", CBasePlayer_SteamID},
  {"SteamID64", CBasePlayer_SteamID64},
  {"AccountID", CBasePlayer_AccountID},
  {"IsAdmin", CBasePlayer_IsAdmin},
  {"IsSuperAdmin", CBasePlayer_IsSuperAdmin},
  {"IsUserGroup", CBasePlayer_IsUserGroup},
  {"GetUserGroup", CBasePlayer_GetUserGroup},

  {"GetViewModel", CBasePlayer_GetViewModel},
  {"GetWaterJumpTime", CBasePlayer_GetWaterJumpTime},
  {"HasWeapon", CBasePlayer_HasWeapon},
  {"GetWeapon", CBasePlayer_GetWeapon},
  {"HintMessage", CBasePlayer_HintMessage},
  //{"IncrementEFNoInterpParity", CBasePlayer_IncrementEFNoInterpParity},
  {"IsBot", CBasePlayer_IsBot},
  {"IsHLTV", CBasePlayer_IsHLTV},
  {"IsInAVehicle", CBasePlayer_IsInAVehicle},
  // HL2SB (2026-09-27): GMod spells it InVehicle() - the ported base-gamemode
  // animations.lua uses ply:InVehicle() throughout.
  {"InVehicle", CBasePlayer_IsInAVehicle},
#ifndef CLIENT_DLL
  {"PickupObject", CBasePlayer_PickupObject},
  {"DropObject", CBasePlayer_DropObject},
#endif
  {"IsObserver", CBasePlayer_IsObserver},
  {"IsPlayer", CBasePlayer_IsPlayer},
  {"IsPlayerUnderwater", CBasePlayer_IsPlayerUnderwater},
  {"IsSuitEquipped", CBasePlayer_IsSuitEquipped},
  {"IsUseableEntity", CBasePlayer_IsUseableEntity},
  {"ItemPostFrame", CBasePlayer_ItemPostFrame},
  {"ItemPreFrame", CBasePlayer_ItemPreFrame},
  // HL2SB GMod compat: Player:KeyDown / KeyPressed / KeyReleased.
  {"KeyDown", CBasePlayer_KeyDown},
  {"KeyPressed", CBasePlayer_KeyPressed},
  {"KeyReleased", CBasePlayer_KeyReleased},
  {"KeyDownLast", CBasePlayer_KeyDownLast},
#ifndef CLIENT_DLL
  {"StripWeapon", CBasePlayer_StripWeapon},
#endif
  {"LeaveVehicle", CBasePlayer_LeaveVehicle},
  {"LocalEyeAngles", CBasePlayer_LocalEyeAngles},
  {"MaxSpeed", CBasePlayer_MaxSpeed},
  {"MyCombatCharacterPointer", CBasePlayer_MyCombatCharacterPointer},
  {"OnRestore", CBasePlayer_OnRestore},
  {"PhysicsSimulate", CBasePlayer_PhysicsSimulate},
  {"PhysicsSolidMaskForEntity", CBasePlayer_PhysicsSolidMaskForEntity},
  {"PlayerUse", CBasePlayer_PlayerUse},
  {"PlayStepSound", CBasePlayer_PlayStepSound},
  {"PostThink", CBasePlayer_PostThink},
  {"PreThink", CBasePlayer_PreThink},
  {"RemoveAllAmmo", CBasePlayer_RemoveAllAmmo},
  {"RemoveAmmo", CBasePlayer_RemoveAmmo},
  {"RemoveFromPlayerSimulationList", CBasePlayer_RemoveFromPlayerSimulationList},
  {"ResetAutoaim", CBasePlayer_ResetAutoaim},
  {"ResetObserverMode", CBasePlayer_ResetObserverMode},
  {"SelectItem", CBasePlayer_SelectItem},
  {"SelectLastItem", CBasePlayer_SelectLastItem},
  {"SetAmmoCount", CBasePlayer_SetAmmoCount},
  // HL2SB GMod compat: Player:SetAmmo / Nick / GetViewPunchAngles /
  // SetViewPunchAngles / SetCanZoom / GetCanZoom -- GMod's spellings for what
  // this fork already ships under engine names (SetAmmoCount, GetPlayerName,
  // the punch angle pair).  hl1sweps uses all of them on the fire path.
  {"SetAmmo", CBasePlayer_SetAmmo},
  {"Nick", CBasePlayer_GetPlayerName},
  // HL2SB GMod compat: Player:Ping / Team / Frags / Deaths -- the scoreboard's
  // K/D/ping row (cl_scoreboard.lua) reads them; AddFrags/AddDeaths are what
  // GMod's base gamemode scores deaths with.
  {"Ping", CBasePlayer_Ping},
  {"Team", CBasePlayer_Team},
  {"Frags", CBasePlayer_Frags},
  {"Deaths", CBasePlayer_Deaths},
#ifndef CLIENT_DLL
  {"AddFrags", CBasePlayer_AddFrags},
  {"AddDeaths", CBasePlayer_AddDeaths},
#endif
  {"GetViewPunchAngles", CBasePlayer_GetPunchAngle},
  {"SetViewPunchAngles", CBasePlayer_SetPunchAngle},
  {"SetCanZoom", CBasePlayer_SetCanZoom},
  {"GetCanZoom", CBasePlayer_GetCanZoom},
  {"SetAnimation", CBasePlayer_SetAnimation},
  {"SetAnimationExtension", CBasePlayer_SetAnimationExtension},
  // HL2SB (2026-09-22): GMod names -- cf_beast's weapon base calls both.
  {"Crouching", CBasePlayer_Crouching},
  // HL2SB GMod compat: Player:IsSprinting (tarkov_hud).
  {"IsSprinting", CBasePlayer_IsSprinting},
  {"DoAnimationEvent", CBasePlayer_DoAnimationEvent},
  {"SetBloodColor", CBasePlayer_SetBloodColor},
  {"SetFOV", CBasePlayer_SetFOV},
  {"SetLadderNormal", CBasePlayer_SetLadderNormal},
  {"SetMaxSpeed", CBasePlayer_SetMaxSpeed},
  {"GetMaxSpeed", CBasePlayer_GetMaxSpeed},
  {"SetNextAttack", CBasePlayer_SetNextAttack},
  {"SetPlayerLocalData", CBasePlayer_SetPlayerLocalData},
  {"SetPlayerUnderwater", CBasePlayer_SetPlayerUnderwater},
  {"SetPreviouslyPredictedOrigin", CBasePlayer_SetPreviouslyPredictedOrigin},
  {"SetPunchAngle", CBasePlayer_SetPunchAngle},
  {"SetStepSoundCache", CBasePlayer_SetStepSoundCache},
  {"SetSuitUpdate", CBasePlayer_SetSuitUpdate},
  {"SetSwimSoundTime", CBasePlayer_SetSwimSoundTime},
  {"SetWaterJumpTime", CBasePlayer_SetWaterJumpTime},
  {"SharedSpawn", CBasePlayer_SharedSpawn},
  {"ShouldShowHints", CBasePlayer_ShouldShowHints},
  {"SimulatePlayerSimulatedEntities", CBasePlayer_SimulatePlayerSimulatedEntities},
  {"SmoothViewOnStairs", CBasePlayer_SmoothViewOnStairs},
  {"Spawn", CBasePlayer_Spawn},
  {"SwitchToNextBestWeapon", CBasePlayer_SwitchToNextBestWeapon},
  {"UpdateClientData", CBasePlayer_UpdateClientData},
  {"UpdateUnderwaterState", CBasePlayer_UpdateUnderwaterState},
  {"UsingStandardWeaponsInVehicle", CBasePlayer_UsingStandardWeaponsInVehicle},
  {"ViewPunch", CBasePlayer_ViewPunch},
  {"ViewPunchReset", CBasePlayer_ViewPunchReset},
  {"Weapon_CanSwitchTo", CBasePlayer_Weapon_CanSwitchTo},
  {"Weapon_OwnsThisType", CBasePlayer_Weapon_OwnsThisType},
  {"Weapon_SetLast", CBasePlayer_Weapon_SetLast},
  {"Weapon_ShootPosition", CBasePlayer_Weapon_ShootPosition},
  {"Weapon_ShouldSelectItem", CBasePlayer_Weapon_ShouldSelectItem},
  {"Weapon_ShouldSetLast", CBasePlayer_Weapon_ShouldSetLast},
  {"Weapon_Switch", CBasePlayer_Weapon_Switch},
  {"WeaponCount", CBasePlayer_WeaponCount},
  {"Alive", CBasePlayer_Alive},
  {"IsAlive", CBasePlayer_Alive},
  {"GetClassID", CBasePlayer_GetClassID},
  {"SetClassID", CBasePlayer_SetClassID},
  {"GetHands", CBasePlayer_GetHands},
  {"SetHands", CBasePlayer_SetHands},
  {"GetHull", CBasePlayer_GetHull},
  {"SetHull", CBasePlayer_SetHull},
  {"GetHullDuck", CBasePlayer_GetHullDuck},
  {"SetHullDuck", CBasePlayer_SetHullDuck},
  {"ResetHull", CBasePlayer_ResetHull},
  {"SetViewOffset", CBasePlayer_SetViewOffset},
  {"GetViewOffsetDucked", CBasePlayer_GetViewOffsetDucked},
  {"SetViewOffsetDucked", CBasePlayer_SetViewOffsetDucked},
  {"SetCurrentViewOffset", CBasePlayer_SetCurrentViewOffset},
  {"GetCurrentViewOffset", CBasePlayer_GetCurrentViewOffset},
  {"GetViewOffset", CBasePlayer_GetViewOffset},
  {"SetEyeAngles", CBasePlayer_SetEyeAngles},
  {"GetRenderAngles", CBasePlayer_GetRenderAngles},
  {"SetRenderAngles", CBasePlayer_SetRenderAngles},
#ifdef CLIENT_DLL
  {"GetAllowWeaponsInVehicle", CBasePlayer_GetAllowWeaponsInVehicle},
  {"ShouldDrawLocalPlayer", CBasePlayer_ShouldDrawLocalPlayer},
  // HL2SB GMod compat: the scoreboard's mute button reads and toggles these.
  {"IsMuted", CBasePlayer_IsMuted},
  {"SetMuted", CBasePlayer_SetMuted},
#endif
  {"__index", CBasePlayer___index},
  {"__newindex", CBasePlayer___newindex},
  {"__eq", CBasePlayer___eq},
  {"__tostring", CBasePlayer___tostring},
  {NULL, NULL}
};


static int luasrc_ToBasePlayer (lua_State *L) {
  lua_pushplayer(L, ToBasePlayer(luaL_checkentity(L, 1)));
  return 1;
}


static const luaL_Reg CBasePlayer_funcs[] = {
  {"ToBasePlayer", luasrc_ToBasePlayer},
  {NULL, NULL}
};


/*
** Open CBasePlayer object
*/
static int player_GetByID( lua_State *L ) {
  const double index = luaL_checknumber( L, 1 );
  CBasePlayer *player = NULL;
  if ( index >= 1 && index < (double)gpGlobals->maxClients + 1 )
    player = UTIL_PlayerByIndex( (int)index );
  lua_pushplayer( L, player );
  return 1;
}

LUALIB_API int luaopen_CBasePlayer_shared (lua_State *L) {
  luaL_getmetatable(L, LUA_BASEPLAYERLIBNAME);
  if (lua_isnoneornil(L, -1)) {
    lua_pop(L, 1);
    luaL_newmetatable(L, LUA_BASEPLAYERLIBNAME);
  }
  luaL_register(L, NULL, CBasePlayermeta);
  lua_pushstring(L, "entity");
  lua_setfield(L, -2, "__type");  /* metatable.__type = "entity" */
  luaL_register(L, "_G", CBasePlayer_funcs);
  lua_getglobal( L, "player" );
  if ( !lua_istable( L, -1 ) ) {
    lua_pop( L, 1 );
    lua_newtable( L );
  }
  lua_pushcfunction( L, player_GetByID );
  lua_setfield( L, -2, "GetByID" );
  lua_setglobal( L, "player" );
  HL2SB_EnsureUserCmdMeta(L);
  lua_pop(L, 1);
  HL2SB_EnsureMoveDataMeta(L);
  lua_pop(L, 1);
  lua_pop(L, 1);
  return 1;
}


// ===== HL2SB GMod compat (2026-09-24): GM:SetupMove / GM:FinishMove ==========
// GMod fires the predicted movement hooks around the engine's move processing:
//   hook.Run( "SetupMove",  ply, mv, cmd )  before movement,
//   hook.Run( "FinishMove", ply, mv, cmd )  after it.
// The First Person Body addon reads mv:GetButtons()/GetOldButtons() there to
// time its jump/duck state.  CMoveData is pushed as a POINTER userdata (its
// lifetime is the ProcessMovement call, no copy needed) with the getters and
// setters GMod addons actually use.  The cmd argument has no binding in this
// fork, so nil is pushed in its slot.

#include "igamemovement.h"

static CMoveData *HL2SB_CheckMoveData (lua_State *L, int narg) {
  CMoveData **ppMove = (CMoveData **)luaL_checkudata(L, narg, "MoveData");
  return *ppMove;
}

#define HL2SB_MOVEDATA_INT_GETSET( name, field )                     \
  static int MoveData_Get##name (lua_State *L) {                     \
    lua_pushinteger(L, HL2SB_CheckMoveData(L, 1)->field);            \
    return 1;                                                        \
  }                                                                  \
  static int MoveData_Set##name (lua_State *L) {                     \
    HL2SB_CheckMoveData(L, 1)->field = luaL_checkint(L, 2);          \
    return 0;                                                        \
  }

#define HL2SB_MOVEDATA_NUM_GETSET( name, field )                     \
  static int MoveData_Get##name (lua_State *L) {                     \
    lua_pushnumber(L, HL2SB_CheckMoveData(L, 1)->field);             \
    return 1;                                                        \
  }                                                                  \
  static int MoveData_Set##name (lua_State *L) {                     \
    HL2SB_CheckMoveData(L, 1)->field = luaL_checknumber(L, 2);       \
    return 0;                                                        \
  }

#define HL2SB_MOVEDATA_VEC_GETSET( name, getter, setter )            \
  static int MoveData_Get##name (lua_State *L) {                     \
    lua_pushvector(L, HL2SB_CheckMoveData(L, 1)->getter());          \
    return 1;                                                        \
  }                                                                  \
  static int MoveData_Set##name (lua_State *L) {                     \
    HL2SB_CheckMoveData(L, 1)->setter(luaL_checkvector(L, 2));       \
    return 0;                                                        \
  }

#define HL2SB_MOVEDATA_ANG_GETSET( name, field )                     \
  static int MoveData_Get##name (lua_State *L) {                     \
    lua_pushangle(L, HL2SB_CheckMoveData(L, 1)->field);              \
    return 1;                                                        \
  }                                                                  \
  static int MoveData_Set##name (lua_State *L) {                     \
    HL2SB_CheckMoveData(L, 1)->field = luaL_checkangle(L, 2);        \
    return 0;                                                        \
  }

HL2SB_MOVEDATA_INT_GETSET( Buttons, m_nButtons )
HL2SB_MOVEDATA_INT_GETSET( OldButtons, m_nOldButtons )
HL2SB_MOVEDATA_INT_GETSET( ImpulseCommand, m_nImpulseCommand )
HL2SB_MOVEDATA_NUM_GETSET( ForwardMove, m_flForwardMove )
HL2SB_MOVEDATA_NUM_GETSET( SideMove, m_flSideMove )
HL2SB_MOVEDATA_NUM_GETSET( UpMove, m_flUpMove )
HL2SB_MOVEDATA_NUM_GETSET( MaxSpeed, m_flMaxSpeed )
HL2SB_MOVEDATA_NUM_GETSET( ClientMaxSpeed, m_flClientMaxSpeed )
HL2SB_MOVEDATA_VEC_GETSET( Origin, GetAbsOrigin, SetAbsOrigin )
HL2SB_MOVEDATA_ANG_GETSET( ViewAngles, m_vecViewAngles )
HL2SB_MOVEDATA_ANG_GETSET( Angles, m_vecAngles )

static int MoveData_GetVelocity (lua_State *L) {
  lua_pushvector(L, HL2SB_CheckMoveData(L, 1)->m_vecVelocity);
  return 1;
}
static int MoveData_SetVelocity (lua_State *L) {
  HL2SB_CheckMoveData(L, 1)->m_vecVelocity = luaL_checkvector(L, 2);
  return 0;
}

// GMod's CMoveData surface: the wish-speed trio plus KeyDown and the
// GetMoveAngles spelling.  This engine's CMoveData predates GMod's added
// m_flForwardSpeed/SideSpeed/UpSpeed fields, so the speeds answer the cmd
// move values, which are the wish speeds in u/s by the time movement runs
// (cl_forwardspeed is driven to maxspeed) - the same quantity the wiki
// documents, and what the portalgun's in-portal movement feeds its wishdir
// from: mv:GetMoveAngles() + GetForwardSpeed()/GetSideSpeed(), jump gated
// by mv:KeyDown( IN_JUMP ).
static int MoveData_GetForwardSpeed (lua_State *L) {
  lua_pushnumber(L, HL2SB_CheckMoveData(L, 1)->m_flForwardMove);
  return 1;
}
static int MoveData_GetSideSpeed (lua_State *L) {
  lua_pushnumber(L, HL2SB_CheckMoveData(L, 1)->m_flSideMove);
  return 1;
}
static int MoveData_GetUpSpeed (lua_State *L) {
  lua_pushnumber(L, HL2SB_CheckMoveData(L, 1)->m_flUpMove);
  return 1;
}
static int MoveData_GetMoveAngles (lua_State *L) {
  lua_pushangle(L, HL2SB_CheckMoveData(L, 1)->m_vecViewAngles);
  return 1;
}
static int MoveData_KeyDown (lua_State *L) {
  lua_pushboolean(L, (HL2SB_CheckMoveData(L, 1)->m_nButtons & luaL_checkint(L, 2)) != 0);
  return 1;
}

static int MoveData_KeyWasDown (lua_State *L) {
  lua_pushboolean(L, (HL2SB_CheckMoveData(L, 1)->m_nOldButtons & luaL_checkint(L, 2)) != 0);
  return 1;
}

// HL2SB (2026-10-16) GMod compat: CMoveData:KeyReleased / SetMoveAngles /
// SetUpSpeed.
//
// Reference behavior (GMod win64): KeyReleased answers
// `(buttons & b) == 0 && (oldButtons & b) != 0`; SetMoveAngles writes the
// angle argument into the move-angles triplet (this engine keeps m_vecAngles
// there); SetUpSpeed writes the up wish-speed field.
// This CMoveData predates GMod's extra m_flUpSpeed member and stores the up
// wish speed in m_flUpMove, which is the slot that actually feeds movement,
// so SetUpSpeed maps onto it (the same field GetUpSpeed already answers).
// The drive system consumes all three from its move hooks
// (lua/drive/drive_sandbox.lua: mv:KeyReleased at :82/:99, mv:SetMoveAngles
// at :91, mv:SetUpSpeed at :138).
static int MoveData_KeyPressed (lua_State *L) {
  CMoveData *pMove = HL2SB_CheckMoveData(L, 1);
  const int bit = luaL_checkint(L, 2);
  lua_pushboolean(L, (pMove->m_nButtons & bit) != 0 && (pMove->m_nOldButtons & bit) == 0);
  return 1;
}

static int MoveData_KeyReleased (lua_State *L) {
  CMoveData *pMove = HL2SB_CheckMoveData(L, 1);
  const int nBit = luaL_checkint(L, 2);
  lua_pushboolean(L, (pMove->m_nButtons & nBit) == 0 && (pMove->m_nOldButtons & nBit) != 0);
  return 1;
}
static int MoveData_SetMoveAngles (lua_State *L) {
  HL2SB_CheckMoveData(L, 1)->m_vecViewAngles = luaL_checkangle(L, 2);
  return 0;
}
static int MoveData_SetUpSpeed (lua_State *L) {
  HL2SB_CheckMoveData(L, 1)->m_flUpMove = luaL_checknumber(L, 2);
  return 0;
}

static int MoveData_AddKey (lua_State *L) {
  HL2SB_CheckMoveData(L, 1)->m_nButtons |= luaL_checkint(L, 2);
  return 0;
}

static const luaL_Reg HL2SB_MoveDatameta[] = {
  {"GetButtons",          MoveData_GetButtons},
  {"SetButtons",          MoveData_SetButtons},
  {"GetOldButtons",       MoveData_GetOldButtons},
  {"SetOldButtons",       MoveData_SetOldButtons},
  {"GetImpulseCommand",   MoveData_GetImpulseCommand},
  {"SetImpulseCommand",   MoveData_SetImpulseCommand},
  {"GetForwardMove",      MoveData_GetForwardMove},
  {"SetForwardMove",      MoveData_SetForwardMove},
  {"GetSideMove",         MoveData_GetSideMove},
  {"SetSideMove",         MoveData_SetSideMove},
  {"GetUpMove",           MoveData_GetUpMove},
  {"SetUpMove",           MoveData_SetUpMove},
  {"GetMaxSpeed",         MoveData_GetMaxSpeed},
  {"SetMaxSpeed",         MoveData_SetMaxSpeed},
  {"GetClientMaxSpeed",   MoveData_GetClientMaxSpeed},
  {"SetClientMaxSpeed",   MoveData_SetClientMaxSpeed},
  {"GetOrigin",           MoveData_GetOrigin},
  {"SetOrigin",           MoveData_SetOrigin},
  {"GetVelocity",         MoveData_GetVelocity},
  {"SetVelocity",         MoveData_SetVelocity},
  {"GetViewAngles",       MoveData_GetViewAngles},
  {"SetViewAngles",       MoveData_SetViewAngles},
  {"GetAngles",           MoveData_GetAngles},
  {"SetAngles",           MoveData_SetAngles},
  {"GetForwardSpeed",     MoveData_GetForwardSpeed},
  {"GetSideSpeed",        MoveData_GetSideSpeed},
  {"GetUpSpeed",          MoveData_GetUpSpeed},
  {"GetMoveAngles",       MoveData_GetMoveAngles},
  {"SetMoveAngles",       MoveData_SetMoveAngles},
  {"SetUpSpeed",          MoveData_SetUpSpeed},
  {"KeyDown",             MoveData_KeyDown},
  {"KeyWasDown",          MoveData_KeyWasDown},
  {"AddKey",              MoveData_AddKey},
  {"KeyPressed",          MoveData_KeyPressed},
  {"KeyReleased",         MoveData_KeyReleased},
  {NULL, NULL}
};

#undef HL2SB_MOVEDATA_INT_GETSET
#undef HL2SB_MOVEDATA_NUM_GETSET
#undef HL2SB_MOVEDATA_VEC_GETSET
#undef HL2SB_MOVEDATA_ANG_GETSET

static void HL2SB_EnsureMoveDataMeta (lua_State *L) {
  luaL_getmetatable(L, "MoveData");
  if (lua_isnil(L, -1)) {
    lua_pop(L, 1);
    luaL_newmetatable(L, "MoveData");
    luaL_register(L, NULL, HL2SB_MoveDatameta);
    lua_pushstring(L, "movedata");
    lua_setfield(L, -2, "__type");
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");
  }
  /* leave the metatable on top: lua_setmetatable() consumes it */
}

static void HL2SB_PushMoveData_Internal (lua_State *L, CMoveData *pMove) {
  CMoveData **ppMove = (CMoveData **)lua_newuserdata(L, sizeof(CMoveData *));
  *ppMove = pMove;
  HL2SB_EnsureMoveDataMeta(L);
  lua_setmetatable(L, -2);
}

// Dispatched from CGameMovement::ProcessMovement via a local extern
// declaration there (AGENTS.md: keep headers untouched, waf will not
// propagate them).  Fires one GMod movement hook.
void HL2SB_LuaVehicleMove( CBasePlayer *player, CBaseEntity *vehicle, CMoveData *move )
{
    if ( L == NULL || player == NULL || vehicle == NULL || move == NULL )
        return;
    BEGIN_LUA_CALL_HOOK( "VehicleMove" );
        lua_pushplayer( L, player );
        CBaseEntity::PushLuaInstanceSafe( L, vehicle );
        HL2SB_PushMoveData_Internal( L, move );
    END_LUA_CALL_HOOK( 3, 0 );
}

// HL2SB: fires one GMod movement hook and answers whether the hook TAKES
// OVER that movement stage (wiki Game_Movement: each of SetupMove/Move/
// FinishMove returns true to suppress only its own default engine step --
// drive.Move returning true must still let GM:SetupMove/GM:FinishMove run,
// which the old early-return dispatch got wrong and wedged the drive exit).
int HL2SB_LuaMoveHooks( CBasePlayer *pPlayer, CMoveData *pMove, const char *pszHook )
{
	if ( L == NULL || pPlayer == NULL || pMove == NULL )
		return 0;

	if (Q_strcmp(pszHook, "SetupMove") != 0)
	{
		BEGIN_LUA_CALL_HOOK( pszHook );
			lua_pushplayer( L, pPlayer );
			HL2SB_PushMoveData_Internal( L, pMove );
		END_LUA_CALL_HOOK( 2, 1 );
		const int handled = lua_toboolean( L, -1 ) != 0 ? 1 : 0;
		lua_pop( L, 1 );
		return handled;
	}

	const CUserCmd *pCmd = pPlayer->GetCurrentUserCommand();
	CUserCmd fallbackCmd;
	if ( pCmd == NULL )
	{
		fallbackCmd.Reset();
		fallbackCmd.viewangles = pMove->m_vecViewAngles;
		fallbackCmd.buttons = pMove->m_nButtons;
		fallbackCmd.forwardmove = pMove->m_flForwardMove;
		fallbackCmd.sidemove = pMove->m_flSideMove;
		fallbackCmd.upmove = pMove->m_flUpMove;
		fallbackCmd.command_number = pPlayer->CurrentCommandNumber();
		fallbackCmd.tick_count = gpGlobals->tickcount;
		pCmd = &fallbackCmd;
	}

	int iTableRef = LUA_NOREF;
	int iTakesOver = 0;

	BEGIN_LUA_CALL_HOOK( pszHook );
		lua_pushplayer( L, pPlayer );
		HL2SB_PushMoveData_Internal( L, pMove );
		HL2SB_PushUserCmdTable( L, pCmd );
		lua_pushvalue( L, -1 );
		iTableRef = luaL_ref( L, LUA_REGISTRYINDEX );
	END_LUA_CALL_HOOK( 3, 1 );
	iTakesOver = lua_toboolean( L, -1 ) != 0 ? 1 : 0;
	lua_pop( L, 1 );

	if ( iTableRef != LUA_NOREF )
	{
		lua_rawgeti( L, LUA_REGISTRYINDEX, iTableRef );
		if ( lua_istable( L, -1 ) )
		{
			CUserCmd *pMutCmd = ( pCmd != &fallbackCmd ) ? const_cast< CUserCmd * >( pCmd ) : NULL;

			lua_getfield( L, -1, "viewangles" );
			if ( lua_isuserdata( L, -1 ) && luaL_checkudata( L, -1, "Angle" ) )
			{
				const QAngle ang = luaL_checkangle( L, -1 );
				if ( pMutCmd != NULL )
				{
					pMutCmd->viewangles[0] = ang[0];
					pMutCmd->viewangles[1] = ang[1];
					// Preserve roll (viewangles[2]) in the command buffer so predicted command replays keep their encoded state
				}
			}
			lua_pop( L, 1 );

			lua_getfield( L, -1, "buttons" );
			if ( lua_isnumber( L, -1 ) )
			{
				const int btns = (int)lua_tointeger( L, -1 );
				if ( pMutCmd != NULL )
					pMutCmd->buttons = btns;
			}
			lua_pop( L, 1 );

			lua_getfield( L, -1, "forwardmove" );
			if ( lua_isnumber( L, -1 ) )
			{
				const float fmove = (float)lua_tonumber( L, -1 );
				if ( pMutCmd != NULL )
					pMutCmd->forwardmove = fmove;
			}
			lua_pop( L, 1 );

			lua_getfield( L, -1, "sidemove" );
			if ( lua_isnumber( L, -1 ) )
			{
				const float smove = (float)lua_tonumber( L, -1 );
				if ( pMutCmd != NULL )
					pMutCmd->sidemove = smove;
			}
			lua_pop( L, 1 );

			lua_getfield( L, -1, "upmove" );
			if ( lua_isnumber( L, -1 ) )
			{
				const float umove = (float)lua_tonumber( L, -1 );
				if ( pMutCmd != NULL )
					pMutCmd->upmove = umove;
			}
			lua_pop( L, 1 );
		}
		lua_pop( L, 1 );
		luaL_unref( L, LUA_REGISTRYINDEX, iTableRef );
	}

	return iTakesOver;
}

// HL2SB (2026-10-19): GM:StartCommand( ply, cmd ) shared hook helper with
// full snapshot copy-back for buttons, viewangles, and directional moves.
void HL2SB_LuaStartCommand( CBasePlayer *pPlayer, CUserCmd *pCmd )
{
	if ( L == NULL || pPlayer == NULL || pCmd == NULL )
		return;

	HL2SB_PushUserCmdTable( L, pCmd );
	const int iTableRef = luaL_ref( L, LUA_REGISTRYINDEX );

	BEGIN_LUA_CALL_HOOK( "StartCommand" );
		lua_pushplayer( L, pPlayer );
		lua_rawgeti( L, LUA_REGISTRYINDEX, iTableRef );
		END_LUA_CALL_HOOK( 2, 0 );

	lua_rawgeti( L, LUA_REGISTRYINDEX, iTableRef );
	if ( lua_istable( L, -1 ) )
	{
		lua_getfield( L, -1, "buttons" );
		if ( lua_isnumber( L, -1 ) )
			pCmd->buttons = (int)lua_tointeger( L, -1 );
		lua_pop( L, 1 );

		lua_getfield( L, -1, "viewangles" );
		if ( lua_isuserdata( L, -1 ) && luaL_checkudata( L, -1, "Angle" ) )
			pCmd->viewangles = luaL_checkangle( L, -1 );
		lua_pop( L, 1 );

		lua_getfield( L, -1, "forwardmove" );
		if ( lua_isnumber( L, -1 ) )
			pCmd->forwardmove = (float)lua_tonumber( L, -1 );
		lua_pop( L, 1 );

		lua_getfield( L, -1, "sidemove" );
		if ( lua_isnumber( L, -1 ) )
			pCmd->sidemove = (float)lua_tonumber( L, -1 );
		lua_pop( L, 1 );

		lua_getfield( L, -1, "upmove" );
		if ( lua_isnumber( L, -1 ) )
			pCmd->upmove = (float)lua_tonumber( L, -1 );
		lua_pop( L, 1 );
	}
	lua_pop( L, 1 );
	luaL_unref( L, LUA_REGISTRYINDEX, iTableRef );
}

// HL2SB (2026-10-08): the two landing/jump gamemode hooks.  gamemovement.cpp
// reaches them through these helpers because that file deliberately stays
// free of the Lua headers (local-extern pattern, see its move-hook blocks).
// OnPlayerHitGround answers GMod's contract: true means the caller skips the
// default landing damage/effects block.

bool HL2SB_LuaPlayerHitGround( CBasePlayer *pPlayer, bool bInWater, bool bOnFloater, float flFallSpeed )
{
	if ( L == NULL || pPlayer == NULL )
		return false;

	bool bSkipDefault = false;

	BEGIN_LUA_CALL_HOOK( "OnPlayerHitGround" );
		lua_pushplayer( L, pPlayer );
		lua_pushboolean( L, bInWater ? 1 : 0 );
		lua_pushboolean( L, bOnFloater ? 1 : 0 );
		lua_pushnumber( L, flFallSpeed );
		END_LUA_CALL_HOOK( 4, 1 );
	if ( L != NULL && lua_isboolean( L, -1 ) )
		bSkipDefault = ( lua_toboolean( L, -1 ) != 0 );
	if ( L != NULL )
		lua_pop( L, 1 );

	return bSkipDefault;
}

void HL2SB_LuaPlayerJump( CBasePlayer *pPlayer, float flJumpSpeed )
{
	if ( L == NULL || pPlayer == NULL )
		return;

	BEGIN_LUA_CALL_HOOK( "OnPlayerJump" );
		lua_pushplayer( L, pPlayer );
		lua_pushnumber( L, flJumpSpeed );
		END_LUA_CALL_HOOK( 2, 0 );
}

// GM:Move( ply, mv ) - the replace-the-movement hook, fired as
// CGameMovement::ProcessMovement's FIRST statement (reference behaviour: the
// dispatch sits before any init, takes the player and the CMoveData and -
// per the binary - NO third CUserCmd argument despite what the wiki
// documents).  Returns true when a hook or the gamemode method returned
// (the GM:Move dispatch moved into HL2SB_LuaMoveHooks/ProcessMovement's
// unified staged sequence -- the old standalone early-return helper answered
// Move's takeover BEFORE SetupMove ever ran, which skipped drive's StartMove
// and FinishMove entirely and wedged the drive exit; reference order is
// PlayerTick -> SetupMove -> Move -> FinishMove, each true suppressing only
// its own default engine step)

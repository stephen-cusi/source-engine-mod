//====== Copyright © 1996-2005, Valve Corporation, All rights reserved. =====//
//
// Purpose: Client-side CBasePlayer.
//
//			- Manages the player's flashlight effect.
//
//===========================================================================//
#define lc_baseplayer_cpp

#include "cbase.h"
#include "c_baseplayer.h"
#include "luamanager.h"
#include "luasrclib.h"
#include "lbaseplayer_shared.h"
#include "mathlib/lvector.h"	// HL2SB: luaL_checkangle for SetEyeAngles
#include "hud_chat.h"
#include "hud_basechat.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

static int CBasePlayer_GetLocalPlayer (lua_State *L) {
  lua_pushplayer(L, CBasePlayer::GetLocalPlayer());
  return 1;
}

static int CBasePlayer_ChatPrint (lua_State *L) {
  luaL_checkplayer( L, 1 );
  const char *message = luaL_checkstring( L, 2 );
  CBaseHudChat *chat = ( CBaseHudChat * )GET_HUDELEMENT( CHudChat );
  if ( chat )
    chat->ChatPrintf( 0, CHAT_FILTER_NONE, "%s", message );
  return 0;
}


//-----------------------------------------------------------------------------
// Purpose: HL2SB - player:UniqueID() on the CLIENT too.
//
// GMod's Player:UniqueID() exists in BOTH realms, and the Lua HUD depends on
// that: lua/game/client/hl2sb_cl_hudpickup.lua compares
//
//     LocalPlayer():UniqueID() != userid      -- userid from item_pickup
//
// to ignore other players' pickups.  The binding only ever existed in
// game/server/lua/lplayer.cpp, so on the client this raised
// "attempt to call a nil value (method 'UniqueID')" -- and lua/includes/modules/
// hook.lua UNREGISTERS a hook that throws, so the pickup HUD died on the very
// first pickup and never came back.  That is why the pickup notification never
// appeared even though the whole event chain (item_pickup -> HUDItemPickedUp ->
// HUDWeaponPickedUp/HUDItemPickedUp/HUDAmmoPickedUp -> HudViewportPaint) was
// wired correctly.
//
// Returns the engine user ID -- the same number CBasePlayer::GetUserID() gives
// on the server, and the same one HL2MP puts in item_pickup.userid, so the
// comparison above actually holds.
//-----------------------------------------------------------------------------
static int CBasePlayer_UniqueID (lua_State *L) {
  lua_pushinteger(L, luaL_checkplayer(L, 1)->GetUserID());
  return 1;
}


// HL2SB GMod compat (Nuke Pack audit 2026-09-20): the client-side view
// toggles.  Both write flags the render path consults:
//   * view.cpp suppresses the viewmodel when g_HL2SB_HideViewModel is set
//   * hud_crosshair.cpp suppresses the crosshair when
//     g_HL2SB_CrosshairHidden is set
// GMod scopes these to the calling player; only the LOCAL player's flag can
// influence this client's render, so a call from any other player is a no-op
// (the flags are global on purpose -- one local player per client).
bool g_HL2SB_HideViewModel = false;
bool g_HL2SB_CrosshairHidden = false;

static int CBasePlayer_DrawViewModel (lua_State *L) {
  C_BasePlayer *pPlayer = luaL_checkplayer( L, 1 );
  if ( pPlayer != NULL && pPlayer == C_BasePlayer::GetLocalPlayer() )
    g_HL2SB_HideViewModel = !luaL_checkboolean( L, 2 );   // bDraw = true -> show
  return 0;
}

static int CBasePlayer_CrosshairDisable (lua_State *L) {
  C_BasePlayer *pPlayer = luaL_checkplayer( L, 1 );
  if ( pPlayer != NULL && pPlayer == C_BasePlayer::GetLocalPlayer() )
    g_HL2SB_CrosshairHidden = true;
  return 0;
}

static int CBasePlayer_CrosshairEnable (lua_State *L) {
  C_BasePlayer *pPlayer = luaL_checkplayer( L, 1 );
  if ( pPlayer != NULL && pPlayer == C_BasePlayer::GetLocalPlayer() )
    g_HL2SB_CrosshairHidden = false;
  return 0;
}

// HL2SB (2026-09-22): Player:ShouldDrawLocalPlayer() -- GMod asks whether the
// local player's own model should be drawn this frame (true in third person,
// mirrored views, vehicle cameras).  cf_beast's Muzzle() uses it to decide
// between a world muzzle flash on the player and the viewmodel shell eject.
// GMod wiki: "Despite this being a method on a player object, this will always
// represent the state of the local player, not of the player entity this
// method is used on." -- so the receiver is validated but ignored; the previous
// receiver==local guard answered false for otherPlayer:ShouldDrawLocalPlayer()
// where GMod answers with the local player's state.  Client-only metatable, so
// the static query is always valid here.
static int CBasePlayer_ShouldDrawLocalPlayer (lua_State *L) {
  luaL_checkplayer( L, 1 );
  lua_pushboolean( L, C_BasePlayer::ShouldDrawLocalPlayer() );
  return 1;
}

// HL2SB (2026-10-11): Player:SetEyeAngles( ang ) on the CLIENT too (server
// binding: game/server/lua/lplayer.cpp).  Reference behavior: the angle is
// applied only when the receiver IS the local player -- a client cannot steer
// another client's view, and the other-player case is a silent no-op rather
// than an error.  The write goes through the engine's view-angle setter, the
// same path mouse input uses, so the new angles render on this frame and
// reach the server with the next usercmd.  First consumer: awpdragon's
// clout_gun_base applies per-shot eye recoil from shared code
// (self.Owner:SetEyeAngles inside ShootBullet) and used to raise "attempt to
// call a nil value (method 'SetEyeAngles')" on every client-side shot.
static int CBasePlayer_SetEyeAngles (lua_State *L) {
  C_BasePlayer *pPlayer = luaL_checkplayer( L, 1 );
  QAngle angles = luaL_checkangle( L, 2 );
  if ( pPlayer != NULL && pPlayer == C_BasePlayer::GetLocalPlayer() )
    engine->SetViewAngles( angles );
  return 0;
}

static const luaL_Reg CBasePlayermeta[] = {
  {"GetLocalPlayer", CBasePlayer_GetLocalPlayer},
  {"UniqueID", CBasePlayer_UniqueID},
  {"ChatPrint", CBasePlayer_ChatPrint},
  {"DrawViewModel", CBasePlayer_DrawViewModel},
  {"CrosshairDisable", CBasePlayer_CrosshairDisable},
  {"CrosshairEnable", CBasePlayer_CrosshairEnable},
  {"ShouldDrawLocalPlayer", CBasePlayer_ShouldDrawLocalPlayer},
  {"SetEyeAngles", CBasePlayer_SetEyeAngles},
  {NULL, NULL}
};


//-----------------------------------------------------------------------------
// HL2SB (2026-09-27): the server's player.GetAll(), client side (server
// version: game/server/lua/lplayer.cpp).  The lsrcinit `player = player or {}`
// stub leaves the client table EMPTY, so anything of GMod's shape that
// iterates players died here with "field 'GetAll' is nil" -- first seen in
// the taunt camera's filter = player.GetAll() trace.
//-----------------------------------------------------------------------------
static int CBasePlayer_GetAll (lua_State *L) {
  lua_newtable( L );
  int i = 0;
  for ( int iClient = 1; iClient <= gpGlobals->maxClients; ++iClient )
  {
    CBasePlayer *pPlayer = UTIL_PlayerByIndex( iClient );
    if ( !pPlayer )
      continue;

    lua_pushinteger( L, ++i );
    lua_pushplayer( L, pPlayer );
    lua_rawset( L, -3 );
  }
  return 1;
}

static int CBasePlayer_GetHumans (lua_State *L) {
  lua_newtable( L );
  int n = 0;
  for ( int index = 1; index <= gpGlobals->maxClients; ++index ) {
    CBasePlayer *player = UTIL_PlayerByIndex( index );
    player_info_t info;
    if ( !player || player->IsMarkedForDeletion() || !engine->GetPlayerInfo(index, &info) || info.fakeplayer ) continue;
    lua_pushinteger( L, ++n );
    lua_pushplayer( L, player );
    lua_rawset( L, -3 );
  }
  return 1;
}

static int CBasePlayer_GetBots (lua_State *L) {
  lua_newtable( L );
  int n = 0;
  for ( int index = 1; index <= gpGlobals->maxClients; ++index ) {
    CBasePlayer *player = UTIL_PlayerByIndex( index );
    player_info_t info;
    if ( !player || player->IsDormant() || !engine->GetPlayerInfo(index, &info) || !info.fakeplayer ) continue;
    lua_pushinteger( L, ++n );
    lua_pushplayer( L, player );
    lua_rawset( L, -3 );
  }
  return 1;
}


/*
** Open CBasePlayer object
*/
LUALIB_API int luaopen_CBasePlayer (lua_State *L) {
  luaL_getmetatable(L, LUA_BASEPLAYERLIBNAME);
  if (lua_isnoneornil(L, -1)) {
    lua_pop(L, 1);
    luaL_newmetatable(L, LUA_BASEPLAYERLIBNAME);
  }
  luaL_register(L, NULL, CBasePlayermeta);

  // HL2SB: expose a global LocalPlayer() that returns the local player entity,
  // so GMod-style scripts (and the player model menu's colour tab) can call
  // LocalPlayer():SetPlayerColor(...) / GetPlayerColor().  engine.GetLocalPlayer()
  // only returns the ent index, which is useless for entity method calls.
  lua_pushcfunction(L, CBasePlayer_GetLocalPlayer);
  lua_setglobal(L, "LocalPlayer");

  // HL2SB (2026-09-27): player.GetAll on the client too -- see CBasePlayer_GetAll.
  lua_getglobal( L, "player" );
  if ( lua_isnoneornil( L, -1 ) )
  {
    lua_pop( L, 1 );
    lua_newtable( L );
  }
  lua_pushcfunction( L, CBasePlayer_GetAll );
  lua_setfield( L, -2, "GetAll" );
  lua_pushcfunction( L, CBasePlayer_GetHumans );
  lua_setfield( L, -2, "GetHumans" );
  lua_pushcfunction( L, CBasePlayer_GetBots );
  lua_setfield( L, -2, "GetBots" );
  lua_setglobal( L, "player" );

  return 1;
}

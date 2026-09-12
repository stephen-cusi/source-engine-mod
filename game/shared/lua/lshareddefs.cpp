//========= Copyright © 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: Definitions that are shared by the game DLL and the client DLL.
//
// $NoKeywords: $
//=============================================================================//

#define lshareddefs_cpp

#include "cbase.h"
#include "ammodef.h"
#include "luamanager.h"
#include "lshareddefs.h"
#include "lbaseentity_shared.h"
#include "mathlib/lvector.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


/*
** access functions (stack -> C)
*/


LUA_API lua_FireBulletsInfo_t lua_tofirebulletsinfo (lua_State *L, int idx) {
  luaL_checktype(L, idx, LUA_TTABLE);
  FireBulletsInfo_t info;

  // HL2SB: FireBulletsInfo_t's default constructor only initialises m_iAmmoType,
  // m_vecSrc and m_vecDirShooting under _DEBUG (shareddefs.h:686) -- in a release
  // build they are indeterminate, and GMod scripts routinely omit AmmoType
  // (weapon_nyangun.lua deliberately comments it out: "For some extremely stupid
  // reason this breaks the tracer effect").  The garbage then reaches
  // GetAmmoDef()->TracerType( info.m_iAmmoType ) / DamageType( ... ) and
  // pAmmoDef->Flags( ... ), which are plain array reads -- an out-of-range access.
  // 0 is the engine's own "None" ammo type, which is also GMod's fallback.
  info.m_iAmmoType = 0;
  info.m_vecSrc.Init();
  info.m_vecDirShooting.Init();

  lua_getfield(L, idx, "m_bPrimaryAttack");
  if (!lua_isnil(L, -1))
    info.m_bPrimaryAttack = luaL_checkboolean(L, -1);
  lua_pop(L, 1);
  lua_getfield(L, idx, "m_flDamageForceScale");
  if (!lua_isnil(L, -1))
    info.m_flDamageForceScale = luaL_checknumber(L, -1);
  lua_pop(L, 1);
  lua_getfield(L, idx, "m_flDistance");
  if (!lua_isnil(L, -1))
    info.m_flDistance = luaL_checknumber(L, -1);
  lua_pop(L, 1);
  lua_getfield(L, idx, "m_iAmmoType");
  if (!lua_isnil(L, -1))
    info.m_iAmmoType = luaL_checkint(L, -1);
  lua_pop(L, 1);
  lua_getfield(L, idx, "m_iDamage");
  if (!lua_isnil(L, -1))
    info.m_flDamage = luaL_checkint(L, -1);
  lua_pop(L, 1);
  lua_getfield(L, idx, "m_iPlayerDamage");
  if (!lua_isnil(L, -1))
    info.m_iPlayerDamage = luaL_checkint(L, -1);
  lua_pop(L, 1);
  lua_getfield(L, idx, "m_iShots");
  if (!lua_isnil(L, -1))
    info.m_iShots = luaL_checkint(L, -1);
  lua_pop(L, 1);
  lua_getfield(L, idx, "m_iTracerFreq");
  if (!lua_isnil(L, -1))
    info.m_iTracerFreq = luaL_checkint(L, -1);
  lua_pop(L, 1);
  lua_getfield(L, idx, "m_nFlags");
  if (!lua_isnil(L, -1))
    info.m_nFlags = luaL_checkint(L, -1);
  lua_pop(L, 1);
  lua_getfield(L, idx, "m_pAdditionalIgnoreEnt");
  if (!lua_isnil(L, -1))
    info.m_pAdditionalIgnoreEnt = lua_toentity(L, -1);
  lua_pop(L, 1);
  lua_getfield(L, idx, "m_pAttacker");
  if (!lua_isnil(L, -1))
    info.m_pAttacker = lua_toentity(L, -1);
  lua_pop(L, 1);
  lua_getfield(L, idx, "m_vecDirShooting");
  if (!lua_isnil(L, -1))
    info.m_vecDirShooting = luaL_checkvector(L, -1);
  lua_pop(L, 1);
  lua_getfield(L, idx, "m_vecSpread");
  if (!lua_isnil(L, -1))
    info.m_vecSpread = luaL_checkvector(L, -1);
  lua_pop(L, 1);
  lua_getfield(L, idx, "m_vecSrc");
  if (!lua_isnil(L, -1))
    info.m_vecSrc = luaL_checkvector(L, -1);
  lua_pop(L, 1);

  // HL2SB GMod SWEP compat: GMod bullet field names take priority when present.
  // GMod's ShootBullet always passes these keys, so an explicit 0 / origin value
  // must be honoured (the old "only if the m_* field is falsy" heuristic silently
  // dropped e.g. Damage = 0). Missing GMod keys still fall back to the m_* values
  // read above, so legacy HL2SB-style tables keep working.
  lua_getfield(L, idx, "Num");   // -> m_iShots
  if (!lua_isnil(L, -1))
    info.m_iShots = luaL_checkint(L, -1);
  lua_pop(L, 1);
  lua_getfield(L, idx, "Src");   // -> m_vecSrc
  if (!lua_isnil(L, -1))
    info.m_vecSrc = luaL_checkvector(L, -1);
  lua_pop(L, 1);
  lua_getfield(L, idx, "Dir");   // -> m_vecDirShooting
  if (!lua_isnil(L, -1))
    info.m_vecDirShooting = luaL_checkvector(L, -1);
  lua_pop(L, 1);
  lua_getfield(L, idx, "Spread");  // -> m_vecSpread
  if (!lua_isnil(L, -1))
    info.m_vecSpread = luaL_checkvector(L, -1);
  lua_pop(L, 1);
  lua_getfield(L, idx, "Damage");  // -> m_flDamage
  if (!lua_isnil(L, -1))
    info.m_flDamage = luaL_checknumber(L, -1);
  lua_pop(L, 1);
  lua_getfield(L, idx, "Force");   // -> m_flDamageForceScale
  if (!lua_isnil(L, -1))
    info.m_flDamageForceScale = luaL_checknumber(L, -1);
  lua_pop(L, 1);
  lua_getfield(L, idx, "AmmoType"); // -> m_iAmmoType (GMod passes a string ammo name)
  if (!lua_isnil(L, -1))
  {
    if (lua_type(L, -1) == LUA_TSTRING)
    {
      const char *pAmmo = lua_tostring(L, -1);
      int idx2 = GetAmmoDef()->Index(pAmmo);
      if (idx2 >= 0)
        info.m_iAmmoType = idx2;
    }
    else if (lua_isnumber(L, -1))
    {
      info.m_iAmmoType = luaL_checkint(L, -1);
    }
  }
  lua_pop(L, 1);
  lua_getfield(L, idx, "Tracer");   // -> m_iTracerFreq
  if (!lua_isnil(L, -1))
    info.m_iTracerFreq = luaL_checkint(L, -1);
  lua_pop(L, 1);

  // HL2SB GMod SWEP compat: GMod's ShootBullet sets bullet.Attacker (and
  // bullet.Inflictor).  FireBulletsInfo_t only carries m_pAttacker; ignore
  // Inflictor (HL2SB's FireBullets derives inflictor itself).
  lua_getfield(L, idx, "Attacker");  // -> m_pAttacker
  if (!lua_isnil(L, -1))
    info.m_pAttacker = lua_toentity(L, -1);
  lua_pop(L, 1);

  return info;
}

LUA_API void lua_toemitsound (lua_State *L, int idx, EmitSound_t &ep) {
  luaL_checktype(L, idx, LUA_TTABLE);
  lua_getfield(L, idx, "m_nChannel");
  if (!lua_isnil(L, -1))
    ep.m_nChannel = luaL_checkint(L, -1);
  lua_pop(L, 1);
  lua_getfield(L, idx, "m_pSoundName");
  if (!lua_isnil(L, -1))
    ep.m_pSoundName = luaL_checkstring(L, -1);
  lua_pop(L, 1);
  lua_getfield(L, idx, "m_flVolume");
  if (!lua_isnil(L, -1))
    ep.m_flVolume = luaL_checknumber(L, -1);
  lua_pop(L, 1);
  lua_getfield(L, idx, "m_SoundLevel");
  if (!lua_isnil(L, -1))
    ep.m_SoundLevel = (soundlevel_t)luaL_checkinteger(L, -1);
  lua_pop(L, 1);
  lua_getfield(L, idx, "m_nFlags");
  if (!lua_isnil(L, -1))
    ep.m_nFlags = luaL_checkint(L, -1);
  lua_pop(L, 1);
  lua_getfield(L, idx, "m_nPitch");
  if (!lua_isnil(L, -1))
    ep.m_nPitch = luaL_checkint(L, -1);
  lua_pop(L, 1);
  lua_getfield(L, idx, "m_pOrigin");
  if (!lua_isnil(L, -1))
    ep.m_pOrigin = &luaL_checkvector(L, -1);
  lua_pop(L, 1);
  lua_getfield(L, idx, "m_flSoundTime");
  if (!lua_isnil(L, -1))
    ep.m_flSoundTime = luaL_checknumber(L, -1);
  lua_pop(L, 1);
  lua_getfield(L, idx, "m_pflSoundDuration");
  if (!lua_isnil(L, -1)) {
    float duration = luaL_checknumber(L, -1);
    ep.m_pflSoundDuration = &duration;
  }
  lua_pop(L, 1);
}


//========= Copyright © 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#define leffect_dispatch_data_cpp

#include "cbase.h"
#if CLIENT_DLL
#include "c_te_effect_dispatch.h"
#else
#include "te_effect_dispatch.h"
#endif
#include "lua.hpp"
#include "luasrclib.h"
#include "leffect_dispatch_data.h"
#include "lbaseentity_shared.h"
#include "mathlib/lvector.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


/*
** access functions (stack -> C)
*/


LUA_API lua_CEffectData &lua_toeffect (lua_State *L, int idx) {
  lua_CEffectData *data = (lua_CEffectData *)lua_touserdata(L, idx);
  return *data;
}



/*
** push functions (C -> stack)
*/


LUA_API void lua_pusheffect (lua_State *L, lua_CEffectData &data) {
  lua_CEffectData *pData = (lua_CEffectData *)lua_newuserdata(L, sizeof(lua_CEffectData));
  *pData = data;
  luaL_getmetatable(L, "CEffectData");
  lua_setmetatable(L, -2);
}


LUALIB_API lua_CEffectData &luaL_checkeffect (lua_State *L, int narg) {
  lua_CEffectData *d = (lua_CEffectData *)luaL_checkudata(L, narg, "CEffectData");
  return *d;
}


#ifdef CLIENT_DLL
  static int CEffectData_entindex (lua_State *L) {
    lua_pushinteger(L, luaL_checkeffect(L, 1).entindex());
    return 1;
  }
#endif

static int CEffectData_GetEffectNameIndex (lua_State *L) {
  lua_pushinteger(L, luaL_checkeffect(L, 1).GetEffectNameIndex());
  return 1;
}

#ifdef CLIENT_DLL
  static int CEffectData_GetEntity (lua_State *L) {
    lua_pushentity(L, luaL_checkeffect(L, 1).GetEntity());
    return 1;
  }
#endif

/*
** HL2SB GMod compat: the accessors a GMod effect script actually calls.
**
** GMod's CEffectData exposes a Get/Set pair for every field, and
** lua/effects/*.lua is written against them -- rb655_nyan_tracer.lua's Init opens
** with
**
**     self.Position  = data:GetStart()
**     self.EndPos    = data:GetOrigin()
**     self.WeaponEnt = data:GetEntity()
**     self.Attachment = data:GetAttachment()
**
** and without them the whole effect died on the first line.  Only
** entindex/GetEffectNameIndex/GetEntity existed here; the field reads/writes in
** __index/__newindex below are not a substitute, because a method call is a
** metatable FUNCTION lookup, not a field lookup.
*/
#define HL2SB_EFFECTDATA_VECTOR_ACCESSORS( Name, Field )      \
  static int CEffectData_Get##Name (lua_State *L) {           \
    lua_pushvector(L, luaL_checkeffect(L, 1).Field);          \
    return 1;                                                 \
  }                                                           \
  static int CEffectData_Set##Name (lua_State *L) {           \
    luaL_checkeffect(L, 1).Field = luaL_checkvector(L, 2);    \
    return 0;                                                 \
  }

#define HL2SB_EFFECTDATA_ANGLE_ACCESSORS( Name, Field )       \
  static int CEffectData_Get##Name (lua_State *L) {           \
    lua_pushangle(L, luaL_checkeffect(L, 1).Field);           \
    return 1;                                                 \
  }                                                           \
  static int CEffectData_Set##Name (lua_State *L) {           \
    luaL_checkeffect(L, 1).Field = luaL_checkangle(L, 2);     \
    return 0;                                                 \
  }

#define HL2SB_EFFECTDATA_NUMBER_ACCESSORS( Name, Field )      \
  static int CEffectData_Get##Name (lua_State *L) {           \
    lua_pushnumber(L, luaL_checkeffect(L, 1).Field);          \
    return 1;                                                 \
  }                                                           \
  static int CEffectData_Set##Name (lua_State *L) {           \
    luaL_checkeffect(L, 1).Field = (float)luaL_checknumber(L, 2); \
    return 0;                                                 \
  }

#define HL2SB_EFFECTDATA_INT_ACCESSORS( Name, Field )         \
  static int CEffectData_Get##Name (lua_State *L) {           \
    lua_pushinteger(L, luaL_checkeffect(L, 1).Field);         \
    return 1;                                                 \
  }                                                           \
  static int CEffectData_Set##Name (lua_State *L) {           \
    luaL_checkeffect(L, 1).Field = luaL_checkint(L, 2);       \
    return 0;                                                 \
  }

HL2SB_EFFECTDATA_VECTOR_ACCESSORS( Start,  m_vStart )
HL2SB_EFFECTDATA_VECTOR_ACCESSORS( Origin, m_vOrigin )
HL2SB_EFFECTDATA_VECTOR_ACCESSORS( Normal, m_vNormal )
HL2SB_EFFECTDATA_ANGLE_ACCESSORS( Angles,  m_vAngles )
HL2SB_EFFECTDATA_NUMBER_ACCESSORS( Scale,     m_flScale )
HL2SB_EFFECTDATA_NUMBER_ACCESSORS( Magnitude, m_flMagnitude )
HL2SB_EFFECTDATA_NUMBER_ACCESSORS( Radius,    m_flRadius )
HL2SB_EFFECTDATA_INT_ACCESSORS( Flags,        m_fFlags )
HL2SB_EFFECTDATA_INT_ACCESSORS( Attachment,   m_nAttachmentIndex )
HL2SB_EFFECTDATA_INT_ACCESSORS( HitBox,       m_nHitBox )
HL2SB_EFFECTDATA_INT_ACCESSORS( DamageType,   m_nDamageType )
HL2SB_EFFECTDATA_INT_ACCESSORS( Material,     m_nMaterial )
HL2SB_EFFECTDATA_INT_ACCESSORS( SurfaceProp,  m_nSurfaceProp )
HL2SB_EFFECTDATA_INT_ACCESSORS( Color,        m_nColor )

// GMod's GetEntIndex / SetEntIndex are aliases of the entity the effect belongs to.
#ifdef CLIENT_DLL
  static int CEffectData_GetEntIndex (lua_State *L) {
    lua_pushinteger(L, luaL_checkeffect(L, 1).entindex());
    return 1;
  }
#else
  static int CEffectData_GetEntIndex (lua_State *L) {
    lua_pushinteger(L, luaL_checkeffect(L, 1).m_nEntIndex);
    return 1;
  }
  static int CEffectData_SetEntIndex (lua_State *L) {
    luaL_checkeffect(L, 1).m_nEntIndex = luaL_checkint(L, 2);
    return 0;
  }
#endif

static int CEffectData___index (lua_State *L) {
  CEffectData data = luaL_checkeffect(L, 1);
  const char *field = luaL_checkstring(L, 2);
  if (Q_strcmp(field, "m_fFlags") == 0)
    lua_pushinteger(L, data.m_fFlags);
  else if (Q_strcmp(field, "m_flMagnitude") == 0)
    lua_pushnumber(L, data.m_flMagnitude);
  else if (Q_strcmp(field, "m_flRadius") == 0)
    lua_pushnumber(L, data.m_flRadius);
  else if (Q_strcmp(field, "m_flScale") == 0)
    lua_pushnumber(L, data.m_flScale);
#ifdef CLIENT_DLL
  else if (Q_strcmp(field, "m_hEntity") == 0)
    lua_pushentity(L, C_BaseEntity::Instance( data.m_hEntity ));
#else
  else if (Q_strcmp(field, "m_nEntIndex") == 0)
    lua_pushinteger(L, data.m_nEntIndex);
#endif
  else if (Q_strcmp(field, "m_nAttachmentIndex") == 0)
    lua_pushinteger(L, data.m_nAttachmentIndex);
  else if (Q_strcmp(field, "m_nDamageType") == 0)
    lua_pushinteger(L, data.m_nDamageType);
  else if (Q_strcmp(field, "m_nHitBox") == 0)
    lua_pushinteger(L, data.m_nHitBox);
  else if (Q_strcmp(field, "m_nMaterial") == 0)
    lua_pushinteger(L, data.m_nMaterial);
  else if (Q_strcmp(field, "m_nSurfaceProp") == 0)
    lua_pushinteger(L, data.m_nSurfaceProp);
  else if (Q_strcmp(field, "m_vAngles") == 0)
    lua_pushangle(L, data.m_vAngles);
  else if (Q_strcmp(field, "m_vNormal") == 0)
    lua_pushvector(L, data.m_vNormal);
  else if (Q_strcmp(field, "m_vOrigin") == 0)
    lua_pushvector(L, data.m_vOrigin);
  else if (Q_strcmp(field, "m_vStart") == 0)
    lua_pushvector(L, data.m_vStart);
  else {
    lua_getmetatable(L, 1);
    lua_pushvalue(L, 2);
    lua_gettable(L, -2);
  }
  return 1;
}

static int CEffectData___newindex (lua_State *L) {
  CEffectData data = luaL_checkeffect(L, 1);
  const char *field = luaL_checkstring(L, 2);
  if (Q_strcmp(field, "m_fFlags") == 0)
    data.m_fFlags = luaL_checkint(L, 3);
  else if (Q_strcmp(field, "m_flMagnitude") == 0)
    data.m_flMagnitude = luaL_checknumber(L, 3);
  else if (Q_strcmp(field, "m_flRadius") == 0)
    data.m_flRadius = luaL_checknumber(L, 3);
  else if (Q_strcmp(field, "m_flScale") == 0)
    data.m_flScale = luaL_checknumber(L, 3);
#ifdef CLIENT_DLL
  else if (Q_strcmp(field, "m_hEntity") == 0)
    data.m_hEntity = lua_toentity(L, 3)->GetRefEHandle();
#else
  else if (Q_strcmp(field, "m_nEntIndex") == 0)
    data.m_nEntIndex = luaL_checkint(L, 3);
#endif
  else if (Q_strcmp(field, "m_nAttachmentIndex") == 0)
    data.m_nAttachmentIndex = luaL_checkint(L, 3);
  else if (Q_strcmp(field, "m_nDamageType") == 0)
    data.m_nDamageType = luaL_checkint(L, 3);
  else if (Q_strcmp(field, "m_nHitBox") == 0)
    data.m_nHitBox = luaL_checkint(L, 3);
  else if (Q_strcmp(field, "m_nMaterial") == 0)
    data.m_nMaterial = luaL_checkint(L, 3);
  else if (Q_strcmp(field, "m_nSurfaceProp") == 0)
    data.m_nSurfaceProp = luaL_checkint(L, 3);
  else if (Q_strcmp(field, "m_vAngles") == 0)
    data.m_vAngles = luaL_checkangle(L, 3);
  else if (Q_strcmp(field, "m_vNormal") == 0)
    data.m_vNormal = luaL_checkvector(L, 3);
  else if (Q_strcmp(field, "m_vOrigin") == 0)
    data.m_vOrigin = luaL_checkvector(L, 3);
  else if (Q_strcmp(field, "m_vStart") == 0)
    data.m_vStart = luaL_checkvector(L, 3);
  return 0;
}

static int CEffectData___tostring (lua_State *L) {
  lua_pushfstring(L, "CEffectData: %p", luaL_checkudata(L, 1, "CEffectData"));
  return 1;
}


static const luaL_Reg CEffectDatameta[] = {
#ifdef CLIENT_DLL
  {"entindex", CEffectData_entindex},
#endif
  {"GetEffectNameIndex", CEffectData_GetEffectNameIndex},
#ifdef CLIENT_DLL
  {"GetEntity", CEffectData_GetEntity},
#endif
  // HL2SB GMod compat: the field accessors lua/effects/*.lua is written against.
  {"GetStart", CEffectData_GetStart},
  {"SetStart", CEffectData_SetStart},
  {"GetOrigin", CEffectData_GetOrigin},
  {"SetOrigin", CEffectData_SetOrigin},
  {"GetNormal", CEffectData_GetNormal},
  {"SetNormal", CEffectData_SetNormal},
  {"GetAngles", CEffectData_GetAngles},
  {"SetAngles", CEffectData_SetAngles},
  {"GetScale", CEffectData_GetScale},
  {"SetScale", CEffectData_SetScale},
  {"GetMagnitude", CEffectData_GetMagnitude},
  {"SetMagnitude", CEffectData_SetMagnitude},
  {"GetRadius", CEffectData_GetRadius},
  {"SetRadius", CEffectData_SetRadius},
  {"GetFlags", CEffectData_GetFlags},
  {"SetFlags", CEffectData_SetFlags},
  {"GetAttachment", CEffectData_GetAttachment},
  {"SetAttachment", CEffectData_SetAttachment},
  {"GetHitBox", CEffectData_GetHitBox},
  {"SetHitBox", CEffectData_SetHitBox},
  {"GetDamageType", CEffectData_GetDamageType},
  {"SetDamageType", CEffectData_SetDamageType},
  {"GetMaterial", CEffectData_GetMaterial},
  {"SetMaterial", CEffectData_SetMaterial},
  {"GetSurfaceProp", CEffectData_GetSurfaceProp},
  {"SetSurfaceProp", CEffectData_SetSurfaceProp},
  {"GetColor", CEffectData_GetColor},
  {"SetColor", CEffectData_SetColor},
  {"GetEntIndex", CEffectData_GetEntIndex},
#ifndef CLIENT_DLL
  {"SetEntIndex", CEffectData_SetEntIndex},
#endif
  {"__index", CEffectData___index},
  {"__newindex", CEffectData___newindex},
  {"__tostring", CEffectData___tostring},
  {NULL, NULL}
};


static int luasrc_CEffectData (lua_State *L) {
  CEffectData data = CEffectData();
  lua_pusheffect(L, data);
  return 1;
}

static int luasrc_DispatchEffect (lua_State *L) {
  DispatchEffect( luaL_checkstring(L, 1), luaL_checkeffect(L, 2) );
  return 0;
}


static const luaL_Reg CEffectData_funcs[] = {
  {"CEffectData", luasrc_CEffectData},
  {"DispatchEffect", luasrc_DispatchEffect},
  {NULL, NULL}
};


/*
** Open CEffectData object
*/
LUALIB_API int luaopen_CEffectData (lua_State *L) {
  luaL_newmetatable(L, LUA_EFFECTDATALIBNAME);
  luaL_register(L, NULL, CEffectDatameta);
  lua_pushstring(L, "effect");
  lua_setfield(L, -2, "__type");  /* metatable.__type = "effect" */
  luaL_register(L, "_G", CEffectData_funcs);

  /*
  ** HL2SB GMod compat: GMod's constructor is EffectData(), not the Team Sandbox
  ** CEffectData().  Both names are published; gmod_camera/shared.lua:190 and
  ** every lua/effects/*.lua script that builds effect data use the GMod spelling,
  ** and GMod's own bootstrap defines the alias in C (the Lua shim that would have
  ** done it, modules/gmod_compatibility/sh_init.lua, is never loaded here -- the
  ** folder pass does not recurse into gmod_compatibility/).
  */
  lua_pushcfunction(L, luasrc_CEffectData);
  lua_setglobal(L, "EffectData");

  lua_pop(L, 1);
  return 1;
}


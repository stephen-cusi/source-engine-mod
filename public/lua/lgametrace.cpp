//========= Copyright © 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=============================================================================//

#define lgametrace_cpp

#include "cbase.h"
#include "luamanager.h"
#include "luasrclib.h"
#include "lgametrace.h"
#include "lcmodel.h"
#include "lbaseentity_shared.h"
#include "mathlib/lvector.h"
// HL2SB: tr.MatType / SurfaceProps read the engine's surface-property database
// (surfacedata_t::game.material); bspflags.h carries the sky / no-draw flags
// for HitSky / HitNoDraw.
#include "vphysics_interface.h"
#include "bspflags.h"
// HL2SB: HitBoxBone reads the hitbox-set-0 table of the hit entity's studio
// model.
#include "studio.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


/*
** access functions (stack -> C)
*/


LUA_API lua_CGameTrace &lua_totrace (lua_State *L, int idx) {
  lua_CGameTrace *tr = (lua_CGameTrace *)lua_touserdata(L, idx);
  return *tr;
}



/*
** push functions (C -> stack)
*/


LUA_API void lua_pushtrace (lua_State *L, lua_CGameTrace &tr) {
  lua_CGameTrace *ptr = (lua_CGameTrace *)lua_newuserdata(L, sizeof(lua_CGameTrace));
  *ptr = tr;
  luaL_getmetatable(L, "CGameTrace");
  lua_setmetatable(L, -2);
}


LUALIB_API lua_CGameTrace &luaL_checktrace (lua_State *L, int narg) {
  lua_CGameTrace *d = (lua_CGameTrace *)luaL_checkudata(L, narg, "CGameTrace");
  return *d;
}


static int CGameTrace_DidHit (lua_State *L) {
  lua_pushboolean(L, luaL_checktrace(L, 1).DidHit());
  return 1;
}

static int CGameTrace_DidHitNonWorldEntity (lua_State *L) {
  lua_pushboolean(L, luaL_checktrace(L, 1).DidHitNonWorldEntity());
  return 1;
}

static int CGameTrace_DidHitWorld (lua_State *L) {
  lua_pushboolean(L, luaL_checktrace(L, 1).DidHitWorld());
  return 1;
}

static int CGameTrace_GetEntityIndex (lua_State *L) {
  lua_pushinteger(L, luaL_checktrace(L, 1).GetEntityIndex());
  return 1;
}

static int CGameTrace_IsDispSurface (lua_State *L) {
  lua_pushboolean(L, luaL_checktrace(L, 1).IsDispSurface());
  return 1;
}

static int CGameTrace_IsDispSurfaceBuildable (lua_State *L) {
  lua_pushboolean(L, luaL_checktrace(L, 1).IsDispSurfaceBuildable());
  return 1;
}

static int CGameTrace_IsDispSurfaceProp1 (lua_State *L) {
  lua_pushboolean(L, luaL_checktrace(L, 1).IsDispSurfaceProp1());
  return 1;
}

static int CGameTrace_IsDispSurfaceProp2 (lua_State *L) {
  lua_pushboolean(L, luaL_checktrace(L, 1).IsDispSurfaceProp2());
  return 1;
}

static int CGameTrace_IsDispSurfaceWalkable (lua_State *L) {
  lua_pushboolean(L, luaL_checktrace(L, 1).IsDispSurfaceWalkable());
  return 1;
}

static int CGameTrace___index (lua_State *L) {
  trace_t tr = luaL_checktrace(L, 1);
  const char *field = luaL_checkstring(L, 2);
  if (Q_strcmp(field, "allsolid") == 0)
    lua_pushboolean(L, tr.allsolid);
  else if (Q_strcmp(field, "contents") == 0)
    lua_pushinteger(L, tr.contents);
  else if (Q_strcmp(field, "dispFlags") == 0)
    lua_pushinteger(L, tr.dispFlags);
  else if (Q_strcmp(field, "endpos") == 0)
    lua_pushvector(L, tr.endpos);
  else if (Q_strcmp(field, "fraction") == 0)
    lua_pushnumber(L, tr.fraction);
  else if (Q_strcmp(field, "fractionleftsolid") == 0)
    lua_pushnumber(L, tr.fractionleftsolid);
  else if (Q_strcmp(field, "hitbox") == 0)
    lua_pushinteger(L, tr.hitbox);
  else if (Q_strcmp(field, "hitgroup") == 0)
    lua_pushinteger(L, tr.hitgroup);
  else if (Q_strcmp(field, "m_pEnt") == 0 || Q_strcmp(field, "HitEntity") == 0 || Q_strcmp(field, "Entity") == 0)
    // Use the hit entity's native Lua type, as scripted callbacks do. LuaJIT
    // requires distinct userdata to use the same __eq metamethod for equality.
    CBaseEntity::PushLuaInstanceSafe(L, tr.m_pEnt);
  else if (Q_strcmp(field, "physicsbone") == 0 || Q_strcmp(field, "PhysicsBone") == 0)
    lua_pushinteger(L, tr.physicsbone);
  else if (Q_strcmp(field, "startpos") == 0)
    lua_pushvector(L, tr.startpos);
  else if (Q_strcmp(field, "startsolid") == 0)
    lua_pushboolean(L, tr.startsolid);
  else if (Q_strcmp(field, "surface") == 0)
    lua_pushcsurface(L, tr.surface);
  // HL2SB GMod SWEP compat: accept GMod trace field names.
  else if (Q_strcmp(field, "HitPos") == 0)
    lua_pushvector(L, tr.endpos);
  else if (Q_strcmp(field, "StartPos") == 0)
    lua_pushvector(L, tr.startpos);
  else if (Q_strcmp(field, "HitNormal") == 0)
    lua_pushvector(L, tr.plane.normal);
  else if (Q_strcmp(field, "Fraction") == 0)
    lua_pushnumber(L, tr.fraction);
  else if (Q_strcmp(field, "HitWorld") == 0)
    lua_pushboolean(L, tr.DidHitWorld());
  // GMod's "Hit" also covers traces that never got out of the solid they
  // started in: the reference table builder answers true for
  // fraction < 1 OR AllSolid OR StartSolid (a fully-swallowed trace has
  // fraction 1 in some of those cases and would read false otherwise).
  else if (Q_strcmp(field, "Hit") == 0)
    lua_pushboolean(L, tr.fraction < 1.0f || tr.allsolid || tr.startsolid);
  // HL2SB GMod compat: the hitbox / hitgroup under GMod's own capitalisation.
  // The lowercase spellings above are this fork's, and a Lua script reading the
  // GMod one got nil -- silently.  SCP-096's nextbot keys its entire aggro on
  // exactly that:
  //
  //     local tr = util.TraceLine( util.GetPlayerTrace( v ) )
  //     if target == self && tr.HitBox == 0 then      -- init.lua:745
  //
  // i.e. "the player's crosshair is on my hitbox 0".  With HitBox nil the test
  // was false forever, so looking at it never started anything.
  else if (Q_strcmp(field, "HitBox") == 0)
    lua_pushinteger(L, tr.hitbox);
  else if (Q_strcmp(field, "HitGroup") == 0)
    lua_pushinteger(L, tr.hitgroup);
  // GMod's last TraceResult key: the bone owning the traced hitbox, filled
  // only for a studio-model (non-world) hit whose hitbox set 0 carries the
  // hitbox index; every other case reads nil.
  else if (Q_strcmp(field, "HitBoxBone") == 0)
  {
    const model_t *pModel = ( tr.m_pEnt != NULL && !tr.DidHitWorld() ) ? tr.m_pEnt->GetModel() : NULL;
    const studiohdr_t *pHdr = ( pModel != NULL ) ? modelinfo->GetStudiomodel( (model_t *)pModel ) : NULL;
    const mstudiohitboxset_t *pSet = ( pHdr != NULL && pHdr->numhitboxsets > 0 ) ? pHdr->pHitboxSet( 0 ) : NULL;
    if ( pSet != NULL && tr.hitbox >= 0 && tr.hitbox < pSet->numhitboxes )
      lua_pushinteger( L, pSet->pHitbox( tr.hitbox )->bone );
    else
      lua_pushnil( L );
  }
  // HL2SB GMod compat: the remaining GMod-cased TraceResult fields.  scp173's
  // teleport search branches on tr.StartSolid (util.TraceLineEx polyfill feeds
  // it every result); nil made "not tr.StartSolid" always true so the search
  // could settle inside geometry.
  else if (Q_strcmp(field, "StartSolid") == 0)
    lua_pushboolean(L, tr.startsolid);
  else if (Q_strcmp(field, "AllSolid") == 0)
    lua_pushboolean(L, tr.allsolid);
  else if (Q_strcmp(field, "FractionLeftSolid") == 0)
    lua_pushnumber(L, tr.fractionleftsolid);
  // HL2SB GMod compat: GMod's "Normal" is the DIRECTION of the trace (unit
  // endpos - startpos), NOT the impact plane normal -- that lives under
  // "HitNormal".  reference from GMod x64 server.dll: the shared trace-table
  // builder used by util.TraceLine/TraceHull/TraceEntity/TraceEntityHull
  // normalizes endpos - startpos, and on a zero-length difference falls back
  // to the negated plane normal, and if that is zero too, to (0,0,1).
  // scp173 keys its entire vision cone on
  //     local aimVec = currVictim:GetEyeTraceNoCursor().Normal
  // with plane.normal instead (or (0,0,0) when the eye ray misses), the
  // view-offset angle came out ~180 degrees while the player stared straight
  // at the statue, so it teleported and neck-snapped under observation.
  else if (Q_strcmp(field, "Normal") == 0)
  {
    Vector vecDir = tr.endpos - tr.startpos;
    float flLen = vecDir.Length();
    if ( flLen != 0.0f )
    {
      vecDir /= flLen;
    }
    else if ( tr.plane.normal.LengthSqr() != 0.0f )
    {
      vecDir.Init( -tr.plane.normal.x, -tr.plane.normal.y, -tr.plane.normal.z );
    }
    else
    {
      vecDir.Init( 0.0f, 0.0f, 1.0f );
    }
    lua_pushvector(L, vecDir);
  }
  // HL2SB GMod compat: the remaining GMod TraceResult fields hl1sweps reads
  // (tr.MatType drives the crowbar's per-material impact sounds, tr.HitSky
  // reflects the gauss beam off the sky, tr.SurfaceProps feeds its decal and
  // physics-force paths).  MatType is the material character the engine's
  // surface-property database carries for the hit surface (the same CHAR_TEX
  // value every impact effect branches on); HitSky is the sky surface flag.
  // GMod only fills "MatType" on a trace that actually hit something; a miss
  // leaves the key absent (reads nil) instead of answering the worldspawn
  // default material.
  else if (Q_strcmp(field, "MatType") == 0)
  {
    if ( tr.fraction < 1.0f || tr.allsolid || tr.startsolid )
    {
      extern IPhysicsSurfaceProps *physprops;
      if ( physprops != NULL )
      {
        surfacedata_t *pSurface = physprops->GetSurfaceData( tr.surface.surfaceProps );
        if ( pSurface != NULL )
          lua_pushinteger(L, pSurface->game.material);
        else
          lua_pushnil(L);
      }
      else
      {
        lua_pushnil(L);
      }
    }
    else
    {
      lua_pushnil(L);
    }
  }
  else if (Q_strcmp(field, "SurfaceProps") == 0)
    lua_pushinteger(L, tr.surface.surfaceProps);
  else if (Q_strcmp(field, "HitSky") == 0)
    lua_pushboolean(L, (tr.surface.flags & (SURF_SKY | SURF_SKY2D)) != 0);
  else if (Q_strcmp(field, "HitTexture") == 0)
  {
    // reference keeps the key absent (nil here) when the surface carries no
    // material name, rather than answering an empty string.
    if ( tr.surface.name != NULL )
      lua_pushstring(L, tr.surface.name);
    else
      lua_pushnil(L);
  }
  else if (Q_strcmp(field, "SurfaceFlags") == 0)
    lua_pushinteger(L, tr.surface.flags);
  else if (Q_strcmp(field, "DispFlags") == 0)
    lua_pushinteger(L, tr.dispFlags);
  else if (Q_strcmp(field, "Contents") == 0)
    lua_pushinteger(L, tr.contents);
  else if (Q_strcmp(field, "HitNoDraw") == 0)
    lua_pushboolean(L, (tr.surface.flags & SURF_NODRAW) != 0);
  else if (Q_strcmp(field, "HitNonWorld") == 0)
    lua_pushboolean(L, tr.DidHitNonWorldEntity());
  else {
    lua_getmetatable(L, 1);
    lua_pushvalue(L, 2);
    lua_gettable(L, -2);
  }
  return 1;
}

static int CGameTrace___newindex (lua_State *L) {
  const char *field = luaL_checkstring(L, 2);
  if (Q_strcmp(field, "allsolid") == 0)
    luaL_checktrace(L, 1).allsolid = luaL_checkboolean(L, 3);
  else if (Q_strcmp(field, "contents") == 0)
    luaL_checktrace(L, 1).contents = luaL_checkint(L, 3);
  else if (Q_strcmp(field, "dispFlags") == 0)
    luaL_checktrace(L, 1).dispFlags = luaL_checkint(L, 3);
  else if (Q_strcmp(field, "endpos") == 0)
    luaL_checktrace(L, 1).endpos = luaL_checkvector(L, 3);
  else if (Q_strcmp(field, "fraction") == 0)
    luaL_checktrace(L, 1).fraction = luaL_checknumber(L, 3);
  else if (Q_strcmp(field, "fractionleftsolid") == 0)
    luaL_checktrace(L, 1).fractionleftsolid = luaL_checknumber(L, 3);
  else if (Q_strcmp(field, "hitbox") == 0)
    luaL_checktrace(L, 1).hitbox = luaL_checkint(L, 3);
  else if (Q_strcmp(field, "hitgroup") == 0)
    luaL_checktrace(L, 1).hitgroup = luaL_checkint(L, 3);
  else if (Q_strcmp(field, "m_pEnt") == 0)
    luaL_checktrace(L, 1).m_pEnt = lua_toentity(L, 3);
  else if (Q_strcmp(field, "physicsbone") == 0 || Q_strcmp(field, "PhysicsBone") == 0)
    luaL_checktrace(L, 1).physicsbone = luaL_checkint(L, 3);
  else if (Q_strcmp(field, "startpos") == 0)
    luaL_checktrace(L, 1).startpos = luaL_checkvector(L, 3);
  else if (Q_strcmp(field, "startsolid") == 0)
    luaL_checktrace(L, 1).startsolid = luaL_checkboolean(L, 3);
  return 0;
}

static int CGameTrace___tostring (lua_State *L) {
  lua_pushfstring(L, "CGameTrace: %p", luaL_checkudata(L, 1, "CGameTrace"));
  return 1;
}


static const luaL_Reg CGameTracemeta[] = {
  {"DidHit", CGameTrace_DidHit},
  {"DidHitNonWorldEntity", CGameTrace_DidHitNonWorldEntity},
  {"DidHitWorld", CGameTrace_DidHitWorld},
  {"GetEntityIndex", CGameTrace_GetEntityIndex},
  {"IsDispSurface", CGameTrace_IsDispSurface},
  {"IsDispSurfaceBuildable", CGameTrace_IsDispSurfaceBuildable},
  {"IsDispSurfaceProp1", CGameTrace_IsDispSurfaceProp1},
  {"IsDispSurfaceProp2", CGameTrace_IsDispSurfaceProp2},
  {"IsDispSurfaceWalkable", CGameTrace_IsDispSurfaceWalkable},
  {"__index", CGameTrace___index},
  {"__newindex", CGameTrace___newindex},
  {"__tostring", CGameTrace___tostring},
  {NULL, NULL}
};


static int luasrc_trace_t (lua_State *L) {
  trace_t tr;
  memset( &tr, 0, sizeof( trace_t ) );
  lua_pushtrace(L, tr);
  return 1;
}


static const luaL_Reg CGameTrace_funcs[] = {
  {"trace_t", luasrc_trace_t},
  {NULL, NULL}
};


/*
** Open CGameTrace object
*/
LUALIB_API int luaopen_CGameTrace (lua_State *L) {
  luaL_newmetatable(L, LUA_GAMETRACELIBNAME);
  luaL_register(L, NULL, CGameTracemeta);
  lua_pushstring(L, "trace");
  lua_setfield(L, -2, "__type");  /* metatable.__type = "trace" */
  luaL_register(L, "_G", CGameTrace_funcs);
  lua_pop(L, 1);
  return 1;
}


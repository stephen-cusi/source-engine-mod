//========= Copyright © 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $Workfile:     $
// $Date:         $
// $NoKeywords: $
//=============================================================================//

#define lbspflags_cpp

#include "cbase.h"
#include "bspflags.h"
#include "luamanager.h"
#include "luasrclib.h"


// HL2SB: publish one SURF_* member under two _E keys.
//
// GMod's lua/includes/modules/gmod_compatibility/sh_enumerations.lua reads
// `_E.SURFACE` (:131) and merges it as SURFACE = "SURF", i.e. it wants the
// SURF_* globals (SURF_LIGHT, SURF_SKY, SURF_NODRAW, ...).  Experiment: Source
// published the table under `_E.SURF` only, and the ported file's
// `_E.SURFACE = _E.SURFACE or {}` stub then fed the merge loop an EMPTY table,
// so every SURF_* global was nil after this file ran -- LUA_SURFLIBNAME has
// been "SURF" since the port.
//
// Rather than rename the key (which would break any existing reader of
// _E.SURF) both keys are published from the same SURF_* constants, so they
// cannot disagree.  Values stay tied to the C++ SURF_* names; only the table
// key differs.
#define HL2SB_PUSH_SURF_ENUM_LIB( L, lib, ... ) \
  LUA_SET_ENUM_LIB_BEGIN( L, lib ); \
  __VA_ARGS__ \
  LUA_SET_ENUM_LIB_END( L )

#define HL2SB_PUSH_SURF_MEMBERS( L ) \
    lua_pushenum(L, SURF_LIGHT, "LIGHT"); \
    lua_pushenum(L, SURF_SKY2D, "SKY2D"); \
    lua_pushenum(L, SURF_SKY, "SKY"); \
    lua_pushenum(L, SURF_WARP, "WARP"); \
    lua_pushenum(L, SURF_TRANS, "TRANS"); \
    lua_pushenum(L, SURF_NOPORTAL, "NOPORTAL"); \
    lua_pushenum(L, SURF_TRIGGER, "TRIGGER"); \
    lua_pushenum(L, SURF_NODRAW, "NODRAW"); \
    lua_pushenum(L, SURF_HINT, "HINT"); \
    lua_pushenum(L, SURF_SKIP, "SKIP"); \
    lua_pushenum(L, SURF_NOLIGHT, "NOLIGHT"); \
    lua_pushenum(L, SURF_BUMPLIGHT, "BUMPLIGHT"); \
    lua_pushenum(L, SURF_NOSHADOWS, "NOSHADOWS"); \
    lua_pushenum(L, SURF_NODECALS, "NODECALS"); \
    lua_pushenum(L, SURF_NOCHOP, "NOCHOP"); \
    lua_pushenum(L, SURF_HITBOX, "HITBOX");


/*
** Open CONTENTS library
*/
LUALIB_API int luaopen_CONTENTS (lua_State *L) {
  BEGIN_LUA_SET_ENUM_LIB(L, LUA_CONTENTSLIBNAME);
    lua_pushenum(L, CONTENTS_EMPTY, "EMPTY");

    lua_pushenum(L, CONTENTS_SOLID, "SOLID");
    lua_pushenum(L, CONTENTS_WINDOW, "WINDOW");
    lua_pushenum(L, CONTENTS_AUX, "AUX");
    lua_pushenum(L, CONTENTS_GRATE, "GRATE");
    lua_pushenum(L, CONTENTS_SLIME, "SLIME");
    lua_pushenum(L, CONTENTS_WATER, "WATER");
    lua_pushenum(L, CONTENTS_BLOCKLOS, "BLOCKLOS");
    lua_pushenum(L, CONTENTS_OPAQUE, "OPAQUE");

    lua_pushenum(L, CONTENTS_TESTFOGVOLUME, "TESTFOGVOLUME");
    lua_pushenum(L, CONTENTS_UNUSED, "UNUSED");

    lua_pushenum(L, CONTENTS_TEAM1, "TEAM1");
    lua_pushenum(L, CONTENTS_TEAM2, "TEAM2");

    lua_pushenum(L, CONTENTS_IGNORE_NODRAW_OPAQUE, "IGNORE_NODRAW_OPAQUE");

    lua_pushenum(L, CONTENTS_MOVEABLE, "MOVEABLE");

    lua_pushenum(L, CONTENTS_AREAPORTAL, "AREAPORTAL");

	lua_pushenum(L, CONTENTS_PLAYERCLIP, "PLAYERCLIP");
	lua_pushenum(L, CONTENTS_MONSTERCLIP, "MONSTERCLIP");

	lua_pushenum(L, CONTENTS_CURRENT_0, "CURRENT_0");
	lua_pushenum(L, CONTENTS_CURRENT_90, "CURRENT_90");
	lua_pushenum(L, CONTENTS_CURRENT_180, "CURRENT_180");
	lua_pushenum(L, CONTENTS_CURRENT_270, "CURRENT_270");
	lua_pushenum(L, CONTENTS_CURRENT_UP, "CURRENT_UP");
	lua_pushenum(L, CONTENTS_CURRENT_DOWN, "CURRENT_DOWN");

	lua_pushenum(L, CONTENTS_ORIGIN, "ORIGIN");

	lua_pushenum(L, CONTENTS_MONSTER, "MONSTER");
	lua_pushenum(L, CONTENTS_DEBRIS, "DEBRIS");
	lua_pushenum(L, CONTENTS_DETAIL, "DETAIL");
	lua_pushenum(L, CONTENTS_TRANSLUCENT, "TRANSLUCENT");
	lua_pushenum(L, CONTENTS_LADDER, "LADDER");
	lua_pushenum(L, CONTENTS_HITBOX, "HITBOX");
  END_LUA_SET_ENUM_LIB(L);
  return 0;
}


/*
** Open SURF library
**
** HL2SB: the same table is published under BOTH the Team Sandbox key (_E.SURF)
** and GMod's key (_E.SURFACE), because sh_enumerations.lua reads the GMod one
** and the members have to come from the same SURF_* constants.  See the macros
** at the top of this file.
*/
LUALIB_API int luaopen_SURF (lua_State *L) {
  HL2SB_PUSH_SURF_ENUM_LIB( L, LUA_SURFLIBNAME, HL2SB_PUSH_SURF_MEMBERS( L ) );
  HL2SB_PUSH_SURF_ENUM_LIB( L, LUA_SURFACEENUMNAME, HL2SB_PUSH_SURF_MEMBERS( L ) );
  return 0;
}


/*
** Open MASK library
*/
LUALIB_API int luaopen_MASK (lua_State *L) {
  BEGIN_LUA_SET_ENUM_LIB(L, LUA_MASKLIBNAME);
    lua_pushenum(L, MASK_ALL, "ALL");
    lua_pushenum(L, MASK_SOLID, "SOLID");
    lua_pushenum(L, MASK_PLAYERSOLID, "PLAYERSOLID");
    lua_pushenum(L, MASK_NPCSOLID, "NPCSOLID");
    lua_pushenum(L, MASK_WATER, "WATER");
    lua_pushenum(L, MASK_OPAQUE, "OPAQUE");
    lua_pushenum(L, MASK_OPAQUE_AND_NPCS, "OPAQUE_AND_NPCS");
    lua_pushenum(L, MASK_BLOCKLOS, "BLOCKLOS");
    lua_pushenum(L, MASK_BLOCKLOS_AND_NPCS, "BLOCKLOS_AND_NPCS");
    lua_pushenum(L, MASK_VISIBLE, "VISIBLE");
    lua_pushenum(L, MASK_VISIBLE_AND_NPCS, "VISIBLE_AND_NPCS");
    lua_pushenum(L, MASK_SHOT, "SHOT");
    lua_pushenum(L, MASK_SHOT_HULL, "SHOT_HULL");
    lua_pushenum(L, MASK_SHOT_PORTAL, "SHOT_PORTAL");
    lua_pushenum(L, MASK_SOLID_BRUSHONLY, "SOLID_BRUSHONLY");
    lua_pushenum(L, MASK_PLAYERSOLID_BRUSHONLY, "PLAYERSOLID_BRUSHONLY");
    lua_pushenum(L, MASK_NPCSOLID_BRUSHONLY, "NPCSOLID_BRUSHONLY");
    lua_pushenum(L, MASK_NPCWORLDSTATIC, "NPCWORLDSTATIC");
    lua_pushenum(L, MASK_SPLITAREAPORTAL, "SPLITAREAPORTAL");

    lua_pushenum(L, MASK_CURRENT, "CURRENT");

    lua_pushenum(L, MASK_DEADSOLID, "DEADSOLID");
  END_LUA_SET_ENUM_LIB(L);
  return 0;
}

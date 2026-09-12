//========= Copyright © 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#ifndef LUAMANAGER_H
#define LUAMANAGER_H
#pragma once

#include "lua.hpp"

#define LUA_ROOT							"lua" // Can't be "LUA_PATH" because luaconf.h uses it.
#define LUA_PATH_CACHE				"lua_cache"
#define LUA_PATH_ADDONS				"addons"
#define LUA_PATH_ENUM					LUA_ROOT "/includes/enum"
#define LUA_PATH_EXTENSIONS		LUA_ROOT "/includes/extensions"
#define LUA_PATH_MODULES			LUA_ROOT "/includes/modules"
#define LUA_PATH_INCLUDES			LUA_ROOT "/includes"
#define LUA_PATH_GAME_CLIENT	LUA_ROOT "/game/client"
#define LUA_PATH_GAME_SERVER	LUA_ROOT "/game/server"
#define LUA_PATH_GAME_SHARED	LUA_ROOT "/game/shared"
#define LUA_PATH_EFFECTS			LUA_ROOT "/effects"
#define LUA_PATH_ENTITIES			LUA_ROOT "/entities"
#define LUA_PATH_GAMEUI				LUA_ROOT "/gameui"
#define LUA_PATH_WEAPONS			LUA_ROOT "/weapons"

// HL2SB: Garry's Mod's autorun roots (wiki: Lua_Loading_Order).  GMod runs
// lua/autorun/*.lua and then lua/autorun/<realm>/** (recursing into subfolders --
// that is how lua/autorun/server/sensorbones/*.lua ships), alphabetically.
#define LUA_PATH_AUTORUN			LUA_ROOT "/autorun"
#define LUA_PATH_AUTORUN_CLIENT		LUA_ROOT "/autorun/client"
#define LUA_PATH_AUTORUN_SERVER		LUA_ROOT "/autorun/server"


#define LUA_BASE_ENTITY_CLASS		"prop_scripted"
#define LUA_BASE_ENTITY_FACTORY	"CBaseAnimating"
#define LUA_BASE_WEAPON					"weapon_hl2mpbase_scriptedweapon"
#define LUA_BASE_GAMEMODE				"deathmatch"


#define LUA_MAX_WEAPON_ACTIVITIES	32


#pragma warning( disable: 4800 )	// forcing value to bool 'true' or 'false' (performance warning)

#define BEGIN_LUA_SET_ENUM_LIB(L, libraryName) \
  const char *lib = libraryName; \
  lua_getglobal(L, "_E"); \
  lua_newtable(L);

/*
** HL2SB: also publish every enum member as a FLAT GLOBAL.
**
** GMod exposes enum members as globals -- FCVAR_ARCHIVE, ACT_VM_DRAW, KEY_A,
** MASK_SHOT_HULL -- and its own Lua depends on that: lua/vgui/DTooltip.lua's
** first statement is a CreateConVar with FCVAR_ARCHIVE.  HL2SB only ever filled
** _E[lib][member], so every one of those names was nil and GMod's files could
** not load.  (gmod_globals.lua had worked around it by hard-coding ~40 ACT_*
** values; that block is now redundant.)
**
** The shortnames are inconsistent between libs -- FCVAR passes "ARCHIVE",
** BUTTON passes "BUTTON_CODE_INVALID" and "KEY_A" -- so BOTH spellings are
** published:
**
**     FCVAR  + "ARCHIVE"             -> ARCHIVE        and FCVAR_ARCHIVE
**     BUTTON + "BUTTON_CODE_INVALID" -> (same string)  and BUTTON_BUTTON_CODE_INVALID
**     BUTTON + "KEY_A"               -> KEY_A          and BUTTON_KEY_A
**
** The second column's junk entries are harmless and are the price of not
** teaching every lib which convention it uses.  The bracket block keeps this
** usable inside the multi-statement macro bodies the enum files already have.
**
** HL2SB: the bare shortname is only published if that global does NOT already
** exist.  Some enum member is literally named CLIENT -- public/
** lenumerations_shared.cpp:573 pushes FL_CLIENT (256) as "CLIENT" -- and
** unconditionally assigning it turned the realm global set in base_open()
** (luamanager.cpp:235-251) into the number 256.  That is truthy, so every
** `if ( CLIENT ) then` on the SERVER took the client branch, which is how the
** server came to import derma/ and the whole lua/vgui/ tree and report 54
** "attempt to index a nil value (global 'derma')" lines per level.
** base_open() runs before the libs are opened, so the realm globals are already
** non-nil here and are left alone; every other flat name (ARCHIVE, KEY_A, ...)
** is still nil and is published as before.
*/
#define lua_pushenum(L, enum, shortname) \
  lua_pushinteger(L, enum); \
  lua_setfield(L, -2, shortname); \
  { \
    char szFlatGlobal[192]; \
    Q_snprintf(szFlatGlobal, sizeof(szFlatGlobal), "%s_%s", lib, (shortname)); \
    lua_getglobal(L, (shortname)); \
    if (lua_isnil(L, -1)) { \
      lua_pop(L, 1); \
      lua_pushinteger(L, enum); \
      lua_setglobal(L, (shortname)); \
    } else { \
      lua_pop(L, 1); \
    } \
    lua_pushinteger(L, enum); \
    lua_setglobal(L, szFlatGlobal); \
  }

/*
** Same as lua_pushenum, except the bare shortname is NEVER published as a
** global (the enum table field and <lib>_<shortname> are still set).
**
** Needed where the bare name belongs to GMod: lin_buttons.cpp publishes
** IN_LEFT = 128 / IN_RIGHT = 256, whose bare names are LEFT and RIGHT -- and
** those are GMod's DOCK enum, i.e. what every `panel:Dock( LEFT )` in Derma
** passes.  The bare value won the race (the engine libs open before the Lua
** extensions) and lua/includes/extensions/gmod_isvalid.lua reported it:
**
**   [HL2SB] WARNING: global 'LEFT' is already 128, but GMod's DOCK enum needs
**           it to be 2 -- docking may misbehave
**
** and then docking did misbehave: the model list never docked to the left.
*/
#define lua_pushenum_nobare(L, enum, shortname) \
  lua_pushinteger(L, enum); \
  lua_setfield(L, -2, shortname); \
  { \
    char szFlatGlobal[192]; \
    Q_snprintf(szFlatGlobal, sizeof(szFlatGlobal), "%s_%s", lib, (shortname)); \
    lua_pushinteger(L, enum); \
    lua_setglobal(L, szFlatGlobal); \
  }

#define END_LUA_SET_ENUM_LIB(L) \
  lua_setfield(L, -2, lib); \
  lua_pop(L, 1);

/*
** Metatable helpers, ported from Experiment: Source so their binding files can
** be dropped in unchanged.
*/

// Creates MetaTableName as a fresh metatable (asserting it did not exist yet).
#define LUA_PUSH_NEW_METATABLE(L, MetaTableName)                        \
  luaL_getmetatable(L, MetaTableName);                                  \
  AssertMsg(lua_isnoneornil(L, -1), "Metatable already exists!");       \
  lua_pop(L, 1);                                                        \
  luaL_newmetatable(L, MetaTableName);

// Pushes an existing metatable that the caller wants to add fields to.
#define LUA_PUSH_METATABLE_TO_EXTEND(L, MetaTableName) \
  luaL_getmetatable(L, MetaTableName);                 \
  AssertMsg(lua_istable(L, -1), "Metatable doesn't exist!");

// Sets the metatable on the value below the top of the stack.
#define LUA_SAFE_SET_METATABLE(L, MetaTableName)     \
  luaL_getmetatable(L, MetaTableName);               \
  AssertMsg(lua_istable(L, -1), "Metatable doesn't exist!"); \
  lua_setmetatable(L, -2);

/*
** Experiment: Source spellings of the two macros above, so their enumeration and
** binding files can be dropped in without edits.  Their version wraps each block
** in braces (each block declares its own `lib`), which is why the alias adds
** them here.
*/
#define LUA_SET_ENUM_LIB_BEGIN(L, libraryName) \
  { BEGIN_LUA_SET_ENUM_LIB(L, libraryName)
#define LUA_SET_ENUM_LIB_END(L) \
  END_LUA_SET_ENUM_LIB(L) }

/*
** Field readers, ported verbatim from Experiment: Source.  GMod spells its
** parameter table keys in UpperCamelCase (Num, Damage, SoundName, ...) while the
** Team Sandbox era bindings read the engine-style names (m_flDamage, ...), so
** every ported binding accepts either: the GMod key wins, the engine key is the
** fallback.  Both variants leave exactly one value on the stack.
*/
#define GET_FIELD_WITH_COMPATIBILITY(L, ArgumentIndex, FieldName, FallbackFieldName) \
  lua_getfield(L, ArgumentIndex, FieldName); \
  if (lua_isnil(L, -1)) { \
    lua_pop(L, 1); /* pop the nil value */ \
    lua_getfield(L, ArgumentIndex, FallbackFieldName); \
  }

#define CHECK_FIELD_OR_ERROR(L, ArgumentIndex, FieldName, CheckFunction) \
  if (!CheckFunction(L, -1)) { \
    luaL_argerror(L, ArgumentIndex, "expected field '" FieldName "'"); \
    return 0; \
  }

#define GET_FIELD_WITH_COMPATIBILITY_OR_ERROR(L, ArgumentIndex, FieldName, FallbackFieldName, CheckFunction) \
  GET_FIELD_WITH_COMPATIBILITY(L, ArgumentIndex, FieldName, FallbackFieldName) \
  CHECK_FIELD_OR_ERROR(L, ArgumentIndex, FieldName, CheckFunction)

/*
** Experiment: Source brackets scripted-entity creation with this pair so a class
** can only be instantiated from inside its own shared script library
** (experiment-source src/game/server/util.h).  HL2SB has no such gate -- its
** LUA_SCRIPTEDENTITIESLIBNAME is registered but nothing enforces it -- so the
** macros stay as no-op markers and the ported binding files compile unchanged.
*/
#define LUA_EXPECTED_SCRIPTED_LIBRARY_BEGIN(libname)
#define LUA_EXPECTED_SCRIPTED_LIBRARY_END(libname)

/*
** Experiment: Source spellings of the hook call, parameterised by lua_State so a
** binding can drive a state that is not the global `L` (their game event listener
** does).  They call hook.Call and pass GAMEMODE; HL2SB's hook module spells them
** hook.call and _GAMEMODE, which is what these use.
*/
#define LUA_CALL_HOOK_FOR_STATE_BEGIN(L, functionName) \
  lua_getglobal(L, "hook"); \
  if (lua_istable(L, -1)) { \
    lua_getfield(L, -1, "call"); \
    if (lua_isfunction(L, -1)) { \
      lua_remove(L, -2); \
      int args = 0; \
      lua_pushstring(L, functionName); \
      lua_getglobal(L, "_GAMEMODE"); \
      args = 2;

#define LUA_CALL_HOOK_FOR_STATE_END(L, nArgs, nresults) \
      args += nArgs; \
      luasrc_pcall(L, args, nresults, 0); \
    } \
    else { lua_pop(L, 2); if ((nresults) > 0) lua_pushnil(L); } \
  } \
  else { lua_pop(L, 1); if ((nresults) > 0) lua_pushnil(L); }

#define BEGIN_LUA_CALL_HOOK(functionName) \
  lua_getglobal(L, "hook"); \
  if (lua_istable(L, -1)) { \
    lua_getfield(L, -1, "call"); \
	if (lua_isfunction(L, -1)) { \
	  lua_remove(L, -2); \
	  int args = 0; \
	  lua_pushstring(L, functionName); \
	  lua_getglobal(L, "_GAMEMODE"); \
	  args = 2;

#define END_LUA_CALL_HOOK(nArgs, nresults) \
	  args += nArgs; \
	  luasrc_pcall(L, args, nresults, 0); \
	} \
	else { lua_pop(L, 2); if ((nresults) > 0) lua_pushnil(L); } \
  } \
  else { lua_pop(L, 1); if ((nresults) > 0) lua_pushnil(L); }

// HL2SB: the table check is not decoration.  m_nTableReference is LUA_NOREF for
// a weapon that has not run InitScriptedWeapon() yet (or whose reference was
// already unref'd), and lua_getref() then yields whatever happens to sit at that
// registry index.  Calling lua_getfield on that raised
//
//     attempt to index a number value
//
// from an unprotected context, which aborted the process via __fastfail.  With
// the guard the call is skipped and the BaseClass:: implementation runs instead.
#define BEGIN_LUA_CALL_WEAPON_METHOD(functionName) \
  lua_getref(L, m_nTableReference); \
  if (lua_istable(L, -1)) { \
    lua_getfield(L, -1, functionName); \
    lua_remove(L, -2); \
    if (lua_isfunction(L, -1)) { \
      int args = 0; \
	  lua_pushweapon(L, this); \
	  ++args;

#define END_LUA_CALL_WEAPON_METHOD(nArgs, nresults) \
	  args += nArgs; \
	  luasrc_pcall(L, args, nresults, 0); \
    } \
    else { lua_pop(L, 1); if ((nresults) > 0) lua_pushnil(L); } \
  } \
  else { lua_pop(L, 1); if ((nresults) > 0) lua_pushnil(L); }

#define BEGIN_LUA_CALL_WEAPON_HOOK(functionName, pWeapon) \
  if (pWeapon->IsScripted() && lua_isrefvalid(L, pWeapon->m_nTableReference)) { \
    lua_getref(L, pWeapon->m_nTableReference); \
    lua_getfield(L, -1, functionName); \
    lua_remove(L, -2); \
    int args = 0; \
    lua_pushweapon(L, pWeapon); \
    ++args;

#define END_LUA_CALL_WEAPON_HOOK(nArgs, nresults) \
    args += nArgs; \
    luasrc_pcall(L, args, nresults, 0); \
  } \
  else \
    if ((nresults) > 0) lua_pushnil(L);

// HL2SB: same guard as BEGIN_LUA_CALL_WEAPON_METHOD above, and for the same
// reason.  m_nTableReference lives in CBaseEntity, so an entity whose scripted
// table was never taken (the client only calls InitScriptedEntity() once
// m_iScriptedClassname has arrived, so an entity removed before that keeps
// LUA_NOREF) or whose reference was already freed by its destructor leaves a
// value that is *not* a table here.  Without the check lua_getfield() raised
// "attempt to index a number value" from an unprotected context -- outside any
// pcall, with an empty Lua traceback -- and aborted the game.
#define BEGIN_LUA_CALL_ENTITY_METHOD(functionName) \
  lua_getref(L, m_nTableReference); \
  if (lua_istable(L, -1)) { \
    lua_getfield(L, -1, functionName); \
    lua_remove(L, -2); \
    if (lua_isfunction(L, -1)) { \
      int args = 0; \
	lua_pushanimating(L, this); \
	++args;

#define END_LUA_CALL_ENTITY_METHOD(nArgs, nresults) \
	args += nArgs; \
	luasrc_pcall(L, args, nresults, 0); \
  } \
  else { lua_pop(L, 1); if ((nresults) > 0) lua_pushnil(L); } \
  } \
  else { lua_pop(L, 1); if ((nresults) > 0) lua_pushnil(L); }

#define BEGIN_LUA_CALL_TRIGGER_METHOD(functionName) \
  lua_getref(L, m_nTableReference); \
  if (lua_istable(L, -1)) { \
    lua_getfield(L, -1, functionName); \
    lua_remove(L, -2); \
    if (lua_isfunction(L, -1)) { \
      int args = 0; \
	lua_pushentity(L, this); \
	++args;

#define END_LUA_CALL_TRIGGER_METHOD(nArgs, nresults) \
	args += nArgs; \
	luasrc_pcall(L, args, nresults, 0); \
  } \
  else { lua_pop(L, 1); if ((nresults) > 0) lua_pushnil(L); } \
  } \
  else { lua_pop(L, 1); if ((nresults) > 0) lua_pushnil(L); }

#define BEGIN_LUA_CALL_PANEL_METHOD(functionName) \
  if (lua_isrefvalid(m_lua_State, m_nTableReference)) { \
    lua_getref(m_lua_State, m_nTableReference); \
    lua_getfield(m_lua_State, -1, functionName); \
    lua_remove(m_lua_State, -2); \
    if (lua_isfunction(m_lua_State, -1)) { \
      int args = 0; \
	  lua_pushpanel(m_lua_State, this); \
	  ++args;

#define END_LUA_CALL_PANEL_METHOD(nArgs, nresults) \
	  args += nArgs; \
	  luasrc_pcall(m_lua_State, args, nresults, 0); \
    } \
    else { lua_pop(m_lua_State, 1); if ((nresults) > 0) lua_pushnil(m_lua_State); } \
  }

/*
** Experiment: Source spellings, so their scripted-control files can be dropped in
** unchanged.  BEGIN/END_LUA_CALL_PANEL_METHOD above are the same macros -- HL2SB
** has had them all along under the unprefixed names -- so these are pure aliases.
*/
#define LUA_CALL_PANEL_METHOD_BEGIN(functionName) \
  BEGIN_LUA_CALL_PANEL_METHOD(functionName)
#define LUA_CALL_PANEL_METHOD_END(nArgs, nresults) \
  END_LUA_CALL_PANEL_METHOD(nArgs, nresults)

/* Experiment's name for LUA_PANELLIBNAME. */
#define LUA_PANELMETANAME LUA_PANELLIBNAME

/*
** Experiment: Source's panel metatable override, reduced to the part HL2SB needs.
**
** Their macro also pulls in lsingleluainstance.h, whose pudata polyfill backs a
** per-instance userdata cache.  HL2SB's scripted controls already carry their own
** Lua table reference (m_nTableReference, pushed by their lua_push<panel> function
** and freed in the destructor), so only the metatable-name hook is required; the
** PushLuaInstanceSafe that their bindings call is declared per class instead.
*/
#define LUA_OVERRIDE_SINGLE_LUA_INSTANCE_METATABLE( ClassName, MetaTableName ) \
  public:                                                                      \
    const char *GetMetatableName() const { return MetaTableName; }


/*
** HL2SB: ported from Experiment: Source (game/shared/luamanager.h).
**
** These are the helpers their scripted controls use to implement __index /
** __newindex on a panel: they consult the panel's Lua-side reference table first,
** then walk up to the base class metatable.  Nothing in them is Experiment
** specific -- they need lua_isrefvalid/lua_getref, which HL2SB already has, and
** PanelIsValid/PanelCollectGarbage, which now live in
** public/lua/vgui_controls/lPanel.h.
*/
#define LUA_GET_REF_TABLE( L, Target )                     \
    if ( !lua_isrefvalid( L, Target->m_nTableReference ) ) \
    {                                                      \
        Target->SetupRefTable( L );                        \
    }                                                      \
    lua_getref( L, Target->m_nTableReference );

#define LUA_METATABLE_INDEX_CHECK_VALID( L, IsValidFunc )         \
    /* IsValid checks before we find out if the target is NULL */ \
    if ( Q_strcmp( luaL_checkstring( L, 2 ), "IsValid" ) == 0 )   \
    {                                                             \
        lua_pushcfunction( L, IsValidFunc );                      \
        return 1;                                                 \
    }

#define LUA_METATABLE_INDEX_CHECK( L, Target )                                                                                       \
    /* Invalid panels fail all checks */                                                                                             \
    if ( Target == NULL )                                                                                                            \
    {                                                                                                                                \
        lua_Debug ar1;                                                                                                               \
        lua_getstack( L, 1, &ar1 );                                                                                                  \
        lua_getinfo( L, "fl", &ar1 );                                                                                                \
        lua_Debug ar2;                                                                                                               \
        lua_getinfo( L, ">S", &ar2 );                                                                                                \
        if ( lua_getmetatable( L, 1 ) )                                                                                              \
        {                                                                                                                            \
            luaL_getmetafield( L, -1, "__name" );                                                                                    \
            const char *__metatableName = lua_tostring( L, -1 );                                                                     \
            lua_pop( L, 2 ); /* Pop the metatable name and the metatable */                                                          \
            lua_pushfstring( L, "%s:%d: attempt to index an invalid %s", ar2.short_src, ar1.currentline, __metatableName );          \
        }                                                                                                                            \
        else                                                                                                                         \
        {                                                                                                                            \
            lua_pushfstring( L, "%s:%d: attempt to index an unknown type (that has no metatable)", ar2.short_src, ar1.currentline ); \
        }                                                                                                                            \
                                                                                                                                    \
        return lua_error( L );                                                                                                       \
    }

// Helper macro to check table on top of the stack for __index
#define LUA_METATABLE_INDEX_CHECK_TABLE( L ) \
    lua_pushvalue( L, 2 );                   \
    lua_gettable( L, -2 );                   \
                                            \
    if ( !lua_isnil( L, -1 ) )               \
    {                                        \
        return 1;                            \
    }                                        \
                                            \
    lua_pop( L, 2 ); /* Pop the table and the nil value */

#define LUA_METATABLE_INDEX_CHECK_REF_TABLE( L, Target )                                    \
    /* We follow by checking if the target has any properties set in its reference table */ \
    if ( Target && L )                                                                      \
    {                                                                                       \
        LUA_GET_REF_TABLE( L, Target );                                                     \
        LUA_METATABLE_INDEX_CHECK_TABLE( L );                                               \
    }

#define LUA_METATABLE_INDEX_DERIVE_INDEX( L, DerivedFrom )         \
    if ( luaL_getmetatable( L, DerivedFrom ) )                     \
    {                                                              \
        lua_getfield( L, -1, "__index" );                          \
        if ( lua_isfunction( L, -1 ) )                             \
        {                                                          \
            lua_pushvalue( L, 1 );                                 \
            lua_pushvalue( L, 2 );                                 \
            lua_call( L, 2, 1 );                                   \
            return 1;                                              \
        }                                                          \
        else if ( lua_istable( L, -1 ) )                           \
        {                                                          \
            lua_pushvalue( L, 2 );                                 \
            lua_gettable( L, -2 );                                 \
            return 1;                                              \
        }                                                          \
                                                                    \
        lua_pop( L, 1 ); /* Pop the result of luaL_getmetatable */ \
    }

#define RETURN_LUA_NONE() \
  if (lua_gettop(L) > 0) { \
    if (lua_isboolean(L, -1)) { \
	  bool res = (bool)luaL_checkboolean(L, -1); \
	  lua_pop(L, 1); \
	  if (!res) \
	    return; \
	} \
    else \
	  lua_pop(L, 1); \
  }

#define RETURN_LUA_PANEL_NONE() \
  if (lua_gettop(m_lua_State) > 0) { \
    if (lua_isboolean(m_lua_State, -1)) { \
	  bool res = (bool)luaL_checkboolean(m_lua_State, -1); \
	  lua_pop(m_lua_State, 1); \
	  if (!res) \
	    return; \
	} \
    else \
	  lua_pop(m_lua_State, 1); \
  }

#define RETURN_LUA_BOOLEAN() \
  if (lua_gettop(L) > 0) { \
    if (lua_isboolean(L, -1)) { \
	  bool res = (bool)luaL_checkboolean(L, -1); \
	  lua_pop(L, 1); \
	  return res; \
	} \
    else \
	  lua_pop(L, 1); \
  }

// GMod semantics for Deploy/Holster: the Lua return value is a VETO only.
//   false      -> cancel (Lua refused the deploy/holster)
//   true / nil -> continue, the engine default MUST still run
// Using RETURN_LUA_BOOLEAN() here is wrong: weapon_base:Deploy() returns true,
// which would short-circuit BaseClass::Deploy() and skip DefaultDeploy() --
// i.e. SetViewModel(), the draw animation, WeaponSound(DEPLOY),
// SetWeaponVisible(true) and the m_flNextPrimaryAttack arming would all be
// lost, leaving no pickup/deploy animation and an unarmed fire gate.
#define RETURN_LUA_VETO() \
  if (lua_gettop(L) > 0) { \
    if (lua_isboolean(L, -1)) { \
	  bool res = (bool)luaL_checkboolean(L, -1); \
	  lua_pop(L, 1); \
	  if (!res) \
	    return false; \
	} \
    else \
	  lua_pop(L, 1); \
  }

#define RETURN_LUA_PANEL_BOOLEAN() \
  if (lua_gettop(m_lua_State) > 0) { \
    if (lua_isboolean(m_lua_State, -1)) { \
	  bool res = (bool)luaL_checkboolean(m_lua_State, -1); \
	  lua_pop(m_lua_State, 1); \
	  return res; \
	} \
    else \
	  lua_pop(m_lua_State, 1); \
  }

#define RETURN_LUA_NUMBER() \
  if (lua_gettop(L) > 0) { \
    if (lua_isnumber(L, -1)) { \
	  float res = luaL_checknumber(L, -1); \
	  lua_pop(L, 1); \
	  return res; \
	} \
    else \
	  lua_pop(L, 1); \
  }

#define RETURN_LUA_INTEGER() \
  if (lua_gettop(L) > 0) { \
    if (lua_isnumber(L, -1)) { \
	  int res = luaL_checkint(L, -1); \
	  lua_pop(L, 1); \
	  return res; \
	} \
    else \
	  lua_pop(L, 1); \
  }

#define RETURN_LUA_ACTIVITY() \
  if (lua_gettop(L) > 0) { \
    if (lua_isnumber(L, -1)) { \
	  int res = luaL_checkint(L, -1); \
	  lua_pop(L, 1); \
	  return (Activity)res; \
	} \
    else \
	  lua_pop(L, 1); \
  }

#define RETURN_LUA_STRING() \
  if (lua_gettop(L) > 0) { \
    if (lua_isstring(L, -1)) { \
	  const char *res = luaL_checkstring(L, -1); \
	  lua_pop(L, 1); \
	  return res; \
	} \
    else \
	  lua_pop(L, 1); \
  }

#define RETURN_LUA_WEAPON() \
  if (lua_gettop(L) > 0) { \
    if (lua_isuserdata(L, -1) && luaL_checkudata(L, -1, "CBaseCombatWeapon")) { \
	  CBaseCombatWeapon *res = luaL_checkweapon(L, -1); \
	  lua_pop(L, 1); \
	  return res; \
	} \
    else \
	  lua_pop(L, 1); \
  }

#define RETURN_LUA_ENTITY() \
  if (lua_gettop(L) > 0) { \
    if (lua_isuserdata(L, -1) && luaL_checkudata(L, -1, "CBaseEntity")) { \
	  CBaseEntity *res = luaL_checkentity(L, -1); \
	  lua_pop(L, 1); \
	  return res; \
	} \
    else \
	  lua_pop(L, 1); \
  }

#define RETURN_LUA_PLAYER() \
  if (lua_gettop(L) > 0) { \
    if (lua_isuserdata(L, -1) && luaL_checkudata(L, -1, "CBasePlayer")) { \
	  CBasePlayer *res = luaL_checkplayer(L, -1); \
	  lua_pop(L, 1); \
	  return res; \
	} \
    else \
	  lua_pop(L, 1); \
  }

#define RETURN_LUA_VECTOR() \
  if (lua_gettop(L) > 0) { \
    if (lua_isuserdata(L, -1) && luaL_checkudata(L, -1, "Vector")) { \
	  Vector res = luaL_checkvector(L, -1); \
	  lua_pop(L, 1); \
	  return res; \
	} \
    else \
	  lua_pop(L, 1); \
  }

#define RETURN_LUA_ANGLE() \
  if (lua_gettop(L) > 0) { \
    if (lua_isuserdata(L, -1) && luaL_checkudata(L, -1, "QAngle")) { \
	  QAngle res = luaL_checkangle(L, -1); \
	  lua_pop(L, 1); \
	  return res; \
	} \
    else \
	  lua_pop(L, 1); \
  }

extern ConVar gamemode;

LUALIB_API int luaL_checkboolean (lua_State *L, int narg);
LUALIB_API int luaL_optboolean (lua_State *L, int narg,
                                              int def);

#ifdef CLIENT_DLL
extern lua_State *LGameUI; // gameui state
#endif

extern lua_State *L;


// Set to true between LevelInit and LevelShutdown.
extern bool	g_bLuaInitialized;

#ifdef CLIENT_DLL
void       luasrc_init_gameui (void);
void       luasrc_shutdown_gameui (void);
#endif

void       luasrc_init (void);
void       luasrc_shutdown (void);

LUA_API int   (luasrc_dostring) (lua_State *L, const char *string);
LUA_API int   (luasrc_dofile) (lua_State *L, const char *filename);
LUA_API void  (luasrc_dofolder) (lua_State *L, const char *path);

// HL2SB: GMod-faithful folder loader for lua/autorun -- recursive and executed in
// alphabetical order, which is what GMod guarantees and luasrc_dofolder does not.
LUA_API void  (luasrc_dofolder_sorted) (lua_State *L, const char *path, bool bRecurse);

// HL2SB: load one named file from lua/includes/ (GMod's engine calls its
// bootstrap by name instead of scanning the directory -- see the definition for
// why the scan is actively harmful).
LUA_API int   (luasrc_dofile_includes) (lua_State *L, const char *pszName);

LUA_API int   (luasrc_pcall) (lua_State *L, int nargs, int nresults, int errfunc);
LUA_API void  (luasrc_print) (lua_State *L, int narg);
LUA_API void  (luasrc_dumpstack) (lua_State *L);

// HL2SB: GMod's lua/effects/*.lua loader.  CLIENT ONLY -- the body is compiled
// out on the server, where GMod does not load effects either.
void       luasrc_LoadEffects (const char *path = 0);
void       luasrc_LoadEntities (const char *path = 0);
void       luasrc_LoadWeapons (const char *path = 0);

bool       luasrc_LoadGamemode (const char *gamemode);
bool       luasrc_SetGamemode (const char *gamemode);

// HL2SB: pull the ammo type definitions out of the "ammo" Lua module.
class CAmmoDef;
void       luasrc_ApplyAmmoTypes (CAmmoDef *pAmmoDef);

#endif // LUAMANAGER_H

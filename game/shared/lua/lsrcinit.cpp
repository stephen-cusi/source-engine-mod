//========== Copyleft © 2011, Team Sandbox, Some rights reserved. ===========//
//
// Purpose: 
//
// $NoKeywords: $
//===========================================================================//


#define lsrcinit_cpp

#include "cbase.h"
#include "lua.hpp"

#include "luasrclib.h"
#include "lauxlib.h"

// HL2SB: game.AddParticles() below drives the particle system manager directly.
#include "particles/particles.h"
#include "filesystem.h"
// HL2SB: luaL_checkentity (SuppressHostEvents) and the GMod global helpers
// registered at the end.
#include "luamanager.h"
#include "lbaseentity_shared.h"
#include "ipredictionsystem.h"
#ifdef CLIENT_DLL
#include "prediction.h"			// CPrediction *prediction, for IsFirstTimePredicted
#endif


static const luaL_Reg luasrclibs[] = {
  // HL2SB: ported from Experiment: Source.  Fills _E with the shared enums.
  {LUA_SHAREDENUMNAME, luaopen_SharedEnumerations},
  // HL2SB: ported from Experiment: Source.  The rest of the _E tables.  They are
  // opened right after luaopen_SharedEnumerations because each one does
  // lua_getglobal("_E") + lua_setfield, and Experiment's gmod_compatibility shim
  // reads them -- sh_enumerations.lua error()s on a missing key.  luaopen_ACTIVITY
  // additionally takes ownership of the activity list away from the world entity
  // (see the LUA_SDK branch of REGISTER_SHARED_ACTIVITY in activitylist.h).
  {LUA_ACTIVITYENUMNAME, luaopen_ACTIVITY},
  {LUA_BUTTONENUMNAME, luaopen_BUTTON},
  {LUA_EFLIBNAME, luaopen_EF},
  {LUA_ENGINEFLAGSENUMLIBNAME, luaopen_FL},
  {LUA_FLEDICTLIBNAME, luaopen_FL_EDICT},
  {LUA_GESTURESLOTLIBNAME, luaopen_GESTURE_SLOT},
  {LUA_LIFELIBNAME, luaopen_LIFE},
  {LUA_MOVECOLLIDELIBNAME, luaopen_MOVECOLLIDE},
  {LUA_MOVETYPELIBNAME, luaopen_MOVETYPE},
  {LUA_OBSMODELIBNAME, luaopen_OBS_MODE},
  {LUA_SOLIDFLAGLIBNAME, luaopen_SOLIDFLAG},
  {LUA_SOLIDLIBNAME, luaopen_SOLID},
  {LUA_BASEANIMATINGLIBNAME, luaopen_CBaseAnimating},
  {LUA_BASEANIMATINGLIBNAME, luaopen_CBaseAnimating_shared},
  {LUA_BASECOMBATWEAPONLIBNAME, luaopen_CBaseCombatWeapon},
  {LUA_BASEENTITYLIBNAME, luaopen_CBaseEntity},
  {LUA_BASEENTITYLIBNAME, luaopen_CBaseEntity_shared},
  // HL2SB: ported from Experiment: Source.  Registered after luaopen_Entities so it
  // merges its Entities.CreateClientEntity into the same `ents` table.
  {LUA_CBASEFLEXLIBNAME, luaopen_CBaseFlex_shared},
  {LUA_BASEPLAYERLIBNAME, luaopen_CBasePlayer},
  {LUA_BASEPLAYERLIBNAME, luaopen_CBasePlayer_shared},
  {LUA_EFFECTDATALIBNAME, luaopen_CEffectData},
  {LUA_GAMETRACELIBNAME, luaopen_CGameTrace},
#ifndef CLIENT_DLL
  {LUA_EFFECTSLIBNAME, luaopen_Effects},
  {LUA_HL2MPPLAYERLIBNAME, luaopen_CHL2MP_Player},
#endif
  {LUA_HL2MPPLAYERLIBNAME, luaopen_CHL2MP_Player_shared},
  {LUA_COLORLIBNAME, luaopen_Color},
  {LUA_CONCOMMANDLIBNAME, luaopen_ConCommand},
  {LUA_CONTENTSLIBNAME, luaopen_CONTENTS},
  {LUA_CONVARLIBNAME, luaopen_ConVar},
  {LUA_PASFILTERLIBNAME, luaopen_CPASFilter},
  {LUA_RECIPIENTFILTERLIBNAME, luaopen_CRecipientFilter},
  {LUA_TAKEDAMAGEINFOLIBNAME, luaopen_CTakeDamageInfo},
  {LUA_CVARLIBNAME, luaopen_cvar},
  {LUA_DBGLIBNAME, luaopen_dbg},
  {LUA_DEBUGOVERLAYLIBNAME, luaopen_debugoverlay},
  {LUA_ENGINELIBNAME, luaopen_engine},
#ifdef CLIENT_DLL
  // FIXME: obsolete? should be passing VPANELs, but passes Panel instead,
  // which always ends up being invalid (we can't access them by pointer)
  {LUA_ENGINEVGUILIBNAME, luaopen_enginevgui},
#endif
  {LUA_FCVARLIBNAME, luaopen_FCVAR},
  {LUA_FILESYSTEMLIBNAME, luaopen_filesystem},
#ifdef CLIENT_DLL
  {LUA_FONTFLAGLIBNAME, luaopen_FONTFLAG},
#endif
#ifndef CLIENT_DLL
  {LUA_ENTLISTLIBNAME, luaopen_gEntList},
#endif
  {LUA_GLOBALSLIBNAME, luaopen_gpGlobals},
  // HL2SB: ported from Experiment: Source.  gameevent.Listen.
  {LUA_GAMEEVENTSLIBNAME, luaopen_GameEvents},
  {LUA_HL2SBLIBNAME, luaopen_hl2sb},
#ifdef CLIENT_DLL
  {LUA_CLIENTSHADOWMGRLIBNAME, luaopen_g_pClientShadowMgr},
  {LUA_FONTLIBNAME, luaopen_HFont},
  {LUA_HSCHEMELIBNAME, luaopen_HScheme},
#endif
  {LUA_MATERIALLIBNAME, luaopen_IMaterial},
  {LUA_MOVEHELPERLIBNAME, luaopen_IMoveHelper},
  {LUA_INLIBNAME, luaopen_IN},
#ifndef CLIENT_DLL
  {LUA_NETCHANNELINFOLIBNAME, luaopen_INetChannelInfo},
#endif
  {LUA_INETWORKSTRINGTABLELIBNAME, luaopen_INetworkStringTable},
#ifdef CLIENT_DLL
  {LUA_INPUTLIBNAME, luaopen_input},
#endif
  {LUA_PHYSICSOBJECTLIBNAME, luaopen_IPhysicsObject},
  {LUA_PHYSICSSURFACEPROPSLIBNAME, luaopen_IPhysicsSurfaceProps},
  {LUA_PREDICTIONSYSTEMLIBNAME, luaopen_IPredictionSystem},
#ifdef CLIENT_DLL
  {LUA_ISCHEMELIBNAME, luaopen_IScheme},
#endif
//  {LUA_STEAMFRIENDSLIBNAME, luaopen_ISteamFriends},
  {LUA_KEYVALUESLIBNAME, luaopen_KeyValues},
  // HL2SB: ported from Experiment: Source
  {LUA_LOCALIZATIONLIBNAME, luaopen_Localizations},
  // HL2SB: ported from Experiment: Source.
  {LUA_PARTICLESYSTEMLIBNAME, luaopen_ParticleSystem},
  {LUA_SYSTEMSLIBNAME, luaopen_Systems},
  // HL2SB: ported from Experiment: Source.  `Entities` is merged onto the same
  // global table as the Team Sandbox era `ents` (Create/GetByIndex).
  {LUA_ENTITIESLIBNAME, luaopen_Entities},
  // HL2SB: ported from Experiment: Source.  AudioChannel and the URL/file
  // streaming bindings were dropped -- they depend on their BASS manager.
  {LUA_SOUNDSLIBNAME, luaopen_Sounds},
  // TODO(port): Files / FileHandle still need a decision on how they coexist with
  // the Team Sandbox era `filesystem` library (LUA_FILESYSTEMLIBNAME) before they
  // can be registered.
  // HL2SB: GMod's file library.  This registration was commented out and there
  // was no implementation, so the lib alias ("file" -> LUA_FILESLIBNAME) at the
  // bottom of this file silently skipped and every imported GMod file that uses
  // file.* failed at load:
  //     extensions/file.lua:2: attempt to index a nil value (global 'file')
  //     extensions/player_auth.lua:77: ... (global 'file')
  // Implemented in public/lua/lfilesystem.cpp, which BOTH game vpcs already list,
  // so no .vpc needed touching and both realms get it.
  {LUA_FILESLIBNAME, luaopen_Files},
  // {LUA_FILEHANDLEMETANAME, luaopen_FileHandle},
#ifdef CLIENT_DLL
  {LUA_CLIENTENUMNAME, luaopen_ClientEnumerations},
#else
  {LUA_SERVERENUMNAME, luaopen_ServerEnumerations},
#endif
  {LUA_MASKLIBNAME, luaopen_MASK},
  {LUA_MATHLIBLIBNAME, luaopen_mathlib},
  {LUA_MATRIXLIBNAME, luaopen_matrix3x4_t},
  {LUA_NETLIBNAME, luaopen_net},
  {LUA_NETWORKSTRINGTABLELIBNAME, luaopen_networkstringtable},
#ifdef CLIENT_DLL
  {LUA_PANELLIBNAME, luaopen_Panel},
  // HL2SB: ported from Experiment: Source.  Label is the base every Derma text
  // control sits on (DLabel, DButton, DTextEntry), so it has to be opened after
  // Panel, whose metatable it extends.
  {LUA_LABELMETANAME, luaopen_Label},
  // HL2SB: ported from Experiment: Source.  TextEntry extends Label,
  // so it opens after it.
  {LUA_TEXTENTRYMETANAME, luaopen_TextEntry},
#endif
  {LUA_PHYSENVLIBNAME, luaopen_physenv},
#ifdef CLIENT_DLL
  {LUA_PREDICTIONLIBNAME, luaopen_prediction},
#endif
  {LUA_QANGLELIBNAME, luaopen_QAngle},
  {LUA_RANDOMLIBNAME, luaopen_random},
#ifdef CLIENT_DLL
  // HL2SB: ported from Experiment: Source.  `Renders` carries GMod's render.*
  // (render.SetColorModulation, render.DrawSprite, render.PushRenderTarget, ...);
  // the Lua content aliases the global `render` onto it.  ITexture is the userdata
  // those bindings hand back, so its metatable is installed first.
  {LUA_ITEXTUREMETANAME, luaopen_ITexture},
  {LUA_RENDERSLIBNAME, luaopen_render},
  // HL2SB: ported from Experiment: Source.  GMod's chat.* lives here.
  {LUA_CHATSLIBNAME, luaopen_Chats},
#endif
#ifdef CLIENT_DLL
  {LUA_SCHEMELIBNAME, luaopen_scheme},
#endif
//  {LUA_STEAMAPICONTEXTLIBNAME, luaopen_steamapicontext},
  {LUA_SURFLIBNAME, luaopen_SURF},
#ifdef CLIENT_DLL
  {LUA_SURFACELIBNAME, luaopen_surface},
#endif
  {LUA_UTILLIBNAME, luaopen_UTIL},
  {LUA_UTILLIBNAME, luaopen_UTIL_shared},
  // HL2SB: there is no C++ `hl2sb_undo` library any more.  Undo is Garry's
  // Mod's Lua module (lua/includes/modules/undo.lua), which is loaded by
  // luasrc_dofolder() and owns the global `undo` table.
  {LUA_VECTORLIBNAME, luaopen_Vector},
#ifdef CLIENT_DLL
  {LUA_VGUILIBNAME, luaopen_vgui},
#endif
  {LUA_VMATRIXLIBNAME, luaopen_VMatrix},
  {NULL, NULL}
};


/*
** ===========================================================================
** HL2SB: metatable names.
**
** Garry's Mod exposes two C globals that all of its Lua framework (and the
** Experiment: Source gmod_compatibility shim) is built on:
**
**   FindMetaTable( name )          -> the metatable registered under `name`
**   RegisterMetaTable( name, tbl ) -> register one (Derma does this)
**
** GMod registers its metatables under the bare class names ("Entity", "Player",
** "Weapon", "Angle", ...); the Team Sandbox era code HL2SB inherited registers
** them under the C++ class names ("CBaseEntity", "CBasePlayer", "QAngle", ...).
** This table bridges the two, and the aliases are mirrored into the registry so
** that a plain `_R.Entity` works as well -- base_open already publishes the
** registry as `_R`, and Experiment's own Lua reads `_R.Entity` directly instead
** of going through FindMetaTable.
**
** A name with no entry falls through to a literal registry lookup, so classes
** HL2SB registers under the GMod name already (Panel, Frame, Button, ...) keep
** working, and RegisterMetaTable can add new ones ("DPanel", "DLabel", ...).
** ===========================================================================
*/
struct LuaMetatableAlias_t
{
  const char *pszGModName;   // name GMod code asks for
  const char *pszNativeName; // name HL2SB's libs registered it under
};

static const LuaMetatableAlias_t s_LuaMetatableAliases[] = {
  { "Entity",            LUA_BASEENTITYLIBNAME },
  { "Player",            LUA_BASEPLAYERLIBNAME },
  { "Weapon",            LUA_BASECOMBATWEAPONLIBNAME },
  { "Angle",             LUA_QANGLELIBNAME },
  { "Vector",            LUA_VECTORLIBNAME },
  { "Matrix",            LUA_MATRIXLIBNAME },
  { "Color",             LUA_COLORLIBNAME },
  { "EffectData",        LUA_EFFECTDATALIBNAME },
  { "Trace",             LUA_GAMETRACELIBNAME },
  { "ConsoleVariable",   LUA_CONVARLIBNAME },
  { "ConsoleCommand",    LUA_CONCOMMANDLIBNAME },
  { "RecipientFilter",   LUA_RECIPIENTFILTERLIBNAME },
  { "TakeDamageInfo",    LUA_TAKEDAMAGEINFOLIBNAME },
  { "Material",          LUA_MATERIALLIBNAME },
  { "MoveHelper",        LUA_MOVEHELPERLIBNAME },
  { "Texture",           LUA_ITEXTUREMETANAME },
  { "KeyValuesHandle",   LUA_KEYVALUESLIBNAME },
  { "FileHandle",        "FileHandle_t" },
  { "PhysicsObject",     LUA_PHYSICSOBJECTLIBNAME },
  { "PhysicsSurfacePropertiesHandle", LUA_PHYSICSSURFACEPROPSLIBNAME },
  { "NetChannelInfo",    LUA_NETCHANNELINFOLIBNAME },
  { "Panel",             "Panel" },
  { "Frame",             "Frame" },
  { "Button",            "Button" },
  { "CheckButton",       "CheckButton" },
  { "EditablePanel",     "EditablePanel" },
  { "ModelPanel",        "ModelPanel" },
  { "ProjectedTexture",  "ProjectedTexture" },  // TODO(port): lc_projected_texture
  { "AudioChannel",      "AudioChannel" },      // TODO(port): needs BASS
  { "MoveData",          "MoveData" },          // TODO(port): lmovedata
  // GMod's name for the user command metatable, confirmed by dumping its registry:
  // "CUserCmd", with MetaID 19 (TYPE_USERCMD).
  { "CUserCmd",          "CUserCmd" },          // TODO(port): lusercmd
  { "MessageReader",     "MessageReader" },     // TODO(port): needs bf_read
  { "MessageWriter",     "MessageWriter" },     // TODO(port): needs bf_write
  { "Label",             "Label" },             // TODO(port): scripted_controls
  { "Html",              "Html" },              // TODO(port): scripted_controls
  { "TextEntry",         "TextEntry" },         // TODO(port): scripted_controls
  { "ModelImagePanel",   "ModelImagePanel" },   // TODO(port): scripted_controls
  { "SteamFriendsHandle", "SteamFriendsHandle" },
  // Confirmed by the registry dump: these all exist in GMod.  They resolve to nil
  // in HL2SB until the owning class is ported, which is the same thing GMod does
  // before the relevant library is loaded.
  { "NPC",               "NPC" },
  { "Vehicle",           "Vehicle" },
  { "NextBot",           "NextBot" },
  { "CSEnt",             "CSEnt" },
  { "Tool",              "Tool" },
  { "ISave",             "ISave" },
  { "IRestore",          "IRestore" },
  { "IMesh",             "IMesh" },
  { "CLuaEmitter",       "CLuaEmitter" },
  { "CLuaParticle",      "CLuaParticle" },
  { "CNewParticleEffect", "CNewParticleEffect" },
  { "SurfaceInfo",       "SurfaceInfo" },
  { "PhysCollide",       "PhysCollide" },
  { "IGModAudioChannel", "IGModAudioChannel" },
  { "IVideoWriter",      "IVideoWriter" },
  { "pixelvis_handle_t", "pixelvis_handle_t" },
  { "MarkupObject",      "MarkupObject" },
  { NULL, NULL }
};

static const char *LuaNativeMetatableName (const char *pszName) {
  for (int i = 0; s_LuaMetatableAliases[i].pszGModName; ++i) {
    if (!Q_stricmp(s_LuaMetatableAliases[i].pszGModName, pszName))
      return s_LuaMetatableAliases[i].pszNativeName;
  }
  return pszName;
}

/*
** GMod returns nil for a metatable that is not registered yet, which is what
** Derma relies on while it is still defining its controls.
**
** It has to be an explicit nil and not "no results": in Lua a call that returns
** zero values vanishes when it is the last argument of another call, so
** `print( FindMetaTable( "Label" ) )` collapsed to a bare `print()` and printed
** an empty line instead of "nil".
*/
static int lua_FindMetaTable (lua_State *L) {
  const char *pszName = luaL_checkstring(L, 1);
  luaL_getmetatable(L, LuaNativeMetatableName(pszName));
  if (lua_isnil(L, -1))
    lua_pushnil(L);  /* leave exactly one value: nil */
  return 1;
}

static int lua_RegisterMetaTable (lua_State *L) {
  const char *pszName = luaL_checkstring(L, 1);
  luaL_checktype(L, 2, LUA_TTABLE);
  lua_pushvalue(L, 2);
  lua_setfield(L, LUA_REGISTRYINDEX, pszName);
  return 0;
}

static const luaL_Reg lua_metatable_funcs[] = {
  {"FindMetaTable", lua_FindMetaTable},
  {"RegisterMetaTable", lua_RegisterMetaTable},
  {NULL, NULL}
};

/* Publish registry[gmodName] = the native metatable, so `_R.Entity` resolves. */
static void luasrc_install_metatable_aliases (lua_State *L) {
  for (int i = 0; s_LuaMetatableAliases[i].pszGModName; ++i) {
    const char *pszGModName = s_LuaMetatableAliases[i].pszGModName;
    const char *pszNativeName = s_LuaMetatableAliases[i].pszNativeName;

    if (!Q_stricmp(pszGModName, pszNativeName))
      continue;

    luaL_getmetatable(L, pszNativeName);
    if (lua_istable(L, -1)) {
      lua_pushvalue(L, -1);
      lua_setfield(L, LUA_REGISTRYINDEX, pszGModName);
    }
    lua_pop(L, 1);
  }
}

/*
** ===========================================================================
** HL2SB: GMod type names.
**
** GMod's Lua type system is not the raw Lua one.  garrysmod/lua/includes/util.lua
** *replaces* the global `type` with:
**
**   function type( v )
**     local v_type = C_type( v )
**     if ( v_type ~= "userdata" ) then return v_type end
**     local metatable = getmetatable( v )
**     local metaName = metatable and metatable.MetaName
**     return C_type( metaName ) == "string" and metaName or "UserData"
**   end
**
** and TypeID() reads metatable.MetaID, and isentity() walks
** metatable.MetaBaseClass to find "Entity".  So a metatable without MetaName
** makes type() report "UserData" for everything the moment GMod's util.lua is
** loaded, which would break all of Derma and the spawnmenu.
**
** HL2SB's own type() (luamanager.cpp) reads __type instead, and its values are
** the Team Sandbox era lowercase ones ("entity", "vector", "panel", ...).  The
** names below are stamped as both __type and MetaName so the two agree, and they
** follow Garry's Mod rather than Experiment: Source, which disagrees in three
** places (it calls a player "Entity", a VMatrix "Matrix", and a physics object
** "PhysicsObject").  Values are Garry's Mod's TYPE_* enum
** (https://wiki.facepunch.com/gmod/Enums/TYPE).
**
** Stamping happens here, once, rather than editing the ~30 binding files that
** create the metatables: every luaopen_* has already run by this point, so this
** covers both realms, and the table stays the single place to audit.
** ===========================================================================
*/
#define LUA_TYPE_ENTITY        9
#define LUA_TYPE_VECTOR       10
#define LUA_TYPE_ANGLE        11
#define LUA_TYPE_PHYSOBJ      12
#define LUA_TYPE_DAMAGEINFO   15
#define LUA_TYPE_EFFECTDATA   16
#define LUA_TYPE_MOVEDATA     17
#define LUA_TYPE_RECIPFILTER  18
#define LUA_TYPE_USERCMD      19
#define LUA_TYPE_MATERIAL     21
#define LUA_TYPE_PANEL        22
#define LUA_TYPE_TEXTURE      25
#define LUA_TYPE_CONVAR       27
#define LUA_TYPE_MATRIX       29
#define LUA_TYPE_FILE         34
#define LUA_TYPE_PROJTEX      41
#define LUA_TYPE_USERDATA      7
// GMod's Color metatable reports MetaID 44, not the TYPE_COLOR = 255 constant --
// 255 is a networking hack for net.WriteType (see the TYPE enum), while the
// metatable itself carries 44, which equals TYPE_COUNT in that enum.  Taken from
// a dump of GMod's own registry, not from the wiki.
#define LUA_TYPE_COLOR        44
// Not in the TYPE enum of the dumped build (TYPE_COUNT is 44 there), but GMod
// does register a MarkupObject metatable and reports 45 for it.
#define LUA_TYPE_MARKUP       45

struct LuaTypeInfo_t
{
  const char *pszMetatable;   // registry name the metatable was created under
  const char *pszTypeName;    // MetaName / __type
  int iTypeID;                // MetaID
  const char *pszBaseMetatable; // MetaBaseClass, or NULL
  bool bIsTableType;          // value is a Lua table: stamp MetaName/MetaID but NOT __type
};

static const LuaTypeInfo_t s_LuaTypeInfo[] = {
  // Entities.  GMod reports Player and Weapon separately and chains them onto
  // Entity, which is what isentity() walks.
  { LUA_BASEENTITYLIBNAME,      "Entity",          LUA_TYPE_ENTITY,     NULL },
  { LUA_BASEPLAYERLIBNAME,      "Player",          LUA_TYPE_ENTITY,     LUA_BASEENTITYLIBNAME },
  { "CHL2MP_Player",            "Player",          LUA_TYPE_ENTITY,     LUA_BASEENTITYLIBNAME },
  { LUA_BASECOMBATWEAPONLIBNAME,"Weapon",          LUA_TYPE_ENTITY,     LUA_BASEENTITYLIBNAME },
  { "CBaseAnimating",           "Entity",          LUA_TYPE_ENTITY,     LUA_BASEENTITYLIBNAME },
  { "CBaseFlex",                "Entity",          LUA_TYPE_ENTITY,     LUA_BASEENTITYLIBNAME },
  { "CBaseCombatCharacter",     "Entity",          LUA_TYPE_ENTITY,     LUA_BASEENTITYLIBNAME },

  // Value types.
  { LUA_VECTORLIBNAME,          "Vector",          LUA_TYPE_VECTOR,     NULL },
  { LUA_QANGLELIBNAME,          "Angle",           LUA_TYPE_ANGLE,      NULL },
  { LUA_COLORLIBNAME,           "Color",           LUA_TYPE_COLOR,      NULL, /*bIsTableType*/ true },
  { LUA_VMATRIXLIBNAME,         "VMatrix",         LUA_TYPE_MATRIX,     NULL },
  { LUA_MATRIXLIBNAME,          "VMatrix",         LUA_TYPE_MATRIX,     NULL },
  { LUA_GAMETRACELIBNAME,       "Trace",           LUA_TYPE_USERDATA,   NULL },
  { LUA_KEYVALUESLIBNAME,       "KeyValues",       LUA_TYPE_USERDATA,   NULL },

  // engine objects
  { LUA_TAKEDAMAGEINFOLIBNAME,  "CTakeDamageInfo", LUA_TYPE_DAMAGEINFO, NULL },
  { LUA_EFFECTDATALIBNAME,      "CEffectData",     LUA_TYPE_EFFECTDATA, NULL },
  { LUA_RECIPIENTFILTERLIBNAME, "CRecipientFilter",LUA_TYPE_RECIPFILTER,NULL },
  { LUA_PASFILTERLIBNAME,       "CRecipientFilter",LUA_TYPE_RECIPFILTER,LUA_RECIPIENTFILTERLIBNAME },
  { LUA_MATERIALLIBNAME,        "IMaterial",       LUA_TYPE_MATERIAL,   NULL },
  { LUA_ITEXTUREMETANAME,       "ITexture",        LUA_TYPE_TEXTURE,    NULL },
  { LUA_CONVARLIBNAME,          "ConVar",          LUA_TYPE_CONVAR,     NULL },
  { LUA_CONCOMMANDLIBNAME,      "ConCommand",      LUA_TYPE_CONVAR,     NULL },
  { LUA_PHYSICSOBJECTLIBNAME,   "PhysObj",         LUA_TYPE_PHYSOBJ,    NULL },
  // The metatable is registered as "FileHandle_t" (lfilesystem.cpp);
  // LUA_FILEHANDLEMETANAME is the *library* name, not the metatable name.
  { "FileHandle_t",             "File",            LUA_TYPE_FILE,       NULL },
  { LUA_MOVEHELPERLIBNAME,      "MoveHelper",      LUA_TYPE_MOVEDATA,   NULL },

  // All vgui controls report "Panel" in GMod, with the class chain in
  // MetaBaseClass -- this is what derma's panels all rely on.
  { "Panel",                    "Panel",           LUA_TYPE_PANEL,      NULL },
  { "EditablePanel",            "Panel",           LUA_TYPE_PANEL,      "Panel" },
  { "Frame",                    "Panel",           LUA_TYPE_PANEL,      "EditablePanel" },
  { "Button",                   "Panel",           LUA_TYPE_PANEL,      "Panel" },
  { "CheckButton",              "Panel",           LUA_TYPE_PANEL,      "Button" },
  { "ModelPanel",               "Panel",           LUA_TYPE_PANEL,      "Panel" },
  { "PropertyDialog",           "Panel",           LUA_TYPE_PANEL,      "Frame" },
  { "PropertyPage",             "Panel",           LUA_TYPE_PANEL,      "EditablePanel" },

  { NULL, NULL, 0, NULL }
};

static void luasrc_install_type_names (lua_State *L) {
  for (int i = 0; s_LuaTypeInfo[i].pszMetatable; ++i) {
    luaL_getmetatable(L, s_LuaTypeInfo[i].pszMetatable);
    if (!lua_istable(L, -1)) {
      lua_pop(L, 1);
      continue;  // not opened in this realm
    }

    // metatable.MetaName is what GMod's type() reads for userdata.
    lua_pushstring(L, s_LuaTypeInfo[i].pszTypeName);
    lua_setfield(L, -2, "MetaName");

    // metatable.__type is what HL2SB's own type() (luamanager.cpp) reads, and it is
    // skipped for the types GMod implements as plain Lua tables (Color): HL2SB's
    // type() reports __type for anything carrying a metatable, while GMod's type()
    // short-circuits on the raw Lua type and answers "table" there.  Verified by
    // dumping GMod: `Color() -> type = table`, and its Color metatable still has
    // MetaName "Color" / MetaID 44.
    if (!s_LuaTypeInfo[i].bIsTableType) {
      lua_pushstring(L, s_LuaTypeInfo[i].pszTypeName);
      lua_setfield(L, -2, "__type");
    }

    lua_pushinteger(L, s_LuaTypeInfo[i].iTypeID);
    lua_setfield(L, -2, "MetaID");

    if (s_LuaTypeInfo[i].pszBaseMetatable) {
      luaL_getmetatable(L, s_LuaTypeInfo[i].pszBaseMetatable);
      if (lua_istable(L, -1))
        lua_setfield(L, -2, "MetaBaseClass");
      else
        lua_pop(L, 1);
    }

    lua_pop(L, 1);
  }
}

/*
** ===========================================================================
** HL2SB: GMod's lowercase library globals.
**
** HL2SB inherited Experiment: Source's capitalised library names for a handful
** of libraries -- Systems, Files, UTIL, Renders, Sounds, Chats,
** ParticleSystems, ScriptedEntities -- while Garry's Mod spells them
** system, file, util, render, sound, chat, particle, scripted_ents.
**
** GMod code and every addon use the lowercase spelling exclusively; GMod's own
** lua/derma/init.lua opens with system.IsLinux(), and its lua/vgui controls are
** full of render.*, surface.*, draw.*.
**
** The alias MERGES the Experiment table into the GMod name (see the function
** below): entries the GMod name already has are kept, the rest are added.  It
** must not replace the table, because the two realms register some of these
** under different spellings and replacing one with the other silently deletes a
** library -- that is exactly how util.PrecacheModel went missing.  A missing
** library (several are realm-gated) is skipped rather than aliased to nil, so the
** name simply stays undefined in the realm that does not have it -- which is what
** GMod does too.
**
** Entities / player / gameevent are already aliased where they are opened
** (luabinding.cpp:88, lplayer.cpp:652, lgameevents.cpp:92); this table is only
** for the ones that had no GMod spelling at all.
**
** Stamped here, once, rather than in each binding file: every luaopen_* has
** already run by the time luasrc_openlibs() reaches this, so both realms are
** covered and the table stays the single place to audit.
** ===========================================================================
*/
static const char *s_pGModLibAliases[][2] = {
  { "system",         LUA_SYSTEMSLIBNAME },
  { "util",           LUA_UTILLIBNAME },
  { "file",           LUA_FILESLIBNAME },
  { "render",         LUA_RENDERSLIBNAME },
  { "sound",          LUA_SOUNDSLIBNAME },
  { "chat",           LUA_CHATSLIBNAME },
  { "particle",       LUA_PARTICLESYSTEMLIBNAME },
  { "scripted_ents",  LUA_SCRIPTEDENTITIESLIBNAME },
  { NULL, NULL }
};

static void luasrc_install_lib_aliases (lua_State *L) {
  for (int i = 0; s_pGModLibAliases[i][0]; ++i) {
    const char *pszGModName = s_pGModLibAliases[i][0];
    const char *pszExpName  = s_pGModLibAliases[i][1];

    lua_getglobal(L, pszExpName);
    if (!lua_istable(L, -1)) {
      lua_pop(L, 1);
      continue;
    }
    const int nSrc = lua_gettop(L);

    lua_getglobal(L, pszGModName);
    if (!lua_istable(L, -1)) {
      /* Nothing under the GMod name yet -- publish the source table itself. */
      lua_pop(L, 1);
      lua_pushvalue(L, nSrc);
      lua_setglobal(L, pszGModName);
      lua_pop(L, 1);
      continue;
    }
    const int nDst = lua_gettop(L);

    /* The GMod name already exists, so MERGE instead of overwriting.
    **
    ** lua_setglobal() here used to clobber a real library.  Concretely: on the
    ** client luaopen_UTIL() (game/client/lua/lcdll_util.cpp) registers its
    ** functions under "UTIL" while luaopen_UTIL_shared()
    ** (game/shared/lua/lutil_shared.cpp) registers under "util" -- and GMod's
    ** spelling is "util".  So `util = UTIL` threw away everything the shared
    ** library had put there: util.PrecacheModel, util.PrecacheSound,
    ** util.TraceLine, util.DecalTrace ...  The visible breakage was
    **
    **   lua/includes/util.lua:225: attempt to call a nil value
    **                             (field 'PrecacheModel')
    **
    ** from Model() in GMod's own util.lua, which weapons/gmod_camera/shared.lua
    ** calls at load time.  Same shape for every realm-gated pair in the table
    ** above, so the rule is: never destroy an existing library, only fill it in.
    ** Existing entries win -- a name the GMod spelling already owns is left
    ** alone, and package.loaded[ name ] stays the same table as _G[ name ]. */
    lua_pushnil(L);
    while (lua_next(L, nSrc) != 0) {
      lua_pushvalue(L, -2);              /* key */
      lua_rawget(L, nDst);               /* dst[ key ] */
      if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        lua_pushvalue(L, -2);            /* key */
        lua_pushvalue(L, -2);            /* value */
        lua_rawset(L, nDst);             /* dst[ key ] = value */
      } else {
        lua_pop(L, 1);
      }
      lua_pop(L, 1);                     /* pop the value, keep the key */
    }

    lua_settop(L, nSrc - 1);             /* drop src and dst */
  }
}

//-----------------------------------------------------------------------------
// HL2SB: the engine half of GMod's `game` library.
//
// Only the members the ported content actually calls are here.  The ported
// flechette gun's SWEP body runs
//     game.AddParticles( "particles/hunter_flechette.pcf" )
// and threw "attempt to call a nil value (field 'AddParticles')" six times per
// session, because the table created below was empty.
//-----------------------------------------------------------------------------
static int lua_game_AddParticles (lua_State *L) {
  const char *pszParticleFile = luaL_checkstring( L, 1 );
  bool bLoaded = false;

  // GMod's game.AddParticles( file ) loads a .pcf so that the particle systems
  // inside it can be precached by name afterwards.  That is what GMod SWEPs rely
  // on: the ported flechette gun runs
  //     game.AddParticles( "particles/hunter_flechette.pcf" )
  // and the weapon then precaches "hunter_muzzle_flash", "flechette_halo", ...
  //
  // This used to hand the *file name* to PrecacheParticleSystem(), which takes a
  // system name, so every call produced
  //     Attemped to precache unknown particle system "particles/....pcf"!
  // and the weapon had no effects at all.  ReadParticleConfigFile() is the same
  // entry the engine's own particles_manifest.txt loader uses.
  if ( pszParticleFile[0] == '\0' ) {
    lua_pushboolean( L, false );
    return 1;
  }

  if ( g_pParticleSystemMgr == NULL ) {
    Warning( "[HL2SB] game.AddParticles: particle system manager not ready, '%s' not loaded\n", pszParticleFile );
  }
  else if ( !filesystem->FileExists( pszParticleFile, "GAME" ) ) {
    Warning( "[HL2SB] game.AddParticles: '%s' is not in the search paths - its particle systems stay unknown\n", pszParticleFile );
  }
  else {
    bLoaded = g_pParticleSystemMgr->ReadParticleConfigFile( pszParticleFile, true, false );

    if ( !bLoaded ) {
      Warning( "[HL2SB] game.AddParticles: '%s' exists but could not be parsed\n", pszParticleFile );
    }
  }

  lua_pushboolean( L, bLoaded );
  return 1;
}

static int lua_game_GetMap (lua_State *L) {
  // The realms disagree on where the map name lives: the client has
  // IVEngineClient::GetLevelName() (CGlobalVarsBase has no mapname), the server
  // has CGlobalVars::mapname (IVEngineServer has no GetLevelName()).
#ifdef CLIENT_DLL
  const char *pszLevel = engine->GetLevelName();
#else
  const char *pszLevel = STRING( gpGlobals->mapname );
#endif
  char szPath[MAX_PATH];
  char szMap[MAX_PATH];
  const char *pszBase = szPath;
  char *pDot;

  Q_strncpy( szPath, pszLevel ? pszLevel : "", sizeof( szPath ) );

  // "maps/foo.bsp" -> "foo"
  for ( const char *p = szPath; *p; ++p ) {
    if ( *p == '/' || *p == '\\' ) {
      pszBase = p + 1;
    }
  }

  Q_strncpy( szMap, pszBase, sizeof( szMap ) );

  pDot = Q_strrchr( szMap, '.' );
  if ( pDot ) {
    *pDot = '\0';
  }

  lua_pushstring( L, szMap );
  return 1;
}

static int lua_game_SinglePlayer (lua_State *L) {
  lua_pushboolean( L, gpGlobals->maxClients <= 1 );
  return 1;
}

static int lua_game_IsDedicated (lua_State *L) {
#ifdef CLIENT_DLL
  lua_pushboolean( L, false );
#else
  lua_pushboolean( L, engine->IsDedicatedServer() != 0 );
#endif
  return 1;
}

//-----------------------------------------------------------------------------
// HL2SB GMod compat: GMod's type().
//
// GMod reports the engine's own types - "Player", "Entity", "Weapon", "Vector",
// "Angle", "Panel", "Color", "Material", "TakeDamageInfo", ... - where stock Lua
// only knows "table"/"userdata".  GMod scripts branch on it constantly, and a
// wrong answer is silent: it is exactly what made the fork's old Lua
// weapon_base's post-frame bail out on its first line
// (type( pPlayer ) ~= "entity") and left every SWEP clicking empty.
//
// The mapping is the same alias table the metatable registry uses, read in the
// native -> GMod direction; a value whose metatable has no __name (a plain table,
// a number, ...) falls through to Lua's own type().
//-----------------------------------------------------------------------------
static int lua_type_gmod (lua_State *L) {
  // Only the types GMod's own type() actually reports.  GMod is deliberately
  // conservative: engine handle classes like MessageWriter, FileHandle,
  // NetChannelInfo or KeyValuesHandle still answer "userdata" there, and its own
  // lua/includes/extensions/net.lua depends on that
  // (type( v ) == "userdata" and getmetatable( v ) ~= nil).
  static const char *s_pReportedTypes[] = {
    "Entity", "Player", "Weapon", "NPC", "Vehicle",
    "Vector", "Angle", "Matrix", "Color",
    "Panel", "Material", "Texture", "PhysicsObject",
    "EffectData", "Trace", "ConsoleVariable", "TakeDamageInfo",
  };

  int nType = lua_type( L, 1 );

  if ( nType == LUA_TTABLE || nType == LUA_TUSERDATA ) {
    if ( lua_getmetatable( L, 1 ) ) {
      lua_getfield( L, -1, "__name" );                    // [metatable, name]
      const char *pszNative = lua_tostring( L, -1 );

      if ( pszNative != NULL ) {
        for ( int i = 0; s_LuaMetatableAliases[i].pszGModName; ++i ) {
          if ( Q_stricmp( s_LuaMetatableAliases[i].pszNativeName, pszNative ) ) {
            continue;
          }

          for ( int j = 0; j < ARRAYSIZE( s_pReportedTypes ); ++j ) {
            if ( !Q_stricmp( s_pReportedTypes[j], s_LuaMetatableAliases[i].pszGModName ) ) {
              lua_pushstring( L, s_LuaMetatableAliases[i].pszGModName );
              return 1;
            }
          }

          break;
        }
      }

      lua_pop( L, 2 );
    }
  }

  // Lua's own type(), captured as this closure's upvalue.
  lua_pushvalue( L, lua_upvalueindex( 1 ) );
  lua_pushvalue( L, 1 );
  lua_call( L, 1, 1 );
  return 1;
}

//-----------------------------------------------------------------------------
// HL2SB GMod compat: the console print globals.
//
// GMod has Msg() (no newline) and MsgN() (newline) on both realms, and its own
// lua/includes files use them - extensions/gmod_isvalid.lua:115 and
// extensions/player_auth.lua:78 both failed with
//     attempt to call a nil value (global 'Msg')
//     attempt to call a nil value (global 'MsgN')
//-----------------------------------------------------------------------------
static int lua_Msg (lua_State *L) {
  int nArgs = lua_gettop( L );

  for ( int i = 1; i <= nArgs; ++i ) {
    size_t nLength = 0;
    const char *pszText = luaL_tolstring( L, i, &nLength );
    Msg( "%s", pszText ? pszText : "" );
    lua_pop( L, 1 );
  }

  return 0;
}

static int lua_MsgN (lua_State *L) {
  lua_Msg( L );
  Msg( "\n" );
  return 0;
}

#ifndef CLIENT_DLL
//-----------------------------------------------------------------------------
// HL2SB GMod compat: SuppressHostEvents( ent ).
//
// GMod's server-side helper for "do not filter this player's own prediction out
// of the temp entities I am about to send" - it is exactly
// IPredictionSystem::SuppressHostEvents(), and the ported flechette gun calls it
// (with NULL) before spawning its projectile.  Without the global the weapon
// aborted on that line and never fired:
//     weapon_flechettegun/shared.lua:58: attempt to call a nil value (global
//     'SuppressHostEvents')
//-----------------------------------------------------------------------------
static int lua_SuppressHostEvents (lua_State *L) {
  // GMod passes its NULL *sentinel* (a userdata, not Lua nil) to clear the
  // suppress host, so this has to go through lua_toentity() - the converter that
  // yields NULL for both - instead of luaL_checkentity(), which rejects the
  // sentinel with "CBaseEntity expected, got NULL entity" and aborted the caller:
  //   weapon_flechettegun/shared.lua:58 (before ents.Create, so the gun never
  //   fired) and weapon_fists/shared.lua:135 (before TakeDamageInfo, so the fists
  //   did no damage).
  IPredictionSystem::SuppressHostEvents( lua_toentity( L, 1 ) );
  return 0;
}
#endif

//-----------------------------------------------------------------------------
// HL2SB GMod compat: the global IsFirstTimePredicted().
//
// GMod exposes it as a global on both realms; this fork only had the client-side
// library method prediction.IsFirstTimePredicted.  The Nyan Gun's PrimaryAttack
// opens with it (weapon_nyangun.lua:95) and threw 65 times in one run - which
// also left its firing sound looping forever, because the code that stops the
// sound comes after that line and never ran.
//-----------------------------------------------------------------------------
static int lua_IsFirstTimePredicted (lua_State *L) {
#ifdef CLIENT_DLL
  lua_pushboolean( L, prediction->IsFirstTimePredicted() );
#else
  // The server is the authority; GMod's server-side answer is "yes, go ahead".
  lua_pushboolean( L, true );
#endif
  return 1;
}

LUALIB_API void luasrc_openlibs (lua_State *L) {
  const luaL_Reg *lib = luasrclibs;
  for (; lib->func; lib++) {
    lua_pushcfunction(L, lib->func);
    lua_pushstring(L, lib->name);
    lua_call(L, 1, 0);
  }

  /* Every lib is open now, so the metatables exist and can be aliased. */
  luasrc_install_metatable_aliases(L);
  luasrc_install_type_names(L);

  /* ...and the library tables themselves can be reachable under GMod's names. */
  luasrc_install_lib_aliases(L);

  /* HL2SB: re-assert the realm globals LAST.
  **
  ** public/lenumerations_shared.cpp:573 pushes the entity flag FL_CLIENT (256)
  ** with the shortname "CLIENT", and lua_pushenum publishes shortnames as flat
  ** globals -- so it overwrote the boolean base_open() had set (luamanager.cpp:
  ** 235-251).  Result, measured in-game: CLIENT=256 on BOTH realms, i.e. truthy
  ** everywhere, so every `if ( CLIENT ) then` took the client branch on the
  ** server too (that is what made the server import derma/ + all of lua/vgui/
  ** and print 54 "nil value (global 'derma')" lines per level).
  **
  ** Fixing the macro alone was not enough -- the object that defines the FL_
  ** enum did not get rebuilt (luamanager.h is not in waf's header dependency
  ** set) and the value was still 256 afterwards.  Stamping them here, once,
  ** after every lib is open, cannot be skipped by a stale object: this file is
  ** the one that changed, so it always recompiles.
  */
#ifdef CLIENT_DLL
  lua_pushboolean(L, 1); lua_setglobal(L, "_CLIENT");
  lua_pushboolean(L, 0); lua_setglobal(L, "SERVER");
  lua_pushboolean(L, 1); lua_setglobal(L, "CLIENT");
#else
  lua_pushboolean(L, 1); lua_setglobal(L, "_GAME");
  lua_pushboolean(L, 1); lua_setglobal(L, "SERVER");
  lua_pushboolean(L, 0); lua_setglobal(L, "CLIENT");
#endif

  /* HL2SB: four GMod globals this engine never had.  All four are LOAD-TIME
  ** prerequisites of files imported verbatim from GMod, and every one of them is
  ** needed on BOTH realms (client.dll and server.dll compile this file), so they
  ** are stamped here rather than in a Lua shim that would only exist in one:
  **
  **   AddCSLuaFile( path )  lua/includes/extensions/coroutine.lua:5 calls it at
  **                         load time -> "attempt to call a nil value (global
  **                         'AddCSLuaFile')".  GMod uses it to mark a file for
  **                         client download; this engine has no download list, so
  **                         it records nothing and returns the path like GMod.
  **   IncludeCS( path )     the client-side twin of the above; same reasoning.
  **   jit                   lua/includes/extensions/util.lua:397 indexes
  **                         jit.version.  Our Lua is 5.4, not LuaJIT, so there is
  **                         no jit table at all and the whole GMod util extension
  **                         failed to load.  The stub carries a version string so
  **                         the feature check runs instead of throwing.
  **   LoadPresets()         lua/includes/modules/presets.lua:6 calls it at load.
  **   SENSORBONE            lua/includes/modules/motionsensor.lua:16 indexes it.
  */
  if ( luaL_loadstring( L,
    "AddCSLuaFile = AddCSLuaFile or function( path ) return path end\n"
    "IncludeCS = IncludeCS or function( path ) return path end\n"
    "jit = jit or { version = 'Lua 5.4 (no LuaJIT)', version_num = 50400 }\n"
    //---------------------------------------------------------------------
    // player: only the SERVER DLL ever created this table
    // (game/server/lua/lplayer.cpp:652 does lua_setglobal( L, "player" ));
    // the client's luaopen_CBasePlayer (game/client/lua/lc_baseplayer.cpp)
    // registers the metatable and LocalPlayer but never the library, so on
    // the client `player` was nil and two imported GMod files died:
    //
    //     extensions/player.lua:265        attempt to index a nil value (global player)
    //     extensions/entity_iter.lua:14    (cascade: player.lua never ran)
    //
    // This snippet runs AFTER the library loop in luasrc_openlibs, so the
    // server's real table is kept and only the client gets an empty one, which
    // is what GMod's own extensions/player.lua then fills in.
    //---------------------------------------------------------------------
    "player = player or {}\n"
    "LoadPresets = LoadPresets or function() end\n"
    "SENSORBONE = SENSORBONE or {}\n"
    //---------------------------------------------------------------------
    // sql: GMod's sql library is SQLite-backed and this engine ships no
    // sqlite3 at all (no sqlite3.h/.c anywhere; the only sql code is the
    // MySQL developer tools under utils/, which are not in client.dll or
    // server.dll).  luasrclib.h declares no sql library either, so there is
    // nothing to bind.
    //
    // This is a STUB, not an implementation.  It keeps GMod's shapes so the
    // three imported files that merely CALL it can load --
    //
    //     modules/cookie.lua:2            attempt to index a nil value (global sql)
    //     extensions/player.lua:37        ... (global sql)
    //     extensions/entity_iter.lua:14   (cascade: player.lua never ran)
    //
    // Queries return empty results and LastError says so, instead of
    // pretending to persist.  Replacing this with sqlite3 is a build-level
    // job (vendor the amalgamation + register the library in lsrcinit.cpp),
    // not a Lua one.
    //---------------------------------------------------------------------
    "sql = sql or {\n"
    "  LastError = function() return 'sqlite is not available in this engine' end,\n"
    "  SQLStr = function( str, bNoQuotes )\n"
    "    local s = tostring( str ):gsub( \"'\", \"''\" )\n"
    "    if ( bNoQuotes ) then return s end\n"
    "    return \"'\" .. s .. \"'\"\n"
    "  end,\n"
    "  TableExists = function() return false end,\n"
    "  Query = function() return {} end,\n"
    "  QueryRow = function() return nil end,\n"
    "  QueryValue = function() return nil end,\n"
    "  Begin = function() end,\n"
    "  Commit = function() end,\n"
    "}\n" ) == 0 )
  {
    lua_pcall( L, 0, 0, 0 );
  }
  else
  {
    // A syntax error here is a bug in this snippet, not in the game.
    Warning( "[HL2SB] lsrcinit: GMod global stubs failed to compile: %s\n", lua_tostring( L, -1 ) );
    lua_pop( L, 1 );
  }

  //-----------------------------------------------------------------------------
  // HL2SB: GMod's `game` table.
  //
  // modules/... no -- extensions/game.lua:24 EXTENDS it (`function
  // game.AddAmmoType( tbl )`), so it died on
  //     extensions/game.lua:24: attempt to index a nil value (global 'game')
  // because this engine has no game library at all (no LUA_GAMELIBNAME
  // anywhere).  Creating the table here lets that file load; the individual
  // game.* functions it expects from the engine are still a follow-up.
  //
  // `or {}` style: if a game library ever appears, its table wins.
  //-----------------------------------------------------------------------------
  if ( lua_getglobal( L, "game" ) == LUA_TNIL )
  {
    lua_pop( L, 1 );
    lua_newtable( L );
    lua_setglobal( L, "game" );
  }
  else
  {
    lua_pop( L, 1 );
  }

  // The engine-backed members (lua_game_* above).  GMod's extensions/game.lua
  // only ADDS to this table, and the Lua shims in extensions/gmod_compat.lua
  // redefine GetMap/SinglePlayer later with the same behaviour, so anything
  // installed here may be overridden by script.
  lua_getglobal( L, "game" );
  if ( lua_istable( L, -1 ) )
  {
    lua_pushcfunction( L, lua_game_AddParticles ); lua_setfield( L, -2, "AddParticles" );
    lua_pushcfunction( L, lua_game_GetMap );       lua_setfield( L, -2, "GetMap" );
    lua_pushcfunction( L, lua_game_SinglePlayer ); lua_setfield( L, -2, "SinglePlayer" );
    lua_pushcfunction( L, lua_game_IsDedicated );  lua_setfield( L, -2, "IsDedicated" );
  }
  lua_pop( L, 1 );

  //-----------------------------------------------------------------------------
  // HL2SB: Source's StudioRender flags, exposed with the ENGINE's own values
  // (public/model_types.h:16-18) rather than literals that could drift.
  //
  // modules/halo.lua:14 is
  //     bit.bor( STUDIO_RENDER, STUDIO_SKIP_DECALS or 0 )
  // and failed with "bad argument #1 to 'bor' (number expected, got nil)".
  // STUDIO_SKIP_DECALS is genuinely not defined in this engine's model_types.h,
  // and halo.lua already tolerates that with "or 0", so it is NOT faked here.
  //-----------------------------------------------------------------------------
  // Values from public/model_types.h:16-18.  The #ifndef fallbacks matter: the
  // first version of this block was wrapped in #ifdef STUDIO_RENDER and the
  // whole thing compiled OUT, because model_types.h is not in this translation
  // unit's include chain -- so the flags were never pushed and modules/halo.lua
  // kept failing with "bad argument #1 to 'bor' (number expected, got nil)".
#ifndef STUDIO_RENDER
#define STUDIO_RENDER 0x00000001
#endif
#ifndef STUDIO_VIEWXFORMATTACHMENTS
#define STUDIO_VIEWXFORMATTACHMENTS 0x00000002
#endif
#ifndef STUDIO_DRAWTRANSLUCENTSUBMODELS
#define STUDIO_DRAWTRANSLUCENTSUBMODELS 0x00000004
#endif

  lua_pushinteger( L, STUDIO_RENDER );                   lua_setglobal( L, "STUDIO_RENDER" );
  lua_pushinteger( L, STUDIO_VIEWXFORMATTACHMENTS );     lua_setglobal( L, "STUDIO_VIEWXFORMATTACHMENTS" );
  lua_pushinteger( L, STUDIO_DRAWTRANSLUCENTSUBMODELS ); lua_setglobal( L, "STUDIO_DRAWTRANSLUCENTSUBMODELS" );

  luaL_register(L, "_G", lua_metatable_funcs);
  lua_pop(L, 1);

  /* HL2SB: GMod globals that the engine owns (see the lua_Msg / lua_SuppressHost
  ** Events definitions above).  Registered after the library pass so nothing
  ** overwrites them. */
  lua_pushcfunction( L, lua_Msg );
  lua_setglobal( L, "Msg" );
  lua_pushcfunction( L, lua_MsgN );
  lua_setglobal( L, "MsgN" );

  lua_pushcfunction( L, lua_IsFirstTimePredicted );
  lua_setglobal( L, "IsFirstTimePredicted" );

  /* GMod's type(): keep the real one as an upvalue and install the wrapper in its
  ** place (see lua_type_gmod above). */
  lua_getglobal( L, "type" );
  lua_pushcclosure( L, lua_type_gmod, 1 );
  lua_setglobal( L, "type" );

#ifndef CLIENT_DLL
  lua_pushcfunction( L, lua_SuppressHostEvents );
  lua_setglobal( L, "SuppressHostEvents" );
#endif
}


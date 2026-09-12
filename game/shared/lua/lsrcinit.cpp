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
// HL2SB: luaL_checkentity (SuppressHostEvents), the GMod global helpers
// registered at the end, the NetworkVar shim (which hands a 0..1 Vector back for
// a Vector-declared variable, a 0..255 Color otherwise) and the vector/color
// accessors it and sent_ball need.
#include "luamanager.h"
#include "lbaseentity_shared.h"
// HL2SB: lua_toanimating() for the NetworkVar shim's m_nSkin carrier.  Each
// realm has its own twin (game/*/lua/l[ c_ ]baseanimating.h); its
// dynamic_cast is the real-RTTI cast that keeps a CBaseEntity from being
// reinterpreted as an animating one.  Same pair lbaseentity_shared.cpp uses.
#ifdef CLIENT_DLL
#include "lc_baseanimating.h"
#else
#include "lbaseanimating.h"
#endif
#include <lColor.h>
#include <mathlib/lvector.h>
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

//-----------------------------------------------------------------------------
// HL2SB GMod compat: ErrorNoHalt( ... ) and ErrorNoHaltWithStack( ... ).
//
// GMod's non-fatal error print.  Neither existed in this fork (the only
// definitions were in lua/includes/modules/gmod_compatibility/sh_init.lua,
// which is inert behind GMOD_COMPATIBILITY = false), so every GMod file that
// reports a soft failure died on the report itself:
//
//   gamemodes/sandbox/gamemode/spawnmenu/creationmenu/content/contentsearch.lua:118
//       ErrorNoHalt( "..." )
//   lua/includes/modules/spawnmenu.lua:292
//       ErrorNoHaltWithStack( "spawnmenu.GetContentType got an invalid value\n" )
//
// and several pre-existing fork files call them too.  Behaviour matches GMod /
// this fork's Msg: the arguments are concatenated with tostring() and written to
// the console, and nothing halts.  ErrorNoHaltWithStack additionally writes a
// Lua traceback, which is the reason GMod scripts reach for it.
//-----------------------------------------------------------------------------
static int lua_ErrorNoHalt (lua_State *L) {
  int nArgs = lua_gettop( L );

  for ( int i = 1; i <= nArgs; ++i ) {
    const char *pszText = luaL_tolstring( L, i, NULL );
    Msg( "%s", pszText ? pszText : "" );
    lua_pop( L, 1 );
  }

  return 0;
}

static int lua_ErrorNoHaltWithStack (lua_State *L) {
  lua_ErrorNoHalt( L );
  Msg( "\n" );

  // luaL_traceback( L, L, NULL, 1 ) writes "stack traceback:" plus the frames
  // into a new string on the stack.  NULL for the message is legal in 5.4.
  luaL_traceback( L, L, NULL, 1 );
  const char *pszTrace = lua_tostring( L, -1 );
  if ( pszTrace != NULL )
    Msg( "%s\n", pszTrace );
  lua_pop( L, 1 );

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

//-----------------------------------------------------------------------------
// HL2SB GMod compat: the scripted-entity network variable shim.
//
// GMod's ENT:SetupDataTables() declares network variables:
//
//     self:NetworkVar( "Float", 0, "BallSize" )
//     self:NetworkVarNotify( "BallSize", self.OnBallSizeChanged )
//
// and GMod's engine turns each declaration into the Set<name>/Get<name> pair the
// script then calls.  This fork has no DataTable slots for Lua entities, so the
// accessors are defined here -- installed by CBaseScripted::InitScriptedEntity()
// as the entity table's NetworkVar / NetworkVarNotify fields (basescripted.cpp:
// 206-238), which is why a stock GMod entity script can call them without being
// modified.
//
// The pair is LARGELY the same shim weapons/weapon_base/shared.lua already builds
// for SWEPs, with one difference that matters: a SWEP stores per-table and each
// realm reads its own copy, but a network var that the SERVER sets and the CLIENT
// reads must cross the wire (sent_ball's SpawnFunction sets BallSize/BallColor on
// the server, while its ENT:Draw reads them on the client).  A plain Lua table
// would leave the client on the defaults -- size 0, black -- so the two variables
// sent_ball declares are ALSO carried by fields the entity already networks:
//
//   BallColor -> SetRenderColor / GetRenderColor   (m_clrRender; the right type,
//               the right semantics, already replicated)
//   BallSize  -> m_nSkin                           (CNetworkVar( int ) on
//               CBaseAnimating, already replicated)
//
// BALLCOLOR UNITS.  The script-visible shape is GMod's 0..1 Vector
// (NetworkVar( "Vector", 0, "BallColor" ); ENT:Initialize does
// SetBallColor( Vector( 1, 0.3, 0.3 ) )) while m_clrRender is a 0..255 color32,
// so the setter scales by 255 and the getter divides by 255, and both clamp.
// The getter returns a 0..1 Vector for a Vector-declared variable and the usual
// 0..255 Color for any other declared type, because the declared type is what
// GMod's own Get<name>() would have returned.
//
// BALLSIZE CARRIER.  BallSize is a Float but m_nSkin is an int, so the carrier
// is lossy: the exact value always lives in the entity's own Lua table (the
// realm that set it reads it back exactly, fractional or not) and m_nSkin is
// only the cross-realm copy the other realm falls back to.  Reading therefore
// consults the entity table FIRST and uses the replicated value only when this
// realm never set the variable -- which also removes the old "0 means unset"
// ambiguity.
//
// Known collision (documented, not hidden): m_nSkin is CBaseAnimating's own
// "skin" INPUT -- DEFINE_INPUT( m_nSkin, FIELD_INTEGER, "skin" ),
// game/server/baseanimating.cpp:164 -- so `ent_fire <ball> skin <n>` (or a menu
// that feeds that input) changes the ball size.  It is an INPUT, not a
// DEFINE_KEYFIELD, so a raw `skin` key in a BSP entity lump is not applied at
// spawn; the collision needs something to fire the input.
//
// THERE IS NO BETTER REPLICATED INT CARRIER on this class:
//   m_nSkin     10 bits signed  (ANIMATION_SKIN_BITS, baseanimating.h:527)  input "skin"
//   m_nBody     32 bits         (ANIMATION_BODY_BITS)          keyfield "body" + input "SetBodyGroup"
//   m_nHitboxSet 2 bits unsigned (ANIMATION_HITBOXSET_BITS)    keyfield "hitboxset" -- too small anyway
//   m_nForceBone 8 bits signed  (-128..127)                    no keyfield, but sent_ball's own
//                                                              MaxSize is 128, so it does not fit
// m_nSkin is the best fit (covers sent_ball's 4..128 and is already proven
// replicated for this entity), so it stays, with the read order above.
//
// Everything else -- and every other type -- keeps the per-table behaviour, so
// this is a superset of the SWEP shim, not a replacement.  Self-designed: GMod
// uses real engine network vars and there is no public implementation to copy.
//-----------------------------------------------------------------------------
enum HL2SB_NWStorage_t
{
  HL2SB_NW_TABLE = 0,   // entity's own Lua table (the SWEP shim's behaviour)
  HL2SB_NW_SKIN,        // m_nSkin        (int,   CNetworkVar)
  HL2SB_NW_RENDERCOLOR, // m_clrRender    (color32, CNetworkVar)
};

static HL2SB_NWStorage_t HL2SB_NWStorageForName (const char *pszName) {
  if ( V_stricmp( pszName, "BallSize" ) == 0 )
    return HL2SB_NW_SKIN;

  if ( V_stricmp( pszName, "BallColor" ) == 0 )
    return HL2SB_NW_RENDERCOLOR;

  return HL2SB_NW_TABLE;
}

// True for the declared types whose values are numeric (GMod's Float and Int),
// which is what decides whether a value is normalised through lua_tonumber()
// before it is stored.  ENT:KeyValue hands sent_ball's SetBallSize() the string
// value of the key ("32"), and GMod's own network-var setter coerces that to a
// number -- without this, GetBallSize() would hand a string back to
// math.Clamp().
static bool HL2SB_NWIsNumericType (const char *pszType) {
  return V_stricmp( pszType, "Float" ) == 0 || V_stricmp( pszType, "Int" ) == 0;
}

// Push the script-visible default for a declared variable.
static void HL2SB_NWPushValue (lua_State *L, const char *pszType) {
  if ( V_stricmp( pszType, "Vector" ) == 0 ) {
    lua_pushvector( L, Vector( 0, 0, 0 ) );
    return;
  }

  if ( V_stricmp( pszType, "Angle" ) == 0 ) {
    lua_pushangle( L, QAngle( 0, 0, 0 ) );
    return;
  }

  if ( V_stricmp( pszType, "Bool" ) == 0 ) {
    lua_pushboolean( L, false );
    return;
  }

  if ( V_stricmp( pszType, "String" ) == 0 ) {
    lua_pushstring( L, "" );
    return;
  }

  // Float / Int / anything unrecognised: GMod's numeric default.
  lua_pushnumber( L, 0 );
}

// Push the table that holds this entity's scripted variables and return its
// stack index; return 0 (pushing nothing) when there is none.
//
// This matters because the two halves of the shim are invoked with DIFFERENT
// kinds of `self`, and the first version assumed they were the same:
//
//   NetworkVar / NetworkVarNotify   self = the entity's Lua TABLE
//       (CBaseScripted::InitScriptedEntity() pushes the table it got from
//        entity.get() -- basescripted.cpp:231 -- and refs that same table into
//        m_nTableReference a few lines later)
//   Set<name> / Get<name>           self = the entity USERDATA
//       (BEGIN_LUA_CALL_ENTITY_METHOD pushes lua_pushanimating() as `self`, so
//        ENT:Initialize()'s self:SetBallSize() hands this closure a userdata)
//
// lua_rawget()/lua_rawset() need a real table: called with a userdata they go
// through Lua's internal api_check (compiled out in release builds) and treat
// the userdata's memory as a Table header.  So the accessors resolve the
// entity's table through the same member CBaseEntity___index uses
// (lbaseentity_shared.cpp:2196).  A table self is used as-is, which keeps
// NetworkVar/NetworkVarNotify and a script that calls Set<name> on the table
// directly working.
static int HL2SB_NWPushStorageTable (lua_State *L, int iSelf) {
  if ( lua_istable( L, iSelf ) ) {
    lua_pushvalue( L, iSelf );
    return lua_gettop( L );
  }

  CBaseEntity *pEntity = lua_toentity( L, iSelf );
  if ( pEntity != NULL && pEntity->m_nTableReference >= 0 &&
       lua_isrefvalid( L, pEntity->m_nTableReference ) ) {
    lua_getref( L, pEntity->m_nTableReference );
    if ( lua_istable( L, -1 ) ) {
      return lua_gettop( L );
    }
    lua_pop( L, 1 );
  }

  return 0;
}

// table[ pszStorageKey ] -> pushed value, true; nothing pushed, false.
static bool HL2SB_NWRawGet (lua_State *L, int iTable, const char *pszStorageKey) {
  lua_pushstring( L, pszStorageKey );
  lua_rawget( L, iTable );
  if ( !lua_isnil( L, -1 ) ) {
    return true;
  }

  lua_pop( L, 1 );
  return false;
}

// table[ pszStorageKey ] = the value at stack index iValue (raw, so a variable
// named like an existing field cannot be hijacked by the table's metatable).
static void HL2SB_NWRawSet (lua_State *L, int iTable, const char *pszStorageKey, int iValue) {
  lua_pushstring( L, pszStorageKey );
  lua_pushvalue( L, iValue );
  lua_rawset( L, iTable );
}

// table[ pszStorageKey ] = flValue (raw).
static void HL2SB_NWRawSetNumber (lua_State *L, int iTable, const char *pszStorageKey, double flValue) {
  lua_pushstring( L, pszStorageKey );
  lua_pushnumber( L, flValue );
  lua_rawset( L, iTable );
}

// Read the current value of a declared variable and push it.  iTable is the
// storage table (or 0 when the entity has none).  The value being replaced stays
// on the stack underneath, so the wrapper can still use it as
// NetworkVarNotify's "old" argument.
static void HL2SB_NWReadValue (lua_State *L, int iSelf, int iTable, const char *pszStorageKey,
                               HL2SB_NWStorage_t storage, const char *pszType) {
  if ( storage == HL2SB_NW_TABLE ) {
    if ( iTable != 0 && HL2SB_NWRawGet( L, iTable, pszStorageKey ) ) {
      return;
    }
    HL2SB_NWPushValue( L, pszType );
    return;
  }

  if ( storage == HL2SB_NW_SKIN ) {
    // The entity's own table wins: it carries the exact value the script set on
    // this realm (m_nSkin truncates a fractional BallSize, and 0 there means
    // "nothing networked" rather than "unset").
    if ( iTable != 0 && HL2SB_NWRawGet( L, iTable, pszStorageKey ) ) {
      return;
    }

    // This realm never set it, so the replicated copy is all there is.  It is
    // read through lua_toanimating()'s real-RTTI cast, not reinterpreted from a
    // CBaseEntity: an entity that is not a CBaseAnimating has no m_nSkin.
    lua_CBaseAnimating *pAnimating = lua_toanimating( L, iSelf );
    if ( pAnimating != NULL && pAnimating->m_nSkin != 0 ) {
      lua_pushnumber( L, pAnimating->m_nSkin );
      return;
    }

    HL2SB_NWPushValue( L, pszType );
    return;
  }

  // m_clrRender.  0..1 Vector when the declared type is Vector -- which is what
  // sent_ball's ENT:Draw reads (c.r / c.g / c.b, then x255 for DrawSprite) --
  // and the engine's 0..255 Color otherwise.  The bytes are already clamped by
  // their type, so the division cannot overflow anything.
  CBaseEntity *pEntity = lua_toentity( L, iSelf );

  if ( pEntity == NULL ) {
    HL2SB_NWPushValue( L, pszType );
    return;
  }

  const color32 clr = pEntity->GetRenderColor();

  if ( V_stricmp( pszType, "Vector" ) == 0 ) {
    lua_pushvector( L, Vector( clr.r / 255.0f, clr.g / 255.0f, clr.b / 255.0f ) );
    return;
  }

  lua_pushcolor( L, Color( clr.r, clr.g, clr.b, clr.a ) );
}

// GMod's m_clrRender is a 0..255 color32, so a script-visible colour has to be
// scaled explicitly.  Everything is clamped: DrawSprite narrows int -> unsigned
// char with no clamp of its own, so an out-of-range value wraps mod 256 instead
// of saturating.
static byte HL2SB_NWColorByte (double flValue) {
  if ( flValue <= 0.0 ) {
    return 0;
  }

  if ( flValue >= 255.0 ) {
    return 255;
  }

  return ( byte )( flValue + 0.5 );
}

// Store the value the script passed and leave the value Get<name>() will hand
// back on top of the stack, so the notify callback can be given the same
// (normalised) "new" value.
static void HL2SB_NWStoreValue (lua_State *L, int iSelf, int iTable, const char *pszStorageKey,
                                HL2SB_NWStorage_t storage, const char *pszType) {
  if ( storage == HL2SB_NW_RENDERCOLOR ) {
    // Two accepted shapes.  A 0..1 Vector is GMod's colour shape
    // (SetBallColor( Vector( 1, 0.3, 0.3 ) )); a 0..255 Color table comes from
    // ENT:KeyValue's "rendercolor" path and from hand-written addons.
    byte r = 0, g = 0, b = 0, a = 255;

    if ( luaL_testudata( L, 2, LUA_VECTORLIBNAME ) != NULL ) {
      const Vector vec = luaL_checkvector( L, 2 );
      r = HL2SB_NWColorByte( ( double )vec.x * 255.0 );
      g = HL2SB_NWColorByte( ( double )vec.y * 255.0 );
      b = HL2SB_NWColorByte( ( double )vec.z * 255.0 );
    } else if ( lua_iscolor( L, 2 ) ) {
      const Color clr = luaL_checkcolor( L, 2 );
      r = HL2SB_NWColorByte( clr.r() );
      g = HL2SB_NWColorByte( clr.g() );
      b = HL2SB_NWColorByte( clr.b() );
      a = HL2SB_NWColorByte( clr.a() );
    } else {
      luaL_argerror( L, 2, "Vector or Color expected" );
    }

    // The script-visible value goes into the entity's table (so a re-read here
    // is exact and cheap), the wire copy into m_clrRender.
    if ( iTable != 0 ) {
      HL2SB_NWRawSet( L, iTable, pszStorageKey, 2 );
    }

    CBaseEntity *pEntity = lua_toentity( L, iSelf );
    if ( pEntity != NULL ) {
      pEntity->SetRenderColor( r, g, b, a );
    }

    HL2SB_NWReadValue( L, iSelf, iTable, pszStorageKey, storage, pszType );  // [.., new]
    return;
  }

  if ( storage == HL2SB_NW_SKIN ) {
    // The exact value is what this realm keeps; m_nSkin is only the int copy the
    // other realm gets.  A numeric string (ENT:KeyValue: "ball_size" "32") is
    // coerced here, the way GMod's own Float network var would.
    const double flValue = lua_tonumber( L, 2 );

    if ( iTable != 0 ) {
      HL2SB_NWRawSetNumber( L, iTable, pszStorageKey, flValue );
    }

    // m_nSkin is a 10-bit signed SendProp (ANIMATION_SKIN_BITS,
    // game/server/baseanimating.h:527), so clamp into what the wire can carry
    // (sent_ball's own sizes are 4..128).  Not an animating entity -> no
    // m_nSkin at all; the variable degrades to the per-table behaviour, which
    // is what the SWEP shim does, instead of throwing inside ENT:Initialize.
    lua_CBaseAnimating *pAnimating = lua_toanimating( L, iSelf );
    if ( pAnimating != NULL ) {
      int iValue = ( int )flValue;
      if ( iValue < 0 ) {
        iValue = 0;
      } else if ( iValue > 511 ) {
        iValue = 511;
      }
      pAnimating->m_nSkin = iValue;
    }

    HL2SB_NWReadValue( L, iSelf, iTable, pszStorageKey, storage, pszType );  // [.., new]
    return;
  }

  // Plain per-table storage.
  if ( iTable != 0 ) {
    if ( HL2SB_NWIsNumericType( pszType ) ) {
      HL2SB_NWRawSetNumber( L, iTable, pszStorageKey, lua_tonumber( L, 2 ) );
    } else {
      HL2SB_NWRawSet( L, iTable, pszStorageKey, 2 );
    }
  }

  HL2SB_NWReadValue( L, iSelf, iTable, pszStorageKey, storage, pszType );  // [.., new]
}

// Set<name>( value ) for one entity.  Upvalue 1 is the type ("Float"), upvalue 2
// the storage key ("__hl2sb_nw_BallSize"), upvalue 3 the slot, upvalue 4 the
// storage kind.  Mirrors the SWEP shim, plus the cross-realm stores above and
// GMod's NetworkVarNotify callback.
//
// Stack discipline (the first version pushed a *stack* slot that did not exist
// and then rawset() a nil key, so every Set<name>() threw "table index is nil";
// the second resolved no table at all, because `self` here is a USERDATA):
//   entry                      [self, value]
//   storage table pushed       [self, value, table]
//   read + store               [self, value, table, old, new]
//   notify (if any)            above, with the callback's frame pushed and popped
//   return                     the extra slots are discarded by the VM
static int HL2SB_Lua_EntityNetworkVarSet (lua_State *L) {
  // Normalise the arguments to self, value (so the notify call below can build
  // its own frame regardless of what the caller passed).
  lua_settop( L, 2 );

  const char *pszType = lua_tostring( L, lua_upvalueindex( 1 ) );
  const char *pszKey = lua_tostring( L, lua_upvalueindex( 2 ) );
  const char *pszName = ( pszKey != NULL ) ? ( pszKey + strlen( "__hl2sb_nw_" ) ) : "";
  const HL2SB_NWStorage_t storage = ( HL2SB_NWStorage_t )lua_tointeger( L, lua_upvalueindex( 4 ) );

  const int iTable = HL2SB_NWPushStorageTable( L, 1 );   // [self, value, table?]

  // GMod fires NetworkVarNotify( name, fn ) as fn( name, old, new ); the old
  // value is read first, before the store below overwrites it.
  HL2SB_NWReadValue( L, 1, iTable, pszKey, storage, pszType );   // [.., old]

  HL2SB_NWStoreValue( L, 1, iTable, pszKey, storage, pszType );  // [.., new]

  const int iNew = lua_gettop( L );
  const int iOld = iNew - 1;

  if ( iTable != 0 ) {
    lua_pushstring( L, "__hl2sb_nwn" );                    // [.., key]
    lua_rawget( L, iTable );                               // [.., notify]
    if ( lua_istable( L, -1 ) ) {
      lua_pushstring( L, pszName );
      lua_rawget( L, -2 );                                 // [.., notify, fn]
      if ( lua_isfunction( L, -1 ) ) {
        lua_pushvalue( L, 1 );                             // self
        lua_pushstring( L, pszName );                      // name
        lua_pushvalue( L, iOld );                          // old
        lua_pushvalue( L, iNew );                          // new
        lua_call( L, 4, 0 );                               // [.., notify]
      } else {
        lua_pop( L, 1 );                                   // [.., notify]
      }
    }

    lua_pop( L, 1 );                                       // [.., table, old, new]
    lua_remove( L, iTable );                               // [self, value, old, new]
  }

  return 0;
}

// Get<name>() for one entity.  Same upvalue layout as the setter.
static int HL2SB_Lua_EntityNetworkVarGet (lua_State *L) {
  const char *pszType = lua_tostring( L, lua_upvalueindex( 1 ) );
  const char *pszKey = lua_tostring( L, lua_upvalueindex( 2 ) );
  const HL2SB_NWStorage_t storage = ( HL2SB_NWStorage_t )lua_tointeger( L, lua_upvalueindex( 4 ) );

  const int iTable = HL2SB_NWPushStorageTable( L, 1 );

  HL2SB_NWReadValue( L, 1, iTable, pszKey, storage, pszType );

  if ( iTable != 0 ) {
    lua_remove( L, iTable );
  }

  return 1;
}

// self:NetworkVar( type, slot, name [, options] )
static int HL2SB_Lua_EntityNetworkVar (lua_State *L) {
  const char *pszType = luaL_checkstring( L, 2 );
  const int iSlot = ( int )luaL_checknumber( L, 3 );
  const char *pszName = luaL_checkstring( L, 4 );

  // The accessors have to end up on the entity's Lua TABLE: that is what
  // CBaseEntity___index falls back to when a script calls Set<name>() on the
  // entity userdata (lbaseentity_shared.cpp:2196).
  const int iTable = HL2SB_NWPushStorageTable( L, 1 );
  if ( iTable == 0 ) {
    luaL_argerror( L, 1, "entity or entity table expected" );
  }

  char szKey[ 160 ];
  Q_snprintf( szKey, sizeof( szKey ), "__hl2sb_nw_%s", pszName );

  const HL2SB_NWStorage_t storage = HL2SB_NWStorageForName( pszName );

  char szSetter[ 160 ];
  char szGetter[ 160 ];
  Q_snprintf( szSetter, sizeof( szSetter ), "Set%s", pszName );
  Q_snprintf( szGetter, sizeof( szGetter ), "Get%s", pszName );

  lua_pushstring( L, pszType );
  lua_pushstring( L, szKey );
  lua_pushinteger( L, iSlot );
  lua_pushinteger( L, ( int )storage );

  lua_pushcclosure( L, HL2SB_Lua_EntityNetworkVarSet, 4 );
  lua_setfield( L, iTable, szSetter );

  lua_pushstring( L, pszType );
  lua_pushstring( L, szKey );
  lua_pushinteger( L, iSlot );
  lua_pushinteger( L, ( int )storage );

  lua_pushcclosure( L, HL2SB_Lua_EntityNetworkVarGet, 4 );
  lua_setfield( L, iTable, szGetter );

  lua_remove( L, iTable );
  return 0;
}

// self:NetworkVarNotify( name, fn )  -- SERVER in GMod; callable anywhere here.
static int HL2SB_Lua_EntityNetworkVarNotify (lua_State *L) {
  const char *pszName = luaL_checkstring( L, 2 );
  luaL_checktype( L, 3, LUA_TFUNCTION );

  const int iTable = HL2SB_NWPushStorageTable( L, 1 );
  if ( iTable == 0 ) {
    // Nothing to attach the callback to (no entity / no scripted table); the
    // variable then simply never notifies, which is what the SWEP shim does.
    return 0;
  }

  lua_pushstring( L, "__hl2sb_nwn" );       // [.., key]
  lua_rawget( L, iTable );                  // [.., notify]
  if ( !lua_istable( L, -1 ) ) {
    lua_pop( L, 1 );
    lua_newtable( L );                      // [.., notify]
    lua_pushstring( L, "__hl2sb_nwn" );     // [.., notify, key]
    lua_pushvalue( L, -2 );                 // [.., notify, key, notify]
    lua_rawset( L, iTable );                // [.., notify]
  }

  lua_pushvalue( L, 3 );                    // [.., notify, fn]
  lua_setfield( L, -2, pszName );           // notify[ name ] = fn
  lua_pop( L, 1 );                          // [..]
  lua_remove( L, iTable );
  return 0;
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
    "  IsStub = true,\n"
    "  LastError = function() return 'sqlite is not available in this engine; nothing is persisted' end,\n"
    "  SQLStr = function( str, bNoQuotes )\n"
    "    local s = tostring( str ):gsub( \"'\", \"''\" )\n"
    "    if ( bNoQuotes ) then return s end\n"
    "    return \"'\" .. s .. \"'\"\n"
    "  end,\n"
    "  TableExists = function() return false end,\n"
    "  Query = function( q )\n"
    "    if ( !sql._warned ) then\n"
    "      sql._warned = true\n"
    "      Msg( \"[HL2SB] sql is a STUB: this engine ships no sqlite3 (see lsrcinit.cpp), so nothing written through sql is persisted and every read comes back empty.\\n\" )\n"
    "      Msg( \"[HL2SB]   first statement swallowed: \" .. tostring( q ) .. \"\\n\" )\n"
    "    end\n"
    "    return {}\n"
    "  end,\n"
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

  /* HL2SB: GMod's non-fatal error prints (see lua_ErrorNoHalt above). */
  lua_pushcfunction( L, lua_ErrorNoHalt );
  lua_setglobal( L, "ErrorNoHalt" );
  lua_pushcfunction( L, lua_ErrorNoHaltWithStack );
  lua_setglobal( L, "ErrorNoHaltWithStack" );

  lua_pushcfunction( L, lua_IsFirstTimePredicted );
  lua_setglobal( L, "IsFirstTimePredicted" );

  /* HL2SB: the scripted-entity network variable shim.  CBaseScripted::
  ** InitScriptedEntity() installs these two onto each entity's Lua table (as
  ** NetworkVar / NetworkVarNotify) before calling ENT:SetupDataTables(), so a
  ** stock GMod entity script declares its network variables without knowing this
  ** engine has no DataTable slots for Lua entities.  Both realms: the client
  ** needs the accessors too, because that is where ENT:Draw reads them. */
  lua_pushcfunction( L, HL2SB_Lua_EntityNetworkVar );
  lua_setglobal( L, "HL2SB_EntityNetworkVar" );
  lua_pushcfunction( L, HL2SB_Lua_EntityNetworkVarNotify );
  lua_setglobal( L, "HL2SB_EntityNetworkVarNotify" );

  /* HL2SB: GMod's `achievements` table, as a STUB.
  **
  ** GMod's achievements library lives in gamemodes/base/gamemode/cl_achievements.lua
  ** and is not part of what was imported here, so the global was simply absent.
  ** GMod's stock sent_ball names it in ENT:Use:
  **     activator:SendLua( "achievements.EatBall()" )
  **
  ** ⚠️ This stub does NOT fix that call, and nothing here claims it does:
  ** Entity:SendLua is a no-op in this fork -- it warns once and returns
  ** (lbaseentity_shared.cpp:2433-2441, "this engine has no client Lua-channel")
  ** -- so the string is never evaluated on either realm and a missing
  ** `achievements` table would never have thrown from this entity.  The
  ** previous comment claimed a console error every time a ball was eaten; that
  ** was wrong, and so is any "fixes the error path" claim.
  **
  ** It is kept because it is harmless and gives GMod scripts/addons that index
  ** the global directly something to call, exactly as the `sql` stub above does.
  ** It records nothing: there is no achievement store and no overlay to award
  ** anything in.  Script may replace the whole table.
  */
  if ( luaL_loadstring( L,
    "achievements = achievements or {}\n"
    "setmetatable( achievements, { __index = function() return function() end end } )\n" ) == 0 )
  {
    lua_pcall( L, 0, 0, 0 );
  }
  else
  {
    Warning( "[HL2SB] lsrcinit: achievements stub failed to compile: %s\n", lua_tostring( L, -1 ) );
    lua_pop( L, 1 );
  }

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


//===== Copyright © 1996-2005, Valve Corporation, All rights reserved. ======//
//
// Purpose: Contains the implementation of Lua for scripting.
//
//===========================================================================//

#include "cbase.h"
#include "filesystem.h"

// HL2SB: the Lua panic handler wants a message box and a hard exit, but pulling
// in <windows.h> here drags in min/max and the window-message macros, which
// break the engine headers included below.  Declare the two entry points
// directly instead.
#ifdef _WIN32
extern "C" __declspec( dllimport ) int __stdcall MessageBoxA( void *hWnd, const char *lpText, const char *lpCaption, unsigned int uType );
extern "C" void __cdecl _exit( int nCode );
extern "C" void __cdecl abort( void );
// Linker-provided base address of this module; lets the panic handler print
// stack addresses as RVAs (symbolizable with the deployed PDB) without pulling
// in <windows.h>, which breaks the engine headers included below.
extern "C" unsigned char __ImageBase;
extern "C" __declspec( dllimport ) unsigned short __stdcall
	RtlCaptureStackBackTrace( unsigned long FramesToSkip, unsigned long FramesToCapture,
	                          void **BackTrace, unsigned long *BackTraceHash );
#define HL2SB_MB_OK         0x00000000u
#define HL2SB_MB_ICONERROR  0x00000010u
#endif
#ifndef CLIENT_DLL
#include "gameinterface.h"
#endif
#include "steam/isteamfriends.h"
#include "networkstringtabledefs.h"
#ifndef CLIENT_DLL
#include "basescriptedtrigger.h"
// HL2SB GMod compat: ENT.Type = "nextbot" (game/server/lua/luanextbot.h).  Server
// only - the client half of a nextbot is C_NextBotCombatCharacter, which DP_NextBot
// pairs with the server's NextBotCombatCharacter.
#include "luanextbot.h"
#endif
#include "basescripted.h"
#include "weapon_hl2mpbase_scriptedweapon.h"
#include "ammodef.h"
#include "luamanager.h"
#include "mountaddons.h"
#include "luasrclib.h"
#include "luacachefile.h"
#include "tier1/lconvar.h"
#include "licvar.h"
#include "lgameevents.h"
#include "activitylist.h"
// HL2SB: the scripted control factories (luaopen_vgui_Panel/Frame/Button) that
// luasrc_init_gameui opens for the main menu state.
#include "lua/vgui_controls/lControls.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

ConVar gamemode( "gamemode", "sandbox", FCVAR_ARCHIVE | FCVAR_REPLICATED );
static char contentSearchPath[MAX_PATH];
static char baseContentSearchPath[MAX_PATH];  // HL2SB: gamemodes/base/content

static void tag_error (lua_State *L, int narg, int tag) {
  luaL_typerror(L, narg, lua_typename(L, tag));
}


LUALIB_API int luaL_checkboolean (lua_State *L, int narg) {
  int d = lua_toboolean(L, narg);
  if (d == 0 && !lua_isboolean(L, narg))  /* avoid extra test when d is not 0 */
    tag_error(L, narg, LUA_TBOOLEAN);
  return d;
}


LUALIB_API int luaL_optboolean (lua_State *L, int narg,
                                              int def) {
  return luaL_opt(L, luaL_checkboolean, narg, def);
}


#ifdef CLIENT_DLL
lua_State *LGameUI;
#endif

lua_State *L;

// Lua system
bool g_bLuaInitialized;

static int luasrc_print (lua_State *L) {
  int n = lua_gettop(L);  /* number of arguments */
  int i;
  char szLine[2048];
  int nOut = 0;
  szLine[0] = '\0';

  lua_getglobal(L, "tostring");
  for (i=1; i<=n; i++) {
    const char *s;
    lua_pushvalue(L, -1);  /* function to be called */
    lua_pushvalue(L, i);   /* value to print */
    lua_call(L, 1, 1);
    s = lua_tostring(L, -1);  /* get result */
    if (s == NULL)
      return luaL_error(L, LUA_QL("tostring") " must return a string to "
                           LUA_QL("print"));

    // HL2SB: the whole line is assembled first so it can go through the shared
    // message writer (colour + hl2sb_lua.log) in ONE piece -- printing per
    // argument put each fragment in the log as its own line.
    nOut += Q_snprintf( szLine + nOut, sizeof( szLine ) - nOut, "%s%s",
                        ( i > 1 ) ? "\t" : "", s );
    if ( nOut >= (int)sizeof( szLine ) - 1 )
      break;
    lua_pop(L, 1);  /* pop result */
  }

  // Same writer the engine uses for its own Lua diagnostics: ice blue, and in
  // the log with an [I] marker when hl2sb_lua_log_colors is on.
  luasrc_LuaConsoleMsg( szLine, 'I', true );
  return 0;
}


static int luasrc_type (lua_State *L) {
  luaL_checkany(L, 1);
  if (lua_getmetatable(L, 1)) {
    lua_pushstring(L, "__type");
	lua_rawget(L, -2);
	lua_remove(L, -2);
	if (!lua_isstring(L, -1))
	  lua_pop(L, 1);
	else
	  return 1;
  }
  lua_pushstring(L, luaL_typename(L, 1));
  return 1;
}


//-----------------------------------------------------------------------------
// HL2SB: include( name ).
//
// Resolved the Team Sandbox way first -- relative to the CALLING file, which is
// what every existing script here relies on.
//
// GMod's include is a search-path lookup instead, so a GMod file that lives in
// one directory can pull in a file from another.  The case that forced this:
// GMod's lua/includes/vgui_base.lua does include( "vgui/DFrame.lua" ) and the
// controls live in lua/vgui/ -- relative resolution would look for
// lua/includes/vgui/DFrame.lua and fail.  GMod's own derma/init.lua is the same
// shape: it is at lua/derma/ and includes "derma.lua" (fine, relative) while the
// bootstrap at lua/includes/ has to reach "derma/init.lua" (needs the root).
//
// So: relative first, then the GMod roots.  Existing behaviour is untouched --
// anything that resolved before still resolves to the same file.
//-----------------------------------------------------------------------------
static bool LuaFileExists (const char *pszPath) {
  return g_pFullFileSystem && g_pFullFileSystem->FileExists( pszPath, "MOD" );
}

//-----------------------------------------------------------------------------
// HL2SB: MOD-relative paths only.  The engine filesystem resolves a path
// against the search paths of the given pathID ("MOD"), so an ABSOLUTE path
// finds nothing -- FileExists answers false and Open fails with
//     cannot open d:/.../lua/includes/extensions/vgui.lua: No such file or directory
// for a file that is plainly there.
//
// Two callers produce absolute paths: include() derives its candidate from the
// calling chunk's name, and a chunk loaded through RelativePathToFullPath
// (lsrcinit's bootstrap pass, luasrc_dofolder_sorted) has an absolute name.
//
// ⚠️ The game directory prefix is stripped BY HAND.  IFileSystem::
// FullPathToRelativePath() strips the *base* directory instead
// ("d:/srceng/hl2sb/lua/x.lua" -> "hl2sb/lua/x.lua"), and a path that still
// carries the game directory name is not MOD-relative either -- every file it
// named failed with "cannot open hl2sb/lua/...: No such file or directory".
//-----------------------------------------------------------------------------
static void LuaMakeRelative (char *pszPath, int nSize) {
  if ( filesystem == NULL || pszPath == NULL || !V_IsAbsolutePath( pszPath ) )
    return;

  char szGameDir[MAX_PATH] = { 0 };
#ifdef CLIENT_DLL
  const char *pszGameDir = engine->GetGameDirectory();
  Q_strncpy( szGameDir, ( pszGameDir != NULL ) ? pszGameDir : "", sizeof( szGameDir ) );
#else
  engine->GetGameDir( szGameDir, sizeof( szGameDir ) );
#endif

  if ( szGameDir[0] )
  {
    // normalise separators on both sides so the compare is meaningful
    for ( char *p = szGameDir; *p; ++p )
      if ( *p == '\\' ) *p = '/';

    int nLen = Q_strlen( szGameDir );
    if ( !Q_strnicmp( pszPath, szGameDir, nLen ) )
    {
      const char *pRest = pszPath + nLen;
      while ( *pRest == '/' || *pRest == '\\' )
        ++pRest;
      Q_strncpy( pszPath, pRest, nSize );
      for ( char *p = pszPath; *p; ++p )
        if ( *p == '\\' ) *p = '/';
      return;
    }
  }

  // Not under the game directory: whatever the filesystem can make of it.
  char szRelative[MAX_PATH];
  if ( filesystem->FullPathToRelativePath( pszPath, szRelative, sizeof( szRelative ) ) && szRelative[0] )
    Q_strncpy( pszPath, szRelative, nSize );
}

//-----------------------------------------------------------------------------
// HL2SB: collapse "a/./b" and "a/../b".  Relative resolution is what makes
// include( "../autorun/x.lua" ) work at all, but the literal path it builds
// ("lua/includes/../autorun/x.lua") is not something the filesystem opens, so
// every candidate has to be normalised before it is tested.
//-----------------------------------------------------------------------------
static void LuaNormalizeDots (const char *pszIn, char *pszOut, int nOutSize) {
  int nOut = 0;
  const char *p = pszIn;

  while (*p && nOut < nOutSize - 2) {
    if (p[0] == '.' && (p[1] == '/' || p[1] == '\\')) { p += 2; continue; }
    if (p[0] == '.' && p[1] == '.' && (p[2] == '/' || p[2] == '\\')) {
      while (nOut > 0 && pszOut[nOut - 1] != '/' && pszOut[nOut - 1] != '\\') --nOut;
      if (nOut > 0) --nOut;                /* drop the separator as well */
      p += 3;
      continue;
    }
    pszOut[nOut++] = *p++;
  }
  pszOut[nOut] = '\0';
}

static int luasrc_include (lua_State *L) {
  lua_Debug ar1;
  lua_getstack(L, 1, &ar1);
  lua_getinfo(L, "f", &ar1);
  lua_Debug ar2;
  lua_getinfo(L, ">S", &ar2);
  int iLength = Q_strlen( ar2.source );
  char source[MAX_PATH];
  Q_StrRight( ar2.source, iLength-1, source, sizeof( source ) );
  Q_StripFilename( source );

  const char *pszName = luaL_checkstring(L, 1);

  // 1. relative to the calling file (Team Sandbox / existing HL2SB scripts).
  char filename[MAX_PATH];
  char normalized[MAX_PATH];
  Q_snprintf( filename, sizeof( filename ), "%s/%s", source, pszName );
  LuaNormalizeDots( filename, normalized, sizeof( normalized ) );
  Q_strncpy( filename, normalized, sizeof( filename ) );
  LuaMakeRelative( filename, sizeof( filename ) );

  if ( !LuaFileExists( filename ) )
  {
    // 2. GMod's roots.  lua/ is the one GMod's own engine defaults to; the
    //    includes/ entry keeps an include() inside lua/includes/ working when
    //    the caller is nested deeper than the file it wants.
    static const char *s_pRoots[] = { "lua/%s", "lua/includes/%s", "%s" };
    for ( int i = 0; i < ARRAYSIZE( s_pRoots ); ++i )
    {
      char candidate[MAX_PATH];
      Q_snprintf( candidate, sizeof( candidate ), s_pRoots[i], pszName );
      LuaNormalizeDots( candidate, normalized, sizeof( normalized ) );
      LuaMakeRelative( normalized, sizeof( normalized ) );
      if ( LuaFileExists( normalized ) )
      {
        Q_strncpy( filename, normalized, sizeof( filename ) );
        break;
      }
    }
  }

  luasrc_dofile(L, filename);
  return 0;
}


static const luaL_Reg base_funcs[] = {
  {"print", luasrc_print},
  {"type", luasrc_type},
  {"include", luasrc_include},
  {NULL, NULL}
};


static void base_open (lua_State *L) {
  /* set global _R */
  lua_pushvalue(L, LUA_REGISTRYINDEX);
  lua_setglobal(L, "_R");
  /* open lib into global table */
  luaL_register(L, "_G", base_funcs);
  lua_pop(L, 1);

  /*
  ** Lua 5.1 standard-library compatibility.
  **
  ** HL2SB moved from Lua 5.1 to 5.4 (taken from Experiment: Source, which also
  ** carries the GLua syntax extensions).  GMod runs Lua 5.1, so every GMod addon
  ** -- and every script in this mod -- assumes the 5.1 library.  Lua 5.4 moved
  ** or removed several of those functions, which broke the hook dispatcher
  ** (hook.lua does `local unpack = unpack`, and unpack is gone -> every hook.call
  ** failed with "attempt to call a nil value (upvalue 'unpack')").
  **
  ** Installing the aliases here fixes every script at once, including addons,
  ** instead of patching each file.  module() and package.seeall are restored by
  ** loadlib.c itself.
  */
  luasrc_dostring( L,
    "unpack = unpack or table.unpack\n"
    "loadstring = loadstring or load\n"
    "table.getn = table.getn or function( t ) return #t end\n"
    "table.setn = table.setn or function( t, n ) return t end\n"
    "table.foreach = table.foreach or function( t, f ) for k, v in pairs( t ) do local r = f( k, v ) if r ~= nil then return r end end end\n"
    "table.foreachi = table.foreachi or function( t, f ) for i, v in ipairs( t ) do local r = f( i, v ) if r ~= nil then return r end end end\n"
    "string.gfind = string.gfind or string.gmatch\n"
    "math.pow = math.pow or function( a, b ) return a ^ b end\n"
    "math.atan2 = math.atan2 or function( y, x ) return math.atan( y, x ) end\n"
    "math.ldexp = math.ldexp or function( m, e ) return m * 2.0 ^ e end\n"
    "math.log10 = math.log10 or function( x ) return math.log( x, 10 ) end\n"
    "math.cosh = math.cosh or function( x ) return ( math.exp( x ) + math.exp( -x ) ) / 2 end\n"
    "math.sinh = math.sinh or function( x ) return ( math.exp( x ) - math.exp( -x ) ) / 2 end\n"
    "math.tanh = math.tanh or function( x ) local e = math.exp( 2 * x ) return ( e - 1 ) / ( e + 1 ) end\n" );

  /* set global _E */
  lua_newtable(L);
  lua_setglobal(L, "_E");
#ifdef CLIENT_DLL
  lua_pushboolean(L, 1);
  lua_setglobal(L, "_CLIENT");  /* set global _CLIENT */
  /* GMod SWEP compat: stock scripts branch on SERVER/CLIENT. */
  lua_pushboolean(L, 0);
  lua_setglobal(L, "SERVER");
  lua_pushboolean(L, 1);
  lua_setglobal(L, "CLIENT");
#else
  lua_pushboolean(L, 1);
  lua_setglobal(L, "_GAME");  /* set global _GAME */
  /* GMod SWEP compat: stock scripts branch on SERVER/CLIENT. */
  lua_pushboolean(L, 1);
  lua_setglobal(L, "SERVER");
  lua_pushboolean(L, 0);
  lua_setglobal(L, "CLIENT");
#endif
}


void luasrc_setmodulepaths(lua_State *L) {
  lua_getglobal(L, LUA_LOADLIBNAME);
#ifdef CLIENT_DLL
	const char *gamePath = engine->GetGameDirectory();
#else
	char gamePath[ 256 ];
	engine->GetGameDir( gamePath, 256 );
#endif

  //Andrew; set package.cpath.
  lua_getfield(L, -1, "cpath");
  //MAX_PATH + package.cpath:len();
  char lookupCPath[MAX_PATH+99];
  Q_snprintf( lookupCPath, sizeof( lookupCPath ), "%s/%s;%s", gamePath,
#ifdef _WIN32
    LUA_PATH_MODULES "\\?.dll",
#elif _LINUX
    LUA_PATH_MODULES "/?.so",
#endif
	luaL_checkstring(L, -1) );
  Q_strlower( lookupCPath );
  Q_FixSlashes( lookupCPath );
  lua_pop(L, 1);  /* pop result */
  lua_pushstring(L, lookupCPath);
  lua_setfield(L, -2, "cpath");

  //Andrew; set package.path.
  lua_getfield(L, -1, "path");
  //MAX_PATH + package.path:len();
  char lookupPath[MAX_PATH+197];
  Q_snprintf( lookupPath, sizeof( lookupPath ), "%s/%s;%s", gamePath, LUA_PATH_MODULES "/?.lua", luaL_checkstring(L, -1) );
  Q_strlower( lookupPath );
  Q_FixSlashes( lookupPath );
  lua_pop(L, 1);  /* pop result */
  lua_pushstring(L, lookupPath);
  lua_setfield(L, -2, "path");

  lua_pop(L, 1);  /* pop result */
}

#ifdef CLIENT_DLL
// Defined below (HL2SB_LuaPanic); the menu state gets it too, otherwise an
// unprotected error in a GameUI script aborts the process the same way.
static int HL2SB_LuaPanic( lua_State *pL );

void luasrc_init_gameui (void) {
  LGameUI = luaL_newstate();
  lua_atpanic( LGameUI, HL2SB_LuaPanic );

  luaL_openlibs(LGameUI);
  base_open(LGameUI);
  lua_pushboolean(LGameUI, 1);
  lua_setglobal(LGameUI, "_GAMEUI");  /* set global _GAMEUI */

  luasrc_setmodulepaths(LGameUI);

  luaopen_ConCommand(LGameUI);
  luaopen_dbg(LGameUI);
  luaopen_engine(LGameUI);
  luaopen_enginevgui(LGameUI);
  luaopen_FCVAR(LGameUI);
  luaopen_KeyValues(LGameUI);
  luaopen_Panel(LGameUI);
  luaopen_surface(LGameUI);
  luaopen_vgui(LGameUI);

  /*
  ** HL2SB: the rest of what a main menu UI needs, so the mod can build one there the
  ** way Garry's Mod does -- its error viewer, for instance, is part of the menu.
  **
  ** Experiment: Source solves the same problem by opening every library with a realm
  ** flag and running their includes/init.lua.  HL2SB has no realm flags on its library
  ** table, and its Team Sandbox era modules are not all menu-safe, so this opens the
  ** specific set the UI actually uses and loads the specific files in dependency
  ** order.  Everything here is additive: the in-game state keeps opening its own set
  ** through luasrc_openlibs and is untouched.
  **
  ** Order matters -- extensions/table.lua provides table.merge, which
  ** extensions/vgui.lua calls, and both must precede the modules that use them.
  */
#ifdef CLIENT_DLL
  // HL2SB: the menu realm's own library (game/client/lua/lua_gameui_menu.cpp): the vgui
  // layout pass this realm never runs, so menu dialogs come up with every control at
  // (0,0).  Declared here rather than in a shared header because the file is client-only
  // (LUALIB_API already carries the linkage, so no extra "extern" - that combination
  // raised C2159).
  LUALIB_API int luaopen_gameui_menu (lua_State *L);

  luaopen_QAngle(LGameUI);
  luaopen_gpGlobals(LGameUI);
  luaopen_input(LGameUI);
  luaopen_Color(LGameUI);  // ColorToHSV / HSVToColor / ColorToHSL / HSLToColor
  // HL2SB: Vector/VMatrix too.  lua/includes/util.lua runs
  // `vector_origin = Vector( 0, 0, 0 )` AT LOAD TIME (util.lua:240), and
  // without the constructor the whole file aborts half way -- taking
  // AccessorFunc / DEFINE_BASECLASS / IsValid with it, which is what left the
  // derma controls unregistered in the menu realm.
  luaopen_Vector(LGameUI);

  // HL2SB: HL2SB_MenuLayout( panel ) / HL2SB_MenuDumpLayout( panel ) - see the header
  // comment in lua_gameui_menu.cpp.  The menu dialogs call the first one after opening:
  // without it SetPos() never lands and every control stacks at (0,0).
  luaopen_gameui_menu(LGameUI);
  luaopen_VMatrix(LGameUI);

  // GMod's file library.  Without it the menu state's only file access is Lua's
  // `io`, which cannot enumerate a folder -- and lua/gameui/contentsubgames.lua
  // needs file.Find / file.Exists to list the Source games installed next to
  // this one (path ID "BASE_PATH").  GMod opens file.* in its menu realm too;
  // that is what its "LuaMenu" path ID is for.
  luaopen_Files(LGameUI);

  // ...and the GMod SPELLINGS for every library opened above.  luaopen_Files
  // publishes "Files"; GMod scripts (and lua/gameui/*.lua) ask for `file`.  The
  // game realms get this from luasrc_openlibs, which this state never calls --
  // so before this, contentsubgames.lua's own `if ( not file ) then return end`
  // guard fired and the Content dialog came up empty with nothing in the log.
  luasrc_install_gmod_lib_aliases(LGameUI);

  // The scripted control factories, normally opened by lsrcinit for the in-game
  // state (see the .vpc entries for scripted_controls/*).
  luaopen_vgui_Panel(LGameUI);  luaopen_vgui_Frame(LGameUI);
  luaopen_vgui_Button(LGameUI);
  luaopen_Label(LGameUI);
  luaopen_TextEntry(LGameUI);

  // HL2SB: the main-menu Addons dialog talks straight to the mount layer
  // (mountaddons.cpp) instead of going through a console command -- it needs
  // the "did it take effect now?" answer back.
  HL2SB_LuaRegisterAddons(LGameUI);

  static const char *const menuFiles[] = {
    LUA_PATH_EXTENSIONS "/table.lua",     // table.merge, used by vgui.register
    LUA_PATH_EXTENSIONS "/vgui.lua",      // vgui.register
    // HL2SB: the panel extension defines the GMod global ToPanel, which
    // lua/gameui/basepanel.lua calls (basepanel.lua:29) and the derma controls
    // rely on.  Without it the Content dialog died on
    // "attempt to call a nil value (global 'ToPanel')".
    LUA_PATH_EXTENSIONS "/panel.lua",
    LUA_PATH_EXTENSIONS "/gmod_globals.lua",  // CurTime, ScrW, ScrH, GetConVar, ...
    LUA_PATH_MODULES "/hook.lua",         // hook.add("LuaError", ...)
    LUA_PATH_MODULES "/concommand.lua",   // concommand.Create
    LUA_PATH_MODULES "/gmod_vgui.lua",    // vgui.Create / vgui.Register
    LUA_PATH_MODULES "/hl2sb_lua_errors.lua",
    NULL
  };

  for (int i = 0; menuFiles[i] != NULL; ++i) {
    // Non-fatal: a missing optional dependency must not take the menu down, and
    // luasrc_dofile already reports what went wrong.
    if (luasrc_dofile(LGameUI, menuFiles[i]) != 0)
      Warning("HL2SB: main menu module failed to load: %s\n", menuFiles[i]);
  }

  // ---------------------------------------------------------------------------
  // HL2SB: the GMod-style derma stack in the menu realm, so main-menu dialogs
  // are real DFrames (the Addons dialog is built on DFrame/DScrollPanel/
  // DCheckBoxLabel/DButton/DTextEntry).  derma/init.lua's realm guard already
  // accepts _GAMEUI; what the menu state lacks versus the client realm is the
  // loader and two globals:
  //
  //   * FindMetaTable -- DFrame.lua captures the Panel metatable with it at
  //     load time (lsrcinit.cpp's lua_metatable_funcs),
  //   * include()     -- derma/init.lua and includes/vgui_base.lua load
  //     lua-root relative files through it.
  //
  // Both load steps are non-fatal: a control file that cannot load must not
  // take the main menu down (the same rule the menuFiles loop follows).
  // ---------------------------------------------------------------------------
  luasrc_register_metatable_globals(LGameUI);

  // NOTE: include() is NOT shimmed here.  base_open() already published the
  // engine's own luasrc_include, which does the same caller-relative-then-roots
  // search; an earlier menu shim replaced it (and lost the lua/includes/ root).
  // What it did need is ".." normalisation, which now lives in luasrc_include.

  // GMod's type helpers (isstring / istable / ispanel / isentity, and the
  // GMod `type` override) live in lua/includes/util.lua, which the client
  // realm loads from init.lua and the menu realm never did -- derma calls
  // istable at load time and died with
  // "attempt to call a nil value (global 'istable')" (hl2sb_derma.lua:190).
  if (luasrc_dofile(LGameUI, LUA_ROOT "/includes/util.lua") != 0)
    Warning("HL2SB: menu realm: GMod type helpers (lua/includes/util.lua) failed to load\n");

  // GMod's DEFINE_BASECLASS is a PREPROCESSOR keyword here (see the rewrite pass
  // below): it expands to `local BaseClass = baseclass.Get( "X" )`, so the
  // module has to exist in whatever realm runs such a file.
  if (luasrc_dofile(LGameUI, LUA_ROOT "/includes/modules/baseclass.lua") != 0)
    Warning("HL2SB: menu realm: baseclass module failed to load\n");

  // NOTE: the full derma stack (lua/derma + lua/includes/vgui_base.lua) is NOT
  // loaded here any more.  It registered nothing in this realm -- every control
  // died in vgui.Register with
  //     base class 'Panel' is not an engine class and not a registered control
  // -- which produced a wall of load failures on every start, and the menu UI
  // (the Addons dialog's default front end, the Content page) is built from the
  // engine controls and never asks for a DFrame.  derma stays a client-realm
  // feature; the addons dialog's derma front end is opt-in and simply falls
  // back when the stack is absent.

  // ---------------------------------------------------------------------------
  // HL2SB: menu-realm self check.  Every UI bug in this realm so far had the
  // same shape: something the CLIENT realm loads (a library, a global, a
  // loader rule) was missing HERE, and it surfaced much later as "a panel
  // behaves strangely" -- a nil DLabel, a monochrome error window, an invisible
  // frame.  One line at startup naming what is missing is worth more than any
  // of those symptoms: it says whether the environment is complete.
  // ---------------------------------------------------------------------------
  static const char *const s_pMenuSelfCheck =
    "local need = { 'include', 'istable', 'isfunction', 'ispanel', 'AccessorFunc',"
    " 'ToPanel', 'IsValid', 'Msg', 'MsgN', 'Warning', 'ErrorNoHalt', 'surface',"
    " 'vgui', 'file', 'baseclass' }\n"
    "local missing = {}\n"
    "for _, n in ipairs( need ) do\n"
    "  if ( _G[ n ] == nil ) then missing[ #missing + 1 ] = n end\n"
    "end\n"
    "-- The engine control factories the menu UI is built from.\n"
    "if ( vgui ~= nil ) then\n"
    "  for _, c in ipairs( { 'Frame', 'Label', 'Button', 'CheckButton', 'TextEntry', 'Panel' } ) do\n"
    "    if ( rawget( vgui, c ) == nil ) then missing[ #missing + 1 ] = 'factory:vgui.' .. c end\n"
    "  end\n"
    "end\n"
    "if ( #missing > 0 ) then\n"
    "  Msg( '[HL2SB] menu realm: MISSING ' .. table.concat( missing, ', ' ) .. '\\n' )\n"
    "else\n"
    "  Msg( '[HL2SB] menu realm: OK (control factories + globals + loaders complete)\\n' )\n"
    "end\n";

  luasrc_dostring(LGameUI, s_pMenuSelfCheck);
#endif

  Msg("Lua Menu initialized (" LUA_VERSION ")\n");
}

void luasrc_shutdown_gameui (void) {
  ResetGameUIConCommandDatabase();

  lua_close(LGameUI);
}
#endif

//-----------------------------------------------------------------------------
// HL2SB: Lua panic handler.
//
// When Lua raises an error and there is NO protected call on the stack,
// luaD_throw() calls g->panic(L).  If no panic function is installed it calls
// abort() -- and on x64 UCRT abort() ends in __fastfail (int 29h).  That is a
// kernel fast-fail: SEH, vectored handlers and even a SIGABRT handler installed
// later in the process never get a chance, so the game simply vanishes with no
// dump (this was the project's dominant crash mode: 26 events, client.dll).
//
// Installing a panic function turns that whole class into a readable report:
// the real error message plus a Lua traceback, written to the console log and
// to hl2sb_lua_panic.log.
//
// Returning from a panic function is undefined behaviour in Lua, so we report
// and then exit; the process is already unusable at that point.
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
// HL2SB: the one place a Lua error is printed.
//
// GMod shows Lua errors in RED, and a plain Warning is easy to lose in a
// scrolling console.  BOTH sinks on purpose: ConColorMsg is what colours the
// console line, Warning is the channel this fork's diagnosis has always been read
// from (it lands in engine.log).  A duplicated line is a far smaller problem than
// a missing one.
//
// Level 0 rather than the ConColorMsg( Color&, ... ) overload: that one gates on
// the "console" spew group at level 1, and IsSpewActive() answers from m_Level,
// whose default is 0 -- so it would drop every Lua error, console and log alike.
//-----------------------------------------------------------------------------

// HL2SB: the Lua log collector.  When hl2sb_lua_log is on, EVERY message that
// goes through luasrc_Lua{Error,Warn,Info}Msg lands in hl2sb_lua.log next to
// ds_debug.log -- one file holding the whole session's Lua activity, instead of
// thousands of console lines mixed with engine spam.  Appends per line (the
// volume is modest and this survives crashes without a close hook).
// HL2SB: FCVAR_CLIENTDLL is not decoration here -- luamanager.cpp is compiled
// into BOTH dlls, and without it the engine rejects the server-side twin with
//     Parent Cvar in server.dll not allowed (hl2sb_lua_log)
// on every start.  The flag is what tells it the cvar belongs to the client.
static ConVar hl2sb_lua_log( "hl2sb_lua_log", "1", FCVAR_ARCHIVE | FCVAR_CLIENTDLL,
	"Write all Lua console output (errors/warnings/diagnostics) to hl2sb_lua.log" );
static ConVar hl2sb_lua_log_colors( "hl2sb_lua_log_colors", "0", FCVAR_ARCHIVE | FCVAR_CLIENTDLL,
	"Prefix each hl2sb_lua.log line with [E]/[W]/[I] severity markers" );

static const char *s_pszLuaLogFile = "hl2sb_lua.log";

void luasrc_LuaLogToFile( const char *pszText, char cSeverity )
{
	if ( !hl2sb_lua_log.GetBool() || !g_pFullFileSystem || pszText == NULL )
		return;

	char szLine[ 2048 ];
	int nLen = Q_strlen( pszText );
	while ( nLen > 0 && ( pszText[ nLen - 1 ] == '\n' || pszText[ nLen - 1 ] == '\r' ) )
		--nLen;
	if ( nLen >= (int)sizeof( szLine ) )
		nLen = sizeof( szLine ) - 1;

	if ( hl2sb_lua_log_colors.GetBool() )
		Q_snprintf( szLine, sizeof( szLine ), "[%c] %.*s", cSeverity, nLen, pszText );
	else
		Q_snprintf( szLine, sizeof( szLine ), "%.*s", nLen, pszText );

	FileHandle_t fh = g_pFullFileSystem->Open( s_pszLuaLogFile, "at", "MOD" );
	if ( fh == FILESYSTEM_INVALID_HANDLE )
		return;
	g_pFullFileSystem->FPrintf( fh, "%s\n", szLine );
	g_pFullFileSystem->Close( fh );
}
// HL2SB: ONE console write per message.  These helpers used to print twice --
// ConColorMsg for the colour and Warning/Msg for the engine log -- and the
// console showed every Lua line doubled.  The file channel is hl2sb_lua.log
// (luasrc_LuaLogToFile below), so the plain write is gone.
LUA_API void luasrc_LuaErrorMsg (const char *pszText)
{
	if ( pszText == NULL )
		return;

	ConColorMsg( 0, Color( 255, 64, 64, 255 ), "%s\n", pszText );	// red
	luasrc_LuaLogToFile( pszText, 'E' );
}

// HL2SB: the warning half.  Orange, and it exists so "this script has no ENT.X"
// and "that lookup raised" do not read like an error that lost an entity -- they
// are the same sentence in two different colours.
LUA_API void luasrc_LuaWarnMsg (const char *pszText)
{
	if ( pszText == NULL )
		return;

	ConColorMsg( 0, Color( 255, 165, 0, 255 ), "%s\n", pszText );	// orange
	luasrc_LuaLogToFile( pszText, 'W' );
}

// HL2SB: the informational half (script loads, bind reports, folder summaries).
// GMod prints these in the console's default colour; here they additionally land
// in hl2sb_lua.log when the collector is on, so a whole session's Lua activity
// is one greppable file.
LUA_API void luasrc_LuaInfoMsg (const char *pszText)
{
	if ( pszText == NULL )
		return;

	ConColorMsg( 0, Color( 200, 236, 255, 255 ), "%s\n", pszText );	// ice blue
	luasrc_LuaLogToFile( pszText, 'I' );
}

//-----------------------------------------------------------------------------
// HL2SB: THE writer for everything Lua prints, so console colour and the
// hl2sb_lua.log are decided in ONE place:
//
//     print / Msg / MsgN            -> info   (ice blue)
//     Warning / dbg.Warning         -> warn   (orange)
//     ErrorNoHalt / Lua errors      -> error  (red)
//
// Msg is a building block -- GMod code writes half a line and finishes it with
// MsgN -- so the trailing newline stays the caller's decision, and only
// complete lines go into the log (a half line would be split at the wrong
// place in the file).
//-----------------------------------------------------------------------------
LUA_API void luasrc_LuaConsoleMsg (const char *pszText, char cSeverity, bool bNewline)
{
	if ( pszText == NULL )
		return;

	Color clr = Color( 200, 236, 255, 255 );	// info: ice blue
	if ( cSeverity == 'E' )
		clr = Color( 255, 64, 64, 255 );		// error: red
	else if ( cSeverity == 'W' )
		clr = Color( 255, 165, 0, 255 );		// warning: orange

	ConColorMsg( 0, clr, "%s%s", pszText, bNewline ? "\n" : "" );

	// The console write above is the ONLY one (see luasrc_LuaErrorMsg).
	if ( bNewline )
		luasrc_LuaLogToFile( pszText, cSeverity );
}

// HL2SB: the printf-style forms (see the declaration for why they exist here
// rather than at the call site).
LUA_API void luasrc_LuaErrorMsgF (const char *pszFormat, ...)
{
	char szBuffer[2048];

	va_list args;
	va_start( args, pszFormat );
	Q_vsnprintf( szBuffer, sizeof( szBuffer ), pszFormat, args );
	va_end( args );

	luasrc_LuaErrorMsg( szBuffer );
}

LUA_API void luasrc_LuaWarnMsgF (const char *pszFormat, ...)
{
	char szBuffer[2048];

	va_list args;
	va_start( args, pszFormat );
	Q_vsnprintf( szBuffer, sizeof( szBuffer ), pszFormat, args );
	va_end( args );

	luasrc_LuaWarnMsg( szBuffer );
}

LUA_API void luasrc_LuaInfoMsgF (const char *pszFormat, ...)
{
	char szBuffer[2048];

	va_list args;
	va_start( args, pszFormat );
	Q_vsnprintf( szBuffer, sizeof( szBuffer ), pszFormat, args );
	va_end( args );

	luasrc_LuaInfoMsg( szBuffer );
}

static int HL2SB_LuaPanic( lua_State *pL )
{
	const char *pszMsg = lua_tostring( pL, -1 );
	if ( !pszMsg )
		pszMsg = "(error object is not a string)";

	// Traceback of where the unprotected error came from.
	//
	// HL2SB: level 0, NOT 1.  A panic function is not a frame on the CallInfo
	// chain -- it is called straight out of luaD_throw -- so level 1 skips the
	// TOPMOST frame, which is exactly the one that raised the error.  With 1 the
	// traceback came back as a bare "stack traceback:" and the log named no file,
	// which is why every one of these reports read as "attempt to call a nil
	// value" and nothing else.
	luaL_traceback( pL, pL, pszMsg, 0 );
	const char *pszTrace = lua_tostring( pL, -1 );

	luasrc_LuaErrorMsg( "\n[HL2SB] *** LUA PANIC - unprotected Lua error ***" );
	luasrc_LuaErrorMsg( pszTrace ? pszTrace : pszMsg );

	// The Lua traceback can still come back empty when the error was raised out of
	// a pure C chain -- no Lua frames at all -- so capture the NATIVE stack too.
	// Only the client has a SIGABRT handler that writes a minidump -- a panic on
	// the server otherwise left no usable information at all.  Printing the
	// addresses relative to this module's base makes them symbolizable with the
	// deployed PDB.
	//
	// __ImageBase is provided by the linker in every DLL, so this needs no
	// Windows headers (including <windows.h> here breaks the engine headers).
	// HL2SB: both __ImageBase and RtlCaptureStackBackTrace are MSVC/Windows-only
	// (gcc: "'__ImageBase' was not declared in this scope"), so the block is
	// guarded; on Linux/Android the SIGSEGV/SIGABRT handler in
	// hl2sb_crash_handler.cpp already writes the native stack to engine.log.
#ifdef _WIN32
	{
		void *pStack[ 64 ];
		unsigned short nFrames = RtlCaptureStackBackTrace( 1, 64, pStack, NULL );
		const unsigned char *pBase = &__ImageBase;

		char szStackLine[ 128 ];
		Q_snprintf( szStackLine, sizeof( szStackLine ),
			"[HL2SB] native stack (%u frames), module base %p:", nFrames, pBase );
		Msg( "%s\n", szStackLine );
		luasrc_LuaLogToFile( szStackLine, 'E' );
		for ( unsigned short i = 0; i < nFrames; i++ )
		{
			Q_snprintf( szStackLine, sizeof( szStackLine ),
				"[HL2SB]   %02u: base+0x%llX  (%p)", i,
				(unsigned long long)( (const unsigned char *)pStack[ i ] - pBase ), pStack[ i ] );
			Msg( "%s\n", szStackLine );
			luasrc_LuaLogToFile( szStackLine, 'E' );
		}

		if ( FILE *fp = fopen( "hl2sb_lua_panic.log", "a" ) )
		{
			fprintf( fp, "native stack, module base %p\n", pBase );
			for ( unsigned short i = 0; i < nFrames; i++ )
				fprintf( fp, "  %02u: base+0x%llX\n", i,
				         (unsigned long long)( (const unsigned char *)pStack[ i ] - pBase ) );
			fflush( fp );
			fclose( fp );
		}
	}
#else
	Msg( "[HL2SB] native stack: see the [HL2SB] crash block in engine.log\n" );
#endif

	if ( FILE *fp = fopen( "hl2sb_lua_panic.log", "a" ) )
	{
		fprintf( fp, "\n=== HL2SB Lua panic ===\n%s\n=== End ===\n", pszTrace ? pszTrace : pszMsg );
		fflush( fp );
		fclose( fp );
	}

#ifdef _WIN32
	{
		char szBox[ 1024 ];
		Q_snprintf( szBox, sizeof( szBox ),
			"HL2SB: unprotected Lua error (would have been a silent abort).\n\n%s\n\n"
			"Full traceback: hl2sb_lua_panic.log and the console log.", pszMsg );
		MessageBoxA( NULL, szBox, "HL2SB Lua Error", HL2SB_MB_OK | HL2SB_MB_ICONERROR );
	}
#endif

	// Do NOT return into Lua -- the stack is unusable.
	//
	// abort() rather than _exit(): the Lua-side traceback above is usually empty
	// (the CallInfo chain is already unwound by the time the panic runs), so the
	// only way to see WHERE this came from is the NATIVE stack.  abort() reaches
	// the SIGABRT handler in hl2sb_crash_handler.cpp, which captures that stack
	// and writes a minidump.
	abort();
	return 0;
}

void luasrc_init (void) {
  if (g_bLuaInitialized)
	  return;
  g_bLuaInitialized = true;

  // HL2SB: session marker for the Lua log collector - every state init starts
  // one of these (state init happens per level load and per realm).
  {
    char szBanner[ 128 ];
    Q_snprintf( szBanner, sizeof( szBanner ),
      "=== Lua log session (state init, %s) ===",
#ifdef CLIENT_DLL
      "client"
#else
      "server"
#endif
    );
    luasrc_LuaLogToFile( szBanner, 'I' );
  }

  L = lua_open();

  // HL2SB: without this, any unprotected Lua error kills the process via
  // __fastfail before any of our crash handling can run.  See above.
  lua_atpanic( L, HL2SB_LuaPanic );

  luaL_openlibs(L);
  base_open(L);
//  not now
//  lcf_open(L);

  // Andrew; Someone set us up the path for great justice
  luasrc_setmodulepaths(L);

  luasrc_openlibs(L);

  // HL2SB: gameevent.Listen() needs an engine-side listener; it lives as long as the
  // state does (Experiment: Source does the same in their Lua init).
  InitializeLuaGameEventHandler(L);

  Msg( "Lua initialized (" LUA_VERSION ")\n" );
}

void luasrc_shutdown (void) {
  if (!g_bLuaInitialized)
	  return;

  g_bLuaInitialized = false;

  filesystem->RemoveSearchPath( contentSearchPath, "MOD" );
  filesystem->RemoveSearchPath( baseContentSearchPath, "MOD" );

  ResetConCommandDatabase();

  RemoveGlobalChangeCallbacks();
  ResetConVarDatabase();

#ifndef CLIENT_DLL
  ResetTriggerFactoryDatabase();
#endif
  ResetEntityFactoryDatabase();
  ResetWeaponFactoryDatabase();

  ShutdownLuaGameEventHandler(L);

  // HL2SB: the net.Receive table holds luaL_ref() registry indices into THIS
  // state.  Drop them before the state goes away, or a LuaNet message that
  // arrives during the level transition is dispatched against stale indices in
  // the next state (that abort was the client.dll crash).
  luasrc_net_reset();

//  lcf_close(L);
  lua_close(L);

  // HL2SB: ported from Experiment: Source.  luaopen_ACTIVITY owns the activity
  // list under the Lua SDK (see the LUA_SDK branch of REGISTER_SHARED_ACTIVITY in
  // activitylist.h and the calls guarded out of c_world.cpp / world.cpp), so it is
  // released here together with the state that filled _E.ACTIVITY rather than per
  // level.  Without this, a shutdown/init cycle would register all ~700 ACT_* a
  // second time and report a shared activity collision for each one.
  ActivityList_Free();

  // Clear the global state pointer: entities are destroyed after Lua shuts
  // down (C_World / C_BaseEntity destructors run from CHLClient::Shutdown),
  // and their lua_unref( L, m_nTableReference ) would otherwise dereference
  // a freed lua_State and crash on exit.
  L = NULL;
}

/*
** HL2SB: report a Lua error to the console *and* to scripts.
**
** Every error that reaches here used to go to Warning() alone, so a mod could
** only see it by reading the console -- GMod instead shows it in-game and lets
** addons hook it.  The hook is "LuaError( message, traceback )"; a re-entrancy
** guard keeps a broken error handler from recursing into itself.
*/
static bool g_bReportingLuaError = false;

LUA_API void luasrc_report_error (lua_State *L, const char *pszError) {
  if (!pszError)
    pszError = "(no error message)";

  // HL2SB: this is THE place a Lua error becomes visible, so it is where the red
  // comes from (GMod shows Lua errors in colour; a plain Warning is easy to lose in
  // a scrolling console).  luasrc_LuaErrorMsg() prints coloured AND still Warning()s,
  // so the text keeps landing in engine.log.
  luasrc_LuaErrorMsg(pszError);

  if (g_bReportingLuaError)
    return;

  g_bReportingLuaError = true;

  // Build the traceback first and copy it into a C buffer.  Doing it inline in the
  // hook call would mean addressing it by stack offset while the hook macros are
  // pushing hook/call/name/_GAMEMODE underneath it.
  char szTraceback[2048];
  szTraceback[0] = '\0';
  // HL2SB: level 0, not 1 -- this runs from C, which is not a CallInfo frame, so
  // level 1 dropped the topmost frame: the very one that raised the error.
  luaL_traceback(L, L, pszError, 0);
  if (lua_isstring(L, -1))
    Q_strncpy(szTraceback, lua_tostring(L, -1), sizeof(szTraceback));
  lua_pop(L, 1);

  LUA_CALL_HOOK_FOR_STATE_BEGIN(L, "LuaError");
    lua_pushstring(L, pszError);
    lua_pushstring(L, szTraceback);
  LUA_CALL_HOOK_FOR_STATE_END(L, 2, 0);

  g_bReportingLuaError = false;
}

LUA_API int luasrc_dostring (lua_State *L, const char *string) {
  int iError = luaL_dostring(L, string);
  if (iError != 0) {
    luasrc_report_error(L, lua_tostring(L, -1));
    lua_pop(L, 1);
  }
  return iError;
}

LUA_API int luasrc_dofile (lua_State *L, const char *filename) {
	// HL2SB: an ABSOLUTE path cannot be opened with pathID "MOD" (see
	// LuaMakeRelative) -- several loaders hand us RelativePathToFullPath output,
	// and every one of those reported
	//     [Lua] FAILED d:/...vgui.lua: cannot open ...: No such file or directory
	// for a file that exists.  Convert first; the check below then sees it.
	char szRelative[MAX_PATH];
	if ( filesystem != NULL && filename != NULL && V_IsAbsolutePath( filename ) &&
		 filesystem->FullPathToRelativePath( filename, szRelative, sizeof( szRelative ) ) && szRelative[0] )
		filename = szRelative;

	// GLua syntax compat: stock GMod scripts (and many workshop SWEPs) use
	// C-style `//` line comments and the `!=` operator, which standard Lua 5.1
	// rejects. Rewrite them in memory before loading: `//` -> `--`,
	// `!=` -> `~=`. String literals and comments are tracked so their
	// contents are left untouched. Falls back to plain luaL_dofile when the
	// file is not visible through the engine filesystem.
	if ( !filesystem || !filesystem->FileExists( filename, "MOD" ) )
	{
		int iFallback = luaL_dofile(L, filename);
		if (iFallback != 0) {
			// HL2SB: greppable marker.  Bulk-importing GMod's derma/ or vgui/
			// (100+ files) has to be triageable from the log alone, so every
			// load failure names its own file.
			luasrc_LuaErrorMsgF( "[Lua] FAILED %s: %s", filename, lua_tostring(L, -1) );
			lua_pop(L, 1);
		}
		return iFallback;
	}

	FileHandle_t fh = g_pFullFileSystem->Open( filename, "rb", "MOD" );
	if ( !fh )
	{
		Warning( "luasrc_dofile: cannot open %s\n", filename );
		lua_pushnil( L );
		lua_pushfstring( L, "cannot open %s: No such file or directory", filename );
		return 2;
	}
	int nSize = g_pFullFileSystem->Size( fh );
	char *pBuf = (char *)malloc( nSize + 1 );
	int nRead = 0;
	if ( pBuf )
		nRead = g_pFullFileSystem->ReadEx( pBuf, nSize, nSize, fh );
	g_pFullFileSystem->Close( fh );
	if ( !pBuf )
	{
		Warning( "luasrc_dofile: out of memory reading %s\n", filename );
		lua_pushnil( L );
		lua_pushfstring( L, "out of memory reading %s", filename );
		return 2;
	}

	// Normalise CRLF (Source files may be checked out either way).
	int nClean = 0;
	for ( int i = 0; i < nRead; i++ )
	{
		if ( pBuf[i] != '\r' )
			pBuf[nClean++] = pBuf[i];
	}

	// HL2SB: 2x, not 1x.  Every rewrite below is length-preserving except
	// DEFINE_BASECLASS, whose replacement (30 bytes) is nearly twice the token
	// (16), so the old nClean+1 buffer would overflow on a file that uses it.
	char *pOut = (char *)malloc( nClean * 2 + 1 );
	if ( !pOut )
	{
		free( pBuf );
		Warning( "luasrc_dofile: out of memory translating %s\n", filename );
		lua_pushnil( L );
		lua_pushfstring( L, "out of memory translating %s", filename );
		return 2;
	}

	size_t o = 0;
	enum EState { CODE, SQ_STR, DQ_STR, LONG_STR, LINE_COMMENT, LONG_COMMENT } state = CODE;
	int i2 = 0;
	while ( i2 < nClean )
	{
		char c = pBuf[i2];
		switch ( state )
		{
		case SQ_STR:
			pOut[o++] = c;
			if ( c == '\\' && i2 + 1 < nClean ) { pOut[o++] = pBuf[++i2]; }
			else if ( c == '\'' ) state = CODE;
			i2++;
			break;
		case DQ_STR:
			pOut[o++] = c;
			if ( c == '\\' && i2 + 1 < nClean ) { pOut[o++] = pBuf[++i2]; }
			else if ( c == '"' ) state = CODE;
			i2++;
			break;
		case LONG_STR:
			pOut[o++] = c;
			if ( c == ']' && i2 + 1 < nClean && pBuf[i2+1] == ']' ) { pOut[o++] = pBuf[++i2]; state = CODE; }
			i2++;
			break;
		case LINE_COMMENT:
			pOut[o++] = c;
			if ( c == '\n' ) state = CODE;
			i2++;
			break;
		case LONG_COMMENT:
			pOut[o++] = c;
			if ( c == ']' && i2 + 1 < nClean && pBuf[i2+1] == ']' ) { pOut[o++] = pBuf[++i2]; state = CODE; }
			i2++;
			break;
		default: // CODE
			if ( c == '\'' ) { pOut[o++] = c; state = SQ_STR; i2++; }
			else if ( c == '"' ) { pOut[o++] = c; state = DQ_STR; i2++; }
			else if ( c == '[' && i2 + 1 < nClean && pBuf[i2+1] == '[' ) { pOut[o++] = c; pOut[o++] = pBuf[++i2]; state = LONG_STR; i2++; }
			else if ( c == '-' && i2 + 1 < nClean && pBuf[i2+1] == '-' )
			{
				pOut[o++] = c; pOut[o++] = pBuf[++i2]; i2++;
				if ( i2 < nClean && pBuf[i2] == '[' && i2 + 1 < nClean && pBuf[i2+1] == '[' )
				{ pOut[o++] = pBuf[i2++]; pOut[o++] = pBuf[i2++]; state = LONG_COMMENT; }
				else state = LINE_COMMENT;
			}
			else if ( c == '/' && i2 + 1 < nClean && pBuf[i2+1] == '/' ) { pOut[o++] = '-'; pOut[o++] = '-'; i2 += 2; state = LINE_COMMENT; }
			else if ( c == '!' && i2 + 1 < nClean && pBuf[i2+1] == '=' ) { pOut[o++] = '~'; pOut[o++] = '='; i2 += 2; }
			// HL2SB: DEFINE_BASECLASS( "X" ) -- a Garry's Mod PREPROCESSOR keyword,
			// not a function.  Its own wiki: "directly replaced with local
			// BaseClass = baseclass.Get", and lua/includes/modules/baseclass.lua
			// documents the same expansion.  Only the identifier is replaced, so
			// the trailing ( "X" ) stays and the result is
			// local BaseClass = baseclass.Get( "X" ).
			//
			// Done here rather than as a global function because GMod does it at
			// the lexer level: the argument is part of the replacement, and a
			// function could not introduce a `local`.
			//
			// Identifier-boundary checked, so DEFINE_BASECLASS_X or a longer name
			// is untouched -- and, unlike GMod's own pass, strings and comments are
			// left alone (the state machine above already guarantees that).
			else if ( ( ( c >= 'a' && c <= 'z' ) || ( c >= 'A' && c <= 'Z' ) || c == '_' ) &&
			          ( i2 == 0 || !( ( pBuf[i2-1] >= 'a' && pBuf[i2-1] <= 'z' ) ||
			                          ( pBuf[i2-1] >= 'A' && pBuf[i2-1] <= 'Z' ) ||
			                          ( pBuf[i2-1] >= '0' && pBuf[i2-1] <= '9' ) ||
			                          pBuf[i2-1] == '_' ) ) &&
			          i2 + 16 <= nClean &&
			          !Q_strnicmp( pBuf + i2, "DEFINE_BASECLASS", 16 ) &&
			          ( i2 + 16 >= nClean || !( ( pBuf[i2+16] >= 'a' && pBuf[i2+16] <= 'z' ) ||
			                                    ( pBuf[i2+16] >= 'A' && pBuf[i2+16] <= 'Z' ) ||
			                                    ( pBuf[i2+16] >= '0' && pBuf[i2+16] <= '9' ) ||
			                                    pBuf[i2+16] == '_' ) ) )
			{
				static const char szDefineBaseClass[] = "local BaseClass = baseclass.Get";
				for ( const char *p = szDefineBaseClass; *p; ++p )
					pOut[o++] = *p;
				i2 += 16;
			}
			else { pOut[o++] = c; i2++; }
			break;
		}
	}
	free( pBuf );

	char chunkname[ MAX_PATH + 16 ];
	Q_snprintf( chunkname, sizeof( chunkname ), "@%s", filename );

	int iError = luaL_loadbuffer( L, pOut, o, chunkname );
	if ( iError == 0 )
		iError = lua_pcall( L, 0, LUA_MULTRET, 0 );
	if ( iError != 0 ) {
		luasrc_LuaErrorMsgF( "[Lua] FAILED %s: %s", filename, lua_tostring(L, -1) );
		lua_pop(L, 1);
	}
	free( pOut );
	return iError;
}

LUA_API void luasrc_dofolder (lua_State *L, const char *path)
{
	FileFindHandle_t fh;

	char searchPath[ 512 ];
	Q_snprintf( searchPath, sizeof( searchPath ), "%s/*.lua", path );

	char const *fn = g_pFullFileSystem->FindFirstEx( searchPath, "MOD", &fh );
	int nLoaded = 0;
	// HL2SB: count failures, and restore the stack after every file.  A chunk
	// that returns something at top level (LUA_MULTRET) would otherwise leave
	// its results behind, one more slot per file -- the same class of silent
	// stack leak that took the HUD down (see AGENTS.md 5.4.2).
	int nFailed = 0;
	const int nTop = lua_gettop( L );

	//-----------------------------------------------------------------------------
	// HL2SB: GMod's module dependency order.
	//
	// This folder used to load purely in directory order, but the GMod modules
	// imported into lua/includes/modules/ were written against GMod's own ordered
	// require list in its lua/includes/init.lua.  Four of them therefore failed to
	// load because the module they use had not been read yet:
	//
	//     cleanup.lua      needs hook         (cleanup < hook)
	//     construct.lua    needs duplicator   (construct < duplicator)
	//     numpad.lua       needs saverestore  (numpad < saverestore)
	//     entity_iter.lua  needs player       (entity_iter < player)
	//
	// Re-loading one is NOT an option: these files are plain dofile'd chunks with
	// no re-entry guard, so a second execution re-runs their `local tHooks = {}`
	// and silently detaches what the first pass registered (AGENTS.md 5.4).  Hence
	// two passes with a skip list rather than a reorder of one pass.
	//
	// Names that are not in this folder (hook.lua when this runs for
	// extensions/, for instance) are simply skipped, so this is safe for every
	// other dofolder call site.
	//-----------------------------------------------------------------------------
	static const char *s_pHL2SBFirst[] = {
		"hook.lua", "net.lua", "timer.lua", "concommand.lua",
		"player_manager.lua", "player.lua", "entity_iter.lua",
		"saverestore.lua", "scripted_ents.lua", "weapons.lua",
		"duplicator.lua", "numpad.lua", "construct.lua", "constraint.lua",
		"cleanup.lua", "usermessage.lua", "properties.lua",
		"presets.lua",
		NULL
	};

	char szLoadedFirst[ ARRAYSIZE( s_pHL2SBFirst ) ][ 64 ];
	int nLoadedFirst = 0;

	for ( int k = 0; s_pHL2SBFirst[ k ] != NULL; ++k )
	{
		char relative[ 512 ];
		Q_snprintf( relative, sizeof( relative ), "%s/%s", path, s_pHL2SBFirst[ k ] );

		if ( !LuaFileExists( relative ) )
			continue;

		char loadname[ 512 ];
		filesystem->RelativePathToFullPath( relative, "MOD", loadname, sizeof( loadname ) );

		luasrc_LuaInfoMsgF( "[Lua]   %s  (prerequisite)\n", relative );
		if ( luasrc_dofile( L, loadname ) != 0 )
			++nFailed;
		lua_settop( L, nTop );

		Q_strncpy( szLoadedFirst[ nLoadedFirst ], s_pHL2SBFirst[ k ], sizeof( szLoadedFirst[ 0 ] ) );
		++nLoadedFirst;
	}

	while ( fn )
	{
		if ( fn[0] != '.' )
		{
			// HL2SB: already loaded in the prerequisite pass above.
			bool bAlreadyLoaded = false;
			for ( int s = 0; s < nLoadedFirst; ++s )
			{
				if ( !Q_stricmp( fn, szLoadedFirst[ s ] ) )
				{
					bAlreadyLoaded = true;
					break;
				}
			}

			if ( bAlreadyLoaded )
			{
				fn = g_pFullFileSystem->FindNext( fh );
				continue;
			}
			char ext[ 10 ];
			Q_ExtractFileExtension( fn, ext, sizeof( ext ) );

			if ( !Q_stricmp( ext, "lua" ) )
			{
				char relative[ 512 ];
				char loadname[ 512 ];
				Q_snprintf( relative, sizeof( relative ), "%s/%s", path, fn );
				filesystem->RelativePathToFullPath( relative, "MOD", loadname, sizeof( loadname ) );
				// HL2SB: load diagnostics - "which Lua files actually ran".
				luasrc_LuaInfoMsgF( "[Lua]   %s\n", relative );
				if ( luasrc_dofile( L, loadname ) != 0 )
					++nFailed;
				lua_settop( L, nTop );
				++nLoaded;
			}
		}

		fn = g_pFullFileSystem->FindNext( fh );
	}
	g_pFullFileSystem->FindClose( fh );
	if ( nFailed > 0 )
		luasrc_LuaWarnMsgF( "[Lua] %s -> %d file(s), %d FAILED\n", path, nLoaded, nFailed );
	else
		Msg( "[Lua] %s -> %d file(s)\n", path, nLoaded );
}

//-----------------------------------------------------------------------------
// Purpose: HL2SB: collect the "*.lua" files under a folder, optionally walking
//          subfolders.  See luasrc_dofolder_sorted() for why this exists.
//-----------------------------------------------------------------------------
static void CollectLuaFiles ( const char *path, CUtlVector< CUtlString > &out, bool bRecurse )
{
	FileFindHandle_t fh;

	char searchPath[ 512 ];
	Q_snprintf( searchPath, sizeof( searchPath ), "%s/*", path );

	char const *fn = g_pFullFileSystem->FindFirstEx( searchPath, "MOD", &fh );
	while ( fn )
	{
		if ( fn[0] != '.' )
		{
			char relative[ 512 ];
			Q_snprintf( relative, sizeof( relative ), "%s/%s", path, fn );

			if ( g_pFullFileSystem->FindIsDirectory( fh ) )
			{
				if ( bRecurse )
				{
					CollectLuaFiles( relative, out, bRecurse );
				}
			}
			else
			{
				char ext[ 10 ];
				Q_ExtractFileExtension( fn, ext, sizeof( ext ) );
				if ( !Q_stricmp( ext, "lua" ) )
				{
					out.AddToTail( CUtlString( relative ) );
				}
			}
		}

		fn = g_pFullFileSystem->FindNext( fh );
	}
	g_pFullFileSystem->FindClose( fh );
}

static int __cdecl CompareLuaFileNames ( const CUtlString *pA, const CUtlString *pB )
{
	return Q_stricmp( pA->Get(), pB->Get() );
}

//-----------------------------------------------------------------------------
// Purpose: HL2SB: Garry's Mod's autorun scan.
//
//          GMod loads lua/autorun/*.lua and then lua/autorun/<realm>/** (it walks
//          into subfolders -- that is how lua/autorun/server/sensorbones/*.lua
//          ships), and it executes them in ALPHABETICAL order on every platform
//          ("Autorun lua files are sorted alphabetically (A-Z) on all OSes",
//          wiki: Lua_Loading_Order) because addons rely on that order.
//
//          luasrc_dofolder() above can do neither: it only matches "*.lua" in the
//          one folder and it executes in whatever order the file system hands the
//          entries over -- which also differs between Windows and Linux.  Autorun
//          therefore gets this loader instead.
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
// Purpose: load ONE named file out of lua/includes/.
//
//          This exists because GMod's engine calls its bootstrap files by name
//          -- lua/includes/init.lua -- instead of scanning the directory, and
//          the difference is not cosmetic.  A non-recursive scan of
//          lua/includes/ loads init.lua, then util.lua, then vgui_base.lua a
//          SECOND time, and that second pass re-runs all 54 lua/vgui controls:
//          derma.DefineControl() sees Controls[name] ~= nil, takes its reloading
//          branch, and every control dies in ReloadClass() ->
//          FindPanelsByClass() -> vgui.GetAll().  Result: 54 FAILED lines and
//          zero controls registered.  init.lua includes util.lua and
//          vgui_base.lua itself, so one named file is one pass.
//
//          Prints the same "[Lua]   <path>" line the folder loader does, so the
//          load order stays readable in ds_debug.log.
//-----------------------------------------------------------------------------
LUA_API int luasrc_dofile_includes (lua_State *L, const char *pszName)
{
	char relative[ MAX_PATH ];
	Q_snprintf( relative, sizeof( relative ), "%s/%s", LUA_PATH_INCLUDES, pszName );

	if ( !LuaFileExists( relative ) )
	{
		luasrc_LuaWarnMsgF( "[Lua] %s: not found -- GMod bootstrap NOT loaded\n", relative );
		return 1;
	}

	char fullpath[ MAX_PATH ];
	filesystem->RelativePathToFullPath( relative, "MOD", fullpath, sizeof( fullpath ) );

	Msg( "[Lua]   %s\n", relative );
	return luasrc_dofile( L, fullpath );
}

LUA_API void luasrc_dofolder_sorted (lua_State *L, const char *path, bool bRecurse)
{
	CUtlVector< CUtlString > files;
	CollectLuaFiles( path, files, bRecurse );

	if ( files.Count() > 0 )
	{
		files.Sort( CompareLuaFileNames );
	}

	int nFailed = 0;
	const int nTop = lua_gettop( L );
	for ( int i = 0; i < files.Count(); ++i )
	{
		char loadname[ 512 ];
		filesystem->RelativePathToFullPath( files[i].Get(), "MOD", loadname, sizeof( loadname ) );
		Msg( "[Lua]   %s\n", files[i].Get() );
		if ( luasrc_dofile( L, loadname ) != 0 )
			++nFailed;
		// HL2SB: a chunk that returns at top level would otherwise leave one
		// more value on the stack per file (AGENTS.md 5.4.2).
		lua_settop( L, nTop );
	}

	if ( nFailed > 0 )
		luasrc_LuaWarnMsgF( "[Lua] %s -> %d file(s), %d FAILED%s\n", path, files.Count(), nFailed, bRecurse ? " (recursive, A-Z)" : " (A-Z)" );
	else
		Msg( "[Lua] %s -> %d file(s)%s\n", path, files.Count(), bRecurse ? " (recursive, A-Z)" : " (A-Z)" );
}

/*
** HL2SB: error message handler that appends a traceback.  Experiment: Source does
** the same via Lua 5.4's luaL_traceback; on Lua 5.1 debug.traceback works as a
** message handler because handlers run before the stack is unwound.
** Without it an engine -> Lua dispatch that fails prints a bare message such as
** "attempt to index a number value" with no file and no line, which cannot be
** traced back to a script.
*/
static int luasrc_traceback (lua_State *L) {
  // HL2SB: build the traceback in C, NOT by calling debug.traceback.
  //
  // The previous version ran LUA from inside the error handler (lua_getglobal +
  // lua_call on debug.traceback), and an error handler is the one place that must
  // not need a healthy Lua stack: it runs while the stack is already in whatever
  // state produced the error.  The SCP-096 abort dump shows what that costs --
  // luaD_reallocstack sits directly under HL2SB_LuaPanic, i.e. the REPORT failed,
  // and all that survived was the original message ("attempt to call a nil
  // value") with no traceback and no file name.
  //
  // luaL_traceback is pure C: it walks the CallInfo chain and cannot raise.
  // Level 0 because this handler is not a CallInfo frame either -- 1 would skip
  // the topmost frame, which is the function that raised.
  luaL_traceback( L, L, lua_tostring( L, 1 ), 0 );
  return 1;
}

LUA_API int luasrc_pcall (lua_State *L, int nargs, int nresults, int errfunc) {
  bool bInjectedHandler = false;

  if (errfunc == 0) {
    /* Place the handler below the function and its arguments so lua_pcall sees
    ** it, then remove it again so the caller's stack shape is unchanged.
    */
    lua_pushcfunction(L, luasrc_traceback);
    lua_insert(L, -(nargs + 2));
    errfunc = lua_gettop(L) - nargs - 1;
    bInjectedHandler = true;
  }

  int iError = lua_pcall(L, nargs, nresults, errfunc);

  if (bInjectedHandler)
    lua_remove(L, errfunc);

  if (iError != 0) {
	luasrc_LuaErrorMsg( lua_tostring(L, -1) );
	lua_pop(L, 1);
  }
  return iError;
}

LUA_API void luasrc_print(lua_State *L, int narg) {
  lua_getglobal(L, "tostring");
  const char *s;
  lua_pushvalue(L, -1);  /* function to be called */
  lua_pushvalue(L, narg);   /* value to print */
  lua_call(L, 1, 1);
  s = lua_tostring(L, -1);  /* get result */
  Msg( " %d:\t%s\n", narg, s );
  lua_pop(L, 1);  /* pop result */
  lua_pop(L, 1);  /* pop function */
}

LUA_API void luasrc_dumpstack(lua_State *L) {
  int n = lua_gettop(L);  /* number of objects */
  int i;
  lua_getglobal(L, "tostring");
  for (i=1; i<=n; i++) {
	const char *s;
	lua_pushvalue(L, -1);  /* function to be called */
	lua_pushvalue(L, i);   /* value to print */
	lua_call(L, 1, 1);
	s = lua_tostring(L, -1);  /* get result */
	Msg( " %d:\t%s\n", i, s );
	lua_pop(L, 1);  /* pop result */
  }
  lua_pop(L, 1);  /* pop function */
}

/*
** HL2SB: a PROTECTED  t[key]  read -- see the declaration in luamanager.h.
**
** lua_getfield() from C is not a protected call.  A script table can answer
** through an __index metamethod, so "just reading a field" can RUN LUA, and an
** error raised there reaches lua_atpanic, which aborts the process with
**
**     attempt to call a nil value
**     stack traceback:            <- empty: the panic runs with no Lua frame left
**
** and no file and no line.  That is what every SCP-096 spawn did, through the
** ENT:Initialize() lookup in basescripted.cpp.
*/
static int luasrc_IndexHelper (lua_State *L)
{
	lua_gettable( L, 1 );		// [t][k] -> [t[k]]
	return 1;
}

LUA_API bool luasrc_PushScriptField (lua_State *L, int nTableIdx, const char *pszKey,
                                     bool *pbLookupErrored)
{
	if ( pbLookupErrored != NULL )
		*pbLookupErrored = false;

	if ( L == NULL || pszKey == NULL )
		return false;

	// The pushes below move the stack, so a relative index has to be resolved
	// first -- by the time lua_pushvalue() reads it, it would point elsewhere.
	if ( nTableIdx < 0 )
		nTableIdx = lua_gettop( L ) + 1 + nTableIdx;

	// Not a table at all (a stale reference yields whatever sits at that registry
	// index): answer nil rather than raising.  This is the old
	// "attempt to index a number value" abort, made harmless.
	if ( !lua_istable( L, nTableIdx ) )
	{
		lua_pushnil( L );
		return false;
	}

	lua_pushcfunction( L, luasrc_IndexHelper );		// [..][t][helper]
	lua_pushvalue( L, nTableIdx );					// [..][t][helper][t]
	lua_pushstring( L, pszKey );					// [..][t][helper][t][k]

	if ( luasrc_pcall( L, 2, 1, 0 ) != 0 )
	{
		// luasrc_pcall reported the error with a traceback and popped the message
		// itself, so the stack is empty here and the caller still gets its value.
		if ( pbLookupErrored != NULL )
			*pbLookupErrored = true;

		lua_pushnil( L );
		return false;
	}

	return lua_isfunction( L, -1 ) ? true : false;
}

/*
** HL2SB: script-visible globals GMod guarantees before any entity/weapon script
** runs, installed once per load pass (both helpers are idempotent).
**
**   GAMEMODE      GMod always has it; HL2SB loads weapons/entities BEFORE
**                 luasrc_LoadGamemode(), so a stock GMod weapon that reads it at
**                 file scope --
**                     if ( GAMEMODE.Name == "Trouble in Terrorist Town" ) then
**                 (weapon_nyangun.lua line 59 does exactly that) --
**                 died with "attempt to index a nil value (global 'GAMEMODE')"
**                 and the SWEP was never registered.  The real table lands in
**                 luasrc_SetGamemode() a moment later.
**
**   timer.Destroy GMod's timer library has both Remove() and Destroy(); this
**                 fork's lua/includes/modules/timer.lua only has Remove, so
**                 weapon_nyangun's KillSounds() (called from Holster/OnRemove)
**                 raised "attempt to call a nil value (field 'Destroy')" and
**                 aborted the rest of the method.
*/
static void luasrc_EnsureGamemodeStub (lua_State *L)
{
	lua_getglobal( L, "GAMEMODE" );
	if ( lua_istable( L, -1 ) )
	{
		lua_pop( L, 1 );
	}
	else
	{
		lua_pop( L, 1 );
		lua_newtable( L );
		lua_setglobal( L, "GAMEMODE" );
	}

	lua_getglobal( L, "timer" );
	if ( lua_istable( L, -1 ) )
	{
		lua_getfield( L, -1, "Destroy" );
		const bool bMissing = lua_isnil( L, -1 ) != 0;
		lua_pop( L, 1 );

		if ( bMissing )
		{
			lua_getfield( L, -1, "Remove" );
			lua_setfield( L, -2, "Destroy" );
		}
	}
	lua_pop( L, 1 );
}

/*
** HL2SB: is this a flat "<name>.lua" entry (as opposed to a directory)?
** GMod loads BOTH layouts and so must this loader.
*/
static bool luasrc_IsFlatLuaFile (const char *pszName, char *pszClassName, size_t nClassNameLen)
{
	const int nLen = Q_strlen( pszName );
	if ( nLen <= 4 || Q_stricmp( pszName + nLen - 4, ".lua" ) != 0 )
		return false;

	Q_strncpy( pszClassName, pszName, MIN( (size_t)nLen - 3, nClassNameLen ) );
	pszClassName[ MIN( (size_t)nLen - 4, nClassNameLen - 1 ) ] = '\0';
	Q_strlower( pszClassName );

	return true;
}

/*
** HL2SB: remember WHICH Lua file a scripted class was loaded from.
**
** This is for the spawn menu's source sidebar (game/client/menu/sm_menu_list.cpp):
** it has to name the addon an entry comes from, and this loader is the only
** place that knows it.  The MOD-relative filename alone is NOT enough -- every
** mounted addon's script resolves to the same relative name ("lua/weapons/
** weapon_nyangun.lua") because mountaddons.cpp mounts each addon folder as its
** own MOD search path -- so the addon folder is folded back in from the file's
** absolute path: "addons/nyangun/lua/weapons/weapon_nyangun.lua".  A file that
** is not inside an addon keeps its plain relative name.
**
** The pair {className -> file} is exactly what the loaders already print with
** their "[Lua] weapon/entity '<x>' <- <path>" line, so this adds no new
** information, only a readable copy of it for the client menu.
*/
static CUtlDict< CUtlString, unsigned short > s_LuaClassScripts;

static void luasrc_NoteClassScript (const char *pszClassName, const char *pszFilename, const char *pszFullPath)
{
	if ( !pszClassName || !pszClassName[0] || !pszFilename || !pszFilename[0] )
		return;

	// Recorded once per process: the loader runs again on every map load and the
	// same class always comes from the same file, so a second insert would only
	// leak the strdup'd key.
	if ( s_LuaClassScripts.Find( pszClassName ) != s_LuaClassScripts.InvalidIndex() )
		return;

	char szKey[ MAX_PATH ];
	Q_strncpy( szKey, pszFilename, sizeof( szKey ) );
	szKey[ sizeof( szKey ) - 1 ] = '\0';

	// An already-qualified name needs no work.
	if ( Q_strnicmp( szKey, "addons/", 7 ) != 0 && pszFullPath && pszFullPath[0] )
	{
		char szFull[ MAX_PATH ];
		Q_strncpy( szFull, pszFullPath, sizeof( szFull ) );
		szFull[ sizeof( szFull ) - 1 ] = '\0';

		for ( char *p = szFull; *p; ++p )
		{
			if ( *p == '\\' )
				*p = '/';
		}

		char szLower[ MAX_PATH ];
		Q_strncpy( szLower, szFull, sizeof( szLower ) );
		szLower[ sizeof( szLower ) - 1 ] = '\0';
		Q_strlower( szLower );

		// The LAST "/addons/" is the mod's own addons folder -- an engine path
		// could contain such a component higher up.
		const char *pFound = NULL;
		for ( const char *p = szLower; ( p = Q_stristr( p, "/addons/" ) ) != NULL; ++p )
			pFound = p;

		if ( pFound )
		{
			// Take the folder name from the ORIGINAL path so its case survives
			// (the lowercased copy was only used to find the marker).
			const char *pFolder = szFull + ( pFound - szLower ) + 1 + 7;
			const char *pSlash = strchr( pFolder, '/' );
			int nFolderLen = pSlash ? (int)( pSlash - pFolder ) : Q_strlen( pFolder );

			if ( nFolderLen > (int)sizeof( szKey ) - 10 )
				nFolderLen = (int)sizeof( szKey ) - 10;

			if ( nFolderLen > 0 )
			{
				char szFolder[ MAX_PATH ];
				Q_memcpy( szFolder, pFolder, nFolderLen );
				szFolder[ nFolderLen ] = '\0';
				Q_snprintf( szKey, sizeof( szKey ), "addons/%s/%s", szFolder, pszFilename );
			}
		}
	}

	s_LuaClassScripts.Insert( pszClassName, CUtlString( szKey ) );
}

const char *luasrc_GetClassScriptFile (const char *pszClassName)
{
	if ( !pszClassName || !pszClassName[0] )
		return "";

	unsigned short i = s_LuaClassScripts.Find( pszClassName );
	if ( i == s_LuaClassScripts.InvalidIndex() )
		return "";

	return (const char *)s_LuaClassScripts[i];
}

/*
** HL2SB: one scripted entity, from whichever layout found it.
** GMod treats "lua/entities/<name>.lua" and "lua/entities/<name>/shared.lua" as
** the same entity class, so both paths funnel through here.
*/
static void luasrc_LoadOneEntity (const char *filename, const char *className)
{
	char fullpath[ MAX_PATH ] = { 0 };

	if ( !filesystem->FileExists( filename, "MOD" ) )
		return;

	filesystem->RelativePathToFullPath( filename, "MOD", fullpath, sizeof( fullpath ) );
	luasrc_LuaInfoMsgF( "[Lua] entity '%s' <- %s\n", className, fullpath );

	// HL2SB: this is the one place that knows which addon (or the tree's own
	// lua/) a scripted class came from -- see luasrc_NoteClassScript.
	luasrc_NoteClassScript( className, filename, fullpath );

	lua_newtable( L );
	char entDir[ MAX_PATH ];
	Q_snprintf( entDir, sizeof( entDir ), "entities/%s", className );
	lua_pushstring( L, entDir );
	lua_setfield( L, -2, "__folder" );
	lua_pushstring( L, LUA_BASE_ENTITY_CLASS );
	lua_setfield( L, -2, "__base" );
	lua_pushstring( L, LUA_BASE_ENTITY_FACTORY );
	lua_setfield( L, -2, "__factory" );
	lua_setglobal( L, "ENT" );

	if ( luasrc_dofile( L, fullpath ) != 0 )
	{
		// HL2SB: a failed entity script used to fail SILENTLY here -- no factory,
		// no registration, and the addon only saw "method is nil" at spawn time.
		luasrc_LuaErrorMsgF( "[HL2SB] entity '%s': script FAILED, no Lua class or factory registered (%s)\n",
			className, fullpath );
		lua_pushnil( L );
		lua_setglobal( L, "ENT" );
		return;
	}

	// The Team Sandbox `entity` module is what the engine's own
	// CBaseScripted::LoadScriptedEntity() reads back -- without this registration
	// the factory exists but the instance has no Lua table.
	lua_getglobal( L, "entity" );
	if ( lua_istable( L, -1 ) )
	{
		lua_getfield( L, -1, "register" );
		if ( lua_isfunction( L, -1 ) )
		{
			lua_remove( L, -2 );
			lua_getglobal( L, "ENT" );
			lua_pushstring( L, className );
			// HL2SB: bReload = true.  entity.register skips when a class of the
			// same name is already registered; a fresh full load (init.lua after
			// shared.lua, or a console reload) must always overwrite, otherwise a
			// partial registration from any earlier pass freezes the class
			// without its ENT methods ("attempt to call a nil value (method
			// 'SetBlockID')" on minecraft_block).
			//
			// ⚠️ nargs here MUST match the pushed arguments: the first version of
			// this change raised luasrc_pcall's nargs to 3 without pushing the
			// third value, so the traceback handler was lua_inserted at an
			// out-of-stack index and every entity load ended in an unprotected
			// Lua panic ("attempt to call a nil value", empty traceback) right
			// after the "[Lua] entity 'cod-c4' <-" line -- a deterministic crash
			// at every map load.
			lua_pushboolean( L, 1 );   // bReload: the fresh load always wins
			luasrc_pcall( L, 3, 0, 0 );
		}
		else
		{
			lua_pop( L, 2 );
		}
	}
	else
	{
		lua_pop( L, 1 );
	}

	// GMod's own registry (scripted_ents.Get / GetSpawnable / baseclass.Set).
	// GMod's engine calls this for every lua/entities/* file, and framework code
	// (duplicator.Allow, baseclass.Set) reads it back.  Failure is not fatal:
	// the engine factory below is what actually spawns the entity.
	lua_getglobal( L, "scripted_ents" );
	if ( lua_istable( L, -1 ) )
	{
		lua_getfield( L, -1, "Register" );
		if ( lua_isfunction( L, -1 ) )
		{
			lua_remove( L, -2 );
			lua_getglobal( L, "ENT" );

			// HL2SB: scripted_ents.Register() warns "has an invalid base entity!"
			// for anything that has neither Base nor one of GMod's Type keys
			// (anim/point/brush/filter) -- which is every Team Sandbox style script
			// here, because those carry __base/__factory instead (set above).
			//
			// The warning itself is noise, but what it implies is not: with no base
			// the entity table inherits nothing, so baseclass.Set() hands out a
			// table without this fork's scripted-entity methods.  Point Base at
			// __base so the registry sees a real base; a script that declared its
			// own Base (or Type) is left exactly as it was.
			lua_getfield( L, -1, "Base" );
			lua_getfield( L, -2, "Type" );
			const bool bHasBase = !lua_isnil( L, -2 );
			const bool bHasType = !lua_isnil( L, -1 );
			lua_pop( L, 2 );

			if ( !bHasBase && !bHasType )
			{
				lua_getfield( L, -1, "__base" );
				if ( lua_isstring( L, -1 ) )
					lua_setfield( L, -2, "Base" );   // pops __base
				else
					lua_pop( L, 1 );
			}

			lua_pushstring( L, className );
			luasrc_pcall( L, 2, 0, 0 );
		}
		else
		{
			lua_pop( L, 2 );
		}
	}
	else
	{
		lua_pop( L, 1 );
	}

	// Register the engine entity factory when the script asked for one.
	lua_getglobal( L, "ENT" );
	if ( lua_istable( L, -1 ) )
	{
		// HL2SB GMod compat: GMod nextbot addons do NOT name this fork's
		// __factory - they say, exactly as the wiki documents,
		//
		//     ENT.Base = "base_nextbot"      (and base_nextbot sets Type = "nextbot")
		//
		// and the base chain is only merged into an INSTANCE's table later, by
		// scripted_ents.Get()/baseclass.Set() (scripted_ents.lua:160-194 - it
		// walks Base with TableInherit).  This code reads the class table at LOAD
		// time, so without the test below every addon nextbot (SCP-096 included)
		// was registered as a plain CBaseAnimating scripted entity: no INextBot,
		// no locomotion, no BehaveUpdate - a bot that just stands there.
		//
		// ⚠️ This is also why ENT.Base matters here and not ENT.Type alone:
		// the addon only sets Base, and Type comes from OUR base_nextbot script.
		bool bIsNextBot = false;

		lua_getfield( L, -1, "Base" );
		if ( lua_isstring( L, -1 ) && Q_stricmp( lua_tostring( L, -1 ), "base_nextbot" ) == 0 )
			bIsNextBot = true;
		lua_pop( L, 1 );

		if ( !bIsNextBot )
		{
			lua_getfield( L, -1, "Type" );
			if ( lua_isstring( L, -1 ) && Q_stricmp( lua_tostring( L, -1 ), "nextbot" ) == 0 )
				bIsNextBot = true;
			lua_pop( L, 1 );
		}

		if ( bIsNextBot )
		{
#ifndef CLIENT_DLL
			// Server only: the client half of a nextbot is C_NextBotCombatCharacter,
			// reached through DT_NextBot, not through a per-class client entry.
			RegisterLuaNextBot( className );
#endif
			lua_pop( L, 1 );
			lua_pushnil( L );
			lua_setglobal( L, "ENT" );
			return;
		}

		lua_getfield( L, -1, "__factory" );
		if ( lua_isstring( L, -1 ) )
		{
			const char *pszClassname = lua_tostring( L, -1 );
			if (Q_strcmp(pszClassname, "CBaseAnimating") == 0)
				RegisterScriptedEntity( className );
#ifndef CLIENT_DLL
			else if (Q_strcmp(pszClassname, "CBaseTrigger") == 0)
				RegisterScriptedTrigger( className );
#endif
		}
		lua_pop( L, 2 );
	}
	else
	{
		lua_pop( L, 1 );
	}

	lua_pushnil( L );
	lua_setglobal( L, "ENT" );
}

void luasrc_LoadEntities (const char *path)
{
	FileFindHandle_t fh;

	if ( !path )
	{
		path = "";
	}

	luasrc_EnsureGamemodeStub( L );

	char root[ MAX_PATH ] = { 0 };

	char filename[ MAX_PATH ] = { 0 };
	char className[ 255 ] = { 0 };

	Q_snprintf( root, sizeof( root ), "%s" LUA_PATH_ENTITIES "/*", path );

	char const *fn = g_pFullFileSystem->FindFirstEx( root, "MOD", &fh );
	while ( fn )
	{
		Q_strcpy( className, fn );
		Q_strlower( className );
		if ( fn[0] != '.' )
		{
			if ( g_pFullFileSystem->FindIsDirectory( fh ) )
			{
#ifdef CLIENT_DLL
				Q_snprintf( filename, sizeof( filename ), "%s" LUA_PATH_ENTITIES "/%s/cl_init.lua", path, className );
#else
				Q_snprintf( filename, sizeof( filename ), "%s" LUA_PATH_ENTITIES "/%s/init.lua", path, className );
#endif
				// GMod compatibility: scripted entities that ship only
				// shared.lua must still load.
				if ( !filesystem->FileExists( filename, "MOD" ) )
				{
					Q_snprintf( filename, sizeof( filename ), "%s" LUA_PATH_ENTITIES "/%s/shared.lua", path, className );
				}
				luasrc_LoadOneEntity( filename, className );
			}
			else if ( luasrc_IsFlatLuaFile( fn, className, sizeof( className ) ) )
			{
				// GMod: "lua/entities/<name>.lua" is a complete scripted entity,
				// registered under the file's basename.
				Q_snprintf( filename, sizeof( filename ), "%s" LUA_PATH_ENTITIES "/%s", path, fn );
				luasrc_LoadOneEntity( filename, className );
			}
		}

		fn = g_pFullFileSystem->FindNext( fh );
	}
	g_pFullFileSystem->FindClose( fh );
}

/*
** HL2SB: one SWEP, from whichever layout found it.
**
** GMod treats "lua/weapons/<name>.lua" and "lua/weapons/<name>/shared.lua" as the
** same weapon, so both paths funnel through here and a flat file behaves exactly
** like <name>/shared.lua -- same seed, same __folder/__base, same
** weapon.register() call, same log line.
*/
static void luasrc_LoadOneWeapon (const char *filename, const char *className)
{
	char fullpath[ MAX_PATH ] = { 0 };

	if ( !filesystem->FileExists( filename, "MOD" ) )
		return;

	filesystem->RelativePathToFullPath( filename, "MOD", fullpath, sizeof( fullpath ) );

	// HL2SB: say which script a weapon was built from, and from which file on
	// disk.  The loader used to be silent here, so "did an addon's SWEP actually
	// get picked up?" could only be answered by walking the file system by hand --
	// and GMod compatibility regressions are exactly the kind of thing that has
	// to be checkable from the log.
	Msg( "[Lua] weapon '%s' <- %s\n", className, fullpath );

	// HL2SB: this is the one place that knows which addon (or the tree's own
	// lua/) a scripted class came from -- see luasrc_NoteClassScript.
	luasrc_NoteClassScript( className, filename, fullpath );

	// GMod semantics: the engine seeds every SWEP with a deep copy of the base
	// weapon table before running the script, so stock scripts can assign fields
	// into SWEP.Primary/SWEP.Secondary at the top level (`SWEP.Primary.Sound = ...`)
	// without creating those tables themselves.
	//
	// The table key is the weapons/ DIRECTORY name, which for the GMod-style base
	// is "weapon_base"; fall back to the engine's own scripted base for older
	// layouts. Primary/Secondary are guaranteed to be tables regardless of what
	// the seed resolved, because a base whose data lives in flat keys
	// (clip_size, ...) has no Primary table of its own.
	luasrc_dostring( L,
		"local __b = weapon.get( \"weapon_base\" )"
		" or weapon.get( \"" LUA_BASE_WEAPON "\" ) or {};"
		"SWEP = table.copy( __b );"
		"if type( SWEP ) ~= \"table\" then SWEP = {} end;"
		"if type( SWEP.Primary ) ~= \"table\" then SWEP.Primary = {} end;"
		"if type( SWEP.Secondary ) ~= \"table\" then SWEP.Secondary = {} end" );
	lua_getglobal( L, "SWEP" );
	if ( !lua_istable( L, -1 ) )
	{
		// Paranoia: the seed should always leave a table here.
		lua_pop( L, 1 );
		lua_newtable( L );
		lua_newtable( L );
		lua_setfield( L, -2, "Primary" );
		lua_newtable( L );
		lua_setfield( L, -2, "Secondary" );
	}

	char entDir[ MAX_PATH ];
	Q_snprintf( entDir, sizeof( entDir ), "weapons/%s", className );
	lua_pushstring( L, entDir );
	lua_setfield( L, -2, "__folder" );
	lua_pushstring( L, LUA_BASE_WEAPON );
	lua_setfield( L, -2, "__base" );
	lua_setglobal( L, "SWEP" );
	if ( luasrc_dofile( L, fullpath ) == 0 )
	{
		lua_getglobal( L, "weapon" );
		if ( lua_istable( L, -1 ) )
		{
			lua_getfield( L, -1, "register" );
			if ( lua_isfunction( L, -1 ) )
			{
				lua_remove( L, -2 );
				lua_getglobal( L, "SWEP" );
				lua_pushstring( L, className );
				luasrc_pcall( L, 2, 0, 0 );
				RegisterScriptedWeapon( className );
			}
			else
			{
				lua_pop( L, 2 );
			}
		}
		else
		{
			lua_pop( L, 1 );
		}
	}
	lua_pushnil( L );
	lua_setglobal( L, "SWEP" );
}

void luasrc_LoadWeapons (const char *path)
{
	FileFindHandle_t fh;

	if ( !path )
	{
		path = "";
	}

	luasrc_EnsureGamemodeStub( L );

	char root[ MAX_PATH ] = { 0 };

	char filename[ MAX_PATH ] = { 0 };
	char className[ MAX_WEAPON_STRING ] = { 0 };

	Q_snprintf( root, sizeof( root ), "%s" LUA_PATH_WEAPONS "/*", path );

	char const *fn = g_pFullFileSystem->FindFirstEx( root, "MOD", &fh );
	while ( fn )
	{
		Q_strcpy( className, fn );
		Q_strlower( className );
		if ( fn[0] != '.' )
		{
			if ( g_pFullFileSystem->FindIsDirectory( fh ) )
			{
#ifdef CLIENT_DLL
				Q_snprintf( filename, sizeof( filename ), "%s" LUA_PATH_WEAPONS "/%s/cl_init.lua", path, className );
#else
				Q_snprintf( filename, sizeof( filename ), "%s" LUA_PATH_WEAPONS "/%s/init.lua", path, className );
#endif
				// GMod compatibility: SWEPs that ship only shared.lua (no
				// init.lua / cl_init.lua) must still load.
				if ( !filesystem->FileExists( filename, "MOD" ) )
				{
					Q_snprintf( filename, sizeof( filename ), "%s" LUA_PATH_WEAPONS "/%s/shared.lua", path, className );
				}
				luasrc_LoadOneWeapon( filename, className );
			}
			else if ( luasrc_IsFlatLuaFile( fn, className, sizeof( className ) ) )
			{
				// GMod: "lua/weapons/<name>.lua" is a complete SWEP, registered
				// under the file's basename.  HL2SB used to skip these entirely,
				// so a flat SWEP did nothing at all and said nothing about it.
				Q_snprintf( filename, sizeof( filename ), "%s" LUA_PATH_WEAPONS "/%s", path, fn );
				luasrc_LoadOneWeapon( filename, className );
			}
		}

		fn = g_pFullFileSystem->FindNext( fh );
	}
	g_pFullFileSystem->FindClose( fh );
}

/*
** HL2SB: GMod's lua/effects/<name>.lua loader.
**
** Effects are CLIENT ONLY (GMod's Lua Loading Order lists effects/ in the client
** column, and a Lua effect is nothing but a table of Init/Think/Render methods
** run by the client's renderer).
**
** This was written long ago and left commented out because "it might load
** something twice" -- it cannot: nothing else in this engine ever walks
** LUA_PATH_EFFECTS (the folder passes cover includes/, game/ and autorun/ only),
** so a file is reached exactly once per call and the call sites are the level
** init plus the per-gamemode content pass, exactly like weapons/entities.
**
** Each file is run with a fresh global EFFECT table (GMod's contract) which is
** then handed to effects.Register(EFFECT, name) and kept in the engine-side
** template registry the effect spawner reads.
*/
void luasrc_LoadEffects (const char *path)
{
#ifdef CLIENT_DLL
	FileFindHandle_t fh;

	if ( !path )
	{
		path = "";
	}

	char root[ MAX_PATH ] = { 0 };
	char filename[ MAX_PATH ] = { 0 };
	char fullpath[ MAX_PATH ] = { 0 };
	char className[ 255 ] = { 0 };

	Q_snprintf( root, sizeof( root ), "%s" LUA_PATH_EFFECTS "/*", path );

	char const *fn = g_pFullFileSystem->FindFirstEx( root, "MOD", &fh );
	while ( fn )
	{
		if ( fn[0] != '.' && !g_pFullFileSystem->FindIsDirectory( fh )
		     && luasrc_IsFlatLuaFile( fn, className, sizeof( className ) ) )
		{
			Q_snprintf( filename, sizeof( filename ), "%s" LUA_PATH_EFFECTS "/%s", path, fn );
			if ( filesystem->FileExists( filename, "MOD" ) )
			{
				filesystem->RelativePathToFullPath( filename, "MOD", fullpath, sizeof( fullpath ) );
				luasrc_LuaInfoMsgF( "[Lua] effect '%s' <- %s\n", className, fullpath );

				lua_newtable( L );
				char effDir[ MAX_PATH ];
				Q_snprintf( effDir, sizeof( effDir ), "effects/%s", className );
				lua_pushstring( L, effDir );
				lua_setfield( L, -2, "__folder" );
				lua_setglobal( L, "EFFECT" );

				if ( luasrc_dofile( L, fullpath ) == 0 )
				{
					// effects.Register(EFFECT, name) -- GMod's own library.
					lua_getglobal( L, "effects" );
					if ( lua_istable( L, -1 ) )
					{
						lua_getfield( L, -1, "Register" );
						if ( lua_isfunction( L, -1 ) )
						{
							lua_remove( L, -2 );
							lua_getglobal( L, "EFFECT" );
							lua_pushstring( L, className );
							luasrc_pcall( L, 2, 0, 0 );
						}
						else
						{
							lua_pop( L, 2 );
						}
					}
					else
					{
						lua_pop( L, 1 );
					}

					// Keep the template where the engine's effect spawner can
					// find it.  It cannot use effects.Create(): that function
					// ends in table.Merge( NewEffect, EffectList["base"] ), and
					// this engine's table.Merge() raises "bad argument #1 to
					// 'for iterator'" on a nil source because no "base" effect is
					// ever registered here.  Copying the template is what
					// effects.Create() is for anyway.
					lua_getglobal( L, "__hl2sb_lua_effects" );
					if ( !lua_istable( L, -1 ) )
					{
						lua_pop( L, 1 );
						lua_newtable( L );
						lua_pushvalue( L, -1 );
						lua_setglobal( L, "__hl2sb_lua_effects" );
					}
					int nRegistry = lua_gettop( L );
					lua_getglobal( L, "EFFECT" );
					if ( lua_istable( L, -1 ) )
					{
						lua_pushstring( L, className );
						lua_pushvalue( L, -2 );
						lua_rawset( L, nRegistry );
					}
					lua_pop( L, 2 );
				}

				lua_pushnil( L );
				lua_setglobal( L, "EFFECT" );
			}
		}

		fn = g_pFullFileSystem->FindNext( fh );
	}
	g_pFullFileSystem->FindClose( fh );
#endif // CLIENT_DLL
}

bool luasrc_LoadGamemode (const char *gamemode) {
  lua_newtable(L);
  lua_pushstring(L, "__folder");
  char gamemodepath[MAX_PATH];
  Q_snprintf( gamemodepath, sizeof( gamemodepath ), "gamemodes/%s", gamemode );
  lua_pushstring(L, gamemodepath);
  lua_settable(L, -3);
  lua_setglobal(L, "GM");
  char filename[MAX_PATH];
  char fullpath[MAX_PATH];
#ifdef CLIENT_DLL
  Q_snprintf( filename, sizeof( filename ), "%s/gamemode/cl_init.lua", gamemodepath );
#else
  Q_snprintf( filename, sizeof( filename ), "%s/gamemode/init.lua", gamemodepath );
#endif
  if ( filesystem->FileExists( filename, "MOD" ) )
  {
    filesystem->RelativePathToFullPath( filename, "MOD", fullpath, sizeof( fullpath ) );
	if (luasrc_dofile(L, fullpath) == 0) {
	  lua_getglobal(L, "gamemode");
	  lua_getfield(L, -1, "register");
	  lua_remove(L, -2);
	  lua_getglobal(L, "GM");
	  lua_pushstring(L, gamemode);
	  lua_getfield(L, -2, "__base");
	  if (lua_isnoneornil(L, -1) && Q_strcmp(gamemode, LUA_BASE_GAMEMODE) != 0) {
	    lua_pop(L, 1);
		lua_pushstring(L, LUA_BASE_GAMEMODE);
	  }
	  luasrc_pcall(L, 3, 0, 0);
	  lua_pushnil(L);
	  lua_setglobal(L, "GM");
	  return true;
	}
	else
	{
	  lua_pushnil(L);
	  lua_setglobal(L, "GM");
	  Warning( "ERROR: Attempted to load an invalid gamemode!\n" );
	  return false;
	}
  }
  else
  {
    lua_pushnil(L);
	lua_setglobal(L, "GM");
	Warning( "ERROR: Attempted to load an invalid gamemode!\n" );
    return false;
  }
}

bool luasrc_SetGamemode (const char *gamemode) {
  lua_getglobal(L, "gamemode");
  if (lua_istable(L, -1)) {
    lua_getfield(L, -1, "get");
	if (lua_isfunction(L, -1)) {
	  lua_remove(L, -2);
	  lua_pushstring(L, gamemode);
	  luasrc_pcall(L, 1, 1, 0);
	  lua_setglobal(L, "_GAMEMODE");
	  // HL2SB: GMod scripts refer to the gamemode table as GAMEMODE.
	  lua_getglobal(L, "_GAMEMODE");
	  lua_setglobal(L, "GAMEMODE");
	  // HL2SB: the base gamemode's content directory is shared by every
	  // gamemode (GMod keeps its base entities in gamemodes/base/entities), so
	  // mount and scan it first - the active gamemode still wins on collisions.
	  Q_snprintf( baseContentSearchPath, sizeof( baseContentSearchPath ), "gamemodes/%s/content", LUA_BASE_GAMEMODE );
	  filesystem->AddSearchPath( baseContentSearchPath, "MOD" );
	  {
	    char baseLoadPath[MAX_PATH];
	    Q_snprintf( baseLoadPath, sizeof( baseLoadPath ), "%s/", baseContentSearchPath );
	    luasrc_LoadWeapons( baseLoadPath );
	    luasrc_LoadEntities( baseLoadPath );
	  }

	  Q_snprintf( contentSearchPath, sizeof( contentSearchPath ), "gamemodes/%s/content", gamemode );
	  filesystem->AddSearchPath( contentSearchPath, "MOD" );
	  char loadPath[MAX_PATH];
	  Q_snprintf( loadPath, sizeof( loadPath ), "%s/", contentSearchPath );
	  luasrc_LoadWeapons( loadPath );
	  luasrc_LoadEntities( loadPath );
	  luasrc_LoadEffects( loadPath );
	  BEGIN_LUA_CALL_HOOK("Initialize");
	  END_LUA_CALL_HOOK(0,0);

	  // HL2SB: the gamemode Lua has now registered its ammo types
	  // (gamemodes/<name>/gamemode/ammo.lua, pulled in by shared.lua),
	  // so push them into the engine every map load.
	  luasrc_ApplyAmmoTypes( GetAmmoDef() );

	  return true;
	}
	else
	{
	  lua_pop(L, 2);
	  Warning( "ERROR: Failed to set gamemode!\n" );
	  return false;
	}
  }
  else
  {
    lua_pop(L, 1);
	Warning( "ERROR: Failed to load gamemode module!\n" );
	return false;
  }
}

#ifdef LUA_SDK
#ifdef CLIENT_DLL
	CON_COMMAND( lua_dostring_cl, "Run a Lua string" )
	{
		if ( !g_bLuaInitialized )
			return;

		if ( args.ArgC() == 1 )
		{
			Msg( "Usage: lua_dostring_cl <string>\n" );
			return;
		}

		int status = luasrc_dostring( L, args.ArgS() );
		if (status == 0 && lua_gettop(L) > 0) {  /* any result to print? */
		  lua_getglobal(L, "print");
		  lua_insert(L, 1);
		  if (lua_pcall(L, lua_gettop(L)-1, 0, 0) != 0)
			Warning("%s", lua_pushfstring(L,
							  "error calling " LUA_QL("print") " (%s)",
							  lua_tostring(L, -1)));
		}
		lua_settop(L, 0);  /* clear stack */
	}
#else
	CON_COMMAND( lua_dostring, "Run a Lua string" )
	{
		if ( !g_bLuaInitialized )
			return;

		if ( !UTIL_IsCommandIssuedByServerAdmin() )
			return;

		if ( args.ArgC() == 1 )
		{
			Msg( "Usage: lua_dostring <string>\n" );
			return;
		}

		int status = luasrc_dostring( L, args.ArgS() );
		if (status == 0 && lua_gettop(L) > 0) {  /* any result to print? */
		  lua_getglobal(L, "print");
		  lua_insert(L, 1);
		  if (lua_pcall(L, lua_gettop(L)-1, 0, 0) != 0)
			Warning("%s", lua_pushfstring(L,
							  "error calling " LUA_QL("print") " (%s)",
							  lua_tostring(L, -1)));
		}
		lua_settop(L, 0);  /* clear stack */
	}
#endif

//-----------------------------------------------------------------------------
// HL2SB: GMod-style script reload.
//
// GMod's lua_reloadents re-runs the entity scripts on the LIVE state; this fork
// re-runs its entity/weapon loaders on every map load but had no way to do it
// without leaving the map -- iterating on an addon therefore meant a full map
// reload each time.
//
// This runs the same loader sequence the map-load path runs (gameinterface.cpp
// for the server, cdll_client_int.cpp for the client), without the state reset:
// re-registering hooks/concommands overwrites by name (what those registries
// expect), and entity.register( ..., true ) overwrites the class table -- that
// bReload argument exists for exactly this.
//-----------------------------------------------------------------------------
static void HL2SB_LuaReloadCmd( const CCommand &args )
{
	if ( !g_bLuaInitialized )
	{
		Warning( "[HL2SB] lua_reloadents: Lua is not initialized yet\n" );
		return;
	}

	luasrc_LuaInfoMsgF( "[HL2SB] lua_reloadents: reloading scripted entities/weapons...\n" );

	luasrc_LoadWeapons();
	luasrc_LoadEntities();
#ifdef CLIENT_DLL
	luasrc_LoadEffects();
#endif

	luasrc_LuaInfoMsgF( "[HL2SB] lua_reloadents: done\n" );
}

// GMod's command name, plus the fork's own alias for discoverability.
static ConCommand lua_reloadents_cmd( "lua_reloadents", HL2SB_LuaReloadCmd,
	"Reload all scripted entities (and weapons/effects) on the live state." );
static ConCommand hl2sb_lua_reload_cmd( "hl2sb_lua_reload", HL2SB_LuaReloadCmd,
	"Reload all scripted entities (and weapons/effects) on the live state." );

static int DoFileCompletion( const char *partial, char commands[ COMMAND_COMPLETION_MAXITEMS ][ COMMAND_COMPLETION_ITEM_LENGTH ] )
{
	int current = 0;

#ifdef CLIENT_DLL
	const char *cmdname = "lua_dofile_cl";
#else
	const char *cmdname = "lua_dofile";
#endif
	char *substring = NULL;
	int substringLen = 0;
	if ( Q_strstr( partial, cmdname ) && strlen(partial) > strlen(cmdname) + 1 )
	{
		substring = (char *)partial + strlen( cmdname ) + 1;
		substringLen = strlen(substring);
	}
	
	FileFindHandle_t fh;

	char WildCard[ MAX_PATH ] = { 0 };
	if ( substring == NULL )
		substring = "";
	Q_snprintf( WildCard, sizeof( WildCard ), LUA_ROOT "/%s*", substring );
	Q_FixSlashes( WildCard );
	char const *fn = g_pFullFileSystem->FindFirstEx( WildCard, "MOD", &fh );
	while ( fn && current < COMMAND_COMPLETION_MAXITEMS )
	{
		if ( fn[0] != '.' )
		{
			char filename[ MAX_PATH ] = { 0 };
			Q_snprintf( filename, sizeof( filename ), LUA_ROOT "/%s/%s", substring, fn );
			Q_FixSlashes( filename );
			if ( filesystem->FileExists( filename, "MOD" ) )
			{
				Q_snprintf( commands[ current ], sizeof( commands[ current ] ), "%s %s%s", cmdname, substring, fn );
				current++;
			}
		}

		fn = g_pFullFileSystem->FindNext( fh );
	}
	g_pFullFileSystem->FindClose( fh );

	return current;
}

#ifdef CLIENT_DLL
	CON_COMMAND_F_COMPLETION( lua_dofile_cl, "Load and run a Lua file", 0, DoFileCompletion )
	{
		if ( !g_bLuaInitialized )
			return;

		if ( args.ArgC() == 1 )
		{
			Msg( "Usage: lua_dofile_cl <filename>\n" );
			return;
		}

		char fullpath[ 512 ] = { 0 };
		char filename[ 256 ] = { 0 };
		Q_snprintf( filename, sizeof( filename ), LUA_ROOT "/%s", args.ArgS() );
		Q_strlower( filename );
		Q_FixSlashes( filename );
		if ( filesystem->FileExists( filename, "MOD" ) )
		{
			filesystem->RelativePathToFullPath( filename, "MOD", fullpath, sizeof( fullpath ) );
		}
		else
		{
			Q_snprintf( fullpath, sizeof( fullpath ), "%s/" LUA_ROOT "/%s", engine->GetGameDirectory(), args.ArgS() );
			Q_strlower( fullpath );
			Q_FixSlashes( fullpath );
		}

		if ( Q_strstr( fullpath, ".." ) )
		{
			return;
		}
		Msg( "Running file %s...\n", args.ArgS() );
		luasrc_dofile( L, fullpath );
	}

	/*
	** HL2SB: GMod's lua_run.  The only way to evaluate Lua in HL2SB used to be
	** lua_dofile_cl, which needs the code written to a file first, so there was no
	** way to poke at the Lua state from the console.  GMod has lua_run (server) and
	** lua_run_cl (client); they are separated by realm for the same reason
	** lua_dofile and lua_dofile_cl are -- both DLLs are loaded in a listen server
	** and a bare lua_run would be registered twice.
	*/
	CON_COMMAND( lua_run_cl, "Run a Lua string (client)" )
	{
		if ( !g_bLuaInitialized )
		{
			// HL2SB: the Lua state is created per level (CHLClient::LevelInitPreEntity),
			// so in the main menu there is nothing to run against -- say so instead of
			// returning silently, which looks like the command did not exist.
			Msg( "lua_run_cl: Lua is not initialized yet (enter a map first)\n" );
			return;
		}

		if ( args.ArgC() == 1 )
		{
			Msg( "Usage: lua_run_cl <lua code>\n" );
			return;
		}

		luasrc_dostring( L, args.ArgS() );
	}
#else
	CON_COMMAND_F_COMPLETION( lua_dofile, "Load and run a Lua file", 0, DoFileCompletion )
	{
		if ( !g_bLuaInitialized )
			return;

		if ( !UTIL_IsCommandIssuedByServerAdmin() )
			return;

		if ( args.ArgC() == 1 )
		{
			Msg( "Usage: lua_dofile <filename>\n" );
			return;
		}

		char fullpath[ 512 ] = { 0 };
		char filename[ 256 ] = { 0 };
		Q_snprintf( filename, sizeof( filename ), LUA_ROOT "lua/%s", args.ArgS() );
		Q_strlower( filename );
		Q_FixSlashes( filename );
		if ( filesystem->FileExists( filename, "MOD" ) )
		{
			filesystem->RelativePathToFullPath( filename, "MOD", fullpath, sizeof( fullpath ) );
		}
		else
		{
			// filename is local to game dir for Steam, so we need to prepend game dir for regular file load
			char gamePath[256];
			engine->GetGameDir( gamePath, 256 );
			Q_StripTrailingSlash( gamePath );
			Q_snprintf( fullpath, sizeof( fullpath ), "%s/" LUA_ROOT "/%s", gamePath, args.ArgS() );
			Q_strlower( fullpath );
			Q_FixSlashes( fullpath );
		}

		if ( Q_strstr( fullpath, ".." ) )
		{
			return;
		}
		Msg( "Running file %s...\n", args.ArgS() );
		luasrc_dofile( L, fullpath );
	}

	/* HL2SB: GMod's lua_run, server realm.  See the client one above. */
	CON_COMMAND( lua_run, "Run a Lua string (server)" )
	{
		if ( !g_bLuaInitialized )
			return;

		if ( !UTIL_IsCommandIssuedByServerAdmin() )
			return;

		if ( args.ArgC() == 1 )
		{
			Msg( "Usage: lua_run <lua code>\n" );
			return;
		}

		luasrc_dostring( L, args.ArgS() );
	}
#endif

#if DEBUG
#ifdef CLIENT_DLL
	CON_COMMAND( lua_dumpstack_cl, "Prints the Lua stack" )
	{
	  if (!g_bLuaInitialized)
	    return;
	  int n = lua_gettop(L);  /* number of objects */
	  int i;
	  lua_getglobal(L, "tostring");
	  for (i=1; i<=n; i++) {
		const char *s;
		lua_pushvalue(L, -1);  /* function to be called */
		lua_pushvalue(L, i);   /* value to print */
		lua_call(L, 1, 1);
		s = lua_tostring(L, -1);  /* get result */
		Warning( " %d:\t%s\n", i, s );
		lua_pop(L, 1);  /* pop result */
	  }
	  lua_pop(L, 1);  /* pop function */
	  if (n>0)
	    Warning( "Warning: %d object(s) left on the stack!\n", n );
	}
#else
	CON_COMMAND( lua_dumpstack, "Prints the Lua stack" )
	{
	  if (!g_bLuaInitialized)
	    return;
	  int n = lua_gettop(L);  /* number of objects */
	  int i;
	  lua_getglobal(L, "tostring");
	  for (i=1; i<=n; i++) {
		const char *s;
		lua_pushvalue(L, -1);  /* function to be called */
		lua_pushvalue(L, i);   /* value to print */
		lua_call(L, 1, 1);
		s = lua_tostring(L, -1);  /* get result */
		Warning( " %d:\t%s\n", i, s );
		lua_pop(L, 1);  /* pop result */
	  }
	  lua_pop(L, 1);  /* pop function */
	  if (n>0)
	    Warning( "Warning: %d object(s) left on the stack!\n", n );
	}
#endif
#endif
#endif

//-----------------------------------------------------------------------------
// HL2SB: the Lua side owns the ammo type definitions.
//
// The engine registers its built-in HL2MP ammo types first, then calls this
// once (from GetAmmoDef) so lua/game/shared/hl2sb_ammo.lua can override any
// field of those types, or add brand new ones.
//
// Only the fields a Lua entry actually supplies are touched; everything else
// keeps the value the engine registered.
//-----------------------------------------------------------------------------
static bool luasrc_AmmoFieldInt (lua_State *L, const char *pszKey, int &iOut)
{
  bool bFound = false;

  lua_getfield(L, -1, pszKey);

  if (lua_isnumber(L, -1))
  {
    iOut = luaL_checkint(L, -1);
    bFound = true;
  }

  lua_pop(L, 1);

  return bFound;
}

static bool luasrc_AmmoFieldFloat (lua_State *L, const char *pszKey, float &flOut)
{
  bool bFound = false;

  lua_getfield(L, -1, pszKey);

  if (lua_isnumber(L, -1))
  {
    flOut = (float)luaL_checknumber(L, -1);
    bFound = true;
  }

  lua_pop(L, 1);

  return bFound;
}

void luasrc_ApplyAmmoTypes (CAmmoDef *pAmmoDef)
{
  if (!pAmmoDef || !L || !g_bLuaInitialized)
    return;

  lua_getglobal(L, "ammo");

  if (!lua_istable(L, -1))
  {
    lua_pop(L, 1);
    return;
  }

  lua_getfield(L, -1, "getammotypes");
  lua_remove(L, -2);  /* drop the ammo table, keep the function */

  if (!lua_isfunction(L, -1))
  {
    lua_pop(L, 1);
    return;
  }

  if (luasrc_pcall(L, 0, 1, 0) != 0)
  {
    /* luasrc_pcall leaves the error object on the stack; the sibling early-outs
    ** around this one pop theirs and this one used to leak a slot.  These run
    ** from weapon/map setup, so the slots used to add up -- and a leaked Lua
    ** stack is what AGENTS.md 5.4.2 turns into "HUD disappears" later. */
    lua_pop(L, 1);
    return;
  }

  if (!lua_istable(L, -1))
  {
    lua_pop(L, 1);
    return;
  }

  int iCount = (int)lua_objlen(L, -1);
  int iApplied = 0;

  for (int i = 1; i <= iCount; ++i)
  {
    lua_rawgeti(L, -1, i);

    if (lua_istable(L, -1))
    {
      lua_getfield(L, -1, "name");
      const char *pszName = lua_isstring(L, -1) ? lua_tostring(L, -1) : NULL;
      lua_pop(L, 1);

      if (pszName && pszName[0])
      {
        int iIndex = pAmmoDef->Index(pszName);

        if (iIndex <= 0)
        {
          /* A type the engine does not know about - create it. */
          int iDmgType = DMG_BULLET, iTracer = TRACER_NONE;
          int iPlrDmg = 0, iNpcDmg = 0, iCarry = 0, iFlags = 0;
          int iMinSplash = 4, iMaxSplash = 8;
          float flForce = 0.0f;

          luasrc_AmmoFieldInt(L, "dmgtype",   iDmgType);
          luasrc_AmmoFieldInt(L, "tracer",    iTracer);
          luasrc_AmmoFieldInt(L, "plydmg",    iPlrDmg);
          luasrc_AmmoFieldInt(L, "npcdmg",    iNpcDmg);
          luasrc_AmmoFieldInt(L, "maxcarry",  iCarry);
          luasrc_AmmoFieldFloat(L, "force",   flForce);
          luasrc_AmmoFieldInt(L, "flags",     iFlags);
          luasrc_AmmoFieldInt(L, "minsplash", iMinSplash);
          luasrc_AmmoFieldInt(L, "maxsplash", iMaxSplash);

          pAmmoDef->AddAmmoType( pszName, iDmgType, iTracer, iPlrDmg, iNpcDmg,
                                 iCarry, flForce, iFlags, iMinSplash, iMaxSplash );

          iIndex = pAmmoDef->Index(pszName);

          if (iIndex > 0)
          {
            Msg( "[Ammo] Lua added ammo type '%s'\n", pszName );
            ++iApplied;
          }
          else
          {
            Warning( "[Ammo] Lua could not add ammo type '%s' (ammo table full?)\n", pszName );
          }
        }
        else
        {
          Ammo_t *pAmmo = &pAmmoDef->m_AmmoType[iIndex];
          int iValue = 0;
          float flValue = 0.0f;

          if (luasrc_AmmoFieldInt(L, "dmgtype", iValue))
            pAmmo->nDamageType = iValue;

          if (luasrc_AmmoFieldInt(L, "tracer", iValue))
            pAmmo->eTracerType = iValue;

          /* Clearing the cvar pointer makes the integer value win. */
          if (luasrc_AmmoFieldInt(L, "plydmg", iValue))
          {
            pAmmo->pPlrDmg = iValue;
            pAmmo->pPlrDmgCVar = NULL;
          }

          if (luasrc_AmmoFieldInt(L, "npcdmg", iValue))
          {
            pAmmo->pNPCDmg = iValue;
            pAmmo->pNPCDmgCVar = NULL;
          }

          if (luasrc_AmmoFieldInt(L, "maxcarry", iValue))
          {
            pAmmo->pMaxCarry = iValue;
            pAmmo->pMaxCarryCVar = NULL;
          }

          if (luasrc_AmmoFieldFloat(L, "force", flValue))
            pAmmo->physicsForceImpulse = flValue;

          if (luasrc_AmmoFieldInt(L, "flags", iValue))
            pAmmo->nFlags = iValue;

          if (luasrc_AmmoFieldInt(L, "minsplash", iValue))
            pAmmo->nMinSplashSize = iValue;

          if (luasrc_AmmoFieldInt(L, "maxsplash", iValue))
            pAmmo->nMaxSplashSize = iValue;

          ++iApplied;
        }
      }
    }

    lua_pop(L, 1);  /* pop the ammo entry */
  }

  lua_pop(L, 1);    /* pop the table */

  if (iApplied > 0)
    Msg( "[Ammo] applied %d Lua ammo definition(s)\n", iApplied );

  //--------------------------------------------------------------------------
  // HL2SB: also consume game.AddAmmoType() entries.
  //
  // GMod addons call game.AddAmmoType({ name = "rb655_nyan" }) at file scope.
  // That lands in extensions/game.lua's local AmmoTypes table, retrieved by
  // game.BuildAmmoTypes().  The ammo-module path above never sees it, so the
  // weapon logs "using undefined primary ammo type (rb655_nyan)" every load.
  // Same field names as the ammo module (name / dmgtype / tracer / ...).
  //--------------------------------------------------------------------------
  if (lua_getglobal(L, "game") != LUA_TTABLE)
  {
    lua_pop(L, 1);
    return;
  }

  lua_getfield(L, -1, "BuildAmmoTypes");
  lua_remove(L, -2);  /* drop game table */

  if (!lua_isfunction(L, -1))
  {
    lua_pop(L, 1);
    return;
  }

  if (luasrc_pcall(L, 0, 1, 0) != 0)
  {
    /* luasrc_pcall leaves the error object on the stack; the sibling early-outs
    ** around this one pop theirs and this one used to leak a slot.  These run
    ** from weapon/map setup, so the slots used to add up -- and a leaked Lua
    ** stack is what AGENTS.md 5.4.2 turns into "HUD disappears" later. */
    lua_pop(L, 1);
    return;
  }

  if (!lua_istable(L, -1))
  {
    lua_pop(L, 1);
    return;
  }

  int iGameCount = (int)lua_objlen(L, -1);
  int iGameApplied = 0;

  for (int i = 1; i <= iGameCount; ++i)
  {
    lua_rawgeti(L, -1, i);

    if (lua_istable(L, -1))
    {
      lua_getfield(L, -1, "name");
      const char *pszName = lua_isstring(L, -1) ? lua_tostring(L, -1) : NULL;
      lua_pop(L, 1);

      if (pszName && pszName[0] && pAmmoDef->Index(pszName) <= 0)
      {
        int iDmgType = DMG_BULLET, iTracer = TRACER_NONE;
        int iPlrDmg = 0, iNpcDmg = 0, iCarry = 9999, iFlags = 0;
        int iMinSplash = 4, iMaxSplash = 8;
        float flForce = 0.0f;

        luasrc_AmmoFieldInt(L, "dmgtype",   iDmgType);
        luasrc_AmmoFieldInt(L, "tracer",    iTracer);
        luasrc_AmmoFieldInt(L, "plydmg",    iPlrDmg);
        luasrc_AmmoFieldInt(L, "npcdmg",    iNpcDmg);
        luasrc_AmmoFieldInt(L, "maxcarry",  iCarry);
        luasrc_AmmoFieldFloat(L, "force",   flForce);
        luasrc_AmmoFieldInt(L, "flags",     iFlags);
        luasrc_AmmoFieldInt(L, "minsplash", iMinSplash);
        luasrc_AmmoFieldInt(L, "maxsplash", iMaxSplash);

        pAmmoDef->AddAmmoType( pszName, iDmgType, iTracer, iPlrDmg, iNpcDmg,
                               iCarry, flForce, iFlags, iMinSplash, iMaxSplash );

        if (pAmmoDef->Index(pszName) > 0)
        {
          Msg( "[Ammo] game.AddAmmoType added '%s'\n", pszName );
          ++iGameApplied;
        }
      }
    }

    lua_pop(L, 1);  /* pop the ammo entry */
  }

  lua_pop(L, 1);    /* pop BuildAmmoTypes result */

  if (iGameApplied > 0)
    Msg( "[Ammo] applied %d game.AddAmmoType definition(s)\n", iGameApplied );
}
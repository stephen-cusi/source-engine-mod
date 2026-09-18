//========== Copyleft © 2011, Team Sandbox, Some rights reserved. ===========//
//
// Purpose:
//
//===========================================================================//

#include "cbase.h"
#include "filesystem.h"
#include "luamanager.h"
#include "hl2sb_gma.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

//-----------------------------------------------------------------------------
// HL2SB: the addon enable/disable list.
//
// The main menu's Addons dialog (lua/gameui/addonsdialog.lua) writes
// <gamedir>/addons_disabled.txt: one addon folder name per line, case
// insensitive, '#' comments and blank lines ignored.  An addon named there is
// NOT MOUNTED, and that is what "disabled" means for the Lua side -- every
// loader reaches addon content through the mounted search paths, so an
// unmounted addon simply does not exist.  Read once per process: mounting
// happens at init and the list can only change between runs anyway.
//-----------------------------------------------------------------------------
static bool s_bDisabledAddonsLoaded = false;
static CUtlVector< CUtlString > s_DisabledAddons;

static void HL2SB_LoadDisabledAddons( void )
{
	if ( s_bDisabledAddonsLoaded )
		return;
	s_bDisabledAddonsLoaded = true;

	char gamePath[ 512 ] = { 0 };
#ifdef CLIENT_DLL
	const char *pszGameDir = engine->GetGameDirectory();
	Q_strncpy( gamePath, ( pszGameDir != NULL ) ? pszGameDir : "", sizeof( gamePath ) );
#else
	engine->GetGameDir( gamePath, sizeof( gamePath ) );
#endif
	if ( !gamePath[0] )
		return;

	char szFull[ 512 ];
	Q_snprintf( szFull, sizeof( szFull ), "%s/addons_disabled.txt", gamePath );

	FILE *fp = fopen( szFull, "r" );
	if ( fp == NULL )
		return;

	char szLine[ 256 ];
	while ( fgets( szLine, sizeof( szLine ), fp ) != NULL )
	{
		char *p = szLine;
		while ( *p == ' ' || *p == '\t' )
			++p;
		if ( *p == '\0' || *p == '\r' || *p == '\n' || *p == '#' || *p == ';' )
			continue;

		char *pEnd = p + Q_strlen( p );
		while ( pEnd > p && ( pEnd[-1] == '\r' || pEnd[-1] == '\n' || pEnd[-1] == ' ' || pEnd[-1] == '\t' ) )
			--pEnd;
		*pEnd = '\0';

		if ( *p != '\0' )
		{
			CUtlString &entry = s_DisabledAddons[ s_DisabledAddons.AddToTail() ];
			entry = p;
			Msg( "[HL2SB] addons: '%s' disabled by addons_disabled.txt - not mounted\n", p );
		}
	}

	fclose( fp );
}

bool HL2SB_IsAddonDisabled( const char *pszAddonName )
{
	if ( pszAddonName == NULL || pszAddonName[0] == '\0' )
		return false;

	HL2SB_LoadDisabledAddons();

	for ( int i = 0; i < s_DisabledAddons.Count(); ++i )
	{
		if ( !V_stricmp( s_DisabledAddons[ i ].String(), pszAddonName ) )
			return true;
	}

	return false;
}

//-----------------------------------------------------------------------------
// HL2SB: live addon switching (main-menu Addons dialog).
//
// The dialog used to only rewrite addons_disabled.txt, so every change waited
// for a restart.  Nothing actually stops us from re-doing the mount right away
// while we are still at the main menu -- nothing has been loaded from an addon
// yet, the search paths are only search paths, and the Lua passes run later,
// per map load.  Only once a map is running is remounting unsafe (models are
// loaded, entity factories are registered from the addon's scripts), so there
// the change is still deferred to the next start.
//-----------------------------------------------------------------------------
static bool HL2SB_AddonsInMap( void )
{
#ifdef CLIENT_DLL
	if ( engine == NULL )
		return true;
	// At the main menu neither is set; during load or play both are.
	return engine->IsConnected() || engine->IsInGame();
#else
	// The server never shows the dialog, and for it a map is always loaded.
	return true;
#endif
}

static void HL2SB_AddonsDisabledPath( char *pOut, int nOutSize )
{
	char gamePath[ 512 ] = { 0 };
#ifdef CLIENT_DLL
	const char *pszGameDir = engine->GetGameDirectory();
	Q_strncpy( gamePath, ( pszGameDir != NULL ) ? pszGameDir : "", sizeof( gamePath ) );
#else
	engine->GetGameDir( gamePath, sizeof( gamePath ) );
#endif

	if ( !gamePath[0] )
	{
		pOut[0] = '\0';
		return;
	}

	Q_snprintf( pOut, nOutSize, "%s/addons_disabled.txt", gamePath );
}

// Rewrites the list file from s_DisabledAddons (sorted, commented header).
static void HL2SB_WriteDisabledAddons( void )
{
	char szFull[ 512 ];
	HL2SB_AddonsDisabledPath( szFull, sizeof( szFull ) );
	if ( !szFull[0] )
		return;

	FILE *fp = fopen( szFull, "w" );
	if ( fp == NULL )
	{
		Warning( "[HL2SB] addons: could not write %s\n", szFull );
		return;
	}

	fprintf( fp, "# HL2SB: addons switched off in the main menu. One folder name per line.\n" );
	fprintf( fp, "# Changes apply immediately when they are made outside a map,\n" );
	fprintf( fp, "# otherwise on the next start.\n" );

	// sorted copy: a stable file that diffs cleanly
	CUtlVector< CUtlString > sorted;
	for ( int i = 0; i < s_DisabledAddons.Count(); ++i )
		sorted.AddToTail( s_DisabledAddons[ i ] );

	for ( int i = 1; i < sorted.Count(); ++i )
	{
		for ( int j = i; j > 0 && V_stricmp( sorted[ j-1 ].String(), sorted[ j ].String() ) > 0; --j )
		{
			CUtlString tmp = sorted[ j-1 ];
			sorted[ j-1 ] = sorted[ j ];
			sorted[ j ] = tmp;
		}
	}

	for ( int i = 0; i < sorted.Count(); ++i )
		fprintf( fp, "%s\n", sorted[ i ].String() );

	fclose( fp );
}

bool HL2SB_AddonsApplyLive( void )
{
	return !HL2SB_AddonsInMap();
}

bool HL2SB_SetAddonEnabled( const char *pszAddonName, bool bEnabled )
{
	if ( pszAddonName == NULL || pszAddonName[0] == '\0' )
		return false;

	HL2SB_LoadDisabledAddons();

	int iFound = -1;
	for ( int i = 0; i < s_DisabledAddons.Count(); ++i )
	{
		if ( !V_stricmp( s_DisabledAddons[ i ].String(), pszAddonName ) )
		{
			iFound = i;
			break;
		}
	}

	if ( bEnabled && iFound >= 0 )
		s_DisabledAddons.Remove( iFound );
	else if ( !bEnabled && iFound < 0 )
		s_DisabledAddons.AddToTail( CUtlString( pszAddonName ) );

	// The choice is persisted whatever the realm ends up doing with it.
	HL2SB_WriteDisabledAddons();

	if ( !HL2SB_AddonsApplyLive() )
	{
		Msg( "[HL2SB] addons: '%s' -> %s saved for the next start (a map is loaded)\n",
			pszAddonName, bEnabled ? "enabled" : "disabled" );
		return false;
	}

	char relativepath[ 512 ];
	Q_snprintf( relativepath, sizeof( relativepath ), LUA_PATH_ADDONS "/%s", pszAddonName );

	if ( bEnabled )
	{
		// Mirror MountAddons() exactly, then let the GMA pass pick up an
		// archive the user just re-enabled (extraction is skipped when its
		// marker file exists, so re-running it costs nothing).
		filesystem->AddSearchPath( relativepath, "MOD", PATH_ADD_TO_TAIL );
		HL2SB_MountGMAAddons();
	}
	else
	{
		// Folders come from MountAddons() (MOD), extracted archives add MOD and
		// GAME -- take both off so nothing keeps resolving out of a disabled
		// addon.  RemoveSearchPath() answers whether it found the path.
		bool bRemoved = filesystem->RemoveSearchPath( relativepath, "MOD" );
		filesystem->RemoveSearchPath( relativepath, "GAME" );

		if ( !bRemoved && !filesystem->IsDirectory( relativepath, "MOD" ) )
			bRemoved = true;    // nothing was mounted: nothing to do
	}

	Msg( "[HL2SB] addons: '%s' %s now\n", pszAddonName, bEnabled ? "mounted" : "unmounted" );
	return true;
}

void MountAddons()
{
	// Andrew; mount the Lua cache directory first. We consider this a temporary
	// addon used across servers
	char fullpath[ 512 ] = { 0 };
	bool bGetCurrentDirectory = V_GetCurrentDirectory( fullpath, sizeof( fullpath ) );
	if ( bGetCurrentDirectory )
	{
#ifdef CLIENT_DLL
		const char *gamePath = engine->GetGameDirectory();
#else
		char gamePath[ 256 ];
		engine->GetGameDir( gamePath, 256 );
#endif
		V_SetCurrentDirectory( gamePath );
	}
	filesystem->AddSearchPath( LUA_PATH_CACHE, "MOD", PATH_ADD_TO_TAIL );
	if ( bGetCurrentDirectory )
		V_SetCurrentDirectory( fullpath );

	FileFindHandle_t fh;

	char relativepath[ MAX_PATH ] = { 0 };
	char addonName[ 255 ] = { 0 };

	char const *fn = g_pFullFileSystem->FindFirstEx( LUA_PATH_ADDONS "/*", "MOD", &fh );
	while ( fn )
	{
		Q_strcpy( addonName, fn );
		if ( fn[0] != '.' )
		{
			if ( g_pFullFileSystem->FindIsDirectory( fh ) )
			{
				// HL2SB: the main-menu Addons dialog can switch an addon off;
				// a disabled one is not mounted (see HL2SB_IsAddonDisabled).
				if ( HL2SB_IsAddonDisabled( addonName ) )
				{
					fn = g_pFullFileSystem->FindNext( fh );
					continue;
				}

#ifdef GAME_DLL
				Msg( "Mounting addon \"%s\"...\n", addonName );
#endif

				Q_snprintf( relativepath, sizeof( relativepath ), LUA_PATH_ADDONS "/%s", addonName );
				char fullpath[ 512 ] = { 0 };
				bool bGetCurrentDirectory = V_GetCurrentDirectory( fullpath, sizeof( fullpath ) );
				if ( bGetCurrentDirectory )
				{
#ifdef CLIENT_DLL
					const char *gamePath = engine->GetGameDirectory();
#else
					char gamePath[ 256 ];
					engine->GetGameDir( gamePath, 256 );
#endif
					V_SetCurrentDirectory( gamePath );
				}
				filesystem->AddSearchPath( relativepath, "MOD", PATH_ADD_TO_TAIL );
				if ( bGetCurrentDirectory )
					V_SetCurrentDirectory( fullpath );
			}
		}

		fn = g_pFullFileSystem->FindNext( fh );
	}
	g_pFullFileSystem->FindClose( fh );
}

//-----------------------------------------------------------------------------
// HL2SB: how the main menu's Addons dialog reaches this file.  Two globals,
// registered into the GameUI Lua state only (luamanager.cpp):
//
//   hl2sb_setaddon( name, enabled ) -> true when it took effect NOW
//   hl2sb_addons_live()             -> whether a change can take effect NOW
//-----------------------------------------------------------------------------
static int HL2SB_LuaSetAddon( lua_State *L )
{
	const char *pszName = luaL_checkstring( L, 1 );
	const bool bEnabled = ( lua_toboolean( L, 2 ) != 0 );

	lua_pushboolean( L, HL2SB_SetAddonEnabled( pszName, bEnabled ) );
	return 1;
}

static int HL2SB_LuaAddonsLive( lua_State *L )
{
	lua_pushboolean( L, HL2SB_AddonsApplyLive() );
	return 1;
}

void HL2SB_LuaRegisterAddons( lua_State *L )
{
	if ( L == NULL )
		return;

	lua_pushcfunction( L, HL2SB_LuaSetAddon );
	lua_setglobal( L, "hl2sb_setaddon" );

	lua_pushcfunction( L, HL2SB_LuaAddonsLive );
	lua_setglobal( L, "hl2sb_addons_live" );
}

//========== Copyleft © 2011, Team Sandbox, Some rights reserved. ===========//
//
// Purpose:
//
//===========================================================================//

#include "cbase.h"
#include "filesystem.h"
#include "luamanager.h"
#include "hl2sb_gma.h"
#include "tier0/icommandline.h"

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
// HL2SB (2026-09-28): mtime of the list file at the last read, for the
// staleness check below.
static time_t s_nDisabledAddonsFileTime = 0;

static void HL2SB_AddonsDisabledPath( char *pOut, int nOutSize );

static void HL2SB_LoadDisabledAddons( void )
{
	// HL2SB (2026-09-28): "read once per process" was wrong in practice.  The
	// main-menu Addons dialog / addons page rewrites the file at RUNTIME and
	// only the realm that ran the dialog updates its in-memory copy; every
	// other copy (the SERVER's, above all) keeps the boot-time list until the
	// whole application exits.  Re-enabling scp173 at the menu then starting
	// a new game in the same process left the server skipping its entities
	// ("npc_scp173" unknown / gm_spawn no such class) while the client loaded
	// them fine -- a per-realm split that survived any number of map restarts.
	// The file's mtime is the shared invalidation signal both realms can see.
	time_t fileTime = g_pFullFileSystem->GetFileTime( "addons_disabled.txt", "MOD" );
	if ( s_bDisabledAddonsLoaded && fileTime == s_nDisabledAddonsFileTime )
		return;

	if ( s_bDisabledAddonsLoaded )
	{
		Msg( "[HL2SB] addons: addons_disabled.txt changed on disk - reloading the disabled list\n" );
		s_DisabledAddons.RemoveAll();
	}
	s_bDisabledAddonsLoaded = true;
	s_nDisabledAddonsFileTime = fileTime;

	// Read through the filesystem (pathID MOD) so "where is the mod dir" is
	// the ENGINE's answer, not ours.  The old form built the path from
	// GetGameDirectory() and fopen()ed it -- the prime suspect for "the menu
	// disabled an addon but it still loads": a wrong/relative gamedir or the
	// CWD juggling in MountAddons silently failed the open and every addon
	// stayed enabled.  The fopen form stays as a fallback.
	FileHandle_t fhFile = g_pFullFileSystem->Open( "addons_disabled.txt", "r", "MOD" );
	if ( fhFile == FILESYSTEM_INVALID_HANDLE )
	{
		char szFull[ 512 ];
		HL2SB_AddonsDisabledPath( szFull, sizeof( szFull ) );
		FILE *fp = ( szFull[0] != '\0' ) ? fopen( szFull, "r" ) : NULL;
		if ( fp == NULL )
		{
			Msg( "[HL2SB] addons: no addons_disabled.txt found (nothing disabled)\n" );
			return;
		}

		Msg( "[HL2SB] addons: reading disabled list from %s (fallback fopen)\n", szFull );

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
		return;
	}

	Msg( "[HL2SB] addons: reading disabled list from addons_disabled.txt (MOD)\n" );

	char szLine[ 256 ];
	while ( g_pFullFileSystem->ReadLine( szLine, sizeof( szLine ), fhFile ) != NULL )
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

	g_pFullFileSystem->Close( fhFile );
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
		luasrc_LuaWarnMsgF( "[HL2SB] addons: could not write %s\n", szFull );
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
		// archive the user just re-enabled (mounting is idempotent).
		filesystem->AddSearchPath( relativepath, "MOD", PATH_ADD_TO_TAIL );
		HL2SB_MountGMAAddons();
	}
	else
	{
		// Folder addons come from MountAddons() (MOD, plus GAME when the GMA
		// pass used to extract) -- and .gma archives are mounted directly as
		// "addons/<name>.gma".  Take both forms off so nothing keeps resolving
		// out of a disabled addon.
		bool bRemoved = filesystem->RemoveSearchPath( relativepath, "MOD" );
		filesystem->RemoveSearchPath( relativepath, "GAME" );

		char szArchiveRelative[ 512 ];
		Q_snprintf( szArchiveRelative, sizeof( szArchiveRelative ), LUA_PATH_ADDONS "/%s.gma", pszAddonName );
		if ( filesystem->RemoveSearchPath( szArchiveRelative, "MOD" ) )
			bRemoved = true;
		filesystem->RemoveSearchPath( szArchiveRelative, "GAME" );

		if ( !bRemoved && !filesystem->IsDirectory( relativepath, "MOD" ) )
			bRemoved = true;    // nothing was mounted: nothing to do
	}

	Msg( "[HL2SB] addons: '%s' %s now\n", pszAddonName, bEnabled ? "mounted" : "unmounted" );
	return true;
}

void MountAddons()
{
	// HL2SB: GMod's launch flags.  -noaddons skips addon mounting entirely
	// (its engine.dll owns the flag; the game side just honours it), and
	// -noworkshop skips the workshop layer -- this fork has no workshop layer,
	// so honouring it is a logged no-op.
	if ( CommandLine()->FindParm( "-noaddons" ) != 0 )
	{
		Msg( "[HL2SB] addons: -noaddons on the command line - not mounting any addon\n" );
		return;
	}
	if ( CommandLine()->FindParm( "-noworkshop" ) != 0 )
		Msg( "[HL2SB] addons: -noworkshop on the command line (no workshop layer in this fork; nothing to skip)\n" );

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
					Msg( "[HL2SB] addons: '%s' is disabled - not mounted this session\n", addonName );
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

//-----------------------------------------------------------------------------
// HL2SB: verdict tool for "the menu disabled an addon but it still loads".
// Prints the disabled list exactly as THIS dll read it, plus every addons/
// entry with its folder/file state and whether the disable applies to it.
// Run from the console after a start where a disabled addon showed up: a wrong
// read (empty list), a name mismatch (entry not flagged) or a missed mount is
// immediately visible instead of guessed at.
//-----------------------------------------------------------------------------
CON_COMMAND( hl2sb_addons_dump, "Dump the addon disabled list and mount state" )
{
	HL2SB_LoadDisabledAddons();

	Msg( "[HL2SB] addons: %d disabled entr%s:\n", s_DisabledAddons.Count(),
		( s_DisabledAddons.Count() == 1 ) ? "y" : "ies" );
	for ( int i = 0; i < s_DisabledAddons.Count(); ++i )
		Msg( "[HL2SB] addons:   disabled: '%s'\n", s_DisabledAddons[ i ].String() );

	FileFindHandle_t fh;
	char const *fn = g_pFullFileSystem->FindFirstEx( LUA_PATH_ADDONS "/*", "MOD", &fh );
	while ( fn )
	{
		if ( fn[0] != '.' )
		{
			Msg( "[HL2SB] addons:   addons/%s (%s)%s\n", fn,
				g_pFullFileSystem->FindIsDirectory( fh ) ? "folder" : "file",
				HL2SB_IsAddonDisabled( fn ) ? "  [DISABLED]" : "" );
		}
		fn = g_pFullFileSystem->FindNext( fh );
	}
	g_pFullFileSystem->FindClose( fh );
}

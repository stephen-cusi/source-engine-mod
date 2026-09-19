//========== Copyleft (c) 2026, HL2SB, Some rights reserved. ===========//
//
// Purpose: Garry's Mod .gma addon archive support for HL2SB.
//
//   Addons dropped into addons/<name>.gma are mounted READ-ONLY and IN PLACE,
//   GMod style: the filesystem (CGmaPackFile, filesystem_stdio.dll) treats each
//   archive as a search path of its own and reads files on demand straight out
//   of it - nothing is ever extracted to disk.
//
//   This file just walks addons/*.gma and calls filesystem->AddSearchPath()
//   for each archive on the MOD and GAME search paths; the ".gma" extension is
//   routed to the pack-file mounter inside AddSearchPathInternal().  Mounting
//   is idempotent (same archive + same path ID = no-op), so calling this from
//   both realms and again on addon re-enable is safe and cheap.
//
//===========================================================================//

#include "cbase.h"
#include "filesystem.h"
#include "utlstring.h"
#include "utlvector.h"

#include "hl2sb_gma.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

//-----------------------------------------------------------------------------
// Purpose: Finds every addons/*.gma and mounts it as a read-only search path.
//          Called by both realms before their Lua passes.
//-----------------------------------------------------------------------------
void HL2SB_MountGMAAddons()
{
	if ( !filesystem )
		return;

	CUtlVector< CUtlString > archives;

	FileFindHandle_t hFind = FILESYSTEM_INVALID_FIND_HANDLE;
	const char *pszFound = filesystem->FindFirstEx( "addons/*.gma", "MOD", &hFind );

	while ( pszFound )
	{
		if ( !filesystem->FindIsDirectory( hFind ) && pszFound[0] != '.' )
		{
			const int nLength = Q_strlen( pszFound );
			if ( nLength > 4 && !V_stricmp( pszFound + nLength - 4, ".gma" ) )
			{
				// Honour the main-menu Addons dialog: an archive switched off in
				// addons_disabled.txt is not mounted at all.
				char szArchiveBase[ 256 ];
				Q_strncpy( szArchiveBase, pszFound, sizeof( szArchiveBase ) );
				if ( nLength - 4 < (int)sizeof( szArchiveBase ) )
				{
					szArchiveBase[ nLength - 4 ] = '\0';

					if ( HL2SB_IsAddonDisabled( szArchiveBase ) )
					{
						Msg( "[HL2SB] GMA: '%s' disabled by addons_disabled.txt - skipped\n", pszFound );
						pszFound = filesystem->FindNext( hFind );
						continue;
					}
				}

				CUtlString &archive = archives[ archives.AddToTail() ];
				archive = pszFound;
			}
		}

		pszFound = filesystem->FindNext( hFind );
	}

	if ( hFind != FILESYSTEM_INVALID_FIND_HANDLE )
		filesystem->FindClose( hFind );

	// The verification signal: this line must appear exactly once per realm.
	Msg( "[HL2SB] GMA: found %d archive(s) in addons/\n", archives.Count() );

	// Sort so the mount order (and therefore search-path precedence between
	// two archives shipping the same file) is deterministic.
	for ( int i = 1; i < archives.Count(); ++i )
	{
		for ( int j = i; j > 0 && V_stricmp( archives[ j - 1 ].String(), archives[ j ].String() ) > 0; --j )
		{
			CUtlString tmp = archives[ j - 1 ];
			archives[ j - 1 ] = archives[ j ];
			archives[ j ] = tmp;
		}
	}

	int64 nTotalBytes = 0;

	for ( int i = 0; i < archives.Count(); ++i )
	{
		char szArchiveRelative[ MAX_PATH ];
		Q_snprintf( szArchiveRelative, sizeof( szArchiveRelative ), "addons/%s", archives[ i ].String() );

		nTotalBytes += (int64)filesystem->Size( szArchiveRelative, "MOD" );

		// Idempotent: an archive already mounted on a path ID is skipped by the
		// filesystem, so re-enabling an addon can just call this again.
		filesystem->AddSearchPath( szArchiveRelative, "MOD", PATH_ADD_TO_TAIL );
		filesystem->AddSearchPath( szArchiveRelative, "GAME", PATH_ADD_TO_TAIL );
	}

	Msg( "[HL2SB] GMA: total %d archive(s), %.1f MB from addons/\n",
		archives.Count(), (double)nTotalBytes / ( 1024.0 * 1024.0 ) );
}

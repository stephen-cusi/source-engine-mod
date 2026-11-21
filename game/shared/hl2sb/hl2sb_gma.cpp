//========== Copyleft (c) 2026, HL2SB, Some rights reserved. ===========//
//
// Purpose: Garry's Mod .gma addon archive support for HL2SB.
//
//   Addons dropped into addons/<name>.gma are mounted READ-ONLY and IN PLACE,
//   GMod style: the filesystem (CGmaPackFile, filesystem_stdio.dll) treats each
//   archive as a search path of its own and reads files on demand straight out
//   of it - nothing is ever extracted to disk.
//
//   This file walks addons/*.gma and addons/<Workshop ID>/*.gma and calls filesystem->AddSearchPath()
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
#include "tier0/icommandline.h"
#include "lua.h"
#include <algorithm>
#include <string>
#include <vector>

#include "hl2sb_gma.h"

// memdbgon must be the last include file in a .cpp file!!!
extern ConVar hl2sb_dev_msgs;

#include "tier0/memdbgon.h"

static bool IsWorkshopId( const char *name )
{
	if ( !name || name[0] < '1' || name[0] > '9' )
		return false;
	const size_t length = strspn( name, "0123456789" );
	return name[length] == '\0' && ( length < 20 ||
		( length == 20 && strcmp( name, "18446744073709551615" ) <= 0 ) );
}

static bool IsGmaName( const char *name )
{
	const char *extension = V_GetFileExtension( name );
	return extension && !V_stricmp( extension, "gma" );
}

static void CollectGmas( const char *directory, CUtlVector< CUtlString > &archives, bool checkDisabled )
{
	char pattern[MAX_PATH];
	const int length = V_snprintf( pattern, sizeof( pattern ), "%s/*.gma", directory );
	if ( length < 0 || length >= (int)sizeof( pattern ) )
		return;
	FileFindHandle_t hFind = FILESYSTEM_INVALID_FIND_HANDLE;
	const char *pszFound = filesystem->FindFirstEx( pattern, "MOD", &hFind );
	while ( pszFound )
	{
		if ( pszFound[0] != '.' && !filesystem->FindIsDirectory( hFind ) && IsGmaName( pszFound ) )
		{
			std::string name( pszFound );
			if ( !checkDisabled || !HL2SB_IsAddonDisabled( name.substr( 0, name.size() - 4 ).c_str() ) )
			{
				char path[MAX_PATH];
				const int pathLength = V_snprintf( path, sizeof( path ), "%s/%s", directory, name.c_str() );
				if ( pathLength >= 0 && pathLength < (int)sizeof( path ) )
					archives.AddToTail( CUtlString( path ) );
			}
		}
		pszFound = filesystem->FindNext( hFind );
	}
	if ( hFind != FILESYSTEM_INVALID_FIND_HANDLE )
		filesystem->FindClose( hFind );
}

static void CollectWorkshopGmas( const char *name, CUtlVector< CUtlString > &archives )
{
	if ( IsWorkshopId( name ) )
	{
		char directory[sizeof( "addons/" ) + 20];
		V_snprintf( directory, sizeof( directory ), "addons/%s", name );
		CollectGmas( directory, archives, false );
	}
}

bool HL2SB_IsWorkshopAddonDirectory( const char *name )
{
	CUtlVector< CUtlString > archives;
	CollectWorkshopGmas( name, archives );
	return archives.Count() != 0;
}

void HL2SB_UnmountWorkshopAddon( const char *name )
{
	CUtlVector< CUtlString > archives;
	CollectWorkshopGmas( name, archives );
	for ( int i = 0; i < archives.Count(); ++i )
	{
		filesystem->RemoveSearchPath( archives[i].String(), "MOD" );
		filesystem->RemoveSearchPath( archives[i].String(), "GAME" );
	}
}

void HL2SB_MountGMAAddons()
{
	if ( !filesystem || CommandLine()->FindParm( "-noaddons" ) != 0 )
		return;

	CUtlVector< CUtlString > archives;
	CollectGmas( "addons", archives, true );
	if ( CommandLine()->FindParm( "-noworkshop" ) == 0 )
	{
		FileFindHandle_t hFind = FILESYSTEM_INVALID_FIND_HANDLE;
		const char *name = filesystem->FindFirstEx( "addons/*", "MOD", &hFind );
		while ( name )
		{
			if ( IsWorkshopId( name ) && filesystem->FindIsDirectory( hFind ) && !HL2SB_IsAddonDisabled( name ) )
				CollectWorkshopGmas( name, archives );
			name = filesystem->FindNext( hFind );
		}
		if ( hFind != FILESYSTEM_INVALID_FIND_HANDLE )
			filesystem->FindClose( hFind );
	}

	// The verification signal: this line must appear exactly once per realm.
	if ( hl2sb_dev_msgs.GetBool() )
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
		const char *szArchiveRelative = archives[i].String();

		nTotalBytes += (int64)filesystem->Size( szArchiveRelative, "MOD" );

		// Idempotent: an archive already mounted on a path ID is skipped by the
		// filesystem, so re-enabling an addon can just call this again.
		filesystem->AddSearchPath( szArchiveRelative, "MOD", PATH_ADD_TO_TAIL );
		filesystem->AddSearchPath( szArchiveRelative, "GAME", PATH_ADD_TO_TAIL );
	}

	if ( hl2sb_dev_msgs.GetBool() )
	Msg( "[HL2SB] GMA: total %d archive(s), %.1f MB from addons/\n",
		archives.Count(), (double)nTotalBytes / ( 1024.0 * 1024.0 ) );
}

void HL2SB_AppendMountedWorkshopAddons( lua_State *L, int firstIndex )
{
	const int size = filesystem->GetSearchPath( "GAME", true, NULL, 0 );
	std::vector<char> paths( size );
	filesystem->GetSearchPath( "GAME", true, paths.data(), size );
	std::vector<std::string> reportedIds;
	for ( const char *start = paths.data(); *start; )
	{
		const char *end = strchr( start, ';' );
		if ( !end )
			end = start + strlen( start );
		std::string path( start, end );
		start = *end ? end + 1 : end;
		for ( size_t i = 0; i < path.size(); ++i )
		{
			if ( path[i] == '\\' )
				path[i] = '/';
		}
		if ( !path.empty() && path.back() == '/' )
			path.pop_back();
		if ( !IsGmaName( path.c_str() ) )
			continue;

		const size_t archiveSlash = path.rfind( '/' );
		if ( archiveSlash == std::string::npos || archiveSlash == 0 )
			continue;
		const size_t parentSlash = path.rfind( '/', archiveSlash - 1 );
		if ( parentSlash == std::string::npos || parentSlash < 7 ||
			Q_strnicmp( path.c_str() + parentSlash - 7, "/addons", 7 ) )
			continue;
		const std::string id = path.substr( parentSlash + 1, archiveSlash - parentSlash - 1 );
		if ( !IsWorkshopId( id.c_str() ) || std::find( reportedIds.begin(), reportedIds.end(), id ) != reportedIds.end() )
			continue;

		reportedIds.push_back( id );
		const std::string file = path.substr( parentSlash - 6 );
		lua_newtable( L );
		lua_pushstring( L, id.c_str() ); lua_setfield( L, -2, "wsid" );
		lua_pushstring( L, id.c_str() ); lua_setfield( L, -2, "title" );
		lua_pushstring( L, file.c_str() ); lua_setfield( L, -2, "file" );
		lua_pushliteral( L, "" ); lua_setfield( L, -2, "tags" );
		lua_pushboolean( L, true ); lua_setfield( L, -2, "mounted" );
		lua_pushboolean( L, true ); lua_setfield( L, -2, "downloaded" );
		const char *emptyFields[] = { "models", "timeadded", "updated", "size" };
		for ( size_t i = 0; i < ARRAYSIZE( emptyFields ); ++i )
		{
			lua_pushinteger( L, 0 );
			lua_setfield( L, -2, emptyFields[i] );
		}
		lua_rawseti( L, -2, firstIndex++ );
	}
}

// The catalog sits beside the active game tree. Only mount paths are used
// by engine.GetAddons; catalog entries remain separate from active addons.
static std::string LocalWorkshopRoot( const char *gameDirectory )
{
	if ( !gameDirectory || !V_IsAbsolutePath( gameDirectory ) )
		return std::string();

	std::string path( gameDirectory );
	std::replace( path.begin(), path.end(), '\\', '/' );
	const size_t last = path.find_last_not_of( '/' );
	if ( last == std::string::npos )
		return std::string();
	path.resize( last + 1 );

	const size_t separator = path.rfind( '/' );
	if ( separator == std::string::npos )
		return std::string();
	return path.substr( 0, separator + 1 ) + "content/workshop/" + path.substr( separator + 1 );
}

struct LocalWorkshopItem
{
	uint64 size;
	time_t updated;
};

static bool ReadWorkshopItem( const std::string &root, const char *id, LocalWorkshopItem *item )
{
	const std::string directory = root + "/" + id + "/";
	const std::string pattern = directory + "*";
	if ( pattern.size() >= MAX_PATH )
		return false;

	FileFindHandle_t find = FILESYSTEM_INVALID_FIND_HANDLE;
	bool hasArchive = false;
	for ( const char *name = filesystem->FindFirst( pattern.c_str(), &find ); name; name = filesystem->FindNext( find ) )
	{
		if ( name[0] == '.' || !IsGmaName( name ) || filesystem->FindIsDirectory( find ) )
			continue;

		const std::string archive = directory + name;
		if ( archive.size() >= MAX_PATH )
			continue;

		FileHandle_t file = filesystem->Open( archive.c_str(), "rb" );
		if ( file == FILESYSTEM_INVALID_HANDLE )
			continue;

		unsigned char header[5];
		const int bytes = filesystem->Read( header, sizeof( header ), file );
		const unsigned int archiveSize = filesystem->Size( file );
		filesystem->Close( file );
		if ( bytes != (int)sizeof( header ) || archiveSize < 22 || memcmp( header, "GMAD", 4 ) ||
			header[4] < 1 || header[4] > 3 )
			continue;

		hasArchive = true;
		if ( !item )
			break;
		item->size += archiveSize;
		const time_t updated = filesystem->GetFileTime( archive.c_str() );
		if ( updated > item->updated )
			item->updated = updated;
	}
	if ( find != FILESYSTEM_INVALID_FIND_HANDLE )
		filesystem->FindClose( find );
	return hasArchive;
}

bool HL2SB_HasLocalWorkshopContent( const char *gameDirectory, const char *id )
{
	if ( !filesystem || !IsWorkshopId( id ) )
		return false;
	const std::string root = LocalWorkshopRoot( gameDirectory );
	return !root.empty() && ReadWorkshopItem( root, id, NULL );
}

void HL2SB_PushLocalWorkshopContent( lua_State *L, const char *gameDirectory )
{
	lua_newtable( L );
	if ( !filesystem )
		return;

	const std::string root = LocalWorkshopRoot( gameDirectory );
	const std::string pattern = root + "/*";
	if ( root.empty() || pattern.size() >= MAX_PATH )
		return;

	std::vector<std::string> ids;
	FileFindHandle_t find = FILESYSTEM_INVALID_FIND_HANDLE;
	for ( const char *name = filesystem->FindFirst( pattern.c_str(), &find ); name; name = filesystem->FindNext( find ) )
	{
		if ( IsWorkshopId( name ) && filesystem->FindIsDirectory( find ) )
			ids.push_back( name );
	}
	if ( find != FILESYSTEM_INVALID_FIND_HANDLE )
		filesystem->FindClose( find );

	std::sort( ids.begin(), ids.end() );
	ids.erase( std::unique( ids.begin(), ids.end() ), ids.end() );
	int index = 1;
	for ( const std::string &id : ids )
	{
		LocalWorkshopItem item = { 0, 0 };
		if ( !ReadWorkshopItem( root, id.c_str(), &item ) )
			continue;

		lua_newtable( L );
		lua_pushstring( L, id.c_str() ); lua_setfield( L, -2, "wsid" );
		lua_pushstring( L, id.c_str() ); lua_setfield( L, -2, "title" );
		lua_pushliteral( L, "" ); lua_setfield( L, -2, "tags" );
		lua_pushinteger( L, item.size ); lua_setfield( L, -2, "size" );
		lua_pushinteger( L, item.updated ); lua_setfield( L, -2, "updated" );
		lua_pushinteger( L, item.updated ); lua_setfield( L, -2, "timeadded" );
		lua_pushboolean( L, false ); lua_setfield( L, -2, "mounted" );
		lua_pushboolean( L, true ); lua_setfield( L, -2, "downloaded" );
		lua_pushboolean( L, true ); lua_setfield( L, -2, "local_workshop" );
		lua_rawseti( L, -2, index++ );
	}
}

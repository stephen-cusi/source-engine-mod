//========== Copyleft (c) 2026, HL2SB, Some rights reserved. ===========//
//
// Purpose: Garry's Mod .gma addon archive support for HL2SB.
//
//   Addons dropped into addons/<name>.gma are mounted READ-ONLY and IN PLACE,
//   GMod style: the filesystem (CGmaPackFile, filesystem_stdio.dll) treats each
//   archive as a search path of its own and reads files on demand straight out
//   of it - nothing is ever extracted to disk.
//
//   This file finds local archives and linked Workshop uploads, then calls filesystem->AddSearchPath()
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
#include "lua.hpp"
#include <algorithm>
#include <string>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#endif

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

// Workshop content is stored beside the active game tree.
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

static std::string CurrentLocalWorkshopRoot()
{
	if ( !engine )
		return std::string();
#ifdef CLIENT_DLL
	return LocalWorkshopRoot( engine->GetGameDirectory() );
#else
	char gameDirectory[MAX_PATH] = { 0 };
	engine->GetGameDir( gameDirectory, sizeof( gameDirectory ) );
	return LocalWorkshopRoot( gameDirectory );
#endif
}

// The filesystem keeps the path used to mount a GMA, including its ID folder.
// Keep the disabled-addon load guard scoped to this game's Workshop archives.
bool HL2SB_IsDisabledWorkshopAddonPath( const char *path )
{
	if ( !path || !V_stristr( path, ".gma" ) )
		return false;

	const std::string root = CurrentLocalWorkshopRoot();
	if ( root.empty() )
		return false;
	std::string normalized( path );
	std::replace( normalized.begin(), normalized.end(), '\\', '/' );

	const std::string relativeRoot = root.substr( root.rfind( "/content/workshop/" ) + 1 );
	const std::string &prefix = V_IsAbsolutePath( path ) ? root : relativeRoot;
	if ( normalized.size() <= prefix.size() || normalized[prefix.size()] != '/' ||
#ifdef _WIN32
		V_strnicmp( normalized.c_str(), prefix.c_str(), prefix.size() ) != 0 )
#else
		normalized.compare( 0, prefix.size(), prefix ) != 0 )
#endif
		return false;

	const size_t idStart = prefix.size() + 1;
	const size_t archiveStart = normalized.find( '/', idStart );
	if ( archiveStart == std::string::npos )
		return false;
	const size_t innerStart = normalized.find( '/', archiveStart + 1 );
	if ( innerStart == std::string::npos )
		return false;

	const std::string id = normalized.substr( idStart, archiveStart - idStart );
	const std::string archive = normalized.substr( archiveStart + 1, innerStart - archiveStart - 1 );
	return IsWorkshopId( id.c_str() ) && IsGmaName( archive.c_str() ) && HL2SB_IsAddonDisabled( id.c_str() );
}

// An explicitly linked directory or archive is an installation. Ordinary
// downloads in the catalog are still only cached unless installed in addons/.
static bool IsWorkshopLink( const std::string &path )
{
#ifdef _WIN32
	wchar_t widePath[MAX_PATH];
	if ( !V_UTF8ToUnicode( path.c_str(), widePath, sizeof( widePath ) ) )
		return false;
	std::replace( widePath, widePath + wcslen( widePath ), L'/', L'\\' );

	WIN32_FIND_DATAW data;
	HANDLE find = FindFirstFileW( widePath, &data );
	if ( find == INVALID_HANDLE_VALUE )
		return false;
	FindClose( find );
	return ( data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT ) != 0 &&
		( data.dwReserved0 == IO_REPARSE_TAG_MOUNT_POINT || data.dwReserved0 == IO_REPARSE_TAG_SYMLINK );
#else
	struct stat data;
	return lstat( path.c_str(), &data ) == 0 && S_ISLNK( data.st_mode );
#endif
}

static void CollectGmas( const char *directory, CUtlVector< CUtlString > &archives, bool checkDisabled, bool linkedFilesOnly = false )
{
	char pattern[MAX_PATH];
	const int length = V_snprintf( pattern, sizeof( pattern ), "%s/*.gma", directory );
	if ( length < 0 || length >= (int)sizeof( pattern ) )
		return;
	FileFindHandle_t hFind = FILESYSTEM_INVALID_FIND_HANDLE;
	const char *pszFound = filesystem->FindFirstEx( pattern, V_IsAbsolutePath( directory ) ? NULL : "MOD", &hFind );
	while ( pszFound )
	{
		if ( pszFound[0] != '.' && !filesystem->FindIsDirectory( hFind ) && IsGmaName( pszFound ) )
		{
			std::string name( pszFound );
			if ( !checkDisabled || !HL2SB_IsAddonDisabled( name.substr( 0, name.size() - 4 ).c_str() ) )
			{
				char path[MAX_PATH];
				const int pathLength = V_snprintf( path, sizeof( path ), "%s/%s", directory, name.c_str() );
				if ( pathLength >= 0 && pathLength < (int)sizeof( path ) &&
					( !linkedFilesOnly || IsWorkshopLink( path ) ) )
					archives.AddToTail( CUtlString( path ) );
			}
		}
		pszFound = filesystem->FindNext( hFind );
	}
	if ( hFind != FILESYSTEM_INVALID_FIND_HANDLE )
		filesystem->FindClose( hFind );
}

static void CollectWorkshopAddonGmas( const char *id, CUtlVector< CUtlString > &archives )
{
	char directory[sizeof( "addons/" ) + 20];
	V_snprintf( directory, sizeof( directory ), "addons/%s", id );
	CollectGmas( directory, archives, false );
}

static void CollectLinkedWorkshopGmas( const std::string &root, const char *id, CUtlVector< CUtlString > &archives )
{
	if ( !root.empty() )
	{
		const std::string directory = root + "/" + id;
		CollectGmas( directory.c_str(), archives, false, !IsWorkshopLink( directory ) );
	}
}

static std::vector<std::string> FindWorkshopIds( const std::string &directory, const char *pathId )
{
	std::vector<std::string> ids;
	const std::string pattern = directory + "/*";
	if ( directory.empty() || pattern.size() >= MAX_PATH )
		return ids;

	FileFindHandle_t find = FILESYSTEM_INVALID_FIND_HANDLE;
	for ( const char *name = filesystem->FindFirstEx( pattern.c_str(), pathId, &find ); name;
		name = filesystem->FindNext( find ) )
	{
		if ( IsWorkshopId( name ) && filesystem->FindIsDirectory( find ) )
			ids.push_back( name );
	}
	if ( find != FILESYSTEM_INVALID_FIND_HANDLE )
		filesystem->FindClose( find );

	std::sort( ids.begin(), ids.end() );
	ids.erase( std::unique( ids.begin(), ids.end() ), ids.end() );
	return ids;
}

bool HL2SB_IsWorkshopAddonDirectory( const char *name )
{
	if ( !filesystem || !IsWorkshopId( name ) )
		return false;

	CUtlVector< CUtlString > archives;
	CollectWorkshopAddonGmas( name, archives );
	if ( archives.IsEmpty() )
		CollectLinkedWorkshopGmas( CurrentLocalWorkshopRoot(), name, archives );
	return !archives.IsEmpty();
}

void HL2SB_UnmountWorkshopAddon( const char *name )
{
	if ( !filesystem || !IsWorkshopId( name ) )
		return;

	CUtlVector< CUtlString > archives;
	CollectWorkshopAddonGmas( name, archives );
	CollectLinkedWorkshopGmas( CurrentLocalWorkshopRoot(), name, archives );
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
		const std::string root = CurrentLocalWorkshopRoot();
		std::vector<std::string> addonIdsWithArchives;
		for ( const std::string &id : FindWorkshopIds( "addons", "MOD" ) )
		{
			if ( HL2SB_IsAddonDisabled( id.c_str() ) )
				continue;
			const int previousCount = archives.Count();
			CollectWorkshopAddonGmas( id.c_str(), archives );
			if ( archives.Count() != previousCount )
				addonIdsWithArchives.push_back( id );
		}

		for ( const std::string &id : FindWorkshopIds( root, NULL ) )
		{
			if ( !std::binary_search( addonIdsWithArchives.begin(), addonIdsWithArchives.end(), id ) &&
				!HL2SB_IsAddonDisabled( id.c_str() ) )
				CollectLinkedWorkshopGmas( root, id.c_str(), archives );
		}
	}

	// The verification signal: this line must appear exactly once per realm.
	if ( hl2sb_dev_msgs.GetBool() )
	Msg( "[HL2SB] GMA: found %d archive(s) for mounting\n", archives.Count() );

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
	Msg( "[HL2SB] GMA: total %d archive(s), %.1f MB\n",
		archives.Count(), (double)nTotalBytes / ( 1024.0 * 1024.0 ) );
}

void HL2SB_AppendMountedWorkshopAddons( lua_State *L, int firstIndex )
{
	const std::string root = CurrentLocalWorkshopRoot();
	const size_t contentPrefixPosition = root.rfind( "/content/workshop/" );
	const int size = filesystem->GetSearchPath( "GAME", true, NULL, 0 );
	if ( size <= 0 )
		return;
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
		if ( parentSlash == std::string::npos )
			continue;
		size_t fileStart;
		if ( parentSlash >= 7 && !V_strnicmp( path.c_str() + parentSlash - 7, "/addons", 7 ) )
			fileStart = parentSlash - 6;
		else if ( !root.empty() && parentSlash == root.size() && contentPrefixPosition != std::string::npos &&
#ifdef _WIN32
			!V_strnicmp( path.c_str(), root.c_str(), root.size() ) )
#else
			path.compare( 0, parentSlash, root ) == 0 )
#endif
			fileStart = contentPrefixPosition + 1;
		else
			continue;

		const std::string id = path.substr( parentSlash + 1, archiveSlash - parentSlash - 1 );
		if ( !IsWorkshopId( id.c_str() ) || std::find( reportedIds.begin(), reportedIds.end(), id ) != reportedIds.end() )
			continue;

		reportedIds.push_back( id );
		const std::string file = path.substr( fileStart );
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

struct LocalWorkshopItem
{
	uint64 size;
	time_t updated;
	bool installed;
	std::string file;
};

static bool ReadWorkshopItem( const std::string &root, const char *id, LocalWorkshopItem *item )
{
	const std::string directory = root + "/" + id;
	const std::string pattern = directory + "/*";
	if ( pattern.size() >= MAX_PATH )
		return false;

	FileFindHandle_t find = FILESYSTEM_INVALID_FIND_HANDLE;
	const bool linkedDirectory = item && IsWorkshopLink( directory );
	bool hasArchive = false;
	for ( const char *name = filesystem->FindFirst( pattern.c_str(), &find ); name; name = filesystem->FindNext( find ) )
	{
		if ( name[0] == '.' || !IsGmaName( name ) || filesystem->FindIsDirectory( find ) )
			continue;

		const std::string archive = directory + "/" + name;
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
		if ( linkedDirectory || IsWorkshopLink( archive ) )
		{
			if ( !item->installed )
				item->file = archive.substr( root.rfind( "/content/workshop/" ) + 1 );
			item->installed = true;
		}
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
	int index = 1;
	for ( const std::string &id : FindWorkshopIds( root, NULL ) )
	{
		LocalWorkshopItem item = {};
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
		lua_pushboolean( L, item.installed ); lua_setfield( L, -2, "installed" );
		if ( item.installed )
		{
			lua_pushstring( L, item.file.c_str() );
			lua_setfield( L, -2, "file" );
		}
		lua_rawseti( L, -2, index++ );
	}
}

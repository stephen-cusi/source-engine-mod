// Public Workshop metadata for locally archived items in the main-menu realm.
#include "cbase.h"
#ifdef LUA_SDK
#include "luamanager.h"
#include "filesystem.h"
#include "hl2sb_gma.h"
#include "tier1/keyvaluesjson.h"
#include "tier1/utlbuffer.h"
#include "../thirdparty/stb/stb_image.h"
#include <atomic>
#include <chrono>
#include <ctime>
#include <deque>
#include <map>
#include <string>
#include <thread>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#undef GetClassName
#pragma comment(lib, "winhttp.lib")
#endif
#include "tier0/memdbgon.h"

namespace
{
const int kDetailsLimit = 256 * 1024;
const int kPreviewLimit = 1024 * 1024;
const int kMaxPending = 48;
const char *const kWorkshopCachePathID = "DEFAULT_WRITE_PATH";
const time_t kCacheTtl = 24 * 60 * 60;
const time_t kFailureRetryDelay = 60;

struct WorkshopInfo
{
	std::string title, description, tags, previewUrl, preview;
};

std::map<std::string, std::vector<int>> s_Callbacks;
std::map<std::string, time_t> s_RetryAfter;
std::deque<std::string> s_Queue;
std::thread s_Worker;
std::atomic<bool> s_Complete( false ), s_Stopping( false );
std::string s_ActiveId, s_Response;
WorkshopInfo s_ActiveInfo;
bool s_DownloadingPreview = false;

std::string CacheDirectory( const std::string &id )
{
	return ".cache/workshop/" + id;
}

bool ReadCache( const std::string &path, int limit, std::string &bytes )
{
	CUtlBuffer buffer;
	if ( !filesystem->ReadFile( path.c_str(), kWorkshopCachePathID, buffer, limit + 1 ) ||
		buffer.TellPut() <= 0 || buffer.TellPut() > limit )
		return false;

	bytes.assign( static_cast<const char *>( buffer.Base() ), buffer.TellPut() );
	return true;
}

bool WriteCache( const std::string &path, const std::string &bytes )
{
	const std::string temporary = path + ".part";
	CUtlBuffer buffer;
	buffer.Put( bytes.data(), static_cast<int>( bytes.size() ) );
	if ( !filesystem->WriteFile( temporary.c_str(), kWorkshopCachePathID, buffer ) )
	{
		filesystem->RemoveFile( temporary.c_str(), kWorkshopCachePathID );
		return false;
	}
#ifdef _WIN32
	// The engine's RenameFile cannot replace an existing file on Windows.
	char source[MAX_PATH];
	bool replaced = false;
	if ( filesystem->RelativePathToFullPath( temporary.c_str(), kWorkshopCachePathID, source, sizeof( source ) ) )
	{
		const size_t length = strlen( source );
		if ( length > 5 && V_strcmp( source + length - 5, ".part" ) == 0 )
		{
			const std::string destination( source, length - 5 );
			replaced = MoveFileExA( source, destination.c_str(), MOVEFILE_REPLACE_EXISTING ) != 0;
		}
	}
#else
	const bool replaced = filesystem->RenameFile( temporary.c_str(), path.c_str(), kWorkshopCachePathID );
#endif
	if ( !replaced ) filesystem->RemoveFile( temporary.c_str(), kWorkshopCachePathID );
	return replaced;
}

// Bound parser recursion as well as bytes. Escaped braces inside strings do
// not contribute to the nesting depth.
bool WorkshopJSONDepthValid( const std::string &json )
{
	int depth = 0;
	bool quoted = false, escaped = false;
	for ( char c : json )
	{
		if ( quoted )
		{
			if ( escaped ) escaped = false;
			else if ( c == '\\' ) escaped = true;
			else if ( c == '"' ) quoted = false;
		}
		else if ( c == '"' ) quoted = true;
		else if ( ( c == '[' || c == '{' ) && ++depth > 32 ) return false;
		else if ( ( c == ']' || c == '}' ) && --depth < 0 ) return false;
	}
	return depth == 0 && !quoted;
}

std::string WorkshopText( const char *text, size_t limit )
{
	std::string result;
	while ( *text && result.size() < limit )
	{
		const unsigned char c = static_cast<unsigned char>( *text++ );
		if ( ( c >= 32 && c != 127 ) || c == '\n' || c == '\t' )
			result += static_cast<char>( c );
	}
	// When the cap falls inside a UTF-8 character, drop its partial bytes.
	if ( *text && ( static_cast<unsigned char>( *text ) & 0xc0 ) == 0x80 )
	{
		while ( !result.empty() && ( static_cast<unsigned char>( result.back() ) & 0xc0 ) == 0x80 )
			result.pop_back();
		if ( !result.empty() ) result.pop_back();
	}
	return result;
}

bool ParseWorkshopDetails( const std::string &id, const std::string &json, WorkshopInfo &info )
{
	if ( json.empty() || !WorkshopJSONDepthValid( json ) ) return false;
	KeyValuesJSONParser parser( json.data(), static_cast<int>( json.size() ) );
	KeyValues *root = parser.ParseFile();
	KeyValues::AutoDelete release( root );
	if ( !root ) return false;
	KeyValues *response = root->FindKey( "response" );
	KeyValues *files = response ? response->FindKey( "publishedfiledetails" ) : NULL;
	KeyValues *item = files ? files->FindKey( "0" ) : NULL;
	if ( !item || item->GetInt( "result" ) != 1 || id != item->GetString( "publishedfileid" ) )
		return false;

	info.title = WorkshopText( item->GetString( "title" ), 240 );
	info.description = WorkshopText( item->GetString( "description" ), 6000 );
	info.previewUrl = item->GetString( "preview_url" );
	KeyValues *tags = item->FindKey( "tags" );
	if ( tags )
	{
		int remaining = 16;
		for ( KeyValues *tag = tags->GetFirstTrueSubKey(); tag && remaining-- > 0; tag = tag->GetNextTrueSubKey() )
		{
			const std::string name = WorkshopText( tag->GetString( "tag" ), 48 );
			if ( name.empty() ) continue;
			if ( !info.tags.empty() ) info.tags += ',';
			info.tags += name;
		}
	}
	return !info.title.empty();
}

const char *PreviewExtension( const std::string &bytes )
{
	const bool png = bytes.size() >= 8 && bytes.compare( 0, 8, "\x89PNG\r\n\x1a\n", 8 ) == 0;
	const bool jpeg = bytes.size() >= 3 && bytes.compare( 0, 3, "\xff\xd8\xff", 3 ) == 0;
	int width, height, channels;
	if ( ( png || jpeg ) && stbi_info_from_memory( reinterpret_cast<const stbi_uc *>( bytes.data() ),
		static_cast<int>( bytes.size() ), &width, &height, &channels ) &&
		width > 0 && width <= 4096 && height > 0 && height <= 4096 )
		return png ? ".png" : ".jpg";
	return NULL;
}

std::string CachedPreview( const std::string &directory )
{
	std::string data;
	for ( const char *name : { "preview.jpg", "preview.png" } )
	{
		const std::string path = directory + "/" + name;
		if ( ReadCache( path, kPreviewLimit, data ) && PreviewExtension( data ) )
			return path;
	}
	return {};
}

bool ReadCachedInfo( const std::string &id, WorkshopInfo &info, time_t &modified )
{
	const std::string directory = CacheDirectory( id );
	const std::string file = directory + "/details.json";
	std::string json;
	if ( !ReadCache( file, kDetailsLimit, json ) || !ParseWorkshopDetails( id, json, info ) )
		return false;
	info.preview = CachedPreview( directory );
	modified = filesystem->GetFileTime( file.c_str(), kWorkshopCachePathID );
	return true;
}

#ifdef _WIN32
bool WorkshopPreviewTarget( const std::string &url, std::wstring &host, std::wstring &path )
{
	const size_t start = sizeof( "https://" ) - 1;
	if ( url.size() > 2048 || url.compare( 0, start, "https://" ) != 0 ) return false;
	const size_t slash = url.find( '/', start );
	if ( slash == std::string::npos ) return false;
	const std::string name = url.substr( start, slash - start );
	if ( name != "images.steamusercontent.com" && name != "steamuserimages-a.akamaihd.net" &&
		name != "steamusercontent-a.akamaihd.net" && name != "community.cloudflare.steamstatic.com" )
		return false;
	for ( unsigned char c : url )
		if ( c <= 32 || c >= 127 || c == '\\' || c == '#' ) return false;
	host.assign( name.begin(), name.end() );
	path.assign( url.begin() + slash, url.end() );
	return true;
}

class WorkshopHttpHandle
{
public:
	explicit WorkshopHttpHandle( HINTERNET handle ) : m_Handle( handle ) {}
	~WorkshopHttpHandle() { if ( m_Handle ) WinHttpCloseHandle( m_Handle ); }
	operator HINTERNET() const { return m_Handle; }
	WorkshopHttpHandle( const WorkshopHttpHandle & ) = delete;
	WorkshopHttpHandle &operator=( const WorkshopHttpHandle & ) = delete;
private:
	HINTERNET m_Handle;
};

std::string RequestWorkshopHTTPS( const wchar_t *host, const wchar_t *path, std::string &post, size_t limit )
{
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds( 8 );
	WorkshopHttpHandle session( WinHttpOpen( L"HL2SB Workshop", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
		WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0 ) );
	if ( !session || !WinHttpSetTimeouts( session, 2500, 2500, 2500, 2500 ) ) return {};
	WorkshopHttpHandle connection( WinHttpConnect( session, host, INTERNET_DEFAULT_HTTPS_PORT, 0 ) );
	if ( !connection ) return {};
	WorkshopHttpHandle request( WinHttpOpenRequest( connection, post.empty() ? L"GET" : L"POST", path,
		NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE ) );
	DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
	DWORD disable = WINHTTP_DISABLE_COOKIES | WINHTTP_DISABLE_AUTHENTICATION;
	if ( !request || !WinHttpSetOption( request, WINHTTP_OPTION_REDIRECT_POLICY, &redirect, sizeof( redirect ) ) ||
		!WinHttpSetOption( request, WINHTTP_OPTION_DISABLE_FEATURE, &disable, sizeof( disable ) ) )
		return {};
	const DWORD length = static_cast<DWORD>( post.size() );
	if ( s_Stopping.load() ||
		!WinHttpSendRequest( request, length ? L"Content-Type: application/x-www-form-urlencoded\r\n" : WINHTTP_NO_ADDITIONAL_HEADERS,
			length ? static_cast<DWORD>( -1 ) : 0, length ? &post[0] : WINHTTP_NO_REQUEST_DATA, length, length, 0 ) ||
		!WinHttpReceiveResponse( request, NULL ) )
		return {};
	DWORD status = 0, statusSize = sizeof( status );
	if ( !WinHttpQueryHeaders( request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
		WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX ) || status != 200 )
		return {};

	std::string data;
	char chunk[8192];
	while ( !s_Stopping.load() && std::chrono::steady_clock::now() < deadline )
	{
		DWORD read = 0;
		if ( !WinHttpReadData( request, chunk, sizeof( chunk ), &read ) || data.size() + read > limit )
			return {};
		if ( !read ) return data;
		data.append( chunk, read );
	}
	return {};
}
#endif

void FetchWorkshopItem( std::string id, std::string previewUrl )
{
#ifdef _WIN32
	if ( previewUrl.empty() )
	{
		std::string post = "itemcount=1&publishedfileids%5B0%5D=" + id;
		s_Response = RequestWorkshopHTTPS( L"api.steampowered.com",
			L"/ISteamRemoteStorage/GetPublishedFileDetails/v1/", post, kDetailsLimit );
	}
	else
	{
		std::wstring host, path;
		std::string post;
		if ( WorkshopPreviewTarget( previewUrl, host, path ) )
			s_Response = RequestWorkshopHTTPS( host.c_str(), path.c_str(), post, kPreviewLimit );
	}
#endif
	s_Complete.store( true, std::memory_order_release );
}

void PushWorkshopInfo( lua_State *state, const WorkshopInfo *info )
{
	if ( !info )
	{
		lua_pushnil( state );
		return;
	}
	lua_createtable( state, 0, 4 );
	lua_pushlstring( state, info->title.data(), info->title.size() ); lua_setfield( state, -2, "title" );
	lua_pushlstring( state, info->description.data(), info->description.size() ); lua_setfield( state, -2, "description" );
	lua_pushlstring( state, info->tags.data(), info->tags.size() ); lua_setfield( state, -2, "tags" );
	if ( !info->preview.empty() )
	{
		lua_pushlstring( state, info->preview.data(), info->preview.size() ); lua_setfield( state, -2, "preview" );
	}
}

void StartWorkshopRequest( const std::string &preview )
{
	s_Response.clear();
	s_Complete.store( false );
	s_DownloadingPreview = !preview.empty();
	s_Worker = std::thread( FetchWorkshopItem, s_ActiveId, preview );
}

void FinishWorkshopRequest( lua_State *state, const WorkshopInfo *info )
{
	auto entry = s_Callbacks.find( s_ActiveId );
	std::vector<int> callbacks;
	callbacks.swap( entry->second );
	s_Callbacks.erase( entry );
	s_ActiveId.clear();
	for ( int callback : callbacks )
	{
		if ( state != LGameUI ) break;
		lua_rawgeti( state, LUA_REGISTRYINDEX, callback );
		luaL_unref( state, LUA_REGISTRYINDEX, callback );
		PushWorkshopInfo( state, info );
		luasrc_pcall( state, 1, 0, 0 );
	}
}
}

int HL2SBWorkshopInfo( lua_State *state )
{
	size_t length;
	const char *id = luaL_checklstring( state, 1, &length );
	luaL_checktype( state, 2, LUA_TFUNCTION );
	if ( length > 20 || length != strlen( id ) ||
		!HL2SB_HasLocalWorkshopContent( engine->GetGameDirectory(), id ) )
	{
		lua_pushvalue( state, 2 );
		lua_pushnil( state );
		luasrc_pcall( state, 1, 0, 0 );
		return 0;
	}

	WorkshopInfo cached;
	time_t modified = 0;
	const bool available = ReadCachedInfo( id, cached, modified );
	const time_t now = std::time( NULL );
	const auto retry = s_RetryAfter.find( id );
	const bool waiting = retry != s_RetryAfter.end() && now < retry->second;
	const auto pending = s_Callbacks.find( id );
	const bool full = pending == s_Callbacks.end() ? s_Callbacks.size() >= kMaxPending : pending->second.size() >= 12;
	const bool fresh = available && modified > 0 && modified <= now && now - modified < kCacheTtl &&
		( !cached.preview.empty() || cached.previewUrl.empty() );
#ifdef _WIN32
	const bool canFetch = true;
#else
	const bool canFetch = false;
#endif
	if ( available || waiting || full || !canFetch )
	{
		// Show a cached item immediately, even during an outage. Refresh the
		// data for the next menu query without keeping a callback waiting.
		if ( !fresh && !waiting && !full && canFetch && pending == s_Callbacks.end() )
		{
			s_Callbacks[id];
			s_Queue.push_back( id );
		}
		lua_pushvalue( state, 2 );
		PushWorkshopInfo( state, available ? &cached : NULL );
		luasrc_pcall( state, 1, 0, 0 );
		return 0;
	}

	if ( pending == s_Callbacks.end() ) s_Queue.push_back( id );
	lua_pushvalue( state, 2 );
	s_Callbacks[id].push_back( luaL_ref( state, LUA_REGISTRYINDEX ) );
	return 0;
}

void HL2SB_MenuPumpWorkshop( lua_State *state )
{
	if ( s_Worker.joinable() )
	{
		if ( !s_Complete.load( std::memory_order_acquire ) ) return;
		s_Worker.join();
		const std::string directory = CacheDirectory( s_ActiveId );
		if ( s_DownloadingPreview )
		{
			const char *extension = PreviewExtension( s_Response );
			if ( extension )
			{
				const std::string preview = directory + "/preview" + extension;
				if ( WriteCache( preview, s_Response ) )
				{
					// A file type change must not leave an earlier image preferred.
					const char *oldName = V_strcmp( extension, ".png" ) == 0 ? "/preview.jpg" : "/preview.png";
					filesystem->RemoveFile( ( directory + oldName ).c_str(), kWorkshopCachePathID );
					s_ActiveInfo.preview = preview;
				}
			}
			if ( s_ActiveInfo.preview.empty() ) s_RetryAfter[s_ActiveId] = std::time( NULL ) + kFailureRetryDelay;
			FinishWorkshopRequest( state, &s_ActiveInfo );
		}
		else
		{
			s_ActiveInfo = WorkshopInfo();
			if ( ParseWorkshopDetails( s_ActiveId, s_Response, s_ActiveInfo ) )
			{
				filesystem->CreateDirHierarchy( directory.c_str(), kWorkshopCachePathID );
				WriteCache( directory + "/details.json", s_Response );
				s_ActiveInfo.preview = CachedPreview( directory );
				if ( !s_ActiveInfo.previewUrl.empty() )
				{
					StartWorkshopRequest( s_ActiveInfo.previewUrl );
					return;
				}
				FinishWorkshopRequest( state, &s_ActiveInfo );
			}
			else
			{
				time_t modified;
				const bool cached = ReadCachedInfo( s_ActiveId, s_ActiveInfo, modified );
				s_RetryAfter[s_ActiveId] = std::time( NULL ) + kFailureRetryDelay;
				FinishWorkshopRequest( state, cached ? &s_ActiveInfo : NULL );
			}
		}
	}
	if ( state == LGameUI && !s_Queue.empty() )
	{
		s_ActiveId = s_Queue.front();
		s_Queue.pop_front();
		StartWorkshopRequest( "" );
	}
}

void HL2SB_MenuStopWorkshop( lua_State *state )
{
	s_Stopping.store( true );
	if ( s_Worker.joinable() ) s_Worker.join();
	if ( state )
	{
		for ( const auto &request : s_Callbacks )
			for ( int callback : request.second )
				luaL_unref( state, LUA_REGISTRYINDEX, callback );
	}
	s_Callbacks.clear();
	s_Queue.clear();
	s_RetryAfter.clear();
	s_ActiveId.clear();
	s_Response.clear();
	s_ActiveInfo = WorkshopInfo();
	s_Stopping.store( false );
}
#endif

//=============================================================================//
//
// HL2SB: the main-menu (GameUI) Lua realm - a small library of its own.
//
// WHY THIS FILE EXISTS
//
//   The mod's main menu is a hand-opened lua_State (luamanager.cpp: luasrc_init_gameui,
//   "LGameUI").  Dialogs are built there out of the same scripted panel classes the game
//   realm uses (LPanel / LFrame / LLabel / LButton / LCheckButton / LTextEntry), but the
//   menu realm has no frame loop of its own that would run vgui's layout pass the way the
//   in-game one does:
//
//     [HL2SB] addonsdialog dbg:   1..14 Panel/Button/FrameSystemButton pos=(0,0) size=64x24
//     [HL2SB] addonsdialog dbg:   15 LLabel      pos=(0,0) size=442x36
//     [HL2SB] addonsdialog dbg:   19 LCheckButton pos=(0,0) size=442x26
//
//   SetSize() lands immediately, SetPos() does not - it is applied by Panel::PerformLayout,
//   which nothing calls in this realm, so every control (and even the frame's own title bar
//   and FrameSystemButtons) stays at (0,0) and the dialogs come up as an overlapping mess.
//
// WHAT GARRY'S MOD DOES
//
//   GMod's menu Lua is part of its own vgui library: the panel classes are vgui's, the
//   menu pulls the GMod Lua stack in through the regular loader, and the engine's paint
//   traversal (which calls PerformLayout when a panel's layout is invalid) covers it.  The
//   scripted panel classes of this fork *do* dispatch PerformLayout to Lua
//   (lPanel.cpp:397, lFrame.cpp:77, lLabel.h:116, lTextEntry.h:122, lModelPanel.cpp:331) -
//   the pass simply never runs here, which is the whole bug.  This file is that missing
//   pass, exposed to the menu realm in the same "one function per job, GMod naming"
//   style: the Lua side calls HL2SB_MenuLayout( panel ) after opening a dialog.
//
// CONTRACT
//
//   HL2SB_MenuLayout( panel )        - run the layout pass over that panel and its whole
//                                      subtree, immediately (recursively, depth first, so
//                                      children of a resized child are laid out too).
//                                      Returns the number of panels visited.
//   HL2SB_MenuDumpLayout( panel )    - log every panel in the subtree (class, pos, size,
//                                      visible, layout-invalid) - the diagnostic that pins
//                                      a layout problem without guessing.
//
//=============================================================================//

#include "cbase.h"
#include "hl2sb_menu_lifecycle.h"
#ifdef LUA_SDK
#include "luamanager.h"
#include "luasrclib.h"
// luaL_checkpanel / lua_Panel
#include "vgui_controls/lPanel.h"
#include "vgui_controls/Panel.h"
#include "vgui/IInput.h"
#include "vgui/IVGui.h"
#include "vgui_int.h"
#include "GameUI/IGameUI.h"
#include "GameUI/IGameConsole.h"
#include "ienginevgui.h"
#include "materialsystem/imaterialsystemhardwareconfig.h"
#include "inetchannelinfo.h"
#include "netadr.h"
#include "filesystem.h"
#include "utlbuffer.h"
#include "steam/steam_api.h"
#include "vgui/ISystem.h"
#include "tier0/icommandline.h"
#include <map>
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#undef ShellExecute
#undef GetClassName
#pragma comment(lib, "winhttp.lib")
#endif
#include "tier0/memdbgon.h"

extern CSteamAPIContext *steamapicontext;	// cdll_client_int.h
int HL2SBWorkshopInfo( lua_State *state );
void HL2SB_MenuPumpWorkshop( lua_State *state );
void HL2SB_MenuStopWorkshop( lua_State *state );

// The reference build always runs inside Steam, so its missing-interface
// Warnings are unreachable there; here the game can start without Steam and
// the menu calls serverlist favorites checks every frame, which would spam
// the log.  Warnings default off; turn on to diagnose Steam plumbing.
static ConVar hl2sb_serverlist_warnings( "hl2sb_serverlist_warnings", "0", FCVAR_ARCHIVE,
	"Print serverlist warnings when Steam interfaces are missing." );

struct HL2SBMenuTimer
{
	double fireTime;
	int functionRef;
};

static std::vector<HL2SBMenuTimer> s_MenuSimpleTimers;

struct MenuNamedTimer
{
	double fireTime;
	float delay;
	int repetitions;
	int functionRef;
	bool removed;
};

static std::map<std::string, MenuNamedTimer> s_MenuNamedTimers;
static CDllDemandLoader s_MenuGameUI( "GameUI" );
static bool s_MenuOverrideActive = false;
static bool s_OpeningCrosshairSetup = false;
static vgui::DHANDLE<vgui::Panel> s_CrosshairSetupFrame;

void HL2SB_MenuTrackTransientPanel( vgui::Panel *panel, lua_State *state )
{
	if ( state == LGameUI && s_OpeningCrosshairSetup && !s_CrosshairSetupFrame.Get() )
		s_CrosshairSetupFrame = panel;
}

static void HL2SB_CloseCrosshairSetup()
{
	vgui::Panel *panel = s_CrosshairSetupFrame.Get();
	s_CrosshairSetupFrame = NULL;
	if ( !panel )
		return;

	// Hide synchronously: a loading or disconnect frame can paint before
	// VGUI processes the deferred deletion of this frame and its popup children.
	panel->SetVisible( false );
	panel->MarkForDeletion();
}

class CHL2SBMenuLifecycle : public IHL2SBMenuLifecycle
{
public:
	void CloseTransientDialogs() override { HL2SB_CloseCrosshairSetup(); }
};
static CHL2SBMenuLifecycle s_MenuLifecycle;
EXPOSE_SINGLE_INTERFACE_GLOBALVAR( CHL2SBMenuLifecycle, IHL2SBMenuLifecycle,
	HL2SB_MENU_LIFECYCLE_VERSION, s_MenuLifecycle );

static std::map<std::string, unsigned int> s_MenuPermanentPermissions;
static std::map<std::string, unsigned int> s_MenuTemporaryPermissions;
static bool s_MenuPermissionsLoaded = false;

static int HL2SB_MenuPermissionIndex( const char *name )
{
	if ( !Q_strcmp( name, "connect" ) ) return 0;
	if ( !Q_strcmp( name, "voicerecord" ) ) return 1;
	if ( !Q_strcmp( name, "motionsensor" ) ) return 6;
	return -1;
}

static const char *HL2SB_MenuCurrentServer()
{
	INetChannelInfo *channel = engine->GetNetChannelInfo();
	return channel ? channel->GetAddress() : NULL;
}

static void HL2SB_MenuLoadPermissions()
{
	if ( s_MenuPermissionsLoaded ) return;
	s_MenuPermissionsLoaded = true;
	CUtlBuffer data;
	if ( !filesystem->ReadFile( "settings/permissions.bin", "MOD", data ) ) return;
	const unsigned char *bytes = static_cast<const unsigned char *>( data.Base() );
	const size_t size = static_cast<size_t>( data.TellPut() );
	if ( !bytes || size < 4 ) return;
	const unsigned count = unsigned(bytes[0]) | (unsigned(bytes[1]) << 8) | (unsigned(bytes[2]) << 16) | (unsigned(bytes[3]) << 24);
	if ( count > 65536 ) return;
	size_t position = 4;
	std::map<std::string, unsigned int> loaded;
	for ( unsigned i = 0; i < count && position < size; ++i ) {
		const size_t start = position;
		while ( position < size && bytes[position] && position - start < 255 ) ++position;
		if ( position >= size || bytes[position] || position == start || position - start >= 255 ) break;
		std::string address( reinterpret_cast<const char *>( bytes + start ), position - start );
		++position;
		if ( size - position < 4 ) break;
		const unsigned flags = unsigned(bytes[position]) | (unsigned(bytes[position + 1]) << 8) |
			(unsigned(bytes[position + 2]) << 16) | (unsigned(bytes[position + 3]) << 24);
		position += 4;
		loaded[address] = flags;
	}
	s_MenuPermanentPermissions.swap( loaded );
}

static void HL2SB_MenuSavePermissions()
{
	CUtlBuffer data( 0, 0, 0 );
	unsigned count = 0;
	for ( const auto &entry : s_MenuPermanentPermissions )
		if ( entry.second && entry.first != "loopback" ) ++count;
	data.PutInt( count );
	for ( const auto &entry : s_MenuPermanentPermissions ) {
		if ( !entry.second || entry.first == "loopback" ) continue;
		data.PutString( entry.first.c_str() );
		data.PutInt( entry.second );
	}
	filesystem->CreateDirHierarchy( "settings", "MOD" );
	if ( !filesystem->WriteFile( "settings/permissions.bin", "MOD", data ) )
		Warning( "Unable to save menu permissions.\n" );
}

static void HL2SB_PushPermissions( lua_State *state, const std::map<std::string, unsigned int> &entries )
{
	static const char *const names[] = { "connect", "voicerecord", "workshopdownload", "javascript", "escapemenu", "filewrite", "motionsensor" };
	lua_newtable( state );
	for ( const auto &entry : entries ) {
		std::string value;
		for ( unsigned i = 0; i < ARRAYSIZE(names); ++i ) {
			if ( !(entry.second & (1u << i)) ) continue;
			if ( !value.empty() ) value += ',';
			value += names[i];
		}
		if ( !value.empty() ) { lua_pushlstring( state, value.data(), value.size() ); lua_setfield( state, -2, entry.first.c_str() ); }
	}
}

static int HL2SB_PermissionsGetAll( lua_State *state )
{
	HL2SB_MenuLoadPermissions();
	lua_newtable( state );
	HL2SB_PushPermissions( state, s_MenuPermanentPermissions ); lua_setfield( state, -2, "permanent" );
	HL2SB_PushPermissions( state, s_MenuTemporaryPermissions ); lua_setfield( state, -2, "temporary" );
	return 1;
}

static void HL2SB_MenuCallHook( const char *name );

static int HL2SB_PermissionsGrant( lua_State *state )
{
	const char *name = luaL_checkstring( state, 1 );
	const char *address = HL2SB_MenuCurrentServer();
	if ( !address || !address[0] ) return 0;
	const int index = HL2SB_MenuPermissionIndex( name );
	if ( index < 0 ) return luaL_error( state, "Invalid permission '%s'!", name );
	HL2SB_MenuLoadPermissions();
	const bool temporary = lua_toboolean( state, 2 ) != 0;
	std::map<std::string, unsigned int> &entries = temporary ? s_MenuTemporaryPermissions : s_MenuPermanentPermissions;
	entries[address] |= 1u << index;
	if ( !temporary ) HL2SB_MenuSavePermissions();
	HL2SB_MenuCallHook( "OnPermissionsChanged" );
	return 0;
}

static int HL2SB_PermissionsRevoke( lua_State *state )
{
	const char *name = luaL_checkstring( state, 1 );
	const char *address = luaL_optstring( state, 2, NULL );
	if ( !address ) address = HL2SB_MenuCurrentServer();
	if ( !address || !address[0] ) return 0;
	const int index = HL2SB_MenuPermissionIndex( name );
	if ( index < 0 ) return luaL_error( state, "Invalid permission '%s'!", name );
	HL2SB_MenuLoadPermissions();
	const unsigned mask = ~(1u << index);
	s_MenuPermanentPermissions[address] &= mask;
	s_MenuTemporaryPermissions[address] &= mask;
	HL2SB_MenuSavePermissions();
	HL2SB_MenuCallHook( "OnPermissionsChanged" );
	return 0;
}

static int HL2SB_PermissionsIsGranted( lua_State *state )
{
	const char *name = luaL_checkstring( state, 1 );
	const char *address = HL2SB_MenuCurrentServer();
	if ( !address || !address[0] ) { lua_pushboolean( state, false ); return 1; }
	const int index = HL2SB_MenuPermissionIndex( name );
	if ( index < 0 ) return luaL_error( state, "Invalid permission '%d'!", 8 );
	HL2SB_MenuLoadPermissions();
	const unsigned flags = s_MenuPermanentPermissions[address] | s_MenuTemporaryPermissions[address];
	lua_pushboolean( state, (flags & (1u << index)) != 0 );
	return 1;
}

static int HL2SB_PermissionsConnect( lua_State *state )
{
	std::string address = luaL_checkstring( state, 1 );
	const size_t first = address.find_first_not_of( " \t;" );
	if ( first == std::string::npos ) return 0;
	const size_t last = address.find_last_not_of( " \t;" );
	address = address.substr( first, last - first + 1 );
	if ( address.find_first_of( ";\r\n\"" ) != std::string::npos || address.size() > 200 ) return 0;
	char command[256];
	Q_snprintf( command, sizeof(command), "connect %s serverbrowser\n", address.c_str() );
	engine->ClientCmd_Unrestricted( command );
	s_MenuTemporaryPermissions[address] |= 1u;
	return 0;
}

static IGameUI *HL2SB_GetMenuGameUI()
{
	CreateInterfaceFn factory = s_MenuGameUI.GetFactory();
	return factory ? static_cast<IGameUI *>( factory( GAMEUI_INTERFACE_VERSION, NULL ) ) : NULL;
}

static void HL2SB_ClearMenuOverride()
{
	if ( !s_MenuOverrideActive ) return;
	IGameUI *gameUI = HL2SB_GetMenuGameUI();
	if ( gameUI ) gameUI->SetMainMenuOverride( NULL );
	s_MenuOverrideActive = false;
}

static int HL2SB_SetMenuOverride( lua_State *state )
{
	vgui::Panel *panel = luaL_checkpanel( state, 1 );
	IGameUI *gameUI = HL2SB_GetMenuGameUI();
	if ( !panel || !gameUI || CommandLine()->HasParm( "-mainmenusource" ) )
	{
		lua_pushboolean( state, false );
		return 1;
	}
	gameUI->SetMainMenuOverride( panel->GetVPanel() );
	s_MenuOverrideActive = true;
	lua_pushboolean( state, true );
	return 1;
}

static int HL2SB_RestoreMenuOverride( lua_State *state )
{
	HL2SB_ClearMenuOverride();
	return 0;
}

static int HL2SB_MenuIsInGame( lua_State *state )
{
	lua_pushboolean( state, engine->IsInGame() );
	return 1;
}

static int HL2SB_MenuCanAddServerToFavorites( lua_State *state )
{
	// Reference behavior (menusystem binding, old reference build): true only
	// while connected to a real remote server - a net channel that is not
	// timing out and whose address parses to a valid, non-loopback,
	// non-localhost IP. The main menu has no net channel, so this is false
	// there and the favorites button stays hidden.
	bool can = false;
	INetChannelInfo *net = engine->GetNetChannelInfo();
	if ( net != NULL && !net->IsTimingOut() )
	{
		netadr_t adr;
		adr.SetFromString( net->GetAddress(), false );
		if ( adr.IsValid() && !adr.IsLoopback() && !adr.IsLocalhost() )
			can = true;
	}
	lua_pushboolean( state, can ? 1 : 0 );
	return 1;
}

//-----------------------------------------------------------------------------
// serverlist: Steam favorites.  Reference behavior (menusystem library, old
// reference build): favorites live in the Steam client's own config and are
// only reached through ISteamMatchmaking; the library keeps a 10 second
// memory cache of this app's favorite server addresses (ip + port packed
// into one 64 bit key) and re-enumerates on a cold or expired cache.  A
// missing Steam interface is never a Lua error - the reference only prints
// a Warning and answers false, and add/remove silently refuse.
//-----------------------------------------------------------------------------

static CUtlVector<uint64> s_vecFavoriteServers;	// (port << 32) | ip keys
static double s_flFavoritesRefreshTime = -1.0;

static void HL2SB_RefreshFavorites( ISteamMatchmaking *matchmaking, ISteamUtils *utils )
{
	double now = Plat_FloatTime();
	if ( s_flFavoritesRefreshTime > 0 && now < s_flFavoritesRefreshTime + 10.0 && s_vecFavoriteServers.Count() > 0 )
		return;

	s_vecFavoriteServers.Purge();
	s_flFavoritesRefreshTime = now;
	if ( matchmaking == NULL )
		return;

	uint32 unOwnAppID = ( utils != NULL ) ? utils->GetAppID() : 0;
	int nCount = matchmaking->GetFavoriteGameCount();
	for ( int i = 0; i < nCount; ++i )
	{
		AppId_t appID = 0; uint32 unIP = 0; uint16 connPort = 0, queryPort = 0;
		uint32 flags = 0, lastPlayed = 0;
		if ( !matchmaking->GetFavoriteGame( i, &appID, &unIP, &connPort, &queryPort, &flags, &lastPlayed ) )
			continue;
		if ( appID != unOwnAppID && appID != 0 ) continue;
		if ( !( flags & k_unFavoriteFlagFavorite ) ) continue;
		s_vecFavoriteServers.AddToTail( ( (uint64)connPort << 32 ) | (uint64)unIP );
	}
}

static void HL2SB_InvalidateFavorites()
{
	s_flFavoritesRefreshTime = -1.0;
	s_vecFavoriteServers.Purge();
}

static uint64 HL2SB_FavoriteKey( const netadr_t &adr )
{
	return ( (uint64)adr.GetPort() << 32 ) | (uint64)adr.GetIPHostByteOrder();
}

static int HL2SB_MenuIsServerFavorite( lua_State *state )
{
	const char *address = luaL_checkstring( state, 1 );
	bool favorite = false;
	ISteamMatchmaking *matchmaking = ( steamapicontext != NULL ) ? steamapicontext->SteamMatchmaking() : NULL;
	ISteamUtils *utils = ( steamapicontext != NULL ) ? steamapicontext->SteamUtils() : NULL;
	if ( matchmaking == NULL )
	{
		if ( hl2sb_serverlist_warnings.GetBool() )
			Warning( "IsServerFavorite: SteamMatchmaking does not exist!\n" );
	}
	else
	{
		HL2SB_RefreshFavorites( matchmaking, utils );
		netadr_t adr;
		adr.SetFromString( address, false );
		favorite = ( s_vecFavoriteServers.Find( HL2SB_FavoriteKey( adr ) ) != s_vecFavoriteServers.InvalidIndex() );
	}
	lua_pushboolean( state, favorite ? 1 : 0 );
	return 1;
}

static int HL2SB_MenuIsCurrentServerFavorite( lua_State *state )
{
	bool favorite = false;
	ISteamMatchmaking *matchmaking = ( steamapicontext != NULL ) ? steamapicontext->SteamMatchmaking() : NULL;
	ISteamUtils *utils = ( steamapicontext != NULL ) ? steamapicontext->SteamUtils() : NULL;
	if ( matchmaking == NULL || utils == NULL )
	{
		if ( hl2sb_serverlist_warnings.GetBool() )
			Warning( "IsCurrentServerFavorite: No SteamUtils or SteamMatchmaking!\n" );
	}
	else
	{
		INetChannelInfo *net = engine->GetNetChannelInfo();
		if ( net != NULL )
		{
			HL2SB_RefreshFavorites( matchmaking, utils );
			netadr_t adr;
			adr.SetFromString( net->GetAddress(), false );
			favorite = ( s_vecFavoriteServers.Find( HL2SB_FavoriteKey( adr ) ) != s_vecFavoriteServers.InvalidIndex() );
		}
	}
	lua_pushboolean( state, favorite ? 1 : 0 );
	return 1;
}

// Shared add/remove body: the reference's add and remove paths are identical
// apart from the Steam call - both require the matchmaking and utils
// interfaces, resolve the address without DNS, pass the connection port as
// both connection and query port with the favorite flag, and refresh the
// cache right away so the next Is* reads the new state.
static void HL2SB_ModifyServerFavorites( const char *address, bool add, const char *failureWarning )
{
	ISteamMatchmaking *matchmaking = ( steamapicontext != NULL ) ? steamapicontext->SteamMatchmaking() : NULL;
	ISteamUtils *utils = ( steamapicontext != NULL ) ? steamapicontext->SteamUtils() : NULL;
	if ( matchmaking == NULL || utils == NULL )
	{
		if ( hl2sb_serverlist_warnings.GetBool() )
			Warning( "%s", failureWarning );
		return;
	}

	netadr_t adr;
	adr.SetFromString( address, false );
	if ( !adr.IsValid() )
		return;

	uint32 unIP = adr.GetIPHostByteOrder();
	uint16 connPort = adr.GetPort();
	if ( add )
		matchmaking->AddFavoriteGame( utils->GetAppID(), unIP, connPort, connPort,
									  k_unFavoriteFlagFavorite, (uint32)time( NULL ) );
	else
		matchmaking->RemoveFavoriteGame( utils->GetAppID(), unIP, connPort, connPort,
										 k_unFavoriteFlagFavorite );

	HL2SB_InvalidateFavorites();
	HL2SB_RefreshFavorites( matchmaking, utils );
}

static int HL2SB_MenuAddServerToFavorites( lua_State *state )
{
	HL2SB_ModifyServerFavorites( luaL_checkstring( state, 1 ), true, "Failed to add server to favorites.\n" );
	return 0;
}

static int HL2SB_MenuRemoveServerFromFavorites( lua_State *state )
{
	HL2SB_ModifyServerFavorites( luaL_checkstring( state, 1 ), false, "Failed to remove server from favorites.\n" );
	return 0;
}

// serverlist.AddCurrentServerToFavorites( bFavorite ): true adds the server
// we are connected to, false removes it.  The menu page passes the flag
// explicitly in both directions.
static int HL2SB_MenuAddCurrentServerToFavorites( lua_State *state )
{
	bool add = lua_toboolean( state, 1 ) ? true : false;
	INetChannelInfo *net = engine->GetNetChannelInfo();
	if ( net == NULL )
		return 0;
	HL2SB_ModifyServerFavorites( net->GetAddress(), add, "Failed to add server to favorites.\n" );
	return 0;
}

// GLOBAL.OpenFolder: reference behavior opens exactly saves/demos/dupes
// from the game dir with the system shell; any other name is silently
// ignored.
static int HL2SB_MenuOpenFolder( lua_State *state )
{
	const char *folder = luaL_checkstring( state, 1 );
	if ( Q_strcmp( folder, "saves" ) != 0 && Q_strcmp( folder, "demos" ) != 0 && Q_strcmp( folder, "dupes" ) != 0 )
		return 0;

	char path[MAX_PATH];
	if ( !filesystem->RelativePathToFullPath( folder, "MOD", path, sizeof( path ) ) )
		return 0;

	vgui::system()->ShellExecute( "open", path );
	return 0;
}

static int HL2SB_MenuIsInLoading( lua_State *state )
{
	lua_pushboolean( state, engine->IsDrawingLoadingImage() );
	return 1;
}

static int HL2SB_RunGameUICommand( lua_State *state )
{
	const char *command = luaL_checkstring( state, 1 );
	IGameUI *gameUI = HL2SB_GetMenuGameUI();
	if ( gameUI ) gameUI->SendMainMenuCommand( command );
	return 0;
}

static std::thread s_ManifestThread;
static std::mutex s_ManifestMutex;
static std::atomic<bool> s_ManifestFinished( false );
static std::string s_ManifestResponse;
static std::vector<int> s_ManifestCallbacks;
static bool s_ManifestRunning = false;
static const char *const kManifestCache = "settings/gmod_manifest.json";

static void HL2SB_FetchManifest()
{
	std::string body;
#ifdef _WIN32
	HINTERNET session = WinHttpOpen( L"HL2SB", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
		WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0 );
	if ( session )
	{
		WinHttpSetTimeouts( session, 5000, 5000, 5000, 5000 );
		HINTERNET connection = WinHttpConnect( session, L"api.facepunch.com", INTERNET_DEFAULT_HTTPS_PORT, 0 );
		if ( connection )
		{
			HINTERNET request = WinHttpOpenRequest( connection, L"GET",
				L"/api/public/manifest?public_key=RWsOQQrO860EaGY3qPsSsBQSev3gNO0KrcF3kv4Rl5frjE9OuUKQgAsRutxMZ4aU",
				NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE );
			if ( request )
			{
				DWORD status = 0, statusSize = sizeof( status );
				if ( WinHttpSendRequest( request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0 ) &&
					 WinHttpReceiveResponse( request, NULL ) &&
					 WinHttpQueryHeaders( request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
						WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX ) && status == 200 )
				{
					char buffer[8192]; DWORD count = 0; bool ok = true;
					do {
						if ( !WinHttpReadData( request, buffer, sizeof( buffer ), &count ) || body.size() + count > 2 * 1024 * 1024 ) { ok = false; break; }
						body.append( buffer, count );
					} while ( count );
					if ( !ok ) body.clear();
				}
				WinHttpCloseHandle( request );
			}
			WinHttpCloseHandle( connection );
		}
		WinHttpCloseHandle( session );
	}
#endif
	{ std::lock_guard<std::mutex> lock( s_ManifestMutex ); s_ManifestResponse.swap( body ); }
	s_ManifestFinished.store( true );
}

static bool HL2SB_ManifestValid( lua_State *state, const std::string &body )
{
	const int top = lua_gettop( state );
	lua_getglobal( state, "util" );
	if ( !lua_istable( state, -1 ) ) { lua_settop( state, top ); return false; }
	lua_getfield( state, -1, "JSONToTable" );
	if ( !lua_isfunction( state, -1 ) ) { lua_settop( state, top ); return false; }
	lua_pushlstring( state, body.data(), body.size() );
	bool valid = lua_pcall( state, 1, 1, 0 ) == LUA_OK && lua_istable( state, -1 );
	if ( valid ) {
		lua_getfield( state, -1, "News" ); valid = lua_istable( state, -1 );
		if ( valid ) { lua_getfield( state, -1, "Blogs" ); valid = lua_istable( state, -1 ); lua_pop( state, 1 ); }
		lua_pop( state, 1 );
	}
	lua_settop( state, top );
	return valid;
}

static void HL2SB_PumpManifest( lua_State *state )
{
	if ( !s_ManifestRunning || !s_ManifestFinished.load() ) return;
	if ( s_ManifestThread.joinable() ) s_ManifestThread.join();
	s_ManifestRunning = false;
	std::string body;
	{ std::lock_guard<std::mutex> lock( s_ManifestMutex ); body.swap( s_ManifestResponse ); }
	if ( HL2SB_ManifestValid( state, body ) ) {
		Msg( "GetAPIManifest: loaded online manifest.\n" );
		filesystem->CreateDirHierarchy( "settings", "MOD" );
		CUtlBuffer data( 0, 0, 0 ); data.Put( body.data(), body.size() );
		if ( !filesystem->WriteFile( kManifestCache, "MOD", data ) ) Warning( "GetAPIManifest: could not write cache.\n" );
	} else {
		body.clear(); CUtlBuffer data;
		if ( filesystem->ReadFile( kManifestCache, "MOD", data ) && data.TellPut() <= 2 * 1024 * 1024 )
			body.assign( static_cast<const char *>( data.Base() ), data.TellPut() );
		if ( !HL2SB_ManifestValid( state, body ) ) {
			body.clear();
			Warning( "GetAPIManifest: online request failed and no valid cache exists.\n" );
		} else Msg( "GetAPIManifest: using cached manifest.\n" );
	}
	std::vector<int> callbacks; callbacks.swap( s_ManifestCallbacks );
	for ( size_t i = 0; i < callbacks.size(); ++i ) {
		lua_rawgeti( state, LUA_REGISTRYINDEX, callbacks[i] );
		luaL_unref( state, LUA_REGISTRYINDEX, callbacks[i] );
		if ( !body.empty() && lua_isfunction( state, -1 ) ) {
			lua_pushlstring( state, body.data(), body.size() ); luasrc_pcall( state, 1, 0, 0 );
		} else lua_pop( state, 1 );
	}
}

static int HL2SB_GetAPIManifest( lua_State *state )
{
	luaL_checktype( state, 1, LUA_TFUNCTION );
	lua_pushvalue( state, 1 );
	s_ManifestCallbacks.push_back( luaL_ref( state, LUA_REGISTRYINDEX ) );
	if ( !s_ManifestRunning ) {
		s_ManifestFinished.store( false ); s_ManifestRunning = true;
		s_ManifestThread = std::thread( HL2SB_FetchManifest );
	}
	return 0;
}

static int HL2SB_MenuGuiActivate( lua_State *state )
{
	if ( !enginevgui || !enginevgui->IsGameUIVisible() )
		engine->ClientCmd_Unrestricted( "gameui_activate\n" );
	return 0;
}

static int HL2SB_MenuGuiHide( lua_State *state )
{
	engine->ClientCmd_Unrestricted( "gameui_hide\n" );
	return 0;
}

static int HL2SB_MenuGuiShowConsole( lua_State *state )
{
	CreateInterfaceFn factory = s_MenuGameUI.GetFactory();
	IGameConsole *console = factory ? static_cast<IGameConsole *>( factory( GAMECONSOLE_INTERFACE_VERSION, NULL ) ) : NULL;
	if ( console ) console->Activate();
	return 0;
}

static int HL2SB_MenuGuiIsConsoleVisible( lua_State *state )
{
	CreateInterfaceFn factory = s_MenuGameUI.GetFactory();
	IGameConsole *console = factory ? static_cast<IGameConsole *>( factory( GAMECONSOLE_INTERFACE_VERSION, NULL ) ) : NULL;
	lua_pushboolean( state, console && console->IsConsoleVisible() );
	return 1;
}

static int HL2SB_MotionSensorUnavailable( lua_State *state )
{
	lua_pushboolean( state, false );
	return 1;
}

static int HL2SB_MenuRenderDXLevel( lua_State *state )
{
	lua_pushnumber( state, g_pMaterialSystemHardwareConfig ? g_pMaterialSystemHardwareConfig->GetDXSupportLevel() : 0 );
	return 1;
}

static int HL2SB_MenuRenderSupportsHDR( lua_State *state )
{
	lua_pushboolean( state, g_pMaterialSystemHardwareConfig && g_pMaterialSystemHardwareConfig->GetHardwareHDRType() != HDR_TYPE_NONE );
	return 1;
}

static int HL2SB_MenuRenderSupportsPS14( lua_State *state )
{
	lua_pushboolean( state, g_pMaterialSystemHardwareConfig && g_pMaterialSystemHardwareConfig->SupportsPixelShaders_1_4() );
	return 1;
}

static int HL2SB_MenuRenderSupportsPS20( lua_State *state )
{
	lua_pushboolean( state, g_pMaterialSystemHardwareConfig && g_pMaterialSystemHardwareConfig->SupportsPixelShaders_2_0() );
	return 1;
}

static int HL2SB_MenuRenderSupportsVS20( lua_State *state )
{
	lua_pushboolean( state, g_pMaterialSystemHardwareConfig && g_pMaterialSystemHardwareConfig->SupportsVertexShaders_2_0() );
	return 1;
}

CON_COMMAND( hl2sb_restore_source_menu, "Restore the Source main menu" )
{
	HL2SB_ClearMenuOverride();
}
class CHL2SBMenuTick;
static CHL2SBMenuTick *s_pMenuTick = NULL;

static int HL2SB_MenuTimerSimple( lua_State *state )
{
	const float delay = static_cast<float>( luaL_checknumber( state, 1 ) );
	luaL_checktype( state, 2, LUA_TFUNCTION );
	lua_pushvalue( state, 2 );
	HL2SBMenuTimer timer = { static_cast<double>( gpGlobals->realtime ) + delay,
		luaL_ref( state, LUA_REGISTRYINDEX ) };
	s_MenuSimpleTimers.push_back( timer );
	return 0;
}

static int MenuTimerCreate( lua_State *state )
{
	const char *name = luaL_checkstring( state, 1 );
	const float delay = static_cast<float>( luaL_checknumber( state, 2 ) );
	const int repetitions = luaL_checkint( state, 3 );
	luaL_checktype( state, 4, LUA_TFUNCTION );
	lua_pushvalue( state, 4 );
	const int ref = luaL_ref( state, LUA_REGISTRYINDEX );

	const MenuNamedTimer timer = { static_cast<double>( gpGlobals->realtime ) + delay, delay,
		repetitions > 0 ? repetitions : -1, ref, false };
	auto current = s_MenuNamedTimers.find( name );
	if ( current != s_MenuNamedTimers.end() )
	{
		luaL_unref( state, LUA_REGISTRYINDEX, current->second.functionRef );
		current->second = timer;
	}
	else
		s_MenuNamedTimers.emplace( name, timer );
	return 0;
}

static int MenuTimerExists( lua_State *state )
{
	const auto timer = s_MenuNamedTimers.find( luaL_checkstring( state, 1 ) );
	lua_pushboolean( state, timer != s_MenuNamedTimers.end() && !timer->second.removed );
	return 1;
}

static int MenuTimerRemove( lua_State *state )
{
	const auto timer = s_MenuNamedTimers.find( luaL_checkstring( state, 1 ) );
	if ( timer != s_MenuNamedTimers.end() )
		timer->second.removed = true;
	return 0;
}

void HL2SB_MenuInstallTimer( lua_State *state )
{
	static const luaL_Reg functions[] =
	{
		{ "Create", MenuTimerCreate },
		{ "Exists", MenuTimerExists },
		{ "Remove", MenuTimerRemove },
		{ "Simple", HL2SB_MenuTimerSimple },
		{ NULL, NULL }
	};
	luaL_register( state, "timer", functions );
	lua_pop( state, 1 );
}

static void MenuRunNamedTimers( lua_State *state, double now )
{
	// The reference walks names in order. Create updates a node in place and
	// Remove only marks it, so Lua callbacks cannot invalidate the iterator.
	for ( auto it = s_MenuNamedTimers.begin(); it != s_MenuNamedTimers.end(); )
	{
		MenuNamedTimer &timer = it->second;
		if ( !timer.removed && timer.fireTime <= now )
		{
			timer.fireTime += timer.delay;
			if ( timer.repetitions > 0 )
				--timer.repetitions;
			lua_rawgeti( state, LUA_REGISTRYINDEX, timer.functionRef );
			const bool failed = luasrc_pcall( state, 0, 0, 0 ) != 0;
			if ( LGameUI != state )
				return;
			if ( failed )
				timer.removed = true;
		}

		if ( timer.removed || timer.repetitions == 0 )
		{
			luaL_unref( state, LUA_REGISTRYINDEX, timer.functionRef );
			it = s_MenuNamedTimers.erase( it );
		}
		else
			++it;
	}
}

static void HL2SB_MenuCallHook( const char *name )
{
	if ( !LGameUI ) return;
	const int top = lua_gettop( LGameUI );
	lua_getglobal( LGameUI, "hook" );
	if ( lua_istable( LGameUI, -1 ) )
	{
		lua_getfield( LGameUI, -1, "Call" );
		if ( lua_isfunction( LGameUI, -1 ) )
		{
			lua_pushstring( LGameUI, name );
			luasrc_pcall( LGameUI, 1, 0, 0 );
		}
	}
	lua_settop( LGameUI, top );
}

class CHL2SBMenuTick : public vgui::Panel
{
public:
	CHL2SBMenuTick() : vgui::Panel( VGui_GetGameUIPanel(), "HL2SBMenuTick" ), m_bInTick( false )
	{
		SetVisible( false );
		vgui::ivgui()->AddTickSignal( GetVPanel(), 0 );
	}
	virtual ~CHL2SBMenuTick() { if ( s_pMenuTick == this ) s_pMenuTick = NULL; }
	virtual void OnTick()
	{
		if ( !LGameUI || m_bInTick ) return;
		m_bInTick = true;
		HL2SB_PumpManifest( LGameUI );
		HL2SB_MenuPumpWorkshop( LGameUI );
		size_t remaining = s_MenuSimpleTimers.size();
		size_t index = 0;
		const double now = gpGlobals->realtime;
		while ( LGameUI && remaining-- && index < s_MenuSimpleTimers.size() )
		{
			if ( s_MenuSimpleTimers[index].fireTime > now ) { ++index; continue; }
			const int ref = s_MenuSimpleTimers[index].functionRef;
			s_MenuSimpleTimers.erase( s_MenuSimpleTimers.begin() + index );
			lua_rawgeti( LGameUI, LUA_REGISTRYINDEX, ref );
			luaL_unref( LGameUI, LUA_REGISTRYINDEX, ref );
			if ( lua_isfunction( LGameUI, -1 ) ) luasrc_pcall( LGameUI, 0, 0, 0 );
			else lua_pop( LGameUI, 1 );
		}
		if ( LGameUI )
			MenuRunNamedTimers( LGameUI, now );
		HL2SB_MenuCallHook( "Think" );
		m_bInTick = false;
	}
private:
	bool m_bInTick;
};

void HL2SB_MenuRuntimeStart()
{
	if ( !LGameUI || s_pMenuTick ) return;
	s_pMenuTick = new CHL2SBMenuTick();
	HL2SB_MenuCallHook( "MenuStart" );
}

void HL2SB_MenuShutdownTimers( lua_State *state )
{
	for ( const auto &timer : s_MenuSimpleTimers )
		luaL_unref( state, LUA_REGISTRYINDEX, timer.functionRef );
	s_MenuSimpleTimers.clear();

	for ( const auto &entry : s_MenuNamedTimers )
		luaL_unref( state, LUA_REGISTRYINDEX, entry.second.functionRef );
	s_MenuNamedTimers.clear();
}

void HL2SB_MenuRuntimeStop()
{
	HL2SB_CloseCrosshairSetup();
	HL2SB_MenuStopWorkshop( LGameUI );
	if ( s_ManifestThread.joinable() ) s_ManifestThread.join();
	if ( LGameUI ) for ( size_t i = 0; i < s_ManifestCallbacks.size(); ++i )
		luaL_unref( LGameUI, LUA_REGISTRYINDEX, s_ManifestCallbacks[i] );
	s_ManifestCallbacks.clear();
	s_ManifestRunning = false;
	s_ManifestFinished.store( false );
	HL2SB_ClearMenuOverride();
	if ( s_pMenuTick )
	{
		vgui::ivgui()->RemoveTickSignal( s_pMenuTick->GetVPanel() );
		delete s_pMenuTick;
		s_pMenuTick = NULL;
	}
	s_MenuTemporaryPermissions.clear();
	s_MenuPermanentPermissions.clear();
	s_MenuPermissionsLoaded = false;
}

//-----------------------------------------------------------------------------
// Purpose: depth-first walk of a panel tree.
//
//   For every panel it:
//     1. re-applies the geometry the panel's own Lua side recorded (x/y/w/h - the fields
//        lPanel.cpp keeps in sync when Lua calls SetPos/SetSize) through the NATIVE
//        setters, so a position that never reached the C++ panel is applied here;
//     2. runs the layout pass (InvalidateLayout( true ) + PerformLayout()).
//
//   Step 1 is what actually moves the menu controls: the dbg dump showed the Lua-side
//   SetSize landing but SetPos not (pos=(0,0) with the right sizes), i.e. the geometry Lua
//   handed the panels never made it into vgui in this realm.  Reading it back out of the
//   panel and setting it natively cannot be defeated by whatever the Lua path did.
//-----------------------------------------------------------------------------
static int HL2SB_LayoutSubtree( lua_State *L, vgui::Panel *pPanel, int nVisited, int nDepth )
{
	if ( pPanel == NULL || nDepth > 32 )
		return nVisited;

	++nVisited;

	// 1. native geometry from the panel's Lua handle.
	lua_pushpanel( L, pPanel );

	if ( lua_type( L, -1 ) == LUA_TUSERDATA || lua_istable( L, -1 ) )
	{
		lua_getfield( L, -1, "x" ); int nX = (int)lua_tointeger( L, -1 ); lua_pop( L, 1 );
		lua_getfield( L, -1, "y" ); int nY = (int)lua_tointeger( L, -1 ); lua_pop( L, 1 );
		lua_getfield( L, -1, "w" ); int nW = (int)lua_tointeger( L, -1 ); lua_pop( L, 1 );
		lua_getfield( L, -1, "h" ); int nH = (int)lua_tointeger( L, -1 ); lua_pop( L, 1 );

		if ( nW > 0 && nH > 0 )
			pPanel->SetSize( nW, nH );

		pPanel->SetPos( nX, nY );
	}

	lua_pop( L, 1 );

	// 2. the layout pass (the scripted classes hand this to the Lua PerformLayout).
	pPanel->InvalidateLayout( true );
	pPanel->PerformLayout();

	int nChildren = pPanel->GetChildCount();

	for ( int i = 0; i < nChildren; ++i )
		nVisited = HL2SB_LayoutSubtree( L, pPanel->GetChild( i ), nVisited, nDepth + 1 );

	return nVisited;
}

//-----------------------------------------------------------------------------
// Purpose: HL2SB_MenuLayout( panel ) -> number of panels laid out.
//-----------------------------------------------------------------------------
static int lua_HL2SB_MenuLayout( lua_State *L )
{
	if ( lua_isnoneornil( L, 1 ) )
	{
		lua_pushinteger( L, 0 );
		return 1;
	}

	vgui::Panel *pPanel = luaL_checkpanel( L, 1 );

	if ( pPanel == NULL )
	{
		lua_pushinteger( L, 0 );
		return 1;
	}

	lua_pushinteger( L, HL2SB_LayoutSubtree( L, pPanel, 0, 0 ) );
	return 1;
}

//-----------------------------------------------------------------------------
// Purpose: HL2SB_MenuDumpLayout( panel ) - one line per panel in the subtree.
//-----------------------------------------------------------------------------
static int lua_HL2SB_MenuDumpLayout( lua_State *L, vgui::Panel *pPanel, int nDepth )
{
	if ( pPanel == NULL || nDepth > 32 )
		return 0;

	int x, y;
	pPanel->GetPos( x, y );

	Msg( "[HL2SB] menu layout: %*s%s '%s' pos=(%d,%d) size=%dx%d visible=%d layoutInvalid=%d\n",
		nDepth * 2, "",
		pPanel->GetClassName(),
		pPanel->GetName(),
		x, y, pPanel->GetWide(), pPanel->GetTall(),
		pPanel->IsVisible() ? 1 : 0,
		pPanel->IsLayoutInvalid() ? 1 : 0 );

	int nCount = 1;

	for ( int i = 0; i < pPanel->GetChildCount(); ++i )
		nCount += lua_HL2SB_MenuDumpLayout( L, pPanel->GetChild( i ), nDepth + 1 );

	return nCount;
}

static int lua_HL2SB_MenuDumpLayout_Entry( lua_State *L )
{
	vgui::Panel *pPanel = luaL_checkpanel( L, 1 );

	lua_pushinteger( L, lua_HL2SB_MenuDumpLayout( L, pPanel, 0 ) );
	return 1;
}

//-----------------------------------------------------------------------------
// Purpose: the menu realm's way into the engine console.
//
//   The menu state opens no engine library (luamanager.cpp gives it only
//   QAngle/gpGlobals/input/Color/Vector/Files/Systems), and RunConsoleCommand's
//   Lua shim only reaches commands registered in the SAME Lua state - a server
//   Lua concommand such as gm_load is invisible from there.  The Addons dialog
//   already solves the same problem by talking straight to the mount layer;
//   the saves page needs the general shape, so this takes a command string,
//   strips everything that could chain or quote a second command, and hands
//   the line to the engine console.
//-----------------------------------------------------------------------------
static int lua_HL2SB_MenuConsoleCommand( lua_State *L )
{
	const char *pszCommand = luaL_checkstring( L, 1 );
	if ( pszCommand == NULL )
	{
		lua_pushboolean( L, 0 );
		return 1;
	}

	// Only what a save name / plain command needs; a ';', '"' or CR in a name
	// would otherwise let the menu run arbitrary console lines.
	char szClean[256];
	int nOut = 0;
	for ( const char *p = pszCommand; *p != '\0' && nOut < ( int )sizeof( szClean ) - 1; ++p )
	{
		char c = *p;
		bool bAllowed = ( c >= 'a' && c <= 'z' ) || ( c >= 'A' && c <= 'Z' ) ||
						( c >= '0' && c <= '9' ) || c == ' ' || c == '-' || c == '_' || c == '.';
		if ( bAllowed )
			szClean[nOut++] = c;
	}
	szClean[nOut] = '\0';

	if ( nOut == 0 )
	{
		lua_pushboolean( L, 0 );
		return 1;
	}

	engine->ClientCmd_Unrestricted( szClean );
	lua_pushboolean( L, 1 );
	return 1;
}

//-----------------------------------------------------------------------------
// Purpose: the menu realm's own library table.
//-----------------------------------------------------------------------------
static const luaL_Reg gameui_menu_funcs[] =
{
	{ "HL2SB_SetMenuOverride", HL2SB_SetMenuOverride },
	{ "HL2SB_RestoreMenuOverride", HL2SB_RestoreMenuOverride },
	{ "IsInGame", HL2SB_MenuIsInGame },
	{ "IsInLoading", HL2SB_MenuIsInLoading },
	{ "CanAddServerToFavorites", HL2SB_MenuCanAddServerToFavorites },
	{ "OpenFolder", HL2SB_MenuOpenFolder },
	{ "RunGameUICommand", HL2SB_RunGameUICommand },
	{ "GetAPIManifest", HL2SB_GetAPIManifest },
	{ "HL2SBWorkshopInfo", HL2SBWorkshopInfo },
	{ "HL2SB_MenuLayout",     lua_HL2SB_MenuLayout },
	{ "HL2SB_MenuDumpLayout", lua_HL2SB_MenuDumpLayout_Entry },
	{ "HL2SB_MenuConsoleCommand", lua_HL2SB_MenuConsoleCommand },
	{ NULL, NULL }
};

	LUALIB_API int luaopen_gameui_menu (lua_State *L)
{
	static const luaL_Reg serverlistFuncs[] = {
		{ "IsServerFavorite", HL2SB_MenuIsServerFavorite },
		{ "IsCurrentServerFavorite", HL2SB_MenuIsCurrentServerFavorite },
		{ "AddServerToFavorites", HL2SB_MenuAddServerToFavorites },
		{ "RemoveServerFromFavorites", HL2SB_MenuRemoveServerFromFavorites },
		{ "AddCurrentServerToFavorites", HL2SB_MenuAddCurrentServerToFavorites },
		{ NULL, NULL }
	};
	luaL_register( L, "serverlist", serverlistFuncs );
	lua_pop( L, 1 );
	static const luaL_Reg permissionFuncs[] = {
		{ "GetAll", HL2SB_PermissionsGetAll }, { "Grant", HL2SB_PermissionsGrant },
		{ "Revoke", HL2SB_PermissionsRevoke }, { "IsGranted", HL2SB_PermissionsIsGranted },
		{ "Connect", HL2SB_PermissionsConnect }, { NULL, NULL }
	};
	luaL_register( L, "permissions", permissionFuncs );
	lua_pop( L, 1 );
	lua_getglobal( L, "gui" );
	if ( lua_istable( L, -1 ) ) {
		lua_pushcfunction( L, HL2SB_MenuGuiActivate ); lua_setfield( L, -2, "ActivateGameUI" );
		lua_pushcfunction( L, HL2SB_MenuGuiHide ); lua_setfield( L, -2, "HideGameUI" );
		lua_pushcfunction( L, HL2SB_MenuGuiShowConsole ); lua_setfield( L, -2, "ShowConsole" );
		lua_pushcfunction( L, HL2SB_MenuGuiIsConsoleVisible ); lua_setfield( L, -2, "IsConsoleVisible" );
	}
	lua_pop( L, 1 );
	static const luaL_Reg menuRender[] = {
		{ "GetDXLevel", HL2SB_MenuRenderDXLevel },
		{ "SupportsHDR", HL2SB_MenuRenderSupportsHDR },
		{ "SupportsPixelShaders_1_4", HL2SB_MenuRenderSupportsPS14 },
		{ "SupportsPixelShaders_2_0", HL2SB_MenuRenderSupportsPS20 },
		{ "SupportsVertexShaders_2_0", HL2SB_MenuRenderSupportsVS20 },
		{ NULL, NULL }
	};
	luaL_register( L, "render", menuRender );
	lua_pop( L, 1 );
	lua_newtable( L );
	lua_pushcfunction( L, HL2SB_MotionSensorUnavailable );
	lua_setfield( L, -2, "IsAvailable" );
	lua_setglobal( L, "motionsensor" );
	lua_pushstring( L, engine->GetProductVersionString() );
	lua_setglobal( L, "VERSIONSTR" );
	char protocol[32];
	Q_snprintf( protocol, sizeof(protocol), "%u", engine->GetProtocolVersion() );
	lua_pushstring( L, protocol );
	lua_setglobal( L, "NETVERSIONSTR" );
	lua_pushliteral( L, "unknown" );
	lua_setglobal( L, "BRANCH" );
	// Publish as plain globals: the menu scripts are plain Lua files, not modules, and
	// GMod's own menu helpers are globals too.  luaL_register( L, NULL, ... )
	// registers into the table ON TOP OF THE STACK -- with nothing pushed, that
	// was whatever leftover table the previous luaopen left, so these functions
	// never actually reached _G.  Push the globals table explicitly.
	lua_pushglobaltable( L );
	luaL_register( L, NULL, gameui_menu_funcs );
	lua_pop( L, 1 );
	return 1;
}

//-----------------------------------------------------------------------------
// Purpose: engine-console routing INTO the menu realm.
//
//   The GameUI options panel fires "crosshair_setup" as a plain engine console
//   command (OptionsSubMultiplayer forwards it with ClientCmd_Unrestricted),
//   which is exactly what the reference does.  The editor itself is a menu-realm
//   Lua concommand (lua/menu/crosshair_setup.lua, loaded by luasrc_init_gameui)
//   -- but the engine console only looks at native ConCommands, so all the
//   forward ever produced was
//       Unknown command "crosshair_setup"
//   This static command receives that forward and hands it to the menu state's
//   concommand.Run, the same dispatch the engine uses for unknown commands in
//   the game realms.  Static registration means it exists as soon as the client
//   dll is loaded, i.e. while the main menu is up.
//-----------------------------------------------------------------------------
static void HL2SB_MenuDispatchCommand( const char *pszCommand )
{
	if ( LGameUI == NULL )
	{
		Warning( "HL2SB: menu realm not started, cannot dispatch '%s'\n", pszCommand );
		return;
	}

	const int iBase = lua_gettop( LGameUI );

	lua_getglobal( LGameUI, "concommand" );
	if ( lua_istable( LGameUI, -1 ) )
	{
		lua_getfield( LGameUI, -1, "Run" );
		if ( lua_isfunction( LGameUI, -1 ) )
		{
			// concommand.Run( player, command, arguments, argumentsStr )
			lua_pushnil( LGameUI );
			lua_pushstring( LGameUI, pszCommand );
			lua_newtable( LGameUI );
			lua_pushstring( LGameUI, pszCommand );
			luasrc_pcall( LGameUI, 4, 0, 0 );
		}
	}

	lua_settop( LGameUI, iBase );
}

CON_COMMAND( crosshair_setup, "Open the crosshair editor (menu realm)" )
{
	if ( !LGameUI || engine->IsDrawingLoadingImage() || s_OpeningCrosshairSetup )
		return;

	HL2SB_CloseCrosshairSetup();
	s_OpeningCrosshairSetup = true;
	HL2SB_MenuDispatchCommand( "crosshair_setup" );
	s_OpeningCrosshairSetup = false;
}

#endif // LUA_SDK

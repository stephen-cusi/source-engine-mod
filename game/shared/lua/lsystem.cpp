#include "cbase.h"
#include "luamanager.h"
#include "luasrclib.h"
#include "lsystem.h"
#include <iostream>
#include <string>
#include <chrono>
#include <ctime>
#include <thread>
#include <vgui_controls/Controls.h>
#include <steam/steam_api.h>
#include <vgui/ISystem.h>

#ifdef INVALID_HANDLE_VALUE
#undef INVALID_HANDLE_VALUE
#endif

#ifdef GetCommandLine
#undef GetCommandLine
#endif

#ifdef ReadConsoleInput
#undef ReadConsoleInput
#endif

#ifdef RegCreateKey
#undef RegCreateKey
#endif

#ifdef RegCreateKeyEx
#undef RegCreateKeyEx
#endif

#ifdef RegOpenKey
#undef RegOpenKey
#endif

#ifdef RegOpenKeyEx
#undef RegOpenKeyEx
#endif

#ifdef RegQueryValue
#undef RegQueryValue
#endif

#ifdef RegQueryValueEx
#undef RegQueryValueEx
#endif

#ifdef RegSetValue
#undef RegSetValue
#endif

#ifdef RegSetValueEx
#undef RegSetValueEx
#endif

#include <winlite.h>

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

/*
** HL2SB: GetAppWindow() lives in Experiment's game/shared/util_shared.cpp; the
** helper is inlined here so this file stays self-contained.
** Original approach: https://stackoverflow.com/a/21767578/3086369
*/
#ifdef _WIN32
struct handle_data
{
	unsigned long process_id;
	HWND window_handle;
};

static BOOL IsMainWindow( HWND handle )
{
	return GetWindow( handle, GW_OWNER ) == ( HWND )0 && IsWindowVisible( handle );
}

static BOOL CALLBACK EnumWindowsCallback( HWND handle, LPARAM lParam )
{
	handle_data &data = *( handle_data * )lParam;
	unsigned long process_id = 0;
	GetWindowThreadProcessId( handle, &process_id );
	if ( data.process_id != process_id || !IsMainWindow( handle ) )
		return TRUE;
	data.window_handle = handle;
	return FALSE;
}

static void *GetAppWindow()
{
	handle_data data;
	data.process_id = GetCurrentProcessId();
	data.window_handle = 0;
	EnumWindows( EnumWindowsCallback, ( LPARAM )&data );
	return data.window_handle;
}
#else
static void *GetAppWindow()
{
	return NULL;
}
#endif

LUA_REGISTRATION_INIT( Systems );

LUA_BINDING_BEGIN( Systems, GetSecondsSinceAppActive, "library", "Get the number of seconds since the application was started." )
{
    lua_pushinteger( L, steamapicontext->SteamUtils()->GetSecondsSinceAppActive() );
    return 1;
}
LUA_BINDING_END( "integer", "The number of seconds since the application was started." )

LUA_BINDING_BEGIN( Systems, GetCountry, "library", "Get the country code of the user." )
{
#ifdef _WIN32
    // GetLocaleInfo/LOCALE_* are Windows-only; on Linux/Android there is no
    // equivalent in this tree, so the binding reports "unknown" like a failed
    // lookup does on Windows.
    char country[10];

    if ( GetLocaleInfo( LOCALE_USER_DEFAULT, LOCALE_SISO3166CTRYNAME, country, sizeof( country ) ) )
    {
        lua_pushstring( L, country );
        return 1;
    }
#endif

    lua_pushnil( L );
    return 1;
}
LUA_BINDING_END( "string", "The country code of the user." )

LUA_BINDING_BEGIN( Systems, HasFocus, "library", "Check if the application has focus." )
{
#ifdef _WIN32
    HWND hWnd = ( HWND )GetAppWindow();
    lua_pushboolean( L, hWnd == GetActiveWindow() );
#else
    // TODO: Implement for other platforms
    Assert( 0 );
    lua_pushboolean( L, false );
#endif

    return 1;
}
LUA_BINDING_END( "boolean", "Whether the application has focus." )

LUA_BINDING_BEGIN( Systems, IsLinux, "library", "Check if the application is running on Linux." )
{
#ifdef __linux__
    lua_pushboolean( L, true );
#else
    lua_pushboolean( L, false );
#endif
    return 1;
}
LUA_BINDING_END( "boolean", "Whether the application is running on Linux." )

LUA_BINDING_BEGIN( Systems, IsOsx, "library", "Check if the application is running on macOS." )
{
#ifdef __APPLE__
    lua_pushboolean( L, true );
#else
    lua_pushboolean( L, false );
#endif
    return 1;
}
LUA_BINDING_END( "boolean", "Whether the application is running on macOS." )

// HL2SB (sbrust): 安卓判定。不能复用 IsLinux（__linux__ 在安卓上同样成立，
// GMod 后缀公式会把安卓算进 linux64 槽位，与桌面 glibc 模块命名撞车）。
LUA_BINDING_BEGIN( Systems, IsAndroid, "library", "Check if the application is running on Android." )
{
#ifdef __ANDROID__
    lua_pushboolean( L, true );
#else
    lua_pushboolean( L, false );
#endif
    return 1;
}
LUA_BINDING_END( "boolean", "Whether the application is running on Android." )

LUA_BINDING_BEGIN( Systems, IsWindows, "library", "Check if the application is running on Windows." )
{
#ifdef _WIN32
    lua_pushboolean( L, true );
#else
    lua_pushboolean( L, false );
#endif

    return 1;
}
LUA_BINDING_END( "boolean", "Whether the application is running on Windows." )

LUA_BINDING_BEGIN( Systems, IsWindowed, "library", "Check if the application is running in windowed mode." )
{
#ifdef _WIN32
    HWND hWnd = ( HWND )GetAppWindow();

    if ( hWnd )
    {
        DWORD dwStyle = GetWindowLong( hWnd, GWL_STYLE );
        lua_pushboolean( L, dwStyle & WS_OVERLAPPEDWINDOW );
        return 1;
    }
#elif __linux__
// TODO
#endif

    lua_pushboolean( L, false );
    return 1;
}
LUA_BINDING_END( "boolean", "Whether the application is running in windowed mode." )

LUA_BINDING_BEGIN( Systems, GetSteamServerRealTime, "library", "Get the server time." )
{
    lua_pushinteger( L, steamapicontext->SteamUtils()->GetServerRealTime() );
    return 1;
}
LUA_BINDING_END( "integer", "The server time." )

LUA_BINDING_BEGIN( Systems, GetSecondsSinceComputerActive, "library", "Get the number of seconds since the computer was started." )
{
    lua_pushinteger( L, steamapicontext->SteamUtils()->GetSecondsSinceComputerActive() );
    return 1;
}
LUA_BINDING_END( "integer", "The number of seconds since the computer was started." )

LUA_BINDING_BEGIN( Systems, SetClipboardText, "library", "Set the text in the clipboard." )
{
    const char* text = LUA_BINDING_ARGUMENT( luaL_checkstring, 1, "text" );
    // HL2SB: vgui's ISystem accessor lives in the vgui namespace; the bare
    // system() resolved to the CRT's system(const char *) and failed to compile.
    vgui::system()->SetClipboardText( text, strlen( text ) );
    return 0;
}
LUA_BINDING_END()

LUALIB_API int( luaopen_Systems )( lua_State* L )
{
    LUA_REGISTRATION_COMMIT_LIBRARY( Systems );
    return 1;
}

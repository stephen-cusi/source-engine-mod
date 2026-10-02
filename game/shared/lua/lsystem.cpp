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

#ifdef _WIN32
#include <dxgi.h>
#elif defined( __ANDROID__ )
#include <sys/system_properties.h>
#include <unistd.h>
#endif

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

// ===========================================================================
// HL2SB: platform / hardware facts for system.GetPlatformInfo().  Every OS
// access below is dynamically loaded or /proc / build-prop based so the
// binding adds no link dependency; hosts that cannot answer just leave the
// field out of the table.
// ===========================================================================
static const char *Platform_OSName()
{
#if defined( _WIN32 )
	return "windows";
#elif defined( __ANDROID__ )
	return "android";
#elif defined( __APPLE__ )
	return "osx";
#elif defined( __linux__ )
	return "linux";
#else
	return "unknown";
#endif
}

#ifdef _WIN32

#ifndef IMAGE_FILE_MACHINE_ARM64
#define IMAGE_FILE_MACHINE_ARM64 0xaa64
#endif

// GetVersionEx reports what the manifest asked for; RtlGetVersion reports the
// real OS build number.
static bool Win_GetOSVersion( char *pszOut, int len )
{
	typedef LONG ( WINAPI *RtlGetVersion_t )( RTL_OSVERSIONINFOW * );
	HMODULE hNtDll = GetModuleHandleA( "ntdll.dll" );
	RtlGetVersion_t pfnRtlGetVersion =
		hNtDll ? ( RtlGetVersion_t )GetProcAddress( hNtDll, "RtlGetVersion" ) : NULL;
	if ( !pfnRtlGetVersion )
		return false;

	RTL_OSVERSIONINFOW vi;
	vi.dwOSVersionInfoSize = sizeof( RTL_OSVERSIONINFOW );
	if ( pfnRtlGetVersion( &vi ) != 0 )
		return false;

	Q_snprintf( pszOut, len, "%lu.%lu.%lu", vi.dwMajorVersion, vi.dwMinorVersion, vi.dwBuildNumber );
	return true;
}

// The OS native machine, not the process view: an x64 binary running under
// ARM64 Windows is served by the x64 emulator, which makes GetSystemInfo and
// GetNativeSystemInfo report AMD64 to the process.  IsWow64Process2 (Win10
// 1709+) exposes the physical machine in nativeMachine and the process view
// in processMachine; fall back to GetNativeSystemInfo where it is missing
// (honest outside emulation).
static bool Win_GetArch( char *pszNative, char *pszProcess, int len )
{
	typedef BOOL ( WINAPI *IsWow64Process2_t )( HANDLE, USHORT *, USHORT * );
	HMODULE hKernel32 = GetModuleHandleA( "kernel32.dll" );
	IsWow64Process2_t pfnIsWow64Process2 =
		hKernel32 ? ( IsWow64Process2_t )GetProcAddress( hKernel32, "IsWow64Process2" ) : NULL;

	const char *pszNativeArch = NULL;
	const char *pszProcessArch = NULL;

	USHORT usProcessMachine = 0, usNativeMachine = 0;
	if ( pfnIsWow64Process2 && pfnIsWow64Process2( GetCurrentProcess(), &usProcessMachine, &usNativeMachine )
		&& usNativeMachine != IMAGE_FILE_MACHINE_UNKNOWN )
	{
		switch ( usNativeMachine )
		{
			case IMAGE_FILE_MACHINE_ARM64: pszNativeArch = "arm64"; break;
			case IMAGE_FILE_MACHINE_AMD64: pszNativeArch = "x64"; break;
			case IMAGE_FILE_MACHINE_I386:  pszNativeArch = "x86"; break;
			default: break;
		}

		// processMachine is UNKNOWN when the process runs natively; any other
		// value is the emulated view the process is served as.
		switch ( usProcessMachine )
		{
			case IMAGE_FILE_MACHINE_ARM64: pszProcessArch = "arm64"; break;
			case IMAGE_FILE_MACHINE_AMD64: pszProcessArch = "x64"; break;
			case IMAGE_FILE_MACHINE_I386:  pszProcessArch = "x86"; break;
			default: pszProcessArch = pszNativeArch; break;
		}
	}

	if ( !pszNativeArch )
	{
		SYSTEM_INFO si;
		GetNativeSystemInfo( &si );
		switch ( si.wProcessorArchitecture )
		{
			case PROCESSOR_ARCHITECTURE_ARM64: pszNativeArch = "arm64"; break;
			case PROCESSOR_ARCHITECTURE_AMD64: pszNativeArch = "x64"; break;
			case PROCESSOR_ARCHITECTURE_INTEL: pszNativeArch = "x86"; break;
			default: return false;
		}
		pszProcessArch = pszNativeArch;
	}

	Q_strncpy( pszNative, pszNativeArch, len );
	Q_strncpy( pszProcess, pszProcessArch, len );
	return true;
}

// Marketing brand from the firmware description the kernel maintains (present
// on x64 and ARM64 alike); vendor strings are space-padded, so trim.
static bool Win_GetCPUBrand( char *pszOut, int len )
{
	typedef LONG ( WINAPI *RegGetValueA_t )( HKEY, LPCSTR, LPCSTR, DWORD, LPDWORD, PVOID, LPDWORD );
	HMODULE hAdvapi32 = LoadLibraryA( "advapi32.dll" );
	if ( !hAdvapi32 )
		return false;

	RegGetValueA_t pfnRegGetValueA = ( RegGetValueA_t )GetProcAddress( hAdvapi32, "RegGetValueA" );
	if ( !pfnRegGetValueA )
	{
		FreeLibrary( hAdvapi32 );
		return false;
	}

	DWORD dwSize = ( DWORD )len;
	LONG lResult = pfnRegGetValueA( HKEY_LOCAL_MACHINE,
		"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
		"ProcessorNameString", RRF_RT_REG_SZ, NULL, pszOut, &dwSize );
	FreeLibrary( hAdvapi32 );

	if ( lResult != ERROR_SUCCESS )
		return false;

	char *p = pszOut;
	while ( *p == ' ' )
		++p;
	if ( p != pszOut )
		memmove( pszOut, p, strlen( p ) + 1 );

	return pszOut[0] != '\0';
}

static bool Win_GetMemory( int *pnTotalMB, int *pnFreeMB )
{
	MEMORYSTATUSEX ms;
	ms.dwLength = sizeof( ms );
	if ( !GlobalMemoryStatusEx( &ms ) )
		return false;

	*pnTotalMB = ( int )( ms.ullTotalPhys >> 20 );
	*pnFreeMB = ( int )( ms.ullAvailPhys >> 20 );
	return true;
}

// Adapter 0 owns the primary desktop output, which is what the game renders
// on short of exotic multi-GPU setups.  Name and dedicated video memory come
// straight from the adapter description, without linking dxgi.lib.
static bool Win_GetGPU( char *pszName, int len, int *pnVramMB )
{
	typedef HRESULT ( WINAPI *CreateDXGIFactory1_t )( REFIID, void ** );
	HMODULE hDxgi = LoadLibraryA( "dxgi.dll" );
	if ( !hDxgi )
		return false;

	CreateDXGIFactory1_t pfnCreateDXGIFactory1 =
		( CreateDXGIFactory1_t )GetProcAddress( hDxgi, "CreateDXGIFactory1" );
	if ( !pfnCreateDXGIFactory1 )
	{
		FreeLibrary( hDxgi );
		return false;
	}

	bool bOK = false;
	IDXGIFactory1 *pFactory = NULL;
	if ( SUCCEEDED( pfnCreateDXGIFactory1( __uuidof( IDXGIFactory1 ), ( void ** )&pFactory ) ) && pFactory )
	{
		IDXGIAdapter1 *pAdapter = NULL;
		if ( SUCCEEDED( pFactory->EnumAdapters1( 0, &pAdapter ) ) && pAdapter )
		{
			DXGI_ADAPTER_DESC1 desc;
			if ( SUCCEEDED( pAdapter->GetDesc1( &desc ) ) )
			{
				WideCharToMultiByte( CP_UTF8, 0, desc.Description, -1, pszName, len, NULL, NULL );
				pszName[len - 1] = '\0';
				*pnVramMB = ( int )( desc.DedicatedVideoMemory >> 20 );
				bOK = pszName[0] != '\0';
			}
			pAdapter->Release();
		}
		pFactory->Release();
	}

	FreeLibrary( hDxgi );
	return bOK;
}

#elif defined( __ANDROID__ )

static bool Android_GetProp( const char *pszKey, char *pszOut )
{
	pszOut[0] = '\0';
	__system_property_get( pszKey, pszOut );
	return pszOut[0] != '\0';
}

static bool Android_GetMemory( int *pnTotalMB, int *pnFreeMB )
{
	FILE *pFile = fopen( "/proc/meminfo", "r" );
	if ( !pFile )
		return false;

	long nTotalKb = 0, nFreeKb = 0;
	char szLine[128];
	while ( fgets( szLine, sizeof( szLine ), pFile ) )
	{
		if ( !strncmp( szLine, "MemTotal:", 9 ) )
			nTotalKb = atol( szLine + 9 );
		else if ( !strncmp( szLine, "MemAvailable:", 13 ) )
			nFreeKb = atol( szLine + 13 );
	}
	fclose( pFile );

	if ( nTotalKb <= 0 )
		return false;

	*pnTotalMB = ( int )( nTotalKb >> 10 );
	*pnFreeMB = ( int )( nFreeKb >> 10 );
	return true;
}

#endif // _WIN32 / __ANDROID__

// Unified arch answer for GetPlatform.
static bool Platform_Arch( char *pszOut, int len )
{
#ifdef _WIN32
	char szProcess[64];
	return Win_GetArch( pszOut, szProcess, len );
#elif defined( __ANDROID__ )
	// arm64-v8a -> "arm64", armeabi-v7a -> "armv7"
	char szAbi[PROP_VALUE_MAX];
	if ( !Android_GetProp( "ro.product.cpu.abi", szAbi ) )
		return false;
	if ( !strncmp( szAbi, "arm64", 5 ) )
		Q_strncpy( pszOut, "arm64", len );
	else if ( !strncmp( szAbi, "armeabi", 7 ) )
		Q_strncpy( pszOut, "armv7", len );
	else
		Q_strncpy( pszOut, szAbi, len );
	return true;
#else
	return false;
#endif
}

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

LUA_BINDING_BEGIN( Systems, GetPlatformInfo, "library", "Returns a table of platform and hardware facts for the current device." )
{
    char szBuffer[256];
    int nTotalMB = 0, nFreeMB = 0;
    bool bHaveMemory = false;

    lua_newtable( L );

    lua_pushstring( L, Platform_OSName() );
    lua_setfield( L, -2, "os" );

#ifdef _WIN32
    char szProcess[64];
    if ( Win_GetArch( szBuffer, szProcess, ( int )sizeof( szBuffer ) ) )
    {
        lua_pushstring( L, szBuffer );
        lua_setfield( L, -2, "arch" );
        lua_pushstring( L, szProcess );
        lua_setfield( L, -2, "processArch" );
        lua_pushboolean( L, strcmp( szBuffer, szProcess ) != 0 );
        lua_setfield( L, -2, "emulated" );
    }
#else
    if ( Platform_Arch( szBuffer, ( int )sizeof( szBuffer ) ) )
    {
        lua_pushstring( L, szBuffer );
        lua_setfield( L, -2, "arch" );
    }
#endif

#ifdef _WIN32
    if ( Win_GetOSVersion( szBuffer, ( int )sizeof( szBuffer ) ) )
    {
        lua_pushstring( L, szBuffer );
        lua_setfield( L, -2, "osVersion" );
    }
    if ( Win_GetCPUBrand( szBuffer, ( int )sizeof( szBuffer ) ) )
    {
        lua_pushstring( L, szBuffer );
        lua_setfield( L, -2, "cpu" );
    }

    SYSTEM_INFO si;
    GetNativeSystemInfo( &si );
    lua_pushinteger( L, si.dwNumberOfProcessors );
    lua_setfield( L, -2, "cores" );

    bHaveMemory = Win_GetMemory( &nTotalMB, &nFreeMB );

    char szGpu[256];
    int nVramMB = 0;
    if ( Win_GetGPU( szGpu, ( int )sizeof( szGpu ), &nVramMB ) )
    {
        lua_pushstring( L, szGpu );
        lua_setfield( L, -2, "gpu" );
        lua_pushinteger( L, nVramMB );
        lua_setfield( L, -2, "gpuMemoryMB" );
    }
#elif defined( __ANDROID__ )
    char szProp[PROP_VALUE_MAX], szProp2[PROP_VALUE_MAX];
    if ( Android_GetProp( "ro.build.version.release", szProp ) )
    {
        if ( Android_GetProp( "ro.build.version.sdk", szProp2 ) )
            Q_snprintf( szBuffer, sizeof( szBuffer ), "%s (API %s)", szProp, szProp2 );
        else
            Q_strncpy( szBuffer, szProp, ( int )sizeof( szBuffer ) );
        lua_pushstring( L, szBuffer );
        lua_setfield( L, -2, "osVersion" );
    }

    // ro.soc.model (Android 12+) is the precise part number; ro.board.platform
    // the platform codename; the vendor falls back to ro.hardware.
    char szSoc[PROP_VALUE_MAX], szBoard[PROP_VALUE_MAX], szVendor[PROP_VALUE_MAX];
    bool bHaveSoc = Android_GetProp( "ro.soc.model", szSoc );
    bool bHaveBoard = Android_GetProp( "ro.board.platform", szBoard );
    bool bHaveVendor = Android_GetProp( "ro.soc.manufacturer", szVendor )
        || Android_GetProp( "ro.hardware", szVendor );

    if ( bHaveSoc || bHaveBoard )
    {
        if ( bHaveSoc && bHaveVendor )
            Q_snprintf( szBuffer, sizeof( szBuffer ), "%s %s", szVendor, szSoc );
        else
            Q_strncpy( szBuffer, bHaveSoc ? szSoc : szBoard, ( int )sizeof( szBuffer ) );
        if ( bHaveSoc && bHaveBoard && strcmp( szBoard, szSoc ) != 0 )
            Q_snprintf( szBuffer + strlen( szBuffer ), sizeof( szBuffer ) - strlen( szBuffer ), " (%s)", szBoard );
        lua_pushstring( L, szBuffer );
        lua_setfield( L, -2, "cpu" );
    }

    long nCores = sysconf( _SC_NPROCESSORS_CONF );
    if ( nCores > 0 )
    {
        lua_pushinteger( L, ( lua_Integer )nCores );
        lua_setfield( L, -2, "cores" );
    }

    bHaveMemory = Android_GetMemory( &nTotalMB, &nFreeMB );

    // Android GPUs are unified-memory, so there is no dedicated VRAM to
    // report; ro.hardware.egl names the driver family ("adreno"/"mali").
    char szEgl[PROP_VALUE_MAX];
    if ( Android_GetProp( "ro.hardware.egl", szEgl ) )
    {
        lua_pushstring( L, szEgl );
        lua_setfield( L, -2, "gpu" );
    }
#endif

    if ( bHaveMemory )
    {
        lua_pushinteger( L, nTotalMB );
        lua_setfield( L, -2, "totalMemoryMB" );
        lua_pushinteger( L, nFreeMB );
        lua_setfield( L, -2, "freeMemoryMB" );
    }

    return 1;
}
LUA_BINDING_END( "table", "Platform and hardware facts. Missing entries mean the host did not report them." )

LUA_BINDING_BEGIN( Systems, GetPlatform, "library", "Returns a short \"os-arch\" token for the current device, e.g. \"windows-arm64\"." )
{
    char szArch[64], szToken[128];
    const char *pszArch = Platform_Arch( szArch, ( int )sizeof( szArch ) ) ? szArch : "unknown";
    Q_snprintf( szToken, sizeof( szToken ), "%s-%s", Platform_OSName(), pszArch );
    lua_pushstring( L, szToken );
    return 1;
}
LUA_BINDING_END( "string", "The platform token, e.g. \"windows-arm64\"." )

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

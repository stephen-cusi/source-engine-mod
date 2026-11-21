// hl2sb_crash_handler.cpp
// Crash handler for HL2SB - captures exceptions and writes to log
//
// HL2SB: every platform we build the client for now appends an identifiable
// "[HL2SB] crash:" block to *engine.log* - the file the launcher opens in
// CSourceAppSystemGroup::PreInit (launcher/launcher.cpp:
// DebugLogger()->Init("engine.log"), i.e. next to the executable) - in addition
// to whatever crash behaviour that platform already had:
//
//   Windows : hl2sb_crash.log + a minidump under dumps/ (SEH and CRT abort paths)
//   Linux   : the same engine.log block, written from POSIX signal handlers
//   Android : unchanged - its launcher already funnels crashes into engine.log
//             (launcher/android/crashhandler.cpp -> DebugLogger()->Write)
//
// Which handle the block is appended through, and why that is safe:
//
//   NOT tier0's IDbgLogger FILE*.  That handle belongs to the engine, it does
//   not exist when the engine was started with -nolog, and its stdio lock may be
//   held by the very thread that just crashed (deadlock).  Instead the block goes
//   through a raw append-only handle that is opened on demand and closed again
//   immediately: FILE_APPEND_DATA on Windows, O_WRONLY|O_CREAT|O_APPEND on
//   Linux.  Both make the *kernel* resolve every write against the current end of
//   file, so the block can never overwrite what the engine has already flushed,
//   and the engine's own fopen( "w+" ) handle (MSVC opens it with _SH_DENYNO,
//   i.e. FILE_SHARE_READ|FILE_SHARE_WRITE) keeps the second open legal.  The
//   write happens on the way towards process death, and we hold no handle
//   afterwards, so there is no window in which the two writers interleave.
//   Nothing on this path allocates.

#include "cbase.h"
#include "hl2sb_crash_handler.h"

#include "tier0/minidump.h"

#if defined( _WIN32 )
#include <windows.h>
#include <dbghelp.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <exception>
#elif defined( LINUX ) && !defined( ANDROID )
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <execinfo.h>
#include <dlfcn.h>
#include <sys/syscall.h>
#include <exception>
#endif

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#if defined( _WIN32 ) || ( defined( LINUX ) && !defined( ANDROID ) )

//-----------------------------------------------------------------------------
// engine.log writer.  Shared by every platform so the format is identical.
//-----------------------------------------------------------------------------

// Stable prefix: this is what makes a crash findable in a busy engine.log.
#define HL2SB_CRASH_PREFIX		"[HL2SB] crash: "
#define HL2SB_ENGINE_LOG_NAME	"engine.log"
#define HL2SB_CRASH_STACK_FRAMES 64

#if defined( _WIN32 )
typedef HANDLE	hl2sb_loghandle_t;
#define HL2SB_LOG_HANDLE_INVALID	INVALID_HANDLE_VALUE
#else
typedef int		hl2sb_loghandle_t;
#define HL2SB_LOG_HANDLE_INVALID	( -1 )
#endif

static const char *HL2SB_BaseName( const char *pszPath )
{
	const char *pszName = pszPath;
	for ( const char *p = pszPath; p && *p; ++p )
	{
		if ( *p == '\\' || *p == '/' )
			pszName = p + 1;
	}
	return pszName;
}

// Raw, append-only, allocation-free.  See the file header for why this is not
// the engine's own FILE*.
static hl2sb_loghandle_t HL2SB_EngineLogOpen( void )
{
#ifdef _WIN32
	return CreateFileA( HL2SB_ENGINE_LOG_NAME, FILE_APPEND_DATA,
		FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS,
		FILE_ATTRIBUTE_NORMAL, NULL );
#else
	// open()/write()/close() are all async-signal-safe.
	return open( HL2SB_ENGINE_LOG_NAME, O_WRONLY | O_CREAT | O_APPEND, 0644 );
#endif
}

static void HL2SB_EngineLogWrite( hl2sb_loghandle_t hFile, const char *pText, int nLength )
{
	if ( hFile == HL2SB_LOG_HANDLE_INVALID || nLength <= 0 || !pText )
		return;

#ifdef _WIN32
	DWORD nWritten = 0;
	WriteFile( hFile, pText, (DWORD)nLength, &nWritten, NULL );
#else
	ssize_t nIgnored = write( hFile, pText, (size_t)nLength );
	( void )nIgnored;
#endif
}

static void HL2SB_EngineLogClose( hl2sb_loghandle_t hFile )
{
	if ( hFile == HL2SB_LOG_HANDLE_INVALID )
		return;

#ifdef _WIN32
	CloseHandle( hFile );
#else
	close( hFile );
#endif
}

// One "[HL2SB] crash: <text>\n" line.
static void HL2SB_EngineLogLine( hl2sb_loghandle_t hFile, const char *pszFormat, ... )
{
	char szLine[512];
	va_list marker;

	va_start( marker, pszFormat );
	Q_vsnprintf( szLine, (int)sizeof( szLine ), pszFormat, marker );
	va_end( marker );
	szLine[ sizeof( szLine ) - 1 ] = 0;

	HL2SB_EngineLogWrite( hFile, HL2SB_CRASH_PREFIX, (int)( sizeof( HL2SB_CRASH_PREFIX ) - 1 ) );
	HL2SB_EngineLogWrite( hFile, szLine, (int)strlen( szLine ) );
	HL2SB_EngineLogWrite( hFile, "\n", 1 );
}

// "module+0xoffset" for an address, or "?" when it is not backed by a module.
static void HL2SB_ModuleForAddress( const void *pAddress, char *pOut, int nOutLen )
{
	pOut[ 0 ] = 0;

	if ( !pAddress )
		return;

#ifdef _WIN32
	HMODULE hModule = NULL;
	if ( GetModuleHandleExA( GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
			GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			(LPCSTR)pAddress, &hModule ) && hModule )
	{
		char szPath[ MAX_PATH ];
		if ( GetModuleFileNameA( hModule, szPath, sizeof( szPath ) ) )
		{
			Q_snprintf( pOut, nOutLen, "%s+0x%X", HL2SB_BaseName( szPath ),
				(unsigned int)( (uintptr_t)pAddress - (uintptr_t)hModule ) );
			return;
		}
	}
#else
	Dl_info info;
	memset( &info, 0, sizeof( info ) );
	if ( dladdr( (void *)pAddress, &info ) && info.dli_fname )
	{
		Q_snprintf( pOut, nOutLen, "%s+0x%X", HL2SB_BaseName( info.dli_fname ),
			(unsigned int)( (uintptr_t)pAddress - (uintptr_t)info.dli_fbase ) );
		return;
	}
#endif

	Q_snprintf( pOut, nOutLen, "?" );
}

static unsigned long HL2SB_CurrentThreadId( void )
{
#ifdef _WIN32
	return (unsigned long)GetCurrentThreadId();
#else
	// Linux OS thread id: what gdb / /proc/<pid>/task shows.
	return (unsigned long)syscall( SYS_gettid );
#endif
}

#ifdef _WIN32

static void HL2SB_EngineLogStack( hl2sb_loghandle_t hFile, unsigned int nFramesToSkip )
{
	void *pStack[ HL2SB_CRASH_STACK_FRAMES ];
	unsigned short nFrames = RtlCaptureStackBackTrace( nFramesToSkip,
		HL2SB_CRASH_STACK_FRAMES, pStack, NULL );

	HL2SB_EngineLogLine( hFile, "stack (%u frames):", (unsigned int)nFrames );

	for ( unsigned int i = 0; i < nFrames; ++i )
	{
		char szModule[ 192 ];
		HL2SB_ModuleForAddress( pStack[ i ], szModule, sizeof( szModule ) );
		HL2SB_EngineLogLine( hFile, "  #%02u %p  %s", i, pStack[ i ], szModule );
	}
}

#else

static void HL2SB_EngineLogStack( hl2sb_loghandle_t hFile, unsigned int nFramesToSkip )
{
	void *pStack[ HL2SB_CRASH_STACK_FRAMES ];
	int nFrames = backtrace( pStack, HL2SB_CRASH_STACK_FRAMES );
	if ( nFrames < 0 )
		nFrames = 0;

	// Drop the signal-handler frames so the trace starts at the fault itself.
	int nFirst = ( (int)nFramesToSkip < nFrames ) ? (int)nFramesToSkip : 0;

	HL2SB_EngineLogLine( hFile, "stack (%d frames):", nFrames - nFirst );

	for ( int i = nFirst; i < nFrames; ++i )
	{
		Dl_info info;
		memset( &info, 0, sizeof( info ) );

		const char *pszModule = "?";
		const char *pszSymbol = "?";
		unsigned int nOffset = 0;

		if ( dladdr( pStack[ i ], &info ) )
		{
			if ( info.dli_fname )
			{
				pszModule = HL2SB_BaseName( info.dli_fname );
				nOffset = (unsigned int)( (uintptr_t)pStack[ i ] - (uintptr_t)info.dli_fbase );
			}
			if ( info.dli_sname )
				pszSymbol = info.dli_sname;
		}

		HL2SB_EngineLogLine( hFile, "  #%02d %p  %s+0x%X (%s)",
			i - nFirst, pStack[ i ], pszModule, nOffset, pszSymbol );
	}
}

#endif // _WIN32

//-----------------------------------------------------------------------------
// The single entry point every crash path funnels through.
//-----------------------------------------------------------------------------
static void HL2SB_EngineLogCrashBlock( const char *pszKind, unsigned int uExceptionCode,
	const void *pAddress, const char *pszDetail, unsigned int nFramesToSkip )
{
	hl2sb_loghandle_t hFile = HL2SB_EngineLogOpen();
	if ( hFile == HL2SB_LOG_HANDLE_INVALID )
		return;		// nothing more we can do from inside a crash

	char szModule[ 192 ];
	HL2SB_ModuleForAddress( pAddress, szModule, sizeof( szModule ) );

	HL2SB_EngineLogLine( hFile, "begin" );
	HL2SB_EngineLogLine( hFile, "kind=%s code=0x%08X address=%p module=%s thread=%lu%s%s",
		pszKind ? pszKind : "?", uExceptionCode, pAddress, szModule,
		HL2SB_CurrentThreadId(),
		( pszDetail && pszDetail[ 0 ] ) ? " detail=" : "",
		( pszDetail && pszDetail[ 0 ] ) ? pszDetail : "" );

	HL2SB_EngineLogStack( hFile, nFramesToSkip );

	HL2SB_EngineLogLine( hFile, "end" );

	HL2SB_EngineLogClose( hFile );
}

#endif // engine.log platforms

#ifdef _WIN32

//-----------------------------------------------------------------------------
// The process aborting through the CRT (std::terminate, an invalid parameter,
// a pure virtual call) does NOT go through SetUnhandledExceptionFilter: on
// x64 the UCRT ends in __fastfail(), which the kernel turns into
// STATUS_STACK_BUFFER_OVERRUN (0xC0000409) *without* consulting SEH.  That is
// why a crash of that kind left no minidump behind -- only
// "client.dll, exception 0xc0000409" in the Windows event log.
//
// So intercept the CRT entries themselves, while the process is still healthy
// enough to walk its own stack and write a dump.
//-----------------------------------------------------------------------------

static const DWORD s_nCompactDump = MiniDumpWithDataSegs |
	MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithProcessThreadData;
static const DWORD s_nDetailedDump = s_nCompactDump | MiniDumpWithThreadInfo |
	MiniDumpWithFullMemoryInfo | MiniDumpWithUnloadedModules | MiniDumpWithHandleData |
	MiniDumpIgnoreInaccessibleMemory;

typedef decltype(&MiniDumpWriteDump) HL2SB_MiniDumpWriteFn;
static HL2SB_MiniDumpWriteFn g_pHL2SBDumpWriter = NULL;
static HMODULE g_hHL2SBDbgHelp = NULL;
static DWORD g_nHL2SBDumpType = s_nDetailedDump;
static wchar_t g_szHL2SBDumpDirectory[MAX_PATH];
static wchar_t g_szHL2SBGameSymbols[MAX_PATH];
static wchar_t g_szHL2SBEngineSymbols[MAX_PATH];
static wchar_t g_szHL2SBExtraSymbols[1024];

static HANDLE g_hHL2SBCrashRequest;
static HANDLE g_hHL2SBCrashNotify;
static HANDLE g_hHL2SBCrashDumpReady;
static HANDLE g_hHL2SBCrashDone;
static HANDLE g_hHL2SBCrashThread;
static HANDLE g_hHL2SBCrashNotifyThread;
static volatile LONG g_bHL2SBCrashPending;
static volatile LONG g_bHL2SBCrashThreadExit;
static DWORD g_nHL2SBCrashThreadId;
static DWORD g_nHL2SBCrashNotifyThreadId;
static volatile LONG g_bHL2SBWritingDump;
static wchar_t g_szHL2SBCrashDumpPath[MAX_PATH];
static const DWORD s_nCrashDumpReportWaitMs = 3000;

static struct
{
	EXCEPTION_RECORD record;
	CONTEXT context;
	const char *reason;
	char detail[512];
	DWORD threadId;
} g_HL2SBCrash;

#if defined( _M_IX86 )
	#define HL2SB_CONTEXT_IP( pContext ) ( (void *)(uintptr_t)( ( pContext )->Eip ) )
#elif defined( _M_ARM ) || defined( _M_ARM64 )
	#define HL2SB_CONTEXT_IP( pContext ) ( (void *)(uintptr_t)( ( pContext )->Pc ) )
#else
	#define HL2SB_CONTEXT_IP( pContext ) ( (void *)(uintptr_t)( ( pContext )->Rip ) )
#endif

static bool HL2SB_TryWriteDump( const wchar_t *directory, const wchar_t *kind,
	MINIDUMP_EXCEPTION_INFORMATION *exception, wchar_t (&path)[MAX_PATH] )
{
	if ( !directory[0] )
		return false;

	CreateDirectoryW( directory, NULL );
	SYSTEMTIME now;
	GetLocalTime( &now );
	if ( _snwprintf_s( path, _countof( path ), _TRUNCATE,
			L"%ls\\hl2sb_%ls_%04u%02u%02u_%02u%02u%02u_%lu_%llu.mdmp",
			directory, kind, (unsigned)now.wYear, (unsigned)now.wMonth, (unsigned)now.wDay,
			(unsigned)now.wHour, (unsigned)now.wMinute, (unsigned)now.wSecond,
			GetCurrentProcessId(), GetTickCount64() ) < 0 )
		return false;

	HANDLE output = CreateFileW( path, GENERIC_WRITE, FILE_SHARE_READ, NULL,
		CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL );
	if ( output == INVALID_HANDLE_VALUE )
		return false;

	const DWORD types[] = { g_nHL2SBDumpType, s_nDetailedDump, s_nCompactDump, MiniDumpNormal };
	bool written = false;
	for ( size_t i = 0; i < _countof( types ); ++i )
	{
		if ( i && types[i] == types[i - 1] )
			continue;
		if ( g_pHL2SBDumpWriter( GetCurrentProcess(), GetCurrentProcessId(), output,
				(MINIDUMP_TYPE)types[i], exception, NULL, NULL ) )
		{
			written = true;
			break;
		}
		if ( SetFilePointer( output, 0, NULL, FILE_BEGIN ) == INVALID_SET_FILE_POINTER ||
				!SetEndOfFile( output ) )
			break;
	}
	CloseHandle( output );
	if ( !written )
		DeleteFileW( path );
	return written;
}

static bool HL2SB_WriteWindowsDump( const wchar_t *kind,
	MINIDUMP_EXCEPTION_INFORMATION *exception, wchar_t (&path)[MAX_PATH] )
{
	path[0] = 0;
	if ( !g_pHL2SBDumpWriter || InterlockedCompareExchange( &g_bHL2SBWritingDump, 1, 0 ) )
		return false;

	bool written = HL2SB_TryWriteDump( g_szHL2SBDumpDirectory, kind, exception, path );
	if ( !written )
	{
		wchar_t temporary[MAX_PATH];
		const DWORD length = GetTempPathW( _countof( temporary ), temporary );
		if ( length && length < _countof( temporary ) )
			written = HL2SB_TryWriteDump( temporary, kind, exception, path );
	}

	InterlockedExchange( &g_bHL2SBWritingDump, 0 );
	if ( !written )
		path[0] = 0;
	return written;
}

static void HL2SB_ShowCrashReport( const wchar_t *dumpPath, bool dumpPending = false )
{
	const void *address = g_HL2SBCrash.record.ExceptionAddress;
	MEMORY_BASIC_INFORMATION mapping = {};
	wchar_t module[MAX_PATH] = L"(unknown)";
	uintptr_t offset = 0;
	if ( VirtualQuery( address, &mapping, sizeof( mapping ) ) && mapping.Type == MEM_IMAGE )
	{
		offset = (uintptr_t)address - (uintptr_t)mapping.AllocationBase;
		// If DbgHelp is stalled, it may hold or be waiting on the loader lock.
		if ( !dumpPending )
		{
			const DWORD length = GetModuleFileNameW( (HMODULE)mapping.AllocationBase, module, _countof( module ) );
			if ( !length || length >= _countof( module ) )
				wcscpy_s( module, L"(unknown)" );
		}
	}
	wchar_t pending[MAX_PATH + 160];
	const wchar_t *dumpStatus = dumpPath[0] ? dumpPath : L"Could not write a dump. Check Windows Error Reporting.";
	if ( dumpPending )
	{
		_snwprintf_s( pending, _countof( pending ), _TRUNCATE,
			L"Writing has not finished. Any dump file may be incomplete.\r\nDump folder: %ls",
			g_szHL2SBDumpDirectory[0] ? g_szHL2SBDumpDirectory : L"%TEMP%" );
		dumpStatus = pending;
	}
	wchar_t reason[128] = {};
	wchar_t detail[512] = {};
	MultiByteToWideChar( CP_ACP, 0, g_HL2SBCrash.reason, -1, reason, _countof( reason ) );
	MultiByteToWideChar( CP_ACP, 0, g_HL2SBCrash.detail, -1, detail, _countof( detail ) );
	wchar_t message[3072];
	_snwprintf_s( message, _countof( message ), _TRUNCATE,
		L"HL2SB stopped unexpectedly.\r\n\r\n%ls%ls%ls\r\n"
		L"Exception: 0x%08lX\r\nAddress: 0x%p (module offset 0x%llX)\r\n"
		L"Module: %ls\r\nThread: %lu\r\n\r\nDump: %ls\r\n\r\n"
		L"PDB lookup folders:\r\n%ls\r\n%ls%ls%ls",
		reason, detail[0] ? L"\r\n" : L"", detail,
		g_HL2SBCrash.record.ExceptionCode, address, (unsigned long long)offset, module,
		g_HL2SBCrash.threadId, dumpStatus,
		g_szHL2SBGameSymbols, g_szHL2SBEngineSymbols,
		g_szHL2SBExtraSymbols[0] ? L"\r\n_NT_SYMBOL_PATH: " : L"", g_szHL2SBExtraSymbols );

	wchar_t crashLog[MAX_PATH];
	HANDLE log = INVALID_HANDLE_VALUE;
	if ( g_szHL2SBDumpDirectory[0] && _snwprintf_s( crashLog, _countof( crashLog ), _TRUNCATE,
			L"%ls\\..\\hl2sb_crash.log", g_szHL2SBDumpDirectory ) >= 0 )
	{
		log = CreateFileW( crashLog, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
			NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL );
	}
	if ( log != INVALID_HANDLE_VALUE )
	{
		char utf8[sizeof( message ) * 2];
		const int n = WideCharToMultiByte( CP_UTF8, 0, message, -1, utf8, sizeof( utf8 ), NULL, NULL );
		if ( n > 1 )
		{
			DWORD written;
			WriteFile( log, utf8, (DWORD)n - 1, &written, NULL );
			WriteFile( log, "\r\n\r\n", 4, &written, NULL );
		}
		CloseHandle( log );
	}

	MessageBoxW( NULL, message, L"HL2SB Crash Report", MB_OK | MB_ICONERROR | MB_SETFOREGROUND );
}

static void HL2SB_CheckHang();

static DWORD WINAPI HL2SB_CrashReporterThread( LPVOID module )
{
	for ( ;; )
	{
		const DWORD wait = WaitForSingleObject( g_hHL2SBCrashRequest, 2000 );
		if ( InterlockedCompareExchange( &g_bHL2SBCrashThreadExit, 0, 0 ) )
			break;
		if ( wait == WAIT_TIMEOUT )
		{
			HL2SB_CheckHang();
			continue;
		}
		if ( wait != WAIT_OBJECT_0 )
			break;

		hl2sb_loghandle_t log = HL2SB_EngineLogOpen();
		HL2SB_EngineLogLine( log, "begin kind=%s code=0x%08lX address=%p thread=%lu",
			g_HL2SBCrash.reason, g_HL2SBCrash.record.ExceptionCode,
			g_HL2SBCrash.record.ExceptionAddress, g_HL2SBCrash.threadId );
		HL2SB_EngineLogClose( log );

		// DbgHelp needs the original thread id and context, not this worker's.
		EXCEPTION_POINTERS pointers = { &g_HL2SBCrash.record, &g_HL2SBCrash.context };
		MINIDUMP_EXCEPTION_INFORMATION info = { g_HL2SBCrash.threadId, &pointers, FALSE };
		HL2SB_WriteWindowsDump( L"crash", &info, g_szHL2SBCrashDumpPath );
		if ( g_hHL2SBCrashDumpReady )
			SetEvent( g_hHL2SBCrashDumpReady );
		log = HL2SB_EngineLogOpen();
		HL2SB_EngineLogLine( log, "dump=%s", g_szHL2SBCrashDumpPath[0] ? "written (see hl2sb_crash.log)" : "write failed" );
		HL2SB_EngineLogLine( log, "end" );
		HL2SB_EngineLogClose( log );
		break;
	}

	FreeLibraryAndExitThread( (HMODULE)module, 0 );
}

static DWORD WINAPI HL2SB_CrashNotifyThread( LPVOID module )
{
	if ( WaitForSingleObject( g_hHL2SBCrashNotify, INFINITE ) == WAIT_OBJECT_0 &&
		!InterlockedCompareExchange( &g_bHL2SBCrashThreadExit, 0, 0 ) )
	{
		const DWORD result = WaitForSingleObject( g_hHL2SBCrashDumpReady, s_nCrashDumpReportWaitMs );
		if ( !InterlockedCompareExchange( &g_bHL2SBCrashThreadExit, 0, 0 ) )
		{
			const bool ready = result == WAIT_OBJECT_0;
			// The writer can still be updating the path when the wait expires.
			HL2SB_ShowCrashReport( ready ? g_szHL2SBCrashDumpPath : L"", !ready );
			SetEvent( g_hHL2SBCrashDone );
		}
	}
	FreeLibraryAndExitThread( (HMODULE)module, 0 );
}

static void HL2SB_ReportCrash( const char *reason, const char *detail, EXCEPTION_POINTERS *exception )
{
	if ( !exception || !exception->ExceptionRecord || !exception->ContextRecord )
		return;
	const DWORD threadId = GetCurrentThreadId();
	if ( threadId == g_nHL2SBCrashThreadId || threadId == g_nHL2SBCrashNotifyThreadId )
		return;
	if ( InterlockedCompareExchange( &g_bHL2SBCrashPending, 1, 0 ) )
	{
		// A second fatal exception should not tear down the process while
		// the first is still writing. Do not wait on reentry on that thread.
		if ( g_hHL2SBCrashThread && threadId != g_HL2SBCrash.threadId )
			WaitForSingleObject( g_hHL2SBCrashDone, 60000 );
		return;
	}
	// Keep this capture valid even if the reporting wait times out.
	g_HL2SBCrash.record = *exception->ExceptionRecord;
	g_HL2SBCrash.record.ExceptionRecord = NULL;
	g_HL2SBCrash.context = *exception->ContextRecord;
	g_HL2SBCrash.reason = reason;
	g_HL2SBCrash.threadId = threadId;
	strncpy_s( g_HL2SBCrash.detail, detail ? detail : "", _TRUNCATE );

	if ( g_hHL2SBCrashThread )
		SetEvent( g_hHL2SBCrashRequest );
	if ( g_hHL2SBCrashNotifyThread )
	{
		SetEvent( g_hHL2SBCrashNotify );
		WaitForSingleObject( g_hHL2SBCrashDone, 60000 );
	}
	else
	{
		// Startup may have failed to create a worker. Never run DbgHelp
		// synchronously on the exception thread.
		const bool ready = g_hHL2SBCrashThread && g_hHL2SBCrashDumpReady &&
			WaitForSingleObject( g_hHL2SBCrashDumpReady, s_nCrashDumpReportWaitMs ) == WAIT_OBJECT_0;
		if ( g_HL2SBCrash.record.ExceptionCode != EXCEPTION_STACK_OVERFLOW )
			HL2SB_ShowCrashReport( ready ? g_szHL2SBCrashDumpPath : L"", g_hHL2SBCrashThread && !ready );
		if ( g_hHL2SBCrashDone )
			SetEvent( g_hHL2SBCrashDone );
	}
}

static void HL2SB_WriteAbortDump( const char *reason, const char *detail )
{
	CONTEXT context;
	RtlCaptureContext( &context );
	EXCEPTION_RECORD record = {};
	record.ExceptionCode = 0xC0000409;
	record.ExceptionAddress = HL2SB_CONTEXT_IP( &context );
	record.ExceptionFlags = EXCEPTION_NONCONTINUABLE;
	EXCEPTION_POINTERS pointers = { &record, &context };
	HL2SB_ReportCrash( reason, detail, &pointers );
}
//-----------------------------------------------------------------------------
// SIGABRT -- the ONLY in-process hook that fires for a bare abort().
//
// UCRT's abort() is:
//     if ( __acrt_get_sigabrt_handler() )  raise( SIGABRT );
//     if ( __abort_behavior & _CALL_REPORTFAULT )
//         __fastfail( FAST_FAIL_FATAL_APP_EXIT );   // int 29h
//     _exit( 3 );
//
// That `int 29h` is the kernel fast-fail: it bypasses SEH, vectored handlers
// and every debugger-independent mechanism, which is why a bare abort() left
// no dump at all.  Registering a SIGABRT handler makes raise() call us *before*
// the fast-fail, with a live stack -- and gives us the actual caller.
//-----------------------------------------------------------------------------
static void __cdecl HL2SB_SigabrtHandler( int )
{
	HL2SB_WriteAbortDump( "SIGABRT / abort()", NULL );

	// Never return into abort(): the next thing it does is int 29h.
	_exit( 3 );
}

//-----------------------------------------------------------------------------
// std::terminate -- an unhandled C++ exception (this is the usual route to
// abort() in a /EHsc build).
//-----------------------------------------------------------------------------
static void __cdecl HL2SB_TerminateHandler( void )
{
	HL2SB_WriteAbortDump( "std::terminate (unhandled C++ exception)", NULL );
	_exit( 3 );
}

//-----------------------------------------------------------------------------
// CRT invalid parameter -- sprintf_s/strcpy_s/vsnprintf with bad arguments.
//-----------------------------------------------------------------------------
static void __cdecl HL2SB_InvalidParameterHandler(
	const wchar_t *pszExpression, const wchar_t *pszFunction,
	const wchar_t *pszFile, unsigned int uiLine, uintptr_t /*pReserved*/ )
{
	char szDetail[ 512 ];
	char szExpr[ 192 ] = { 0 };
	char szFunc[ 192 ] = { 0 };

	if ( pszExpression )
		WideCharToMultiByte( CP_ACP, 0, pszExpression, -1, szExpr, sizeof( szExpr ), NULL, NULL );
	if ( pszFunction )
		WideCharToMultiByte( CP_ACP, 0, pszFunction, -1, szFunc, sizeof( szFunc ), NULL, NULL );

	Q_snprintf( szDetail, sizeof( szDetail ), "expr=\"%s\" func=\"%s\" line=%u",
		szExpr, szFunc, uiLine );

	HL2SB_WriteAbortDump( "CRT invalid parameter", szDetail );

	// Do not return: the caller is in an unrecoverable state.
	ExitProcess( 3 );
}

//-----------------------------------------------------------------------------
// Pure virtual call.
//-----------------------------------------------------------------------------
static void __cdecl HL2SB_PurecallHandler( void )
{
	// Same re-entry reasoning as g_bHL2SBMiniDumpChainActive (2026-09-19):
	// a purecall raised while writing this dump must not recurse here.
	static volatile LONG s_bPurecallEntered = 0;
	if ( InterlockedCompareExchange( &s_bPurecallEntered, 1, 0 ) != 0 )
		_exit( 3 );

	HL2SB_WriteAbortDump( "pure virtual function call", NULL );
	_exit( 3 );
}

//-----------------------------------------------------------------------------
// The engine writes most of its own minidumps: CEngineAPI::Run() wraps the whole
// listen server in CatchAndWriteMiniDump() (tier0/minidump.cpp), and that
// __except runs g_pfnWriteMiniDump *before* the process unhandled-exception
// filter is consulted.  HL2SB_ExceptionFilter therefore never ran for those
// crashes - which is exactly why hl2sb_crash.log was sometimes missing entirely.
//
// SetMiniDumpFunction() is that same pointer and is an existing tier0 export
// (no new interface, nothing that can break loading against a deployed
// tier0.dll).  Chaining it puts the engine.log block on disk for every dump the
// engine writes, and does so *before* handing over, so the trace survives even
// if the dump writing itself hangs or faults.
//-----------------------------------------------------------------------------
static FnMiniDump g_pHL2SBInnerMiniDumpFunction = NULL;

static void __cdecl HL2SB_MiniDumpChain( unsigned int code, EXCEPTION_POINTERS *exception,
	const char *suffix )
{
	// WriteMiniDump (diagnostic/assert/long tick) synthesizes a breakpoint.
	// Preserve that path independently of the terminal exception reports.
	if ( code == EXCEPTION_BREAKPOINT || !exception || !exception->ContextRecord )
	{
		static volatile LONG writingDiagnostic = 0;
		if ( InterlockedCompareExchange( &writingDiagnostic, 1, 0 ) )
			return;
		if ( g_pHL2SBInnerMiniDumpFunction )
			g_pHL2SBInnerMiniDumpFunction( code, exception, suffix );
		InterlockedExchange( &writingDiagnostic, 0 );
		return;
	}
	if ( !g_pHL2SBDumpWriter && g_pHL2SBInnerMiniDumpFunction )
		g_pHL2SBInnerMiniDumpFunction( code, exception, suffix );
	HL2SB_ReportCrash( "Engine exception", suffix, exception );
}

// A direct CPU fast-fail does not enter user-mode handlers. If Windows does
// dispatch a noncontinuable heap/fast-fail exception, capture it before SEH.
static LONG CALLBACK HL2SB_VectoredHandler( PEXCEPTION_POINTERS exception )
{
	if ( exception && exception->ExceptionRecord &&
			( exception->ExceptionRecord->ExceptionFlags & EXCEPTION_NONCONTINUABLE ) )
	{
		const DWORD code = exception->ExceptionRecord->ExceptionCode;
		if ( code == 0xC0000374 || code == 0xC0000409 )
			HL2SB_ReportCrash( "Noncontinuable Windows exception", NULL, exception );
	}
	return EXCEPTION_CONTINUE_SEARCH;
}

static LONG WINAPI HL2SB_ExceptionFilter( LPEXCEPTION_POINTERS exception )
{
	HL2SB_ReportCrash( "Unhandled Windows exception", NULL, exception );
	// Allow Windows Error Reporting to recover the report if our writer failed.
	return EXCEPTION_CONTINUE_SEARCH;
}
//-----------------------------------------------------------------------------
// Purpose: Hang watchdog.  Every exception path above needs an exception; a deadlock
//		 or an infinite loop produces none, and the field symptom is exactly that:
//		 engine.log stops mid-sentence, no minidump, no hl2sb_crash.log entry, no WER
//		 report - because nothing ever faulted.  The main thread ticks here once per
//		 rendered frame (CHLClient::RenderView), and this watcher thread writes a dump
//		 of the still-frozen process from its own healthy stack when the ticks stop.
//-----------------------------------------------------------------------------
static volatile LONG64 g_llHL2SBAliveTick = 0;
static volatile LONG	 g_bHL2SBHangDumped = 0;

#define HL2SB_HANG_TIMEOUT_MS ( 30 * 1000 )	// longer than any legitimate level load

void HL2SB_NotifyAlive( void )
{
	InterlockedExchange64( &g_llHL2SBAliveTick, (LONG64)GetTickCount64() );
}

static void HL2SB_CheckHang()
{
	const LONG64 alive = InterlockedCompareExchange64( &g_llHL2SBAliveTick, 0, 0 );
	if ( !alive || InterlockedCompareExchange( &g_bHL2SBCrashPending, 0, 0 ) )
		return;

	const LONG64 silent = (LONG64)GetTickCount64() - alive;
	if ( silent < HL2SB_HANG_TIMEOUT_MS || InterlockedCompareExchange( &g_bHL2SBHangDumped, 1, 0 ) )
		return;

	HL2SB_EngineLogCrashBlock( "hang-watchdog", (unsigned int)silent, NULL,
		"no rendered frame for this long", 0 );
	wchar_t dumpPath[MAX_PATH];
	HL2SB_WriteWindowsDump( L"hang", NULL, dumpPath );
}
// Everything registered below is process-global but lives in this DLL's image.
// The engine unloads client.dll during shutdown while these hooks stay
// registered: the next exception - even the benign first-chance thread-exit
// notification raised on the final ExitThread - then dispatches straight into
// freed memory.  Under the ARM64 x64 emulator that fault happens inside the
// call-dispatch stub itself, which re-faults on every nested dispatch and
// nests hundreds of exception records until the 1 MB stack is gone: the
// "exit game after visiting a map" crash.  Save everything needed to take the
// hooks back down before the unload.
static void *g_pHL2SBVehHandle = NULL;
static LPTOP_LEVEL_EXCEPTION_FILTER g_pHL2SBPrevFilter = NULL;
static terminate_handler g_pHL2SBPrevTerminate = NULL;
static _invalid_parameter_handler g_pHL2SBPrevInvalidParameter = NULL;
static _purecall_handler g_pHL2SBPrevPurecall = NULL;
static void ( __cdecl *g_pHL2SBPrevSigabrt )( int ) = NULL;
static bool g_bHL2SBCrashHandlerInstalled = false;

static HANDLE HL2SB_StartCrashReporterThread( LPTHREAD_START_ROUTINE start, DWORD *threadId )
{
	// Keep the client DLL mapped if DbgHelp or the dialog outlives shutdown.
	HMODULE module = NULL;
	if ( !GetModuleHandleExW( GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, (LPCWSTR)start, &module ) )
		return NULL;
	HANDLE thread = CreateThread( NULL, 0, start, module, 0, threadId );
	if ( !thread )
		FreeLibrary( module );
	return thread;
}

static void HL2SB_InitCrashReporter()
{
	if ( g_hHL2SBCrashThread )
		return;
	wchar_t rawGameDirectory[MAX_PATH] = {};
	const char *game = engine ? engine->GetGameDirectory() : NULL;
	if ( game && MultiByteToWideChar( CP_ACP, 0, game, -1, rawGameDirectory, _countof( rawGameDirectory ) ) )
	{
		wchar_t gameDirectory[MAX_PATH];
		const DWORD length = GetFullPathNameW( rawGameDirectory, _countof( gameDirectory ), gameDirectory, NULL );
		if ( length && length < _countof( gameDirectory ) )
		{
			_snwprintf_s( g_szHL2SBDumpDirectory, _countof( g_szHL2SBDumpDirectory ), _TRUNCATE,
				L"%ls\\dumps", gameDirectory );
			_snwprintf_s( g_szHL2SBGameSymbols, _countof( g_szHL2SBGameSymbols ), _TRUNCATE,
				L"%ls\\bin", gameDirectory );
		}
	}
	const DWORD exeLength = GetModuleFileNameW( NULL, g_szHL2SBEngineSymbols, _countof( g_szHL2SBEngineSymbols ) );
	if ( exeLength && exeLength < _countof( g_szHL2SBEngineSymbols ) )
	{
		wchar_t *separator = wcsrchr( g_szHL2SBEngineSymbols, L'\\' );
		if ( separator )
			wcscpy_s( separator, _countof( g_szHL2SBEngineSymbols ) - ( separator - g_szHL2SBEngineSymbols ), L"\\bin" );
	}
	else
		g_szHL2SBEngineSymbols[0] = 0;
	GetEnvironmentVariableW( L"_NT_SYMBOL_PATH", g_szHL2SBExtraSymbols, _countof( g_szHL2SBExtraSymbols ) );
	wchar_t full[8] = {};
	const DWORD fullLength = GetEnvironmentVariableW( L"HL2SB_FULL_DUMP", full, _countof( full ) );
	g_nHL2SBDumpType = s_nDetailedDump;
	if ( fullLength == 1 && full[0] == L'1' )
		g_nHL2SBDumpType |= MiniDumpWithFullMemory;

	// Load outside the fault path, when the process and loader lock are healthy.
	g_hHL2SBDbgHelp = LoadLibraryExW( L"dbghelp.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32 );
	if ( g_hHL2SBDbgHelp )
		g_pHL2SBDumpWriter = (HL2SB_MiniDumpWriteFn)GetProcAddress( g_hHL2SBDbgHelp, "MiniDumpWriteDump" );

	InterlockedExchange( &g_bHL2SBCrashThreadExit, 0 );
	InterlockedExchange( &g_bHL2SBCrashPending, 0 );
	g_szHL2SBCrashDumpPath[0] = 0;
	g_hHL2SBCrashDone = CreateEventW( NULL, TRUE, FALSE, NULL );
	g_hHL2SBCrashRequest = CreateEventW( NULL, FALSE, FALSE, NULL );
	g_hHL2SBCrashDumpReady = CreateEventW( NULL, TRUE, FALSE, NULL );
	g_hHL2SBCrashNotify = CreateEventW( NULL, FALSE, FALSE, NULL );
	if ( g_hHL2SBCrashDone && g_hHL2SBCrashRequest )
		g_hHL2SBCrashThread = HL2SB_StartCrashReporterThread( HL2SB_CrashReporterThread, &g_nHL2SBCrashThreadId );
	if ( g_hHL2SBCrashThread && g_hHL2SBCrashDumpReady && g_hHL2SBCrashNotify )
		g_hHL2SBCrashNotifyThread = HL2SB_StartCrashReporterThread( HL2SB_CrashNotifyThread, &g_nHL2SBCrashNotifyThreadId );

	// The guarantee is per-thread. The main thread is the one installing us.
	ULONG stackBytes = 32 * 1024;
	SetThreadStackGuarantee( &stackBytes );
}

static void HL2SB_StopCrashReporter()
{
	HANDLE workers[2];
	DWORD count = 0;
	if ( g_hHL2SBCrashThread )
		workers[count++] = g_hHL2SBCrashThread;
	if ( g_hHL2SBCrashNotifyThread )
		workers[count++] = g_hHL2SBCrashNotifyThread;
	if ( count )
	{
		InterlockedExchange( &g_bHL2SBCrashThreadExit, 1 );
		SetEvent( g_hHL2SBCrashRequest );
		SetEvent( g_hHL2SBCrashDone );
		if ( g_hHL2SBCrashNotifyThread )
		{
			SetEvent( g_hHL2SBCrashNotify );
			SetEvent( g_hHL2SBCrashDumpReady );
		}
		if ( WaitForMultipleObjects( count, workers, TRUE, 5000 ) != WAIT_OBJECT_0 )
			return; // A worker may still need the handles and owns a DLL reference.
		for ( DWORD i = 0; i < count; ++i )
			CloseHandle( workers[i] );
		g_hHL2SBCrashThread = NULL;
		g_hHL2SBCrashNotifyThread = NULL;
		g_nHL2SBCrashThreadId = 0;
		g_nHL2SBCrashNotifyThreadId = 0;
	}
	if ( g_hHL2SBCrashRequest )
		CloseHandle( g_hHL2SBCrashRequest );
	if ( g_hHL2SBCrashDone )
		CloseHandle( g_hHL2SBCrashDone );
	if ( g_hHL2SBCrashDumpReady )
		CloseHandle( g_hHL2SBCrashDumpReady );
	if ( g_hHL2SBCrashNotify )
		CloseHandle( g_hHL2SBCrashNotify );
	g_hHL2SBCrashRequest = g_hHL2SBCrashDone = NULL;
	g_hHL2SBCrashDumpReady = g_hHL2SBCrashNotify = NULL;
	g_pHL2SBDumpWriter = NULL;
	if ( g_hHL2SBDbgHelp )
		FreeLibrary( g_hHL2SBDbgHelp );
	g_hHL2SBDbgHelp = NULL;
}
void HL2SB_InstallCrashHandler( void )
{
	if ( g_bHL2SBCrashHandlerInstalled )
		return;

	HL2SB_InitCrashReporter();
	g_pHL2SBPrevFilter = SetUnhandledExceptionFilter( HL2SB_ExceptionFilter );

	// First-chance net: __fastfail / heap corruption / stack overflow never reach the
	// filter above, and without this the process just vanishes with engine.log
	// stopping mid-sentence and no dump anywhere.
	g_pHL2SBVehHandle = AddVectoredExceptionHandler( 1, HL2SB_VectoredHandler );

	// The abort paths that never reach the filter above.
	g_pHL2SBPrevTerminate = std::set_terminate( HL2SB_TerminateHandler );
	g_pHL2SBPrevInvalidParameter = _set_invalid_parameter_handler( HL2SB_InvalidParameterHandler );
	g_pHL2SBPrevPurecall = _set_purecall_handler( HL2SB_PurecallHandler );

	// Bare abort(): only SIGABRT fires before the int 29h fast-fail.
	g_pHL2SBPrevSigabrt = signal( SIGABRT, HL2SB_SigabrtHandler );

	// Crashes the engine catches itself (CatchAndWriteMiniDump) never reach the
	// unhandled-exception filter; chain its dump writer so engine.log gets them.
	g_pHL2SBInnerMiniDumpFunction = SetMiniDumpFunction( HL2SB_MiniDumpChain );

	g_bHL2SBCrashHandlerInstalled = true;

	Msg( "[HL2SB] Crash handler installed (SEH + SIGABRT + terminate/invalid-parameter/purecall + minidump chain; logs: engine.log + hl2sb_crash.log)\n" );
}

void HL2SB_UninstallCrashHandler( void )
{
	if ( !g_bHL2SBCrashHandlerInstalled )
		return;
	g_bHL2SBCrashHandlerInstalled = false;

	// The vectored entry is the storm source: the fault happens inside the
	// dispatcher's call into the (now freed) handler address, before any of our
	// code could run - the entry itself has to be gone before the unload.
	if ( g_pHL2SBVehHandle )
	{
		RemoveVectoredExceptionHandler( g_pHL2SBVehHandle );
		g_pHL2SBVehHandle = NULL;
	}

	SetUnhandledExceptionFilter( g_pHL2SBPrevFilter );
	g_pHL2SBPrevFilter = NULL;

	std::set_terminate( g_pHL2SBPrevTerminate );
	g_pHL2SBPrevTerminate = NULL;
	_set_invalid_parameter_handler( g_pHL2SBPrevInvalidParameter );
	g_pHL2SBPrevInvalidParameter = NULL;
	_set_purecall_handler( g_pHL2SBPrevPurecall );
	g_pHL2SBPrevPurecall = NULL;
	signal( SIGABRT, g_pHL2SBPrevSigabrt ? g_pHL2SBPrevSigabrt : SIG_DFL );
	g_pHL2SBPrevSigabrt = NULL;

	SetMiniDumpFunction( g_pHL2SBInnerMiniDumpFunction );
	g_pHL2SBInnerMiniDumpFunction = NULL;
	HL2SB_StopCrashReporter();

	Msg( "[HL2SB] Crash handler removed (client.dll unload pending)\n" );
}

#elif defined( LINUX ) && !defined( ANDROID )

//-----------------------------------------------------------------------------
// Linux.  This tree has no minidump writer for !_WIN32 (tier0/minidump.cpp is a
// no-op there and CEngineAPI::Run only installs a handler under _WIN32), so
// before this a crash left nothing behind but a core dump.  Every signal funnels
// into the same engine.log block the Windows side writes.
//-----------------------------------------------------------------------------

#define HL2SB_ALTSTACK_SIZE ( 64 * 1024 )

static volatile sig_atomic_t g_bHL2SBInCrashHandler = 0;
static char g_szHL2SBAltStack[ HL2SB_ALTSTACK_SIZE ];

// Same contract as the Windows branch: the signal actions live in this DLL's
// image but are process-global and survive dlclose.  Save what was there
// before so the shutdown path can put the originals back.
static const int g_nHL2SBSignals[] = { SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT };
static struct sigaction g_prevHL2SBSigaction[ 5 ];
static bool g_bHL2SBSignalActionsSaved = false;
static bool g_bHL2SBAltStackInstalled = false;
static std::terminate_handler g_pHL2SBPrevTerminate = NULL;

static const char *HL2SB_SignalName( int nSignal )
{
	switch ( nSignal )
	{
	case SIGSEGV:	return "SIGSEGV";
	case SIGBUS:	return "SIGBUS";
	case SIGFPE:	return "SIGFPE";
	case SIGILL:	return "SIGILL";
	case SIGABRT:	return "SIGABRT";
	default:		return "signal";
	}
}

static void HL2SB_PosixSignalHandler( int nSignal, siginfo_t *pInfo, void *pContext )
{
	( void )pContext;

	// A fault inside the crash path itself (stack overflow, or the trace touching
	// unmapped memory) would recurse until the kernel killed us: record only the
	// first one and let the default action take over.
	if ( g_bHL2SBInCrashHandler )
	{
		signal( nSignal, SIG_DFL );
		raise( nSignal );
		return;
	}
	g_bHL2SBInCrashHandler = 1;

	HL2SB_EngineLogCrashBlock( HL2SB_SignalName( nSignal ), 0,
		pInfo ? pInfo->si_addr : NULL, NULL, 2 );

	g_bHL2SBInCrashHandler = 0;

	// Die the way we would have: restore the default action and re-raise (the
	// signal stays pending until we return from the handler).
	signal( nSignal, SIG_DFL );
	raise( nSignal );
}

static void HL2SB_PosixTerminateHandler( void )
{
	HL2SB_EngineLogCrashBlock( "std::terminate (unhandled C++ exception)", 0, NULL, NULL, 2 );
	abort();
}

void HL2SB_InstallCrashHandler( void )
{
	if ( g_bHL2SBSignalActionsSaved )
		return;

	// A stack overflow cannot run a handler on the stack it just exhausted.
	stack_t altStack;
	altStack.ss_sp = g_szHL2SBAltStack;
	altStack.ss_size = sizeof( g_szHL2SBAltStack );
	altStack.ss_flags = 0;
	if ( sigaltstack( &altStack, NULL ) == 0 )
		g_bHL2SBAltStackInstalled = true;

	struct sigaction act;
	memset( &act, 0, sizeof( act ) );
	act.sa_sigaction = HL2SB_PosixSignalHandler;
	act.sa_flags = SA_SIGINFO | SA_ONSTACK;
	sigemptyset( &act.sa_mask );

	for ( int i = 0; i < 5; i++ )
		sigaction( g_nHL2SBSignals[ i ], &act, &g_prevHL2SBSigaction[ i ] );
	g_bHL2SBSignalActionsSaved = true;

	g_pHL2SBPrevTerminate = std::set_terminate( HL2SB_PosixTerminateHandler );

	Msg( "[HL2SB] Crash handler installed (SIGSEGV/SIGBUS/SIGFPE/SIGILL/SIGABRT -> %s)\n",
		HL2SB_ENGINE_LOG_NAME );
}

void HL2SB_UninstallCrashHandler( void )
{
	if ( !g_bHL2SBSignalActionsSaved )
		return;
	g_bHL2SBSignalActionsSaved = false;

	// Put the original actions back before this DLL is unloaded, or the next
	// signal jumps into freed code.
	for ( int i = 0; i < 5; i++ )
		sigaction( g_nHL2SBSignals[ i ], &g_prevHL2SBSigaction[ i ], NULL );

	if ( g_bHL2SBAltStackInstalled )
	{
		stack_t dis;
		dis.ss_sp = NULL;
		dis.ss_size = 0;
		dis.ss_flags = SS_DISABLE;
		sigaltstack( &dis, NULL );
		g_bHL2SBAltStackInstalled = false;
	}

	std::set_terminate( g_pHL2SBPrevTerminate );
	g_pHL2SBPrevTerminate = NULL;

	Msg( "[HL2SB] Crash handler removed (client.so unload pending)\n" );
}

#else

void HL2SB_InstallCrashHandler( void )
{
	// Not our platform to change here: Android's launcher already funnels crashes
	// into engine.log (launcher/android/crashhandler.cpp -> DebugLogger()->Write),
	// and the other POSIX targets (macOS/BSD) are out of scope for this pass.
}

void HL2SB_UninstallCrashHandler( void )
{
}

#endif

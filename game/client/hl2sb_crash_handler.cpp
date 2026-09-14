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

// MINIDUMP_TYPE: DataSegs | IndirectlyReferencedMemory | ProcessThreadData.
// Same shape as the engine's own dumps (~29 MB, enough for a usable stack).
#define HL2SB_MINIDUMP_TYPE ( 0x00000001 | 0x00000040 | 0x00000100 )

// CONTEXT names its instruction pointer per architecture.  32-bit x86 has no
// Rip at all - that is what the win32 CI job failed on (C2039: 'Rip' is not a
// member of '_CONTEXT').
#if defined( _M_IX86 )
	#define HL2SB_CONTEXT_IP( pContext )	( (void *)(uintptr_t)( ( pContext )->Eip ) )
#elif defined( _M_ARM ) || defined( _M_ARM64 )
	#define HL2SB_CONTEXT_IP( pContext )	( (void *)(uintptr_t)( ( pContext )->Pc ) )
#else
	#define HL2SB_CONTEXT_IP( pContext )	( (void *)(uintptr_t)( ( pContext )->Rip ) )
#endif

//-----------------------------------------------------------------------------
// Purpose: Write a crash log entry plus a minidump for a CRT abort path.
//          Called while the process is still alive, so the stack is real.
//-----------------------------------------------------------------------------
static void HL2SB_WriteAbortDump( const char *pszReason, const char *pszDetail )
{
	// The faulting context, captured while the stack is still intact.
	CONTEXT ctx;
	RtlCaptureContext( &ctx );

	// engine.log first, and with the raw writer: it must not depend on the CRT
	// heap or on stdio still being usable.
	HL2SB_EngineLogCrashBlock( pszReason, 0xC0000409, HL2SB_CONTEXT_IP( &ctx ),
		pszDetail, 3 );

	FILE *fp = fopen( "hl2sb_crash.log", "a" );
	if ( fp )
	{
		fprintf( fp, "\n=== HL2SB Crash (CRT abort) ===\n" );
		fprintf( fp, "Reason: %s\n", pszReason );
		if ( pszDetail )
			fprintf( fp, "Detail: %s\n", pszDetail );

		void *stack[ 96 ];
		unsigned short frames = RtlCaptureStackBackTrace( 1, 96, stack, NULL );
		fprintf( fp, "\nStack Trace (%u frames):\n", frames );
		for ( unsigned int i = 0; i < frames; i++ )
			fprintf( fp, "  %02u: 0x%p\n", i, stack[ i ] );

		fprintf( fp, "\n=== End Crash ===\n" );
		fflush( fp );
		fclose( fp );
	}

	// Minidump: build a synthetic exception record around the current context so
	// the dump has a walkable stack (the fastfail path gives us none).
	EXCEPTION_RECORD rec;
	ZeroMemory( &rec, sizeof( rec ) );
	rec.ExceptionCode = 0xC0000409;				// STATUS_STACK_BUFFER_OVERRUN
	rec.ExceptionAddress = HL2SB_CONTEXT_IP( &ctx );
	rec.ExceptionFlags = EXCEPTION_NONCONTINUABLE;

	EXCEPTION_POINTERS info;
	info.ExceptionRecord = &rec;
	info.ContextRecord = &ctx;

	WriteMiniDumpUsingExceptionInfo( 0xC0000409, &info, HL2SB_MINIDUMP_TYPE, "abort" );

	Msg( "\n[HL2SB] CRASH (abort): %s%s%s\n", pszReason,
		pszDetail ? " - " : "", pszDetail ? pszDetail : "" );
	Msg( "[HL2SB] Crash log: engine.log + hl2sb_crash.log, minidump: dumps/\n" );

	char szBox[ 512 ];
	Q_snprintf( szBox, sizeof( szBox ),
		"HL2SB crashed!\n\nReason: %s\n%s\n\nSee engine.log, hl2sb_crash.log and the dumps/ folder.",
		pszReason, pszDetail ? pszDetail : "" );
	MessageBoxA( NULL, szBox, "HL2SB Crash", MB_OK | MB_ICONERROR );
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
	abort();
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
	HL2SB_WriteAbortDump( "pure virtual function call", NULL );
	abort();
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

static void __cdecl HL2SB_MiniDumpChain( unsigned int uStructuredExceptionCode,
	_EXCEPTION_POINTERS *pExceptionInfo, const char *pszFilenameSuffix )
{
	const void *pAddress = NULL;
	if ( pExceptionInfo && pExceptionInfo->ExceptionRecord )
		pAddress = pExceptionInfo->ExceptionRecord->ExceptionAddress;

	HL2SB_EngineLogCrashBlock( "minidump", uStructuredExceptionCode, pAddress,
		pszFilenameSuffix, 3 );

	if ( g_pHL2SBInnerMiniDumpFunction )
		g_pHL2SBInnerMiniDumpFunction( uStructuredExceptionCode, pExceptionInfo, pszFilenameSuffix );
}

//-----------------------------------------------------------------------------
// Purpose: The last net - a vectored (first-chance) handler for the non-continuable
//		 codes that reach neither the CRT hooks above nor SetUnhandledExceptionFilter.
//		 ntdll's heap manager reports corruption, and __fastfail(), by raising
//		 STATUS_STACK_BUFFER_OVERRUN / STATUS_HEAP_CORRUPTION straight through the
//		 exception dispatch; the process can then be torn down before the unhandled
//		 filter ever runs.  The field symptom of exactly that is engine.log simply
//		 stopping mid-sentence with no minidump and no hl2sb_crash.log entry.
//		 Vectored handlers run before SEH, so this sees it while the stacks are
//		 still intact and the raw engine.log writer still works.
//-----------------------------------------------------------------------------
#ifndef STATUS_HEAP_CORRUPTION
	#define STATUS_HEAP_CORRUPTION		( (DWORD)0xC0000374 )
#endif
#ifndef STATUS_STACK_BUFFER_OVERRUN
	#define STATUS_STACK_BUFFER_OVERRUN	( (DWORD)0xC0000409 )
#endif
#ifndef STATUS_ASSERTION_FAILURE
	#define STATUS_ASSERTION_FAILURE	( (DWORD)0xC0000420 )
#endif

static volatile LONG g_bHL2SBVehFired = 0;

static LONG CALLBACK HL2SB_VectoredHandler( PEXCEPTION_POINTERS pExceptionInfo )
{
	if ( !pExceptionInfo || !pExceptionInfo->ExceptionRecord )
		return EXCEPTION_CONTINUE_SEARCH;

	const DWORD code = pExceptionInfo->ExceptionRecord->ExceptionCode;

	// First-chance dispatch sees plenty of benign exceptions; only these few are
	// process-ending and get past the other hooks.
	if ( code != STATUS_STACK_BUFFER_OVERRUN
	  && code != STATUS_HEAP_CORRUPTION
	  && code != STATUS_ASSERTION_FAILURE
	  && code != EXCEPTION_STACK_OVERFLOW )
	{
		return EXCEPTION_CONTINUE_SEARCH;
	}

	if ( InterlockedCompareExchange( &g_bHL2SBVehFired, 1, 0 ) != 0 )
		return EXCEPTION_CONTINUE_SEARCH;	// already reported in this process

	// engine.log only, deliberately: no Msg()/Warning() (they allocate, and the heap
	// may be the thing that just died) and no MessageBox (it would block teardown).
	HL2SB_EngineLogCrashBlock( "veh fastfail/heap", (unsigned int)code,
		pExceptionInfo->ExceptionRecord->ExceptionAddress, NULL, 0 );

	// A stack overflow has no stack left to run a dump writer on; the log block is
	// all it can get.  For the others, write a real minidump.
	if ( code != EXCEPTION_STACK_OVERFLOW )
	{
		WriteMiniDumpUsingExceptionInfo( (unsigned int)code, pExceptionInfo,
			HL2SB_MINIDUMP_TYPE, "veh" );
	}

	return EXCEPTION_CONTINUE_SEARCH;	// let the normal teardown / WER run as well
}

static LONG WINAPI HL2SB_ExceptionFilter( LPEXCEPTION_POINTERS lpExceptionInfo )
{
	// engine.log first (raw append, no CRT): this is the one place that always
	// has a home, whatever happens to the CRT or to the dumps/ folder.
	HL2SB_EngineLogCrashBlock( "SEH unhandled exception",
		(unsigned int)lpExceptionInfo->ExceptionRecord->ExceptionCode,
		lpExceptionInfo->ExceptionRecord->ExceptionAddress,
		NULL, 3 );

	char szLogPath[MAX_PATH];
	Q_snprintf( szLogPath, sizeof(szLogPath), "hl2sb_crash.log" );

	// Write crash info to log
	FILE *fp = fopen( szLogPath, "a" );
	if ( fp )
	{
		fprintf( fp, "\n=== HL2SB Crash ===\n" );
		fprintf( fp, "Exception Code: 0x%08X\n", (unsigned int)lpExceptionInfo->ExceptionRecord->ExceptionCode );
		fprintf( fp, "Exception Address: 0x%p\n", lpExceptionInfo->ExceptionRecord->ExceptionAddress );
		fprintf( fp, "Exception Flags: %u\n", lpExceptionInfo->ExceptionRecord->ExceptionFlags );
		fprintf( fp, "Number Parameters: %u\n", lpExceptionInfo->ExceptionRecord->NumberParameters );
		
		// Capture stack
		void *stack[64];
		unsigned short frames = RtlCaptureStackBackTrace( 1, 64, stack, NULL );
		
		fprintf( fp, "\nStack Trace (%u frames):\n", frames );
		for ( unsigned int i = 0; i < frames; i++ )
		{
			fprintf( fp, "  %02u: 0x%p\n", i, stack[i] );
		}
		
		fprintf( fp, "\n=== End Crash ===\n" );
		fclose( fp );
	}

	// A real minidump for SEH-reachable exceptions too.
	WriteMiniDumpUsingExceptionInfo(
		(unsigned int)lpExceptionInfo->ExceptionRecord->ExceptionCode,
		lpExceptionInfo, HL2SB_MINIDUMP_TYPE, "hl2sb" );

	// Also write to console
	Msg( "\n[HL2SB] CRASH DETECTED! Exception 0x%08X at 0x%p\n", 
		(unsigned int)lpExceptionInfo->ExceptionRecord->ExceptionCode,
		lpExceptionInfo->ExceptionRecord->ExceptionAddress );
	Warning( "[HL2SB] Crash written to engine.log (%s) and hl2sb_crash.log\n", HL2SB_ENGINE_LOG_NAME );
	
	// Show message box
#ifdef _WIN32
	MessageBoxA( NULL, 
		"HL2SB crashed!\nCheck engine.log / hl2sb_crash.log for details.", 
		"HL2SB Crash", 
		MB_OK | MB_ICONERROR );
#endif
	
	// Return EXCEPTION_EXECUTE_HANDLER to terminate gracefully
	return EXCEPTION_EXECUTE_HANDLER;
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

static DWORD WINAPI HL2SB_HangWatchdogThread( LPVOID pArg )
{
	( void )pArg;

	for (;;)
	{
		Sleep( 2000 );

		LONG64 llAlive = InterlockedCompareExchange64( &g_llHL2SBAliveTick, 0, 0 );
		if ( llAlive == 0 )
			continue;							// never ticked: still in the first load

		LONG64 llSilent = (LONG64)GetTickCount64() - llAlive;
		if ( llSilent < HL2SB_HANG_TIMEOUT_MS )
			continue;

		if ( InterlockedCompareExchange( &g_bHL2SBHangDumped, 1, 0 ) != 0 )
			continue;							// one report per process

		HL2SB_EngineLogCrashBlock( "hang-watchdog", (unsigned int)llSilent, NULL,
			"no rendered frame for this long", 0 );

		HMODULE hDbgHelp = LoadLibrary( "dbghelp.dll" );
		if ( hDbgHelp )
		{
			typedef int (WINAPI *MiniDumpWriteDumpFn)( void *, unsigned long, void *,
				unsigned long, void *, void *, void * );
			MiniDumpWriteDumpFn pfnWrite = (MiniDumpWriteDumpFn)GetProcAddress( hDbgHelp, "MiniDumpWriteDump" );
			if ( pfnWrite )
			{
				CreateDirectoryA( "dumps", NULL );

				char szPath[ 260 ];
				Q_snprintf( szPath, sizeof( szPath ), "dumps\\hl2sb_hang_%lu.mdmp", GetTickCount() );

				HANDLE hFile = CreateFileA( szPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
					FILE_ATTRIBUTE_NORMAL, NULL );
				if ( hFile != INVALID_HANDLE_VALUE )
				{
					pfnWrite( GetCurrentProcess(), GetCurrentProcessId(), hFile,
						HL2SB_MINIDUMP_TYPE, NULL, NULL, NULL );
					CloseHandle( hFile );
				}
			}
		}

		return 0;
	}
}

void HL2SB_InstallCrashHandler( void )
{
	SetUnhandledExceptionFilter( HL2SB_ExceptionFilter );

	// First-chance net: __fastfail / heap corruption / stack overflow never reach the
	// filter above, and without this the process just vanishes with engine.log
	// stopping mid-sentence and no dump anywhere.
	AddVectoredExceptionHandler( 1, HL2SB_VectoredHandler );

	// Hang net: a deadlock or an infinite loop raises no exception, so nothing above
	// would ever fire - watch the main thread's ticks instead.
	HANDLE hWatchdog = CreateThread( NULL, 0, HL2SB_HangWatchdogThread, NULL, 0, NULL );
	if ( hWatchdog )
		CloseHandle( hWatchdog );

	// The abort paths that never reach the filter above.
	std::set_terminate( HL2SB_TerminateHandler );
	_set_invalid_parameter_handler( HL2SB_InvalidParameterHandler );
	_set_purecall_handler( HL2SB_PurecallHandler );

	// Bare abort(): only SIGABRT fires before the int 29h fast-fail.
	signal( SIGABRT, HL2SB_SigabrtHandler );

	// Crashes the engine catches itself (CatchAndWriteMiniDump) never reach the
	// unhandled-exception filter; chain its dump writer so engine.log gets them.
	g_pHL2SBInnerMiniDumpFunction = SetMiniDumpFunction( HL2SB_MiniDumpChain );

	Msg( "[HL2SB] Crash handler installed (SEH + SIGABRT + terminate/invalid-parameter/purecall + minidump chain; logs: engine.log + hl2sb_crash.log)\n" );
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
	// A stack overflow cannot run a handler on the stack it just exhausted.
	stack_t altStack;
	altStack.ss_sp = g_szHL2SBAltStack;
	altStack.ss_size = sizeof( g_szHL2SBAltStack );
	altStack.ss_flags = 0;
	sigaltstack( &altStack, NULL );

	struct sigaction act;
	memset( &act, 0, sizeof( act ) );
	act.sa_sigaction = HL2SB_PosixSignalHandler;
	act.sa_flags = SA_SIGINFO | SA_ONSTACK;
	sigemptyset( &act.sa_mask );

	sigaction( SIGSEGV, &act, NULL );
	sigaction( SIGBUS,  &act, NULL );
	sigaction( SIGFPE,  &act, NULL );
	sigaction( SIGILL,  &act, NULL );
	sigaction( SIGABRT, &act, NULL );

	std::set_terminate( HL2SB_PosixTerminateHandler );

	Msg( "[HL2SB] Crash handler installed (SIGSEGV/SIGBUS/SIGFPE/SIGILL/SIGABRT -> %s)\n",
		HL2SB_ENGINE_LOG_NAME );
}

#else

void HL2SB_InstallCrashHandler( void )
{
	// Not our platform to change here: Android's launcher already funnels crashes
	// into engine.log (launcher/android/crashhandler.cpp -> DebugLogger()->Write),
	// and the other POSIX targets (macOS/BSD) are out of scope for this pass.
}

#endif

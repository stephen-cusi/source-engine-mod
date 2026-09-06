// hl2sb_crash_handler.cpp
// Crash handler for HL2SB - captures exceptions and writes to log

#include "cbase.h"
#include "hl2sb_crash_handler.h"

#ifdef _WIN32
#include <windows.h>
#endif

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#ifdef _WIN32

static LONG WINAPI HL2SB_ExceptionFilter( LPEXCEPTION_POINTERS lpExceptionInfo )
{
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
	
	// Also write to console
	Msg( "\n[HL2SB] CRASH DETECTED! Exception 0x%08X at 0x%p\n", 
		(unsigned int)lpExceptionInfo->ExceptionRecord->ExceptionCode,
		lpExceptionInfo->ExceptionRecord->ExceptionAddress );
	Warning( "[HL2SB] Crash log written to hl2sb_crash.log\n" );
	
	// Show message box
#ifdef _WIN32
	MessageBoxA( NULL, 
		"HL2SB crashed!\nCheck hl2sb_crash.log for details.", 
		"HL2SB Crash", 
		MB_OK | MB_ICONERROR );
#endif
	
	// Return EXCEPTION_EXECUTE_HANDLER to terminate gracefully
	return EXCEPTION_EXECUTE_HANDLER;
}

void HL2SB_InstallCrashHandler( void )
{
	SetUnhandledExceptionFilter( HL2SB_ExceptionFilter );
	Msg( "[HL2SB] Crash handler installed\n" );
}

#else

void HL2SB_InstallCrashHandler( void )
{
	// Non-Windows: no op
}

#endif

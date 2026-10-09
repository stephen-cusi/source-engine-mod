/*
Copyright (C) 2022 nillerusr

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdint.h>
#include <inttypes.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <ucontext.h>
#include <dlfcn.h>
#include <unwind.h>
#include <android/log.h>
#include "libunwind/libunwind.h"

#define IN_LIBGCC2 1 // means we want to define __cxxabiv1::__cxa_demangle
namespace __cxxabiv1
{
	extern "C"
	{
		#include "demangle/cp-demangle.c"
	}
}

#define MAX_FRAMES 2048
#define CRASH_ALTSTACK_SIZE ( 64 * 1024 )
#define CRASH_STACK_IMAGE_BYTES ( 64 * 1024 )
#define CRASH_NUM_SIGNALS 6
#define CRASH_DUMP_MAGIC 0x504D4453 // 'SDMP'

static const int g_signals[ CRASH_NUM_SIGNALS ] = { SIGSEGV, SIGABRT, SIGBUS, SIGFPE, SIGTRAP, SIGILL };
static struct sigaction g_old_sa[ CRASH_NUM_SIGNALS ];

// The CDbgLogger stdio writer is not async-signal-safe, and its destructor
// fcloses the FILE* while leaving the member pointer set - Write() after
// teardown is a fprintf on a closed file, which used to kill the handler
// itself: crashes during shutdown produced no engine.log entry at all.  The
// handler therefore writes through this raw fd, opened once at startup with
// O_APPEND: lines always land at the end of engine.log, the fd stays valid
// after the logger is long gone, and only write() is ever called on it.
static int g_nCrashFd = -1;
static volatile sig_atomic_t g_nInCrashHandler = 0;

static size_t SafeWrite( const void *p, size_t n )
{
	size_t total = 0;
	if ( g_nCrashFd < 0 )
		return 0;
	const char *s = ( const char * )p;
	while ( n )
	{
		ssize_t w = write( g_nCrashFd, s, n );
		if ( w <= 0 )
		{
			if ( errno == EINTR )
				continue;
			break;
		}
		s += w;
		n -= ( size_t )w;
		total += ( size_t )w;
	}
	return total;
}

static void SafeWriteStr( const char *s )
{
	SafeWrite( s, strlen( s ) );
}

static void SafeLogf( const char *fmt, ... )
{
	char buf[ 512 ];
	va_list ap;
	va_start( ap, fmt );
	int n = vsnprintf( buf, sizeof( buf ), fmt, ap );
	va_end( ap );
	if ( n > 0 )
	{
		size_t len = ( size_t )n > sizeof( buf ) - 1 ? sizeof( buf ) - 1 : ( size_t )n;
		SafeWrite( buf, len );
	}
}

static void EmitLine( const char *msg )
{
	SafeWriteStr( msg );
	__android_log_print( ANDROID_LOG_DEBUG, "SRCENG", "%s", msg );
}

#define Log(msg) EmitLine(msg)

struct backtrace_t
{
	int count;
	uintptr_t frames[ MAX_FRAMES ];
};

struct CrashDumpHeader
{
	uint32_t magic;
	uint32_t version;
	uint32_t pid;
	uint32_t tid;
	uint32_t signal;
	uint32_t sigcode;
	uint32_t ucontext_size;
	uint32_t siginfo_size;
	uint32_t map_bytes;
	uint32_t stack_bytes;
	uint64_t fault_addr;
	uint64_t time_sec;
	uint64_t stack_start;
};

static size_t WriteBuf( int fd, const void *p, size_t n )
{
	size_t total = 0;
	const char *s = ( const char * )p;
	while ( n )
	{
		ssize_t w = write( fd, s, n );
		if ( w <= 0 )
		{
			if ( errno == EINTR )
				continue;
			break;
		}
		s += w;
		n -= ( size_t )w;
		total += ( size_t )w;
	}
	return total;
}

// Best-effort in-process dump, raw syscalls only: signal context, memory map
// and an image of the stack around SP.  Pulled off the device with adb, the
// map lets an offline symbolizer turn frame addresses into module+offset.
static void WriteCrashDump( int sig, siginfo_t *si, ucontext_t *uc )
{
	char path[ 64 ];
	snprintf( path, sizeof( path ), "dumps/crash_%d.dmp", ( int )getpid() );
	int fd = open( path, O_WRONLY | O_CREAT | O_TRUNC, 0644 );
	if ( fd < 0 )
		return;

	CrashDumpHeader hdr;
	memset( &hdr, 0, sizeof( hdr ) );
	hdr.magic = CRASH_DUMP_MAGIC;
	hdr.version = 1;
	hdr.pid = ( uint32_t )getpid();
	hdr.tid = ( uint32_t )syscall( SYS_gettid );
	hdr.signal = ( uint32_t )sig;
	hdr.sigcode = si ? ( uint32_t )si->si_code : 0;
	hdr.fault_addr = si ? ( uint64_t )( uintptr_t )si->si_addr : 0;
	hdr.time_sec = ( uint64_t )time( NULL );
	hdr.ucontext_size = uc ? ( uint32_t )sizeof( *uc ) : 0;
	hdr.siginfo_size = si ? ( uint32_t )sizeof( *si ) : 0;

	WriteBuf( fd, &hdr, sizeof( hdr ) );
	if ( uc )
		WriteBuf( fd, uc, sizeof( *uc ) );
	if ( si )
		WriteBuf( fd, si, sizeof( *si ) );

	uint32_t map_bytes = 0;
	int in = open( "/proc/self/maps", O_RDONLY );
	if ( in >= 0 )
	{
		char buf[ 4096 ];
		for ( ;; )
		{
			ssize_t r = read( in, buf, sizeof( buf ) );
			if ( r <= 0 )
				break;
			WriteBuf( fd, buf, ( size_t )r );
			map_bytes += ( uint32_t )r;
		}
		close( in );
	}

	uint64_t stack_start = 0;
	uint32_t stack_bytes = 0;
	if ( uc )
	{
#ifdef __aarch64__
		stack_start = ( uint64_t )uc->uc_mcontext.sp;
#elif defined( __arm__ )
		stack_start = ( uint64_t )uc->uc_mcontext.arm_sp;
#endif
	}

	// write() straight out of the faulting stack: SA_ONSTACK put this handler
	// on the alternate stack, so the original stack (frames live above SP) is
	// still mapped and readable here.  Short writes on partially unmapped
	// pages are fine, the real byte count goes into the header.
	if ( stack_start )
		stack_bytes = ( uint32_t )WriteBuf( fd, ( const void * )( uintptr_t )stack_start, CRASH_STACK_IMAGE_BYTES );

	// patch the sizes that were only known after streaming
	off_t at = ( off_t )offsetof( CrashDumpHeader, map_bytes );
	lseek( fd, at, SEEK_SET );
	WriteBuf( fd, &map_bytes, sizeof( map_bytes ) );
	lseek( fd, ( off_t )offsetof( CrashDumpHeader, stack_bytes ), SEEK_SET );
	WriteBuf( fd, &stack_bytes, sizeof( stack_bytes ) );
	lseek( fd, ( off_t )offsetof( CrashDumpHeader, stack_start ), SEEK_SET );
	WriteBuf( fd, &stack_start, sizeof( stack_start ) );
	lseek( fd, 0, SEEK_END );

	close( fd );
}

void printPC(void *pc)
{
	char message[4096];

	const char* symbol = "unknown";

	Dl_info info = { 0 };
	const char* fname = "unknown";

	if( dladdr(pc, &info) <= 0 )
	{
		snprintf( message, sizeof(message), "0x%" PRIXPTR "\n", (uintptr_t)pc );
		Log(message);
		return;
	}

	if( info.dli_fname )
		fname = info.dli_fname;

	if( info.dli_sname )
		symbol = info.dli_sname;

	int status = 0;
	char *demangled = __cxxabiv1::__cxa_demangle(symbol, 0, 0, &status);

	if( NULL != demangled && 0 == status )
		symbol = demangled;

	uintptr_t relative_addr = (uintptr_t)pc - (uintptr_t)info.dli_fbase;

	snprintf( message, sizeof(message), "0x%" PRIXPTR ":\t%s (base=0x%" PRIXPTR ", %s)\n", relative_addr, symbol, (uintptr_t)info.dli_fbase, fname );
	Log(message);
}

_Unwind_Reason_Code UnwindBacktraceCallback(struct _Unwind_Context* unwind_context, void* state_voidp)
{
	uintptr_t pc = _Unwind_GetIP(unwind_context);
	backtrace_t *bt = (backtrace_t*)state_voidp;

	if( bt->count < MAX_FRAMES )
		bt->frames[bt->count++] = pc;
	else
		return _URC_END_OF_STACK;

	return _URC_NO_REASON;
}

static void CrashHandler( int sig, siginfo_t *si, void *uc)
{
	// A second fault raised while this handler runs (the unwinder or the dump
	// writer touching something already gone) must not recurse: restore the
	// default action and die.
	if ( g_nInCrashHandler )
	{
		signal( sig, SIG_DFL );
		raise( sig );
		return;
	}
	g_nInCrashHandler = 1;

	const ucontext_t* signal_ucontext = (ucontext_t*)uc;
	const mcontext_t* signal_mcontext = &(signal_ucontext->uc_mcontext);

	// Essentials first, signal-safe only: even if everything below dies, the
	// next engine.log line names the signal and the faulting PC.
	Log(">>> crash report begin\n");
	SafeLogf("Signal=%d, errno=%d, code=%d, addr=0x%" PRIXPTR "\n", sig, si->si_errno, si->si_code, (uintptr_t)si->si_addr);

#ifdef __aarch64__
	SafeLogf("pc=0x%" PRIXPTR " lr=0x%" PRIXPTR " sp=0x%" PRIXPTR "\n",
		(uintptr_t)signal_mcontext->pc, (uintptr_t)signal_mcontext->regs[30], (uintptr_t)signal_mcontext->sp);

	char buf[ 1024 ];
	int n = snprintf( buf, sizeof( buf ), "regs:" );
	for ( int r = 0; r < 31 && n > 0 && n < ( int )sizeof( buf ) - 1; r++ )
		n += snprintf( buf + n, sizeof( buf ) - n, " x%d=0x%" PRIx64, r, ( uint64_t )signal_mcontext->regs[ r ] );
	if ( n > 0 )
	{
		SafeWriteStr( buf );
		SafeWriteStr( "\n" );
	}
#elif defined( __arm__ )
	SafeLogf("pc=0x%" PRIXPTR " lr=0x%" PRIXPTR " sp=0x%" PRIXPTR "\n",
		(uintptr_t)signal_mcontext->arm_pc, (uintptr_t)signal_mcontext->arm_lr, (uintptr_t)signal_mcontext->arm_sp);
#endif

	// Real dump artifact before anything fancy can fail.
	WriteCrashDump( sig, si, (ucontext_t*)uc );

#ifdef __aarch64__
	// Doesn't work good on armv7a
	static backtrace_t bt;
	bt.count = 0;

	// unwinder can get stuck on the signal frame, so always print
	// the real faulting PC/LR/SP from the signal context first
	printPC( (void*)(uintptr_t)signal_mcontext->pc );
	printPC( (void*)(uintptr_t)signal_mcontext->regs[30] );
	snprintf( buf, sizeof( buf ), "sp=0x%" PRIXPTR "\n", (uintptr_t)signal_mcontext->sp );
	Log( buf );

	_Unwind_Backtrace(UnwindBacktraceCallback, &bt);

	for( int i = 0; i < bt.count; i++ )
	{
		printPC( (void*)bt.frames[i] );
	}
#else
	// Initialize unw_context and unw_cursor.
	unw_context_t unw_context = {};
	unw_getcontext(&unw_context);
	unw_cursor_t  unw_cursor = {};
	unw_init_local(&unw_cursor, &unw_context);

	while (unw_step(&unw_cursor) > 0) {
		unw_word_t ip = 0;
		unw_get_reg(&unw_cursor, UNW_REG_IP, &ip);
		printPC( (void*)ip );
	}
#endif

	Log(">>> crash report end\n");

	// Chain into whatever was installed before us.  With no other custom
	// handler that is bionic's own, so the system debuggerd still writes a
	// full tombstone under /data/tombstones - the closest thing to a WER dump
	// on Android.
	const struct sigaction *pOld = NULL;
	for ( int i = 0; i < CRASH_NUM_SIGNALS; i++ )
	{
		if ( g_signals[ i ] == sig )
		{
			pOld = &g_old_sa[ i ];
			break;
		}
	}

	if ( pOld && ( pOld->sa_flags & SA_SIGINFO ) && pOld->sa_sigaction )
		( ( void ( * )( int, siginfo_t *, void * ) )pOld->sa_sigaction )( sig, si, uc );
	else if ( pOld && pOld->sa_handler && pOld->sa_handler != SIG_IGN )
		( ( void ( * )( int ) )pOld->sa_handler )( sig );
	else
	{
		signal( sig, SIG_DFL );
		raise( sig );
	}
}

void InitCrashHandler()
{
	// Same relative path engine.log is opened with; O_APPEND keeps crash lines
	// at the end of the file regardless of the stdio writer's buffer state.
	g_nCrashFd = open( "engine.log", O_WRONLY | O_CREAT | O_APPEND, 0644 );
	mkdir( "dumps", 0777 );

	// SA_ONSTACK below is useless without an alternate stack to run on: a
	// stack-overflow crash leaves no room for the handler, which used to mean
	// stack-overflows never produced any log at all.
	static char szAltStack[ CRASH_ALTSTACK_SIZE ];
	stack_t ss;
	memset( &ss, 0, sizeof( ss ) );
	ss.ss_sp = szAltStack;
	ss.ss_size = sizeof( szAltStack );
	ss.ss_flags = 0;
	sigaltstack( &ss, NULL );

	struct sigaction act;
	memset( &act, 0, sizeof( act ) );
	act.sa_sigaction = CrashHandler;
	act.sa_flags = SA_SIGINFO | SA_ONSTACK;
	sigemptyset( &act.sa_mask );

	// One saved old action PER signal: the previous code reused a single
	// struct for all of them, so every re-raise chained into whichever old
	// handler happened to be saved last.
	for ( int i = 0; i < CRASH_NUM_SIGNALS; i++ )
		sigaction( g_signals[ i ], &act, &g_old_sa[ i ] );
}

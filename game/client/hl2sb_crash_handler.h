// hl2sb_crash_handler.h
// Crash handler for HL2SB - captures exceptions and writes to log

#ifndef HL2SB_CRASH_HANDLER_H
#define HL2SB_CRASH_HANDLER_H

#ifdef _WIN32
#pragma once
#endif

void HL2SB_InstallCrashHandler( void );

// HL2SB: hang watchdog.  Exceptions are covered by the handler above, but a deadlock
// or an infinite loop raises none - the process simply stops, engine.log ends mid
// sentence, and no dump is written anywhere.  The main thread calls this every
// rendered frame; a watcher thread writes a dump when it goes silent.
void HL2SB_NotifyAlive( void );

#endif // HL2SB_CRASH_HANDLER_H

// hl2sb_crash_handler.h
// Crash handler for HL2SB - captures exceptions and writes to log

#ifndef HL2SB_CRASH_HANDLER_H
#define HL2SB_CRASH_HANDLER_H

#ifdef _WIN32
#pragma once
#endif

void HL2SB_InstallCrashHandler( void );

#endif // HL2SB_CRASH_HANDLER_H

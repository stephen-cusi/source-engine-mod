//=============================================================================
//
// HL2SB: GMod-compatible sound.PlayFile / IGModAudioChannel runtime.
//
// Client-only. Decoded PCM is mixed into the engine paint buffer through the
// HL2SBLuaStreamMix001 engine interface (single output device, no secondary
// audio device); decoding runs on our own worker thread and never blocks the
// mix thread. See docs in this file's implementation for the parity notes.
//
// Integration points (all called from the client DLL):
//   CHLClient::Init          -> HL2SB_AudioConnect
//   CHLClient::HudUpdate     -> HL2SB_AudioFramePump
//   CHLClient::LevelShutdown -> HL2SB_AudioLevelShutdown
//   CHLClient::Shutdown      -> HL2SB_AudioShutdown
//   public/lsounds.cpp       -> HL2SB_AudioPlayFile (Sounds library, CLIENT)
//   lsrcinit.cpp client pass -> luaopen_AudioChannel (IGModAudioChannel metatable)
//
//=============================================================================

#ifndef HL2SB_LGMODAUDIO_H
#define HL2SB_LGMODAUDIO_H

#ifdef CLIENT_DLL

#include "interface.h"

// Declared with the elaborated type specifier so this header also parses in
// TUs that have not pulled the Lua headers yet.
struct lua_State;

// Fetches the engine stream-mix interface and starts the decode worker.
void HL2SB_AudioConnect( CreateInterfaceFn engineFactory );

// Per-frame main-thread work: dispatch queued load callbacks, refresh the 3D
// listener. Safe to call when audio is not initialized.
void HL2SB_AudioFramePump( void );

// Map unload: stops every channel, drops pending load callbacks. The mix
// callback stays registered.
void HL2SB_AudioLevelShutdown( void );

// Client shutdown: kills the worker, unregisters the engine mix callback,
// releases every channel. No channel lock may be held when this runs.
void HL2SB_AudioShutdown( void );

// sound.PlayFile binding body, called from the Sounds library registration
// in public/lsounds.cpp (CLIENT only).
int HL2SB_AudioPlayFile( struct lua_State *L );

// Registers the IGModAudioChannel metatable (GMod's 41 channel methods).
// Export linkage comes from the LUALIB_API definition in lgmodaudio.cpp.
int luaopen_AudioChannel( struct lua_State *L );

#endif // CLIENT_DLL

#endif // HL2SB_LGMODAUDIO_H

//========= Copyright Valve Corporation, All rights reserved. ============//
//
// HL2SB frame profiler: cheap per-frame statistics behind the
// hl2sb_framestats convar, for locating frame-time sinks on the
// Windows-ARM64-emulated test machines (where every full-screen pass and
// every extra Lua dispatch is expensive and the engine log goes silent for
// tens of seconds while the user reports "still laggy").
//
// With the convar at 0 the only per-frame work is a boolean check; every
// accumulator stays cold.  With the convar at 1 a one-line summary is printed
// to the console once per second:
//
//   [HL2SB framestats] fps=.. avg=..ms max=..ms | Lua HUDPaint=..ms RSE=..ms
//                      PostDrawEffects=..ms | surfaceDraws=..
//
//   fps/avg/max   - interval between consecutive main-view frames, measured
//                   where GM:HUDPaint is pumped (CViewRender's 2D HUD pass).
//                   Monitor/overlay views that re-run that pass show up as
//                   extra, shorter intervals in the average.
//   Lua <hook>    - wall time spent INSIDE the hook.call dispatch for the
//                   named per-frame hook (registered hooks + gamemode
//                   fallback + our Lua-side call overhead all included).
//   surfaceDraws  - Lua-issued vgui surface draw primitives during the
//                   window (rects, textured rects, rotated quads, text).
//
// Do not add work to the convar-off path.
//
//===========================================================================//

#ifndef HL2SB_FRAMESTATS_H
#define HL2SB_FRAMESTATS_H
#ifdef _WIN32
#pragma once
#endif

// True when hl2sb_framestats is on.  Callers gate their measurement overhead
// on this so the off path stays a single predictable branch.
bool HL2SB_FrameStats_Enabled();

// One Lua-issued vgui surface draw primitive was issued.
void HL2SB_FrameStats_AddSurfaceDraw();

// Wall seconds spent inside one dispatch of the named per-frame hook.
void HL2SB_FrameStats_NoteLuaHook( const char *pHookName, double flSeconds );

// Call once per main-view frame (at the HUDPaint pump in CViewRender).  Owns
// the frame-interval measurement and the one-second console report.
void HL2SB_FrameStats_FramePulse();

#endif // HL2SB_FRAMESTATS_H
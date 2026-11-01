//=============================================================================//
//
// HL2SB: GMod's lua/effects/*.lua runtime.
//
// Scripted effects are client entities populated by the effects module.
//
//=============================================================================//

#ifndef LUA_EFFECTS_H
#define LUA_EFFECTS_H

#ifdef _WIN32
#pragma once
#endif

class CEffectData;

// Spawns the Lua effect registered under pszName, handing it `data` through
// EFFECT:Init.  Returns false when no such effect is registered, in which case
// the caller must fall through to the engine's own effect callbacks.
bool HL2SB_CreateLuaEffect( const char *pszName, const CEffectData &data );

// True when at least one effect is registered under pszName (falls back to the
// engine's own dispatch when false).
bool HL2SB_HasLuaEffect( const char *pszName );

#endif // LUA_EFFECTS_H

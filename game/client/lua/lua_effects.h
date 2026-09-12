//=============================================================================//
//
// HL2SB: GMod's lua/effects/*.lua runtime.
//
// GMod effect scripts define a global EFFECT table with Init/Think/Render
// methods; the engine registers it under the file's basename (see
// luasrc_LoadEffects() in game/shared/lua/luamanager.cpp) and then spawns one
// copy of it per util.Effect() / DispatchEffect() call.
//
// This is the client half: it owns the live effects and drives them from the
// engine's client-side effect list (CEffectsList::DrawEffects, called from
// CViewRender::ViewDrawScene right after DrawWorldAndEntities and before the
// viewmodel), which is the same place Source draws its own client FX.
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

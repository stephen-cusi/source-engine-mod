//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: HL2SB (2026-10-07): client side of GMod's env_skypaint (see
//          c_env_skypaint.cpp).  Exposes just enough for the SkyPaint
//          material proxy registration in the proxy factory chain.
//
// $NoKeywords: $
//===========================================================================//

#ifndef C_ENV_SKYPAINT_H
#define C_ENV_SKYPAINT_H
#ifdef _WIN32
#pragma once
#endif

class C_EnvSkyPaint;
class IMaterialProxy;

// The active client sky-paint entity (GMod's g_SkyPaint global), NULL while
// the map has none or during shutdown.
C_EnvSkyPaint *HL2SB_GetEnvSkyPaint( void );

// Factory hook: returns a new "SkyPaint" material proxy for
// CPlayerColorProxyFactory::CreateProxy.
IMaterialProxy *HL2SB_CreateSkyPaintProxy( void );

#endif // C_ENV_SKYPAINT_H

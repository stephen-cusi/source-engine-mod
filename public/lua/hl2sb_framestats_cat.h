//========= Copyright Valve Corporation, All rights reserved. ============//
//
// HL2SB frame profiler - per-category micro-accounting, the second report
// line of hl2sb_framestats.
//
// The GM:HUDPaint hook dispatch is measured as one wall-time block in
// CViewRender; when that block is slow the number alone cannot say WHICH
// binding call inside it ate the time.  The categories below are accumulated
// with a scoped timer around the hot Lua binding bodies the HUD dispatch
// drives (the Material() global, the surface.* draw family, convar reads and
// lookups, gpGlobals.curtime, util.TraceLine).  hl2sb_framestats.cpp drains
// and prints them once per second, right after the main line, together with
// a residual bucket (HUDPaint total minus the accounted categories).
//
// The accumulators live in function-local statics of inline functions, so
// every translation unit including this header shares one instance per DLL.
// Only client.dll's instance is drained; server/GameUI link their own copies
// that nothing ever enables or reads - a few unused bytes, no cross-DLL
// plumbing.  Because the instrumentation only touches header-inline state,
// TUs compiled into server.dll keep linking clean.
//
// With hl2sb_framestats at 0 the per-call cost is one predictable branch on
// a bool; no timer is read and nothing is written.  With it at 1 each
// instrumented call adds two Plat_FloatTime() reads.
//
// NOTE: the accumulators cover the whole reporting second, not only the
// HUDPaint window - Lua-driven panel paints outside GM:HUDPaint (derma
// skins, spawnmenu) land in the same buckets.  That is deliberate: a hot
// category names the expensive binding no matter which dispatch drove it,
// and the residual bucket is printed as (HUDPaint - accounted) so outside
// contributions can make it dip negative, which is itself informative.
//
//===========================================================================//

#ifndef HL2SB_FRAMESTATS_CAT_H
#define HL2SB_FRAMESTATS_CAT_H
#ifdef _WIN32
#pragma once
#endif

#include "tier0/platform.h"

// One bucket per measured binding family.  The order here is the report
// order; the name table in hl2sb_framestats.cpp must be kept in sync.
enum HL2SB_FrameStatsCat_t
{
	HL2SB_FCAT_MATERIAL_IMG = 0,	// global Material(), image-extension path
	HL2SB_FCAT_MATERIAL_VMT,		// global Material(), VMT/dictionary path
	HL2SB_FCAT_SURF_SETMATERIAL,	// surface.SetMaterial
	HL2SB_FCAT_SURF_TEXRECT,		// surface.DrawTexturedRect / UV / SubRect / Rotated
	HL2SB_FCAT_SURF_RECT,			// surface.DrawRect / FilledRect / OutlinedRect / FilledRectFade
	HL2SB_FCAT_SURF_TEXTPRINT,		// surface.DrawPrintText
	HL2SB_FCAT_SURF_TEXTMEASURE,	// surface.GetTextSize (both spellings)
	HL2SB_FCAT_SURF_SETFONT,		// surface.SetFont / surface.DrawSetTextFont
	HL2SB_FCAT_CONVAR_READ,			// ConVar object GetBool/GetFloat/GetInt/GetString
	HL2SB_FCAT_CONVAR_LOOKUP,		// by-name lookup: ConVar(name), GetConVar_Internal, cvar.FindVar
	HL2SB_FCAT_CURTIME,				// gpGlobals.curtime (behind the Lua CurTime())
	HL2SB_FCAT_UTIL_TRACE,			// util.TraceLine
	HL2SB_FCAT_COUNT
};

struct HL2SB_FrameCatAcc_t
{
	double flSeconds;
	int    nCalls;
};

// The enable flag is flipped by hl2sb_framestats.cpp once per frame pulse.
inline bool &HL2SB_FrameStats_CatsEnabled( void )
{
	static bool s_bEnabled = false;
	return s_bEnabled;
}

inline HL2SB_FrameCatAcc_t *HL2SB_FrameStats_Cats( void )
{
	static HL2SB_FrameCatAcc_t s_rgCats[HL2SB_FCAT_COUNT] = {};
	return s_rgCats;
}

inline void HL2SB_FrameStats_NoteCategory( int iCategory, double flSeconds )
{
	HL2SB_FrameCatAcc_t *pAcc = HL2SB_FrameStats_Cats();
	pAcc[iCategory].flSeconds += flSeconds;
	++pAcc[iCategory].nCalls;
}

// Scoped wall-time timer for a binding body.  Inactive state is a flag load
// plus a predictable branch in ctor and dtor.
class HL2SB_FrameStatsCatScope
{
public:
	explicit HL2SB_FrameStatsCatScope( int iCategory )
	  : m_iCategory( iCategory ),
		m_bActive( HL2SB_FrameStats_CatsEnabled() ),
		m_flStart( m_bActive ? Plat_FloatTime() : 0.0 ) {}

	~HL2SB_FrameStatsCatScope()
	{
		if ( m_bActive )
			HL2SB_FrameStats_NoteCategory( m_iCategory, Plat_FloatTime() - m_flStart );
	}

private:
	HL2SB_FrameStatsCatScope( const HL2SB_FrameStatsCatScope & );
	HL2SB_FrameStatsCatScope &operator=( const HL2SB_FrameStatsCatScope & );

	int    m_iCategory;
	bool   m_bActive;
	double m_flStart;
};

#endif // HL2SB_FRAMESTATS_CAT_H

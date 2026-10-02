//========= Copyright Valve Corporation, All rights reserved. ============//
//
// HL2SB frame profiler - see hl2sb_framestats.h.
//
//===========================================================================//

#include "cbase.h"
#include "hl2sb_framestats.h"
#include "convar.h"
#include "tier0/dbg.h"
#include "tier0/platform.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

ConVar hl2sb_framestats(
	"hl2sb_framestats", "0", 0,
	"Print a one-line frame statistics summary every second (frame ms, "
	"GM:HUDPaint / RenderScreenspaceEffects / PostDrawEffects Lua time, "
	"Lua surface draw count). Zero overhead when 0." );

bool HL2SB_FrameStats_Enabled()
{
	return hl2sb_framestats.GetBool() != 0;
}

// --- accumulators ----------------------------------------------------------

static int  s_nSurfaceDraws = 0;
static double s_flLuaHudPaint = 0.0;
static double s_flLuaRse = 0.0;
static double s_flLuaPostDrawEffects = 0.0;

static double s_flLastFrameTime = 0.0;
static double s_flIntervalSum = 0.0;
static double s_flIntervalMax = 0.0;
static int    s_nIntervals = 0;
static double s_flNextReport = 0.0;

void HL2SB_FrameStats_AddSurfaceDraw()
{
	if ( hl2sb_framestats.GetBool() )
		++s_nSurfaceDraws;
}

void HL2SB_FrameStats_NoteLuaHook( const char *pHookName, double flSeconds )
{
	if ( !hl2sb_framestats.GetBool() )
		return;

	if ( Q_stricmp( pHookName, "HUDPaint" ) == 0 )
		s_flLuaHudPaint += flSeconds;
	else if ( Q_stricmp( pHookName, "RenderScreenspaceEffects" ) == 0 )
		s_flLuaRse += flSeconds;
	else if ( Q_stricmp( pHookName, "PostDrawEffects" ) == 0 )
		s_flLuaPostDrawEffects += flSeconds;
}

void HL2SB_FrameStats_FramePulse()
{
	if ( !hl2sb_framestats.GetBool() )
	{
		// Keep the window anchored at "next frame" so a freshly enabled convar
		// does not report an interval spanning the disabled gap.
		s_flLastFrameTime = 0.0;
		return;
	}

	double flNow = Plat_FloatTime();

	if ( s_flLastFrameTime > 0.0 )
	{
		double flDt = flNow - s_flLastFrameTime;
		if ( flDt > 0.0 && flDt < 1.0 )	// ignore multi-second pauses (menu etc.)
		{
			s_flIntervalSum += flDt;
			if ( flDt > s_flIntervalMax )
				s_flIntervalMax = flDt;
			++s_nIntervals;
		}
	}
	s_flLastFrameTime = flNow;

	if ( s_flNextReport == 0.0 )
	{
		s_flNextReport = flNow + 1.0;
		return;
	}

	if ( flNow < s_flNextReport )
		return;

	if ( s_nIntervals > 0 )
	{
		Msg( "[HL2SB framestats] fps=%d avg=%.2fms max=%.2fms | Lua HUDPaint=%.2fms RSE=%.2fms PostDrawEffects=%.2fms | surfaceDraws=%d\n",
			s_nIntervals,
			( s_flIntervalSum / s_nIntervals ) * 1000.0,
			s_flIntervalMax * 1000.0,
			s_flLuaHudPaint * 1000.0,
			s_flLuaRse * 1000.0,
			s_flLuaPostDrawEffects * 1000.0,
			s_nSurfaceDraws );
	}

	s_flIntervalSum = 0.0;
	s_flIntervalMax = 0.0;
	s_nIntervals = 0;
	s_flLuaHudPaint = 0.0;
	s_flLuaRse = 0.0;
	s_flLuaPostDrawEffects = 0.0;
	s_nSurfaceDraws = 0;
	s_flNextReport = flNow + 1.0;
}

//=============================================================================//
//
// HL2SB: the C half of GMod's `effects` library (client).
//
// GMod's lua/includes/modules/effects.lua only defines Register/Create/GetList
// in Lua.  effects.Bubbles / BubbleTrail / BeamRingPoint are C functions that
// GMod registers into the same global table BEFORE that module file runs; the
// module's module("effects") call then finds the table in package.loaded and
// merges its own Lua functions onto it.  The "effects" entry in lsrcinit.cpp's
// luasrclibs (which runs before the lua/includes dofile pass) wires this in
// exactly that way, so lua/effects/*.lua see the full GMod surface.
//
// Signatures audited against the wiki pages scraped to D:\project\wiki
// (effects_Bubbles/BubbleTrail/BeamRingPoint.txt, 2026-09):
//
//   effects.Bubbles( mins, maxs, count, height, speed = 0, delay = 0 )
//   effects.BubbleTrail( startPos, endPos, count, height, speed = 0, delay = 0 )
//   effects.BeamRingPoint( pos, lifetime, startRad, endRad, width, amplitude,
//                          color, extra )
//       extra = { speed, spread, delay, flags, framerate, material } -- all
//       optional; material defaults to "sprites/lgtning.vmt", like GMod.
//
// effects.TracerSound stays a Lua shim in the game repo's effects.lua: the
// engine already registers a "TracerSound" client effect callback
// (fx_tracer.cpp) and the shim only re-frames it through util.Effect.
//
// Everything plays through the client's own local effect systems -- the
// tempents manager (c_te_bubbles.cpp / c_te_bubbletrail.cpp) and the view
// beams (the exact call the networked TE_BeamRingPoint makes).  GMod's
// `delay` argument is a networked-TE scheduling value with no local
// equivalent; it is accepted everywhere and ignored.
//=============================================================================//

#include "cbase.h"

#include "itempents.h"
#include "c_te_legacytempents.h"	// extern ITempEnts *tempents (local playback)
#include "iviewrender_beams.h"		// extern IViewRenderBeams *beams
#include "c_baseentity.h"			// C_BaseEntity::PrecacheModel
#include "luamanager.h"
#include "luasrclib.h"
#include "mathlib/lvector.h"
#include "lColor.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#define HL2SB_EFFECTS_BUBBLE_MODEL "sprites/bubble.vmt"
#define HL2SB_EFFECTS_BEAM_MODEL "sprites/lgtning.vmt"

// effects.Bubbles( mins, maxs, count, height, speed = 0, delay = 0 )
//
// Local playback through the client's own tempents (the same path the
// networked TE_Bubbles callback takes).  GMod's `delay` is a networked-TE
// scheduling value with no local equivalent here, so it is accepted and
// ignored -- the effect plays immediately.
static int leffects_Bubbles( lua_State *L )
{
	Vector mins = luaL_checkvector( L, 1 );
	Vector maxs = luaL_checkvector( L, 2 );
	int count = luaL_checkint( L, 3 );
	float height = (float)luaL_optnumber( L, 4, 0 );
	float speed = (float)luaL_optnumber( L, 5, 0 );
	// arg 6 = delay: accepted, ignored (see comment above)

	int iModel = C_BaseEntity::PrecacheModel( HL2SB_EFFECTS_BUBBLE_MODEL );
	if ( iModel < 0 )
	{
		luasrc_LuaInfoMsgF( "[HL2SB] effects.Bubbles: model '%s' has no client model index\n",
			HL2SB_EFFECTS_BUBBLE_MODEL );
		return 0;
	}

	tempents->Bubbles( mins, maxs, height, iModel, count, speed );
	return 0;
}

// effects.BubbleTrail( startPos, endPos, count, height, speed = 0, delay = 0 )
static int leffects_BubbleTrail( lua_State *L )
{
	Vector start = luaL_checkvector( L, 1 );
	Vector end = luaL_checkvector( L, 2 );
	int count = luaL_checkint( L, 3 );
	float height = (float)luaL_optnumber( L, 4, 0 );
	float speed = (float)luaL_optnumber( L, 5, 0 );
	// arg 6 = delay: accepted, ignored (see comment above)

	int iModel = C_BaseEntity::PrecacheModel( HL2SB_EFFECTS_BUBBLE_MODEL );
	if ( iModel < 0 )
	{
		luasrc_LuaInfoMsgF( "[HL2SB] effects.BubbleTrail: model '%s' has no client model index\n",
			HL2SB_EFFECTS_BUBBLE_MODEL );
		return 0;
	}

	tempents->BubbleTrail( start, end, height, iModel, count, speed );
	return 0;
}

// effects.BeamRingPoint( pos, lifetime, startRad, endRad, width, amplitude,
//                        color, extra )
static int leffects_BeamRingPoint( lua_State *L )
{
	Vector pos = luaL_checkvector( L, 1 );
	float life = (float)luaL_checknumber( L, 2 );
	float startRadius = (float)luaL_checknumber( L, 3 );
	float endRadius = (float)luaL_checknumber( L, 4 );
	float width = (float)luaL_checknumber( L, 5 );
	float amplitude = (float)luaL_optnumber( L, 6, 0 );
	lua_Color color = luaL_checkcolor( L, 7 );

	// wiki: extra table, every key optional.
	float speed = 0;
	float framerate = 0;
	int spread = 0;
	int flags = 0;
	const char *material = HL2SB_EFFECTS_BEAM_MODEL;

	if ( lua_istable( L, 8 ) )
	{
		lua_getfield( L, 8, "speed" );
		if ( lua_isnumber( L, -1 ) ) speed = (float)lua_tonumber( L, -1 );
		lua_pop( L, 1 );

		lua_getfield( L, 8, "spread" );
		if ( lua_isnumber( L, -1 ) ) spread = lua_tointeger( L, -1 );
		lua_pop( L, 1 );

		// arg "delay": accepted for the wiki signature, ignored (see Bubbles)
		lua_getfield( L, 8, "delay" );
		lua_pop( L, 1 );

		lua_getfield( L, 8, "flags" );
		if ( lua_isnumber( L, -1 ) ) flags = lua_tointeger( L, -1 );
		lua_pop( L, 1 );

		lua_getfield( L, 8, "framerate" );
		if ( lua_isnumber( L, -1 ) ) framerate = (float)lua_tonumber( L, -1 );
		lua_pop( L, 1 );

		lua_getfield( L, 8, "material" );
		if ( lua_isstring( L, -1 ) ) material = lua_tostring( L, -1 );
		lua_pop( L, 1 );
	}

	int iModel = C_BaseEntity::PrecacheModel( material );
	if ( iModel < 0 )
	{
		luasrc_LuaInfoMsgF( "[HL2SB] effects.BeamRingPoint: material '%s' has no client model index\n",
			material );
		return 0;
	}

	// Same field mapping as the networked TE_BeamRingPoint free function
	// (c_te_beamringpoint.cpp): spread feeds the end-width slot, speed and
	// framerate are 0.1-scaled by the TE layer's convention.
	beams->CreateBeamRingPoint( pos, startRadius, endRadius, iModel, 0 /* halo */, 0.0f /* haloScale */,
		life, width, 0.1f * spread, 0.0f /* fadeLength */, amplitude,
		(float)color.a(), 0.1f * speed, 0 /* startFrame */, 0.1f * framerate,
		(float)color.r(), (float)color.g(), (float)color.b(), flags );
	return 0;
}

static const luaL_Reg leffects_funcs[] = {
	{ "Bubbles", leffects_Bubbles },
	{ "BubbleTrail", leffects_BubbleTrail },
	{ "BeamRingPoint", leffects_BeamRingPoint },
	{ NULL, NULL }
};

LUALIB_API int luaopen_HL2SBClientEffects (lua_State *L )
{
	// Lowercase "effects" -- the global table GMod's
	// lua/includes/modules/effects.lua merges onto via package.loaded.
	luaL_register( L, "effects", leffects_funcs );
	return 1;
}

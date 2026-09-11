//========== Copyleft (c) 2026, HL2SB, Some rights reserved. ===========//
//
// Purpose: Garry's Mod SWEP.HoldType -> Activity translation.
//
//   This is the engine-side, self-contained transcription of Garry's Mod's
//   weapon_base/sh_anim.lua.  It answers one question:
//
//       "the player is playing ACT_MP_RUN and is holding a shotgun - which
//        ACT_HL2MP_* activity does Garry's Mod want?"
//
//   The answer is derived from the Lua, not invented:  sh_anim.lua keeps a
//   table of hold type -> *base* activity, then adds the same small offsets to
//   that base for every player activity it knows about:
//
//       ActivityTranslate[ ACT_MP_STAND_IDLE ]               = index + 0
//       ActivityTranslate[ ACT_MP_WALK ]                     = index + 1
//       ActivityTranslate[ ACT_MP_RUN ]                      = index + 2
//       ActivityTranslate[ ACT_MP_CROUCH_IDLE ]              = index + 3
//       ActivityTranslate[ ACT_MP_CROUCHWALK ]               = index + 4
//       ActivityTranslate[ ACT_MP_ATTACK_STAND_PRIMARYFIRE ] = index + 5
//       ActivityTranslate[ ACT_MP_ATTACK_CROUCH_PRIMARYFIRE ]= index + 5
//       ActivityTranslate[ ACT_MP_RELOAD_STAND ]             = index + 6
//       ActivityTranslate[ ACT_MP_RELOAD_CROUCH ]            = index + 6
//       ActivityTranslate[ ACT_MP_JUMP ]                     = index + 7
//       ActivityTranslate[ ACT_RANGE_ATTACK1 ]               = index + 8
//       ActivityTranslate[ ACT_MP_SWIM_IDLE ]                = index + 8
//       ActivityTranslate[ ACT_MP_SWIM ]                     = index + 9
//
//   and one special case ("normal" has no jump animation, so it gets
//   ACT_HL2MP_JUMP_SLAM).
//
//   WHY SOME ENTRIES ARE ABSENT IN THIS FORK
//   ----------------------------------------
//   GMod's own ACT_HL2MP_* block gives every hold type ten consecutive
//   activities, which is what makes "index + N" reach a meaningful activity for
//   N = 0..9.  This fork's ai_activity.h declares only seven per hold type, and
//   it spells them per hold type (ACT_HL2MP_IDLE_PISTOL, ACT_HL2MP_IDLE_SMG1,
//   ...), so the eight entries here are resolved by *name*, one member at a
//   time, instead of adding GMod's offsets and hoping the two block layouts
//   agree.  The fork's seven members line up with sh_anim.lua's first seven
//   keys, in order:
//
//       ACT_HL2MP_IDLE_<HT>                    ACT_MP_STAND_IDLE
//       ACT_HL2MP_RUN_<HT>                     ACT_MP_WALK
//       ACT_HL2MP_IDLE_CROUCH_<HT>             ACT_MP_RUN
//       ACT_HL2MP_WALK_CROUCH_<HT>             ACT_MP_CROUCH_IDLE
//       ACT_HL2MP_GESTURE_RANGE_ATTACK_<HT>    ACT_MP_CROUCHWALK
//       ACT_HL2MP_GESTURE_RELOAD_<HT>          ACT_MP_ATTACK_*_PRIMARYFIRE
//       ACT_HL2MP_JUMP_<HT>                    ACT_MP_RELOAD_*
//
//   Four Lua entries - ACT_MP_JUMP, ACT_RANGE_ATTACK1, ACT_MP_SWIM_IDLE and
//   ACT_MP_SWIM - address members this fork does not declare at all, so they
//   have no translation: GMod_TranslateActivity() returns the input activity
//   unchanged for them (never -1, never 0), exactly as GMod's own
//   SWEP:TranslateActivity() returning -1 leaves the activity alone, and the
//   fallback chain then hands back the untranslated activity.  Only eleven of
//   GMod's hold types have a block here at all; the other eight have no
//   translations.
//
//   Every hold type name still resolves, including the ones this fork has no
//   block for (fist, melee2, passive, knife, duel, camera, magic, revolver):
//   they are known names with no mapping, which is a different thing from an
//   unknown name (HL2SB_HOLDTYPE_INVALID), and the two must not be conflated.
//
//===========================================================================//

#ifndef HL2SB_GMOD_ACTIVITY_TRANSLATE_H
#define HL2SB_GMOD_ACTIVITY_TRANSLATE_H
#ifdef _WIN32
#pragma once
#endif

#include "ai_activity.h"

// This header only ever names CBaseAnimating as a pointer, so it does not need
// the class definition - and it must not pull in a realm-specific header, since
// it is shared between the client and the server DLL.  Callers in either realm
// include their own animating header first (cbase.h already does).
class CBaseAnimating;

//-----------------------------------------------------------------------------
// Hold types - exactly the strings Garry's Mod's sh_anim.lua ActIndex table
// uses, plus the sentinel for "not a hold type at all".
//-----------------------------------------------------------------------------
enum GModHoldType_t
{
	HL2SB_HOLDTYPE_INVALID = -1,

	HL2SB_HOLDTYPE_PISTOL = 0,	// "pistol"		ACT_HL2MP_IDLE_PISTOL
	HL2SB_HOLDTYPE_SMG,			// "smg"		ACT_HL2MP_IDLE_SMG1
	HL2SB_HOLDTYPE_GRENADE,		// "grenade"	ACT_HL2MP_IDLE_GRENADE
	HL2SB_HOLDTYPE_AR2,			// "ar2"		ACT_HL2MP_IDLE_AR2
	HL2SB_HOLDTYPE_SHOTGUN,		// "shotgun"	ACT_HL2MP_IDLE_SHOTGUN
	HL2SB_HOLDTYPE_RPG,			// "rpg"		ACT_HL2MP_IDLE_RPG
	HL2SB_HOLDTYPE_PHYSGUN,		// "physgun"	ACT_HL2MP_IDLE_PHYSGUN
	HL2SB_HOLDTYPE_CROSSBOW,	// "crossbow"	ACT_HL2MP_IDLE_CROSSBOW
	HL2SB_HOLDTYPE_MELEE,		// "melee"		ACT_HL2MP_IDLE_MELEE
	HL2SB_HOLDTYPE_SLAM,		// "slam"		ACT_HL2MP_IDLE_SLAM
	HL2SB_HOLDTYPE_NORMAL,		// "normal"		ACT_HL2MP_IDLE
	HL2SB_HOLDTYPE_FIST,		// "fist"		(absent from this fork)
	HL2SB_HOLDTYPE_MELEE2,		// "melee2"		(absent from this fork)
	HL2SB_HOLDTYPE_PASSIVE,		// "passive"	(absent from this fork)
	HL2SB_HOLDTYPE_KNIFE,		// "knife"		(absent from this fork)
	HL2SB_HOLDTYPE_DUEL,		// "duel"		(absent from this fork)
	HL2SB_HOLDTYPE_CAMERA,		// "camera"		(absent from this fork)
	HL2SB_HOLDTYPE_MAGIC,		// "magic"		(absent from this fork)
	HL2SB_HOLDTYPE_REVOLVER,	// "revolver"	(absent from this fork)

	HL2SB_HOLDTYPE_COUNT
};

// "index" in the Lua, i.e. the ActIndex[] entry, chosen so that
// index + GMod's offsets lands on the right ACT_HL2MP_* member.  0 == invalid.
int GMod_HoldTypeBaseActivity( int iHoldType );

// Unknown (including NULL and "") -> HL2SB_HOLDTYPE_INVALID.  Garry's Mod's
// SetWeaponHoldType falls back to "normal" on an unknown name; callers that
// want that behaviour ask for it explicitly rather than getting it by accident.
int GMod_HoldTypeFromString( const char *pszHoldType );

// Garry's Mod's own spelling of the hold type ("smg", "melee2", ...);
// "invalid" for anything outside the enum.  Never returns NULL.
const char *GMod_HoldTypeName( int iHoldType );

// Garry's Mod's ActivityTranslate lookup: the translated activity, or `act`
// unchanged when this hold type has no entry for it (GMod's own fallback -
// its SWEP:TranslateActivity returns -1 and the caller keeps the activity).
// `act` is also returned unchanged when iHoldType is unknown.
//
// The entries that have no translation in this fork (ACT_MP_JUMP,
// ACT_RANGE_ATTACK1, ACT_MP_SWIM_IDLE, ACT_MP_SWIM) go down that same path, so
// the caller gets its own activity back and never ACT_INVALID, -1 or 0.
Activity GMod_TranslateActivity( int iHoldType, Activity act );

//-----------------------------------------------------------------------------
// Fallback chain, for callers that have a model in hand.
//
//   1. the translated activity, if the model can play it
//   2. the untranslated `act`, if the model can play that
//   3. ACT_MP_STAND_IDLE  (Garry's Mod's CalcMainActivity default)
//   4. ACT_HL2MP_IDLE     (this fork's generic player idle)
//   5. ACT_IDLE           (generic Source idle)
//   -> ACT_INVALID when the model can answer none of them
//
// "the model can play it" means: the activity has a name, the model reports it
// through LookupActivity(), and CBaseAnimating::SelectWeightedSequence() finds
// a sequence for it.  ACT_INVALID/ACT_RESET are never handed to
// SelectWeightedSequence (it asserts on ACT_INVALID).
//
// The template is realm-agnostic on purpose: pass a CBaseAnimating* (server),
// a C_BaseAnimating* (client) or anything derived (CBasePlayer, ...).  The
// method set used is identical in both realms.
//-----------------------------------------------------------------------------
enum GModActivityFallback_t
{
	HL2SB_ACTFALLBACK_TRANSLATED = 0,	// the hold type had an entry and the model has it
	HL2SB_ACTFALLBACK_UNTRANSLATED,		// hold type had no entry (or untranslated differs) - GMod keeps `act`
	HL2SB_ACTFALLBACK_STAND_IDLE,		// ACT_MP_STAND_IDLE
	HL2SB_ACTFALLBACK_HL2MP_IDLE,		// ACT_HL2MP_IDLE
	HL2SB_ACTFALLBACK_IDLE,				// ACT_IDLE
	HL2SB_ACTFALLBACK_NONE,				// ACT_INVALID
};

Activity GMod_TranslateActivityAndFallback( int iHoldType, Activity act, CBaseAnimating *pModel,
											GModActivityFallback_t *pUsedStep = NULL );

// Debug helper: the ACT_* spelling of an activity ("ACT_MP_STAND_IDLE"),
// "ACT_UNKNOWN" when this fork's ai_activity.h has no such name, and
// "ACT_INVALID" for -1.  Phase 8's debug commands use this.
const char *GMod_ActivityName( Activity act );

//-----------------------------------------------------------------------------
// Template wrapper: forwards to the CBaseAnimating* implementation.  Its only
// job is to accept C_BaseAnimating*/CBasePlayer* without this header having to
// know which realm's animating class is in scope.
//-----------------------------------------------------------------------------
template < typename T >
inline Activity GMod_TranslateActivityAndFallback( int iHoldType, Activity act, T *pModel,
												   GModActivityFallback_t *pUsedStep = NULL )
{
	return GMod_TranslateActivityAndFallback( iHoldType, act, (CBaseAnimating *)pModel, pUsedStep );
}

#endif // HL2SB_GMOD_ACTIVITY_TRANSLATE_H

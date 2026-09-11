//========== Copyleft (c) 2026, HL2SB, Some rights reserved. ===========//
//
// Purpose: Garry's Mod SWEP.HoldType -> Activity translation.
//          See gmod_activity_translate.h for the contract and for where the
//          numbers come from.  This file is the transcription itself.
//
//===========================================================================//

#include "cbase.h"
#include "activitylist.h"

#ifdef CLIENT_DLL
	// This .cpp names CBaseAnimating because the shared methods it calls
	// (SelectWeightedSequence / LookupActivity / GetSequenceActivity) live on
	// the client's C_BaseAnimating.  Same trick as game/shared/base_playeranimstate.h
	// / multiplayer_animstate.h: in the client realm they are the same thing.
	#define CBaseAnimating C_BaseAnimating
	#include "c_baseanimating.h"
#else
	#include "baseanimating.h"
#endif

#include "gmod_activity_translate.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

//-----------------------------------------------------------------------------
// Debug helper name.
//
// NOTE: this used to be a 1750-row literal transcription of ai_activity.h,
// because ActivityList_NameForIndex() only knows REGISTERED activities and the
// ACT_HL2MP_* family was not registered.  It is now registered (in full, on both
// realms - see activitylist.cpp), so the engine's own registry is authoritative
// and the transcription is gone: it could only ever drift out of sync with the
// enum, which would silently turn every hold-type slot into ACT_INVALID.
//-----------------------------------------------------------------------------

const char *GMod_ActivityName( Activity act )
{
	if ( act == ACT_INVALID )
		return "ACT_INVALID";

	// The fork registers the whole GMod activity vocabulary as shared activities,
	// so the engine's own registry is the authority here.  NULL means the value is
	// not a registered activity at all (which is itself a useful answer: it is what
	// the ACT_HL2MP_* name check in GMod_MemberActivity() wants to reject).
	const char *pszName = ActivityList_NameForIndex( act );
	return pszName ? pszName : "ACT_UNKNOWN";
}

//-----------------------------------------------------------------------------
// Hold type table.
//
// Both columns are copied from Garry's Mod's weapon_base/sh_anim.lua:
//
//   s_GMod_HoldTypes[].pszName          - the ActIndex key, i.e. the literal
//                                         SWEP:SetWeaponHoldType() string
//   s_GMod_HoldTypes[].pszBaseActivity  - the *name* of the ActIndex value,
//                                         i.e. the activity GMod treats as
//                                         "index + 0" for that hold type
//
// and the base activity is then resolved through the compiler's own
// ai_activity.h, one row per hold type.  A hold type whose base activity this
// fork does not define has base 0 ("no index"), which turns every entry into
// ACT_INVALID - which is the truthful answer, not a guess.  Every hold type in
// the table below now has a real block in ai_activity.h, so all of them resolve.
//-----------------------------------------------------------------------------
struct GModHoldTypeInfo_t
{
	const char	*pszName;			// GMod's spelling
	const char	*pszBaseActivity;	// GMod's ActIndex value (an ACT_* name)
	int			 iBaseActivity;		// that name resolved in *this* fork (0 = absent)
};

// The base activity column is filled in by GMod_InitHoldTypeTable() so the
// ACT_* names here are resolved by the compiler instead of being retyped as
// numbers.  ASCII order, which is also the enum's order.
static GModHoldTypeInfo_t s_GMod_HoldTypes[ HL2SB_HOLDTYPE_COUNT ] =
{
	{ "pistol",		"ACT_HL2MP_IDLE_PISTOL",	0 },
	{ "smg",		"ACT_HL2MP_IDLE_SMG1",		0 },
	{ "grenade",	"ACT_HL2MP_IDLE_GRENADE",	0 },
	{ "ar2",		"ACT_HL2MP_IDLE_AR2",		0 },
	{ "shotgun",	"ACT_HL2MP_IDLE_SHOTGUN",	0 },
	{ "rpg",		"ACT_HL2MP_IDLE_RPG",		0 },
	{ "physgun",	"ACT_HL2MP_IDLE_PHYSGUN",	0 },
	{ "crossbow",	"ACT_HL2MP_IDLE_CROSSBOW",	0 },
	{ "melee",		"ACT_HL2MP_IDLE_MELEE",		0 },
	{ "slam",		"ACT_HL2MP_IDLE_SLAM",		0 },
	{ "normal",		"ACT_HL2MP_IDLE",			0 },
	{ "fist",		"ACT_HL2MP_IDLE_FIST",		0 },
	{ "melee2",		"ACT_HL2MP_IDLE_MELEE2",	0 },
	{ "passive",	"ACT_HL2MP_IDLE_PASSIVE",	0 },
	{ "knife",		"ACT_HL2MP_IDLE_KNIFE",		0 },
	{ "duel",		"ACT_HL2MP_IDLE_DUEL",		0 },
	{ "camera",		"ACT_HL2MP_IDLE_CAMERA",	0 },
	{ "magic",		"ACT_HL2MP_IDLE_MAGIC",		0 },
	{ "revolver",	"ACT_HL2MP_IDLE_REVOLVER",	0 },
};

// Garry's Mod's one hand-written entry:
//
//     -- "normal" jump animation doesn't exist
//     if ( t == "normal" ) then self.ActivityTranslate[ ACT_MP_JUMP ] = ACT_HL2MP_JUMP_SLAM end
//
// ACT_MP_JUMP is slot 7 below (this fork's ACT_HL2MP_JUMP_<HT> member), so for
// "normal" the override replaces that one slot's answer - which is exactly what
// the Lua does by mutating its own ActivityTranslate table.  The other hold types
// keep their ACT_HL2MP_JUMP_<HT> member.
static const Activity GMOD_NORMAL_JUMP = ACT_HL2MP_JUMP_SLAM;

//-----------------------------------------------------------------------------
// Where each GMod activity goes, slot by slot.
//
// Garry's Mod resolves its table by *arithmetic* on its own ACT_HL2MP_* block
// (index + N), so the block must have ten members per hold type in GMod's order.
// ai_activity.h now does exactly that (see the generated family there): every
// hold type has the same ten slots, in the same order, so ACT_HL2MP_IDLE_<HT> + N
// is the member named N in this table for every hold type - and, because the base
// block follows the same order, ACT_HL2MP_IDLE + 1..6 and +9 are WALK, RUN,
// IDLE_CROUCH, WALK_CROUCH, GESTURE_RANGE_ATTACK, GESTURE_RELOAD and SWIM, which
// is what GMod's own gamemode/animations.lua computes.
//
//   slot  member                          key that names it                GMod offset
//   0     ACT_HL2MP_IDLE_<HT>             ACT_MP_STAND_IDLE                +0
//   1     ACT_HL2MP_WALK_<HT>             ACT_MP_WALK                      +1
//   2     ACT_HL2MP_RUN_<HT>              ACT_MP_RUN                       +2
//   3     ACT_HL2MP_IDLE_CROUCH_<HT>      ACT_MP_CROUCH_IDLE               +3
//   4     ACT_HL2MP_WALK_CROUCH_<HT>      ACT_MP_CROUCHWALK                +4
//   5     ..._GESTURE_RANGE_ATTACK_<HT>   ACT_MP_ATTACK_STAND_PRIMARYFIRE  +5
//   6     ..._GESTURE_RELOAD_<HT>         ACT_MP_RELOAD_STAND              +6
//   7     ACT_HL2MP_JUMP_<HT>             ACT_MP_JUMP                      +7
//   8     ACT_HL2MP_SWIM_IDLE_<HT>        ACT_MP_SWIM_IDLE                 +8
//   9     ACT_HL2MP_SWIM_<HT>             ACT_MP_SWIM                      +9
//
// Two more keys name the same member as a slot above (sh_anim.lua gives them the
// same offset as it gives that slot), so they are aliases and not slots of their
// own:
//
//   ACT_MP_ATTACK_CROUCH_PRIMARYFIRE  -> slot 5, same member as the standing fire
//   ACT_MP_RELOAD_CROUCH              -> slot 6, same member as the standing reload
//
// One key still names a member this fork declares for no hold type, so it is
// deliberately absent from the table:
//
//   ACT_RANGE_ATTACK1    the block has no melee/ranged-attack member (GMod's own
//                        table has no entry for it either - sh_anim.lua only
//                        names it in a comment)
//
// For that key GMod_TranslateActivity() returns the input activity unchanged
// (GMod's own "no entry in the table" behaviour), which is what makes the
// fallback chain hand back the untranslated activity - never -1 and never 0.
//
// Each slot is still filled through a name check rather than assumed: if
// ai_activity.h is ever reordered, the build fails on the static asserts below
// instead of a slot silently becoming another hold type's animation.
//-----------------------------------------------------------------------------
#define GMOD_MAX_ROLE		10		// members in this fork's ACT_HL2MP_* block
#define GMOD_MAX_HOLDTYPE	19		// entries in s_GMod_HoldTypes

// Member-name suffixes of the fork's ACT_HL2MP_* block, indexed by slot.
// This is Garry's Mod's own slot order, which ai_activity.h now mirrors, so
// ACT_HL2MP_IDLE_<HT> + slot == the member named here for every hold type.
static const char *const s_HL2MPMemberSuffix[ GMOD_MAX_ROLE ] =
{
	"_IDLE",					// 0  ACT_MP_STAND_IDLE
	"_WALK",					// 1  ACT_MP_WALK
	"_RUN",						// 2  ACT_MP_RUN
	"_IDLE_CROUCH",				// 3  ACT_MP_CROUCH_IDLE
	"_WALK_CROUCH",				// 4  ACT_MP_CROUCHWALK
	"_GESTURE_RANGE_ATTACK",	// 5  ACT_MP_ATTACK_STAND_PRIMARYFIRE
	"_GESTURE_RELOAD",			// 6  ACT_MP_RELOAD_STAND
	"_JUMP",					// 7  ACT_MP_JUMP
	"_SWIM_IDLE",				// 8  ACT_MP_SWIM_IDLE
	"_SWIM",					// 9  ACT_MP_SWIM
};

// The hold type part of an ACT_HL2MP_* member name, for the block-based hold
// types ("_PISTOL" -> ACT_HL2MP_IDLE_PISTOL).  Indexed like s_GMod_HoldTypes;
// the "normal" row is "" (the generic block carries no hold type part).
static const char *const s_GMod_HoldTypeSuffix[ HL2SB_HOLDTYPE_COUNT ] =
{
	"_PISTOL", "_SMG1", "_GRENADE", "_AR2", "_SHOTGUN", "_RPG", "_PHYSGUN",
	"_CROSSBOW", "_MELEE", "_SLAM", "", "_FIST", "_MELEE2", "_PASSIVE",
	"_KNIFE", "_DUEL", "_CAMERA", "_MAGIC", "_REVOLVER"
};

// ACT_MP_* keys, in the same order as s_HL2MPMemberSuffix - which is what makes
// the translation table a flat [hold type][slot] array.  These are GMod's own
// offsets (sh_anim.lua / animations.lua), matched to the member of the same
// name.  The two keys sh_anim.lua gives a shared offset are in the alias table
// below, not here.
static const Activity s_GMod_TranslatedKeys[ GMOD_MAX_ROLE ] =
{
	ACT_MP_STAND_IDLE,					// 0  ACT_HL2MP_IDLE_<HT>
	ACT_MP_WALK,						// 1  ACT_HL2MP_WALK_<HT>
	ACT_MP_RUN,							// 2  ACT_HL2MP_RUN_<HT>
	ACT_MP_CROUCH_IDLE,					// 3  ACT_HL2MP_IDLE_CROUCH_<HT>
	ACT_MP_CROUCHWALK,					// 4  ACT_HL2MP_WALK_CROUCH_<HT>
	ACT_MP_ATTACK_STAND_PRIMARYFIRE,	// 5  ..._GESTURE_RANGE_ATTACK_<HT>
	ACT_MP_RELOAD_STAND,				// 6  ..._GESTURE_RELOAD_<HT>
	ACT_MP_JUMP,						// 7  ACT_HL2MP_JUMP_<HT>
	ACT_MP_SWIM_IDLE,					// 8  ACT_HL2MP_SWIM_IDLE_<HT>
	ACT_MP_SWIM,						// 9  ACT_HL2MP_SWIM_<HT>
};

// sh_anim.lua gives two more keys the *same offset* as the slot key above them -
// ACT_MP_ATTACK_CROUCH_PRIMARYFIRE is also "+5" (sh_anim.lua:47) and
// ACT_MP_RELOAD_CROUCH is also "+6" (sh_anim.lua:49) - so they answer with that
// slot's member.  They are aliases of a key, not slots of their own: the member
// set stays ten per hold type.
static const struct GModTranslatedKeyAlias_t
{
	Activity	key;		// the extra GMod key
	Activity	target;		// the slot key it shares a member with
}
s_GMod_TranslatedKeyAliases[] =
{
	{ ACT_MP_ATTACK_CROUCH_PRIMARYFIRE,	ACT_MP_ATTACK_STAND_PRIMARYFIRE },
	{ ACT_MP_RELOAD_CROUCH,				ACT_MP_RELOAD_STAND },
};

//-----------------------------------------------------------------------------
// Compile-time proof of the arithmetic GMod_MemberActivity() relies on.
//
// The runtime name check below catches a member whose *name* is not what the
// suffix tables say.  It cannot catch the other half of the contract, which is
// that member N of a block is numerically ACT_HL2MP_IDLE_<HT> + N - and that is
// exactly what lets GMod_MemberActivity() reach member N without a name lookup
// table of its own.  These asserts pin all 22 of the fork's hold-type blocks (ten
// members each, in GMod's slot order) to their declared slot; reordering
// ai_activity.h now fails the build instead of silently degrading a slot to
// ACT_INVALID.
//-----------------------------------------------------------------------------
#define GMOD_STATIC_CHECK_MEMBER( ht, member, slot ) \
	static_assert( ACT_HL2MP_##member##_##ht == ACT_HL2MP_IDLE_##ht + ( slot ), \
				   "ACT_HL2MP_" #member "_" #ht " is not ACT_HL2MP_IDLE_" #ht " + " #slot )

#define GMOD_STATIC_CHECK_HOLDTYPE( ht ) \
	GMOD_STATIC_CHECK_MEMBER( ht, IDLE, 0 ); \
	GMOD_STATIC_CHECK_MEMBER( ht, WALK, 1 ); \
	GMOD_STATIC_CHECK_MEMBER( ht, RUN, 2 ); \
	GMOD_STATIC_CHECK_MEMBER( ht, IDLE_CROUCH, 3 ); \
	GMOD_STATIC_CHECK_MEMBER( ht, WALK_CROUCH, 4 ); \
	GMOD_STATIC_CHECK_MEMBER( ht, GESTURE_RANGE_ATTACK, 5 ); \
	GMOD_STATIC_CHECK_MEMBER( ht, GESTURE_RELOAD, 6 ); \
	GMOD_STATIC_CHECK_MEMBER( ht, JUMP, 7 ); \
	GMOD_STATIC_CHECK_MEMBER( ht, SWIM_IDLE, 8 ); \
	GMOD_STATIC_CHECK_MEMBER( ht, SWIM, 9 )

#define GMOD_STATIC_CHECK_GENERIC_MEMBER( member, slot ) \
	static_assert( ACT_HL2MP_##member == ACT_HL2MP_IDLE + ( slot ), \
				   "ACT_HL2MP_" #member " is not ACT_HL2MP_IDLE + " #slot )

GMOD_STATIC_CHECK_HOLDTYPE( PISTOL );
GMOD_STATIC_CHECK_HOLDTYPE( SMG1 );
GMOD_STATIC_CHECK_HOLDTYPE( GRENADE );
GMOD_STATIC_CHECK_HOLDTYPE( AR2 );
GMOD_STATIC_CHECK_HOLDTYPE( SHOTGUN );
GMOD_STATIC_CHECK_HOLDTYPE( RPG );
GMOD_STATIC_CHECK_HOLDTYPE( PHYSGUN );
GMOD_STATIC_CHECK_HOLDTYPE( CROSSBOW );
GMOD_STATIC_CHECK_HOLDTYPE( MELEE );
GMOD_STATIC_CHECK_HOLDTYPE( SLAM );
GMOD_STATIC_CHECK_HOLDTYPE( FIST );
GMOD_STATIC_CHECK_HOLDTYPE( MELEE2 );
GMOD_STATIC_CHECK_HOLDTYPE( PASSIVE );
GMOD_STATIC_CHECK_HOLDTYPE( KNIFE );
GMOD_STATIC_CHECK_HOLDTYPE( DUEL );
GMOD_STATIC_CHECK_HOLDTYPE( CAMERA );
GMOD_STATIC_CHECK_HOLDTYPE( MAGIC );
GMOD_STATIC_CHECK_HOLDTYPE( REVOLVER );
GMOD_STATIC_CHECK_HOLDTYPE( ANGRY );
GMOD_STATIC_CHECK_HOLDTYPE( SCARED );
GMOD_STATIC_CHECK_HOLDTYPE( ZOMBIE );
GMOD_STATIC_CHECK_HOLDTYPE( SUITCASE );

GMOD_STATIC_CHECK_GENERIC_MEMBER( IDLE, 0 );
GMOD_STATIC_CHECK_GENERIC_MEMBER( WALK, 1 );
GMOD_STATIC_CHECK_GENERIC_MEMBER( RUN, 2 );
GMOD_STATIC_CHECK_GENERIC_MEMBER( IDLE_CROUCH, 3 );
GMOD_STATIC_CHECK_GENERIC_MEMBER( WALK_CROUCH, 4 );
GMOD_STATIC_CHECK_GENERIC_MEMBER( GESTURE_RANGE_ATTACK, 5 );
GMOD_STATIC_CHECK_GENERIC_MEMBER( GESTURE_RELOAD, 6 );
GMOD_STATIC_CHECK_GENERIC_MEMBER( JUMP, 7 );
GMOD_STATIC_CHECK_GENERIC_MEMBER( SWIM_IDLE, 8 );
GMOD_STATIC_CHECK_GENERIC_MEMBER( SWIM, 9 );

// The two "how many" constants must stay tied to the enums they index, or the
// loops below would walk past their arrays.
static_assert( GMOD_MAX_ROLE == 10, "the fork's HL2MP block has ten members" );
static_assert( GMOD_MAX_HOLDTYPE == HL2SB_HOLDTYPE_COUNT,
			   "s_GMod_HoldTypes must have one row per GModHoldType_t value" );

static Activity s_GMod_ActivityTranslate[ GMOD_MAX_HOLDTYPE ][ GMOD_MAX_ROLE ];
static bool s_GMod_TranslateTableBuilt = false;

//-----------------------------------------------------------------------------
// Helpers
//-----------------------------------------------------------------------------

static bool GMod_IsValidHoldType( int iHoldType )
{
	return iHoldType >= 0 && iHoldType < GMOD_MAX_HOLDTYPE;
}

static bool GMod_IsResolvableActivity( Activity act )
{
	// ACT_INVALID is the "no activity" sentinel; ACT_RESET (0) is a command to
	// restart the current activity, not something a model has a sequence for.
	// SelectWeightedSequence() asserts on both, so they must never get there.
	return act > ACT_RESET;
}

// Resolve a hold type's member for one slot, *by name*.
//
// The name is built from the slot's suffix plus the hold type's own suffix
// ("_IDLE" + "_PISTOL"), so "smg" asks for ACT_HL2MP_IDLE_SMG1 and never for
// ACT_HL2MP_IDLE_SMG.  Returns ACT_INVALID when this fork does not declare that
// member, or when the arithmetic answer and the name disagree.
//
// The lookup is arithmetic-first with a *name check*: every hold type now has the
// same ten slots in the same order, so iBase + iSlot is the member, and the check
// is what keeps a future reordering of ai_activity.h from making a slot resolve
// to another hold type's animation.
static Activity GMod_MemberActivity( int iHoldType, int iSlot )
{
	const int iBase = s_GMod_HoldTypes[ iHoldType ].iBaseActivity;
	if ( !GMod_IsResolvableActivity( (Activity)iBase ) )
		return ACT_INVALID;

	const char *pszSuffix = s_GMod_HoldTypeSuffix[ iHoldType ];
	if ( !pszSuffix )
		return ACT_INVALID;

	char szExpected[ 64 ];
	Q_snprintf( szExpected, sizeof( szExpected ), "ACT_HL2MP%s%s",
				s_HL2MPMemberSuffix[ iSlot ], pszSuffix );

	const Activity iMember = (Activity)( iBase + iSlot );
	if ( Q_stricmp( GMod_ActivityName( iMember ), szExpected ) != 0 )
		return ACT_INVALID;

	return iMember;
}

//-----------------------------------------------------------------------------
// Table construction.  Runs once, lazily, on first query.  No allocation, no
// locking beyond the shared statics Source already has.
//-----------------------------------------------------------------------------
static void GMod_InitHoldTypeTable( void )
{
	if ( s_GMod_TranslateTableBuilt )
		return;

	s_GMod_TranslateTableBuilt = true;

	// 1. Resolve each hold type's base activity.  The compiler does the
	//    name -> number work here, which is the whole reason the table above
	//    stores names rather than numbers.
	s_GMod_HoldTypes[ HL2SB_HOLDTYPE_PISTOL ].iBaseActivity		= (Activity)ACT_HL2MP_IDLE_PISTOL;
	s_GMod_HoldTypes[ HL2SB_HOLDTYPE_SMG ].iBaseActivity		= (Activity)ACT_HL2MP_IDLE_SMG1;
	s_GMod_HoldTypes[ HL2SB_HOLDTYPE_GRENADE ].iBaseActivity	= (Activity)ACT_HL2MP_IDLE_GRENADE;
	s_GMod_HoldTypes[ HL2SB_HOLDTYPE_AR2 ].iBaseActivity		= (Activity)ACT_HL2MP_IDLE_AR2;
	s_GMod_HoldTypes[ HL2SB_HOLDTYPE_SHOTGUN ].iBaseActivity	= (Activity)ACT_HL2MP_IDLE_SHOTGUN;
	s_GMod_HoldTypes[ HL2SB_HOLDTYPE_RPG ].iBaseActivity		= (Activity)ACT_HL2MP_IDLE_RPG;
	s_GMod_HoldTypes[ HL2SB_HOLDTYPE_PHYSGUN ].iBaseActivity	= (Activity)ACT_HL2MP_IDLE_PHYSGUN;
	s_GMod_HoldTypes[ HL2SB_HOLDTYPE_CROSSBOW ].iBaseActivity	= (Activity)ACT_HL2MP_IDLE_CROSSBOW;
	s_GMod_HoldTypes[ HL2SB_HOLDTYPE_MELEE ].iBaseActivity		= (Activity)ACT_HL2MP_IDLE_MELEE;
	s_GMod_HoldTypes[ HL2SB_HOLDTYPE_SLAM ].iBaseActivity		= (Activity)ACT_HL2MP_IDLE_SLAM;
	s_GMod_HoldTypes[ HL2SB_HOLDTYPE_NORMAL ].iBaseActivity		= (Activity)ACT_HL2MP_IDLE;
	s_GMod_HoldTypes[ HL2SB_HOLDTYPE_FIST ].iBaseActivity		= (Activity)ACT_HL2MP_IDLE_FIST;
	s_GMod_HoldTypes[ HL2SB_HOLDTYPE_MELEE2 ].iBaseActivity		= (Activity)ACT_HL2MP_IDLE_MELEE2;
	s_GMod_HoldTypes[ HL2SB_HOLDTYPE_PASSIVE ].iBaseActivity	= (Activity)ACT_HL2MP_IDLE_PASSIVE;
	s_GMod_HoldTypes[ HL2SB_HOLDTYPE_KNIFE ].iBaseActivity		= (Activity)ACT_HL2MP_IDLE_KNIFE;
	s_GMod_HoldTypes[ HL2SB_HOLDTYPE_DUEL ].iBaseActivity		= (Activity)ACT_HL2MP_IDLE_DUEL;
	s_GMod_HoldTypes[ HL2SB_HOLDTYPE_CAMERA ].iBaseActivity		= (Activity)ACT_HL2MP_IDLE_CAMERA;
	s_GMod_HoldTypes[ HL2SB_HOLDTYPE_MAGIC ].iBaseActivity		= (Activity)ACT_HL2MP_IDLE_MAGIC;
	s_GMod_HoldTypes[ HL2SB_HOLDTYPE_REVOLVER ].iBaseActivity	= (Activity)ACT_HL2MP_IDLE_REVOLVER;
	// Every hold type in the table now has a real block in ai_activity.h, so all
	// 19 rows resolve; the runtime self-check below proves it.

	// 2. GMod's ActivityTranslate, entry by entry.
	for ( int i = 0; i < GMOD_MAX_HOLDTYPE; ++i )
	{
		// Each key in s_GMod_TranslatedKeys names the member at its slot (see the
		// slot table in the comment block above); GMod_MemberActivity() refuses
		// the slot if the fork's own name does not match.  The two shared keys
		// (ACT_MP_ATTACK_CROUCH_PRIMARYFIRE, ACT_MP_RELOAD_CROUCH) are resolved
		// to these slots by s_GMod_TranslatedKeyAliases in
		// GMod_TranslateActivity(); the one key that names no member here
		// (ACT_RANGE_ATTACK1) is not in the table at all, so no slot answers it
		// -> ACT_INVALID -> the caller gets its own activity back, never -1 and
		// never 0.  The "normal" ACT_MP_JUMP override is applied by
		// GMod_TranslateActivity() when the key is asked for, not stored into
		// the matrix here.
		for ( int iSlot = 0; iSlot < GMOD_MAX_ROLE; ++iSlot )
			s_GMod_ActivityTranslate[ i ][ iSlot ] = GMod_MemberActivity( i, iSlot );
	}

	// 3. Runtime self-check of the matrix just built.  The compile-time asserts
	//    above already pin the arithmetic; this catches the other half - the
	//    engine's activity registry disagreeing with the members named by
	//    s_HL2MPMemberSuffix/s_GMod_HoldTypeSuffix, which would otherwise turn a
	//    whole hold type into ACT_INVALID silently.
	for ( int i = 0; i < GMOD_MAX_HOLDTYPE; ++i )
	{
		int iResolved = 0;
		for ( int iSlot = 0; iSlot < GMOD_MAX_ROLE; ++iSlot )
		{
			if ( s_GMod_ActivityTranslate[ i ][ iSlot ] != ACT_INVALID )
				++iResolved;
		}

		const bool bHasBlock = ( s_GMod_HoldTypeSuffix[ i ] != NULL );
		AssertMsg( iResolved == ( bHasBlock ? GMOD_MAX_ROLE : 0 ),
				   "GMod hold type table: \"%s\" resolved %d of %d members (block %s)\n",
				   s_GMod_HoldTypes[ i ].pszName, iResolved, GMOD_MAX_ROLE,
				   bHasBlock ? "declared" : "absent" );
	}
}

//-----------------------------------------------------------------------------
// Public API
//-----------------------------------------------------------------------------

int GMod_HoldTypeBaseActivity( int iHoldType )
{
	if ( !GMod_IsValidHoldType( iHoldType ) )
		return 0;

	GMod_InitHoldTypeTable();
	return (int)s_GMod_HoldTypes[ iHoldType ].iBaseActivity;
}

int GMod_HoldTypeFromString( const char *pszHoldType )
{
	if ( !pszHoldType || !pszHoldType[0] )
		return HL2SB_HOLDTYPE_INVALID;

	for ( int i = 0; i < GMOD_MAX_HOLDTYPE; ++i )
	{
		if ( V_stricmp( s_GMod_HoldTypes[ i ].pszName, pszHoldType ) == 0 )
			return i;
	}

	return HL2SB_HOLDTYPE_INVALID;
}

const char *GMod_HoldTypeName( int iHoldType )
{
	if ( !GMod_IsValidHoldType( iHoldType ) )
		return "invalid";

	return s_GMod_HoldTypes[ iHoldType ].pszName;
}

Activity GMod_TranslateActivity( int iHoldType, Activity act )
{
	if ( !GMod_IsValidHoldType( iHoldType ) )
		return act;

	GMod_InitHoldTypeTable();

	// Which slot does this activity address?  First resolve sh_anim.lua's two
	// shared keys to the slot key they name (s_GMod_TranslatedKeyAliases), then
	// find that key in the slot table.  `act` itself stays untouched so the
	// "no member here" path below can hand the *caller's* activity back.
	Activity actForSlot = act;
	for ( int i = 0; i < ARRAYSIZE( s_GMod_TranslatedKeyAliases ); ++i )
	{
		if ( s_GMod_TranslatedKeyAliases[ i ].key == act )
		{
			actForSlot = s_GMod_TranslatedKeyAliases[ i ].target;
			break;
		}
	}

	int iSlot = -1;
	for ( int i = 0; i < GMOD_MAX_ROLE; ++i )
	{
		if ( s_GMod_TranslatedKeys[ i ] == actForSlot )
		{
			iSlot = i;
			break;
		}
	}

	// Garry's Mod's "normal" jump override (sh_anim.lua:56-58): for "normal" the
	// ACT_MP_JUMP entry is replaced by ACT_HL2MP_JUMP_SLAM.  Every other hold type
	// answers ACT_MP_JUMP from slot 7 (its ACT_HL2MP_JUMP_<HT> member).
	if ( act == ACT_MP_JUMP && iHoldType == HL2SB_HOLDTYPE_NORMAL )
		return GMOD_NORMAL_JUMP;

	if ( iSlot >= 0 )
	{
		const Activity iTranslated = s_GMod_ActivityTranslate[ iHoldType ][ iSlot ];
		if ( iTranslated != ACT_INVALID )
			return iTranslated;
	}

	// GMod's own behaviour when the weapon's ActivityTranslate has no entry for
	// this activity: SWEP:TranslateActivity() returns -1 and GM:TranslateActivity
	// keeps the activity it was given.  That covers the one key that names no
	// member here (ACT_RANGE_ATTACK1), every activity sh_anim.lua does not
	// mention at all (ACT_MP_JUMP_LAND, ACT_MP_AIRWALK, every ACT_VM_*, ...), and
	// any activity the target model simply has no sequence for.
	return act;
}

// Can this model actually play this activity?  Model must be non-NULL: the
// engine's SelectWeightedSequence/LookupActivity assert on a NULL model.
static bool GMod_ModelHasActivity( CBaseAnimating *pModel, Activity act )
{
	if ( !pModel || !pModel->GetModelPtr() )
		return false;

	if ( !GMod_IsResolvableActivity( act ) )
		return false;

	const char *pszName = GMod_ActivityName( act );
	if ( !pszName || Q_stricmp( pszName, "ACT_UNKNOWN" ) == 0 )
		return false;

	// The model only knows the activity if its own $sequence/activity list
	// names it (LookupActivity walks the model's activity names).
	if ( pModel->LookupActivity( pszName ) == ACT_INVALID )
		return false;

	// ...and it needs at least one sequence carrying that activity.
	// SelectWeightedSequence returns ACTIVITY_NOT_AVAILABLE (-1) otherwise.
	// Both sentinels are <= 0; sequence 0 is the "reference" pose, not a
	// selectable activity on these player models, and the engine's own callers
	// (e.g. CBaseAnimatingOverlay) treat <= 0 as "not found" too.
	return pModel->SelectWeightedSequence( act ) > 0;
}

Activity GMod_TranslateActivityAndFallback( int iHoldType, Activity act, CBaseAnimating *pModel,
											GModActivityFallback_t *pUsedStep )
{
	if ( pUsedStep )
		*pUsedStep = HL2SB_ACTFALLBACK_NONE;

	if ( !pModel || !pModel->GetModelPtr() )
	{
		// Nothing to test against: hand back the translation, which is what a
		// caller with no model can act on.  (GMod does the same - the Lua
		// translation never looks at the model.)  A caller that needs
		// ACT_INVALID for "no model" can check the pointer itself.
		return GMod_TranslateActivity( iHoldType, act );
	}

	// 1. the translated activity
	const Activity iTranslated = GMod_TranslateActivity( iHoldType, act );
	if ( iTranslated != act && GMod_ModelHasActivity( pModel, iTranslated ) )
	{
		if ( pUsedStep ) *pUsedStep = HL2SB_ACTFALLBACK_TRANSLATED;
		return iTranslated;
	}

	// 2. the untranslated activity (also the "hold type had no entry" result)
	if ( GMod_ModelHasActivity( pModel, act ) )
	{
		if ( pUsedStep ) *pUsedStep = HL2SB_ACTFALLBACK_UNTRANSLATED;
		return act;
	}

	// 3./4./5. generic idles, most specific first
	static const Activity s_FallbackIdles[] =
	{
		ACT_MP_STAND_IDLE,		// GMod GM:CalcMainActivity's default
		ACT_HL2MP_IDLE,			// this fork's HL2MP player idle
		ACT_IDLE,				// plain Source idle
	};
	static const GModActivityFallback_t s_FallbackSteps[] =
	{
		HL2SB_ACTFALLBACK_STAND_IDLE,
		HL2SB_ACTFALLBACK_HL2MP_IDLE,
		HL2SB_ACTFALLBACK_IDLE,
	};

	for ( int i = 0; i < ARRAYSIZE( s_FallbackIdles ); ++i )
	{
		if ( GMod_ModelHasActivity( pModel, s_FallbackIdles[ i ] ) )
		{
			if ( pUsedStep ) *pUsedStep = s_FallbackSteps[ i ];
			return s_FallbackIdles[ i ];
		}
	}

	return ACT_INVALID;
}

//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//
//=============================================================================//
#include "cbase.h"

#ifdef CLIENT_DLL
#include "c_hl2mp_player.h"
#include "prediction.h"
#include "iclientvehicle.h"		// HL2SB: vehicle animation state
#define CRecipientFilter C_RecipientFilter
#else
#include "hl2mp_player.h"
#include "iservervehicle.h"		// HL2SB: vehicle animation state
#endif

#include "hl2mp_gamerules.h"
#include "activitylist.h"
#include "Multiplayer/multiplayer_animstate.h"	// HL2SB: GESTURE_SLOT_* + PlayerAnimEvent_t for the client gesture system

// HL2SB diagnostic. Prints the body animation state of every HL2MP player once
// per second, on both realms, so the "legs play a few frames then stop" class of
// bug can be told apart without guessing (see AGENTS.md 27):
//
//   seq/activity  - which sequence was selected and what activity it carries
//   cycle         - 0 forever = something resets it, or nothing advances it
//   rate          - 0 = FrameAdvance() advances nothing
//   animtime      - 0 = game/client/c_baseanimating.cpp:5323 zeroes the interval
//   csa           - the client-side animation flag the two realms disagree about
//
// 0 = off. Works on the client and on the server (listen server: set it once).
ConVar hl2sb_anim_debug( "hl2sb_anim_debug", "0", FCVAR_REPLICATED | FCVAR_NOTIFY, "Print per-second HL2MP player animation state (sequence/cycle/rate)." );

#include "engine/IEngineSound.h"
#include "SoundEmitterSystem/isoundemittersystembase.h"

#include "luamanager.h"
#include "lhl2mp_player_shared.h"
#include "lbaseplayer_shared.h"
#include "mathlib/lvector.h"
#include "lvphysics_interface.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

extern ConVar sv_footsteps;

const char *g_ppszPlayerSoundPrefixNames[PLAYER_SOUNDS_MAX] =
{
	"NPC_Citizen",
	"NPC_CombineS",
	"NPC_MetroPolice",
};

bool CHL2MP_Player::ShouldCollide( int collisionGroup, int contentsMask ) const
{
	if ( HL2SB_GetNoCollideWithTeammates() &&
		( collisionGroup == COLLISION_GROUP_PLAYER || collisionGroup == COLLISION_GROUP_PLAYER_MOVEMENT ||
		  ( collisionGroup == COLLISION_GROUP_PROJECTILE && !( contentsMask & CONTENTS_MONSTER ) ) ) )
	{
		int teamMask = 0;
		switch ( GetTeamNumber() )
		{
		case 1: teamMask = 0x800; break;
		case 2: teamMask = 0x1000; break;
		case 3: teamMask = 0x400; break;
		case 4: teamMask = 0x200; break;
		}
		if ( teamMask && !( contentsMask & teamMask ) )
			return false;
	}
	return BaseClass::ShouldCollide( collisionGroup, contentsMask );
}

#ifndef CLIENT_DLL
bool HL2SB_NonCollidingTeammates( CBaseEntity *first, CBaseEntity *second )
{
	CHL2MP_Player *a = ToHL2MPPlayer( first );
	CHL2MP_Player *b = ToHL2MPPlayer( second );
	return a && b && a->HL2SB_GetNoCollideWithTeammates() && b->HL2SB_GetNoCollideWithTeammates() &&
		a->GetTeamNumber() == b->GetTeamNumber();
}
#endif

const char *CHL2MP_Player::GetPlayerModelSoundPrefix( void )
{
	// HL2SB: m_iPlayerSoundType is not range checked anywhere, and for a custom
	// playermodel it is never assigned at all (see SetupPlayerSoundsByModel in
	// server/hl2mp/hl2mp_player.cpp - it only matches human/police/combine, with no
	// else branch). Indexing this 3-entry table with uninitialised memory handed a
	// wild pointer to DeathSound()/footsteps -> access violation on death.
	// Crash dump 20260915_091815: CHL2MP_Player::GetPlayerModelSoundPrefix+0xE
	// [hl2mp_player_shared.cpp:56], Rax=0x3FDA3D71, AV READ of 0x00008000D9D419B8.
	if ( m_iPlayerSoundType < 0 || m_iPlayerSoundType >= PLAYER_SOUNDS_MAX )
	{
		return g_ppszPlayerSoundPrefixNames[PLAYER_SOUNDS_CITIZEN];
	}

	return g_ppszPlayerSoundPrefixNames[m_iPlayerSoundType];
}

void CHL2MP_Player::PrecacheFootStepSounds( void )
{
	int iFootstepSounds = ARRAYSIZE( g_ppszPlayerSoundPrefixNames );
	int i;

	for ( i = 0; i < iFootstepSounds; ++i )
	{
		char szFootStepName[128];

		Q_snprintf( szFootStepName, sizeof( szFootStepName ), "%s.RunFootstepLeft", g_ppszPlayerSoundPrefixNames[i] );
		PrecacheScriptSound( szFootStepName );

		Q_snprintf( szFootStepName, sizeof( szFootStepName ), "%s.RunFootstepRight", g_ppszPlayerSoundPrefixNames[i] );
		PrecacheScriptSound( szFootStepName );
	}
}

//-----------------------------------------------------------------------------
// Consider the weapon's built-in accuracy, this character's proficiency with
// the weapon, and the status of the target. Use this information to determine
// how accurately to shoot at the target.
//-----------------------------------------------------------------------------
Vector CHL2MP_Player::GetAttackSpread( CBaseCombatWeapon *pWeapon, CBaseEntity *pTarget )
{
	if ( pWeapon )
		return pWeapon->GetBulletSpread( WEAPON_PROFICIENCY_PERFECT );
	
	return VECTOR_CONE_15DEGREES;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : step - 
//			fvol - 
//			force - force sound to play
//-----------------------------------------------------------------------------
void CHL2MP_Player::PlayStepSound( Vector &vecOrigin, surfacedata_t *psurface, float fvol, bool force )
{
#if defined( LUA_SDK )
	BEGIN_LUA_CALL_HOOK( "PlayerPlayStepSound" );
		lua_pushhl2mpplayer( L, this );
		lua_pushvector( L, vecOrigin );
		lua_pushsurfacedata( L, psurface );
		lua_pushnumber( L, fvol );
		lua_pushboolean( L, force );
	END_LUA_CALL_HOOK( 5, 1 );

	RETURN_LUA_NONE();
#endif

	if ( gpGlobals->maxClients > 1 && !sv_footsteps.GetFloat() )
		return;

#if defined( CLIENT_DLL )
	// during prediction play footstep sounds only once
	if ( !prediction->IsFirstTimePredicted() )
		return;
#endif

	if ( GetFlags() & FL_DUCKING )
		return;

	m_Local.m_nStepside = !m_Local.m_nStepside;

	char szStepSound[128];

	if ( m_Local.m_nStepside )
	{
		Q_snprintf( szStepSound, sizeof( szStepSound ), "%s.RunFootstepLeft", GetPlayerModelSoundPrefix() );
	}
	else
	{
		Q_snprintf( szStepSound, sizeof( szStepSound ), "%s.RunFootstepRight", GetPlayerModelSoundPrefix() );
	}

	CSoundParameters params;
	if ( GetParametersForSound( szStepSound, params, NULL ) == false )
		return;

	CRecipientFilter filter;
	filter.AddRecipientsByPAS( vecOrigin );

#ifndef CLIENT_DLL
	// im MP, server removed all players in origins PVS, these players 
	// generate the footsteps clientside
	if ( gpGlobals->maxClients > 1 )
		filter.RemoveRecipientsByPVS( vecOrigin );
#endif

	EmitSound_t ep;
	ep.m_nChannel = CHAN_BODY;
	ep.m_pSoundName = params.soundname;
	ep.m_flVolume = fvol;
	ep.m_SoundLevel = params.soundlevel;
	ep.m_nFlags = 0;
	ep.m_nPitch = params.pitch;
	ep.m_pOrigin = &vecOrigin;

	EmitSound( filter, entindex(), ep );
}


//==========================
// ANIMATION CODE
//==========================


// Below this many degrees, slow down turning rate linearly
#define FADE_TURN_DEGREES	45.0f
// After this, need to start turning feet
#define MAX_TORSO_ANGLE		90.0f
// Below this amount, don't play a turning animation/perform IK
#define MIN_TURN_ANGLE_REQUIRING_TURN_ANIMATION		15.0f

static ConVar tf2_feetyawrunscale( "tf2_feetyawrunscale", "2", FCVAR_REPLICATED, "Multiplier on tf2_feetyawrate to allow turning faster when running." );
extern ConVar sv_backspeed;
extern ConVar mp_feetyawrate;
extern ConVar mp_facefronttime;
extern ConVar mp_ik;

//-----------------------------------------------------------------------------
// HL2SB: GMod-style gesture name -> sequence.
//
// GMod's `act` command is not a fixed list: it takes any gesture the playermodel
// provides (the m_anm family declares 32 ACT_GMOD_* activities plus the sequences
// behind them). So: try the activity-name spellings those models use, and if none of
// them resolves, scan the model's own activity names / sequence labels.
//-----------------------------------------------------------------------------
int HL2SB_ResolveGestureSequence( CBaseAnimating *pAnim, const char *pszName, Activity *pActivityOut )
{
	if ( pActivityOut )
		*pActivityOut = ACT_INVALID;

	if ( !pAnim || !pszName || !pszName[0] )
		return -1;

	CStudioHdr *pStudioHdr = pAnim->GetModelPtr();
	if ( !pStudioHdr )
		return -1;

	char szUpper[128];
	Q_strncpy( szUpper, pszName, sizeof( szUpper ) );
	Q_strupr( szUpper );
	for ( int i = 0; szUpper[i]; ++i )
	{
		if ( szUpper[i] == ' ' )
			szUpper[i] = '_';
	}

	static const char *s_pGesturePrefixes[] =
	{
		"ACT_GMOD_%s",
		"ACT_GMOD_GESTURE_%s",
		"ACT_GMOD_TAUNT_%s",
		"ACT_GMOD_GESTURE_TAUNT_%s",
		"ACT_%s",
	};

	for ( int i = 0; i < ARRAYSIZE( s_pGesturePrefixes ); ++i )
	{
		char szActivityName[160];
		Q_snprintf( szActivityName, sizeof( szActivityName ), s_pGesturePrefixes[i], szUpper );

		const int nActivity = ActivityList_IndexForName( szActivityName );
		if ( nActivity == ACT_INVALID )
			continue;

		const int iSequence = pAnim->SelectWeightedSequence( (Activity)nActivity );
		if ( iSequence > 0 )
		{
			if ( pActivityOut )
				*pActivityOut = (Activity)nActivity;

			return iSequence;
		}
	}

	const int nSequences = pStudioHdr->GetNumSeq();
	for ( int i = 0; i < nSequences; ++i )
	{
		const char *pszActivityName = pStudioHdr->pSeqdesc( i ).pszActivityName();
		const char *pszLabel = pStudioHdr->pSeqdesc( i ).pszLabel();

		if ( ( pszActivityName && Q_stristr( pszActivityName, szUpper ) ) ||
			 ( pszLabel && Q_stristr( pszLabel, szUpper ) ) )
		{
			if ( pActivityOut )
				*pActivityOut = (Activity)pAnim->GetSequenceActivity( i );

			return i;
		}
	}

	return -1;
}

//-----------------------------------------------------------------------------
// HL2SB: the seat animation, chosen from the VEHICLE + the PLAYER MODEL's own
// sequence data. This is GMod's GM:HandlePlayerDriving (and the seat's
// Members.HandleAnimation) ported to the engine side.
//
// GMod's chain, in order (gamemodes/base/gamemode/animations.lua:139-191):
//
//   1. the seat's Lua table: Members.HandleAnimation( ply ) -> a sequence index;
//      every GMod chair/pod/car-seat entry in lua/autorun/base_vehicles.lua uses
//      `ply:SelectWeightedSequence( ACT_GMOD_SIT_ROLLERCOASTER )` -> sit_rollercoaster
//   2. otherwise, BY CLASS:
//        prop_vehicle_jeep   -> ply:LookupSequence( "drive_jeep" )
//        prop_vehicle_airboat-> ply:LookupSequence( "drive_airboat" )
//        prop_vehicle_prisoner_pod + models/vehicles/prisoner_pod_inner.mdl
//                            -> ply:LookupSequence( "drive_pd" )
//        anything else       -> ply:LookupSequence( "sit_rollercoaster" )
//
// HL2SB's seats are spawned from a console line (SMenu_BuildVehicleListCommand),
// so step 1's data does not exist and the vehicle's CLASS replaces it - never a
// model name. Step 2's names ARE the same "one thing that survives between the
// engines" that HL2SB_SelectPlayerSequence pins: sequence LABELS. (They cannot be
// activities here: ACT_DRIVE_JEEP / ACT_DRIVE_AIRBOAT / ACT_DRIVE_POD do not exist
// in this fork's Activity enum at all - see game/shared/ai_activity.h - which is
// why the previous version fell through to the sit_<holdtype> family and posed a
// jeep driver with sit_pistol.)
//
// WHY THE POSE AND THE SEAT POINT MUST COME FROM THE SAME CONVENTION - the
// measured root of "the model sinks / is not sitting on the seat" (2026-09-16,
// dumped straight out of the models with tools/studiomdl_inspect.py; Z is up,
// values are the sequence's own bbmin/bbmax in model space, i.e. where the BODY
// is relative to the model ORIGIN the engine places at the seat point):
//
//   pose                  bbmin.z   bbmax.z   ORIGIN is at
//   sit_pistol/sit_*      -18.7     +35.0     the PELVIS (the seat surface):
//                                             eyes ~+26 = the chair's authored
//                                             feet->eyes delta (48.0-22.0 = 26.0
//                                             on nova/chair_office02)
//   sit_rollercoaster     -22.7     +43.9     the pelvis as well (bbmax is the
//                                             raised coaster ARMS, not the head)
//   drive_jeep             -6.3     +38.6     the FEET (floorboard): eyes ~+32 =
//                                             the jeep's measured eyes-minus-seat
//                                             32.0 (log: seat world z 24.7, camera
//                                             world z 56.7, hl2sb_veh_thirdperson_debug)
//   drive_airboat         -10.6     +36.5     the feet
//   drive_pd               -3.4     +72.9     the feet (the Valve pod is a STANDING
//                                             pose: 76 units tall, eyes at the
//                                             standing VEC_VIEW height)
//
// So `vehicle_feet_passenger0` is the seat SURFACE on the GMod chair props and
// the player's FEET on Valve's own vehicles, and only the matching pose lands the
// model on the seat. Playing sit_pistol in the jeep put the pelvis at the
// floorboard - 22.7 units below the jeep's seat - which is the reported sinking.
//
// Returns the sequence, or -1 when the model carries none of them (the caller then
// falls back to the old sit_<holdtype> family).
// *pActivityOut receives the activity that was asked for (ACT_INVALID when none applied).
//-----------------------------------------------------------------------------
int HL2SB_SelectVehicleSitSequence( CBaseAnimating *pAnim, CBaseEntity *pVehicle, Activity *pActivityOut )
{
	if ( pActivityOut )
		*pActivityOut = ACT_INVALID;

	if ( !pAnim || !pVehicle )
		return -1;

	const char *pszClass = pVehicle->GetClassname();
	if ( !pszClass || !pszClass[0] )
		return -1;

	// A sequence picked by NAME carries no activity of its own in this fork
	// (ACT_DRIVE_* are absent from the enum), so make sure the caller never gets
	// ACT_INVALID: m_Activity is what the rest of the animation code reads, and
	// ACT_HL2MP_SIT is the family the sit_* poses belong to.
	if ( pActivityOut )
		*pActivityOut = ACT_HL2MP_SIT;

	// --- 1. GMod's Members.HandleAnimation for a chair / pod / car seat ---------
	// Every chair-style entry in GMod's vehicle list is a prop_vehicle_prisoner_pod
	// whose handler asks for ACT_GMOD_SIT_ROLLERCOASTER. A pod whose model has no
	// such sequence (a Valve pod, the phx seats' plain ACT_HL2MP_SIT) falls through.
	if ( !Q_stricmp( pszClass, "prop_vehicle_prisoner_pod" ) )
	{
		const int iSequence = pAnim->SelectWeightedSequence( ACT_GMOD_SIT_ROLLERCOASTER );
		if ( iSequence >= 0 )
		{
			if ( pActivityOut )
				*pActivityOut = ACT_GMOD_SIT_ROLLERCOASTER;

			return iSequence;
		}

		return -1;
	}

	// --- 2. GMod's class table: the pose Valve authored for that exact seat -----
	// The names come from GMod, the EXISTENCE check comes from the player's model.
	const char *pszPoseName = NULL;
	if ( !Q_stricmp( pszClass, "prop_vehicle_jeep" ) || !Q_stricmp( pszClass, "prop_vehicle_jeep_old" ) )
	{
		pszPoseName = "drive_jeep";
	}
	else if ( !Q_stricmp( pszClass, "prop_vehicle_airboat" ) )
	{
		pszPoseName = "drive_airboat";
	}

	if ( pszPoseName )
	{
		const int iSequence = pAnim->LookupSequence( pszPoseName );
		if ( iSequence >= 0 )
		{
			if ( pActivityOut )
			{
				// The sequence's own activity when the enum has it, otherwise the
				// sit family (never ACT_INVALID - see above).
				const Activity seqActivity = (Activity)pAnim->GetSequenceActivity( iSequence );
				*pActivityOut = ( seqActivity != ACT_INVALID ) ? seqActivity : ACT_HL2MP_SIT;
			}

			return iSequence;
		}
	}

	return -1;
}

//-----------------------------------------------------------------------------
// HL2SB: THE seat pose for a player in a vehicle, one function for both realms.
//
// The server pins this in CHL2MP_Player::SetAnimation(); the client resolves the same
// sequence in CPlayerAnimState::UpdateVehicleAnimation() to tell "still seated" from
// "the ride is over but the vehicle entity has not been torn down yet" (the exit
// animation window - see the comment there). Keeping the chain in one place is what makes
// the two realms agree instead of each carrying its own copy of the fallback.
//-----------------------------------------------------------------------------
int HL2SB_ResolveSeatedSequence( CHL2MP_Player *pPlayer, CBaseEntity *pVehicle, Activity *pActivityOut )
{
	if ( pActivityOut )
		*pActivityOut = ACT_INVALID;

	if ( !pPlayer )
		return -1;

	int iSequence = HL2SB_SelectVehicleSitSequence( pPlayer, pVehicle, pActivityOut );
	if ( iSequence >= 0 )
		return iSequence;

	// Fallback: the sit_<holdtype> family (see AGENTS.md 24/26). It needs the
	// weapon-translated activity, which exists server-side only - the client's
	// C_BaseCombatCharacter has no Weapon_TranslateActivity() - so on the client this
	// resolves to nothing and the caller falls back to its other signal (the move parent,
	// which the client does get over the wire: c_baseentity.cpp:2636 PostDataUpdate ->
	// HierarchySetParent( m_hNetworkMoveParent )).
#ifdef CLIENT_DLL
	if ( pActivityOut )
		*pActivityOut = ACT_HL2MP_SIT;

	return -1;
#else
	Activity translatedActivity = pPlayer->Weapon_TranslateActivity( ACT_HL2MP_SIT );
	if ( translatedActivity == ACT_INVALID )
		translatedActivity = ACT_HL2MP_SIT;

	iSequence = HL2SB_SelectPlayerSequence( pPlayer, translatedActivity, ACT_HL2MP_SIT );

	// HL2SB: a model that carries NO sit pose at all - the HL2 citizen player
	// models (models/player/humans/group03/... -> models/humans/fomale_*) have no
	// "sit_rollercoaster" and no "sit_<holdtype>" - used to leave the branch with
	// -1 and the rider kept the gait he entered with: the reported "sitting in the
	// chair plays the run animation" (veh/sv log: parent=1 seat_pose=0
	// seq=28(run_pistol)). GMod shows the same gap as a standing rider; match that
	// by pinning the model's idle pose instead of leaving a sprint on the seat.
	// "sit_pistol" is tried by name as well, for the case where the weapon table
	// did not translate ACT_HL2MP_SIT into a hold type (then the family is empty
	// and SelectPlayerSequence never reaches its pistol fallback).
	if ( iSequence < 0 )
		iSequence = pPlayer->LookupSequence( "sit_pistol" );

	if ( iSequence < 0 )
	{
		iSequence = HL2SB_SelectPlayerSequence( pPlayer, ACT_HL2MP_IDLE, ACT_HL2MP_IDLE );

		if ( ( iSequence >= 0 ) && pActivityOut )
			*pActivityOut = ACT_HL2MP_IDLE;
	}
	else if ( pActivityOut )
	{
		*pActivityOut = ACT_HL2MP_SIT;
	}

	return iSequence;
#endif
}

//-----------------------------------------------------------------------------
// HL2SB: the sequence a vehicle / seat model rests in.
//
// "idle" by LABEL first (a data-driven name, the same kind of lookup
// HL2SB_SelectVehicleSitSequence already does with "drive_jeep"): every GMod seat model
// (models/nova/chair_office01/02, chair_wood01, chair_plastic01, jeep_seat, airboat_seat)
// and both Valve vehicles label their first sequence "idle". Falling back to sequence 0 is
// what the engine itself does - m_nSequence starts at 0 and no seat model has more than
// one sequence - so a chair reads exactly the pose it was already reading.
//-----------------------------------------------------------------------------
int HL2SB_GetRestingSequence( CBaseAnimating *pAnim )
{
	if ( !pAnim )
		return -1;

	const int iIdle = pAnim->LookupSequence( "idle" );
	if ( iIdle >= 0 )
		return iIdle;

	CStudioHdr *pStudioHdr = pAnim->GetModelPtr();
	if ( pStudioHdr && pStudioHdr->GetNumSeq() > 0 )
		return 0;

	return -1;
}

//-----------------------------------------------------------------------------
// HL2SB: an attachment's transform in the model's RESTING sequence pose (model space).
//
// The stomp/query/restore is the engine's own idiom for exactly this problem
// (CBaseServerVehicle::GetLocalAttachmentAtTime, vehicle_baseserver.cpp:1008): the
// sequence and cycle are saved, replaced with the resting pose, the attachment is read,
// and both are put back with the bone cache invalidated on each side - so the entity's
// own animation is bit-for-bit untouched and nothing in the frame sees the stomp.
//-----------------------------------------------------------------------------
bool HL2SB_GetRestingAttachmentLocal( CBaseAnimating *pAnim, const char *pszAttachment,
									  Vector *pVecOrigin, QAngle *pAngles )
{
	if ( !pAnim || !pszAttachment || !pszAttachment[0] )
		return false;

	const int iAttachment = pAnim->LookupAttachment( pszAttachment );	// 1-based, 0 == not found
	if ( iAttachment <= 0 )
		return false;

	const int iRestSequence = HL2SB_GetRestingSequence( pAnim );
	if ( iRestSequence < 0 )
		return false;

	const int iOldSequence = pAnim->GetSequence();
	const float flOldCycle = pAnim->GetCycle();

	pAnim->SetSequence( iRestSequence );
	pAnim->SetCycle( 0.0f );
	pAnim->InvalidateBoneCache();

	Vector vecOrigin;
	QAngle angAngles;
	const bool bFound = pAnim->GetAttachmentLocal( iAttachment, vecOrigin, angAngles );

	pAnim->SetSequence( iOldSequence );
	pAnim->SetCycle( flOldCycle );
	pAnim->InvalidateBoneCache();

	if ( !bFound )
		return false;

	if ( pVecOrigin )
	{
		*pVecOrigin = vecOrigin;
	}

	if ( pAngles )
	{
		*pAngles = angAngles;
	}

	return true;
}

//-----------------------------------------------------------------------------
// HL2SB: model space -> world space for a point/orientation on a vehicle.
//
// The same conversion CBaseServerVehicle::GetPassengerSeatPoint() applies to the seat
// point with UTIL_ParentToWorldSpace(), and the same one that function itself performs
// (game/server/util.cpp:2965). That function only exists in the server's util.cpp, so the
// shared animation code carries its own copy - the two MUST agree, because the client
// renders the body at the yaw this produces while the server seated the player with the
// other one.
//-----------------------------------------------------------------------------
static void HL2SB_VehicleModelToWorldSpace( CBaseEntity *pEntity, Vector &vecPosition, QAngle &vecAngles )
{
	if ( !pEntity )
		return;

	matrix3x4_t matModelToEntity;
	AngleMatrix( vecAngles, matModelToEntity );
	MatrixSetColumn( vecPosition, 3, matModelToEntity );

	matrix3x4_t matResult;
	ConcatTransforms( pEntity->EntityToWorldTransform(), matModelToEntity, matResult );

	MatrixGetColumn( matResult, 3, vecPosition );
	MatrixAngles( matResult, vecAngles );
}

//-----------------------------------------------------------------------------
// HL2SB: vehicle animation (GMod-style port, stages 1-2 + safe steering).
//
//  * The SITTING POSE itself is already produced server-side by
//    CHL2MP_Player::SetAnimation() (IsInAVehicle() -> ACT_HL2MP_SIT ->
//    sit_<holdtype> family, verified in the log as "sit_pistol"). This state machine
//    classifies what the player is doing inside the vehicle, so pose parameters
//    (body/head/steering) can be layered on top of that pose without touching the
//    ground/walk/run path at all.
//  * ENTER/EXIT are server-authoritative: IServerVehicle::IsPassengerEntering()/
//    IsPassengerExiting() plus the vehicle's own entry/exit animation. The client
//    cannot see those, so it classifies IDLE and lets the networked sequence drive
//    the visual.
//  * Role: driver when the server vehicle's GetDriver() is this player, otherwise
//    passenger (0 = driver, 1 = passenger; no assumption about seat enums).
//  * Steering: there is NO engine steering API for players, so it is taken from the
//    vehicle model's `vehicle_steer` pose parameter when present and handed to the
//    player model only when THAT model declares `vehicle_steer` (the m_anm 9-way
//    family does). Missing parameters are a no-op, never a crash.
//-----------------------------------------------------------------------------
// HL2SB: write one seated pose parameter in the MODEL's own units.
//
// The same parameter names mean different things in different model families (and
// the old code assumed the wrong one for both):
//   * GMod's anim models carry them in DEGREES - measured in models/f_anm.mdl
//     (what a cl_playermodel model includes for its animation):
//       aim_yaw -63.408..71.206   aim_pitch  -86.758..82.842
//       head_yaw -75..75          head_pitch -60..60
//   * a 0..1 blend parameter has a range of 0..1, where "centred" is the middle.
// So the range is read out of the model (CBaseAnimating::GetPoseParameterRange)
// and flDegrees is either clamped as degrees or mapped into the 0..1 range.
// Studio_SetPoseParameter() clamps again to the model's range on the way in.
//
// Returns false when this model does not declare the parameter, so callers can
// try their fallback spelling.
//-----------------------------------------------------------------------------
static bool HL2SB_SetSeatedPoseParameter( CBaseAnimating *pAnim, const char *pszName,
										  float flDegrees, float flLimit )
{
	if ( !pAnim || !pszName )
		return false;

	const int iParam = pAnim->LookupPoseParameter( pszName );
	if ( iParam < 0 )
		return false;

	flDegrees = clamp( flDegrees, -flLimit, flLimit );

	float flMin = 0.0f;
	float flMax = 1.0f;
	pAnim->GetPoseParameterRange( iParam, flMin, flMax );

	if ( flMax <= 1.5f && flMin >= -0.5f )
	{
		// 0..1 convention: the middle of the range is centred, +-flLimit is +-half.
		const float flCentre = ( flMin + flMax ) * 0.5f;
		const float flHalf = ( flMax - flMin ) * 0.5f;
		pAnim->SetPoseParameter( iParam, flCentre + ( flDegrees / flLimit ) * flHalf );
	}
	else
	{
		pAnim->SetPoseParameter( iParam, flDegrees );
	}

	return true;
}

void CPlayerAnimState::UpdateVehicleAnimation( bool bWriteSeatedPose )
{
    m_eHL2SBVehicleAnimState = VEHICLE_ANIM_NONE;
    m_iHL2SBVehicleRole = 0;
    m_flHL2SBVehicleSteering = 0.5f;
    CHL2MP_Player *pPlayer = GetOuter();
    CBaseEntity *pParent = pPlayer ? pPlayer->GetMoveParent() : NULL;
    if ( !pParent || !pPlayer->IsInAVehicle() )
        return;
#ifdef CLIENT_DLL
    IClientVehicle *pVehicle = pPlayer->GetVehicle();
#else
    IServerVehicle *pVehicle = pPlayer->GetVehicle();
#endif
    if ( !pVehicle )
        return;
    m_iHL2SBVehicleRole = pVehicle->GetPassengerRole( pPlayer ) == VEHICLE_ROLE_DRIVER ? 0 : 1;
    m_eHL2SBVehicleAnimState = m_iHL2SBVehicleRole == 0 ? VEHICLE_ANIM_DRIVER : VEHICLE_ANIM_PASSENGER;
#ifndef CLIENT_DLL
    if ( pVehicle->IsPassengerEntering() )
        m_eHL2SBVehicleAnimState = VEHICLE_ANIM_ENTER;
    else if ( pVehicle->IsPassengerExiting() )
        m_eHL2SBVehicleAnimState = VEHICLE_ANIM_EXIT;
#endif
    // Use the same live attachment frame that parents the rider.
    matrix3x4_t parentMatrix;
    const matrix3x4_t &parentToWorld = pPlayer->GetParentToWorldTransform( parentMatrix );
    MatrixAngles( parentToWorld, m_angRender );
    m_flHL2SBSeatYaw = m_angRender[YAW];
#ifndef CLIENT_DLL
    pPlayer->SetAbsAngles( m_angRender );
#endif
}

CPlayerAnimState::CPlayerAnimState( CHL2MP_Player *outer )
	: m_pOuter( outer )
{
	m_flGaitYaw = 0.0f;
	m_flGoalFeetYaw = 0.0f;
	m_flCurrentFeetYaw = 0.0f;
	m_flCurrentTorsoYaw = 0.0f;
	m_flLastYaw = 0.0f;
	m_flLastTurnTime = 0.0f;
	m_flTurnCorrectionTime = 0.0f;

	m_bHL2SBNoclipping = false;
	m_iHL2SBNoclipLayer = -1;

	m_eHL2SBVehicleAnimState = VEHICLE_ANIM_NONE;
	m_iHL2SBVehicleRole = 0;
	m_flHL2SBVehicleSteering = 0.0f;
	m_flHL2SBSeatYaw = 0.0f;
};

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
// Animation hooks run on both realms; Lua owns the activity and playback rate.

static bool HL2SB_PushPlayerAnimationHook( const char *pszName, CHL2MP_Player *pPlayer )
{
    if ( L == NULL )
        return false;
    lua_checkstack( L, 16 );
    lua_getglobal( L, "hook" );
    if ( !lua_istable( L, -1 ) )
    {
        lua_pop( L, 1 );
        return false;
    }
    lua_getfield( L, -1, "Run" );
    lua_remove( L, -2 );
    if ( !lua_isfunction( L, -1 ) )
    {
        lua_pop( L, 1 );
        return false;
    }
    lua_pushstring( L, pszName );
    lua_pushplayer( L, pPlayer );
    return true;
}

void CPlayerAnimState::RestartGesture( int iGestureSlot, Activity iGestureActivity, bool bAutoKill )
{
	if ( !m_pOuter )
		return;

#ifdef CLIENT_DLL
	m_pOuter->HL2SB_ClientRestartGesture( iGestureSlot, iGestureActivity, bAutoKill );
#else
	m_pOuter->HL2SB_AnimRestartGestureLocal( iGestureSlot, iGestureActivity, bAutoKill );
#endif
}

void CPlayerAnimState::Update()
{
    CHL2MP_Player *pPlayer = GetOuter();
    CStudioHdr *pHdr = pPlayer->GetModelPtr();
    if ( !pHdr || !pHdr->SequencesAvailable() || !pPlayer->IsAlive() )
        return;

    extern CBaseEntity *HL2SB_GetPlayerDrivingEntity( CBasePlayer * );
    if (HL2SB_GetPlayerDrivingEntity(pPlayer) != NULL)
    {
        const int stackTop = L ? lua_gettop(L) : 0;
        if (HL2SB_PushPlayerAnimationHook("PlayerDriveAnimate", pPlayer))
        {
            luasrc_pcall(L, 2, 0, 0);
            lua_settop(L, stackTop);
        }
        return;
    }

    Vector velocity;
    GetOuterAbsVelocity( velocity );
    Activity ideal = ACT_MP_STAND_IDLE;
    int sequenceOverride = -1;
    const int stackTop = L ? lua_gettop( L ) : 0;
    if ( HL2SB_PushPlayerAnimationHook( "CalcMainActivity", pPlayer ) )
    {
        lua_pushvector( L, velocity );
        if ( luasrc_pcall( L, 3, 2, 0 ) == 0 )
        {
            if ( lua_isnumber( L, -2 ) )
                ideal = (Activity)lua_tointeger( L, -2 );
            if ( lua_isnumber( L, -1 ) )
                sequenceOverride = lua_tointeger( L, -1 );
        }
        lua_settop( L, stackTop );
    }

    Activity translated = ideal;
    CBaseCombatWeapon *pWeapon = pPlayer->GetActiveWeapon();
    bool translatedByLua = false;
    if ( HL2SB_PushPlayerAnimationHook( "TranslateActivity", pPlayer ) )
    {
        lua_pushinteger( L, ideal );
        if ( luasrc_pcall( L, 3, 1, 0 ) == 0 && lua_isnumber( L, -1 ) )
        {
            translated = (Activity)lua_tointeger( L, -1 );
            translatedByLua = true;
        }
        lua_settop( L, stackTop );
    }
    if ( !translatedByLua && pWeapon )
        translated = pWeapon->ActivityOverride( ideal, NULL );

    int desiredSequence = sequenceOverride;
    const int currentSequence = pPlayer->GetSequence();
    if ( desiredSequence < 0 )
    {
        if ( currentSequence >= 0 && currentSequence < pHdr->GetNumSeq() &&
             pPlayer->GetSequenceActivity( currentSequence ) == translated )
            desiredSequence = currentSequence;
        else
            desiredSequence = pPlayer->SelectWeightedSequence( translated );
    }
    if ( desiredSequence < 0 || desiredSequence >= pHdr->GetNumSeq() )
        desiredSequence = 0;
    if ( desiredSequence != currentSequence )
    {
        pPlayer->ResetSequence( desiredSequence );
        pPlayer->SetCycle( 0.0f );
    }

    ComputePoseParam_BodyYaw();
    ComputePoseParam_BodyPitch( pHdr );
    ComputePoseParam_BodyLookYaw();
    UpdateVehicleAnimation( false );

    if ( HL2SB_PushPlayerAnimationHook( "UpdateAnimation", pPlayer ) )
    {
        lua_pushvector( L, velocity );
        lua_pushnumber( L, pPlayer->GetSequenceGroundSpeed( pPlayer->GetSequence() ) );
        luasrc_pcall( L, 4, 0, 0 );
        lua_settop( L, stackTop );
    }
#ifdef CLIENT_DLL
    if ( !pPlayer->IsInAVehicle() )
        pPlayer->UpdateLookAt();
#endif
    if ( hl2sb_anim_debug.GetBool() )
    {
        static float nextPrint[MAX_PLAYERS + 1] = {};
        int index = pPlayer->entindex();
        if ( index > 0 && index <= MAX_PLAYERS &&
             ( gpGlobals->curtime >= nextPrint[index] || nextPrint[index] > gpGlobals->curtime + 1.0f ) )
        {
            nextPrint[index] = gpGlobals->curtime + 1.0f;
            Msg( "[HL2SB player animation] side=%s entity=%d sequence=%d cycle=%.4f rate=%.3f speed=%.1f vehicle=%d parent=%d local=(%.3f %.3f %.3f) localang=(%.2f %.2f %.2f) render=(%.2f %.2f %.2f) eye=(%.2f %.2f %.2f) viewoff=(%.2f %.2f %.2f) aim=(%.4f %.4f) head=(%.4f %.4f) feet=(%.2f %.2f)\n",
#ifdef CLIENT_DLL
                 "client",
#else
                 "server",
#endif
                 index, pPlayer->GetSequence(), pPlayer->GetCycle(), pPlayer->GetPlaybackRate(), velocity.Length2D(),
                 pPlayer->IsInAVehicle() ? 1 : 0, pPlayer->GetMoveParent() ? pPlayer->GetMoveParent()->entindex() : -1,
                 pPlayer->GetLocalOrigin().x, pPlayer->GetLocalOrigin().y, pPlayer->GetLocalOrigin().z,
                 pPlayer->GetLocalAngles().x, pPlayer->GetLocalAngles().y, pPlayer->GetLocalAngles().z,
                 m_angRender.x, m_angRender.y, m_angRender.z,
                 pPlayer->GetAnimEyeAngles().x, pPlayer->GetAnimEyeAngles().y, pPlayer->GetAnimEyeAngles().z,
                 pPlayer->GetViewOffset().x, pPlayer->GetViewOffset().y, pPlayer->GetViewOffset().z,
                 pPlayer->GetPoseParameter( pPlayer->LookupPoseParameter( "aim_yaw" ) ), pPlayer->GetPoseParameter( pPlayer->LookupPoseParameter( "aim_pitch" ) ),
                 pPlayer->GetPoseParameter( pPlayer->LookupPoseParameter( "head_yaw" ) ), pPlayer->GetPoseParameter( pPlayer->LookupPoseParameter( "head_pitch" ) ),
                 m_flCurrentFeetYaw, m_flGoalFeetYaw );
        }
    }
}

#ifdef CLIENT_DLL
// ============================================================================
// HL2SB (2026-10-02): client-local gesture layers - GMod TE_PlayerAnimEvent
// architecture.
//
// overlay_vars is excluded from DT_HL2MP_Player (hl2mp_player.cpp), so the
// layers a remote player's model blends are NEVER decoded from the network:
// each client builds them itself when the "PlayerAnimEvent" temp entity
// arrives (C_TEPlayerAnimEvent in c_hl2mp_player.cpp ->
// HL2SB_ClientDoAnimationEvent below).  The previous architecture networked
// the layers and the client-side LATCH_ANIMATION_VAR interpolation zeroed
// them on remote entities (probe: correct first frame, dead 10ms later) -
// which is why nobody could see other players attack.
//
// Semantics follow the GMod/TF ported CMultiPlayerAnimState gesture-slot
// machinery (game/shared/Multiplayer/multiplayer_animstate.cpp): one layer
// per gesture slot, m_nOrder = slot, same-slot-same-activity restarts the
// cycle, autokill removes the layer when the cycle completes.  Slot state is
// keyed by entindex in file statics so the client class layout is untouched.
// ============================================================================
namespace HL2SBClientGesture
{
	struct SlotState_t
	{
		Activity	m_iActivity;
		int			m_iSequence;
		bool		m_bActive;
		bool		m_bAutoKill;
	};

	// GESTURE_SLOT_COUNT gesture slots + one dedicated noclip slot (its layer
	// is persistent and must not fight CUSTOM gestures for a slot).
	enum { NOCLIP_SLOT = GESTURE_SLOT_COUNT, SLOT_COUNT = GESTURE_SLOT_COUNT + 1 };

	static SlotState_t s_pSlots[MAX_PLAYERS + 1][SLOT_COUNT];

	static void ClearSlot( int iEnt, int iSlot )
	{
		if ( iEnt <= 0 || iEnt > MAX_PLAYERS || iSlot < 0 || iSlot >= SLOT_COUNT )
			return;
		s_pSlots[iEnt][iSlot].m_bActive = false;
		s_pSlots[iEnt][iSlot].m_iActivity = ACT_INVALID;
		s_pSlots[iEnt][iSlot].m_iSequence = 0;
		s_pSlots[iEnt][iSlot].m_bAutoKill = false;
	}
}

// Sizes the (client-only) overlay vector the first time a gesture is needed.
// The vector grows WITHOUT m_iv_AnimOverlay - that is fine: no overlay decode
// can arrive for players anymore, so CheckForLayerChanges' interpolation loop
// (bounded by m_iv_AnimOverlay.Count()) simply never runs for them.
static void HL2SB_ClientEnsureOverlaySlots( C_HL2MP_Player *pPlayer )
{
	if ( pPlayer->GetNumAnimOverlays() >= HL2SBClientGesture::SLOT_COUNT )
		return;

	int iOldSlots = pPlayer->GetNumAnimOverlays();
	pPlayer->SetNumAnimOverlays( HL2SBClientGesture::SLOT_COUNT );

	// SetNumAnimOverlays runs C_AnimationLayer's constructor, and that
	// constructor's Reset() zeroes every field EXCEPT m_nOrder - a grown slot
	// keeps whatever heap bytes were there (before the TE port the overlay
	// decode overwrote them; with overlay_vars excluded no decode ever arrives
	// for players).  A negative leftover passed AccumulateLayers' signed order
	// gate and indexed its stack bucket array out of bounds - the
	// layer[m_nOrder] read below RSP that crashed rendering with faulting
	// element i=5 (GESTURE_SLOT_VCD, never written by this system).
	for ( int k = iOldSlots; k < HL2SBClientGesture::SLOT_COUNT; k++ )
	{
		C_AnimationLayer *pLayer = pPlayer->GetAnimOverlay( k );
		pLayer->m_nOrder = C_BaseAnimatingOverlay::MAX_OVERLAYS;
		pLayer->m_nSequence = 0;
		pLayer->m_flWeight = 0.0f;
	}

	for ( int k = 0; k < C_BaseAnimatingOverlay::MAX_OVERLAYS; k++ )
		pPlayer->m_flOverlayPrevEventCycle[k] = -1.0f;
}

// Low-level layer write used by RestartGesture and the noclip layer.
// GMod's CLIENT AddToGestureSlot writes the layer fields directly (ported
// multiplayer_animstate.cpp CLIENT branch) - no AddGestureSequence needed.
void CHL2MP_Player::HL2SB_ClientSetLayerSequence( int iSlot, int iSequence, Activity activity, bool bAutoKill, float flPlaybackRate )
{
	int iEnt = entindex();
	if ( iEnt <= 0 || iEnt > MAX_PLAYERS )
		return;
	if ( iSlot < 0 || iSlot >= HL2SBClientGesture::SLOT_COUNT || iSequence <= 0 )
		return;

	HL2SB_ClientEnsureOverlaySlots( this );
	if ( iSlot >= GetNumAnimOverlays() )	// belt+braces after ensure
		return;

	C_AnimationLayer *pLayer = GetAnimOverlay( iSlot );
	pLayer->m_nSequence = iSequence;
	pLayer->m_nOrder = iSlot;
	pLayer->m_flWeight = 1.0f;
	pLayer->m_flPlaybackRate = flPlaybackRate;
	pLayer->m_flCycle = 0.0f;
	pLayer->m_flPrevCycle = 0.0f;
	pLayer->m_flLayerAnimtime = 0.0f;
	pLayer->m_flLayerFadeOuttime = 0.0f;
	pLayer->m_flBlendIn = 0.0f;
	pLayer->m_flBlendOut = 0.0f;
	pLayer->m_bClientBlend = false;
	m_flOverlayPrevEventCycle[iSlot] = -1.0f;

	HL2SBClientGesture::SlotState_t &s = HL2SBClientGesture::s_pSlots[iEnt][iSlot];
	s.m_bActive = true;
	s.m_iActivity = activity;
	s.m_iSequence = iSequence;
	s.m_bAutoKill = bAutoKill;

	if ( hl2sb_anim_debug.GetBool() )
	{
		Msg( "[HL2SB gesture/cl] LOCAL LAYER: ent=%d slot=%d seq=%d act=%s autokill=%d\n",
			 iEnt, iSlot, iSequence, ActivityList_NameForIndex( (int)activity ), bAutoKill ? 1 : 0 );
	}
}

// GMod CMultiPlayerAnimState::RestartGesture semantics: same slot + same
// activity = restart the cycle (replay), otherwise replace the layer.
void CHL2MP_Player::HL2SB_ClientRestartGesture( int iSlot, Activity activity, bool bAutoKill )
{
	int iEnt = entindex();
	if ( iEnt <= 0 || iEnt > MAX_PLAYERS )
		return;
	if ( iSlot < 0 || iSlot >= HL2SBClientGesture::SLOT_COUNT || activity <= ACT_INVALID )
		return;

	int iSequence = SelectWeightedSequence( activity );
	if ( iSequence <= 0 )
	{
		if ( hl2sb_anim_debug.GetBool() )
		{
			Msg( "[HL2SB gesture/cl] NO SEQUENCE: ent=%d slot=%d act=%s (model %s lacks it)\n",
				 iEnt, iSlot, ActivityList_NameForIndex( (int)activity ),
				 GetModelPtr() ? GetModelPtr()->pszName() : "?" );
		}
		return;
	}

	HL2SBClientGesture::SlotState_t &s = HL2SBClientGesture::s_pSlots[iEnt][iSlot];
	if ( s.m_bActive && s.m_iActivity == activity && s.m_iSequence == iSequence
		 && iSlot < GetNumAnimOverlays() && (int)GetAnimOverlay( iSlot )->m_nSequence == iSequence )
	{
		// replay: the gesture an active slot is ALREADY playing restarts
		// its cycle (GMod RestartGesture), so rapid fire re-animates.
		GetAnimOverlay( iSlot )->m_flCycle = 0.0f;
		GetAnimOverlay( iSlot )->m_flPrevCycle = 0.0f;
		m_flOverlayPrevEventCycle[iSlot] = -1.0f;
		return;
	}

	HL2SB_ClientSetLayerSequence( iSlot, iSequence, activity, bAutoKill, 1.0f );
}

void CHL2MP_Player::HL2SB_ClientResetGestureSlot( int iSlot )
{
	int iEnt = entindex();
	if ( iEnt <= 0 || iEnt > MAX_PLAYERS )
		return;
	if ( iSlot < 0 || iSlot >= HL2SBClientGesture::SLOT_COUNT )
		return;

	HL2SBClientGesture::ClearSlot( iEnt, iSlot );

	if ( iSlot < GetNumAnimOverlays() )
	{
		C_AnimationLayer *pLayer = GetAnimOverlay( iSlot );
		pLayer->m_flWeight = 0.0f;
		pLayer->m_nOrder = C_BaseAnimatingOverlay::MAX_OVERLAYS;
		pLayer->m_nSequence = 0;
		pLayer->m_flCycle = 0.0f;
		pLayer->m_flPrevCycle = 0.0f;
	}
}

// TE receiver entry: GMod's CHL2MPPlayerAnimState::DoAnimationEvent switch
// (reference  + the ported base-class cases), reduced to the
// events this fork actually emits.  CUSTOM_GESTURE - what the server funnel
// broadcasts - lands in GESTURE_SLOT_CUSTOM exactly like GMod's
// CMultiPlayerAnimState::DoAnimationEvent CUSTOM_GESTURE case.
void CHL2MP_Player::HL2SB_ClientDoAnimationEvent( int event, int nData )
{
	const int stackTop = L ? lua_gettop( L ) : 0;
	if ( HL2SB_PushPlayerAnimationHook( "DoAnimationEvent", this ) )
	{
		lua_pushinteger( L, event );
		lua_pushinteger( L, nData );
		const bool handled = luasrc_pcall( L, 4, 1, 0 ) == 0 && lua_isnumber( L, -1 );
		lua_settop( L, stackTop );
		if ( handled )
			return;
	}
	bool bDucking = ( GetFlags() & FL_DUCKING ) != 0;

	switch ( event )
	{
	case PLAYERANIMEVENT_ATTACK_PRIMARY:
		HL2SB_ClientRestartGesture( GESTURE_SLOT_ATTACK_AND_RELOAD,
			bDucking ? ACT_MP_ATTACK_CROUCH_PRIMARYFIRE : ACT_MP_ATTACK_STAND_PRIMARYFIRE, true );
		break;

	case PLAYERANIMEVENT_ATTACK_SECONDARY:
		HL2SB_ClientRestartGesture( GESTURE_SLOT_ATTACK_AND_RELOAD,
			bDucking ? ACT_MP_ATTACK_CROUCH_SECONDARYFIRE : ACT_MP_ATTACK_STAND_SECONDARYFIRE, true );
		break;

	case PLAYERANIMEVENT_ATTACK_GRENADE:
		HL2SB_ClientRestartGesture( GESTURE_SLOT_GRENADE, ACT_MP_ATTACK_STAND_GRENADE, true );
		break;

	case PLAYERANIMEVENT_RELOAD:
		HL2SB_ClientRestartGesture( GESTURE_SLOT_ATTACK_AND_RELOAD,
			bDucking ? ACT_MP_RELOAD_CROUCH : ACT_MP_RELOAD_STAND, true );
		break;

	case PLAYERANIMEVENT_RELOAD_LOOP:
		HL2SB_ClientRestartGesture( GESTURE_SLOT_ATTACK_AND_RELOAD,
			bDucking ? ACT_MP_RELOAD_CROUCH_LOOP : ACT_MP_RELOAD_STAND_LOOP, true );
		break;

	case PLAYERANIMEVENT_RELOAD_END:
		HL2SB_ClientRestartGesture( GESTURE_SLOT_ATTACK_AND_RELOAD,
			bDucking ? ACT_MP_RELOAD_CROUCH_END : ACT_MP_RELOAD_STAND_END, true );
		break;

	case PLAYERANIMEVENT_CUSTOM_GESTURE:
		// The funnel broadcasts (CUSTOM_GESTURE, activity); raw
		// DoCustomAnimEvent traffic lands here too.  GMod's ported base
		// class puts these in GESTURE_SLOT_CUSTOM.
		HL2SB_ClientRestartGesture( GESTURE_SLOT_CUSTOM, (Activity)nData, true );
		break;

	default:
		// Events without a Lua answer or a native gesture mapping are diagnostic only.
		if ( hl2sb_anim_debug.GetBool() )
		{
			Msg( "[HL2SB gesture/cl] unhandled event=%d data=%d (ent=%d)\n",
				 event, nData, entindex() );
		}
		break;
	}
}

// Called every frame from C_HL2MP_Player::UpdateClientSideAnimation, which
// runs for EVERY player (local and remote) through m_bClientSideAnimation.
// Mirrors the ported CMultiPlayerAnimState::UpdateGestureLayer CLIENT branch:
// advance the cycle, autokill-remove when it completes.
void CHL2MP_Player::HL2SB_ClientAdvanceGestures( void )
{
	int iEnt = entindex();
	if ( iEnt <= 0 || iEnt > MAX_PLAYERS )
		return;
	if ( GetNumAnimOverlays() <= 0 )
		return;

	float flInterval = gpGlobals->frametime;
	if ( flInterval <= 0.0f )
		return;

	CStudioHdr *pHdr = GetModelPtr();

	for ( int iSlot = 0; iSlot < HL2SBClientGesture::SLOT_COUNT; iSlot++ )
	{
		HL2SBClientGesture::SlotState_t &s = HL2SBClientGesture::s_pSlots[iEnt][iSlot];
		if ( !s.m_bActive )
			continue;
		if ( iSlot >= GetNumAnimOverlays() )
		{
			HL2SBClientGesture::ClearSlot( iEnt, iSlot );
			continue;
		}

		C_AnimationLayer *pLayer = GetAnimOverlay( iSlot );
		// Model changed (playermodel switch resets the layers in OnNewModel)
		// - drop the bookkeeping; self-healing.
		if ( (int)pLayer->m_nSequence <= 0 || (int)pLayer->m_nOrder != iSlot )
		{
			HL2SBClientGesture::ClearSlot( iEnt, iSlot );
			continue;
		}
		if ( !pHdr )
			continue;

		float flCycle = pLayer->m_flCycle
			+ GetSequenceCycleRate( pHdr, pLayer->m_nSequence ) * flInterval
			* pLayer->m_flPlaybackRate;

		pLayer->m_flPrevCycle = pLayer->m_flCycle;
		pLayer->m_flCycle = flCycle;

		if ( flCycle > 1.0f )
		{
			if ( s.m_bAutoKill )
			{
				// GMod: RunGestureSlotAnimEventsToCompletion + ResetGestureSlot.
				HL2SB_ClientResetGestureSlot( iSlot );
				continue;
			}
			pLayer->m_flCycle = 1.0f;
		}
	}
}
#endif	// CLIENT_DLL

//-----------------------------------------------------------------------------
// HL2SB: GMod's noclip pose.
//
// GMod does this in Lua - gamemodes/base/gamemode/animations.lua,
// GM:HandlePlayerNoClipping():
//     if ( !plyTable.m_bWasNoclipping ) then
//         ply:AnimRestartGesture( GESTURE_SLOT_CUSTOM, ACT_GMOD_NOCLIP_LAYER, false )
//     end
// On the engine side that is a layer in gesture slot 6 (GESTURE_SLOT_CUSTOM in
// game/shared/Multiplayer/multiplayer_animstate.h; the enum is not part of the
// HL2MP build, hence the literal below). Keeping it here makes it realm neutral:
// the server runs this state machine for every player and the layers travel over
// DT_BaseAnimatingOverlay, while the client runs it for the local player.
//
// The pose is held still (playback rate 0): gmod_breath_noclip_layer is an upper
// body layer and nothing else drives its cycle on the client. Models that do not
// declare the activity (male_anims/female_anims have no ACT_GMOD_*) resolve to -1
// and simply keep their normal upper body.
//-----------------------------------------------------------------------------
#define HL2SB_GESTURE_SLOT_NOCLIP 6

void CPlayerAnimState::UpdateNoclipLayer( void )
{
	CHL2MP_Player *pPlayer = GetOuter();
	if ( !pPlayer )
		return;

	const bool bNoclipping = ( pPlayer->GetMoveType() == MOVETYPE_NOCLIP );

	// Only touch the layer on the transition - re-setting the cycle every frame
	// would hold the pose on the first frame (that is what froze the main
	// sequence, see AGENTS.md 27).
	if ( bNoclipping == m_bHL2SBNoclipping )
		return;

	m_bHL2SBNoclipping = bNoclipping;

	// Resolve the sequence before doing anything, so the diagnostic below can report
	// it whether or not the model has it.
	int iSequence = -1;
	if ( bNoclipping )
	{
		iSequence = pPlayer->SelectWeightedSequence( ACT_GMOD_NOCLIP_LAYER );
		if ( iSequence <= 0 )
		{
			// The activity is declared by models/m_anm.mdl; fall back to the label
			// in case the model's activity list was cached before this activity.
			iSequence = pPlayer->LookupSequence( "gmod_breath_noclip_layer" );
		}
	}

	if ( bNoclipping && ( iSequence > 0 ) )
	{
#ifdef CLIENT_DLL
		// HL2SB (2026-10-02): overlay_vars is excluded from the send table
		// now (GMod architecture), so the networked decode that clobbered the
		// first client-side attempt - and crashed the 20260915_094606 dump -
		// can no longer arrive.  Build the persistent layer locally, held at
		// cycle 0 with playback rate 0 (upper-body hold pose).
		pPlayer->HL2SB_ClientSetLayerSequence( HL2SBClientGesture::NOCLIP_SLOT,
			iSequence, ACT_GMOD_NOCLIP_LAYER, false, 0.0f );
#else
		m_iHL2SBNoclipLayer = pPlayer->AddLayeredSequence( iSequence, 0 );
		pPlayer->SetLayerCycle( m_iHL2SBNoclipLayer, 0.0f, 0.0f );
		pPlayer->SetLayerPlaybackRate( m_iHL2SBNoclipLayer, 0.0f );
		pPlayer->SetLayerWeight( m_iHL2SBNoclipLayer, 1.0f );
		pPlayer->SetLayerAutokill( m_iHL2SBNoclipLayer, false );
		pPlayer->SetLayerLooping( m_iHL2SBNoclipLayer, false );
#endif
	}
	else if ( !bNoclipping )
	{
#ifdef CLIENT_DLL
		pPlayer->HL2SB_ClientResetGestureSlot( HL2SBClientGesture::NOCLIP_SLOT );
#else
		if ( m_iHL2SBNoclipLayer >= 0 )
			pPlayer->RemoveLayer( m_iHL2SBNoclipLayer, 0.0f, 0.0f );

		m_iHL2SBNoclipLayer = -1;
#endif
	}

	// HL2SB diagnostic: one line per transition, so "no visible pose" can be told
	// apart from "activity not resolved on this model" and from "new binary not
	// loaded" (see hl2sb_anim_debug).
	if ( hl2sb_anim_debug.GetBool() )
	{
#ifdef CLIENT_DLL
		const char *pszRealm = "cl";
#else
		const char *pszRealm = "sv";
#endif
		Msg( "[HL2SB noclip/%s] on=%d seq=%d layer=%d model=%s\n",
			 pszRealm, bNoclipping ? 1 : 0, iSequence, m_iHL2SBNoclipLayer,
			 pPlayer->GetModelPtr() ? pPlayer->GetModelPtr()->pszName() : "?" );
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CPlayerAnimState::ComputePlaybackRate()
{
	// Determine ideal playback rate
	Vector vel;
	GetOuterAbsVelocity( vel );

	float speed = vel.Length2D();

	bool isMoving = ( speed > 0.5f ) ? true : false;

	float maxspeed = GetOuter()->GetSequenceGroundSpeed( GetOuter()->GetSequence() );
	
	if ( isMoving && ( maxspeed > 0.0f ) )
	{
		float flFactor = 1.0f;

		// Note this gets set back to 1.0 if sequence changes due to ResetSequenceInfo below
		GetOuter()->SetPlaybackRate( ( speed * flFactor ) / maxspeed );

		// BUG BUG:
		// This stuff really should be m_flPlaybackRate = speed / m_flGroundSpeed
	}
	else
	{
		GetOuter()->SetPlaybackRate( 1.0f );
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
// Output : CBasePlayer
//-----------------------------------------------------------------------------
CHL2MP_Player *CPlayerAnimState::GetOuter()
{
	return m_pOuter;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : dt - 
//-----------------------------------------------------------------------------
void CPlayerAnimState::EstimateYaw( void )
{
	float dt = gpGlobals->frametime;

	if ( !dt )
	{
		return;
	}

	Vector est_velocity;
	QAngle	angles;

	GetOuterAbsVelocity( est_velocity );

	angles = GetOuter()->GetLocalAngles();

	if ( est_velocity[1] == 0 && est_velocity[0] == 0 )
	{
		float flYawDiff = angles[YAW] - m_flGaitYaw;
		flYawDiff = flYawDiff - (int)(flYawDiff / 360) * 360;
		if (flYawDiff > 180)
			flYawDiff -= 360;
		if (flYawDiff < -180)
			flYawDiff += 360;

		if (dt < 0.25)
			flYawDiff *= dt * 4;
		else
			flYawDiff *= dt;

		m_flGaitYaw += flYawDiff;
		m_flGaitYaw = m_flGaitYaw - (int)(m_flGaitYaw / 360) * 360;
	}
	else
	{
		m_flGaitYaw = (atan2(est_velocity[1], est_velocity[0]) * 180 / M_PI);

		if (m_flGaitYaw > 180)
			m_flGaitYaw = 180;
		else if (m_flGaitYaw < -180)
			m_flGaitYaw = -180;
	}
}

//-----------------------------------------------------------------------------
// Purpose: Override for backpeddling
// Input  : dt - 
//-----------------------------------------------------------------------------
void CPlayerAnimState::ComputePoseParam_BodyYaw( void )
{
    CHL2MP_Player *pPlayer = GetOuter();
    EstimateYaw();
    const float yaw = AngleNormalize( m_flGaitYaw - pPlayer->GetAnimEyeAngles().y );
    const int moveX = pPlayer->LookupPoseParameter( "move_x" );
    const int moveY = pPlayer->LookupPoseParameter( "move_y" );
    if ( moveX < 0 || moveY < 0 )
    {
        const int moveYaw = pPlayer->LookupPoseParameter( "move_yaw" );
        if ( moveYaw >= 0 )
            pPlayer->SetPoseParameter( moveYaw, yaw );
        return;
    }
    Vector velocity;
    GetOuterAbsVelocity( velocity );
    const float speed = velocity.Length2D();
    Vector2D direction( 0.0f, 0.0f );
    if ( speed > 0.5f )
    {
        direction.x = cos( DEG2RAD( yaw ) );
        direction.y = -sin( DEG2RAD( yaw ) );
        const float scale = MAX( fabs( direction.x ), fabs( direction.y ) );
        if ( scale > 0.0f )
        {
            direction.x /= scale;
            direction.y /= scale;
        }
        pPlayer->SetPoseParameter( moveX, direction.x );
        pPlayer->SetPoseParameter( moveY, direction.y );
        const float fullSpeed = pPlayer->GetSequenceGroundSpeed( pPlayer->GetSequence() );
        if ( fullSpeed > speed )
        {
            direction.x *= speed / fullSpeed;
            direction.y *= speed / fullSpeed;
        }
    }
    pPlayer->SetPoseParameter( moveX, direction.x );
    pPlayer->SetPoseParameter( moveY, direction.y );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CPlayerAnimState::ComputePoseParam_BodyPitch( CStudioHdr *pStudioHdr )
{
    const float pitch = AngleNormalize( GetOuter()->GetAnimEyeAngles().x );
    const int aimPitch = GetOuter()->LookupPoseParameter( pStudioHdr, "aim_pitch" );
    if ( aimPitch >= 0 )
        GetOuter()->SetPoseParameter( pStudioHdr, aimPitch, pitch );
    const int headPitch = GetOuter()->LookupPoseParameter( pStudioHdr, "head_pitch" );
    if ( headPitch >= 0 )
        GetOuter()->SetPoseParameter( pStudioHdr, headPitch, pitch );
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : goal - 
//			maxrate - 
//			dt - 
//			current - 
// Output : int
//-----------------------------------------------------------------------------
int CPlayerAnimState::ConvergeAngles( float goal,float maxrate, float dt, float& current )
{
	int direction = TURN_NONE;

	float anglediff = goal - current;
	float anglediffabs = fabs( anglediff );

	anglediff = AngleNormalize( anglediff );

	float scale = 1.0f;
	if ( anglediffabs <= FADE_TURN_DEGREES )
	{
		scale = anglediffabs / FADE_TURN_DEGREES;
		// Always do at least a bit of the turn ( 1% )
		scale = clamp( scale, 0.01f, 1.0f );
	}

	float maxmove = maxrate * dt * scale;

	if ( fabs( anglediff ) < maxmove )
	{
		current = goal;
	}
	else
	{
		if ( anglediff > 0 )
		{
			current += maxmove;
			direction = TURN_LEFT;
		}
		else
		{
			current -= maxmove;
			direction = TURN_RIGHT;
		}
	}

	current = AngleNormalize( current );

	return direction;
}

void CPlayerAnimState::ComputePoseParam_BodyLookYaw( void )
{
    const float eyeYaw = AngleNormalize( GetOuter()->GetAnimEyeAngles().y );
    Vector velocity;
    GetOuterAbsVelocity( velocity );
    if ( velocity.Length() > 1.0f )
        m_flGoalFeetYaw = eyeYaw;
    else if ( m_flLastTurnTime <= 0.0f )
    {
        m_flGoalFeetYaw = m_flCurrentFeetYaw = eyeYaw;
        m_flLastTurnTime = gpGlobals->curtime;
    }
    else
    {
        const float delta = AngleNormalize( m_flGoalFeetYaw - eyeYaw );
        if ( fabs( delta ) > 45.0f )
            m_flGoalFeetYaw += delta > 0.0f ? -45.0f : 45.0f;
    }
    m_flGoalFeetYaw = AngleNormalize( m_flGoalFeetYaw );
    const float delta = AngleNormalize( m_flGoalFeetYaw - m_flCurrentFeetYaw );
    const float turn = gpGlobals->frametime * 720.0f * clamp( fabs( delta ) / 60.0f, 0.01f, 1.0f );
    m_flCurrentFeetYaw = AngleNormalize( m_flCurrentFeetYaw + clamp( delta, -turn, turn ) );
    m_flCurrentTorsoYaw = AngleNormalize( eyeYaw - m_flCurrentFeetYaw );
    const int aimYaw = GetOuter()->LookupPoseParameter( "aim_yaw" );
    if ( aimYaw >= 0 )
        GetOuter()->SetPoseParameter( aimYaw, m_flCurrentTorsoYaw );
    m_angRender.Init( 0.0f, m_flCurrentFeetYaw, 0.0f );
}


 
//-----------------------------------------------------------------------------
// Purpose: 
// Input  : activity - 
// Output : Activity
//-----------------------------------------------------------------------------
Activity CPlayerAnimState::BodyYawTranslateActivity( Activity activity )
{
	// Not even standing still, sigh
	if ( activity != ACT_IDLE )
		return activity;

	// Not turning
	switch ( m_nTurningInPlace )
	{
	default:
	case TURN_NONE:
		return activity;
	/*
	case TURN_RIGHT:
		return ACT_TURNRIGHT45;
	case TURN_LEFT:
		return ACT_TURNLEFT45;
	*/
	case TURN_RIGHT:
	case TURN_LEFT:
		return mp_ik.GetBool() ? ACT_TURN : activity;
	}

	Assert( 0 );
	return activity;
}

const QAngle& CPlayerAnimState::GetRenderAngles()
{
	return m_angRender;
}


void CPlayerAnimState::GetOuterAbsVelocity( Vector& vel )
{
#if defined( CLIENT_DLL )
	GetOuter()->EstimateAbsVelocity( vel );
#else
	vel = GetOuter()->GetAbsVelocity();
#endif
}

void HL2SB_GetPlayerDuckDurations( CBasePlayer *pPlayer, float &flDuck, float &flUnDuck )
{
	CHL2MP_Player *pHL2MPPlayer = dynamic_cast<CHL2MP_Player *>( pPlayer );
	if ( pHL2MPPlayer )
	{
		flDuck = pHL2MPPlayer->m_flHL2SBDuckSpeed;
		flUnDuck = pHL2MPPlayer->m_flHL2SBUnDuckSpeed;
	}
	if ( flDuck <= 0.0f )
		flDuck = 0.01f;
	if ( flUnDuck <= 0.0f )
		flUnDuck = 0.01f;
}


// ---------------------------------------------------------------------------
// HL2SB: name-pinned player locomotion sequences (declared in the .h).
//
// Evidence for the naming scheme (dumped straight out of the .mdl string
// tables, 2026-09-15):
//
//   models/m_anm.mdl / f_anm.mdl / z_anm.mdl   (GMod, 9-way move_x/move_y)
//       idle_pistol  walk_ar2  run_smg1  jump_melee  cidle_pistol  cwalk_ar2
//       swim_idle_rpg  swimming_fist  sit_camera   + holdtype-less
//       idle_all_01/02  run_all_01/02  run_all_charging  run_all_panicked_01...
//       run_all_protected  walk_all  sit_all  + drive_jeep/drive_airboat
//
//   models/player/male_anims.mdl / female_anims.mdl  (HL2MP, 8-way move_yaw)
//       idle_pistol  run_ar2  jump_slam  cidle_pistol  cwalk_ar2
//       (+ "_mod" copies of run_*/cwalk_*) - no walk_*, no swim_*, no sit_*
//
// The activity suffix is NOT always the sequence suffix: the activities say
// PHYSGUN (HL2MP's enum word, which GMod kept) while the sequences are named
// gravgun (idle_gravgun, run_gravgun), and GMod's DUEL activity has both
// "dual" (locomotion) and "duel" (sit/swim) sequences. Both are handled below.
//
// Why not the activity path: SelectWeightedSequence() re-rolls its weighted
// random pick on every call (the "sticky" shortcut needs a negative actweight
// and studiomdl writes 0 - game/shared/animation.cpp:294), it resolves to
// nothing when the model lacks the hold type, and it depends on the weapon's
// activity table having a row for the state. Pinning by name is deterministic.
// ---------------------------------------------------------------------------

// The activity states that map onto a locomotion sequence, longest name first so
// that IDLE_CROUCH is matched before IDLE.  A NULL sequence prefix means "leave
// this one to the activity path" (gestures are single sequences and work).
static const struct
{
	const char *pszActivityState;
	const char *pszSequenceState;
} s_HL2SBPlayerAnimStates[] =
{
	{ "WALK_CROUCH",			"cwalk" },
	{ "IDLE_CROUCH",			"cidle" },
	{ "SWIM_IDLE",				"swim_idle" },
	{ "GESTURE_RANGE_ATTACK",	NULL },
	{ "GESTURE_RELOAD",			NULL },
	{ "IDLE",					"idle" },
	{ "WALK",					"walk" },
	{ "RUN",					"run" },
	{ "JUMP",					"jump" },
	{ "SWIM",					"swimming" },
	{ "SIT",					"sit" },
};

// "ACT_HL2MP_IDLE_PISTOL" -> state index of IDLE, family "pistol".
// Accepts the GMod player-layer names too (ACT_MP_*) so a weapon table that maps
// straight to those still works.
static bool HL2SB_SplitPlayerActivityName( Activity activity, int *pStateIndex, char *pFamily, int nFamilySize )
{
	*pStateIndex = -1;
	pFamily[ 0 ] = '\0';

	const char *pszName = ActivityList_NameForIndex( (int)activity );
	if ( !pszName || !pszName[ 0 ] )
		return false;

	const char *pszBody = NULL;
	if ( !Q_strnicmp( pszName, "ACT_HL2MP_", 10 ) )
		pszBody = pszName + 10;
	else if ( !Q_strnicmp( pszName, "ACT_", 4 ) )
		pszBody = pszName + 4;
	else
		return false;

	for ( int i = 0; i < ARRAYSIZE( s_HL2SBPlayerAnimStates ); ++i )
	{
		const char *pszState = s_HL2SBPlayerAnimStates[ i ].pszActivityState;
		int nLen = Q_strlen( pszState );

		if ( Q_strnicmp( pszBody, pszState, nLen ) )
			continue;

		// The state has to be the whole name or a '_' separated prefix: "IDLE"
		// must not match the "IDLE_SWIM" style leftovers of a longer word.
		if ( pszBody[ nLen ] != '\0' && pszBody[ nLen ] != '_' )
			continue;

		*pStateIndex = i;

		if ( pszBody[ nLen ] == '_' )
		{
			int j = 0;
			for ( const char *p = pszBody + nLen + 1; *p && j < nFamilySize - 1; ++p, ++j )
				pFamily[ j ] = (char)tolower( (unsigned char)*p );
			pFamily[ j ] = '\0';
		}
		return true;
	}

	return false;
}

int HL2SB_SelectPlayerSequence( CBaseAnimating *pAnim, Activity translatedActivity, Activity baseActivity )
{
	if ( !pAnim )
		return ACT_INVALID;

	CStudioHdr *pStudioHdr = pAnim->GetModelPtr();
	if ( !pStudioHdr || !pStudioHdr->SequencesAvailable() )
		return ACT_INVALID;

	char szFamily[ 64 ];
	int iState = -1;

	if ( !HL2SB_SplitPlayerActivityName( translatedActivity, &iState, szFamily, sizeof( szFamily ) ) ||
		 s_HL2SBPlayerAnimStates[ iState ].pszSequenceState == NULL )
	{
		if ( !HL2SB_SplitPlayerActivityName( baseActivity, &iState, szFamily, sizeof( szFamily ) ) ||
			 s_HL2SBPlayerAnimStates[ iState ].pszSequenceState == NULL )
		{
			return ACT_INVALID;
		}
	}

	const char *pszState = s_HL2SBPlayerAnimStates[ iState ].pszSequenceState;

	// HL2SB: the gravgun/physgun hold type is the one family whose sequence
	// suffix differs from its activity suffix - but it differs PER STATE, and the
	// model data (models/m_anm.mdl, verified 2026-09-27) is:
	//
	//     idle_physgun walk_physgun run_physgun cwalk_physgun cidle_physgun
	//     jump_physgun sit_physgun          <- "_physgun"
	//     swimming_gravgun swim_idle_gravgun <- "_gravgun"
	//
	// Converting the family once, unconditionally, made every non-swim state of
	// the physgun miss ("cwalk_gravgun" does not exist) and drop to the generic
	// cwalk_all family - the reported "some weapons lose their crouch animation"
	// (the physgun is this fork's default weapon).  Keep the activity spelling and
	// try BOTH for this one family.
	char szAltFamily[ 64 ];
	szAltFamily[ 0 ] = '\0';
	if ( szFamily[ 0 ] && !Q_stricmp( szFamily, "physgun" ) )
		Q_strncpy( szAltFamily, "gravgun", sizeof( szAltFamily ) );
	else if ( szFamily[ 0 ] && !Q_stricmp( szFamily, "gravgun" ) )
		Q_strncpy( szAltFamily, "physgun", sizeof( szAltFamily ) );

	char szCandidate[ 128 ];

	// 1) the weapon's own hold type (and, for physgun/gravgun, its other spelling).
	if ( szFamily[ 0 ] )
	{
		for ( int iSpelling = 0; iSpelling < 2; ++iSpelling )
		{
			const char *pszFamily = ( iSpelling == 0 ) ? szFamily : szAltFamily;
			if ( !pszFamily[ 0 ] )
				continue;

			Q_snprintf( szCandidate, sizeof( szCandidate ), "%s_%s", pszState, pszFamily );
			int iSequence = pAnim->LookupSequence( szCandidate );
			if ( iSequence >= 0 )
				return iSequence;

			// HL2MP ships a "_mod" copy of run_*/cwalk_* next to the plain one.
			Q_snprintf( szCandidate, sizeof( szCandidate ), "%s_%s_mod", pszState, pszFamily );
			iSequence = pAnim->LookupSequence( szCandidate );
			if ( iSequence >= 0 )
				return iSequence;
		}
	}

	// 2) The alternate run styles are named "run_all_charging",
	// "run_all_panicked_01", "run_all_protected" - the hold type is *after* the
	// "all", so ACT_HL2MP_RUN_CHARGING ("family" charging) lands here. This has to
	// come before the generic run_all_01 fallback below, or charging/panicked
	// would silently become a plain run.
	if ( szFamily[ 0 ] )
	{
		static const char *s_pRunStyleStates[] = { "run", "walk", "idle" };
		for ( int i = 0; i < ARRAYSIZE( s_pRunStyleStates ); ++i )
		{
			Q_snprintf( szCandidate, sizeof( szCandidate ), "%s_all_%s", s_pRunStyleStates[ i ], szFamily );
			int iSequence = pAnim->LookupSequence( szCandidate );
			if ( iSequence >= 0 )
				return iSequence;

			// ... and its numbered copies: run_all_panicked_01 / _02 / _03.
			for ( int j = 1; j <= 3; ++j )
			{
				Q_snprintf( szCandidate, sizeof( szCandidate ), "%s_all_%s_%02d", s_pRunStyleStates[ i ], szFamily, j );
				iSequence = pAnim->LookupSequence( szCandidate );
				if ( iSequence >= 0 )
					return iSequence;
			}
		}
	}

	// 3) Hold-type-less variants: the GMod models name them idle_all_01/02,
	// run_all_01/02 and plain walk_all / sit_all, cwalk_all, cidle_all.
	static const char *s_pAllSuffixes[] = { "_all_01", "_all_02", "_all", "" };
	for ( int i = 0; i < ARRAYSIZE( s_pAllSuffixes ); ++i )
	{
		Q_snprintf( szCandidate, sizeof( szCandidate ), "%s%s", pszState, s_pAllSuffixes[ i ] );
		int iSequence = pAnim->LookupSequence( szCandidate );
		if ( iSequence >= 0 )
			return iSequence;
	}

	// 4) Jump: no anim model ships a hold-type-less "jump", and GMod's own table
	// maps the bare ACT_MP_JUMP onto ACT_HL2MP_JUMP_SLAM, so use that.
	if ( !Q_stricmp( pszState, "jump" ) )
	{
		static const char *s_pJumpFallbacks[] = { "jump_slam", "jump_land" };
		for ( int i = 0; i < ARRAYSIZE( s_pJumpFallbacks ); ++i )
		{
			int iSequence = pAnim->LookupSequence( s_pJumpFallbacks[ i ] );
			if ( iSequence >= 0 )
				return iSequence;
		}
	}

	// 5) Last resort: the pistol pose, which every model family ships (the four
	// GMod-only jump poses above are the exception). GMod's own
	// weapon_base/sh_anim.lua falls back the same way
	// ("camera = camera, normal, pistol"). Without this a hold type the model
	// does not carry - GMod-only poses on an HL2MP anim model - would resolve to
	// nothing and the caller would drop to sequence 0.
	if ( szFamily[ 0 ] && Q_stricmp( szFamily, "pistol" ) )
	{
		Q_snprintf( szCandidate, sizeof( szCandidate ), "%s_pistol", pszState );
		int iSequence = pAnim->LookupSequence( szCandidate );
		if ( iSequence >= 0 )
			return iSequence;

		Q_snprintf( szCandidate, sizeof( szCandidate ), "%s_pistol_mod", pszState );
		iSequence = pAnim->LookupSequence( szCandidate );
		if ( iSequence >= 0 )
			return iSequence;
	}

	return ACT_INVALID;
}

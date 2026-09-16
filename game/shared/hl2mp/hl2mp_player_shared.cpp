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

	if ( pActivityOut )
		*pActivityOut = ACT_HL2MP_SIT;

	return HL2SB_SelectPlayerSequence( pPlayer, translatedActivity, ACT_HL2MP_SIT );
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
//    (body/head/steering) and the debug output can be layered on top of that pose
//    without touching the ground/walk/run path at all.
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
ConVar hl2sb_vehicle_anim_debug( "hl2sb_vehicle_anim_debug", "0", FCVAR_REPLICATED | FCVAR_NOTIFY,
								 "Print HL2MP vehicle animation state once per second." );

static const char *HL2SB_VehicleStateName( CPlayerAnimState::VehicleAnimState_t e )
{
	switch ( e )
	{
	case CPlayerAnimState::VEHICLE_ANIM_ENTER:		return "ENTER";
	case CPlayerAnimState::VEHICLE_ANIM_IDLE:		return "IDLE";
	case CPlayerAnimState::VEHICLE_ANIM_DRIVER:		return "DRIVER";
	case CPlayerAnimState::VEHICLE_ANIM_PASSENGER:	return "PASSENGER";
	case CPlayerAnimState::VEHICLE_ANIM_EXIT:		return "EXIT";
	default:										return "NONE";
	}
}

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

void CPlayerAnimState::UpdateVehicleAnimation( void )
{
	m_eHL2SBVehicleAnimState = VEHICLE_ANIM_NONE;
	m_iHL2SBVehicleRole = 0;
	m_flHL2SBVehicleSteering = 0.0f;

	CHL2MP_Player *pPlayer = GetOuter();
	if ( !pPlayer )
		return;

#ifdef CLIENT_DLL
	IClientVehicle *pVehicle = pPlayer->GetVehicle();
#else
	IServerVehicle *pVehicle = pPlayer->GetVehicle();
#endif
	if ( !pVehicle )
		return;

	// --- role (shared API, works on both realms) -----------------------------
	// IVehicle::GetPassengerRole() returns VEHICLE_ROLE_DRIVER for the driver seat,
	// which is what GMod uses to pick the driver vs passenger pose.
	const int iRole = pVehicle->GetPassengerRole( pPlayer );
	m_iHL2SBVehicleRole = ( iRole == VEHICLE_ROLE_DRIVER ) ? 0 : 1;

	// --- state ---------------------------------------------------------------
	// ENTER/EXIT are server-only (the client never sees the vehicle's entry state);
	// both realms can tell driver from passenger through GetPassengerRole().
#ifdef CLIENT_DLL
	m_eHL2SBVehicleAnimState = ( m_iHL2SBVehicleRole == 0 ) ? VEHICLE_ANIM_DRIVER
														    : VEHICLE_ANIM_PASSENGER;
#else
	if ( pVehicle->IsPassengerEntering() )
		m_eHL2SBVehicleAnimState = VEHICLE_ANIM_ENTER;
	else if ( pVehicle->IsPassengerExiting() )
		m_eHL2SBVehicleAnimState = VEHICLE_ANIM_EXIT;
	else if ( m_iHL2SBVehicleRole == 0 )
		m_eHL2SBVehicleAnimState = VEHICLE_ANIM_DRIVER;
	else
		m_eHL2SBVehicleAnimState = VEHICLE_ANIM_PASSENGER;
#endif

	// --- seat / vehicle entity -----------------------------------------------
	// HL2MP's CLIENT vehicle interface is player-backed: GetVehicleEnt() returns the
	// PLAYER there, not the car (log: client "vehicle=player" vs server
	// "vehicle=prop_vehicle_jeep"). Anything read off that entity - render bounds,
	// steering pose - is therefore the player's, not the vehicle's (the client used
	// to report the player model's centred 0.500 as "steering"). Only a real other
	// entity is usable; when there is none, fall back to what we are attached to and
	// otherwise leave the pose at its centred default.
	CBaseEntity *pVehEnt = pVehicle->GetVehicleEnt();
	if ( pVehEnt == pPlayer || ( pVehEnt && pVehEnt->IsPlayer() ) )
		pVehEnt = pPlayer->GetMoveParent();
	if ( pVehEnt && pVehEnt->IsPlayer() )
		pVehEnt = NULL;

	// --- still seated? --------------------------------------------------------
	// HL2SB: "in a vehicle" is NOT the same question as "sitting in it", and the gap
	// between the two is the reported "the model keeps the seated pose for a fraction of a
	// second after dismounting".
	//
	// CBasePlayer::GetInVehicle() parents the rider to the vehicle; leaving goes the other
	// way round: CBaseServerVehicle::HandlePassengerExit() plays the vehicle's exit
	// animation and UNPARENTS the player at its START (vehicle_baseserver.cpp SetParent(
	// NULL )), while CBasePlayer::LeaveVehicle() - the only place m_hVehicle is cleared -
	// runs when that animation FINISHES. For the whole 0.2-0.5s in between the player is
	// unparented but still IsInAVehicle(), so the sit sequence (server) and this body/head
	// layering (both realms) survived the dismount.
	//
	// Two signals, because the realms do not carry the same one:
	//   * the move parent      - authoritative on the server (GetInVehicle parents,
	//                            HandlePassengerExit unparents). GMod keys off exactly
	//                            this: gamemodes/base/gamemode/animations.lua:144
	//                            "The player must have a parent to be in a vehicle. If
	//                            there's no parent, we are in the exit anim, so don't do
	//                            sitting in 3rd person anymore". The client gets the same
	//                            field over the wire (c_baseentity.cpp:464 RecvPropInt(
	//                            RECVINFO_NAME( m_hNetworkMoveParent, moveparent ),
	//                            RecvProxy_IntToMoveParent )).
	//   * the seat pose itself - the sequence the SERVER pins (HL2SB_ResolveSeatedSequence)
	//                            is networked, so a realm that somehow missed the parent
	//                            change still sees the moment SetAnimation() stops pinning
	//                            the seat.
	Activity seatActivity = ACT_INVALID;
	const int iSeatSequence = HL2SB_ResolveSeatedSequence( pPlayer, pVehEnt, &seatActivity );
	const bool bPlayingSeatPose = ( iSeatSequence >= 0 && pPlayer->GetSequence() == iSeatSequence );

	if ( ( pPlayer->GetMoveParent() == NULL ) && !bPlayingSeatPose )
	{
		// Fold the state back to NONE: the seated body/head layering below is gated on it,
		// and its absence IS the reset - CPlayerAnimState::Update() already set m_angRender
		// from the player's own angles and ComputePoseParam_BodyPitch/BodyLookYaw own
		// aim_pitch/aim_yaw, so the normal gait takes the body back with no snap of its own
		// (the game is already interpolating the dismount).
		m_eHL2SBVehicleAnimState = VEHICLE_ANIM_NONE;
		m_iHL2SBVehicleRole = 0;
		m_flHL2SBSeatYaw = 0.0f;
	}

	m_flHL2SBVehicleSteering = 0.5f;	// 0..1 convention: centred == unknown

	// --- steering (safe, model-driven) --------------------------------------
	if ( pVehEnt )
	{
		CBaseAnimating *pVehAnim = pVehEnt->GetBaseAnimating();
		if ( pVehAnim )
		{
			const int iVehSteer = pVehAnim->LookupPoseParameter( "vehicle_steer" );
			if ( iVehSteer >= 0 )
				m_flHL2SBVehicleSteering = pVehAnim->GetPoseParameter( iVehSteer );
		}
	}

	// The player's own steering pose is owned by the SERVER (the client receives it
	// through m_flPoseParameter). Letting the client write it re-applied a stale
	// value and fought the server, so the write stays server-side.
#ifndef CLIENT_DLL
	const int iPlayerSteer = pPlayer->LookupPoseParameter( "vehicle_steer" );
	if ( iPlayerSteer >= 0 )
		pPlayer->SetPoseParameter( iPlayerSteer, m_flHL2SBVehicleSteering );
#endif

	// --- seated body / head layering -----------------------------------------
	// The body follows the SEAT, the head/upper body follows the view RELATIVE to it.
	//
	// What was wrong: m_angRender was set from the player's own local angles, and on
	// BOTH realms those follow the mouse:
	//   * client, local player: C_BasePlayer::PostDataUpdate() does
	//     SetLocalAngles( engine->GetViewAngles() ) (c_baseplayer.cpp:853-868)
	//   * server: ComputePoseParam_BodyYaw() writes the eye yaw in as well
	//     ("Adrian: Make the model's angle match the legs", hl2mp_player_shared.cpp:905)
	// so the whole seated model - legs and all - swept around with the view and through
	// the chair, reported as "when you turn the view the player model turns with it" and
	// "it sinks into the seat / the pose looks wrong".
	//
	// The seat datum is the vehicle's OWN `vehicle_feet_passenger0` attachment: it is
	// what CBaseServerVehicle::GetPassengerSeatPoint() seats the player by, so the body
	// and the seat cannot disagree. (Measured: chair world z 38.4 = the seat; the vehicle
	// model's attachment angles carry its yaw, so a chair that has been physgunned round
	// takes the body with it.) Nothing here is a model name.
	// HL2SB: the seat attachment's yaw in the vehicle's own space, for the debug line at the
	// bottom of this function (declared out here because that line sits outside the state
	// block below).
	float flSeatYawLocal = 0.0f;

	if ( m_eHL2SBVehicleAnimState != VEHICLE_ANIM_NONE )
	{
		float flSeatYaw = 0.0f;
		bool bHaveSeatYaw = false;

		CBaseAnimating *pSeatAnim = pVehEnt ? pVehEnt->GetBaseAnimating() : NULL;
		if ( pSeatAnim && pVehEnt )
		{
			// --- the seat datum --------------------------------------------------------
			// Two things have to be true at once, and each was a separate bug:
			//   * it must be read from the vehicle's RESTING pose
			//     (HL2SB_GetRestingAttachmentLocal: "idle" by label, else sequence 0), and
			//     never from the live pose - the seat attachments hang off the vehicle's
			//     animated driver-view bone, and reading that live is how the airboat got
			//     "seated outside the boat" on its enter animation;
			//   * the angles that helper returns are in the VEHICLE's model space, so they
			//     have to be converted into world space with the vehicle's own transform -
			//     used as a world yaw they pinned the rider to one fixed world direction,
			//     so the hull appeared to swing the body while driving ("airboat jeep 在移动
			//     时候 人物模型会转向").
			// The conversion is the same one the server seats the player with
			// (CBaseServerVehicle::GetPassengerSeatPoint -> UTIL_ParentToWorldSpace), so
			// the two realms cannot disagree about where the seat points.
			Vector vecSeatPos;
			QAngle angSeat;
			if ( HL2SB_GetRestingAttachmentLocal( pSeatAnim, "vehicle_feet_passenger0", &vecSeatPos, &angSeat ) )
			{
				flSeatYawLocal = angSeat[YAW];
				HL2SB_VehicleModelToWorldSpace( pVehEnt, vecSeatPos, angSeat );
				flSeatYaw = angSeat[YAW];
				bHaveSeatYaw = true;
			}
			else if ( pSeatAnim->GetAttachment( "vehicle_feet_passenger0", vecSeatPos, angSeat ) )
			{
				// Fallback for a model with no sequences at all: the pose it is in, which
				// GetAttachment() already reports in world space.
				flSeatYaw = angSeat[YAW];
				bHaveSeatYaw = true;
			}
		}

		if ( !bHaveSeatYaw && pVehEnt )
		{
			// No seat attachment at all (GetPassengerSeatPoint() falls back to the
			// vehicle origin in that case): the vehicle's own yaw IS the seat yaw.
			flSeatYaw = pVehEnt->GetAbsAngles()[YAW];
			bHaveSeatYaw = true;
		}

		if ( !bHaveSeatYaw )
		{
			// No vehicle entity to read either: keep the previous behaviour rather
			// than inventing an orientation.
			flSeatYaw = pPlayer->GetLocalAngles()[YAW];
		}

		m_flHL2SBSeatYaw = flSeatYaw;
		m_angRender = QAngle( 0.0f, flSeatYaw, 0.0f );

#ifndef CLIENT_DLL
		// The server owns the entity ANGLES: they are what the client receives for
		// every remote player and what the server-side hitboxes are built from
		// (CBaseAnimating::SetupBones() uses GetRenderAngles()), so they have to agree
		// with the body that is drawn or shots at a seated player land on a body that
		// is facing somewhere else. Only the YAW is changed - the pitch is left exactly
		// as ComputePoseParam_BodyYaw() wrote it (the anim models layer it through
		// aim_pitch). SetAbsAngles() converts to parent-relative space.
		QAngle angSeated = pPlayer->GetAbsAngles();
		angSeated[YAW] = flSeatYaw;
		angSeated[ROLL] = 0.0f;
		pPlayer->SetAbsAngles( angSeated );
#endif

		// --- the head / upper body, relative to the seat ----------------------
		// GMod does exactly this for a seat (gamemodes/base/gamemode/animations.lua:236):
		//     ply:SetPoseParameter( "aim_yaw", math.NormalizeAngle(
		//         ply:GetAimVector():Angle().y - Vehicle:GetAngles().y - 90 ) )
		//
		// The two anim model families carry these parameters in DIFFERENT units, and the
		// old code got it wrong for both: it wrote 0.5 (its "0..1, centred" convention)
		// into head_yaw. models/f_anm.mdl (GMod, and what cl_playermodel models include)
		// declares head_yaw -75..75, head_pitch -60..60, aim_yaw -63.4..71.2,
		// aim_pitch -86.8..82.8 - all DEGREES - so 0.5 put the head at half a degree,
		// i.e. the head never turned at all. Read the range out of the model and write
		// the value in the model's own units instead (HL2SB_SetSeatedPoseParameter()).
		//
		// One spelling per axis, never both: aim_* is the whole upper body (which the
		// head bone follows), head_* is the head bone itself - writing both would turn
		// the head twice as far.
		const QAngle angView = pPlayer->EyeAngles();
		const float flViewYawDelta = AngleNormalize( angView[YAW] - flSeatYaw );

		if ( !HL2SB_SetSeatedPoseParameter( pPlayer, "aim_yaw", flViewYawDelta, 60.0f ) )
		{
			// Models without an aim chain (Valve's HL2MP anim models have neither
			// aim_yaw nor the sit_* poses) keep a head-only turn.
			HL2SB_SetSeatedPoseParameter( pPlayer, "head_yaw", flViewYawDelta, 60.0f );
		}

		if ( !HL2SB_SetSeatedPoseParameter( pPlayer, "aim_pitch", angView[PITCH], 60.0f ) )
		{
			HL2SB_SetSeatedPoseParameter( pPlayer, "head_pitch", angView[PITCH], 30.0f );
		}

		// body_yaw is a whole-body blender in some NPC models and absent from the anim
		// models; "centred" is the middle of whatever range this model declares.
		const int iBodyYaw = pPlayer->LookupPoseParameter( "body_yaw" );
		if ( iBodyYaw >= 0 )
		{
			float flMin = 0.0f;
			float flMax = 1.0f;
			pPlayer->GetPoseParameterRange( iBodyYaw, flMin, flMax );
			pPlayer->SetPoseParameter( iBodyYaw, ( flMin + flMax ) * 0.5f );
		}
	}

	// --- debug (stage 11) ----------------------------------------------------
	if ( hl2sb_vehicle_anim_debug.GetBool() )
	{
		static float s_flNextVehAnimPrint = 0.0f;
		if ( gpGlobals->curtime >= s_flNextVehAnimPrint )
		{
			s_flNextVehAnimPrint = gpGlobals->curtime + 1.0f;

			CStudioHdr *pStudioHdr = pPlayer->GetModelPtr();
			const int iSequence = pPlayer->GetSequence();
			const char *pszLabel = "?";
			const char *pszActivity = "?";
			if ( pStudioHdr && iSequence >= 0 && iSequence < pStudioHdr->GetNumSeq() )
			{
				pszLabel = pStudioHdr->pSeqdesc( iSequence ).pszLabel();
				pszActivity = ActivityList_NameForIndex( (int)pPlayer->GetSequenceActivity( iSequence ) );
				if ( !pszActivity )
					pszActivity = "?";
			}

			const int iBodyYaw = pPlayer->LookupPoseParameter( "body_yaw" );
			const int iAimYaw = pPlayer->LookupPoseParameter( "aim_yaw" );
			const int iHeadYaw = pPlayer->LookupPoseParameter( "head_yaw" );
			const int iHeadPitch = pPlayer->LookupPoseParameter( "head_pitch" );

#ifdef CLIENT_DLL
			const char *pszRealm = "cl";
#else
			const char *pszRealm = "sv";
#endif
			// HL2SB: C_BaseEntity::GetClassname() returns ONE shared static buffer on the
			// client (game/client/c_baseentity.cpp:4813), so two calls inside a single
			// Msg() print the SAME name whichever way the compiler orders the arguments.
			// That is where the client's misleading "vehicle=player" (vs the server's
			// "vehicle=prop_vehicle_prisoner_pod") came from - a logging artifact, not a
			// player-backed vehicle. Copy both names out first.
			char szPlayerClass[128];
			char szVehicleClass[128];
			Q_strncpy( szPlayerClass, pPlayer->GetClassname(), sizeof( szPlayerClass ) );
			Q_strncpy( szVehicleClass, pVehEnt ? pVehEnt->GetClassname() : "<none>", sizeof( szVehicleClass ) );

			// HL2SB: the seated layering, for "does the body follow the seat and only
			// the head follow the view?". seat_yaw is the vehicle's own
			// vehicle_feet_passenger0 yaw (m_angRender); view_yaw is the player's eye
			// yaw; aim_yaw/head_yaw are read back out of the model (DEGREES in the anim
			// models) and should equal wrap(view_yaw - seat_yaw), clamped.
			//
			// view_ofs is the seated view offset GetInVehicle() now takes from the
			// vehicle's feet->eyes attachment pair, so
			//     seat line world_z + view_ofs.z == veh3rd/calc eye z
			// is the check that the player's own eye and the vehicle camera agree.
			//
			// parent=Y/N and seat_pose=Y/N are the two signals that decide whether this
			// body is seated or merely still owns a (dying) vehicle handle: parent=N with
			// state=NONE right after a dismount is the fix for "the seat pose lingered for
			// a fraction of a second" working. seat_pose=Y means the player is still
			// playing THIS vehicle's seat sequence (networked), which keeps the layering on
			// for the frame or two it takes a parent change to arrive.
			//
			// seat_ofs is the seat yaw in the vehicle's own (model) space and seat_yaw is
			// the world yaw derived from it. seat_ofs must stay constant for the whole ride
			// (it is read from the vehicle's resting pose) while seat_yaw follows the
			// vehicle - if seat_ofs moves while driving, the seat datum is being read from
			// the live animation again.
			const Vector vecViewOfs = pPlayer->GetViewOffset();
			Msg( "[HL2SB veh/%s] player=%s vehicle=%s state=%s role=%d parent=%d seat_pose=%d seq=%d(%s) act=%s steer=%.3f body_yaw=%.1f aim_yaw=%.1f head_yaw=%.1f head_pitch=%.1f seat_ofs=%.1f seat_yaw=%.1f view_yaw=%.1f view_ofs=(%.1f %.1f %.1f)\n",
				 pszRealm, szPlayerClass, szVehicleClass,
				 HL2SB_VehicleStateName( m_eHL2SBVehicleAnimState ), m_iHL2SBVehicleRole,
				 ( pPlayer->GetMoveParent() != NULL ) ? 1 : 0, bPlayingSeatPose ? 1 : 0,
				 iSequence, pszLabel, pszActivity, m_flHL2SBVehicleSteering,
				 ( iBodyYaw >= 0 ) ? pPlayer->GetPoseParameter( iBodyYaw ) : 0.0f,
				 ( iAimYaw >= 0 ) ? pPlayer->GetPoseParameter( iAimYaw ) : 0.0f,
				 ( iHeadYaw >= 0 ) ? pPlayer->GetPoseParameter( iHeadYaw ) : 0.0f,
				 ( iHeadPitch >= 0 ) ? pPlayer->GetPoseParameter( iHeadPitch ) : 0.0f,
				 flSeatYawLocal, m_flHL2SBSeatYaw, pPlayer->EyeAngles()[YAW],
				 vecViewOfs.x, vecViewOfs.y, vecViewOfs.z );
		}
	}
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
void CPlayerAnimState::Update()
{
	m_angRender = GetOuter()->GetLocalAngles();
	m_angRender[ PITCH ] = m_angRender[ ROLL ] = 0.0f;

	ComputePoseParam_BodyYaw();
	ComputePoseParam_BodyPitch(GetOuter()->GetModelPtr());
	ComputePoseParam_BodyLookYaw();

	ComputePlaybackRate();

	UpdateNoclipLayer();

	// HL2SB: GMod-style vehicle animation state. Runs every frame but is a no-op
	// (VEHICLE_ANIM_NONE, no pose parameter writes beyond a missing-parameter no-op)
	// whenever the player is not in a vehicle, so ground/air/crouch/swim are untouched.
	UpdateVehicleAnimation();

#ifdef CLIENT_DLL
	GetOuter()->UpdateLookAt();
#endif

	// HL2SB diagnostic: does the cycle advance on this realm, and with which
	// sequence/rate/animtime? (See hl2sb_anim_debug above.)
	if ( hl2sb_anim_debug.GetBool() )
	{
		static float s_flNextAnimPrint = 0.0f;
		if ( gpGlobals->curtime >= s_flNextAnimPrint )
		{
			s_flNextAnimPrint = gpGlobals->curtime + 1.0f;

			Vector vel;
			GetOuterAbsVelocity( vel );

			int iSequence = GetOuter()->GetSequence();

			const char *pszLabel = "?";
			const char *pszActivity = "?";
			CStudioHdr *pStudioHdr = GetOuter()->GetModelPtr();
			if ( pStudioHdr && iSequence >= 0 && iSequence < pStudioHdr->GetNumSeq() )
			{
				pszLabel = pStudioHdr->pSeqdesc( iSequence ).pszLabel();
				pszActivity = ActivityList_NameForIndex( (int)GetOuter()->GetSequenceActivity( iSequence ) );
				if ( !pszActivity )
					pszActivity = "?";
			}

#ifdef CLIENT_DLL
			const char *pszRealm = "cl";
#else
			const char *pszRealm = "sv";
#endif

			Msg( "[HL2SB anim/%s] model=%s seq=%d(%s) act=%s cycle=%.3f rate=%.2f animtime=%.3f curtime=%.3f speed=%.1f maxspeed=%.1f\n",
				 pszRealm, pStudioHdr ? pStudioHdr->pszName() : "?", iSequence, pszLabel, pszActivity,
				 GetOuter()->GetCycle(), GetOuter()->GetPlaybackRate(), GetOuter()->GetAnimTime(),
				 gpGlobals->curtime, vel.Length2D(), GetOuter()->GetSequenceGroundSpeed( iSequence ) );

			// HL2SB diagnostic (AGENTS.md 28): the third-person body does not follow
			// the view. Print every yaw that could own the visible direction, on both
			// realms, so whichever one is not tracking the view can be identified.
			Msg( "[HL2SB yaw/%s] local=%.1f eye=%.1f render=%.1f feet=%.1f goal=%.1f torso=%.1f gait=%.1f\n",
				 pszRealm, GetOuter()->GetLocalAngles()[YAW], GetOuter()->EyeAngles()[YAW],
				 m_angRender[YAW], m_flCurrentFeetYaw, m_flGoalFeetYaw, m_flCurrentTorsoYaw, m_flGaitYaw );
		}
	}
}

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
		// HL2SB: nothing to do on the client. The server branch below puts the pose
		// into the player's overlay layer and DT_BaseAnimatingOverlay networks it -
		// writing a local layer instead (the first attempt, which also called
		// SetNumAnimOverlays) got clobbered by the next networked decode and was the
		// crash of 20260915_094606.
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
#ifndef CLIENT_DLL
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
	// HL2SB: which leg blender does this model have?
	//
	// HL2MP's own animation models (models/player/male_anims.mdl, ...) use the
	// 8-way "move_yaw" angle. GMod's animation models (models/m_anm.mdl,
	// f_anm.mdl, z_anm.mdl - and every playermodel that $includemodels them) have
	// NO move_yaw at all: they use TF2's 9-way move_x / move_y pair. The old code
	// looked up move_yaw, got -1 and returned on its first line, so on a GMod
	// playermodel the lower body never got any direction information.
	int iYaw = GetOuter()->LookupPoseParameter( "move_yaw" );

	int iMoveX = -1;
	int iMoveY = -1;
	if ( iYaw < 0 )
	{
		iMoveX = GetOuter()->LookupPoseParameter( "move_x" );
		iMoveY = GetOuter()->LookupPoseParameter( "move_y" );
		if ( iMoveX < 0 || iMoveY < 0 )
			return;
	}

	// view direction relative to movement
	float flYaw;	 

	EstimateYaw();

	QAngle	angles = GetOuter()->GetLocalAngles();
	float ang = angles[ YAW ];
	if ( ang > 180.0f )
	{
		ang -= 360.0f;
	}
	else if ( ang < -180.0f )
	{
		ang += 360.0f;
	}

	// calc side to side turning
	flYaw = ang - m_flGaitYaw;
	// Invert for mapping into 8way blend
	flYaw = -flYaw;
	flYaw = flYaw - (int)(flYaw / 360) * 360;

	if (flYaw < -180)
	{
		flYaw = flYaw + 360;
	}
	else if (flYaw > 180)
	{
		flYaw = flYaw - 360;
	}
	
	if ( iYaw >= 0 )
	{
		GetOuter()->SetPoseParameter( iYaw, flYaw );
	}
	else
	{
		// 9-way blend, copied from CMultiPlayerAnimState::ComputePoseParam_MoveYaw
		// (game/shared/Multiplayer/multiplayer_animstate.cpp:1539, the LEGANIM_9WAY
		// branch). flYaw above is the same view-vs-movement yaw the 8-way path
		// writes into move_yaw, so both paths share EstimateYaw()'s maths.
		Vector vel;
		GetOuterAbsVelocity( vel );
		float flSpeed = vel.Length2D();

		Vector2D vecMove( 0.0f, 0.0f );
		if ( flSpeed > 0.5f )
		{
			// convert the yaw back into a vector ...
			vecMove.x = cos( DEG2RAD( flYaw ) );
			vecMove.y = -sin( DEG2RAD( flYaw ) );

			// ... push the edges out to the -1..1 box ...
			float flInvScale = MAX( fabs( vecMove.x ), fabs( vecMove.y ) );
			if ( flInvScale != 0.0f )
			{
				vecMove.x /= flInvScale;
				vecMove.y /= flInvScale;
			}

			// ... and scale by how fast we actually move versus the speed the
			// sequence was authored for (otherwise slow movement plays a full
			// stride at full amplitude).
			float flMaxSpeed = GetOuter()->GetSequenceGroundSpeed( GetOuter()->GetSequence() );
			if ( flMaxSpeed > flSpeed )
			{
				vecMove.x *= flSpeed / flMaxSpeed;
				vecMove.y *= flSpeed / flMaxSpeed;
			}
		}

		GetOuter()->SetPoseParameter( iMoveX, vecMove.x );
		GetOuter()->SetPoseParameter( iMoveY, vecMove.y );
	}

#ifndef CLIENT_DLL
		//Adrian: Make the model's angle match the legs so the hitboxes match on both sides.
		GetOuter()->SetLocalAngles( QAngle( GetOuter()->GetAnimEyeAngles().x, m_flCurrentFeetYaw, 0 ) );
#endif
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CPlayerAnimState::ComputePoseParam_BodyPitch( CStudioHdr *pStudioHdr )
{
	// Get pitch from v_angle
	float flPitch = GetOuter()->GetLocalAngles()[ PITCH ];

	if ( flPitch > 180.0f )
	{
		flPitch -= 360.0f;
	}
	flPitch = clamp( flPitch, -90, 90 );

	QAngle absangles = GetOuter()->GetAbsAngles();
	absangles.x = 0.0f;
	m_angRender = absangles;
	m_angRender[ PITCH ] = m_angRender[ ROLL ] = 0.0f;

	// See if we have a blender for pitch
	GetOuter()->SetPoseParameter( pStudioHdr, "aim_pitch", flPitch );
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
	QAngle absangles = GetOuter()->GetAbsAngles();
	absangles.y = AngleNormalize( absangles.y );
	m_angRender = absangles;
	m_angRender[ PITCH ] = m_angRender[ ROLL ] = 0.0f;

	// See if we even have a blender for pitch
	int upper_body_yaw = GetOuter()->LookupPoseParameter( "aim_yaw" );
	if ( upper_body_yaw < 0 )
	{
		return;
	}

	// Assume upper and lower bodies are aligned and that we're not turning
	float flGoalTorsoYaw = 0.0f;
	int turning = TURN_NONE;
	float turnrate = 360.0f;

	Vector vel;
	
	GetOuterAbsVelocity( vel );

	bool isMoving = ( vel.Length() > 1.0f ) ? true : false;

	if ( !isMoving )
	{
		// Just stopped moving, try and clamp feet
		if ( m_flLastTurnTime <= 0.0f )
		{
			m_flLastTurnTime	= gpGlobals->curtime;
			m_flLastYaw			= GetOuter()->GetAnimEyeAngles().y;
			// Snap feet to be perfectly aligned with torso/eyes
			m_flGoalFeetYaw		= GetOuter()->GetAnimEyeAngles().y;
			m_flCurrentFeetYaw	= m_flGoalFeetYaw;
			m_nTurningInPlace	= TURN_NONE;
		}

		// If rotating in place, update stasis timer
		if ( m_flLastYaw != GetOuter()->GetAnimEyeAngles().y )
		{
			m_flLastTurnTime	= gpGlobals->curtime;
			m_flLastYaw			= GetOuter()->GetAnimEyeAngles().y;
		}

		if ( m_flGoalFeetYaw != m_flCurrentFeetYaw )
		{
			m_flLastTurnTime	= gpGlobals->curtime;
		}

		turning = ConvergeAngles( m_flGoalFeetYaw, turnrate, gpGlobals->frametime, m_flCurrentFeetYaw );

		QAngle eyeAngles = GetOuter()->GetAnimEyeAngles();
		QAngle vAngle = GetOuter()->GetLocalAngles();

		// See how far off current feetyaw is from true yaw
		float yawdelta = GetOuter()->GetAnimEyeAngles().y - m_flCurrentFeetYaw;
		yawdelta = AngleNormalize( yawdelta );

		bool rotated_too_far = false;

		float yawmagnitude = fabs( yawdelta );

		// If too far, then need to turn in place
		if ( yawmagnitude > 45 )
		{
			rotated_too_far = true;
		}

		// Standing still for a while, rotate feet around to face forward
		// Or rotated too far
		// FIXME:  Play an in place turning animation
		if ( rotated_too_far || 
			( gpGlobals->curtime > m_flLastTurnTime + mp_facefronttime.GetFloat() ) )
		{
			m_flGoalFeetYaw		= GetOuter()->GetAnimEyeAngles().y;
			m_flLastTurnTime	= gpGlobals->curtime;

		/*	float yd = m_flCurrentFeetYaw - m_flGoalFeetYaw;
			if ( yd > 0 )
			{
				m_nTurningInPlace = TURN_RIGHT;
			}
			else if ( yd < 0 )
			{
				m_nTurningInPlace = TURN_LEFT;
			}
			else
			{
				m_nTurningInPlace = TURN_NONE;
			}

			turning = ConvergeAngles( m_flGoalFeetYaw, turnrate, gpGlobals->frametime, m_flCurrentFeetYaw );
			yawdelta = GetOuter()->GetAnimEyeAngles().y - m_flCurrentFeetYaw;*/

		}

		// Snap upper body into position since the delta is already smoothed for the feet
		flGoalTorsoYaw = yawdelta;
		m_flCurrentTorsoYaw = flGoalTorsoYaw;
	}
	else
	{
		m_flLastTurnTime = 0.0f;
		m_nTurningInPlace = TURN_NONE;
		m_flCurrentFeetYaw = m_flGoalFeetYaw = GetOuter()->GetAnimEyeAngles().y;
		flGoalTorsoYaw = 0.0f;
		m_flCurrentTorsoYaw = GetOuter()->GetAnimEyeAngles().y - m_flCurrentFeetYaw;
	}


	if ( turning == TURN_NONE )
	{
		m_nTurningInPlace = turning;
	}

	if ( m_nTurningInPlace != TURN_NONE )
	{
		// If we're close to finishing the turn, then turn off the turning animation
		if ( fabs( m_flCurrentFeetYaw - m_flGoalFeetYaw ) < MIN_TURN_ANGLE_REQUIRING_TURN_ANIMATION )
		{
			m_nTurningInPlace = TURN_NONE;
		}
	}

	// Rotate entire body into position
	absangles = GetOuter()->GetAbsAngles();
	absangles.y = m_flCurrentFeetYaw;
	m_angRender = absangles;
	m_angRender[ PITCH ] = m_angRender[ ROLL ] = 0.0f;

	GetOuter()->SetPoseParameter( upper_body_yaw, clamp( m_flCurrentTorsoYaw, -60.0f, 60.0f ) );

	/*
	// FIXME: Adrian, what is this?
	int body_yaw = GetOuter()->LookupPoseParameter( "body_yaw" );

	if ( body_yaw >= 0 )
	{
		GetOuter()->SetPoseParameter( body_yaw, 30 );
	}
	*/

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

	// The one hold type whose sequence suffix differs from its activity suffix.
	if ( szFamily[ 0 ] && !Q_stricmp( szFamily, "physgun" ) )
		Q_strncpy( szFamily, "gravgun", sizeof( szFamily ) );

	char szCandidate[ 128 ];

	// 1) the weapon's own hold type.
	if ( szFamily[ 0 ] )
	{
		Q_snprintf( szCandidate, sizeof( szCandidate ), "%s_%s", pszState, szFamily );
		int iSequence = pAnim->LookupSequence( szCandidate );
		if ( iSequence >= 0 )
			return iSequence;

		// HL2MP ships a "_mod" copy of run_*/cwalk_* next to the plain one.
		Q_snprintf( szCandidate, sizeof( szCandidate ), "%s_%s_mod", pszState, szFamily );
		iSequence = pAnim->LookupSequence( szCandidate );
		if ( iSequence >= 0 )
			return iSequence;
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
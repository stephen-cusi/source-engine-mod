//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: GMod-style player animation state for HL2MP/HL2SB.
//
//          Phase 1a is a behaviour-neutral takeover: every formula below is a
//          line-for-line transcription of the old hand-written CPlayerAnimState
//          in game/shared/hl2mp/hl2mp_player_shared.cpp, which this class
//          replaces in both realms. See PHASE1A_REPORT.md for the
//          formula -> original file:line table.
//
//          DO NOT "improve" any of the maths here in Phase 1a: GMod behaviour
//          arrives in Phases 2-5 by replacing these steps, and the acceptance
//          criterion for this milestone is that the swap is a no-op.
//
//=============================================================================//

#include "cbase.h"

#ifdef CLIENT_DLL
	#include "c_hl2mp_player.h"
#else
	#include "hl2mp_player.h"
#endif

#include "gmod_player_animstate.h"

#include "animation.h"			// IndexModelSequences / LookupActivity / SelectWeightedSequence
#include "engine/ivmodelinfo.h"	// modelinfo, FindOrLoadModel / GetStudiomodel (mdlcache comes from cbase.h)
#include "activitylist.h"		// ActivityList_IndexForName, to tell shared from private

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// Below this many degrees, slow down turning rate linearly
// [old hl2mp_player_shared.cpp:152-157]
#define FADE_TURN_DEGREES	45.0f
// After this, need to start turning feet
#define MAX_TORSO_ANGLE		90.0f
// Below this amount, don't play a turning animation/perform IK
#define MIN_TURN_ANGLE_REQUIRING_TURN_ANIMATION		15.0f

// Garry's Mod clamps the player's main-animation playback rate with
// math.min( movement, 2 ) in gamemodes/base/gamemode/animations.lua, so a rate
// outside [0, 2] is never a real speed.
#define GMOD_MAX_PLAYBACK_RATE	2.0f

// Stage 2 switch, for isolating the GMod leg blend from everything else while
// testing in game:  gmod_anim_9way 0  falls back to the HL2-era behaviour.
static ConVar gmod_anim_9way( "gmod_anim_9way", "1", FCVAR_ARCHIVE,
	"1 = drive Garry's Mod's move_x/move_y 3x3 leg blend on models that declare it" );

// Stage 3 switch.  Only affects a player whose model is a GMod animation library
// (one that declares move_x/move_y), so HL2-era models keep the HL2MP decision.
static ConVar gmod_anim_decide( "gmod_anim_decide", "1", FCVAR_ARCHIVE,
	"1 = let Garry's Mod's CalcMainActivity rules pick the player's state activity" );

// Both mp_feetyawrate and mp_facefronttime are defined in
// game/shared/base_playeranimstate.cpp:40/46, which is compiled into BOTH
// client.dll and server.dll (client_base.vpc:191 / server_base.vpc:219). They are
// extern'd here exactly as the old hl2mp_player_shared.cpp:161-163 did.
extern ConVar mp_feetyawrate;
extern ConVar mp_facefronttime;
extern ConVar mp_ik;

//-----------------------------------------------------------------------------
// Purpose: constructor. Replaces CPlayerAnimState::CPlayerAnimState
//          [old hl2mp_player_shared.cpp:165-175].
//          The base class machinery is initialised here (Init() sets m_pOuter and
//          m_AnimConfig) but nothing from the base is used in this milestone.
//          LEGANIM_8WAY / m_bUseAimSequences=false is the honest description of
//          what HL2MP models blend today (move_yaw 8-way, no aim overlays). If
//          the base Update() is ever reached it will select no activities for
//          8WAY (ComputeMainSequence is only reached from the base Update, which
//          we never call) and will not touch playback rate for 8WAY.
//-----------------------------------------------------------------------------
CGModPlayerAnimState::CGModPlayerAnimState( CHL2MP_Player *outer )
	: m_pGModOuter( outer ),
	  m_pReportedStudioHdr( NULL ),
	  m_bGModJumping( false ),
	  m_bGModFirstJumpFrame( false ),
	  m_bGModWasOnGround( false ),
	  m_bGModInSwim( false ),
	  m_bGModWasNoclipping( false ),
	  m_flGModJumpStartTime( 0.0f ),
	  m_flGModGroundTime( 0.0f )
{
	CModAnimConfig config;
	config.m_flMaxBodyYawDegrees	= 0.0f;
	config.m_LegAnimType			= LEGANIM_8WAY;
	config.m_bUseAimSequences		= false;
	Init( (CBaseAnimatingOverlay *)outer, config );

	// The base constructor already zeroes m_flGaitYaw / m_flStoredCycle /
	// m_flGoalFeetYaw / m_flCurrentFeetYaw / m_flCurrentTorsoYaw / m_flLastYaw /
	// m_flLastTurnTime / m_nTurningInPlace / m_angRender
	// (base_playeranimstate.cpp:62-85) with the same values the old constructor
	// used (hl2mp_player_shared.cpp:168-174), so there is nothing left to do here.
}

//-----------------------------------------------------------------------------
// Purpose: typed accessor for the player. The old class returned CHL2MP_Player*
//          from GetOuter(); the base's GetOuter() returns CBaseAnimatingOverlay*,
//          so hide it with the same signature as before.
//-----------------------------------------------------------------------------
CHL2MP_Player *CGModPlayerAnimState::GetOuter()
{
	return m_pGModOuter;
}

//-----------------------------------------------------------------------------
// Purpose: The plain-CBasePlayerAnimState gate is GetOuter()->IsAlive(), which
//          would skip the whole pose pass for a dead player. The old
//          CPlayerAnimState had no such gate, so disabling it keeps the swap
//          behaviour-neutral. Only reachable from the base Update(), which we
//          do not call.
//-----------------------------------------------------------------------------
bool CGModPlayerAnimState::ShouldUpdateAnimState()
{
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: see header - keeps the base from ever reconsidering our sequences.
//-----------------------------------------------------------------------------
bool CGModPlayerAnimState::ShouldChangeSequences( void ) const
{
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Base no-ops (see header). The old class wrote no body_yaw.
//-----------------------------------------------------------------------------
void CGModPlayerAnimState::ComputePoseParam_BodyYaw()
{
}

void CGModPlayerAnimState::ComputePoseParam_BodyPitch( CStudioHdr *pStudioHdr )
{
}

void CGModPlayerAnimState::ComputePoseParam_MoveYaw( CStudioHdr *pStudioHdr )
{
}

//-----------------------------------------------------------------------------
// Purpose: Unreachable in this milestone (our Update() does not call the base).
//          Old CPlayerAnimState picked no activities at all, so ACT_INVALID is
//          the faithful "there is no activity here" answer.
//-----------------------------------------------------------------------------
Activity CGModPlayerAnimState::CalcMainActivity()
{
	return ACT_INVALID;
}

int CGModPlayerAnimState::CalcAimLayerSequence( float *flCycle, float *flAimSequenceWeight, bool bForceIdle )
{
	return -1;
}

//-----------------------------------------------------------------------------
// Purpose: Old ComputePlaybackRate() used GetSequenceGroundSpeed() directly
//          (hl2mp_player_shared.cpp:210), not any interpolated ground speed.
//-----------------------------------------------------------------------------
float CGModPlayerAnimState::GetCurrentMaxGroundSpeed()
{
	return GetOuter()->GetSequenceGroundSpeed( GetOuter()->GetSequence() );
}

//-----------------------------------------------------------------------------
// Purpose: Replaces CPlayerAnimState::Update
//          [old hl2mp_player_shared.cpp:180-195] - same order, same calls.
//          Deliberately does NOT call CBasePlayerAnimState::Update(float,float):
//          that would ClearAnimationLayers() -> SetNumAnimOverlays(5) every frame
//          (the player owns exactly 3 overlays, hl2mp_player.cpp:335) and pick
//          ACT_* activities via CalcMainActivity().
//-----------------------------------------------------------------------------
void CGModPlayerAnimState::Update()
{
	m_angRender = GetOuter()->GetLocalAngles();
	m_angRender[ PITCH ] = m_angRender[ ROLL ] = 0.0f;

	CStudioHdr *pStudioHdr = GetOuter()->GetModelPtr();

	// Guarantee this model's sequences have been resolved from their activity
	// NAMES.  A .mdl stores -1 for every sequence's activity (studiomdl only
	// records the name), so until IndexModelSequences() has run, every
	// SelectWeightedSequence() on this model fails and the player sits on
	// sequence 0 - the reference pose.  VerifySequenceIndex() is version-guarded
	// (game/shared/animation.cpp:216), so this is one integer compare per frame.
	VerifySequenceIndex( pStudioHdr );

	ComputeGModPoseParam_BodyYaw();			// move_yaw, for HL2-style models only
	ComputeGModPoseParam_Move( pStudioHdr );	// move_x/move_y 3x3, for GMod models only
	ComputeGModPoseParam_BodyPitch( pStudioHdr );
	ComputeGModPoseParam_BodyLookYaw();

	ComputeGModPlaybackRate();

#ifdef CLIENT_DLL
	GetOuter()->UpdateLookAt();
#endif
}

//-----------------------------------------------------------------------------
// Purpose: Stage 2 - drive GMod's leg blend.
//
// GMod's player animation libraries blend the legs on a 3x3 grid:
//
//     $sequence $walkname$ {
//         a_walking_..._SW  a_walking_..._S  a_walking_..._SE
//         a_walking_..._W   a_walking_..._C  a_walking_..._E
//         a_walking_..._NW  a_walking_..._N  a_walking_..._NE
//         blendwidth 3 blend move_y -1 1 blend move_x -1 1
//     }
//                                          [GMod macro_movement_m.qci:87-91]
//
// and the engine feeds that grid from move_x/move_y.  This fork's engine already
// contains that exact feeder (CBasePlayerAnimState::ComputePoseParam_MoveYaw,
// LEGANIM_9WAY - base_playeranimstate.cpp:636-664, the code CS:GO and Portal
// players run), and GMod's engine is a Source 2013 multiplayer derivative, so the
// grid GMod compiled its models with is the grid that code writes.  Reusing it is
// therefore the faithful choice, not a re-derivation.
//
// It is deliberately NOT a general switch to LEGANIM_9WAY: the HL2-era models in
// this fork declare move_yaw and are written by ComputeGModPoseParam_BodyYaw()
// above, exactly as HL2MP always did.  Only a model that actually declares
// move_x AND move_y takes this path.
//-----------------------------------------------------------------------------
void CGModPlayerAnimState::ComputeGModPoseParam_Move( CStudioHdr *pStudioHdr )
{
	if ( !pStudioHdr )
		return;

	ReportGModPoseParamsOnce( pStudioHdr );

	if ( !gmod_anim_9way.GetBool() )
		return;

	if ( GetOuter()->LookupPoseParameter( pStudioHdr, "move_x" ) < 0 ||
		 GetOuter()->LookupPoseParameter( pStudioHdr, "move_y" ) < 0 )
		return;

	// Only LEGANIM_9WAY reaches the 9-way writer; LEGANIM_8WAY would take the
	// move_yaw branch, which also drives the 8-way idle overlay (layer
	// MAIN_IDLE_SEQUENCE_LAYER) - an HL2MP player owns three overlays and that
	// layer belongs to the base's own sequence logic, which this class does not
	// use.  So the config is switched for the duration of this one call.
	const LegAnimType_t iSavedLegAnimType = m_AnimConfig.m_LegAnimType;
	m_AnimConfig.m_LegAnimType = LEGANIM_9WAY;
	CBasePlayerAnimState::ComputePoseParam_MoveYaw( pStudioHdr );
	m_AnimConfig.m_LegAnimType = iSavedLegAnimType;
}

//-----------------------------------------------------------------------------
// Purpose: Print, once per model, exactly which animation surface this model
//          offers.  Stage 2's rule is "report a missing parameter, never guess
//          one": a playermodel that declares no usable leg blend looks frozen, and
//          this is what makes that visible in the log instead of mysterious.
//
// The eight parameters GMod's libraries declare were established from the model
// bytes in Stage 0 (STAGE0_GMOD_ANIM_INVENTORY.md section 4):
//   move_y, move_x, aim_yaw, aim_pitch, vertical_velocity, vehicle_steer,
//   head_yaw, head_pitch      (body_yaw does NOT exist in them)
//-----------------------------------------------------------------------------
static const char *const s_GModPoseParamNames[] =
{
	"move_x", "move_y", "move_yaw", "aim_yaw", "aim_pitch",
	"body_yaw", "body_pitch", "head_yaw", "head_pitch",
	"vertical_velocity", "vehicle_steer",
};

void CGModPlayerAnimState::ReportGModPoseParamsOnce( CStudioHdr *pStudioHdr )
{
	if ( !pStudioHdr || m_pReportedStudioHdr == pStudioHdr )
		return;

	m_pReportedStudioHdr = pStudioHdr;

	char szPresent[ 640 ];
	char szAbsent[ 256 ];
	szPresent[ 0 ] = '\0';
	szAbsent[ 0 ] = '\0';

	for ( int i = 0; i < ARRAYSIZE( s_GModPoseParamNames ); ++i )
	{
		const int iParam = GetOuter()->LookupPoseParameter( pStudioHdr, s_GModPoseParamNames[ i ] );
		if ( iParam < 0 )
		{
			Q_strncat( szAbsent, s_GModPoseParamNames[ i ], sizeof( szAbsent ), COPY_ALL_CHARACTERS );
			Q_strncat( szAbsent, " ", sizeof( szAbsent ), COPY_ALL_CHARACTERS );
			continue;
		}

		const mstudioposeparamdesc_t &desc = pStudioHdr->pPoseParameter( iParam );
		char szOne[ 96 ];
		Q_snprintf( szOne, sizeof( szOne ), "%s[%.0f..%.0f] ",
					s_GModPoseParamNames[ i ], desc.start, desc.end );
		Q_strncat( szPresent, szOne, sizeof( szPresent ), COPY_ALL_CHARACTERS );
	}

	const bool bNineWay =
		GetOuter()->LookupPoseParameter( pStudioHdr, "move_x" ) >= 0 &&
		GetOuter()->LookupPoseParameter( pStudioHdr, "move_y" ) >= 0;

	Warning( "[HL2SB] GMod animstate on %s\n",
			 modelinfo->GetModelName( GetOuter()->GetModel() ) );
	Warning( "[HL2SB]   leg blend: %s\n", bNineWay
			 ? "move_x/move_y 3x3 (GMod animation library)"
			 : "move_yaw 8-way (HL2-era model)" );
	Warning( "[HL2SB]   present  : %s\n", szPresent[ 0 ] ? szPresent : "(none)" );
	Warning( "[HL2SB]   absent   : %s\n", szAbsent[ 0 ] ? szAbsent : "(none)" );
}

//-----------------------------------------------------------------------------
// Purpose: Stage 3 - Garry's Mod's activity decision.
//
// A port of gamemodes/base/gamemode/animations.lua, in the order
// GM:CalcMainActivity (:305-330) calls its handlers:
//
//   HandlePlayerLanding (:129)   HandlePlayerNoClipping (:71)
//   HandlePlayerDriving (:139)   HandlePlayerVaulting (:100)
//   HandlePlayerJumping (:2)     HandlePlayerSwimming (:113)
//   HandlePlayerDucking (:55)    then the 150 u/s run/walk/idle fallback (:320)
//
// Driving is deliberately NOT ported: HL2MP already handles vehicle parenting and
// GMod's branch only resolves <vehicle>_drive / sit_<holdtype> sequence NAMES,
// which the models this fork ships do not reliably bind.
//
// The returned activity is the BASE one.  CHL2MP_Player::SetAnimation() then runs
// the weapon's own act table over it (Weapon_TranslateActivity), which is what
// applies the hold type - the same order GMod uses (TranslateWeaponActivity
// first, the +N idle table only if the weapon did not decide).
//-----------------------------------------------------------------------------
bool CGModPlayerAnimState::GMod_ShouldDecideActivity()
{
	if ( !gmod_anim_decide.GetBool() )
		return false;

	// Only Garry's Mod's animation libraries take this path; an HL2-era model
	// (move_yaw) keeps the HL2MP decision untouched.
	CStudioHdr *pStudioHdr = GetOuter()->GetModelPtr();
	return pStudioHdr != NULL &&
		   GetOuter()->LookupPoseParameter( pStudioHdr, "move_x" ) >= 0 &&
		   GetOuter()->LookupPoseParameter( pStudioHdr, "move_y" ) >= 0;
}

//-----------------------------------------------------------------------------
// Purpose: GMod's PLAYERANIMEVENT_JUMP handling (animations.lua:388-396).
//-----------------------------------------------------------------------------
void CGModPlayerAnimState::GMod_OnJumpEvent()
{
	m_bGModJumping = true;
	m_bGModFirstJumpFrame = true;
	m_flGModJumpStartTime = gpGlobals->curtime;
	RestartMainSequence();
}

Activity CGModPlayerAnimState::GMod_CalcMainActivity( float flSpeed2D, bool bOnGround, bool bDucking, int iWaterLevel, int iMoveType )
{
	const float flSpeedSqr = flSpeed2D * flSpeed2D;
	const float flCurTime = gpGlobals->curtime;
	const bool bNoClip = ( iMoveType == MOVETYPE_NOCLIP );

	// HandlePlayerLanding (:129-137): the landing gesture fires on the frame the
	// player touches down, before any activity is chosen.  Gestures are a server
	// entry point in this fork (the client has no RestartGesture).
#ifndef CLIENT_DLL
	if ( !bNoClip && bOnGround && !m_bGModWasOnGround )
	{
		GetOuter()->RestartGesture( ACT_LAND, true );
	}
#endif

	// HandlePlayerNoClipping (:71-98).  GMod restarts the noclip gesture layer with
	// addifmissing = false and leaves the activity alone.
	if ( bNoClip && !m_bGModWasNoclipping )
	{
#ifndef CLIENT_DLL
		GetOuter()->RestartGesture( ACT_GMOD_NOCLIP_LAYER, false );
#endif
	}
	m_bGModWasNoclipping = bNoClip;

	// HandlePlayerJumping (:2-53)
	if ( bNoClip )
	{
		m_bGModJumping = false;
	}
	else if ( !m_bGModJumping && !bOnGround && iWaterLevel <= 0 )
	{
		// Airwalk until horizontal speed is gone, then it becomes the jump anim.
		if ( m_flGModGroundTime == 0.0f )
		{
			m_flGModGroundTime = flCurTime;
		}
		else if ( ( flCurTime - m_flGModGroundTime ) > 0.0f && flSpeedSqr < 0.25f )
		{
			m_bGModJumping = true;
			m_bGModFirstJumpFrame = false;
			m_flGModJumpStartTime = 0.0f;
		}
	}

	if ( m_bGModJumping )
	{
		if ( m_bGModFirstJumpFrame )
		{
			m_bGModFirstJumpFrame = false;
			RestartMainSequence();
		}

		// 0.2 s after the jump event, or on touching water, the jump is over.
		if ( iWaterLevel >= 2 || ( ( flCurTime - m_flGModJumpStartTime ) > 0.2f && bOnGround ) )
		{
			m_bGModJumping = false;
			m_flGModGroundTime = 0.0f;
			RestartMainSequence();
		}

		if ( m_bGModJumping )
		{
			m_bGModWasOnGround = bOnGround;
			// GMod's own table maps ACT_MP_JUMP to ACT_HL2MP_JUMP_SLAM because no
			// ACT_HL2MP_JUMP sequence exists; m_anm.mdl confirms that offline.
			return ACT_HL2MP_JUMP_SLAM;
		}
	}

	// HandlePlayerVaulting (:100-111): a very fast airborne player is animated as
	// if swimming.  This is GMod's own rule, odd as it reads.
	if ( flSpeedSqr >= 1000000.0f && !bOnGround )
	{
		m_bGModWasOnGround = bOnGround;
		return ACT_HL2MP_SWIM;
	}

	// HandlePlayerSwimming (:113-127)
	if ( iWaterLevel >= 2 && !bOnGround )
	{
		m_bGModInSwim = true;
		m_bGModWasOnGround = bOnGround;
		return ACT_HL2MP_SWIM;
	}
	m_bGModInSwim = false;

	// HandlePlayerDucking (:55-69)
	if ( bDucking )
	{
		m_bGModWasOnGround = bOnGround;
		return ( flSpeedSqr > 0.25f ) ? ACT_HL2MP_WALK_CROUCH : ACT_HL2MP_IDLE_CROUCH;
	}

	// The fallback (:320-322): 150 u/s splits a run from a walk.
	Activity idealActivity = ACT_HL2MP_IDLE;
	if ( flSpeedSqr > 22500.0f )
		idealActivity = ACT_HL2MP_RUN;
	else if ( flSpeedSqr > 0.25f )
		idealActivity = ACT_HL2MP_WALK;

	m_bGModWasOnGround = bOnGround;
	return idealActivity;
}


//-----------------------------------------------------------------------------
// Purpose: Replaces CPlayerAnimState::ComputePlaybackRate
//          [old hl2mp_player_shared.cpp:200-226].
//-----------------------------------------------------------------------------
void CGModPlayerAnimState::ComputeGModPlaybackRate()
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
		float flRate = ( speed * flFactor ) / maxspeed;

		// A GMod library sequence whose animation data did not load reports a
		// denormal ground speed, which turns this ratio into garbage: the game log
		// shows the datatable clamping m_flPlaybackRate from 28348063744.  GMod's
		// own Lua clamps the rate with math.min( movement, 2 ), so anything outside
		// [0, 2] here is a broken ground speed, not a real speed - play at 1.0
		// rather than inheriting it.  (NaN fails the first comparison on purpose.)
		if ( flRate != flRate || flRate < 0.0f || flRate > GMOD_MAX_PLAYBACK_RATE )
			flRate = 1.0f;

		GetOuter()->SetPlaybackRate( flRate );

		// BUG BUG:
		// This stuff really should be m_flPlaybackRate = speed / m_flGroundSpeed
	}
	else
	{
		GetOuter()->SetPlaybackRate( 1.0f );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Replaces CPlayerAnimState::EstimateYaw
//          [old hl2mp_player_shared.cpp:241-283].
//-----------------------------------------------------------------------------
void CGModPlayerAnimState::EstimateYaw( void )
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
		float flYawDiff = angles[YAW] - m_flGModGaitYaw;
		flYawDiff = flYawDiff - (int)(flYawDiff / 360) * 360;
		if (flYawDiff > 180)
			flYawDiff -= 360;
		if (flYawDiff < -180)
			flYawDiff += 360;

		if (dt < 0.25)
			flYawDiff *= dt * 4;
		else
			flYawDiff *= dt;

		m_flGModGaitYaw += flYawDiff;
		m_flGModGaitYaw = m_flGModGaitYaw - (int)(m_flGModGaitYaw / 360) * 360;
	}
	else
	{
		m_flGModGaitYaw = (atan2(est_velocity[1], est_velocity[0]) * 180 / M_PI);

		if (m_flGModGaitYaw > 180)
			m_flGModGaitYaw = 180;
		else if (m_flGModGaitYaw < -180)
			m_flGModGaitYaw = -180;
	}
}

//-----------------------------------------------------------------------------
// Purpose: Replaces CPlayerAnimState::ComputePoseParam_BodyYaw
//          [old hl2mp_player_shared.cpp:289-332]. This is the "move_yaw" writer.
//
//          NULL-MODEL GUARD: LookupPoseParameter returns 0 (not -1) when the
//          model pointer is NULL (baseanimating.cpp:1286-1289 /
//          c_baseanimating.cpp:5606-5609), so an unloaded custom player model
//          would silently resolve to pose parameter index 0. The old code had no
//          guard; with a NULL studiohdr SetPoseParameter() bails out anyway
//          (baseanimating.cpp:1218-1223), so bailing out earlier here is
//          behaviour-identical and cannot write index 0 on a NULL model.
//-----------------------------------------------------------------------------
void CGModPlayerAnimState::ComputeGModPoseParam_BodyYaw( void )
{
	CStudioHdr *pStudioHdr = GetOuter()->GetModelPtr();
	if ( !pStudioHdr )
		return;

	int iYaw = GetOuter()->LookupPoseParameter( pStudioHdr, "move_yaw" );
	if ( iYaw < 0 )
		return;

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
	flYaw = ang - m_flGModGaitYaw;
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

	GetOuter()->SetPoseParameter( pStudioHdr, iYaw, flYaw );

#ifndef CLIENT_DLL
	//Adrian: Make the model's angle match the legs so the hitboxes match on both sides.
	GetOuter()->SetLocalAngles( QAngle( GetOuter()->GetAnimEyeAngles().x, m_flCurrentFeetYaw, 0 ) );
#endif
}

//-----------------------------------------------------------------------------
// Purpose: Replaces CPlayerAnimState::ComputePoseParam_BodyPitch
//          [old hl2mp_player_shared.cpp:337-355]. This is the "aim_pitch" writer.
//          Same NULL-model early-out as ComputeGModPoseParam_BodyYaw.
//-----------------------------------------------------------------------------
void CGModPlayerAnimState::ComputeGModPoseParam_BodyPitch( CStudioHdr *pStudioHdr )
{
	if ( !pStudioHdr )
		return;

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
// Purpose: Replaces CPlayerAnimState::ConvergeAngles
//          [old hl2mp_player_shared.cpp:365-405].
//          NOTE: 4-argument HL2MP form (goal, maxrate, dt, current). It HIDES the
//          base's 5-argument ConvergeAngles(goal,maxrate,maxgap,dt,current); the
//          "using" declaration in the header keeps both visible.
//-----------------------------------------------------------------------------
int CGModPlayerAnimState::ConvergeAngles( float goal, float maxrate, float dt, float& current )
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

//-----------------------------------------------------------------------------
// Purpose: Replaces CPlayerAnimState::ComputePoseParam_BodyLookYaw
//          [old hl2mp_player_shared.cpp:407-550]. This is the "aim_yaw" writer
//          (clamped to +/-60 at :538). The commented-out body_yaw block
//          (:540-548) is deliberately NOT transcribed: it is inside a comment, so
//          body_yaw was never written.
//-----------------------------------------------------------------------------
void CGModPlayerAnimState::ComputeGModPoseParam_BodyLookYaw( void )
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
// Purpose: Replaces CPlayerAnimState::GetOuterAbsVelocity
//          [old hl2mp_player_shared.cpp:592-599].
//-----------------------------------------------------------------------------
void CGModPlayerAnimState::GetOuterAbsVelocity( Vector& vel )
{
#if defined( CLIENT_DLL )
	GetOuter()->EstimateAbsVelocity( vel );
#else
	vel = GetOuter()->GetAbsVelocity();
#endif
}

//-----------------------------------------------------------------------------
// Purpose: In-game proof of the activity-vocabulary fix, and the tool for
//          diagnosing a playermodel's GMod animation surface without a debugger.
//
//          * pose parameters with their real declared ranges - what Stage 2 can
//            drive, and what it must report as missing
//          * for every activity name the model binds: the number it resolves to,
//            whether that name is a SHARED activity, and which sequence the
//            engine picks for it.
//
//          The shared/private column is the one that matters.  A name that is
//          PRIVATE on the server and absent on the client is exactly the silent
//          realm divergence that registering the vocabulary removed; this command
//          makes it a number on the screen.
//-----------------------------------------------------------------------------
CON_COMMAND( gmod_anim_dumpmodel, "dump a model's GMod animation surface: pose params + activity name -> sequence" )
{
	if ( args.ArgC() < 2 )
	{
		Msg( "usage: gmod_anim_dumpmodel <model path, e.g. models/player/group01/male_01.mdl>\n" );
		return;
	}

	const char *pszModelName = args.Arg( 1 );
	const model_t *pModel = modelinfo->FindOrLoadModel( pszModelName );
	if ( !pModel )
	{
		Warning( "gmod_anim_dumpmodel: cannot load '%s'\n", pszModelName );
		return;
	}

	studiohdr_t *pStudioHdrRaw = modelinfo->GetStudiomodel( pModel );
	if ( !pStudioHdrRaw )
	{
		Warning( "gmod_anim_dumpmodel: '%s' has no studiohdr (not a studio model?)\n", pszModelName );
		return;
	}

	CStudioHdr studioHdr( pStudioHdrRaw, mdlcache );

	// Force the name -> activity-number pass, so what is printed below is what
	// the engine will actually use at runtime for this model.
	IndexModelSequences( &studioHdr );

	Msg( "gmod_anim_dumpmodel: %s\n", pszModelName );
	Msg( "  sequences %d, pose parameters %d, ik chains %d\n",
		 studioHdr.GetNumSeq(), studioHdr.GetNumPoseParameters(), studioHdr.GetNumIKChains() );

	Msg( "  pose parameters:\n" );
	for ( int i = 0; i < studioHdr.GetNumPoseParameters(); ++i )
	{
		const mstudioposeparamdesc_t &desc = studioHdr.pPoseParameter( i );
		Msg( "    [%2d] %-20s %.2f .. %.2f   loop %.2f\n",
			 i, desc.pszName(), desc.start, desc.end, desc.loop );
	}

	CUtlVector<const char *> seenActivities;
	int iShared = 0, iPrivate = 0, iUnknown = 0, iWithSequence = 0, iWithoutSequence = 0;

	Msg( "  activity names bound by this model:\n" );
	for ( int i = 0; i < studioHdr.GetNumSeq(); ++i )
	{
		mstudioseqdesc_t &seqdesc = studioHdr.pSeqdesc( i );
		const char *pszActivity = seqdesc.pszActivityName();
		if ( !pszActivity || !pszActivity[ 0 ] )
			continue;

		if ( seenActivities.Find( pszActivity ) != seenActivities.InvalidIndex() )
			continue;
		seenActivities.AddToTail( pszActivity );

		// Is this name a SHARED activity (identical on both realms), or did the
		// load-time pass have to invent a private number for it?
		const int iRegistered = ActivityList_IndexForName( pszActivity );
		if ( iRegistered < 0 )
			++iUnknown;
		else if ( iRegistered < LAST_SHARED_ACTIVITY )
			++iShared;
		else
			++iPrivate;

		const int iActivity = LookupActivity( &studioHdr, pszActivity );
		const int iSequence = ( iActivity > ACT_RESET )
			? SelectWeightedSequence( &studioHdr, iActivity ) : -1;

		if ( iSequence >= 0 )
			++iWithSequence;
		else
			++iWithoutSequence;

		const char *pszKind = ( iRegistered < 0 ) ? "UNREGISTERED"
			: ( iRegistered < LAST_SHARED_ACTIVITY ) ? "shared" : "PRIVATE";

		Msg( "    %-46s %-12s activity=%-5d sequence=%d\n",
			 pszActivity, pszKind, iActivity, iSequence );
	}

	Msg( "  summary: %d distinct names (shared %d, private %d, unregistered %d); "
		 "sequences found %d, not found %d\n",
		 seenActivities.Count(), iShared, iPrivate, iUnknown, iWithSequence, iWithoutSequence );

	if ( iUnknown || iPrivate || iWithoutSequence )
		Warning( "gmod_anim_dumpmodel: %s has %d unregistered and %d private activity names, "
				 "and %d names with no sequence\n",
				 pszModelName, iUnknown, iPrivate, iWithoutSequence );
}


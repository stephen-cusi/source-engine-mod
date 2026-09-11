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

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// Below this many degrees, slow down turning rate linearly
// [old hl2mp_player_shared.cpp:152-157]
#define FADE_TURN_DEGREES	45.0f
// After this, need to start turning feet
#define MAX_TORSO_ANGLE		90.0f
// Below this amount, don't play a turning animation/perform IK
#define MIN_TURN_ANGLE_REQUIRING_TURN_ANIMATION		15.0f

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
	: m_pGModOuter( outer )
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

	ComputeGModPoseParam_BodyYaw();
	ComputeGModPoseParam_BodyPitch( GetOuter()->GetModelPtr() );
	ComputeGModPoseParam_BodyLookYaw();

	ComputeGModPlaybackRate();

#ifdef CLIENT_DLL
	GetOuter()->UpdateLookAt();
#endif
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

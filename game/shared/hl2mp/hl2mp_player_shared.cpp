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
#define CRecipientFilter C_RecipientFilter
#else
#include "hl2mp_player.h"
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
		Q_snprintf( szStepSound, sizeof( szStepSound ), "%s.RunFootstepLeft", g_ppszPlayerSoundPrefixNames[m_iPlayerSoundType] );
	}
	else
	{
		Q_snprintf( szStepSound, sizeof( szStepSound ), "%s.RunFootstepRight", g_ppszPlayerSoundPrefixNames[m_iPlayerSoundType] );
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
		}
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
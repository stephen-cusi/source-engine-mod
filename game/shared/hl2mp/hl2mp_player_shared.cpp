//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//
//=============================================================================//
#include "cbase.h"
#include "decals.h"
#ifdef CLIENT_DLL
#include "c_hl2mp_player.h"
#include "prediction.h"
#include "engine/ivdebugoverlay.h"
#define CRecipientFilter C_RecipientFilter
#else
#include "hl2mp_player.h"
#include "soundent.h"
#include "cs_gamestats.h"
#endif
#include "effect_dispatch_data.h"
#include "gamevars_shared.h"

#include "engine/IEngineSound.h"
#include "SoundEmitterSystem/isoundemittersystembase.h"

#include "datacache/imdlcache.h"

void DispatchEffect(const char* pName, const CEffectData& data);

extern ConVar sv_footsteps;
ConVar sv_showimpacts("sv_showimpacts", "0", FCVAR_REPLICATED, "Shows client (red) and server (blue) bullet impact point (1=both, 2=client-only, 3=server-only)");
ConVar sv_showplayerhitboxes("sv_showplayerhitboxes", "0", FCVAR_REPLICATED, "Show lag compensated hitboxes for the specified player index whenever a player fires.");
#define	CS_MASK_SHOOT (MASK_SOLID|CONTENTS_DEBRIS)

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
	int iYaw = GetOuter()->LookupPoseParameter( "move_yaw" );
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
	
	GetOuter()->SetPoseParameter( iYaw, flYaw );

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

inline void UTIL_TraceLineIgnoreTwoEntities(const Vector& vecAbsStart, const Vector& vecAbsEnd, unsigned int mask,
	const IHandleEntity* ignore, const IHandleEntity* ignore2, int collisionGroup, trace_t* ptr)
{
	Ray_t ray;
	ray.Init(vecAbsStart, vecAbsEnd);
	CTraceFilterSkipTwoEntities traceFilter(ignore, ignore2, collisionGroup);
	enginetrace->TraceRay(ray, mask, &traceFilter, ptr);
	if (r_visualizetraces.GetBool())
	{
		DebugDrawLine(ptr->startpos, ptr->endpos, 255, 0, 0, true, -1.0f);
	}
}

static void GetMaterialParameters(int iMaterial, float& flPenetrationModifier, float& flDamageModifier)
{
	switch (iMaterial)
	{
	case CHAR_TEX_METAL:
		flPenetrationModifier = 0.5;  // If we hit metal, reduce the thickness of the brush we can't penetrate
		flDamageModifier = 0.3;
		break;
	case CHAR_TEX_DIRT:
		flPenetrationModifier = 0.5;
		flDamageModifier = 0.3;
		break;
	case CHAR_TEX_CONCRETE:
		flPenetrationModifier = 0.4;
		flDamageModifier = 0.25;
		break;
	case CHAR_TEX_GRATE:
		flPenetrationModifier = 1.0;
		flDamageModifier = 0.99;
		break;
	case CHAR_TEX_VENT:
		flPenetrationModifier = 0.5;
		flDamageModifier = 0.45;
		break;
	case CHAR_TEX_TILE:
		flPenetrationModifier = 0.65;
		flDamageModifier = 0.3;
		break;
	case CHAR_TEX_COMPUTER:
		flPenetrationModifier = 0.4;
		flDamageModifier = 0.45;
		break;
	case CHAR_TEX_WOOD:
		flPenetrationModifier = 1.0;
		flDamageModifier = 0.6;
		break;
	default:
		flPenetrationModifier = 1.0;
		flDamageModifier = 0.5;
		break;
	}

	Assert(flPenetrationModifier > 0);
	Assert(flDamageModifier < 1.0f); // Less than 1.0f for avoiding infinite loops
}


static bool TraceToExit(Vector& start, Vector& dir, Vector& end, float flStepSize, float flMaxDistance)
{
	float flDistance = 0;
	Vector last = start;

	while (flDistance <= flMaxDistance)
	{
		flDistance += flStepSize;

		end = start + flDistance * dir;

		if ((UTIL_PointContents(end) & MASK_SOLID) == 0)
		{
			// found first free point
			return true;
		}
	}

	return false;
}

void CHL2MP_Player::FireBullet(
	Vector vecSrc,	// shooting postion
	const QAngle& shootAngles,  //shooting angle
	float flDistance, // max distance
	int iPenetration, // how many obstacles can be penetrated
	int iBulletType, // ammo type
	int iDamage, // base damage
	float flRangeModifier, // damage range modifier
	CBaseEntity* pevAttacker, // shooter
	bool bDoEffects,
	float xSpread, float ySpread
)
{
	float fCurrentDamage = iDamage;   // damage of the bullet at it's current trajectory
	float flCurrentDistance = 0.0;  //distance that the bullet has traveled so far

	Vector vecDirShooting, vecRight, vecUp;
	AngleVectors(shootAngles, &vecDirShooting, &vecRight, &vecUp);

	// MIKETODO: put all the ammo parameters into a script file and allow for CS-specific params.
	float flPenetrationPower = 0;		// thickness of a wall that this bullet can penetrate
	float flPenetrationDistance = 0;	// distance at which the bullet is capable of penetrating a wall
	float flDamageModifier = 0.5;		// default modification of bullets power after they go through a wall.
	float flPenetrationModifier = 1.f;

	GetBulletTypeParameters(iBulletType, flPenetrationPower, flPenetrationDistance);


	if (!pevAttacker)
		pevAttacker = this;  // the default attacker is ourselves

	// add the spray
	Vector vecDir = vecDirShooting + xSpread * vecRight + ySpread * vecUp;

	VectorNormalize(vecDir);

	//Adrian: visualize server/client player positions
	//This is used to show where the lag compesator thinks the player should be at.
#if 0
	for (int k = 1; k <= gpGlobals->maxClients; k++)
	{
		CBasePlayer* clientClass = (CBasePlayer*)CBaseEntity::Instance(k);

		if (clientClass == NULL)
			continue;

		if (k == entindex())
			continue;

#ifdef CLIENT_DLL
		debugoverlay->AddBoxOverlay(clientClass->GetAbsOrigin(), clientClass->WorldAlignMins(), clientClass->WorldAlignMaxs(), QAngle(0, 0, 0), 255, 0, 0, 127, 4);
#else
		NDebugOverlay::Box(clientClass->GetAbsOrigin(), clientClass->WorldAlignMins(), clientClass->WorldAlignMaxs(), 0, 0, 255, 127, 4);
#endif

	}

#endif
	//=============================================================================
	// HPE_BEGIN:
	//=============================================================================

#ifndef CLIENT_DLL
	// [pfreese] Track number player entities killed with this bullet
	int iPenetrationKills = 0;

	// [menglish] Increment the shots fired for this player
	CCS_GameStats.Event_ShotFired(this, GetActiveWeapon());
#endif

	//=============================================================================
	// HPE_END
	//=============================================================================

	bool bFirstHit = true;

	CBasePlayer* lastPlayerHit = NULL;

	if (sv_showplayerhitboxes.GetInt() > 0)
	{
		CBasePlayer* lagPlayer = UTIL_PlayerByIndex(sv_showplayerhitboxes.GetInt());
		if (lagPlayer)
		{
#ifdef CLIENT_DLL
			lagPlayer->DrawClientHitboxes(4, true);
#else
			lagPlayer->DrawServerHitboxes(4, true);
#endif
		}
	}

	MDLCACHE_CRITICAL_SECTION();
	while (fCurrentDamage > 0)
	{
		Vector vecEnd = vecSrc + vecDir * flDistance;

		trace_t tr; // main enter bullet trace

		UTIL_TraceLineIgnoreTwoEntities(vecSrc, vecEnd, CS_MASK_SHOOT | CONTENTS_HITBOX, this, lastPlayerHit, COLLISION_GROUP_NONE, &tr);
		{
			CTraceFilterSkipTwoEntities filter(this, lastPlayerHit, COLLISION_GROUP_NONE);

			// Check for player hitboxes extending outside their collision bounds
			const float rayExtension = 40.0f;
			UTIL_ClipTraceToPlayers(vecSrc, vecEnd + vecDir * rayExtension, CS_MASK_SHOOT | CONTENTS_HITBOX, &filter, &tr);
		}

		lastPlayerHit = ToBasePlayer(tr.m_pEnt);

		if (tr.fraction == 1.0f)
			break; // we didn't hit anything, stop tracing shoot

#ifdef _DEBUG
		if (bFirstHit)
			AddBulletStat(gpGlobals->realtime, VectorLength(vecSrc - tr.endpos), tr.endpos);
#endif

		bFirstHit = false;

#ifndef CLIENT_DLL
		//
		// Propogate a bullet impact event
		// @todo Add this for shotgun pellets (which dont go thru here)
		//
		IGameEvent* event = gameeventmanager->CreateEvent("bullet_impact");
		if (event)
		{
			event->SetInt("userid", GetUserID());
			event->SetFloat("x", tr.endpos.x);
			event->SetFloat("y", tr.endpos.y);
			event->SetFloat("z", tr.endpos.z);
			gameeventmanager->FireEvent(event);
		}
#endif

		/************* MATERIAL DETECTION ***********/
		surfacedata_t* pSurfaceData = physprops->GetSurfaceData(tr.surface.surfaceProps);
		int iEnterMaterial = pSurfaceData->game.material;

		GetMaterialParameters(iEnterMaterial, flPenetrationModifier, flDamageModifier);

		bool hitGrate = tr.contents & CONTENTS_GRATE;

		// since some railings in de_inferno are CONTENTS_GRATE but CHAR_TEX_CONCRETE, we'll trust the
		// CONTENTS_GRATE and use a high damage modifier.
		if (hitGrate)
		{
			// If we're a concrete grate (TOOLS/TOOLSINVISIBLE texture) allow more penetrating power.
			flPenetrationModifier = 1.0f;
			flDamageModifier = 0.99f;
		}

#ifdef CLIENT_DLL
		if (sv_showimpacts.GetInt() == 1 || sv_showimpacts.GetInt() == 2)
		{
			// draw red client impact markers
			debugoverlay->AddBoxOverlay(tr.endpos, Vector(-2, -2, -2), Vector(2, 2, 2), QAngle(0, 0, 0), 255, 0, 0, 127, 4);

			if (tr.m_pEnt && tr.m_pEnt->IsPlayer())
			{
				C_BasePlayer* player = ToBasePlayer(tr.m_pEnt);
				player->DrawClientHitboxes(4, true);
			}
		}
#else
		if (sv_showimpacts.GetInt() == 1 || sv_showimpacts.GetInt() == 3)
		{
			// draw blue server impact markers
			NDebugOverlay::Box(tr.endpos, Vector(-2, -2, -2), Vector(2, 2, 2), 0, 0, 255, 127, 4);

			if (tr.m_pEnt && tr.m_pEnt->IsPlayer())
			{
				CBasePlayer* player = ToBasePlayer(tr.m_pEnt);
				player->DrawServerHitboxes(4, true);
			}
		}
#endif

		//calculate the damage based on the distance the bullet travelled.
		flCurrentDistance += tr.fraction * flDistance;
		fCurrentDamage *= pow(flRangeModifier, (flCurrentDistance / 500));

		// check if we reach penetration distance, no more penetrations after that
		if (flCurrentDistance > flPenetrationDistance && iPenetration > 0)
			iPenetration = 0;

#ifndef CLIENT_DLL
		// This just keeps track of sounds for AIs (it doesn't play anything).
		CSoundEnt::InsertSound(SOUND_BULLET_IMPACT, tr.endpos, 400, 0.2f, this);
#endif

		int iDamageType = DMG_BULLET | DMG_NEVERGIB;

		if (bDoEffects)
		{
			// See if the bullet ended up underwater + started out of the water
			if (enginetrace->GetPointContents(tr.endpos) & (CONTENTS_WATER | CONTENTS_SLIME))
			{
				trace_t waterTrace;
				UTIL_TraceLine(vecSrc, tr.endpos, (MASK_SHOT | CONTENTS_WATER | CONTENTS_SLIME), this, COLLISION_GROUP_NONE, &waterTrace);

				if (waterTrace.allsolid != 1)
				{
					CEffectData	data;
					data.m_vOrigin = waterTrace.endpos;
					data.m_vNormal = waterTrace.plane.normal;
					data.m_flScale = random->RandomFloat(8, 12);

					if (waterTrace.contents & CONTENTS_SLIME)
					{
						data.m_fFlags |= FX_WATER_IN_SLIME;
					}

					DispatchEffect("gunshotsplash", data);
				}
			}
			else
			{
				//Do Regular hit effects

				// Don't decal nodraw surfaces
				if (!(tr.surface.flags & (SURF_SKY | SURF_NODRAW | SURF_HINT | SURF_SKIP)))
				{
					CBaseEntity* pEntity = tr.m_pEnt;
					if (!(!friendlyfire.GetBool() && pEntity && pEntity->GetTeamNumber() == GetTeamNumber()))
					{
						UTIL_ImpactTrace(&tr, iDamageType);
					}
				}
			}
		} // bDoEffects

		// add damage to entity that we hit

#ifndef CLIENT_DLL
		ClearMultiDamage();

		//=============================================================================
		// HPE_BEGIN:
		// [pfreese] Check if enemy players were killed by this bullet, and if so,
		// add them to the iPenetrationKills count
		//=============================================================================

		CBaseEntity* pEntity = tr.m_pEnt;

		CTakeDamageInfo info(pevAttacker, pevAttacker, fCurrentDamage, iDamageType);
		CalculateBulletDamageForce(&info, iBulletType, vecDir, tr.endpos);
		pEntity->DispatchTraceAttack(info, vecDir, &tr);

		bool bWasAlive = pEntity->IsAlive();

		TraceAttackToTriggers(info, tr.startpos, tr.endpos, vecDir);

		ApplyMultiDamage();

		if (bWasAlive && !pEntity->IsAlive() && pEntity->IsPlayer() && pEntity->GetTeamNumber() != GetTeamNumber())
		{
			++iPenetrationKills;
		}

		//=============================================================================
		// HPE_END
		//=============================================================================

#endif

		// check if bullet can penetrate another entity
		if (iPenetration == 0 && !hitGrate)
			break; // no, stop

		// If we hit a grate with iPenetration == 0, stop on the next thing we hit
		if (iPenetration < 0)
			break;

		Vector penetrationEnd;

		// try to penetrate object, maximum penetration is 128 inch
		if (!TraceToExit(tr.endpos, vecDir, penetrationEnd, 24, 128))
			break;

		// find exact penetration exit
		trace_t exitTr;
		UTIL_TraceLine(penetrationEnd, tr.endpos, CS_MASK_SHOOT | CONTENTS_HITBOX, NULL, &exitTr);

		if (exitTr.m_pEnt != tr.m_pEnt && exitTr.m_pEnt != NULL)
		{
			// something was blocking, trace again
			UTIL_TraceLine(penetrationEnd, tr.endpos, CS_MASK_SHOOT | CONTENTS_HITBOX, exitTr.m_pEnt, COLLISION_GROUP_NONE, &exitTr);
		}

		// get material at exit point
		pSurfaceData = physprops->GetSurfaceData(exitTr.surface.surfaceProps);
		int iExitMaterial = pSurfaceData->game.material;

		hitGrate = hitGrate && (exitTr.contents & CONTENTS_GRATE);

		// if enter & exit point is wood or metal we assume this is
		// a hollow crate or barrel and give a penetration bonus
		if (iEnterMaterial == iExitMaterial)
		{
			if (iExitMaterial == CHAR_TEX_WOOD ||
				iExitMaterial == CHAR_TEX_METAL)
			{
				flPenetrationModifier *= 2;
			}
		}

		float flTraceDistance = VectorLength(exitTr.endpos - tr.endpos);

		// check if bullet has enough power to penetrate this distance for this material
		if (flTraceDistance > (flPenetrationPower * flPenetrationModifier))
			break; // bullet hasn't enough power to penetrate this distance

		// penetration was successful

		// bullet did penetrate object, exit Decal
		if (bDoEffects)
		{
			UTIL_ImpactTrace(&exitTr, iDamageType);
		}

		//setup new start end parameters for successive trace

		flPenetrationPower -= flTraceDistance / flPenetrationModifier;
		flCurrentDistance += flTraceDistance;

		// NDebugOverlay::Box( exitTr.endpos, Vector(-2,-2,-2), Vector(2,2,2), 0,255,0,127, 8 );

		vecSrc = exitTr.endpos;
		flDistance = (flDistance - flCurrentDistance) * 0.5;

		// reduce damage power each time we hit something other than a grate
		fCurrentDamage *= flDamageModifier;

		// reduce penetration counter
		iPenetration--;
	}

#ifndef CLIENT_DLL
	//=============================================================================
	// HPE_BEGIN:
	// [pfreese] If we killed at least two enemies with a single bullet, award the
	// TWO_WITH_ONE_SHOT achievement
	//=============================================================================

	if (iPenetrationKills >= 2)
	{
		AwardAchievement(CSKillTwoWithOneShot);
	}

	//=============================================================================
	// HPE_END
	//=============================================================================
#endif
}

void CHL2MP_Player::GetBulletTypeParameters(
	int iBulletType,
	float& fPenetrationPower,
	float& flPenetrationDistance)
{
	//MIKETODO: make ammo types come from a script file.
	if (IsAmmoType(iBulletType, BULLET_PLAYER_50AE))
	{
		fPenetrationPower = 30;
		flPenetrationDistance = 1000.0;
	}
	else if (IsAmmoType(iBulletType, BULLET_PLAYER_762MM))
	{
		fPenetrationPower = 39;
		flPenetrationDistance = 5000.0;
	}
	else if (IsAmmoType(iBulletType, BULLET_PLAYER_556MM) ||
		IsAmmoType(iBulletType, BULLET_PLAYER_556MM_BOX))
	{
		fPenetrationPower = 35;
		flPenetrationDistance = 4000.0;
	}
	else if (IsAmmoType(iBulletType, BULLET_PLAYER_338MAG))
	{
		fPenetrationPower = 45;
		flPenetrationDistance = 8000.0;
	}
	else if (IsAmmoType(iBulletType, BULLET_PLAYER_9MM))
	{
		fPenetrationPower = 21;
		flPenetrationDistance = 800.0;
	}
	else if (IsAmmoType(iBulletType, BULLET_PLAYER_BUCKSHOT))
	{
		fPenetrationPower = 0;
		flPenetrationDistance = 0.0;
	}
	else if (IsAmmoType(iBulletType, BULLET_PLAYER_45ACP))
	{
		fPenetrationPower = 15;
		flPenetrationDistance = 500.0;
	}
	else if (IsAmmoType(iBulletType, BULLET_PLAYER_357SIG))
	{
		fPenetrationPower = 25;
		flPenetrationDistance = 800.0;
	}
	else if (IsAmmoType(iBulletType, BULLET_PLAYER_57MM))
	{
		fPenetrationPower = 30;
		flPenetrationDistance = 2000.0;
	}
	else
	{
		// What kind of ammo is this?
		Assert(false);
		fPenetrationPower = 0;
		flPenetrationDistance = 0.0;
	}
}
void CHL2MP_Player::KickBack(float up_base, float lateral_base, float up_modifier, float lateral_modifier, float up_max, float lateral_max, int direction_change)
{
	float flKickUp;
	float flKickLateral;

	if (m_iShotsFired == 1) // This is the first round fired
	{
		flKickUp = up_base;
		flKickLateral = lateral_base;
	}
	else
	{
		flKickUp = up_base + m_iShotsFired * up_modifier;
		flKickLateral = lateral_base + m_iShotsFired * lateral_modifier;
	}


	QAngle angle = GetPunchAngle();

	angle.x -= flKickUp;
	if (angle.x < -1 * up_max)
		angle.x = -1 * up_max;

	if (m_iDirection == 1)
	{
		angle.y += flKickLateral;
		if (angle.y > lateral_max)
			angle.y = lateral_max;
	}
	else
	{
		angle.y -= flKickLateral;
		if (angle.y < -1 * lateral_max)
			angle.y = -1 * lateral_max;
	}

	if (!SharedRandomInt("KickBack", 0, direction_change))
		m_iDirection = 1 - m_iDirection;

	SetPunchAngle(angle);
}
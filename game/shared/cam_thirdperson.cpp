//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose:
//
//=============================================================================//

#include "cbase.h"
#include "cam_thirdperson.h"
#include "gamerules.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

static Vector CAM_HULL_MIN(-CAM_HULL_OFFSET,-CAM_HULL_OFFSET,-CAM_HULL_OFFSET);
static Vector CAM_HULL_MAX( CAM_HULL_OFFSET, CAM_HULL_OFFSET, CAM_HULL_OFFSET);

#ifdef CLIENT_DLL

#include "input.h"


extern const ConVar *sv_cheats;

extern ConVar cam_idealdist;
extern ConVar cam_idealdistright;
extern ConVar cam_idealdistup;

void CAM_ToThirdPerson(void);
void CAM_ToFirstPerson(void);

void ToggleThirdPerson( bool bValue )
{
	if ( bValue == true )
	{
		CAM_ToThirdPerson();
	}
	else
	{
		CAM_ToFirstPerson();
	}
}

void ThirdPersonChange( IConVar *pConVar, const char *pOldValue, float flOldValue )
{
	ConVarRef var( pConVar );

	ToggleThirdPerson( var.GetBool() );
}

// HL2SB: dropped FCVAR_DEVELOPMENTONLY so the convar is always registered and the
// `thirdperson` / `firstperson` commands (game/client/in_camera.cpp) can drive it
// through ConVarRef. Default stays 0: the game starts in first person, and the commands
// switch the *preference* so it stays consistent with CThirdPersonManager::Update().
ConVar cl_thirdperson( "cl_thirdperson", "0", FCVAR_NOT_CONNECTED | FCVAR_USERINFO | FCVAR_ARCHIVE, "Enables/Disables third person", ThirdPersonChange );

#endif

void CThirdPersonManager::Init( void )
{
	m_bOverrideThirdPerson = false;
	m_bForced = false;
	m_flUpFraction = 0.0f;
	m_flFraction = 1.0f;

	m_flUpLerpTime = 0.0f;
	m_flLerpTime = 0.0f;

	m_flUpOffset = CAMERA_UP_OFFSET;

	if ( input )
	{
		input->CAM_SetCameraThirdData( NULL, vec3_angle );
	}
}

void CThirdPersonManager::Update( void )
{

#ifdef CLIENT_DLL
	if ( !sv_cheats )
	{
		sv_cheats = cvar->FindVar( "sv_cheats" );
	}

	// If cheats have been disabled, pull us back out of third-person view.
	if ( sv_cheats && !sv_cheats->GetBool() && GameRules() && GameRules()->AllowThirdPersonCamera() == false )
	{
		if ( (bool)input->CAM_IsThirdPerson() == true )
		{
			input->CAM_ToFirstPerson();
		}
		return;
	}

	if ( IsOverridingThirdPerson() == false )
	{
		if ( (bool)input->CAM_IsThirdPerson() != ( cl_thirdperson.GetBool() || m_bForced ) && GameRules() && GameRules()->AllowThirdPersonCamera() == true )
		{
			ToggleThirdPerson( m_bForced || cl_thirdperson.GetBool() );
		}
	}
#endif

}

Vector CThirdPersonManager::GetDesiredCameraOffset( void )
{ 
	if ( IsOverridingThirdPerson() == true )
	{
		return Vector( cam_idealdist.GetFloat(), cam_idealdistright.GetFloat(), cam_idealdistup.GetFloat() );
	}

	Vector vecOffset = m_vecDesiredCameraOffset;

#ifdef CLIENT_DLL
	// HL2SB: in this build the desired camera offset is never actually set - the only
	// writer in the whole tree is CInput::CAM_ToFirstPerson(), which zeroes it
	// (in_camera.cpp:699). So the third-person offset was always (0,0,0) and
	// ClientModeShared::OverrideView subtracted nothing: the camera stayed at the eye,
	// inside the player's own head, both on foot and in a vehicle (exactly the reported
	// "third person is on but it is a zoomed-in view"). Derive it from the standard
	// cam_ideal* cvars instead - the same ones the engine's own third-person controls
	// (cammousemove / +camdistance) drive, so those keep working.
	if ( vecOffset[ DIST_FORWARD ] <= 0.0f )
	{
		vecOffset = Vector( cam_idealdist.GetFloat(), cam_idealdistright.GetFloat(), cam_idealdistup.GetFloat() );
	}

	// GMod derives the vehicle camera distance from the vehicle's render bounds
	// ((mn - mx):Length()); an HL2 vehicle is far bigger than the stock 150 unit
	// cam_idealdist, so in a vehicle the camera is pushed out far enough to clear the
	// car. On foot nothing changes and cam_idealdist still rules.
	C_BasePlayer *pLocalPlayer = C_BasePlayer::GetLocalPlayer();
	if ( pLocalPlayer && pLocalPlayer->GetVehicle() && vecOffset[ DIST_FORWARD ] < 280.0f )
	{
		vecOffset[ DIST_FORWARD ] = 280.0f;
	}
#endif

	return vecOffset; 
}

Vector CThirdPersonManager::GetFinalCameraOffset( void )
{
	Vector vDesired = GetDesiredCameraOffset();

	if ( m_flUpFraction != 1.0f )
	{
		vDesired.z += m_flUpOffset;
	}

	return vDesired;

}

Vector CThirdPersonManager::GetDistanceFraction( void )
{
	if ( IsOverridingThirdPerson() == true )
	{
		return Vector( m_flTargetFraction, m_flTargetFraction, m_flTargetFraction );
	}

	float flFraction = m_flFraction;
	float flUpFraction = m_flUpFraction;

	float flFrac = RemapValClamped( gpGlobals->curtime - m_flLerpTime, 0, CAMERA_OFFSET_LERP_TIME, 0, 1 );

	flFraction = Lerp( flFrac, m_flFraction, m_flTargetFraction );

	if ( flFrac == 1.0f )
	{
		m_flFraction = m_flTargetFraction;
	}

	flFrac = RemapValClamped( gpGlobals->curtime - m_flUpLerpTime, 0, CAMERA_UP_OFFSET_LERP_TIME, 0, 1 );

	flUpFraction = 1.0f - Lerp( flFrac, m_flUpFraction, m_flTargetUpFraction );

	if ( flFrac == 1.0f )
	{
		m_flUpFraction = m_flTargetUpFraction;
	}

	return Vector( flFraction, flFraction, flUpFraction );
}

void CThirdPersonManager::PositionCamera( CBasePlayer *pPlayer, QAngle angles )
{
	if ( pPlayer )
	{
		trace_t trace;

		Vector camForward, camRight, camUp;

		// find our player's origin, and from there, the eye position
		Vector origin = pPlayer->GetLocalOrigin();
		origin += pPlayer->GetViewOffset();

		AngleVectors( angles, &camForward, &camRight, &camUp );
	
		Vector endPos = origin;

		Vector vecCamOffset = endPos + (camForward * - GetDesiredCameraOffset()[DIST_FORWARD]) + (camRight * GetDesiredCameraOffset()[ DIST_RIGHT ]) + (camUp * GetDesiredCameraOffset()[ DIST_UP ] );

		// HL2SB: in a vehicle the camera trace must ignore props and vehicles, or it
		// collides with the car it rides in, GetDistanceFraction() collapses to a
		// fraction of cam_idealdist and the view stays at the driver's back - the
		// reported "thirdperson is on but the view is still inside the jeep". This is
		// GMod's GM:CalcVehicleView filter (which drops prop_physics / prop_dynamic /
		// phys_bone_follower / vehicles) expressed as a contents mask: world and brush
		// geometry only. On foot the stock behaviour is untouched.
		unsigned int nCameraMask = MASK_SOLID & ~CONTENTS_MONSTER;
#ifdef CLIENT_DLL
		if ( pPlayer->GetVehicle() )
		{
			nCameraMask = MASK_SOLID_BRUSHONLY;
		}
#endif

		// use our previously #defined hull to collision trace
		CTraceFilterSimple traceFilter( pPlayer, COLLISION_GROUP_NONE );
		UTIL_TraceHull( endPos, vecCamOffset, CAM_HULL_MIN, CAM_HULL_MAX, nCameraMask, &traceFilter, &trace );
		
		if ( trace.fraction != m_flTargetFraction )
		{
			m_flLerpTime = gpGlobals->curtime;
		}

		m_flTargetFraction = trace.fraction;
		m_flTargetUpFraction = 1.0f;

		//If we're getting closer to a wall snap the fraction right away.
		if ( m_flTargetFraction < m_flFraction )
		{
			m_flFraction = m_flTargetFraction;
			m_flLerpTime = gpGlobals->curtime;
		}
	

		// move the camera closer if it hit something
		if( trace.fraction < 1.0  )
		{
			m_vecCameraOffset[ DIST ] *= trace.fraction;

			UTIL_TraceHull( endPos, endPos + (camForward * - GetDesiredCameraOffset()[DIST_FORWARD]), CAM_HULL_MIN, CAM_HULL_MAX, nCameraMask, &traceFilter, &trace );

			if ( trace.fraction != 1.0f )
			{
				if ( trace.fraction != m_flTargetUpFraction )
				{
					m_flUpLerpTime = gpGlobals->curtime;
				}

				m_flTargetUpFraction = trace.fraction;

				if ( m_flTargetUpFraction < m_flUpFraction )
				{
					m_flUpFraction = trace.fraction;
					m_flUpLerpTime = gpGlobals->curtime;
				}
			}
		}
	}
}

bool CThirdPersonManager::WantToUseGameThirdPerson( void )
{
	return cl_thirdperson.GetBool() && GameRules() && GameRules()->AllowThirdPersonCamera() && IsOverridingThirdPerson() == false;
}


CThirdPersonManager g_ThirdPersonManager;

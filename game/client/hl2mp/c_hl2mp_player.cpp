//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose:		Player for HL2.
//
//=============================================================================//

#include "cbase.h"
#include "vcollide_parse.h"
#include "c_hl2mp_player.h"
#include "view.h"
#include "takedamageinfo.h"
#include "hl2mp_gamerules.h"
#include "in_buttons.h"
#include "iviewrender_beams.h"			// flashlight beam
#include "r_efx.h"
#include "dlight.h"
#include "prop_portal_shared.h"
#include "ivieweffects.h"		// for screenshake
#include "portal_shareddefs.h"
#include "PortalRender.h"
#include "ScreenSpaceEffects.h"
#include "toolframework/itoolframework.h"
#include "toolframework_client.h"
#include "c_cs_playerresource.h"
#include "cs_blackmarket.h"
#include "hl2mp_player_shared.h"
#include "vgui_controls/Controls.h"
#include "vgui/IScheme.h"
#include "steam/steam_api.h"
#include "c_basetempentity.h"

// Don't alias here
#if defined( CHL2MP_Player )
#undef CHL2MP_Player	
#endif

#define REORIENTATION_RATE 120.0f
#define REORIENTATION_ACCELERATION_RATE 400.0f

#define ENABLE_PORTAL_EYE_INTERPOLATION_CODE

LINK_ENTITY_TO_CLASS( player, C_HL2MP_Player );

IMPLEMENT_CLIENTCLASS_DT(C_HL2MP_Player, DT_HL2MP_Player, CHL2MP_Player)
	RecvPropFloat( RECVINFO( m_angEyeAngles[0] ) ),
	RecvPropFloat( RECVINFO( m_angEyeAngles[1] ) ),
	RecvPropEHandle( RECVINFO( m_hRagdoll ) ),
	RecvPropInt( RECVINFO( m_iSpawnInterpCounter ) ),
	RecvPropInt( RECVINFO( m_iPlayerSoundType) ),

	RecvPropBool(RECVINFO(m_bHeldObjectOnOppositeSideOfPortal)),
	RecvPropEHandle(RECVINFO(m_pHeldObjectPortal)),
	RecvPropBool(RECVINFO(m_bPitchReorientation)),
	RecvPropEHandle(RECVINFO(m_hPortalEnvironment)),
END_RECV_TABLE()

BEGIN_PREDICTION_DATA( C_HL2MP_Player )
	DEFINE_PRED_FIELD( m_hPortalEnvironment, FIELD_EHANDLE, FTYPEDESC_INSENDTABLE ),
END_PREDICTION_DATA()


class C_TEPlayerAnimEvent : public C_BaseTempEntity
{
public:
	DECLARE_CLASS(C_TEPlayerAnimEvent, C_BaseTempEntity);
	DECLARE_CLIENTCLASS();

	virtual void PostDataUpdate(DataUpdateType_t updateType)
	{
		// Create the effect.
		C_HL2MP_Player* pPlayer = dynamic_cast<C_HL2MP_Player*>(m_hPlayer.Get());
		if (pPlayer && !pPlayer->IsDormant())
		{
			pPlayer->DoAnimationEvent((PlayerAnimEvent_t)m_iEvent.Get(), m_nData);
		}
	}

public:
	CNetworkHandle(CBasePlayer, m_hPlayer);
	CNetworkVar(int, m_iEvent);
	CNetworkVar(int, m_nData);
};

IMPLEMENT_CLIENTCLASS_EVENT(C_TEPlayerAnimEvent, DT_TEPlayerAnimEvent, CTEPlayerAnimEvent);

BEGIN_RECV_TABLE_NOBASE(C_TEPlayerAnimEvent, DT_TEPlayerAnimEvent)
RecvPropEHandle(RECVINFO(m_hPlayer)),
RecvPropInt(RECVINFO(m_iEvent)),
RecvPropInt(RECVINFO(m_nData))
END_RECV_TABLE()


#define	HL2_WALK_SPEED hl2_walkspeed->GetFloat()
#define	HL2_NORM_SPEED hl2_normspeed->GetFloat()
#define	HL2_SPRINT_SPEED hl2_sprintspeed->GetFloat()

static ConVar cl_playermodel( "cl_playermodel", "none", FCVAR_USERINFO | FCVAR_ARCHIVE | FCVAR_SERVER_CAN_EXECUTE, "Default Player Model");
static ConVar cl_defaultweapon( "cl_defaultweapon", "weapon_physcannon", FCVAR_USERINFO | FCVAR_ARCHIVE, "Default Spawn Weapon");
ConVar cl_reorient_in_air("cl_reorient_in_air", "1", FCVAR_ARCHIVE, "Allows the player to only reorient from being upside down while in the air.");

extern bool g_bUpsideDown;

void SpawnBlood (Vector vecSpot, const Vector &vecDir, int bloodColor, float flDamage);

C_HL2MP_Player::C_HL2MP_Player() : m_PlayerAnimState( this ), m_iv_angEyeAngles( "C_HL2MP_Player::m_iv_angEyeAngles" )
{
	m_iIDEntIndex = 0;
	m_iSpawnInterpCounterCache = 0;

	m_angEyeAngles.Init();

	AddVar( &m_angEyeAngles, &m_iv_angEyeAngles, LATCH_SIMULATION_VAR );

	m_EntClientFlags |= ENTCLIENTFLAG_DONTUSEIK;
	m_blinkTimer.Invalidate();

	m_pFlashlightBeam = NULL;
}

C_HL2MP_Player::~C_HL2MP_Player( void )
{
	ReleaseFlashlight();
}

int C_HL2MP_Player::GetIDTarget() const
{
	return m_iIDEntIndex;
}

//-----------------------------------------------------------------------------
// Purpose: Update this client's target entity
//-----------------------------------------------------------------------------
void C_HL2MP_Player::UpdateIDTarget()
{
	if ( !IsLocalPlayer() )
		return;

	// Clear old target and find a new one
	m_iIDEntIndex = 0;

	// don't show IDs in chase spec mode
	if ( GetObserverMode() == OBS_MODE_CHASE || 
		 GetObserverMode() == OBS_MODE_DEATHCAM )
		 return;

	trace_t tr;
	Vector vecStart, vecEnd;
	VectorMA( MainViewOrigin(), 1500, MainViewForward(), vecEnd );
	VectorMA( MainViewOrigin(), 10,   MainViewForward(), vecStart );
	UTIL_TraceLine( vecStart, vecEnd, MASK_SOLID, this, COLLISION_GROUP_NONE, &tr );

	if ( !tr.startsolid && tr.DidHitNonWorldEntity() )
	{
		C_BaseEntity *pEntity = tr.m_pEnt;

		if ( pEntity && (pEntity != this) )
		{
			m_iIDEntIndex = pEntity->entindex();
		}
	}
}

void C_HL2MP_Player::TraceAttack( const CTakeDamageInfo &info, const Vector &vecDir, trace_t *ptr, CDmgAccumulator *pAccumulator )
{
	Vector vecOrigin = ptr->endpos - vecDir * 4;

	float flDistance = 0.0f;
	
	if ( info.GetAttacker() )
	{
		flDistance = (ptr->endpos - info.GetAttacker()->GetAbsOrigin()).Length();
	}

	if ( m_takedamage )
	{
		AddMultiDamage( info, this );

		int blood = BloodColor();
		
		CBaseEntity *pAttacker = info.GetAttacker();

		if ( pAttacker )
		{
			if ( HL2MPRules()->IsTeamplay() && pAttacker->InSameTeam( this ) == true )
				return;
		}

		if ( blood != DONT_BLEED )
		{
			SpawnBlood( vecOrigin, vecDir, blood, flDistance );// a little surface blood.
			TraceBleed( flDistance, vecDir, ptr, info.GetDamageType() );
		}
	}
}


C_HL2MP_Player* C_HL2MP_Player::GetLocalHL2MPPlayer()
{
	return (C_HL2MP_Player*)C_BasePlayer::GetLocalPlayer();
}

void C_HL2MP_Player::Initialize( void )
{
	m_headYawPoseParam = LookupPoseParameter( "head_yaw" );
	GetPoseParameterRange( m_headYawPoseParam, m_headYawMin, m_headYawMax );

	m_headPitchPoseParam = LookupPoseParameter( "head_pitch" );
	GetPoseParameterRange( m_headPitchPoseParam, m_headPitchMin, m_headPitchMax );

	CStudioHdr *hdr = GetModelPtr();
	if (!hdr)
		return;
	for ( int i = 0; i < hdr->GetNumPoseParameters() ; i++ )
	{
		SetPoseParameter( hdr, i, 0.0 );
	}
}

CStudioHdr *C_HL2MP_Player::OnNewModel( void )
{
	CStudioHdr *hdr = BaseClass::OnNewModel();
	
	Initialize( );

	return hdr;
}

//-----------------------------------------------------------------------------
/**
 * Orient head and eyes towards m_lookAt.
 */
void C_HL2MP_Player::UpdateLookAt( void )
{
	// head yaw
	if (m_headYawPoseParam < 0 || m_headPitchPoseParam < 0)
		return;

	// orient eyes
	m_viewtarget = m_vLookAtTarget;

	// blinking
	if (m_blinkTimer.IsElapsed())
	{
		m_blinktoggle = !m_blinktoggle;
		m_blinkTimer.Start( RandomFloat( 1.5f, 4.0f ) );
	}

	// Figure out where we want to look in world space.
	QAngle desiredAngles;
	Vector to = m_vLookAtTarget - EyePosition();
	VectorAngles( to, desiredAngles );

	// Figure out where our body is facing in world space.
	QAngle bodyAngles( 0, 0, 0 );
	bodyAngles[YAW] = GetLocalAngles()[YAW];


	float flBodyYawDiff = bodyAngles[YAW] - m_flLastBodyYaw;
	m_flLastBodyYaw = bodyAngles[YAW];
	

	// Set the head's yaw.
	float desired = AngleNormalize( desiredAngles[YAW] - bodyAngles[YAW] );
	desired = clamp( desired, m_headYawMin, m_headYawMax );
	m_flCurrentHeadYaw = ApproachAngle( desired, m_flCurrentHeadYaw, 130 * gpGlobals->frametime );

	// Counterrotate the head from the body rotation so it doesn't rotate past its target.
	m_flCurrentHeadYaw = AngleNormalize( m_flCurrentHeadYaw - flBodyYawDiff );
	desired = clamp( desired, m_headYawMin, m_headYawMax );
	
	SetPoseParameter( m_headYawPoseParam, m_flCurrentHeadYaw );

	
	// Set the head's yaw.
	desired = AngleNormalize( desiredAngles[PITCH] );
	desired = clamp( desired, m_headPitchMin, m_headPitchMax );
	
	m_flCurrentHeadPitch = ApproachAngle( desired, m_flCurrentHeadPitch, 130 * gpGlobals->frametime );
	m_flCurrentHeadPitch = AngleNormalize( m_flCurrentHeadPitch );
	SetPoseParameter( m_headPitchPoseParam, m_flCurrentHeadPitch );
}

void C_HL2MP_Player::ClientThink( void )
{
	bool bFoundViewTarget = false;
	
	Vector vForward;
	AngleVectors( GetLocalAngles(), &vForward );

	FixTeleportationRoll();

	for( int iClient = 1; iClient <= gpGlobals->maxClients; ++iClient )
	{
		CBaseEntity *pEnt = UTIL_PlayerByIndex( iClient );
		if(!pEnt || !pEnt->IsPlayer())
			continue;

		if ( pEnt->entindex() == entindex() )
			continue;

		Vector vTargetOrigin = pEnt->GetAbsOrigin();
		Vector vMyOrigin =  GetAbsOrigin();

		Vector vDir = vTargetOrigin - vMyOrigin;
		
		if ( vDir.Length() > 128 ) 
			continue;

		VectorNormalize( vDir );

		if ( DotProduct( vForward, vDir ) < 0.0f )
			 continue;

		m_vLookAtTarget = pEnt->EyePosition();
		bFoundViewTarget = true;
		break;
	}

	if ( bFoundViewTarget == false )
	{
		m_vLookAtTarget = GetAbsOrigin() + vForward * 512;
	}

	UpdateIDTarget();
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
int C_HL2MP_Player::DrawModel( int flags )
{
	if ( !m_bReadyToDraw )
		return 0;

    return BaseClass::DrawModel(flags);
}

//-----------------------------------------------------------------------------
// Should this object receive shadows?
//-----------------------------------------------------------------------------
bool C_HL2MP_Player::ShouldReceiveProjectedTextures( int flags )
{
	Assert( flags & SHADOW_FLAGS_PROJECTED_TEXTURE_TYPE_MASK );

	if ( IsEffectActive( EF_NODRAW ) )
		 return false;

	if( flags & SHADOW_FLAGS_FLASHLIGHT )
	{
		return true;
	}

	return BaseClass::ShouldReceiveProjectedTextures( flags );
}

void C_HL2MP_Player::DoImpactEffect( trace_t &tr, int nDamageType )
{
	if ( GetActiveWeapon() )
	{
		GetActiveWeapon()->DoImpactEffect( tr, nDamageType );
		return;
	}

	BaseClass::DoImpactEffect( tr, nDamageType );
}

void C_HL2MP_Player::PreThink( void )
{
	QAngle vTempAngles = GetLocalAngles();

	if ( GetLocalPlayer() == this )
	{
		vTempAngles[PITCH] = EyeAngles()[PITCH];
	}
	else
	{
		vTempAngles[PITCH] = m_angEyeAngles[PITCH];
	}

	if ( vTempAngles[YAW] < 0.0f )
	{
		vTempAngles[YAW] += 360.0f;
	}

	SetLocalAngles( vTempAngles );

	BaseClass::PreThink();

	HandleSpeedChanges();

	if ( m_HL2Local.m_flSuitPower <= 0.0f )
	{
		if( IsSprinting() )
		{
			StopSprinting();
		}
	}
}

const QAngle &C_HL2MP_Player::EyeAngles()
{
	if( IsLocalPlayer() )
	{
		return BaseClass::EyeAngles();
	}
	else
	{
		return m_angEyeAngles;
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void C_HL2MP_Player::AddEntity( void )
{
	BaseClass::AddEntity();

	QAngle vTempAngles = GetLocalAngles();
	vTempAngles[PITCH] = m_angEyeAngles[PITCH];

	SetLocalAngles( vTempAngles );
		
	m_PlayerAnimState.Update();

	// Zero out model pitch, blending takes care of all of it.
	SetLocalAnglesDim( X_INDEX, 0 );

	if( this != C_BasePlayer::GetLocalPlayer() )
	{
		if ( IsEffectActive( EF_DIMLIGHT ) )
		{
			int iAttachment = LookupAttachment( "anim_attachment_RH" );

			if ( iAttachment < 0 )
				return;

			Vector vecOrigin;
			QAngle eyeAngles = m_angEyeAngles;
	
			GetAttachment( iAttachment, vecOrigin, eyeAngles );

			Vector vForward;
			AngleVectors( eyeAngles, &vForward );
				
			trace_t tr;
			UTIL_TraceLine( vecOrigin, vecOrigin + (vForward * 200), MASK_SHOT, this, COLLISION_GROUP_NONE, &tr );

			if( !m_pFlashlightBeam )
			{
				BeamInfo_t beamInfo;
				beamInfo.m_nType = TE_BEAMPOINTS;
				beamInfo.m_vecStart = tr.startpos;
				beamInfo.m_vecEnd = tr.endpos;
				beamInfo.m_pszModelName = "sprites/glow01.vmt";
				beamInfo.m_pszHaloName = "sprites/glow01.vmt";
				beamInfo.m_flHaloScale = 3.0;
				beamInfo.m_flWidth = 8.0f;
				beamInfo.m_flEndWidth = 35.0f;
				beamInfo.m_flFadeLength = 300.0f;
				beamInfo.m_flAmplitude = 0;
				beamInfo.m_flBrightness = 60.0;
				beamInfo.m_flSpeed = 0.0f;
				beamInfo.m_nStartFrame = 0.0;
				beamInfo.m_flFrameRate = 0.0;
				beamInfo.m_flRed = 255.0;
				beamInfo.m_flGreen = 255.0;
				beamInfo.m_flBlue = 255.0;
				beamInfo.m_nSegments = 8;
				beamInfo.m_bRenderable = true;
				beamInfo.m_flLife = 0.5;
				beamInfo.m_nFlags = FBEAM_FOREVER | FBEAM_ONLYNOISEONCE | FBEAM_NOTILE | FBEAM_HALOBEAM;
				
				m_pFlashlightBeam = beams->CreateBeamPoints( beamInfo );
			}

			if( m_pFlashlightBeam )
			{
				BeamInfo_t beamInfo;
				beamInfo.m_vecStart = tr.startpos;
				beamInfo.m_vecEnd = tr.endpos;
				beamInfo.m_flRed = 255.0;
				beamInfo.m_flGreen = 255.0;
				beamInfo.m_flBlue = 255.0;

				beams->UpdateBeamInfo( m_pFlashlightBeam, beamInfo );

				dlight_t *el = effects->CL_AllocDlight( 0 );
				el->origin = tr.endpos;
				el->radius = 50; 
				el->color.r = 200;
				el->color.g = 200;
				el->color.b = 200;
				el->die = gpGlobals->curtime + 0.1;
			}
		}
		else if ( m_pFlashlightBeam )
		{
			ReleaseFlashlight();
		}
	}
}

ShadowType_t C_HL2MP_Player::ShadowCastType( void ) 
{
	if ( !IsVisible() )
		 return SHADOWS_NONE;

	return SHADOWS_RENDER_TO_TEXTURE_DYNAMIC;
}


const QAngle& C_HL2MP_Player::GetRenderAngles()
{
	if ( IsRagdoll() )
	{
		return vec3_angle;
	}
	else
	{
		return m_PlayerAnimState.GetRenderAngles();
	}
}

bool C_HL2MP_Player::ShouldDraw( void )
{
	// If we're dead, our ragdoll will be drawn for us instead.
	if ( !IsAlive() )
		return false;

//	if( GetTeamNumber() == TEAM_SPECTATOR )
//		return false;

	if( IsLocalPlayer() && IsRagdoll() )
		return true;
	
	if ( IsRagdoll() )
		return false;

	return BaseClass::ShouldDraw();
}

void C_HL2MP_Player::NotifyShouldTransmit( ShouldTransmitState_t state )
{
	if ( state == SHOULDTRANSMIT_END )
	{
		if( m_pFlashlightBeam != NULL )
		{
			ReleaseFlashlight();
		}
	}

	BaseClass::NotifyShouldTransmit( state );
}

void C_HL2MP_Player::OnDataChanged( DataUpdateType_t type )
{
	BaseClass::OnDataChanged( type );

	if ( type == DATA_UPDATE_CREATED )
	{
		SetNextClientThink( CLIENT_THINK_ALWAYS );
	}

	UpdateVisibility();
}

void C_HL2MP_Player::PostDataUpdate( DataUpdateType_t updateType )
{
	if ( m_iSpawnInterpCounter != m_iSpawnInterpCounterCache )
	{
		MoveToLastReceivedPosition( true );
		ResetLatched();
		m_iSpawnInterpCounterCache = m_iSpawnInterpCounter;
	}

	BaseClass::PostDataUpdate( updateType );
}

void C_HL2MP_Player::ReleaseFlashlight( void )
{
	if( m_pFlashlightBeam )
	{
		m_pFlashlightBeam->flags = 0;
		m_pFlashlightBeam->die = gpGlobals->curtime - 1;

		m_pFlashlightBeam = NULL;
	}
}

float C_HL2MP_Player::GetFOV( void )
{
	//Find our FOV with offset zoom value
	float flFOVOffset = C_BasePlayer::GetFOV() + GetZoom();

	// Clamp FOV in MP
	int min_fov = GetMinFOV();
	
	// Don't let it go too low
	flFOVOffset = MAX( min_fov, flFOVOffset );

	return flFOVOffset;
}

//=========================================================
// Autoaim
// set crosshair position to point to enemey
//=========================================================
Vector C_HL2MP_Player::GetAutoaimVector( float flDelta )
{
	// Never autoaim a predicted weapon (for now)
	Vector	forward;
	AngleVectors( EyeAngles() + m_Local.m_vecPunchAngle, &forward );
	return	forward;
}

//-----------------------------------------------------------------------------
// Purpose: Returns whether or not we are allowed to sprint now.
//-----------------------------------------------------------------------------
bool C_HL2MP_Player::CanSprint( void )
{
	return ( (!m_Local.m_bDucked && !m_Local.m_bDucking) && (GetWaterLevel() != 3) );
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void C_HL2MP_Player::StartSprinting( void )
{
	static const ConVar* hl2_sprintspeed = g_pCVar->FindVar("sb_sprintspeed");
	if( m_HL2Local.m_flSuitPower < 10 )
	{
		// Don't sprint unless there's a reasonable
		// amount of suit power.
		CPASAttenuationFilter filter( this );
		filter.UsePredictionRules();
		EmitSound( filter, entindex(), "HL2Player.SprintNoPower" );
		return;
	}

	CPASAttenuationFilter filter( this );
	filter.UsePredictionRules();
	EmitSound( filter, entindex(), "HL2Player.SprintStart" );

	SetMaxSpeed( HL2_SPRINT_SPEED );
	m_fIsSprinting = true;
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void C_HL2MP_Player::StopSprinting( void )
{
	static const ConVar* hl2_normspeed = g_pCVar->FindVar("sb_normspeed");
	SetMaxSpeed( HL2_NORM_SPEED );
	m_fIsSprinting = false;
}

void C_HL2MP_Player::HandleSpeedChanges( void )
{
	int buttonsChanged = m_afButtonPressed | m_afButtonReleased;

	if( buttonsChanged & IN_SPEED )
	{
		// The state of the sprint/run button has changed.
		if ( IsSuitEquipped() )
		{
			if ( !(m_afButtonPressed & IN_SPEED)  && IsSprinting() )
			{
				StopSprinting();
			}
			else if ( (m_afButtonPressed & IN_SPEED) && !IsSprinting() )
			{
				if ( CanSprint() )
				{
					StartSprinting();
				}
				else
				{
					// Reset key, so it will be activated post whatever is suppressing it.
					m_nButtons &= ~IN_SPEED;
				}
			}
		}
	}
	else if( buttonsChanged & IN_WALK )
	{
		if ( IsSuitEquipped() )
		{
			// The state of the WALK button has changed. 
			if( IsWalking() && !(m_afButtonPressed & IN_WALK) )
			{
				StopWalking();
			}
			else if( !IsWalking() && !IsSprinting() && (m_afButtonPressed & IN_WALK) && !(m_nButtons & IN_DUCK) )
			{
				StartWalking();
			}
		}
	}

	if ( IsSuitEquipped() && m_fIsWalking && !(m_nButtons & IN_WALK)  ) 
		StopWalking();
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void C_HL2MP_Player::StartWalking( void )
{

	static const ConVar* hl2_walkspeed = g_pCVar->FindVar("sb_walkspeed");
	SetMaxSpeed( HL2_WALK_SPEED );
	m_fIsWalking = true;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void C_HL2MP_Player::StopWalking( void )
{
	static const ConVar* hl2_normspeed = g_pCVar->FindVar("sb_normspeed");
	SetMaxSpeed( HL2_NORM_SPEED );
	m_fIsWalking = false;
}

void C_HL2MP_Player::ItemPreFrame( void )
{
	if ( GetFlags() & FL_FROZEN )
		 return;

	// Disallow shooting while zooming
	if ( m_nButtons & IN_ZOOM )
	{
		//FIXME: Held weapons like the grenade get sad when this happens
		m_nButtons &= ~(IN_ATTACK|IN_ATTACK2);
	}

	BaseClass::ItemPreFrame();

}
	
void C_HL2MP_Player::ItemPostFrame( void )
{
	if ( GetFlags() & FL_FROZEN )
		 return;

	BaseClass::ItemPostFrame();
}

C_BaseAnimating *C_HL2MP_Player::BecomeRagdollOnClient()
{
	// Let the C_CSRagdoll entity do this.
	// m_builtRagdoll = true;
	return NULL;
}

bool C_HL2MP_Player::DetectAndHandlePortalTeleportation(void)
{
	if (m_bPortalledMessagePending)
	{
		m_bPortalledMessagePending = false;

		//C_Prop_Portal *pOldPortal = PreDataChanged_Backup.m_hPortalEnvironment.Get();
		//Assert( pOldPortal );
		//if( pOldPortal )
		{
			Vector ptNewPosition = GetNetworkOrigin();

			UTIL_Portal_PointTransform(m_PendingPortalMatrix, PortalEyeInterpolation.m_vEyePosition_Interpolated, PortalEyeInterpolation.m_vEyePosition_Interpolated);
			UTIL_Portal_PointTransform(m_PendingPortalMatrix, PortalEyeInterpolation.m_vEyePosition_Uninterpolated, PortalEyeInterpolation.m_vEyePosition_Uninterpolated);

			PortalEyeInterpolation.m_bEyePositionIsInterpolating = true;

			UTIL_Portal_AngleTransform(m_PendingPortalMatrix, m_qEyeAngles_LastCalcView, m_angEyeAngles);
			m_angEyeAngles.x = AngleNormalize(m_angEyeAngles.x);
			m_angEyeAngles.y = AngleNormalize(m_angEyeAngles.y);
			m_angEyeAngles.z = AngleNormalize(m_angEyeAngles.z);
			m_iv_angEyeAngles.Reset(); //copies from m_angEyeAngles

			if (engine->IsPlayingDemo())
			{
				pl.v_angle = m_angEyeAngles;
				engine->SetViewAngles(pl.v_angle);
			}

			engine->ResetDemoInterpolation();
			if (IsLocalPlayer())
			{
				//DevMsg( "FPT: %.2f %.2f %.2f\n", m_angEyeAngles.x, m_angEyeAngles.y, m_angEyeAngles.z );
				SetLocalAngles(m_angEyeAngles);
			}

			//m_PlayerAnimState->Teleport(&ptNewPosition, &GetNetworkAngles(), this);

			// Reorient last facing direction to fix pops in view model lag
			for (int i = 0; i < MAX_VIEWMODELS; i++)
			{
				CBaseViewModel* vm = GetViewModel(i);
				if (!vm)
					continue;

				UTIL_Portal_VectorTransform(m_PendingPortalMatrix, vm->m_vecLastFacing, vm->m_vecLastFacing);
			}
		}
		m_bPortalledMessagePending = false;
	}

	return false;
}

void C_HL2MP_Player::UpdatePortalEyeInterpolation(void)
{
#ifdef ENABLE_PORTAL_EYE_INTERPOLATION_CODE
	//PortalEyeInterpolation.m_bEyePositionIsInterpolating = false;
	if (PortalEyeInterpolation.m_bUpdatePosition_FreeMove)
	{
		PortalEyeInterpolation.m_bUpdatePosition_FreeMove = false;

		C_Prop_Portal* pOldPortal = PreDataChanged_Backup.m_hPortalEnvironment.Get();
		if (pOldPortal)
		{
			UTIL_Portal_PointTransform(pOldPortal->MatrixThisToLinked(), PortalEyeInterpolation.m_vEyePosition_Interpolated, PortalEyeInterpolation.m_vEyePosition_Interpolated);
			//PortalEyeInterpolation.m_vEyePosition_Interpolated = pOldPortal->m_matrixThisToLinked * PortalEyeInterpolation.m_vEyePosition_Interpolated;

			//Vector vForward;
			//m_hPortalEnvironment.Get()->GetVectors( &vForward, NULL, NULL );

			PortalEyeInterpolation.m_vEyePosition_Interpolated = EyeFootPosition();

			PortalEyeInterpolation.m_bEyePositionIsInterpolating = true;
		}
	}

	if (IsInAVehicle())
		PortalEyeInterpolation.m_bEyePositionIsInterpolating = false;

	if (!PortalEyeInterpolation.m_bEyePositionIsInterpolating)
	{
		PortalEyeInterpolation.m_vEyePosition_Uninterpolated = EyeFootPosition();
		PortalEyeInterpolation.m_vEyePosition_Interpolated = PortalEyeInterpolation.m_vEyePosition_Uninterpolated;
		return;
	}

	Vector vThisFrameUninterpolatedPosition = EyeFootPosition();

	//find offset between this and last frame's uninterpolated movement, and apply this as freebie movement to the interpolated position
	PortalEyeInterpolation.m_vEyePosition_Interpolated += (vThisFrameUninterpolatedPosition - PortalEyeInterpolation.m_vEyePosition_Uninterpolated);
	PortalEyeInterpolation.m_vEyePosition_Uninterpolated = vThisFrameUninterpolatedPosition;

	Vector vDiff = vThisFrameUninterpolatedPosition - PortalEyeInterpolation.m_vEyePosition_Interpolated;
	float fLength = vDiff.Length();
	float fFollowSpeed = gpGlobals->frametime * 100.0f;
	const float fMaxDiff = 150.0f;
	if (fLength > fMaxDiff)
	{
		//camera lagging too far behind, give it a speed boost to bring it within maximum range
		fFollowSpeed = fLength - fMaxDiff;
	}
	else if (fLength < fFollowSpeed)
	{
		//final move
		PortalEyeInterpolation.m_bEyePositionIsInterpolating = false;
		PortalEyeInterpolation.m_vEyePosition_Interpolated = vThisFrameUninterpolatedPosition;
		return;
	}

	if (fLength > 0.001f)
	{
		vDiff *= (fFollowSpeed / fLength);
		PortalEyeInterpolation.m_vEyePosition_Interpolated += vDiff;
	}
	else
	{
		PortalEyeInterpolation.m_vEyePosition_Interpolated = vThisFrameUninterpolatedPosition;
	}



#else
	PortalEyeInterpolation.m_vEyePosition_Interpolated = BaseClass::EyePosition();
#endif
}

Vector C_HL2MP_Player::EyePosition()
{
	return PortalEyeInterpolation.m_vEyePosition_Interpolated;
}

Vector C_HL2MP_Player::EyeFootPosition(const QAngle& qEyeAngles)
{
	//interpolate between feet and normal eye position based on view roll (gets us wall/ceiling & ceiling/ceiling teleportations without an eye position pop)
	float fFootInterp = fabs(qEyeAngles[ROLL]) * ((1.0f / 180.0f) * 0.75f); //0 when facing straight up, 0.75 when facing straight down
	return (BaseClass::EyePosition() - (fFootInterp * m_vecViewOffset)); //TODO: Find a good Up vector for this rolled player and interpolate along actual eye/foot axis
}

void C_HL2MP_Player::CalcView( Vector &eyeOrigin, QAngle &eyeAngles, float &zNear, float &zFar, float &fov )
{
	DetectAndHandlePortalTeleportation();
	m_iForceNoDrawInPortalSurface = -1;
	bool bEyeTransform_Backup = m_bEyePositionIsTransformedByPortal;
	m_bEyePositionIsTransformedByPortal = false; //assume it's not transformed until it provably is
	UpdatePortalEyeInterpolation();

	QAngle qEyeAngleBackup = EyeAngles();
	Vector ptEyePositionBackup = EyePosition();
	C_Prop_Portal* pPortalBackup = m_hPortalEnvironment.Get();

	if ( m_lifeState != LIFE_ALIVE && !IsObserver() )
	{
		Vector origin = EyePosition();			

		IRagdoll *pRagdoll = GetRepresentativeRagdoll();

		if ( pRagdoll )
		{
			origin = pRagdoll->GetRagdollOrigin();
			origin.z += VEC_DEAD_VIEWHEIGHT_SCALED( this ).z; // look over ragdoll, not through
		}

		BaseClass::CalcView( eyeOrigin, eyeAngles, zNear, zFar, fov );

		eyeOrigin = origin;
		
		Vector vForward; 
		AngleVectors( eyeAngles, &vForward );

		VectorNormalize( vForward );
		VectorMA( origin, -CHASE_CAM_DISTANCE_MAX, vForward, eyeOrigin );

		Vector WALL_MIN( -WALL_OFFSET, -WALL_OFFSET, -WALL_OFFSET );
		Vector WALL_MAX( WALL_OFFSET, WALL_OFFSET, WALL_OFFSET );

		trace_t trace; // clip against world
		C_BaseEntity::PushEnableAbsRecomputations( false ); // HACK don't recompute positions while doing RayTrace
		UTIL_TraceHull( origin, eyeOrigin, WALL_MIN, WALL_MAX, MASK_SOLID_BRUSHONLY, this, COLLISION_GROUP_NONE, &trace );
		C_BaseEntity::PopEnableAbsRecomputations();

		if (trace.fraction < 1.0)
		{
			eyeOrigin = trace.endpos;
		}
		
		return;
	}
	m_qEyeAngles_LastCalcView = qEyeAngleBackup;
	m_ptEyePosition_LastCalcView = ptEyePositionBackup;
	m_pPortalEnvironment_LastCalcView = pPortalBackup;

	BaseClass::CalcView( eyeOrigin, eyeAngles, zNear, zFar, fov );
}

void C_HL2MP_Player::CalcPortalView(Vector& eyeOrigin, QAngle& eyeAngles)
{
	//although we already ran CalcPlayerView which already did these copies, they also fudge these numbers in ways we don't like, so recopy
	VectorCopy(EyePosition(), eyeOrigin);
	VectorCopy(EyeAngles(), eyeAngles);

	//Re-apply the screenshake (we just stomped it)
	vieweffects->ApplyShake(eyeOrigin, eyeAngles, 1.0);

	C_Prop_Portal* pPortal = m_hPortalEnvironment.Get();
	assert(pPortal);

	C_Prop_Portal* pRemotePortal = pPortal->m_hLinkedPortal;
	if (!pRemotePortal)
	{
		return; //no hacks possible/necessary
	}

	Vector ptPortalCenter;
	Vector vPortalForward;

	ptPortalCenter = pPortal->GetNetworkOrigin();
	pPortal->GetVectors(&vPortalForward, NULL, NULL);
	float fPortalPlaneDist = vPortalForward.Dot(ptPortalCenter);

	bool bOverrideSpecialEffects = false; //sometimes to get the best effect we need to kill other effects that are simply for cleanliness

	float fEyeDist = vPortalForward.Dot(eyeOrigin) - fPortalPlaneDist;
	bool bTransformEye = false;
	if (fEyeDist < 0.0f) //eye behind portal
	{
		if (pPortal->m_PortalSimulator.EntityIsInPortalHole(this)) //player standing in portal
		{
			bTransformEye = true;
		}
		else if (vPortalForward.z < -0.01f) //there's a weird case where the player is ducking below a ceiling portal. As they unduck their eye moves beyond the portal before the code detects that they're in the portal hole.
		{
			Vector ptPlayerOrigin = GetAbsOrigin();
			float fOriginDist = vPortalForward.Dot(ptPlayerOrigin) - fPortalPlaneDist;

			if (fOriginDist > 0.0f)
			{
				float fInvTotalDist = 1.0f / (fOriginDist - fEyeDist); //fEyeDist is negative
				Vector ptPlaneIntersection = (eyeOrigin * fOriginDist * fInvTotalDist) - (ptPlayerOrigin * fEyeDist * fInvTotalDist);
				Assert(fabs(vPortalForward.Dot(ptPlaneIntersection) - fPortalPlaneDist) < 0.01f);

				Vector vIntersectionTest = ptPlaneIntersection - ptPortalCenter;

				Vector vPortalRight, vPortalUp;
				pPortal->GetVectors(NULL, &vPortalRight, &vPortalUp);

				if ((vIntersectionTest.Dot(vPortalRight) <= PORTAL_HALF_WIDTH) &&
					(vIntersectionTest.Dot(vPortalUp) <= PORTAL_HALF_HEIGHT))
				{
					bTransformEye = true;
				}
			}
		}
	}

	if (bTransformEye)
	{
		m_bEyePositionIsTransformedByPortal = true;

		//DevMsg( 2, "transforming portal view from <%f %f %f> <%f %f %f>\n", eyeOrigin.x, eyeOrigin.y, eyeOrigin.z, eyeAngles.x, eyeAngles.y, eyeAngles.z );

		VMatrix matThisToLinked = pPortal->MatrixThisToLinked();
		UTIL_Portal_PointTransform(matThisToLinked, eyeOrigin, eyeOrigin);
		UTIL_Portal_AngleTransform(matThisToLinked, eyeAngles, eyeAngles);

		//DevMsg( 2, "transforming portal view to   <%f %f %f> <%f %f %f>\n", eyeOrigin.x, eyeOrigin.y, eyeOrigin.z, eyeAngles.x, eyeAngles.y, eyeAngles.z );

		if (IsToolRecording())
		{
			static EntityTeleportedRecordingState_t state;

			KeyValues* msg = new KeyValues("entity_teleported");
			msg->SetPtr("state", &state);
			state.m_bTeleported = false;
			state.m_bViewOverride = true;
			state.m_vecTo = eyeOrigin;
			state.m_qaTo = eyeAngles;
			MatrixInvert(matThisToLinked.As3x4(), state.m_teleportMatrix);

			// Post a message back to all IToolSystems
			Assert((int)GetToolHandle() != 0);
			ToolFramework_PostToolMessage(GetToolHandle(), msg);

			msg->deleteThis();
		}

		bOverrideSpecialEffects = true;
	}
	else
	{
		m_bEyePositionIsTransformedByPortal = false;
	}

	if (bOverrideSpecialEffects)
	{
		m_iForceNoDrawInPortalSurface = ((pRemotePortal->m_bIsPortal2) ? (2) : (1));
		pRemotePortal->m_fStaticAmount = 0.0f;
	}
}

extern float g_fMaxViewModelLag;
void C_HL2MP_Player::CalcViewModelView(const Vector& eyeOrigin, const QAngle& eyeAngles)
{
	// HACK: Manually adjusting the eye position that view model looking up and down are similar
	// (solves view model "pop" on floor to floor transitions)
	Vector vInterpEyeOrigin = eyeOrigin;

	Vector vForward;
	Vector vRight;
	Vector vUp;
	AngleVectors(eyeAngles, &vForward, &vRight, &vUp);

	if (vForward.z < 0.0f)
	{
		float fT = vForward.z * vForward.z;
		vInterpEyeOrigin += vRight * (fT * 4.7f) + vForward * (fT * 5.0f) + vUp * (fT * 4.0f);
	}

	if (UTIL_IntersectEntityExtentsWithPortal(this))
		g_fMaxViewModelLag = 0.0f;
	else
		g_fMaxViewModelLag = 1.5f;

	for (int i = 0; i < MAX_VIEWMODELS; i++)
	{
		CBaseViewModel* vm = GetViewModel(i);
		if (!vm)
			continue;

		vm->CalcViewModelView(this, vInterpEyeOrigin, eyeAngles);
	}
}

void C_HL2MP_Player::FixTeleportationRoll(void)
{
	if (IsInAVehicle()) //HL2 compatibility fix. do absolutely nothing to the view in vehicles
		return;

	if (!IsLocalPlayer())
		return;

	// Normalize roll from odd portal transitions
	QAngle vAbsAngles = EyeAngles();


	Vector vCurrentForward, vCurrentRight, vCurrentUp;
	AngleVectors(vAbsAngles, &vCurrentForward, &vCurrentRight, &vCurrentUp);

	if (vAbsAngles[ROLL] == 0.0f)
	{
		m_fReorientationRate = 0.0f;
		g_bUpsideDown = (vCurrentUp.z < 0.0f);
		return;
	}

	bool bForcePitchReorient = (vAbsAngles[ROLL] > 175.0f && vCurrentForward.z > 0.99f);
	bool bOnGround = (GetGroundEntity() != NULL);

	if (bForcePitchReorient)
	{
		m_fReorientationRate = REORIENTATION_RATE * ((bOnGround) ? (2.0f) : (1.0f));
	}
	else
	{
		// Don't reorient in air if they don't want to
		if (!cl_reorient_in_air.GetBool() && !bOnGround)
		{
			g_bUpsideDown = (vCurrentUp.z < 0.0f);
			return;
		}
	}

	if (vCurrentUp.z < 0.75f)
	{
		m_fReorientationRate += gpGlobals->frametime * REORIENTATION_ACCELERATION_RATE;

		// Upright faster if on the ground
		float fMaxReorientationRate = REORIENTATION_RATE * ((bOnGround) ? (2.0f) : (1.0f));
		if (m_fReorientationRate > fMaxReorientationRate)
			m_fReorientationRate = fMaxReorientationRate;
	}
	else
	{
		if (m_fReorientationRate > REORIENTATION_RATE * 0.5f)
		{
			m_fReorientationRate -= gpGlobals->frametime * REORIENTATION_ACCELERATION_RATE;
			if (m_fReorientationRate < REORIENTATION_RATE * 0.5f)
				m_fReorientationRate = REORIENTATION_RATE * 0.5f;
		}
		else if (m_fReorientationRate < REORIENTATION_RATE * 0.5f)
		{
			m_fReorientationRate += gpGlobals->frametime * REORIENTATION_ACCELERATION_RATE;
			if (m_fReorientationRate > REORIENTATION_RATE * 0.5f)
				m_fReorientationRate = REORIENTATION_RATE * 0.5f;
		}
	}

	if (!m_bPitchReorientation && !bForcePitchReorient)
	{
		// Randomize which way we roll if we're completely upside down
		if (vAbsAngles[ROLL] == 180.0f && RandomInt(0, 1) == 1)
		{
			vAbsAngles[ROLL] = -180.0f;
		}

		if (vAbsAngles[ROLL] < 0.0f)
		{
			vAbsAngles[ROLL] += gpGlobals->frametime * m_fReorientationRate;
			if (vAbsAngles[ROLL] > 0.0f)
				vAbsAngles[ROLL] = 0.0f;
			engine->SetViewAngles(vAbsAngles);
		}
		else if (vAbsAngles[ROLL] > 0.0f)
		{
			vAbsAngles[ROLL] -= gpGlobals->frametime * m_fReorientationRate;
			if (vAbsAngles[ROLL] < 0.0f)
				vAbsAngles[ROLL] = 0.0f;
			engine->SetViewAngles(vAbsAngles);
			m_angEyeAngles = vAbsAngles;
			m_iv_angEyeAngles.Reset();
		}
	}
	else
	{
		if (vAbsAngles[ROLL] != 0.0f)
		{
			if (vCurrentUp.z < 0.2f)
			{
				float fDegrees = gpGlobals->frametime * m_fReorientationRate;
				if (vCurrentForward.z > 0.0f)
				{
					fDegrees = -fDegrees;
				}

				// Rotate around the right axis
				VMatrix mAxisAngleRot = SetupMatrixAxisRot(vCurrentRight, fDegrees);

				vCurrentUp = mAxisAngleRot.VMul3x3(vCurrentUp);
				vCurrentForward = mAxisAngleRot.VMul3x3(vCurrentForward);

				VectorAngles(vCurrentForward, vCurrentUp, vAbsAngles);

				engine->SetViewAngles(vAbsAngles);
				m_angEyeAngles = vAbsAngles;
				m_iv_angEyeAngles.Reset();
			}
			else
			{
				if (vAbsAngles[ROLL] < 0.0f)
				{
					vAbsAngles[ROLL] += gpGlobals->frametime * m_fReorientationRate;
					if (vAbsAngles[ROLL] > 0.0f)
						vAbsAngles[ROLL] = 0.0f;
					engine->SetViewAngles(vAbsAngles);
					m_angEyeAngles = vAbsAngles;
					m_iv_angEyeAngles.Reset();
				}
				else if (vAbsAngles[ROLL] > 0.0f)
				{
					vAbsAngles[ROLL] -= gpGlobals->frametime * m_fReorientationRate;
					if (vAbsAngles[ROLL] < 0.0f)
						vAbsAngles[ROLL] = 0.0f;
					engine->SetViewAngles(vAbsAngles);
					m_angEyeAngles = vAbsAngles;
					m_iv_angEyeAngles.Reset();
				}
			}
		}
	}

	// Keep track of if we're upside down for look control
	vAbsAngles = EyeAngles();
	AngleVectors(vAbsAngles, NULL, NULL, &vCurrentUp);

	if (bForcePitchReorient)
		g_bUpsideDown = (vCurrentUp.z < 0.0f);
	else
		g_bUpsideDown = false;
}

IRagdoll* C_HL2MP_Player::GetRepresentativeRagdoll() const
{
	if ( m_hRagdoll.Get() )
	{
		C_HL2MPRagdoll *pRagdoll = (C_HL2MPRagdoll*)m_hRagdoll.Get();

		return pRagdoll->GetIRagdoll();
	}
	else
	{
		return NULL;
	}
}

//HL2MPRAGDOLL


IMPLEMENT_CLIENTCLASS_DT_NOBASE( C_HL2MPRagdoll, DT_HL2MPRagdoll, CHL2MPRagdoll )
	RecvPropVector( RECVINFO(m_vecRagdollOrigin) ),
	RecvPropEHandle( RECVINFO( m_hPlayer ) ),
	RecvPropInt( RECVINFO( m_nModelIndex ) ),
	RecvPropInt( RECVINFO(m_nForceBone) ),
	RecvPropVector( RECVINFO(m_vecForce) ),
	RecvPropVector( RECVINFO( m_vecRagdollVelocity ) )
END_RECV_TABLE()



C_HL2MPRagdoll::C_HL2MPRagdoll()
{

}

C_HL2MPRagdoll::~C_HL2MPRagdoll()
{
	PhysCleanupFrictionSounds( this );

	if ( m_hPlayer )
	{
		m_hPlayer->CreateModelInstance();
	}
}

void C_HL2MPRagdoll::Interp_Copy( C_BaseAnimatingOverlay *pSourceEntity )
{
	if ( !pSourceEntity )
		return;
	
	VarMapping_t *pSrc = pSourceEntity->GetVarMapping();
	VarMapping_t *pDest = GetVarMapping();
    	
	// Find all the VarMapEntry_t's that represent the same variable.
	for ( int i = 0; i < pDest->m_Entries.Count(); i++ )
	{
		VarMapEntry_t *pDestEntry = &pDest->m_Entries[i];
		const char *pszName = pDestEntry->watcher->GetDebugName();
		for ( int j=0; j < pSrc->m_Entries.Count(); j++ )
		{
			VarMapEntry_t *pSrcEntry = &pSrc->m_Entries[j];
			if ( !Q_strcmp( pSrcEntry->watcher->GetDebugName(), pszName ) )
			{
				pDestEntry->watcher->Copy( pSrcEntry->watcher );
				break;
			}
		}
	}
}

void C_HL2MPRagdoll::ImpactTrace( trace_t *pTrace, int iDamageType, const char *pCustomImpactName )
{
	IPhysicsObject *pPhysicsObject = VPhysicsGetObject();

	if( !pPhysicsObject )
		return;

	Vector dir = pTrace->endpos - pTrace->startpos;

	if ( iDamageType == DMG_BLAST )
	{
		dir *= 4000;  // adjust impact strenght
				
		// apply force at object mass center
		pPhysicsObject->ApplyForceCenter( dir );
	}
	else
	{
		Vector hitpos;  
	
		VectorMA( pTrace->startpos, pTrace->fraction, dir, hitpos );
		VectorNormalize( dir );

		dir *= 4000;  // adjust impact strenght

		// apply force where we hit it
		pPhysicsObject->ApplyForceOffset( dir, hitpos );	

		// Blood spray!
//		FX_CS_BloodSpray( hitpos, dir, 10 );
	}

	m_pRagdoll->ResetRagdollSleepAfterTime();
}


void C_HL2MPRagdoll::CreateHL2MPRagdoll( void )
{
	// First, initialize all our data. If we have the player's entity on our client,
	// then we can make ourselves start out exactly where the player is.
	C_HL2MP_Player *pPlayer = dynamic_cast< C_HL2MP_Player* >( m_hPlayer.Get() );
	
	if ( pPlayer && !pPlayer->IsDormant() )
	{
		// move my current model instance to the ragdoll's so decals are preserved.
		pPlayer->SnatchModelInstance( this );

		VarMapping_t *varMap = GetVarMapping();

		// Copy all the interpolated vars from the player entity.
		// The entity uses the interpolated history to get bone velocity.
		bool bRemotePlayer = (pPlayer != C_BasePlayer::GetLocalPlayer());			
		if ( bRemotePlayer )
		{
			Interp_Copy( pPlayer );

			SetAbsAngles( pPlayer->GetRenderAngles() );
			GetRotationInterpolator().Reset();

			m_flAnimTime = pPlayer->m_flAnimTime;
			SetSequence( pPlayer->GetSequence() );
			m_flPlaybackRate = pPlayer->GetPlaybackRate();
		}
		else
		{
			// This is the local player, so set them in a default
			// pose and slam their velocity, angles and origin
			SetAbsOrigin( m_vecRagdollOrigin );
			
			SetAbsAngles( pPlayer->GetRenderAngles() );

			SetAbsVelocity( m_vecRagdollVelocity );

			int iSeq = pPlayer->GetSequence();
			if ( iSeq == -1 )
			{
				Assert( false );	// missing walk_lower?
				iSeq = 0;
			}
			
			SetSequence( iSeq );	// walk_lower, basic pose
			SetCycle( 0.0 );

			Interp_Reset( varMap );
		}		
	}
	else
	{
		// overwrite network origin so later interpolation will
		// use this position
		SetNetworkOrigin( m_vecRagdollOrigin );

		SetAbsOrigin( m_vecRagdollOrigin );
		SetAbsVelocity( m_vecRagdollVelocity );

		Interp_Reset( GetVarMapping() );
		
	}

	SetModelIndex( m_nModelIndex );

	// Make us a ragdoll..
	m_nRenderFX = kRenderFxRagdoll;

	matrix3x4_t boneDelta0[MAXSTUDIOBONES];
	matrix3x4_t boneDelta1[MAXSTUDIOBONES];
	matrix3x4_t currentBones[MAXSTUDIOBONES];
	const float boneDt = 0.05f;

	if ( pPlayer && !pPlayer->IsDormant() )
	{
		pPlayer->GetRagdollInitBoneArrays( boneDelta0, boneDelta1, currentBones, boneDt );
	}
	else
	{
		GetRagdollInitBoneArrays( boneDelta0, boneDelta1, currentBones, boneDt );
	}

	InitAsClientRagdoll( boneDelta0, boneDelta1, currentBones, boneDt );
}


void C_HL2MPRagdoll::OnDataChanged( DataUpdateType_t type )
{
	BaseClass::OnDataChanged( type );

	if ( type == DATA_UPDATE_CREATED )
	{
		CreateHL2MPRagdoll();
	}
}

IRagdoll* C_HL2MPRagdoll::GetIRagdoll() const
{
	return m_pRagdoll;
}

void C_HL2MPRagdoll::UpdateOnRemove( void )
{
	VPhysicsSetObject( NULL );

	BaseClass::UpdateOnRemove();
}

//-----------------------------------------------------------------------------
// Purpose: clear out any face/eye values stored in the material system
//-----------------------------------------------------------------------------
void C_HL2MPRagdoll::SetupWeights( const matrix3x4_t *pBoneToWorld, int nFlexWeightCount, float *pFlexWeights, float *pFlexDelayedWeights )
{
	BaseClass::SetupWeights( pBoneToWorld, nFlexWeightCount, pFlexWeights, pFlexDelayedWeights );

	static float destweight[128];
	static bool bIsInited = false;

	CStudioHdr *hdr = GetModelPtr();
	if ( !hdr )
		return;

	int nFlexDescCount = hdr->numflexdesc();
	if ( nFlexDescCount )
	{
		Assert( !pFlexDelayedWeights );
		memset( pFlexWeights, 0, nFlexWeightCount * sizeof(float) );
	}

	if ( m_iEyeAttachment > 0 )
	{
		matrix3x4_t attToWorld;
		if (GetAttachment( m_iEyeAttachment, attToWorld ))
		{
			Vector local, tmp;
			local.Init( 1000.0f, 0.0f, 0.0f );
			VectorTransform( local, attToWorld, tmp );
			modelrender->SetViewTarget( GetModelPtr(), GetBody(), tmp );
		}
	}
}

void C_HL2MP_Player::PostThink( void )
{
	BaseClass::PostThink();

	// Store the eye angles pitch so the client can compute its animation state correctly.
	m_angEyeAngles = EyeAngles();
}

void C_HL2MP_Player::PlayerPortalled(C_Prop_Portal* pEnteredPortal)
{
	if (pEnteredPortal)
	{
		m_bPortalledMessagePending = true;
		m_PendingPortalMatrix = pEnteredPortal->MatrixThisToLinked();

		if (IsLocalPlayer())
			g_pPortalRender->EnteredPortal(pEnteredPortal);
	}
}

bool LocalPlayerIsCloseToPortal(void)
{
	if (C_HL2MP_Player::GetLocalHL2MPPlayer())
	{
		return C_HL2MP_Player::GetLocalHL2MPPlayer()->IsCloseToPortal();
	}
	return false;
}


bool C_HL2MP_Player::HasDefuser() const
{
	return m_bHasDefuser;
}

void C_HL2MP_Player::GiveDefuser()
{
	m_bHasDefuser = true;
}

void C_HL2MP_Player::RemoveDefuser()
{
	m_bHasDefuser = false;
}

bool C_HL2MP_Player::HasNightVision() const
{
	return m_bHasNightVision;
}

bool C_HL2MP_Player::IsVIP() const
{
	C_CS_PlayerResource* pCSPR = (C_CS_PlayerResource*)GameResources();

	if (!pCSPR)
		return false;

	return pCSPR->IsVIP(entindex());
}

C_HL2MP_Player* C_HL2MP_Player::GetLocalCSPlayer()
{
	return (C_HL2MP_Player*)C_BasePlayer::GetLocalPlayer();
}


CSPlayerState C_HL2MP_Player::State_Get() const
{
	return m_iPlayerState;
}



int C_HL2MP_Player::GetAccount() const
{
	return m_iAccount;
}


int C_HL2MP_Player::PlayerClass() const
{
	return m_iClass;
}

bool C_HL2MP_Player::IsInBuyZone()
{
	return m_bInBuyZone;
}

bool C_HL2MP_Player::CanShowTeamMenu() const
{
	return true;
}


int C_HL2MP_Player::ArmorValue() const
{
	return m_ArmorValue;
}

bool C_HL2MP_Player::HasHelmet() const
{
	return m_bHasHelmet;
}

int C_HL2MP_Player::GetCurrentAssaultSuitPrice()
{
	// WARNING: This price logic also exists in CHL2MP_Player::AttemptToBuyAssaultSuit
	// and must be kept in sync if changes are made.

	int fullArmor = ArmorValue() >= 100 ? 1 : 0;
	if (fullArmor && !HasHelmet())
	{
		return HELMET_PRICE;
	}
	else if (!fullArmor && HasHelmet())
	{
		return KEVLAR_PRICE;
	}
	else
	{
		// NOTE: This applies to the case where you already have both
		// as well as the case where you have neither.  In the case
		// where you have both, the item should still have a price
		// and become disabled when you have little or no money left.
		return ASSAULTSUIT_PRICE;
	}
}
void C_HL2MP_Player::DoAnimationEvent(PlayerAnimEvent_t event, int nData)
{
	/*if (event == PLAYERANIMEVENT_THROW_GRENADE)
	{
		// Let the server handle this event. It will update m_iThrowGrenadeCounter and the client will
		// pick up the event in CCSPlayerAnimState.
	}
	else
	{
		//m_PlayerAnimState->DoAnimationEvent(event, nData);
	}*/
}
bool C_HL2MP_Player::HasC4(void)
{
	if (this == C_HL2MP_Player::GetLocalPlayer())
	{
		return Weapon_OwnsThisType("weapon_c4");
	}
	else
	{
		C_CS_PlayerResource* pCSPR = (C_CS_PlayerResource*)GameResources();

		return pCSPR->HasC4(entindex());
	}
}
bool C_HL2MP_Player::IsPlayerDominated(int iPlayerIndex)
{
	return m_bPlayerDominated.Get(iPlayerIndex);
}

bool C_HL2MP_Player::IsPlayerDominatingMe(int iPlayerIndex)
{
	return m_bPlayerDominatingMe.Get(iPlayerIndex);
}

CWeaponHL2MPBase* C_HL2MP_Player::GetActiveCSWeapon() const
{
	return dynamic_cast<CWeaponHL2MPBase*>(GetActiveWeapon());
}

CWeaponHL2MPBase* C_HL2MP_Player::GetCSWeapon(CSWeaponID id) const
{
	for (int i = 0; i < MAX_WEAPONS; i++)
	{
		CBaseCombatWeapon* weapon = GetWeapon(i);
		if (weapon)
		{
			CWeaponHL2MPBase* csWeapon = dynamic_cast<CWeaponHL2MPBase*>(weapon);
			if (csWeapon)
			{
				if (id == csWeapon->GetWeaponID())
				{
					return csWeapon;
				}
			}
		}
	}

	return NULL;
}

bool C_HL2MP_Player::IsInHostageRescueZone()
{
	return 	m_bInHostageRescueZone;
}

vgui::IImage* GetDefaultAvatarImage(C_BasePlayer* pPlayer)
{
	vgui::IImage* result = NULL;

	switch (pPlayer ? pPlayer->GetTeamNumber() : TEAM_MAXCOUNT)
	{
	case TEAM_TERRORIST:
		result = vgui::scheme()->GetImage(CSTRIKE_DEFAULT_T_AVATAR, true);
		break;

	case TEAM_CT:
		result = vgui::scheme()->GetImage(CSTRIKE_DEFAULT_CT_AVATAR, true);
		break;

	default:
		result = vgui::scheme()->GetImage(CSTRIKE_DEFAULT_AVATAR, true);
		break;
	}

	return result;
}
bool C_HL2MP_Player::HasPlayerAsFriend(C_HL2MP_Player* player)
{
	if (!steamapicontext || !steamapicontext->SteamFriends() || !steamapicontext->SteamUtils() || !player)
	{
		return false;
	}

	player_info_t pi;
	if (!engine->GetPlayerInfo(player->entindex(), &pi))
	{
		return false;
	}

	if (!pi.friendsID)
	{
		return false;
	}

	// check and see if they're on the local player's friends list
	CSteamID steamID(pi.friendsID, 1, steamapicontext->SteamUtils()->GetConnectedUniverse(), k_EAccountTypeIndividual);
	return steamapicontext->SteamFriends()->HasFriend(steamID, k_EFriendFlagImmediate);
}
C_HL2MP_Player* GetLocalOrInEyeCSPlayer(void)
{
	C_HL2MP_Player* player = C_HL2MP_Player::GetLocalCSPlayer();

	if (player && player->GetObserverMode() == OBS_MODE_IN_EYE)
	{
		C_BaseEntity* target = player->GetObserverTarget();

		if (target && target->IsPlayer())
		{
			return ToCSPlayer(target);
		}
	}
	return player;
}
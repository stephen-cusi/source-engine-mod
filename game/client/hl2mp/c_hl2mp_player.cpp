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
#include "hl2sb_model_scan.h"
#include "hl2sb_model_config.h"

#if defined( LUA_SDK )
#include "luamanager.h"
#include "weapon_hl2mpbase_scriptedweapon.h"	// HL2SB: SWEP:TranslateFOV / SWEP:CalcView dispatch
#include "lgametrace.h"
#include "lhl2mp_player_shared.h"
#include "ltakedamageinfo.h"
#include "mathlib/lvector.h"
#endif

// Don't alias here
#if defined( CHL2MP_Player )
#undef CHL2MP_Player	
#endif

LINK_ENTITY_TO_CLASS( player, C_HL2MP_Player );

IMPLEMENT_CLIENTCLASS_DT(C_HL2MP_Player, DT_HL2MP_Player, CHL2MP_Player)
	RecvPropFloat( RECVINFO( m_angEyeAngles[0] ) ),
	RecvPropFloat( RECVINFO( m_angEyeAngles[1] ) ),
	RecvPropEHandle( RECVINFO( m_hRagdoll ) ),
	RecvPropInt( RECVINFO( m_iSpawnInterpCounter ) ),
	RecvPropInt( RECVINFO( m_iPlayerSoundType) ),
	RecvPropFloat( RECVINFO( m_flStartCharge ) ),
	RecvPropFloat( RECVINFO( m_flAmmoStartCharge ) ),
	RecvPropFloat( RECVINFO( m_flPlayAftershock ) ),
	RecvPropFloat( RECVINFO( m_flNextAmmoBurn ) ),
	RecvPropBool( RECVINFO( m_fIsWalking ) ),
END_RECV_TABLE()

BEGIN_PREDICTION_DATA( C_HL2MP_Player )
	DEFINE_PRED_FIELD( m_fIsWalking, FIELD_BOOLEAN, FTYPEDESC_INSENDTABLE ),
END_PREDICTION_DATA()

#define	HL2_WALK_SPEED 150
#define	HL2_NORM_SPEED 190
#define	HL2_SPRINT_SPEED 320

static ConVar cl_playermodel( "cl_playermodel", "none", FCVAR_USERINFO | FCVAR_ARCHIVE | FCVAR_SERVER_CAN_EXECUTE, "Default Player Model");
static ConVar cl_defaultweapon( "cl_defaultweapon", "weapon_physcannon", FCVAR_USERINFO | FCVAR_ARCHIVE, "Default Spawn Weapon");

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
#if defined ( LUA_SDK )
	// Andrew; push a copy of the damageinfo/vector, bring the changes back out
	// of Lua and set info/vecDir to the new value if it's been modified.
	CTakeDamageInfo lInfo = info;
	Vector lvecDir = vecDir;

	BEGIN_LUA_CALL_HOOK( "PlayerTraceAttack" );
		lua_pushhl2mpplayer( L, this );
		lua_pushdamageinfo( L, lInfo );
		lua_pushvector( L, lvecDir );
		lua_pushtrace( L, *ptr );
	END_LUA_CALL_HOOK( 4, 1 );

	RETURN_LUA_NONE();
#endif

#if defined ( LUA_SDK )
	Vector vecOrigin = ptr->endpos - lvecDir * 4;
#else
	Vector vecOrigin = ptr->endpos - vecDir * 4;
#endif

	float flDistance = 0.0f;

#if defined ( LUA_SDK )
	if ( lInfo.GetAttacker() )
	{
		flDistance = (ptr->endpos - lInfo.GetAttacker()->GetAbsOrigin()).Length();
	}
#else
	if ( info.GetAttacker() )
	{
		flDistance = (ptr->endpos - info.GetAttacker()->GetAbsOrigin()).Length();
	}
#endif

	if ( m_takedamage )
	{
#if defined ( LUA_SDK )
		AddMultiDamage( lInfo, this );
#else
		AddMultiDamage( info, this );
#endif
		int blood = BloodColor();
		
#if defined ( LUA_SDK )
		CBaseEntity *pAttacker = lInfo.GetAttacker();
#else
		CBaseEntity *pAttacker = info.GetAttacker();
#endif

		if ( pAttacker )
		{
			if ( HL2MPRules()->IsTeamplay() && pAttacker->InSameTeam( this ) == true )
				return;
		}

		if ( blood != DONT_BLEED )
		{
#if defined ( LUA_SDK )
			SpawnBlood( vecOrigin, lvecDir, blood, flDistance );// a little surface blood.
			TraceBleed( flDistance, lvecDir, ptr, lInfo.GetDamageType() );
#else
			SpawnBlood( vecOrigin, vecDir, blood, flDistance );// a little surface blood.
			TraceBleed( flDistance, vecDir, ptr, info.GetDamageType() );
#endif
		}
	}
}


C_HL2MP_Player* C_HL2MP_Player::GetLocalHL2MPPlayer()
{
	return (C_HL2MP_Player*)C_BasePlayer::GetLocalPlayer();
}

void C_HL2MP_Player::Initialize( void )
{
	CStudioHdr *hdr = GetModelPtr();
	// Custom player models can be huge and load asynchronously - GetModelPtr()
	// is NULL until the dynamic model is ready. OnModelLoadCompleted calls
	// OnNewModel again, so just bail out here.
	if ( !hdr )
		return;

	m_headYawPoseParam = LookupPoseParameter( hdr, "head_yaw" );
	GetPoseParameterRange( m_headYawPoseParam, m_headYawMin, m_headYawMax );

	m_headPitchPoseParam = LookupPoseParameter( hdr, "head_pitch" );
	GetPoseParameterRange( m_headPitchPoseParam, m_headPitchMin, m_headPitchMax );

	for ( int i = 0; i < hdr->GetNumPoseParameters() ; i++ )
	{
		SetPoseParameter( hdr, i, 0.0 );
	}
}

CStudioHdr *C_HL2MP_Player::OnNewModel( void )
{
	CStudioHdr *hdr = BaseClass::OnNewModel();

	// Only initialize pose data once the model (studio hdr) is actually available.
	if ( hdr )
	{
		Initialize( );
	}

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

extern int g_nHL2SBClientSideAnimUpdates;
extern ConVar hl2sb_anim_debug;
extern bool HL2SB_GetClientSideAnimListEntry( C_BaseAnimating *pAnim, int *pCount, unsigned int *pFlags );
extern int g_nHL2SBCycleZeroCount[4];

// HL2SB: last cycle this client advanced to, and the sequence it belongs to. Used
// to detect a data update / interpolator running the cycle backwards (see the
// snap-back in C_HL2MP_Player::UpdateClientSideAnimation). The local player is
// unique, so file-scope state is enough. -1 means "nothing latched yet".
static float s_flHL2SBLastCycle = -1.0f;
static int s_nHL2SBLastSeq = -1;
static int s_nHL2SBRollbacks = 0;
static float s_flHL2SBLastRollbackMagnitude = 0.0f;

void C_HL2MP_Player::ClientThink( void )
{
	// ---------------------------------------------------------------------------
	// HL2SB: make sure the local player is registered for client-side animation.
	//
	// The registration happens in C_BaseAnimating::PostDataUpdate(), and for this
	// entity it evidently never took effect: with hl2sb_anim_debug on, the client's
	// cycle sat at 0.011 for the whole run while the server's wrapped normally
	// (0.241 -> 0.674 -> ...), and UpdateClientSideAnimation() was never called once
	// (csa=1 csaupdates=0). An entity that is not in g_ClientSideAnimationList never
	// has its cycle advanced, and because the server believes the client animates
	// the body it does not send the cycle either - the body freezes after the brief
	// motion a sequence change causes. See AGENTS.md 27.
	//
	// ClientThink() is the one client path guaranteed to run every frame for the
	// local player (SetNextClientThink(CLIENT_THINK_ALWAYS) in OnDataChanged), so
	// the registration is repaired from here.
	// ---------------------------------------------------------------------------
	if ( m_bClientSideAnimation )
	{
		unsigned int nFlags = 0;
		if ( !HL2SB_GetClientSideAnimListEntry( this, NULL, &nFlags ) )
		{
			m_ClientSideAnimationListHandle = INVALID_CLIENTSIDEANIMATION_LIST_HANDLE;
			AddToClientSideAnimationList();
		}
		else if ( nFlags == 0 )
		{
			// In the list, but not marked for cycle updates: recompute the flags.
			ClientSideAnimationChanged();
		}
	}

	// HL2SB diagnostic (see hl2sb_anim_debug in hl2mp_player_shared.cpp). csaupdates
	// counts how often the engine reached C_BaseAnimating::UpdateClientSideAnimation()
	// for this entity - it must grow once per frame for the body to animate.
	if ( hl2sb_anim_debug.GetBool() && C_BasePlayer::GetLocalPlayer() == this )
	{
		static float s_flNextPrint = 0.0f;
		if ( gpGlobals->curtime >= s_flNextPrint )
		{
			s_flNextPrint = gpGlobals->curtime + 1.0f;

			int iSequence = GetSequence();
			const char *pszLabel = "?";
			CStudioHdr *pStudioHdr = GetModelPtr();
			if ( pStudioHdr && iSequence >= 0 && iSequence < pStudioHdr->GetNumSeq() )
				pszLabel = pStudioHdr->pSeqdesc( iSequence ).pszLabel();

			int nListCount = 0;
			unsigned int nFlags = 0;
			bool bInList = HL2SB_GetClientSideAnimListEntry( this, &nListCount, &nFlags );

			Msg( "[HL2SB anim/cl] ClientThink: csa=%d csaupdates=%d inlist=%d flags=0x%X listcount=%d seq=%d(%s) cycle=%.3f rate=%.2f animtime=%.3f curtime=%.3f\n",
				 m_bClientSideAnimation ? 1 : 0, g_nHL2SBClientSideAnimUpdates, bInList ? 1 : 0, nFlags, nListCount,
				 iSequence, pszLabel, GetCycle(), m_flPlaybackRate, m_flAnimTime, gpGlobals->curtime );
		}
	}

	bool bFoundViewTarget = false;
	
	Vector vForward;
	AngleVectors( GetLocalAngles(), &vForward );

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
//-----------------------------------------------------------------------------
// Purpose: HL2SB: the per-frame player animation state update.
//
// HL2MP never overrode this. The client therefore ran the player animstate
// exactly ONCE - from AddEntity(), i.e. when the entity entered the client's
// list, standing still - so the movement pose parameters (move_x / move_y on
// GMod's anim models, move_yaw on HL2MP's own), the aim/head parameters and the
// playback rate were never written again on the client. The legs kept the pose
// they had at that instant and did not follow the keyboard, while the cycle on
// its own kept running.
//
// C_CSPlayer and C_DODPlayer / C_Portal_Player all override this the same way,
// and CS's ordering note applies here too: the cycle has to be advanced before
// the animstate looks at it, or the upper body synchronizes against a stale
// cycle.
//-----------------------------------------------------------------------------
void C_HL2MP_Player::UpdateClientSideAnimation()
{
	// HL2SB diagnostic: this override IS the client-side animation entry point for
	// this entity (it deliberately does not chain to the base class, the same way
	// C_CSPlayer does not), so the "was it called" counter has to live here.
	const bool bLocalPlayer = ( C_BasePlayer::GetLocalPlayer() == this );
	if ( bLocalPlayer )
		++g_nHL2SBClientSideAnimUpdates;

	float flCycleBefore = GetCycle();
	float flInterval = 0.0f;

	// HL2SB: the client owns the cycle of the local player (AGENTS.md 27). If a
	// snapshot, the interpolation var map or a stale m_flOldCycle ran it backwards
	// inside the same sequence, put it back before advancing: otherwise the visible
	// pose stays pinned on the first frame of the sequence while the counters still
	// show frames being processed. A wrap (0.99 -> 0.01) is forward motion, not a
	// rollback.
	if ( bLocalPlayer && s_flHL2SBLastCycle >= 0.0f && GetSequence() == s_nHL2SBLastSeq )
	{
		const float flNow = GetCycle();
		const bool bWrapped = ( s_flHL2SBLastCycle > 0.95f ) && ( flNow < 0.05f );
		if ( !bWrapped && flNow < s_flHL2SBLastCycle - 0.002f )
		{
			++s_nHL2SBRollbacks;
			s_flHL2SBLastRollbackMagnitude = s_flHL2SBLastCycle - flNow;
			SetCycle( s_flHL2SBLastCycle );
			flCycleBefore = s_flHL2SBLastCycle;
		}
	}

	if ( GetSequence() != -1 )
	{
		// HL2SB: keep the cycle out of the interpolation var map. The interpolator
		// (C_BaseEntity::Interpolate -> m_iv_flCycle.Interpolate) writes *m_pValue
		// directly, so it can undo FrameAdvance without ever passing through
		// SetCycle(), and RemoveBaseAnimatingInterpolatedVars() is only reached
		// through UpdateRelevantInterpolatedVars().
		RemoveVar( &m_flCycle, false );

		// move frame forward
		flInterval = FrameAdvance( 0.0f );	// 0 means to use the time we last advanced instead of a constant

		// HL2SB: keep the client's own animation bookkeeping in sync. The engine
		// relies on C_BaseAnimating::PreDataUpdate() for both of these, and that
		// function does not run for the local player, so both "restores" were
		// actively destroying the cycle we just advanced:
		//   PostDataUpdate() -> SetCycle( m_flOldCycle )        (m_flOldCycle stayed 0)
		//   OnDataChanged()  -> if ( m_bClientSideFrameReset != m_bLastClientSideFrameReset )
		//                           ResetClientsideFrame()      (-> SetCycle( 0 ))
		// The log showed exactly that: "before=0.0000" on every single FrameAdvance
		// call while the server's cycle advanced normally. See AGENTS.md 27.
		m_flOldCycle = GetCycle();
		m_bLastClientSideFrameReset = m_bClientSideFrameReset;

		// HL2SB: and keep the sequence parity bookkeeping in sync as well.
		// C_BaseAnimating::PostDataUpdate() calls m_iv_flCycle.Reset() when the
		// parity mismatches. That Reset() is NOT what eats the cycle - it only
		// clears the history and re-seeds it with the current value
		// (interpolatedvar.h:739-752, verified). It is kept in sync anyway because
		// the same mismatch also force-adds a transition layer.
		ClientSideAnimationChanged();

		m_PlayerAnimState.Update();

		// latch old values
		OnLatchInterpolatedVariables( LATCH_ANIMATION_VAR );

		// HL2SB: remember the cycle this client advanced to, so the next call can
		// tell a rollback from a wrap.
		if ( bLocalPlayer )
		{
			s_flHL2SBLastCycle = GetCycle();
			s_nHL2SBLastSeq = GetSequence();
		}
	}
	else
	{
		m_PlayerAnimState.Update();
	}

	// HL2SB diagnostic: did the cycle move, and how big was the step? "before ==
	// after" with an interval of ~0 means m_flAnimTime was already curtime when we
	// were called, i.e. the step is taken away again (see AGENTS.md 27).
	if ( bLocalPlayer && hl2sb_anim_debug.GetBool() )
	{
		static float s_flNextFramePrint = 0.0f;
		if ( gpGlobals->curtime >= s_flNextFramePrint )
		{
			s_flNextFramePrint = gpGlobals->curtime + 1.0f;

			// HL2SB: is m_flCycle still in the interpolation var map? The
			// interpolator writes *m_pValue directly, so that is the one writer
			// SetCycle() cannot see.
			int bCycleInMap = 0;
			VarMapping_t *pVarMap = GetVarMapping();
			if ( pVarMap )
			{
				for ( int i = 0; i < pVarMap->m_Entries.Count(); ++i )
				{
					if ( pVarMap->m_Entries[i].data == &m_flCycle )
					{
						bCycleInMap = 1;
						break;
					}
				}
			}

			Msg( "[HL2SB anim/cl] FrameAdvance: before=%.4f interval=%.4f after=%.4f rate=%.2f animtime=%.3f curtime=%.3f oldcycle=%.4f fsr=%d/%d updates=%d inmap=%d ivcur=%.4f rb=%d rbmag=%.3f z0=%d z1=%d z2=%d\n",
				 flCycleBefore, flInterval, GetCycle(), m_flPlaybackRate, m_flAnimTime, gpGlobals->curtime,
				 m_flOldCycle, m_bClientSideFrameReset ? 1 : 0, m_bLastClientSideFrameReset ? 1 : 0,
				 g_nHL2SBClientSideAnimUpdates, bCycleInMap, m_iv_flCycle.GetCurrent(),
				 s_nHL2SBRollbacks, s_flHL2SBLastRollbackMagnitude,
				 g_nHL2SBCycleZeroCount[0], g_nHL2SBCycleZeroCount[1], g_nHL2SBCycleZeroCount[2] );
		}
	}
}

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
	return ( (!m_Local.m_bDucked && !m_Local.m_bDucking) && (!IsInAVehicle()) && (GetWaterLevel() != 3) );
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void C_HL2MP_Player::StartSprinting( void )
{
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
	SetMaxSpeed( HL2_WALK_SPEED );
	m_fIsWalking = true;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void C_HL2MP_Player::StopWalking( void )
{
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

void C_HL2MP_Player::CalcView( Vector &eyeOrigin, QAngle &eyeAngles, float &zNear, float &zFar, float &fov )
{
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

	BaseClass::CalcView( eyeOrigin, eyeAngles, zNear, zFar, fov );

	// HL2SB: GMod's weapon view hooks, for the LOCAL player's view only.
	// SWEP:TranslateFOV is what turns the camera's Zoom networked var into an
	// actual field of view, and SWEP:CalcView is where the camera applies its
	// Roll.  Both are dispatched only for scripted (Lua SWEP) weapons, so stock
	// weapons and other players' views keep the unmodified engine result; GMod
	// applies TranslateFOV in this same view path (not in C_BasePlayer::GetFOV),
	// which keeps c_effects.cpp / cs_hud_scope.cpp readers on the stock value.
#if defined( LUA_SDK )
	if ( this == C_BasePlayer::GetLocalPlayer() )
	{
		CBaseCombatWeapon *pWeapon = GetActiveWeapon();

		if ( pWeapon && pWeapon->IsScripted() )
		{
			CHL2MPScriptedWeapon *pScripted = static_cast<CHL2MPScriptedWeapon *>( pWeapon );

			fov = pScripted->TranslateFOV( fov );
			pScripted->CalcView( this, eyeOrigin, eyeAngles, fov );
		}
	}
#endif
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

	// HL2SB: these used to be matrix3x4_t boneDelta0/1/currentBones[MAXSTUDIOBONES]
	// (128).  GetRagdollInitBoneArrays / InitAsClientRagdoll fill them per numbones(),
	// so any player model with more than 128 bones (community models - miku and
	// friends) ran off the stack and /GS fast-failed the process as soon as it died
	// and turned into a ragdoll.  Size them to the model instead.
	const CStudioHdr *pRagdollHdr = ( pPlayer && !pPlayer->IsDormant() ) ? pPlayer->GetModelPtr() : GetModelPtr();
	const int nRagdollBones = pRagdollHdr ? pRagdollHdr->numbones() : MAXSTUDIOBONES;
	CUtlVector<matrix3x4_t> boneDelta0Buf, boneDelta1Buf, currentBonesBuf;
	boneDelta0Buf.SetSize( nRagdollBones );
	boneDelta1Buf.SetSize( nRagdollBones );
	currentBonesBuf.SetSize( nRagdollBones );
	matrix3x4_t *boneDelta0 = boneDelta0Buf.Base();
	matrix3x4_t *boneDelta1 = boneDelta1Buf.Base();
	matrix3x4_t *currentBones = currentBonesBuf.Base();
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
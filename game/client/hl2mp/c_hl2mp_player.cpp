//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose:		Player for HL2.
//
//=============================================================================//

#include "cbase.h"
#include "vcollide_parse.h"
#include "c_hl2mp_player.h"
#include "c_basetempentity.h"	// HL2SB: C_TEPlayerAnimEvent (GMod gesture broadcast)

#include "view.h"
#include "takedamageinfo.h"
#include "hl2mp_gamerules.h"
#include "in_buttons.h"
#include "iviewrender_beams.h"			// flashlight beam
#include "r_efx.h"
#include "dlight.h"
#include "hl2sb_model_scan.h"
#include "hl2sb_model_config.h"
#include "in_main.h"			// HL2SB: in_duck, for the in-vehicle camera toggle
#include "iinput.h"				// HL2SB: input->CAM_ToThirdPerson() / KeyState()
#include "activitylist.h"		// HL2SB: ActivityList_NameForIndex() for the act command
#include "collisionutils.h"

// HL2SB: the in_camera.cpp free wrappers. These are what the `thirdperson` /
// `firstperson` console commands call; unlike CInput::CAM_ToThirdPerson() they also
// run g_ThirdPersonManager.SetOverridingThirdPerson(), which is the bit CAM_Think()
// honours - calling only the CInput member gets reverted every frame (log evidence:
// "actcam=1 thirdperson=0" and the local player never entered the render list).
extern void CAM_ToThirdPerson( void );
extern void CAM_ToFirstPerson( void );

#if defined( LUA_SDK )
#include "luamanager.h"
#include "lbaseentity_shared.h"
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

// HL2SB (2026-09-27): lua_pushplayer for the GM:UpdateAnimation dispatch.
// Declared here instead of including lbaseplayer_shared.h, which requires the
// full CBasePlayer type before its typedef.  This MUST stay at file scope: a
// block-scope `class CBasePlayer;` declares a NEW incomplete class that
// shadows the real global one for the rest of the function, and clang
// (Linux/Android) then rejects both C_BasePlayer::GetLocalPlayer() and
// passing `this` where the parameter wants C_BasePlayer* - MSVC merges the
// names, which is why Windows never saw the break.
class CBasePlayer;
LUA_API void lua_pushplayer( lua_State *L, CBasePlayer *pPlayer );

// ============================================================================
// HL2SB (2026-10-02): GMod gesture broadcast receiver - C_TEPlayerAnimEvent.
//
// The server excludes DT_BaseAnimatingOverlay::overlay_vars from
// DT_HL2MP_Player, so this temp entity is the ONLY thing that makes other
// players' gestures (attacks/reloads/act/land/chat) appear: it carries
// (player handle, event, data) - zero layer data - and the client rebuilds
// the layer locally via HL2SB_ClientDoAnimationEvent.  Structure mirrors the
// in-tree CS/DOD C_TEPlayerAnimEvent; the body mirrors GMod's reference
// PostDataUpdate (): handle -> entity, dormant check, dispatch.
// ============================================================================
class C_TEPlayerAnimEvent : public C_BaseTempEntity
{
public:
	DECLARE_CLASS( C_TEPlayerAnimEvent, C_BaseTempEntity );
	DECLARE_CLIENTCLASS();

	virtual void PostDataUpdate( DataUpdateType_t updateType )
	{
		C_HL2MP_Player *pPlayer = dynamic_cast<C_HL2MP_Player *>( m_hPlayer.Get() );
		if ( pPlayer && !pPlayer->IsDormant() )
		{
			pPlayer->HL2SB_ClientDoAnimationEvent( m_iEvent.Get(), m_nData.Get() );
		}
	}

public:
	CNetworkHandle( CBasePlayer, m_hPlayer );
	CNetworkVar( int, m_iEvent );
	CNetworkVar( int, m_nData );
};

IMPLEMENT_CLIENTCLASS_EVENT( C_TEPlayerAnimEvent, DT_TEPlayerAnimEvent, CTEPlayerAnimEvent );

BEGIN_RECV_TABLE_NOBASE( C_TEPlayerAnimEvent, DT_TEPlayerAnimEvent )
	RecvPropEHandle( RECVINFO( m_hPlayer ) ),
	RecvPropInt( RECVINFO( m_iEvent ) ),
	RecvPropInt( RECVINFO( m_nData ) )
END_RECV_TABLE()

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
	RecvPropFloat( RECVINFO( m_flHL2SBWalkSpeed ) ),
	RecvPropFloat( RECVINFO( m_flHL2SBRunSpeed ) ),
	RecvPropFloat( RECVINFO( m_flHL2SBSlowWalkSpeed ) ),
	RecvPropFloat( RECVINFO( m_flHL2SBJumpPower ) ),
	RecvPropFloat( RECVINFO( m_flHL2SBDuckSpeed ) ),
	RecvPropFloat( RECVINFO( m_flHL2SBUnDuckSpeed ) ),
	RecvPropFloat( RECVINFO( m_flHL2SBTauntEnd ) ),
	RecvPropInt( RECVINFO( m_iHL2SBPlayerClassID ) ),
	RecvPropInt( RECVINFO( m_iHL2SBPlayerFlags ) ),
	RecvPropEHandle( RECVINFO( m_hHands ) ),
END_RECV_TABLE()

BEGIN_PREDICTION_DATA( C_HL2MP_Player )
	// Player body animation advances at render time, outside command prediction.
	DEFINE_PRED_FIELD( m_nSequence, FIELD_INTEGER, FTYPEDESC_PRIVATE | FTYPEDESC_OVERRIDE | FTYPEDESC_NOERRORCHECK ),
	DEFINE_PRED_FIELD( m_flPlaybackRate, FIELD_FLOAT, FTYPEDESC_PRIVATE | FTYPEDESC_OVERRIDE | FTYPEDESC_NOERRORCHECK ),
	DEFINE_PRED_FIELD( m_flCycle, FIELD_FLOAT, FTYPEDESC_PRIVATE | FTYPEDESC_OVERRIDE | FTYPEDESC_NOERRORCHECK ),
	DEFINE_PRED_FIELD( m_fIsWalking, FIELD_BOOLEAN, FTYPEDESC_INSENDTABLE ),
END_PREDICTION_DATA()

#define	HL2_WALK_SPEED 150
#define	HL2_NORM_SPEED 190
#define	HL2_SPRINT_SPEED 320

// HL2SB (2026-09-27): GMod's default playermodel.  The old hardcoded team model
// lists are gone, so an empty/invalid cl_playermodel now lands here (the server
// side falls back to the same path).
static ConVar cl_playermodel( "cl_playermodel", "models/player/kleiner.mdl", FCVAR_USERINFO | FCVAR_ARCHIVE | FCVAR_SERVER_CAN_EXECUTE, "Default Player Model");
static ConVar cl_defaultweapon( "cl_defaultweapon", "weapon_physgun", FCVAR_USERINFO | FCVAR_ARCHIVE, "Default Spawn Weapon");

/*
** HL2SB: the rest of GMod's player-editor settings.  GMod's player model selector
** (garrysmod/gamemodes/sandbox/gamemode/editor_player.lua) writes these, and the server
** picks them up the same way it already picks up cl_playermodel:
**
**     RunConsoleCommand( "cl_playerbodygroups", table.concat( str, " " ) )
**     RunConsoleCommand( "cl_playerskin", math.floor( val ) )
**     RunConsoleCommand( "cl_playercolor", tostring( plycol:GetVector() ) )   -- "r g b", 0..1
**     RunConsoleCommand( "cl_weaponcolor", tostring( wepcol:GetVector() ) )
**
** FCVAR_USERINFO is what makes engine->GetClientConVarValue() (server,
** game/server/hl2sb_player_model_manager.cpp) able to read them.
**
** Defaults: "1 1 1" is no tint at all (see HL2SB_ApplyClientAppearance), so a player who
** never opens the editor looks exactly like before, and an empty bodygroup string means
** "leave the bodygroups alone" instead of resetting them to 0.
*/
static ConVar cl_playerbodygroups( "cl_playerbodygroups", "", FCVAR_USERINFO | FCVAR_ARCHIVE, "Player model bodygroups (space separated)" );
static ConVar cl_playerskin( "cl_playerskin", "0", FCVAR_USERINFO | FCVAR_ARCHIVE, "Player model skin" );
static ConVar cl_playercolor( "cl_playercolor", "1 1 1", FCVAR_USERINFO | FCVAR_ARCHIVE, "Player model color (r g b, 0-1)" );
static ConVar cl_weaponcolor( "cl_weaponcolor", "1 1 1", FCVAR_USERINFO | FCVAR_ARCHIVE, "Weapon color (r g b, 0-1)" );

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

	// HL2SB (2026-09-27): GMod speed defaults, matching CHL2MP_Player's ctor
	// (prediction runs these before the first network update arrives).
	m_flHL2SBWalkSpeed = 150.0f;
	m_flHL2SBRunSpeed = 400.0f;
	m_flHL2SBSlowWalkSpeed = 100.0f;
	m_flHL2SBJumpPower = 200.0f;
	m_iHL2SBPlayerFlags = 0;

	// HL2SB: stored-only GMod knobs (see the server header).
	m_flHL2SBCrouchedWalkSpeed = 0.34f;
	m_flHL2SBDuckSpeed = 0.3f;
	m_flHL2SBUnDuckSpeed = 0.3f;
	m_flHL2SBLadderClimbSpeed = 100.0f;
	m_flHL2SBStepSize = 18.0f;
	m_bHL2SBAllowWeaponsInVehicle = false;
}

// HL2SB (2026-09-27): GMod movement - the client-side prediction twin of
// CHL2MP_Player::GetPlayerMaxSpeed.  Without this the predicted maxspeed came
// from hl2_normspeed (190) while the server ran 400, so every movement command
// mispredicted.
float C_HL2MP_Player::GetPlayerMaxSpeed( void )
{
	if ( IsObserver() || !IsAlive() )
		return BaseClass::GetPlayerMaxSpeed();

	if ( m_nButtons & IN_WALK )
		return m_flHL2SBWalkSpeed;

	return m_flHL2SBRunSpeed;
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

// HL2SB (2026-09-27): GMod's taunt state on the client -- the replicated clock
// (act command), same formula as the server (reference ).
bool C_HL2MP_Player::HL2SB_IsPlayingTaunt( void )
{
	const float flEnd = m_flHL2SBTauntEnd;
	return ( gpGlobals->curtime <= flEnd ) && ( flEnd != gpGlobals->curtime );
}

// HL2SB: C_BasePlayer::ShouldDrawLocalPlayer() consults this so the Lua taunt
// camera (gamemodes/deathmatch/gamemode/taunt_camera.lua + GM:CalcView) keeps
// the body in the render list while it orbits.  The old client-side act camera
// is gone -- this now keys off the networked taunt clock, exactly like GMod.
bool HL2SB_CustomThirdPersonActive( void )
{
	C_HL2MP_Player *pPlayer = static_cast<C_HL2MP_Player *>( C_BasePlayer::GetLocalPlayer() );
	return pPlayer != NULL && pPlayer->HL2SB_IsPlayingTaunt();
}

// (The GMod taunt camera's mouse-orbit + body lock runs through Lua --
// GM:CreateMove in gamemodes/deathmatch/gamemode -- since 2026-09-27.)
//
// HL2SB (sbrust): the in-vehicle IN_DUCK third-person toggle detector that used
// to sit in CreateMove is GONE -- the third CTRL path.  The toggle is now the
// server's job alone (CPropVehicleDriveable::HL2SB_UpdateCameraState reads the
// driver's IN_DUCK edge in the driveable's control tick and flips the networked
// per-vehicle state, the C++ port of GMod's GM:VehicleMove); a client-side
// toggle was one of the reasons CTRL double-fired against the ConCommand and
// the Lua Think edge of the same era.

static void HL2SB_AvoidTeammates( C_HL2MP_Player *player, CUserCmd *cmd )
{
	if ( !player->HL2SB_GetAvoidPlayers() || !player->IsAlive() || player->GetMoveType() == MOVETYPE_NOCLIP )
		return;

	Vector center = player->GetAbsOrigin();
	Vector mins = player->GetPlayerMins();
	Vector maxs = player->GetPlayerMaxs();
	center.z += ( maxs.z - mins.z ) * 0.5f;
	mins += center;
	maxs += center;

	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		C_HL2MP_Player *other = ToHL2MPPlayer( UTIL_PlayerByIndex( i ) );
		if ( !other || other == player || other->GetTeamNumber() != player->GetTeamNumber() ||
			 other->IsDormant() || other->IsSolidFlagSet( FSOLID_NOT_SOLID ) || other->GetMoveType() == MOVETYPE_NOCLIP )
			continue;

		Vector otherCenter = other->GetAbsOrigin();
		Vector otherMins = other->GetPlayerMins();
		Vector otherMaxs = other->GetPlayerMaxs();
		otherCenter.z += ( otherMaxs.z - otherMins.z ) * 0.5f;
		otherMins += otherCenter;
		otherMaxs += otherCenter;
		if ( !IsBoxIntersectingBox( mins, maxs, otherMins, otherMaxs ) )
			continue;

		Vector delta = other->WorldSpaceCenter() - center;
		Vector radius = other->WorldAlignMaxs() - other->WorldAlignMins();
		radius.z = 0.0f;
		const float radiusLength = radius.Length();
		const float strength = radiusLength == 0.0f ? 256.0f :
			clamp( ( radiusLength - delta.Length() ) / radiusLength, 0.0f, 1.0f ) * 256.0f;
		if ( strength < 0.01f )
			return;

		Vector push;
		if ( player->GetAbsVelocity().Length2DSqr() > 0.1f )
		{
			Vector velocity = player->GetAbsVelocity();
			velocity.z = 0.0f;
			CrossProduct( Vector( 0, 0, 1 ), velocity, push );
			VectorNormalize( push );
		}
		else
		{
			QAngle view = cmd->viewangles;
			view.x = 0.0f;
			AngleVectors( view, NULL, &push, NULL );
		}
		Vector separation = push * ( delta.Dot( push ) < 0.0f ? strength : -strength );
		float maxSpeed = player->MaxSpeed();
		if ( ( player->GetFlags() & FL_DUCKING ) && player->GetGroundEntity() ) maxSpeed *= 1.33333333f;
		if ( separation.LengthSqr() > maxSpeed * maxSpeed )
		{
			VectorNormalize( separation );
			separation *= maxSpeed;
		}
		QAngle view = cmd->viewangles;
		view.x = 0.0f;
		Vector forward, right;
		AngleVectors( view, &forward, &right, NULL );
		VectorNormalize( separation );
		float moveForward = cmd->forwardmove + forward.Dot( separation ) * strength;
		float moveSide = cmd->sidemove + right.Dot( separation ) * strength;
		float scaleForward = fabsf( moveForward ) > 400.0f ? 400.0f / fabsf( moveForward ) : 1.0f;
		float scaleSide = fabsf( moveSide ) > 400.0f ? 400.0f / fabsf( moveSide ) : 1.0f;
		float scale = MIN( scaleForward, scaleSide );
		cmd->forwardmove = moveForward * scale;
		cmd->sidemove = moveSide * scale;
		return;
	}
}

bool C_HL2MP_Player::CreateMove( float flInputSampleTime, CUserCmd *pCmd )
{
	const bool result = BaseClass::CreateMove( flInputSampleTime, pCmd );
	HL2SB_AvoidTeammates( this, pCmd );

	// (The old C++ act-camera body lock lived here; the GMod-parity act now runs
	// the lock through Lua -- GM:CreateMove in gamemodes/deathmatch/gamemode --
	// exactly like taunt_camera.lua's CAM.CreateMove.)

	return result;
}

// HL2SB: explicit first/third person toggle (bindable, e.g. bind F "hl2sb_vcam").
// Vehicle third-person was removed again - it needed a per-vehicle flag and its own
// camera/trace work and kept misbehaving, so this is on-foot only.
static void CC_HL2SB_VCam( const CCommand &args )
{
	if ( input->CAM_IsThirdPerson() )
		::CAM_ToFirstPerson();
	else
		::CAM_ToThirdPerson();
}

static ConCommand hl2sb_vcam( "hl2sb_vcam", CC_HL2SB_VCam, "Toggle first/third person.", FCVAR_CLIENTDLL );

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

	// ---------------------------------------------------------------------------
	// HL2SB: per-frame bookkeeping for the in-vehicle camera toggle.  (The old
	// act-camera teardown lived here too; the Lua taunt camera ends itself from
	// the replicated taunt clock and needs no client-side cleanup.)
	// ---------------------------------------------------------------------------
	if ( C_BasePlayer::GetLocalPlayer() == this )
	{
		// Diagnostic (hl2sb_anim_debug): vehicle/taunt camera state.
		if ( hl2sb_anim_debug.GetBool() )
		{
			static float s_flNextVehPrint = 0.0f;
			if ( gpGlobals->curtime >= s_flNextVehPrint )
			{
				s_flNextVehPrint = gpGlobals->curtime + 1.0f;
				Msg( "[HL2SB vcam/cl] invehicle=%d thirdperson=%d taunt=%d\n",
					 IsInAVehicle() ? 1 : 0,
					 input->CAM_IsThirdPerson() ? 1 : 0, HL2SB_CustomThirdPersonActive() ? 1 : 0 );
			}
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
// Purpose: Run one client-owned player animation update per frame.
//-----------------------------------------------------------------------------
void C_HL2MP_Player::UpdateClientSideAnimation()
{
    if ( !m_bClientSideAnimation )
        return;
    const bool localPlayer = this == C_BasePlayer::GetLocalPlayer();
    if ( localPlayer )
    {
        ++g_nHL2SBClientSideAnimUpdates;
        m_angEyeAngles = EyeAngles();
    }
    // Main sequence and poses are selected before advancing the current cycle.
    m_PlayerAnimState.Update();
    HL2SB_ClientAdvanceGestures();
    if ( GetSequence() >= 0 )
    {
        RemoveVar( &m_flCycle, false );
        OnLatchInterpolatedVariables( LATCH_ANIMATION_VAR );
        FrameAdvance( 0.0f );
        m_flOldCycle = GetCycle();
        m_bLastClientSideFrameReset = m_bClientSideFrameReset;
        ClientSideAnimationChanged();
        InvalidateBoneCache();
    }
}

void C_HL2MP_Player::AddEntity( void )
{
	BaseClass::AddEntity();

	QAngle vTempAngles = GetLocalAngles();
	vTempAngles[PITCH] = m_angEyeAngles[PITCH];

	SetLocalAngles( vTempAngles );

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
	if ( !IsVisible() || IsEffectActive( EF_NOSHADOW ) )
		return SHADOWS_NONE;

	static ConVarRef drawOwnShadow( "cl_drawownshadow" );
	if ( IsLocalPlayer() && !C_BasePlayer::ShouldDrawLocalPlayer() && !drawOwnShadow.GetBool() )
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

C_BaseEntity *C_HL2MP_Player::GetRagdollEntity() const
{
    return m_hRagdoll.Get();
}

bool C_HL2MP_Player::ShouldDraw( void )
{
    if (GetMoveType() == MOVETYPE_OBSERVER || IsEffectActive(EF_NODRAW))
        return false;
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

	static EHANDLE s_hClassEntity[MAX_EDICTS];
	static int s_iClassID[MAX_EDICTS] = {};
	const int iEntityIndex = entindex();
	bool bClassChanged = false;
	if ( iEntityIndex > 0 && iEntityIndex < MAX_EDICTS )
	{
		if ( s_hClassEntity[iEntityIndex] != this )
		{
			s_hClassEntity[iEntityIndex] = this;
			s_iClassID[iEntityIndex] = m_iHL2SBPlayerClassID;
			bClassChanged = m_iHL2SBPlayerClassID != 0;
		}
		else if ( s_iClassID[iEntityIndex] != m_iHL2SBPlayerClassID )
		{
			s_iClassID[iEntityIndex] = m_iHL2SBPlayerClassID;
			bClassChanged = true;
		}
	}
	if ( bClassChanged )
	{
#if defined ( LUA_SDK )
		if ( L != NULL )
		{
		BEGIN_LUA_CALL_HOOK( "PlayerClassChanged" );
			lua_pushplayer( L, this );
			lua_pushinteger( L, m_iHL2SBPlayerClassID );
		END_LUA_CALL_HOOK( 2, 0 );
		}
#endif
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
// HL2SB (2026-09-28): GMod's aux-suit-power switch (see game/shared/ammodef.cpp).
extern ConVar gmod_suit;

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

	// HL2SB (2026-09-28): the sprint start cue is part of the aux suit power
	// experience -- gated behind gmod_suit, matching GMod's client.dll
	// (the HL2Player.SprintStart emit sits inside its convar check).
	if ( gmod_suit.GetBool() )
	{
		CPASAttenuationFilter filter( this );
		filter.UsePredictionRules();
		EmitSound( filter, entindex(), "HL2Player.SprintStart" );
	}

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
	// HL2SB (2026-09-27): the old C++ act camera lived here.  GMod's act view is
	// Lua -- gamemodes/deathmatch/gamemode/taunt_camera.lua through GM:CalcView
	// (hooked from baseplayer_shared.cpp), keyed on the replicated taunt clock
	// (HL2SB_IsPlayingTaunt) -- so the hand-rolled orbit/trace math is gone.

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
	// HL2SB: corpse skin/bodygroups - the server copies the dead player's
	// values into these (GMod's CreateRagdollEntity does the same) and the
	// client-side ragdoll renders with them.
	RecvPropInt( RECVINFO( m_nSkin ) ),
	RecvPropInt( RECVINFO( m_nBody ) ),
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

	const bool bCreated = InitAsClientRagdoll( boneDelta0, boneDelta1, currentBones, boneDt );
#if defined( LUA_SDK )
	if ( bCreated && pPlayer && !pPlayer->IsDormant() && L != NULL )
	{
		BEGIN_LUA_CALL_HOOK( "CreateClientsideRagdoll" );
			lua_pushplayer( L, pPlayer );
			lua_pushentity( L, this );
		END_LUA_CALL_HOOK( 2, 0 );
	}
#endif
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

	// HL2SB (2026-10-14): the GM:UpdateAnimation dispatch used to live here,
	// but PostThink does not run for the LOCAL player - the local rider never
	// got the gamemode's seat-pose work (the pod aim_yaw pin, the steer
	// conversion), which is what bent his arms in third person.  The dispatch
	// now runs from UpdateClientSideAnimation() below, which reaches every
	// player every frame exactly once and runs after the C++ animstate wrote
	// its own pose parameters - the same "gamemode wins" order the reference
	// has.

	// Store the eye angles pitch so the client can compute its animation state correctly.
	m_angEyeAngles = EyeAngles();
}

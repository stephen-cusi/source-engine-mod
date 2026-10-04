//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=============================================================================//

#include "cbase.h"
#include "luamanager.h"
// HL2SB: GM:CanExitVehicle / GM:CanPlayerEnterVehicle dispatch
#include "lbaseentity_shared.h"

#include "npcevent.h"
#include "vehicle_base.h"
#include "engine/IEngineSound.h"
#include "in_buttons.h"
#include "soundenvelope.h"
#include "soundent.h"
#include "physics_saverestore.h"
#include "vphysics/constraints.h"
#include "vcollide_parse.h"
#include "ndebugoverlay.h"
#include "hl2_player.h"
#include "props.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#define	VEHICLE_HITBOX_DRIVER		1


//
// Anim events.
//
enum
{
	AE_POD_OPEN = 1,	// The pod is now open and can be entered or exited.
	AE_POD_CLOSE = 2,	// The pod is now closed and cannot be entered or exited.
};


extern ConVar g_debug_vehicledriver;


class CPropVehiclePrisonerPod;


// Pod bones that have physics followers
static const char *pPodFollowerBoneNames[] =
{
	"base",
};


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
class CPrisonerPodServerVehicle : public CBaseServerVehicle
{
	typedef CBaseServerVehicle BaseClass;

// IServerVehicle
public:
	void GetVehicleViewPosition( int nRole, Vector *pAbsOrigin, QAngle *pAbsAngles, float *pFOV = NULL );
	virtual void ItemPostFrame( CBasePlayer *pPlayer );

	virtual bool	IsPassengerEntering( void ) { return false; }	// NOTE: This mimics the scenario HL2 would have seen
	virtual bool	IsPassengerExiting( void ) { return false; }

protected:

	CPropVehiclePrisonerPod *GetPod( void );
};


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
class CPropVehiclePrisonerPod : public CPhysicsProp, public IDrivableVehicle
{
	DECLARE_CLASS( CPropVehiclePrisonerPod, CPhysicsProp );

public:
	DECLARE_DATADESC();
	DECLARE_SERVERCLASS();

	CPropVehiclePrisonerPod( void )
	{
		m_ServerVehicle.SetVehicle( this );

		// HL2SB: GMod's `limitview` key. true = the SDK's clamped pod view; GMod (and
		// HL2SB's own seat list) spawn every seat with `limitview 0` so the player's view
		// angles stay free - that is what lets the vehicle third person camera sit behind
		// the vehicle instead of being pinned to the pod's own downward pitch.
		// Set HERE, in the constructor: the keyvalue is applied after construction and
		// before Spawn(), so a default written in Spawn() would clobber it.
		m_bLimitView = true;

		// HL2SB (sbrust): GMod's DT defaults -- first person, the distance
		// multiplier whose zero frames the vehicle at its render-bounds radius.
		m_bThirdPersonMode = false;
		m_flCameraDistance = 0.0f;
		m_bHL2SBDuckHeld = false;
		Q_strncpy( m_szVehicleClass.GetForModify(), "", 64 );
	}

	~CPropVehiclePrisonerPod( void )
	{
	}

	// CBaseEntity
	virtual void	Precache( void );
	void			Spawn( void );
	void			Think(void);
	virtual int		ObjectCaps( void ) { return BaseClass::ObjectCaps() | FCAP_IMPULSE_USE; };
	virtual void	Use( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value );
	virtual void	DrawDebugGeometryOverlays( void );

	virtual Vector	BodyTarget( const Vector &posSrc, bool bNoisy = true );
	virtual void	TraceAttack( const CTakeDamageInfo &info, const Vector &vecDir, trace_t *ptr, CDmgAccumulator *pAccumulator );
	virtual int		OnTakeDamage( const CTakeDamageInfo &info );

	void			PlayerControlInit( CBasePlayer *pPlayer );
	void			PlayerControlShutdown( void );
	void			ResetUseKey( CBasePlayer *pPlayer );

	virtual bool OverridePropdata() { return true; }

	void			GetVectors(Vector* pForward, Vector* pRight, Vector* pUp) const;

	bool ShouldForceExit() { return m_bForcedExit; }
	void ClearForcedExit() { m_bForcedExit = false; }

	// HL2SB (sbrust): GMod keeps the third person camera state and the vehicle
	// TABLE name ON the vehicle (their Vehicle:SetThirdPersonMode /
	// SetCameraDistance / SetVehicleClass are DT slot writes), and the seat pose
	// resolves through list "Vehicles" keyed by GetVehicleClass().  This pod is
	// not a CPropVehicleDriveable in this fork (CPhysicsProp + IDrivableVehicle),
	// so it carries the same trio itself.  Appended fields + non-virtual
	// accessors: no vtable slot anywhere (iron rule).  The writer is
	// HL2SB_UpdateCameraState (the C++ port of GMod's base-gamemode
	// GM:VehicleMove: IN_DUCK press edge flips the mode, the usercmd mouse wheel
	// drives the distance multiplier), run from CPrisonerPodServerVehicle::
	// ItemPostFrame -- the pod's driver control tick.
	bool	HL2SB_GetThirdPersonMode( void ) const { return m_bThirdPersonMode; }
	float	HL2SB_GetCameraDistance( void ) const { return m_flCameraDistance; }
	void	HL2SB_SetThirdPersonMode( bool bEnable ) { m_bThirdPersonMode = bEnable; }
	void	HL2SB_SetCameraDistance( float flDistance ) { m_flCameraDistance = flDistance; }
	const char *HL2SB_GetVehicleClass( void ) const { return m_szVehicleClass; }
	void	HL2SB_SetVehicleClass( const char *pszClass );
	void	HL2SB_UpdateCameraState( CBasePlayer *pDriver );

	// CBaseAnimating
	void HandleAnimEvent( animevent_t *pEvent );

	// Inputs
	void InputEnterVehicleImmediate( inputdata_t &inputdata );
	void InputEnterVehicle( inputdata_t &inputdata );
	void InputExitVehicle( inputdata_t &inputdata );
	void InputLock( inputdata_t &inputdata );
	void InputUnlock( inputdata_t &inputdata );
	// HL2SB (sbrust): vehicle TABLE name keyvalue input (see m_szVehicleClass).
	void InputVehicleClass( inputdata_t &inputdata );
	void InputOpen( inputdata_t &inputdata );
	void InputClose( inputdata_t &inputdata );

	CNetworkHandle( CBasePlayer, m_hPlayer );

// IDrivableVehicle
public:

	virtual bool PassengerShouldReceiveDamage( CTakeDamageInfo &info ) 
	{ 
		if ( info.GetDamageType() & DMG_VEHICLE )
			return true;

		return (info.GetDamageType() & (DMG_RADIATION|DMG_BLAST) ) == 0; 
	}

	virtual CBaseEntity *GetDriver( void );
	virtual void ProcessMovement( CBasePlayer *pPlayer, CMoveData *pMoveData ) { return; }
	virtual void FinishMove( CBasePlayer *player, CUserCmd *ucmd, CMoveData *move ) { return; }
	virtual bool CanEnterVehicle( CBaseEntity *pEntity );
	virtual bool CanExitVehicle( CBaseEntity *pEntity );
	virtual void UpdateOnRemove( void );
	virtual void SetVehicleEntryAnim( bool bOn );
	virtual void SetVehicleExitAnim( bool bOn, Vector vecEyeExitEndpoint ) { m_bExitAnimOn = bOn; if ( bOn ) m_vecEyeExitEndpoint = vecEyeExitEndpoint; }
	virtual void EnterVehicle( CBaseCombatCharacter *pPassenger );

	virtual bool AllowBlockedExit( CBaseCombatCharacter *pPassenger, int nRole ) { return true; }
	virtual bool AllowMidairExit( CBaseCombatCharacter *pPassenger, int nRole ) { return true; }
	virtual void PreExitVehicle( CBaseCombatCharacter *pPassenger, int nRole ) {}
	virtual void ExitVehicle( int nRole );

	virtual void ItemPostFrame( CBasePlayer *pPlayer ) {}
	virtual void SetupMove( CBasePlayer *player, CUserCmd *ucmd, IMoveHelper *pHelper, CMoveData *move ) {}
	virtual string_t GetVehicleScriptName() { return m_vehicleScript; }
	
	// If this is a vehicle, returns the vehicle interface
	virtual IServerVehicle *GetServerVehicle() { return &m_ServerVehicle; }

protected:

	// Contained IServerVehicle
	CPrisonerPodServerVehicle m_ServerVehicle;

private:

	// Entering / Exiting
	bool				m_bLocked;
	CNetworkVar( bool,	m_bEnterAnimOn );
	CNetworkVar( bool,	m_bExitAnimOn );
	CNetworkVector(		m_vecEyeExitEndpoint );
	bool				m_bForcedExit;

	// HL2SB: GMod's `limitview` key (Valve's own VDC keyvalue for prop_vehicle_prisoner_pod).
	// Networked because the clamp it controls lives on the CLIENT
	// (C_PropVehiclePrisonerPod::UpdateViewAngles, game/client/hl2/c_vehicle_prisoner_pod.cpp).
	CNetworkVar( bool,	m_bLimitView );

	// HL2SB (sbrust): GMod's per-vehicle camera state + vehicle TABLE name (the
	// seat-pose lookup key).  Appended at the class end; the SendProps go at the
	// END of DT_PropVehiclePrisonerPod and the client RecvProps at the END of its
	// recv table -- matched BY NAME.
	CNetworkVar( bool,	m_bThirdPersonMode );
	CNetworkVar( float,	m_flCameraDistance );
	CNetworkString(	m_szVehicleClass, 64 );

	// CTRL held-bit shadow for HL2SB_UpdateCameraState (not networked).
	bool				m_bHL2SBDuckHeld;

	// Vehicle script filename
	string_t			m_vehicleScript;

	COutputEvent		m_playerOn;
	COutputEvent		m_playerOff;
	COutputEvent		m_OnOpen;
	COutputEvent		m_OnClose;
};

LINK_ENTITY_TO_CLASS( prop_vehicle_prisoner_pod, CPropVehiclePrisonerPod );


BEGIN_DATADESC( CPropVehiclePrisonerPod )

	// Inputs
	DEFINE_INPUTFUNC( FIELD_VOID, "Lock",	InputLock ),
	DEFINE_INPUTFUNC( FIELD_VOID, "Unlock",	InputUnlock ),
	// HL2SB (sbrust): the vehicle TABLE name keyvalue (see m_szVehicleClass).
	DEFINE_INPUTFUNC( FIELD_STRING, "VehicleClass", InputVehicleClass ),
	DEFINE_INPUTFUNC( FIELD_VOID, "EnterVehicle", InputEnterVehicle ),
	DEFINE_INPUTFUNC( FIELD_VOID, "EnterVehicleImmediate", InputEnterVehicleImmediate ),
	DEFINE_INPUTFUNC( FIELD_VOID, "ExitVehicle", InputExitVehicle ),
	DEFINE_INPUTFUNC( FIELD_VOID, "Open", InputOpen ),
	DEFINE_INPUTFUNC( FIELD_VOID, "Close", InputClose ),

	// Keys
	DEFINE_EMBEDDED( m_ServerVehicle ),

	DEFINE_FIELD( m_hPlayer, FIELD_EHANDLE ),
	DEFINE_FIELD( m_bEnterAnimOn, FIELD_BOOLEAN ),
	DEFINE_FIELD( m_bExitAnimOn, FIELD_BOOLEAN ),
	DEFINE_FIELD( m_bForcedExit, FIELD_BOOLEAN ),
 	DEFINE_FIELD( m_vecEyeExitEndpoint, FIELD_POSITION_VECTOR ),

	DEFINE_KEYFIELD( m_vehicleScript, FIELD_STRING, "vehiclescript" ),
	DEFINE_KEYFIELD( m_bLocked, FIELD_BOOLEAN, "vehiclelocked" ),
	// HL2SB: GMod's `limitview` key (`ent_create prop_vehicle_prisoner_pod ... limitview 0`).
	DEFINE_KEYFIELD( m_bLimitView, FIELD_BOOLEAN, "limitview" ),
	// HL2SB (sbrust): the vehicle TABLE name (GMod's list "Vehicles" key that the
	// seat pose lookup in animations.lua HandlePlayerDriving consumes -- GMod
	// writes it with SetVehicleClass at spawn time, gamemodes/sandbox/gamemode/
	// commands.lua:1057).  NOT a keyfield: the datamap FIELD_CHARACTER key
	// path writes a single byte into the array, so the spawn menu applies the
	// `vehicleclass` KeyValue explicitly (GM_SpawnAtEyeTrace,
	// game/server/hl2sb_gm_commands.cpp).  The VehicleClass input function
	// below covers map entities and outputs; the array keeps save/restore.
	DEFINE_ARRAY( m_szVehicleClass, FIELD_CHARACTER, 64 ),

	DEFINE_OUTPUT( m_playerOn, "PlayerOn" ),
	DEFINE_OUTPUT( m_playerOff, "PlayerOff" ),
	DEFINE_OUTPUT( m_OnOpen, "OnOpen" ),
	DEFINE_OUTPUT( m_OnClose, "OnClose" ),

END_DATADESC()

IMPLEMENT_SERVERCLASS_ST(CPropVehiclePrisonerPod, DT_PropVehiclePrisonerPod)
	SendPropEHandle(SENDINFO(m_hPlayer)),
	SendPropBool(SENDINFO(m_bEnterAnimOn)),
	SendPropBool(SENDINFO(m_bExitAnimOn)),
	SendPropVector(SENDINFO(m_vecEyeExitEndpoint), -1, SPROP_COORD),
	// HL2SB: must stay LAST - the recv table
	// (game/client/hl2/c_vehicle_prisoner_pod.cpp) appends its RecvPropBool in the same
	// position.
	SendPropBool(SENDINFO(m_bLimitView)),

	// HL2SB (sbrust): camera state + vehicle table name appended at the END
	// (props match by name; the client table carries the same three).
	SendPropBool(SENDINFO(m_bThirdPersonMode)),
	SendPropFloat(SENDINFO(m_flCameraDistance), 0, SPROP_NOSCALE ),
	SendPropString( SENDINFO(m_szVehicleClass) ),
END_SEND_TABLE();


//------------------------------------------------
// Precache
//------------------------------------------------
void CPropVehiclePrisonerPod::Precache( void )
{
	BaseClass::Precache();

	PrecacheScriptSound( "d3_citadel.pod_open" );
	PrecacheScriptSound( "d3_citadel.pod_close" );

	m_ServerVehicle.Initialize( STRING(m_vehicleScript) );
}


//------------------------------------------------
// Spawn
//------------------------------------------------
void CPropVehiclePrisonerPod::Spawn( void )
{
	Precache();
	SetModel( STRING( GetModelName() ) );
	SetCollisionGroup( COLLISION_GROUP_VEHICLE );

	BaseClass::Spawn();

	m_takedamage = DAMAGE_EVENTS_ONLY;

	SetNextThink( gpGlobals->curtime );
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CPropVehiclePrisonerPod::TraceAttack( const CTakeDamageInfo &info, const Vector &vecDir, trace_t *ptr, CDmgAccumulator *pAccumulator )
{
	if ( ptr->hitbox == VEHICLE_HITBOX_DRIVER )
	{
		if ( m_hPlayer != NULL )
		{
			m_hPlayer->TakeDamage( info );
		}
	}
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
int CPropVehiclePrisonerPod::OnTakeDamage( const CTakeDamageInfo &inputInfo )
{
	// Do scaled up physics damage to the pod
	CTakeDamageInfo info = inputInfo;
	info.ScaleDamage( 25 );

	// reset the damage
	info.SetDamage( inputInfo.GetDamage() );

	// Check to do damage to prisoner
	if ( m_hPlayer != NULL )
	{
		// Take no damage from physics damages
		if ( info.GetDamageType() & DMG_CRUSH )
			return 0;

		// Take the damage
		m_hPlayer->TakeDamage( info );
	}

	return 0;
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
Vector CPropVehiclePrisonerPod::BodyTarget( const Vector &posSrc, bool bNoisy )
{
	Vector	shotPos;
	matrix3x4_t	matrix;

	int eyeAttachmentIndex = LookupAttachment("vehicle_driver_eyes");
	GetAttachment( eyeAttachmentIndex, matrix );
	MatrixGetColumn( matrix, 3, shotPos );

	if ( bNoisy )
	{
		shotPos[0] += random->RandomFloat( -8.0f, 8.0f );
		shotPos[1] += random->RandomFloat( -8.0f, 8.0f );
		shotPos[2] += random->RandomFloat( -8.0f, 8.0f );
	}

	return shotPos;
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CPropVehiclePrisonerPod::Think(void)
{
	SetNextThink( gpGlobals->curtime + 0.1 );

	if ( GetDriver() )
	{
		BaseClass::Think();
		
		// If the enter or exit animation has finished, tell the server vehicle
		if ( IsSequenceFinished() && (m_bExitAnimOn || m_bEnterAnimOn) )
		{
			GetServerVehicle()->HandleEntryExitFinish( m_bExitAnimOn, true );
		}
	}

	StudioFrameAdvance();
	DispatchAnimEvents( this );
}


//------------------------------------------------------------------------------
// Purpose:
//------------------------------------------------------------------------------
void CPropVehiclePrisonerPod::InputOpen( inputdata_t &inputdata )
{
	int nSequence = LookupSequence( "open" );

	// Set to the desired anim, or default anim if the desired is not present
	if ( nSequence > ACTIVITY_NOT_AVAILABLE )
	{
		SetCycle( 0 );
		m_flAnimTime = gpGlobals->curtime;
		ResetSequence( nSequence );
		ResetClientsideFrame();
		EmitSound( "d3_citadel.pod_open" );
	}
	else
	{
		// Not available try to get default anim
		Msg( "Prisoner pod %s: missing open sequence\n", GetDebugName() );
		SetSequence( 0 );
	}
}


//------------------------------------------------------------------------------
// Purpose:
//------------------------------------------------------------------------------
void CPropVehiclePrisonerPod::InputClose( inputdata_t &inputdata )
{
	// The enter anim closes the pod, so don't do this redundantly!
	if ( m_bLocked || m_bEnterAnimOn )
		return;

	int nSequence = LookupSequence( "close" );

	// Set to the desired anim, or default anim if the desired is not present
	if ( nSequence > ACTIVITY_NOT_AVAILABLE )
	{
		SetCycle( 0 );
		m_flAnimTime = gpGlobals->curtime;
		ResetSequence( nSequence );
		ResetClientsideFrame();
		EmitSound( "d3_citadel.pod_close" );
	}
	else
	{
		// Not available try to get default anim
		Msg( "Prisoner pod %s: missing close sequence\n", GetDebugName() );
		SetSequence( 0 );
	}
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CPropVehiclePrisonerPod::HandleAnimEvent( animevent_t *pEvent )
{
	if ( pEvent->event == AE_POD_OPEN )
	{
		m_OnOpen.FireOutput( this, this );
		m_bLocked = false;
	}
	else if ( pEvent->event == AE_POD_CLOSE )
	{
		m_OnClose.FireOutput( this, this );
		m_bLocked = true;
	}
}


//-----------------------------------------------------------------------------
// HL2SB: this class derives from CPhysicsProp, not from CPropVehicleDriveable,
// so the driveable's passenger-ejecting UpdateOnRemove never ran here:
// removing an occupied pod (undo, cleanup, kill) deleted the entity out from
// under a rider still parented to it in MOVETYPE_NOCLIP with the vehicle's
// view - the "undo while seated goes black" report.  Mirror the driveable eject.
//-----------------------------------------------------------------------------
void CPropVehiclePrisonerPod::UpdateOnRemove( void )
{
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CBasePlayer *pPlayer = UTIL_PlayerByIndex( i );
		if ( pPlayer && pPlayer->GetVehicleEntity() == this )
		{
			// Eject where the player already is; LeaveVehicle() steps up by
			// itself when the vehicle can no longer supply an exit point.
			pPlayer->LeaveVehicle( pPlayer->GetAbsOrigin(), pPlayer->GetAbsAngles() );
		}
	}

	BaseClass::UpdateOnRemove();
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CPropVehiclePrisonerPod::Use( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value )
{
	CBasePlayer *pPlayer = ToBasePlayer( pActivator );
	if ( !pPlayer )
		return;

	ResetUseKey( pPlayer );

	GetServerVehicle()->HandlePassengerEntry( pPlayer, (value > 0) );
}


//-----------------------------------------------------------------------------
// Purpose: Return true of the player's allowed to enter / exit the vehicle
//-----------------------------------------------------------------------------
bool CPropVehiclePrisonerPod::CanEnterVehicle( CBaseEntity *pEntity )
{
	// Prevent entering if the vehicle's being driven by an NPC
	if ( GetDriver() && GetDriver() != pEntity )
		return false;

	// Prevent entering if the vehicle's locked
	return !m_bLocked;
}


//-----------------------------------------------------------------------------
// HL2SB: the SDK's CanExitVehicle tested this class's own m_bLocked, and the
// pod's "close" sequence fires AE_POD_CLOSE, whose handler sets exactly that
// flag - a menu-spawned pod locked itself the moment its entry animation
// finished and +use (E) could never leave it.  The reference behavior keeps
// the same override shape but WITHOUT the lock term:
// GM:CanExitVehicle hook first (returning false denies, nil/true fall
// through), then zero local angular velocity and no entry/exit animation
// playing.  The hatch lock only gates re-entering through the use path
// (CanEnterVehicle above), it must not trap the rider.
//-----------------------------------------------------------------------------
bool CPropVehiclePrisonerPod::CanExitVehicle( CBaseEntity *pEntity )
{
	BEGIN_LUA_CALL_HOOK( "CanExitVehicle" );
		lua_pushentity( L, this );
		lua_pushentity( L, pEntity );
	END_LUA_CALL_HOOK( 2, 1 );

	bool bAllowed = true;
	if ( lua_isboolean( L, -1 ) )
		bAllowed = lua_toboolean( L, -1 ) != 0;
	lua_pop( L, 1 );

	if ( bAllowed == false )
		return false;

	return ( (GetLocalAngularVelocity() == vec3_angle) && !m_bEnterAnimOn && !m_bExitAnimOn );
}


//-----------------------------------------------------------------------------
// Purpose: Override base class to add display
//-----------------------------------------------------------------------------
void CPropVehiclePrisonerPod::DrawDebugGeometryOverlays(void) 
{
	// Draw if BBOX is on
	if ( m_debugOverlays & OVERLAY_BBOX_BIT )
	{
	}

	BaseClass::DrawDebugGeometryOverlays();
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CPropVehiclePrisonerPod::EnterVehicle( CBaseCombatCharacter *pPassenger )
{
	if ( pPassenger == NULL )
		return;

	CBasePlayer *pPlayer = ToBasePlayer( pPassenger );
	if ( pPlayer != NULL )
	{
		// Remove any player who may be in the vehicle at the moment
		if ( m_hPlayer )
		{
			ExitVehicle( VEHICLE_ROLE_DRIVER );
		}

		m_hPlayer = pPlayer;
		m_playerOn.FireOutput( pPlayer, this, 0 );

		m_ServerVehicle.SoundStart();
	}
	else
	{
		// NPCs are not supported yet - jdw
		Assert( 0 );
	}
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CPropVehiclePrisonerPod::SetVehicleEntryAnim( bool bOn )
{
	m_bEnterAnimOn = bOn;
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CPropVehiclePrisonerPod::ExitVehicle( int nRole )
{
	CBasePlayer *pPlayer = m_hPlayer;
	if ( !pPlayer )
		return;

	m_hPlayer = NULL;
	ResetUseKey( pPlayer );

	m_playerOff.FireOutput( pPlayer, this, 0 );
	m_bEnterAnimOn = false;
	// HL2SB: end every completed exit UNLOCKED.  The entry sequence locks the
	// hatch (AE_POD_CLOSE) and the exit sequence is what unlocks it; if that
	// event is ever missed (the anim got interrupted or replaced), the lock
	// would silently refuse every later +use entry with nothing in the log.
	m_bLocked = false;

	m_ServerVehicle.SoundShutdown( 1.0 );
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CPropVehiclePrisonerPod::ResetUseKey( CBasePlayer *pPlayer )
{
	pPlayer->m_afButtonPressed &= ~IN_USE;
}


//-----------------------------------------------------------------------------
// Purpose: Vehicles are permanently oriented off angle for vphysics.
//-----------------------------------------------------------------------------
void CPropVehiclePrisonerPod::GetVectors(Vector* pForward, Vector* pRight, Vector* pUp) const
{
	// This call is necessary to cause m_rgflCoordinateFrame to be recomputed
	const matrix3x4_t &entityToWorld = EntityToWorldTransform();

	if (pForward != NULL)
	{
		MatrixGetColumn( entityToWorld, 1, *pForward ); 
	}

	if (pRight != NULL)
	{
		MatrixGetColumn( entityToWorld, 0, *pRight ); 
	}

	if (pUp != NULL)
	{
		MatrixGetColumn( entityToWorld, 2, *pUp ); 
	}
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
CBaseEntity *CPropVehiclePrisonerPod::GetDriver( void ) 
{ 
	return m_hPlayer; 
}

//-----------------------------------------------------------------------------
// Purpose: Prevent the player from entering / exiting the vehicle
//-----------------------------------------------------------------------------
void CPropVehiclePrisonerPod::InputLock( inputdata_t &inputdata )
{
	m_bLocked = true;
}


//-----------------------------------------------------------------------------
// Purpose: Allow the player to enter / exit the vehicle
//-----------------------------------------------------------------------------
void CPropVehiclePrisonerPod::InputUnlock( inputdata_t &inputdata )
{
	m_bLocked = false;
}


//-----------------------------------------------------------------------------
// HL2SB (sbrust): the vehicle TABLE name (GMod's list "Vehicles" key for the
// seat pose lookup) + the third person camera state, GMod's per-vehicle
// pattern: their Vehicle:SetVehicleClass / SetThirdPersonMode /
// SetCameraDistance are DT slot writes on the vehicle (gamemodes/base/gamemode/
// entity.lua:666), and sandbox writes the spawnmenu's table name at spawn
// (gamemodes/sandbox/gamemode/commands.lua:1057).  The fork's SMenu passes the
// same value as the `vehicleclass` keyvalue, and HL2SB_SetVehicleClass is the
// single C++ entry point both paths share.  This pod class is local to this
// file, so the accessors are exported as free functions for the Vehicle Lua
// dispatch (game/shared/lua/lvehicle_shared.cpp) -- same mechanism the client
// half of the pod file uses.
//-----------------------------------------------------------------------------
void CPropVehiclePrisonerPod::InputVehicleClass( inputdata_t &inputdata )
{
	HL2SB_SetVehicleClass( inputdata.value.String() );
}

void CPropVehiclePrisonerPod::HL2SB_SetVehicleClass( const char *pszClass )
{
	Q_strncpy( m_szVehicleClass.GetForModify(), pszClass ? pszClass : "", 64 );
}

void CPropVehiclePrisonerPod::HL2SB_UpdateCameraState( CBasePlayer *pDriver )
{
	if ( pDriver == NULL )
		return;

	const bool bDuckHeld = ( pDriver->m_nButtons & IN_DUCK ) != 0;
	if ( bDuckHeld && !m_bHL2SBDuckHeld )
	{
		m_bThirdPersonMode = !m_bThirdPersonMode;
	}
	m_bHL2SBDuckHeld = bDuckHeld;

	const int iWheel = pDriver->HL2SB_GetUserCmdMouseWheel();
	if ( iWheel != 0 )
	{
		float flDistance = m_flCameraDistance;
		flDistance = clamp( flDistance - iWheel * 0.03f * ( 1.1f + flDistance ), -1.0f, 10.0f );
		m_flCameraDistance = flDistance;

		// HL2SB (sbrust): throttled wheel diagnostic (hl2sb_veh_thirdperson_debug 1).
		static float s_flNextWheelLine = 0.0f;
		extern ConVar hl2sb_vehicle_anim_debug;
		if ( hl2sb_vehicle_anim_debug.GetBool() && gpGlobals->curtime >= s_flNextWheelLine )
		{
			s_flNextWheelLine = gpGlobals->curtime + 0.5f;
			Msg( "[HL2SB vehcam] pod wheel=%d dist=%.3f\n", iWheel, flDistance );
		}
	}
}

bool HL2SB_IsPrisonerPodEntity( CBaseEntity *pEntity )
{
	return dynamic_cast< CPropVehiclePrisonerPod * >( pEntity ) != NULL;
}

bool HL2SB_PodGetThirdPerson( CBaseEntity *pEntity )
{
	CPropVehiclePrisonerPod *pPod = dynamic_cast< CPropVehiclePrisonerPod * >( pEntity );
	return pPod ? pPod->HL2SB_GetThirdPersonMode() : false;
}

float HL2SB_PodGetCameraDistance( CBaseEntity *pEntity )
{
	CPropVehiclePrisonerPod *pPod = dynamic_cast< CPropVehiclePrisonerPod * >( pEntity );
	return pPod ? pPod->HL2SB_GetCameraDistance() : 0.0f;
}

void HL2SB_PodSetThirdPerson( CBaseEntity *pEntity, bool bEnable )
{
	CPropVehiclePrisonerPod *pPod = dynamic_cast< CPropVehiclePrisonerPod * >( pEntity );
	if ( pPod )
		pPod->HL2SB_SetThirdPersonMode( bEnable );
}

void HL2SB_PodSetCameraDistance( CBaseEntity *pEntity, float flDistance )
{
	CPropVehiclePrisonerPod *pPod = dynamic_cast< CPropVehiclePrisonerPod * >( pEntity );
	if ( pPod )
		pPod->HL2SB_SetCameraDistance( flDistance );
}

const char *HL2SB_PodGetVehicleClass( CBaseEntity *pEntity )
{
	CPropVehiclePrisonerPod *pPod = dynamic_cast< CPropVehiclePrisonerPod * >( pEntity );
	return pPod ? pPod->HL2SB_GetVehicleClass() : NULL;
}

void HL2SB_PodSetVehicleClass( CBaseEntity *pEntity, const char *pszClass )
{
	CPropVehiclePrisonerPod *pPod = dynamic_cast< CPropVehiclePrisonerPod * >( pEntity );
	if ( pPod )
		pPod->HL2SB_SetVehicleClass( pszClass );
}

// HL2SB (sbrust): the spawn-menu path for the vehicle TABLE name (GMod's
// SetVehicleClass(VName) at spawn -- gamemodes/sandbox/gamemode/commands.lua:1057).
// The datamap FIELD_CHARACTER key-value path writes one byte into an array, so
// gm_spawnvehicle applies this key explicitly here, onto either vehicle kind.
void HL2SB_SetEntityVehicleClass( CBaseEntity *pEntity, const char *pszClass )
{
	CPropVehicleDriveable *pDriveable = dynamic_cast< CPropVehicleDriveable * >( pEntity );
	if ( pDriveable != NULL )
	{
		pDriveable->HL2SB_SetVehicleClass( pszClass );
		return;
	}
	HL2SB_PodSetVehicleClass( pEntity, pszClass );
}


//-----------------------------------------------------------------------------
// Purpose: Force the player to enter the vehicle.
//-----------------------------------------------------------------------------
void CPropVehiclePrisonerPod::InputEnterVehicle( inputdata_t &inputdata )
{
	if ( m_bEnterAnimOn )
		return;

	// Try the activator first & use them if they are a player.
	CBaseCombatCharacter *pPassenger = ToBaseCombatCharacter( inputdata.pActivator );
	if ( pPassenger == NULL )
	{
		// Activator was not a player, just grab the singleplayer player.
		pPassenger = UTIL_PlayerByIndex( 1 );
		if ( pPassenger == NULL )
			return;
	}

	// FIXME: I hate code like this. I should really add a parameter to HandlePassengerEntry
	//		  to allow entry into locked vehicles
	bool bWasLocked = m_bLocked;
	m_bLocked = false;
	GetServerVehicle()->HandlePassengerEntry( pPassenger, true );
	m_bLocked = bWasLocked;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : &inputdata - 
//-----------------------------------------------------------------------------
void CPropVehiclePrisonerPod::InputEnterVehicleImmediate( inputdata_t &inputdata )
{
	if ( m_bEnterAnimOn )
		return;

	// Try the activator first & use them if they are a player.
	CBaseCombatCharacter *pPassenger = ToBaseCombatCharacter( inputdata.pActivator );
	if ( pPassenger == NULL )
	{
		// Activator was not a player, just grab the singleplayer player.
		pPassenger = UTIL_PlayerByIndex( 1 );
		if ( pPassenger == NULL )
			return;
	}

	CBasePlayer *pPlayer = ToBasePlayer( pPassenger );
	if ( pPlayer != NULL )
	{
		if ( pPlayer->IsInAVehicle() )
		{
			// Force the player out of whatever vehicle they are in.
			pPlayer->LeaveVehicle();
		}
		
		// HL2SB: wrapper so the enter hooks fire here too
		pPlayer->EnterVehicle( GetServerVehicle(), VEHICLE_ROLE_DRIVER );
	}
	else
	{
		// NPCs are not currently supported - jdw
		Assert( 0 );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Force the player to exit the vehicle.
//-----------------------------------------------------------------------------
void CPropVehiclePrisonerPod::InputExitVehicle( inputdata_t &inputdata )
{
	m_bForcedExit = true;
}


//========================================================================================================================================
// CRANE VEHICLE SERVER VEHICLE
//========================================================================================================================================
CPropVehiclePrisonerPod *CPrisonerPodServerVehicle::GetPod( void )
{
	return (CPropVehiclePrisonerPod *)GetDrivableVehicle();
}


//-----------------------------------------------------------------------------
// Purpose: 
// Input  : pPlayer - 
//-----------------------------------------------------------------------------
void CPrisonerPodServerVehicle::ItemPostFrame( CBasePlayer *player )
{
	Assert( player == GetDriver() );

	GetDrivableVehicle()->ItemPostFrame( player );

	// HL2SB: raw held button, not the pressed edge - PlayerUse (ItemPreFrame)
	// runs first, selects the pod the driver sits inside (the eye trace starts
	// solid) and ResetUseKey() wipes the pressed edge.  Same gate as the generic
	// server vehicle; See CBaseServerVehicle::ItemPostFrame.
	if (( player->m_nButtons & IN_USE ) || GetPod()->ShouldForceExit() )
	{
		GetPod()->ClearForcedExit();
		if ( GetDrivableVehicle()->CanExitVehicle(player) )
		{
			// Let the vehicle try to play the exit animation
			if ( !HandlePassengerExit( player ) && ( player != NULL ) )
			{
				player->PlayUseDenySound();
			}
		}
	}

	// HL2SB (sbrust): this pod overrides ItemPostFrame entirely, so the third
	// person camera state writer that the generic CBaseServerVehicle::ItemPostFrame
	// runs for driveables must fire here too (GMod lets you CTRL-toggle and
	// wheel-zoom the seat camera; the seat is a driveable in GMod because the
	// pod class inherits it -- here it carries the state itself).
	GetPod()->HL2SB_UpdateCameraState( player );
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CPrisonerPodServerVehicle::GetVehicleViewPosition( int nRole, Vector *pAbsOrigin, QAngle *pAbsAngles, float *pFOV /*= NULL*/ )
{
	// FIXME: This needs to be reconciled with the other versions of this function!
	Assert( nRole == VEHICLE_ROLE_DRIVER );
	CBasePlayer *pPlayer = ToBasePlayer( GetDrivableVehicle()->GetDriver() );
	Assert( pPlayer );

	*pAbsAngles = pPlayer->EyeAngles(); // yuck. this is an in/out parameter.

	float flPitchFactor = 1.0;
	matrix3x4_t vehicleEyePosToWorld;
	Vector vehicleEyeOrigin;
	QAngle vehicleEyeAngles;
	GetPod()->GetAttachment( "vehicle_driver_eyes", vehicleEyeOrigin, vehicleEyeAngles );
	AngleMatrix( vehicleEyeAngles, vehicleEyePosToWorld );

	// Compute the relative rotation between the unperterbed eye attachment + the eye angles
	matrix3x4_t cameraToWorld;
	AngleMatrix( *pAbsAngles, cameraToWorld );

	matrix3x4_t worldToEyePos;
	MatrixInvert( vehicleEyePosToWorld, worldToEyePos );

	matrix3x4_t vehicleCameraToEyePos;
	ConcatTransforms( worldToEyePos, cameraToWorld, vehicleCameraToEyePos );

	// Now perterb the attachment point
	vehicleEyeAngles.x = RemapAngleRange( PITCH_CURVE_ZERO * flPitchFactor, PITCH_CURVE_LINEAR, vehicleEyeAngles.x );
	vehicleEyeAngles.z = RemapAngleRange( ROLL_CURVE_ZERO * flPitchFactor, ROLL_CURVE_LINEAR, vehicleEyeAngles.z );
	AngleMatrix( vehicleEyeAngles, vehicleEyeOrigin, vehicleEyePosToWorld );

	// Now treat the relative eye angles as being relative to this new, perterbed view position...
	matrix3x4_t newCameraToWorld;
	ConcatTransforms( vehicleEyePosToWorld, vehicleCameraToEyePos, newCameraToWorld );

	// output new view abs angles
	MatrixAngles( newCameraToWorld, *pAbsAngles );

	// UNDONE: *pOrigin would already be correct in single player if the HandleView() on the server ran after vphysics
	MatrixGetColumn( newCameraToWorld, 3, *pAbsOrigin );
}

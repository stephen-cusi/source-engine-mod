//========= Copyright � 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose:
//
// $NoKeywords: $
//=============================================================================//

#include "cbase.h"
#include "weapon_hl2mpbasehlmpcombatweapon.h"
#include "beam_shared.h"
#ifndef CLIENT_DLL
#include "player.h"
#endif
#include "gamerules.h"
#ifdef CLIENT_DLL
#include "clienteffectprecachesystem.h"
#endif
// HL2SB (2026-10-03): the ViewModelDrawn anchor type.  Client: the client
// view model class (which also pulls in the full C_BaseAnimating for the
// beam-end bone transform).  Server: the shared header the old code used.
#ifdef CLIENT_DLL
#include "c_baseviewmodel.h"
#else
#include "baseviewmodel.h"
#endif
#include "vphysics/constraints.h"
#include "physics.h"
#include "effect_dispatch_data.h"
#ifndef CLIENT_DLL
#include "te_effect_dispatch.h"		// HL2SB: server-side DispatchEffect( name, data )
#include "recipientfilter.h"
#endif
#include "in_buttons.h"
#include "basescripted.h"
#include "IEffects.h"
#include "soundenvelope.h"
#include "engine/IEngineSound.h"
#ifndef CLIENT_DLL
#include "ndebugoverlay.h"
#endif
#include "physics_saverestore.h"
#ifndef CLIENT_DLL
#include "player_pickup.h"
#endif
#include "SoundEmitterSystem/isoundemittersystembase.h"
#ifdef CLIENT_DLL
#include "model_types.h"
#include "c_baseanimating.h"	// HL2SB (2026-10-03): C_BaseAnimating full type - the
								// beam-end m_vecHoldPos transform reads GetBone/GetModelPtr
#include "view_shared.h"
#include "view.h"
#include "iviewrender.h"
#include "ragdoll.h"
#include "c_basehlcombatweapon.h"
#include "beamdraw.h"
#include "iviewrender_beams.h"	// HL2SB (2026-09-26): HL2 physcannon fork-tendril beams
#include "fx_interpvalue.h"		// HL2SB (2026-09-26): CInterpolatedValue sprite params
#include "iefx.h"		// HL2SB GMod compat: the held prop's full-body soft light (dlight)
#include "dlight.h"
#include "iinput.h"				// HL2SB GMod compat: E-rotate input interception
#include "materialsystem/imaterialsystem.h"
#include "texture_group_names.h"
#include "cdll_client_int.h"	// HL2SB GMod compat: modelrender (the viewmodel glow shell)

// HL2SB GMod compat (2026-09-24): local prototype -- the definition lives in
// lbaseplayer_shared.cpp; cl_weaponcolor drives the physgun beam/sprites,
// exactly like GMod13.
Vector HL2SB_GetWeaponColor( int iUserID );
#else
#include "physics_prop_ragdoll.h"
#include "props.h"
#include "basehlcombatweapon.h"
#include "ai_basenpc.h"	// HL2SB GMod compat: MyNPCPointer()->CanBecomeRagdoll() in the NPC-grab path
// HL2SB GMod compat: the physgun's held-prop glow sprite + the GMod physgun
// hooks (GM:PhysgunPickup / GM:PhysgunDrop).
#include "Sprite.h"
// HL2SB (2026-10-03): the networked beam entity this weapon drives.
#include "physgun_beam.h"
#endif

// HL2SB GMod compat: the GMod physgun hooks fire in BOTH realms (server:
// PhysgunPickup/OnPhysgunPickup/PhysgunDrop/OnPhysgunFreeze/OnPhysgunReload/
// GetPreferredCarryAngles; client: GM:DrawPhysgunBeam), so the Lua headers
// sit OUTSIDE the realm split.
#if defined ( LUA_SDK )
#include "luamanager.h"
#include "lbaseentity_shared.h"
#include "lbaseplayer_shared.h"
#include "lvphysics_interface.h"	// lua_pushphysicsobject (GM:OnPhysgunFreeze)
#include "mathlib/lvector.h"		// lua_pushvector / lua_toangle (DrawPhysgunBeam / GetPreferredCarryAngles)
#endif

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#ifdef CLIENT_DLL
CLIENTEFFECT_REGISTER_BEGIN( PrecacheHL2SBPhysgunMaterials )
    CLIENTEFFECT_MATERIAL( "sprites/physbeam" )
    CLIENTEFFECT_MATERIAL( "sprites/physbeama" )
    CLIENTEFFECT_MATERIAL( "sprites/physg_glow1" )
    CLIENTEFFECT_MATERIAL( "sprites/physg_glow2" )
CLIENTEFFECT_REGISTER_END()
#endif


// HL2SB GMod compat (ConVars In Garrysmod): the physgun's dynamic feel, same
// names GMod exposes.  SERVER side: the shadow controller and the input maths
// run there.  (physgun_halo / physgun_drawbeams are CLIENT -- see lhalo.cpp.)
#ifndef CLIENT_DLL
ConVar physgun_timeToArrive( "physgun_timeToArrive", "0.05", FCVAR_ARCHIVE | FCVAR_REPLICATED, "Physgun: seconds for the held object to reach the target" );
ConVar physgun_timeToArriveRagdoll( "physgun_timeToArriveRagdoll", "0.1", FCVAR_ARCHIVE | FCVAR_REPLICATED, "Physgun: seconds for a held ragdoll to reach the target" );
ConVar physgun_teleportDistance( "physgun_teleportDistance", "0", FCVAR_ARCHIVE | FCVAR_REPLICATED, "Physgun: distance at which the held object teleports instead of sliding" );
ConVar physgun_maxSpeed( "physgun_maxSpeed", "5000", FCVAR_ARCHIVE | FCVAR_REPLICATED, "Physgun: maximum linear speed of the held object" );
ConVar physgun_maxAngular( "physgun_maxAngular", "5000", FCVAR_ARCHIVE | FCVAR_REPLICATED, "Physgun: maximum angular speed of the held object" );
ConVar physgun_maxSpeedDamping( "physgun_maxSpeedDamping", "10000", FCVAR_ARCHIVE | FCVAR_REPLICATED, "Physgun: linear speed where damping kicks in" );
ConVar physgun_maxAngularDamping( "physgun_maxAngularDamping", "10000", FCVAR_ARCHIVE | FCVAR_REPLICATED, "Physgun: angular speed where damping kicks in" );
ConVar physgun_DampingFactor( "physgun_DampingFactor", "0.8", FCVAR_ARCHIVE | FCVAR_REPLICATED, "Physgun: damping factor for held object movement" );
#endif

// HL2SB: defaults reference from GMod:
// E-rotate is mousedx * 0.05 deg per RAW mouse count (the usercmd carries
// unscaled accumulators -- mouse sensitivity never touches cmd->mousedx), the
// wheel moves max(|wheelspeed|,0.1) units per notch, the hold distance clamps
// to [physgun_minrange, physgun_maxrange], E+A/D spins the held object at
// phys_spinspeed deg/s and Shift+E snaps the held angles to gm_snapangles.
// Registered on BOTH realms like GMod does (its client.dll registers the same
// names without reading them -- plugins may query them client-side).
ConVar physgun_rotation_sensitivity( "physgun_rotation_sensitivity", "0.05", FCVAR_ARCHIVE | FCVAR_REPLICATED, "Physgun: E+mouse rotation (degrees per mouse count)" );
ConVar physgun_wheelspeed( "physgun_wheelspeed", "10", FCVAR_ARCHIVE | FCVAR_REPLICATED, "Physgun: wheel push/pull distance per notch" );
ConVar physgun_limited( "physgun_limited", "0", FCVAR_ARCHIVE | FCVAR_REPLICATED, "Physgun: when enabled only props and ragdolls can be picked up" );
ConVar physgun_minrange( "physgun_minrange", "40", FCVAR_ARCHIVE | FCVAR_REPLICATED, "Physgun: minimum hold distance" );
ConVar physgun_maxrange( "physgun_maxrange", "4096", FCVAR_ARCHIVE | FCVAR_REPLICATED, "Physgun: maximum hold distance" );
ConVar phys_spinspeed( "phys_spinspeed", "200", FCVAR_ARCHIVE | FCVAR_REPLICATED, "Physgun: E+A/D spin speed (degrees per second)" );
ConVar gm_snapangles( "gm_snapangles", "45", FCVAR_ARCHIVE | FCVAR_REPLICATED, "Physgun: Shift+E angle snap grid (degrees, 0 disables)" );
ConVar gm_snapgrid( "gm_snapgrid", "0", FCVAR_ARCHIVE | FCVAR_REPLICATED, "Physgun: snap grid size (0 disables)" );



// HL2SB (2026-10-03): physgun visual diagnostics, BOTH realms (the server
// prints its beam-entity spawn, the client prints the client entity's
// constructor and draw calls).  The networked physgun_beam entity draws ONLY
// while this is on: the shipping renderer is the weapon's own
// DrawModel / ViewModelDrawn path, the entity stays as the diagnostic and
// future net-reference.  With it on, the answer to "no visuals" is one of
//   no "client entity created" line -> the client entity is never created;
//   "created" but no "draw" line      -> it exists but never reaches
//                                       DrawModel (leaf system gate).
// HL2SB (2026-10-03): renamed from hl2sb_physgun_debug (its archived "1" in
// existing config.cfg files would keep re-enabling the debug renderer after
// the default flip; a fresh non-archived name starts clean).  The only
// remaining function is the beam entity's debug renderer - all visual
// probes were removed.
ConVar hl2sb_physgun_beamdebug( "hl2sb_physgun_beamdebug", "0", FCVAR_NONE, "Physgun: draw the physgun_beam entity's diagnostic renderer" );

IPhysicsObject *GetPhysObjFromPhysicsBone( CBaseEntity *pEntity, short physicsbone )
{
	if ( pEntity == NULL || physicsbone < 0 )
		return NULL;
	if ( pEntity->IsNPC() )
		return pEntity->VPhysicsGetObject();

	IPhysicsObject *pList[VPHYSICS_MAX_OBJECT_LIST_COUNT];
	int count = pEntity->VPhysicsGetObjectList( pList, ARRAYSIZE( pList ) );
	if ( physicsbone >= count || physicsbone >= ARRAYSIZE( pList ) )
		return NULL;
	return pList[physicsbone];
}

#ifdef CLIENT_DLL
extern bool HL2SB_GetRagdollPhysicsBoneMatrix( C_BaseAnimating *pEntity, int physicsbone, matrix3x4_t &matrix );
#endif

bool HL2SB_PhysgunGetPhysicsBoneMatrix( CBaseEntity *pEntity, int physicsbone, matrix3x4_t &matrix )
{
	if ( pEntity == NULL || physicsbone < 0 || physicsbone > SHRT_MAX )
		return false;
#ifdef CLIENT_DLL
	C_BaseAnimating *pAnimating = pEntity->GetBaseAnimating();
	if ( pAnimating != NULL && HL2SB_GetRagdollPhysicsBoneMatrix( pAnimating, physicsbone, matrix ) )
		return true;
#endif
	IPhysicsObject *pObject = GetPhysObjFromPhysicsBone( pEntity, (short)physicsbone );
	if ( pObject == NULL )
	{
#ifdef CLIENT_DLL
		MatrixCopy( pEntity->EntityToWorldTransform(), matrix );
		return true;
#else
		return false;
#endif
	}
	pObject->GetPositionMatrix( &matrix );
	return true;
}

static bool HL2SB_PhysgunIsRagdoll( CBaseEntity *pEntity )
{
	CBaseAnimating *pAnimating = pEntity != NULL ? pEntity->GetBaseAnimating() : NULL;
	return pAnimating != NULL && pAnimating->IsRagdoll();
}

class CGravControllerPoint : public IMotionEvent
{
	DECLARE_SIMPLE_DATADESC();

public:
	CGravControllerPoint( void );
	~CGravControllerPoint( void );
	void AttachEntity( CBasePlayer *pPlayer, CBaseEntity *pEntity, IPhysicsObject *pPhys, short physicsbone, const Vector &position );
	void DetachEntity( void );

	bool UpdateObject( CBasePlayer *pPlayer, CBaseEntity *pEntity );

	void SetTargetPosition( const Vector &target, const QAngle &targetOrientation );
	bool IsAttachedObjectValid( void ) const;
	QAngle TransformAnglesToPlayerSpace( const QAngle &anglesIn, CBasePlayer *pPlayer );
	QAngle TransformAnglesFromPlayerSpace( const QAngle &anglesIn, CBasePlayer *pPlayer );

	IMotionEvent::simresult_e Simulate( IPhysicsMotionController *pController, IPhysicsObject *pObject, float deltaTime, Vector &linear, AngularImpulse &angular );
	Vector			m_localPosition;
	Vector			m_targetPosition;
	Vector			m_worldPosition;
	float			m_saveDamping;
	float			m_saveMass;
	float			m_maxAcceleration;
	Vector			m_maxAngularAcceleration;
	EHANDLE			m_attachedEntity;
	short			m_attachedPhysicsBone;
	QAngle			m_targetRotation;
	float			m_timeToArrive;

	IPhysicsMotionController *m_controller;
	IPhysicsObject *m_pAttachedObject;

private:
	hlshadowcontrol_params_t	m_shadow;
};

BEGIN_SIMPLE_DATADESC( CGravControllerPoint )

	DEFINE_FIELD( m_localPosition,		FIELD_VECTOR ),
	DEFINE_FIELD( m_targetPosition,		FIELD_POSITION_VECTOR ),
	DEFINE_FIELD( m_worldPosition,		FIELD_POSITION_VECTOR ),
	DEFINE_FIELD( m_saveDamping,			FIELD_FLOAT ),
	DEFINE_FIELD( m_saveMass,			FIELD_FLOAT ),
	DEFINE_FIELD( m_maxAcceleration,		FIELD_FLOAT ),
	DEFINE_FIELD( m_maxAngularAcceleration,	FIELD_VECTOR ),
	DEFINE_FIELD( m_attachedEntity,		FIELD_EHANDLE ),
	DEFINE_FIELD( m_attachedPhysicsBone,		FIELD_SHORT ),
	DEFINE_FIELD( m_targetRotation,		FIELD_VECTOR ),
	DEFINE_FIELD( m_timeToArrive,			FIELD_FLOAT ),

	// Physptrs can't be saved in embedded classes... this is to silence classcheck
	// DEFINE_PHYSPTR( m_controller ),

END_DATADESC()

CGravControllerPoint::CGravControllerPoint( void )
{
	m_shadow.dampFactor = 0.8;
	m_shadow.teleportDistance = 0;
	// make this controller really stiff!
	m_shadow.maxSpeed = 5000;
	m_shadow.maxAngular = m_shadow.maxSpeed;
	m_shadow.maxDampSpeed = m_shadow.maxSpeed*2;
	m_shadow.maxDampAngular = m_shadow.maxAngular*2;
	m_attachedEntity = NULL;
	m_attachedPhysicsBone = 0;
	m_controller = NULL;
	m_pAttachedObject = NULL;
	m_saveMass = -1.0f;
	m_localPosition = vec3_origin;
	m_targetPosition = vec3_origin;
	m_targetRotation = vec3_angle;
}

CGravControllerPoint::~CGravControllerPoint( void )
{
	DetachEntity();
}

QAngle CGravControllerPoint::TransformAnglesToPlayerSpace( const QAngle &anglesIn, CBasePlayer *pPlayer )
{
	matrix3x4_t test;
	QAngle angleTest = pPlayer->EyeAngles();
	angleTest.x = 0;
	AngleMatrix( angleTest, test );
	return TransformAnglesToLocalSpace( anglesIn, test );
}

QAngle CGravControllerPoint::TransformAnglesFromPlayerSpace( const QAngle &anglesIn, CBasePlayer *pPlayer )
{
	matrix3x4_t test;
	QAngle angleTest = pPlayer->EyeAngles();
	angleTest.x = 0;
	AngleMatrix( angleTest, test );
	return TransformAnglesToWorldSpace( anglesIn, test );
}

bool CGravControllerPoint::IsAttachedObjectValid( void ) const
{
	return m_pAttachedObject != NULL &&
		GetPhysObjFromPhysicsBone( m_attachedEntity, m_attachedPhysicsBone ) == m_pAttachedObject;
}

void CGravControllerPoint::SetTargetPosition( const Vector &target, const QAngle &targetOrientation )
{
	if ( !IsAttachedObjectValid() || !target.IsValid() || !targetOrientation.IsValid() )
		return;

	Vector worldGrab, origin;
	m_pAttachedObject->LocalToWorld( &worldGrab, m_localPosition );
	m_pAttachedObject->GetPosition( &origin, NULL );
	m_targetPosition = target;
	m_shadow.targetPosition = target - ( worldGrab - origin );
	m_shadow.targetRotation = targetOrientation;
	m_pAttachedObject->Wake();
}

void CGravControllerPoint::AttachEntity( CBasePlayer *pPlayer, CBaseEntity *pEntity, IPhysicsObject *pPhys, short physicsbone, const Vector &vGrabPosition )
{
	DetachEntity();
	m_attachedEntity = pEntity;
	m_attachedPhysicsBone = physicsbone;
	m_pAttachedObject = pPhys;
	pPhys->WorldToLocal( &m_localPosition, vGrabPosition );
	m_worldPosition = vGrabPosition;
	m_saveMass = pPhys->GetMass();

	// Preserve ragdoll body masses and existing heavy or vehicle bodies.
	bool bPreserveMass = HL2SB_PhysgunIsRagdoll( pEntity ) || m_saveMass > 45678.0f ||
		FClassnameIs( pEntity, "prop_vehicle_jeep" ) ||
		FClassnameIs( pEntity, "prop_vehicle_apc" ) ||
		FClassnameIs( pEntity, "prop_vehicle_jeep_old" );
	if ( bPreserveMass )
		m_saveMass = -1.0f;
	else if ( m_saveMass != 45678.0f )
		pPhys->SetMass( 45678.0f );

	pPhys->EnableMotion( true );
	pPhys->Wake();
	m_controller = physenv->CreateMotionController( this );
	m_controller->AttachObject( pPhys, true );
	m_controller->SetPriority( IPhysicsMotionController::HIGH_PRIORITY );
	QAngle angles;
	pPhys->GetPosition( NULL, &angles );
	SetTargetPosition( vGrabPosition, angles );
	m_targetRotation = TransformAnglesToPlayerSpace( angles, pPlayer );
}

void CGravControllerPoint::DetachEntity( void )
{
	// A rebuilt ragdoll can reuse the same element number for a different body.
	// Never restore mass through a pointer that is no longer in that slot.
	if ( IsAttachedObjectValid() )
	{
		m_pAttachedObject->Wake();
		if ( m_saveMass > 0.0f && m_saveMass != 45678.0f )
			m_pAttachedObject->SetMass( m_saveMass );
	}
	if ( physenv != NULL && m_controller != NULL )
		physenv->DestroyMotionController( m_controller );
	m_controller = NULL;
	m_pAttachedObject = NULL;
	m_attachedEntity = NULL;
	m_attachedPhysicsBone = 0;
	m_saveMass = -1.0f;
	m_targetPosition = vec3_origin;
	m_worldPosition = vec3_origin;
}

void AxisAngleQAngle( const Vector &axis, float angle, QAngle &outAngles )
{
	// map back to HL rotation axes
	outAngles.z = axis.x * angle;
	outAngles.x = axis.y * angle;
	outAngles.y = axis.z * angle;
}

IMotionEvent::simresult_e CGravControllerPoint::Simulate( IPhysicsMotionController *pController, IPhysicsObject *pObject, float deltaTime, Vector &linear, AngularImpulse &angular )
{
	linear.Init();
	angular.Init();
	if ( !IsAttachedObjectValid() || pObject != m_pAttachedObject ||
		!m_shadow.targetPosition.IsValid() || !m_shadow.targetRotation.IsValid() ||
		!IsFinite( deltaTime ) || deltaTime <= 0.0f )
		return SIM_NOTHING;
	hlshadowcontrol_params_t shadowParams = m_shadow;
#ifndef CLIENT_DLL
	// HL2SB GMod compat: the controller follows the physgun_* convars every
	// tick, exactly the knobs GMod exposes.
	shadowParams.maxSpeed = physgun_maxSpeed.GetFloat();
	shadowParams.maxAngular = physgun_maxAngular.GetFloat();
	shadowParams.maxDampSpeed = physgun_maxSpeedDamping.GetFloat();
	shadowParams.maxDampAngular = physgun_maxAngularDamping.GetFloat();
	shadowParams.teleportDistance = physgun_teleportDistance.GetFloat();
	shadowParams.dampFactor = physgun_DampingFactor.GetFloat();
	// HL2SB (2026-10-03, reference): the arrival constant is per-TARGET-TYPE
	// -- physgun_timeToArrive (0.05) normally, physgun_timeToArriveRagdoll
	// (0.1) when the held entity is a ragdoll; it is NOT frametime-based.
	float flTimeToArrive = physgun_timeToArrive.GetFloat();
	if ( HL2SB_PhysgunIsRagdoll( m_attachedEntity ) )
		flTimeToArrive = physgun_timeToArriveRagdoll.GetFloat();
	m_timeToArrive = pObject->ComputeShadowControl( shadowParams, flTimeToArrive, deltaTime );
#else
	m_timeToArrive = pObject->ComputeShadowControl( shadowParams, (TICK_INTERVAL*2), deltaTime );
#endif

	linear.Init();
	angular.Init();

	return SIM_LOCAL_ACCELERATION;
}

//-----------------------------------------------------------------------------
// HL2SB (2026-10-03): the HL2 physcannon client effect system is GONE - the
// reference physgun renders nothing itself; the networked physgun_beam entity
// owns the whole visual (game/client/c_physgun_beam.cpp).
//-----------------------------------------------------------------------------

#ifdef CLIENT_DLL
#define CWeaponGravityGun C_WeaponGravityGun
#endif

class CWeaponGravityGun : public CBaseHL2MPCombatWeapon
{
	DECLARE_DATADESC();

public:
	DECLARE_CLASS( CWeaponGravityGun, CBaseHL2MPCombatWeapon );

	DECLARE_NETWORKCLASS();
	DECLARE_PREDICTABLE();

	CWeaponGravityGun();

#ifdef CLIENT_DLL
	void GetRenderBounds( Vector& mins, Vector& maxs )
	{
		BaseClass::GetRenderBounds( mins, maxs );
	}

	void GetRenderBoundsWorldspace( Vector& mins, Vector& maxs )
	{
		BaseClass::GetRenderBoundsWorldspace( mins, maxs );
		// World-space bounds must stay in world space for reflection culling.
		C_BasePlayer *pOwner = ToBasePlayer( GetOwner() );
		if ( pOwner != NULL )
			AddPointToBounds( pOwner->Weapon_ShootPosition(), mins, maxs );
		C_BaseEntity *pTarget = m_hObject;
		if ( pTarget != NULL && pTarget != this )
		{
			Vector targetMins, targetMaxs;
			pTarget->GetRenderBoundsWorldspace( targetMins, targetMaxs );
			AddPointToBounds( targetMins, mins, maxs );
			AddPointToBounds( targetMaxs, mins, maxs );
		}
	}

	int KeyInput( int down, ButtonCode_t keynum, const char *pszCurrentBinding )
	{
		if ( gHUD.m_iKeyBits & IN_ATTACK )
		{
			switch ( keynum )
			{
			case MOUSE_WHEEL_UP:
				m_bInWeapon1 = true;
				// gHUD.m_iKeyBits |= IN_WEAPON1;
				if ( gpGlobals->maxClients > 1 )
					//gHUD.m_bSkipClear = true;
				return 0;

			case MOUSE_WHEEL_DOWN:
				m_bInWeapon2 = true;
				// gHUD.m_iKeyBits |= IN_WEAPON2;
				if ( gpGlobals->maxClients > 1 )
					//gHUD.m_bSkipClear = true;
				return 0;
			}
		}

		// Allow engine to process
		return BaseClass::KeyInput( down, keynum, pszCurrentBinding );
	}

	void HandleInput()
	{
		if ( m_bInWeapon1 )
		{
			gHUD.m_iKeyBits |= IN_WEAPON1;
			m_bInWeapon1 = false;
		}

		if ( m_bInWeapon2 )
		{
			gHUD.m_iKeyBits |= IN_WEAPON2;
			m_bInWeapon2 = false;
		}
	}

	// HL2SB (2026-10-03, GUARANTEED RENDER PATH): the beam visual draws from
	// the WEAPON's own DrawModel / ViewModelDrawn.  The weapon is always in
	// the render lists (it carries a studio model), so the visual cannot be
	// lost to leaf-system classification the way a model-less entity's can.
	// The networked physgun_beam entity stays for STATE and draws only as a
	// hl2sb_physgun_debug diagnostic.
	int					DrawModel( int flags );
	void				ViewModelDrawn( C_BaseViewModel *pBaseViewModel );
	bool				IsTransparent( void );
	void				DrawPhysgunVisuals( C_BaseViewModel *pViewModel );
	void				OnDataChanged( DataUpdateType_t type );
#endif

	void Spawn( void );
	void OnRestore( void );
	void Precache( void );

	virtual void	UpdateOnRemove(void);
	void PrimaryAttack( void );
	void SecondaryAttack( void );
	void PlayAttackAnimation( void );
	void ItemPreFrame( void );
	void ItemPostFrame( void );
	virtual bool Holster( CBaseCombatWeapon *pSwitchingTo )
	{
		EffectDestroy();
		SoundDestroy();
		return BaseClass::Holster( pSwitchingTo );
	}

	bool Reload( void );
	void Drop(const Vector &vecVelocity)
	{
		EffectDestroy();
		SoundDestroy();

#ifndef CLIENT_DLL
		UTIL_Remove( this );
#endif
	}

	bool HasAnyAmmo( void );

	// HL2SB (2026-10-03): the GetWorldModel()=GetViewModel(0) override is
	// GONE.  The reference weapon script uses viewmodel
	// c_superphyscannon + playermodel w_Physics exactly like ours (extracted
	// from the reference install's scripts/weapons/weapon_physgun.txt), and
	// its client class has NO model override -- the third-person gun is
	// w_Physics wearing skin 1 (the weapon-colour-tinted material; skin 0 is
	// the stock HL2 gold "yellow gravity gun").  The override forced the c_
	// model into the world pass, and the mirror/third-person mismatch and
	// pose-sync fights all follow from that.  The script pair renders as-is.

	void AttachObject( CBaseEntity *pEdict, IPhysicsObject *pPhysics, short physicsbone, const Vector& start, const Vector &end, float distance );
	void UpdateObject( void );
	void DetachObject( void );

	void TraceLine( trace_t *ptr );

	void EffectCreate( void );
	void EffectUpdate( void );
	void EffectDestroy( void );

	void SoundCreate( void );
	void SoundDestroy( void );
	void SoundStop( void );
	void SoundStart( void );
	void SoundUpdate( void );

	int ObjectCaps( void )
	{
		int caps = BaseClass::ObjectCaps();
		if ( m_active )
		{
			caps |= FCAP_DIRECTIONAL_USE;
		}
		return caps;
	}

	// HL2SB GMod compat: client helpers for the glow shell / mouse-rotate /
	// wheel-distance intercepts
	bool	IsHolding( void ) const { return m_hObject != NULL; }
	CBaseEntity *GetHeldEntity( void ) const { return m_hObject; }
	void	HL2SB_AdjustDistance( float flDelta )
	{
		// HL2SB (2026-09-26): GMod clamps the hold distance to
		// [physgun_minrange, physgun_maxrange] (reference: 40..4096)
		// instead of the old hardwired 40..1024.
		m_distance = clamp( m_distance + flDelta,
			physgun_minrange.GetFloat(), physgun_maxrange.GetFloat() );
	}

private:
	CNetworkVar( int, m_active );
	bool		m_useDown;
	bool		m_bDetaching;
	// HL2SB GMod compat: RMB freeze releases the object; while LMB is STILL
	// held this latch stops the per-frame scan from instantly re-grabbing the
	// just-frozen body (AttachObject unfreezes on grab, so the re-grab made
	// every freeze die within one frame).  Cleared when LMB comes up.
	bool		m_bFreezeReleaseLatch;
	CNetworkHandle( CBaseEntity, m_hObject );
	CNetworkVar( int, m_physicsBone );
	float		m_distance;
	float		m_movementLength;
	int			m_soundState;
	Vector		m_originalObjectPosition;
	CNetworkVector	( m_targetPosition );
	CNetworkVector	( m_worldPosition );
	// HL2SB (2026-10-03): GMod DT_PhysBeam m_HoldPos - the grab point in
	// TARGET-LOCAL space (world offset while the target is unrotated, bone
	// local for physics entities, entity local otherwise); the beam end
	// transforms it back every frame so the beam sticks to the grabbed spot.
	CNetworkVector	( m_vecHoldPos );

#ifndef CLIENT_DLL
	// HL2SB (2026-10-03): the reference's physgun_beam entity - created on
	// the first grab, carrying the beam state to the client class that
	// draws everything.  The weapon itself renders nothing.
	EHANDLE		m_hBeam;

	// HL2SB GMod compat: the held prop's soft full-body light is a CLIENT
	// dlight (see EffectUpdate) - no server-side entity needed.
	float		m_flLastReloadPress;	// for the double-tap-R unfreeze-all
	bool		m_bDraggingNPC;			// teleport-drag mode: NPC / nextbot / script ent / force-allowed player
	QAngle		m_heldWorldAngles;		// GMod style: the held object keeps its WORLD orientation
	Vector		m_vecGrabOffset;		// world offset from the hit point to the origin, kept while dragged
#endif

	CSoundPatch					*m_sndMotor;		// Whirring sound for the gun
	CSoundPatch					*m_sndLockedOn;
	CSoundPatch					*m_sndLightObject;
	CSoundPatch					*m_sndHeavyObject;

	CGravControllerPoint		m_gravCallback;

	bool		m_bInWeapon1;
	bool		m_bInWeapon2;

	DECLARE_ACTTABLE();
};

#ifndef CLIENT_DLL
extern bool PlayerPickupControllerIsHoldingEntity( CBaseEntity *pController, CBaseEntity *pEntity );
extern CBaseEntity *PhysCannonGetHeldEntity( CBaseCombatWeapon *pWeapon );

static bool HL2SB_PhysgunHeldByOtherPlayer( CBaseEntity *pEntity, CBasePlayer *pOwner )
{
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CBasePlayer *pOther = UTIL_PlayerByIndex( i );
		if ( pOther == NULL || pOther == pOwner )
			continue;
		if ( PlayerPickupControllerIsHoldingEntity( pOther->GetUseEntity(), pEntity ) ||
			PhysCannonGetHeldEntity( pOther->GetActiveWeapon() ) == pEntity )
			return true;
		CWeaponGravityGun *pGun = dynamic_cast<CWeaponGravityGun *>( pOther->GetActiveWeapon() );
		if ( pGun != NULL && pGun->GetHeldEntity() == pEntity )
			return true;
	}
	return false;
}
#endif

IMPLEMENT_NETWORKCLASS_ALIASED( WeaponGravityGun, DT_WeaponGravityGun )

BEGIN_NETWORK_TABLE( CWeaponGravityGun, DT_WeaponGravityGun )
#ifdef CLIENT_DLL
	RecvPropEHandle( RECVINFO( m_hObject ) ),
	RecvPropInt( RECVINFO( m_physicsBone ) ),
	RecvPropVector( RECVINFO( m_vecHoldPos ) ),
	RecvPropVector( RECVINFO( m_targetPosition ) ),
	RecvPropVector( RECVINFO( m_worldPosition ) ),
	RecvPropInt( RECVINFO(m_active) ),
#else
	SendPropEHandle( SENDINFO( m_hObject ) ),
	SendPropInt( SENDINFO( m_physicsBone ) ),
	SendPropVector( SENDINFO( m_vecHoldPos ) ),
	SendPropVector(SENDINFO( m_targetPosition ), -1, SPROP_COORD),
	SendPropVector(SENDINFO( m_worldPosition ), -1, SPROP_COORD),
	SendPropInt( SENDINFO(m_active), 1, SPROP_UNSIGNED ),
#endif
END_NETWORK_TABLE()

#ifdef CLIENT_DLL
BEGIN_PREDICTION_DATA( CWeaponGravityGun )
	DEFINE_PRED_FIELD( m_hObject, FIELD_EHANDLE, FTYPEDESC_INSENDTABLE ),
	DEFINE_PRED_FIELD( m_vecHoldPos, FIELD_VECTOR, FTYPEDESC_INSENDTABLE ),
	DEFINE_PRED_FIELD( m_physicsBone, FIELD_INTEGER, FTYPEDESC_INSENDTABLE ),
	DEFINE_PRED_FIELD( m_active, FIELD_INTEGER, FTYPEDESC_INSENDTABLE ),
	DEFINE_PRED_FIELD( m_bFreezeReleaseLatch, FIELD_BOOLEAN, 0 ),
	DEFINE_PRED_FIELD( m_distance, FIELD_FLOAT, 0 ),
END_PREDICTION_DATA()
#endif

LINK_ENTITY_TO_CLASS( weapon_physgun, CWeaponGravityGun );
PRECACHE_WEAPON_REGISTER(weapon_physgun);

acttable_t	CWeaponGravityGun::m_acttable[] =
{
	// ------------------------------------------------------------------
	// GMod player layer (ACT_MP_*). This weapon already had most of it;
	// completed with the states GMod's animation system can ask for.
	// Hold type: "physgun" (activity suffix PHYSGUN).
	// ------------------------------------------------------------------
	{ ACT_MP_STAND_IDLE, ACT_HL2MP_IDLE_PHYSGUN, false },
	{ ACT_MP_CROUCH_IDLE, ACT_HL2MP_IDLE_CROUCH_PHYSGUN, false },
	{ ACT_MP_RUN, ACT_HL2MP_RUN_PHYSGUN, false },
	{ ACT_MP_CROUCHWALK, ACT_HL2MP_WALK_CROUCH_PHYSGUN, false },
	{ ACT_MP_WALK, ACT_HL2MP_WALK_PHYSGUN, false },
	{ ACT_MP_SPRINT, ACT_HL2MP_RUN_FAST, false },
	{ ACT_MP_SWIM, ACT_HL2MP_SWIM_PHYSGUN, false },
	{ ACT_MP_JUMP_START, ACT_HL2MP_JUMP_PHYSGUN, false },
	{ ACT_MP_JUMP_LAND, ACT_HL2MP_JUMP_PHYSGUN, false },
	{ ACT_MP_JUMP_FLOAT, ACT_HL2MP_JUMP_PHYSGUN, false },
	{ ACT_MP_DOUBLEJUMP, ACT_HL2MP_JUMP_PHYSGUN, false },
	{ ACT_MP_ATTACK_STAND_PRIMARYFIRE, ACT_HL2MP_GESTURE_RANGE_ATTACK_PHYSGUN, false },
	{ ACT_MP_ATTACK_CROUCH_PRIMARYFIRE, ACT_HL2MP_GESTURE_RANGE_ATTACK_PHYSGUN, false },
	{ ACT_MP_ATTACK_SWIM_PRIMARYFIRE, ACT_HL2MP_GESTURE_RANGE_ATTACK_PHYSGUN, false },
	{ ACT_MP_RELOAD_STAND, ACT_HL2MP_GESTURE_RELOAD_PHYSGUN, false },
	{ ACT_MP_RELOAD_CROUCH, ACT_HL2MP_GESTURE_RELOAD_PHYSGUN, false },
	{ ACT_MP_RELOAD_SWIM, ACT_HL2MP_GESTURE_RELOAD_PHYSGUN, false },
	{ ACT_MP_JUMP, ACT_HL2MP_JUMP_PHYSGUN, false },

	// ------------------------------------------------------------------
	// GMod base states this table never had (only on models that include
	// models/m_anm.mdl - HL2MP's own *_anims.mdl has none of them).
	// ------------------------------------------------------------------
	{ ACT_HL2MP_WALK, ACT_HL2MP_WALK_PHYSGUN, false },
	{ ACT_HL2MP_SWIM, ACT_HL2MP_SWIM_PHYSGUN, false },
	{ ACT_HL2MP_SWIM_IDLE, ACT_HL2MP_SWIM_IDLE_PHYSGUN, false },
	{ ACT_HL2MP_SIT, ACT_HL2MP_SIT_PHYSGUN, false },
	{ ACT_HL2MP_RUN_FAST, ACT_HL2MP_RUN_PHYSGUN, false },
	{ ACT_HL2MP_RUN_CHARGING, ACT_HL2MP_RUN_PHYSGUN, false },
	{ ACT_HL2MP_RUN_PANICKED, ACT_HL2MP_RUN_PHYSGUN, false },
	{ ACT_HL2MP_RUN_PROTECTED, ACT_HL2MP_RUN_PHYSGUN, false },

	{ ACT_HL2MP_IDLE, ACT_HL2MP_IDLE_PHYSGUN, false },
	{ ACT_HL2MP_RUN, ACT_HL2MP_RUN_PHYSGUN, false },
	{ ACT_HL2MP_IDLE_CROUCH, ACT_HL2MP_IDLE_CROUCH_PHYSGUN, false },
	{ ACT_HL2MP_WALK_CROUCH, ACT_HL2MP_WALK_CROUCH_PHYSGUN, false },
	{ ACT_HL2MP_GESTURE_RANGE_ATTACK, ACT_HL2MP_GESTURE_RANGE_ATTACK_PHYSGUN, false },
	{ ACT_HL2MP_GESTURE_RELOAD, ACT_HL2MP_GESTURE_RELOAD_PHYSGUN, false },
	{ ACT_HL2MP_JUMP, ACT_HL2MP_JUMP_PHYSGUN, false },

	{ ACT_RANGE_ATTACK1, ACT_HL2MP_GESTURE_RANGE_ATTACK_PHYSGUN, false },
};

IMPLEMENT_ACTTABLE(CWeaponGravityGun);

//---------------------------------------------------------
// Save/Restore
//---------------------------------------------------------
BEGIN_DATADESC( CWeaponGravityGun )

	DEFINE_FIELD( m_active,				FIELD_INTEGER ),
	DEFINE_FIELD( m_useDown,				FIELD_BOOLEAN ),
	DEFINE_FIELD( m_hObject,				FIELD_EHANDLE ),
	DEFINE_FIELD( m_physicsBone,				FIELD_INTEGER ),
	DEFINE_FIELD( m_distance,			FIELD_FLOAT ),
	DEFINE_FIELD( m_movementLength,		FIELD_FLOAT ),
	DEFINE_FIELD( m_soundState,			FIELD_INTEGER ),
	DEFINE_FIELD( m_originalObjectPosition,	FIELD_POSITION_VECTOR ),
#ifndef CLIENT_DLL
	DEFINE_FIELD( m_hBeam,					FIELD_EHANDLE ),
#endif
	DEFINE_SOUNDPATCH( m_sndMotor ),
	DEFINE_SOUNDPATCH( m_sndLockedOn ),
	DEFINE_SOUNDPATCH( m_sndLightObject ),
	DEFINE_SOUNDPATCH( m_sndHeavyObject ),
	DEFINE_EMBEDDED( m_gravCallback ),
	// Physptrs can't be saved in embedded classes..
	DEFINE_PHYSPTR( m_gravCallback.m_controller ),

END_DATADESC()

enum physgun_soundstate { SS_SCANNING, SS_LOCKEDON };
enum physgun_soundIndex { SI_LOCKEDON = 0, SI_SCANNING = 1, SI_LIGHTOBJECT = 2, SI_HEAVYOBJECT = 3, SI_ON, SI_OFF };

//=========================================================
//=========================================================

CWeaponGravityGun::CWeaponGravityGun()
{
	m_active = false;
	m_bFiresUnderwater = true;
	m_bInWeapon1 = false;
	m_bInWeapon2 = false;
	m_bFreezeReleaseLatch = false;
	m_bDetaching = false;
#ifndef CLIENT_DLL
	m_flLastReloadPress = 0.0f;
	m_bDraggingNPC = false;
	m_heldWorldAngles = vec3_angle;
	m_vecGrabOffset = vec3_origin;
	m_hBeam = NULL;
#endif
}

//-----------------------------------------------------------------------------
// On Remove
//-----------------------------------------------------------------------------
void CWeaponGravityGun::UpdateOnRemove(void)
{
	EffectDestroy();
	SoundDestroy();

#ifndef CLIENT_DLL
	// HL2SB (2026-10-03): the weapon is gone - take the beam entity with it.
	if ( m_hBeam != NULL )
	{
		CBaseEntity *pBeamEnt = (CBaseEntity *)m_hBeam;
		if ( pBeamEnt != NULL )
			UTIL_Remove( pBeamEnt );
		m_hBeam = NULL;
	}
#endif

	BaseClass::UpdateOnRemove();
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
// adnan
// want to add an angles modifier key
bool CGravControllerPoint::UpdateObject( CBasePlayer *pPlayer, CBaseEntity *pEntity )
{
	return pEntity != NULL && IsAttachedObjectValid();
}

//=========================================================
//=========================================================
void CWeaponGravityGun::Spawn( )
{
	BaseClass::Spawn();
//	SetModel( GetWorldModel() );

	// HL2SB (2026-10-03): the third-person gun is w_Physics wearing skin 1 -
	// w_physics_sheet2, the material carrying the PlayerWeaponColor proxy
	// (the tinted look).  Skin 0 is the stock HL2 gold sheet, and the
	// reference install ships only those two skins, so the tint REQUIRES
	// skin 1.  m_nSkin is the standard networked BaseAnimating prop, so this
	// reaches every third-person render (map-placed, given and dropped
	// weapons alike); the first-person c_ model has its own materials.
	m_nSkin = 1;

#ifndef CLIENT_DLL
	FallInit();
#endif
}

void CWeaponGravityGun::OnRestore( void )
{
	BaseClass::OnRestore();

	if ( m_gravCallback.m_controller )
	{
		m_gravCallback.m_pAttachedObject = GetPhysObjFromPhysicsBone( m_hObject, m_physicsBone );
		m_gravCallback.m_controller->SetEventHandler( &m_gravCallback );
	}
}

//=========================================================
//=========================================================
void CWeaponGravityGun::Precache( void )
{
	BaseClass::Precache();

	PrecacheScriptSound( "Weapon_Physgun.Scanning" );
	PrecacheScriptSound( "Weapon_Physgun.LockedOn" );
	PrecacheScriptSound( "Weapon_Physgun.Scanning" );
	PrecacheScriptSound( "Weapon_Physgun.LightObject" );
	PrecacheScriptSound( "Weapon_Physgun.HeavyObject" );
}

void CWeaponGravityGun::EffectCreate( void )
{
	EffectUpdate();
	m_active = true;
}

// Andrew; added so we can trace both in EffectUpdate and DrawModel with the same results
void CWeaponGravityGun::TraceLine( trace_t *ptr )
{
	CBasePlayer *pOwner = ToBasePlayer( GetOwner() );
	if ( !pOwner )
		return;

	Vector start, forward;
	pOwner->EyeVectors( &forward, NULL, NULL );

	start = pOwner->Weapon_ShootPosition();
	Vector end = start + forward * ( 2.0f * physgun_maxrange.GetFloat() );
	UTIL_TraceLine( start, end, MASK_SHOT_HULL | CONTENTS_AUX, pOwner, COLLISION_GROUP_NONE, ptr );
}

//-----------------------------------------------------------------------------
// HL2SB (2026-09-29, GMod reference): GMod does NOT force-drive held objects.
// It computes the target pose every frame and GATES the move with sweeps
// (CBaseEntity::TraceEntityFiltered, mask 0x400B = SOLID|WINDOW|GRATE|MOVEABLE,
// the entity's own hull, self excluded): clear -> the object may stand at the
// target; blocked -> the axis deltas are re-tested individually and only the
// free ones are kept (that is what makes a held prop SLIDE along a surface).
// The sweep uses the entity HULL, not a zero-radius ray - a ray ignores the
// body, which let dragged NPCs sink hull-deep into the floor.
//-----------------------------------------------------------------------------
static Vector HL2SB_PhysgunGateTarget( CBaseEntity *pObject, const Vector &vecDest )
{
	static const unsigned int uiGateMask = CONTENTS_SOLID | CONTENTS_WINDOW | CONTENTS_GRATE | CONTENTS_MOVEABLE;
	const Vector vecFrom = pObject->GetAbsOrigin();

	trace_t tr;
	UTIL_TraceEntity( pObject, vecFrom, vecFrom, uiGateMask, &tr );
	if ( tr.fraction < 1.0f || tr.startsolid || tr.allsolid )
	{
		UTIL_TraceEntity( pObject, vecDest, vecDest, uiGateMask, &tr );
		if ( tr.fraction >= 1.0f && !tr.startsolid && !tr.allsolid )
			return vecDest;
	}
	UTIL_TraceEntity( pObject, vecFrom, vecDest, uiGateMask, &tr );
	if ( tr.fraction >= 1.0f && !tr.startsolid && !tr.allsolid )
		return vecDest;

	// Blocked: keep only the axis deltas that sweep free.
	const Vector delta = vecDest - vecFrom;
	Vector allowed( 0.0f, 0.0f, 0.0f );
	trace_t axisTr;

	UTIL_TraceEntity( pObject, vecFrom, vecFrom + Vector( delta.x, 0.0f, 0.0f ), uiGateMask, &axisTr );
	if ( axisTr.fraction >= 1.0f && !axisTr.startsolid )
		allowed.x = delta.x;

	UTIL_TraceEntity( pObject, vecFrom, vecFrom + Vector( 0.0f, delta.y, 0.0f ), uiGateMask, &axisTr );
	if ( axisTr.fraction >= 1.0f && !axisTr.startsolid )
		allowed.y = delta.y;

	UTIL_TraceEntity( pObject, vecFrom, vecFrom + Vector( 0.0f, 0.0f, delta.z ), uiGateMask, &axisTr );
	if ( axisTr.fraction >= 1.0f && !axisTr.startsolid )
		allowed.z = delta.z;

	Vector vecResult = vecFrom + allowed;

	// Re-test the combined slide and clamp at whatever it still touches.
	UTIL_TraceEntity( pObject, vecFrom, vecResult, uiGateMask, &tr );
	if ( !tr.startsolid && tr.fraction < 1.0f )
		vecResult = tr.endpos;

	return vecResult;
}

void CWeaponGravityGun::EffectUpdate( void )
{
	Vector start, forward, right;
	trace_t tr;

	CBasePlayer *pOwner = ToBasePlayer( GetOwner() );
	if ( !pOwner )
		return;

	pOwner->EyeVectors( &forward, &right, NULL );

	start = pOwner->Weapon_ShootPosition();

	TraceLine( &tr );
	Vector end = tr.endpos;
	float distance = start.DistTo( end );

	if ( m_hObject == NULL && !m_bFreezeReleaseLatch &&
		m_flNextPrimaryAttack <= gpGlobals->curtime && tr.DidHit() &&
		( pOwner->m_afButtonPressed & IN_ATTACK ) )
	{
		PlayAttackAnimation();
	}

	if ( m_hObject == NULL && !m_bFreezeReleaseLatch && tr.DidHitNonWorldEntity() )
	{
		CBaseEntity *pEntity = tr.m_pEnt;
		AttachObject( pEntity, GetPhysObjFromPhysicsBone( pEntity, tr.physicsbone ), tr.physicsbone, start, tr.endpos, distance );
	}

	CBaseEntity *pObject = m_hObject;

#ifndef CLIENT_DLL
	// HL2SB (2026-10-03): keep the physgun_beam entity in lockstep with the
	// hold.  Created on the first activate and kept for the weapon's life;
	// m_bIsOn is the LMB-held state, so the beam also shows with NO target -
	// ending at the gate-traced scan point - exactly like the reference.
	{
		CPhysBeam *pBeam = dynamic_cast< CPhysBeam * >( (CBaseEntity *)m_hBeam );
		if ( pBeam == NULL )
		{
			CPhysBeam *pNew = (CPhysBeam *)CreateEntityByName( "physgun_beam" );
			if ( pNew != NULL )
			{
				pNew->SetHolder( pOwner );
				pNew->Spawn();
				m_hBeam = pNew;
				pBeam = pNew;
			}
		}
		if ( pBeam != NULL )
		{
			pBeam->SetHolder( pOwner );
			if ( pObject != NULL )
				pBeam->SetTarget( pObject, m_physicsBone, m_vecHoldPos );
			else
				pBeam->SetTarget( NULL, 0, vec3_origin );
			if ( m_active )
				pBeam->TurnOn();
		}
	}
#endif

	// HL2SB (2026-10-03): NO dynamic lights anywhere on this weapon.  The
	// earlier "dual muzzle light" read was a misattribution - the
	// function it cited is the reference env_dynamiclight's ClientThink,
	// not a physgun path; the reference weapon class has no light members
	// and no ClientThink.  All light-looking behaviour is the beam ribbon
	// and the two sprite clusters drawn in DrawPhysgunVisuals.
	// HL2SB GMod compat: RMB freezes the held object in place AND releases it.
	// In GMod, SecondaryAttack handles freeze completely.
	if ( pObject && ( pOwner->m_afButtonPressed & IN_ATTACK2 ) )
	{
		SecondaryAttack();
		return;
	}

	if ( pObject )
	{
#ifndef CLIENT_DLL
		// HL2SB GMod compat: E+mouse rotates relative to the PLAYER'S VIEW SPACE.
		// The mouse deltas arrive with the user command (the client freezes the
		// VIEW while E is held, see HL2SB_PhysgunMouseRotate in in_mouse.cpp).
		// Rotating in player-view space allows smooth pitch/yaw/roll in any
		// compass direction with zero clamping at vertical.
		if ( pOwner->m_nButtons & IN_USE )
		{
			int nMouseDx = 0, nMouseDy = 0;
			const CUserCmd *pCmd = pOwner->GetCurrentUserCommand();
			if ( pCmd != NULL )
			{
				nMouseDx = pCmd->mousedx;
				nMouseDy = pCmd->mousedy;
			}

			float flRotSens = clamp( physgun_rotation_sensitivity.GetFloat(), -1000.0f, 1000.0f );
			float flDeltaYaw   = -(float)nMouseDx * flRotSens;
			float flDeltaPitch =  (float)nMouseDy * flRotSens;

			// GMod reference: construct a coordinate frame from the player's view yaw
			QAngle playerYawAngles( 0, pOwner->EyeAngles().y, 0 );
			matrix3x4_t matPlayerYaw;
			AngleMatrix( playerYawAngles, matPlayerYaw );

			// Transform current held orientation into player view space
			QAngle localAngles = TransformAnglesToLocalSpace( m_heldWorldAngles, matPlayerYaw );

			// Apply mouse pitch/yaw deltas and A/D spin roll in player view space
			QAngle deltaAngles( flDeltaPitch, flDeltaYaw, 0.0f );
			if ( pOwner->m_nButtons & IN_MOVELEFT )
				deltaAngles.z -= phys_spinspeed.GetFloat() * gpGlobals->frametime;
			else if ( pOwner->m_nButtons & IN_MOVERIGHT )
				deltaAngles.z += phys_spinspeed.GetFloat() * gpGlobals->frametime;

			matrix3x4_t matLocal, matDelta, matNewLocal;
			AngleMatrix( localAngles, matLocal );
			AngleMatrix( deltaAngles, matDelta );
			ConcatTransforms( matDelta, matLocal, matNewLocal );
			MatrixAngles( matNewLocal, localAngles );

			// Transform back to world orientation -- no pitch clamping!
			// Shift+E angle snap grid (degrees, 0 disables): GMod quantizes its
			// accumulated VIEW-SPACE angles, so the snap rides the view axes and
			// each notch re-rounds -- turning the mouse walks the object notch
			// by notch regardless of world orientation.
			float flSnap = gm_snapangles.GetFloat();
			if ( flSnap > 0.0f && ( pOwner->m_nButtons & IN_SPEED ) )
			{
				localAngles.x = roundf( localAngles.x / flSnap ) * flSnap;
				localAngles.y = roundf( localAngles.y / flSnap ) * flSnap;
				localAngles.z = roundf( localAngles.z / flSnap ) * flSnap;
			}
			m_heldWorldAngles = TransformAnglesToWorldSpace( localAngles, matPlayerYaw );
		}

#ifndef CLIENT_DLL
		// HL2SB GMod compat (2026-10-14): wheel push/pull consumes the player's
		// per-tick mouse-wheel accumulator (CInput packs it into every usercmd;
		// player_command.cpp folds it into HL2SB_GetUserCmdMouseWheel).  GMod
		// moves max(|physgun_wheelspeed|,0.1) units per notch the same way.
		// The old IN_WEAPON1/2 button path never delivered its bits and is gone.
		int iWheelDelta = pOwner->HL2SB_GetUserCmdMouseWheel();
		if ( iWheelDelta != 0 )
		{
			HL2SB_AdjustDistance( (float)iWheelDelta * fmaxf( fabsf( physgun_wheelspeed.GetFloat() ), 0.1f ) );
			pOwner->HL2SB_ResetUserCmdMouseWheel();
		}
#endif

		QAngle angles = m_heldWorldAngles;
		if ( pObject->IsNPC() || pObject->MyNPCPointer() != NULL )
		{
			angles.x = 0.0f;
			angles.z = 0.0f;
		}

		// HL2SB GMod compat: teleport-drag for everything without a vphysics
		// body (NPCs, nextbots, scripted ents) - the entity stays alive and
		// keeps playing its animations (it slides exactly like the video's
		// GMan).
		if ( m_bDraggingNPC )
		{
			// hold the entity at the held distance ALONG THE VIEW, plus the
			// offset from the original grab point, so tall/anchored ents stay
			// where they were grabbed instead of snapping to the eye line
			Vector npcTarget = start + forward * m_distance + m_vecGrabOffset;
			// HL2SB (2026-09-29, GMod parity): the NPC path runs through the
			// same trace gate as props -- before this an NPC could be shoved
			// through walls and into the floor (the report).
			npcTarget = HL2SB_PhysgunGateTarget( pObject, npcTarget );
			pObject->Teleport( &npcTarget, &angles, NULL );
			pObject->SetAbsVelocity( vec3_origin );
			m_movementLength = ( npcTarget - pObject->GetLocalOrigin() ).Length();
			return;
		}
#else
		QAngle angles = m_gravCallback.TransformAnglesFromPlayerSpace( m_gravCallback.m_targetRotation, pOwner );
#endif

#ifndef CLIENT_DLL
		// HL2SB (2026-09-26): GMod pushes/pulls at a CONSTANT 100 u/s toward
		// physgun_maxrange / physgun_minrange while E is held (reference:
		// Approach(maxrange, dist, frametime*100) -- the old exponential
		// dist*0.1 crawl capped at 1024 felt nothing like it).
		if ( ( pOwner->m_nButtons & IN_USE ) && ( pOwner->m_nButtons & IN_FORWARD ) )
		{
#ifndef CLIENT_DLL
			pOwner->SetPhysicsFlag( PFLAG_DIROVERRIDE, true );
#endif
			m_distance = Approach( physgun_maxrange.GetFloat(), m_distance,
				gpGlobals->frametime * 100.0f );
		}

		if ( ( pOwner->m_nButtons & IN_USE ) && ( pOwner->m_nButtons & IN_BACK ) )
		{
#ifndef CLIENT_DLL
			pOwner->SetPhysicsFlag( PFLAG_DIROVERRIDE, true );
#endif
			m_distance = Approach( physgun_minrange.GetFloat(), m_distance,
				gpGlobals->frametime * 100.0f );
		}

		IPhysicsObject *pPhys = GetPhysObjFromPhysicsBone( pObject, m_physicsBone );
		if ( pPhys )
		{
			if ( pPhys->IsAsleep() )
			{
				// on the odd chance that it's gone to sleep while under anti-gravity
				pPhys->Wake();
			}

			Vector newPosition = start + forward * m_distance;
			// HL2SB (2026-10-03, server-side reference correction): the sweep
			// gate (the 0x400B trace cluster) exists ONLY on the teleport-drag
			// path (NPCs / body-less script ents).  A physics object rides the
			// shadow controller with NO trace gating at all -- vphysics itself
			// resolves the collisions, which is where the reference's smooth
			// prop slide actually comes from.  GateTarget here fought the
			// controller (stop-start crawling against thin geometry).
			m_gravCallback.SetTargetPosition( newPosition, angles );
			Vector dir = (newPosition - pObject->GetLocalOrigin());
			m_movementLength = dir.Length();
		}
#endif
	}
	else
	{
		m_targetPosition = end;
		//m_gravCallback.SetTargetPosition( end, m_gravCallback.m_targetRotation );
	}
}

void CWeaponGravityGun::SoundCreate( void )
{
}

void CWeaponGravityGun::SoundDestroy( void )
{
	SoundStop();
}

void CWeaponGravityGun::SoundStop( void )
{
	if ( m_sndMotor )
	{
		(CSoundEnvelopeController::GetController()).SoundDestroy( m_sndMotor );
		m_sndMotor = NULL;
	}
	if ( m_sndLockedOn )
	{
		(CSoundEnvelopeController::GetController()).SoundDestroy( m_sndLockedOn );
		m_sndLockedOn = NULL;
	}
	if ( m_sndLightObject )
	{
		(CSoundEnvelopeController::GetController()).SoundDestroy( m_sndLightObject );
		m_sndLightObject = NULL;
	}
	if ( m_sndHeavyObject )
	{
		(CSoundEnvelopeController::GetController()).SoundDestroy( m_sndHeavyObject );
		m_sndHeavyObject = NULL;
	}
}

void CWeaponGravityGun::SoundStart( void )
{
}

void CWeaponGravityGun::SoundUpdate( void )
{
}


void CWeaponGravityGun::EffectDestroy( void )
{
#ifdef CLIENT_DLL
	//gHUD.m_bSkipClear = false;
#endif
	m_active = false;
	SoundStop();

	DetachObject();
}

//-----------------------------------------------------------------------------
// HL2SB (2026-09-26): the HL2 physcannon effect system, ported from
// game/shared/hl2mp/weapon_physcannon.cpp (GMod's physgun client IS that
// class).  Sprite colours are tinted with the player's WEAPON colour where
// HL2 hardcoded its orange/blue.
//-----------------------------------------------------------------------------

#ifdef CLIENT_DLL

//-----------------------------------------------------------------------------
// Gets the complete list of values needed to render an effect
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Whether or not an effect is set to display
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Draws the effect sprite, given an effect parameter ID
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Render the physgun gun-glow sprite cluster
//-----------------------------------------------------------------------------
// HL2SB (2026-09-29, GMod reference client.dll  (CPhysBeam draw)
// +  (cluster) + , third iteration -- the video
// review): GMod anchors every sprite at the weapon's ATTACHMENT 1 (muzzle on
// the c_superphyscannon skeleton) and DRIFTS each next sprite along the
// attachment's forward direction -- there is NO random positional jitter and
// NO fork-attachment spread (the earlier fork1b/1m/1t spread drew at
// inconsistent spots and the +-5 random jitter read as flicker):
//     at rest              : 3 sprites, size sin(curtime*5 + i*15)*8 + 48,
//                            alpha RandomInt(120,255), drift 5.0/sprite,
//                            white
//     active, no target    : 2 sprites, size RandomFloat(16,64),
//                            alpha RandomInt(100,255), drift 2.0/sprite,
//                            white
//     active, holding      : 7 sprites, same sizes/alpha as above, drift
//                            2.0/sprite, tinted by the held object's RENDER
//                            colour (GMod reads the beam target's colour;
//                            there is NO weapon-colour fallback -- a white
//                            prop stays white)
// ( line 689 confirms the cluster mode flag is the weapon's
// active state, not the has-target state: 7 vs 2 is 's cVar3.)
// TINT (fourth-iteration correction):  calls  with
// `param_1 - 8` (beam minus 8), so 's `param_1 + 0x744` is the
// beam's +0x73C = the OWNING PLAYER, and the +0xc40 vcall is the player
// colour getter -- i.e. BOTH clusters (muzzle and the end cluster in
//  itself, whose own tint block also reads +0x73c) are tinted
// by the PLAYER'S WEAPON COLOUR, always.  The earlier "white / held-object
// colour" readings ignored the -8 pointer shift.

//-----------------------------------------------------------------------------
// Initialize all sprites and beams
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Closing effects
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Ready effects
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Holding effects
//-----------------------------------------------------------------------------
// HL2SB (2026-09-26, reference-confirmed): the HL2 physcannon "holding" state
// (core sprite between the claws, 6 fork glows, 3 endcaps, fork tendril beams)
// does not exist in GMod's physgun.  GMod's held-object look is only the
// physbeama "active" beam overlay plus the physg_glow endpoint pair, both
// driven by the beam draw path.  This hook is a no-op now; the state machine
// keeps running so m_EffectState networking is unchanged.

//-----------------------------------------------------------------------------
// Launch effects
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Shutdown for the weapon when it's holstered
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Idle effect (pulsing)
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Update the pose parameter for the gun (the claw open/close animation)
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Think function for the client
// HL2SB (2026-10-03): GONE - the reference weapon has NO ClientThink and
// renders nothing itself; there is nothing to pump.
//-----------------------------------------------------------------------------

#endif // CLIENT_DLL

//-----------------------------------------------------------------------------
// Effect dispatcher (shared -- the client realm applies the sprite state,
// the server realm just tracks the state value)
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Destroy all sprites and beams
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Open the claw elements (both realms drive this from the shared hold state)
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Close the claw elements
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Client entity created / received -- start the gun-glow system
//-----------------------------------------------------------------------------
#ifdef CLIENT_DLL
void CWeaponGravityGun::OnDataChanged( DataUpdateType_t type )
{
	BaseClass::OnDataChanged( type );

	if ( type == DATA_UPDATE_CREATED )
	{
		SetNextClientThink( CLIENT_THINK_ALWAYS );
	}
}
#endif

void CWeaponGravityGun::UpdateObject( void )
{
	CBasePlayer *pPlayer = ToBasePlayer( GetOwner() );
	Assert( pPlayer );

	CBaseEntity *pObject = m_hObject;
	if ( !pObject )
	{
		m_gravCallback.DetachEntity();
		return;
	}

	// HL2SB (2026-10-03): teleport-drive targets (NPCs, nextbots, brush
	// models, body-less script ents) have NO shadow controller - the drive
	// moves them in its own per-tick path.  Feeding them through the vphysics
	// callback here made it fail (no controller / no physics bone) and
	// DetachObject drop the handle, so the next EffectUpdate re-attached and
	// replayed the grab ACT_VM_PRIMARYATTACK - the "gun twitches the whole
	// time an NPC is held" report.  The handle stays put for drive targets.
	// (The flag is server-side only; the update itself runs server-side too.)
#ifndef CLIENT_DLL
	if ( m_bDraggingNPC )
		return;
#endif

	if ( !m_gravCallback.UpdateObject( pPlayer, pObject ) )
	{
		DetachObject();
		return;
	}
}

void CWeaponGravityGun::DetachObject( void )
{
	if ( m_bDetaching )
		return;
	m_bDetaching = true;
	CBaseEntity *pObject = m_hObject;
#ifndef CLIENT_DLL
	CBasePlayer *pOwner = ToBasePlayer( GetOwner() );
	if ( pObject != NULL )
	{
		if ( L != NULL && pOwner != NULL )
		{
			BEGIN_LUA_CALL_HOOK( "PhysgunDrop" );
				lua_pushplayer( L, pOwner );
				lua_pushentity( L, pObject );
			END_LUA_CALL_HOOK( 2, 0 );
		}
		if ( m_hObject.Get() != NULL )
			Pickup_OnPhysGunDrop( m_hObject, pOwner, DROPPED_BY_CANNON );
	}
	m_bDraggingNPC = false;
	if ( pOwner != NULL )
		pOwner->SetPhysicsFlag( PFLAG_DIROVERRIDE, false );
#endif

#ifndef CLIENT_DLL
	if ( m_hObject.Get() != NULL && !HL2SB_PhysgunHeldByOtherPlayer( m_hObject, pOwner ) )
	{
		if ( m_gravCallback.IsAttachedObjectValid() )
			PhysClearGameFlags( m_gravCallback.m_pAttachedObject, FVPHYSICS_PLAYER_HELD );
	}
#endif
	m_gravCallback.DetachEntity();
	m_hObject = NULL;
	m_physicsBone = 0;
	m_vecHoldPos = vec3_origin;
#ifndef CLIENT_DLL
	CPhysBeam *pBeam = dynamic_cast<CPhysBeam *>( (CBaseEntity *)m_hBeam );
	if ( pBeam != NULL )
		pBeam->TurnOff();
#endif
	m_bDetaching = false;
}

void CWeaponGravityGun::AttachObject( CBaseEntity *pObject, IPhysicsObject *pPhysics, short physicsbone, const Vector& start, const Vector &end, float distance )
{
	CBasePlayer *pOwner = ToBasePlayer( GetOwner() );
	if ( pOwner == NULL || pObject == NULL || m_bDetaching )
		return;
	if ( m_hObject != NULL || m_gravCallback.m_controller != NULL || m_gravCallback.m_pAttachedObject != NULL )
		DetachObject();
	m_physicsBone = 0;
	m_vecHoldPos = vec3_origin;
	m_useDown = false;

#ifndef CLIENT_DLL
	m_bDraggingNPC = false;
	m_heldWorldAngles = vec3_angle;
	m_vecGrabOffset = vec3_origin;
	// HL2SB GMod compat: the gamemode rules live in GM:PhysgunPickup( ply, ent ).
	// GMod contract: literal false vetoes, literal true force-allows (that is
	// how gamemodes allow grabbing PLAYERS), nil keeps the physgun's own rules
	// (players never grabbable).
	bool bPhysgunPickupAllowed = true;
	bool bForceAllow = false;
	if ( L != NULL )
	{
		BEGIN_LUA_CALL_HOOK( "PhysgunPickup" );
			lua_pushplayer( L, pOwner );
			lua_pushentity( L, pObject );
		END_LUA_CALL_HOOK( 2, 1 );

		if ( lua_gettop( L ) > 0 )
		{
			if ( lua_isboolean( L, -1 ) )
			{
				if ( lua_toboolean( L, -1 ) == 0 )
					bPhysgunPickupAllowed = false;
				else
					bForceAllow = true;
			}
			lua_pop( L, 1 );
		}
	}

	if ( !bPhysgunPickupAllowed )
	{
		m_hObject = NULL;
		return;
	}

	if ( pObject->IsPlayer() && !bForceAllow )
	{
		m_hObject = NULL;
		return;
	}

	// GMod implements physgun_limited in the GAMEMODE, not in the C++ weapon:
	// sandbox/gamemode/shared.lua registers the convar with the help text
	// "Prevent the Physics Gun from picking up certain map entities." and its
	// GM:PhysgunPickup rejects prop_dynamic, prop_door, func_physbox with
	// SF_PHYSBOX_MOTIONDISABLED, and prop_* with SF_PHYSPROP_MOTIONDISABLED /
	// SF_PHYSPROP_PREVENT_PICKUP.  Ordinary props AND NPCs stay pickable, so no
	// C++ rule belongs here (an earlier hard "prop_ only" rule was wrong).

	// HL2SB: a wheel burst accumulated while not holding must not fling the
	// new hold distance on the first tick after the grab.
	pOwner->HL2SB_ResetUserCmdMouseWheel();

	// HL2SB GMod compat: 笔刷实体（func_reflective_glass, func_brush, func_door 等）
	// 以及无刚体的世界装饰绝对不可抓取。
	// 允许抓取的实体必须满足：
	// 1) 拥有 MOVETYPE_VPHYSICS 且带有有效物理对象；
	// 2) 或者属于真 NPC / NextBot（通过 IsNPC 或 MyNPCPointer 判断）；
	// 3) 或者属于被 gamemode 显式放行的玩家。
	bool bHasBody = ( pPhysics != NULL && pObject->GetMoveType() == MOVETYPE_VPHYSICS );
	bool bIsNPC = pObject->IsNPC() || pObject->MyNPCPointer() != NULL || pObject->MyNextBotPointer() != NULL ||
		( dynamic_cast< CBaseScripted * >( pObject ) != NULL && pObject->GetMoveType() == MOVETYPE_STEP );
	bool bIsPlayer = pObject->IsPlayer();

	if ( !bHasBody && !bIsNPC && !bIsPlayer )
	{
		m_hObject = NULL;
		return;
	}

	if ( bIsPlayer )
		m_bDraggingNPC = bForceAllow && !bHasBody;
	else
		m_bDraggingNPC = bIsNPC && !bHasBody;

	// GMod style: the held object keeps its WORLD orientation, E+mouse rotates
	m_heldWorldAngles = pObject->GetAbsAngles();

	if ( !m_heldWorldAngles.IsValid() || !end.IsValid() )
	{
		m_bDraggingNPC = false;
		return;
	}
	m_hObject = pObject;
	m_physicsBone = physicsbone;
#endif

#ifndef CLIENT_DLL
	if ( m_bDraggingNPC )
	{
		// teleport-drag attach: no controller, no bone bookkeeping - just hold
		// the handle.  Keep the grab offset (origin vs. hit point) so the
		// entity does not snap toward the player's eye line.
		m_vecGrabOffset = pObject->GetAbsOrigin() - end;

		// HL2SB: store the grab point in ENTITY-LOCAL space, via the same
		// single world->entity transform the client inverts with
		// EntityToWorldSpace.  (The old MatrixInverseTR + VectorITransform
		// pair inverted TWICE - world point times the world rotation - so
		// the networked hold point was garbage and the beam end flew off
		// every dragged NPC.)
		pObject->WorldToEntitySpace( end, &m_vecHoldPos.GetForModify() );
		Pickup_OnPhysGunPickup( pObject, pOwner );

		m_distance = distance;

		// GM:OnPhysgunPickup( ply, ent ) -- fired after a SUCCESSFUL pickup
		if ( L != NULL )
		{
			BEGIN_LUA_CALL_HOOK( "OnPhysgunPickup" );
				lua_pushplayer( L, pOwner );
				lua_pushentity( L, pObject );
			END_LUA_CALL_HOOK( 2, 0 );
		}


		return;
	}
#endif

#ifdef CLIENT_DLL
	matrix3x4_t bodyToWorld;
	m_hObject = pObject;
	m_physicsBone = physicsbone;
	m_distance = distance;
	if ( pObject->GetMoveType() == MOVETYPE_VPHYSICS &&
		HL2SB_PhysgunGetPhysicsBoneMatrix( pObject, physicsbone, bodyToWorld ) )
	{
		VectorITransform( end, bodyToWorld, m_vecHoldPos.GetForModify() );
		return;
	}
	if ( pObject->IsNPC() || pObject->IsNextBot() ||
		( dynamic_cast< CBaseScripted * >( pObject ) != NULL && pObject->GetMoveType() == MOVETYPE_STEP ) )
	{
		pObject->WorldToEntitySpace( end, &m_vecHoldPos.GetForModify() );
		return;
	}
	m_hObject = NULL;
	m_physicsBone = 0;
	return;
#else
	if ( pPhysics && pObject->GetMoveType() == MOVETYPE_VPHYSICS )
	{
		m_distance = distance;

		Vector worldPosition;
		pPhysics->WorldToLocal( &worldPosition, end );
		m_worldPosition = worldPosition;

		m_vecHoldPos = worldPosition;
		Vector vecOrigin;
		pPhysics->GetPosition( &vecOrigin, &m_heldWorldAngles );
		if ( !vecOrigin.IsValid() || !m_heldWorldAngles.IsValid() || !worldPosition.IsValid() )
		{
			m_hObject = NULL;
			m_physicsBone = 0;
			m_vecHoldPos = vec3_origin;
			return;
		}
		m_gravCallback.AttachEntity( pOwner, pObject, pPhysics, physicsbone, end );

		m_originalObjectPosition = vecOrigin;

		pPhysics->Wake();
		PhysSetGameFlags( pPhysics, FVPHYSICS_PLAYER_HELD );

		Pickup_OnPhysGunPickup( pObject, pOwner );

		// GM:OnPhysgunPickup( ply, ent ) -- fired after a SUCCESSFUL pickup
		if ( L != NULL )
		{
			BEGIN_LUA_CALL_HOOK( "OnPhysgunPickup" );
				lua_pushplayer( L, pOwner );
				lua_pushentity( L, pObject );
			END_LUA_CALL_HOOK( 2, 0 );
		}
	}
	else
	{
		m_hObject = NULL;
		m_physicsBone = 0;
	}
#endif
}

//=========================================================
//=========================================================
void CWeaponGravityGun::PrimaryAttack( void )
{
	if ( !m_active )
	{
		EffectCreate();
		SoundCreate();
	}
	else
	{
		EffectUpdate();
		SoundUpdate();
	}
}

void CWeaponGravityGun::PlayAttackAnimation( void )
{
#ifdef CLIENT_DLL
	CBasePlayer *pOwner = ToBasePlayer( GetOwner() );
	if ( pOwner == NULL || pOwner != C_BasePlayer::GetLocalPlayer() )
		return;

	CBaseViewModel *pViewModel = pOwner->GetViewModel( m_nViewModelIndex, false );
	if ( pViewModel == NULL )
		return;

	// The weapon's render model may be the world model, not the viewmodel.
	const int nSequence = pViewModel->SelectWeightedSequence( ACT_VM_PRIMARYATTACK );
	if ( nSequence != ACTIVITY_NOT_AVAILABLE )
		pViewModel->SendViewModelMatchingSequence( nSequence );
#else
	SendWeaponAnim( ACT_VM_PRIMARYATTACK );
#endif
}

void CWeaponGravityGun::SecondaryAttack( void )
{
	CBasePlayer *pOwner = ToBasePlayer( GetOwner() );
	if ( pOwner == NULL || !m_hObject.Get() || m_flNextSecondaryAttack > gpGlobals->curtime )
		return;
	m_flNextPrimaryAttack = gpGlobals->curtime + 0.5f;
	m_flNextSecondaryAttack = gpGlobals->curtime + 0.5f;
#ifndef CLIENT_DLL
	if ( L != NULL )
	{
		IPhysicsObject *pPhys = m_bDraggingNPC ? NULL : GetPhysObjFromPhysicsBone( m_hObject, m_physicsBone );
		BEGIN_LUA_CALL_HOOK( "OnPhysgunFreeze" );
			lua_pushentity( L, this );
			lua_pushphysicsobject( L, pPhys );
			lua_pushentity( L, m_hObject );
			lua_pushplayer( L, pOwner );
		END_LUA_CALL_HOOK( 4, 0 );
	}
#endif
	PlayAttackAnimation();
	m_bFreezeReleaseLatch = true;
	DetachObject();
}

#ifdef CLIENT_DLL
extern bool g_bRenderingReflection; // HL2SB: mirror reflection flag in viewrender.cpp

//-----------------------------------------------------------------------------
// HL2SB (2026-10-03): the reference client's physgun beam visual, drawn from
// the WEAPON's own networked state (m_active / m_hObject / m_physicsBone /
// m_vecHoldPos - all already in DT_WeaponGravityGun).  One implementation
// serves every view; pViewModel is the anchor when the caller draws from the
// view-model pass (first person), NULL anchors on this weapon entity's
// attachment 1 (third person / mirrors), falling back to the owner's shoot
// position.
//
//   gate      : physgun_drawbeams + the DrawPhysgunBeam Lua hook (6 args,
//               literal false suppresses everything).  This path is the ONE
//               authoritative hook source; the debug-only beam entity does
//               not fire it.
//   idle      : 3 claw sprites at the anchor's attachment 1, pulsing
//               sin(t*5 + i*15)*8+48, alpha RandomInt(120,255); NO beam,
//               NO end dot.  Shows whenever the physgun is the holder's
//               active weapon - NOT tied to holding.
//   active    : quadratic-Bezier ribbon (17 points) muzzle -> hold point,
//               3 passes with widths 2.0 / rand(2,5) / rand(2,6) and black
//               endpoints (physbeama + reversed scroll on bone holds); claw
//               sprites (2, or 7 on a bone target) march along the aim
//               direction at 2 units each; end dot = 2 (7 on a bone target)
//               sprites of size rand(1,16), alpha RandomInt(100,255) at the
//               hold point
//   colour    : every sprite is the OWNER PLAYER's weapon colour (the
//               reference reads m_hPlayer's render colour); the ribbon
//               carries the same colour
//   hold point: m_vecHoldPos transformed by the target -- a plain world
//               offset while the target is unrotated, the physics
//               element's bone matrix for MOVETYPE_VPHYSICS, the entity
//               matrix otherwise; no target -> the scan trace endpos
//-----------------------------------------------------------------------------
static void HL2SB_DrawPhysgunRibbon( const Vector &start, const Vector &control, const Vector &end, float width, const Vector &color, float scroll )
{
	// 17-point quadratic Bezier with black endpoints; the texture scroll is
	// scaled by the span length (texcoord = fmod(scroll,1) - len * t * 0.02),
	// which the engine's unit-speed DrawBeamQuadratic cannot express.
	const int subdivisions = 16;
	const float flSpan = start.DistTo( end );

	CMatRenderContextPtr pRenderContext( materials );
	CBeamSegDraw beamDraw;
	beamDraw.Start( pRenderContext, subdivisions + 1, NULL );

	BeamSeg_t seg;
	seg.m_flAlpha = 1.0f;
	seg.m_flWidth = width;

	float u = fmod( scroll, 1.0f );
	float dt = 1.0f / (float)subdivisions;
	float t = 0.0f;
	for ( int i = 0; i <= subdivisions; i++, t += dt )
	{
		float omt = ( 1.0f - t );
		seg.m_vPos = omt * omt * start + 2.0f * t * omt * control + t * t * end;
		seg.m_flTexCoord = u - flSpan * t * 0.02f;
		seg.m_vColor = ( i == 0 || i == subdivisions ) ? vec3_origin : color;
		beamDraw.NextSeg( &seg );
	}

	beamDraw.End();
}

static void HL2SB_DrawPhysgunSprites( IMaterial *pMaterial, const Vector &pos, const Vector &fwd, int nCount, float flDrift, byte r, byte g, byte b, float flSizeMin, float flSizeMax, int nAlphaMin, int nAlphaMax )
{
	CMatRenderContextPtr pRenderContext( materials );
	Vector vecPos = pos;
	for ( int i = 0; i < nCount; i++ )
	{
		float flSize = random->RandomFloat( flSizeMin, flSizeMax );
		color32 clr;
		clr.r = r; clr.g = g; clr.b = b;
		clr.a = (byte)random->RandomInt( nAlphaMin, nAlphaMax );
		pRenderContext->Bind( pMaterial );
		// HL2SB (2026-10-03): every "size" in this file is a HALF extent in
		// the reference's sprite quad, while DrawSprite takes FULL width and
		// height -- the same rule the idle claw loop below applies with its
		// explicit * 2.0f.  Without the doubling the holding sprites drew
		// half-size and vanished behind the weapon body.
		DrawSprite( vecPos, flSize * 2.0f, flSize * 2.0f, clr );
		vecPos += fwd * flDrift;
	}
}

void CWeaponGravityGun::DrawPhysgunVisuals( C_BaseViewModel *pViewModel )
{
	C_BasePlayer *pOwner = ToBasePlayer( GetOwner() );
	if ( pOwner == NULL )
		return;

	// Idle glow policy: the claw glow shows whenever the physgun is the
	// holder's ACTIVE weapon - not tied to holding.  DrawModel and
	// ViewModelDrawn only run for a drawn weapon; this re-checks the
	// holder's inventory so a holstered gun never glows.
	if ( pOwner->GetActiveWeapon() != this )
		return;

	// The anchor is the view model's attachment 1 (muzzle) in first person;
	// the weapon entity's attachment 1 otherwise (the mirror passes may cull
	// the view model).  Fallback: the owner's shoot position.
	C_BaseAnimating *pAnchor = pViewModel;
	if ( pAnchor == NULL )
		pAnchor = this;

	Vector vecMuzzle;
	QAngle angMuzzle;
	if ( !pAnchor->GetAttachment( 1, vecMuzzle, angMuzzle ) )
	{
		vecMuzzle = pOwner->Weapon_ShootPosition();
		angMuzzle = pOwner->EyeAngles();
	}

	// The ribbon direction is the PLAYER'S AIM - the eye angles (body angles
	// in a vehicle).  Not the muzzle attachment's angles, which sway with
	// the view-model animation.
	Vector vecAimDir;
	if ( pOwner->GetVehicle() != NULL )
		AngleVectors( pOwner->GetAbsAngles(), &vecAimDir );
	else
		AngleVectors( pOwner->EyeAngles(), &vecAimDir );

	bool bBoneTarget = ( m_hObject != NULL );

	// The beam end: m_vecHoldPos transformed back by the target, or the scan
	// trace endpos when nothing is held.
	// For multi-bone ragdolls, bone-matrix transform is used.
	// For standard props, EntityToWorldSpace mirrors server's WorldToLocal exactly.
	C_BaseEntity *pTarget = m_hObject;
	Vector vecEnd;
	if ( pTarget != NULL )
	{
		if ( pTarget->GetMoveType() == MOVETYPE_VPHYSICS )
		{
			matrix3x4_t bodyToWorld;
			if ( HL2SB_PhysgunGetPhysicsBoneMatrix( pTarget, m_physicsBone, bodyToWorld ) )
				VectorTransform( m_vecHoldPos, bodyToWorld, vecEnd );
			else
				pTarget->EntityToWorldSpace( m_vecHoldPos, &vecEnd );
		}
		else
		{
			pTarget->EntityToWorldSpace( m_vecHoldPos, &vecEnd );
		}
	}
	else
	{
		trace_t tr;
		Vector start = pOwner->Weapon_ShootPosition();
		UTIL_TraceLine( start, start + vecAimDir * ( physgun_maxrange.GetFloat() * 0.5f ),
			MASK_SHOT_HULL, pOwner, COLLISION_GROUP_NONE, &tr );
		vecEnd = tr.endpos;
	}

	// GM:DrawPhysgunBeam( ply, weapon, bOn, target, boneid, pos ) - a literal
	// false suppresses the entire visual.
	bool bSuppressed = false;
	if ( L != NULL )
	{
		BEGIN_LUA_CALL_HOOK( "DrawPhysgunBeam" );
			lua_pushplayer( L, pOwner );
			lua_pushentity( L, this );
			lua_pushboolean( L, m_active != 0 );
			lua_pushentity( L, pTarget );
			lua_pushinteger( L, m_physicsBone );
			lua_pushvector( L, vecEnd );
		END_LUA_CALL_HOOK( 6, 1 );

		if ( lua_gettop( L ) > 0 )
		{
			bSuppressed = ( lua_isboolean( L, -1 ) && lua_toboolean( L, -1 ) == 0 );
			lua_pop( L, 1 );
		}
	}

	if ( bSuppressed )
		return;

	// Every sprite and the ribbon carry the HOLDER's weapon colour.  The sprite
	// path is a byte pipeline (as the reference engine's sprites are), so HDR
	// components above 1 clamp here - only the material proxies keep the float.
	Vector vW = HL2SB_GetWeaponColor( pOwner->GetUserID() );
	byte r = (byte)clamp( vW.x * 255.0f, 0.0f, 255.0f );
	byte g = (byte)clamp( vW.y * 255.0f, 0.0f, 255.0f );
	byte b = (byte)clamp( vW.z * 255.0f, 0.0f, 255.0f );

	IMaterial *pGlowClaw = materials->FindMaterial( "sprites/physg_glow2", TEXTURE_GROUP_CLIENT_EFFECTS );
	IMaterial *pGlowEnd = materials->FindMaterial( "sprites/physg_glow1", TEXTURE_GROUP_CLIENT_EFFECTS );

	// IDLE (scan beam off): 3 pulsing claw sprites at the muzzle only.
	// HL2SB (2026-10-03, reference alignment): the reference's idle call site
	// hands the sprite pass the VIEWMODEL MUZZLE attachment angles (it reads
	// GetAttachment(1) pos AND ang), so the drift rides the barrel and sways
	// with the view model - the player's eye aim is only the holding-path
	// direction.  And the reference sprite quad takes HALF extents while
	// DrawSprite takes full width/height, so every "size" below is a half
	// extent and goes through DrawSprite doubled.
	if ( !m_active )
	{
		CMatRenderContextPtr pRenderContext( materials );
		Vector vecPos = vecMuzzle;
		Vector vecDrift;
		AngleVectors( angMuzzle, &vecDrift );
		for ( int i = 0; i < 3; i++ )
		{
			float flSize = sinf( gpGlobals->curtime * 5.0f + (float)i * 15.0f ) * 8.0f + 48.0f;
			color32 clr;
			clr.r = r; clr.g = g; clr.b = b;
			clr.a = (byte)random->RandomInt( 120, 255 );
			pRenderContext->Bind( pGlowClaw );
			DrawSprite( vecPos, flSize * 2.0f, flSize * 2.0f, clr );
			vecPos += vecDrift * 5.0f;
		}
		return;
	}

	extern ConVar physgun_drawbeams;
	if ( !physgun_drawbeams.GetBool() )
		return;

	// ACTIVE: claw sprites (2, or 7 with a target) marching along the aim
	// direction at 2 units each.
	int nClaw = bBoneTarget ? 7 : 2;
	// HL2SB (2026-10-03): the reference's hold claw size arguments were
	// optimized out of the binary, but gameplay footage shows the muzzle glow
	// keeps the IDLE scale on grab.  The previous RandomFloat(4, 48) shrank
	// the light on every attack ("muzzle light got smaller") - use the idle
	// pulsing formula instead.
	{
		CMatRenderContextPtr pSpriteContext( materials );
		Vector vecPos = vecMuzzle;
		for ( int i = 0; i < nClaw; i++ )
		{
			float flSize = sinf( gpGlobals->curtime * 5.0f + (float)i * 15.0f ) * 8.0f + 48.0f;
			color32 clr;
			clr.r = r; clr.g = g; clr.b = b;
			clr.a = (byte)random->RandomInt( 100, 255 );
			pSpriteContext->Bind( pGlowClaw );
			DrawSprite( vecPos, flSize * 2.0f, flSize * 2.0f, clr );
			vecPos += vecAimDir * 2.0f;
		}
	}

	// The control point follows the aim ray while the end follows the held body.
	Vector vecControl = pTarget != NULL
		? pOwner->Weapon_ShootPosition() + vecAimDir * vecMuzzle.DistTo( vecEnd ) * 0.6f
		: vecEnd;

	CMatRenderContextPtr pRenderContext( materials );
	IMaterial *pRibbon = materials->FindMaterial( bBoneTarget ? "sprites/physbeama" : "sprites/physbeam", TEXTURE_GROUP_CLIENT_EFFECTS );
	pRenderContext->Bind( pRibbon );
	Vector vecRibbonColor( r / 255.0f, g / 255.0f, b / 255.0f );

	float flScroll = gpGlobals->curtime * ( bBoneTarget ? 10.0f : -2.0f );
	HL2SB_DrawPhysgunRibbon( vecMuzzle, vecControl, vecEnd, 2.0f, vecRibbonColor, flScroll );
	HL2SB_DrawPhysgunRibbon( vecEnd, vecControl, vecMuzzle, random->RandomFloat( 2.0f, 5.0f ), vecRibbonColor, flScroll * 1.1f );
	HL2SB_DrawPhysgunRibbon( vecEnd, vecControl, vecMuzzle, random->RandomFloat( 2.0f, 6.0f ), vecRibbonColor, flScroll * -0.9f );

	// End dot at the hold point - the "aim dot"; idle shows NOTHING there.
	int nEnd = bBoneTarget ? 7 : 2;
	HL2SB_DrawPhysgunSprites( pGlowEnd, vecEnd, vecAimDir, nEnd, 0.0f, r, g, b, 1.0f, 16.0f, 100, 255 );
}

//-----------------------------------------------------------------------------
// Third-person / mirror draw.  The weapon is always in the render lists (it
// carries a studio model), so this is the render path that CANNOT silently
// vanish.  The weapon body renders exactly like the base class on whatever
// pass the engine asks for; the beam visual joins it on the transparency
// pass.  First person is ViewModelDrawn's job.
//-----------------------------------------------------------------------------
int CWeaponGravityGun::DrawModel( int flags )
{
	// Keep the weapon body drawing exactly like the base class on both
	// passes (IsTransparent classifies this renderable as translucent, so
	// the transparency pass is the only one that carries the body).
	int nRet = BaseClass::DrawModel( flags );

	if ( flags & STUDIO_TRANSPARENCY )
	{
		C_BasePlayer *pOwner = ToBasePlayer( GetOwner() );
		if ( pOwner != NULL )
		{
			// The local player's world weapon stays in the render lists for
			// mirror reflections, so skip the first-person copy here -
			// ViewModelDrawn draws it at the view model.
			if ( !IsCarriedByLocalPlayer() || g_bRenderingReflection || !ShouldDrawUsingViewModel() )
			{
				DrawPhysgunVisuals( NULL );
			}
		}
	}

	return nRet;
}

//-----------------------------------------------------------------------------
// First-person function call after the view model has been drawn.  The
// visual draws in WORLD SPACE while the view-model pass is active; without
// a 3D view push, world-space draws made here get depth-clipped (proven in
// the earlier build whose beam rendered) - so wrap them in the player view.
//-----------------------------------------------------------------------------
void CWeaponGravityGun::ViewModelDrawn( C_BaseViewModel *pBaseViewModel )
{
	C_BasePlayer *pOwner = ToBasePlayer( GetOwner() );
	if ( pOwner != NULL && pOwner == C_BasePlayer::GetLocalPlayer() )
	{
		CViewSetup beamView = *view->GetPlayerViewSetup();

		Frustum dummyFrustum;
		render->Push3DView( beamView, 0, NULL, dummyFrustum );

		// HACK HACK: munge the depth range to prevent the beam from poking
		// into walls (same trick the working build used).
		CMatRenderContextPtr pRenderContext( materials );
		pRenderContext->DepthRange( 0.1f, 0.2f );

		DrawPhysgunVisuals( pBaseViewModel );

		// HL2SB (2026-10-03, the "first-person arms invisible" root cause):
		// this runs INSIDE the view model pass (viewrender.cpp DrawViewModels
		// set DepthRange(0.0f, 0.1f) for the whole pass, the depth hack that
		// keeps the gun from clipping into walls).  Restoring the WORLD range
		// (0,1) here leaked that state into everything the pass draws AFTER
		// this weapon - the gmod_hands entity drawn from GM:PostDrawViewModel.
		// The gun is already in the depth buffer compressed into [0,0.1]; the
		// arms, merged onto the very same bones at the same world positions,
		// then map to depth values ten times larger and lose the depth test
		// everywhere they overlap the gun - drawn every frame, never visible.
		// Restore the pass's working band; DrawViewModels itself puts the full
		// range back after PostDrawViewModel.
		pRenderContext->DepthRange( 0.0f, 0.1f );

		render->PopView( dummyFrustum );
	}

	// Pass this back up
	BaseClass::ViewModelDrawn( pBaseViewModel );
}

//-----------------------------------------------------------------------------
// We are always considered transparent - this is what gives DrawModel its
// STUDIO_TRANSPARENCY pass (the leaf system classifies the renderable by
// IsTransparent) and puts the beam visual on the sorted translucent pass.
//-----------------------------------------------------------------------------
bool CWeaponGravityGun::IsTransparent( void )
{
	return true;
}

#endif

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CWeaponGravityGun::ItemPreFrame()
{
	BaseClass::ItemPreFrame();

#ifndef CLIENT_DLL
	// Update the object if the weapon is switched on.
	if( m_active )
	{
		UpdateObject();
	}
#endif
}

void CWeaponGravityGun::ItemPostFrame( void )
{
	CBasePlayer *pOwner = ToBasePlayer( GetOwner() );
	if (!pOwner)
		return;

#ifndef CLIENT_DLL
	pOwner->SetPhysicsFlag( PFLAG_DIROVERRIDE, m_hObject != NULL && ( pOwner->m_nButtons & IN_USE ) != 0 );
	// HL2SB (2026-10-03): the beam entity exists from deploy (the reference
	// creates it on its holster/deploy path), so the idle claw glow shows as
	// soon as the gun is out - not only after the first grab.
	if ( m_hBeam == NULL )
	{
		CPhysBeam *pNew = (CPhysBeam *)CreateEntityByName( "physgun_beam" );
		if ( pNew != NULL )
		{
			pNew->SetHolder( pOwner );
			pNew->Spawn();
			m_hBeam = pNew;
		}
	}
#endif

	// GMod keeps the final pose after the deploy/grab/freeze animation.
	// ItemPostFrame does not start a looping idle animation.

	// HL2SB GMod compat: SecondaryAttack (freeze)
	if ( pOwner->m_afButtonPressed & IN_ATTACK2 )
	{
		SecondaryAttack();
	}

	// HL2SB GMod compat: R works whether or not LMB is held (unfreeze aimed /
	// dragged / everything on double-tap)
	if ( pOwner->m_afButtonPressed & IN_RELOAD )
	{
		Reload();
	}

	if ( pOwner->m_nButtons & IN_ATTACK )
	{
		PrimaryAttack();
	}
	else
	{
		// HL2SB GMod compat: LMB up -> the freeze-release latch lifts, the
		// scan may grab (and unfreeze) again on the next click.
		m_bFreezeReleaseLatch = false;
		if ( m_active )
		{
			EffectDestroy();
			SoundDestroy();
		}


		return;
	}
}

//-----------------------------------------------------------------------------
// Purpose:
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool CWeaponGravityGun::HasAnyAmmo( void )
{
	//Always report that we have ammo
	return true;
}

// HL2SB (2026-09-25): the hl2sb_physgun_push/pull console commands are gone --
// the wheel now rides the native IN_WEAPON1/2 usercmd button path (consumed in
// EffectUpdate), which is server-authoritative and does not depend on any
// client-side hold detection.

#ifndef CLIENT_DLL
// HL2SB GMod compat (2026-09-25): server-side "is this player's physgun
// carrying something" -- used by CHL2_Player::PlayerUse to suppress the HL2
// +use system while carrying (GMod: E belongs to the physgun when holding;
// the use system's interaction sound and use-grab fought the carry).
bool HL2SB_PhysgunServerIsHolding( CBasePlayer *pPlayer )
{
	CBaseCombatWeapon *pWpn = pPlayer ? pPlayer->GetActiveWeapon() : NULL;
	if ( pWpn == NULL || !FClassnameIs( pWpn, "weapon_physgun" ) )
		return false;
	CWeaponGravityGun *pGun = dynamic_cast< CWeaponGravityGun * >( pWpn );
	return pGun != NULL && pGun->IsHolding();
}
#endif

#ifndef CLIENT_DLL
void HL2SB_PhysgunForceDrop( CBaseCombatWeapon *pWeapon, CBaseEntity *pOnlyIfHoldingThis )
{
	CWeaponGravityGun *pGun = dynamic_cast<CWeaponGravityGun *>( pWeapon );
	if ( pGun != NULL && ( pOnlyIfHoldingThis == NULL || pGun->GetHeldEntity() == pOnlyIfHoldingThis ) )
		pGun->EffectDestroy();
}
#endif

bool CWeaponGravityGun::Reload( void )
{
#ifndef CLIENT_DLL
	CBasePlayer *pOwner = ToBasePlayer( GetOwner() );
	if ( pOwner == NULL )
		return false;

	// GMod 0.05s gate
	float flNow = gpGlobals->curtime;
	if ( flNow - m_flLastReloadPress < 0.05f )
		return false;
	m_flLastReloadPress = flNow;

	// GMod reference: C++ Reload only dispatches GM:OnPhysgunReload( weapon, ply )
	// Unfreezing and player notifications are handled completely by Lua
	if ( L != NULL )
	{
		BEGIN_LUA_CALL_HOOK( "OnPhysgunReload" );
			lua_pushentity( L, this );
			lua_pushplayer( L, pOwner );
		END_LUA_CALL_HOOK( 2, 0 );
	}

	return true;
#endif
	return false;
}

#ifdef CLIENT_DLL
//-----------------------------------------------------------------------------
// HL2SB GMod compat: client hooks the rest of the client dll uses -
//   * the outline/glow shell pass on the held entity (c_baseanimating)
//   * mouse-view interception while E-rotating (in_mouse)
//   * wheel push/pull instead of weapon switching (weapon_selection)
//-----------------------------------------------------------------------------
C_BaseEntity *HL2SB_PhysgunHeldEntity( void )
{
	C_BasePlayer *pLocal = C_BasePlayer::GetLocalPlayer();
	if ( pLocal == NULL )
		return NULL;

	CBaseCombatWeapon *pWpn = pLocal->GetActiveWeapon();
	C_WeaponGravityGun *pGun = dynamic_cast< C_WeaponGravityGun * >( pWpn );
	return ( pGun != NULL && pGun->IsHolding() ) ? pGun->GetHeldEntity() : NULL;
}

bool HL2SB_PhysgunMouseRotate( void )
{
	C_BasePlayer *pLocal = C_BasePlayer::GetLocalPlayer();
	if ( pLocal == NULL )
		return false;

	CBaseCombatWeapon *pWpn = pLocal->GetActiveWeapon();
	C_WeaponGravityGun *pGun = dynamic_cast< C_WeaponGravityGun * >( pWpn );
	if ( pGun == NULL || !pGun->IsHolding() )
		return false;

	// E held: the mouse belongs to the object, not the view
	return ( input->GetButtonBits( 0 ) & IN_USE ) != 0;
}

bool HL2SB_PhysgunIsHolding( void )
{
	C_BasePlayer *pLocal = C_BasePlayer::GetLocalPlayer();
	if ( pLocal == NULL )
		return false;

	CBaseCombatWeapon *pWpn = pLocal->GetActiveWeapon();
	C_WeaponGravityGun *pGun = dynamic_cast< C_WeaponGravityGun * >( pWpn );
	return ( pGun != NULL && pGun->IsHolding() );
}

IMaterial *HL2SB_PhysgunGlowMaterial( void )
{
	static IMaterial *pMaterial = NULL;
	if ( pMaterial == NULL )
	{
		pMaterial = materials->FindMaterial( "models/effects/hl2sb_physgun_glow", TEXTURE_GROUP_CLIENT_EFFECTS );
		if ( pMaterial != NULL )
			pMaterial->IncrementReferenceCount();
	}
	return pMaterial;
}
#endif

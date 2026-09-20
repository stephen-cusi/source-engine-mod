//========= Copyright � 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose:
//
// $NoKeywords: $
//=============================================================================//

#include "cbase.h"
#include "beam_shared.h"
#ifndef CLIENT_DLL
#include "player.h"
#endif
#include "gamerules.h"
#ifdef CLIENT_DLL
#include "clienteffectprecachesystem.h"
#endif
#ifndef CLIENT_DLL
#include "baseviewmodel.h"
#endif
#include "vphysics/constraints.h"
#include "physics.h"
#include "in_buttons.h"
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
#include "view_shared.h"
#include "view.h"
#include "iviewrender.h"
#include "ragdoll.h"
#include "c_basehlcombatweapon.h"
#include "beamdraw.h"
#include "iefx.h"		// HL2SB GMod compat: the held prop's full-body soft light (dlight)
#include "dlight.h"
#include "iinput.h"				// HL2SB GMod compat: E-rotate input interception
#include "materialsystem/imaterialsystem.h"
#include "texture_group_names.h"
#else
#include "physics_prop_ragdoll.h"
#include "props.h"
#include "basehlcombatweapon.h"
#include "ai_basenpc.h"	// HL2SB GMod compat: MyNPCPointer()->CanBecomeRagdoll() in the NPC-grab path
// HL2SB GMod compat: the physgun's held-prop glow sprite + the GMod physgun
// hooks (GM:PhysgunPickup / GM:PhysgunDrop).
#include "Sprite.h"
#if defined ( LUA_SDK ) && !defined ( CLIENT_DLL )
#include "luamanager.h"
#include "lbaseentity_shared.h"
#include "lbaseplayer_shared.h"
#endif
#endif

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

static int g_physgunBeam1;
static int g_physgunBeam;
static int g_physgunGlow;

#define PHYSGUN_BEAM_SPRITE1	"sprites/physbeam1.vmt"
#define PHYSGUN_BEAM_SPRITE		"sprites/physbeam.vmt"
#define PHYSGUN_BEAM_GLOW		"sprites/physglow.vmt"

#define	PHYSGUN_SKIN	1

class CWeaponGravityGun;

#ifdef CLIENT_DLL
CLIENTEFFECT_REGISTER_BEGIN( PrecacheEffectGravityGun )
CLIENTEFFECT_MATERIAL( "sprites/physbeam1" )
CLIENTEFFECT_MATERIAL( "sprites/physbeam" )
CLIENTEFFECT_MATERIAL( "sprites/physglow" )
CLIENTEFFECT_REGISTER_END()

#endif

IPhysicsObject *GetPhysObjFromPhysicsBone( CBaseEntity *pEntity, short physicsbone )
{
	if( pEntity->IsNPC() )
	{
		return pEntity->VPhysicsGetObject();
	}

	CBaseAnimating *pModel = static_cast< CBaseAnimating * >( pEntity );
	if ( pModel != NULL )
	{
		IPhysicsObject	*pPhysicsObject = NULL;

		//Find the real object we hit.
		if( physicsbone >= 0 )
		{
#ifdef CLIENT_DLL
			if ( pModel->m_pRagdoll )
			{
				CRagdoll *pCRagdoll = dynamic_cast < CRagdoll * > ( pModel->m_pRagdoll );
#else
				// Affect the object
				CRagdollProp *pCRagdoll = dynamic_cast<CRagdollProp*>( pEntity );
#endif
				if ( pCRagdoll )
				{
					ragdoll_t *pRagdollT = pCRagdoll->GetRagdoll();

					if ( physicsbone < pRagdollT->listCount )
					{
						pPhysicsObject = pRagdollT->list[physicsbone].pObject;
					}
					return pPhysicsObject;
				}
#ifdef CLIENT_DLL
			}
#endif
		}
	}

	return pEntity->VPhysicsGetObject();
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

	void SetTargetPosition( const Vector &target, const QAngle &targetOrientation )
	{
		m_shadow.targetPosition = target;
		m_shadow.targetRotation = targetOrientation;

		m_timeToArrive = gpGlobals->frametime;

		CBaseEntity *pAttached = m_attachedEntity;
		if ( pAttached )
		{
			IPhysicsObject *pObj = pAttached->VPhysicsGetObject();

			if ( pObj != NULL )
			{
				pObj->Wake();
			}
			else
			{
				DetachEntity();
			}
		}
	}
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

void CGravControllerPoint::AttachEntity( CBasePlayer *pPlayer, CBaseEntity *pEntity, IPhysicsObject *pPhys, short physicsbone, const Vector &vGrabPosition )
{
	m_attachedEntity = pEntity;
	m_attachedPhysicsBone = physicsbone;
	pPhys->WorldToLocal( &m_localPosition, vGrabPosition );
	m_worldPosition = vGrabPosition;
	pPhys->GetDamping( NULL, &m_saveDamping );
	m_saveMass = pPhys->GetMass();
	float damping = 2;
	pPhys->SetDamping( NULL, &damping );
	pPhys->SetMass( 50000 );
	m_controller = physenv->CreateMotionController( this );
	m_controller->AttachObject( pPhys, true );
	Vector position;
	QAngle angles;
	pPhys->GetPosition( &position, &angles );
	SetTargetPosition( vGrabPosition, angles );
	m_targetRotation = TransformAnglesToPlayerSpace( angles, pPlayer );
}

void CGravControllerPoint::DetachEntity( void )
{
	CBaseEntity *pEntity = m_attachedEntity;
	if ( pEntity )
	{
		IPhysicsObject *pPhys = GetPhysObjFromPhysicsBone( pEntity, m_attachedPhysicsBone );
		if ( pPhys )
		{
			// on the odd chance that it's gone to sleep while under anti-gravity
			pPhys->Wake();
			pPhys->SetDamping( NULL, &m_saveDamping );
			pPhys->SetMass( m_saveMass );
		}
	}
	m_attachedEntity = NULL;
	m_attachedPhysicsBone = 0;
	if ( physenv )
	{
		physenv->DestroyMotionController( m_controller );
	}
	m_controller = NULL;

	// UNDONE: Does this help the networking?
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
	hlshadowcontrol_params_t shadowParams = m_shadow;
#ifndef CLIENT_DLL
	m_timeToArrive = pObject->ComputeShadowControl( shadowParams, m_timeToArrive, deltaTime );
#else
	m_timeToArrive = pObject->ComputeShadowControl( shadowParams, (TICK_INTERVAL*2), deltaTime );
#endif

	linear.Init();
	angular.Init();

	return SIM_LOCAL_ACCELERATION;
}

#ifdef CLIENT_DLL
#define CWeaponGravityGun C_WeaponGravityGun
#endif

class CWeaponGravityGun : public CBaseHLCombatWeapon
{
	DECLARE_DATADESC();

public:
	DECLARE_CLASS( CWeaponGravityGun, CBaseHLCombatWeapon );

	DECLARE_NETWORKCLASS();
	DECLARE_PREDICTABLE();

	CWeaponGravityGun();

#ifdef CLIENT_DLL
	void GetRenderBounds( Vector& mins, Vector& maxs )
	{
		BaseClass::GetRenderBounds( mins, maxs );

		// add to the bounds, don't clear them.
		// ClearBounds( mins, maxs );
		AddPointToBounds( vec3_origin, mins, maxs );
		AddPointToBounds( m_targetPosition, mins, maxs );
		AddPointToBounds( m_worldPosition, mins, maxs );
		CBaseEntity *pEntity = GetBeamEntity();
		if ( pEntity )
		{
			mins -= pEntity->GetRenderOrigin();
			maxs -= pEntity->GetRenderOrigin();
		}
	}

	void GetRenderBoundsWorldspace( Vector& mins, Vector& maxs )
	{
		BaseClass::GetRenderBoundsWorldspace( mins, maxs );

		// add to the bounds, don't clear them.
		// ClearBounds( mins, maxs );
		AddPointToBounds( vec3_origin, mins, maxs );
		AddPointToBounds( m_targetPosition, mins, maxs );
		AddPointToBounds( m_worldPosition, mins, maxs );
		mins -= GetRenderOrigin();
		maxs -= GetRenderOrigin();
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

	int	 DrawModel( int flags );
	void ViewModelDrawn( C_BaseViewModel *pBaseViewModel );
	bool IsTransparent( void );

	// We need to render opaque and translucent pieces
	RenderGroup_t	GetRenderGroup( void ) {	return RENDER_GROUP_TWOPASS;	}
#endif

	void Spawn( void );
	void OnRestore( void );
	void Precache( void );

	virtual void	UpdateOnRemove(void);
	void PrimaryAttack( void );
	void SecondaryAttack( void );
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

	CBaseEntity *GetBeamEntity();

	// HL2SB GMod compat: client helpers for the glow shell / mouse-rotate /
	// wheel-distance intercepts
	bool	IsHolding( void ) const { return m_hObject != NULL; }
	CBaseEntity *GetHeldEntity( void ) const { return m_hObject; }
	void	HL2SB_AdjustDistance( float flDelta )
	{
		m_distance = clamp( m_distance + flDelta, 40.0f, 1024.0f );
	}

private:
	CNetworkVar( int, m_active );
	bool		m_useDown;
	CNetworkHandle( CBaseEntity, m_hObject );
	CNetworkVar( int, m_physicsBone );
	float		m_distance;
	float		m_movementLength;
	int			m_soundState;
	Vector		m_originalObjectPosition;
	CNetworkVector	( m_targetPosition );
	CNetworkVector	( m_worldPosition );

#ifndef CLIENT_DLL
	// HL2SB GMod compat: the held prop's soft full-body light is a CLIENT
	// dlight (see EffectUpdate) - no server-side entity needed.
	float		m_flLastReloadPress;	// for the double-tap-R unfreeze-all
	bool		m_bDraggingNPC;			// holding a LIVE npc (teleport-drag, no ragdoll)
	QAngle		m_heldWorldAngles;		// GMod style: the held object keeps its WORLD orientation
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

IMPLEMENT_NETWORKCLASS_ALIASED( WeaponGravityGun, DT_WeaponGravityGun )

BEGIN_NETWORK_TABLE( CWeaponGravityGun, DT_WeaponGravityGun )
#ifdef CLIENT_DLL
	RecvPropEHandle( RECVINFO( m_hObject ) ),
	RecvPropInt( RECVINFO( m_physicsBone ) ),
	RecvPropVector( RECVINFO( m_targetPosition ) ),
	RecvPropVector( RECVINFO( m_worldPosition ) ),
	RecvPropInt( RECVINFO(m_active) ),
#else
	SendPropEHandle( SENDINFO( m_hObject ) ),
	SendPropInt( SENDINFO( m_physicsBone ) ),
	SendPropVector(SENDINFO( m_targetPosition ), -1, SPROP_COORD),
	SendPropVector(SENDINFO( m_worldPosition ), -1, SPROP_COORD),
	SendPropInt( SENDINFO(m_active), 1, SPROP_UNSIGNED ),
#endif
END_NETWORK_TABLE()

#ifdef CLIENT_DLL
BEGIN_PREDICTION_DATA( CWeaponGravityGun )
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
#ifndef CLIENT_DLL
	m_flLastReloadPress = 0.0f;
	m_bDraggingNPC = false;
	m_heldWorldAngles = vec3_angle;
#endif
}

//-----------------------------------------------------------------------------
// On Remove
//-----------------------------------------------------------------------------
void CWeaponGravityGun::UpdateOnRemove(void)
{
	EffectDestroy();
	SoundDestroy();
	BaseClass::UpdateOnRemove();
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
// adnan
// want to add an angles modifier key
bool CGravControllerPoint::UpdateObject( CBasePlayer *pPlayer, CBaseEntity *pEntity )
{
	IPhysicsObject *pPhysics = GetPhysObjFromPhysicsBone( pEntity, m_attachedPhysicsBone );
	if ( !pEntity || !pPhysics )
	{
		return false;
	}
	SetTargetPosition( m_targetPosition, m_targetRotation );

	return true;
}

//=========================================================
//=========================================================
void CWeaponGravityGun::Spawn( )
{
	BaseClass::Spawn();
//	SetModel( GetWorldModel() );

	// The physgun uses a different skin
	//m_nSkin = PHYSGUN_SKIN;

#ifndef CLIENT_DLL
	FallInit();
#endif
}

void CWeaponGravityGun::OnRestore( void )
{
	BaseClass::OnRestore();

	if ( m_gravCallback.m_controller )
	{
		m_gravCallback.m_controller->SetEventHandler( &m_gravCallback );
	}
}

//=========================================================
//=========================================================
void CWeaponGravityGun::Precache( void )
{
	BaseClass::Precache();

	g_physgunBeam1 = PrecacheModel(PHYSGUN_BEAM_SPRITE1);
	g_physgunBeam = PrecacheModel(PHYSGUN_BEAM_SPRITE);
	g_physgunGlow = PrecacheModel(PHYSGUN_BEAM_GLOW);

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

	Vector start, forward, right;
	pOwner->EyeVectors( &forward, &right, NULL );

	start = pOwner->Weapon_ShootPosition();
	Vector end = start + forward * 4096;

	// UTIL_TraceLine( start, end, MASK_SHOT, pOwner, COLLISION_GROUP_NONE, ptr );
	UTIL_TraceLine( start, end, MASK_SHOT|CONTENTS_GRATE, pOwner, COLLISION_GROUP_NONE, ptr );
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
	float distance = tr.fraction * 4096;

	if ( m_hObject == NULL && tr.DidHitNonWorldEntity() )
	{
		CBaseEntity *pEntity = tr.m_pEnt;
		AttachObject( pEntity, GetPhysObjFromPhysicsBone( pEntity, tr.physicsbone ), tr.physicsbone, start, tr.endpos, distance );
	}

	CBaseEntity *pObject = m_hObject;

	// HL2SB GMod compat: the held object is wrapped in a soft full-body LIGHT
	// (the video's "浅光"), not a sprite decal - a dlight at its centre lights
	// every surface facing it.  Keyed on the entity so it never stacks.
#ifdef CLIENT_DLL
	if ( pObject != NULL )
	{
		dlight_t *dl = effects->CL_AllocDlight( pObject->entindex() );
		if ( dl != NULL )
		{
			dl->origin = pObject->WorldSpaceCenter();
			dl->color.r = 140;
			dl->color.g = 200;
			dl->color.b = 255;
			dl->radius = MAX( 140.0f, pObject->BoundingRadius() * 2.5f );
			dl->die = gpGlobals->curtime + 0.05f;
		}
	}
#endif

	// HL2SB GMod compat: RMB freezes the held object in place AND releases it
	// (GMod's physgun behaviour), instead of the old freeze-while-held toggle.
	if ( pObject && ( pOwner->m_afButtonPressed & IN_ATTACK2 ) )
	{
#ifndef CLIENT_DLL
		if ( m_bDraggingNPC )
		{
			// a dragged NPC has no physics body to freeze - RMB just drops it
			DetachObject();
			EffectDestroy();
			SoundDestroy();
			return;
		}

		IPhysicsObject *pPhys = GetPhysObjFromPhysicsBone( pObject, m_physicsBone );
		if ( pPhys != NULL )
		{
			pPhys->EnableMotion( false );

			// HL2SB GMod compat: GM:PhysgunDrop( ply, ent ) -- the freeze
			// releases the object, so the drop hook fires here too.
			DetachObject();
		}
#endif

		EffectDestroy();
		SoundDestroy();
		return;
	}

	if ( pObject )
	{
#ifndef CLIENT_DLL
		// HL2SB GMod compat: LIVE NPC drag - drive the position every tick,
		// the NPC stays alive and keeps playing its animations (it slides
		// exactly like the video's GMan).
		if ( m_bDraggingNPC )
		{
			Vector npcTarget = start + forward * m_distance;
			pObject->Teleport( &npcTarget, NULL, NULL );
			pObject->SetAbsVelocity( vec3_origin );
			m_movementLength = ( npcTarget - pObject->GetLocalOrigin() ).Length();
			return;
		}

		// HL2SB GMod compat: E+mouse rotates - the mouse deltas arrive with the
		// user command (the client freezes the VIEW while E is held, see
		// HL2SB_PhysgunMouseRotate in in_mouse.cpp), so the object rotates
		// without the camera swinging along.
		if ( pOwner->m_nButtons & IN_USE )
		{
			int nMouseDx = 0, nMouseDy = 0;
			const CUserCmd *pCmd = pOwner->GetCurrentUserCommand();
			if ( pCmd != NULL )
			{
				nMouseDx = pCmd->mousedx;
				nMouseDy = pCmd->mousedy;
			}

			if ( nMouseDx != 0 )
				m_heldWorldAngles.y -= nMouseDx * 0.4f;
			if ( nMouseDy != 0 )
				m_heldWorldAngles.x += nMouseDy * 0.4f;
		}

		QAngle angles = m_heldWorldAngles;
#else
		QAngle angles = m_gravCallback.TransformAnglesFromPlayerSpace( m_gravCallback.m_targetRotation, pOwner );
#endif

		if ( ( pOwner->m_nButtons & IN_USE ) && ( pOwner->m_nButtons & IN_FORWARD ) )
		{
#ifndef CLIENT_DLL
			pOwner->SetPhysicsFlag( PFLAG_DIROVERRIDE, true );
#endif
			m_distance = Approach( 1024, m_distance, m_distance * 0.1 );
		}

		if ( ( pOwner->m_nButtons & IN_USE ) && ( pOwner->m_nButtons & IN_BACK ) )
		{
#ifndef CLIENT_DLL
			pOwner->SetPhysicsFlag( PFLAG_DIROVERRIDE, true );
#endif
			m_distance = Approach( 40, m_distance, m_distance * 0.1 );
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
			Vector offset;
			pPhys->LocalToWorld( &offset, m_worldPosition );
			Vector vecOrigin;
			pPhys->GetPosition( &vecOrigin, NULL );
			m_gravCallback.SetTargetPosition( newPosition + (vecOrigin - offset), angles );
			Vector dir = (newPosition - pObject->GetLocalOrigin());
			m_movementLength = dir.Length();
		}
	}
	else
	{
		m_targetPosition = end;
		//m_gravCallback.SetTargetPosition( end, m_gravCallback.m_targetRotation );
	}
}

void CWeaponGravityGun::SoundCreate( void )
{
	m_soundState = SS_SCANNING;
	SoundStart();
}

void CWeaponGravityGun::SoundDestroy( void )
{
	SoundStop();
}

void CWeaponGravityGun::SoundStop( void )
{
	CBasePlayer *pOwner = ToBasePlayer( GetOwner() );
	if ( !pOwner )
		return;

	switch( m_soundState )
	{
	case SS_SCANNING:
		(CSoundEnvelopeController::GetController()).SoundDestroy( m_sndMotor );
		m_sndMotor = NULL;
		break;
	case SS_LOCKEDON:
		(CSoundEnvelopeController::GetController()).SoundDestroy( m_sndMotor );
		m_sndMotor = NULL;
		(CSoundEnvelopeController::GetController()).SoundDestroy( m_sndLockedOn );
		m_sndLockedOn = NULL;
		(CSoundEnvelopeController::GetController()).SoundDestroy( m_sndLightObject );
		m_sndLightObject = NULL;
		(CSoundEnvelopeController::GetController()).SoundDestroy( m_sndHeavyObject );
		m_sndHeavyObject = NULL;
		break;
	}
}

//-----------------------------------------------------------------------------
// Purpose: returns the linear fraction of value between low & high (0.0 - 1.0) * scale
//			e.g. UTIL_LineFraction( 1.5, 1, 2, 1 ); will return 0.5 since 1.5 is
//			halfway between 1 and 2
// Input  : value - a value between low & high (clamped)
//			low - the value that maps to zero
//			high - the value that maps to "scale"
//			scale - the output scale
// Output : parametric fraction between low & high
//-----------------------------------------------------------------------------
static float UTIL_LineFraction( float value, float low, float high, float scale )
{
	if ( value < low )
		value = low;
	if ( value > high )
		value = high;

	float delta = high - low;
	if ( delta == 0 )
		return 0;

	return scale * (value-low) / delta;
}

void CWeaponGravityGun::SoundStart( void )
{
	CPASAttenuationFilter filter( this );

	switch( m_soundState )
	{
	case SS_SCANNING:
		{
			m_sndMotor = (CSoundEnvelopeController::GetController()).SoundCreate( filter, entindex(), CHAN_STATIC, "Weapon_Physgun.Scanning", ATTN_NORM );
			(CSoundEnvelopeController::GetController()).Play( m_sndMotor, 1.0f, 100 );
		}
		break;
	case SS_LOCKEDON:
		{
			m_sndLockedOn = (CSoundEnvelopeController::GetController()).SoundCreate( filter, entindex(), CHAN_STATIC, "Weapon_Physgun.LockedOn", ATTN_NORM );
			(CSoundEnvelopeController::GetController()).Play( m_sndLockedOn, 1.0f, 100 );
			m_sndMotor = (CSoundEnvelopeController::GetController()).SoundCreate( filter, entindex(), CHAN_STATIC, "Weapon_Physgun.Scanning", ATTN_NORM );
			(CSoundEnvelopeController::GetController()).Play( m_sndMotor, 1.0f, 100 );
			m_sndLightObject = (CSoundEnvelopeController::GetController()).SoundCreate( filter, entindex(), CHAN_STATIC, "Weapon_Physgun.LightObject", ATTN_NORM );
			(CSoundEnvelopeController::GetController()).Play( m_sndLightObject, 1.0f, 100 );
			m_sndHeavyObject = (CSoundEnvelopeController::GetController()).SoundCreate( filter, entindex(), CHAN_STATIC, "Weapon_Physgun.HeavyObject", ATTN_NORM );
			(CSoundEnvelopeController::GetController()).Play( m_sndHeavyObject, 1.0f, 100 );
		}
		break;
	}
													//   volume, att, flags, pitch
}

void CWeaponGravityGun::SoundUpdate( void )
{
	int newState;

	if ( m_hObject )
		newState = SS_LOCKEDON;
	else
		newState = SS_SCANNING;

	if ( newState != m_soundState )
	{
		SoundStop();
		m_soundState = newState;
		SoundStart();
	}

	switch( m_soundState )
	{
	case SS_SCANNING:
		break;
	case SS_LOCKEDON:
		{
			CPASAttenuationFilter filter( this );

			float height = m_hObject->GetAbsOrigin().z - m_originalObjectPosition.z;

			// go from pitch 90 to 150 over a height of 500
			int pitch = 90 + (int)UTIL_LineFraction( height, 0, 500, 60 );

			assert(m_sndLockedOn!=NULL);
			if ( m_sndLockedOn != NULL )
			{
				(CSoundEnvelopeController::GetController()).SoundChangePitch( m_sndLockedOn, pitch, 0.0f );
			}

			// attenutate the movement sounds over 200 units of movement
			float distance = UTIL_LineFraction( m_movementLength, 0, 200, 1.0 );

			// blend the "mass" sounds between 50 and 500 kg
			IPhysicsObject *pPhys = GetPhysObjFromPhysicsBone( m_hObject, m_physicsBone );
			if ( pPhys == NULL )
			{
				// we no longer exist!
				break;
			}

			float fade = UTIL_LineFraction( pPhys->GetMass(), 50, 500, 1.0 );

			(CSoundEnvelopeController::GetController()).SoundChangeVolume( m_sndLightObject, fade * distance, 0.0f );

			(CSoundEnvelopeController::GetController()).SoundChangeVolume( m_sndHeavyObject, (1.0 - fade) * distance, 0.0f );
		}
		break;
	}
}

CBaseEntity *CWeaponGravityGun::GetBeamEntity()
{
	CBasePlayer *pOwner = ToBasePlayer( GetOwner() );
	if ( !pOwner )
		return NULL;

	// Make sure I've got a view model
	CBaseViewModel *vm = pOwner->GetViewModel();
	if ( vm )
		return vm;

	return pOwner;
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

void CWeaponGravityGun::UpdateObject( void )
{
	CBasePlayer *pPlayer = ToBasePlayer( GetOwner() );
	Assert( pPlayer );

	CBaseEntity *pObject = m_hObject;
	if ( !pObject )
		return;

	if ( !m_gravCallback.UpdateObject( pPlayer, pObject ) )
	{
		DetachObject();
		return;
	}
}

void CWeaponGravityGun::DetachObject( void )
{
	if ( m_hObject )
	{
#ifndef CLIENT_DLL
		CBasePlayer *pOwner = ToBasePlayer( GetOwner() );

		// HL2SB GMod compat: GM:PhysgunDrop( ply, ent )
		if ( L != NULL && pOwner != NULL )
		{
			BEGIN_LUA_CALL_HOOK( "PhysgunDrop" );
				lua_pushplayer( L, pOwner );
				lua_pushentity( L, m_hObject );
			END_LUA_CALL_HOOK( 2, 0 );
		}

		Pickup_OnPhysGunDrop( m_hObject, pOwner, DROPPED_BY_CANNON );

		m_bDraggingNPC = false;
#endif

		IPhysicsObject *pList[VPHYSICS_MAX_OBJECT_LIST_COUNT];
		int count = m_hObject->VPhysicsGetObjectList( pList, ARRAYSIZE(pList) );
		for ( int i = 0; i < count; i++ )
		{
			PhysClearGameFlags( pList[i], FVPHYSICS_PLAYER_HELD );
		}
		m_gravCallback.DetachEntity();
		m_hObject = NULL;
		m_physicsBone = 0;
	}
}

void CWeaponGravityGun::AttachObject( CBaseEntity *pObject, IPhysicsObject *pPhysics, short physicsbone, const Vector& start, const Vector &end, float distance )
{
	CBasePlayer *pOwner = ToBasePlayer( GetOwner() );
	if( !pOwner )
		return;
	m_hObject = pObject;
	m_physicsBone = physicsbone;
	m_useDown = false;

#ifndef CLIENT_DLL
	// HL2SB GMod compat: players are never grabbable, and the gamemode may
	// veto the pickup through GM:PhysgunPickup( ply, ent ) - the literal false
	// vetoes, anything else keeps the physgun's own rules.
	if ( pObject->IsPlayer() )
	{
		m_hObject = NULL;
		return;
	}

	bool bPhysgunPickupAllowed = true;
	if ( L != NULL )
	{
		BEGIN_LUA_CALL_HOOK( "PhysgunPickup" );
			lua_pushplayer( L, pOwner );
			lua_pushentity( L, pObject );
		END_LUA_CALL_HOOK( 2, 1 );

		if ( lua_gettop( L ) > 0 )
		{
			if ( lua_isboolean( L, -1 ) && lua_toboolean( L, -1 ) == 0 )
				bPhysgunPickupAllowed = false;
			lua_pop( L, 1 );
		}
	}

	if ( !bPhysgunPickupAllowed )
	{
		m_hObject = NULL;
		return;
	}

	// HL2SB GMod compat: LIVE NPCs are DRAGGED ALIVE (the video's GMan slides
	// on his feet while held).  A live NPC moves by its locomotion controller,
	// so there is no rigid body for the grab controller - the NPC gets its own
	// drag mode (per-tick position drive, see EffectUpdate) and stays alive.
	m_bDraggingNPC = pObject->IsNPC();

	// GMod style: the held object keeps its WORLD orientation, E+mouse rotates
	m_heldWorldAngles = pObject->GetAbsAngles();
#endif

#ifndef CLIENT_DLL
	if ( m_bDraggingNPC )
	{
		// NPC drag: no controller, no bone bookkeeping - just hold the handle
		Pickup_OnPhysGunPickup( pObject, pOwner );

		static int s_nNpcGrabDiag = 0;
		if ( s_nNpcGrabDiag < 15 )
		{
			++s_nNpcGrabDiag;
			luasrc_LuaInfoMsgF( "[HL2SB physgun] NPC drag attached '%s' (alive)\n", pObject->GetClassname() );
		}

		m_distance = distance;
		return;
	}
#endif

	if ( pPhysics && pObject->GetMoveType() == MOVETYPE_VPHYSICS )
	{
		m_distance = distance;

		// GMod: grabbing a frozen object drags it (and unfreezes it while
		// held; RMB re-freezes on release-in-place)
		if ( !pPhysics->IsMoveable() )
			pPhysics->EnableMotion( true );

		Vector worldPosition;
		pPhysics->WorldToLocal( &worldPosition, end );
		m_worldPosition = worldPosition;
		Vector vecOrigin;
		pPhysics->GetPosition( &vecOrigin, NULL );
		m_gravCallback.AttachEntity( pOwner, pObject, pPhysics, physicsbone, vecOrigin );

		m_originalObjectPosition = vecOrigin;

		pPhysics->Wake();
		IPhysicsObject *pList[VPHYSICS_MAX_OBJECT_LIST_COUNT];
		int count = pObject->VPhysicsGetObjectList( pList, ARRAYSIZE(pList) );
		for ( int i = 0; i < count; i++ )
		{
			PhysSetGameFlags( pList[i], FVPHYSICS_PLAYER_HELD );
		}

#ifndef CLIENT_DLL
		Pickup_OnPhysGunPickup( pObject, pOwner );
#endif
	}
	else
	{
		m_hObject = NULL;
		m_physicsBone = 0;
	}
}

//=========================================================
//=========================================================
void CWeaponGravityGun::PrimaryAttack( void )
{
	if ( !m_active )
	{
		SendWeaponAnim( ACT_VM_PRIMARYATTACK );
		EffectCreate();
		SoundCreate();
	}
	else
	{
		EffectUpdate();
		SoundUpdate();

#ifndef CLIENT_DLL
		// HL2SB GMod compat: while attack is HELD the one-shot grab animation
		// used to finish and leave the viewmodel frozen on its last frame -
		// the arms off-screen (the "arms vanish on held attack" report).  Re-arm: the
		// hold-idle (@hold_idle = ACT_VM_RELOAD) while carrying something,
		// plain idle while scanning.
		if ( IsViewModelSequenceFinished() )
			SendWeaponAnim( m_hObject != NULL ? ACT_VM_RELOAD : ACT_VM_IDLE );
#endif
	}
}

void CWeaponGravityGun::SecondaryAttack( void )
{
	return;
}

#ifdef CLIENT_DLL
extern bool g_bRenderingReflection; // HL2SB: mirror reflection flag in viewrender.cpp

//-----------------------------------------------------------------------------
// Purpose: Third-person function call to render world model
//-----------------------------------------------------------------------------
int CWeaponGravityGun::DrawModel( int flags )
{
	// Only render these on the transparent pass
	if ( flags & STUDIO_TRANSPARENCY )
	{
		if ( !m_active )
			return 0;

		C_BasePlayer *pOwner = ToBasePlayer( GetOwner() );

		if ( !pOwner )
			return 0;

		// HL2SB: the local player's world weapon stays in the render lists for
		// mirror reflections, so skip the first-person copy of the beam/glow;
		// ViewModelDrawn draws it at the viewmodel attachment.
		if ( IsCarriedByLocalPlayer() && !g_bRenderingReflection && ShouldDrawUsingViewModel() )
			return 0;

		Vector points[3];
		QAngle tmpAngle;

		C_BaseEntity *pObject = m_hObject;
		//if ( pObject == NULL )
		//	return 0;

		GetAttachment( 1, points[0], tmpAngle );

		// a little noise 11t & 13t should be somewhat non-periodic looking
		//points[1].z += 4*sin( gpGlobals->curtime*11 ) + 5*cos( gpGlobals->curtime*13 );
		if ( pObject == NULL )
		{
			//points[2] = m_targetPosition;
			trace_t tr;
			TraceLine( &tr );
			points[2] = tr.endpos;
		}
		else
		{
			pObject->EntityToWorldSpace( m_worldPosition, &points[2] );
		}

		Vector forward, right, up;
		QAngle playerAngles = pOwner->EyeAngles();
		AngleVectors( playerAngles, &forward, &right, &up );
		if ( pObject == NULL )
		{
			Vector vecDir = points[2] - points[0];
			VectorNormalize( vecDir );
			points[1] = points[0] + 0.5f * (vecDir * points[2].DistTo(points[0]));
		}
		else
		{
			Vector vecSrc = pOwner->Weapon_ShootPosition( );
			points[1] = vecSrc + 0.5f * (forward * points[2].DistTo(points[0]));
		}

		IMaterial *pMat = materials->FindMaterial( "sprites/physbeam1", TEXTURE_GROUP_CLIENT_EFFECTS );
		if ( pObject )
			pMat = materials->FindMaterial( "sprites/physbeam", TEXTURE_GROUP_CLIENT_EFFECTS );
		Vector color;
		color.Init(1,1,1);

		float scrollOffset = gpGlobals->curtime - (int)gpGlobals->curtime;
		CMatRenderContextPtr pRenderContext( materials );
		pRenderContext->Bind( pMat );
		DrawBeamQuadratic( points[0], points[1], points[2], pObject ? 13/3.0f : 13/5.0f, color, scrollOffset );
		DrawBeamQuadratic( points[0], points[1], points[2], pObject ? 13/3.0f : 13/5.0f, color, -scrollOffset );

		IMaterial *pMaterial = materials->FindMaterial( "sprites/physglow", TEXTURE_GROUP_CLIENT_EFFECTS );

		color32 clr={0,64,255,255};
		if ( pObject )
		{
			clr.r = 186;
			clr.g = 253;
			clr.b = 247;
			clr.a = 255;
		}

		float scale = random->RandomFloat( 3, 5 ) * ( pObject ? 3 : 2 );

		// Draw the sprite
		pRenderContext->Bind( pMaterial );
		for ( int i = 0; i < 3; i++ )
		{
			DrawSprite( points[2], scale, scale, clr );
		}
		return 1;
	}

	// Only do this on the opaque pass
	return BaseClass::DrawModel( flags );
}

//-----------------------------------------------------------------------------
// Purpose: First-person function call after viewmodel has been drawn
//-----------------------------------------------------------------------------
void CWeaponGravityGun::ViewModelDrawn( C_BaseViewModel *pBaseViewModel )
{
	if ( !m_active )
		return;

	// Render our effects
	C_BasePlayer *pOwner = ToBasePlayer( GetOwner() );

	if ( !pOwner )
		return;

	Vector points[3];
	QAngle tmpAngle;

	C_BaseEntity *pObject = m_hObject;
	//if ( pObject == NULL )
	//	return;

	pBaseViewModel->GetAttachment( 1, points[0], tmpAngle );

	// a little noise 11t & 13t should be somewhat non-periodic looking
	//points[1].z += 4*sin( gpGlobals->curtime*11 ) + 5*cos( gpGlobals->curtime*13 );
	if ( pObject == NULL )
	{
		//points[2] = m_targetPosition;
		trace_t tr;
		TraceLine( &tr );
		points[2] = tr.endpos;
	}
	else
	{
		pObject->EntityToWorldSpace(m_worldPosition, &points[2]);
	}

	Vector forward, right, up;
	QAngle playerAngles = pOwner->EyeAngles();
	AngleVectors( playerAngles, &forward, &right, &up );
	Vector vecSrc = pOwner->Weapon_ShootPosition( );
	points[1] = vecSrc + 0.5f * (forward * points[2].DistTo(points[0]));

	IMaterial *pMat = materials->FindMaterial( "sprites/physbeam1", TEXTURE_GROUP_CLIENT_EFFECTS );
	if ( pObject )
		pMat = materials->FindMaterial( "sprites/physbeam", TEXTURE_GROUP_CLIENT_EFFECTS );
	Vector color;
	color.Init(1,1,1);

	// Now draw it.
	CViewSetup beamView = *view->GetPlayerViewSetup();

	Frustum dummyFrustum;
	render->Push3DView( beamView, 0, NULL, dummyFrustum );

	float scrollOffset = gpGlobals->curtime - (int)gpGlobals->curtime;
	CMatRenderContextPtr pRenderContext( materials );
	pRenderContext->Bind( pMat );
#if 1
	// HACK HACK:  Munge the depth range to prevent view model from poking into walls, etc.
	// Force clipped down range
	pRenderContext->DepthRange( 0.1f, 0.2f );
#endif
	DrawBeamQuadratic( points[0], points[1], points[2], pObject ? 13/3.0f : 13/5.0f, color, scrollOffset );
	DrawBeamQuadratic( points[0], points[1], points[2], pObject ? 13/3.0f : 13/5.0f, color, -scrollOffset );

	IMaterial *pMaterial = materials->FindMaterial( "sprites/physglow", TEXTURE_GROUP_CLIENT_EFFECTS );

	color32 clr={0,64,255,255};
	if ( pObject )
	{
		clr.r = 186;
		clr.g = 253;
		clr.b = 247;
		clr.a = 255;
	}

	float scale = random->RandomFloat( 3, 5 ) * ( pObject ? 3 : 2 );

	// Draw the sprite
	pRenderContext->Bind( pMaterial );
	for ( int i = 0; i < 3; i++ )
	{
		DrawSprite( points[2], scale, scale, clr );
	}
#if 1
	pRenderContext->DepthRange( 0.0f, 1.0f );
#endif

	render->PopView( dummyFrustum );

	// Pass this back up
	BaseClass::ViewModelDrawn( pBaseViewModel );
}

//-----------------------------------------------------------------------------
// Purpose: We are always considered transparent
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
		if ( m_active )
		{
			EffectDestroy();
			SoundDestroy();
		}
		WeaponIdle( );
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

//=========================================================
//=========================================================
#ifndef CLIENT_DLL
CON_COMMAND( hl2sb_physgun_push, "Physgun: push the held object away (mouse wheel)" )
{
	CBasePlayer *pPlayer = UTIL_GetCommandClient();
	if ( pPlayer == NULL )
		return;

	CWeaponGravityGun *pGun = dynamic_cast< CWeaponGravityGun * >( pPlayer->GetActiveWeapon() );
	if ( pGun != NULL && pGun->IsHolding() )
		pGun->HL2SB_AdjustDistance( 45.0f );
}

CON_COMMAND( hl2sb_physgun_pull, "Physgun: pull the held object closer (mouse wheel)" )
{
	CBasePlayer *pPlayer = UTIL_GetCommandClient();
	if ( pPlayer == NULL )
		return;

	CWeaponGravityGun *pGun = dynamic_cast< CWeaponGravityGun * >( pPlayer->GetActiveWeapon() );
	if ( pGun != NULL && pGun->IsHolding() )
		pGun->HL2SB_AdjustDistance( -45.0f );
}
#endif

bool CWeaponGravityGun::Reload( void )
{
#ifndef CLIENT_DLL
	// HL2SB GMod compat (the on-screen hints promise exactly this):
	//   R while dragging a frozen entity  -> unfreeze it (keep dragging)
	//   R aimed at a frozen entity        -> unfreeze it
	//   double-tap R                      -> unfreeze everything
	CBasePlayer *pOwner = ToBasePlayer( GetOwner() );
	if ( pOwner == NULL )
		return false;

	float flNow = gpGlobals->curtime;
	bool bDoubleTap = ( flNow - m_flLastReloadPress ) < 0.35f;
	m_flLastReloadPress = flNow;

	// 1) the entity currently being dragged: unfreeze and keep holding it
	if ( m_hObject != NULL && !m_bDraggingNPC )
	{
		IPhysicsObject *pPhys = GetPhysObjFromPhysicsBone( m_hObject, m_physicsBone );
		if ( pPhys != NULL && !pPhys->IsMoveable() )
		{
			pPhys->EnableMotion( true );
			pPhys->Wake();
			return true;
		}
	}

	// 2) the entity under the crosshair
	trace_t tr;
	TraceLine( &tr );
	if ( tr.DidHitNonWorldEntity() && tr.m_pEnt != NULL )
	{
		IPhysicsObject *pPhys = tr.m_pEnt->VPhysicsGetObject();
		if ( pPhys != NULL && !pPhys->IsMoveable() )
		{
			pPhys->EnableMotion( true );
			pPhys->Wake();
			return true;
		}
	}

	// 3) double-tap R: unfreeze every physics entity on the map
	if ( bDoubleTap )
	{
		int nUnfrozen = 0;
		CBaseEntity *pEnt = gEntList.FirstEnt();
		for ( ; pEnt != NULL; pEnt = gEntList.NextEnt( pEnt ) )
		{
			if ( pEnt->GetMoveType() != MOVETYPE_VPHYSICS || pEnt->IsWorld() )
				continue;

			IPhysicsObject *pPhys = pEnt->VPhysicsGetObject();
			if ( pPhys != NULL && !pPhys->IsMoveable() )
			{
				pPhys->EnableMotion( true );
				pPhys->Wake();
				++nUnfrozen;
			}
		}

		static int s_nUnfreezeAllDiag = 0;
		if ( s_nUnfreezeAllDiag < 15 )
		{
			++s_nUnfreezeAllDiag;
			luasrc_LuaInfoMsgF( "[HL2SB physgun] double-R unfroze %d objects\n", nUnfrozen );
		}
		return true;
	}
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

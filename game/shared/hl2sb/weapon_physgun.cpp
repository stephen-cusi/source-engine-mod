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
#include "effect_dispatch_data.h"	// HL2SB GMod compat: the RMB-freeze TeslaHitboxes dispatch
#ifndef CLIENT_DLL
#include "te_effect_dispatch.h"		// HL2SB: server-side DispatchEffect( name, data )
#endif
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
Color HL2SB_GetWeaponColor( int iUserID );
#else
#include "physics_prop_ragdoll.h"
#include "props.h"
#include "basehlcombatweapon.h"
#include "ai_basenpc.h"	// HL2SB GMod compat: MyNPCPointer()->CanBecomeRagdoll() in the NPC-grab path
// HL2SB GMod compat: the physgun's held-prop glow sprite + the GMod physgun
// hooks (GM:PhysgunPickup / GM:PhysgunDrop).
#include "Sprite.h"
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

static int g_physgunBeam1;
static int g_physgunBeam;
static int g_physgunGlow;

// HL2SB GMod compat (ConVars In Garrysmod): the physgun's dynamic feel, same
// names GMod exposes.  SERVER side: the shadow controller and the input maths
// run there.  (physgun_halo / physgun_drawbeams are CLIENT -- see lhalo.cpp.)
#ifndef CLIENT_DLL
ConVar physgun_timeToArrive( "physgun_timeToArrive", "0.15", FCVAR_ARCHIVE, "Physgun: seconds for the held object to reach the target" );
ConVar physgun_teleportDistance( "physgun_teleportDistance", "250", FCVAR_ARCHIVE, "Physgun: distance at which the held object teleports instead of sliding" );
ConVar physgun_maxSpeed( "physgun_maxSpeed", "5000", FCVAR_ARCHIVE, "Physgun: maximum linear speed of the held object" );
ConVar physgun_maxAngular( "physgun_maxAngular", "5400", FCVAR_ARCHIVE, "Physgun: maximum angular speed of the held object" );
ConVar physgun_maxSpeedDamping( "physgun_maxSpeedDamping", "10000", FCVAR_ARCHIVE, "Physgun: linear speed where damping kicks in" );
ConVar physgun_maxAngularDamping( "physgun_maxAngularDamping", "10000", FCVAR_ARCHIVE, "Physgun: angular speed where damping kicks in" );
#endif

// HL2SB (2026-09-26): defaults reference from GMod server.dll ():
// E-rotate is mousedx * 0.05 deg per RAW mouse count (the usercmd carries
// unscaled accumulators -- mouse sensitivity never touches cmd->mousedx), the
// wheel moves max(|wheelspeed|,0.1) units per notch, the hold distance clamps
// to [physgun_minrange, physgun_maxrange], E+A/D spins the held object at
// phys_spinspeed deg/s and Shift+E snaps the held angles to gm_snapangles.
// Registered on BOTH realms like GMod does (its client.dll registers the same
// names without reading them -- plugins may query them client-side).
ConVar physgun_rotation_sensitivity( "physgun_rotation_sensitivity", "0.05", FCVAR_ARCHIVE, "Physgun: E+mouse rotation (degrees per mouse count)" );
ConVar physgun_wheelspeed( "physgun_wheelspeed", "10", FCVAR_ARCHIVE, "Physgun: wheel push/pull distance per notch" );
ConVar physgun_minrange( "physgun_minrange", "40", FCVAR_ARCHIVE, "Physgun: minimum hold distance" );
ConVar physgun_maxrange( "physgun_maxrange", "4096", FCVAR_ARCHIVE, "Physgun: maximum hold distance" );
ConVar phys_spinspeed( "phys_spinspeed", "200", FCVAR_ARCHIVE, "Physgun: E+A/D spin speed (degrees per second)" );
ConVar gm_snapangles( "gm_snapangles", "45", FCVAR_ARCHIVE, "Physgun: Shift+E angle snap grid (degrees, 0 disables)" );

#define PHYSGUN_BEAM_SPRITE1	"sprites/physbeam1.vmt"
#define PHYSGUN_BEAM_SPRITE		"sprites/physbeam.vmt"
#define PHYSGUN_BEAM_GLOW		"sprites/physglow.vmt"

// HL2SB (2026-09-26): the HL2 physcannon gun-glow set.  reference-OUT
// (2026-09-26): GMod's physgun does NOT use ANY of these -- GMod's client.dll
// contains no "physcannon_bluecore1/2", "physcannon_blueflare1" or
// "physcannon_bluelight1" string at all.  Those belong to HL2's separate
// C_WeaponPhysCannon (the EP2 mega-cannon / gravity gun), a different weapon.
// The macro block is kept only so the (now disabled) HL2 sprite machine below
// still compiles; nothing renders it.
#define PHYSGUN_FORKGLOW_SPRITE	"sprites/glow04_noz"
#define PHYSGUN_FORKEND_SPRITE	"sprites/physcannon_blueflare1"
#define PHYSGUN_CORE_SPRITE		"sprites/physcannon_bluecore1"
#define PHYSGUN_BLAST_SPRITE	"sprites/physcannon_bluecore2"
#define PHYSGUN_TENDRIL_SPRITE	"sprites/physcannon_bluelight1.vmt"
#define PHYSGUN_TENDRIL_NOZ		"sprites/physcannon_bluelight1b.vmt"
#define PHYSGUN_SPRITE_SCALE	128.0f

// HL2SB (2026-09-26, reference-confirmed): GMod's physgun rendering is exactly
// a beam trail plus an endpoint glow, and the materials are GMod's OWN:
//   * beam          = sprites/physbeam.vmt        (basetexture physbeam_white)
//   * active beam   = sprites/physbeama.vmt       (basetexture physbeam_active_white)
//   * endpoint glow = sprites/physg_glow1.vmt / sprites/physg_glow2.vmt
//                     (basetexture physgun_glow, two additive layers).
// C_PhysBeam::DrawModel (client.dll) builds exactly two engine
// beam trails (physbeam always, physbeama while active) and the two glow
// layers; there is no core/blast/fork/endcap sprite anywhere.  physbeam.vmt
// and physbeama.vmt were MISSING from this fork's content until now -- their
// absence is the long-standing "beam never looked right / no glow" cause.
#define PHYSGUN_BEAM_ACTIVE		"sprites/physbeama.vmt"
#define PHYSGUN_ENDGLOW_SPRITE	"sprites/physg_glow1"
#define PHYSGUN_ENDGLOW_SPRITE2	"sprites/physg_glow2"

#define	PHYSGUN_SKIN	1

// HL2SB (2026-09-26): HL2 physcannon effect/enumeration states (shared --
// both realms drive the machine locally).
enum
{
	EFFECT_NONE = 0,
	EFFECT_CLOSED,
	EFFECT_READY,
	EFFECT_HOLDING,
	EFFECT_LAUNCH,
};

enum
{
	ELEMENT_STATE_NONE = -1,
	ELEMENT_STATE_OPEN = 0,
	ELEMENT_STATE_CLOSED,
};

#ifdef CLIENT_DLL
// Sprite parameter slots (must be in order; NUM_PHYSGUN_EFFECTS = 11)
enum PhysgunEffectType_t
{
	PHYSGUN_CORE = 0,
	PHYSGUN_BLAST,
	PHYSGUN_GLOW1,	// Must be in order!
	PHYSGUN_GLOW2,
	PHYSGUN_GLOW3,
	PHYSGUN_GLOW4,
	PHYSGUN_GLOW5,
	PHYSGUN_GLOW6,
	PHYSGUN_ENDCAP1,	// Must be in order!
	PHYSGUN_ENDCAP2,
	PHYSGUN_ENDCAP3,
	PHYSGUN_NUM_EFFECTS,
};
#define NUM_GLOW_SPRITES	6
#define NUM_ENDCAP_SPRITES	3
#endif

class CWeaponGravityGun;

#ifdef CLIENT_DLL
CLIENTEFFECT_REGISTER_BEGIN( PrecacheEffectGravityGun )
CLIENTEFFECT_MATERIAL( "sprites/physbeam1" )
CLIENTEFFECT_MATERIAL( PHYSGUN_BEAM_SPRITE )
CLIENTEFFECT_MATERIAL( PHYSGUN_BEAM_ACTIVE )
CLIENTEFFECT_MATERIAL( PHYSGUN_BEAM_GLOW )
CLIENTEFFECT_MATERIAL( PHYSGUN_ENDGLOW_SPRITE )
CLIENTEFFECT_MATERIAL( PHYSGUN_ENDGLOW_SPRITE2 )
CLIENTEFFECT_MATERIAL( PHYSGUN_FORKGLOW_SPRITE )
CLIENTEFFECT_MATERIAL( PHYSGUN_FORKEND_SPRITE )
CLIENTEFFECT_MATERIAL( PHYSGUN_CORE_SPRITE )
CLIENTEFFECT_MATERIAL( PHYSGUN_BLAST_SPRITE )
CLIENTEFFECT_MATERIAL( "sprites/physcannon_bluelight1" )
CLIENTEFFECT_MATERIAL( "sprites/physcannon_bluelight1b" )
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
	// HL2SB GMod compat: the controller follows the physgun_* convars every
	// tick, exactly the knobs GMod exposes.
	shadowParams.maxSpeed = physgun_maxSpeed.GetFloat();
	shadowParams.maxAngular = physgun_maxAngular.GetFloat();
	shadowParams.maxDampSpeed = physgun_maxSpeedDamping.GetFloat();
	shadowParams.maxDampAngular = physgun_maxAngularDamping.GetFloat();
	shadowParams.teleportDistance = physgun_teleportDistance.GetFloat();
	m_timeToArrive = pObject->ComputeShadowControl( shadowParams, MAX( m_timeToArrive, physgun_timeToArrive.GetFloat() ), deltaTime );
#else
	m_timeToArrive = pObject->ComputeShadowControl( shadowParams, (TICK_INTERVAL*2), deltaTime );
#endif

	linear.Init();
	angular.Init();

	return SIM_LOCAL_ACCELERATION;
}

#ifdef CLIENT_DLL

//-----------------------------------------------------------------------------
// HL2SB (2026-09-26): the HL2 physcannon effect system (GMod parity) --
// ported from game/shared/hl2mp/weapon_physcannon.cpp.  GMod's physgun IS
// HL2's CWeaponPhysCannon client (reference: same attachments fork1..3m/t/b,
// same DT fields, same HoldSound), so the gun-glow / claw / tendril rendering
// comes straight across, tinted by the player's weapon colour.
//-----------------------------------------------------------------------------

class CPhysgunEffectSprite
{
public:
	CPhysgunEffectSprite( void ) : m_vecColor( 255, 255, 255 ), m_bVisible( true ), m_nAttachment( -1 ) {};

	void SetAttachment( int attachment ) { m_nAttachment = attachment; }
	int	GetAttachment( void ) const { return m_nAttachment; }

	void SetVisible( bool visible = true ) { m_bVisible = visible; }
	bool IsVisible( void ) const { return m_bVisible; }

	void SetColor( const Vector &color ) { m_vecColor = color; }
	const Vector &GetColor( void ) const { return m_vecColor; }

	bool SetMaterial( const char *materialName )
	{
		m_hMaterial.Init( materialName, TEXTURE_GROUP_CLIENT_EFFECTS );
		return ( m_hMaterial != NULL );
	}

	CMaterialReference &GetMaterial( void ) { return m_hMaterial; }

	CInterpolatedValue &GetAlpha( void ) { return m_Alpha; }
	CInterpolatedValue &GetScale( void ) { return m_Scale; }

private:
	CInterpolatedValue	m_Alpha;
	CInterpolatedValue	m_Scale;

	Vector				m_vecColor;
	bool				m_bVisible;
	int					m_nAttachment;
	CMaterialReference	m_hMaterial;
};

class CPhysgunEffectBeam
{
public:
	CPhysgunEffectBeam( void ) : m_pBeam( NULL ) {};

	~CPhysgunEffectBeam( void )
	{
		Release();
	}

	void Release( void )
	{
		if ( m_pBeam != NULL )
		{
			m_pBeam->flags = 0;
			m_pBeam->die = gpGlobals->curtime - 1;

			m_pBeam = NULL;
		}
	}

	void Init( int startAttachment, int endAttachment, CBaseEntity *pEntity, bool firstPerson )
	{
		if ( m_pBeam != NULL )
			return;

		BeamInfo_t beamInfo;

		beamInfo.m_pStartEnt = pEntity;
		beamInfo.m_nStartAttachment = startAttachment;
		beamInfo.m_pEndEnt = pEntity;
		beamInfo.m_nEndAttachment = endAttachment;
		beamInfo.m_nType = TE_BEAMPOINTS;
		beamInfo.m_vecStart = vec3_origin;
		beamInfo.m_vecEnd = vec3_origin;

		beamInfo.m_pszModelName = ( firstPerson ) ? PHYSGUN_TENDRIL_NOZ : PHYSGUN_TENDRIL_SPRITE;

		beamInfo.m_flHaloScale = 0.0f;
		beamInfo.m_flLife = 0.0f;

		if ( firstPerson )
		{
			beamInfo.m_flWidth = 0.0f;
			beamInfo.m_flEndWidth = 4.0f;
		}
		else
		{
			beamInfo.m_flWidth = 0.5f;
			beamInfo.m_flEndWidth = 2.0f;
		}

		beamInfo.m_flFadeLength = 0.0f;
		beamInfo.m_flAmplitude = 16;
		beamInfo.m_flBrightness = 255.0;
		beamInfo.m_flSpeed = 150.0f;
		beamInfo.m_nStartFrame = 0.0;
		beamInfo.m_flFrameRate = 30.0;
		beamInfo.m_flRed = 255.0;
		beamInfo.m_flGreen = 255.0;
		beamInfo.m_flBlue = 255.0;
		beamInfo.m_nSegments = 8;
		beamInfo.m_bRenderable = true;
		beamInfo.m_nFlags = FBEAM_FOREVER;

		m_pBeam = beams->CreateBeamEntPoint( beamInfo );
	}

	// HL2SB: GMod tints every physgun effect with the player's weapon colour.
	void SetColor( float r, float g, float b )
	{
		if ( m_pBeam == NULL )
			return;

		m_pBeam->r = r;
		m_pBeam->g = g;
		m_pBeam->b = b;
	}

	void SetVisible( bool state = true )
	{
		if ( m_pBeam == NULL )
			return;

		m_pBeam->brightness = ( state ) ? 255.0f : 0.0f;
	}

private:
	Beam_t	*m_pBeam;
};

#endif // CLIENT_DLL

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

	// HL2SB (2026-09-26): HL2 physcannon effect system, CLIENT side (GMod parity)
	void				OnDataChanged( DataUpdateType_t type );
	void				ClientThink( void );
	void				StartEffects( void );
	void				DoEffectClosed( void );
	void				DoEffectReady( void );
	void				DoEffectHolding( void );
	void				DoEffectLaunch( Vector *pos );
	void				DoEffectNone( void );
	void				DoEffectIdle( void );
	void				UpdateElementPosition( void );
	void				GetEffectParameters( int effectID, color32 &color, float &scale, IMaterial **pMaterial, Vector &vecAttachment );
	bool				IsEffectVisible( int effectID );
	void				DrawEffectSprite( int effectID );
	void				DrawEffects( void );

	// We need to render opaque and translucent pieces
	RenderGroup_t	GetRenderGroup( void ) {	return RENDER_GROUP_TWOPASS;	}
#endif

	// HL2SB (2026-09-26): HL2 physcannon effect system, SHARED side -- both
	// realms drive the same machine from the locally-known hold state.
	void				DestroyEffects( void );
	void				DoEffect( int effectType, Vector *pos = NULL );
	void				OpenElements( void );
	void				CloseElements( void );

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
		// HL2SB (2026-09-26): GMod clamps the hold distance to
		// [physgun_minrange, physgun_maxrange] (reference: 40..4096)
		// instead of the old hardwired 40..1024.
		m_distance = clamp( m_distance + flDelta,
			physgun_minrange.GetFloat(), physgun_maxrange.GetFloat() );
	}

private:
	CNetworkVar( int, m_active );
	bool		m_useDown;
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

	// HL2SB (2026-09-26): HL2 physcannon effect state -- driven locally on
	// BOTH realms (the shared OpenElements/CloseElements/DoEffect run where
	// the hold state changes), so no new netvars are needed for GMod parity.
	int			m_EffectState;
	int			m_nChangeState;
	float		m_flElementDebounce;
	bool		m_bOpen;

#ifdef CLIENT_DLL
	// Gun-glow sprite set + fork tendrils (HL2 physcannon rendering)
	CInterpolatedValue	m_ElementParameter;
	CPhysgunEffectSprite	m_Parameters[11];	// CORE, BLAST, GLOW1-6, ENDCAP1-3
	CPhysgunEffectBeam		m_Beams[3];
	int			m_nOldEffectState;
	bool		m_bOldOpen;
#endif

#ifndef CLIENT_DLL
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
	m_bFreezeReleaseLatch = false;
	m_bOpen = false;
	m_nChangeState = ELEMENT_STATE_NONE;
	m_flElementDebounce = 0.0f;
	m_EffectState = EFFECT_NONE;
#ifndef CLIENT_DLL
	m_flLastReloadPress = 0.0f;
	m_bDraggingNPC = false;
	m_heldWorldAngles = vec3_angle;
	m_vecGrabOffset = vec3_origin;
#else
	m_nOldEffectState = EFFECT_NONE;
	m_bOldOpen = false;
#endif
}

//-----------------------------------------------------------------------------
// On Remove
//-----------------------------------------------------------------------------
void CWeaponGravityGun::UpdateOnRemove(void)
{
	DestroyEffects();
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

	// HL2SB (2026-09-26, reference-confirmed): precache GMod's own beam
	// materials -- physbeam (idle trail) + physbeama (active/held overlay).
	g_physgunBeam1 = PrecacheModel(PHYSGUN_BEAM_SPRITE1);
	g_physgunBeam = PrecacheModel(PHYSGUN_BEAM_SPRITE);
	g_physgunGlow = PrecacheModel(PHYSGUN_BEAM_ACTIVE);

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

	if ( m_hObject == NULL && !m_bFreezeReleaseLatch && tr.DidHitNonWorldEntity() )
	{
		CBaseEntity *pEntity = tr.m_pEnt;
		AttachObject( pEntity, GetPhysObjFromPhysicsBone( pEntity, tr.physicsbone ), tr.physicsbone, start, tr.endpos, distance );
	}

	CBaseEntity *pObject = m_hObject;

#ifndef CLIENT_DLL
	// HL2SB diag (2026-09-24 physgun input): what buttons ACTUALLY reach the
	// weapon while holding?  1/sec, capped -- pairs with the client-side
	// "CM buttons" dump to bisect client-input vs transport vs weapon code.
	if ( pObject != NULL )
	{
		static int s_nSrvBtnDiag = 0;
		static float s_flNextSrvBtnDiag = 0.0f;
		if ( s_nSrvBtnDiag < 12 && gpGlobals->curtime >= s_flNextSrvBtnDiag )
		{
			s_flNextSrvBtnDiag = gpGlobals->curtime + 1.0f;
			++s_nSrvBtnDiag;
			luasrc_LuaInfoMsgF( "[HL2SB physgun] srv buttons=0x%X obj='%s'\n",
				( unsigned )pOwner->m_nButtons, pObject->GetClassname() );
		}
	}
#endif

	// HL2SB GMod compat: the held object is wrapped in a soft full-body LIGHT
	// (the video's "浅光"), not a sprite decal - a dlight at its centre lights
	// every surface facing it.  Keyed on the entity so it never stacks.
	// 2026-09-24: colour follows the owner's WEAPON colour (was hardcoded
	// blue) -- this also gives NPCs/ragdolls a glow wash, which the material
	// shell cannot touch.
#ifdef CLIENT_DLL
	if ( pObject != NULL )
	{
		Color clrW = HL2SB_GetWeaponColor( pOwner->GetUserID() );
		dlight_t *dl = effects->CL_AllocDlight( pObject->entindex() );
		if ( dl != NULL )
		{
			dl->origin = pObject->WorldSpaceCenter();
			dl->color.r = clrW.r();
			dl->color.g = clrW.g();
			dl->color.b = clrW.b();
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

		// GMod 13 parity (2026-09-24 wiki audit): "the Physgun was updated to
		// exclude freezing vehicles" -- RMB just releases them unfrozen.
		if ( pObject->GetServerVehicle() != NULL )
		{
			DetachObject();
			EffectDestroy();
			SoundDestroy();
			return;
		}

		IPhysicsObject *pPhys = GetPhysObjFromPhysicsBone( pObject, m_physicsBone );

		// HL2SB diagnostic: RMB freeze reached the server? (capped)
		static int s_nFreezeDiag = 0;
		if ( s_nFreezeDiag < 6 )
		{
			++s_nFreezeDiag;
			luasrc_LuaInfoMsgF( "[HL2SB physgun] RMB freeze: obj='%s' phys=%p\n",
				pObject->GetClassname(), (void *)pPhys );
		}

		if ( pPhys != NULL )
		{
			// GM:OnPhysgunFreeze( weapon, physobj, ent, ply ) -- GMod contract:
			// any non-nil return blocks the default freeze (the object stays
			// held and unfrozen).
			bool bFreezeBlocked = false;
			if ( L != NULL )
			{
				BEGIN_LUA_CALL_HOOK( "OnPhysgunFreeze" );
					lua_pushentity( L, this );
					lua_pushphysicsobject( L, pPhys );
					lua_pushentity( L, pObject );
					lua_pushplayer( L, pOwner );
				END_LUA_CALL_HOOK( 4, 1 );

				if ( lua_gettop( L ) > 0 )
				{
					bFreezeBlocked = !lua_isnil( L, -1 );
					lua_pop( L, 1 );
				}
			}

			if ( !bFreezeBlocked )
			{
				pPhys->EnableMotion( false );

				// HL2SB GMod compat: RMB freeze also ENDS the hold (GMod
				// behaviour).  Latch off the auto-regrab while LMB stays
				// down -- AttachObject unfreezes on grab, so without the
				// latch the next frame's scan re-grabbed and unfroze the
				// body within one tick (the "RMB never freezes" bug).
				m_bFreezeReleaseLatch = true;

				// record it on the player's frozen list -- what R / double-R /
				// Player:PhysgunUnfreeze / UnfreezePhysicsObjects drain from
				HL2SB_PlayerAddFrozenObject( pOwner, pObject, pPhys );

				// HL2SB GMod compat: the freeze feedback -- electric arcs over
				// the frozen body (util.Effect-style dispatch; the engine's
				// TeslaHitboxes callback reads the entity) + the Special1 zap.
				CEffectData zap;
				zap.m_nEntIndex = pObject->entindex();
				DispatchEffect( "TeslaHitboxes", zap );
				pObject->EmitSound( "Weapon_Physgun.Special1" );

				// HL2SB GMod compat: GM:PhysgunDrop( ply, ent ) -- the freeze
				// releases the object, so the drop hook fires here too.
				DetachObject();
			}
			else
			{
				// blocked: keep holding the unfrozen object, GMod-style
				// (no EffectDestroy/SoundDestroy -- the beam stays up)
				return;
			}
		}
#endif

		EffectDestroy();
		SoundDestroy();
		return;
	}

	if ( pObject )
	{
#ifndef CLIENT_DLL
		// HL2SB GMod compat: E+mouse rotates - the mouse deltas arrive with the
		// user command (the client freezes the VIEW while E is held, see
		// HL2SB_PhysgunMouseRotate in in_mouse.cpp), so the object rotates
		// without the camera swinging along.  Runs for BOTH carry modes
		// (shadow-carry and teleport-drive) -- before the drive's early return.
		if ( pOwner->m_nButtons & IN_USE )
		{
			int nMouseDx = 0, nMouseDy = 0;
			const CUserCmd *pCmd = pOwner->GetCurrentUserCommand();
			if ( pCmd != NULL )
			{
				nMouseDx = pCmd->mousedx;
				nMouseDy = pCmd->mousedy;
			}

			// HL2SB (2026-09-26): GMod applies the rotation at
			// physgun_rotation_sensitivity degrees per RAW mouse count
			// (reference: 0.05, clamped to +-1000; the usercmd deltas are
			// unscaled accumulators) and clamps pitch so the prop cannot
			// flip over the top.
			float flRotSens = clamp( physgun_rotation_sensitivity.GetFloat(), -1000.0f, 1000.0f );
			if ( nMouseDx != 0 )
				m_heldWorldAngles.y -= nMouseDx * flRotSens;
			if ( nMouseDy != 0 )
			{
				m_heldWorldAngles.x += nMouseDy * flRotSens;
				m_heldWorldAngles.x = clamp( m_heldWorldAngles.x, -89.0f, 89.0f );
			}

			// HL2SB (2026-09-26): GMod spins the held object at
			// phys_spinspeed deg/s while A/D is held together with E
			// (reference ).  The player still walks -- GMod
			// does not gate the movement keys.
			if ( pOwner->m_nButtons & IN_MOVELEFT )
				m_heldWorldAngles.y += phys_spinspeed.GetFloat() * gpGlobals->frametime;
			else if ( pOwner->m_nButtons & IN_MOVERIGHT )
				m_heldWorldAngles.y -= phys_spinspeed.GetFloat() * gpGlobals->frametime;

			// HL2SB (2026-09-26): GMod snaps the held angles to the
			// gm_snapangles grid while Shift is held with E (reference
			// : angle = round(angle/snap)*snap, gated on
			// buttons & (IN_SPEED|IN_USE)); each tick re-rounds so turning
			// the mouse walks the object notch by notch.
			float flSnap = gm_snapangles.GetFloat();
			if ( flSnap > 0.0f && ( pOwner->m_nButtons & IN_SPEED ) )
			{
				m_heldWorldAngles.x = roundf( m_heldWorldAngles.x / flSnap ) * flSnap;
				m_heldWorldAngles.y = roundf( m_heldWorldAngles.y / flSnap ) * flSnap;
				m_heldWorldAngles.z = roundf( m_heldWorldAngles.z / flSnap ) * flSnap;
				m_heldWorldAngles.x = clamp( m_heldWorldAngles.x, -89.0f, 89.0f );
			}

			// HL2SB diagnostic: are RAW MOUSE MOVEMENT deltas reaching the
			// server?  Only on actual movement frames -- the first cut burned
			// its 3-print cap on the zero-delta frames between E-press and
			// the first mouse move and left real movement unobserved.
			static int s_nRotDiag = 0;
			if ( ( nMouseDx != 0 || nMouseDy != 0 ) && s_nRotDiag < 4 )
			{
				++s_nRotDiag;
				luasrc_LuaInfoMsgF( "[HL2SB physgun] E-rot MOVE dx=%d dy=%d angles=(%.1f %.1f %.1f)\n",
					nMouseDx, nMouseDy, m_heldWorldAngles.x, m_heldWorldAngles.y, m_heldWorldAngles.z );
			}
		}

#ifndef CLIENT_DLL
		// HL2SB GMod compat (2026-09-25): mouse wheel push/pull through the
		// HL2 gravity-gun wheel mechanism -- the client KeyInput maps
		// MOUSE_WHEEL_UP/DOWN to IN_WEAPON1/2 for one cmd while IN_ATTACK is
		// held, and those bits ride the RELIABLE usercmd button path (the
		// earlier hl2sb_physgun_push/pull console-command forward depended on
		// the client hold state, which kept reading false at scroll time).
		// Works for both carry modes (shadow-carry and teleport-drive).
		// HL2SB (2026-09-26): GMod moves max(|physgun_wheelspeed|,0.1) units
		// per notch (reference ) -- not 45*wheelspeed.
		if ( pOwner->m_nButtons & IN_WEAPON1 )
			HL2SB_AdjustDistance( fmaxf( fabsf( physgun_wheelspeed.GetFloat() ), 0.1f ) );
		if ( pOwner->m_nButtons & IN_WEAPON2 )
			HL2SB_AdjustDistance( -fmaxf( fabsf( physgun_wheelspeed.GetFloat() ), 0.1f ) );
#endif

		QAngle angles = m_heldWorldAngles;

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
			pObject->Teleport( &npcTarget, &angles, NULL );
			pObject->SetAbsVelocity( vec3_origin );
			m_movementLength = ( npcTarget - pObject->GetLocalOrigin() ).Length();

			// HL2SB diagnostic: is the NPC teleport-drive even ticking?
			static int s_nNpcDriveDiag = 0;
			if ( s_nNpcDriveDiag < 3 )
			{
				++s_nNpcDriveDiag;
				luasrc_LuaInfoMsgF( "[HL2SB physgun] drive tick '%s' dist=%.0f target=(%.0f %.0f %.0f)\n",
					pObject->GetClassname(), m_distance,
					npcTarget.x, npcTarget.y, npcTarget.z );
			}
			return;
		}
#else
		QAngle angles = m_gravCallback.TransformAnglesFromPlayerSpace( m_gravCallback.m_targetRotation, pOwner );
#endif

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
void CWeaponGravityGun::GetEffectParameters( int effectID, color32 &color, float &scale, IMaterial **pMaterial, Vector &vecAttachment )
{
	const float dt = gpGlobals->curtime;

	float alpha = m_Parameters[effectID].GetAlpha().Interp( dt );
	scale = m_Parameters[effectID].GetScale().Interp( dt );
	*pMaterial = (IMaterial *) m_Parameters[effectID].GetMaterial();

	// HL2SB: GMod tints the gun glow with the player's weapon colour.
	color.r = (int) m_Parameters[effectID].GetColor().x;
	color.g = (int) m_Parameters[effectID].GetColor().y;
	color.b = (int) m_Parameters[effectID].GetColor().z;
	color.a = (int) alpha;

	C_BasePlayer *pOwner = ToBasePlayer( GetOwner() );
	if ( pOwner != NULL )
	{
		Color clrW = HL2SB_GetWeaponColor( pOwner->GetUserID() );
		color.r = (byte)MIN( 255, (int)( color.r * clrW.r() / 255.0f ) );
		color.g = (byte)MIN( 255, (int)( color.g * clrW.g() / 255.0f ) );
		color.b = (byte)MIN( 255, (int)( color.b * clrW.b() / 255.0f ) );
	}

	int	attachment = m_Parameters[effectID].GetAttachment();
	QAngle	angles;

	// HL2SB: the fork resolves weapon attachments on the weapon entity itself
	// (the same proven path the first-person beam uses -- the hidden world
	// model follows the viewmodel in first person).
	GetAttachment( attachment, vecAttachment, angles );
}

//-----------------------------------------------------------------------------
// Whether or not an effect is set to display
//-----------------------------------------------------------------------------
bool CWeaponGravityGun::IsEffectVisible( int effectID )
{
	return m_Parameters[effectID].IsVisible();
}

//-----------------------------------------------------------------------------
// Draws the effect sprite, given an effect parameter ID
//-----------------------------------------------------------------------------
void CWeaponGravityGun::DrawEffectSprite( int effectID )
{
	color32 color;
	float scale;
	IMaterial *pMaterial;
	Vector	vecAttachment;

	if ( IsEffectVisible( effectID ) == false )
		return;

	GetEffectParameters( effectID, color, scale, &pMaterial, vecAttachment );

	if ( color.a <= 0.0f )
		return;

	CMatRenderContextPtr pRenderContext( materials );
	pRenderContext->Bind( pMaterial, this );
	DrawSprite( vecAttachment, scale, scale, color );
}

//-----------------------------------------------------------------------------
// Render the physcannon gun-glow sprite set
//-----------------------------------------------------------------------------
// HL2SB (2026-09-26, reference-confirmed): DISABLED.  This is the HL2
// physcannon / EP2 gravity-gun sprite machine (core + blast + 6 fork glows +
// 3 endcaps).  GMod's physgun does NOT render any of it: GMod's client.dll has
// no physcannon_bluecore / blueflare / bluelight strings, and C_PhysBeam draws
// only the physbeam trail plus the physg_glow endpoint pair ().
// Running this on top of the beam is exactly the "wrong effects piling up"
// the user reported.  Kept as an empty shell so the effect state machine
// (DoEffect*/StartEffects/UpdateElementPosition) still compiles and the shared
// m_EffectState networking keeps working.
void CWeaponGravityGun::DrawEffects( void )
{
}

//-----------------------------------------------------------------------------
// Initialize all sprites and beams
//-----------------------------------------------------------------------------
void CWeaponGravityGun::StartEffects( void )
{
	// ------------------------------------------
	// Core
	// ------------------------------------------
	if ( m_Parameters[PHYSGUN_CORE].GetMaterial() == NULL )
	{
		m_Parameters[PHYSGUN_CORE].GetScale().Init( 0.0f, 1.0f, 0.1f );
		m_Parameters[PHYSGUN_CORE].GetAlpha().Init( 255.0f, 255.0f, 0.1f );
		m_Parameters[PHYSGUN_CORE].SetAttachment( 1 );

		m_Parameters[PHYSGUN_CORE].SetMaterial( PHYSGUN_CORE_SPRITE );
	}

	// ------------------------------------------
	// Blast
	// ------------------------------------------
	if ( m_Parameters[PHYSGUN_BLAST].GetMaterial() == NULL )
	{
		m_Parameters[PHYSGUN_BLAST].GetScale().Init( 0.0f, 1.0f, 0.1f );
		m_Parameters[PHYSGUN_BLAST].GetAlpha().Init( 255.0f, 255.0f, 0.1f );
		m_Parameters[PHYSGUN_BLAST].SetAttachment( 1 );
		m_Parameters[PHYSGUN_BLAST].SetVisible( false );

		m_Parameters[PHYSGUN_BLAST].SetMaterial( PHYSGUN_BLAST_SPRITE );
	}

	// ------------------------------------------
	// Glows (fork mid/tips)
	// ------------------------------------------
	const char *attachNamesGlowThirdPerson[NUM_GLOW_SPRITES] =
	{
		"fork1m",
		"fork1t",
		"fork2m",
		"fork2t",
		"fork3m",
		"fork3t",
	};

	const char *attachNamesGlow[NUM_GLOW_SPRITES] =
	{
		"fork1b",
		"fork1m",
		"fork1t",
		"fork2b",
		"fork2m",
		"fork2t"
	};

	for ( int i = PHYSGUN_GLOW1; i < (PHYSGUN_GLOW1+NUM_GLOW_SPRITES); i++ )
	{
		if ( m_Parameters[i].GetMaterial() != NULL )
			continue;

		m_Parameters[i].GetScale().SetAbsolute( 0.05f * PHYSGUN_SPRITE_SCALE );
		m_Parameters[i].GetAlpha().SetAbsolute( 64.0f );

		// Different for different views
		if ( ShouldDrawUsingViewModel() )
		{
			m_Parameters[i].SetAttachment( LookupAttachment( attachNamesGlow[i-PHYSGUN_GLOW1] ) );
		}
		else
		{
			m_Parameters[i].SetAttachment( LookupAttachment( attachNamesGlowThirdPerson[i-PHYSGUN_GLOW1] ) );
		}
		m_Parameters[i].SetColor( Vector( 255, 255, 255 ) );

		m_Parameters[i].SetMaterial( PHYSGUN_FORKGLOW_SPRITE );
	}

	// ------------------------------------------
	// End caps (fork tips)
	// ------------------------------------------
	const char *attachNamesEndCap[NUM_ENDCAP_SPRITES] =
	{
		"fork1t",
		"fork2t",
		"fork3t"
	};

	for ( int i = PHYSGUN_ENDCAP1; i < (PHYSGUN_ENDCAP1+NUM_ENDCAP_SPRITES); i++ )
	{
		if ( m_Parameters[i].GetMaterial() != NULL )
			continue;

		m_Parameters[i].GetScale().SetAbsolute( 0.05f * PHYSGUN_SPRITE_SCALE );
		m_Parameters[i].GetAlpha().SetAbsolute( 255.0f );
		m_Parameters[i].SetAttachment( LookupAttachment( attachNamesEndCap[i-PHYSGUN_ENDCAP1] ) );
		m_Parameters[i].SetVisible( false );

		m_Parameters[i].SetMaterial( PHYSGUN_FORKEND_SPRITE );
	}
}

//-----------------------------------------------------------------------------
// Closing effects
//-----------------------------------------------------------------------------
void CWeaponGravityGun::DoEffectClosed( void )
{
	// Turn off the end-caps
	for ( int i = PHYSGUN_ENDCAP1; i < (PHYSGUN_ENDCAP1+NUM_ENDCAP_SPRITES); i++ )
	{
		m_Parameters[i].SetVisible( false );
	}
}

//-----------------------------------------------------------------------------
// Ready effects
//-----------------------------------------------------------------------------
void CWeaponGravityGun::DoEffectReady( void )
{
	// Special POV case
	if ( ShouldDrawUsingViewModel() )
	{
		//Turn on the center sprite
		m_Parameters[PHYSGUN_CORE].GetScale().InitFromCurrent( 14.0f, 0.2f );
		m_Parameters[PHYSGUN_CORE].GetAlpha().InitFromCurrent( 128.0f, 0.2f );
		m_Parameters[PHYSGUN_CORE].SetVisible();
	}
	else
	{
		//Turn off the center sprite
		m_Parameters[PHYSGUN_CORE].GetScale().InitFromCurrent( 8.0f, 0.2f );
		m_Parameters[PHYSGUN_CORE].GetAlpha().InitFromCurrent( 0.0f, 0.2f );
		m_Parameters[PHYSGUN_CORE].SetVisible();
	}

	// Turn on the glow sprites
	for ( int i = PHYSGUN_GLOW1; i < (PHYSGUN_GLOW1+NUM_GLOW_SPRITES); i++ )
	{
		m_Parameters[i].GetScale().InitFromCurrent( 0.4f * PHYSGUN_SPRITE_SCALE, 0.2f );
		m_Parameters[i].GetAlpha().InitFromCurrent( 64.0f, 0.2f );
		m_Parameters[i].SetVisible();
	}

	// Turn off the end-caps
	for ( int i = PHYSGUN_ENDCAP1; i < (PHYSGUN_ENDCAP1+NUM_ENDCAP_SPRITES); i++ )
	{
		m_Parameters[i].SetVisible( false );
	}
}

//-----------------------------------------------------------------------------
// Holding effects
//-----------------------------------------------------------------------------
// HL2SB (2026-09-26, reference-confirmed): the HL2 physcannon "holding" state
// (core sprite between the claws, 6 fork glows, 3 endcaps, fork tendril beams)
// does not exist in GMod's physgun.  GMod's held-object look is only the
// physbeama "active" beam overlay plus the physg_glow endpoint pair, both
// driven by the beam draw path.  This hook is a no-op now; the state machine
// keeps running so m_EffectState networking is unchanged.
void CWeaponGravityGun::DoEffectHolding( void )
{
}

//-----------------------------------------------------------------------------
// Launch effects
//-----------------------------------------------------------------------------
void CWeaponGravityGun::DoEffectLaunch( Vector *pos )
{
	C_BasePlayer *pOwner = ToBasePlayer( GetOwner() );
	if ( pOwner == NULL )
		return;

	Vector	endPos;
	Vector	shotDir;

	if ( pos == NULL )
	{
		endPos = pOwner->Weapon_ShootPosition();
		pOwner->EyeVectors( &shotDir );

		trace_t	tr;
		UTIL_TraceLine( endPos, endPos + ( shotDir * MAX_TRACE_LENGTH ), MASK_SHOT, pOwner, COLLISION_GROUP_NONE, &tr );

		endPos = tr.endpos;
		shotDir = endPos - pOwner->Weapon_ShootPosition();
		VectorNormalize( shotDir );
	}
	else
	{
		endPos = *pos;
		shotDir = ( endPos - pOwner->Weapon_ShootPosition() );
		VectorNormalize( shotDir );
	}

	//Turn on the blast sprite and scale
	m_Parameters[PHYSGUN_BLAST].GetScale().Init( 8.0f, 64.0f, 0.1f );
	m_Parameters[PHYSGUN_BLAST].GetAlpha().Init( 255.0f, 0.0f, 0.2f );
	m_Parameters[PHYSGUN_BLAST].SetVisible();
}

//-----------------------------------------------------------------------------
// Shutdown for the weapon when it's holstered
//-----------------------------------------------------------------------------
void CWeaponGravityGun::DoEffectNone( void )
{
	//Turn off main glows
	m_Parameters[PHYSGUN_CORE].SetVisible( false );
	m_Parameters[PHYSGUN_BLAST].SetVisible( false );

	for ( int i = PHYSGUN_GLOW1; i < (PHYSGUN_GLOW1+NUM_GLOW_SPRITES); i++ )
	{
		m_Parameters[i].SetVisible( false );
	}

	for ( int i = PHYSGUN_ENDCAP1; i < (PHYSGUN_ENDCAP1+NUM_ENDCAP_SPRITES); i++ )
	{
		m_Parameters[i].SetVisible( false );
	}

	m_Beams[0].SetVisible( false );
	m_Beams[1].SetVisible( false );
	m_Beams[2].SetVisible( false );
}

//-----------------------------------------------------------------------------
// Idle effect (pulsing)
//-----------------------------------------------------------------------------
void CWeaponGravityGun::DoEffectIdle( void )
{
	StartEffects();

	// Turn on the glow sprites
	for ( int i = PHYSGUN_GLOW1; i < (PHYSGUN_GLOW1+NUM_GLOW_SPRITES); i++ )
	{
		m_Parameters[i].GetScale().SetAbsolute( random->RandomFloat( 0.05f, 0.075f ) * PHYSGUN_SPRITE_SCALE );
		m_Parameters[i].GetAlpha().SetAbsolute( random->RandomInt( 24, 32 ) );
	}

	// Turn on the end-cap sprites
	for ( int i = PHYSGUN_ENDCAP1; i < (PHYSGUN_ENDCAP1+NUM_ENDCAP_SPRITES); i++ )
	{
		m_Parameters[i].GetScale().SetAbsolute( random->RandomFloat( 3, 5 ) );
		m_Parameters[i].GetAlpha().SetAbsolute( random->RandomInt( 200, 255 ) );
	}

	if ( m_EffectState != EFFECT_HOLDING )
	{
		// Turn beams off
		m_Beams[0].SetVisible( false );
		m_Beams[1].SetVisible( false );
		m_Beams[2].SetVisible( false );
	}
}

//-----------------------------------------------------------------------------
// Update the pose parameter for the gun (the claw open/close animation)
//-----------------------------------------------------------------------------
void CWeaponGravityGun::UpdateElementPosition( void )
{
	CBasePlayer *pOwner = ToBasePlayer( GetOwner() );

	float flElementPosition = m_ElementParameter.Interp( gpGlobals->curtime );

	if ( ShouldDrawUsingViewModel() )
	{
		if ( pOwner != NULL )
		{
			CBaseViewModel *vm = pOwner->GetViewModel();

			if ( vm != NULL )
			{
				vm->SetPoseParameter( "active", flElementPosition );
			}
		}
	}
	else
	{
		SetPoseParameter( "active", flElementPosition );
	}
}

//-----------------------------------------------------------------------------
// Think function for the client
//-----------------------------------------------------------------------------
void CWeaponGravityGun::ClientThink( void )
{
	// Update our elements visually
	UpdateElementPosition();

	// Update our effects
	DoEffectIdle();

	BaseClass::ClientThink();
}

#endif // CLIENT_DLL

//-----------------------------------------------------------------------------
// Effect dispatcher (shared -- the client realm applies the sprite state,
// the server realm just tracks the state value)
//-----------------------------------------------------------------------------
void CWeaponGravityGun::DoEffect( int effectType, Vector *pos )
{
	m_EffectState = effectType;

#ifdef CLIENT_DLL
	// Save predicted state
	m_nOldEffectState = m_EffectState;

	switch( effectType )
	{
	case EFFECT_CLOSED:
		DoEffectClosed( );
		break;

	case EFFECT_READY:
		DoEffectReady( );
		break;

	case EFFECT_HOLDING:
		DoEffectHolding();
		break;

	case EFFECT_LAUNCH:
		DoEffectLaunch( pos );
		break;

	default:
	case EFFECT_NONE:
		DoEffectNone();
		break;
	}
#endif
}

//-----------------------------------------------------------------------------
// Destroy all sprites and beams
//-----------------------------------------------------------------------------
void CWeaponGravityGun::DestroyEffects( void )
{
#ifdef CLIENT_DLL
	// Free our beams
	m_Beams[0].Release();
	m_Beams[1].Release();
	m_Beams[2].Release();
#endif
}

//-----------------------------------------------------------------------------
// Open the claw elements (both realms drive this from the shared hold state)
//-----------------------------------------------------------------------------
void CWeaponGravityGun::OpenElements( void )
{
	if ( m_bOpen )
		return;

#ifndef CLIENT_DLL
	EmitSound( "Weapon_PhysCannon.OpenClaws" );
#endif

	CBasePlayer *pOwner = ToBasePlayer( GetOwner() );

	if ( pOwner == NULL )
		return;

	SendWeaponAnim( ACT_VM_IDLE );

	m_bOpen = true;

	DoEffect( EFFECT_READY );

#ifdef CLIENT_DLL
	// Element prediction
	m_ElementParameter.InitFromCurrent( 1.0f, 0.2f, INTERP_SPLINE );
	m_bOldOpen = true;
#endif
}

//-----------------------------------------------------------------------------
// Close the claw elements
//-----------------------------------------------------------------------------
void CWeaponGravityGun::CloseElements( void )
{
	if ( m_bOpen == false )
		return;

#ifndef CLIENT_DLL
	EmitSound( "Weapon_PhysCannon.CloseClaws" );
#endif

	CBasePlayer *pOwner = ToBasePlayer( GetOwner() );

	if ( pOwner == NULL )
		return;

	SendWeaponAnim( ACT_VM_IDLE );

	m_bOpen = false;

	DoEffect( EFFECT_CLOSED );

#ifdef CLIENT_DLL
	// Element prediction
	m_ElementParameter.InitFromCurrent( 0.0f, 0.5f, INTERP_SPLINE );
	m_bOldOpen = false;
#endif
}

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
		StartEffects();
	}

	// Update effect state when out of parity with the server
	if ( m_nOldEffectState != m_EffectState )
	{
		DoEffect( m_EffectState );
		m_nOldEffectState = m_EffectState;
	}

	// Update element state when out of parity
	if ( m_bOldOpen != m_bOpen )
	{
		if ( m_bOpen )
		{
			m_ElementParameter.InitFromCurrent( 1.0f, 0.2f, INTERP_SPLINE );
		}
		else
		{
			m_ElementParameter.InitFromCurrent( 0.0f, 0.5f, INTERP_SPLINE );
		}

		m_bOldOpen = (bool) m_bOpen;
	}
}
#endif

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

		// HL2SB (2026-09-26): HL2 physcannon effect machine -- claws close,
		// gun-glow drops back to idle (GMod parity).
		CloseElements();
		DoEffect( EFFECT_CLOSED );
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
	// HL2SB diagnostic (2026-09-22): WHY did the attach fail/choose a mode?
	// One line per attach attempt (capped): class, NPC/player flags, movetype,
	// the physics body the trace handed us.
	static int s_nAttachDiag = 0;
	if ( s_nAttachDiag < 12 )
	{
		++s_nAttachDiag;
		luasrc_LuaInfoMsgF( "[HL2SB physgun] attach: '%s' npc=%d player=%d movetype=%d phys=%p bone=%d\n",
			pObject->GetClassname(),
			pObject->IsNPC() ? 1 : 0,
			pObject->IsPlayer() ? 1 : 0,
			(int)pObject->GetMoveType(),
			(void *)pPhysics, (int)physicsbone );
	}

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
		// HL2SB diag (2026-09-24): a veto here silently drops what we just
		// grabbed; a veto storm shows up as the rapid re-attach bursts in the
		// session log (attach x6 same pointer).
		static int s_nVetoDiag = 0;
		if ( s_nVetoDiag < 4 )
		{
			++s_nVetoDiag;
			luasrc_LuaInfoMsgF( "[HL2SB physgun] pickup VETO '%s'\n", pObject->GetClassname() );
		}
		m_hObject = NULL;
		return;
	}

	if ( pObject->IsPlayer() && !bForceAllow )
	{
		m_hObject = NULL;
		return;
	}

	// HL2SB GMod compat: GMod grabs EVERYTHING. Anything with a vphysics body
	// rides the shadow controller; everything else -- NPCs, nextbots, scripted
	// NPCs, body-less script ents -- rides the teleport-drive. Players only
	// when the gamemode force-allowed the pickup (hook returned true).
	bool bHasBody = ( pPhysics != NULL && pObject->GetMoveType() == MOVETYPE_VPHYSICS );
	if ( pObject->IsPlayer() )
		m_bDraggingNPC = bForceAllow && !bHasBody;
	else
		m_bDraggingNPC = !bHasBody;

	// GMod style: the held object keeps its WORLD orientation, E+mouse rotates
	m_heldWorldAngles = pObject->GetAbsAngles();

	// GM:GetPreferredCarryAngles( ent, ply ) -> Angle|nil -- a non-nil result
	// is the pose the object snaps to while carried.
	if ( L != NULL )
	{
		BEGIN_LUA_CALL_HOOK( "GetPreferredCarryAngles" );
			lua_pushentity( L, pObject );
			lua_pushplayer( L, pOwner );
		END_LUA_CALL_HOOK( 2, 1 );

		if ( lua_gettop( L ) > 0 )
		{
			if ( !lua_isnil( L, -1 ) )
				m_heldWorldAngles = lua_toangle( L, -1 );
			lua_pop( L, 1 );
		}
	}
#endif

#ifndef CLIENT_DLL
	if ( m_bDraggingNPC )
	{
		// teleport-drag attach: no controller, no bone bookkeeping - just hold
		// the handle.  Keep the grab offset (origin vs. hit point) so the
		// entity does not snap toward the player's eye line.
		m_vecGrabOffset = pObject->GetAbsOrigin() - end;
		Pickup_OnPhysGunPickup( pObject, pOwner );

		static int s_nNpcGrabDiag = 0;
		if ( s_nNpcGrabDiag < 15 )
		{
			++s_nNpcGrabDiag;
			luasrc_LuaInfoMsgF( "[HL2SB physgun] drive-drag attached '%s' (offset %.0f %.0f %.0f)\n",
				pObject->GetClassname(), m_vecGrabOffset.x, m_vecGrabOffset.y, m_vecGrabOffset.z );
		}

		m_distance = distance;

		// GM:OnPhysgunPickup( ply, ent ) -- fired after a SUCCESSFUL pickup
		if ( L != NULL )
		{
			BEGIN_LUA_CALL_HOOK( "OnPhysgunPickup" );
				lua_pushplayer( L, pOwner );
				lua_pushentity( L, pObject );
			END_LUA_CALL_HOOK( 2, 0 );
		}

		// HL2SB (2026-09-26): claws open + gun-glow to holding (GMod parity)
		OpenElements();
		DoEffect( EFFECT_HOLDING );
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

		// GM:OnPhysgunPickup( ply, ent ) -- fired after a SUCCESSFUL pickup
		if ( L != NULL )
		{
			BEGIN_LUA_CALL_HOOK( "OnPhysgunPickup" );
				lua_pushplayer( L, pOwner );
				lua_pushentity( L, pObject );
			END_LUA_CALL_HOOK( 2, 0 );
		}
#endif

		// HL2SB (2026-09-26): claws open + gun-glow to holding (GMod parity)
		OpenElements();
		DoEffect( EFFECT_HOLDING );
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
		// used to finish and leave the viewmodel frozen on its last frame - the
		// arms off-screen (the "arms vanish on held attack" report).  Re-arm to
		// the PLAIN IDLE: ACT_VM_RELOAD (the first cut's hold pose) has no
		// sequence in the physgun's viewmodel, which is what made the arms
		// vanish entirely.
		if ( IsViewModelSequenceFinished() )
			SendWeaponAnim( ACT_VM_IDLE );
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

		// HL2SB GMod compat: GM:DrawPhysgunBeam( ply, weapon, enabled, target,
		// physBone, hitPos ) -- literal false hides the default beam + sprites.
		// hitPos is the LOCAL grab offset on the held body (the wiki's
		// "relative to the physics bone"), or the world scan endpos.
		if ( L != NULL )
		{
			C_BaseEntity *pHeld = m_hObject;
			Vector vecHookHit( vec3_origin );
			if ( pHeld != NULL )
				vecHookHit = m_worldPosition;
			else
			{
				trace_t tr;
				TraceLine( &tr );
				vecHookHit = tr.endpos;
			}

			BEGIN_LUA_CALL_HOOK( "DrawPhysgunBeam" );
				lua_pushplayer( L, pOwner );
				lua_pushentity( L, this );
				lua_pushboolean( L, m_active );
				lua_pushentity( L, pHeld );
				lua_pushinteger( L, m_physicsBone );
				lua_pushvector( L, vecHookHit );
			END_LUA_CALL_HOOK( 6, 1 );

			if ( lua_gettop( L ) > 0 )
			{
				bool bSuppressed = ( lua_isboolean( L, -1 ) && lua_toboolean( L, -1 ) == 0 );
				lua_pop( L, 1 );
				if ( bSuppressed )
					return 0;
			}
		}

		// physgun_drawbeams 0 hides the DEFAULT beam/sprites (the hook above
		// still fires, exactly as the wiki documents it)
		{
			extern ConVar physgun_drawbeams;
			if ( !physgun_drawbeams.GetBool() )
				return 0;
		}

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
			// HL2SB GMod compat (2026-09-24): the beam leaves along the
			// MUZZLE's barrel direction and bends to the target -- the GMod13
			// curve.  A control point on the eye's sight line read as a
			// straight rod.
			Vector vecMuzzleDir;
			AngleVectors( tmpAngle, &vecMuzzleDir );
			points[1] = points[0] + vecMuzzleDir * ( points[2].DistTo( points[0] ) * 0.45f );
		}

		// HL2SB (2026-09-26, reference-confirmed): C_PhysBeam draws exactly two
		// beam trails -- sprites/physbeam.vmt always, sprites/physbeama.vmt as
		// the additive "active" overlay while something is held.  These are
		// GMod's own materials (their absence from this fork's content was the
		// long-standing "beam looks wrong" cause).  The old HL2 physbeam1/
		// physbeam path is gone.
		Color clrWeapon = HL2SB_GetWeaponColor( pOwner->GetUserID() );
		Vector color;
		color.Init( clrWeapon.r() / 255.0f, clrWeapon.g() / 255.0f, clrWeapon.b() / 255.0f );

		float scrollOffset = gpGlobals->curtime - (int)gpGlobals->curtime;
		CMatRenderContextPtr pRenderContext( materials );

		float flWidth = pObject ? 13 / 3.0f : 13 / 5.0f;

		IMaterial *pMat = materials->FindMaterial( PHYSGUN_BEAM_SPRITE, TEXTURE_GROUP_CLIENT_EFFECTS );
		pRenderContext->Bind( pMat );
		DrawBeamQuadratic( points[0], points[1], points[2], flWidth, color, scrollOffset );

		if ( pObject != NULL )
		{
			IMaterial *pMatActive = materials->FindMaterial( PHYSGUN_BEAM_ACTIVE, TEXTURE_GROUP_CLIENT_EFFECTS );
			pRenderContext->Bind( pMatActive );
			DrawBeamQuadratic( points[0], points[1], points[2], flWidth, color, scrollOffset );
		}

		// Endpoint glow: GMod's two physg_glow layers, drawn ONCE each at the
		// target.  (The old code drew 3x each plus a muzzle copy -- that
		// over-draw, stacked on the HL2 sprite machine, was the "wrong effects
		// piling up" the user saw.)
		color32 clr =
		{
			(byte)MIN( 255, (int)( clrWeapon.r() ) ),
			(byte)MIN( 255, (int)( clrWeapon.g() ) ),
			(byte)MIN( 255, (int)( clrWeapon.b() ) ),
			255
		};

		float scale = random->RandomFloat( 3, 5 ) * ( pObject ? 3 : 2 );

		IMaterial *pGlow1 = materials->FindMaterial( PHYSGUN_ENDGLOW_SPRITE, TEXTURE_GROUP_CLIENT_EFFECTS );
		IMaterial *pGlow2 = materials->FindMaterial( PHYSGUN_ENDGLOW_SPRITE2, TEXTURE_GROUP_CLIENT_EFFECTS );
		pRenderContext->Bind( pGlow1 );
		DrawSprite( points[2], scale, scale, clr );
		pRenderContext->Bind( pGlow2 );
		DrawSprite( points[2], scale, scale, clr );

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

	// HL2SB GMod compat: GM:DrawPhysgunBeam( ply, weapon, enabled, target,
	// physBone, hitPos ) -- literal false hides the default effects (the
	// first-person path; the third-person path hooks inside DrawModel).
	if ( L != NULL )
	{
		C_BaseEntity *pHeld = m_hObject;
		Vector vecHookHit( vec3_origin );
		if ( pHeld != NULL )
			vecHookHit = m_worldPosition;
		else
		{
			trace_t tr;
			TraceLine( &tr );
			vecHookHit = tr.endpos;
		}

		BEGIN_LUA_CALL_HOOK( "DrawPhysgunBeam" );
			lua_pushplayer( L, pOwner );
			lua_pushentity( L, this );
			lua_pushboolean( L, m_active );
			lua_pushentity( L, pHeld );
			lua_pushinteger( L, m_physicsBone );
			lua_pushvector( L, vecHookHit );
		END_LUA_CALL_HOOK( 6, 1 );

		if ( lua_gettop( L ) > 0 )
		{
			bool bSuppressed = ( lua_isboolean( L, -1 ) && lua_toboolean( L, -1 ) == 0 );
			lua_pop( L, 1 );
			if ( bSuppressed )
				return;
		}
	}

	// physgun_drawbeams 0 hides the DEFAULT beam/sprites (first-person path)
	{
		extern ConVar physgun_drawbeams;
		if ( !physgun_drawbeams.GetBool() )
			return;
	}

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

	// HL2SB GMod compat (2026-09-24): beam leaves along the VIEWMODEL MUZZLE's
	// barrel direction (tmpAngle = attachment 1) and bends to the target --
	// the GMod13 curve.  The old eye-sight-line control point read as a
	// straight rod.
	Vector vecMuzzleDir;
	AngleVectors( tmpAngle, &vecMuzzleDir );
	points[1] = points[0] + vecMuzzleDir * ( points[2].DistTo( points[0] ) * 0.45f );

	// HL2SB (2026-09-26, reference-confirmed): C_PhysBeam draws exactly two
	// beam trails -- sprites/physbeam.vmt always, sprites/physbeama.vmt as the
	// additive "active" overlay while something is held (client.dll
	// ).  Both are GMod's own materials.  The old HL2
	// physbeam1/physbeam split is gone, as is the HL2 physcannon sprite machine.
	Color clrWeapon = HL2SB_GetWeaponColor( pOwner->GetUserID() );
	Vector color;
	color.Init( clrWeapon.r() / 255.0f, clrWeapon.g() / 255.0f, clrWeapon.b() / 255.0f );

	// Now draw it.
	CViewSetup beamView = *view->GetPlayerViewSetup();

	Frustum dummyFrustum;
	render->Push3DView( beamView, 0, NULL, dummyFrustum );

	float scrollOffset = gpGlobals->curtime - (int)gpGlobals->curtime;
	CMatRenderContextPtr pRenderContext( materials );
#if 1
	// HACK HACK:  Munge the depth range to prevent view model from poking into walls, etc.
	// Force clipped down range
	pRenderContext->DepthRange( 0.1f, 0.2f );
#endif
	float flWidth = pObject ? 13 / 3.0f : 13 / 5.0f;

	IMaterial *pMat = materials->FindMaterial( PHYSGUN_BEAM_SPRITE, TEXTURE_GROUP_CLIENT_EFFECTS );
	pRenderContext->Bind( pMat );
	DrawBeamQuadratic( points[0], points[1], points[2], flWidth, color, scrollOffset );

	if ( pObject != NULL )
	{
		IMaterial *pMatActive = materials->FindMaterial( PHYSGUN_BEAM_ACTIVE, TEXTURE_GROUP_CLIENT_EFFECTS );
		pRenderContext->Bind( pMatActive );
		DrawBeamQuadratic( points[0], points[1], points[2], flWidth, color, scrollOffset );
	}

	// Endpoint glow: GMod's two physg_glow layers, once each at the target.
	color32 clr =
	{
		(byte)MIN( 255, (int)( clrWeapon.r() ) ),
		(byte)MIN( 255, (int)( clrWeapon.g() ) ),
		(byte)MIN( 255, (int)( clrWeapon.b() ) ),
		255
	};

	float scale = random->RandomFloat( 3, 5 ) * ( pObject ? 3 : 2 );

	IMaterial *pGlow1 = materials->FindMaterial( PHYSGUN_ENDGLOW_SPRITE, TEXTURE_GROUP_CLIENT_EFFECTS );
	IMaterial *pGlow2 = materials->FindMaterial( PHYSGUN_ENDGLOW_SPRITE2, TEXTURE_GROUP_CLIENT_EFFECTS );
	pRenderContext->Bind( pGlow1 );
	DrawSprite( points[2], scale, scale, clr );
	pRenderContext->Bind( pGlow2 );
	DrawSprite( points[2], scale, scale, clr );

#if 1
	pRenderContext->DepthRange( 0.0f, 1.0f );
#endif

	render->PopView( dummyFrustum );

	// HL2SB (2026-09-24): the viewmodel GLOW is not drawn from here anymore.
	// A same-transform additive shell re-drew the viewmodel and (a) recursed
	// through C_BaseViewModel::DrawModel's tail call into this very function
	// (fixed with a latch) and (b) still rendered as a growing white blob
	// fighting the surrounding view context (user capture 02:01).  The glow
	// now comes from the NATIVE PlayerWeaponColor material proxy
	// (c_viewmodel_attachment.cpp) pulsing brighter while HL2SB_PhysgunIsHolding()
	// -- the same chain GMod13 uses on its own physgun materials.

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

	// HL2SB (2026-09-26): HL2 physcannon scan glow (CheckForTarget parity,
	// both realms) -- the claws open when the beam points at a grabbable
	// target and close after a 0.5 s debounce when it does not.
	if ( m_hObject == NULL && !m_bFreezeReleaseLatch )
	{
		trace_t trScan;
		TraceLine( &trScan );

		if ( trScan.DidHitNonWorldEntity() )
		{
			m_nChangeState = ELEMENT_STATE_NONE;
			OpenElements();
		}
		else if ( !m_active && ( m_flElementDebounce < gpGlobals->curtime ) && ( m_nChangeState == ELEMENT_STATE_NONE ) )
		{
			m_nChangeState = ELEMENT_STATE_CLOSED;
			m_flElementDebounce = gpGlobals->curtime + 0.5f;
		}
	}

	if ( ( m_flElementDebounce < gpGlobals->curtime ) && ( m_nChangeState != ELEMENT_STATE_NONE ) )
	{
		if ( m_nChangeState == ELEMENT_STATE_OPEN )
		{
			OpenElements();
		}
		else if ( m_nChangeState == ELEMENT_STATE_CLOSED )
		{
			CloseElements();
		}

		m_nChangeState = ELEMENT_STATE_NONE;
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

bool CWeaponGravityGun::Reload( void )
{
#ifndef CLIENT_DLL
	// HL2SB GMod compat (2026-09-22 physgun audit), GMod's R contract:
	//   GM:OnPhysgunReload( physgun, ply )   non-nil return -> NO default action
	//   single R   -> unfreeze the frozen bodies of the entity under the
	//                 crosshair (GM:Player:PhysgunUnfreeze semantics)
	//   double-R   -> unfreeze EVERYTHING THIS PLAYER froze
	//                (Player:UnfreezePhysicsObjects semantics -- NOT the
	//                whole map, which the first cut did)
	CBasePlayer *pOwner = ToBasePlayer( GetOwner() );
	if ( pOwner == NULL )
		return false;

	if ( L != NULL )
	{
		BEGIN_LUA_CALL_HOOK( "OnPhysgunReload" );
			lua_pushentity( L, this );
			lua_pushplayer( L, pOwner );
		END_LUA_CALL_HOOK( 2, 1 );

		if ( lua_gettop( L ) > 0 )
		{
			bool bBlocked = !lua_isnil( L, -1 );
			lua_pop( L, 1 );
			if ( bBlocked )
				return false;
		}
	}

	float flNow = gpGlobals->curtime;
	bool bDoubleTap = ( flNow - m_flLastReloadPress ) < 0.35f;
	m_flLastReloadPress = flNow;

	if ( bDoubleTap )
	{
		int nUnfrozen = HL2SB_PlayerUnfreezeAll( pOwner );

		static int s_nUnfreezeAllDiag = 0;
		if ( s_nUnfreezeAllDiag < 15 )
		{
			++s_nUnfreezeAllDiag;
			luasrc_LuaInfoMsgF( "[HL2SB physgun] double-R unfroze %d object(s) on the player's list\n", nUnfrozen );
		}
		return nUnfrozen > 0;
	}

	// single R: the aimed entity's frozen bodies (GMod counts them too)
	int nUnfrozen = HL2SB_PlayerUnfreezeAimed( pOwner );
	return nUnfrozen > 0;
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

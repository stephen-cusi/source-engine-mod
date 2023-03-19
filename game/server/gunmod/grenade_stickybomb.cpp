//========= Copyright © 1996-2001, Valve LLC, All rights reserved. ============
//
// Purpose:
//
// $NoKeywords: $
//=============================================================================

#include "cbase.h"
#include "grenade_stickybomb.h"
#include "explode.h"
#include "model_types.h"
#include "studio.h"
#include "filesystem.h"

void LaunchStickyBomb( CBaseCombatCharacter *pOwner, const Vector &origin, const Vector &direction )
{
	CStickyBomb *pGrenade = (CStickyBomb *)CBaseEntity::Create( "grenade_stickybomb", origin, vec3_angle, pOwner );
	pGrenade->SetVelocity( direction * 1200, vec3_origin );
}

LINK_ENTITY_TO_CLASS( grenade_stickybomb, CStickyBomb );

IMPLEMENT_SERVERCLASS_ST(CStickyBomb, DT_StickyBomb)
	SendPropInt( SENDINFO( m_boneIndexAttached ), MAXSTUDIOBONEBITS, SPROP_UNSIGNED ),
	SendPropVector(SENDINFO(m_bonePosition), -1,  SPROP_COORD ),
	SendPropAngle(SENDINFO_VECTORELEM(m_boneAngles, 0), 13 ),
	SendPropAngle(SENDINFO_VECTORELEM(m_boneAngles, 1), 13 ),
	SendPropAngle(SENDINFO_VECTORELEM(m_boneAngles, 2), 13 ),
END_SEND_TABLE()


//---------------------------------------------------------
// Save/Restore
//---------------------------------------------------------
BEGIN_DATADESC( CStickyBomb )
	DEFINE_FIELD( m_hOperator,			FIELD_EHANDLE ),
	DEFINE_FIELD( m_boneIndexAttached,	FIELD_INTEGER ),
	DEFINE_FIELD( m_bonePosition,		FIELD_VECTOR ),
	DEFINE_FIELD( m_boneAngles,			FIELD_VECTOR ),
END_DATADESC()

// global list for fast searching
CUtlVector<CStickyBomb *> CStickyBomb::m_stickyList;

//-----------------------------------------------------------------------------
// Purpose: Sticky bomb class
//-----------------------------------------------------------------------------
CStickyBomb::CStickyBomb()
{
	m_stickyList.AddToTail(this);
}

CStickyBomb::~CStickyBomb()
{
	int index = m_stickyList.Find(this);
	if ( m_stickyList.IsValidIndex(index) )
	{
		m_stickyList.FastRemove(index);
	}
}

void CStickyBomb::Precache()
{
	KeyValues *kv = new KeyValues( "SMenu" );
	if ( kv )
	{
		if ( kv->LoadFromFile(g_pFullFileSystem, "addons/toolgun/dynamite.txt") )
		{
			kv->GetFirstSubKey();
			modelName = kv->GetString("modelname", "");
		}
	}
	PrecacheModel( modelName );
	
	BaseClass::Precache();
	Assert( (1<<MAXSTUDIOBONEBITS) >= MAXSTUDIOBONES );
}

void CStickyBomb::Spawn()
{
	m_hOperator = GetOwnerEntity();
	Precache();
	SetModel( modelName );
	SetSolid( SOLID_BBOX );
	SetMoveType( MOVETYPE_FLYGRAVITY, MOVECOLLIDE_FLY_CUSTOM );
	SetCollisionGroup( COLLISION_GROUP_WEAPON );
	m_takedamage = DAMAGE_NO;
	SetNextThink( TICK_NEVER_THINK );
	m_flAnimTime = gpGlobals->curtime;
	m_flPlaybackRate = 0.0;
	UTIL_SetSize( this, vec3_origin, vec3_origin );
	// no shadows, please.
	AddEffects( EF_NOSHADOW|EF_NORECEIVESHADOW );
}
void CStickyBomb::SetVelocity( const Vector &velocity, const AngularImpulse &angVelocity )
{
	SetAbsVelocity( velocity );
}

void CStickyBomb::DetonateByOperator( CBaseEntity *pOperator )
{
	for ( int i = m_stickyList.Count()-1; i >= 0; --i )
	{
		if ( m_stickyList[i]->m_hOperator == pOperator || m_stickyList[i]->m_hOperator.Get() == NULL )
		{
			if ( m_stickyList[i]->GetNextThink() < gpGlobals->curtime )
			{
				m_stickyList[i]->SetNextThink( gpGlobals->curtime );
			}
		}
	}
}

void CStickyBomb::SetBombOrigin()
{
	if ( IsFollowingEntity() )
	{
		CBaseAnimating *pOtherAnim = dynamic_cast<CBaseAnimating *>(GetFollowedEntity());
		if ( pOtherAnim )
		{
			Vector origin;
			matrix3x4_t boneToWorld;
			pOtherAnim->GetBoneTransform( m_boneIndexAttached, boneToWorld );
			VectorTransform( m_bonePosition, boneToWorld, origin );
			StopFollowingEntity( );
			SetMoveType( MOVETYPE_NONE );
			SetAbsOrigin( origin );
		}
	}
}

void CStickyBomb::Detonate()
{
	SetBombOrigin();
	ExplosionCreate( GetAbsOrigin(), GetAbsAngles(), m_hOperator, 100, 250, true );
	UTIL_ScreenShake( GetAbsOrigin(), 25.0, 150.0, 1.0, 250, SHAKE_START );
}

void CStickyBomb::Think()
{
	Detonate();
}

void CStickyBomb::NotifySystemEvent( CBaseEntity *pNotify, notify_system_event_t eventType, const notify_system_event_params_t &params )
{
	// UNDONE: Do this for client ragdolls too?
	// UNDONE: Add notify event die or client ragdoll?
	if ( eventType == NOTIFY_EVENT_DESTROY )
	{
		if ( pNotify == GetParent() )
		{
			SetBombOrigin();
			SetMoveType( MOVETYPE_FLYGRAVITY, MOVECOLLIDE_FLY_CUSTOM );
		}
	}
}

void CStickyBomb::Touch( CBaseEntity *pOther )
{
	// Don't stick if already stuck
	if ( GetMoveType() == MOVETYPE_FLYGRAVITY )
	{
		trace_t tr; 
		GetTouchTrace();
		// stickies don't stick to each other or sky
		if ( FClassnameIs(pOther, "grenade_stickybomb") || (tr.surface.flags & SURF_SKY) )
		{
			// bounce
			Vector vecNewVelocity;
			PhysicsClipVelocity( GetAbsVelocity(), tr.plane.normal, vecNewVelocity, 1.0 );
			SetAbsVelocity( vecNewVelocity );
		}
		else 
		{
			SetAbsVelocity( vec3_origin );
			SetMoveType( MOVETYPE_NONE );
			if ( pOther->entindex() != 0 )
			{
				// set up notification if the parent is deleted before we explode
				g_pNotify->AddEntity( this, pOther );

				if ( (tr.surface.flags & SURF_HITBOX) && modelinfo->GetModelType( pOther->GetModel() ) == mod_studio )
				{
					CBaseAnimating *pOtherAnim = dynamic_cast<CBaseAnimating *>(pOther);
					if ( pOtherAnim )
					{
						matrix3x4_t bombWorldSpace;
						MatrixCopy( EntityToWorldTransform(), bombWorldSpace );

						// get the bone info so we can follow the bone
						FollowEntity( pOther );
						SetOwnerEntity( pOther );
						m_boneIndexAttached = pOtherAnim->GetHitboxBone( tr.hitbox );
						matrix3x4_t boneToWorld;
						pOtherAnim->GetBoneTransform( m_boneIndexAttached, boneToWorld );

						// transform my current position/orientation into the hit bone's space
						// UNDONE: Eventually we need to intersect with the mesh here
						// REVISIT: maybe do something like the decal code to find a spot on
						//			the mesh.
						matrix3x4_t worldToBone, localMatrix;
						MatrixInvert( boneToWorld, worldToBone );
						ConcatTransforms( worldToBone, bombWorldSpace, localMatrix );
						MatrixAngles( localMatrix, m_boneAngles.GetForModify(), m_bonePosition.GetForModify() );
						return;
					}
				}
				SetParent( pOther );
			}
		}
	}
}
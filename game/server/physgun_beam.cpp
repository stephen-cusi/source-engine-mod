//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: HL2SB GMod compat (2026-10-03): the physics gun's beam entity.
//
// A direct translation of the reference "physgun_beam" server entity: a
// model-less, non-solid entity parented to the holding player that carries
// the beam STATE (who holds, what is held, where the grab point sits in the
// target's local space, whether the beam is lit).  The client class draws
// everything; the server only updates these fields.
//
//   DT_PhysBeam: m_hPlayer, m_hTargetEnt, m_HoldPos, m_bIsOn, m_iPhysBone
//
// m_HoldPos is the grab point in TARGET-LOCAL space (a plain world offset
// while the target is unrotated, the physics element's local for
// MOVETYPE_VPHYSICS, the entity-matrix local otherwise) - the client
// transforms it back by the matching rule every frame, which is what makes
// the beam end stick to the grabbed spot while the object turns.
//
//=============================================================================//

#include "cbase.h"
#include "physgun_beam.h"

extern ConVar hl2sb_physgun_beamdebug;	// HL2SB (2026-10-03): defined in weapon_physgun.cpp

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

LINK_ENTITY_TO_CLASS( physgun_beam, CPhysBeam );

IMPLEMENT_SERVERCLASS_ST( CPhysBeam, DT_PhysBeam )
	SendPropEHandle( SENDINFO( m_hPlayer ) ),
	SendPropEHandle( SENDINFO( m_hTargetEnt ) ),
	SendPropVector( SENDINFO( m_HoldPos ), -1, SPROP_COORD ),
	SendPropInt( SENDINFO( m_bIsOn ), 1, SPROP_UNSIGNED ),
	SendPropInt( SENDINFO( m_iPhysBone ) ),
END_SEND_TABLE()

CPhysBeam::CPhysBeam( void )
{
	m_hPlayer = NULL;
	m_hTargetEnt = NULL;
	m_HoldPos = vec3_origin;
	m_bIsOn = false;
	m_iPhysBone = 0;
}

void CPhysBeam::Spawn( void )
{
	BaseClass::Spawn();

	// Reference creator: non-solid, no movement, sized so the networked
	// bounds track the player it follows.
	SetSolid( SOLID_NONE );
	SetMoveType( MOVETYPE_NONE );
	SetCollisionGroup( COLLISION_GROUP_NONE );
	SetSize( Vector( -60, -60, -60 ), Vector( 60, 60, 60 ) );
	SetRenderMode( kRenderNormal );

	// HL2SB (2026-10-03): NO EF_NODRAW.  The reference server sets it, but
	// its CLIENT class re-opens the render lists with a render-flags
	// override; our translated client class has no such override, and
	// EF_NODRAW is exactly the kind of structural delta that keeps a
	// model-less entity out of DrawModel forever.  This entity draws nothing
	// on its own (no model) - the client class' DrawModel is the only thing
	// EF_NODRAW would have been suppressing.
}

int CPhysBeam::UpdateTransmitState( void )
{
	// The beam follows its holder everywhere; send it whenever the player
	// goes out instead of relying on the PVS check.
	return SetTransmitState( FL_EDICT_ALWAYS );
}

void CPhysBeam::TurnOff( void )
{
	m_bIsOn = false;
	m_hTargetEnt = NULL;
	m_iPhysBone = 0;
	m_HoldPos = vec3_origin;
}

void CPhysBeam::SetHolder( CBasePlayer *pPlayer )
{
	m_hPlayer = pPlayer;
	if ( pPlayer != NULL && GetMoveParent() != pPlayer )
	{
		// Reference: SetOwnerEntity + SetParent(player, -1).  Parenting keeps
		// the entity's transmit state tied to the holder and its origin
		// riding the player.
		SetOwnerEntity( pPlayer );
		SetParent( pPlayer, -1 );
	}
}

void CPhysBeam::SetTarget( CBaseEntity *pTarget, int iPhysBone, const Vector &vecHoldPos )
{
	m_hTargetEnt = pTarget;
	m_iPhysBone = iPhysBone;
	m_HoldPos = vecHoldPos;
}

//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: HL2SB GMod compat (2026-10-03): the server side of the physics
// gun's beam entity - a model-less, non-solid entity parented to the holding
// player that carries the beam STATE to the client class
// (game/client/c_physgun_beam.cpp), which draws the whole visual.
//
//   DT_PhysBeam: m_hPlayer, m_hTargetEnt, m_HoldPos, m_bIsOn, m_iPhysBone
//
//=============================================================================//

#ifndef PHYSGUN_BEAM_H
#define PHYSGUN_BEAM_H
#ifdef _WIN32
#pragma once
#endif

class CPhysBeam : public CBaseEntity
{
	DECLARE_CLASS( CPhysBeam, CBaseEntity );

public:
	DECLARE_SERVERCLASS();

	CPhysBeam( void );
	virtual void Spawn( void );
	virtual int UpdateTransmitState( void );

	void TurnOn( void ) { m_bIsOn = true; }
	void TurnOff( void );
	void SetHolder( CBasePlayer *pPlayer );
	void SetTarget( CBaseEntity *pTarget, int iPhysBone, const Vector &vecHoldPos );

private:
	CNetworkHandle( CBaseEntity, m_hPlayer );
	CNetworkHandle( CBaseEntity, m_hTargetEnt );
	CNetworkVector( m_HoldPos );
	CNetworkVar( bool, m_bIsOn );
	CNetworkVar( int, m_iPhysBone );
};

#endif // PHYSGUN_BEAM_H

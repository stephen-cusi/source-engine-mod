//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: RPG
//
//=============================================================================//


#ifndef WEAPON_RPG_H
#define WEAPON_RPG_H
#ifdef _WIN32
#pragma once
#endif


#include "weapon_hl2mpbase.h"

#ifdef CLIENT_DLL
#include "iviewrender_beams.h"
#include "c_smoke_trail.h"
#endif

#ifndef CLIENT_DLL
#include "smoke_trail.h"
#include "Sprite.h"
#include "npcevent.h"
#include "beam_shared.h"
#include "hl1_basegrenade.h"

class CHL1MPWeaponRPG;

//###########################################################################
//	CRpgRocket
//###########################################################################
class CRpgRocket : public CHL1BaseGrenade
{
	DECLARE_CLASS( CRpgRocket, CHL1BaseGrenade );
	DECLARE_SERVERCLASS();

public:
	CRpgRocket();

	Class_T Classify( void ) { return CLASS_NONE; }
	
	void Spawn( void );
	void Precache( void );
	void RocketTouch( CBaseEntity *pOther );
	void IgniteThink( void );
	void SeekThink( void );

	virtual void Detonate( void );

	static CRpgRocket *Create( const Vector &vecOrigin, const QAngle &angAngles, CBasePlayer *pentOwner = NULL );

	CHandle<CHL1MPWeaponRPG>		m_hOwner;
	float					m_flIgniteTime;
	int						m_iTrail;
	
	DECLARE_DATADESC();
};


#endif

#ifdef CLIENT_DLL
#define CHL1MPLaserDot C_HL1MPLaserDot
#endif

class CHL1MPLaserDot;

#ifdef CLIENT_DLL
#define CHL1MPWeaponRPG C_HL1MPWeaponRPG
#endif

//-----------------------------------------------------------------------------
// CHL1MPWeaponRPG
//-----------------------------------------------------------------------------
class CHL1MPWeaponRPG : public CWeaponHL2MPBase
{
	DECLARE_CLASS( CHL1MPWeaponRPG, CWeaponHL2MPBase );
public:

	CHL1MPWeaponRPG( void );
	~CHL1MPWeaponRPG();

	void	ItemPostFrame( void );
	void	Precache( void );
	bool	Deploy( void );
	void	PrimaryAttack( void );
	void	WeaponIdle( void );
	bool	Holster( CBaseCombatWeapon *pSwitchingTo = NULL );
	void	NotifyRocketDied( void );
	bool	Reload( void );

	void	Drop( const Vector &vecVelocity );

	virtual int	GetDefaultClip1( void ) const;

//	DECLARE_SERVERCLASS();
	DECLARE_DATADESC();
	DECLARE_NETWORKCLASS();
	DECLARE_PREDICTABLE();

private:
	void	CreateLaserPointer( void );
	void	UpdateSpot( void );
	bool	IsGuiding( void );
	void	StartGuiding( void );
	void	StopGuiding( void );

#ifndef CLIENT_DLL
//	DECLARE_ACTTABLE();
#endif

private:
//	bool				m_bGuiding;
//	CHandle<CHL1MPLaserDot>	m_hHL1MPLaserDot;
//	CHandle<CRpgRocket>	m_hMissile;
//	bool				m_bIntialStateUpdate;
//	bool				m_bHL1MPLaserDotSuspended;
//	float				m_flHL1MPLaserDotReviveTime;

	CNetworkVar( bool, m_bGuiding );
	CNetworkVar( bool, m_bIntialStateUpdate );
	CNetworkVar( bool, m_bHL1MPLaserDotSuspended );
	CNetworkVar( float, m_flHL1MPLaserDotReviveTime );

	CNetworkHandle( CBaseEntity, m_hMissile );

#ifndef CLIENT_DLL
	CHandle<CHL1MPLaserDot>	m_hHL1MPLaserDot;
#endif
};


#endif	// WEAPON_RPG_H

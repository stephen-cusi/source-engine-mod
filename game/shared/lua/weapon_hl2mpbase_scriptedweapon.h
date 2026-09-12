//========= Copyright ? 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=============================================================================//

#include "weapon_hl2mpbasehlmpcombatweapon.h"
#include "luamanager.h"

#ifndef BASEHLCOMBATWEAPON_H
#define BASEHLCOMBATWEAPON_H
#ifdef _WIN32
#pragma once
#endif

#if defined( CLIENT_DLL )
	#define CHL2MPScriptedWeapon C_HL2MPScriptedWeapon
#endif

//=========================================================
// Scripted weapon base class
//=========================================================
class CHL2MPScriptedWeapon : public CBaseHL2MPCombatWeapon
{
public:
	DECLARE_CLASS( CHL2MPScriptedWeapon, CWeaponHL2MPBase );
	DECLARE_DATADESC();

	CHL2MPScriptedWeapon();
	~CHL2MPScriptedWeapon();

	bool			IsScripted( void ) const { return true; }
	
	DECLARE_NETWORKCLASS(); 
	DECLARE_PREDICTABLE();
	// DECLARE_ACTTABLE();

	acttable_t m_acttable[LUA_MAX_WEAPON_ACTIVITIES];
	acttable_t *ActivityList( void );
	int ActivityListCount( void );

	void			Precache( void );
	void			InitScriptedWeapon( void );

	void	PrimaryAttack( void );
	void	SecondaryAttack( void );

	// Firing animations
	virtual Activity		GetDrawActivity( void );
	virtual bool			SendWeaponAnim( int iActivity );

	// Default calls through to m_hOwner, but plasma weapons can override and shoot projectiles here.
	virtual void	ItemPostFrame( void );
	virtual void	ItemBusyFrame( void );
	virtual void	FireBullets( const FireBulletsInfo_t &info );
	virtual bool	Reload();

	// HL2SB GMod compat: SWEP:DoImpactEffect( trace, damageType ) -- the impact
	// effect of every scripted weapon (weapon_nyangun's util.Effect(
	// "rb655_nyan_bounce" ) lives there).  Both are OVERRIDES of virtuals that
	// CBaseEntity already declares, so the class layout and the vtable slot
	// count are unchanged -- deliberately no data members were added for the
	// tracer name (see GetTracerType in the .cpp: it reads the weapon's own Lua
	// table, which the FireBullets binding writes).
	virtual void	DoImpactEffect( trace_t &tr, int nDamageType );
	virtual const char *GetTracerType( void );


	virtual bool	Deploy( void );
	virtual bool	Holster( CBaseCombatWeapon *pSwitchingTo );

#ifdef CLIENT_DLL
	virtual void	OnDataChanged( DataUpdateType_t updateType );
	virtual const char *GetScriptedClassname( void );
#endif

	virtual const Vector &GetBulletSpread( void );

public:

	// Weapon info accessors for data in the weapon's data file
	CHL2MPSWeaponInfo *m_pLuaWeaponInfo;
	virtual const FileWeaponInfo_t	&GetWpnData( void ) const;
	virtual const char		*GetViewModel( int viewmodelindex = 0 ) const;
	virtual const char		*GetWorldModel( void ) const;
	virtual const char		*GetAnimPrefix( void ) const;
	virtual bool			UseHands( void ) const;
	virtual int				GetMaxClip1( void ) const;
	virtual int				GetMaxClip2( void ) const;
	virtual int				GetDefaultClip1( void ) const;
	virtual int				GetDefaultClip2( void ) const;
	virtual int				GetWeight( void ) const;
	virtual bool			AllowsAutoSwitchTo( void ) const;
	virtual bool			AllowsAutoSwitchFrom( void ) const;
	virtual int				GetWeaponFlags( void ) const;
	virtual int				GetSlot( void ) const;
	virtual bool 			IsSpawnable( void ) const;
	virtual bool  			DrawAmmo() const;
#ifdef CLIENT_DLL
	virtual int 			GetFOV( void ) const;
#endif
	virtual int				GetPosition( void ) const;
	virtual char const		*GetPrintName( void ) const;
	// HL2SB GMod SWEP compat: SWEP.IconOverride / SWEP.WepSelectIcon, read by
	// the weapon selection HUD.  Deliberately NOT virtual - adding a slot to
	// CBaseCombatWeapon's vtable would silently mis-dispatch in every object
	// file waf does not recompile (it does not track header changes).
	char const				*GetWepSelectIcon( void ) const;
	bool					IsMeleeWeapon() const;

public:
// Server Only Methods
#if !defined( CLIENT_DLL )

	virtual int				CapabilitiesGet( void );

// Client only methods
#else

	// Returns the aiment render origin + angles
	virtual int				DrawModel( int flags );

#endif // End client-only methods

private:
	
	CHL2MPScriptedWeapon( const CHL2MPScriptedWeapon & );

	CNetworkString( m_iScriptedClassname, MAX_WEAPON_STRING );

};

void RegisterScriptedWeapon( const char *szClassname );
void ResetWeaponFactoryDatabase( void );

#endif // BASEHLCOMBATWEAPON_H

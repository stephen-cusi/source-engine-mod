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

	// wiki: WEAPON:NPCShoot_Primary( shootPos, shootDir ) / NPCShoot_Secondary
	// -- "called internally during TASK_RANGE_ATTACK1 -> OnRangeAttack1".
	// Override of the EXISTING CBaseCombatWeapon::Operator_ForceNPCFire
	// virtual, same vtable slot.  NPC fire is server-only.
#ifndef CLIENT_DLL
	virtual void			Operator_ForceNPCFire( CBaseCombatCharacter *pOperator, bool bSecondary );
#endif

	// wiki: WEAPON:TranslateActivity( act ) -> act -- per-weapon activity
	// translation, layered in FRONT of the existing m_acttable lookup
	// (falls back to BaseClass::ActivityOverride when the script does not
	// answer).  Override of the EXISTING CBaseCombatWeapon::ActivityOverride
	// virtual, same vtable slot.
	virtual Activity		ActivityOverride( Activity baseAct, bool *pRequired );

	// wiki: WEAPON:OnRestore() -- "called when the weapon entity is reloaded
	// from a Source Engine save ... or on a changelevel".  Override of the
	// EXISTING CBaseCombatWeapon::OnRestore virtual, same vtable slot.
	virtual void			OnRestore( void );

	// wiki: WEAPON:AcceptInput( inputName, activator, caller, data ) ->
	// boolean.  Override of the EXISTING CBaseEntity::AcceptInput virtual
	// (inherited through CBaseCombatWeapon), same vtable slot.  Server only:
	// entity I/O never reaches the client.
#ifndef CLIENT_DLL
	virtual bool			AcceptInput( const char *szInputName, CBaseEntity *pActivator, CBaseEntity *pCaller, variant_t Value, int outputID );
#endif

	// HL2SB: GMod's SWEP:Equip( newOwner ) -- override of the EXISTING
	// CBaseCombatWeapon::Equip virtual (same vtable slot, no layout change).
	virtual void			Equip( CBaseCombatCharacter *pOwner );

	// Default calls through to m_hOwner, but plasma weapons can override and shoot projectiles here.
	// HL2SB: GMod's client-side weapon view hooks (SWEP:TranslateFOV / SWEP:CalcView).
	// Deliberately NON-virtual: waf does not track header changes (AGENTS.md 5.0), so
	// adding virtuals here would shift vtable slots for every translation unit that
	// includes this header but does not get rebuilt.  The call site reaches them
	// through IsScripted() + static_cast instead.
	float	TranslateFOV( float flFOV );
	void	CalcView( CBasePlayer *pPlayer, Vector &vecOrigin, QAngle &vecAngles, float &flFOV );

	// HL2SB: GMod's weapon input/HUD hooks, same NON-virtual contract as the
	// pair above.  Call sites: CInput::MouseMove (in_mouse.cpp) for
	// FreezeMovement / AdjustMouseSensitivity, CHudElement::ShouldDraw
	// (hud.cpp) for HUDShouldDraw -- all through IsScripted() + static_cast.
	bool	DispatchFreezeMovement( void );
	// GMod's signature: WEAPON:AdjustMouseSensitivity( defaultSensitivity, localFOV, defaultFOV )
	// (wiki: defaultSensitivity "in general this will be 0"; localFOV is the
	// player's current FOV; defaultFOV the player's default FOV).  Addons do
	// arithmetic on all three, so they must always arrive as numbers.
	float	DispatchAdjustMouseSensitivity( float flDefaultSensitivity, float flLocalFOV, float flDefaultFOV );
	bool	DispatchHUDShouldDraw( const char *pszElementName );

	virtual void	ItemPostFrame( void );
	virtual void	ItemBusyFrame( void );
	virtual void	FireBullets( const FireBulletsInfo_t &info );
	virtual bool	Reload();

	// HL2SB (2026-09-30): GMod parity -- Lua SWEPs get NO idle management at
	// all.  GMod's own weapon_base has an EMPTY Think() and never re-sends
	// ACT_VM_IDLE; the viewmodel just holds the pose the script sent, and a
	// SWEP that wants an idle cycle re-sends it itself (weapon_nyangun's
	// Think timer).
	//
	// This MUST be a real override, not just "don't call WeaponIdle() from
	// our own ItemPostFrame": CHL2MPScriptedWeapon's actual base class is
	// CBaseHL2MPCombatWeapon, which has its OWN WeaponIdle() override
	// (weapon_hl2mpbasehlmpcombatweapon.cpp) that unconditionally re-sends
	// ACT_VM_IDLE once HasWeaponIdleTimeElapsed() -- shared code, so it runs
	// on the CLIENT's predicted ItemPostFrame too, completely independent of
	// this class's own server-side ItemPostFrame.  Without an override here
	// that inherited implementation still ran every predicted client frame
	// and force-restarted the viewmodel each time (SendWeaponAnim always
	// restarts on this fork) -- the reported per-frame twitch, worse right
	// after a vehicle exit because that is what next reset the idle timer to
	// something already elapsed.  Empty override, same vtable slot.
	virtual void	WeaponIdle( void ) { }

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

	// HL2SB GMod compat (wiki-documented WEAPON hooks, NPC fire line):
	// Weapon:GetNPCBulletSpread( proficiency ), Weapon:GetNPCBurstSettings()
	// and Weapon:GetNPCRestTimes() feed the SDK's shot regulator
	// (CAI_BaseNPC::OnUpdateShotRegulator reads GetFireRate/GetMinBurst/
	// GetMaxBurst/GetMinRestTime/GetMaxRestTime).  All are OVERRIDES of
	// virtuals CBaseCombatWeapon already declares (basecombatweapon_shared.h),
	// so no new vtable slots are introduced.  Server-only: the regulator is
	// AI-side; on the client the base implementations answer.  (The override
	// DECLARATION is guarded with the implementation -- an unconditional
	// declaration with a server-only definition is an unresolved-external
	// link error on the client.)
#ifndef CLIENT_DLL
	virtual Vector			GetBulletSpread( WeaponProficiency_t proficiency );
	virtual float			GetFireRate( void );
	virtual int				GetMinBurst( void );
	virtual int				GetMaxBurst( void );
	virtual float			GetMinRestTime( void );
	virtual float			GetMaxRestTime( void );
#endif

	// wiki: WEAPON:ShouldDropOnDie() -- "Return true to drop the weapon,
	// false otherwise."  The vote is three-valued because the engine's own
	// death-drop rule (gamerules DeadPlayerWeapons -> the active weapon)
	// also has a say:
	//   1  hook explicitly returned true (drop)
	//   0  hook explicitly returned false (veto -- delete with the rest)
	//  -1  no hook / no boolean answer (no opinion; the rule decides)
	// Server only (the death drop runs in CBasePlayer::PackDeadPlayerItems).
#ifndef CLIENT_DLL
	int		DispatchShouldDropOnDieVote( void );
#endif

#ifdef CLIENT_DLL
	// wiki: WEAPON:DrawWeaponSelection( x, y, wide, tall, alpha ) -- a
	// scripted weapon that defines the hook draws the selected box's icon
	// area itself.  Returns true when the hook ran (the caller then skips
	// the engine icon path).
	bool	DispatchDrawWeaponSelection( int x, int y, int wide, int tall, int alpha );

	// wiki: WEAPON:CustomAmmoDisplay() -> table { Draw, PrimaryClip,
	// PrimaryAmmo, ... }.  Writes back only the fields present; returns true
	// when the hook supplied a table at all.
	bool	DispatchCustomAmmoDisplay( bool *pbDraw, int *pnPrimaryClip, int *pnPrimaryAmmo );
#endif

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

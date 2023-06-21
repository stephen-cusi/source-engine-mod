//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=============================================================================//

#ifndef WEAPON_HL2MPBASE_H
#define WEAPON_HL2MPBASE_H
#ifdef _WIN32
#pragma once
#endif

#ifdef CLIENT_DLL
#include "c_hl2mp_player.h"
#undef C_WeaponHL2MPBase
#else
#include "hl2mp_player.h"

#endif

#include "cs_weapon_parse.h"
#include "cs_playeranimstate.h"
#include "hl2mp_player_shared.h"
#include "basecombatweapon_shared.h"


#if defined( CLIENT_DLL )
	//#define CWeaponHL2MPBase C_WeaponHL2MPBase
	void UTIL_ClipPunchAngleOffset( QAngle &in, const QAngle &punch, const QAngle &clip );
#endif


extern CSWeaponID AliasToWeaponID( const char *alias );
extern const char *WeaponIDToAlias( int id );
extern const char *GetTranslatedWeaponAlias( const char *alias);
extern const char * GetWeaponAliasFromTranslated(const char *translatedAlias);
extern bool	IsPrimaryWeapon( CSWeaponID id );
extern bool IsSecondaryWeapon( CSWeaponID  id );
extern int GetShellForAmmoType( const char *ammoname );

#define SHIELD_VIEW_MODEL "models/weapons/v_shield.mdl"
#define SHIELD_WORLD_MODEL "models/weapons/w_shield.mdl"


class CHL2MP_Player;



#define BULLET_PLAYER_50AE		"BULLET_PLAYER_50AE"
#define BULLET_PLAYER_762MM		"BULLET_PLAYER_762MM"
#define BULLET_PLAYER_556MM		"BULLET_PLAYER_556MM"
#define BULLET_PLAYER_556MM_BOX	"BULLET_PLAYER_556MM_BOX"
#define BULLET_PLAYER_338MAG	"BULLET_PLAYER_338MAG"
#define BULLET_PLAYER_9MM		"BULLET_PLAYER_9MM"
#define BULLET_PLAYER_BUCKSHOT	"BULLET_PLAYER_BUCKSHOT"
#define BULLET_PLAYER_45ACP		"BULLET_PLAYER_45ACP"
#define BULLET_PLAYER_357SIG	"BULLET_PLAYER_357SIG"
#define BULLET_PLAYER_57MM		"BULLET_PLAYER_57MM"
#define AMMO_TYPE_HEGRENADE		"AMMO_TYPE_HEGRENADE"
#define AMMO_TYPE_FLASHBANG		"AMMO_TYPE_FLASHBANG"
#define AMMO_TYPE_SMOKEGRENADE	"AMMO_TYPE_SMOKEGRENADE"

#define CROSSHAIR_CONTRACT_PIXELS_PER_SECOND	7.0f


enum CSWeaponMode
{
	Primary_Mode = 0,
	Secondary_Mode,
	WeaponMode_MAX
};

#if defined( CLIENT_DLL )

//--------------------------------------------------------------------------------------------------------------
/**
*  Returns the client's ID_* value for the currently owned weapon, or ID_NONE if no weapon is owned
*/
CSWeaponID GetClientWeaponID(bool primary);

#endif

//--------------------------------------------------------------------------------------------------------------
CCSWeaponInfo* GetWeaponInfo(CSWeaponID weaponID);



// These are the names of the ammo types that go in the CAmmoDefs and that the 
// weapon script files reference.

// Given an ammo type (like from a weapon's GetPrimaryAmmoType()), this compares it
// against the ammo name you specify.
// MIKETODO: this should use indexing instead of searching and strcmp()'ing all the time.
bool IsAmmoType( int iAmmoType, const char *pAmmoName );

class CWeaponHL2MPBase : public CBaseCombatWeapon
{
public:
	DECLARE_CLASS( CWeaponHL2MPBase, CBaseCombatWeapon );
	DECLARE_NETWORKCLASS(); 
	DECLARE_PREDICTABLE();

	CWeaponHL2MPBase();

	#ifdef GAME_DLL
		DECLARE_DATADESC();

		//void SendReloadSoundEvent( void );

		virtual void Materialize( void );
		virtual	int ObjectCaps( void );
		virtual	void FallThink( void );

		virtual void CheckRespawn();
		virtual CBaseEntity* Respawn();

		virtual const Vector& GetBulletSpread();
		virtual Vector	GetBulletSpread(WeaponProficiency_t proficiency) { return GetBulletSpread(); }
		virtual float	GetDefaultAnimSpeed();

		virtual void	BulletWasFired(const Vector& vecStart, const Vector& vecEnd);
		virtual bool	ShouldRemoveOnRoundRestart();

		//=============================================================================
		// HPE_BEGIN:
		// [dwenger] Handle round restart processing for the weapon.
		//=============================================================================

		virtual void    OnRoundRestart();

		//=============================================================================
		// HPE_END
		//=============================================================================
		void SendReloadEvents();
		virtual bool	DefaultReload(int iClipSize1, int iClipSize2, int iActivity);

		void AttemptToMaterialize();
		virtual void Use(CBaseEntity* pActivator, CBaseEntity* pCaller, USE_TYPE useType, float value);

		virtual bool IsRemoveable();

	#endif


	virtual bool	Holster( CBaseCombatWeapon *pSwitchingTo );

	// Pistols reset m_iShotsFired to 0 when the attack button is released.
	bool			IsPistol() const;

	virtual bool IsFullAuto() const;

	virtual float GetMaxSpeed() const;	// What's the player's max speed while holding this weapon.

	// Get CS-specific weapon data.
	CCSWeaponInfo const	&GetCSWpnData() const;

	// Get specific CS weapon ID (ie: WEAPON_AK47, etc)
	virtual CSWeaponID GetWeaponID( void ) const		{ return WEAPON_NONE; }

	// return true if this weapon is an instance of the given weapon type (ie: "IsA" WEAPON_GLOCK)
	bool IsA( CSWeaponID id ) const						{ return GetWeaponID() == id; }

	// return true if this weapon is a kinf of the given weapon type (ie: "IsKindOf" WEAPONTYPE_RIFLE )
	bool IsKindOf( CSWeaponType type ) const			{ return GetCSWpnData().m_WeaponType == type; }

	// return true if this weapon has a silencer equipped
	virtual bool IsSilenced( void ) const				{ return false; }

	virtual void SetWeaponModelIndex( const char *pName );
	virtual void OnPickedUp( CBaseCombatCharacter *pNewOwner );

	virtual void OnJump( float fImpulse );
	virtual void OnLand( float fVelocity );






	// All predicted weapons need to implement and return true
	virtual bool	IsPredicted() const;

	CBasePlayer* GetPlayerOwner() const;
	CHL2MP_Player* GetHL2MPPlayerOwner() const;

	void WeaponSound( WeaponSound_t sound_type, float soundtime = 0.0f );
	
	CCSWeaponInfo const	&GetHL2MPWpnData() const;


	virtual void FireBullets( const FireBulletsInfo_t &info );
	virtual void FallInit( void );
	
public:
	#if defined( CLIENT_DLL )
		
		virtual bool	ShouldPredict();
		virtual void	OnDataChanged( DataUpdateType_t type );

		virtual bool	OnFireEvent( C_BaseViewModel *pViewModel, const Vector& origin, const QAngle& angles, int event, const char *options );


	#endif

	float		m_flPrevAnimTime;
	float  m_flNextResetCheckTime;

	Vector	GetOriginalSpawnOrigin( void ) { return m_vOriginalSpawnOrigin;	}
	QAngle	GetOriginalSpawnAngles( void ) { return m_vOriginalSpawnAngles;	}

#if defined( CLIENT_DLL )

	virtual void	ProcessMuzzleFlashEvent();
	virtual void	DrawCrosshair();

	virtual int		GetMuzzleAttachment(void);
	virtual bool	HideViewModelWhenZoomed(void) { return true; }

	float			m_flCrosshairDistance;
	int				m_iAmmoLastCheck;
	int				m_iAlpha;
	int				m_iScopeTextureID;
	int				m_iCrosshairTextureID; // for white additive texture

	virtual int GetMuzzleFlashStyle(void);

#else

	virtual bool	Reload();
	virtual void	Spawn();
	virtual bool	KeyValue(const char* szKeyName, const char* szValue);

	virtual bool PhysicsSplash(const Vector& centerPoint, const Vector& normal, float rawSpeed, float scaledSpeed);

#endif

	bool IsUseable();
	virtual bool	CanDeploy(void);
	virtual void	UpdateShieldState(void);
	virtual bool	SendWeaponAnim(int iActivity);
	virtual void	SecondaryAttack(void);
	virtual void	Precache(void);
	virtual bool	CanBeSelected(void);
	virtual Activity GetDeployActivity(void);
	virtual bool	DefaultDeploy(char* szViewModel, char* szWeaponModel, int iActivity, char* szAnimExt);
	virtual void 	DefaultTouch(CBaseEntity* pOther);	// default weapon touch
	virtual bool	DefaultPistolReload();

	virtual bool	Deploy();
	virtual void	Drop(const Vector& vecVelocity);
	bool PlayEmptySound();
	virtual void	ItemPostFrame();
	virtual void	ItemBusyFrame();
	virtual const char* GetViewModel() const;


	bool	m_bDelayFire;			// This variable is used to delay the time between subsequent button pressing.
	float	m_flAccuracy;

	//=============================================================================
	// HPE_BEGIN:
	// [pfreese] new accuracy model
	//=============================================================================

	CNetworkVar(CSWeaponMode, m_weaponMode);

	virtual float GetInaccuracy(void) const;
	virtual float GetSpread(void) const;

	virtual void UpdateAccuracyPenalty();

	CNetworkVar(float, m_fAccuracyPenalty);

	//=============================================================================
	// HPE_END
	//=============================================================================

	void SetExtraAmmoCount(int count) { m_iExtraPrimaryAmmo = count; }
	int GetExtraAmmoCount(void) { return m_iExtraPrimaryAmmo; }

	//=============================================================================
	// HPE_BEGIN:	
	//=============================================================================

	// [tj] Accessors for the previous owner of the gun
	void SetPreviousOwner(CHL2MP_Player* player) { m_prevOwner = player; }
	CHL2MP_Player* GetPreviousOwner() { return m_prevOwner; }

	// [tj] Accessors for the donor system
	void SetDonor(CHL2MP_Player* player) { m_donor = player; }
	CHL2MP_Player* GetDonor() { return m_donor; }
	void SetDonated(bool donated) { m_donated = true; }
	bool GetDonated() { return m_donated; }

	//[dwenger] Accessors for the prior owner list
	void AddToPriorOwnerList(CHL2MP_Player* pPlayer);
	bool IsAPriorOwner(CHL2MP_Player* pPlayer);

	//=============================================================================
	// HPE_END
	//=============================================================================

	void EjectShell(CBaseEntity* pPlayer, int iType);

protected:

	float	CalculateNextAttackTime(float flCycleTime);

private:

	float	m_flDecreaseShotsFired;

	CWeaponHL2MPBase(const CWeaponHL2MPBase&);

	int		m_iExtraPrimaryAmmo;

	float	m_nextPrevOwnerTouchTime;
	CHL2MP_Player* m_prevOwner;

	int m_iDefaultExtraAmmo;

	//=============================================================================
	// HPE_BEGIN:
	//=============================================================================

	// [dwenger] track all prior owners of this weapon
	CUtlVector< CHL2MP_Player* >    m_PriorOwners;

	// [tj] To keep track of people who drop weapons for teammates during the buy round
	CHandle<CHL2MP_Player> m_donor;
	bool m_donated;

	//=============================================================================
	// HPE_END
	//=============================================================================






private:


	Vector m_vOriginalSpawnOrigin;
	QAngle m_vOriginalSpawnAngles;
};
extern ConVar weapon_accuracy_model;

#endif // WEAPON_HL2MPBASE_H

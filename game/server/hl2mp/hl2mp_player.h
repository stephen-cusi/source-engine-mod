//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//
//=============================================================================//
#ifndef HL2MP_PLAYER_H
#define HL2MP_PLAYER_H
#pragma once
class CHL2MP_Player;

#include "basemultiplayerplayer.h"
#include "hl2_playerlocaldata.h"
#include "hl2_player.h"
#include "simtimer.h"
#include "soundenvelope.h"
#include "hl2mp_player_shared.h"
#include "hl2mp_gamerules.h"
#include "utldict.h"
#include "prop_portal.h"
#include "func_liquidportal.h"
#include "cs_shareddefs.h"
#include "basehlcombatweapon_shared.h"
#include "cs_autobuy.h"
#include "cs_weapon_parse.h"
#include "cs_playeranimstate.h"
struct PortalPlayerStatistics_t
{
	int iNumPortalsPlaced;
	int iNumStepsTaken;
	float fNumSecondsTaken;
};

void UTIL_AwardMoneyToTeam(int iAmount, int iTeam, CBaseEntity* pIgnore);

#define MENU_STRING_BUFFER_SIZE	1024
#define MENU_MSG_TEXTCHUNK_SIZE	50

enum
{
	MIN_NAME_CHANGE_INTERVAL = 10,			// minimum number of seconds between name changes
	NAME_CHANGE_HISTORY_SIZE = 5,			// number of times a player can change names in NAME_CHANGE_HISTORY_INTERVAL
	NAME_CHANGE_HISTORY_INTERVAL = 600,	// no more than NAME_CHANGE_HISTORY_SIZE name changes can be made in this many seconds
};

extern ConVar bot_mimic;




//=======================================
//Record of either damage taken or given.
//Contains the player name that we hurt or that hurt us,
//and the total damage
//=======================================
class CDamageRecord
{
public:
	CDamageRecord(const char* name, int iDamage, int iCounter)
	{
		Q_strncpy(m_szPlayerName, name, sizeof(m_szPlayerName));
		m_iDamage = iDamage;
		m_iNumHits = 1;
		m_iLastBulletUpdate = iCounter;
	}

	void AddDamage(int iDamage, int iCounter)
	{
		m_iDamage += iDamage;

		if (m_iLastBulletUpdate != iCounter)
			m_iNumHits++;

		m_iLastBulletUpdate = iCounter;
	}

	char* GetPlayerName(void) { return m_szPlayerName; }
	int GetDamage(void) { return m_iDamage; }
	int GetNumHits(void) { return m_iNumHits; }

private:
	char m_szPlayerName[MAX_PLAYER_NAME_LENGTH];
	int m_iDamage;		//how much damage was done
	int m_iNumHits;		//how many hits
	int	m_iLastBulletUpdate; // update counter
};

// Message display history (CHL2MP_Player::m_iDisplayHistoryBits)
// These bits are set when hint messages are displayed, and cleared at
// different times, according to the DHM_xxx bitmasks that follow

#define DHF_ROUND_STARTED		( 1 << 1 )
#define DHF_HOSTAGE_SEEN_FAR	( 1 << 2 )
#define DHF_HOSTAGE_SEEN_NEAR	( 1 << 3 )
#define DHF_HOSTAGE_USED		( 1 << 4 )
#define DHF_HOSTAGE_INJURED		( 1 << 5 )
#define DHF_HOSTAGE_KILLED		( 1 << 6 )
#define DHF_FRIEND_SEEN			( 1 << 7 )
#define DHF_ENEMY_SEEN			( 1 << 8 )
#define DHF_FRIEND_INJURED		( 1 << 9 )
#define DHF_FRIEND_KILLED		( 1 << 10 )
#define DHF_ENEMY_KILLED		( 1 << 11 )
#define DHF_BOMB_RETRIEVED		( 1 << 12 )
#define DHF_AMMO_EXHAUSTED		( 1 << 15 )
#define DHF_IN_TARGET_ZONE		( 1 << 16 )
#define DHF_IN_RESCUE_ZONE		( 1 << 17 )
#define DHF_IN_ESCAPE_ZONE		( 1 << 18 ) // unimplemented
#define DHF_IN_VIPSAFETY_ZONE	( 1 << 19 ) // unimplemented
#define	DHF_NIGHTVISION			( 1 << 20 )
#define	DHF_HOSTAGE_CTMOVE		( 1 << 21 )
#define	DHF_SPEC_DUCK			( 1 << 22 )

// DHF_xxx bits to clear when the round restarts

#define DHM_ROUND_CLEAR ( \
	DHF_ROUND_STARTED | \
	DHF_HOSTAGE_KILLED | \
	DHF_FRIEND_KILLED | \
	DHF_BOMB_RETRIEVED )


// DHF_xxx bits to clear when the player is restored

#define DHM_CONNECT_CLEAR ( \
	DHF_HOSTAGE_SEEN_FAR | \
	DHF_HOSTAGE_SEEN_NEAR | \
	DHF_HOSTAGE_USED | \
	DHF_HOSTAGE_INJURED | \
	DHF_FRIEND_SEEN | \
	DHF_ENEMY_SEEN | \
	DHF_FRIEND_INJURED | \
	DHF_ENEMY_KILLED | \
	DHF_AMMO_EXHAUSTED | \
	DHF_IN_TARGET_ZONE | \
	DHF_IN_RESCUE_ZONE | \
	DHF_IN_ESCAPE_ZONE | \
	DHF_IN_VIPSAFETY_ZONE | \
	DHF_HOSTAGE_CTMOVE | \
	DHF_SPEC_DUCK )

// radio messages (these must be kept in sync with actual radio) -------------------------------------
enum RadioType
{
	RADIO_INVALID = 0,

	RADIO_START_1,							///< radio messages between this and RADIO_START_2 and part of Radio1()

	RADIO_COVER_ME,
	RADIO_YOU_TAKE_THE_POINT,
	RADIO_HOLD_THIS_POSITION,
	RADIO_REGROUP_TEAM,
	RADIO_FOLLOW_ME,
	RADIO_TAKING_FIRE,

	RADIO_START_2,							///< radio messages between this and RADIO_START_3 are part of Radio2()

	RADIO_GO_GO_GO,
	RADIO_TEAM_FALL_BACK,
	RADIO_STICK_TOGETHER_TEAM,
	RADIO_GET_IN_POSITION_AND_WAIT,
	RADIO_STORM_THE_FRONT,
	RADIO_REPORT_IN_TEAM,

	RADIO_START_3,							///< radio messages above this are part of Radio3()

	RADIO_AFFIRMATIVE,
	RADIO_ENEMY_SPOTTED,
	RADIO_NEED_BACKUP,
	RADIO_SECTOR_CLEAR,
	RADIO_IN_POSITION,
	RADIO_REPORTING_IN,
	RADIO_GET_OUT_OF_THERE,
	RADIO_NEGATIVE,
	RADIO_ENEMY_DOWN,

	RADIO_END,

	RADIO_NUM_EVENTS
};

extern const char* RadioEventName[RADIO_NUM_EVENTS + 1];

/**
 * Convert name to RadioType
 */
extern RadioType NameToRadioEvent(const char* name);

enum BuyResult_e
{
	BUY_BOUGHT,
	BUY_ALREADY_HAVE,
	BUY_CANT_AFFORD,
	BUY_PLAYER_CANT_BUY,	// not in the buy zone, is the VIP, is past the timelimit, etc
	BUY_NOT_ALLOWED,		// weapon is restricted by VIP mode, team, etc
	BUY_INVALID_ITEM,
};

//=============================================================================
// HPE_BEGIN:
//=============================================================================

// [tj] The phases for the "Goose Chase" achievement
enum GooseChaseAchievementStep
{
	GC_NONE,
	GC_SHOT_DURING_DEFUSE,
	GC_STOPPED_AFTER_GETTING_SHOT
};

// [tj] The phases for the "Defuse Defense" achievement
enum DefuseDefenseAchivementStep
{
	DD_NONE,
	DD_STARTED_DEFUSE,
	DD_KILLED_TERRORIST
};


//=============================================================================
// >> HL2MP_Player
//=============================================================================
class CHL2MPPlayerStateInfo
{
public:
	CSPlayerState m_iPlayerState;
	const char *m_pStateName;

	void (CHL2MP_Player::*pfnEnterState)();	// Init and deinit the state.
	void (CHL2MP_Player::*pfnLeaveState)();

	void (CHL2MP_Player::*pfnPreThink)();	// Do a PreThink() in this state.
};

class CHL2MP_Player : public CHL2_Player
{
public:
	DECLARE_CLASS( CHL2MP_Player, CHL2_Player );

	CHL2MP_Player();
	~CHL2MP_Player( void );
	
	static CHL2MP_Player *CreatePlayer( const char *className, edict_t *ed )
	{
		CHL2MP_Player::s_PlayerEdict = ed;
		return (CHL2MP_Player*)CreateEntityByName( className );
	}
	static CHL2MP_Player* Instance(int iEnt);

	DECLARE_SERVERCLASS();
	DECLARE_DATADESC();

	virtual void InputKill(void) { return; }

	virtual void Precache( void );
	virtual void Spawn( void );
	virtual void PostThink( void );
	virtual void PreThink( void );
	virtual void PlayerDeathThink( void );
	virtual void SetAnimation( PLAYER_ANIM playerAnim );
	virtual bool HandleCommand_JoinTeam( int team );
	virtual bool ClientCommand( const CCommand &args );
	virtual void CreateViewModel( int viewmodelindex = 0 );
	virtual bool BecomeRagdollOnClient( const Vector &force );
	virtual void Event_Killed( const CTakeDamageInfo &info );
	virtual int OnTakeDamage( const CTakeDamageInfo &inputInfo );
	virtual bool WantsLagCompensationOnEntity( const CBasePlayer *pPlayer, const CUserCmd *pCmd, const CBitVec<MAX_EDICTS> *pEntityTransmitBits ) const;
	virtual void FireBullets ( const FireBulletsInfo_t &info );
	virtual bool Weapon_Switch( CBaseCombatWeapon *pWeapon, int viewmodelindex = 0);
	virtual bool BumpWeapon( CBaseCombatWeapon *pWeapon );
	virtual void ChangeTeam( int iTeam );
	virtual void PickupObject ( CBaseEntity *pObject, bool bLimitMassAndSize );
	virtual void PlayStepSound( Vector &vecOrigin, surfacedata_t *psurface, float fvol, bool force );
	virtual void Weapon_Drop( CBaseCombatWeapon *pWeapon, const Vector *pvecTarget = NULL, const Vector *pVelocity = NULL );
	virtual void UpdateOnRemove( void );
	virtual void DeathSound( const CTakeDamageInfo &info );
	virtual CBaseEntity* EntSelectSpawnPoint( void );
		
	int FlashlightIsOn( void );
	void FlashlightTurnOn( void );
	void FlashlightTurnOff( void );
	void	PrecacheFootStepSounds( void );
	bool	ValidatePlayerModel( const char *pModel );

	QAngle GetAnimEyeAngles( void ) { return m_angEyeAngles.Get(); }

	Vector GetAttackSpread( CBaseCombatWeapon *pWeapon, CBaseEntity *pTarget = NULL );

	void CheatImpulseCommands( int iImpulse );
	void CreateRagdollEntity( void );
	void GiveAllItems( void );
	void GiveDefaultItems( void );

	void NoteWeaponFired( void );

	void ResetAnimation( void );
	void SetPlayerModel( void );
	void SetPlayerTeamModel( void );
	Activity TranslateTeamActivity( Activity ActToTranslate );
	
	float GetNextModelChangeTime( void ) { return m_flNextModelChangeTime; }
	float GetNextTeamChangeTime( void ) { return m_flNextTeamChangeTime; }
	void  PickDefaultSpawnTeam( void );
	void  SetupPlayerSoundsByModel( const char *pModelName );
	const char *GetPlayerModelSoundPrefix( void );
	int	  GetPlayerModelType( void ) { return m_iPlayerSoundType;	}
	
	void  DetonateTripmines( void );

	void Reset();

	bool IsReady();
	void SetReady( bool bReady );

	void CheckChatText( char *p, int bufsize );

	void State_Transition( CSPlayerState newState );
	void State_Enter( CSPlayerState newState );
	void State_Leave();
	void State_PreThink();
	CHL2MPPlayerStateInfo *State_LookupInfo( CSPlayerState state );

	void State_Enter_ACTIVE();
	void State_PreThink_ACTIVE();
	void State_Enter_OBSERVER_MODE();
	void State_PreThink_OBSERVER_MODE();


	virtual bool StartObserverMode( int mode );
	virtual void StopObserverMode( void );

	void ToggleHeldObjectOnOppositeSideOfPortal(void) { m_bHeldObjectOnOppositeSideOfPortal = !m_bHeldObjectOnOppositeSideOfPortal; }
	void SetHeldObjectOnOppositeSideOfPortal(bool p_bHeldObjectOnOppositeSideOfPortal) { m_bHeldObjectOnOppositeSideOfPortal = p_bHeldObjectOnOppositeSideOfPortal; }
	bool IsHeldObjectOnOppositeSideOfPortal(void) { return m_bHeldObjectOnOppositeSideOfPortal; }
	CProp_Portal* GetHeldObjectPortal(void) { return m_pHeldObjectPortal; }
	void SetHeldObjectPortal(CProp_Portal* pPortal) { m_pHeldObjectPortal = pPortal; }

	void SetStuckOnPortalCollisionObject(void) { m_bStuckOnPortalCollisionObject = true; }

	CWeaponHL2MPBase* GetActivePortalWeapon() const;

	void IncrementPortalsPlaced(void);
	void IncrementStepsTaken(void);
	void UpdateSecondsTaken(void);
	void ResetThisLevelStats(void);
	int NumPortalsPlaced(void) const { return m_StatsThisLevel.iNumPortalsPlaced; }
	int NumStepsTaken(void) const { return m_StatsThisLevel.iNumStepsTaken; }
	float NumSecondsTaken(void) const { return m_StatsThisLevel.fNumSecondsTaken; }

	void SetNeuroToxinDamageTime(float fCountdownSeconds) { m_fNeuroToxinDamageTime = gpGlobals->curtime + fCountdownSeconds; }

	void IncNumCamerasDetatched(void) { ++m_iNumCamerasDetatched; }
	int GetNumCamerasDetatched(void) const { return m_iNumCamerasDetatched; }

	Vector m_vecTotalBulletForce;	//Accumulator for bullet force in a single frame

	// Tracks our ragdoll entity.
	CNetworkHandle( CBaseEntity, m_hRagdoll );	// networked entity handle 

	virtual bool	CanHearAndReadChatFrom( CBasePlayer *pPlayer );

	bool m_bSilentDropAndPickup;
	void SuppressCrosshair(bool bState) { m_bSuppressingCrosshair = bState; }

	virtual void UpdatePortalViewAreaBits(unsigned char* pvs, int pvssize);
	virtual void SetupVisibility(CBaseEntity* pViewEntity, unsigned char* pvs, int pvssize);
	void ForceDuckThisFrame(void);
	bool RunMimicCommand(CUserCmd& cmd);

	CNetworkVar(bool, m_bInBombZone);
	CNetworkVar(bool, m_bInBuyZone);
	int m_iBombSiteIndex;

	bool IsInBuyZone();
	bool CanPlayerBuy(bool display);

	CNetworkVar(bool, m_bInHostageRescueZone);
	void RescueZoneTouch(inputdata_t& inputdata);
	bool HasC4() const;
	bool m_bIsVIP;

	void IncrementNumFollowers() { m_iNumFollowers++; }
	void DecrementNumFollowers() { m_iNumFollowers--; if (m_iNumFollowers < 0) m_iNumFollowers = 0; }
	int GetNumFollowers() { return m_iNumFollowers; }
	void SetIsRescuing(bool in_bRescuing) { m_bIsRescuing = in_bRescuing; }
	bool IsRescuing() { return m_bIsRescuing; }
	void SetInjuredAHostage(bool in_bInjured) { m_bInjuredAHostage = in_bInjured; }
	bool InjuredAHostage() { return m_bInjuredAHostage; }
	float GetBombPickuptime() { return m_bombPickupTime; }
	void SetBombPickupTime(float time) { m_bombPickupTime = time; }
	CHL2MP_Player* GetLastFlashbangAttacker() { return m_lastFlashBangAttacker; }
	void SetLastFlashbangAttacker(CHL2MP_Player* attacker) { m_lastFlashBangAttacker = attacker; }

	static CSWeaponID GetWeaponIdCausingDamange(const CTakeDamageInfo& info);
	static void ProcessPlayerDeathAchievements(CHL2MP_Player* pAttacker, CHL2MP_Player* pVictim, const CTakeDamageInfo& info);
	unsigned int m_iDisplayHistoryBits;

	int m_NumEnemiesKilledThisRound;
	int m_NumEnemiesAtRoundStart;
	int m_KillingSpreeStartTime;

	float m_firstKillBlindStartTime; //This is the start time of the blind effect during which we got our most recent kill.
	int m_killsWhileBlind;

	bool m_bIsRescuing;         // tracks whether this player is currently rescuing a hostage
	bool m_bInjuredAHostage;    // tracks whether this player injured a hostage
	int  m_iNumFollowers;       // Number of hostages following this player
	bool m_bSurvivedHeadshotDueToHelmet;

	CWeaponHL2MPBase* GetActiveCSWeapon() const;

	void AddAccount(int amount, bool bTrackChange = true, bool bItemBought = false, const char* pItemName = NULL);

	void HintMessage(const char* pMessage, bool bDisplayIfDead, bool bOverrideClientSettings = false); // Displays a hint message to the player
	CHintMessageQueue* m_pHintMessageQueue;
	bool m_bShowHints;
	float m_flLastAttackedTeammate;
	float m_flNextMouseoverUpdate;
	void UpdateMouseoverHints();

	// mark this player as not receiving money at the start of the next round.
	void MarkAsNotReceivingMoneyNextRound();
	bool DoesPlayerGetRoundStartMoney(); // self-explanitory :)

	void DropC4();	// Get rid of the C4 bomb.

	bool HasDefuser();		// Is this player carrying a bomb defuser?
	void GiveDefuser(bool bPickedUp = false);		// give the player a defuser
	void RemoveDefuser();	// remove defuser from the player and remove the model attachment

	int                         m_lastRoundResult; //save the reason for the last round ending.

	bool                        m_bMadeFootstepNoise;

	float                       m_bombPickupTime;

	bool                        m_bMadePurchseThisRound;

	int                         m_roundsWonWithoutPurchase;

	bool						m_bKilledDefuser;
	bool						m_bKilledRescuer;
	int							m_maxGrenadeKills;

	int							m_grenadeDamageTakenThisRound;

	bool						GetKilledDefuser() { return m_bKilledDefuser; }
	bool						GetKilledRescuer() { return m_bKilledRescuer; }
	int							GetMaxGrenadeKills() { return m_maxGrenadeKills; }

	void						CheckMaxGrenadeKills(int grenadeKills);

	CHandle<CHL2MP_Player>                  m_lastFlashBangAttacker;



	bool IsReloading(void) const;

	bool HasShield() const;
	bool IsShieldDrawn() const;
	void GiveShield(void);
	void RemoveShield(void);
	bool IsProtectedByShield(void) const;		// returns true if player has a shield and is currently hidden behind it

	bool IsBlindForAchievement();	// more stringent than IsBlind; more accurately represents when the player can see again

	//=============================================================================
	// HPE_END
	//=============================================================================

	bool IsBlind(void) const;		// return true if this player is blind (from a flashbang)
	virtual void Blind(float holdTime, float fadeTime, float startingAlpha = 255);	// player blinded by a flashbang
	float m_blindUntilTime;
	float m_blindStartTime;

	void Deafen(float flDistance);		//make the player deaf / apply dsp preset to muffle sound

	void ApplyDeafnessEffect();				// apply the deafness effect for a nearby explosion.

	bool IsAutoFollowAllowed(void) const;		// return true if this player will allow bots to auto follow
	void InhibitAutoFollow(float duration);	// prevent bots from auto-following for given duration
	void AllowAutoFollow(void);				// allow bots to auto-follow immediately
	float m_allowAutoFollowTime;				// bots can auto-follow after this time

	// Have this guy speak a message into his radio.
	void Radio(const char* szRadioSound, const char* szRadioText = NULL);
	void ConstructRadioFilter(CRecipientFilter& filter);

	void EmitPrivateSound(const char* soundName);		///< emit given sound that only we can hear


	CNetworkVar(float, m_flFlashDuration);
	CNetworkVar(float, m_flFlashMaxAlpha);

	CNetworkVar(float, m_flProgressBarStartTime);
	CNetworkVar(int, m_iProgressBarDuration);
	CNetworkVar(int, m_iThrowGrenadeCounter);	// used to trigger grenade throw animations.



	int GetClass(void) const;
	void SetClanTag(const char* pTag);
	const char* GetClanTag(void) const;


	CNetworkVar(int, m_iClass); // One of the CS_CLASS_ enums.
	char m_szClanTag[MAX_CLAN_TAG_LENGTH];
	int	m_iHostagesKilled;

	void AwardAchievement(int iAchievement, int iCount = 1);
	void	SetNumMVPs(int iNumMVP);
	void	IncrementNumMVPs(CSMvpReason_t mvpReason);
	int		GetNumMVPs();
	bool CSWeaponDrop(CBaseCombatWeapon* pWeapon, bool bDropShield = true, bool bThrow = false);
	bool DropRifle(bool fromDeath = false);
	bool DropPistol(bool fromDeath = false);
	bool HasPrimaryWeapon(void);
	bool HasSecondaryWeapon(void);
	CNetworkVar(bool, m_bHasDefuser);			    // Does this player have a defuser kit?
	CNetworkVar(bool, m_bHasNightVision);		    // Does this player have night vision?
	CNetworkVar(bool, m_bNightVisionOn);		    // Is the NightVision turned on ?

	virtual int SpawnArmorValue(void) const { return ArmorValue(); }

	BuyResult_e AttemptToBuyAmmo(int iAmmoType);
	BuyResult_e AttemptToBuyAmmoSingle(int iAmmoType);
	BuyResult_e AttemptToBuyVest(void);
	BuyResult_e AttemptToBuyAssaultSuit(void);
	BuyResult_e AttemptToBuyDefuser(void);
	BuyResult_e AttemptToBuyNightVision(void);
	BuyResult_e AttemptToBuyShield(void);

	BuyResult_e BuyAmmo(int nSlot, bool bBlinkMoney);
	BuyResult_e BuyGunAmmo(CBaseCombatWeapon* pWeapon, bool bBlinkMoney);
	void HandleMenu_Radio1(int slot);
	void HandleMenu_Radio2(int slot);
	void HandleMenu_Radio3(int slot);
	bool HandleCommand_JoinClass(int iClass);

	BuyResult_e HandleCommand_Buy(const char* item);

	BuyResult_e HandleCommand_Buy_Internal(const char* item);

	bool IsVIP() const;

	void MakeVIP(bool isVIP);
	CNetworkVar(int, m_iDirection);	// The current lateral kicking direction; 1 = right,  0 = left
	CNetworkVar(int, m_iShotsFired);	// number of shots fired recently

	void FireBullet(
		Vector vecSrc,
		const QAngle& shootAngles,
		float flDistance,
		int iPenetration,
		int iBulletType,
		int iDamage,
		float flRangeModifier,
		CBaseEntity* pevAttacker,
		bool bDoEffects,
		float xSpread, float ySpread);

	void KickBack(
		float up_base,
		float lateral_base,
		float up_modifier,
		float lateral_modifier,
		float up_max,
		float lateral_max,
		int direction_change);

	void GetBulletTypeParameters(
		int iBulletType,
		float& fPenetrationPower,
		float& flPenetrationDistance);
	void DoAnimationEvent(PlayerAnimEvent_t event, int nData = 0);
	CNetworkVar(bool, m_bResumeZoom);
	CNetworkVar(int, m_iLastZoom); // after firing a shot, set the FOV to 90, and after showing the animation, bring the FOV back to last zoom level.
	CNetworkVar(bool, m_bIsDefusing);			// tracks whether this player is currently defusing a bomb
	int m_LastHitGroup;			// the last body region that took damage
	void SetShieldDrawnState(bool bState);
	void DropShield(void);
	static void	StartNewBulletGroup();	// global function
	CSPlayerState State_Get() const;				// Get the current state.
	CNetworkVar(bool, m_bHasHelmet);				// Does the player have helmet armor
	bool m_bEscaped;			// Has this terrorist escaped yet?
	void ResetRoundBasedAchievementVariables();
	void OnRoundEnd(int winningTeam, int reason);
	void OnPreResetRound();

	int GetNumEnemyDamagers();
	int GetNumEnemiesDamaged();
	CBaseEntity* GetNearestSurfaceBelow(float maxTrace);

	// Returns the % of the enemies this player killed in the round
	int GetPercentageOfEnemyTeamKilled();

	//List of times of recent kills to check for sprees
	CUtlVector<float>			m_killTimes;

	//List of all players killed this round
	CUtlVector<CHandle<CHL2MP_Player> >      m_enemyPlayersKilledThisRound;

	//List of weapons we have used to kill players with this round
	CUtlVector<int>				m_killWeapons;


	void WieldingKnifeAndKilledByGun(bool bState) { m_bWieldingKnifeAndKilledByGun = bState; }
	bool WasWieldingKnifeAndKilledByGun() { return m_bWieldingKnifeAndKilledByGun; }
	void PlayerUsedFirearm(CBaseCombatWeapon* pBaseWeapon);
	int GetNumFirearmsUsed() { return m_WeaponTypesUsed.Count(); }
	bool PickedUpDefuser() { return m_bPickedUpDefuser; }
	void SetDefusedWithPickedUpKit(bool bDefusedWithPickedUpKit) { m_bDefusedWithPickedUpKit = bDefusedWithPickedUpKit; }
	bool GetDefusedWithPickedUpKit() { return m_bDefusedWithPickedUpKit; }

	// Returns one of the CS_CLASS_ enums.
	int PlayerClass() const;
	void			AutoBuy(); // this should take into account what the player can afford and should buy the best equipment for them.

	bool			IsInAutoBuy(void) { return m_bIsInAutoBuy; }
	bool			IsInReBuy(void) { return m_bIsInRebuy; }
	bool			m_bIsInAutoBuy;
	bool			m_bAutoReload;
	void    RemoveNemesisRelationships();
	void	SetDeathFlags(int iDeathFlags) { m_iDeathFlags = iDeathFlags; }
	int		GetDeathFlags() { return m_iDeathFlags; }
	void	SetPlayerDominated(CHL2MP_Player* pPlayer, bool bDominated);
	void	SetPlayerDominatingMe(CHL2MP_Player* pPlayer, bool bDominated);
	bool	IsPlayerDominated(int iPlayerIndex);
	bool	IsPlayerDominatingMe(int iPlayerIndex);
	float m_flRadioTime;
	int m_iRadioMessages;
	int iRadioMenu;

	void StockPlayerAmmo(CBaseCombatWeapon* pNewWeapon = NULL);
	void			Rebuy();
	EHANDLE		m_hDroppedEquipment[DROPPED_COUNT];
	void SetProgressBarTime(int barTime);
	void                        OnCanceledDefuse();
	void                        OnStartedDefuse();
	int m_iNextTimeCheck;		// Next time the player can execute a "timeleft" command
	// Used to be GETINTOGAME state.
	void GetIntoGame();
	bool m_isVIP;
	BuyResult_e	CombineBuyResults(BuyResult_e prevResult, BuyResult_e newResult);
	void			ParseAutoBuyString(const char* string, bool& boughtPrimary, bool& boughtSecondary);
	GooseChaseAchievementStep   m_gooseChaseStep;
	DefuseDefenseAchivementStep m_defuseDefenseStep;
	CHandle<CHL2MP_Player>          m_pGooseChaseDistractingPlayer;

	bool	m_wasNotKilledNaturally; //Set if the player is dead from a kill command or late login

	bool	WasNotKilledNaturally() { return m_wasNotKilledNaturally; }
	bool			ShouldExecuteAutoBuyCommand(const AutoBuyInfoStruct* commandInfo, bool boughtPrimary, bool boughtSecondary);
	void			PostAutoBuyCommandProcessing(const AutoBuyInfoStruct* commandInfo, bool& boughtPrimary, bool& boughtSecondary);
	AutoBuyInfoStruct* GetAutoBuyCommandInfo(const char* command);
	void			PrioritizeAutoBuyString(char* autobuyString, const char* priorityString); // reorders the tokens in autobuyString based on the order of tokens in the priorityString.
	void RoundRespawn(void);
	void ObserverRoundRespawn(void);
	void CheckTKPunishment(void);
	bool CanChangeName(void);	// Checks if the player can change his name
	void ChangeName(const char* pszNewName);
	void SwitchTeam(int iTeamNum);	// Changes teams without penalty - used for auto team balancing
	bool			m_bIsBeingGivenItem;
	virtual bool		IsBeingGivenItem() const { return m_bIsBeingGivenItem; }
	bool m_bIgnoreRadio;

	CUtlLinkedList< CDamageRecord*, int >& GetDamageGivenList() { return m_DamageGivenList; }
	CUtlLinkedList< CDamageRecord*, int >& GetDamageTakenList() { return m_DamageTakenList; }
	void ClearFlashbangScreenFade(void);
	char m_szNewName[MAX_PLAYER_NAME_LENGTH]; // not empty if player requested a namechange

	bool m_receivesMoneyNextRound;
	void ResetDamageCounters();	//Reset all lists
	float m_flNameChangeHistory[NAME_CHANGE_HISTORY_SIZE]; // index 0 = most recent change
	void OutputDamageTaken(void);
	void OutputDamageGiven(void);

	CNetworkVar(float, m_flStartCharge);
	CNetworkVar(float, m_flAmmoStartCharge);
	CNetworkVar(float, m_flPlayAftershock);
	CNetworkVar(float, m_flNextAmmoBurn);	// while charging, when to absorb another unit of player's ammo?
	CNetworkVar(bool, m_bHasLongJump);
	CNetworkVar(bool, m_bIsPullingObject);

private:

	//A list of damage given
	CUtlLinkedList< CDamageRecord*, int >	m_DamageGivenList;

	//A list of damage taken
	CUtlLinkedList< CDamageRecord*, int >	m_DamageTakenList;







	int m_iDeathFlags; // Flags holding revenge and domination info about a death
	void			BuildRebuyStruct();
	int m_iMVPs;
	CNetworkArray(bool, m_bPlayerDominated, MAX_PLAYERS + 1);		// array of state per other player whether player is dominating other players
	CNetworkArray(bool, m_bPlayerDominatingMe, MAX_PLAYERS + 1);	// array of state per other player whether other players are dominating this player




	bool			m_bIsInRebuy;
	RebuyStruct		m_rebuyStruct;
	bool			m_bUsingDefaultPistol;


	bool                        m_bPickedUpDefuser;         // Did player pick up the defuser kit as opposed to buying it?
	bool                        m_bDefusedWithPickedUpKit;  // Did player defuse the bomb with a picked-up defuse kit?

	CUtlVector<CSWeaponID> m_WeaponTypesUsed;
	bool m_bWieldingKnifeAndKilledByGun;
	CNetworkQAngle( m_angEyeAngles );
	CPlayerAnimState   m_PlayerAnimState;

	int m_iLastWeaponFireUsercmd;
	int m_iModelType;
	CNetworkVar( int, m_iSpawnInterpCounter );
	CNetworkVar( int, m_iPlayerSoundType );

	float m_flNextModelChangeTime;
	float m_flNextTeamChangeTime;

	CNetworkVar(bool, m_bHeldObjectOnOppositeSideOfPortal);
	CNetworkHandle(CProp_Portal, m_pHeldObjectPortal);	// networked entity handle

	bool m_bIntersectingPortalPlane;
	bool m_bStuckOnPortalCollisionObject;

	float m_fTimeLastHurt;
	bool  m_bIsRegenerating;		// Is the player currently regaining health

	float m_fNeuroToxinDamageTime;

	PortalPlayerStatistics_t m_StatsThisLevel;
	float m_fTimeLastNumSecondsUpdate;

	int		m_iNumCamerasDetatched;

	float m_flSlamProtectTime;	

	CSPlayerState m_iPlayerState;
	CHL2MPPlayerStateInfo *m_pCurStateInfo;

	bool ShouldRunRateLimitedCommand( const CCommand &args );

	// This lets us rate limit the commands the players can execute so they don't overflow things like reliable buffers.
	CUtlDict<float,int>	m_RateLimitLastCommandTimes;

    bool m_bEnterObserver;
	bool m_bReady;
	bool m_bSuppressingCrosshair;
	QAngle						m_qPrePortalledViewAngles;
	bool						m_bFixEyeAnglesFromPortalling;
	VMatrix						m_matLastPortalled;
	CAI_Expresser* m_pExpresser;
	string_t					m_iszExpressionScene;
	EHANDLE						m_hExpressionSceneEnt;
	float						m_flExpressionLoopTime;
	
	
	BuyResult_e	RebuyPrimaryWeapon();
	BuyResult_e	RebuyPrimaryAmmo();
	BuyResult_e	RebuySecondaryWeapon();
	BuyResult_e	RebuySecondaryAmmo();
	BuyResult_e	RebuyHEGrenade();
	BuyResult_e	RebuyFlashbang();
	BuyResult_e	RebuySmokeGrenade();
	BuyResult_e	RebuyDefuser();
	BuyResult_e	RebuyNightVision();
	BuyResult_e	RebuyArmor();
	

public:

	int m_iNumSpawns;			// Number of times player has spawned this round
	int m_iOldTeam;				// Keep what team they were last on so we can allow joining spec and switching back to their real team
	bool m_bTeamChanged;		// Just allow one team change per round

	int m_iShouldHaveCash;

	bool m_bJustKilledTeammate;
	bool m_bPunishedForTK;
	int m_iTeamKills;
	float m_flLastMovement;

	CNetworkVar(int, m_iAccount);	// How much cash this player has.
	CNetworkVar(bool, m_bPitchReorientation);
	CNetworkHandle(CProp_Portal, m_hPortalEnvironment); //if the player is in a portal environment, this is the associated portal
	CNetworkHandle(CFunc_LiquidPortal, m_hSurroundingLiquidPortal); //if the player is standing in a liquid portal, this will point to it

	friend class CProp_Portal;
};

inline CHL2MP_Player *ToHL2MPPlayer( CBaseEntity *pEntity )
{
	if ( !pEntity || !pEntity->IsPlayer() )
		return NULL;

	return dynamic_cast<CHL2MP_Player*>( pEntity );
}

inline CHL2MP_Player* ToPortalPlayer(CBaseEntity* pEntity)
{
	if (!pEntity || !pEntity->IsPlayer())
		return NULL;

	return dynamic_cast<CHL2MP_Player*>(pEntity);
}

inline CHL2MP_Player* GetPortalPlayer(int iPlayerIndex)
{
	return static_cast<CHL2MP_Player*>(UTIL_PlayerByIndex(iPlayerIndex));
}

inline CHL2MP_Player* ToCSPlayer(CBaseEntity* pEntity)
{
	if (!pEntity || !pEntity->IsPlayer())
		return NULL;

	return dynamic_cast<CHL2MP_Player*>(pEntity);
}

inline bool CHL2MP_Player::IsReloading(void) const
{
	CBaseCombatWeapon* gun = GetActiveWeapon();
	if (gun == NULL)
		return false;

	return gun->m_bInReload;
}

inline bool CHL2MP_Player::IsProtectedByShield(void) const
{
	return HasShield() && IsShieldDrawn();
}

inline bool CHL2MP_Player::IsBlind(void) const
{
	return gpGlobals->curtime < m_blindUntilTime;
}

//=============================================================================
// HPE_BEGIN
// [sbodenbender] Need a different test for player blindness for the achievements
//=============================================================================
inline bool CHL2MP_Player::IsBlindForAchievement()
{
	return (m_blindStartTime + m_flFlashDuration) > gpGlobals->curtime;
}
//=============================================================================
// HPE_END
//=============================================================================

inline bool CHL2MP_Player::IsAutoFollowAllowed(void) const
{
	return (gpGlobals->curtime > m_allowAutoFollowTime);
}

inline void CHL2MP_Player::InhibitAutoFollow(float duration)
{
	m_allowAutoFollowTime = gpGlobals->curtime + duration;
}

inline void CHL2MP_Player::AllowAutoFollow(void)
{
	m_allowAutoFollowTime = 0.0f;
}

inline int CHL2MP_Player::GetClass(void) const
{
	return m_iClass;
}

inline const char* CHL2MP_Player::GetClanTag(void) const
{
	return m_szClanTag;
}
inline CSPlayerState CHL2MP_Player::State_Get() const
{
	return m_iPlayerState;
}


#endif //HL2MP_PLAYER_H

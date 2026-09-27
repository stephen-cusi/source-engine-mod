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
// HL2SB (2026-09-27): GMod player-animation glue needs the shared gesture-slot
// enum (GESTURE_SLOT_ATTACK_AND_RELOAD .. _CUSTOM) for the Lua bindings.
#include "Multiplayer/multiplayer_animstate.h"

//=============================================================================
// >> HL2MP_Player
//=============================================================================
class CHL2MPPlayerStateInfo
{
public:
	HL2MPPlayerState m_iPlayerState;
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

	DECLARE_SERVERCLASS();
	DECLARE_DATADESC();

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

	void State_Transition( HL2MPPlayerState newState );
	void State_Enter( HL2MPPlayerState newState );
	void State_Leave();
	void State_PreThink();
	CHL2MPPlayerStateInfo *State_LookupInfo( HL2MPPlayerState state );

	void State_Enter_ACTIVE();
	void State_PreThink_ACTIVE();
	void State_Enter_OBSERVER_MODE();
	void State_PreThink_OBSERVER_MODE();


	virtual bool StartObserverMode( int mode );
	virtual void StopObserverMode( void );


	Vector m_vecTotalBulletForce;	//Accumulator for bullet force in a single frame

	// Tracks our ragdoll entity.
	CNetworkHandle( CBaseEntity, m_hRagdoll );	// networked entity handle 

	CNetworkVar( float, m_flStartCharge );
	CNetworkVar( float, m_flAmmoStartCharge );
	CNetworkVar( float, m_flPlayAftershock );
	CNetworkVar( float, m_flNextAmmoBurn );

	virtual bool	CanHearAndReadChatFrom( CBasePlayer *pPlayer );

	// ------------------------------------------------------------------
	// HL2SB (2026-09-27): GMod player-animation / movement glue.
	// GM:CalcMainActivity / GM:DoAnimationEvent / GM:UpdateAnimation are the
	// three hooks GMod's base gamemode animations.lua drives player animation
	// with (reference hook-id table in lua_shared.dll: CalcMainActivity=4,
	// DoAnimationEvent=0x11).  The gesture-slot system mirrors GMod's
	// CMultiPlayerAnimState slots on top of CBaseAnimatingOverlay layers.
	// ------------------------------------------------------------------
	virtual float	GetPlayerMaxSpeed( void );	// GMod SetRunSpeed/SetWalkSpeed
	virtual float	GetHL2SBJumpPower( void ) const { return m_flHL2SBJumpPower; }
	bool			HL2SB_DoAnimationEventLua( PlayerAnimEvent_t event, int nData );
	Activity		HL2SB_TranslateActivityLua( Activity act, Activity fallback );
	void			HL2SB_AnimRestartGesture( int iSlot, Activity activity, bool bRestart );
	void			HL2SB_AnimResetGestureSlot( int iSlot );
	void			HL2SB_AnimSetGestureWeight( int iSlot, float flWeight );
	void			HL2SB_AnimSetGestureSequence( int iSlot, int iSequence );
	bool			HL2SB_IsPlayingTaunt( void );
	float			HL2SB_TauntEnd( void ) const { return m_flHL2SBTauntEnd; }
	void			HL2SB_SetTauntEnd( float flEnd ) { m_flHL2SBTauntEnd = flEnd; }

	// GMod gesture-slot tracking (server-side; layers replicate through
	// DT_BaseAnimatingOverlay).  Indexed by GESTURE_SLOT_* values.
	int				m_iHL2SBSlotLayer[GESTURE_SLOT_COUNT];
	Activity		m_iHL2SBSlotActivity[GESTURE_SLOT_COUNT];

	// GMod per-player movement speeds (networked; prediction reads them).
	CNetworkVar( float, m_flHL2SBWalkSpeed );
	CNetworkVar( float, m_flHL2SBRunSpeed );
	CNetworkVar( float, m_flHL2SBSlowWalkSpeed );
	CNetworkVar( float, m_flHL2SBJumpPower );

	// HL2SB (2026-09-27): GMod's taunt clock.  The reference act handler keeps
	// ONE replicated float ("taunt busy until"); IsPlayingTaunt() on every realm
	// is `curtime <= end && end != curtime`, and the act camera keys off it.
	CNetworkVar( float, m_flHL2SBTauntEnd );

	// HL2SB: remaining GMod Player speed/state knobs.  v1 stores the values
	// (Lua-visible both realms); only walk/run/jump feed the movement engine.
	float			m_flHL2SBCrouchedWalkSpeed;
	float			m_flHL2SBDuckSpeed;
	float			m_flHL2SBUnDuckSpeed;
	float			m_flHL2SBLadderClimbSpeed;
	float			m_flHL2SBStepSize;
	bool			m_bHL2SBAllowWeaponsInVehicle;


private:

	CNetworkQAngle( m_angEyeAngles );
	CPlayerAnimState   m_PlayerAnimState;

	int m_iLastWeaponFireUsercmd;
	int m_iModelType;
	CNetworkVar( int, m_iSpawnInterpCounter );
	CNetworkVar( int, m_iPlayerSoundType );

	float m_flNextModelChangeTime;
	float m_flNextTeamChangeTime;

	float m_flSlamProtectTime;	

	HL2MPPlayerState m_iPlayerState;
	CHL2MPPlayerStateInfo *m_pCurStateInfo;

	bool ShouldRunRateLimitedCommand( const CCommand &args );

	// This lets us rate limit the commands the players can execute so they don't overflow things like reliable buffers.
	CUtlDict<float,int>	m_RateLimitLastCommandTimes;

    bool m_bEnterObserver;
	bool m_bReady;
};

inline CHL2MP_Player *ToHL2MPPlayer( CBaseEntity *pEntity )
{
	if ( !pEntity || !pEntity->IsPlayer() )
		return NULL;

	return dynamic_cast<CHL2MP_Player*>( pEntity );
}

#endif //HL2MP_PLAYER_H

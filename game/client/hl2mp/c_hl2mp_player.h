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

class C_HL2MP_Player;
class C_WeaponHL2MPBase;
#include "c_basehlplayer.h"
#include "hl2mp_player_shared.h"
#include "beamdraw.h"
#include "c_prop_portal.h"
#include "c_func_liquidportal.h"
#include "cs_shareddefs.h"
#include "weapon_hl2mpbase.h"
#include "baseparticleentity.h"
#include "cs_weapon_parse.h"
#include "cs_playeranimstate.h"




//=============================================================================
// >> HL2MP_Player
//=============================================================================
class C_HL2MP_Player : public C_BaseHLPlayer
{
public:
	DECLARE_CLASS( C_HL2MP_Player, C_BaseHLPlayer );

	DECLARE_CLIENTCLASS();
	DECLARE_PREDICTABLE();
	DECLARE_INTERPOLATION();


	C_HL2MP_Player();
	~C_HL2MP_Player( void );

	void ClientThink( void );

	static C_HL2MP_Player* GetLocalHL2MPPlayer();

	virtual int DrawModel( int flags );
	virtual void AddEntity( void );

	QAngle GetAnimEyeAngles( void ) { return m_angEyeAngles; }
	Vector GetAttackSpread( CBaseCombatWeapon *pWeapon, CBaseEntity *pTarget = NULL );


	// Should this object cast shadows?
	virtual ShadowType_t		ShadowCastType( void );
	virtual C_BaseAnimating *BecomeRagdollOnClient();
	virtual const QAngle& GetRenderAngles();
	virtual bool ShouldDraw( void );
	virtual void OnDataChanged( DataUpdateType_t type );
	virtual float GetFOV( void );
	virtual CStudioHdr *OnNewModel( void );
	virtual void TraceAttack( const CTakeDamageInfo &info, const Vector &vecDir, trace_t *ptr, CDmgAccumulator *pAccumulator );
	virtual void ItemPreFrame( void );
	virtual void ItemPostFrame( void );
	virtual float GetMinFOV()	const { return 5.0f; }
	virtual Vector GetAutoaimVector( float flDelta );
	virtual void NotifyShouldTransmit( ShouldTransmitState_t state );
	virtual void CreateLightEffects( void ) {}
	virtual bool ShouldReceiveProjectedTextures( int flags );
	virtual void PostDataUpdate( DataUpdateType_t updateType );
	virtual void PlayStepSound( Vector &vecOrigin, surfacedata_t *psurface, float fvol, bool force );
	virtual void PreThink( void );
	virtual void DoImpactEffect( trace_t &tr, int nDamageType );
	IRagdoll* GetRepresentativeRagdoll() const;
	virtual void CalcView( Vector &eyeOrigin, QAngle &eyeAngles, float &zNear, float &zFar, float &fov );
	virtual const QAngle& EyeAngles( void );

	
	bool	CanSprint( void );
	void	StartSprinting( void );
	void	StopSprinting( void );
	void	HandleSpeedChanges( void );
	void	UpdateLookAt( void );
	void	Initialize( void );
	int		GetIDTarget() const;
	void	UpdateIDTarget( void );
	void	PrecacheFootStepSounds( void );
	const char	*GetPlayerModelSoundPrefix( void );
	void	PlayerPortalled( C_Prop_Portal *pEnteredPortal );

	CSPlayerState State_Get() const;

	// Walking
	void StartWalking( void );
	void StopWalking( void );
	bool IsWalking( void ) { return m_fIsWalking; }
	bool IsSuppressingCrosshair(void) { return m_bSuppressingCrosshair; }

	virtual void PostThink( void );

	bool	m_bPitchReorientation;
	float	m_fReorientationRate;
	bool	m_bEyePositionIsTransformedByPortal;

	CHandle<C_Prop_Portal>	m_hPortalEnvironment;

	inline bool		IsCloseToPortal(void) //it's usually a good idea to turn on draw hacks when this is true
	{
		return ((PortalEyeInterpolation.m_bEyePositionIsInterpolating) || (m_hPortalEnvironment.Get() != NULL));
	}

	virtual Vector			EyePosition();
	Vector					EyeFootPosition(const QAngle& qEyeAngles);//interpolates between eyes and feet based on view angle roll
	inline Vector			EyeFootPosition(void) { return EyeFootPosition(EyeAngles()); };
	void			CalcPortalView(Vector& eyeOrigin, QAngle& eyeAngles);
	virtual void	CalcViewModelView(const Vector& eyeOrigin, const QAngle& eyeAngles);
	void FixTeleportationRoll( void );
	bool					DetectAndHandlePortalTeleportation(void); //detects if the player has portalled and fixes views

	bool HasDefuser() const;

	void GiveDefuser();
	void RemoveDefuser();

	bool HasNightVision() const;

	static C_HL2MP_Player* GetLocalCSPlayer();
	// Get how much $$$ this guy has.
	int GetAccount() const;

	// Returns one of the CS_CLASS_ enums.
	int PlayerClass() const;

	bool IsInBuyZone();
	bool CanShowTeamMenu() const;	// Returns true if we're allowed to show the team menu right now.

	// Get the amount of armor the player has.
	int ArmorValue() const;
	bool HasHelmet() const;
	int GetCurrentAssaultSuitPrice();

	CNetworkVar(bool, m_bResumeZoom);
	CNetworkVar(int, m_iLastZoom); // after firing a shot, set the FOV to 90, and after showing the animation, bring the FOV back to last zoom level.
	CNetworkVar(CSPlayerState, m_iPlayerState);	// SupraFiend: this gives the current state in the joining process, the states are listed above
	CNetworkVar(bool, m_bIsDefusing);			// tracks whether this player is currently defusing a bomb
	CNetworkVar(bool, m_bInBombZone);
	CNetworkVar(bool, m_bInBuyZone);
	CNetworkVar(int, m_iThrowGrenadeCounter);	// used to trigger grenade throw animations.

	bool IsInHostageRescueZone(void);

	// This is a combination of the ADDON_ flags in cs_shareddefs.h.
	CNetworkVar(int, m_iAddonBits);

	// Clients don't know about holstered weapons, so we need to be told about them here
	CNetworkVar(int, m_iPrimaryAddon);
	CNetworkVar(int, m_iSecondaryAddon);

	// How long the progress bar takes to get to the end. If this is 0, then the progress bar
	// should not be drawn.
	CNetworkVar(int, m_iProgressBarDuration);

	// When the progress bar should start.
	CNetworkVar(float, m_flProgressBarStartTime);

	CNetworkVar(float, m_flStamina);
	CNetworkVar(int, m_iDirection);	// The current lateral kicking direction; 1 = right,  0 = left
	CNetworkVar(int, m_iShotsFired);	// number of shots fired recently
	CNetworkVar(bool, m_bNightVisionOn);
	CNetworkVar(bool, m_bHasNightVision);

	//=============================================================================
	// HPE_BEGIN:
	// [dwenger] Added for fun-fact support
	//=============================================================================

	//CNetworkVar( bool, m_bPickedUpDefuser );
	//CNetworkVar( bool, m_bDefusedWithPickedUpKit );

	//=============================================================================
	// HPE_END
	//=============================================================================

	CNetworkVar(float, m_flVelocityModifier);

	bool		m_bDetected;

	EHANDLE	m_hRagdoll;

	C_WeaponHL2MPBase* GetActiveCSWeapon() const;
	C_WeaponHL2MPBase* GetCSWeapon(CSWeaponID id) const;

#ifdef CS_SHIELD_ENABLED
	bool HasShield(void) { return m_bHasShield; }
	bool IsShieldDrawn(void) { return m_bShieldDrawn; }
	void SetShieldDrawnState(bool bState) { m_bShieldDrawn = bState; }
#else
	bool HasShield(void) { return false; }
	bool IsShieldDrawn(void) { return false; }
	void SetShieldDrawnState(bool bState) {}
#endif

	float m_flNightVisionAlpha;

	float m_flFlashAlpha;
	float m_flFlashBangTime;
	CNetworkVar(float, m_flFlashMaxAlpha);
	CNetworkVar(float, m_flFlashDuration);

	// Having the RecvProxy in the player allows us to keep the var private
	static void RecvProxy_CycleLatch(const CRecvProxyData* pData, void* pStruct, void* pOut);

	// Bots and hostages auto-duck during jumps
	bool m_duckUntilOnGround;

	Vector m_lastStandingPos; // used by the gamemovement code for finding ladders

	void SurpressLadderChecks(const Vector& pos, const Vector& normal);
	bool CanGrabLadder(const Vector& pos, const Vector& normal);

	//=============================================================================
	// HPE_BEGIN:
	//=============================================================================

	// [tj] checks if this player has another given player on their Steam friends list.
	bool HasPlayerAsFriend(C_HL2MP_Player* player);
	void KickBack(
		float up_base,
		float lateral_base,
		float up_modifier,
		float lateral_modifier,
		float up_max,
		float lateral_max,
		int direction_change);
	void DoAnimationEvent(PlayerAnimEvent_t event, int nData = 0);
	bool		 HasC4(void);
	bool IsPlayerDominated(int iPlayerIndex);
	bool IsPlayerDominatingMe(int iPlayerIndex);
	CPlayerAnimState* GetPlayerAnimState() { return &m_PlayerAnimState; }
	//ICSPlayerAnimState* m_PlayerAnimState;

	void GetBulletTypeParameters(
		int iBulletType,
		float& fPenetrationPower,
		float& flPenetrationDistance);

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


	bool IsVIP() const;	// Is this player the VIP?
	CPlayerAnimState m_PlayerAnimState;
	bool	m_bHasDefuser;
	CUtlVector<C_BaseParticleEntity*> m_SmokeGrenades;
	bool	m_bInHostageRescueZone;
private:
	
	C_HL2MP_Player( const C_HL2MP_Player & );

	void UpdatePortalEyeInterpolation(void);


	QAngle	m_angEyeAngles;

	CInterpolatedVar< QAngle >	m_iv_angEyeAngles;


	int	m_headYawPoseParam;
	int	m_headPitchPoseParam;
	float m_headYawMin;
	float m_headYawMax;
	float m_headPitchMin;
	float m_headPitchMax;

	bool m_isInit;
	Vector m_vLookAtTarget;

	float m_flLastBodyYaw;
	float m_flCurrentHeadYaw;
	float m_flCurrentHeadPitch;

	int	  m_iIDEntIndex;

	CountdownTimer m_blinkTimer;

	int	  m_iSpawnInterpCounter;
	int	  m_iSpawnInterpCounterCache;

	int	  m_iPlayerSoundType;

	void ReleaseFlashlight( void );
	Beam_t	*m_pFlashlightBeam;

	bool	m_bPortalledMessagePending; //Player portalled. It's easier to wait until we get a OnDataChanged() event or a CalcView() before we do anything about it. Otherwise bits and pieces can get undone
	VMatrix m_PendingPortalMatrix;

	bool m_fIsWalking;
	bool m_bSuppressingCrosshair;

	bool  m_bHeldObjectOnOppositeSideOfPortal;
	CProp_Portal* m_pHeldObjectPortal;

	int	m_iForceNoDrawInPortalSurface; //only valid for one frame, used to temp disable drawing of the player model in a surface because of freaky artifacts

	struct PortalEyeInterpolation_t
	{
		bool	m_bEyePositionIsInterpolating; //flagged when the eye position would have popped between two distinct positions and we're smoothing it over
		Vector	m_vEyePosition_Interpolated; //we'll be giving the interpolation a certain amount of instant movement per frame based on how much an uninterpolated eye would have moved
		Vector	m_vEyePosition_Uninterpolated; //can't have smooth movement without tracking where we just were
		//bool	m_bNeedToUpdateEyePosition;
		//int		m_iFrameLastUpdated;

		int		m_iTickLastUpdated;
		float	m_fTickInterpolationAmountLastUpdated;
		bool	m_bDisableFreeMovement; //used for one frame usually when error in free movement is likely to be high
		bool	m_bUpdatePosition_FreeMove;

		PortalEyeInterpolation_t(void) : m_iTickLastUpdated(0), m_fTickInterpolationAmountLastUpdated(0.0f), m_bDisableFreeMovement(false), m_bUpdatePosition_FreeMove(false) { };
	} PortalEyeInterpolation;

	struct PreDataChanged_Backup_t
	{
		CHandle<C_Prop_Portal>	m_hPortalEnvironment;
		CHandle<C_Func_LiquidPortal>	m_hSurroundingLiquidPortal;
		//Vector					m_ptPlayerPosition;
		QAngle					m_qEyeAngles;
	} PreDataChanged_Backup;

	Vector	m_ptEyePosition_LastCalcView;
	QAngle	m_qEyeAngles_LastCalcView; //we've got some VERY persistent single frame errors while teleporting, this will be updated every frame in CalcView() and will serve as a central source for fixed angles
	C_Prop_Portal* m_pPortalEnvironment_LastCalcView;

	int		m_iAccount;
	bool	m_bHasHelmet;
	int		m_iClass;
	int		m_ArmorValue;
	CNetworkArray(bool, m_bPlayerDominated, MAX_PLAYERS + 1);		// array of state per other player whether player is dominating other players
	CNetworkArray(bool, m_bPlayerDominatingMe, MAX_PLAYERS + 1);	// array of state per other player whether other players are dominating this player

};

inline C_HL2MP_Player *ToHL2MPPlayer( CBaseEntity *pEntity )
{
	if ( !pEntity || !pEntity->IsPlayer() )
		return NULL;

	return dynamic_cast<C_HL2MP_Player*>( pEntity );
}

inline C_HL2MP_Player* ToPortalPlayer(CBaseEntity* pEntity)
{
	if (!pEntity || !pEntity->IsPlayer())
		return NULL;

	return dynamic_cast<C_HL2MP_Player*>(pEntity);
}

inline C_HL2MP_Player* GetPortalPlayer(void)
{
	return static_cast<C_HL2MP_Player*>(C_BasePlayer::GetLocalPlayer());
}

class C_HL2MPRagdoll : public C_BaseAnimatingOverlay
{
public:
	DECLARE_CLASS( C_HL2MPRagdoll, C_BaseAnimatingOverlay );
	DECLARE_CLIENTCLASS();
	
	C_HL2MPRagdoll();
	~C_HL2MPRagdoll();

	virtual void OnDataChanged( DataUpdateType_t type );

	int GetPlayerEntIndex() const;
	IRagdoll* GetIRagdoll() const;

	void ImpactTrace( trace_t *pTrace, int iDamageType, const char *pCustomImpactName );
	void UpdateOnRemove( void );
	virtual void SetupWeights( const matrix3x4_t *pBoneToWorld, int nFlexWeightCount, float *pFlexWeights, float *pFlexDelayedWeights );
	
private:
	
	C_HL2MPRagdoll( const C_HL2MPRagdoll & ) {}

	void Interp_Copy( C_BaseAnimatingOverlay *pDestinationEntity );
	void CreateHL2MPRagdoll( void );

private:

	EHANDLE	m_hPlayer;
	CNetworkVector( m_vecRagdollVelocity );
	CNetworkVector( m_vecRagdollOrigin );
};

C_HL2MP_Player* GetLocalOrInEyeCSPlayer(void);

inline C_HL2MP_Player* ToCSPlayer(CBaseEntity* pEntity)
{
	if (!pEntity || !pEntity->IsPlayer())
		return NULL;

	return dynamic_cast<C_HL2MP_Player*>(pEntity);
}

namespace vgui
{
	class IImage;
	class IScheme;
}

vgui::IImage* GetDefaultAvatarImage(C_BasePlayer* pPlayer);

#endif //HL2MP_PLAYER_H


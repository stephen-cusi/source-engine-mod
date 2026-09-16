//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//
//=============================================================================//
#ifndef HL2MP_PLAYER_SHARED_H
#define HL2MP_PLAYER_SHARED_H
#pragma once

#define HL2MP_PUSHAWAY_THINK_INTERVAL		(1.0f / 20.0f)
#include "studio.h"


enum
{
	PLAYER_SOUNDS_CITIZEN = 0,
	PLAYER_SOUNDS_COMBINESOLDIER,
	PLAYER_SOUNDS_METROPOLICE,
	PLAYER_SOUNDS_MAX,
};

enum HL2MPPlayerState
{
	// Happily running around in the game.
	STATE_ACTIVE=0,
	STATE_OBSERVER_MODE,		// Noclipping around, watching players, etc.
	NUM_PLAYER_STATES
};


#if defined( CLIENT_DLL )
#define CHL2MP_Player C_HL2MP_Player
#endif

class CBaseAnimating;
class CBaseEntity;

// ---------------------------------------------------------------------------
// HL2SB: name-pinned player locomotion sequences.
//
// Both animation model families name their locomotion sequences
// "<state>_<holdtype>" - idle_pistol / run_smg1 / walk_ar2 / jump_melee /
// cidle_pistol (crouch idle) / cwalk_ar2 (crouch walk) / swim_idle_rpg /
// swimming_fist / sit_camera - and the holdtype-less ones "idle_all_01",
// "run_all_01", ...  Sequence NAMES are the one thing that survives between the
// engines, so this picks the sequence by name instead of going through the
// activity -> holdtype -> weighted-random-sequence chain.
//
// Returns ACT_INVALID when the model has no sequence with any name we know, so
// the caller can fall back to the activity path.
//
//   translatedActivity - the weapon-translated activity
//                        (WEAPON_TranslateActivity(idealActivity), e.g.
//                        ACT_HL2MP_IDLE_PISTOL - it carries the hold type)
//   baseActivity       - the untranslated one (ACT_HL2MP_IDLE), used when the
//                        weapon's table has no opinion
// ---------------------------------------------------------------------------
int HL2SB_SelectPlayerSequence( CBaseAnimating *pAnim, Activity translatedActivity, Activity baseActivity );

// HL2SB: resolve a GMod-style gesture name ("wave", "agree", "taunt_dance", ...) to a
// sequence. GMod's act command is NOT a fixed list - it takes any gesture the model
// provides - so this tries the ACT_GMOD_* spellings first and then scans the model's
// own activity/sequence names. Returns -1 when nothing matches.
int HL2SB_ResolveGestureSequence( CBaseAnimating *pAnim, const char *pszName, Activity *pActivityOut );

// HL2SB: the sequence a player sitting in THIS vehicle should play.
//
// GMod gets it from the seat's own Lua table (lua/autorun/base_vehicles.lua:108
// `Members.HandleAnimation = function( vehicle, player ) return
// player:SelectWeightedSequence( ACT_GMOD_SIT_ROLLERCOASTER ) end`), and GMod's engine
// calls that function from the player's animation code.  HL2SB's seats are spawned from a
// console line (`ent_create prop_vehicle_prisoner_pod model ... vehiclescript ...
// limitview 0`, SMenu_BuildVehicleListCommand), so there is no Lua table to carry it and
// the vehicle's own CLASS decides instead - never a model name, which is what makes one
// GMod seat a chair and another a car seat.
//
// Returns the sequence, or -1 when the vehicle supplies no seat animation (or the model
// does not carry it) so the caller can fall back to the sit_<holdtype> family.
// *pActivityOut receives the activity that was asked for (ACT_INVALID when none applied).
int HL2SB_SelectVehicleSitSequence( CBaseAnimating *pAnim, CBaseEntity *pVehicle, Activity *pActivityOut );

// HL2SB: THE seat pose for a player in a vehicle - HL2SB_SelectVehicleSitSequence() and,
// when the vehicle has no seat animation of its own, the sit_<holdtype> fallback family.
// The SERVER pins this sequence in CHL2MP_Player::SetAnimation(); the CLIENT resolves the
// same sequence to tell "still seated" from "the exit animation is running" (the vehicle
// is still around then - see UpdateVehicleAnimation()), so both realms must ask the same
// question through this one function.
//
// Returns the sequence, or -1 when nothing resolves (the caller then leaves the pose alone).
// The sit_<holdtype> fallback needs the WEAPON-translated activity, which only exists on the
// server, so the client resolves only the vehicle's own seat pose and gets -1 for the rest.
int HL2SB_ResolveSeatedSequence( CHL2MP_Player *pPlayer, CBaseEntity *pVehicle, Activity *pActivityOut );

// HL2SB: the sequence a vehicle / seat model RESTS in - "idle" by label (every
// models/nova/*.mdl seat and both Valve vehicles have one), else the model's first
// sequence (the engine leaves m_nSequence at 0, and every GMod seat model has exactly
// one). Returns -1 when the model has no sequences at all.
int HL2SB_GetRestingSequence( CBaseAnimating *pAnim );

// HL2SB: an attachment's transform in the model's RESTING sequence pose, in MODEL space.
//
// This is what a seat must be read from. GetAttachment()/GetAttachmentLocal() evaluate
// the pose the entity happens to be playing, and for a vehicle that is not the seat while
// the vehicle plays its ENTER animation: CBaseServerVehicle::GetPassengerSeatPoint() runs
// from CBasePlayer::GetInVehicle(), i.e. right after HandlePassengerEntry() told the
// vehicle to play its enter sequence (vehicle_baseserver.cpp:1190-1197), and on Valve's
// vehicles the seat attachments hang off the animated driver-view bone that the enter/exit
// sequences drive along the entry path:
//     models/airboat.mdl  enter1..enter8 / exit1..exit10 animate bone 1 `Airboat.view`
//                         (ANIMPOS|ANIMROT) - the bone vehicle_feet_passenger0 AND
//                         vehicle_driver_eyes are attached to - with POSITIONS ALONG THE
//                         ENTRY PATH (frame 0 of enter1 lands at model x -64, while the
//                         hull only reaches x -44.7), while idle and propeller_spin1 do
//                         not touch the bone at all
//     models/buggy.mdl    same shape (enter1..4 / exit1..8 animate bone 12
//                         `Rig_Buggy.view`), but the jeep never gets read in that state
//                         because its ACT_IDLE branch stomps the sequence first - which
//                         is why the jeep has always been right and the airboat was
//                         "seated outside the boat, rotated with it"
// (measured with tools/mdl_seq_bones.py + tools/mdl_attach_at_frame.py.)
//
// The query itself is the engine's own stomp/query/restore idiom
// (CBaseServerVehicle::GetLocalAttachmentAtTime): the entity's sequence and cycle are put
// back exactly as they were found, so nothing about the visible animation changes.
// Returns false when the model/attachment cannot be resolved.
bool HL2SB_GetRestingAttachmentLocal( CBaseAnimating *pAnim, const char *pszAttachment,
									  Vector *pVecOrigin, QAngle *pAngles );

class CPlayerAnimState
{
public:
	enum
	{
		TURN_NONE = 0,
		TURN_LEFT,
		TURN_RIGHT
	};

	CPlayerAnimState( CHL2MP_Player *outer );

	// HL2SB: GMod-style vehicle animation state. Enter/Exit are server-authoritative
	// (IServerVehicle::IsPassengerEntering/Exiting + the vehicle's own entry anim), the
	// rest is derived from the seat the player occupies. See UpdateVehicleAnimation().
	enum VehicleAnimState_t
	{
		VEHICLE_ANIM_NONE = 0,
		VEHICLE_ANIM_ENTER,
		VEHICLE_ANIM_IDLE,
		VEHICLE_ANIM_DRIVER,
		VEHICLE_ANIM_PASSENGER,
		VEHICLE_ANIM_EXIT,
	};

	void				UpdateVehicleAnimation( void );
	VehicleAnimState_t	GetVehicleAnimState( void ) const { return m_eHL2SBVehicleAnimState; }
	int					GetVehicleRole( void ) const { return m_iHL2SBVehicleRole; }
	float				GetVehicleSteering( void ) const { return m_flHL2SBVehicleSteering; }

	Activity			BodyYawTranslateActivity( Activity activity );

	void				Update();

	const QAngle&		GetRenderAngles();
				
	void				GetPoseParameters( CStudioHdr *pStudioHdr, float poseParameter[MAXSTUDIOPOSEPARAM] );

	CHL2MP_Player		*GetOuter();

private:
	void				GetOuterAbsVelocity( Vector& vel );

	int					ConvergeAngles( float goal,float maxrate, float dt, float& current );

	void				EstimateYaw( void );
	void				ComputePoseParam_BodyYaw( void );
	void				ComputePoseParam_BodyPitch( CStudioHdr *pStudioHdr );
	void				ComputePoseParam_BodyLookYaw( void );

	void				ComputePlaybackRate();

	// HL2SB: GMod's noclip pose (ACT_GMOD_NOCLIP_LAYER) - see the .cpp.
	void				UpdateNoclipLayer( void );

	// HL2SB: vehicle animation bookkeeping (state machine + steering pose parameter).
	VehicleAnimState_t	m_eHL2SBVehicleAnimState;
	int					m_iHL2SBVehicleRole;
	float				m_flHL2SBVehicleSteering;

	// HL2SB: the world yaw of the seat the player is riding (the vehicle's own
	// `vehicle_feet_passenger0` attachment, so it is the same datum the player is
	// seated by). The seated body is rendered at this yaw and the head pose
	// parameters are the view relative to it; see UpdateVehicleAnimation().
	//
	// ⚠ DO NOT add members to this class casually. CPlayerAnimState is embedded in the
	// player classes, so growing it moves every member that follows it (C_HL2MP_Player's
	// m_angEyeAngles / m_hRagdoll / m_headYawPoseParam / m_iPlayerSoundType / ... on the
	// client, CHL2MP_Player's members on the server) - and waf does NOT recompile a
	// header's dependents when this header changes (2026-09-16 01:56: only
	// hl2mp_player_shared.cpp was rebuilt, while c_hl2mp_player.cpp, c_baseentity.cpp and
	// lhl2mp_player_shared.cpp kept the old offsets). The resulting layout split crashed
	// the client inside C_BaseEntity::OnLatchInterpolatedVariables right at spawn
	// (crash_20260916_020140). Anything stateful for the seat has to live OUTSIDE this
	// class, or every dependent .cpp must be force-rebuilt afterwards.
	float				m_flHL2SBSeatYaw;

	CHL2MP_Player		*m_pOuter;

	bool				m_bHL2SBNoclipping;
	int					m_iHL2SBNoclipLayer;

	float				m_flGaitYaw;
	float				m_flStoredCycle;

	// The following variables are used for tweaking the yaw of the upper body when standing still and
	//  making sure that it smoothly blends in and out once the player starts moving
	// Direction feet were facing when we stopped moving
	float				m_flGoalFeetYaw;
	float				m_flCurrentFeetYaw;

	float				m_flCurrentTorsoYaw;

	// To check if they are rotating in place
	float				m_flLastYaw;
	// Time when we stopped moving
	float				m_flLastTurnTime;

	// One of the above enums
	int					m_nTurningInPlace;

	QAngle				m_angRender;

	float				m_flTurnCorrectionTime;
};

#endif //HL2MP_PLAYER_SHARED_h

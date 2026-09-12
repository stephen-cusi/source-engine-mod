//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: GMod-style player animation state for HL2MP/HL2SB.
//
//          Phase 1a: behaviour-neutral takeover. This class replaces the old
//          hand-written CPlayerAnimState (game/shared/hl2mp/hl2mp_player_shared.{h,cpp})
//          slot-for-slot, but derives from CBasePlayerAnimState so the GMod
//          machinery (layers, gesture slots, sequence transitioners) is available
//          in later phases.
//
//          In THIS milestone Update() is fully overridden and the base
//          CBasePlayerAnimState::Update(float,float) is deliberately NOT called:
//          the base would hijack SetNumAnimOverlays(5), select ACT_MP_*/ACT_* via
//          CalcMainActivity() and write body_pitch/body_yaw, which would change
//          behaviour. The base class is used for its machinery later, not now.
//
//=============================================================================//

#ifndef GMOD_PLAYER_ANIMSTATE_H
#define GMOD_PLAYER_ANIMSTATE_H
#ifdef _WIN32
#pragma once
#endif

#include "hl2mp_player_shared.h"	// CHL2MP_Player (= C_HL2MP_Player on client)
#include "base_playeranimstate.h"	// CBasePlayerAnimState, CModAnimConfig, LegAnimType_t

// ------------------------------------------------------------------------------------------------ //
// CGModPlayerAnimState
//
// Plumbing notes (behaviour-neutral takeover, Phase 1a):
//
//  * Update() is the no-argument HL2MP entry point (called from
//    c_hl2mp_player.cpp:425 and hl2mp_player.cpp:658). The base class has
//    Update(float,float) (base_playeranimstate.cpp:121), so the no-arg overload
//    HIDES it. The "using CBasePlayerAnimState::Update;" below pulls the base
//    overload back into scope so the two coexist instead of conflicting.
//
//  * The base's virtual ComputePoseParam_BodyYaw/BodyPitch/MoveYaw are overridden
//    BELOW so that even if the base Update() is ever reached (refactor, or a
//    future phase that wants the machinery), it can never write body_yaw /
//    body_pitch / move_x / move_y / the 8-way idle overlay on top of ours.
//    They are not called from our Update(); our Update() calls our own steps
//    directly, exactly like the old class did.
//
//  * BodyYawTranslateActivity() and GetPoseParameters() were dead API in the old
//    class (the former had no callers, the latter was never defined anywhere in
//    the repo - see anim_audit/Z_core_finding.md 1.4). They are intentionally NOT
//    ported.
// ------------------------------------------------------------------------------------------------ //
class CGModPlayerAnimState : public CBasePlayerAnimState
{
public:
	CGModPlayerAnimState( CHL2MP_Player *outer );

	// Bring the base's 2-argument Update(float,float) into scope alongside our
	// no-argument overload, so this is an overload set and not a name conflict.
	using CBasePlayerAnimState::Update;

	// The HL2MP entry point. Does NOT call CBasePlayerAnimState::Update().
	virtual void Update();

	// Base machinery is not used yet: keep it from gating or side-effecting us.
	virtual bool ShouldUpdateAnimState();
	virtual bool ShouldChangeSequences( void ) const;

	// Overridden so a future base Update() can never stomp our pose parameters.
	// All three are deliberate no-ops in this milestone: the old CPlayerAnimState
	// wrote NO body_yaw / body_pitch / move_x / move_y pose parameters.
	virtual void ComputePoseParam_BodyYaw();
	virtual void ComputePoseParam_BodyPitch( CStudioHdr *pStudioHdr );
	virtual void ComputePoseParam_MoveYaw( CStudioHdr *pStudioHdr );

	// Pure virtuals of CBasePlayerAnimState that this milestone never reaches
	// (our Update() does not call the base). Implemented so the class is
	// concrete; CalcMainActivity/CalcAimLayerSequence are unreachable, and
	// GetCurrentMaxGroundSpeed() is only reachable if the base playback-rate
	// path is ever used.
	virtual Activity CalcMainActivity();
	virtual int CalcAimLayerSequence( float *flCycle, float *flAimSequenceWeight, bool bForceIdle );
	virtual float GetCurrentMaxGroundSpeed();

	CHL2MP_Player *GetOuter();

private:
	void GetOuterAbsVelocity( Vector& vel );

	// Bring the base's 5-argument ConvergeAngles into scope: ours is the 4-arg
	// HL2MP form (old hl2mp_player_shared.cpp:365) and would otherwise hide it.
	using CBasePlayerAnimState::ConvergeAngles;

	int  ConvergeAngles( float goal, float maxrate, float dt, float& current );

	void EstimateYaw( void );
	void ComputeGModPoseParam_BodyYaw( void );
	void ComputeGModPoseParam_BodyPitch( CStudioHdr *pStudioHdr );
	void ComputeGModPoseParam_BodyLookYaw( void );

	// Stage 2: the GMod leg blend. GMod's libraries (m_anm/f_anm/z_anm) declare
	// move_x/move_y and blend the legs on a 3x3 grid
	// ("blendwidth 3 blend move_y -1 1 blend move_x -1 1", macro_movement_m.qci:91);
	// the HL2-era models this fork shipped declare the single move_yaw instead.
	// Reuses the engine's own 9-way maths, which is the code GMod's engine is
	// derived from, rather than re-deriving the mapping here.
	void ComputeGModPoseParam_Move( CStudioHdr *pStudioHdr );

	// Stage 2 discipline: report exactly which pose parameters the current model
	// declares, once per model, instead of guessing at a missing one.
	void ReportGModPoseParamsOnce( CStudioHdr *pStudioHdr );

	void ComputeGModPlaybackRate();

	CHL2MP_Player *m_pGModOuter;

	// Model whose pose-parameter inventory has already been reported.
	CStudioHdr *m_pReportedStudioHdr;

	// The base keeps its own m_flGaitYaw PRIVATE (base_playeranimstate.h:258),
	// so the 8-way yaw estimator needs its own copy. Nothing else in the base
	// touches it; it would only diverge if the base Update() were ever called,
	// which this milestone does not do.
	float m_flGModGaitYaw;
};

#endif // GMOD_PLAYER_ANIMSTATE_H

//========= Copyright (c) All rights reserved. ============//
//
// Purpose: Client-side viewmodel attachment entity for GMod-style c_hands.
//          The hands model is a proper client entity, parented to the weapon
//          viewmodel with EF_BONEMERGE. Matching bones (ValveBiped.* on c_arms
//          rigs, with or without the prefix on the weapon) are driven entirely
//          by the weapon viewmodel's animation, which is what aligns the arms
//          with the gun - the same way GMod does it.
//
//=============================================================================//

#ifndef C_VIEWMODEL_ATTACHMENT_H
#define C_VIEWMODEL_ATTACHMENT_H

#ifdef _WIN32
#pragma once
#endif

#include "c_baseanimating.h"

class C_BaseViewModel;

//-----------------------------------------------------------------------------
// Purpose: Entity that renders a hands/arms model bonemerged onto a viewmodel
//-----------------------------------------------------------------------------
class C_ViewmodelAttachment : public C_BaseAnimating
{
	DECLARE_CLASS( C_ViewmodelAttachment, C_BaseAnimating );
public:
	C_ViewmodelAttachment( void );
	~C_ViewmodelAttachment( void );

	// Initialize as a proper client entity and load the hands model.
	// Returns false if the model could not be loaded.
	bool SetHandsModel( const char *pszModelName );

	// Attach to a viewmodel entity (SetParent + EF_BONEMERGE follow).
	void AttachToViewmodel( C_BaseViewModel *pViewModel );

	// Detach from viewmodel
	void DetachFromViewmodel( void );

	bool IsAttached( void ) const { return m_bAttached; }

	// Per-frame sync with the parent viewmodel: if the hands model has a
	// sequence with the same name as the viewmodel's current sequence, play it
	// with the same cycle/playback rate (GMod fallback for weapons whose
	// animations don't drive the arm bones).
	void SyncToViewModel( C_BaseViewModel *pViewModel );

	// Override drawing to use bonemerge from parent
	virtual int DrawModel( int flags );

	// Override SetupBones to apply the cl_hands_offset_* / cl_hands_angle_*
	// correction (in viewmodel space) after native bonemerge.
	virtual bool SetupBones( matrix3x4_t *pBoneToWorldOut, int nMaxBones, int boneMask, float currentTime );

	// Re-apply model-dependent state (default sequence) when the model changes
	virtual CStudioHdr *OnNewModel( void );

	// Entity is always transmitted to local player
	virtual int ShouldTransmit( const CCheckTransmitInfo *pInfo, const void *pVSPTState );

private:
	// Rigid viewmodel-space correction from the cl_hands_offset_/angle_ cvars,
	// applied uniformly to every bone after the merge.
	void ApplyHandsOffset( void );

	CHandle<C_BaseViewModel> m_hParentViewModel;	// Handle to parent viewmodel
	bool m_bAttached;								// Is currently attached
	int m_iDefaultSequence;							// "proportions"/"idle"/"reference" fallback
	float m_flLastOffsetTime;						// Guard so the offset is applied once per frame
};

#endif // C_VIEWMODEL_ATTACHMENT_H

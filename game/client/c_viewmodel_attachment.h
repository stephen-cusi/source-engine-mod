//========= Copyright (c) All rights reserved. ============//
//
// Purpose: Client-side viewmodel attachment entity for hands/arms models
//          Attaches to viewmodels and renders hands alongside weapons
//
//=============================================================================//

#ifndef C_VIEWMODEL_ATTACHMENT_H
#define C_VIEWMODEL_ATTACHMENT_H

#ifdef _WIN32
#pragma once
#endif

#include "c_baseanimating.h"
#include "utlvector.h"

class C_BaseViewModel;

//-----------------------------------------------------------------------------
// Purpose: Entity that attaches hands/arms model to a viewmodel
//          Uses bonemerge to inherit animation from parent viewmodel
//          Applies a wrist-local correction to fix 90° orientation mismatch
//-----------------------------------------------------------------------------
class C_ViewmodelAttachment : public C_BaseAnimating
{
	DECLARE_CLASS( C_ViewmodelAttachment, C_BaseAnimating );
public:
	C_ViewmodelAttachment( void );
	~C_ViewmodelAttachment( void );

	// Initialize with a hands model path
	bool SetHandsModel( const char *pszModelName );

	// Attach to a viewmodel entity
	void AttachToViewmodel( C_BaseViewModel *pViewModel );

	// Detach from viewmodel
	void DetachFromViewmodel( void );

	// Override drawing to use bonemerge from parent
	virtual int DrawModel( int flags );

	// Override SetupBones to apply wrist-local correction after bonemerge
	virtual bool SetupBones( matrix3x4_t *pBoneToWorldOut, int nMaxBones, int boneMask, float currentTime );

	// Entity is always transmitted to local player
	virtual int ShouldTransmit( const CCheckTransmitInfo *pInfo, const void *pVSPTState );

private:
	// Rebuild wrist bone chain caches (called on model change)
	void RebuildBoneChainCache( void );

	// Apply wrist-local correction to a bone chain
	void ApplyWristCorrection( void );

	CHandle<C_BaseViewModel> m_hParentViewModel;		// Handle to parent viewmodel
	bool m_bAttached;								// Is currently attached

	// Cached bone chains (hand bone + all descendant bones in this model's hierarchy)
	CUtlVector<int> m_RHandChain;
	CUtlVector<int> m_LHandChain;
	int m_RHandIndex;
	int m_LHandIndex;
	bool m_bBoneChainCached;
};

#endif // C_VIEWMODEL_ATTACHMENT_H

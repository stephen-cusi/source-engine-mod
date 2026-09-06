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

class C_BaseViewModel;

//-----------------------------------------------------------------------------
// Purpose: Entity that attaches hands/arms model to a viewmodel
//          Uses bonemerge to inherit animation from parent viewmodel
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

	// Entity is always transmitted to local player
	virtual int ShouldTransmit( const CCheckTransmitInfo *pInfo, const void *pVSPTState );

private:
	CHandle<C_BaseViewModel> m_hParentViewModel;		// Handle to parent viewmodel
	bool m_bAttached;								// Is currently attached
};

#endif // C_VIEWMODEL_ATTACHMENT_H

//========= Copyright (c) All rights reserved. ============//
//
// Purpose: Client-side viewmodel attachment entity for hands/arms models
//          Attaches to viewmodels and renders hands alongside weapons
//
//=============================================================================//

#include "cbase.h"
#include "c_viewmodel_attachment.h"
#include "c_baseviewmodel.h"
#include "bone_setup.h"
#include "model_types.h"
#include "hands_model_mapping.h"
#include "tier0/memdbgon.h"

// ConVar for overriding hands model
// Set to "auto" to use automatic mapping, or a model path to force a specific hands model
ConVar cl_hands_model( "cl_hands_model", "auto", FCVAR_ARCHIVE, "Override hands model (auto = use player model mapping)" );

//-----------------------------------------------------------------------------
// Purpose: Constructor
//-----------------------------------------------------------------------------
C_ViewmodelAttachment::C_ViewmodelAttachment( void ) : 
	m_hParentViewModel( NULL ),
	m_bAttached( false )
{
}

//-----------------------------------------------------------------------------
// Purpose: Destructor
//-----------------------------------------------------------------------------
C_ViewmodelAttachment::~C_ViewmodelAttachment( void )
{
	DetachFromViewmodel();
}

//-----------------------------------------------------------------------------
// Purpose: Initialize with a hands model path
// Input  : pszModelName - Path to the hands/arms model
// Output : Returns true on success, false on failure
//-----------------------------------------------------------------------------
bool C_ViewmodelAttachment::SetHandsModel( const char *pszModelName )
{
	if ( !pszModelName )
		return false;

	// Set the model (handles precaching internally)
	SetModelName( pszModelName );
	SetModel( pszModelName );

	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Attach to a viewmodel entity
// Input  : pViewModel - The viewmodel to attach to
//-----------------------------------------------------------------------------
void C_ViewmodelAttachment::AttachToViewmodel( C_BaseViewModel *pViewModel )
{
	if ( !pViewModel )
		return;

	// Detach from previous viewmodel if any
	DetachFromViewmodel();

	// Store handle to parent
	m_hParentViewModel = pViewModel;

	// Set as owned by the same entity as the viewmodel
	SetOwnerEntity( pViewModel->GetOwnerEntity() );

	// Spawn and add to client entity list
	Spawn();
	AddToLeafSystem();

	// Set parent for bonemerge
	SetParent( pViewModel );

	// Enable bonemerge
	AddEffects( EF_BONEMERGE );

	m_bAttached = true;
}

//-----------------------------------------------------------------------------
// Purpose: Detach from viewmodel
//-----------------------------------------------------------------------------
void C_ViewmodelAttachment::DetachFromViewmodel( void )
{
	if ( !m_bAttached )
		return;

	// Remove effects
	RemoveEffects( EF_BONEMERGE );

	// Detach from parent
	SetParent( NULL );

	// Remove from client entity list
	RemoveFromLeafSystem();

	m_hParentViewModel = NULL;
	m_bAttached = false;
}

//-----------------------------------------------------------------------------
// Purpose: Draw the hands model
// Input  : flags - Drawing flags
// Output : Number of bones rendered
//-----------------------------------------------------------------------------
int C_ViewmodelAttachment::DrawModel( int flags )
{
	// Don't draw if not attached or no model
	if ( !m_bAttached || !GetModel() )
		return 0;

	// Don't draw if parent viewmodel isn't visible
	C_BaseViewModel *pViewModel = m_hParentViewModel.Get();
	if ( !pViewModel )
		return 0;

	// Use same render settings as parent viewmodel
	float blend = (float)( pViewModel->GetFxBlend() / 255.0f );
	if ( blend <= 0.0f )
		return 0;

	render->SetBlend( blend );

	float color[3];
	pViewModel->GetColorModulation( color );
	render->SetColorModulation( color );

	// Draw with bonemerge from parent
	int ret = BaseClass::DrawModel( flags );

	return ret;
}

//-----------------------------------------------------------------------------
// Purpose: Always transmit to local player
//-----------------------------------------------------------------------------
int C_ViewmodelAttachment::ShouldTransmit( const CCheckTransmitInfo *pInfo, const void *pVSPTState )
{
	// Always transmit - it's attached to a viewmodel
	return FL_EDICT_ALWAYS;
}

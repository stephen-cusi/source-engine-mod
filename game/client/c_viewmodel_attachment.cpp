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
#include "cliententitylist.h"
#include "gamestringpool.h"
#include "tier0/memdbgon.h"

// ConVar for overriding hands model
// Set to "auto" to use automatic mapping, or a model path to force a specific hands model
ConVar cl_hands_model( "cl_hands_model", "auto", FCVAR_ARCHIVE, "Override hands model (auto = use player model mapping)" );

// Wrist-local correction values
ConVar cl_hands_offset_x( "cl_hands_offset_x", "0", FCVAR_ARCHIVE, "Hands model local X offset (wrist space)" );
ConVar cl_hands_offset_y( "cl_hands_offset_y", "0", FCVAR_ARCHIVE, "Hands model local Y offset (wrist space)" );
ConVar cl_hands_offset_z( "cl_hands_offset_z", "0", FCVAR_ARCHIVE, "Hands model local Z offset (wrist space)" );
ConVar cl_hands_angle_pitch( "cl_hands_angle_pitch", "0", FCVAR_ARCHIVE, "Hands model local pitch correction (degrees)" );
ConVar cl_hands_angle_yaw( "cl_hands_angle_yaw", "0", FCVAR_ARCHIVE, "Hands model local yaw correction (degrees)" );
ConVar cl_hands_angle_roll( "cl_hands_angle_roll", "0", FCVAR_ARCHIVE, "Hands model local roll correction (degrees)" );

//-----------------------------------------------------------------------------
// Purpose: Constructor
//-----------------------------------------------------------------------------
C_ViewmodelAttachment::C_ViewmodelAttachment( void ) : 
	m_hParentViewModel( NULL ),
	m_bAttached( false ),
	m_RHandIndex( -1 ),
	m_LHandIndex( -1 ),
	m_bBoneChainCached( false )
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

	// Standard SetModel - server should have precached this model
	SetModelName( AllocPooledString( pszModelName ) );
	bool bSuccess = SetModel( pszModelName );

	if ( bSuccess )
	{
		// Rebuild wrist bone chain cache for the new model
		RebuildBoneChainCache();
	}

	Msg( "[HL2SB-HANDS] SetHandsModel: %s -> %s (GetModel=%p)\n", 
		pszModelName, bSuccess ? "OK" : "FAIL", GetModel() );

	return bSuccess;
}

//-----------------------------------------------------------------------------
// Purpose: Attach to a viewmodel entity using standard Source "follow" method
// Input  : pViewModel - The viewmodel to attach to
//-----------------------------------------------------------------------------
void C_ViewmodelAttachment::AttachToViewmodel( C_BaseViewModel *pViewModel )
{
	if ( !pViewModel )
		return;

	// Store handle to parent
	m_hParentViewModel = pViewModel;

	// Set as owned by the same entity as the viewmodel
	SetOwnerEntity( pViewModel->GetOwnerEntity() );

	// Standard follow attachment (see CBaseEntity::FollowEntity):
	// SetParent + no movement + not solid + zero local transforms + EF_BONEMERGE
	SetParent( pViewModel );
	SetMoveType( MOVETYPE_NONE );
	AddSolidFlags( FSOLID_NOT_SOLID );
	SetLocalOrigin( vec3_origin );
	SetLocalAngles( vec3_angle );
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
	m_bBoneChainCached = false;
	m_RHandChain.RemoveAll();
	m_LHandChain.RemoveAll();
	m_RHandIndex = -1;
	m_LHandIndex = -1;
}

//-----------------------------------------------------------------------------
// Purpose: Rebuild wrist bone chain caches
//          Stores hand bone index + all descendant bones in this model's hierarchy
//-----------------------------------------------------------------------------
void C_ViewmodelAttachment::RebuildBoneChainCache( void )
{
	m_RHandChain.RemoveAll();
	m_LHandChain.RemoveAll();
	m_RHandIndex = -1;
	m_LHandIndex = -1;
	m_bBoneChainCached = false;

	CStudioHdr *hdr = GetModelPtr();
	if ( !hdr )
		return;

	int nBones = hdr->numbones();

	// Find R_Hand and L_Hand bones
	for ( int i = 0; i < nBones; i++ )
	{
		const char *pszBoneName = hdr->pBone( i )->pszName();
		if ( !pszBoneName )
			continue;

		if ( !Q_stricmp( pszBoneName, "ValveBiped.Bip01_R_Hand" ) ||
			 !Q_stricmp( pszBoneName, "VolvoBipod.Bip01_R_Hand" ) )
		{
			m_RHandIndex = i;
		}
		else if ( !Q_stricmp( pszBoneName, "ValveBiped.Bip01_L_Hand" ) ||
				  !Q_stricmp( pszBoneName, "VolvoBipod.Bip01_L_Hand" ) )
		{
			m_LHandIndex = i;
		}
	}

	// Build descendant chains using parent indices
	// For each bone, find all descendants of the hand bone
	if ( m_RHandIndex >= 0 )
	{
		m_RHandChain.AddToTail( m_RHandIndex );
		for ( int i = 0; i < nBones; i++ )
		{
			int nParent = hdr->pBone( i )->parent;
			// Walk up parent chain
			int nCur = nParent;
			bool bDescendant = false;
			int nDepth = 0;
			while ( nCur >= 0 && nDepth < 64 )
			{
				if ( nCur == m_RHandIndex )
				{
					bDescendant = true;
					break;
				}
				nCur = hdr->pBone( nCur )->parent;
				nDepth++;
			}
			if ( bDescendant )
			{
				m_RHandChain.AddToTail( i );
			}
		}
	}

	if ( m_LHandIndex >= 0 )
	{
		m_LHandChain.AddToTail( m_LHandIndex );
		for ( int i = 0; i < nBones; i++ )
		{
			int nParent = hdr->pBone( i )->parent;
			int nCur = nParent;
			bool bDescendant = false;
			int nDepth = 0;
			while ( nCur >= 0 && nDepth < 64 )
			{
				if ( nCur == m_LHandIndex )
				{
					bDescendant = true;
					break;
				}
				nCur = hdr->pBone( nCur )->parent;
				nDepth++;
			}
			if ( bDescendant )
			{
				m_LHandChain.AddToTail( i );
			}
		}
	}

	m_bBoneChainCached = true;

	Msg( "[HL2SB-HANDS] Bone chain cache: R_Hand=%d (chain %d bones), L_Hand=%d (chain %d bones)\n",
		m_RHandIndex, m_RHandChain.Count(), m_LHandIndex, m_LHandChain.Count() );
}

//-----------------------------------------------------------------------------
// Purpose: Setup bones - allow native bonemerge first, then apply wrist-local correction
//-----------------------------------------------------------------------------
bool C_ViewmodelAttachment::SetupBones( matrix3x4_t *pBoneToWorldOut, int nMaxBones, int boneMask, float currentTime )
{
	// First let native bonemerge do its job
	bool bResult = BaseClass::SetupBones( pBoneToWorldOut, nMaxBones, boneMask, currentTime );

	// If we have cached chains and a parent viewmodel, apply wrist-local correction
	if ( m_bBoneChainCached && m_hParentViewModel.Get() )
	{
		ApplyWristCorrection();
	}

	return bResult;
}

//-----------------------------------------------------------------------------
// Purpose: Apply a unified rigid-body correction in each wrist's local space
//          correctionWorld = pivotOld * localCorrection * Inverse(pivotOld)
//          newWorld = correctionWorld * oldWorld  (for every bone in the chain)
//-----------------------------------------------------------------------------
void C_ViewmodelAttachment::ApplyWristCorrection( void )
{
	CStudioHdr *hdr = GetModelPtr();
	if ( !hdr )
		return;

	// Build local correction matrix (rotate first, then translate)
	QAngle correctionAngles( 
		cl_hands_angle_pitch.GetFloat(), 
		cl_hands_angle_yaw.GetFloat(), 
		cl_hands_angle_roll.GetFloat() );
	Vector correctionOffset( 
		cl_hands_offset_x.GetFloat(), 
		cl_hands_offset_y.GetFloat(), 
		cl_hands_offset_z.GetFloat() );

	matrix3x4_t localCorrection;
	AngleMatrix( correctionAngles, correctionOffset, localCorrection );

	// Apply to right hand chain
	if ( m_RHandIndex >= 0 && m_RHandChain.Count() > 0 )
	{
		matrix3x4_t pivotOld = m_BoneAccessor.GetBone( m_RHandIndex );
		matrix3x4_t oldInv;
		MatrixInvert( pivotOld, oldInv );

		// correctionWorld = pivotOld * localCorrection * Inverse(pivotOld)
		matrix3x4_t temp, correctionWorld;
		ConcatTransforms( pivotOld, localCorrection, temp );
		ConcatTransforms( temp, oldInv, correctionWorld );

		for ( int i = 0; i < m_RHandChain.Count(); i++ )
		{
			int iBone = m_RHandChain[i];
			matrix3x4_t oldWorld = m_BoneAccessor.GetBone( iBone );
			matrix3x4_t newWorld;
			ConcatTransforms( correctionWorld, oldWorld, newWorld );
			m_BoneAccessor.GetBoneForWrite( iBone ) = newWorld;
		}
	}

	// Apply to left hand chain
	if ( m_LHandIndex >= 0 && m_LHandChain.Count() > 0 )
	{
		matrix3x4_t pivotOld = m_BoneAccessor.GetBone( m_LHandIndex );
		matrix3x4_t oldInv;
		MatrixInvert( pivotOld, oldInv );

		matrix3x4_t temp, correctionWorld;
		ConcatTransforms( pivotOld, localCorrection, temp );
		ConcatTransforms( temp, oldInv, correctionWorld );

		for ( int i = 0; i < m_LHandChain.Count(); i++ )
		{
			int iBone = m_LHandChain[i];
			matrix3x4_t oldWorld = m_BoneAccessor.GetBone( iBone );
			matrix3x4_t newWorld;
			ConcatTransforms( correctionWorld, oldWorld, newWorld );
			m_BoneAccessor.GetBoneForWrite( iBone ) = newWorld;
		}
	}
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

	// Draw with bone transforms set by SetupBones
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

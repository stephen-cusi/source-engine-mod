//========= Copyright (c) All rights reserved. ============//
//
// Purpose: Client-side viewmodel attachment entity for GMod-style c_hands.
//          Relies on the engine's native EF_BONEMERGE (with optional prefix
//          -stripped bone name matching) for all alignment. No per-bone
//          hand-tuning: GMod c_model weapons animate the ValveBiped arm bones
//          themselves, so merged arms follow the gun automatically. Stock
//          HL2/HL2MP viewmodels use unprefixed "Bip01_*" names, which the
//          lenient merge maps onto the c_arms "ValveBiped.Bip01_*" bones.
//
//=============================================================================//

#include "cbase.h"
#include "c_viewmodel_attachment.h"
#include "c_baseviewmodel.h"
#include "bone_setup.h"
#include "model_types.h"
#include "cliententitylist.h"
#include "gamestringpool.h"
#include "tier0/memdbgon.h"

// Master switch for the c_hands system
ConVar cl_hands( "cl_hands", "1", FCVAR_ARCHIVE, "Show GMod-style viewmodel hands (requires cl_hands_model or a player model mapping)" );

// ConVar for overriding hands model
// Set to "auto" to use automatic mapping, or a model path to force a specific hands model
ConVar cl_hands_model( "cl_hands_model", "auto", FCVAR_ARCHIVE, "Override hands model (auto = use player model mapping)" );

// ViewModel-space offsets applied after bone merge (fine tuning only - for
// correctly rigged c_arms + c_model weapons these should stay at 0)
ConVar cl_hands_offset_x( "cl_hands_offset_x", "0", FCVAR_ARCHIVE, "Hands model viewmodel-space X offset" );
ConVar cl_hands_offset_y( "cl_hands_offset_y", "0", FCVAR_ARCHIVE, "Hands model viewmodel-space Y offset" );
ConVar cl_hands_offset_z( "cl_hands_offset_z", "0", FCVAR_ARCHIVE, "Hands model viewmodel-space Z offset" );
ConVar cl_hands_angle_pitch( "cl_hands_angle_pitch", "0", FCVAR_ARCHIVE, "Hands model viewmodel-space pitch correction (degrees)" );
ConVar cl_hands_angle_yaw( "cl_hands_angle_yaw", "0", FCVAR_ARCHIVE, "Hands model viewmodel-space yaw correction (degrees)" );
ConVar cl_hands_angle_roll( "cl_hands_angle_roll", "0", FCVAR_ARCHIVE, "Hands model viewmodel-space roll correction (degrees)" );

ConVar cl_hands_debug( "cl_hands_debug", "0", FCVAR_ARCHIVE, "Verbose c_hands debug output" );

//-----------------------------------------------------------------------------
// Purpose: Constructor
//-----------------------------------------------------------------------------
C_ViewmodelAttachment::C_ViewmodelAttachment( void ) :
	m_hParentViewModel( NULL ),
	m_bAttached( false ),
	m_iDefaultSequence( -1 ),
	m_flLastOffsetTime( -1.0f )
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
// Purpose: Initialize as a client entity and load the hands model
//-----------------------------------------------------------------------------
bool C_ViewmodelAttachment::SetHandsModel( const char *pszModelName )
{
	if ( !pszModelName || !pszModelName[0] )
		return false;

	// Make sure the model is registered with the client DLL before
	// InitializeAsClientEntity looks up the model index.
	CBaseEntity::PrecacheModel( pszModelName );

	if ( !InitializeAsClientEntity( pszModelName, RENDER_GROUP_OPAQUE_ENTITY ) )
	{
		Warning( "[HL2SB-HANDS] SetHandsModel: InitializeAsClientEntity failed for %s\n", pszModelName );
		return false;
	}

	// We are drawn manually from C_BaseViewModel::DrawModel inside the
	// viewmodel render pass - remove us from the normal leaf-system draws.
	RemoveFromLeafSystem();

	// c_arms rigs merge across rig name conventions (ValveBiped. prefix).
	SetLenientBoneMerge( true );

	// Avoid simulation/solidity nonsense - we only exist to be bonemerged.
	SetMoveType( MOVETYPE_NONE );
	AddSolidFlags( FSOLID_NOT_SOLID );
	SetCollisionGroup( COLLISION_GROUP_NONE );

	Msg( "[HL2SB-HANDS] SetHandsModel: %s OK\n", pszModelName );
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Attach to a viewmodel entity using standard Source "follow" method
//-----------------------------------------------------------------------------
void C_ViewmodelAttachment::AttachToViewmodel( C_BaseViewModel *pViewModel )
{
	if ( !pViewModel )
		return;

	// Store handle to parent
	m_hParentViewModel = pViewModel;

	// Standard follow attachment (see CBaseEntity::FollowEntity):
	// SetParent + MOVETYPE_NONE + not solid + zero local transforms + EF_BONEMERGE
	SetParent( pViewModel );
	SetMoveType( MOVETYPE_NONE );
	AddSolidFlags( FSOLID_NOT_SOLID );
	SetLocalOrigin( vec3_origin );
	SetLocalAngles( vec3_angle );
	AddEffects( EF_BONEMERGE | EF_BONEMERGE_FASTCULL );

	m_bAttached = true;

	if ( cl_hands_debug.GetBool() )
	{
		CStudioHdr *pHdr = GetModelPtr();
		CStudioHdr *pVMHdr = pViewModel->GetModelPtr();
		Msg( "[HL2SB-HANDS] Attached to viewmodel %s (hands bones=%d, weapon bones=%d)\n",
			pVMHdr ? pVMHdr->pszName() : "<none>",
			pHdr ? pHdr->numbones() : 0,
			pVMHdr ? pVMHdr->numbones() : 0 );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Detach from viewmodel
//-----------------------------------------------------------------------------
void C_ViewmodelAttachment::DetachFromViewmodel( void )
{
	if ( !m_bAttached )
		return;

	RemoveEffects( EF_BONEMERGE | EF_BONEMERGE_FASTCULL );
	SetParent( NULL );

	m_hParentViewModel = NULL;
	m_bAttached = false;
}

//-----------------------------------------------------------------------------
// Purpose: Pick the fallback sequence for a freshly loaded c_arms model.
//          "proportions" (autoplay predelta) fixes bone proportions, "idle" is
//          the usual relaxed pose, "reference" is the bind pose.
//-----------------------------------------------------------------------------
CStudioHdr *C_ViewmodelAttachment::OnNewModel( void )
{
	CStudioHdr *pNewHdr = BaseClass::OnNewModel();

	m_iDefaultSequence = -1;

	CStudioHdr *pHdr = GetModelPtr();
	if ( !pHdr || !pHdr->SequencesAvailable() )
		return pNewHdr;

	int iProp = LookupSequence( "proportions" );
	int iIdle = LookupSequence( "idle" );
	int iRef = LookupSequence( "reference" );

	m_iDefaultSequence = ( iProp >= 0 ) ? iProp : ( ( iIdle >= 0 ) ? iIdle : iRef );

	if ( m_iDefaultSequence >= 0 && GetSequence() < 0 )
	{
		SetSequence( m_iDefaultSequence );
		SetCycle( 0.0f );
	}

	// The bone merge cache is rebuilt lazily; if it already exists for a
	// previous model, drop it so the next setup rebuilds with lenient matching.
	if ( m_pBoneMergeCache )
	{
		m_pBoneMergeCache->SetLenientNameMatching( true );
	}

	if ( cl_hands_debug.GetBool() )
	{
		Msg( "[HL2SB-HANDS] OnNewModel: %s (%d bones), default sequence %d\n",
			pHdr->pszName(), pHdr->numbones(), m_iDefaultSequence );
	}

	return pNewHdr;
}

//-----------------------------------------------------------------------------
// Purpose: Per-frame animation sync with the parent viewmodel
//-----------------------------------------------------------------------------
void C_ViewmodelAttachment::SyncToViewModel( C_BaseViewModel *pViewModel )
{
	if ( !pViewModel || !GetModelPtr() || !pViewModel->GetModelPtr() )
		return;

	if ( !GetModelPtr()->SequencesAvailable() )
		return;

	// Lazy (re)compute of the default sequence - OnNewModel may have run before
	// the model data (sequences) was available.
	if ( m_iDefaultSequence < 0 )
	{
		int iProp = LookupSequence( "proportions" );
		int iIdle = LookupSequence( "idle" );
		int iRef = LookupSequence( "reference" );
		m_iDefaultSequence = ( iProp >= 0 ) ? iProp : ( ( iIdle >= 0 ) ? iIdle : iRef );
	}

	const char *pszVMSeq = pViewModel->GetSequenceName( pViewModel->GetSequence() );
	if ( !pszVMSeq || !pszVMSeq[0] )
		return;

	int iSeq = LookupSequence( pszVMSeq );
	if ( iSeq >= 0 )
	{
		// Same-named sequence exists in the hands model: play it in lockstep
		// (this is the fallback that aligns arms on weapons whose animations
		// don't move the arm bones).
		if ( GetSequence() != iSeq )
		{
			SetSequence( iSeq );
		}
		SetCycle( pViewModel->GetCycle() );
		SetPlaybackRate( pViewModel->GetPlaybackRate() );
	}
	else if ( m_iDefaultSequence >= 0 && GetSequence() != m_iDefaultSequence )
	{
		SetSequence( m_iDefaultSequence );
		SetCycle( 0.0f );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Setup bones - native bonemerge does all alignment; we only apply the
//          optional viewmodel-space correction afterwards.
//-----------------------------------------------------------------------------
bool C_ViewmodelAttachment::SetupBones( matrix3x4_t *pBoneToWorldOut, int nMaxBones, int boneMask, float currentTime )
{
	bool bResult = BaseClass::SetupBones( pBoneToWorldOut, nMaxBones, boneMask, currentTime );

	// Apply at most once per frame (BaseClass::SetupBones early-outs on
	// repeated same-time calls, and the correction is not idempotent).
	if ( m_flLastOffsetTime != currentTime )
	{
		m_flLastOffsetTime = currentTime;
		ApplyHandsOffset();
	}

	return bResult;
}

//-----------------------------------------------------------------------------
// Purpose: Rigid viewmodel-space correction from cl_hands_offset_/angle_ cvars
//          newWorld = vmXform * localCorrection * Inverse(vmXform) * oldWorld
//-----------------------------------------------------------------------------
void C_ViewmodelAttachment::ApplyHandsOffset( void )
{
	QAngle angOffset(
		cl_hands_angle_pitch.GetFloat(),
		cl_hands_angle_yaw.GetFloat(),
		cl_hands_angle_roll.GetFloat() );
	Vector vecOffset(
		cl_hands_offset_x.GetFloat(),
		cl_hands_offset_y.GetFloat(),
		cl_hands_offset_z.GetFloat() );

	if ( vecOffset == vec3_origin && angOffset == vec3_angle )
		return;

	C_BaseViewModel *pViewModel = m_hParentViewModel.Get();
	if ( !pViewModel )
		return;

	CStudioHdr *pHdr = GetModelPtr();
	if ( !pHdr )
		return;

	// Build the correction in viewmodel space so X/Y/Z stay intuitive
	// regardless of where the viewmodel entity sits in the world.
	matrix3x4_t vmXform = pViewModel->EntityToWorldTransform();
	matrix3x4_t vmInv;
	MatrixInvert( vmXform, vmInv );

	matrix3x4_t localCorrection;
	AngleMatrix( angOffset, vecOffset, localCorrection );

	matrix3x4_t temp, correctionWorld;
	ConcatTransforms( vmXform, localCorrection, temp );
	ConcatTransforms( temp, vmInv, correctionWorld );

	int nBones = pHdr->numbones();
	for ( int i = 0; i < nBones; i++ )
	{
		const matrix3x4_t &oldWorld = m_BoneAccessor.GetBone( i );
		matrix3x4_t newWorld;
		ConcatTransforms( correctionWorld, oldWorld, newWorld );
		m_BoneAccessor.GetBoneForWrite( i ) = newWorld;
	}
}

//-----------------------------------------------------------------------------
// Purpose: Draw the hands model
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

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
#include "hands_model_mapping.h"
#include "bone_setup.h"
#include "model_types.h"
#include "cliententitylist.h"
#include "gamestringpool.h"
#include "materialsystem/imaterialproxy.h"
#include "materialsystem/imaterialproxyfactory.h"
#include "materialsystem/imaterial.h"
#include "materialsystem/imaterialvar.h"
#include "c_baseplayer.h"
// HL2SB: LUA_NOREF / the entity's Lua table reference, for the PlayerColor proxy's
// "is this a Lua-created clientside model?" test.
#include "luamanager.h"
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

// GMod "PlayerColor"?-style player sleeve color. GMod tints the c_arms sleeves
// with each player's own colour (player:GetPlayerColor), defaulting to a teal
// (62,88,106)/255. Here the local player's sleeve colour is a client convar.
ConVar hl2sb_player_color( "hl2sb_player_color", "0.243 0.345 0.416", FCVAR_ARCHIVE,
	"c_arms sleeve tint (PlayerColor proxy). GMod default teal 62/88/106. Format: 'r g b'" );

//-----------------------------------------------------------------------------
// GMod-style per-player sleeve colour. The server sends "hl2sb_setplayercolor
// <r> <g> <b> <a>" via ClientCommand to a client when a script calls
// player:SetPlayerColor; this command stores it for the local player so the
// PlayerColor proxy renders the tint (the colour decision stays in Lua).
//-----------------------------------------------------------------------------
static void CC_HL2SB_SetPlayerColor( const CCommand &args )
{
	C_BasePlayer *pLocal = C_BasePlayer::GetLocalPlayer();
	if ( !pLocal || args.ArgC() < 4 )
		return;
	int r = atoi( args[1] ), g = atoi( args[2] ), b = atoi( args[3] );
	int a = ( args.ArgC() >= 5 ) ? atoi( args[4] ) : 255;
	HL2SB_SetPlayerColor( pLocal->GetUserID(), Color( r, g, b, a ) );
}
static ConCommand hl2sb_setplayercolor( "hl2sb_setplayercolor", CC_HL2SB_SetPlayerColor,
	"Set the local player's sleeve colour (server sends this). Usage: hl2sb_setplayercolor <r> <g> <b> <a>" );

//-----------------------------------------------------------------------------
// Global registry of live hands-attachment entities. Every weapon viewmodel
// owns at most one of these, and they are all destroyed on player spawn / level
// change so nothing can leak from a previous session. HL2SB_AnyHandsInLeafSystem
// exists to prove the important invariant: none of them may be registered in the
// leaf system (that is what made the engine draw a ghost second arm).
//-----------------------------------------------------------------------------
static CUtlVector< CHandle< C_ViewmodelAttachment > > s_HandsAttachments;

void HL2SB_RegisterHandsAttachment( C_ViewmodelAttachment *pAttach )
{
	if ( pAttach )
		s_HandsAttachments.AddToTail( pAttach );
}

void HL2SB_UnregisterHandsAttachment( C_ViewmodelAttachment *pAttach )
{
	if ( pAttach )
		s_HandsAttachments.FindAndRemove( pAttach );
}

void HL2SB_DestroyAllHandsAttachments( void )
{
	for ( int i = s_HandsAttachments.Count() - 1; i >= 0; --i )
	{
		C_ViewmodelAttachment *p = s_HandsAttachments[ i ].Get();
		if ( p )
			p->Release();
	}
	s_HandsAttachments.RemoveAll();
}

int HL2SB_CountLiveHandsAttachments( void )
{
	// Prune dead entries so the count reflects real entities only.
	for ( int i = s_HandsAttachments.Count() - 1; i >= 0; --i )
	{
		if ( !s_HandsAttachments[ i ].Get() )
			s_HandsAttachments.Remove( i );
	}
	return s_HandsAttachments.Count();
}

bool HL2SB_AnyHandsInLeafSystem( void )
{
	for ( int i = 0; i < s_HandsAttachments.Count(); ++i )
	{
		C_ViewmodelAttachment *p = s_HandsAttachments[ i ].Get();
		if ( p && p->IsInLeafSystem() )
			return true;
	}
	return false;
}

//-----------------------------------------------------------------------------
// Verification aid: how many times the arms were really rendered in the most
// recent client frame (exactly 1 is correct) and how many of those draws came
// from somewhere other than the viewmodel pass (must stay 0). hl2sb_status
// reports both, so a regression is observable on demand instead of via console
// spam.
//-----------------------------------------------------------------------------
static int s_iHandsDrawFrame = -1;
static int s_iHandsDrawCount = 0;
static int s_iManualHandsDrawDepth = 0;
static int s_iGhostHandsDraws = 0;

void HL2SB_BeginManualHandsDraw( void )
{
	++s_iManualHandsDrawDepth;
}

void HL2SB_EndManualHandsDraw( void )
{
	--s_iManualHandsDrawDepth;
	if ( s_iManualHandsDrawDepth < 0 )
		s_iManualHandsDrawDepth = 0;
}

void HL2SB_NoteHandsDraw( int flags )
{
	if ( !( flags & STUDIO_RENDER ) )
		return;

	if ( s_iHandsDrawFrame != gpGlobals->framecount )
	{
		s_iHandsDrawFrame = gpGlobals->framecount;
		s_iHandsDrawCount = 0;
	}
	++s_iHandsDrawCount;

	// A draw that did not come through C_BaseViewModel::DrawModel is the ghost
	// second arm: ShouldDraw() returning false is what prevents it, so a non-zero
	// count here means that invariant has been broken again.
	if ( s_iManualHandsDrawDepth == 0 )
		++s_iGhostHandsDraws;
}

int HL2SB_HandsDrawCountLastFrame( void )
{
	// The count of the most recent frame that actually rendered hands. Console
	// commands run before the frame is drawn, so comparing against the current
	// frame number would always read 0 - report the last real frame instead.
	return s_iHandsDrawCount;
}

int HL2SB_GhostHandsDrawCount( void )
{
	return s_iGhostHandsDraws;
}

// When enabled, skip merging hands onto viewmodels that already draw their own
// arms (stock HL2/EP2/HL2MP weapon viewmodels, which reference the shared
// "v_hand" material). Merging an extra pair onto those double-draws the arms
// (visible on SLAM, grenade, crowbar, ...). MMOD-style replacement viewmodels
// are gun-only (no v_hand material) and still receive the merged hands.
// HL2SB: on by default - baked-arm weapons (detected by scanning the studio
// texture table for a "v_hand" material, see ViewModelHasBakedArms) get their
// own arms and must not receive a second, mis-merged c_hands attachment (that
// shows up as a detached pair of arms floating after death/respawn). Set to 0
// only if you run gun-only c_* viewmodels with no baked arms everywhere.
ConVar cl_hands_skip_baked_arms( "cl_hands_skip_baked_arms", "1", FCVAR_ARCHIVE, "Don't merge c_hands onto viewmodels that already have their own arms (stock HL2 v_hand models)" );

//-----------------------------------------------------------------------------
// Purpose: Constructor
//-----------------------------------------------------------------------------
C_ViewmodelAttachment::C_ViewmodelAttachment( void ) :
	m_hParentViewModel( NULL ),
	m_bAttached( false ),
	m_iDefaultSequence( -1 ),
	m_flLastOffsetTime( -1.0f )
{
	Q_strncpy( m_szHandsModelName, "", sizeof( m_szHandsModelName ) );
	Q_strncpy( m_szHandsKey, "", sizeof( m_szHandsKey ) );
}

//-----------------------------------------------------------------------------
// Purpose: Record the "model|skin|body" key this entity implements, so the
//          owning viewmodel can tell whether it already has the right hands.
//-----------------------------------------------------------------------------
void C_ViewmodelAttachment::SetHandsKey( const char *pszKey )
{
	Q_strncpy( m_szHandsKey, pszKey ? pszKey : "", sizeof( m_szHandsKey ) );
}

//-----------------------------------------------------------------------------
// Purpose: Destructor
//-----------------------------------------------------------------------------
C_ViewmodelAttachment::~C_ViewmodelAttachment( void )
{
	HL2SB_UnregisterHandsAttachment( this );
	DetachFromViewmodel();
}

//-----------------------------------------------------------------------------
// Purpose: Initialize as a client entity and load the hands model
//-----------------------------------------------------------------------------
bool C_ViewmodelAttachment::SetHandsModel( const char *pszModelName )
{
	if ( !pszModelName || !pszModelName[0] )
		return false;

	// Register the hands model in the client model pool *by name*. Client-side
	// CBaseEntity::PrecacheModel is just an index lookup, and engine->LoadModel
	// alone can leave the name unindexed: on a fresh connection (where the
	// server's precache of these player-only c_arms models hasn't arrived yet)
	// GetModelIndex returned -1 and InitializeAsClientEntity failed, which
	// dropped the hands and spammed on every reconnect. RegisterDynamicModel
	// guarantees a valid index (bClientSide = don't wait for network precache),
	// and if the model data is still loading asynchronously C_BaseAnimating's
	// own OnNewModel load-callback completes the bones/skins when it arrives.
	int iModelIndex = modelinfo->GetModelIndex( pszModelName );
	if ( iModelIndex == -1 )
	{
		iModelIndex = modelinfo->RegisterDynamicModel( pszModelName, true );
	}
	if ( iModelIndex == -1 )
	{
		Warning( "[HL2SB-HANDS] SetHandsModel: could not register model %s\n", pszModelName );
		return false;
	}

	if ( !InitializeAsClientEntityByIndex( iModelIndex, RENDER_GROUP_OPAQUE_ENTITY ) )
	{
		Warning( "[HL2SB-HANDS] SetHandsModel: InitializeAsClientEntity failed for %s\n", pszModelName );
		return false;
	}

	Q_strncpy( m_szHandsModelName, pszModelName, sizeof( m_szHandsModelName ) );

	// We are drawn manually from C_BaseViewModel::DrawModel inside the
	// viewmodel render pass, so take us out of the normal leaf-system draws.
	//
	// IMPORTANT: this call alone is NOT sufficient - it is a one-shot removal
	// and the base class re-adds the renderable whenever it recomputes
	// visibility (C_BaseEntity::SetModelPointer, C_BaseAnimating::
	// OnModelLoadComplete -> UpdateVisibility, SetDormant, ...). That is the
	// actual reason the arms "proliferated" from the second map onwards: on the
	// first load the arms .mdl was already resident so the entity was built
	// synchronously and stayed removed, but on later loads the model arrives
	// asynchronously, OnModelLoadComplete() runs UpdateVisibility() and the
	// arms get re-added to the *world* leaf list. The engine then draws a
	// second, stale-transform copy of the merged arms next to the real
	// viewmodel hands - the extra floating hand. ShouldDraw() below is the
	// authoritative gate that keeps us out of the leaf system permanently.
	RemoveFromLeafSystem();

	// c_arms rigs merge across rig name conventions (ValveBiped. prefix).
	SetLenientBoneMerge( true );

	// Avoid simulation/solidity nonsense - we only exist to be bonemerged.
	SetMoveType( MOVETYPE_NONE );
	AddSolidFlags( FSOLID_NOT_SOLID );
	SetCollisionGroup( COLLISION_GROUP_NONE );

	// Track this entity so a respawn/level-change can destroy any leftovers.
	HL2SB_RegisterHandsAttachment( this );

	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Never let the world renderer draw us.
//
// The arms exist only to be bone-merged onto a weapon viewmodel and are drawn
// explicitly by C_BaseViewModel::DrawModel during the viewmodel pass, which is
// the only pass that gets the viewmodel's transform, depth range and blending
// right. If this entity is in the leaf system the world pass draws it a second
// time at the raw viewmodel origin (in front of the camera, unclipped), which
// is what looks like an extra arm growing out of nowhere.
//
// C_BaseEntity::UpdateVisibility() is the single funnel that adds or removes a
// renderable, and it keys off ShouldDraw(); returning false here therefore
// makes the removal stick no matter how often the engine re-evaluates us.
//-----------------------------------------------------------------------------
bool C_ViewmodelAttachment::ShouldDraw( void )
{
	return false;
}

//-----------------------------------------------------------------------------
// Purpose: Are we currently merged onto this exact viewmodel?
//-----------------------------------------------------------------------------
bool C_ViewmodelAttachment::IsAttachedTo( C_BaseViewModel *pViewModel ) const
{
	return ( m_bAttached && m_hParentViewModel.Get() == pViewModel );
}

//-----------------------------------------------------------------------------
// Purpose: Debug/verification - must always be false (see ShouldDraw).
//-----------------------------------------------------------------------------
bool C_ViewmodelAttachment::IsInLeafSystem( void ) const
{
	return GetRenderHandle() != INVALID_CLIENT_RENDER_HANDLE;
}

//-----------------------------------------------------------------------------
// Purpose: Attach to a viewmodel entity using standard Source "follow" method
//-----------------------------------------------------------------------------
void C_ViewmodelAttachment::AttachToViewmodel( C_BaseViewModel *pViewModel )
{
	if ( !pViewModel )
		return;

	// Idempotent: re-attaching to the same viewmodel must not churn the
	// transform/effects (this runs from the hands update path, which fires on
	// every viewmodel data change).
	if ( IsAttachedTo( pViewModel ) )
		return;

	// Moving to a different viewmodel: drop the old parentage first so we are
	// never merged onto an entity we no longer belong to.
	if ( m_bAttached )
		DetachFromViewmodel();

	// Store handle to parent
	m_hParentViewModel = pViewModel;

	// Standard follow attachment (see CBaseEntity::FollowEntity):
	// SetParent + MOVETYPE_NONE + not solid + zero local transforms + EF_BONEMERGE
	SetParent( pViewModel );
	SetMoveType( MOVETYPE_NONE );
	AddSolidFlags( FSOLID_NOT_SOLID );
	SetLocalOrigin( vec3_origin );
	SetLocalAngles( vec3_angle );
	// NOTE: no EF_BONEMERGE_FASTCULL - it re-places the render origin at the
	// parent's WorldSpaceCenter (the viewmodel sits at the camera near-plane),
	// which flings the merged arms out of view. We draw the arms manually from
	// C_BaseViewModel::DrawModel, so the leaf-cull optimization isn't needed.
	AddEffects( EF_BONEMERGE );

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

	RemoveEffects( EF_BONEMERGE );
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

	// Prefer a gripping hold pose ("idle") over "proportions". "proportions" is
	// an autoplay-build pose with the fingers spread for calibration; a c_arms
	// rig resting on it looks like the hands are open/floating beside the gun.
	// "idle" is the relaxed two-handed grip and reads correctly on the weapon.
	m_iDefaultSequence = ( iIdle >= 0 ) ? iIdle : ( ( iRef >= 0 ) ? iRef : iProp );

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

	// Only draw when the parent viewmodel is the one the owner is actually
	// holding out front. A stale attachment left over from a previous
	// death/respawn or weapon switch is still parented to an old (non-active or
	// orphaned) viewmodel; without this gate it lingers and renders as a
	// detached pair of hands floating in the world after you die.
	//
	// NOTE: this deliberately does NOT compare against GetViewModel(0).
	// MAX_VIEWMODELS is 2 and weapons are spread across those slots, so that
	// test silently killed the arms for every weapon not living in slot 0.
	C_BasePlayer *pOwner = ToBasePlayer( pViewModel->GetOwner() );
	if ( !pOwner || !pOwner->IsAlive() )
		return 0;

	C_BaseCombatWeapon *pActive = pOwner->GetActiveWeapon();
	C_BaseCombatWeapon *pVmWeapon = pViewModel->GetOwningWeapon();
	if ( pActive && pVmWeapon && pActive != pVmWeapon )
		return 0;

	// Use same render settings as parent viewmodel
	float blend = (float)( pViewModel->GetFxBlend() / 255.0f );
	if ( blend <= 0.0f )
		return 0;

	render->SetBlend( blend );

	float color[3];
	pViewModel->GetColorModulation( color );
	render->SetColorModulation( color );

	// Draw the merged bones directly. We must NOT call BaseClass::DrawModel here:
	// as an EF_BONEMERGE follower of the viewmodel, C_BaseAnimating::DrawModel would
	// take the "follow" branch (follow->DrawModel(0) to refresh the master, then
	// gate the child render on that returning non-zero). Called with the viewmodel's
	// flags, that reentrant master draw returns 0 (no STUDIO_RENDER) so our arms
	// would never render. InternalDrawModel runs SetupBones (which does the
	// bonemerge) and draws directly - matching the old visible path.
	HL2SB_NoteHandsDraw( flags );
	int ret = InternalDrawModel( flags );

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

//-----------------------------------------------------------------------------
// Purpose: "PlayerColor" material proxy - tints a material (e.g. the GMod
//          c_arms sleeves) by the local player's color, so each player's
//          arms/sleeves render in their own color. This is the proxy the GMod
//          c_arms_citizen_sleeves.vmt references via:
//            Proxies { PlayerColor { resultVar $color2 default 0.2 0.4 0.7 } }
//          HL2SB did not register it, so those materials failed to compile.
//-----------------------------------------------------------------------------
class CPlayerColorProxy : public IMaterialProxy
{
public:
	CPlayerColorProxy( void ) : m_pColor( NULL ), m_bArmsMaterial( false )
	{
		m_flDefault[0] = 0.2f; m_flDefault[1] = 0.4f; m_flDefault[2] = 0.7f;
	}
	virtual ~CPlayerColorProxy( void ) { }

	virtual bool Init( IMaterial *pMaterial, KeyValues *pKeyValues )
	{
		bool found = false;
		const char *pszResultVar = pKeyValues->GetString( "resultVar", "$color2" );
		m_pColor = pMaterial->FindVar( pszResultVar, &found, false );

		// HL2SB: which colour this material belongs to is a property of the MATERIAL, not
		// of whoever is drawing it.  The arm/sleeve materials are the c_arms_* chain, and
		// deciding by entity class broke as soon as the player model selector opened: a
		// non-matching draw of the same material (the menu's own clientside models) wrote
		// the default white over the arms (2026-09-17).
		const char *pszMatName = pMaterial->GetName();
		m_bArmsMaterial = ( pszMatName != NULL && Q_stristr( pszMatName, "c_arms" ) != NULL );

		// Parse the "default" color (e.g. "0.2 0.4 0.7").
		const char *pszDefault = pKeyValues->GetString( "default", NULL );
		if ( pszDefault )
		{
			sscanf( pszDefault, "%f %f %f", &m_flDefault[0], &m_flDefault[1], &m_flDefault[2] );
		}

		return m_pColor != NULL;
	}

	virtual void OnBind( void *pBindable )
	{
		if ( !m_pColor )
			return;

		float r = m_flDefault[0], g = m_flDefault[1], b = m_flDefault[2];

		// HL2SB: the arm/sleeve chain always wears the WEAPON colour (cl_weaponcolor), and
		// that is decided by the MATERIAL - so no other entity drawing the same material can
		// knock it back to white (opening the player model selector did exactly that,
		// 2026-09-17).
		if ( m_bArmsMaterial )
		{
			static ConVarRef s_cl_arms_weaponcolor( "cl_weaponcolor" );

			if ( s_cl_arms_weaponcolor.IsValid() )
			{
				const char *pszCol = s_cl_arms_weaponcolor.GetString();

				if ( pszCol && pszCol[0] )
					sscanf( pszCol, "%f %f %f", &r, &g, &b );
			}

			m_pColor->SetVecValue( clamp( r, 0.0f, 1.5f ), clamp( g, 0.0f, 1.5f ), clamp( b, 0.0f, 1.5f ) );
			return;
		}

		// HL2SB: GMod modulates *your* model with *your* colour, so take the player from
		// the entity being drawn.  The old code always read the LOCAL player's entry of
		// HL2SB_GetPlayerColor(), which painted every model on screen in the local
		// player's colour (2026-09-17).
		C_BaseEntity *pEntity = NULL;

		if ( pBindable != NULL )
		{
			IClientRenderable *pRenderable = ( IClientRenderable * )pBindable;
			IClientUnknown *pUnknown = pRenderable->GetIClientUnknown();

			if ( pUnknown != NULL )
			{
				pEntity = pUnknown->GetBaseEntity();
			}
		}

		C_BasePlayer *pLocal = C_BasePlayer::GetLocalPlayer();
		C_BasePlayer *pPlayer = NULL;

		if ( pEntity != NULL )
		{
			if ( pEntity->IsPlayer() )
			{
				pPlayer = ToBasePlayer( pEntity );
			}
			else if ( pLocal != NULL && pEntity->m_nTableReference != LUA_NOREF )
			{
				// A Lua-created clientside model (the player model selector's own preview:
				// lua/vgui/DModelPanel.lua's ClientsideModel, which the editor also gives a
				// GetPlayerColor field) follows the local player's colour.  The model list's
				// THUMBNAILS are entities the engine owns and never hand to Lua, so they have
				// no table and stay untouched - which is what keeps the grid from all turning
				// one colour (2026-09-17).
				pPlayer = pLocal;
			}
			else if ( pLocal != NULL )
			{
				// GMod-style separation, as asked for on 2026-09-17: the ARMS follow the
				// WEAPON colour (cl_weaponcolor) while the player MODEL follows the player
				// colour (cl_playercolor).  The arms' VMTs declare "PlayerColor", so they
				// used to move with the skin mixer and the two were inseparable - the
				// weapon mixer looked dead.  The weapon sheets keep their own
				// PlayerWeaponColor proxy.
				const char *pszClass = pEntity->GetClassname();

				if ( pszClass != NULL &&
					 ( Q_stristr( pszClass, "viewmodel" ) != NULL ||
					   Q_stristr( pszClass, "arms" ) != NULL ||
					   Q_stristr( pszClass, "hands" ) != NULL ) )
				{
					static ConVarRef s_cl_weaponcolor( "cl_weaponcolor" );

					if ( s_cl_weaponcolor.IsValid() )
					{
						const char *pszCol = s_cl_weaponcolor.GetString();

						if ( pszCol && pszCol[0] )
							sscanf( pszCol, "%f %f %f", &r, &g, &b );
					}

					r = clamp( r, 0.0f, 1.5f );
					g = clamp( g, 0.0f, 1.5f );
					b = clamp( b, 0.0f, 1.5f );

					m_pColor->SetVecValue( r, g, b );
					return;
				}
			}
		}

		if ( pPlayer != NULL )
		{
			if ( pPlayer == pLocal )
			{
				// straight from the convar, so a colour change shows up immediately and
				// nothing has to write the Lua colour table
				static ConVarRef s_cl_playercolor( "cl_playercolor" );

				if ( s_cl_playercolor.IsValid() )
				{
					const char *pszCol = s_cl_playercolor.GetString();

					if ( pszCol && pszCol[0] )
					{
						sscanf( pszCol, "%f %f %f", &r, &g, &b );
					}
				}
			}
			else
			{
				Color c = HL2SB_GetPlayerColor( pPlayer->GetUserID() );
				r = c.r() / 255.0f;
				g = c.g() / 255.0f;
				b = c.b() / 255.0f;
			}
		}
		else
		{
			// Nothing to tint: the material keeps its default value, i.e. the model is
			// drawn as authored (this is the model list's thumbnails and every other
			// clientside model).  The old code fell back to the hl2sb_player_color convar
			// here, whose GMod-teal default tinted them all.
			r = m_flDefault[0];
			g = m_flDefault[1];
			b = m_flDefault[2];
		}

		// Clamp to the 0.01..1.5 range the sleeve vmt's Clamp proxy expects.
		r = clamp( r, 0.01f, 1.5f );
		g = clamp( g, 0.01f, 1.5f );
		b = clamp( b, 0.01f, 1.5f );

		m_pColor->SetVecValue( r, g, b );
	}

	virtual void Release( void ) { }

	virtual IMaterial *GetMaterial( void ) { return NULL; }

private:
	IMaterialVar	*m_pColor;
	float			m_flDefault[3];
	bool			m_bArmsMaterial;
};

EXPOSE_INTERFACE( CPlayerColorProxy, IMaterialProxy, "PlayerColor" IMATERIAL_PROXY_INTERFACE_VERSION );

//-----------------------------------------------------------------------------
// HL2SB: "PlayerWeaponColor" material proxy - the same idea for the weapon chain.
//
// The GMod weapon sheets and the cstrike / combine arms reference it:
//
//     Proxies { PlayerWeaponColor { resultVar $color2 default 1 1 1 } }
//
// and NOTHING implemented it, so the material system answered
//
//     Error: Material "models/weapons/v_physcannon/v_superphyscannon_sheet"
//            : proxy "PlayerWeaponColor" not found!
//
// and those materials stayed untinted (that is the "weapon colour does nothing /
// the arms have no colour" report, 2026-09-17).  The colour is cl_weaponcolor, the
// convar the player model selector's weapon mixer writes.
//-----------------------------------------------------------------------------
class CPlayerWeaponColorProxy : public IMaterialProxy
{
public:
	CPlayerWeaponColorProxy( void ) : m_pColor( NULL )
	{
		m_flDefault[0] = m_flDefault[1] = m_flDefault[2] = 1.0f;
	}
	virtual ~CPlayerWeaponColorProxy( void ) { }

	virtual bool Init( IMaterial *pMaterial, KeyValues *pKeyValues )
	{
		bool found = false;
		const char *pszResultVar = pKeyValues->GetString( "resultVar", "$color2" );
		m_pColor = pMaterial->FindVar( pszResultVar, &found, false );

		const char *pszDefault = pKeyValues->GetString( "default", NULL );
		if ( pszDefault )
		{
			sscanf( pszDefault, "%f %f %f", &m_flDefault[0], &m_flDefault[1], &m_flDefault[2] );
		}

		return m_pColor != NULL;
	}

	virtual void OnBind( void *pBindable )
	{
		if ( !m_pColor )
			return;

		float r = m_flDefault[0], g = m_flDefault[1], b = m_flDefault[2];

		static ConVarRef s_cl_weaponcolor( "cl_weaponcolor" );

		if ( s_cl_weaponcolor.IsValid() )
		{
			const char *pszCol = s_cl_weaponcolor.GetString();

			if ( pszCol && pszCol[0] )
			{
				sscanf( pszCol, "%f %f %f", &r, &g, &b );
			}
		}

		r = clamp( r, 0.0f, 1.0f );
		g = clamp( g, 0.0f, 1.0f );
		b = clamp( b, 0.0f, 1.0f );

		m_pColor->SetVecValue( r, g, b );
	}

	virtual void Release( void ) { }

	virtual IMaterial *GetMaterial( void ) { return NULL; }

private:
	IMaterialVar	*m_pColor;
	float			m_flDefault[3];
};

EXPOSE_INTERFACE( CPlayerWeaponColorProxy, IMaterialProxy, "PlayerWeaponColor" IMATERIAL_PROXY_INTERFACE_VERSION );

// HL2SB: CPlayerColorProxy was only EXPOSE_INTERFACE'd, which never registers it
// with the material system, so any vmt's "PlayerColor" proxy (the GMod player
// model body / sleeve tint chain) found no handler and the colour did nothing.
// Register a proxy factory (chain-preserving) so "PlayerColor" proxies bind.
class CPlayerColorProxyFactory : public IMaterialProxyFactory
{
public:
	CPlayerColorProxyFactory() : m_pOld( NULL ), m_bRegistered( false ) { }
	virtual IMaterialProxy *CreateProxy( const char *proxyName )
	{
		if ( proxyName && !Q_stricmp( proxyName, "PlayerColor" ) )
			return new CPlayerColorProxy;
		if ( proxyName && !Q_stricmp( proxyName, "PlayerWeaponColor" ) )
			return new CPlayerWeaponColorProxy;
		return m_pOld ? m_pOld->CreateProxy( proxyName ) : NULL;
	}
	virtual void DeleteProxy( IMaterialProxy *pProxy )
	{
		if ( pProxy )
			pProxy->Release();
	}
	void SetOld( IMaterialProxyFactory *pOld ) { m_pOld = pOld; }
	bool m_bRegistered;
private:
	IMaterialProxyFactory *m_pOld;
};

static CPlayerColorProxyFactory g_PlayerColorProxyFactory;

// Non-static: called from lModelPanel luaopen_vgui_ModelPanel (client Lua init,
// after the material system is up and before player-model materials compile).
void RegisterPlayerColorProxyFactory()
{
	// Try every time (not gated by m_bRegistered): luaopen_vgui runs for both
	// LGameUI and the game Lua L, and the first one (GameUI) may run before the
	// game material system is ready, so a one-shot gate would skip the good one.
	IMaterialProxyFactory *pNew = &g_PlayerColorProxyFactory;
	if ( materials && materials->GetMaterialProxyFactory() != pNew )
	{
		IMaterialProxyFactory *pOld = materials->GetMaterialProxyFactory();
		g_PlayerColorProxyFactory.SetOld( pOld );
		materials->SetMaterialProxyFactory( pNew );
	}
	Msg( "[HL2SB] PlayerColorProxyFactory: materials=%s\n", materials ? "yes" : "NO" );
}

// Register as soon as the client DLL is loaded (static init).  The material
// system is up before the game DLL is loaded, so `materials` is valid here;
// the proxy factory must be set before any model material that uses a
// "PlayerColor" proxy is compiled.
struct CPlayerColorProxyFactoryReg
{
	CPlayerColorProxyFactoryReg() { RegisterPlayerColorProxyFactory(); }
} g_PlayerColorProxyFactoryReg;



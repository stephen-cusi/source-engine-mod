//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Client side view model implementation. Responsible for drawing
//			the view model.
//
// $NoKeywords: $
//=============================================================================//
#include "cbase.h"
#include "c_baseviewmodel.h"
#include "c_viewmodel_attachment.h"
#include "model_types.h"
#include "hud.h"
#include "view_shared.h"
#include "iviewrender.h"
#include "view.h"
#include "mathlib/vmatrix.h"
#include "cl_animevent.h"
#include "eventlist.h"
#include "tools/bonelist.h"
#include <KeyValues.h>
#include "hltvcamera.h"
#include "hl2sb_model_config.h"
#include "hands_model_mapping.h"

#if defined( REPLAY_ENABLED )
#include "replay/replaycamera.h"
#include "replay/ireplaysystem.h"
#include "replay/ienginereplay.h"
#endif

// NVNT haptics system interface
#include "haptics/ihaptics.h"


// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

ConVar cl_righthand( "cl_righthand", "1", FCVAR_ARCHIVE, "Use right-handed view models." );

#ifdef TF_CLIENT_DLL
	ConVar cl_flipviewmodels( "cl_flipviewmodels", "0", FCVAR_USERINFO | FCVAR_ARCHIVE | FCVAR_NOT_CONNECTED, "Flip view models." );
#endif

void PostToolMessage( HTOOLHANDLE hEntity, KeyValues *msg );

void FormatViewModelAttachment( Vector &vOrigin, bool bInverse )
{
	// Presumably, SetUpView has been called so we know our FOV and render origin.
	const CViewSetup *pViewSetup = view->GetPlayerViewSetup();
	
	float worldx = tan( pViewSetup->fov * M_PI/360.0 );
	float viewx = tan( pViewSetup->fovViewmodel * M_PI/360.0 );

	// aspect ratio cancels out, so only need one factor
	// the difference between the screen coordinates of the 2 systems is the ratio
	// of the coefficients of the projection matrices (tan (fov/2) is that coefficient)
	float factorX = worldx / viewx;

	float factorY = factorX;
	
	// Get the coordinates in the viewer's space.
	Vector tmp = vOrigin - pViewSetup->origin;
	Vector vTransformed( MainViewRight().Dot( tmp ), MainViewUp().Dot( tmp ), MainViewForward().Dot( tmp ) );

	// Now squash X and Y.
	if ( bInverse )
	{
		if ( factorX != 0 && factorY != 0 )
		{
			vTransformed.x /= factorX;
			vTransformed.y /= factorY;
		}
		else
		{
			vTransformed.x = 0.0f;
			vTransformed.y = 0.0f;
		}
	}
	else
	{
		vTransformed.x *= factorX;
		vTransformed.y *= factorY;
	}



	// Transform back to world space.
	Vector vOut = (MainViewRight() * vTransformed.x) + (MainViewUp() * vTransformed.y) + (MainViewForward() * vTransformed.z);
	vOrigin = pViewSetup->origin + vOut;
}


void C_BaseViewModel::FormatViewModelAttachment( int nAttachment, matrix3x4_t &attachmentToWorld )
{
	Vector vecOrigin;
	MatrixPosition( attachmentToWorld, vecOrigin );
	::FormatViewModelAttachment( vecOrigin, false );
	PositionMatrix( vecOrigin, attachmentToWorld );
}


bool C_BaseViewModel::IsViewModel() const
{
	return true;
}

void C_BaseViewModel::UncorrectViewModelAttachment( Vector &vOrigin )
{
	// Unformat the attachment.
	::FormatViewModelAttachment( vOrigin, true );
}


//-----------------------------------------------------------------------------
// Purpose
//-----------------------------------------------------------------------------
void C_BaseViewModel::FireEvent( const Vector& origin, const QAngle& angles, int event, const char *options )
{
	// We override sound requests so that we can play them locally on the owning player
	if ( ( event == AE_CL_PLAYSOUND ) || ( event == CL_EVENT_SOUND ) )
	{
		// Only do this if we're owned by someone
		if ( GetOwner() != NULL )
		{
			CLocalPlayerFilter filter;
			EmitSound( filter, GetOwner()->GetSoundSourceIndex(), options, &GetAbsOrigin() );
			return;
		}
	}

	// Otherwise pass the event to our associated weapon
	C_BaseCombatWeapon *pWeapon = GetActiveWeapon();
	if ( pWeapon )
	{
		// NVNT notify the haptics system of our viewmodel's event
		if ( haptics )
			haptics->ProcessHapticEvent(4,"Weapons",pWeapon->GetName(),"AnimationEvents",VarArgs("%i",event));

		bool bResult = pWeapon->OnFireEvent( this, origin, angles, event, options );
		if ( !bResult )
		{
			BaseClass::FireEvent( origin, angles, event, options );
		}
	}
}

bool C_BaseViewModel::Interpolate( float currentTime )
{
	CStudioHdr *pStudioHdr = GetModelPtr();
	// Make sure we reset our animation information if we've switch sequences
	UpdateAnimationParity();

	bool bret = BaseClass::Interpolate( currentTime );

	// Hack to extrapolate cycle counter for view model
	float elapsed_time = currentTime - m_flAnimTime;
	C_BasePlayer *pPlayer = C_BasePlayer::GetLocalPlayer();

	// Predicted viewmodels have fixed up interval
	if ( GetPredictable() || IsClientCreated() )
	{
		Assert( pPlayer );
		float curtime = pPlayer ? pPlayer->GetFinalPredictedTime() : gpGlobals->curtime;
		elapsed_time = curtime - m_flAnimTime;
		// Adjust for interpolated partial frame
		if ( !engine->IsPaused() )
		{
			elapsed_time += ( gpGlobals->interpolation_amount * TICK_INTERVAL );
		}
	}

	// Prediction errors?	
	if ( elapsed_time < 0 )
	{
		elapsed_time = 0;
	}

	float dt = elapsed_time * GetSequenceCycleRate( pStudioHdr, GetSequence() ) * GetPlaybackRate();
	if ( dt >= 1.0f )
	{
		if ( !IsSequenceLooping( GetSequence() ) )
		{
			dt = 0.999f;
		}
		else
		{
			dt = fmod( dt, 1.0f );
		}
	}

	SetCycle( dt );
	return bret;
}


inline bool C_BaseViewModel::ShouldFlipViewModel()
{
	// If cl_righthand is set, then we want them all right-handed.
	CBaseCombatWeapon *pWeapon = m_hWeapon.Get();
	if ( pWeapon )
	{
		const FileWeaponInfo_t *pInfo = &pWeapon->GetWpnData();
		return pInfo->m_bAllowFlipping && pInfo->m_bBuiltRightHanded != cl_righthand.GetBool();
	}

#ifdef TF_CLIENT_DLL
	CBaseCombatWeapon *pWeapon = m_hWeapon.Get();
	if ( pWeapon )
	{
		return pWeapon->m_bFlipViewModel != cl_flipviewmodels.GetBool();
	}
#endif

	return false;
}


void C_BaseViewModel::ApplyBoneMatrixTransform( matrix3x4_t& transform )
{
	if ( ShouldFlipViewModel() )
	{
		matrix3x4_t viewMatrix, viewMatrixInverse;

		// We could get MATERIAL_VIEW here, but this is called sometimes before the renderer
		// has set that matrix. Luckily, this is called AFTER the CViewSetup has been initialized.
		const CViewSetup *pSetup = view->GetPlayerViewSetup();
		AngleMatrix( pSetup->angles, pSetup->origin, viewMatrixInverse );
		MatrixInvert( viewMatrixInverse, viewMatrix );

		// Transform into view space.
		matrix3x4_t temp, temp2;
		ConcatTransforms( viewMatrix, transform, temp );
		
		// Flip it along X.
		
		// (This is the slower way to do it, and it equates to negating the top row).
		//matrix3x4_t mScale;
		//SetIdentityMatrix( mScale );
		//mScale[0][0] = 1;
		//mScale[1][1] = -1;
		//mScale[2][2] = 1;
		//ConcatTransforms( mScale, temp, temp2 );
		temp[1][0] = -temp[1][0];
		temp[1][1] = -temp[1][1];
		temp[1][2] = -temp[1][2];
		temp[1][3] = -temp[1][3];

		// Transform back out of view space.
		ConcatTransforms( viewMatrixInverse, temp, transform );
	}
}

//-----------------------------------------------------------------------------
// Purpose: check if weapon viewmodel should be drawn
//-----------------------------------------------------------------------------
bool C_BaseViewModel::ShouldDraw()
{
	if ( engine->IsHLTV() )
	{
		return ( HLTVCamera()->GetMode() == OBS_MODE_IN_EYE &&
				 HLTVCamera()->GetPrimaryTarget() == GetOwner()	);
	}
#if defined( REPLAY_ENABLED )
	else if ( g_pEngineClientReplay->IsPlayingReplayDemo() )
	{
		return ( ReplayCamera()->GetMode() == OBS_MODE_IN_EYE &&
				 ReplayCamera()->GetPrimaryTarget() == GetOwner() );
	}
#endif
	else
	{
		return BaseClass::ShouldDraw();
	}
}

//-----------------------------------------------------------------------------
// Purpose: Render the weapon. Draw the Viewmodel if the weapon's being carried
//			by this player, otherwise draw the worldmodel.
//-----------------------------------------------------------------------------
int C_BaseViewModel::DrawModel( int flags )
{
	if ( !m_bReadyToDraw )
		return 0;

	if ( flags & STUDIO_RENDER )
	{
		// Determine blending amount and tell engine
		float blend = (float)( GetFxBlend() / 255.0f );

		// Totally gone
		if ( blend <= 0.0f )
			return 0;

		// Tell engine
		render->SetBlend( blend );

		float color[3];
		GetColorModulation( color );
		render->SetColorModulation(	color );
	}
		
	C_BasePlayer *pPlayer = C_BasePlayer::GetLocalPlayer();
	C_BaseCombatWeapon *pWeapon = GetOwningWeapon();
	int ret;
	// If the local player's overriding the viewmodel rendering, let him do it
	if ( pPlayer && pPlayer->IsOverridingViewmodel() )
	{
		ret = pPlayer->DrawOverriddenViewmodel( this, flags );
	}
	else if ( pWeapon && pWeapon->IsOverridingViewmodel() )
	{
		ret = pWeapon->DrawOverriddenViewmodel( this, flags );
	}
	else
	{
		ret = BaseClass::DrawModel( flags );
	}

	// Now that we've rendered, reset the animation restart flag
	if ( flags & STUDIO_RENDER )
	{
		if ( m_nOldAnimationParity != m_nAnimationParity )
		{
			m_nOldAnimationParity = m_nAnimationParity;
		}
		// Tell the weapon itself that we've rendered, in case it wants to do something
		if ( pWeapon )
		{
			pWeapon->ViewModelDrawn( this );
		}
	}

	// Draw hands attachment if present. The attachment renders via
	// InternalDrawModel (no EF_BONEMERGE follow re-entrancy), so no guard is
	// needed here.
	if ( m_hHandsAttachment.Get() )
	{
		// Sync animation state first so the bone merge reads this frame's bones
		m_hHandsAttachment->SyncToViewModel( this );
		m_hHandsAttachment->DrawModel( flags );
	}

	return ret;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
int C_BaseViewModel::InternalDrawModel( int flags )
{
	CMatRenderContextPtr pRenderContext( materials );
	if ( ShouldFlipViewModel() )
		pRenderContext->CullMode( MATERIAL_CULLMODE_CW );

	int ret = BaseClass::InternalDrawModel( flags );

	pRenderContext->CullMode( MATERIAL_CULLMODE_CCW );

	return ret;
}

//-----------------------------------------------------------------------------
// Purpose: Called by the player when the player's overriding the viewmodel drawing. Avoids infinite recursion.
//-----------------------------------------------------------------------------
int C_BaseViewModel::DrawOverriddenViewmodel( int flags )
{
	return BaseClass::DrawModel( flags );
}

//-----------------------------------------------------------------------------
// Purpose: 
// Output : int
//-----------------------------------------------------------------------------
int C_BaseViewModel::GetFxBlend( void )
{
	// See if the local player wants to override the viewmodel's rendering
	C_BasePlayer *pPlayer = C_BasePlayer::GetLocalPlayer();
	if ( pPlayer && pPlayer->IsOverridingViewmodel() )
	{
		pPlayer->ComputeFxBlend();
		return pPlayer->GetFxBlend();
	}

	C_BaseCombatWeapon *pWeapon = GetOwningWeapon();
	if ( pWeapon && pWeapon->IsOverridingViewmodel() )
	{
		pWeapon->ComputeFxBlend();
		return pWeapon->GetFxBlend();
	}

	return BaseClass::GetFxBlend();
}

//-----------------------------------------------------------------------------
// Purpose: 
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool C_BaseViewModel::IsTransparent( void )
{
	// See if the local player wants to override the viewmodel's rendering
	C_BasePlayer *pPlayer = C_BasePlayer::GetLocalPlayer();
	if ( pPlayer && pPlayer->IsOverridingViewmodel() )
	{
		return pPlayer->ViewModel_IsTransparent();
	}

	C_BaseCombatWeapon *pWeapon = GetOwningWeapon();
	if ( pWeapon && pWeapon->IsOverridingViewmodel() )
		return pWeapon->ViewModel_IsTransparent();

	return BaseClass::IsTransparent();
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
bool C_BaseViewModel::UsesPowerOfTwoFrameBufferTexture( void )
{
	// See if the local player wants to override the viewmodel's rendering
	C_BasePlayer *pPlayer = C_BasePlayer::GetLocalPlayer();
	if ( pPlayer && pPlayer->IsOverridingViewmodel() )
	{
		return pPlayer->ViewModel_IsUsingFBTexture();
	}

	C_BaseCombatWeapon *pWeapon = GetOwningWeapon();
	if ( pWeapon && pWeapon->IsOverridingViewmodel() )
	{
		return pWeapon->ViewModel_IsUsingFBTexture();
	}

	return BaseClass::UsesPowerOfTwoFrameBufferTexture();
}

//-----------------------------------------------------------------------------
// Purpose: If the animation parity of the weapon has changed, we reset cycle to avoid popping
//-----------------------------------------------------------------------------
void C_BaseViewModel::UpdateAnimationParity( void )
{
	C_BasePlayer *pPlayer = C_BasePlayer::GetLocalPlayer();
	
	// If we're predicting, then we don't use animation parity because we change the animations on the clientside
	// while predicting. When not predicting, only the server changes the animations, so a parity mismatch
	// tells us if we need to reset the animation.
	if ( m_nOldAnimationParity != m_nAnimationParity && !GetPredictable() )
	{
		float curtime = (pPlayer && IsIntermediateDataAllocated()) ? pPlayer->GetFinalPredictedTime() : gpGlobals->curtime;
		// FIXME: this is bad
		// Simulate a networked m_flAnimTime and m_flCycle
		// FIXME:  Do we need the magic 0.1?
		SetCycle( 0.0f ); // GetSequenceCycleRate( GetSequence() ) * 0.1;
		m_flAnimTime = curtime;
	}
}

//-----------------------------------------------------------------------------
// Purpose: Update global map state based on data received
// Input  : bnewentity - 
//-----------------------------------------------------------------------------
void C_BaseViewModel::OnDataChanged( DataUpdateType_t updateType )
{
	SetPredictionEligible( true );
	BaseClass::OnDataChanged(updateType);

	// Update hands attachment when viewmodel changes
	UpdateHandsAttachment();
}

void C_BaseViewModel::PostDataUpdate( DataUpdateType_t updateType )
{
	BaseClass::PostDataUpdate(updateType);
	OnLatchInterpolatedVariables( LATCH_ANIMATION_VAR );
}


//-----------------------------------------------------------------------------
// Purpose: Add entity to visible view models list
//-----------------------------------------------------------------------------
void C_BaseViewModel::AddEntity( void )
{
	// Server says don't interpolate this frame, so set previous info to new info.
	if ( IsNoInterpolationFrame() )
	{
		ResetLatched();
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void C_BaseViewModel::GetBoneControllers(float controllers[MAXSTUDIOBONECTRLS])
{
	BaseClass::GetBoneControllers( controllers );

	// Tell the weapon itself that we've rendered, in case it wants to do something
	C_BaseCombatWeapon *pWeapon = GetActiveWeapon();
	if ( pWeapon )
	{
		pWeapon->GetViewmodelBoneControllers( this, controllers );
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
// Output : RenderGroup_t
//-----------------------------------------------------------------------------
RenderGroup_t C_BaseViewModel::GetRenderGroup()
{
	return RENDER_GROUP_VIEW_MODEL_OPAQUE;
}

//-----------------------------------------------------------------------------
// Purpose: Update hands attachment based on player model
//-----------------------------------------------------------------------------
// Track last successfully attached hands model (non-static: hl2sb_status reads it)
char g_pszLastHandsModel[MAX_PATH] = "";

// Last hands model whose entity creation failed. Retrying every frame just
// spams the console and churns entities - a bad/missing model path stays
// blocked until the requested model changes or the attachment is released.
char g_pszFailedHandsModel[MAX_PATH] = "";

// Accessor for the current c_hands state, so console commands (hl2sb_status)
// can report what hands model is actually attached without reaching into the
// viewmodel internals.
const char *HL2SB_GetActiveHandsModel( void )
{
	// Empty means no hands have been attached this session -> stock/none.
	if ( !g_pszLastHandsModel[0] )
		return NULL;
	return g_pszLastHandsModel;
}

// Master switch, defined in c_viewmodel_attachment.cpp
extern ConVar cl_hands;
// Skip-baked-arms switch, defined in c_viewmodel_attachment.cpp
extern ConVar cl_hands_skip_baked_arms;

//-----------------------------------------------------------------------------
// Purpose: Does this viewmodel already draw its own arms?
//          Stock HL2/EP2/HL2MP weapon viewmodels bake the arm/hand mesh into
//          the model and texture it with the shared "v_hand" material
//          (e.g. "v_hand_sheet"). Merging an extra pair of c_hands onto them
//          double-draws the arms. MMOD-style replacement viewmodels are gun
//          only (arms live in default-off bodygroups or are absent) and carry
//          no v_hand material, so they must still receive the merged hands.
//          We detect the baked arms by scanning the studio texture table for a
//          material whose name contains "v_hand" (case-insensitive).
//-----------------------------------------------------------------------------
static bool ViewModelHasBakedArms( C_BaseViewModel *pVM )
{
	if ( !pVM )
		return false;

	CStudioHdr *pHdr = pVM->GetModelPtr();
	if ( !pHdr )
		return false;

	const studiohdr_t *pRaw = pHdr->GetRenderHdr();
	if ( !pRaw )
		return false;

	for ( int i = 0; i < pRaw->numtextures; i++ )
	{
		const mstudiotexture_t *pTex = pRaw->pTexture( i );
		if ( !pTex )
			continue;

		const char *pszName = pTex->pszName();
		if ( pszName && Q_stristr( pszName, "v_hand" ) )
			return true;
	}

	return false;
}

void C_BaseViewModel::ReleaseHandsAttachment( void )
{
	if ( C_ViewmodelAttachment *pOld = m_hHandsAttachment.Get() )
	{
		pOld->DetachFromViewmodel();
		pOld->Release();
		m_hHandsAttachment = NULL;
	}
	// Clear the "already attached" cache (our entity is gone; another viewmodel
	// that holds the same model simply re-attaches it a bit early - harmless).
	g_pszLastHandsModel[0] = '\0';
	g_pszFailedHandsModel[0] = '\0';
}

void C_BaseViewModel::UpdateHandsAttachment( void )
{
	C_BasePlayer *pOwner = ToBasePlayer( GetOwner() );
	if ( !pOwner )
		return;

	// Master switch off - drop any existing attachment
	if ( !cl_hands.GetBool() )
	{
		ReleaseHandsAttachment();
		return;
	}

	// Don't merge hands onto a viewmodel that already draws its own arms
	// (stock HL2 v_hand models) - that would double-draw the arms. Release any
	// attachment we may have made earlier (e.g. the player just switched from a
	// gun-only MMOD weapon to a stock one).
	if ( cl_hands_skip_baked_arms.GetBool() && ViewModelHasBakedArms( this ) )
	{
		ReleaseHandsAttachment();
		return;
	}

	// Get player model path
	const char *pszPlayerModel = modelinfo->GetModelName( pOwner->GetModel() );

	// Get hands model: cl_hands_model override first, else config-system mapping
	const char *pszHandsModel = NULL;
	char szHandsBuf[ MAX_PATH ];
	Q_strncpy( szHandsBuf, "", sizeof( szHandsBuf ) );
	const char *pszOverride = cl_hands_model.GetString();
	if ( pszOverride && pszOverride[0] && Q_stricmp( pszOverride, "auto" ) != 0 )
	{
		pszHandsModel = pszOverride;
	}
	else
	{
		pszHandsModel = HL2SB_GetHandsModelForPlayer( pszPlayerModel );
	}

	// The config system may encode skin/bodygroup after the path:
	//   "models/weapons/c_arms_citizen.mdl|2|0000000"
	int iHandsSkin = -1, iHandsBodyValid = 0;
	char szHandsBody[ 32 ] = "";
	if ( pszHandsModel )
	{
		Q_strncpy( szHandsBuf, pszHandsModel, sizeof( szHandsBuf ) );
		char *pPipe1 = strchr( szHandsBuf, '|' );
		if ( pPipe1 )
		{
			*pPipe1 = '\0';
			char *pPipe2 = strchr( pPipe1 + 1, '|' );
			if ( pPipe2 )
			{
				*pPipe2 = '\0';
				iHandsSkin = atoi( pPipe1 + 1 );
				Q_strncpy( szHandsBody, pPipe2 + 1, sizeof( szHandsBody ) );
				iHandsBodyValid = 1;
			}
			else
			{
				Q_strncpy( szHandsBody, pPipe1 + 1, sizeof( szHandsBody ) );
				iHandsBodyValid = 1;
			}
		}
		pszHandsModel = szHandsBuf;
	}

	// Cache key includes encoded skin/body so switching between two player
	// models that share a hands model but differ in skin (e.g. citizen skin0
	// vs bloody-zombie skin2) still re-applies.
	char szHandsKey[ MAX_PATH + 64 ];
	if ( pszHandsModel )
	{
		if ( iHandsSkin >= 0 )
			Q_snprintf( szHandsKey, sizeof( szHandsKey ), "%s|%i|%s", pszHandsModel, iHandsSkin, szHandsBody );
		else
			Q_snprintf( szHandsKey, sizeof( szHandsKey ), "%s|%s", pszHandsModel, szHandsBody );
		pszHandsModel = szHandsKey;
	}

	// No hands model for this player model - remove attachment
	if ( !pszHandsModel )
	{
		ReleaseHandsAttachment();
		return;
	}

	// Same model that already failed to load this session - stay quiet instead
	// of re-creating and re-warning every frame (resets via cl_hands toggle,
	// model/cvar change, or viewmodel removal).
	if ( g_pszFailedHandsModel[0] && !Q_stricmp( g_pszFailedHandsModel, pszHandsModel ) )
	{
		return;
	}

	// g_pszLastHandsModel is global; another viewmodel may own the cached
	// attachment already. Only skip when *our* attachment is the right model.
	if ( !Q_stricmp( g_pszLastHandsModel, pszHandsModel ) && m_hHandsAttachment.Get() )
	{
		return;
	}

	// Remove our old attachment (if any)
	ReleaseHandsAttachment();

	// Create new hands attachment (load the bare model path, not the cache key)
	C_ViewmodelAttachment *pAttach = new C_ViewmodelAttachment;
	if ( pAttach && pAttach->SetHandsModel( szHandsBuf ) )
	{
		// Apply skin/bodygroup encoded in the config ("path|skin|body").
		// e.g. zombie/charple/corpse use c_arms_citizen skin 2 = bloody hands.
		if ( iHandsSkin >= 0 )
			pAttach->m_nSkin = iHandsSkin;
		if ( iHandsBodyValid && szHandsBody[0] )
		{
			for ( int iGroup = 0; szHandsBody[ iGroup ] >= '0' && szHandsBody[ iGroup ] <= '9'; ++iGroup )
				static_cast<C_BaseAnimating *>( pAttach )->SetBodygroup( iGroup, szHandsBody[ iGroup ] - '0' );
		}
		pAttach->AttachToViewmodel( this );
		m_hHandsAttachment = pAttach;
		Q_strncpy( g_pszLastHandsModel, pszHandsModel, sizeof(g_pszLastHandsModel) );
		g_pszFailedHandsModel[0] = '\0';
		Msg( "[HL2SB-HANDS] Attached hands model: %s\n", pszHandsModel );
	}
	else
	{
		if ( pAttach ) delete pAttach;
		Q_strncpy( g_pszFailedHandsModel, pszHandsModel, sizeof(g_pszFailedHandsModel) );
	}
}

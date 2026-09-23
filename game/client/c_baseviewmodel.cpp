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
#include "luamanager.h"	// HL2SB: sound.Add script lookup in FireEvent below

#if defined( REPLAY_ENABLED )
#include "replay/replaycamera.h"
#include "replay/ireplaysystem.h"
#include "replay/ienginereplay.h"
#endif

// NVNT haptics system interface
#include "haptics/ihaptics.h"


extern bool g_bRenderingReflection; // HL2SB: mirror reflection flag in viewrender.cpp

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


// HL2SB: sound.Add scripts live in a Lua-side table (gmod_compat.lua) that the
// C++ sound system cannot see - viewmodel animation-event sounds registered by
// name ("Rifle.ClipOut" in cf_beast) resolved to nothing, so reloads were
// silent while Lua EmitSound(wav) fire sounds played fine.  Returns true when
// pName is a registered script: copies the (randomly picked per GMod's
// semantics) wav path, and level/volume/channel leave -1 when unregistered.
static bool HL2SB_ResolveLuaSoundScript( const char *pName, char *pOutPath, int nOutLen, float *pOutVolume, int *pOutLevel, int *pOutChannel )
{
	if ( L == NULL || pName == NULL || pName[0] == '\0' )
		return false;

	lua_getglobal( L, "sound" );				// [sound]
	if ( !lua_istable( L, -1 ) )
	{
		lua_pop( L, 1 );
		return false;
	}
	lua_getfield( L, -1, "GetProperties" );		// [sound][fn]
	if ( !lua_isfunction( L, -1 ) )
	{
		lua_pop( L, 2 );
		return false;
	}
	lua_pushstring( L, pName );
	if ( luasrc_pcall( L, 1, 1, 0 ) != 0 )
	{
		// luasrc_pcall reported + popped the message: only [sound] is left.
		lua_pop( L, 1 );
		return false;
	}
	if ( !lua_istable( L, -1 ) )				// not a registered script
	{
		lua_pop( L, 2 );						// nil + sound
		return false;
	}

	const int iProps = lua_gettop( L );			// [sound][props]

	// Path field: "Sounds" (shim key) or GMod SoundData's "sound"; a string,
	// or a table of variants (GMod picks one at random per play).
	lua_getfield( L, iProps, "Sounds" );		// [sound][props][v]
	if ( !lua_istable( L, -1 ) )
	{
		lua_pop( L, 1 );
		lua_getfield( L, iProps, "sound" );
	}
	bool bResolved = false;
	if ( lua_type( L, -1 ) == LUA_TSTRING )
	{
		Q_strncpy( pOutPath, lua_tostring( L, -1 ), nOutLen );
		bResolved = ( pOutPath[0] != '\0' );
	}
	else if ( lua_istable( L, -1 ) )
	{
		const int nVariants = (int)lua_rawlen( L, -1 );
		if ( nVariants > 0 )
		{
			lua_rawgeti( L, -1, RandomInt( 1, nVariants ) );	// [..][v][pick]
			if ( lua_type( L, -1 ) == LUA_TSTRING )
			{
				Q_strncpy( pOutPath, lua_tostring( L, -1 ), nOutLen );
				bResolved = ( pOutPath[0] != '\0' );
			}
			lua_pop( L, 1 );
		}
	}
	lua_pop( L, 1 );							// [sound][props]

	if ( bResolved )
	{
		// Registered params (shim stores capitalized keys, GMod SoundData
		// lowercase - accept both); only overwrite the caller's -1 defaults.
		lua_getfield( L, iProps, "Level" );
		if ( !lua_isnumber( L, -1 ) ) { lua_pop( L, 1 ); lua_getfield( L, iProps, "level" ); }
		if ( lua_isnumber( L, -1 ) ) *pOutLevel = (int)lua_tonumber( L, -1 );
		lua_pop( L, 1 );

		lua_getfield( L, iProps, "Volume" );
		if ( !lua_isnumber( L, -1 ) ) { lua_pop( L, 1 ); lua_getfield( L, iProps, "volume" ); }
		if ( lua_isnumber( L, -1 ) ) *pOutVolume = (float)lua_tonumber( L, -1 );
		lua_pop( L, 1 );

		lua_getfield( L, iProps, "Channel" );
		if ( !lua_isnumber( L, -1 ) ) { lua_pop( L, 1 ); lua_getfield( L, iProps, "channel" ); }
		if ( lua_isnumber( L, -1 ) ) *pOutChannel = (int)lua_tonumber( L, -1 );
		lua_pop( L, 1 );
	}

	lua_pop( L, 2 );							// props + sound
	return bResolved;
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
			char szScripted[256];
			float flVolume = 1.0f;
			int nLevel = -1;
			int nChannel = -1;
			if ( HL2SB_ResolveLuaSoundScript( options, szScripted, sizeof( szScripted ), &flVolume, &nLevel, &nChannel ) )
			{
				// sound.Add script: emit the resolved wav path with the
				// registered params - the script name is unknown to the
				// C++ sound system and would resolve to nothing.
				Vector vOrigin = GetAbsOrigin();
				EmitSound_t params;
				params.m_pSoundName = szScripted;
				params.m_flVolume = flVolume;
				if ( nLevel >= 0 )
					params.m_SoundLevel = (soundlevel_t)nLevel;
				if ( nChannel >= 0 )
					params.m_nChannel = nChannel;
				params.m_pOrigin = &vOrigin;
				EmitSound( filter, GetOwner()->GetSoundSourceIndex(), params );
			}
			else
			{
				EmitSound( filter, GetOwner()->GetSoundSourceIndex(), options, &GetAbsOrigin() );
			}
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
	// HL2SB: don't draw first-person weapon model in mirror reflection
	if ( g_bRenderingReflection )
		return false;

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
// Defined further below: per-viewmodel flag that is set while the owner is
// riding in a vehicle (the hands are released for the ride).
static bool HL2SB_HandsHeldForVehicle( C_BaseViewModel *pVM );

int C_BaseViewModel::DrawModel( int flags )
{
	if ( !m_bReadyToDraw )
		return 0;

	// Just left a vehicle: rebuild the hands before this frame draws, so the
	// arms appear together with the weapon. Relying on the next OnDataChanged
	// (the HUD-hide repaint on exit) drew the gun first and the hands a few
	// frames later.
	if ( ( flags & STUDIO_RENDER ) && HL2SB_HandsHeldForVehicle( this ) )
	{
		C_BasePlayer *pOwner = ToBasePlayer( GetOwner() );
		if ( pOwner && !pOwner->GetVehicle() )
			UpdateHandsAttachment();
	}

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

	// Draw the hands attachment. This is the ONLY place the arms are rendered:
	// C_ViewmodelAttachment::ShouldDraw() returns false so the world renderer
	// never picks the entity up. That second, unclipped draw at the raw
	// viewmodel origin (re-added by UpdateVisibility when the arms model loads
	// asynchronously) is exactly the "extra arm" that appeared from the second
	// map onwards. The IsAttachedTo() gate below is the belt to those braces: a
	// handle left pointing at an entity that has been re-parented or released
	// can never draw a stale copy either.
	C_ViewmodelAttachment *pAttach = m_hHandsAttachment.Get();
	if ( pAttach )
	{
		if ( !pAttach->IsAttachedTo( this ) )
		{
			// Not ours any more - drop it; UpdateHandsAttachment builds a fresh
			// attachment on the next pass.
			m_hHandsAttachment = NULL;
		}
		else
		{
			C_BasePlayer *pOwner = ToBasePlayer( GetOwner() );
			C_BaseCombatWeapon *pActive = pOwner ? pOwner->GetActiveWeapon() : NULL;
			C_BaseCombatWeapon *pThis = GetOwningWeapon();
			CStudioHdr *pHdr = pAttach->GetModelPtr();

			// Only the held weapon's viewmodel draws its arms, only while the
			// owner is alive and NOT riding a vehicle (the vehicle viewmodel is
			// drawn instead, and the arms would merge onto bones nobody
			// refreshes), and only once the arms rig is actually loaded - a rig
			// with no bones cannot merge and renders as a degenerate floating
			// hand.
			if ( pOwner && pOwner->IsAlive() && !pOwner->GetVehicle() &&
				 !( pActive && pThis && pActive != pThis ) &&
				 pHdr && pHdr->numbones() > 0 )
			{
				pAttach->SyncToViewModel( this );
				HL2SB_BeginManualHandsDraw();
				pAttach->DrawModel( flags );
				HL2SB_EndManualHandsDraw();
			}
		}
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
// Purpose: Re-run the hands decision once the model (and its texture table) is
//          available. On a fast reconnect the first UpdateHandsAttachment can
//          run before the viewmodel model data is parsed, so ViewModelHasBakedArms
//          sees an empty texture table and merges a c_hands pair onto a weapon
//          that actually draws its own baked arms -> double hands. Refreshing
//          here re-checks the bake state and releases the merged pair if the
//          weapon has its own arms.
//-----------------------------------------------------------------------------
CStudioHdr *C_BaseViewModel::OnNewModel( void )
{
	CStudioHdr *pResult = BaseClass::OnNewModel();
	UpdateHandsAttachment();
	return pResult;
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

	// The cache only describes reality while the entity it was set for is
	// actually alive (it dies with its viewmodel on a level change).
	if ( HL2SB_CountLiveHandsAttachments() == 0 )
		return NULL;

	return g_pszLastHandsModel;
}

// Master switch, defined in c_viewmodel_attachment.cpp
extern ConVar cl_hands;
// Skip-baked-arms switch, defined in c_viewmodel_attachment.cpp
extern ConVar cl_hands_skip_baked_arms;
// Verbose logging switch, defined in c_viewmodel_attachment.cpp
extern ConVar cl_hands_debug;

//-----------------------------------------------------------------------------
// "Hands released for a vehicle ride" state, indexed by entity index.
//
// This cannot live as a member of CBaseViewModel: the class is shared with the
// server and growing it re-lays-out every dependent translation unit - an
// incremental build then mixes old and new offsets and corrupts the heap (it
// happened). MAX_EDICTS bools cost 20 KB once and can never desync.
//-----------------------------------------------------------------------------
static bool s_bHandsHeldForVehicle[MAX_EDICTS];

static bool HL2SB_HandsHeldForVehicle( C_BaseViewModel *pVM )
{
	int i = pVM->entindex();
	return ( i >= 0 && i < MAX_EDICTS ) ? s_bHandsHeldForVehicle[ i ] : false;
}

static void HL2SB_SetHandsHeldForVehicle( C_BaseViewModel *pVM, bool bHeld )
{
	int i = pVM->entindex();
	if ( i >= 0 && i < MAX_EDICTS )
		s_bHandsHeldForVehicle[ i ] = bHeld;
}

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

	// A "v_hand" material only means baked arms when a mesh actually renders it.
	// GMod c_model weapons (e.g. c_shotgun) can carry a stray v_hand_sheet entry
	// in the texture table that no mesh references - those are gun-only and must
	// still receive the merged c_hands. Scan the mesh materials, not just the
	// texture table.
	for ( int iBody = 0; iBody < pRaw->numbodyparts; iBody++ )
	{
		const mstudiobodyparts_t *pBody = pRaw->pBodypart( iBody );
		if ( !pBody )
			continue;

		for ( int iModel = 0; iModel < pBody->nummodels; iModel++ )
		{
			const mstudiomodel_t *pModel = pBody->pModel( iModel );
			if ( !pModel )
				continue;

			for ( int iMesh = 0; iMesh < pModel->nummeshes; iMesh++ )
			{
				const mstudiomesh_t *pMesh = pModel->pMesh( iMesh );
				if ( !pMesh )
					continue;

				const int iMat = pMesh->material;
				if ( iMat < 0 || iMat >= pRaw->numtextures )
					continue;

				const mstudiotexture_t *pTex = pRaw->pTexture( iMat );
				if ( !pTex )
					continue;

				const char *pszName = pTex->pszName();
				if ( pszName && Q_stristr( pszName, "v_hand" ) )
					return true;
			}
		}
	}

	return false;
}

//-----------------------------------------------------------------------------
// Purpose: Is the viewmodel's studio header fully parsed, so the texture scan
//          above can actually see its materials? A model that is still loading
//          reports zero textures, and "zero textures" is indistinguishable from
//          "no v_hand material" - which is how a second pair of arms ended up
//          merged onto stock weapons after a level change.
//-----------------------------------------------------------------------------
static bool ViewModelHasLoadedTextureTable( C_BaseViewModel *pVM )
{
	if ( !pVM )
		return false;

	CStudioHdr *pHdr = pVM->GetModelPtr();
	if ( !pHdr || !pHdr->GetRenderHdr() )
		return false;

	return pHdr->GetRenderHdr()->numtextures > 0;
}

void C_BaseViewModel::ReleaseHandsAttachment( void )
{
	if ( C_ViewmodelAttachment *pOld = m_hHandsAttachment.Get() )
	{
		pOld->DetachFromViewmodel();
		pOld->Release();
		m_hHandsAttachment = NULL;

		// The globals describe the hands entity that has just been destroyed.
		// Only touch them when this viewmodel actually owned one: a viewmodel
		// that never had hands used to wipe another viewmodel's cache here,
		// which forced a rebuild (and so a second live arms entity) on the very
		// next pass.
		g_pszLastHandsModel[0] = '\0';
		g_pszFailedHandsModel[0] = '\0';
	}
}

void C_BaseViewModel::UpdateHandsAttachment( void )
{
	C_BasePlayer *pOwner = ToBasePlayer( GetOwner() );
	if ( !pOwner )
		return;

	// Player is dead: holster the hands. Without this they stay EF_BONEMERGE'd
	// to the (dead/holstered) viewmodel and float out into the world, and the
	// still-set g_pszLastHandsModel cache makes the respawn's UpdateHandsAttachment
	// early-out on the stale attachment instead of building a fresh one (so the
	// respawn has no hands). Releasing here clears the cache and detaches the
	// arms; the respawn's OnDataChanged then re-attaches them cleanly.
	if ( !pOwner->IsAlive() )
	{
		ReleaseHandsAttachment();
		return;
	}

	// In a vehicle the engine draws the VEHICLE viewmodel instead of the
	// weapon's, so our arms are not rendered. Keep the arms entity (and its
	// loaded model) through the ride and just stop drawing it in DrawModel -
	// releasing it here destroyed the entity and unloaded the arms model, so on
	// exit the model had to reload and the hands popped in ~0.5s after the
	// weapon. The flag marks the ride so DrawModel skips drawing them.
	if ( pOwner->GetVehicle() != NULL )
	{
		HL2SB_SetHandsHeldForVehicle( this, true );
		return;
	}

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
	if ( cl_hands_skip_baked_arms.GetBool() )
	{
		// We can only answer the baked-arms question once the viewmodel's
		// material table is readable. Right after a level change the model may
		// still be arriving asynchronously; guessing "no baked arms" there is
		// what merged a second pair of arms onto stock weapons. Defer instead -
		// OnNewModel()/OnModelLoadComplete() re-run this decision as soon as the
		// data is here.
		if ( !ViewModelHasLoadedTextureTable( this ) )
			return;

		if ( ViewModelHasBakedArms( this ) )
		{
			ReleaseHandsAttachment();
			return;
		}
	}

	// If the owner's player model isn't resolved yet (e.g. right at respawn,
	// before the model is networked), don't tear down an existing hands
	// attachment - that would drop the hands and, since nothing re-triggers
	// UpdateHandsAttachment after the model arrives, they'd stay lost.
	if ( !pOwner->GetModel() )
	{
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

	// Our handle may point at an entity that has been re-parented or released;
	// drop it first so the check below can never act on somebody else's hands.
	if ( m_hHandsAttachment.Get() && !m_hHandsAttachment->IsAttachedTo( this ) )
		m_hHandsAttachment = NULL;

	// Already holding exactly this hands model? Nothing to do. This is the hot
	// path - OnDataChanged runs on every animation parity change, so the
	// comparison has to be cheap and must be per-viewmodel: each weapon's
	// viewmodel owns its own single arms entity. This is also the just-left-a-
	// vehicle case: the arms were kept through the ride, so clearing the ride
	// flag here is all that is needed for them to draw again.
	if ( m_hHandsAttachment.Get()
			&& !Q_stricmp( m_hHandsAttachment->GetHandsKey(), pszHandsModel ) )
	{
		HL2SB_SetHandsHeldForVehicle( this, false );
		return;
	}

	HL2SB_SetHandsHeldForVehicle( this, false );

	// Death only HOLSTERS the weapon, so a transient failed-model lookup or
	// failed-attach inside the death/respawn window could leave the sticky
	// g_pszFailedHandsModel set and block the hands from reattaching forever.
	// Give every reattach a clean attempt; a genuinely-bad model just re-fails
	// (and warns) on the next weapon change instead of silently dropping the
	// hands.
	g_pszFailedHandsModel[0] = '\0';

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
		pAttach->SetHandsKey( pszHandsModel );
		m_hHandsAttachment = pAttach;
		Q_strncpy( g_pszLastHandsModel, pszHandsModel, sizeof(g_pszLastHandsModel) );
		g_pszFailedHandsModel[0] = '\0';
		if ( cl_hands_debug.GetBool() )
			Msg( "[HL2SB-HANDS] Attached hands model: %s\n", pszHandsModel );
	}
	else
	{
		if ( pAttach ) delete pAttach;
		Q_strncpy( g_pszFailedHandsModel, pszHandsModel, sizeof(g_pszFailedHandsModel) );
	}
}

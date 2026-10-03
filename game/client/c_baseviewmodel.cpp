//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Client side view model implementation. Responsible for drawing
//			the view model.
//
// $NoKeywords: $
//=============================================================================//
#include "cbase.h"
#include "c_baseviewmodel.h"
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
#include "luamanager.h"	// HL2SB: sound.Add script lookup in FireEvent below
#include "lbaseentity_shared.h"	// HL2SB: lua_pushentity (OnViewModelChanged hook dispatch)

#if defined( REPLAY_ENABLED )
#include "replay/replaycamera.h"
#include "replay/ireplaysystem.h"
#include "replay/ienginereplay.h"
#endif

// NVNT haptics system interface
#include "haptics/ihaptics.h"


extern bool g_bRenderingReflection; // HL2SB: mirror reflection flag in viewrender.cpp
extern ConVar hl2sb_anim_debug; // HL2SB: replicated probe switch (hl2mp_player_shared.cpp)

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
		// error path leaves the nil placeholder (2026-10-04): [sound][nil]
		lua_pop( L, 2 );
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
		// HL2SB: a viewmodel is only ever meant for its owner's screen - the
		// server gates them to owner/in-eye spectators in ShouldTransmit. A
		// remote viewmodel that reaches this client (transmit gate missed)
		// renders as a weapon parked at its raw networked origin - never
		// positioned, no move parent - while playing its owner's weapon
		// animations, i.e. the "second weapon floating in the world" that
		// keeps syncing with the other player's attacks. Draw only the local
		// player's own viewmodel slots; HLTV/Replay in-eye above keep their
		// reference behavior.
		C_BasePlayer *pLocal = C_BasePlayer::GetLocalPlayer();
		if ( pLocal && this != pLocal->GetViewModel( 0 ) && this != pLocal->GetViewModel( 1 ) )
			return false;

		return BaseClass::ShouldDraw();
	}
}

//-----------------------------------------------------------------------------
// Purpose: Render the weapon. Draw the Viewmodel if the weapon's being carried
//			by this player, otherwise draw the worldmodel.
//-----------------------------------------------------------------------------
int C_BaseViewModel::DrawModel( int flags )
{
	// HL2SB: a viewmodel can reach its first draw with the vtable pointer
	// already wiped (object freed or not yet constructed while still queued
	// for rendering). Every virtual call below would then read through a null
	// vtable and crash, so skip the draw and report the instance instead --
	// the owner index identifies the entity path to trace.
	if ( *reinterpret_cast< void ** >( this ) == NULL )
	{
		C_BaseEntity *pOwner = GetOwnerEntity();
		int ownerIndex = ( pOwner != NULL && *reinterpret_cast< void ** >( pOwner ) != NULL )
			? pOwner->entindex() : -1;
		Warning( "HL2SB: DrawModel skipped, viewmodel has no vtable (this=%p owner=%d)\n",
			this, ownerIndex );
		return 0;
	}

	if ( !m_bReadyToDraw )
		return 0;

	// HL2SB diagnostic (hl2sb_anim_debug): viewmodels reach DrawModel for
	// remote owners too - once a second print owner/parent/positions and the
	// played sequence, to split a detached viewmodel (weapon model lying in
	// the world playing its own animations) from the weapon-entity state in
	// the wpns probes.  Local viewmodel stays quiet.
	if ( hl2sb_anim_debug.GetBool() )
	{
		static float s_flHL2SBVmClDump[MAX_EDICTS + 1] = {};
		int iEnt = entindex();
		if ( iEnt > 0 && iEnt <= MAX_EDICTS && gpGlobals->curtime >= s_flHL2SBVmClDump[iEnt] )
		{
			s_flHL2SBVmClDump[iEnt] = gpGlobals->curtime + 1.0f;

			C_BaseEntity *pOwnerEnt = GetOwnerEntity();
			C_BaseEntity *pLocal = C_BasePlayer::GetLocalPlayer();
			if ( pOwnerEnt != pLocal )
			{
				C_BaseEntity *pParent = GetMoveParent();
				const Vector &vHere = GetAbsOrigin();
				const Vector &vLocalOrg = GetLocalOrigin();
				const Vector &vOwner = pOwnerEnt ? pOwnerEnt->GetAbsOrigin() : vec3_origin;
				Msg( "[HL2SB vm/cl] ent=%d owner=%d ownerpos=(%.0f %.0f %.0f) parent=%s#%d abs=(%.0f %.0f %.0f) local=(%.0f %.0f %.0f) seq=%d cyc=%.2f model=%s\n",
					 iEnt, pOwnerEnt ? pOwnerEnt->entindex() : -1,
					 vOwner.x, vOwner.y, vOwner.z,
					 pParent ? pParent->GetClassname() : "NONE",
					 pParent ? pParent->entindex() : -1,
					 vHere.x, vHere.y, vHere.z, vLocalOrg.x, vLocalOrg.y, vLocalOrg.z,
					 GetSequence(), (float)GetCycle(),
					 GetModel() ? modelinfo->GetModelName( GetModel() ) : "?" );
			}
		}
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

	// HL2SB: return the value BaseClass::DrawModel computed above. This was
	// dropped when the old hands-attachment block was removed (falling off the
	// end of a value-returning function is undefined behaviour; MSVC happened
	// to leave the value in a register, other toolchains may not).
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

}

void C_BaseViewModel::PostDataUpdate( DataUpdateType_t updateType )
{
	BaseClass::PostDataUpdate(updateType);
	OnLatchInterpolatedVariables( LATCH_ANIMATION_VAR );
}

//-----------------------------------------------------------------------------
// Purpose: Model swap (weapon change).  The old hands-attachment rebuild that
//          used to live here is gone with the legacy c_hands renderer; what
//          remains is GMod's OnViewModelChanged hook so the gmod_hands entity
//          re-parents onto the fresh viewmodel.
//-----------------------------------------------------------------------------
CStudioHdr *C_BaseViewModel::OnNewModel( void )
{
	// HL2SB (2026-10-02): GMod's contract is ENT:ViewModelChanged( vm, old, new )
	// - capture the OLD model name before BaseClass swaps m_nModelIndex.
	char szOldModel[ MAX_PATH ] = "";
	if ( GetModel() != NULL )
	{
		const char *pszOld = modelinfo->GetModelName( GetModel() );
		if ( pszOld != NULL )
			Q_strncpy( szOldModel, pszOld, sizeof( szOldModel ) );
	}

	CStudioHdr *pResult = BaseClass::OnNewModel();
#if defined( LUA_SDK )
	// HL2SB (2026-09-27): GMod's engine hook (the name lives only in
	// lua_shared's registry there -> by-ID dispatch; gmod_hands re-parents onto
	// the fresh viewmodel through it).  Args mirror GMod's only consumer,
	// ENT:ViewModelChanged( vm, old, new ).
	if ( L != NULL )
	{
		BEGIN_LUA_CALL_HOOK( "OnViewModelChanged" );
			lua_pushentity( L, this );
			lua_pushstring( L, szOldModel );
			const char *pszModelName = modelinfo->GetModelName( GetModel() );
			lua_pushstring( L, ( pszModelName != NULL ) ? pszModelName : "" );
		END_LUA_CALL_HOOK( 3, 0 );
	}
#endif
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

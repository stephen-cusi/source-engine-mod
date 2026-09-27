//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Client side implementation of CBaseCombatWeapon.
//
// $NoKeywords: $
//=============================================================================//
#include "cbase.h"
#include "history_resource.h"
#include "iclientmode.h"
#include "iinput.h"
#include "weapon_selection.h"
#include "hud_crosshair.h"
#include "engine/ivmodelinfo.h"
#include "tier0/vprof.h"
#include "hltvcamera.h"
#include "tier1/KeyValues.h"
#include "toolframework/itoolframework.h"
#include "toolframework_client.h"
#include "luamanager.h"	// HL2SB GMod compat: SWEP:DrawWorldModel / DrawWorldModelTranslucent
#include "lbasecombatweapon_shared.h"	// HL2SB: lua_pushweapon
#include "model_types.h"	// HL2SB: STUDIO_TRANSPARENCY

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

//-----------------------------------------------------------------------------
// Purpose: Gets the local client's active weapon, if any.
//-----------------------------------------------------------------------------
C_BaseCombatWeapon *GetActiveWeapon( void )
{
	C_BasePlayer *player = C_BasePlayer::GetLocalPlayer();

	if ( !player )
		return NULL;

	return player->GetActiveWeapon();
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void C_BaseCombatWeapon::SetDormant( bool bDormant )
{
	// If I'm going from active to dormant and I'm carried by another player, holster me.
	if ( !IsDormant() && bDormant && GetOwner() && !IsCarriedByLocalPlayer() )
	{
		Holster( NULL );
	}

	BaseClass::SetDormant( bDormant );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void C_BaseCombatWeapon::NotifyShouldTransmit( ShouldTransmitState_t state )
{
	BaseClass::NotifyShouldTransmit(state);

	if (state == SHOULDTRANSMIT_END)
	{
		if (m_iState == WEAPON_IS_ACTIVE)
		{
			m_iState = WEAPON_IS_CARRIED_BY_PLAYER;
		}
	}
	else if( state == SHOULDTRANSMIT_START )
	{
		if( m_iState == WEAPON_IS_CARRIED_BY_PLAYER )
		{
			if( GetOwner() && GetOwner()->GetActiveWeapon() == this )
			{
				// Restore the Activeness of the weapon if we client-twiddled it off in the first case above.
				m_iState = WEAPON_IS_ACTIVE;
			}
		}
	}
}


//-----------------------------------------------------------------------------
// Purpose: To wrap PORTAL mod specific functionality into one place
//-----------------------------------------------------------------------------
static inline bool ShouldDrawLocalPlayerViewModel( void )
{
#if defined( PORTAL )
	return false;
#else
	return !C_BasePlayer::ShouldDrawLocalPlayer();
#endif
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void C_BaseCombatWeapon::OnRestore()
{
	BaseClass::OnRestore();

	// if the player is holding this weapon, 
	// mark it as just restored so it won't show as a new pickup
	if (GetOwner() == C_BasePlayer::GetLocalPlayer())
	{
		m_bJustRestored = true;
	}
}

int C_BaseCombatWeapon::GetWorldModelIndex( void )
{
	if ( GameRules() )
	{
		const char *pBaseName = modelinfo->GetModelName( modelinfo->GetModel( m_iWorldModelIndex ) );
		const char *pTranslatedName = GameRules()->TranslateEffectForVisionFilter( "weapons", pBaseName );

		if ( pTranslatedName != pBaseName )
		{
			return modelinfo->GetModelIndex( pTranslatedName );
		}
	}

	return m_iWorldModelIndex;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : bnewentity - 
//-----------------------------------------------------------------------------
void C_BaseCombatWeapon::OnDataChanged( DataUpdateType_t updateType )
{
	BaseClass::OnDataChanged(updateType);

	CHandle< C_BaseCombatWeapon > handle = this;

	// If it's being carried by the *local* player, on the first update,
	// find the registered weapon for this ID

	C_BasePlayer *pPlayer = C_BasePlayer::GetLocalPlayer();
	C_BaseCombatCharacter *pOwner = GetOwner();

	// check if weapon is carried by local player
	bool bIsLocalPlayer = pPlayer && pPlayer == pOwner;
	if ( bIsLocalPlayer && ShouldDrawLocalPlayerViewModel() )		// TODO: figure out the purpose of the ShouldDrawLocalPlayer() test.
	{
		// If I was just picked up, or created & immediately carried, add myself to this client's list of weapons
		if ( (m_iState != WEAPON_NOT_CARRIED ) && (m_iOldState == WEAPON_NOT_CARRIED) )
		{
			// Tell the HUD this weapon's been picked up
			if ( ShouldDrawPickup() )
			{
				CBaseHudWeaponSelection *pHudSelection = GetHudWeaponSelection();
				if ( pHudSelection )
				{
					pHudSelection->OnWeaponPickup( this );
				}

				pPlayer->EmitSound( "Player.PickupWeapon" );
			}
		}
	}
	else // weapon carried by other player or not at all
	{
		int overrideModelIndex = CalcOverrideModelIndex();
		if( overrideModelIndex != -1 && overrideModelIndex != GetModelIndex() )
		{
			SetModelIndex( overrideModelIndex );
		}
	}

	UpdateVisibility();

	m_iOldState = m_iState;

	m_bJustRestored = false;
}

//-----------------------------------------------------------------------------
// Is anyone carrying it?
//-----------------------------------------------------------------------------
bool C_BaseCombatWeapon::IsBeingCarried() const
{
	return ( m_hOwner.Get() != NULL );
}

//-----------------------------------------------------------------------------
// Is the carrier alive?
//-----------------------------------------------------------------------------
bool C_BaseCombatWeapon::IsCarrierAlive() const
{
	if ( !m_hOwner.Get() )
		return false;

	return m_hOwner.Get()->GetHealth() > 0;
}

//-----------------------------------------------------------------------------
// Should this object cast shadows?
//-----------------------------------------------------------------------------
ShadowType_t C_BaseCombatWeapon::ShadowCastType()
{
	if ( IsEffectActive( /*EF_NODRAW |*/ EF_NOSHADOW ) )
		return SHADOWS_NONE;

	if (!IsBeingCarried())
		return SHADOWS_RENDER_TO_TEXTURE;

	if (IsCarriedByLocalPlayer() && !C_BasePlayer::ShouldDrawLocalPlayer())
		return SHADOWS_NONE;

	return SHADOWS_RENDER_TO_TEXTURE;
}

//-----------------------------------------------------------------------------
// Purpose: This weapon is the active weapon, and it should now draw anything
//			it wants to. This gets called every frame.
//-----------------------------------------------------------------------------
void C_BaseCombatWeapon::Redraw()
{
	if ( g_pClientMode->ShouldDrawCrosshair() )
	{
		DrawCrosshair();
	}

	// ammo drawing has been moved into hud_ammo.cpp
}

//-----------------------------------------------------------------------------
// Purpose: Draw the weapon's crosshair
//-----------------------------------------------------------------------------
void C_BaseCombatWeapon::DrawCrosshair()
{
	C_BasePlayer *player = C_BasePlayer::GetLocalPlayer();
	if ( !player )
		return;

	Color clr = gHUD.m_clrNormal;
/*

	// TEST: if the thing under your crosshair is on a different team, light the crosshair with a different color.
	Vector vShootPos, vShootAngles;
	GetShootPosition( vShootPos, vShootAngles );

	Vector vForward;
	AngleVectors( vShootAngles, &vForward );
	
	
	// Change the color depending on if we're looking at a friend or an enemy.
	CPartitionFilterListMask filter( PARTITION_ALL_CLIENT_EDICTS );	
	trace_t tr;
	traceline->TraceLine( vShootPos, vShootPos + vForward * 10000, COLLISION_GROUP_NONE, MASK_SHOT, &tr, true, ~0, &filter );

	if ( tr.index != 0 && tr.index != INVALID_CLIENTENTITY_HANDLE )
	{
		C_BaseEntity *pEnt = ClientEntityList().GetBaseEntityFromHandle( tr.index );
		if ( pEnt )
		{
			if ( pEnt->GetTeamNumber() != player->GetTeamNumber() )
			{
				g = b = 0;
			}
		}
	}		 
*/

	CHudCrosshair *crosshair = GET_HUDELEMENT( CHudCrosshair );
	if ( !crosshair )
		return;

	// Find out if this weapon's auto-aimed onto a target
	bool bOnTarget = ( m_iState == WEAPON_IS_ONTARGET );
	
	if ( player->GetFOV() >= 90 )
	{ 
		// normal crosshairs
		if ( bOnTarget && GetWpnData().iconAutoaim )
		{
			clr[3] = 255;

			crosshair->SetCrosshair( GetWpnData().iconAutoaim, clr );
		}
		else if ( GetWpnData().iconCrosshair )
		{
			clr[3] = 255;
			crosshair->SetCrosshair( GetWpnData().iconCrosshair, clr );
		}
		else
		{
			crosshair->ResetCrosshair();
		}
	}
	else
	{ 
		Color white( 255, 255, 255, 255 );

		// zoomed crosshairs
		if (bOnTarget && GetWpnData().iconZoomedAutoaim)
			crosshair->SetCrosshair(GetWpnData().iconZoomedAutoaim, white);
		else if ( GetWpnData().iconZoomedCrosshair )
			crosshair->SetCrosshair( GetWpnData().iconZoomedCrosshair, white );
		else
			crosshair->ResetCrosshair();
	}
}

//-----------------------------------------------------------------------------
// Purpose: This weapon is the active weapon, and the viewmodel for it was just drawn.
//-----------------------------------------------------------------------------
void C_BaseCombatWeapon::ViewModelDrawn( C_BaseViewModel *pViewModel )
{
}

//-----------------------------------------------------------------------------
// Purpose: Returns true if this client's carrying this weapon
//-----------------------------------------------------------------------------
bool C_BaseCombatWeapon::IsCarriedByLocalPlayer( void )
{
	if ( !GetOwner() )
		return false;

	return ( GetOwner() == C_BasePlayer::GetLocalPlayer() );
}


//-----------------------------------------------------------------------------
// Purpose: Returns true if this client is carrying this weapon and is
//			using the view models
//-----------------------------------------------------------------------------
bool C_BaseCombatWeapon::ShouldDrawUsingViewModel( void )
{
	return IsCarriedByLocalPlayer() && !C_BasePlayer::ShouldDrawLocalPlayer();
}


//-----------------------------------------------------------------------------
// Purpose: Returns true if this weapon is the local client's currently wielded weapon
//-----------------------------------------------------------------------------
bool C_BaseCombatWeapon::IsActiveByLocalPlayer( void )
{
	if ( IsCarriedByLocalPlayer() )
	{
		return (m_iState == WEAPON_IS_ACTIVE);
	}

	return false;
}

bool C_BaseCombatWeapon::GetShootPosition( Vector &vOrigin, QAngle &vAngles )
{
	// Get the entity because the weapon doesn't have the right angles.
	C_BaseCombatCharacter *pEnt = ToBaseCombatCharacter( GetOwner() );
	if ( pEnt )
	{
		if ( pEnt == C_BasePlayer::GetLocalPlayer() )
		{
			vAngles = pEnt->EyeAngles();
		}
		else
		{
			vAngles = pEnt->GetRenderAngles();	
		}
	}
	else
	{
		vAngles.Init();
	}

	QAngle vDummy;
	if ( IsActiveByLocalPlayer() && ShouldDrawLocalPlayerViewModel() )
	{
		C_BasePlayer *player = ToBasePlayer( pEnt );
		C_BaseViewModel *vm = player ? player->GetViewModel( 0 ) : NULL;
		if ( vm )
		{
			int iAttachment = vm->LookupAttachment( "muzzle" );
			if ( vm->GetAttachment( iAttachment, vOrigin, vDummy ) )
			{
				return true;
			}
		}
	}
	else
	{
		// Thirdperson
		int iAttachment = LookupAttachment( "muzzle" );
		if ( GetAttachment( iAttachment, vOrigin, vDummy ) )
		{
			return true;
		}
	}

	vOrigin = GetRenderOrigin();
	return false;
}


//-----------------------------------------------------------------------------
// Purpose: 
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool C_BaseCombatWeapon::ShouldDraw( void )
{
	if ( m_iWorldModelIndex == 0 )
		return false;

	// FIXME: All weapons with owners are set to transmit in CBaseCombatWeapon::UpdateTransmitState,
	// even if they have EF_NODRAW set, so we have to check this here. Ideally they would never
	// transmit except for the weapons owned by the local player.
	if ( IsEffectActive( EF_NODRAW ) )
		return false;

	C_BaseCombatCharacter *pOwner = GetOwner();

	// weapon has no owner, always draw it
	if ( !pOwner )
		return true;

	bool bIsActive = ( m_iState == WEAPON_IS_ACTIVE );

	C_BasePlayer *pLocalPlayer = C_BasePlayer::GetLocalPlayer();

	 // carried by local player?
	if ( pOwner == pLocalPlayer )
	{
		// Only ever show the active weapon
		if ( !bIsActive )
			return false;

		if ( !pOwner->ShouldDraw() )
		{
			// Our owner is invisible.
			// This also tests whether the player is zoomed in, in which case you don't want to draw the weapon.
			return false;
		}

		// HL2SB: always return true so weapon is in render list for mirror reflection.
		// DrawModel() controls actual visibility.
		return true;
	}

	// If it's a player, then only show active weapons
	if ( pOwner->IsPlayer() )
	{
		// Show it if it's active...
		return bIsActive;
	}

	// FIXME: We may want to only show active weapons on NPCs
	// These are carried by AIs; always show them
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Return true if a weapon-pickup icon should be displayed when this weapon is received
//-----------------------------------------------------------------------------
bool C_BaseCombatWeapon::ShouldDrawPickup( void )
{
	if ( GetWeaponFlags() & ITEM_FLAG_NOITEMPICKUP )
		return false;

	if ( m_bJustRestored )
		return false;

	return true;
}
		   
//-----------------------------------------------------------------------------
// Purpose: Render the weapon. Draw the Viewmodel if the weapon's being carried
//			by this player, otherwise draw the worldmodel.
//-----------------------------------------------------------------------------
extern bool g_bRenderingReflection; // HL2SB: mirror reflection flag in viewrender.cpp

int C_BaseCombatWeapon::DrawModel( int flags )
{
	VPROF_BUDGET( "C_BaseCombatWeapon::DrawModel", VPROF_BUDGETGROUP_MODEL_RENDERING );
	if ( !m_bReadyToDraw )
		return 0;

	if ( !IsVisible() )
		return 0;

	// check if local player chases owner of this weapon in first person
	C_BasePlayer *localplayer = C_BasePlayer::GetLocalPlayer();

	if ( localplayer && localplayer->IsObserver() && GetOwner() )
	{
		// don't draw weapon if chasing this guy as spectator
		// we don't check that in ShouldDraw() since this may change
		// without notification

		if ( localplayer->GetObserverMode() == OBS_MODE_IN_EYE &&
			 localplayer->GetObserverTarget() == GetOwner() )
			return false;
	}

	// HL2SB: skip world weapon model in first person (viewmodel handles it).
	// Only draw in reflection or third person.
	if ( IsCarriedByLocalPlayer() && !g_bRenderingReflection && ShouldDrawLocalPlayerViewModel() )
		return 0;

	// HL2SB (2026-09-27): the carried weapon entity's NETWORKED model is the
	// VIEWMODEL model -- server-side Equip() does SetModel( GetViewModel() ),
	// and the world-model switch in OnDataChanged only runs when a network
	// update happens to arrive.  A weapon picked up in first person therefore
	// keeps the c_ viewmodel model indefinitely: third-person views (act
	// camera, cl_first_person_uses_world_model) then bone-merge the c_ model
	// onto the player rig -- the floating / misaligned world gun (also hit
	// other weapons, per the user report).  Fix the model index right before
	// drawing: every path that reaches here is a third-person view (first
	// person returned above), so the world model is the correct one.  The
	// networked model index still wins on the next PDU; we just re-fix it per
	// frame, the same way OnDataChanged and tool recording already do.
	//
	// This MUST run BEFORE the Lua SWEP:DrawWorldModel dispatch below.  That
	// dispatch ends in `self:DrawModel()`, which resolves through the entity
	// metatable to CBaseEntity_DrawModel -> InternalDrawModel -- a draw that
	// never returns to this function, so a fixup placed after the dispatch was
	// skipped for every scripted weapon whose table carries weapon_base's
	// DrawWorldModel (the loader seeds it into every GMod-style SWEP).  Those
	// weapons rendered the NETWORKED viewmodel index in the world pass: ak47
	// showed c_ak47_beast bone-merged at the hand (the "offset" report) and
	// gmod_camera showed c_arms_animations overlaid on the body while the
	// actual camera.mdl never drew (the "camera has no model" report).  Plain
	// HL2 weapons skip the Lua branch, which is why only Lua weapons broke.
	if ( GetWorldModelIndex() > 0 && GetWorldModelIndex() != GetModelIndex() )
	{
		SetModelIndex( GetWorldModelIndex() );
	}

	// HL2SB: a c_ model used as a world model (GMod SWEPs point WorldModel at a
	// c_ model rigged for viewmodel space) reads as "held" only when it plays
	// the pose the viewmodel is playing -- by default it sits in sequence 0
	// (reference pose), which is why the mirror showed a wrongly gripped gun.
	// Sync pose by sequence NAME from the active viewmodel, so real w_ models
	// (whose sequence tables differ) are left untouched.  Same ordering rule
	// as the fixup above: the Lua dispatch must see the synced pose.
	if ( IsCarriedByLocalPlayer() && g_bRenderingReflection )
	{
		C_BasePlayer *pOwner = ToBasePlayer( GetOwner() );
		C_BaseViewModel *pVM = pOwner ? pOwner->GetViewModel( 0 ) : NULL;
		if ( pVM && GetModel() )
		{
			const char *pszSeqName = pVM->GetSequenceName( pVM->GetSequence() );
			int iSeq = ( pszSeqName && pszSeqName[0] ) ? LookupSequence( pszSeqName ) : -1;
			if ( iSeq != -1 )
			{
				if ( GetSequence() != iSeq )
					SetSequence( iSeq );
				SetCycle( pVM->GetCycle() );
				InvalidateBoneCache();
			}
		}
	}

	// HL2SB GMod compat (2026-09-23): SWEP:DrawWorldModel( flags ) for the
	// opaque pass, SWEP:DrawWorldModelTranslucent( flags ) for the translucent
	// pass.  Per the wiki, DEFINING the callback replaces the default draw
	// entirely (the examples call self:DrawModel( flags ) themselves to keep
	// it), so a scripted weapon with the method draws ONLY through Lua.
#if defined( LUA_SDK )
	if ( IsScripted() && L != NULL && lua_isrefvalid( L, m_nTableReference ) )
	{
		// ⚠️ recursion latch (2026-09-24): the GMod-documented pattern inside
		// WEAPON:DrawWorldModel is `self:DrawModel( flags )` -- which re-enters
		// THIS function.  While dispatching, fall straight through to the
		// default render so that inner call draws the plain model instead of
		// recursing forever.
		static bool s_bInDrawWorldModel = false;
		if ( s_bInDrawWorldModel )
			return BaseClass::DrawModel( flags );

		bool bTranslucent = ( flags & STUDIO_TRANSPARENCY ) != 0;
		const char *pszMethod = bTranslucent ? "DrawWorldModelTranslucent" : "DrawWorldModel";

		lua_getref( L, m_nTableReference );
		lua_getfield( L, -1, pszMethod );
		if ( lua_isfunction( L, -1 ) )
		{
			// The stack must be [table, func, self, flags] BEFORE lua_remove(-4).
			// It used to run while only [table, func] were pushed, so -4 pointed
			// one TValue BEFORE the stack array base - the heap block header of
			// the stack itself.  lua_rotate then rewrote that header every frame
			// a scripted world model drew, and the next luaD_shrinkstack realloc
			// hit the corruption (the nuke-launch heap crashes, 2026-09-23, each
			// caught with sent_nuke / weapon_redeemer on screen).  Push self and
			// flags first, then drop the table so pcall sees the function at
			// -(nargs)-1.
			lua_pushweapon( L, this );
			lua_pushinteger( L, flags );
			lua_remove( L, -4 );
			s_bInDrawWorldModel = true;
			luasrc_pcall( L, 2, 0, 0 );
			s_bInDrawWorldModel = false;
			return 1;
		}
		lua_pop( L, 2 );
	}
#endif

	return BaseClass::DrawModel( flags );
}

//-----------------------------------------------------------------------------
// Allows the client-side entity to override what the network tells it to use for
// a model. This is used for third person mode, specifically in HL2 where the
// the weapon timings are on the view model and not the world model. That means the
// server needs to use the view model, but the client wants to use the world model.
//-----------------------------------------------------------------------------
int C_BaseCombatWeapon::CalcOverrideModelIndex() 
{ 
	C_BasePlayer *localplayer = C_BasePlayer::GetLocalPlayer();
	if ( localplayer && 
		localplayer == GetOwner() &&
		ShouldDrawLocalPlayerViewModel() )
	{
		return BaseClass::CalcOverrideModelIndex();
	}
	else
	{
		return GetWorldModelIndex();
	}
}

//-----------------------------------------------------------------------------
// tool recording
//-----------------------------------------------------------------------------
void C_BaseCombatWeapon::GetToolRecordingState( KeyValues *msg )
{
	if ( !ToolsEnabled() )
		return;

	int nModelIndex = GetModelIndex();
	int nWorldModelIndex = GetWorldModelIndex();
	if ( nModelIndex != nWorldModelIndex )
	{
		SetModelIndex( nWorldModelIndex );
	}

	BaseClass::GetToolRecordingState( msg );

	if ( m_iState == WEAPON_NOT_CARRIED )
	{
		BaseEntityRecordingState_t *pBaseEntity = (BaseEntityRecordingState_t*)msg->GetPtr( "baseentity" );
		pBaseEntity->m_nOwner = -1;
	}
	else
	{
		msg->SetInt( "worldmodel", 1 );
		if ( m_iState == WEAPON_IS_ACTIVE )
		{
			BaseEntityRecordingState_t *pBaseEntity = (BaseEntityRecordingState_t*)msg->GetPtr( "baseentity" );
			pBaseEntity->m_bVisible = true;
		}
	}

	if ( nModelIndex != nWorldModelIndex )
	{
		SetModelIndex( nModelIndex );
	}
}

//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#include "cbase.h"
#include "hud.h"
#include "hud_crosshair.h"
#include "iclientmode.h"
#include "view.h"
#include "vgui_controls/Controls.h"
#include "vgui/ISurface.h"
#include "ivrenderview.h"
#include "materialsystem/imaterialsystem.h"
#include "VGuiMatSurface/IMatSystemSurface.h"
#include "client_virtualreality.h"
#include "sourcevr/isourcevirtualreality.h"
#include "luamanager.h"	// HL2SB GMod compat: SWEP:DoDrawCrosshair
#include "lbasecombatweapon_shared.h"	// HL2SB: lua_pushweapon

#ifdef SIXENSE
#include "sixense/in_sixense.h"
#endif

#ifdef PORTAL
#include "c_portal_player.h"
#endif // PORTAL

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

ConVar crosshair( "crosshair", "1", FCVAR_ARCHIVE );
ConVar cl_observercrosshair( "cl_observercrosshair", "1", FCVAR_ARCHIVE );

// HL2SB: the crosshair customization family the reference registers on the
// client.  Names, defaults, clamps and help strings line up with the editor
// that drives them (lua/menu/crosshair_setup.lua) -- all archive + client flags.
ConVar cl_crosshairstyle( "cl_crosshairstyle", "1", FCVAR_ARCHIVE | FCVAR_CLIENTDLL, "0 = Default HL2, 1 = Custom image, 2 = Custom Image + HL, 3 = Classic" );
ConVar cl_crosshairusealpha( "cl_crosshairusealpha", "1", FCVAR_ARCHIVE | FCVAR_CLIENTDLL, "If set, use cl_crosshairalpha, otherwise be additive" );
ConVar cl_crosshairgap( "cl_crosshairgap", "5", FCVAR_ARCHIVE | FCVAR_CLIENTDLL, "Gap between crosshair middle and the horizontal lines" );
ConVar cl_crosshairsize( "cl_crosshairsize", "6", FCVAR_ARCHIVE | FCVAR_CLIENTDLL, "Length of each of the crosshair lines" );
ConVar cl_crosshairthickness( "cl_crosshairthickness", "0", FCVAR_ARCHIVE | FCVAR_CLIENTDLL, "Thinkness of each of the crosshair lines" );
ConVar cl_crosshairdot( "cl_crosshairdot", "0", FCVAR_ARCHIVE | FCVAR_CLIENTDLL, "Whether to draw a dot in the middle of the crosshair" );
ConVar cl_crosshair_t( "cl_crosshair_t", "0", FCVAR_ARCHIVE | FCVAR_CLIENTDLL, "Whether to draw the top line nor not" );
ConVar cl_crosshaircolor_r( "cl_crosshaircolor_r", "255", FCVAR_ARCHIVE | FCVAR_CLIENTDLL, "Classic Crosshair red color", true, 0, true, 255 );
ConVar cl_crosshaircolor_g( "cl_crosshaircolor_g", "255", FCVAR_ARCHIVE | FCVAR_CLIENTDLL, "Classic Crosshair green color", true, 0, true, 255 );
ConVar cl_crosshaircolor_b( "cl_crosshaircolor_b", "255", FCVAR_ARCHIVE | FCVAR_CLIENTDLL, "Classic Crosshair blue color", true, 0, true, 255 );
ConVar cl_crosshairalpha( "cl_crosshairalpha", "255", FCVAR_ARCHIVE | FCVAR_CLIENTDLL, "Classic Crosshair alpha", true, 0, true, 255 );
ConVar cl_crosshair_drawoutline( "cl_crosshair_drawoutline", "1", FCVAR_ARCHIVE | FCVAR_CLIENTDLL, "Draws a black outline around the crosshair for better visibility" );
ConVar cl_crosshair_outlinethickness( "cl_crosshair_outlinethickness", "1", FCVAR_ARCHIVE | FCVAR_CLIENTDLL, "Set how thick you want your crosshair outline to draw (0.1-3)", true, 0.1f, true, 3.0f );

using namespace vgui;

int ScreenTransform( const Vector& point, Vector& screen );

#ifdef TF_CLIENT_DLL
// If running TF, we use CHudTFCrosshair instead (which is derived from CHudCrosshair)
#else
DECLARE_HUDELEMENT( CHudCrosshair );
#endif

CHudCrosshair::CHudCrosshair( const char *pElementName ) :
		CHudElement( pElementName ), BaseClass( NULL, "HudCrosshair" )
{
	vgui::Panel *pParent = g_pClientMode->GetViewport();
	SetParent( pParent );

	m_pCrosshair = 0;

	m_clrCrosshair = Color( 0, 0, 0, 0 );

	m_vecCrossHairOffsetAngle.Init();

	SetHiddenBits( HIDEHUD_PLAYERDEAD | HIDEHUD_CROSSHAIR );
}

CHudCrosshair::~CHudCrosshair()
{
}

void CHudCrosshair::ApplySchemeSettings( IScheme *scheme )
{
	BaseClass::ApplySchemeSettings( scheme );

	m_pDefaultCrosshair = gHUD.GetIcon("crosshair_default");
	SetPaintBackgroundEnabled( false );

    SetSize( ScreenWidth(), ScreenHeight() );

	SetForceStereoRenderToFrameBuffer( true );
}

//-----------------------------------------------------------------------------
// Purpose: Save CPU cycles by letting the HUD system early cull
// costly traversal.  Called per frame, return true if thinking and 
// painting need to occur.
//-----------------------------------------------------------------------------
// HL2SB GMod compat: Player:CrosshairDisable/Enable toggle this (lc_baseplayer.cpp)
extern bool g_HL2SB_CrosshairHidden;

bool CHudCrosshair::ShouldDraw( void )
{
	if ( g_HL2SB_CrosshairHidden )
		return false;

	bool bNeedsDraw;

	if ( m_bHideCrosshair )
		return false;

	C_BasePlayer* pPlayer = C_BasePlayer::GetLocalPlayer();
	if ( !pPlayer )
		return false;

	C_BaseCombatWeapon *pWeapon = pPlayer->GetActiveWeapon();
	if ( pWeapon && !pWeapon->ShouldDrawCrosshair() )
		return false;

#ifdef PORTAL
	C_Portal_Player *portalPlayer = ToPortalPlayer(pPlayer);
	if ( portalPlayer && portalPlayer->IsSuppressingCrosshair() )
		return false;
#endif // PORTAL

	/* disabled to avoid assuming it's an HL2 player.
	// suppress crosshair in zoom.
	if ( pPlayer->m_HL2Local.m_bZooming )
		return false;
	*/

	// draw a crosshair only if alive or spectating in eye
	// HL2SB: the custom styles (cl_crosshairstyle >= 1) draw without a weapon
	// sprite, so the sprite requirement only gates the default style.
	bool bCustomStyle = ( cl_crosshairstyle.GetInt() >= 1 );
	if ( IsX360() )
	{
		bNeedsDraw = ( m_pCrosshair || bCustomStyle ) &&
			!engine->IsDrawingLoadingImage() &&
			!engine->IsPaused() &&
			( !pPlayer->IsSuitEquipped() || g_pGameRules->IsMultiplayer() ) &&
			g_pClientMode->ShouldDrawCrosshair() &&
			!( pPlayer->GetFlags() & FL_FROZEN ) &&
			( pPlayer->entindex() == render->GetViewEntity() ) &&
			( pPlayer->IsAlive() ||	( pPlayer->GetObserverMode() == OBS_MODE_IN_EYE ) || ( cl_observercrosshair.GetBool() && pPlayer->GetObserverMode() == OBS_MODE_ROAMING ) );
	}
	else
	{
		bNeedsDraw = ( m_pCrosshair || bCustomStyle ) &&
			crosshair.GetInt() &&
			!engine->IsDrawingLoadingImage() &&
			!engine->IsPaused() && 
			g_pClientMode->ShouldDrawCrosshair() &&
			!( pPlayer->GetFlags() & FL_FROZEN ) &&
			( pPlayer->entindex() == render->GetViewEntity() ) &&
			!pPlayer->IsInVGuiInputMode() &&
			( pPlayer->IsAlive() ||	( pPlayer->GetObserverMode() == OBS_MODE_IN_EYE ) || ( cl_observercrosshair.GetBool() && pPlayer->GetObserverMode() == OBS_MODE_ROAMING ) );
	}

	return ( bNeedsDraw && CHudElement::ShouldDraw() );
}

#ifdef TF_CLIENT_DLL
extern ConVar cl_crosshair_red;
extern ConVar cl_crosshair_green;
extern ConVar cl_crosshair_blue;
extern ConVar cl_crosshair_scale;
#endif


void CHudCrosshair::GetDrawPosition ( float *pX, float *pY, bool *pbBehindCamera, QAngle angleCrosshairOffset )
{
	QAngle curViewAngles = CurrentViewAngles();
	Vector curViewOrigin = CurrentViewOrigin();

	int vx, vy, vw, vh;
	vgui::surface()->GetFullscreenViewport( vx, vy, vw, vh );

	float screenWidth = vw;
	float screenHeight = vh;

	float x = screenWidth / 2;
	float y = screenHeight / 2;

	bool bBehindCamera = false;

	C_BasePlayer* pPlayer = C_BasePlayer::GetLocalPlayer();
	if ( ( pPlayer != NULL ) && ( pPlayer->GetObserverMode()==OBS_MODE_NONE ) )
	{
		bool bUseOffset = false;
		
		Vector vecStart;
		Vector vecEnd;

		if ( UseVR() )
		{
			// These are the correct values to use, but they lag the high-speed view data...
			vecStart = pPlayer->Weapon_ShootPosition();
			Vector vecAimDirection = pPlayer->GetAutoaimVector( 1.0f );
			// ...so in some aim modes, they get zapped by something completely up-to-date.
			g_ClientVirtualReality.OverrideWeaponHudAimVectors ( &vecStart, &vecAimDirection );
			vecEnd = vecStart + vecAimDirection * MAX_TRACE_LENGTH;

			bUseOffset = true;
		}

#ifdef SIXENSE
		// TODO: actually test this Sixsense code interaction with things like HMDs & stereo.
        if ( g_pSixenseInput->IsEnabled() && !UseVR() )
		{
			// Never autoaim a predicted weapon (for now)
			vecStart = pPlayer->Weapon_ShootPosition();
			Vector aimVector;
			AngleVectors( CurrentViewAngles() - g_pSixenseInput->GetViewAngleOffset(), &aimVector );
			// calculate where the bullet would go so we can draw the cross appropriately
			vecEnd = vecStart + aimVector * MAX_TRACE_LENGTH;
			bUseOffset = true;
		}
#endif

		if ( bUseOffset )
		{
			trace_t tr;
			UTIL_TraceLine( vecStart, vecEnd, MASK_SHOT, pPlayer, COLLISION_GROUP_NONE, &tr );

			Vector screen;
			screen.Init();
			bBehindCamera = ScreenTransform(tr.endpos, screen) != 0;

			x = 0.5f * ( 1.0f + screen[0] ) * screenWidth + 0.5f;
			y = 0.5f * ( 1.0f - screen[1] ) * screenHeight + 0.5f;
		}
	}

	// MattB - angleCrosshairOffset is the autoaim angle.
	// if we're not using autoaim, just draw in the middle of the 
	// screen
	if ( angleCrosshairOffset != vec3_angle )
	{
		QAngle angles;
		Vector forward;
		Vector point, screen;

		// this code is wrong
		angles = curViewAngles + angleCrosshairOffset;
		AngleVectors( angles, &forward );
		VectorAdd( curViewOrigin, forward, point );
		ScreenTransform( point, screen );

		x += 0.5f * screen[0] * screenWidth + 0.5f;
		y += 0.5f * screen[1] * screenHeight + 0.5f;
	}

	*pX = x;
	*pY = y;
	*pbBehindCamera = bBehindCamera;
}


//-----------------------------------------------------------------------------
// HL2SB: the reference's custom crosshair renderer, client side.
//   style 1   - the crosshair image, tinted with the color/alpha family.
//   style > 1 - classic geometry: length/thickness scale with ScrH/480, the gap
//               is unscaled, the top bar is optional (cl_crosshair_t), an
//               optional center dot, and every piece can carry a black outline
//               (cl_crosshair_drawoutline / cl_crosshair_outlinethickness).
//   cl_crosshairusealpha == 0 flips the classic pieces to additive (alpha
//   forced to 200 over the white additive material), mirroring the editor's
//   own preview.
//-----------------------------------------------------------------------------
static void HL2SB_CrosshairRect( int x0, int y0, int x1, int y1,
								 const Color &clr, bool bAdditive, int iAddTextureID )
{
	if ( cl_crosshair_drawoutline.GetBool() )
	{
		float flThick = clamp( cl_crosshair_outlinethickness.GetFloat(), 0.1f, 3.0f );
		vgui::surface()->DrawSetColor( 0, 0, 0, clr.a() );
		vgui::surface()->DrawFilledRect( (int)( x0 - flThick ), (int)( y0 - flThick ),
										 (int)( x1 + flThick ), (int)( y1 + flThick ) );
	}

	vgui::surface()->DrawSetColor( clr.r(), clr.g(), clr.b(), clr.a() );
	if ( bAdditive )
	{
		vgui::surface()->DrawSetTexture( iAddTextureID );
		vgui::surface()->DrawTexturedRect( x0, y0, x1, y1 );
	}
	else
	{
		vgui::surface()->DrawFilledRect( x0, y0, x1, y1 );
	}
}

static void HL2SB_DrawCustomCrosshair( int x, int y )
{
	int iStyle = cl_crosshairstyle.GetInt();
	float flScale = (float)ScreenHeight() / 480.0f;

	int r = clamp( cl_crosshaircolor_r.GetInt(), 0, 255 );
	int g = clamp( cl_crosshaircolor_g.GetInt(), 0, 255 );
	int b = clamp( cl_crosshaircolor_b.GetInt(), 0, 255 );
	int a = clamp( cl_crosshairalpha.GetInt(), 0, 255 );
	bool bAdditive = ( cl_crosshairusealpha.GetInt() == 0 );

	if ( iStyle == 1 )
	{
		// the crosshair image, centered, tinted with the color family
		IMaterial *pMat = materials->FindMaterial( "gui/crosshair.png", TEXTURE_GROUP_VGUI );
		static int iTextureID = -1;
		if ( iTextureID == -1 )
		{
			iTextureID = vgui::surface()->CreateNewTextureID();
			g_pMatSystemSurface->DrawSetTextureMaterial( iTextureID, pMat );
		}
		vgui::surface()->DrawSetColor( r, g, b, a );
		vgui::surface()->DrawSetTexture( iTextureID );
		int w = 0, h = 0;
		vgui::surface()->DrawGetTextureSize( iTextureID, w, h );
		vgui::surface()->DrawTexturedRect( x - w / 2, y - h / 2, x - w / 2 + w, y - h / 2 + h );
		return;
	}

	int iAddTextureID = 0;
	if ( bAdditive )
	{
		IMaterial *pAdd = materials->FindMaterial( "vgui/white_additive", TEXTURE_GROUP_VGUI );
		static int iCachedAddTextureID = -1;
		if ( iCachedAddTextureID == -1 )
		{
			iCachedAddTextureID = vgui::surface()->CreateNewTextureID();
			g_pMatSystemSurface->DrawSetTextureMaterial( iCachedAddTextureID, pAdd );
		}
		iAddTextureID = iCachedAddTextureID;
		a = 200;
	}
	Color clr( r, g, b, a );

	int iBarSize = (int)( flScale * cl_crosshairsize.GetFloat() + 0.5f );
	int iThickness = max( 1, (int)( flScale * cl_crosshairthickness.GetFloat() + 0.5f ) );
	int iGap = cl_crosshairgap.GetInt();

	int iInnerLeft = x - iGap - iThickness / 2;
	int iInnerRight = iInnerLeft + 2 * iGap + iThickness;
	int iOuterLeft = iInnerLeft - iBarSize;
	int iOuterRight = iInnerRight + iBarSize;
	int y0 = y - iThickness / 2;
	int y1 = y0 + iThickness;

	HL2SB_CrosshairRect( iOuterLeft, y0, iInnerLeft, y1, clr, bAdditive, iAddTextureID );
	HL2SB_CrosshairRect( iInnerRight, y0, iOuterRight, y1, clr, bAdditive, iAddTextureID );

	int x0 = x - iThickness / 2;
	int x1 = x0 + iThickness;
	int iInnerTop = y - iGap - iThickness / 2;
	int iInnerBottom = iInnerTop + 2 * iGap + iThickness;
	int iOuterTop = iInnerTop - iBarSize;
	int iOuterBottom = iInnerBottom + iBarSize;

	if ( cl_crosshair_t.GetInt() == 0 )
		HL2SB_CrosshairRect( x0, iOuterTop, x1, iInnerTop, clr, bAdditive, iAddTextureID );
	HL2SB_CrosshairRect( x0, iInnerBottom, x1, iOuterBottom, clr, bAdditive, iAddTextureID );

	if ( cl_crosshairdot.GetInt() != 0 )
		HL2SB_CrosshairRect( x0, y - iThickness / 2, x1, y - iThickness / 2 + iThickness, clr, bAdditive, iAddTextureID );
}

void CHudCrosshair::Paint( void )
{
	if ( !m_pCrosshair && cl_crosshairstyle.GetInt() < 1 )
		return;

	if ( !IsCurrentViewAccessAllowed() )
		return;

	C_BasePlayer* pPlayer = C_BasePlayer::GetLocalPlayer();
	if ( !pPlayer )
		return;

	float x, y;
	bool bBehindCamera;
	GetDrawPosition ( &x, &y, &bBehindCamera, m_vecCrossHairOffsetAngle );

	if( bBehindCamera )
		return;

	float flWeaponScale = 1.f;
	// HL2SB: the custom styles reach Paint before any weapon has attached its
	// sprite texture (fresh level load, cancelled load), so the sprite size
	// reads are guarded -- the style >= 1 branch below never uses them, and the
	// style 0 branch only runs when the top guard proved m_pCrosshair valid.
	int iTextureW = m_pCrosshair ? m_pCrosshair->Width() : 0;
	int iTextureH = m_pCrosshair ? m_pCrosshair->Height() : 0;
	C_BaseCombatWeapon *pWeapon = pPlayer->GetActiveWeapon();
	if ( pWeapon )
	{
		pWeapon->GetWeaponCrosshairScale( flWeaponScale );
	}

	// HL2SB GMod compat (2026-09-23): SWEP:DoDrawCrosshair( x, y ) -- literal
	// true means the callback drew its own crosshair and the default one is
	// skipped (wiki: "Return true to override the default crosshair").
#if defined( LUA_SDK )
	if ( pWeapon && pWeapon->IsScripted() )
	{
		BEGIN_LUA_CALL_WEAPON_HOOK( "DoDrawCrosshair", pWeapon );
			lua_pushnumber( L, x );
			lua_pushnumber( L, y );
		END_LUA_CALL_WEAPON_HOOK( 2, 1 );

		if ( lua_gettop( L ) > 0 )
		{
			bool bOverride = lua_isboolean( L, -1 ) && lua_toboolean( L, -1 ) != 0;
			lua_pop( L, 1 );
			if ( bOverride )
				return;
		}
	}
#endif

	// HL2SB: the reference dispatches on cl_crosshairstyle before the weapon
	// sprite path -- style 0 keeps the classic sprite below, everything else
	// draws the customization family and stops there.
	if ( cl_crosshairstyle.GetInt() >= 1 )
	{
		HL2SB_DrawCustomCrosshair( (int)( x + 0.5f ), (int)( y + 0.5f ) );
		return;
	}

	int iScreenDiv = 1600;
	if ( IsSteamDeck() )
		iScreenDiv = 1440;

	float flPlayerScale;
	if ( !m_pCrosshair->bRenderUsingFont )
		flPlayerScale = (ScreenHeight() / iScreenDiv) + 1;
	else
		flPlayerScale = 1.0f;
#ifdef TF_CLIENT_DLL
	Color clr( cl_crosshair_red.GetInt(), cl_crosshair_green.GetInt(), cl_crosshair_blue.GetInt(), 255 );
	flPlayerScale = cl_crosshair_scale.GetFloat() / 32.0f;  // the player can change the scale in the options/multiplayer tab
#else
	Color clr = m_clrCrosshair;
#endif
	float flWidth = flWeaponScale * flPlayerScale * (float)iTextureW;
	float flHeight = flWeaponScale * flPlayerScale * (float)iTextureH;
	int iWidth = (int)( flWidth + 0.5f );
	int iHeight = (int)( flHeight + 0.5f );
	int iX = (int)( x + 0.5f );
	int iY = (int)( y + 0.5f );

	m_pCrosshair->DrawSelfCropped (
		iX-(iWidth/2), iY-(iHeight/2),
		0, 0,
		iTextureW, iTextureH,
		iWidth, iHeight,
		clr );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CHudCrosshair::SetCrosshairAngle( const QAngle& angle )
{
	VectorCopy( angle, m_vecCrossHairOffsetAngle );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CHudCrosshair::SetCrosshair( CHudTexture *texture, const Color& clr )
{
	m_pCrosshair = texture;
	m_clrCrosshair = clr;
}

//-----------------------------------------------------------------------------
// Purpose: Resets the crosshair back to the default
//-----------------------------------------------------------------------------
void CHudCrosshair::ResetCrosshair()
{
	SetCrosshair( m_pDefaultCrosshair, Color(255, 255, 255, 255) );
}

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
#include "in_buttons.h"
#include "ivrenderview.h"
#include "materialsystem/imaterialsystem.h"
#include "VGuiMatSurface/IMatSystemSurface.h"
#include "client_virtualreality.h"
#include "sourcevr/isourcevirtualreality.h"

#ifdef SIXENSE
#include "sixense/in_sixense.h"
#endif

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

ConVar crosshair( "crosshair", "1", FCVAR_ARCHIVE );
ConVar cl_crosshair_scale( "cl_crosshair_scale", "0.1", FCVAR_ARCHIVE );
ConVar cl_observercrosshair( "cl_observercrosshair", "1", FCVAR_ARCHIVE );

using namespace vgui;

int ScreenTransform( const Vector& point, Vector& screen );

DECLARE_HUDELEMENT( CHudCrosshair );

CHudCrosshair::CHudCrosshair( const char *pElementName ) :
		CHudElement( pElementName ), BaseClass( NULL, "HudCrosshair" )
{
	vgui::Panel *pParent = g_pClientMode->GetViewport();
	SetParent( pParent );

	m_pCrosshair = vgui::scheme()->GetImage("hud/crosshair", false);

	m_vecCrossHairOffsetAngle.Init();
	m_flLastUse = 0;
	m_flLastThink = gpGlobals->curtime;
	m_iAlpha = 0;

	SetHiddenBits( HIDEHUD_PLAYERDEAD | HIDEHUD_CROSSHAIR );
}

CHudCrosshair::~CHudCrosshair()
{
}

void CHudCrosshair::Reset()
{
	m_flLastUse = 0;
	m_flLastThink = gpGlobals->curtime;
	m_iAlpha = 0;
}

void CHudCrosshair::ApplySchemeSettings( IScheme *scheme )
{
	BaseClass::ApplySchemeSettings( scheme );

	SetPaintBackgroundEnabled( false );

    SetSize( ScreenWidth(), ScreenHeight() );

	SetForceStereoRenderToFrameBuffer( true );
}

//-----------------------------------------------------------------------------
// Purpose: Save CPU cycles by letting the HUD system early cull
// costly traversal.  Called per frame, return true if thinking and 
// painting need to occur.
//-----------------------------------------------------------------------------
bool CHudCrosshair::ShouldDraw( void )
{
	float dt = gpGlobals->curtime - m_flLastThink;
	m_flLastThink = gpGlobals->curtime;
	bool bNeedsDraw;

	if ( m_bHideCrosshair )
		return false;

	C_BasePlayer* pPlayer = C_BasePlayer::GetLocalPlayer();
	if ( !pPlayer )
		return false;

	if(pPlayer->m_nButtons & IN_USE)
		m_flLastUse = gpGlobals->curtime;

	bool isHatchet = false;
	C_BaseCombatWeapon *pWeapon = pPlayer->GetActiveWeapon();

	if ( pWeapon && Q_strcmp(pWeapon->GetClassname(),"weapon_nh_hatchet") == 0)
		isHatchet = true;

	bool bIsVisible = false;
	bIsVisible |= pWeapon && !isHatchet && pPlayer->m_nButtons & IN_ATTACK2;
	bIsVisible |= m_flLastUse > 0 && m_flLastUse >= gpGlobals->curtime - 1.5;

	if(bIsVisible)
		m_iAlpha += 700 * dt;
	else
		m_iAlpha -= 700 * dt;

	m_iAlpha = clamp(m_iAlpha, 0, 255);

	if(m_iAlpha < 1)
		return false;

	// draw a crosshair only if alive or spectating in eye
	if ( IsX360() )
	{
		bNeedsDraw = m_pCrosshair && 
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
		bNeedsDraw = m_pCrosshair && 
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


void CHudCrosshair::Paint( void )
{
	if ( !m_pCrosshair )
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
	float flPlayerScale = cl_crosshair_scale.GetFloat();
	float flWidth = flWeaponScale * flPlayerScale * 32.0;
	float flHeight = flWeaponScale * flPlayerScale * 32.0;
	int iWidth = (int)( flWidth + 0.5f );
	int iHeight = (int)( flHeight + 0.5f );
	int iX = (int)( x + 0.5f );
	int iY = (int)( y + 0.5f );

	m_pCrosshair->SetPos(iX-(iWidth/2), iY-(iHeight/2));
	m_pCrosshair->SetSize(iWidth, iHeight);
	m_pCrosshair->SetColor(Color(255,255,255,m_iAlpha));
	m_pCrosshair->Paint();
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
}

//-----------------------------------------------------------------------------
// Purpose: Resets the crosshair back to the default
//-----------------------------------------------------------------------------
void CHudCrosshair::ResetCrosshair()
{
}

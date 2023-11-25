//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Item pickup history displayed onscreen when items are picked up.
//
// $NoKeywords: $
//=============================================================================//
#include "cbase.h"
#include "hud_pickups.h"
#include "hud_macros.h"
#include <vgui_controls/Controls.h>
#include <vgui/ILocalize.h>
#include <vgui/ISurface.h>
#include "iclientmode.h"
#include "vgui_controls/AnimationController.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

ConVar hud_pickups_debug("hud_pickups_debug", "0");

using namespace vgui;

DECLARE_HUDELEMENT( CHudPickups );
DECLARE_HUD_MESSAGE( CHudPickups, ItemPickup );

//-----------------------------------------------------------------------------
// Purpose: Constructor
//-----------------------------------------------------------------------------
CHudPickups::CHudPickups( const char *pElementName ) :
	CHudElement( pElementName ), BaseClass( NULL, "NH2Pickups" )
{	
	vgui::Panel *pParent = g_pClientMode->GetViewport();
	SetParent(pParent);
	SetHiddenBits(HIDEHUD_NEEDSUIT);

	m_iWep = 0;
	m_iAmmo = 0;
	m_iAmmoQuantity = 0;

	m_pHealthKit = vgui::scheme()->GetImage("hud/medkit", false);
	m_pHealthVial = vgui::scheme()->GetImage("hud/bandage", false);

	m_pWep[0] = vgui::scheme()->GetImage("hud/slot1_icon", false);
	m_pWep[1] = vgui::scheme()->GetImage("hud/slot2_icon", false);
	m_pWep[2] = vgui::scheme()->GetImage("hud/slot3_icon", false);
	m_pWep[3] = vgui::scheme()->GetImage("hud/slot4_icon", false);
	m_pWep[4] = vgui::scheme()->GetImage("hud/slot5_icon", false);

	m_pNumbers = vgui::scheme()->GetImage("hud/numbers", false);
	m_pAmmo[0] = vgui::scheme()->GetImage("hud/slot2_ammo_icon", false);
	m_pAmmo[1] = vgui::scheme()->GetImage("hud/slot3_ammo_icon", false);
	m_pAmmo[2] = vgui::scheme()->GetImage("hud/slot4_ammo_icon", false);
	m_pAmmo[3] = vgui::scheme()->GetImage("hud/slot5_ammo_icon", false);
}

void CHudPickups::Init()
{
	HOOK_HUD_MESSAGE( CHudPickups, ItemPickup );
	Reset();
}

void CHudPickups::Reset()
{
	SetFgColor(Color(255,255,255,255));
	SetBgColor(Color(0,0,0,0));
	m_bKitVisible = false;
	m_bVialVisible = false;
	m_bWepVisible = false;
	m_bAmmoVisible = false;
	m_flKitAlpha = 0;
	m_flVialAlpha = 0;
	m_flWepAlpha = 0;
	m_flAmmoAlpha = 0;
}

void CHudPickups::Think()
{
	if(m_bKitVisible && m_flKitUntil < gpGlobals->curtime)
	{
		m_bKitVisible = false;
		g_pClientMode->GetViewportAnimationController()->RunAnimationCommand( this, "KitAlpha", 0.0f, 0.0f, m_flFadeTime, vgui::AnimationController::INTERPOLATOR_LINEAR);
	}
	if(m_bVialVisible && m_flVialUntil < gpGlobals->curtime)
	{
		m_bVialVisible = false;
		g_pClientMode->GetViewportAnimationController()->RunAnimationCommand( this, "VialAlpha", 0.0f, 0.0f, m_flFadeTime, vgui::AnimationController::INTERPOLATOR_LINEAR);
	}

	if(m_bWepVisible && m_flWepUntil < gpGlobals->curtime)
	{
		m_bWepVisible = false;
		g_pClientMode->GetViewportAnimationController()->RunAnimationCommand( this, "WepAlpha", 0.0f, 0.0f, m_flFadeTime, vgui::AnimationController::INTERPOLATOR_LINEAR);
	}

	if(m_bAmmoVisible && m_flAmmoUntil < gpGlobals->curtime)
	{
		m_bAmmoVisible = false;
		g_pClientMode->GetViewportAnimationController()->RunAnimationCommand( this, "AmmoAlpha", 0.0f, 0.0f, m_flFadeTime, vgui::AnimationController::INTERPOLATOR_LINEAR);
	}
}

void CHudPickups::ShowPickup(C_BaseCombatWeapon *weapon)
{
	//Add weapon to history
	m_iWep = weapon->GetSlot();
	g_pClientMode->GetViewportAnimationController()->RunAnimationCommand( this, "WepAlpha", 1.0f, 0.0f, m_flFadeTime, vgui::AnimationController::INTERPOLATOR_LINEAR);
	m_bWepVisible = true;
	m_flWepUntil = gpGlobals->curtime + m_flDisplayTime;
}

void CHudPickups::ShowAmmo(int type, int quantity)
{
	switch(type)
	{
		case 3:
			m_iAmmo = 0; break;
		case 5:
			m_iAmmo = 1; break;
		case 4:
			m_iAmmo = 2; break;
		case 7:
			m_iAmmo = 3; break;
		default:
			return;
	}
	m_iAmmoQuantity = quantity;
	m_bAmmoVisible = true;
	m_flAmmoUntil = gpGlobals->curtime + m_flDisplayTime;
	g_pClientMode->GetViewportAnimationController()->RunAnimationCommand( this, "AmmoAlpha", 1.0f, 0.0f, m_flFadeTime, vgui::AnimationController::INTERPOLATOR_LINEAR);
	g_pClientMode->GetViewportAnimationController()->RunAnimationCommand( this, "QtyAlpha", 1.0f, 0.0f, m_flFadeTime, vgui::AnimationController::INTERPOLATOR_LINEAR);
}

void CHudPickups::MsgFunc_ItemPickup( bf_read &msg )
{
	char szName[1024];
	msg.ReadString(szName, sizeof(szName));
	// Add the item to the history
	if(Q_strcmp("item_healthkit", szName) == 0)
	{
		g_pClientMode->GetViewportAnimationController()->RunAnimationCommand( this, "KitAlpha", 1.0f, 0.0f, m_flFadeTime, vgui::AnimationController::INTERPOLATOR_LINEAR);
		m_bKitVisible = true;
		m_flKitUntil = gpGlobals->curtime + m_flDisplayTime;
	}

	if(Q_strcmp("item_healthvial", szName) == 0)
	{
		g_pClientMode->GetViewportAnimationController()->RunAnimationCommand( this, "VialAlpha", 1.0f, 0.0f, m_flFadeTime, vgui::AnimationController::INTERPOLATOR_LINEAR);
		m_bVialVisible = true;
		m_flVialUntil = gpGlobals->curtime + m_flDisplayTime;
	}
}

double calcPos(double pos, double scale, double max)
{
	return pos >= 0 ? scale * pos : scale * pos + max;
}

void CHudPickups::Paint()
{
	int wide, tall;
	GetHudSize(wide, tall);
	SetBounds(0, 0, wide, tall);

	float scale = tall / 480.0;
	bool dbg = hud_pickups_debug.GetBool();

	m_pHealthKit->SetSize(scale * m_flKitScale, scale * m_flKitScale);
	m_pHealthKit->SetPos(calcPos(m_flKitX, scale, wide), calcPos(m_flKitY, scale, tall));
	m_pHealthKit->SetColor(Color(255,255,255,255 * (dbg ? 1.0 : m_flKitAlpha)));
	m_pHealthKit->Paint();

	m_pHealthVial->SetSize(scale * m_flVialScale, scale * m_flVialScale);
	m_pHealthVial->SetPos(calcPos(m_flVialX, scale, wide), calcPos(m_flVialY, scale, tall));
	m_pHealthVial->SetColor(Color(255,255,255,255 * (dbg ? 1.0 : m_flVialAlpha)));
	m_pHealthVial->Paint();

	if(m_iWep >= 0 && m_iWep < 5)
	{
		m_pWep[m_iWep]->SetSize(scale * 2*m_flWepScale, scale * m_flWepScale);
		m_pWep[m_iWep]->SetPos(scale * m_flWepX + (m_flWepX > 0 ? 0 : scale * wide), scale * m_flWepY + (m_flWepY > 0 ? 0 : scale * tall));
		m_pWep[m_iWep]->SetPos(calcPos(m_flWepX, scale, wide), calcPos(m_flWepY, scale, tall));
		m_pWep[m_iWep]->SetColor(Color(255,255,255,255 * (dbg ? 1.0 : m_flWepAlpha)));
		m_pWep[m_iWep]->Paint();
	}

	if(m_iAmmo >= 0 && m_iAmmo < 4)
	{
		m_pAmmo[m_iAmmo]->SetSize(scale * m_flAmmoScale, scale * m_flAmmoScale);
		m_pAmmo[m_iAmmo]->SetPos(scale * (m_flAmmoX + (m_flAmmoX > 0 ? 0 : scale * wide)), scale * (m_flAmmoY + (m_flAmmoY > 0 ? 0 : scale * tall)));
		m_pAmmo[m_iAmmo]->SetPos(calcPos(m_flAmmoX, scale, wide), calcPos(m_flAmmoY, scale, tall));
		m_pAmmo[m_iAmmo]->SetColor(Color(255,255,255,255 * (dbg ? 1.0 : m_flAmmoAlpha)));
		m_pAmmo[m_iAmmo]->Paint();

		m_pNumbers->SetSize(scale * m_flAmmoQuantityScale, scale * m_flAmmoQuantityScale);
		m_pNumbers->SetPos(calcPos(m_flAmmoQuantityX, scale, wide), calcPos(m_flAmmoQuantityY, scale, tall));
		m_pNumbers->SetColor(Color(255,255,255,255 * (dbg ? 1.0 : m_flAmmoAlpha)));
		m_pNumbers->SetFrame(m_iAmmoQuantity);
		m_pNumbers->Paint();
	}
}

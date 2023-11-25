//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=============================================================================//

#include "cbase.h"
#include "hud.h"
#include "hudelement.h"
#include "hud_macros.h"
#include "iclientmode.h"
#include <vgui_controls/AnimationController.h>
#include <vgui/ILocalize.h>
#include <vgui/ISurface.h>

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

class CNH2Ammo : public CHudElement, public vgui::Panel
{
	DECLARE_CLASS_SIMPLE(CNH2Ammo, vgui::Panel);

public:
	CNH2Ammo( const char *pElementName );
	void Init( void );
	void Reset();

	virtual void Paint( void );
	virtual void PaintBackground() {};

protected:
	virtual void OnThink();
	void UpdatePlayerAmmo();
	
private:
	CHandle< C_BaseCombatWeapon > m_hCurrentActiveWeapon;
	int m_iClip;
	int m_iTotal;
	int m_iSlot;
	vgui::IImage* m_pBackground;
	vgui::IImage* m_pNumbers;
	vgui::IImage* m_pAmmoIcons[4];
	CPanelAnimationVar(float, m_flClipSize, "ClipSize", "1.0");
	CPanelAnimationVar(float, m_flClipInset, "ClipInset", "0.2");
	CPanelAnimationVar(float, m_flTotalSize, "TotalSize", "0.7");
	CPanelAnimationVar(float, m_flTotalInset, "TotalInset", "0.375");
	CPanelAnimationVar(float, m_flIconSize, "IconSize", "1.0");
	CPanelAnimationVar(float, m_flIconInset, "IconInset", "0.05");
};

DECLARE_HUDELEMENT( CNH2Ammo );

//-----------------------------------------------------------------------------
// Purpose: Constructor
//-----------------------------------------------------------------------------
CNH2Ammo::CNH2Ammo( const char *pElementName ) :
	CHudElement( pElementName ), BaseClass(NULL, "NH2Ammo")
{
	vgui::Panel* pParent = g_pClientMode->GetViewport();
	SetParent(pParent);
	m_pBackground = vgui::scheme()->GetImage("hud/bar_bg", false);
	m_pNumbers = vgui::scheme()->GetImage("hud/numbers", false);
	m_pAmmoIcons[0] = vgui::scheme()->GetImage("hud/slot2_ammo_icon", false);
	m_pAmmoIcons[1] = vgui::scheme()->GetImage("hud/slot3_ammo_icon", false);
	m_pAmmoIcons[2] = vgui::scheme()->GetImage("hud/slot4_ammo_icon", false);
	m_pAmmoIcons[3] = vgui::scheme()->GetImage("hud/slot5_ammo_icon", false);
	SetHiddenBits( HIDEHUD_HEALTH | HIDEHUD_PLAYERDEAD | HIDEHUD_NEEDSUIT | HIDEHUD_WEAPONSELECTION );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CNH2Ammo::Init( void )
{
	m_iClip = 0;
	m_iTotal = 0;
	m_iSlot = 0;
}

void CNH2Ammo::Reset()
{
	m_hCurrentActiveWeapon = NULL;
	m_iClip = 0;
	m_iTotal = 0;
	m_iSlot = 0;
	UpdatePlayerAmmo();
	SetFgColor(Color(255,255,255,255));
	SetBgColor(Color(0,0,0,0));
}

//-----------------------------------------------------------------------------
// Purpose: called every frame to get ammo info from the weapon
//-----------------------------------------------------------------------------
void CNH2Ammo::UpdatePlayerAmmo()
{
	C_BasePlayer *player = C_BasePlayer::GetLocalPlayer();
	C_BaseCombatWeapon *wpn = GetActiveWeapon();

	if ( !wpn || !player || !wpn->UsesPrimaryAmmo() )
	{
		SetPaintEnabled(false);
		return;
	}

	SetPaintEnabled(true);

	m_iClip = wpn->Clip1();
	m_iTotal = player->GetAmmoCount(wpn->GetPrimaryAmmoType());
	m_iSlot = wpn->GetSlot();
}

void CNH2Ammo::OnThink()
{
	UpdatePlayerAmmo();
}

void CNH2Ammo::Paint( void )
{
	int panelWidth, panelHeight;
	GetSize(panelWidth, panelHeight);

	m_pBackground->SetSize(panelWidth, panelHeight);
	m_pBackground->SetPos(0,0);
	m_pBackground->Paint();

	//Draw clip ammo count
	m_pNumbers->SetColor(Color(255,255,255,255));
	m_pNumbers->SetSize(panelHeight * m_flClipSize, panelHeight * m_flClipSize);
	m_pNumbers->SetPos(panelWidth * m_flClipInset, 0.5 * panelHeight * (1.0 - m_flClipSize));
	m_pNumbers->SetFrame(static_cast<int>(m_iClip));
	m_pNumbers->Paint();

	//Draw total ammo count
	m_pNumbers->SetSize(panelHeight * m_flTotalSize, panelHeight * m_flTotalSize);
	m_pNumbers->SetPos(panelWidth * m_flTotalInset, 0.5 * panelHeight * (1.0 - m_flTotalSize));
	m_pNumbers->SetFrame(static_cast<int>(m_iTotal));
	m_pNumbers->Paint();

	//Figure out the slot icon to use
	if(m_iSlot >= 1 && m_iSlot <= 4)
	{
		int index = m_iSlot - 1;
		m_pAmmoIcons[index]->SetSize(panelHeight * m_flIconSize, panelHeight * m_flIconSize);
		m_pAmmoIcons[index]->SetPos(panelWidth * m_flIconInset, 0.5 * panelHeight * (1.0 - m_flIconSize));
		m_pAmmoIcons[index]->SetColor(Color(255,255,255,255));
		m_pAmmoIcons[index]->Paint();
	}
}

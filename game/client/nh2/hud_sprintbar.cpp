#include "cbase.h"
#include "hud.h"
#include "hud_macros.h"
#include "c_baseplayer.h"
#include "iclientmode.h"

#include "hudelement.h"
#include "hud_numericdisplay.h"
#include "vgui_controls/AnimationController.h"
#include "vgui/IScheme.h"
#include "vgui/ISurface.h"
#include "vgui/ILocalize.h"
#include "vgui/IImage.h"
#include "c_basehlplayer.h"

#include "tier0/memdbgon.h"

class CHudSprintBar : public CHudElement, public vgui::Panel
{
	DECLARE_CLASS_SIMPLE(CHudSprintBar, vgui::Panel);

public:
	CHudSprintBar(const char* pElementName);

	virtual void Init (void);
	virtual void Reset (void);
	virtual void OnThink (void);

protected:
	virtual void Paint();
	virtual void PaintBackground() {};

	float m_flPower;
	vgui::IImage* m_pBar;
	vgui::IImage* m_pBackground;
	vgui::IImage* m_pIcon;
	CPanelAnimationVar(float, m_flIconSize, "IconSize", "1.0");
	CPanelAnimationVar(float, m_flIconInset, "IconInset", "-0.008");
	CPanelAnimationVar(float, m_flBarWidth, "BarWidth", "0.7");
	CPanelAnimationVar(float, m_flBarHeight, "BarHeight", "0.775");
	CPanelAnimationVar(float, m_flBarInsetX, "BarInsetX", "0.15");
	CPanelAnimationVar(float, m_flBarInsetY, "BarInsetY", "0.125");
};

DECLARE_HUDELEMENT (CHudSprintBar);

//------------------------------------------------------------------------
// Purpose: Constructor
//------------------------------------------------------------------------
CHudSprintBar::CHudSprintBar (const char * pElementName) :
	CHudElement(pElementName), BaseClass(NULL, "NH2SprintBar")
{
	vgui::Panel* pParent = g_pClientMode->GetViewport();
	SetParent(pParent);
	m_pBar = vgui::scheme()->GetImage("hud/bar_fg", false);
	m_pBackground = vgui::scheme()->GetImage("hud/bar_bg", false);
	m_pIcon = vgui::scheme()->GetImage("hud/sprint_icon", false);
	SetHiddenBits (HIDEHUD_HEALTH | HIDEHUD_PLAYERDEAD | HIDEHUD_NEEDSUIT);
}

void CHudSprintBar::Init()
{
	Reset();
}

void CHudSprintBar::Reset (void)
{
	SetFgColor(Color(255,255,255,255));
	SetBgColor(Color(0,0,0,0));
	SetAlpha(0);
	m_flPower = 100;
}

void CHudSprintBar::OnThink()
{
	C_BaseHLPlayer *pPlayer = (C_BaseHLPlayer *)C_BasePlayer::GetLocalPlayer();
	if ( !pPlayer )
		return;

	const float power = pPlayer->m_HL2Local.m_flSuitPower;

	if (m_flPower != power)
	{
		if (power >= 100 && m_flPower < 100)
			g_pClientMode->GetViewportAnimationController()->RunAnimationCommand( this, "alpha", 0.0f, 0.0f, 0.4f, vgui::AnimationController::INTERPOLATOR_LINEAR);
		else if(power < 100 && m_flPower >= 100)
			g_pClientMode->GetViewportAnimationController()->RunAnimationCommand( this, "alpha", 255.0f, 0.0f, 0.4f, vgui::AnimationController::INTERPOLATOR_LINEAR);
		m_flPower = power;
	}
}

void CHudSprintBar::Paint()
{
	int panelWidth, panelHeight;
	GetSize(panelWidth, panelHeight);

	m_pBackground->SetSize(panelWidth, panelHeight);
	m_pBackground->SetPos(0,0);
	m_pBackground->Paint();

	m_pBar->SetSize(panelWidth*m_flBarWidth, panelHeight*m_flBarHeight);
	m_pBar->SetPos(panelWidth*m_flBarInsetX, panelHeight*m_flBarInsetY);
	m_pBar->SetFrame(min(max(0.f,m_flPower),99.f));
	m_pBar->Paint();

	//Draw sprint icon
	m_pIcon->SetSize(panelHeight * m_flIconSize, panelHeight * m_flIconSize);
	m_pIcon->SetPos(panelWidth * m_flIconInset, 0.5 * panelHeight * (1.0 - m_flIconSize));
	m_pIcon->Paint();
}

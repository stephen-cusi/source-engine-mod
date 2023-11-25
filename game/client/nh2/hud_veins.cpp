 
#include "cbase.h"
#include "hud.h"
#include "hudelement.h"
#include "hud_macros.h"
#include "iclientmode.h"
#include "c_basehlplayer.h"
#include "vgui_controls/Panel.h"
#include "vgui_controls/AnimationController.h"
#include "vgui/ISurface.h"
#include <vgui/ILocalize.h>
#include "ienginevgui.h"
 
using namespace vgui;
 
#include "tier0/memdbgon.h" 

static int veins_offuntil;
void cb_veinsoverlay( IConVar *pConVar, const char *pOldString, float flOldValue )
{
	veins_offuntil = gpGlobals->curtime + 10.0;
}

ConVar r_bluroverlay("r_bluroverlay", "1");
ConVar r_veinsoverlay("r_veinsoverlay", "1", FCVAR_NONE, "Veins overlay", false, 0, true, 1, cb_veinsoverlay);
extern ConVar r_flashlightforceflicker;

class CHudVeins : public CHudElement, public vgui::Panel
{
	DECLARE_CLASS_SIMPLE(CHudVeins, vgui::Panel);
 
public:
	CHudVeins(const char * pElementName);
 
	virtual void Init();
	virtual void Reset();
	virtual void Think();
	virtual void Paint();

private:
	vgui::IImage* m_pVeins;
	vgui::IImage* m_pBlur;
	float m_flLastHeartbeat;
	CPanelAnimationVar(float, m_flMinHeartbeatInterval, "MinHeartbeatInterval", "0.5");
	CPanelAnimationVar(float, m_flMaxHeartbeatInterval, "MaxHeartbeatInterval", "1.0");
};
 
DECLARE_HUDELEMENT_DEPTH( CHudVeins, 100 );
 
CHudVeins:: CHudVeins (const char * pElementName) : 
	CHudElement (pElementName), BaseClass (NULL, "NH2Veins")
{
	vgui::Panel * pParent = g_pClientMode->GetViewport();
	SetParent (pParent);
	m_pVeins = vgui::scheme()->GetImage("hud/veins", false);
	m_pBlur = vgui::scheme()->GetImage("hud/blureffect", false);
}

void CHudVeins::Init()
{
	Reset();
}
 
void CHudVeins::Reset (void)
{
	SetFgColor(Color(255,255,255,255));
	SetBgColor(Color(255,255,255,0));
	r_veinsoverlay.SetValue(true);
	r_bluroverlay.SetValue(true);
	r_flashlightforceflicker.SetValue(0);
	m_flLastHeartbeat = 0;
}

void CHudVeins::Think()
{
	if ( gpGlobals->curtime > veins_offuntil )
		r_veinsoverlay.SetValue( 1 );

	C_BaseHLPlayer *pPlayer = (C_BaseHLPlayer *)C_BasePlayer::GetLocalPlayer();
	if ( !pPlayer )
		return;

	if(pPlayer->GetHealth() > 25 || pPlayer->GetHealth() <= 0)
		return;

	if(!r_veinsoverlay.GetBool())
	  return;

	const float health = pPlayer->GetHealth() / 25.0;
	const float interval = m_flMinHeartbeatInterval + health * (m_flMaxHeartbeatInterval - m_flMinHeartbeatInterval);
	if(m_flLastHeartbeat + interval < gpGlobals->curtime)
	{
		vgui::surface()->PlaySound("nh2/heartbeat.wav");
		m_flLastHeartbeat = gpGlobals->curtime;
	}
}
 
void CHudVeins::Paint()
{
	int wide, tall;
	GetHudSize(wide, tall);
	SetBounds(0, 0, wide, tall);

	m_pBlur->SetSize(wide, tall);
	m_pBlur->SetPos(0,0);
	m_pBlur->SetColor(Color(255,255,255,255));
	if(r_bluroverlay.GetBool())
		m_pBlur->Paint();

	C_BaseHLPlayer *pPlayer = (C_BaseHLPlayer *)C_BasePlayer::GetLocalPlayer();
	if ( !pPlayer )
		return;

	if(pPlayer->GetHealth() > 25)
		return;

	m_pVeins->SetSize(wide, tall);
	m_pVeins->SetPos(0,0);
	m_pVeins->SetColor(Color(255,255,255,255 - pPlayer->GetHealth() * 10));
	if(r_veinsoverlay.GetBool())
		m_pVeins->Paint();
}

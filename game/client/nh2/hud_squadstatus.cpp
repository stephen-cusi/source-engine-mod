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
#include "c_basehlplayer.h"
#include "vgui_controls/Panel.h"
#include "vgui_controls/AnimationController.h"
#include "vgui/ISurface.h"
#include <vgui/ILocalize.h>

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

ConVar player_squad_transient_commands( "player_squad_transient_commands", "1", FCVAR_REPLICATED );

//-----------------------------------------------------------------------------
// Purpose: Shows the sprint power bar
//-----------------------------------------------------------------------------
class CNH2SquadStatus : public CHudElement, public vgui::Panel
{
	DECLARE_CLASS_SIMPLE( CNH2SquadStatus, vgui::Panel );

public:
	CNH2SquadStatus( const char *pElementName );
	virtual void Init( void );
	virtual void Reset( void );
	virtual void OnThink( void );
	bool ShouldDraw();

protected:
	virtual void Paint();
	virtual void PaintBackground() {};

private:
	CPanelAnimationVarAliasType( float, m_flIconInsetX, "IconInsetX", "8", "proportional_float" );
	CPanelAnimationVarAliasType( float, m_flIconInsetY, "IconInsetY", "8", "proportional_float" );
	CPanelAnimationVarAliasType( float, m_flIconGap, "IconGap", "20", "proportional_float" );

	int m_iSquadMembers;
	vgui::IImage* m_pBackground;
	vgui::IImage* m_pIcon;
};


DECLARE_HUDELEMENT( CNH2SquadStatus );

//-----------------------------------------------------------------------------
// Purpose: Constructor
//-----------------------------------------------------------------------------
CNH2SquadStatus::CNH2SquadStatus( const char *pElementName ) : CHudElement( pElementName ), BaseClass( NULL, "NH2SquadStatus" )
{
	vgui::Panel *pParent = g_pClientMode->GetViewport();
	SetParent( pParent );

	m_pBackground = vgui::scheme()->GetImage("hud/bar_bg", false);
	m_pIcon = vgui::scheme()->GetImage("hud/swat", false);

	SetHiddenBits( HIDEHUD_HEALTH | HIDEHUD_PLAYERDEAD | HIDEHUD_NEEDSUIT );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CNH2SquadStatus::Init( void )
{
	m_iSquadMembers = 0;
	SetFgColor(Color(255,255,255,255));
	SetBgColor(Color(0,0,0,0));
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CNH2SquadStatus::Reset( void )
{
	Init();
}

//-----------------------------------------------------------------------------
// Purpose: Save CPU cycles by letting the HUD system early cull
// costly traversal.  Called per frame, return true if thinking and 
// painting need to occur.
//-----------------------------------------------------------------------------
bool CNH2SquadStatus::ShouldDraw( void )
{
	C_BaseHLPlayer *pPlayer = (C_BaseHLPlayer *)C_BasePlayer::GetLocalPlayer();
	if ( !pPlayer )
		return false;

	return pPlayer->m_HL2Local.m_iSquadMemberCount > 0;
}

//-----------------------------------------------------------------------------
// Purpose: updates hud icons
//-----------------------------------------------------------------------------
void CNH2SquadStatus::OnThink( void )
{
	C_BaseHLPlayer *pPlayer = (C_BaseHLPlayer *)C_BasePlayer::GetLocalPlayer();
	if ( !pPlayer )
		return;

	m_iSquadMembers = pPlayer->m_HL2Local.m_iSquadMemberCount;
}

//-----------------------------------------------------------------------------
// Purpose: draws the power bar
//-----------------------------------------------------------------------------
void CNH2SquadStatus::Paint()
{
	C_BaseHLPlayer *pPlayer = (C_BaseHLPlayer *)C_BasePlayer::GetLocalPlayer();
	if ( !pPlayer )
		return;

	int panelWidth, panelHeight;
	GetSize(panelWidth, panelHeight);

	m_pBackground->SetSize(panelWidth, panelHeight);
	m_pBackground->SetPos(0,0);
	m_pBackground->Paint();

	m_pIcon->SetSize(panelHeight, panelHeight);
	int xpos = m_flIconInsetX, ypos = m_flIconInsetY;
	for (int i = 0; i < m_iSquadMembers; i++)
	{
		m_pIcon->SetPos(xpos, ypos);
		m_pIcon->Paint();
		xpos += m_flIconGap;
	}
}

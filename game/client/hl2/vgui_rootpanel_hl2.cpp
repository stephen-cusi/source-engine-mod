//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//
#include "cbase.h"
#include "vgui_int.h"
#include "ienginevgui.h"
#ifdef LUA_SDK
#include "vgui/IVGui.h"
#include "vgui_rootpanel_hl2.h"
#include "clientmode_shared.h"
#endif

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#ifdef LUA_SDK
C_ScriptedBaseGameUIPanel *g_pScriptedBaseGameUIPanel = NULL;


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void VGUI_CreateGameUIRootPanel( void )
{
	// Idempotent: VGui_GetGameUIPanel() also creates this on demand, so a second
	// call must not leak the first panel (or orphan the tick signal it registered).
	if ( g_pScriptedBaseGameUIPanel != NULL )
		return;

	g_pScriptedBaseGameUIPanel = new C_ScriptedBaseGameUIPanel( enginevgui->GetPanel( PANEL_GAMEUIDLL ) );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void VGUI_DestroyGameUIRootPanel( void )
{
	delete g_pScriptedBaseGameUIPanel;
	g_pScriptedBaseGameUIPanel = NULL;
}
#endif

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void VGUI_CreateClientDLLRootPanel( void )
{
	// Just using PANEL_ROOT in HL2 right now
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void VGUI_DestroyClientDLLRootPanel( void )
{
}

//-----------------------------------------------------------------------------
// Purpose: Game specific root panel
// Output : vgui::Panel
//-----------------------------------------------------------------------------
vgui::VPANEL VGui_GetClientDLLRootPanel( void )
{
	vgui::VPANEL root = enginevgui->GetPanel( PANEL_CLIENTDLL );
	return root;
}

#ifdef LUA_SDK
vgui::Panel *VGui_GetGameUIPanel( void )
{
	// HL2SB: created on demand.
	//
	// Nothing in this fork ever called VGUI_CreateGameUIRootPanel(), so this
	// pointer stayed NULL forever and the function handed back an invalid panel.
	// That is not the same as returning nil: lua_pushpanel() wraps NULL in a
	// PHandle userdata, so luaL_optpanel()'s default never applied and any script
	// that did
	//
	//     vgui.CContentDialog( VGui_GetGameUIPanel(), "ContentDialog" )
	//
	// died before constructing anything:
	//
	//     vgui.lua:37: bad argument #1 to '?' (Panel expected, got INVALID_PANEL)
	//
	// That is the "click Content and it errors" report.  Building it here rather
	// than at DLL init also avoids depending on the engine's GameUI panel already
	// existing that early; the parent may legitimately be 0, and the wrapper is
	// still a real vgui Panel either way.
	if ( g_pScriptedBaseGameUIPanel == NULL )
	{
		VGUI_CreateGameUIRootPanel();
	}

	return g_pScriptedBaseGameUIPanel;
}

//-----------------------------------------------------------------------------
// Purpose: Game specific root panel
// Output : vgui::Panel
//-----------------------------------------------------------------------------
vgui::Panel *VGui_GetClientLuaRootPanel( void )
{
	ClientModeShared *mode = ( ClientModeShared * )GetClientModeNormal();
	vgui::Panel *pRoot = mode->m_pClientLuaPanel;
	return pRoot;
}
#endif

//-----------------------------------------------------------------------------
// C_ScriptedBaseGameUIPanel implementation.
//-----------------------------------------------------------------------------
C_ScriptedBaseGameUIPanel::C_ScriptedBaseGameUIPanel( vgui::VPANEL parent )
	: BaseClass( NULL, "ScriptedBaseGameUIPanel" )
{
	SetParent( parent );
	SetPaintEnabled( false );
	SetPaintBorderEnabled( false );
	SetPaintBackgroundEnabled( false );

	// This panel does post child painting
	SetPostChildPaintEnabled( true );

	// Make it screen sized
	SetBounds( 0, 0, ScreenWidth(), ScreenHeight() );

	// Ask for OnTick messages
	vgui::ivgui()->AddTickSignal( GetVPanel() );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
C_ScriptedBaseGameUIPanel::~C_ScriptedBaseGameUIPanel( void )
{
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void C_ScriptedBaseGameUIPanel::PostChildPaint()
{
	BaseClass::PostChildPaint();

	// Draw all panel effects
	RenderPanelEffects();
}

//-----------------------------------------------------------------------------
// Purpose: For each panel effect, check if it wants to draw and draw it on
//  this panel/surface if so
//-----------------------------------------------------------------------------
void C_ScriptedBaseGameUIPanel::RenderPanelEffects( void )
{
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void C_ScriptedBaseGameUIPanel::OnTick( void )
{
}

//-----------------------------------------------------------------------------
// Purpose: Reset effects on level load/shutdown
//-----------------------------------------------------------------------------
void C_ScriptedBaseGameUIPanel::LevelInit( void )
{
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void C_ScriptedBaseGameUIPanel::LevelShutdown( void )
{
}


//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=============================================================================//

#include "cbase.h"
#include "hud.h"
#include "hudelement.h"
#include "hud_macros.h"
#include "hud_numericdisplay.h"
#include "iclientmode.h"
#include "c_basehlplayer.h"
#include "VGuiMatSurface/IMatSystemSurface.h"
#include "materialsystem/imaterial.h"
#include "materialsystem/imesh.h"
#include "materialsystem/imaterialvar.h"
#include "../hud_crosshair.h"

#include <vgui/IScheme.h>
#include <vgui/ISurface.h>
#include <KeyValues.h>
#include <vgui_controls/AnimationController.h>
#include "vgui_video.h"

#include "cbase.h"
#include <vgui/IVGui.h>
#include "vgui/IInput.h"
#include <vgui/ISurface.h>
#include "ienginevgui.h"
#include "iclientmode.h"
#include "vgui_video.h"
#include "engine/IEngineSound.h"

#include "vgui_video.h"
#include "engine/IEngineSound.h"
// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

//-----------------------------------------------------------------------------
// Purpose: Draws the zoom screen
//-----------------------------------------------------------------------------
class CHudVideo: public VideoPanel, public CHudElement
{
	DECLARE_CLASS_SIMPLE( CHudVideo, vgui::Panel );

public:
	CHudVideo( const char *pElementName );
	~CHudVideo(){};

	void OnClose( void )
	{
		//enginesound->NotifyEndMoviePlayback();
		if ( vgui::input()->GetAppModalSurface() == GetVPanel() )
		{
			vgui::input()->ReleaseAppModalSurface();
		}
		SetVisible( false );
		vgui::surface()->RestrictPaintToSinglePanel( NULL );

		if ( g_pVideo != NULL && m_VideoMaterial != NULL )
		{
			g_pVideo->DestroyVideoMaterial( m_VideoMaterial );
			m_VideoMaterial = NULL;
		}
	}

	void FireGameEvent(IGameEvent* pEvent)
	{
		// check event type and print message
		if (!strcmp("nh2_playvideo", pEvent->GetName()))
		{
			BeginPlayback( pEvent->GetString("filename") );
			MoveToFront();
		}
		else
			CHudElement::FireGameEvent(pEvent);
	}

	 bool ShouldDraw( void )
	 {
		 return true;
	 }
};

//DECLARE_HUDELEMENT( CHudVideo );

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------

DECLARE_HUDELEMENT_DEPTH( CHudVideo, 100 );

using namespace vgui;

//-----------------------------------------------------------------------------
// Purpose: Constructor
//-----------------------------------------------------------------------------
CHudVideo::CHudVideo( const char *pElementName ) : CHudElement(pElementName), VideoPanel(0,0,512,512,false)
{
	//vgui::Panel *pParent = g_pClientMode->GetViewport();
	//We want this to show, even if hud is disabled entirely! Parnet to CLIENTDLL panel.
	//Instead of the viewport, which gets disabled by cl_drawhud 0
	vgui::VPANEL Parent = enginevgui->GetPanel(PANEL_CLIENTDLL);
	SetParent( Parent );
	SetParentedToClientDLLRootPanel(true);
	
	SetKeyBoardInputEnabled( true );
	SetMouseInputEnabled( false );
	SetCursorAlwaysVisible( false );

	SetProportional( false );
	SetVisible( true );
	SetPaintBackgroundEnabled( false );
	SetPaintBorderEnabled( false );

	// Set us up
	SetTall( ScreenHeight() );
	SetWide( ScreenWidth() );
	SetPos( 0, 0 );

	gameeventmanager->AddListener(this, "nh2_playvideo", false);
}

CON_COMMAND( nh2_playvideo, "Plays a video: <filename> [width height]" )
{
	if ( args.ArgC() < 2 )
		return;

	//unsigned int nScreenWidth = Q_atoi( args[2] );
	//unsigned int nScreenHeight = Q_atoi( args[3] );
	
	char strFullpath[MAX_PATH];
	Q_strncpy( strFullpath, "media/", MAX_PATH );	// Assume we must play out of the media directory
	char strFilename[MAX_PATH];
	Q_StripExtension( args[1], strFilename, MAX_PATH );
	Q_strncat( strFullpath, args[1], MAX_PATH );
	Q_strncat( strFullpath, ".bik", MAX_PATH );		// Assume we're a .bik extension type
	
	IGameEvent* pEvent = gameeventmanager->CreateEvent("nh2_playvideo");
	// The event object will be NULL if there aren't any clients connected to the server.
	// Always perform a NULL check before setting properties and sending the event.
	if (pEvent)
	{
		pEvent->SetString("filename", strFullpath);
		gameeventmanager->FireEventClientSide(pEvent);
	}
	return;
}
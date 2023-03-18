//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: DebugSystemUi спасибо за детсво!!!!!!!
//
// $NoKeywords: $
//===========================================================================//
#include "cbase.h"
#include "sm_menu.h"
#include <vgui/ISurface.h>
#include <vgui_controls/Label.h>
#include <vgui_controls/Controls.h>
#include <vgui_controls/MenuButton.h>
#include <vgui_controls/Menu.h>
#include <vgui_controls/MenuItem.h>
#include <vgui_controls/ImageList.h>
#include <vgui/IScheme.h>
#include <vgui/IVGui.h>
#include <vgui_controls/Frame.h>
#include <vgui_controls/PropertyPage.h>
#include <vgui_controls/PropertyDialog.h>
#include <vgui_controls/PropertySheet.h>
#include "vgui_imagebutton.h"
#include "filesystem.h"

#include "tier0/memdbgon.h"

using namespace vgui;
//-----------------------------------------------------------------------------
// Purpose: A menu button that knows how to parse cvar/command menu data from gamedir/addons/menu/spawnmenu.ctx
//-----------------------------------------------------------------------------
class CSMButton : public MenuButton
{
	typedef MenuButton BaseClass;

public:
	// Construction
	CSMButton( Panel *parent, const char *panelName, const char *text );

private:
	// Menu associated with this button
	Menu	*m_pMenu;
};

class CSMCommandButton : public vgui::Button
{
typedef vgui::Button BaseClass;
public:
	CSMCommandButton( vgui::Panel *parent, const char *panelName, const char *labelText, const char *command )
		: BaseClass( parent, panelName, labelText )
	{
		AddActionSignalTarget( this );
		SetCommand( command );
	}

	virtual void OnCommand( const char *command )
	{
		engine->ClientCmd((char *)command);
	}

	virtual void OnTick( void )
	{
	}
};

class CSMPage : public vgui::PropertyPage
{
	typedef vgui::PropertyPage BaseClass;
public:
	CSMPage ( vgui::Panel *parent, const char *panelName )
		: BaseClass( parent, panelName )
	{
		vgui::ivgui()->AddTickSignal( GetVPanel(), 250 );
	}

	virtual void OnTick( void )
	{
		BaseClass::OnTick();

		if ( !IsVisible() )
			return;

		int c = m_LayoutItems.Count();
		for ( int i = 0; i < c; i++ )
		{
			vgui::Panel *p = m_LayoutItems[ i ];
			p->OnTick();
		}
	}

	virtual void OnCommand( const char *command )
	{
		engine->ClientCmd( (char *)command );
	}


	virtual void PerformLayout()
	{
		BaseClass::PerformLayout();	
		int x = 5;
		int y = 5;
		int w = 127;
		int h = 128;
		int gap = 2;

		int c = m_LayoutItems.Count();
		int tall = GetTall();

		for ( int i = 0; i < c; i++ )
		{
			vgui::Panel *p = m_LayoutItems[ i ];
			p->SetBounds( x, y, w, h );

			y += ( h + gap );
			if ( y >= tall - h )
			{
				x += ( w + gap );
				y = 5;
			}	
		}	
	}

	void Init( KeyValues *kv )
	{
		for (KeyValues *control = kv->GetFirstSubKey(); control != NULL; control = control->GetNextKey())
		{
			int iType = control->GetInt("imagebutton", 0);
			
			if ( iType == 1 )
			{
				const char *a, *b, *c, *t;
				//command
				t = control->GetString( "command", "" );
				a = control->GetString( "normal", "" );
				b = control->GetString( "overimage", "");
				c = control->GetString( "mouseclick", "");
				
				if ( t, a, b, c && t[0], a[0], b[0], c[0] )
				{
					ImageButton *img = new ImageButton( this, "ImageButton", a, b, c, t );
					m_LayoutItems.AddToTail( img );
					continue;
				}
			}
			else
			{
				const char *m;

				//command
				m = control->GetString("command", "");
				if ( m && m[0] )
				{
					CSMCommandButton *btn = new CSMCommandButton( this, "CommandButton", control->GetName(), m );
					m_LayoutItems.AddToTail( btn );
					continue;
				}
			}
		}
	}
private:
	CUtlVector< vgui::Panel * >		m_LayoutItems; 
};

ConVar sm_menu("sm_menu", "0", FCVAR_CLIENTDLL, "Spawn Menu");

class CSMenu : public vgui::PropertyDialog
{
	typedef vgui::PropertyDialog BaseClass;
public:

	CSMenu( vgui::VPANEL *parent, const char *panelName )
		: BaseClass( NULL, "SMenu" )
	{
		SetTitle( "SMenu", true );

		KeyValues *kv = new KeyValues( "SMenu" );
		if ( kv )
		{
			if ( kv->LoadFromFile(g_pFullFileSystem, "addons/menu/spawnmenu.txt") )
			{
				for (KeyValues *dat = kv->GetFirstSubKey(); dat != NULL; dat = dat->GetNextKey())
				{
					if ( !Q_strcasecmp( dat->GetName(), "width" ) )
					{
						SetWide( dat->GetInt() );
						continue;
					}
					else if ( !Q_strcasecmp( dat->GetName(), "height" ) )
					{
						SetTall( dat->GetInt() );
						continue;
					}

					CSMPage *page = new CSMPage( this, dat->GetName() );
					page->Init( dat );
	
					AddPage( page, dat->GetName() );
				}
			}
			kv->deleteThis();
		}

		vgui::ivgui()->AddTickSignal(GetVPanel(), 100);
	
		GetPropertySheet()->SetTabWidth(72);
		SetMoveable( true );
		SetVisible( true );
		SetSizeable( true );
		SetProportional(false);
	}

	void	Init( KeyValues *kv );

	void 	OnTick()
	{
		BaseClass::OnTick();
		SetVisible(sm_menu.GetBool());
	}

	void 	OnCommand( const char *command )
	{
		BaseClass::OnCommand( command );
		
		if (!Q_stricmp(command, "Close"))	
		{
			sm_menu.SetValue(0);
		}
	}

};

void CSMenu::Init( KeyValues *kv )
{
}

class CSMPanelInterface : public SMPanel
{
private:
	CSMenu *SMPanel;
public:
	CSMPanelInterface()
	{
		SMPanel = NULL;
	}
	void Create(vgui::VPANEL parent)
	{
		SMPanel = new CSMenu(&parent, "SMenu");
	}
	void Destroy()
	{
		if (SMPanel)
		{
			SMPanel->SetParent((vgui::Panel *)NULL);
			delete SMPanel;
		}
	}
	void Activate(void)
	{
		if (SMPanel)
		{
			SMPanel->Activate();
		}
	}
};
static CSMPanelInterface g_SMPanel;
SMPanel* smenu = (SMPanel*)&g_SMPanel;

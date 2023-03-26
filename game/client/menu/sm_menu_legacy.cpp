//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: DebugSystemUi спасибо за детсво!!!!!!!
//
// $NoKeywords: $
//===========================================================================//
#include "cbase.h"
#include "sm_menu_legacy.h"
#include <vgui/ISurface.h>
#include <vgui_controls/Label.h>
#include <vgui_controls/Controls.h>
#include <vgui_controls/MenuButton.h>
#include <vgui_controls/Menu.h>
#include <vgui_controls/MenuItem.h>
#include <vgui/IScheme.h>
#include <vgui/IVGui.h>
#include <vgui_controls/Frame.h>
#include <vgui_controls/PropertyPage.h>
#include <vgui_controls/PropertySheet.h>
#include <vgui_controls/PropertyDialog.h>
#include "vgui_imagebutton.h"
#include "filesystem.h"

#include "tier0/memdbgon.h"

using namespace vgui;

ConVar sm_legacy_wide("sm_legacy_wide", "0");
ConVar sm_legacy_height("sm_legacy_height", "0");

class CSMLButton : public MenuButton
{
	typedef MenuButton BaseClass;
public:
	CSMLButton( Panel *parent, const char *panelName, const char *text );

private:
	Menu	*m_pMenu;
};

class CSMLCommandButton : public vgui::Button
{
typedef vgui::Button BaseClass;
public:
	CSMLCommandButton( vgui::Panel *parent, const char *panelName, const char *labelText, const char *command )
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

class CSMLPage : public vgui::PropertyPage
{
	typedef vgui::PropertyPage BaseClass;
public:
	CSMLPage ( vgui::Panel *parent, const char *panelName )
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

	virtual void PerformLayout()
	{
		BaseClass::PerformLayout();	
		int x = 5;
		int y = 5;
		int w = sm_legacy_wide.GetInt();
		int h = sm_legacy_height.GetInt();
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
					CSMLCommandButton *btn = new CSMLCommandButton( this, "CommandButton", control->GetName(), m );
					m_LayoutItems.AddToTail( btn );
					continue;
				}
			}
		}
	}
private:
	CUtlVector< vgui::Panel * >		m_LayoutItems; 
};

ConVar sm_legacy_menu("sm_legacy_menu", "0", FCVAR_CLIENTDLL, "Spawn Menu");

class CSMLMenu : public vgui::PropertyDialog
{
	typedef vgui::PropertyDialog BaseClass;
public:

	CSMLMenu( vgui::VPANEL *parent, const char *panelName )
		: BaseClass( NULL, "SMLenu" )
	{
		SetTitle( "SMLenu", true );

		KeyValues *kv = new KeyValues( "SMLenu" );
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

					CSMLPage *page = new CSMLPage( this, dat->GetName() );
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

	void Init( KeyValues *kv );

	void OnTick()
	{
		BaseClass::OnTick();
		SetVisible(sm_legacy_menu.GetBool());
	}

	void OnCommand( const char *command )
	{
		BaseClass::OnCommand( command );
		
		if (!Q_stricmp(command, "Close"))	
		{
			sm_legacy_menu.SetValue(0);
		}
	}

};

void CSMLMenu::Init( KeyValues *kv )
{
}

class CSMLPanelInterface : public SMLPanel
{
private:
	CSMLMenu *SMLPanel;
public:
	CSMLPanelInterface()
	{
		SMLPanel = NULL;
	}
	void Create(vgui::VPANEL parent)
	{
		SMLPanel = new CSMLMenu(&parent, "SMenu");
	}
	void Destroy()
	{
		if (SMLPanel)
		{
			SMLPanel->SetParent((vgui::Panel *)NULL);
			delete SMLPanel;
		}
	}
	void Activate(void)
	{
		if (SMLPanel)
		{
			SMLPanel->Activate();
		}
	}
};
static CSMLPanelInterface g_SMLPanel;
SMLPanel* smlmenu = (SMLPanel*)&g_SMLPanel;
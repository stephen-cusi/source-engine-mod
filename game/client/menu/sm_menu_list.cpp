//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: DebugSystemUi спасибо за детсво!!!!!!!
//
// $NoKeywords: $
//===========================================================================//
#include "cbase.h"
#include "sm_menu_list.h"
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

ConVar sm_wide("sm_wide", "0");
ConVar sm_height("sm_height", "0");
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
		int w = sm_wide.GetInt();
		int h = sm_height.GetInt();
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

	void InitNPCs( KeyValues *kv )
	{
		for ( KeyValues *control = kv->GetFirstSubKey(); control != NULL; control = control->GetNextKey() )
		{
			const char *entname;

			if ( !Q_strcasecmp( control->GetName(), "entity" ) )
			{
				entname = control->GetString();
			}

			if( Q_strncmp( entname, "npc_", Q_strlen("npc_") ) == 0 )
			{
				if ( entname && entname[0] )
				{
					char entspawn[256];
					Q_snprintf( entspawn, 256, "ent_create %s", entname );

					char normalImage[256];					
					Q_snprintf( normalImage, 256, "pic/%s.vmt", entname );

					ImageButton *btn = new ImageButton( this, "ImageButton", normalImage, normalImage, normalImage, entspawn );
					m_LayoutItems.AddToTail( btn );
					continue;
				}
			}
		}		
	}

	void InitWeapons( KeyValues *kv )
	{
		for ( KeyValues *control = kv->GetFirstSubKey(); control != NULL; control = control->GetNextKey() )
		{
			const char *weaponName;

			if ( !Q_strcasecmp( control->GetName(), "entity" ) )
			{
				weaponName = control->GetString();
			}

			if( Q_strncmp( weaponName, "weapon_", Q_strlen("weapon_") ) == 0 )
			{
				if ( weaponName && weaponName[0] )
				{
					 char weaponspawn[1024];
					Q_snprintf( weaponspawn, 1024, "ent_create %s", weaponName );

					char normalImage[1024];					
					Q_snprintf( normalImage, 1024, "pic/%s.vmt", weaponName );

					ImageButton *btn = new ImageButton( this, "ImageButton", normalImage, normalImage, normalImage, weaponspawn );
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

		SetWide( 800 );
		SetTall( 640 );

		KeyValues *kv = new KeyValues( "SMenu" );
		if ( kv )
		{
			if ( kv->LoadFromFile(g_pFullFileSystem, "addons/menu/entitylist.txt") )
			{
				CSMPage *npces = new CSMPage( this, "NPCs" );
				CSMPage *weapons = new CSMPage( this, "Weapons" );
		
				npces->InitNPCs( kv );
				weapons->InitWeapons( kv );
				AddPage( npces, "NPCs" );
				AddPage( weapons, "Weapons");
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

	void Init();

	void OnTick()
	{
		BaseClass::OnTick();
		SetVisible(sm_menu.GetBool());
	}

	void OnCommand( const char *command )
	{
		BaseClass::OnCommand( command );
		
		if (!Q_stricmp(command, "Close"))	
		{
			sm_menu.SetValue(0);
		}
	}

};

void CSMenu::Init()
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

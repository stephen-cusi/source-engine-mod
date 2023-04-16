//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: DebugSystemUi спасибо за детсво!!!!!!!
//
// $NoKeywords: $
//===========================================================================//
#include "cbase.h"
#include <stdio.h>
#include "sm_menu_list.h"
#include <vgui/ISurface.h>
#include <vgui_controls/Label.h>
#include <vgui_controls/Controls.h>
#include <vgui_controls/MenuButton.h>
#include <vgui_controls/Menu.h>
#include <vgui_controls/MenuItem.h>
#include <vgui_controls/ImageList.h>
#include <vgui_controls/PanelListPanel.h>
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

const char *ignore[] =
{
	"characters",
	"hostage",
	"player",
	"humans",
};

class CSMList : public vgui::PanelListPanel
{
public:
	typedef vgui::PanelListPanel BaseClass;
	
	CSMList( vgui::Panel *parent, const char *pName ) : BaseClass( parent, pName )
	{
		SetBounds( 0, 0, 800, 640 );
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

		int w = 64;
		int h = 64;
		int x = 5;
		int y = 5;
		int gap = 2;

		int c = m_LayoutItems.Count();
		int wide = GetWide();

		for ( int i = 0; i < c; i++ )
		{
			vgui::Panel *p = m_LayoutItems[ i ];
			p->SetBounds( x, y, w, h );

			x += ( w + gap );
			if ( x >= wide - w )
			{
				y += ( h + gap );
				x = 5;
			}	
		}	
	}

	virtual void AddImageButton( CSMList *panel, const char *image, const char *command )
	{
		ImageButton *btn = new ImageButton( panel, image, image, NULL, NULL, command );
		m_LayoutItems.AddToTail( btn );
		panel->AddItem( NULL, btn );
	}
	
	virtual void InitEntities( KeyValues *kv, CSMList *panel, const char *enttype )
	{
		for ( KeyValues *control = kv->GetFirstSubKey(); control != NULL; control = control->GetNextKey() )
		{
			const char *entname; 

			if ( !Q_strcasecmp( control->GetName(), "entity" ) )
			{
				entname = control->GetString();
			}

			if( Q_strncmp( entname, enttype, Q_strlen(enttype) ) == 0 )
			{
				if ( entname && entname[0] )
				{
					char entspawn[MAX_PATH], normalImage[MAX_PATH], vtf[MAX_PATH], vtf_without_ex[MAX_PATH], vmt[MAX_PATH];
					
					Q_snprintf( entspawn, sizeof(entspawn), "ent_create %s", entname );
					Q_snprintf( normalImage, sizeof(normalImage), "smenu/%s", entname );
					Q_snprintf( vtf, sizeof( vtf ), "materials/vgui/smenu/%s.vtf", entname );
					Q_snprintf( vtf_without_ex, sizeof(vtf_without_ex), "vgui/smenu/%s", entname );
					Q_snprintf( vmt, sizeof( vmt ), "hl2sb/materials/vgui/smenu/%s.vmt", entname );

					if ( filesystem->FileExists( vtf ) && filesystem->FileExists( vmt ) )
					{
						AddImageButton( panel, normalImage, entspawn );
						continue;
					}
				}
			}
		}		
	}

	virtual void InitModels( CSMList *panel, const char *modeltype, const char *modelfolder, const char *mdlPath )
	{
		FileFindHandle_t fh;
		for ( const char *pModel = filesystem->FindFirst( mdlPath, &fh ); pModel && *pModel; pModel = filesystem->FindNext( fh ) )
		{
			char file[MAX_PATH];
			Q_FileBase( pModel, file, sizeof( file ) );
			
			if ( pModel && pModel[0] )
			{
				char normalImage[MAX_PATH], vtf[MAX_PATH], modelfile[MAX_PATH], entspawn[MAX_PATH], normalMaterial[MAX_PATH];
				Q_snprintf( modelfile, sizeof(modelfile), "%s/%s", modelfolder, file );
				Q_snprintf( normalImage, sizeof(normalImage), "smenu/models/%s", modelfile );
				Q_snprintf( vtf,  sizeof(vtf),  "materials/vgui/%s.vtf", normalImage );
				Q_snprintf( entspawn, sizeof(entspawn), "%s_create %s", modeltype, modelfile );
				Q_snprintf( normalMaterial, sizeof(normalMaterial), "materials/vgui/%s.vmt", normalImage );
					
				if ( filesystem->FileExists( normalMaterial ) && filesystem->FileExists( vtf ) )
				{
					AddImageButton( panel, normalImage, entspawn );
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
				CSMList *npces = new CSMList( this, "EntityPanel");
				npces->InitEntities( kv, npces, "npc_" );
				npces->InitEntities( kv, npces, "monster_"); // hl1 npces
				CSMList *weapons = new CSMList( this, "EntityPanel");
				weapons->InitEntities( kv, weapons, "weapon_" );
				weapons->InitEntities( kv, weapons, "item_");
				weapons->InitEntities( kv, weapons, "ammo_");

				AddPage( npces, "NPCs" );
				AddPage( weapons, "Weapons");
			}
			kv->deleteThis();
		}

		CSMList *models = new CSMList( this, "ModelPanel");

		FileFindHandle_t fh;
		for ( const char *pDir = filesystem->FindFirst( "models/*", &fh ); pDir && *pDir; pDir = filesystem->FindNext( fh ) )
		{			
			if ( filesystem->FindIsDirectory( fh ) )
			{
				char dir[MAX_PATH];
				char file[MAX_PATH];
				Q_FileBase( pDir, file, sizeof( file ) );
				Q_snprintf( dir, sizeof( dir ), "models/%s/*.mdl", file ); 

				if ( !FStrEq( file, ignore[0]) && !FStrEq(file, ignore[1]) && !FStrEq(file, ignore[2]) && !FStrEq(file, ignore[2]) && !FStrEq( file, ignore[3]) ) {
					models->InitModels( models, "prop_physics", file, dir );
				}
				else
					models->InitModels( models, "prop_ragdoll", file, dir );
			}
		}
		
		AddPage( models, "Props");
		
		vgui::ivgui()->AddTickSignal(GetVPanel(), 100);
	
		GetPropertySheet()->SetTabWidth(72);
		SetMoveable( true );
		SetVisible( true );
		SetSizeable( true );
		SetProportional(true);
	}

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

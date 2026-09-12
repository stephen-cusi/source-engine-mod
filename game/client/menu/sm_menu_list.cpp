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
#include "game_controls/basemodel_panel.h"

#include "tier0/memdbgon.h"

using namespace vgui;

//-----------------------------------------------------------------------------
// HL2SB: is this class Lua content (a GMod SWEP or a scripted entity)?
//
// Such content never ships its own VTF/VMT icon - which is why it is the only
// thing allowed to fall back to the generic SWEP icon.  Doing that for every
// entry turned the hundreds of deliberately icon-less stock entity entries into
// sheets of paper.
//-----------------------------------------------------------------------------
static bool SMenu_IsScriptedClass( const char *pszClass )
{
	static const char *s_pFolders[] = { "lua/weapons", "lua/entities" };
	static const char *s_pFiles[] = { "shared.lua", "init.lua", "cl_init.lua" };

	if ( !pszClass || !pszClass[0] )
		return false;

	for ( int i = 0; i < ARRAYSIZE( s_pFolders ); ++i )
	{
		for ( int j = 0; j < ARRAYSIZE( s_pFiles ); ++j )
		{
			char szPath[MAX_PATH];
			Q_snprintf( szPath, sizeof( szPath ), "%s/%s/%s", s_pFolders[i], pszClass, s_pFiles[j] );

			if ( filesystem->FileExists( szPath ) )
				return true;
		}
	}

	// HL2SB: GMod's scripted ENTITIES are a single FILE, not a folder:
	// lua/entities/<class>.lua (sent_ball is one).  SWEPs use the folder form tested
	// above.  Without this test a stock GMod entity was not recognised as scripted
	// content, so it was not allowed the generic icon fallback and was dropped from
	// the list altogether - which is exactly why the Entities page came up empty even
	// though addons/menu/entitylist.txt listed it and materials/entities/<class>.png
	// was installed.
	char szEntityScript[MAX_PATH];
	Q_snprintf( szEntityScript, sizeof( szEntityScript ), "lua/entities/%s.lua", pszClass );

	if ( filesystem->FileExists( szEntityScript ) )
		return true;

	return false;
}

//-----------------------------------------------------------------------------
// HL2SB: is this class a ported GMod SWEP (a lua/weapons/<class> script)?
//
// The Weapons page below used to be prefix based, so a SWEP whose classname has
// no weapon_/item_/ammo_ prefix was skipped entirely - the pristine admin gun is
// "pist_weagon", which matched nothing and therefore never appeared.
//-----------------------------------------------------------------------------
static bool SMenu_IsScriptedWeapon( const char *pszClass )
{
	static const char *s_pFiles[] = { "shared.lua", "init.lua", "cl_init.lua" };

	if ( !pszClass || !pszClass[0] )
		return false;

	for ( int j = 0; j < ARRAYSIZE( s_pFiles ); ++j )
	{
		char szPath[MAX_PATH];
		Q_snprintf( szPath, sizeof( szPath ), "lua/weapons/%s/%s", pszClass, s_pFiles[j] );

		if ( filesystem->FileExists( szPath ) )
			return true;
	}

	return false;
}

// HL2SB: what clicking a Weapons entry does.
//   0 (DEFAULT) - ent_create <weapon>: place the weapon ENTITY in the world,
//                 where you are looking ("point and place").  This is what the
//                 weapons page has always done, and what a spawn menu should do.
//   1           - give it straight to the player instead, the way GMod's
//                 spawnmenu does (handy for the stock weapon_* / item_* / ammo_*
//                 entries, less so for ported Lua SWEPs).
// (Declared before CSMList because AddEntityButton() below reads it.)
ConVar sm_menu_give( "sm_menu_give", "0", FCVAR_CLIENTDLL, "SMenu weapons page: 0 (default) places the weapon entity where you look, 1 gives it to the player" );

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
		int wide = GetWide();

		for ( int i = 0; i < m_LayoutItems.Count(); i++ )
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

	virtual void AddModelPanel( CSMList *panel, const char *mdlname )
	{
		CBaseModelPanel *mdl = new CBaseModelPanel( panel, "MDLPanel" );
		mdl->SetMDL( mdlname );
		m_LayoutItems.AddToTail( mdl );
		panel->AddItem( NULL, mdl );
	}
	
	// HL2SB: how one entitylist entry behaves and which icon it uses.
	virtual void AddEntityButton( CSMList *panel, const char *entname )
	{
		char entspawn[MAX_PATH], normalImage[MAX_PATH], vtf[MAX_PATH], vtf_without_ex[MAX_PATH], vmt[MAX_PATH], png[MAX_PATH];

		// HL2SB: the weapons page places the weapon ENTITY where you look
		// (ent_create, the default), the way this menu has always done it;
		// sm_menu_give 1 hands it to the player instead, like GMod's spawnmenu.
		// Anything that is not a weapon/item/ammo is always created in the world,
		// and a ported GMod SWEP counts as a weapon whatever its class name.
		const bool bIsWeapon = !Q_strnicmp( entname, "weapon_", 7 ) ||
							   !Q_strnicmp( entname, "item_", 5 ) ||
							   !Q_strnicmp( entname, "ammo_", 5 ) ||
							   SMenu_IsScriptedWeapon( entname );

		const bool bGive = sm_menu_give.GetBool() && bIsWeapon;

		Q_snprintf( entspawn, sizeof(entspawn), "%s %s", bGive ? "give" : "ent_create", entname );
		Q_snprintf( normalImage, sizeof(normalImage), "smenu/%s", entname );
		Q_snprintf( vtf, sizeof( vtf ), "materials/vgui/smenu/%s.vtf", entname );
		Q_snprintf( vtf_without_ex, sizeof(vtf_without_ex), "vgui/smenu/%s", entname );
		Q_snprintf( vmt, sizeof( vmt ), "materials/vgui/smenu/%s.vmt", entname );
		// HL2SB: GMod content (and everything ported from it) ships PNGs, not
		// VTFs - accept either one next to the .vmt.
		Q_snprintf( png, sizeof( png ), "materials/vgui/smenu/%s.png", entname );

		if ( filesystem->FileExists( vmt ) &&
			 ( filesystem->FileExists( vtf ) || filesystem->FileExists( png ) ) )
		{
			AddImageButton( panel, normalImage, entspawn );
		}
		// No icon of its own: only Lua classes get the generic SWEP icon
		// (they never have one).  Stock entries without an icon stay hidden,
		// exactly as they were before.
		else if ( SMenu_IsScriptedClass( entname ) &&
				  filesystem->FileExists( "materials/vgui/smenu/weapon_default.vmt" ) &&
				  filesystem->FileExists( "materials/vgui/smenu/weapon_default.png" ) )
		{
			AddImageButton( panel, "smenu/weapon_default", entspawn );
		}
	}

	virtual void InitEntities( KeyValues *kv, CSMList *panel, const char *enttype )
	{
		for ( KeyValues *control = kv->GetFirstSubKey(); control != NULL; control = control->GetNextKey() )
		{
			const char *entname = NULL;

			if ( !Q_strcasecmp( control->GetName(), "entity" ) )
			{
				entname = control->GetString();
			}

			// HL2SB: entname used to be left uninitialised for every key that is
			// not an "entity" key, so the Q_strncmp() below compared garbage.
			if ( !entname || !entname[0] )
				continue;

			if ( Q_strncmp( entname, enttype, Q_strlen( enttype ) ) != 0 )
				continue;

			AddEntityButton( panel, entname );
		}
	}

	// HL2SB: the prefix passes above cannot see a ported GMod SWEP whose
	// classname has no weapon_/item_/ammo_/gmod_ prefix (the pristine admin gun
	// is "pist_weagon"), so find those by their lua/weapons/<class> script.
	virtual void InitScriptedWeapons( KeyValues *kv, CSMList *panel )
	{
		for ( KeyValues *control = kv->GetFirstSubKey(); control != NULL; control = control->GetNextKey() )
		{
			const char *entname = NULL;

			if ( !Q_strcasecmp( control->GetName(), "entity" ) )
			{
				entname = control->GetString();
			}

			if ( !entname || !entname[0] )
				continue;

			// Already listed by one of the prefix passes.
			if ( !Q_strnicmp( entname, "weapon_", 7 ) || !Q_strnicmp( entname, "item_", 5 ) ||
				 !Q_strnicmp( entname, "ammo_", 5 ) || !Q_strnicmp( entname, "gmod_", 5 ) )
				continue;

			if ( SMenu_IsScriptedWeapon( entname ) )
				AddEntityButton( panel, entname );
		}
	}

	virtual void InitModels( CSMList *panel, const char *modeltype, const char *modelfolder, const char *mdlPath )
	{
		FileFindHandle_t fh;
		char const *pModel = g_pFullFileSystem->FindFirst( mdlPath, &fh );
		while ( pModel )
		{
			if ( pModel[0] != '.' )
			{
				char ext[ 10 ];
				Q_ExtractFileExtension( pModel, ext, sizeof( ext ) );

				if ( !Q_stricmp( ext, "mdl" ) )
				{
					char file[MAX_PATH];
					Q_FileBase( pModel, file, sizeof( file ) );
				
					if ( pModel && pModel[0] )
					{
						char normalImage[MAX_PATH], vtf[MAX_PATH], modelfile[MAX_PATH], entspawn[MAX_PATH], vmt[MAX_PATH], file1[MAX_PATH], vtf_without_ex[MAX_PATH];
						Q_snprintf( modelfile, sizeof(modelfile), "%s/%s", modelfolder, file );
						Q_snprintf( normalImage, sizeof(normalImage), "smenu/models/%s", modelfile );
						Q_snprintf( vtf,  sizeof(vtf),  "materials/vgui/%s.vtf", normalImage );
						Q_snprintf( entspawn, sizeof(entspawn), "%s_create %s", modeltype, modelfile );
						Q_snprintf( vmt, sizeof(vmt), "materials/vgui/%s.vmt", normalImage );
						Q_snprintf( vtf_without_ex, sizeof( vtf_without_ex ), "vgui/%s", normalImage );
						
						if ( filesystem->FileExists( vtf ) && filesystem->FileExists( vmt ) )
						{
							AddImageButton( panel, normalImage, entspawn );
						}
					}
				}
			}
			pModel = g_pFullFileSystem->FindNext( fh );
		}
		g_pFullFileSystem->FindClose( fh );
	}
private:
	CUtlVector< vgui::Panel * >		m_LayoutItems;
};

ConVar sm_menu("sm_menu", "0", FCVAR_CLIENTDLL, "Spawn Menu");

// GMod-style hold-to-open / release-to-close spawn menu.
// Binding Q to "+smenu" opens it while held; the engine fires "-smenu"
// on release, which closes it again.
static void SMenuDown( const CCommand &args )
{
	sm_menu.SetValue( 1 );
}
static ConCommand smenu_down_cmd( "+smenu", SMenuDown, "Open SMenu (hold)" );

static void SMenuUp( const CCommand &args )
{
	sm_menu.SetValue( 0 );
}
static ConCommand smenu_up_cmd( "-smenu", SMenuUp, "Close SMenu (release)" );

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
				// HL2SB: SWEP class names that are not weapon_* - the ported
				// GMod camera (gmod_camera) is one.
				weapons->InitEntities( kv, weapons, "gmod_" );
				// HL2SB: ... and SWEPs with no recognisable prefix at all, found
				// by their lua/weapons/<class> script (pist_weagon).
				weapons->InitScriptedWeapons( kv, weapons );

				// HL2SB: a third page for scripted entities, and note what this does
				// NOT do: it does not scan the generic "entity" key.  entitylist.txt
				// carries ~600 engine class names (ai_*, env_*, xen_*, ...) that are
				// useless in a spawn menu and would bury the handful that matter.
				// "sent_" is the prefix GMod's scripted entities use (sent_ball is
				// one), so this lists exactly those and nothing else.
				CSMList *entities = new CSMList( this, "EntityPanel");
				entities->InitEntities( kv, entities, "sent_" );

				AddPage( npces, "NPCs" );
				AddPage( weapons, "Weapons");
				AddPage( entities, "Entities" );
			}
			kv->deleteThis();
		}

		CSMList *models = new CSMList( this, "ModelPanel");

		FileFindHandle_t fh;
		for ( const char *pDir = filesystem->FindFirstEx( "models/*", "GAME", &fh ); pDir && *pDir; pDir = filesystem->FindNext( fh ) )
		{			
			if ( Q_strncmp( pDir, "props_", Q_strlen("props_") ) == 0 ) {
				if ( filesystem->FindIsDirectory( fh ) )
				{
					char dir[MAX_PATH];
					char file[MAX_PATH];
					Q_FileBase( pDir, file, sizeof( file ) );
					Q_snprintf( dir, sizeof( dir ), "models/%s/*.mdl", file );
					printf("%s\n", pDir );
					list.AddToTail( pDir );
					models->InitModels( models, "prop_physics", file, dir );
				}
			}
		}
		AddPage( models, "Props");

/*		for ( const char *pD = filesystem->FindFirstEx( "models/*", "MOD", &fh ); pD && *pD; pD = filesystem->FindNext( fh ) )
		{			
			if ( Q_strncmp( pD, "props_", Q_strlen("props_") ) == 0 ) {
				
				if ( filesystem->FindIsDirectory( fh ) )
				{	
					for ( int index = 0; index < list.Count(); index++ )
					{
						const char *i = list [ index ];

						if ( !FStrEq( pD, i ) ) {
							char dir[MAX_PATH];
							char file[MAX_PATH];
							Q_FileBase( pD, file, sizeof( file ) );
							Q_snprintf( dir, sizeof( dir ), "models/%s/*.mdl", file );
							printf("ALSO: %s\n", pD );
							models2->InitModels( models2, "prop_physics", file, dir );
						}
					}
				}
			}
		}
		AddPage( models2, "Props_MOD");
*/		
		vgui::ivgui()->AddTickSignal(GetVPanel(), 100);
	
		GetPropertySheet()->SetTabWidth(72);
		SetMoveable( true );
		SetVisible( true );
		SetSizeable( true );

		// GMod-style: the spawn menu must NOT capture keyboard input, otherwise
		// vgui treats the popup as the key focus and swallows WASD before the
		// engine can fire +forward/+back/+moveleft/+moveright, so the player
		// cannot walk while the menu is open. Leave mouse input enabled so the
		// tabs / items still respond to clicks.
		SetKeyBoardInputEnabled( false );
		SetMouseInputEnabled( true );
	}

	~CSMenu()
	{
		list.RemoveAll();
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
private: 
	CUtlVector<const char* > list;
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

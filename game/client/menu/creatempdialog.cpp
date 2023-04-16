#include "cbase.h"
#include "creatempdialog.h"
#include <vgui_controls/PropertyPage.h>
#include <vgui_controls/PropertyDialog.h>
#include <vgui_controls/PropertySheet.h>
#include <vgui_controls/PanelListPanel.h>
#include <vgui_controls/Label.h>
#include <vgui/IScheme.h>
#include <vgui/IVGui.h>
#include "vgui_imagebutton.h"

#include <stdio.h>
#include "filesystem.h"

using namespace vgui;

ConVar selmap("selmap", "");
ConVar mpdialog("mpdialog", "");

void MakeVMT( const char *vmt, const char *vtf_without_ex )
{
	FILE *fp;
	fp = fopen( vmt, "w+" );
	fprintf( fp, "UnlitGeneric\n");
	fprintf( fp, "{\n" );
	fprintf( fp, "		$basetexture %s\n", vtf_without_ex );
	fprintf( fp, "}");
	fflush( fp );
}

MapListPanel::MapListPanel( vgui::Panel *parent, const char *pName ) : BaseClass( parent, pName )
{
	SetBounds( 0, 0, 800, 640 );
}

void MapListPanel::OnTick( void )
{
	BaseClass::OnTick();

	if ( !IsVisible() )
		return;

	int c = layoutItems.Count();
	for ( int i = 0; i < c; i++ )
	{
		vgui::Panel *p = layoutItems[ i ];
		p->OnTick();
	}
}

//Just perform like a SMenu
void MapListPanel::PerformLayout()
{
	BaseClass::PerformLayout();

	int w = 127;
	int h = 127;
	int x = 5;
	int y = 5;
	int gap = 2;

	int c = layoutItems.Count();
	int wide = GetWide();

	for ( int i = 0; i < c; i++ )
	{
		vgui::Panel *p = layoutItems[ i ];
		p->SetBounds( x, y, w, h );

		x += ( w + gap );
		if ( x >= wide - w )
		{
			y += ( h + gap );
			x = 5;
		}	
	}	
}

void MapListPanel::AddButton( MapListPanel *panel, const char *image, const char *command )
{
	ImageButton *img = new ImageButton( panel, image, image, NULL, NULL, command );
	layoutItems.AddToTail( img );
	panel->AddItem( NULL, img );
}

void MapListPanel::LoadMaps( MapListPanel *panel )
{
	FileFindHandle_t findhandle;
	for ( const char *pMap = filesystem->FindFirst( "maps/*.bsp", &findhandle ); pMap && *pMap; pMap = filesystem->FindNext( findhandle ) )
	{
		char file[64];
		Q_FileBase( pMap, file, sizeof( file ) );
		
		if ( pMap && pMap[0] )
		{
			char vtf[260];
			Q_snprintf( vtf, sizeof( vtf ), "thumb/%s.vtf", file );
			char vtf_without_ex[260];
			Q_snprintf( vtf_without_ex, sizeof( vtf_without_ex ), "thumb/%s", file );
			char vmt[260];
			Q_snprintf( vmt, sizeof( vmt ), "vgui/thumb/%s.vmt", file );
			char imageCommand[260];
			Q_snprintf( imageCommand, sizeof( imageCommand ), "selmap %s", pMap );
			char fl[260];
			Q_snprintf( fl, sizeof(fl), "materials/vgui/thumb/%s.vtf", file );

			if ( filesystem->FileExists( fl ) ) {
				AddButton( panel, vtf_without_ex, imageCommand );
				continue;
			}
		}	
	}
}

void MapListPanel::OnCommand( const char *command )
{
	engine->ClientCmd( command );
}


MapList::MapList( vgui::VPANEL *parent, const char *pName ) : BaseClass( NULL, "MapList" )
{
	SetSize( 800, 640 );
	SetTitle("New Game", true);
	
	MapListPanel *maplist = new MapListPanel( this, NULL );
	MapListPanel *info = new MapListPanel( this, NULL );
	maplist->LoadMaps( maplist );
	AddPage( maplist, "Maps" );
	AddPage( info, "Server Settings");

	vgui::ivgui()->AddTickSignal(GetVPanel(), 100);
	
	GetPropertySheet()->SetTabWidth(72);
	SetMoveable( true );
	SetVisible( true );
	SetSizeable( true );
	SetProportional( true );
}

void MapList::OnTick()
{
	BaseClass::OnTick();
	
	SetVisible(mpdialog.GetBool());
}

class MapListInterface : public CMapList
{
private:
	MapList *CMapList;
public:
	MapListInterface()
	{
		CMapList = NULL;
	}
	void Create(vgui::VPANEL parent)
	{
		CMapList = new MapList( &parent, NULL );
	}
	void Destroy()
	{
		if (CMapList)
		{
			CMapList->SetParent((vgui::Panel *)NULL);
			delete CMapList;
		}
	}
	void Activate(void)
	{
		if (CMapList)
		{
			CMapList->Activate();
		}
	}
};
static MapListInterface g_MapList;
CMapList* maplist = (CMapList*)&g_MapList;
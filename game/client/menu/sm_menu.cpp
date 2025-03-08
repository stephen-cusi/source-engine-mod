/////////////////////////////////////////////////////////////////
// Shitcoded by ItzVladik
/////////////////////////////////////////////////////////////////
#include "cbase.h"
#include <stdio.h>
#include "vprof.h"
#include <vgui/IVGui.h>
#include <vgui/IScheme.h>
#include <vgui/ISurface.h>

#include "filesystem.h"
#include "sm_menu.h"

#include "viewrender.h"
#include "materialsystem/imaterialvar.h"

#include "tier0/memdbgon.h"

using namespace vgui;

ConVar sm_menu_mode("sm_menu_mode", "0");
ConVar sm_menu_theme("sm_menu_theme", "0");

void ReloadScheme(vgui::Panel *panel)
{
	vgui::Panel *parent;
	parent = panel->GetParent()->GetParent();
	const char * theme;
	if (sm_menu_theme.GetBool())
		theme = "resource/SourceScheme_Dark.res";
	else
		theme = "resource/SourceScheme.res";
	parent->SetScheme(vgui::scheme()->LoadSchemeFromFile(theme, "SourceScheme"));
	parent->InvalidateLayout( true, true );
}

void CheckVar( const char *cvarName )
{
	ConVarRef convar( cvarName );
	if ( convar.GetBool() )
		convar.SetValue( 0 );
	else
		convar.SetValue( 1 );
}


extern void CAM_ToThirdPerson( void );
extern void CAM_ToFirstPerson( void );

SMMovement::SMMovement( vgui::Panel *parent, const char *panelName ) : BaseClass( parent, panelName )
{
 	vgui::ivgui()->AddTickSignal( GetVPanel(), 250 );
	m_AFH = new CheckButton( this, "AFH", "Enable AFH");
	m_AutoJump = new CheckButton( this, "m_AutoJump", "Enable AutoJump" );
	m_OldEngine = new CheckButton( this, "m_OldEngine", "Enable Old Engine Bunny Hopping");
	m_MainScale = new TextEntry( this, "MainScale");
	m_MainScale->SetText("0");
	m_ForwardScale = new TextEntry( this, "ForwardScale" );
	m_ForwardScale->SetText( "0.5" );
	m_SprintForwardScale = new TextEntry( this, "SprintForwardScale" );
	m_SprintForwardScale->SetText( "0.1" );
	m_NoclipSpeed = new TextEntry(this, "m_NoclipSpeed");
	m_NoclipSpeed->SetText( "5" );
	m_NormSpeed = new TextEntry(this, "m_NormSpeed");
	m_NormSpeed->SetText( "190" );
	m_SprintSpeed = new TextEntry(this, "m_SprintSpeed");
	m_SprintSpeed->SetText( "320" );
	m_WalkSpeed = new TextEntry(this, "m_WalkSpeed");
	m_WalkSpeed->SetText( "150" );
	LoadControlSettings("resource/ui/smmovement.res");
}

void SMMovement::OnCommand( const char *cmd )
{
	BaseClass::OnCommand( cmd );
}

void SMMovement::OnTick( void )
{
	BaseClass::OnTick();
	
	if ( !IsVisible() )
		return;	
	
	static ConVar  *mov_2004 = cvar->FindVar("mov_2004");
	//static ConVar  *mov_only_ducking = cvar->FindVar("mov_only_ducking");
	static ConVar  *mov_afh = cvar->FindVar("mov_afh");
	static ConVar  *mov_jumpforwardscale = cvar->FindVar("mov_jumpforwardscale");
	static ConVar  *mov_jumpforwardsprintscale = cvar->FindVar("mov_jumpforwardsprintscale");
	static ConVar  *mov_scale = cvar->FindVar("mov_scale");
	static ConVar  *mov_autojump = cvar->FindVar("mov_autojump");

	char buf[64], buf1[64], buf2[64], buf3[64];
	
	m_NoclipSpeed->GetText( buf, 64 );
	ConVarRef sv_noclipspeed("sv_noclipspeed");
	sv_noclipspeed.SetValue( buf );
	
	m_NormSpeed->GetText( buf1, 64 );
	ConVarRef hl2_normspeed("hl2_normspeed");
	hl2_normspeed.SetValue( buf1 );
	
	m_SprintSpeed->GetText( buf2, 64 );
	ConVarRef hl2_sprintspeed("hl2_sprintspeed");
	hl2_sprintspeed.SetValue( buf2 );
	
	m_WalkSpeed->GetText( buf3, 64 );
	ConVarRef hl2_walkspeed("hl2_walkspeed");
	hl2_walkspeed.SetValue( buf3 );


	mov_2004->SetValue( m_OldEngine->IsSelected() );
	mov_afh->SetValue( m_AFH->IsSelected() );
	mov_autojump->SetValue( m_AutoJump->IsSelected() );
	//mov_only_ducking->SetValue( m_Duck->IsSelected() );
	char scale[64];
	char scale1[64];
	char scale2[64];
	m_MainScale->GetText( scale, 64 );
	m_ForwardScale->GetText( scale1, 64 );
	m_SprintForwardScale->GetText( scale2, 64 );
	mov_scale->SetValue( scale );
	mov_jumpforwardscale->SetValue( scale1 );
	mov_jumpforwardsprintscale->SetValue(scale2);

	if ( !m_OldEngine->IsSelected() )
	{
		m_ForwardScale->SetEditable( false );
		m_SprintForwardScale->SetEditable( false );
	}
	else
	{
		m_ForwardScale->SetEditable( true );
		m_SprintForwardScale->SetEditable( true );
	}
}


SMVisual::SMVisual( vgui::Panel *parent, const char *panelName ) : BaseClass( parent, panelName )
{
	vgui::ivgui()->AddTickSignal( GetVPanel(), 250 );
	m_Yuv = new CheckButton( this, "Black and White", "Black and White" );
	m_Hsv = new CheckButton( this, "Black and White v2", "Black and White v2" );
	m_CombineBinocle = new CheckButton( this, "Combine Binocle", "Combine Binocle" );
	m_Eyefx = new CheckButton( this, "EyeFX", "EyeFX" );
	m_Refract = new CheckButton( this, "Refract", "Refract" );
	LoadControlSettings("resource/ui/smvisual.res");
}

void SMVisual::ScreenOverlay( const char *materialname )
{
	char mat[MAX_PATH];
	Q_snprintf(mat, sizeof(mat), "%s", materialname );
	IMaterial *pMat = view->GetScreenOverlayMaterial();	
	IMaterial *pMaterial = materials->FindMaterial( mat, TEXTURE_GROUP_OTHER, false );
	
	if ( !IsErrorMaterial( pMaterial ) )
	{
		view->SetScreenOverlayMaterial( pMaterial );
	}
	else
	{
		view->SetScreenOverlayMaterial( NULL );
	}
}

void SMVisual::OnCommand( const char *cmd )
{
	BaseClass::OnCommand(cmd);
}

void SMVisual::Check()
{
	if ( m_Yuv->IsSelected() )
		ScreenOverlay( "debug/yuv" );
	else if ( m_Hsv->IsSelected() )
		ScreenOverlay( "debug/hsv" );
	else if( m_CombineBinocle->IsSelected() )
		ScreenOverlay( "effects/combine_binocoverlay");
	else if ( m_Eyefx->IsSelected() )
		ScreenOverlay( "effects/tp_eyefx/tp_eyefx");
	else if ( m_Refract->IsSelected() )
		ScreenOverlay( "effects/tp_refract");
	else
		ScreenOverlay( NULL );
}

void SMVisual::OnTick( void )
{
	if ( !IsVisible() )
		return;	
	
	Check();
	BaseClass::OnTick();
}

ImageButton::ImageButton( vgui::Panel *parent, const char *panelName, const char *normalImage, const char *mouseOverImage, const char *mouseClickImage, const char *pCmd )
 : ImagePanel( parent, panelName ) 
{
	m_pParent = parent;
	
	SetParent(parent);

	m_bScaleImage = true;

	if ( pCmd != NULL )
	{
		Q_strncpy( command, pCmd, 100 );
		hasCommand = true;
	}
	else
		hasCommand = false;

	Q_strncpy( m_normalImage, normalImage, 100 );

	i_normalImage = vgui::scheme()->GetImage( m_normalImage, true );

	if ( mouseOverImage != NULL )
	{
		Q_strcpy( m_mouseOverImage, mouseOverImage );
		i_mouseOverImage = vgui::scheme()->GetImage( m_mouseOverImage, true );
		hasMouseOverImage = true;
	}
	else
		hasMouseOverImage = false;

	if ( mouseClickImage != NULL )
	{
		Q_strcpy( m_mouseClickImage, mouseClickImage );
		i_mouseClickImage = vgui::scheme()->GetImage( m_mouseClickImage, true );
		hasMouseClickImage = true;
	}
	else
		hasMouseClickImage = false;

	SetNormalImage();
}

void ImageButton::OnCursorEntered()
{
	if ( hasMouseOverImage )
		SetMouseOverImage();
}

void ImageButton::OnCursorExited()
{
	if ( hasMouseOverImage )
		SetNormalImage();
}

void ImageButton::OnMouseReleased( vgui::MouseCode code )
{
	m_pParent->OnCommand( command );

	if ( ( code == MOUSE_LEFT ) && hasMouseClickImage )
		SetNormalImage();
}

void ImageButton::OnMousePressed( vgui::MouseCode code )
{
	if ( ( code == MOUSE_LEFT ) && hasMouseClickImage )
		SetMouseClickImage();
}

void ImageButton::SetNormalImage( void )
{
	SetImage(i_normalImage);
	Repaint();
}

void ImageButton::SetMouseOverImage( void )
{
	SetImage(i_mouseOverImage);
	Repaint();
}

void ImageButton::SetMouseClickImage( void )
{
	SetImage(i_mouseClickImage);
	Repaint();
}

void ImageButton::SetImage( vgui::IImage *image )
{
	BaseClass::SetImage( image );
}

ConVar sm_menu("sm_menu", "0", FCVAR_CLIENTDLL, "Spawn Menu");

CON_COMMAND( spawnmenu, "Open SMenu" )
{
	CheckVar("sm_menu");
}

SMModels::SMModels( vgui::Panel *parent, const char *panelName ) : BaseClass( parent, panelName )
{
	vgui::ivgui()->AddTickSignal( GetVPanel(), 250 );
	box = new ComboBox( this, "ComboBox", 50, false);
	mdl = new CMDLPanel( this, "MDLPanel" );
	LoadControlSettings( "resource/ui/smmodels.res" );
}

void SMModels::InitModels( Panel *panel, const char *modeltype, const char *modelfolder, const char *mdlPath )
{
	if ( !FStrEq(modelfolder, "props_c17") )
		return;

	FileFindHandle_t fh;
	char const *pModel = g_pFullFileSystem->FindFirstEx( mdlPath, "GAME", &fh );
	
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
					char modelname[MAX_PATH], 
						 entspawn[MAX_PATH], 
						 modelfile[MAX_PATH];

					Q_snprintf( modelname, sizeof(modelname), "%s/%s", modelfolder, file );
					Q_snprintf( entspawn, sizeof(entspawn), "%s_create %s", modeltype, modelname );
					Q_snprintf( modelfile, sizeof( modelfile ), "models/%s.mdl", modelname );
					
					box->AddItem( modelname, NULL );
				}
			}
		}
		pModel = g_pFullFileSystem->FindNext( fh );
	}
	g_pFullFileSystem->FindClose( fh );
}

const char *SMModels::GetText()
{
	box->GetText( sz_mdlname, 260 );
	return sz_mdlname;
}

void SMModels::OnCommand( const char *command )
{
	BaseClass::OnCommand( command );

	if(!Q_stricmp(command, "spawn"))
	{
		char spawncommand[260];

		Q_snprintf( spawncommand, sizeof( spawncommand ), "prop_physics_create %s", GetText() );
		engine->ClientCmd( spawncommand );
	}
	else if ( !Q_stricmp( command, "preview")  )
	{	
		char mdlname[260];
		Q_snprintf( mdlname, sizeof( mdlname ), "models/%s.mdl", GetText() );
		mdl->SetMDL( mdlname );
		mdl->Paint();
		mdl->LookAtMDL();
	}
}

// ToolMenu
SMToolMenu::SMToolMenu ( vgui::Panel *parent, const char *panelName ) : BaseClass( parent, panelName )
{
	vgui::ivgui()->AddTickSignal( GetVPanel(), 250 );

	pParent = parent;

	bCam = false;

	m_Red = new vgui::TextEntry(this, "Red");
	m_Green = new vgui::TextEntry(this, "Green");
	m_Blue = new vgui::TextEntry(this, "Blue");
	m_Modelscale = new vgui::TextEntry(this, "Duration");
	m_EntCreate = new vgui::TextEntry(this, "EntityCreate");

	m_Timescale = new CCvarSlider( this, "Slider", "Timescale", 0.1f, 10.0f, "host_timescale" );
	m_Timescale->SetSliderValue( 1.f );

	m_PhysTimescale = new CCvarSlider( this, "Slider2", "PhysTimescale", 0.1f, 10.0f, "phys_timescale" );
	m_PhysTimescale->SetSliderValue( 1.f );

	m_Gravity = new CCvarSlider( this, "Slider1", "Timescale", 0, 1000, "sv_gravity" );
	m_Gravity->SetSliderValue( 600.0f );

	m_GravityValue = new vgui::TextEntry(this, "GravityValue");
	char gbuf[64];
  	Q_snprintf(gbuf, sizeof( gbuf ), "600");
	m_GravityValue->SetText(gbuf);

	m_TimescaleValue = new vgui::TextEntry(this, "TimescaleValue");
	char buf[64];
  	Q_snprintf(buf, sizeof( buf ), "1");
	m_TimescaleValue->SetText(buf);

	m_PhysTimescaleValue = new vgui::TextEntry(this, "PhysTimescaleValue");
	char pbuf[64];
  	Q_snprintf(pbuf, sizeof( pbuf ), "1");
	m_PhysTimescaleValue->SetText(pbuf);

	m_Noclip = new CheckButton( this, "Noclip", "Noclip" );
	m_Notarget = new CheckButton( this, "Notarget", "Notarget" );
	m_DarkTheme = new CheckButton( this, "m_DarkTheme", "Dark Theme" );
	
	LoadControlSettings("resource/ui/toolmenu.res");
}

void SMToolMenu::OnTick( void )
{
	if ( !IsVisible() )
		return;	
		
	BaseClass::OnTick();
	ApplyValues();
}

void SMToolMenu::OnTextChanged(Panel *panel)
{
	if ( panel == m_TimescaleValue )
	{
		char str[64];
		float b;
		m_TimescaleValue->GetText( str, 64 );
			
		int i = sscanf(str, "%f", &b);
		if ( ( i == 1 ) && ( b >= 0.0f ) )
		{
			m_Timescale->SetSliderValue( b );
		}	
		return;
	}
	else if ( panel == m_GravityValue )
	{
		char str[64];
		float b;
		m_GravityValue->GetText( str, 64 );
			
		int i = sscanf(str, "%f", &b);
		if ( ( i == 1 ) && ( b >= 0.0f ) )
		{
			m_Gravity->SetSliderValue( b );
		}	
		return;
	}
	else if ( panel == m_PhysTimescaleValue )
	{
		char str[64];
		float b;
		m_PhysTimescaleValue->GetText( str, 64 );
			
		int i = sscanf(str, "%f", &b);
		if ( ( i == 1 ) && ( b >= 0.0f ) )
		{
			m_PhysTimescale->SetSliderValue( b );
		}	
		return;
	}
}

void SMToolMenu::OnControlModified(Panel *panel )
{
	if ( panel == m_Timescale && m_Timescale->HasBeenModified() )
	{
		char buf[64];
  	  	Q_snprintf(buf, sizeof( buf ), " %.2f", m_Timescale->GetSliderValue());
		m_TimescaleValue->SetText(buf);
	}
	else if ( panel == m_Gravity && m_Gravity->HasBeenModified() )
	{
		char buf[64];
  	  	Q_snprintf(buf, sizeof( buf ), " %.2f", m_Gravity->GetSliderValue());
		m_GravityValue->SetText(buf);
	}
	else if ( panel == m_PhysTimescale && m_PhysTimescale->HasBeenModified() )
	{
		char buf[64];
  	  	Q_snprintf(buf, sizeof( buf ), " %.2f", m_PhysTimescale->GetSliderValue());
		m_PhysTimescaleValue->SetText(buf);
	}
}

void SMToolMenu::ApplyValues()
{
	char szColorCommand[64], szDurationCommand[64], szEntCreate[64];
	
	char red[256], green[256], blue[256];
	
	m_Red->GetText( red, sizeof(red));
	m_Green->GetText( green, sizeof(green) );
	m_Blue->GetText( blue, sizeof(blue) );
	
	char duration[256], entname[256];
	
	m_Modelscale->GetText(duration, sizeof(duration));
	m_EntCreate->GetText(entname, sizeof(entname));
	
	if ( m_Red->GetTextLength() && m_Blue->GetTextLength() && m_Green->GetTextLength() )
	{
		Q_snprintf(szColorCommand, sizeof( szColorCommand ), "tool_red %s\n;tool_green %s\n;tool_blue %s\n", red, green, blue );
		engine->ClientCmd(szColorCommand);
	}
	
	if ( m_Modelscale->GetTextLength() )
	{
		Q_snprintf(szDurationCommand, sizeof( szDurationCommand ), "tool_duration %s\n", duration );
		engine->ClientCmd( szDurationCommand );
	}
	
	if ( m_EntCreate->GetTextLength() )
	{
		Q_snprintf(szEntCreate, sizeof( szEntCreate ), "tool_create %s\n", entname );
		engine->ClientCmd( szEntCreate );
	}

	m_Timescale->ApplyChanges();
	m_PhysTimescale->ApplyChanges();
	m_Gravity->ApplyChanges();

	sm_menu_theme.SetValue( m_DarkTheme->IsSelected() );
}

void SMToolMenu::OnCommand( const char *pcCommand )
{
	// idk
	BaseClass::OnCommand( pcCommand );

	if(!Q_stricmp(pcCommand, "remover"))
		engine->ClientCmd("toolmode 0");
	else if(!Q_stricmp(pcCommand, "setcolor"))
		engine->ClientCmd("toolmode 1");
	else if(!Q_stricmp(pcCommand, "modelscale"))
		engine->ClientCmd("toolmode 2");
	else if(!Q_stricmp(pcCommand, "igniter"))
		engine->ClientCmd("toolmode 3");
	else if(!Q_stricmp(pcCommand, "spawner"))
		engine->ClientCmd("toolmode 4");
	else if(!Q_stricmp(pcCommand, "duplicator"))
		engine->ClientCmd("toolmode 5");
	else if(!Q_stricmp(pcCommand, "teleport"))
		engine->ClientCmd("toolmode 6");
	else if(!Q_stricmp(pcCommand, "noclip"))
		engine->ClientCmd( "noclip" );
	else if(!Q_stricmp(pcCommand, "notarget"))
		engine->ClientCmd("notarget");
	else if(!Q_stricmp(pcCommand, "giveall"))
		engine->ClientCmd( "impulse 101" );
	else if(!Q_stricmp(pcCommand, "phys") )
		CheckVar( "physcannon_mega_enabled" );
	else if(!Q_stricmp(pcCommand, "thirdperson"))
	{	
		if ( bCam )
		{
			CAM_ToFirstPerson();
			bCam = false;
		}	
		else 
		{
			CAM_ToThirdPerson();
			bCam = true;
		}
	}
	else if ( !Q_stricmp(pcCommand, "give_tool") )
		engine->ClientCmd("give weapon_toolgun");
	else if( !Q_stricmp(pcCommand, "ai_disable"))
		engine->ClientCmd("ai_disable");
	else if( !Q_stricmp(pcCommand, "god"))
		engine->ClientCmd("god");
	else if( !Q_stricmp(pcCommand, "give_phys"))
		engine->ClientCmd("give weapon_physgun");
	else if( !Q_stricmp(pcCommand, "infinite"))
		CheckVar( "sv_infinite_aux_power" );
	else if (!Q_stricmp(pcCommand, "reload"))
	{
		ReloadScheme( this );
	}
}
// List of Models and Entities
SMList::SMList( vgui::Panel *parent, const char *pName ) : BaseClass( parent, pName )
{
	SetBounds( 0, 0, 800, 690 );
}


void SMList::ExtraSpawn( const char *name, bool bCitizen )
{
	SMFrame *frame = new SMFrame( this, "ExtraSpawn", name, bCitizen );
	frame->Activate();
}

SMFrame::SMFrame( vgui::Panel *parent, const char *panelName, const char *name, bool bCitizen ) : BaseClass( parent, panelName )
{
	vgui::ivgui()->AddTickSignal(GetVPanel(), 30);
	
	bCombine = true;

	SetProportional( true );
	SetSizeable( true );

	int wide = 300;
	int tall = 260;
	int swide, stall;
	surface()->GetScreenSize(swide, stall);
	SetProportionalBounds( this, (swide - wide) / 2, (stall - tall) / 2, wide, tall  );
	//SetSize( wide, tall );
	//SetPos((swide - wide) / 2, (stall - tall) / 2);
	
	if ( bCitizen )
	{
		m_CitizenType = new ComboBox( this, "CitizenType", 4, false );
		m_CitizenType->AddItem( "", NULL );
		m_CitizenType->AddItem( "Citizen", NULL );
		m_CitizenType->AddItem( "Shabby Citizen", NULL );
		m_CitizenType->AddItem( "Rebel", NULL );
		SetProportionalBounds( m_CitizenType, 48, 120, 200, 40 );
		//m_CitizenType->SetBounds( 48, 120, 100, 40 );
		
		m_CitizenLabel = new Label( this, "Name", "CitizenType" );
		SetProportionalBounds(m_CitizenLabel, 48, 100, 100, 16 );
		//m_CitizenLabel->SetBounds( 58, 100, 100, 16 );

		bCombine = false;
	}

	m_WeaponType = new ComboBox( this, "WeaponType", 32, false );
	SetProportionalBounds(m_WeaponType, 48, 52, 200, 40 );
	//m_WeaponType->SetBounds( 48, 52, 100, 40 );
	
	m_WeaponLabel = new Label( this, "Name", "Weapon" );
	SetProportionalBounds( m_WeaponLabel, 48, 34, 100, 16 );
	//m_WeaponLabel->SetBounds( 68, 34, 100, 16 );

	
	m_Spawn = new Button( this, "Button", "Spawn" );
	m_Spawn->SetContentAlignment( Label::a_center );
	SetProportionalBounds(m_Spawn, 48, 184, 200, 40 );
	//m_Spawn->SetBounds( 48, 184, 100, 40 );


	//LoadControlSettings("resource/ui/smframe.res");
	SetSizeable( false );
	SetTitle( "ExtraSpawn", true );
	
	InitWeapons();
}

void SMFrame::SetProportionalBounds( vgui::Panel *panel, int x, int y, int w, int h )
{
	if ( panel->IsProportional() )
		panel->SetBounds( scheme()->GetProportionalScaledValueEx(GetScheme(), x), 
						  scheme()->GetProportionalScaledValueEx(GetScheme(), y), 
						  scheme()->GetProportionalScaledValueEx(GetScheme(), w), 
						  scheme()->GetProportionalScaledValueEx(GetScheme(), h) );
}

void SMFrame::InitWeapons()
{
	KeyValues *kv = new KeyValues( "NPC_WEAPONS" );
	if ( kv )
	{
		if ( kv->LoadFromFile(g_pFullFileSystem, "addons/menu/npc_weapons.json") )
		{
			for ( KeyValues *control = kv->GetFirstSubKey(); control != NULL; control = control->GetNextKey() )
			{	
				if ( !Q_strcasecmp( control->GetName(), "entity" ) )
				{	
					if ( !Q_strncmp( control->GetString(), "weapon_", 7 ) )
					{
						m_WeaponType->AddItem( control->GetString(), NULL );
					}
				}
			}			
		}
		kv->deleteThis();
	}
}

void SMFrame::OnCommand( const char *cmd )
{
	BaseClass::OnCommand( cmd );

	if(!Q_stricmp( cmd , "Close"))
		delete this;
	else
		engine->ClientCmd( cmd );
}

void SMFrame::MakeCommand()
{
	if ( !IsVisible() )
		return;

	char weapon[64];
	char buf[128];

	m_WeaponType->GetText( weapon, 64 );

	if ( bCombine )
		Q_snprintf( buf, sizeof(buf), "ent_create npc_combine_s additionalequipment %s", weapon );
	else
	{
		if (  m_CitizenType->GetActiveItem() != 0 )
			Q_snprintf( buf, sizeof(buf), "ent_create npc_citizen citizentype %i additionalequipment %s", m_CitizenType->GetActiveItem(), weapon );
	}

	m_Spawn->SetCommand( buf );
}

void SMFrame::OnTick()
{
	BaseClass::OnTick();
	
	MakeCommand();
}

void SMList::OnTick( void )
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

void SMList::OnCommand( const char *command )
{
	if(!Q_stricmp(command, "npc_citizen"))
		ExtraSpawn( "Spawn Citizen", true );
	else if ( !Q_stricmp( command, "npc_combine_s" ) )
		ExtraSpawn( "Spawn Combine", false );
	else
		engine->ClientCmd( (char *)command );
}

void SMList::PerformLayout()
{
	BaseClass::PerformLayout();

	int w = 128;
	int h = 128;
	int x = 5;
	int y = 5;

	for ( int i = 0; i < m_LayoutItems.Count(); i++ )
	{	
		vgui::Panel *p = m_LayoutItems[ i ];
		p->SetBounds( x, y, w, h );
		x += ( w + 2 );
		if ( x >= GetWide() - w )
		{
			y += ( h + 2 );
			x = 5;
		}	
	}
}

void SMList::AddImageButton( PanelListPanel *panel, const char *image, const char *hover, const char *command )
{
	ImageButton *btn = new ImageButton( panel, image, image, hover, NULL, command );
	m_LayoutItems.AddToTail( btn );
	panel->AddItem( NULL, btn );
}

// void SMList::AddModelPanel( PanelListPanel *panel, const char *mdlname, const char *cmd )
// {
// 	CMDLPanel *mdl = new CMDLPanel( panel, "MDLPanel" );
// 	mdl->SetMDL( mdlname );
// 	m_LayoutItems.AddToTail( mdl );
// 	panel->AddItem( NULL, mdl );
// 	mdl->Paint();
// 	mdl->LookAtMDL();
// }
	
void SMList::InitEntities( KeyValues *kv, PanelListPanel *panel, const char *enttype )
{	
	for ( KeyValues *control = kv->GetFirstSubKey(); control != NULL; control = control->GetNextKey() )
	{
		const char *entname = control->GetString();
		
		if( Q_strncmp( entname, enttype, Q_strlen(enttype) ) == 0 )
		{
			if ( entname && entname[0] )
			{
				char entspawn[MAX_PATH], img[MAX_PATH], vtf[MAX_PATH], vmt[MAX_PATH];
				
				if ( FStrEq( enttype, "weapon_" ) )
					Q_snprintf( entspawn, sizeof(entspawn), "give %s", entname );
				else
				{
					const char *command = "";
					if ( FStrEq( entname, "npc_monk" ) )
						command = "additionalequipment weapon_annabelle";
					else if ( FStrEq( entname, "npc_alyx") )
						command = "additionalequipment weapon_alyxgun";
					else if ( FStrEq( entname, "npc_metropolice") )
						command = "additionalequipment weapon_stunstick";
					else if ( FStrEq( entname, "npc_fishreman"))
						command = "additionalequipment weapon_oldmanharpoon";

					Q_snprintf( entspawn, sizeof(entspawn), "ent_create %s %s", entname, command );
				}
				
				Q_snprintf( img, sizeof( img ), "smenu/%s", entname );
				Q_snprintf( vtf, sizeof( vtf ), "materials/vgui/smenu/%s.vtf", entname );
				Q_snprintf( vmt, sizeof( vmt ), "materials/vgui/smenu/%s.vmt", entname );

				if ( filesystem->FileExists( vtf ) && filesystem->FileExists( vmt ) )
				{
					if ( FStrEq(entname, "npc_combine_s") || FStrEq( entname, "npc_citizen" ) )
						AddImageButton( panel, img, NULL, entname );
					else
					{
						AddImageButton( panel, img, NULL, entspawn );
					}
					continue;
				}
			}
		}
	}
}

// void SMList::InitModels( PanelListPanel *panel, const char *modeltype, const char *modelfolder, const char *mdlPath )
// {
// 	// Crash for now, temporary remove
// 	if ( Q_strncmp( modelfolder, "props_combine", 14 ) == 0 )
// 		return;

// 	if ( !FStrEq( modelfolder, "props_c17" ) )
// 		return;

// 	VPROF( "SMList::InitModels" );

// 	FileFindHandle_t fh;
// 	char const *pModel = g_pFullFileSystem->FindFirst( mdlPath, &fh );
	
// 	while ( pModel )
// 	{
// 		if ( pModel[0] != '.' )
// 		{
// 			char ext[ 10 ];
// 			Q_ExtractFileExtension( pModel, ext, sizeof( ext ) );
// 			if ( !Q_stricmp( ext, "mdl" ) )
// 			{
// 				char file[MAX_PATH];
// 				Q_FileBase( pModel, file, sizeof( file ) );
			
// 				if ( pModel && pModel[0] )
// 				{
// 					char modelname[MAX_PATH], 
// 						 entspawn[MAX_PATH], 
// 						 modelfile[MAX_PATH];
// 					Q_snprintf( modelname, sizeof(modelname), "%s/%s", modelfolder, file );
// 					Q_snprintf( entspawn, sizeof(entspawn), "%s_create %s", modeltype, modelname );
// 					Q_snprintf( modelfile, sizeof( modelfile ), "models/%s.mdl", modelname );
					
// 					if ( filesystem->FileExists( modelfile ) )
// 					{ 
// 						AddModelPanel( panel, modelfile, entspawn );
// 					}
// 				}
// 			}
// 		}
// 		pModel = g_pFullFileSystem->FindNext( fh );
// 	}
// 	g_pFullFileSystem->FindClose( fh );
// }

// SMenu dialog

CSMenu::CSMenu( vgui::VPANEL *parent, const char *panelName ) : BaseClass( NULL, "SMenu" )
{
	SetTitle( "SMenu", true );
	//SetScheme(vgui::scheme()->LoadSchemeFromFile("resource/SourceScheme.res", "SourceScheme"));

	int screenW, screenH;
	int margin = 30;
    vgui::surface()->GetScreenSize(screenW, screenH);
     // 定义边距
   // int margin = 10;  
    // 计算菜单实际尺寸（两侧留白）
    int menuWidth = screenW - 2 * margin;  // 左右各留 margin
    int menuHeight = screenH - 2 * margin; // 上下各留 margin
    
    // 计算居中位置
    int posX = (screenW - menuWidth) / 2;  // 等价于 margin
    int posY = (screenH - menuHeight) / 2; // 等价于 margin
    
    // 设置位置和大小
    SetPos(posX, posY);
    SetSize(menuWidth, menuHeight);
	
	SMToolMenu *tool = new SMToolMenu(this, "Panel");

	AddPage( tool, "General" );
	
	KeyValues *kv = new KeyValues( "SMenu" );
	if ( kv )
	{
		if ( kv->LoadFromFile(g_pFullFileSystem, "addons/menu/entitylist.json") )
		{
			SMList *npces = new SMList( this, "EntityPanel");
			npces->InitEntities( kv, npces, "npc_" );
			npces->InitEntities( kv, npces, "monster_"); // hl1 npces
			SMList *weapons = new SMList( this, "EntityPanel");
			weapons->InitEntities( kv, weapons, "weapon_" );
			SMList *items = new SMList( this, "EntityPanel");
			items->InitEntities( kv, items, "item_");
			items->InitEntities( kv, items, "ammo_");
			AddPage( npces, "NPCs" );
			AddPage( weapons, "Weapons");
			AddPage( items, "Items");
		}
		kv->deleteThis();
	}
	
	SMModels *mdl = new SMModels( this, "PropMenu" );
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
				mdl->InitModels( mdl, "prop_physics", file, dir );
			}
		}
	}

	AddPage( mdl, "Props" );

//	SMMovement *mv = new SMMovement( this, "Movement Settings");
//  AddPage( mv, "Movement" );

	SMVisual *visual = new SMVisual( this, "Visual Settings");
	AddPage( visual, "Visual" );

	SMWeapons *wp = new SMWeapons( this, "Weapons Settings" );
	AddPage(wp, "Weapons Settings");

	SetCancelButtonVisible( false );
	
	vgui::ivgui()->AddTickSignal(GetVPanel(), 100);
	GetPropertySheet()->SetTabWidth(72);
	SetMoveable( false );
	SetVisible( true );
	SetSizeable( false );

}

void CSMenu::OnTick()
{
	BaseClass::OnTick();
	SetVisible(sm_menu.GetBool());	
	if (sm_menu.GetBool()) {  
        engine->ClientCmd( "touch_hide 1" );
    	engine->ClientCmd( "touch_enable 0" );
	}else{
     	engine->ClientCmd( "touch_hide 0" );
		engine->ClientCmd( "touch_enable 1" );
	}
}

void CSMenu::OnCommand( const char *command )
{
	BaseClass::OnCommand( command );
		
	if (!Q_stricmp(command, "Close"))	
	{
		sm_menu.SetValue(0);
		engine->ClientCmd( "touch_hide 0" );
		engine->ClientCmd( "touch_enable 1" );
	//	engine->ClientCmd( "exec touch.cfg" );
	}
}

SMWeapons::SMWeapons( vgui::Panel *parent, const char *panelName ) : BaseClass( parent, panelName )
{
	vgui::ivgui()->AddTickSignal( GetVPanel(), 250 );
	m_AllowBetaWeapons = new CheckButton( this, "ABW", "Allow Beta Weapons");
	m_AllowHLSWeapons = new CheckButton( this, "AHL1W", "Allow Half-Life: Source Weapons");
	m_AllowStunstick = new CheckButton( this, "StunStick", "Allow StunStick as Weapon");

	m_CheckVar = new TextEntry(this, "CheckVar");
	m_ComboBox = new ComboBox( this, "ComboBox", 50, false);

	CheckWeapons();

	LoadControlSettings("resource/ui/smweapons.res");
}

void SMWeapons::OnCommand( const char *cmd )
{
	BaseClass::OnCommand(cmd);
}

void SMWeapons::CheckWeapons()
{
	KeyValues *kv = new KeyValues( "Skill Issue" );
	if ( kv )
	{
		if ( kv->LoadFromFile(g_pFullFileSystem, "addons/menu/skill.json") )
		{
			for ( const KeyValues *control = kv->GetFirstSubKey(); control != NULL; control = control->GetNextKey() )
			{
				const char * weaponName, * cvarName;
				cvarName = control->GetName();
				m_ComboBox->AddItem( cvarName, control );
				continue;
			}
		}
		kv->deleteThis();
	}
}

void SMWeapons::OnTick( void )
{
	BaseClass::OnTick();
	
	if ( !IsVisible() )
		return;	
	
	const char * data; 
	char buf[64];
	m_CheckVar->GetText( buf, 64 );
	data = m_ComboBox->GetActiveItemUserData()->GetString();
	ConVarRef convar( data );
	convar.SetValue( buf );
/*		
	ConVarRef hls_weapons_allowed("hls_weapons_allowed" );
	hls_weapons_allowed.SetValue( m_AllowHLSWeapons->IsSelected() );
		
	ConVarRef beta_weapons_allowed("beta_weapons_allowed" );
	beta_weapons_allowed.SetValue( m_AllowBetaWeapons->IsSelected() );

	ConVarRef allow_stunstick_as_weapon("allow_stunstick_as_weapon");
	allow_stunstick_as_weapon.SetValue( m_AllowStunstick->IsSelected() );*/
	
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
